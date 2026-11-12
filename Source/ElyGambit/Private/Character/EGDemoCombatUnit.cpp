#include "Character/EGDemoCombatUnit.h"
#include "UI/EGVitalsLayoutSubsystem.h"
#include "Engine/World.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Presentation/EGWeaponVisuals.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGDeploymentBoard.h"
#include "Components/EGCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Presentation/EGBoardLocomotion.h"
#include "UI/Widget/EGDemoPanel.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Actor/EGBoard.h"
#include "Game/EGChessGameMode.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Presentation/EGPresentationBurst.h"
#include "UObject/ConstructorHelpers.h"

AEGDemoCombatUnit::AEGDemoCombatUnit()
{
 PrimaryActorTick.bCanEverTick=true;
 Vitals=CreateDefaultSubobject<UWidgetComponent>(TEXT("Vitals"));
 Vitals->SetupAttachment(GetRootComponent());
 Vitals->SetWidgetSpace(EWidgetSpace::Screen);
 Vitals->SetDrawSize(FVector2D(128,72));Vitals->SetPivot(FVector2D(.5,1));
 Vitals->SetRelativeLocation(FVector(0,0,100));
 Vitals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Vitals->SetWidgetClass(UEGUnitVitals::StaticClass());
 Bolt=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AttackPulse"));
 Bolt->SetupAttachment(GetRootComponent());
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
 Bolt->SetStaticMesh(Sphere.Object);
 Bolt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Bolt->SetCastShadow(false);Bolt->SetVisibility(false);
 GetMesh()->SetRelativeLocation(FVector(0,0,-88));
 GetMesh()->SetRelativeRotation(FRotator(0,-90,0));
}

bool AEGDemoCombatUnit::InitializeUnit(UEGHeroDefinition* Definition,FGuid Id,EEGFaction NewFaction,int32 Level)
{
 if(!Super::InitializeUnit(Definition,Id,NewFaction,Level))return false;
 ConfigurePresentation();
 Vitals->SetWidgetClass(UEGUnitVitals::ResolveWidgetClass());
 Vitals->InitWidget();
 if(auto* Layout=GetWorld()->GetSubsystem<UEGVitalsLayoutSubsystem>())Layout->Register(Vitals);
 if(auto* View=Cast<UEGUnitVitals>(Vitals->GetUserWidgetObject()))View->SetUnit(this);
 return true;
}
void AEGDemoCombatUnit::BeginPlay()
{
 Super::BeginPlay();OnDamageReceived.AddUniqueDynamic(this,&ThisClass::HandleMonsterDamage);Vitals->InitWidget();
 if(auto* Layout=GetWorld()->GetSubsystem<UEGVitalsLayoutSubsystem>())Layout->Register(Vitals);
 if(auto* View=Cast<UEGUnitVitals>(Vitals->GetUserWidgetObject()))View->SetUnit(this);
}
void AEGDemoCombatUnit::EndPlay(const EEndPlayReason::Type Reason)
{
 OnDamageReceived.RemoveDynamic(this,&ThisClass::HandleMonsterDamage);
 FEGBoardModelSizing::Forget(this);
 if(Locomotion)Locomotion->Reset();
 EndAttackPounce();
 RestorePiece();
 if(auto* View=Cast<UEGUnitVitals>(Vitals->GetUserWidgetObject()))View->SetUnit(nullptr);
 Super::EndPlay(Reason);
}
void AEGDemoCombatUnit::HandleMonsterDamage(float Amount)
{
 const UEGHeroDefinition* Definition=GetDefinition();
 if(!FMath::IsFinite(Amount)||Amount<=0.f||!Definition||!Definition->MonsterAttack||
    !IsAlive_Implementation()||GetCombatState_Implementation()!=EEGUnitState::Fighting||
    !GetHeroSkill()||GetHeroSkill()->IsBusy()||GetHeroSkill()->IsActionProtected())return;
 // Only the cosmetic idle/locomotion pose may be interrupted. Attacks and
 // skills retain their montage, notify identity and authoritative timing.
 FEGAnimationPlayback::Play(PresentationMesh(),EGPresentationAction::Hit);
}
void AEGDemoCombatUnit::ConfigurePresentation()
{
 const auto* Definition=GetDefinition();if(!Definition)return;
 IdleAnimation=Definition->GetIdleAnimation().LoadSynchronous();WalkAnimation=Definition->GetWalkAnimation().LoadSynchronous();
 RunAnimation=Definition->GetRunAnimation().LoadSynchronous();
 AttackAnimation=Definition->GetAttackAnimation().LoadSynchronous();DeathAnimation=Definition->GetDeathAnimation().LoadSynchronous();
 SkillAnimation=Definition->GetSkillAnimation().LoadSynchronous();
 AttackMaterial=Definition->GetAttackMaterial().LoadSynchronous();SkillMaterial=Definition->GetSkillMaterial().LoadSynchronous();
 FEGBoardModelSizing::Forget(this);
 const float BoardModelScale=FEGBoardModelSizing::Resolve(Definition,this);
 GetMesh()->SetRelativeScale3D(Definition->GetMeshScale()*BoardModelScale);GetMesh()->SetRelativeRotation(Definition->GetPresentationRotation());
 GetMesh()->SetRelativeLocation(FVector(0,0,-88.f+Definition->GetPresentationGroundOffset()*BoardModelScale));
 if(Definition->GetVitalsAnchorHeight()>0)Vitals->SetRelativeLocation(FVector(0,0,Definition->GetVitalsAnchorHeight()-88.f));
 PreviousLocation=GetActorLocation();
 if(!Locomotion)Locomotion=MakeShared<FEGBoardLocomotion>();
 Locomotion->Reset();Locomotion->Sample(PreviousLocation,0.f,Definition->GetRunAnimationMinSpeed());
 EGApplyWeaponVisuals(this,GetMesh(),Definition,BoardModelScale);
 FEGAnimationPlayback::Configure(GetMesh(),Definition);
 PlayPresentation(IdleAnimation,true);
}
void AEGDemoCombatUnit::ConfigureMonster(int32 Kind){ConfigurePresentation();}

