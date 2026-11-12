#include "Presentation/EGFiveHeroPresentation.h"

#include "Camera/PlayerCameraManager.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGChessNiagaraBurst.h"

namespace
{
 FTransform SakuraCueTransform(const AEGChessUnit* Unit, const AEGChessUnit* Target,
  const FVector& Point, const FVector& Offset, const FRotator& Rotation, float Scale, bool bFaceCamera)
 {
  FRotator Base=Unit->GetActorRotation();
  FVector Anchor=Point;
  if(bFaceCamera)
  {
   if(const auto* Camera=UGameplayStatics::GetPlayerCameraManager(Unit,0))
   {
    const FVector CameraLocation=Camera->GetCameraLocation();
    if(const auto* Mesh=IsValid(Target)?Target->GetVisiblePresentationMesh():nullptr)
    {
     const FBox Box=Mesh->Bounds.GetBox();
     if(Mesh->IsRegistered() && Box.IsValid && !Mesh->Bounds.Origin.ContainsNaN()
      && !Mesh->Bounds.BoxExtent.ContainsNaN() && Mesh->Bounds.BoxExtent.GetMin()>0.f)
     {
      if(!Box.IsInsideOrOn(Anchor))Anchor=Mesh->Bounds.Origin;
      const FVector ToCamera=CameraLocation-Anchor;
      const double Distance=ToCamera.Size();
      if(FMath::IsFinite(Distance) && Distance>1.0)
      {
       const FVector Direction=ToCamera/Distance;
       double Exit=MAX_dbl;
       for(int32 Axis=0;Axis<3;++Axis)
       {
        const double D=Direction[Axis];
        if(FMath::Abs(D)<UE_DOUBLE_SMALL_NUMBER)continue;
        Exit=FMath::Min(Exit,((D>0?Box.Max[Axis]:Box.Min[Axis])-Anchor[Axis])/D);
       }
       if(FMath::IsFinite(Exit) && Exit>=0.0 && Exit!=MAX_dbl)
        Anchor+=Direction*FMath::Max(0.0,FMath::Min3(Exit+3.0,180.0,Distance-1.0));
      }
     }
    }
    const FVector Direction=CameraLocation-Anchor;
    // The authored Stroke mesh lies in local XY, hence local Z is its normal.
    if(!Direction.IsNearlyZero())Base=FRotationMatrix::MakeFromZ(Direction).Rotator();
   }
  }
  const FQuat Orientation=Base.Quaternion()*Rotation.Quaternion();
  return FTransform(Orientation,Anchor+Orientation.RotateVector(Offset),FVector(Scale));
 }
}

AEGFiveHeroPresentation::AEGFiveHeroPresentation()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickGroup=TG_PostUpdateWork;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 SetActorEnableCollision(false);
 bReplicates=false;
}

bool AEGFiveHeroPresentation::Emit(EEGFiveHeroCue Cue, AEGChessUnit* Caster,
 AEGChessUnit* Target, const FVector& Point, const FGuid& RoundId,
 const FGuid& ActionId, int32 Value)
{
 (void)Value;
 // Other heroes' cues are reserved until their own visual designs are approved.
 if(Cue>EEGFiveHeroCue::SakuraFanCancel || !IsValid(Caster) || Point.ContainsNaN())return false;
 auto* World=Caster->GetWorld();
 auto* SkillComponent=Caster->GetHeroSkill();
 auto* Context=SkillComponent?SkillComponent->Battle.Get():nullptr;
 if(!World || World->GetNetMode()==NM_DedicatedServer || !IsValid(Context)
  || !Context->IsBattleActive() || !RoundId.IsValid() || Context->GetRoundId()!=RoundId
  || !IEGCombatant::Execute_IsAlive(Caster))return false;
 if(Cue!=EEGFiveHeroCue::SakuraEvade && !ActionId.IsValid())return false;

 AEGFiveHeroPresentation* Receiver=nullptr;
 for(TActorIterator<AEGFiveHeroPresentation> It(World);It;++It)
  if(It->Unit.Get()==Caster && It->Round==RoundId){Receiver=*It;break;}
 if(!Receiver)
 {
  if(Cue==EEGFiveHeroCue::SakuraFanCancel)return false;
  const auto* Hero=Caster->GetDefinition();
  const auto* Definition=Hero?Cast<UEGSakuraSkillDefinition>(Hero->SkillDefinition):nullptr;
  if(!Definition || Definition->ReworkPresentation.IsNull())return false;
  // Background residency normally resolves this graph. Preserve the first
  // presentation if it is still loading; this fallback can hitch on cold IO.
  if(!Definition->ReworkPresentation.IsValid())
   UE_LOG(LogTemp,Display,TEXT("Sakura presentation config required a synchronous fallback for %s"),*GetNameSafe(Caster));
  auto* Presentation=Definition->ReworkPresentation.LoadSynchronous();
  if(!IsValid(Presentation))
  {
   UE_LOG(LogTemp,Warning,TEXT("Sakura rework presentation config failed to load for %s"),*GetNameSafe(Caster));
   return false;
  }
  FActorSpawnParameters Params;
  Params.Owner=Caster;
  Params.ObjectFlags|=RF_Transient;
  Receiver=World->SpawnActor<AEGFiveHeroPresentation>(Params);
  if(!Receiver)return false;
  Receiver->Unit=Caster;Receiver->Skill=SkillComponent;Receiver->Battle=Context;
  Receiver->Round=RoundId;Receiver->Config=Presentation;
 }
 return Receiver->Receive(Cue,Target,Point,ActionId);
}

