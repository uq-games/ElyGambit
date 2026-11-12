#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceDistortionPass.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Character/EGChessUnit.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Curves/CurveFloat.h"
#include "EGNativeRendererLibrary.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"

bool FEGSourceEffectTrack::IsSourceVisible(float Time) const
{
 return !VisibilityCurve || VisibilityCurve->GetFloatValue(Time)>.5f;
}
bool FEGSourceEffectTrack::ApplyNativeRendering(UStaticMeshComponent* Component,UObject* LifetimeOwner,float Time) const
{
 if(NativePassDefinition.IsNull())return true;
 // Distortion buffers and depth-only carriers have different consumers. A
 // source definition must contain all its own passes rather than combining them.
 if(bSourceDistortion||bSourceDepthOnly)return false;
 auto* Native=UEGNativeRendererLibrary::ApplySourceDefinition(Component,NativePassDefinition.LoadSynchronous(),LifetimeOwner);
 if(!Native)return false;
 for(const auto& Curve:MaterialCurves)if(Curve.Value)Native->SetNativeScalarParameter(Curve.Key,Curve.Value->GetFloatValue(Time));
 Native->SetNativeScalarParameter(TEXT("SourceTimeline"),Time);
 return true;
}

FQuat EGSourceBillboardRotation(const FQuat& SampleRotation,const FVector& FacingNormal,const FVector& CameraRight)
{
 // prepare_source_effects.py: C = [[-1,0,0],[0,0,1],[0,1,0]].
 const FQuat SourceBasis(0.,UE_SQRT_2*.5,UE_SQRT_2*.5,0.);
 const FQuat SourceRotation=SourceBasis.Inverse()*SampleRotation*SourceBasis;
 return (FRotationMatrix::MakeFromZX(FacingNormal,CameraRight).ToQuat()*SourceRotation).GetNormalized();
}