void AEGDemoCombatUnit::AttachPiece(AEGDeployedPiece* Piece)
{
 if(!IsValid(Piece)||Piece->IsActorBeingDestroyed()||Piece->IsPendingKillPending()||!IsValid(Piece->Body))return;
 const TWeakObjectPtr<AEGDeployedPiece> Target(Piece),Previous=ArtPiece;
 const TWeakObjectPtr<USkeletalMeshComponent> OldBody(PresentationMesh()),TargetBody(Piece->Body.Get());
 const TWeakObjectPtr<USkeletalMesh> OldMesh(OldBody.IsValid()?OldBody->GetSkeletalMeshAsset():nullptr);
 const TWeakObjectPtr<USkeletalMesh> TargetMesh(TargetBody->GetSkeletalMeshAsset());
 const TWeakObjectPtr<AActor> TargetParent(Piece->GetAttachParentActor());
 if(PresentationMesh()!=Piece->Body.Get())NotifyPresentationWillRelease();
 if(IsActorBeingDestroyed()||IsPendingKillPending()||!Target.IsValid()||Target->IsActorBeingDestroyed()||Target->IsPendingKillPending()||
    ArtPiece.Get()!=Previous.Get()||PresentationMesh()!=OldBody.Get()||!OldBody.IsValid()||OldBody->GetSkeletalMeshAsset()!=OldMesh.Get()||
    Target->Body.Get()!=TargetBody.Get()||!TargetBody.IsValid()||TargetBody->GetSkeletalMeshAsset()!=TargetMesh.Get()||
    Target->GetAttachParentActor()!=TargetParent.Get())return;
 Piece=Target.Get();
 ArtPiece=Piece;GetMesh()->SetVisibility(false,true);PreparationTransform=Piece->GetActorTransform();
 Piece->SetBattlePresentation(true);
 Piece->SetPreparationVitalsVisible(false);
 Piece->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
 Piece->SetActorRelativeLocation(FVector(0,0,-88));
 // The preparation root already includes its authored mesh-facing offset. Keep
 // its world rotation through attachment instead of applying that offset again.
 Piece->SetActorEnableCollision(false);
 PlayPresentation(IdleAnimation,true);
 Vitals->SetRelativeLocation(FVector(0,0,GetDefinition()&&GetDefinition()->GetVitalsAnchorHeight()>0?GetDefinition()->GetVitalsAnchorHeight()-88.f:120.f));
 if(auto* Layout=GetWorld()->GetSubsystem<UEGVitalsLayoutSubsystem>())Layout->Register(Vitals);
}