bool AEGFiveHeroPresentation::Receive(EEGFiveHeroCue Cue, AEGChessUnit* Target,
 const FVector& Point, const FGuid& ActionId)
{
 if(!IsValid(Config))return false;
 switch(Cue)
 {
 case EEGFiveHeroCue::SakuraEvade:
  BeginDodge(Target);return Play(Cue,Config->Evade,nullptr,Point,ActionId,false) || IsDodgeActive();
 case EEGFiveHeroCue::SakuraDelayedMark:
  return IsValid(Target) && Play(Cue,Config->DelayedMark,Target,Point,ActionId,true);
 case EEGFiveHeroCue::SakuraFanMark:
  return IsValid(Target) && Play(Cue,Config->FanMark,Target,Point,ActionId,true);
 case EEGFiveHeroCue::SakuraDelayedRelease:
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("EnhancedHit"),Point,Unit->GetActorRotation());
  RemoveMarks(ActionId,Target);return Play(Cue,Config->DelayedRelease,Target,Point,ActionId,false);
 case EEGFiveHeroCue::SakuraFanRelease:
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("EnhancedFinalHit"),Point,Unit->GetActorRotation());
  RemoveMarks(ActionId,Target);return Play(Cue,Config->FanRelease,Target,Point,ActionId,false);
 case EEGFiveHeroCue::SakuraFanCancel:
  RemoveMarks(ActionId,Target);if(!Target)ClearDodge();return true;
 default:return false;
 }
}

