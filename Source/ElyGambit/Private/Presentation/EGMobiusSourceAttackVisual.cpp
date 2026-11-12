#include "Presentation/EGMobiusSourceAttackVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"

AEGMobiusSourceAttackVisual::AEGMobiusSourceAttackVisual()
{PrimaryActorTick.bCanEverTick=true;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));SetActorEnableCollision(false);bReplicates=false;}
AEGMobiusSourceAttackVisual* AEGMobiusSourceAttackVisual::Create(UEGHeroSkillComponent* InSkill,AEGChessUnit* InTarget)
{
 if(!IsValid(InSkill)||!IsValid(InTarget))return nullptr;
 auto* U=Cast<AEGChessUnit>(InSkill->GetOwner());
 if(!U||U->GetNetMode()==NM_DedicatedServer||!InSkill->Battle.IsValid()||!InSkill->Battle->IsBattleActive())return nullptr;
 FActorSpawnParameters Params;Params.Owner=U;
 auto* V=U->GetWorld()->SpawnActor<AEGMobiusSourceAttackVisual>(Params);if(!V)return nullptr;
 V->Skill=InSkill;V->Target=InTarget;V->ActionId=InSkill->ActionId;V->RoundId=InSkill->Battle->GetRoundId();
 V->StartPoint=U->GetActorLocation()-FVector(0,0,88);
 V->EndPoint=InTarget->GetActorLocation()-FVector(0,0,88);
 V->SetActorLocation(V->EndPoint);V->SetActorRotation((V->EndPoint-V->StartPoint).GetSafeNormal2D().Rotation());
 V->AddTickPrerequisiteComponent(U->GetVisiblePresentationMesh());V->AddTickPrerequisiteComponent(InSkill);
 return V;
}
AEGCombatVisual* AEGMobiusSourceAttackVisual::CreateSourceVisual(UEGSourcePreviewDefinition* Definition,float Scale)
{
 if(!Definition||Definition->Motions.IsEmpty()||Definition->Motions[0].Effects.IsEmpty())return nullptr;
 auto* V=GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!V)return nullptr;
 V->Definition=Definition;V->bAutoPlay=false;V->bLoop=false;V->FinishSpawning(FTransform::Identity);V->SetActorTickEnabled(false);
 V->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
 V->SetActorRelativeTransform(FTransform(FRotator(0,-90,0),FVector::ZeroVector,FVector(Scale)));
 V->SetActorHiddenInGame(true);return V;
}
AEGMobiusSourceAttackVisual* AEGMobiusSourceAttackVisual::SpawnBasic(UEGHeroSkillComponent* Skill,AEGChessUnit* Target,const FEGMobiusBasicSourceStage& Stage,int32 StageIndex,const TArray<UEGSourceEffectDefinition*>* WorkshopOverrides)
{
 if(Stage.Cues.IsEmpty())return nullptr;
 for(const auto& C:Stage.Cues)
  if(C.Assembly.IsNull()||!FMath::IsFinite(C.StartTime)||C.StartTime<0||!FMath::IsFinite(C.Duration)||C.Duration<0
     ||!FMath::IsFinite(C.PlayRate)||C.PlayRate<=0||!FMath::IsFinite(C.Scale)||C.Scale<=0)return nullptr;
 auto* V=Create(Skill,Target);if(!V)return nullptr;
 V->BasicStage=Stage;V->BasicStageIndex=StageIndex;
 for(int32 Index=0;Index<Stage.Cues.Num();++Index)
 {
  const auto& Cue=Stage.Cues[Index];
  // Background presentation loading is optional. Resolve a needed cue here so
  // an immediate first attack cannot silently lose its entire source effect.
  auto* Definition=Cue.Assembly.LoadSynchronous();
  auto* Replacement=WorkshopOverrides&&WorkshopOverrides->IsValidIndex(Index)?(*WorkshopOverrides)[Index]:nullptr;
  bool bWorkshop=false;
  // Only replace the known single-effect target-front source at its original
  // montage rate. The private copy has no character or preview mannequin.
  if(IsValid(Replacement)&&!Replacement->Tracks.IsEmpty()
     &&!Replacement->Tracks.ContainsByPredicate([](const auto& Track){return Track.Samples.IsEmpty()||!Track.Mesh.LoadSynchronous()||!Track.Material.LoadSynchronous();})
     &&IsValid(Definition)&&Definition->Motions.Num()==1
     &&Definition->Motions[0].Parts.IsEmpty()&&Definition->Motions[0].Effects.Num()==1
     &&Definition->Motions[0].Effects[0].AnchorPartId.IsNone()&&Definition->Motions[0].Effects[0].AnchorBone.IsNone()
     &&Cue.Anchor==EEGMobiusSourceAnchor::TargetFront&&FMath::IsNearlyEqual(Cue.PlayRate,1.f))
  {
   if(auto* Private=DuplicateObject<UEGSourcePreviewDefinition>(Definition,V))
   {
    Private->Motions[0].Effects[0].Effect=Replacement;
    V->PrivateCueDefinitions.Add(Private);Definition=Private;bWorkshop=true;
   }
  }
  auto* Source=V->CreateSourceVisual(Definition,bWorkshop?Cue.Scale/3.f:Cue.Scale);
  if(!Source&&bWorkshop){bWorkshop=false;Source=V->CreateSourceVisual(Cue.Assembly.LoadSynchronous(),Cue.Scale);}
  if(!Source){V->Destroy();return nullptr;}
  if(!V->SourceDefinition)V->SourceDefinition=Source->Definition;
  V->CueVisuals.Add(Source);
  // Snapshot enemy ground/body once. Neither target movement nor turn-in-place
  // can drag an already formed spike across the board. Only a muzzle follows its hand.
  if(Cue.Anchor==EEGMobiusSourceAnchor::TargetBody||Cue.Anchor==EEGMobiusSourceAnchor::TargetFront)
   Source->AddActorWorldOffset(FVector(0,0,bWorkshop?100.f:Target->GetDefinition()->GetPresentationHeight()*.45f));
  if(Cue.Anchor==EEGMobiusSourceAnchor::TargetFront)
   Source->AddActorWorldOffset(-V->GetActorForwardVector()*Target->GetDefinition()->GetPresentationPickRadius());
 }
 return V;
}
AEGMobiusSourceAttackVisual* AEGMobiusSourceAttackVisual::SpawnImpact(UEGHeroSkillComponent* Skill,AEGChessUnit* Target,UEGSourcePreviewDefinition* Definition,float Scale,float PlaybackRate,bool bGround)
{
 if(!FMath::IsFinite(Scale)||Scale<=0||!FMath::IsFinite(PlaybackRate)||PlaybackRate<=0)return nullptr;
 auto* V=Create(Skill,Target);if(!V)return nullptr;
 V->SourceDefinition=Definition;V->SourceVisual=V->CreateSourceVisual(Definition,Scale);
 if(!V->SourceVisual){V->Destroy();return nullptr;}
 V->bImpact=true;V->ImpactRate=FMath::Clamp(PlaybackRate,.1f,8.f);
 V->EndPoint+=FVector(0,0,bGround?0.f:Target->GetDefinition()->GetPresentationHeight()*.45f);
 V->SetActorLocation(V->EndPoint);V->SetActorRotation(Skill->GetOwner()->GetActorRotation());V->SourceVisual->SetActorHiddenInGame(false);return V;
}
void AEGMobiusSourceAttackVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 auto* S=Skill.Get();auto* U=S?Cast<AEGChessUnit>(S->GetOwner()):nullptr;auto* T=Target.Get();
 if(!S||!U||!S->Battle.IsValid()||!S->Battle->IsBattleActive()||S->Battle->GetRoundId()!=RoundId
    ||(!bImpact&&!bNaturalTail&&(S->ActionId!=ActionId||(!bHitConfirmed&&(!T||!IEGCombatant::Execute_IsAlive(T)))))
    ||!IEGCombatant::Execute_IsAlive(U)){Destroy();return;}
 if(bImpact)
 {
  ImpactAge+=DeltaSeconds*ImpactRate;SourceSampleTime=ImpactAge;
  if(ImpactAge>=SourceDefinition->Motions[0].Duration){Destroy();return;}
  SourceVisual->SetSourceTime(ImpactAge);
  if(StormBeam){StormBeam->SetVisibility(ImpactAge<.4f);if(StormBeamMaterial)StormBeamMaterial->SetVectorParameterValue(TEXT("SourceColor"),FLinearColor(.35f,1.f,.15f,FMath::Clamp(1.f-ImpactAge/.4f,0.f,1.f)));}return;
 }
 if(bNaturalTail)SourceClock+=DeltaSeconds*SourceClockRate;
 else
 {
  auto* Mesh=U->GetVisiblePresentationMesh();auto* I=Mesh?Mesh->GetAnimInstance():nullptr;auto* M=S->GetActiveMontage();
  if(!I||!M){Destroy();return;}
  SourceClock=I->Montage_GetPosition(M);SourceClockRate=FMath::Max(.01f,I->Montage_GetPlayRate(M));
 }
 const float Position=SourceClock;ActiveCueCount=0;float FinalSourcePosition=0.f;
 for(int32 Index=0;Index<BasicStage.Cues.Num();++Index)
 {
  const auto& Cue=BasicStage.Cues[Index];auto* Visual=CueVisuals[Index].Get();
  const float LocalTime=(Position-Cue.StartTime)*Cue.PlayRate;
  const float Length=Cue.Duration>0?FMath::Min(Cue.Duration,Visual->Definition->Motions[0].Duration):Visual->Definition->Motions[0].Duration;
  FinalSourcePosition=FMath::Max(FinalSourcePosition,Cue.StartTime+Length/Cue.PlayRate);
  const bool bVisible=LocalTime>=0&&LocalTime<Length&&(!Cue.bRequiresHit||bHitConfirmed);
  Visual->SetActorHiddenInGame(!bVisible);if(!bVisible)continue;
  if(Cue.Anchor==EEGMobiusSourceAnchor::CasterHand)
  {
   FVector Hand;if(!S->GetAssemblyBoneLocation(TEXT("Body"),Cue.Bone,Hand))Hand=U->GetActorLocation();
   Visual->SetActorLocation(Hand);
  }
  Visual->SetSourceTime(LocalTime);++ActiveCueCount;
  if(Index==0)SourceSampleTime=LocalTime;
 }
 if(bNaturalTail&&Position>=FinalSourcePosition)Destroy();
}
bool AEGMobiusSourceAttackVisual::ReleaseNaturalTail()
{
 if(bImpact||bNaturalTail||!bHitConfirmed||CueVisuals.IsEmpty()||!Skill.IsValid())return false;
 bNaturalTail=true;return true;
}
void AEGMobiusSourceAttackVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 if(IsValid(SourceVisual))SourceVisual->Destroy();SourceVisual=nullptr;
 for(const auto& Visual:CueVisuals)if(IsValid(Visual))Visual->Destroy();CueVisuals.Reset();
 Super::EndPlay(Reason);
}
void AEGMobiusSourceAttackVisual::AddStormBeam(UMaterialInterface* Material,const FVector& Origin)
{
 if(!bImpact||!Material||Origin.ContainsNaN()||EndPoint.ContainsNaN())return;
 const FVector Delta=EndPoint-Origin;if(Delta.SizeSquared()<1.f)return;StartPoint=Origin;
 StormBeam=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(StormBeam);StormBeam->SetupAttachment(GetRootComponent());
 StormBeam->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane")));StormBeam->SetMaterial(0,Material);StormBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);StormBeam->SetGenerateOverlapEvents(false);StormBeam->SetCanEverAffectNavigation(false);StormBeam->SetCastShadow(false);StormBeam->RegisterComponent();
 StormBeam->SetWorldLocation((Origin+EndPoint)*.5f);StormBeam->SetWorldRotation(FRotationMatrix::MakeFromXY(Delta.GetSafeNormal(),FVector::CrossProduct(FVector::UpVector,Delta).GetSafeNormal()).Rotator());StormBeam->SetWorldScale3D(FVector(Delta.Size()/100.f,.55f,1.f));StormBeamMaterial=StormBeam->CreateDynamicMaterialInstance(0);
 if(StormBeamMaterial)StormBeamMaterial->SetVectorParameterValue(TEXT("SourceColor"),FLinearColor(.35f,1.f,.15f,1.f));
}