void AEGDemoCombatUnit::RestorePiece()
{
 const TWeakObjectPtr<AEGDeployedPiece> Previous=ArtPiece;
 const TWeakObjectPtr<USkeletalMeshComponent> OldBody(PresentationMesh());
 const TWeakObjectPtr<USkeletalMesh> OldMesh(OldBody.IsValid()?OldBody->GetSkeletalMeshAsset():nullptr);
 const TWeakObjectPtr<AActor> PreviousParent(Previous.IsValid()?Previous->GetAttachParentActor():nullptr);
 if(Previous.IsValid())
 {
  NotifyPresentationWillRelease();
  if(IsPendingKillPending()||!Previous.IsValid()||Previous->IsActorBeingDestroyed()||Previous->IsPendingKillPending()||
     ArtPiece.Get()!=Previous.Get()||!OldBody.IsValid()||PresentationMesh()!=OldBody.Get()||
     OldBody->GetSkeletalMeshAsset()!=OldMesh.Get()||Previous->GetAttachParentActor()!=PreviousParent.Get())return;
 }
 EndAttackPounce();
 BoltTime=0;Bolt->SetVisibility(false);
 if(!ArtPiece.IsValid())return;
 auto* Piece=ArtPiece.Get();
 Piece->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
 Piece->SetActorTransform(PreparationTransform);
 Piece->SetActorHiddenInGame(false);Piece->SetActorEnableCollision(true);
 Piece->SetPreparationVitalsVisible(true);Piece->RefreshPreparationVitals();
 if(FEGAnimationPlayback::IsSupported(Piece->Body)){FEGAnimationPlayback::Stop(Piece->Body);FEGAnimationPlayback::Speed(Piece->Body,0.f);}
 else if(Piece->IdleAnimation)Piece->Body->PlayAnimation(Piece->IdleAnimation,true);
 Piece->SetBattlePresentation(false);
 ArtPiece.Reset();
 SetActorHiddenInGame(true);
}

void AEGDemoCombatUnit::PresentAttack(AEGChessUnit* Target,bool bUltimate)
{TryPresentAction(Target,bUltimate);}