bool AEGFiveHeroPresentation::Play(EEGFiveHeroCue CueId, const FEGSakuraReworkCue& Cue, AEGChessUnit* Target,
 const FVector& Point, const FGuid& ActionId, bool bMark)
{
 if(!Unit.IsValid() || !Skill.IsValid() || !FMath::IsFinite(Cue.Scale) || Cue.Scale<=0.f
  || !FMath::IsFinite(Cue.LifetimeSeconds) || Cue.LifetimeSeconds<=0.f
  || Cue.LocalOffset.ContainsNaN() || Cue.LocalRotation.ContainsNaN())return false;
 const bool bSource=!Cue.SourceEffect.IsNull();
 const bool bNiagara=!Cue.NiagaraSystem.IsNull();
 if(!bSource && !bNiagara)return false;
 if(bSource && bNiagara)
 {
  UE_LOG(LogTemp,Warning,TEXT("Sakura rework cue requires exactly one SourceEffect or NiagaraSystem"));
  return false;
 }

 FEGSakuraReworkActiveCue Active;
 Active.Cue=CueId;Active.ActionId=ActionId;Active.Target=Target;Active.bMark=bMark;Active.bFollowTarget=bMark;
 Active.Offset=Cue.LocalOffset;Active.Rotation=Cue.LocalRotation;Active.Lifetime=Cue.LifetimeSeconds;
 Active.Transform=SakuraCueTransform(Unit.Get(),Target,Point,Active.Offset,Active.Rotation,Cue.Scale,bMark || Target!=nullptr);
 if(bSource)
 {
  if(!Cue.SourceEffect.IsValid())UE_LOG(LogTemp,Display,TEXT("Sakura SourceEffect required a synchronous fallback"));
  auto* Definition=Cue.SourceEffect.LoadSynchronous();
  auto* Mesh=Unit->GetVisiblePresentationMesh();
  // A socket-bound definition would overwrite the captured target transform on tick.
  if(!IsValid(Definition) || !Definition->AnchorBone.IsNone() || !IsValid(Mesh))
  {
   UE_LOG(LogTemp,Warning,TEXT("Sakura rework world cue has invalid SourceEffect or a socket anchor"));
   return false;
  }
  auto* Source=AEGSourceEffect::Spawn(Skill.Get(),Mesh,Definition,1.f,true);
  if(!Source)return false;
  // The normal round-bound lifespan uses world time. This receiver instead
  // owns cleanup, so holds pause both the source timeline and its safety cap.
  Source->SetLifeSpan(0.f);
  Source->AddTickPrerequisiteActor(this);
  Source->SetActorTickEnabled(Battle.IsValid() && Battle->IsCombatRunning());
  Source->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
  FTransform SourceTransform=Active.Transform;
  SourceTransform.SetScale3D(FVector(Cue.Scale*Definition->PresentationScale));
  Source->SetActorTransform(SourceTransform);
  Active.Source=Source;
 }
 else
 {
  if(!Cue.NiagaraSystem.IsValid())UE_LOG(LogTemp,Display,TEXT("Sakura Niagara cue required a synchronous fallback"));
  auto* System=Cue.NiagaraSystem.LoadSynchronous();
  if(!IsValid(System))return false;
  auto* Niagara=NewObject<UNiagaraComponent>(this);
  AddInstanceComponent(Niagara);
  Niagara->SetupAttachment(GetRootComponent());
  Niagara->SetAutoActivate(false);Niagara->SetAutoDestroy(false);
  Niagara->SetAsset(System);Niagara->RegisterComponent();
  Niagara->SetWorldTransform(Active.Transform);
  Niagara->Activate(true);
  Niagara->AddTickPrerequisiteActor(this);
  Niagara->SetPaused(!Battle.IsValid() || !Battle->IsCombatRunning());
  Active.Niagara=Niagara;
 }
 ActiveCues.Add(MoveTemp(Active));
 return true;
}

void AEGFiveHeroPresentation::RemoveMarks(const FGuid& ActionId, AEGChessUnit* Target)
{
 for(int32 Index=ActiveCues.Num()-1;Index>=0;--Index)
  if(ActiveCues[Index].bMark && ActiveCues[Index].ActionId==ActionId
   && (!Target || ActiveCues[Index].Target.Get()==Target))RemoveCue(Index);
}

void AEGFiveHeroPresentation::RemoveCue(int32 Index)
{
 if(!ActiveCues.IsValidIndex(Index))return;
 auto& Active=ActiveCues[Index];
 if(IsValid(Active.Niagara)){Active.Niagara->DeactivateImmediate();Active.Niagara->DestroyComponent();}
 if(Active.Source.IsValid())Active.Source->Destroy();
 ActiveCues.RemoveAtSwap(Index);
}

int32 AEGFiveHeroPresentation::GetActiveMarkCount() const
{
 int32 Count=0;
 for(const auto& Active:ActiveCues)if(Active.bMark)++Count;
 return Count;
}

int32 AEGFiveHeroPresentation::GetActiveCueCount(EEGFiveHeroCue Cue) const
{
 int32 Count=0;
 for(const auto& Active:ActiveCues)if(Active.Cue==Cue)++Count;
 return Count;
}