AEGSourceEffect::AEGSourceEffect()
{
 PrimaryActorTick.bCanEverTick=true;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 SetActorEnableCollision(false);
}
AEGSourceEffect* AEGSourceEffect::Spawn(UEGHeroSkillComponent* Skill,USkeletalMeshComponent* Mesh,UEGSourceEffectDefinition* Definition,float Rate,bool Independent)
{
 return SpawnInternal(Skill,Mesh,Definition,Rate,Independent,nullptr);
}
AEGSourceEffect* AEGSourceEffect::SpawnAt(UEGHeroSkillComponent* Skill,USkeletalMeshComponent* Mesh,UEGSourceEffectDefinition* Definition,float Rate,const FVector& Point,const FRotator& Rotation,float ScaleMultiplier)
{
 if(!IsValid(Definition)||!Definition->bTargetGround||!Definition->AnchorBone.IsNone()||Point.ContainsNaN()||Rotation.ContainsNaN()
    ||!FMath::IsFinite(ScaleMultiplier)||ScaleMultiplier<=0.f||!FMath::IsFinite(Definition->PresentationScale)||Definition->PresentationScale<=0.f)return nullptr;
 const float Scale=Definition->PresentationScale*ScaleMultiplier;
 if(!FMath::IsFinite(Scale)||Scale<=0.f)return nullptr;
 const FTransform Fixed(Rotation,Point,FVector(Scale));
 return SpawnInternal(Skill,Mesh,Definition,Rate,false,&Fixed);
}
AEGSourceEffect* AEGSourceEffect::SpawnInternal(UEGHeroSkillComponent* Skill,USkeletalMeshComponent* Mesh,UEGSourceEffectDefinition* Definition,float Rate,bool Independent,const FTransform* FixedTransform)
{
 if(!IsValid(Skill)||!IsValid(Mesh)||!IsValid(Definition)||Definition->Tracks.IsEmpty()
    ||!Mesh->GetWorld()||Mesh->GetWorld()->GetNetMode()==NM_DedicatedServer||!FMath::IsFinite(Rate)||Rate<=0
    ||!FMath::IsFinite(Definition->PresentationScale)||Definition->PresentationScale<=0||Definition->SourceAnchor.ContainsNaN()||!FMath::IsFinite(Definition->TargetHeightFraction)
    ||!Skill->Battle.IsValid()||!Skill->Battle->IsBattleActive())return nullptr;
 auto* Effect=Mesh->GetWorld()->SpawnActor<AEGSourceEffect>();if(!Effect)return nullptr;
 Effect->Data=Definition;Effect->ActionOwner=Skill;Effect->ActionToken=Skill->ActionId;Effect->PlayRate=Rate;
 Effect->BattleRound=Skill->Battle->GetRoundId();Effect->bRoundBound=Independent;Effect->AnchorMesh=Mesh;
 Effect->AddTickPrerequisiteComponent(Mesh);
 Effect->SetOwner(Skill->GetOwner());
 // Source geometry and sample positions are already converted from metres to cm.
 // These are independently spawned effects, not skeletal geometry: a board-fit
 // mesh scale must not enlarge their world-space footprint a second time.
 Effect->GetRootComponent()->SetAbsolute(false,false,true);
 if(FixedTransform)
  Effect->SetActorTransform(*FixedTransform);
 else if(Definition->bTargetGround)
  Effect->SetActorTransform(FTransform(Mesh->GetComponentQuat(),Skill->GetPresentationTargetGround(Definition->TargetHeightFraction),FVector(Definition->PresentationScale)));
 else
 {
  Effect->AttachToComponent(Mesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale);
  Effect->SetActorScale3D(FVector(Definition->PresentationScale));
 }
 for(const auto& Track:Definition->Tracks)
 {
  if(!FMath::IsFinite(Track.StartTime)||!FMath::IsFinite(Track.Duration)||Track.StartTime<0||Track.Duration<=0
      ||Track.Samples.IsEmpty()||Track.Samples.ContainsByPredicate([](const auto& S){return S.Transform.ContainsNaN()||!FMath::IsFinite(S.Color.R)||!FMath::IsFinite(S.Color.G)||!FMath::IsFinite(S.Color.B)||!FMath::IsFinite(S.Color.A);}))
  {Effect->Destroy();return nullptr;}
  auto* Geometry=Track.Mesh.LoadSynchronous();auto* Material=Track.Material.LoadSynchronous();
  if(!Geometry||!Material){Effect->Destroy();return nullptr;}
  auto* Part=NewObject<UStaticMeshComponent>(Effect);Effect->AddInstanceComponent(Part);Part->SetupAttachment(Effect->GetRootComponent());
  Part->SetStaticMesh(Geometry);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCastShadow(false);
  Part->SetRenderInMainPass(!Track.bSourceDepthOnly);Part->SetRenderInDepthPass(true);
  Part->SetMaterial(0,Material);Part->SetVisibility(false);Part->RegisterComponent();
  Effect->Parts.Add(Part);Effect->Materials.Add(Part->CreateDynamicMaterialInstance(0));
  if(Track.bSourceDistortion)
  {
   Part->SetVisibleInSceneCaptureOnly(true);
   auto* Composite=Definition->SourceDistortionComposite.LoadSynchronous();
   if(!AEGSourceDistortionPass::RegisterSource(Part,Composite))
   {
    Effect->RejectedDistortionTracks.Add(Effect->Parts.Num()-1);
    UE_LOG(LogTemp,Warning,TEXT("SourceFX distortion track %d of %s suppressed: composite %s could not be registered."),
      Effect->Parts.Num()-1,*GetPathNameSafe(Definition),*Definition->SourceDistortionComposite.ToSoftObjectPath().ToString());
   }
  }
  Effect->Duration=FMath::Max(Effect->Duration,Track.StartTime+Track.Duration);
 }
 // Action effects use their own elapsed time and action identity rather than
 // an Actor lifespan based on world time.
 // Independent round-bound effects retain their original world-time lifespan.
 if(Independent)Effect->SetLifeSpan(Effect->Duration/Rate+.1f);
 Effect->Tick(0);return Effect;
}
void AEGSourceEffect::SetPlaybackRate(float Rate)
{
 if(!FMath::IsFinite(Rate)||Rate<=0||bRoundBound)return;
 PlayRate=Rate;
}
void AEGSourceEffect::Tick(float DeltaSeconds)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EG_SourceEffectTick);
 Super::Tick(DeltaSeconds);
 auto* Skill=ActionOwner.Get();auto* Unit=Skill?Cast<AEGChessUnit>(Skill->GetOwner()):nullptr;
 if(!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit)||!Skill->Battle.IsValid()||!Data
    ||(Skill->Battle->GetRoundId().IsValid()&&Skill->Battle->GetRoundId()!=BattleRound)
    ||(!bRoundBound&&(!Skill->Battle->IsBattleActive()||Skill->ActionId!=ActionToken))){Destroy();return;}
 if(!Data->bTargetGround&&!Data->AnchorBone.IsNone()&&AnchorMesh.IsValid()&&AnchorMesh->DoesSocketExist(Data->AnchorBone))
 {
  const FQuat Basis=AnchorMesh->GetComponentQuat();
  SetActorLocationAndRotation(AnchorMesh->GetSocketLocation(Data->AnchorBone)-Basis.RotateVector(Data->SourceAnchor*Data->PresentationScale),Basis);
 }
 Age+=DeltaSeconds*PlayRate;
 if(Age>=Duration){Destroy();return;}
 for(int32 I=0;I<Parts.Num();++I)
 {
  if(RejectedDistortionTracks.Contains(I)){Parts[I]->SetVisibility(false);continue;}
  const auto& Track=Data->Tracks[I];const float Time=Age-Track.StartTime;
  const bool Visible=Time>=0&&Time<Track.Duration&&Track.IsSourceVisible(Time);Parts[I]->SetVisibility(Visible);if(!Visible)continue;
  if(!Track.ApplyNativeRendering(Parts[I],this,Time)){Parts[I]->SetVisibility(false);continue;}
  const float Frame=FMath::Clamp(Time/Track.Duration,0.f,1.f)*(Track.Samples.Num()-1);
  const int32 Low=FMath::FloorToInt(Frame),High=FMath::Min(Low+1,Track.Samples.Num()-1);const float Alpha=Frame-Low;
  FTransform Transform;Transform.Blend(Track.Samples[Low].Transform,Track.Samples[High].Transform,Alpha);
  Parts[I]->SetRelativeTransform(Transform);
  if(Track.bCameraFacing)if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
   Parts[I]->SetWorldRotation(Track.SourceBillboardAlignment==INDEX_NONE
    ? FRotationMatrix::MakeFromZ(Camera->GetCameraLocation()-Parts[I]->GetComponentLocation()).ToQuat()
    : EGSourceBillboardRotation(Transform.GetRotation(),Track.SourceBillboardAlignment==0
     ? -Camera->GetCameraRotation().Vector():Camera->GetCameraLocation()-Parts[I]->GetComponentLocation(),Camera->GetCameraRotation().Quaternion().GetRightVector()));
  if(Materials[I])
  {
   FLinearColor Color=FMath::Lerp(Track.Samples[Low].Color,Track.Samples[High].Color,Alpha);
   // Native custom-data dissolve is outside this converted subset. Give anchored
   // weapon trails a release instead of cutting a nearly opaque mesh away.
   if(!bRoundBound&&!Data->AnchorBone.IsNone())
    Color.A*=FMath::SmoothStep(0.f,FMath::Min(.08f,Track.Duration*.3f),Track.Duration-Time);
   Materials[I]->SetVectorParameterValue(TEXT("SourceColor"),Color);
   Materials[I]->SetVectorParameterValue(TEXT("SourceParticleUV"),Track.Samples[Low].TextureUV);
   Materials[I]->SetVectorParameterValue(TEXT("SourceCustom1"),FMath::Lerp(Track.Samples[Low].CustomData1,Track.Samples[High].CustomData1,Alpha));
   Materials[I]->SetVectorParameterValue(TEXT("SourceCustom2"),FMath::Lerp(Track.Samples[Low].CustomData2,Track.Samples[High].CustomData2,Alpha));
   Materials[I]->SetScalarParameterValue(TEXT("SourceAge"),Time);
   Materials[I]->SetScalarParameterValue(TEXT("SourceTimeline"),Age);
  }
 }
}
void UEGSourceEffectNotify::Notify(USkeletalMeshComponent* Mesh,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& Reference)
{
 Super::Notify(Mesh,Animation,Reference);
 if(!IsValid(Mesh)||!Mesh->GetWorld()||!Mesh->GetWorld()->IsGameWorld())return;
 auto* Owner=Mesh->GetOwner();auto* Unit=Cast<AEGChessUnit>(Owner);
 if(!Unit&&Owner)Unit=Cast<AEGChessUnit>(Owner->GetAttachParentActor());
 auto* Instance=Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance());
 const auto* Context=Reference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
 if(!Unit||!Instance||!Context||!Unit->GetHeroSkill()
    ||!Instance->AcceptsNotify(Animation,Context->MontageInstanceID,Unit->GetHeroSkill()->ActionId))return;
 Unit->GetHeroSkill()->PresentSourceEffect(Effect.LoadSynchronous(),Mesh);
}