bool AEGDemoCombatUnit::TryPresentAction(AEGChessUnit* Target,bool bUltimate)
{
 if(!IsValid(Target))return false;
 auto* Animation=bUltimate&&SkillAnimation?SkillAnimation.Get():AttackAnimation.Get();
 const auto* Active=GetHeroSkill()->GetActiveAnimation();
 AnimationTime=Active?Active->GetPlayLength():(Animation?Animation->GetPlayLength():(bUltimate?.7f:.4f));
 const bool bPlayed=GetHeroSkill()->GetActiveMontage()?FEGAnimationPlayback::Play(PresentationMesh(),bUltimate?EGPresentationAction::Skill:EGPresentationAction::Attack):PlayPresentation(Animation,false);
 if(GetDefinition()&&(GetDefinition()->HeroKind!=EEGHeroKind::Generic||GetDefinition()->MonsterAttack)&&!bPlayed){AnimationTime=0;return false;}
 ++AttackCount;if(bUltimate)++UltimateCount;
 // Presentation-only: optional pounce on the visible mesh for basic attacks. Gameplay
 // transform/range/damage are untouched; the authored mesh transform is restored on exit.
 if(!bUltimate&&bPlayed)BeginAttackPounce();
 if(GetDefinition()&&(GetDefinition()->HeroKind!=EEGHeroKind::Generic||GetDefinition()->MonsterAttack))return true;
 if(auto* FX=AEGPresentationBurst::Spawn(GetWorld(),Target->GetActorLocation()-FVector(0,0,85),bUltimate?SkillMaterial.Get():AttackMaterial.Get(),bUltimate?2.2f:.65f))if(GetHeroSkill()->Battle.IsValid())FX->RoundId=GetHeroSkill()->Battle->GetRoundId();
 if(auto* Mat=bUltimate?SkillMaterial.Get():AttackMaterial.Get())Bolt->SetMaterial(0,Mat);
 BoltStart=GetActorLocation()+FVector(0,0,10);
 BoltEnd=Target->GetActorLocation()+FVector(0,0,10);
 BoltTime=.3f;bSkillPulse=bUltimate;
 Bolt->SetWorldLocation(bUltimate?BoltEnd:BoltStart);
 Bolt->SetWorldScale3D(bUltimate?FVector(.1f,.1f,.06f):FVector(.12f));
 Bolt->SetVisibility(true);
 return true;
}

void AEGDemoCombatUnit::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 // Presentation-only pounce. Funnel every terminal state (death/flight/cancel/battle end)
 // through EndAttackPounce so the mesh transform is never left displaced.
 if(bPounceActive&&(!IEGCombatant::Execute_IsAlive(this)||GetHeroSkill()->bInFlight))EndAttackPounce();
 else TickAttackPounce(DeltaSeconds);
 if(BoltTime>0.f)
 {
  BoltTime=FMath::Max(0.f,BoltTime-DeltaSeconds);
  const float Alpha=1.f-BoltTime/.3f;
  Bolt->SetWorldLocation(bSkillPulse?BoltEnd:FMath::Lerp(BoltStart,BoltEnd,Alpha));
  Bolt->SetWorldScale3D(bSkillPulse?FVector(.1f+Alpha*4.f,.1f+Alpha*4.f,.06f):FVector(.12f));
  if(BoltTime<=0.f)Bolt->SetVisibility(false);
 }
 if(GetHeroSkill()->bInFlight)
 {
  if(Locomotion)Locomotion->Reset();
  if(!bFlightShown){bFlightShown=true;PlayPresentation(SkillAnimation,false);}
  PreviousLocation=GetActorLocation();return;
 }
 if(bFlightShown){bFlightShown=false;PlayPresentation(IdleAnimation,true);}
 if(GetHeroSkill()->IsRecoverable())
 {if(Locomotion)Locomotion->Reset();if(!bReviveShown){bReviveShown=true;PlayPresentation(DeathAnimation,false);}return;}
 bReviveShown=false;
 if(!IEGCombatant::Execute_IsAlive(this))
 {
  if(Locomotion)Locomotion->Reset();
  if(!bDeathShown)
  {
   bDeathShown=true;AnimationTime=0.f;DeathPresentationElapsed=0.f;
   PlayPresentation(DeathAnimation,false);
   Vitals->SetVisibility(false);BoltTime=0;Bolt->SetVisibility(false);
  }
  if(GetDefinition()&&GetDefinition()->MonsterAttack)TickMonsterDeath(DeltaSeconds);
  else if(const auto* P=GetDefinition()?GetDefinition()->PresentationDefinition.Get():nullptr;P&&P->DeathVisualHideDelay>=0.f)
  {
   DeathPresentationElapsed+=DeltaSeconds;
   if(DeathPresentationElapsed>=P->DeathVisualHideDelay)PresentationMesh()->SetVisibility(false,true);
  }
  return;
 }
 if(IEGCombatant::Execute_GetCombatState(this)!=EEGUnitState::Fighting)
 {if(Locomotion)Locomotion->Reset();PreviousLocation=GetActorLocation();return;}
 const auto* VillV=FindComponentByClass<UEGVillVMechanismComponent>();
 if(auto* Target=GetCombat()->GetTarget();Target&&!VillV&&!GetHeroSkill()->IsActionProtected()&&!(GetDefinition()->MonsterAttack&&GetHeroSkill()->IsBusy()))
 {
  FVector Direction=Target->GetActorLocation()-GetActorLocation();Direction.Z=0;
  if(GetDefinition()->HeroKind==EEGHeroKind::Knight)
  {
   const FVector Movement=GetActorLocation()-PreviousLocation;
   if(!GetHeroSkill()->IsBusy()&&Movement.SizeSquared2D()>1.f)Direction=Movement.GetSafeNormal2D();
   if(!GetHeroSkill()->IsBusy()&&!Direction.IsNearlyZero())SetActorRotation(FMath::RInterpTo(GetActorRotation(),Direction.Rotation(),DeltaSeconds,8.f));
  }
  else if(!Direction.IsNearlyZero())SetActorRotation(Direction.Rotation());
 }
 PreviousLocation=GetActorLocation();
 if(!Locomotion)Locomotion=MakeShared<FEGBoardLocomotion>();
 const auto* Definition=GetDefinition();
 float Speed=Locomotion->Sample(PreviousLocation,DeltaSeconds,Definition->GetRunAnimationMinSpeed());
 AnimationTime-=DeltaSeconds;
 if(Definition->MonsterAttack&&GetHeroSkill()->IsBusy()){Locomotion->Reset();Speed=0.f;}
 if(AnimationTime<=0.f)
 {
  if(FEGAnimationPlayback::IsSupported(PresentationMesh()))
  {
   FEGAnimationPlayback::Speed(PresentationMesh(),Speed);
  }
  else
  {
   auto* Gait=RunAnimation&&Locomotion->IsRunning()?RunAnimation.Get():WalkAnimation.Get();
   auto* Desired=Speed>0.f&&Gait?Gait:IdleAnimation.Get();
   auto* DisplayMesh=PresentationMesh();
   if(Desired&&DisplayMesh->GetAnimationMode()==EAnimationMode::AnimationSingleNode)
   {
    auto* Instance=DisplayMesh->GetSingleNodeInstance();
    if(!Instance||Instance->GetCurrentAsset()!=Desired)PlayPresentation(Desired,true);
   }
  }
 }

}