void AEGFiveHeroPresentation::BeginDodge(AEGChessUnit* Attacker)
{
 ClearDodge();
 auto* Caster=Unit.Get();auto* Body=Caster?Caster->GetVisiblePresentationMesh():nullptr;
 if(!IsValid(Body) || !IsValid(Body->GetSkeletalMeshAsset()) || !IsValid(Config))return;
 DodgeBody=Body;DodgeBodyAsset=Body->GetSkeletalMeshAsset();
 DodgeAge=0.f;DodgeDuration=FMath::Clamp(Config->DodgeDuration,.15f,.25f);
 DodgeDistance=FMath::Clamp(Config->DodgeDistance,0.f,35.f);
 DodgeOpacity=FMath::Clamp(Config->AfterimageOpacity,0.f,1.f);
 DodgeDirection=Caster->GetActorRightVector().GetSafeNormal2D();
 if(IsValid(Attacker))
 {
  const FVector AttackDirection=(Attacker->GetActorLocation()-Caster->GetActorLocation()).GetSafeNormal2D();
  if(FVector::DotProduct(DodgeDirection,AttackDirection)>0.f)DodgeDirection=-DodgeDirection;
 }

 // The body can be accompanied by components owned by the active combat rig.
 // Only visible model and weapon meshes are included; effects are untouched.
 TArray<USceneComponent*> Parts;Parts.Add(Body);
 auto AddParts=[&Parts](AActor* PartsOwner,bool bRig)
 {
  if(!IsValid(PartsOwner))return;
  TArray<USkeletalMeshComponent*> Skeletal;PartsOwner->GetComponents(Skeletal);
  for(auto* Part:Skeletal)if(IsValid(Part) && Part->IsRegistered() && Part->IsVisible()
   && Part->GetSkeletalMeshAsset() && (bRig || Part->ComponentHasTag(TEXT("EGWeapon"))))Parts.AddUnique(Part);
  TArray<UStaticMeshComponent*> Static;PartsOwner->GetComponents(Static);
  for(auto* Part:Static)if(IsValid(Part) && Part->IsRegistered() && Part->IsVisible()
   && Part->GetStaticMesh() && Part->ComponentHasTag(TEXT("EGWeapon")))Parts.AddUnique(Part);
 };
 AddParts(Caster,false);
 for(TActorIterator<AEGCombatVisual> It(GetWorld());It;++It)
  if(It->GetOwner()==Caster && It->BodyMesh==Body)AddParts(*It,true);

 for(auto* Part:Parts)
 {
  bool bAttachedToMovedPart=false;
  for(const auto* Other:Parts)
   if(Other!=Part && Part->IsAttachedTo(Other)){bAttachedToMovedPart=true;break;}
  if(!bAttachedToMovedPart){FEGSakuraDodgeMovedPart Moved;Moved.Component=Part;DodgeParts.Add(MoveTemp(Moved));}
 }

 if(Config->AfterimageMaterial.IsNull())return;
 if(!Config->AfterimageMaterial.IsValid())UE_LOG(LogTemp,Display,TEXT("Sakura afterimage material required a synchronous fallback"));
 auto* Material=Config->AfterimageMaterial.LoadSynchronous();if(!IsValid(Material))return;
 for(int32 Image=0;Image<2;++Image)
 {
  auto* MID=UMaterialInstanceDynamic::Create(Material,this);if(!MID)continue;
  MID->SetScalarParameterValue(TEXT("Opacity"),DodgeOpacity*(Image==0?1.f:.55f));
  DodgeGhostMaterials.Add(MID);
  const FVector Offset=-DodgeDirection*(8.f+Image*11.f);
  for(auto* Part:Parts)
  {
   UMeshComponent* Ghost=nullptr;
   if(auto* Skeletal=Cast<USkeletalMeshComponent>(Part))
   {
    auto* Pose=NewObject<UPoseableMeshComponent>(this);AddInstanceComponent(Pose);
    Pose->SetupAttachment(GetRootComponent());Pose->SetSkinnedAssetAndUpdate(Skeletal->GetSkeletalMeshAsset());
    Pose->SetCollisionEnabled(ECollisionEnabled::NoCollision);Pose->SetCastShadow(false);Pose->RegisterComponent();
    Pose->SetWorldTransform(Skeletal->GetComponentTransform());Pose->CopyPoseFromSkeletalComponent(Skeletal);
    for(int32 Bone=0;Bone<Skeletal->GetNumBones();++Bone)
    {
     const FName Name=Skeletal->GetBoneName(Bone);
     if(Skeletal->IsBoneHiddenByName(Name))Pose->HideBoneByName(Name,EPhysBodyOp::PBO_None);
    }
    Ghost=Pose;
   }
   else if(auto* Static=Cast<UStaticMeshComponent>(Part))
   {
    auto* Copy=NewObject<UStaticMeshComponent>(this);AddInstanceComponent(Copy);
    Copy->SetupAttachment(GetRootComponent());Copy->SetStaticMesh(Static->GetStaticMesh());
    Copy->SetCollisionEnabled(ECollisionEnabled::NoCollision);Copy->SetCastShadow(false);Copy->RegisterComponent();
    Copy->SetWorldTransform(Static->GetComponentTransform());Ghost=Copy;
   }
   if(!IsValid(Ghost))continue;
   Ghost->SetGenerateOverlapEvents(false);Ghost->SetCanEverAffectNavigation(false);
   for(int32 Slot=0;Slot<Ghost->GetNumMaterials();++Slot)Ghost->SetMaterial(Slot,MID);
   Ghost->AddWorldOffset(Offset);DodgeGhostParts.Add(Ghost);
  }
 }
}