void AEGDemoCombatUnit::TickMonsterDeath(float DeltaSeconds)
{
 const auto* Profile=GetDefinition()->MonsterAttack.Get();
 if(Profile->DeathMaterials.IsEmpty()||GetNetMode()==NM_DedicatedServer)return;
 MonsterDeathElapsed+=DeltaSeconds;
 const float FallSeconds=DeathAnimation?DeathAnimation->GetPlayLength():0.f;
 const float DissolveAge=MonsterDeathElapsed-FallSeconds-Profile->DeathHoldSeconds;
 if(DissolveAge<0)return;
 auto* DisplayMesh=PresentationMesh();
 if(DeathMaterials.IsEmpty())
 {
  // The variants retain the live shader's surface inputs and add Aura's mask/glow.
  // Actor-owned dynamic instances cannot leak dissolve progress to other monsters.
  for(int32 Slot=0;Slot<DisplayMesh->GetNumMaterials();++Slot)
   if(Profile->DeathMaterials.IsValidIndex(Slot))if(auto* Material=Profile->DeathMaterials[Slot].LoadSynchronous())
   {
    auto* Instance=UMaterialInstanceDynamic::Create(Material,this);
    Instance->SetScalarParameterValue(TEXT("Dissolve"),-1.f);DisplayMesh->SetMaterial(Slot,Instance);DeathMaterials.Add(Instance);
   }
 }
 const float Progress=FMath::Clamp(DissolveAge/Profile->DissolveSeconds,0.f,1.f);
 for(const auto& Material:DeathMaterials)Material->SetScalarParameterValue(TEXT("Dissolve"),FMath::Lerp(-1.f,1.f,Progress));
 if(Progress>=1.f)DisplayMesh->SetVisibility(false,true);
 // Keep the dead incarnation registered for settlement; the existing round reset
 // destroys it and its material instances. No lifespan callback touches later rounds.
}

USkeletalMeshComponent* AEGDemoCombatUnit::PresentationMesh() const
{
 return ArtPiece.IsValid()?ArtPiece->Body.Get():GetMesh();
}
void AEGDemoCombatUnit::DriveLocomotion(float Speed)
{
 // The anim instance blends idle/walk/run itself; only the board speed has to be published.
 FEGAnimationPlayback::Speed(PresentationMesh(),FMath::Max(0.f,Speed));
}
bool AEGDemoCombatUnit::PlayPresentation(UAnimSequence* Animation,bool bLoop)
{
 auto* DisplayMesh=PresentationMesh();
 if(FEGAnimationPlayback::IsSupported(DisplayMesh))
 {
  // Several source units deliberately reuse idle as their death fallback. A
  // requested looping gait must never be misidentified as that terminal action.
  if(bLoop&&(Animation==IdleAnimation||Animation==WalkAnimation||Animation==RunAnimation))
  {FEGAnimationPlayback::Stop(DisplayMesh);FEGAnimationPlayback::Speed(DisplayMesh,Animation==IdleAnimation?0.f:GetAttributes()->GetMoveSpeed());return true;}
  // Crossfading path: clips are identified against the definition so callers keep passing assets.
  if(Animation&&Animation==SkillAnimation)return FEGAnimationPlayback::Play(DisplayMesh,EGPresentationAction::Skill);
  if(Animation&&Animation==AttackAnimation)return FEGAnimationPlayback::Play(DisplayMesh,EGPresentationAction::Attack);
  if(Animation&&Animation==DeathAnimation)return FEGAnimationPlayback::Play(DisplayMesh,EGPresentationAction::Death);
  FEGAnimationPlayback::Stop(DisplayMesh);
  if(Animation&&Animation==IdleAnimation)FEGAnimationPlayback::Speed(DisplayMesh,0.f);
  return bLoop;
 }
 if(Animation&&DisplayMesh&&DisplayMesh->GetSkeletalMeshAsset()&&Animation->GetSkeleton()==DisplayMesh->GetSkeletalMeshAsset()->GetSkeleton())
 {DisplayMesh->PlayAnimation(Animation,bLoop);if(bLoop)DisplayMesh->SetPlayRate(1.f);return true;}
 return false;
}

void AEGDemoCombatUnit::SetPresentationRate(float Rate)
{
 if(!FMath::IsFinite(Rate)||Rate<=0)return;
 auto* DisplayMesh=PresentationMesh();FEGAnimationPlayback::Rate(DisplayMesh,Rate);
 auto* Montage=GetHeroSkill()->GetActiveMontage();auto* Instance=DisplayMesh?DisplayMesh->GetAnimInstance():nullptr;
 if(Montage&&Instance)AnimationTime=FMath::Max(0.f,Montage->GetPlayLength()-Instance->Montage_GetPosition(Montage))/Rate;
 else AnimationTime/=Rate;
 // The action wall duration is now exact; the pounce uses it as its presentation clock.
 if(bPounceAwaitingRate){PounceDuration=FMath::Max(.05f,AnimationTime);bPounceAwaitingRate=false;}
}
void AEGDemoCombatUnit::CancelPresentationAction()
{
 EndAttackPounce();
 FEGAnimationPlayback::Stop(PresentationMesh());
 if(GetDefinition()&&GetDefinition()->MonsterAttack)
 {AnimationTime=0.f;if(Locomotion)Locomotion->Reset();FEGAnimationPlayback::Speed(PresentationMesh(),0.f);}
}
void AEGDemoCombatUnit::PresentRevive()
{
 bDeathShown=false;bReviveShown=true;
 if(FEGAnimationPlayback::IsSupported(PresentationMesh())){FEGAnimationPlayback::Stop(PresentationMesh());FEGAnimationPlayback::Speed(PresentationMesh(),0.f);}
 else
 {
  PlayPresentation(DeathAnimation,false);
  if(DeathAnimation)if(auto* I=PresentationMesh()->GetSingleNodeInstance()){I->SetPosition(DeathAnimation->GetPlayLength(),false);I->SetPlayRate(-DeathAnimation->GetPlayLength()/.35f);I->SetPlaying(true);}
 }
 AnimationTime=.35f;
}