void AEGFiveHeroPresentation::RestoreDodgeOffsets()
{
 for(auto& Part:DodgeParts)
 {
  auto* Component=Part.Component.Get();
  // Never overwrite a transform changed by the montage/rig or another owner.
  if(Part.bApplied && IsValid(Component) && Component->GetRelativeTransform().Equals(Part.Applied,.01f))
   Component->SetRelativeTransform(Part.Base);
  Part.bApplied=false;
 }
}

void AEGFiveHeroPresentation::ClearDodge()
{
 RestoreDodgeOffsets();DodgeParts.Reset();
 for(const auto& Ghost:DodgeGhostParts)if(IsValid(Ghost.Get()))Ghost->DestroyComponent();
 DodgeGhostParts.Reset();DodgeGhostMaterials.Reset();DodgeBody.Reset();DodgeBodyAsset.Reset();
 DodgeAge=0.f;DodgeDuration=0.f;
}

void AEGFiveHeroPresentation::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Unit.IsValid() || !IEGCombatant::Execute_IsAlive(Unit.Get()) || !Skill.IsValid()
  || !Battle.IsValid() || !Battle->IsBattleActive() || Battle->GetRoundId()!=Round)
 {Destroy();return;}
 const bool bAdvance=Battle->IsCombatRunning();
 if(DodgeDuration>0.f)
 {
  auto* Body=DodgeBody.Get();
  if(!IsValid(Body) || Unit->GetVisiblePresentationMesh()!=Body || Body->GetSkeletalMeshAsset()!=DodgeBodyAsset.Get())ClearDodge();
  else
  {
   RestoreDodgeOffsets();
   if(bAdvance)DodgeAge+=FMath::Max(0.f,DeltaSeconds);
   if(DodgeAge>=DodgeDuration)ClearDodge();
   else
   {
    const float Progress=FMath::Clamp(DodgeAge/DodgeDuration,0.f,1.f);
    const FVector Offset=DodgeDirection*(DodgeDistance*FMath::Sin(PI*Progress));
    for(auto& Part:DodgeParts)if(auto* Component=Part.Component.Get())
    {
     Part.Base=Component->GetRelativeTransform();Component->AddWorldOffset(Offset);
     Part.Applied=Component->GetRelativeTransform();Part.bApplied=true;
    }
    for(int32 Index=0;Index<DodgeGhostMaterials.Num();++Index)
     if(auto* MID=DodgeGhostMaterials[Index].Get())MID->SetScalarParameterValue(TEXT("Opacity"),DodgeOpacity*(Index==0?1.f:.55f)*(1.f-Progress));
   }
  }
 }
 for(int32 Index=ActiveCues.Num()-1;Index>=0;--Index)
 {
  auto& Active=ActiveCues[Index];
  if(Active.Source.IsValid())Active.Source->SetActorTickEnabled(bAdvance);
  if(IsValid(Active.Niagara))Active.Niagara->SetPaused(!bAdvance);
  if(bAdvance)Active.Age+=FMath::Max(0.f,DeltaSeconds);
  auto* Target=Active.Target.Get();
  if(Active.Age>=Active.Lifetime || (Active.bFollowTarget && (!Target || !IEGCombatant::Execute_IsAlive(Target)))
   || (!IsValid(Active.Niagara) && !Active.Source.IsValid()))
  {RemoveCue(Index);continue;}
  if(Active.bFollowTarget)
  {
   const auto Transform=SakuraCueTransform(Unit.Get(),Target,Target->GetActorLocation(),Active.Offset,
    Active.Rotation,Active.Transform.GetScale3D().X,true);
   if(IsValid(Active.Niagara))Active.Niagara->SetWorldTransform(Transform);
   if(Active.Source.IsValid())
   {
    FTransform SourceTransform=Transform;
    SourceTransform.SetScale3D(Active.Source->GetActorScale3D());
    Active.Source->SetActorTransform(SourceTransform);
   }
  }
 }
 if(ActiveCues.IsEmpty() && DodgeDuration<=0.f)Destroy();
}

void AEGFiveHeroPresentation::EndPlay(const EEndPlayReason::Type Reason)
{
 for(int32 Index=ActiveCues.Num()-1;Index>=0;--Index)RemoveCue(Index);
 ClearDodge();
 Super::EndPlay(Reason);
}