FVector AEGDemoCombatUnit::GetActionOrigin() const
{
 auto* M=PresentationMesh();const auto* D=GetDefinition();
 if(M&&D&&D->MonsterAttack)
 {
  const auto* P=D->MonsterAttack.Get();
  const FVector Base=M->DoesSocketExist(P->LaunchBone)?M->GetSocketLocation(P->LaunchBone):
   GetActorLocation()+FVector(0,0,D->GetVitalsAnchorHeight()*.6f-88.f);
  return Base+GetActorRotation().RotateVector(P->LaunchOffset);
 }
 if(M&&D)
 {
  TArray<USceneComponent*> WeaponChildren;M->GetChildrenComponents(false,WeaponChildren);
  for(auto* Child:WeaponChildren)if(Child->ComponentHasTag(TEXT("EGWeapon")))
   if(auto* Weapon=Cast<USkeletalMeshComponent>(Child);Weapon&&Weapon->DoesSocketExist(D->GetProjectileSocket()))
    return Weapon->GetSocketLocation(D->GetProjectileSocket());
 }
 return M&&D&&M->DoesSocketExist(D->GetProjectileSocket())?M->GetSocketLocation(D->GetProjectileSocket()):Super::GetActionOrigin();
}

void AEGDemoCombatUnit::BeginAttackPounce()
{
 // Hard no-op for every unit whose BP leaves the struct neutral; only BP_Combat_Can opts in.
 EndAttackPounce();
 if(!AttackPounce.IsConfigured())return;
 // The pounce writes the visible mesh's own RELATIVE transform; an attached board piece would
 // need a different parent space, and only own-mesh units opt in.
 if(ArtPiece.IsValid())return;
 auto* DisplayMesh=GetMesh();if(!DisplayMesh)return;
 bPounceActive=true;bPounceAwaitingRate=true;PounceElapsed=0.f;
 PounceRestRelative=DisplayMesh->GetRelativeTransform();
 // Same-frame fallback for the action wall duration. UEGHeroSkillComponent calls
 // SetPresentationRate right after TryPresentAction and overwrites this with the exact value.
 PounceDuration=0.f;
 if(const auto* AttrSet=GetAttributes())
 {
  const float AttackSpeed=AttrSet->GetAttackSpeed();
  if(FMath::IsFinite(AttackSpeed)&&AttackSpeed>KINDA_SMALL_NUMBER)PounceDuration=FMath::Max(.1f,1.f/AttackSpeed);
 }
}

void AEGDemoCombatUnit::EndAttackPounce()
{
 if(!bPounceActive)return;
 bPounceActive=false;bPounceAwaitingRate=false;PounceElapsed=0.f;PounceDuration=0.f;
 // Restore the authored relative transform exactly, including scale (proportions must be
 // identical to the definition after every attack/cancel/death/battle stop).
 if(auto* DisplayMesh=GetMesh())
  DisplayMesh->SetRelativeTransform(PounceRestRelative,false,nullptr,ETeleportType::TeleportPhysics);
}

void AEGDemoCombatUnit::TickAttackPounce(float DeltaSeconds)
{
 if(!bPounceActive)return;
 auto* DisplayMesh=GetMesh();
 if(!DisplayMesh||!IsValid(GetHeroSkill())||!GetHeroSkill()->IsBusy()||PounceDuration<=KINDA_SMALL_NUMBER
    ||!IEGCombatant::Execute_IsAlive(this))
 {EndAttackPounce();return;}
 PounceElapsed+=DeltaSeconds;
 if(PounceElapsed>=PounceDuration){EndAttackPounce();return;}
 const FEGAttackPouncePresentation& P=AttackPounce;
 const float ContactFraction=FMath::Clamp(P.ContactFraction,.05f,.98f);
 const float ContactTime=ContactFraction*PounceDuration;
 const float LaunchTime=P.Seconds>KINDA_SMALL_NUMBER?FMath::Max(0.f,ContactTime-P.Seconds)
  :FMath::Clamp(P.LaunchFraction,0.f,FMath::Max(0.f,ContactFraction-.02f))*PounceDuration;
 const float ReturnTime=FMath::Clamp(FMath::Max(P.ReturnFraction,ContactFraction),0.f,1.f)*PounceDuration;
 FVector Offset=FVector::ZeroVector;
 FVector ScaleMultiplier=FVector(1.f);
 if(PounceElapsed>LaunchTime&&PounceElapsed<ReturnTime)
 {
  const bool bBeforeContact=PounceElapsed<=ContactTime&&ContactTime>LaunchTime;
  const float FlightT=bBeforeContact
   ?FMath::Clamp((PounceElapsed-LaunchTime)/FMath::Max(KINDA_SMALL_NUMBER,ContactTime-LaunchTime),0.f,1.f):1.f;
  // Forward offset eases out to Distance at contact, then eases home during recovery.
  const float Horizontal=bBeforeContact?P.Distance*(.5f-.5f*FMath::Cos(PI*FlightT))
   :P.Distance*(1.f-(.5f-.5f*FMath::Cos(PI*FMath::Clamp((PounceElapsed-ContactTime)
    /FMath::Max(KINDA_SMALL_NUMBER,ReturnTime-ContactTime),0.f,1.f))));
  // Parabolic arc: zero at launch and at contact, peak in the middle.
  const float Arc=bBeforeContact?4.f*FlightT*(1.f-FlightT):0.f;
  // Root-local space: the character root rotates with the actor, so the actor's facing
  // direction converted into root-local space is the visible mesh's forward.
  const FTransform RootWorld=GetRootComponent()?GetRootComponent()->GetComponentTransform():FTransform::Identity;
  const FVector LocalForward=RootWorld.InverseTransformVectorNoScale(GetActorForwardVector()).GetSafeNormal();
  Offset=LocalForward*Horizontal+FVector(0,0,P.Height*Arc);
  if(P.Squash>KINDA_SMALL_NUMBER)
  {
   if(bBeforeContact)
   {
    const float Stretch=P.Squash*.35f*Arc;
    ScaleMultiplier=FVector(1.f-Stretch*.5f,1.f-Stretch*.5f,1.f+Stretch);
   }
   else
   {
    // Landing squash: strongest at the contact frame, eased back to the exact scale.
    const float Recovery=FMath::Clamp((PounceElapsed-ContactTime)/FMath::Max(.02f,P.SquashSeconds),0.f,1.f);
    const float Bell=(1.f-Recovery)*(1.f-Recovery);
    ScaleMultiplier=FVector(1.f+P.Squash*.55f*Bell,1.f+P.Squash*.55f*Bell,1.f-P.Squash*Bell);
   }
  }
 }
 if(PounceElapsed>=ReturnTime){EndAttackPounce();return;}
 if(Offset.IsNearlyZero()&&ScaleMultiplier.Equals(FVector(1.f)))return;
 // Relative-only write: no dependence on world/root composition order, and the exact authored
 // transform is restored by EndAttackPounce().
 DisplayMesh->SetRelativeLocationAndRotation(PounceRestRelative.GetLocation()+Offset,
  PounceRestRelative.GetRotation(),false,nullptr,ETeleportType::TeleportPhysics);
 DisplayMesh->SetRelativeScale3D(PounceRestRelative.GetScale3D()*ScaleMultiplier);
}
