#include "Components/EGElysiaOriginKnockupComponent.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"

UEGElysiaOriginKnockupComponent::UEGElysiaOriginKnockupComponent()
{PrimaryComponentTick.bCanEverTick=true;}

UEGElysiaOriginKnockupComponent* UEGElysiaOriginKnockupComponent::Start(AEGChessUnit* Source,AEGChessUnit* Target,
 UEGBattleContextComponent* Battle,FGuid Round,FGuid Action,float Height,float Duration)
{
 if(!IsValid(Source)||!IsValid(Target)||!Source->HasAuthority()||!Target->HasAuthority()||
    !Battle||!Battle->IsCombatRunning()||Battle->GetRoundId()!=Round||!Action.IsValid()||
    !Source->GetCombat()->IsValidEnemy(Target)||!Target->GetHeroSkill()->CanReceiveHostileControl()||
    !FMath::IsFinite(Height)||Height<=0.f||!FMath::IsFinite(Duration)||Duration<=0.f)return nullptr;
 if(const auto* Boss=Target->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&Boss->IsBoss())return nullptr;
 // Never seize a target already under another instance's movement control.
 if(Target->FindComponentByClass<UEGElysiaOriginKnockupComponent>()||Target->GetCombat()->HasExternalActionBlock()||Target->GetCombat()->IsBoardLandingPending())return nullptr;
 auto* Move=Target->GetCharacterMovement();
 if(!Move||Move->MovementMode==MOVE_None||Move->IsFalling())return nullptr;
 auto* Reaction=NewObject<UEGElysiaOriginKnockupComponent>(Target);
 if(!Reaction)return nullptr;
 Reaction->SourceUnit=Source;Reaction->BattleContext=Battle;Reaction->RoundId=Round;Reaction->ActionId=Action;
 Reaction->BlockToken=FGuid::NewGuid();Reaction->GroundZ=Target->GetActorLocation().Z;
 Reaction->PeakHeight=Height;Reaction->ReactionDuration=Duration;
 Reaction->PreviousMode=Move->MovementMode;Reaction->PreviousCustomMode=Move->CustomMovementMode;
 Reaction->bOwnsReaction=true;Target->GetCombat()->AddExternalActionBlock(Reaction->BlockToken);
 if(Target->GetHeroSkill()->IsActionRunning())Target->GetHeroSkill()->CancelAction();
 if(!IsValid(Target)||!IsValid(Source)||!Battle->IsCombatRunning()||Battle->GetRoundId()!=Round||
    !IEGCombatant::Execute_IsAlive(Target)||!IEGCombatant::Execute_IsAlive(Source))
 {Reaction->FinishReaction();return nullptr;}
 Move->StopMovementImmediately();Move->SetMovementMode(MOVE_None);Reaction->bOwnsMovement=true;
 Target->AddInstanceComponent(Reaction);Reaction->RegisterComponent();
 Target->OnCombatStateChanged.AddUniqueDynamic(Reaction,&ThisClass::OnTargetStateChanged);
 return Reaction;
}
void UEGElysiaOriginKnockupComponent::OnTargetStateChanged(EEGUnitState State)
{
 // Revive owns the next landing and may complete before the next component
 // tick. Release our pending landing now rather than replacing its callback.
 if(State==EEGUnitState::Reviving)CompleteLanding(true);
}
void UEGElysiaOriginKnockupComponent::TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,TickType,Function);
 auto* Target=Cast<AEGChessUnit>(GetOwner());auto* Source=SourceUnit.Get();auto* Battle=BattleContext.Get();
 if(bAwaitingMovementRelease)
 {
  const bool bRecoverable=IsValid(Target)&&Target->GetHeroSkill()->IsRecoverable();
  if(!IsValid(Target)||Target->IsDeathPending()||(!IEGCombatant::Execute_IsAlive(Target)&&!bRecoverable)||
     !Battle||!Battle->IsBattleActive()||Battle->GetRoundId()!=RoundId||
     !Target->GetCombat()->HasExternalActionBlockOtherThan(BlockToken)||Target->GetCharacterMovement()->MovementMode!=MOVE_None)
   CompleteLanding(false);
  return;
 }
 if(bWaitingLanding)
 {
  if(!IsValid(Target)||!IEGCombatant::Execute_IsAlive(Target)||
     !Battle||!Battle->IsBattleActive()||Battle->GetRoundId()!=RoundId)CompleteLanding(true);
  return;
 }
 if(!bOwnsReaction||!IsValid(Target)||!IsValid(Source)||!IEGCombatant::Execute_IsAlive(Source)||
    !IEGCombatant::Execute_IsAlive(Target)||!Battle||!Battle->IsCombatRunning()||Battle->GetRoundId()!=RoundId)
 {FinishReaction();return;}
 Elapsed+=FMath::Max(0.f,Delta);
 const float Alpha=FMath::Clamp(Elapsed/ReactionDuration,0.f,1.f);
 FVector Location=Target->GetActorLocation();Location.Z=GroundZ+PeakHeight*FMath::Sin(PI*Alpha);
 // The unit keeps its occupied XY footprint for the entire reaction. Adjacent
 // one-cell capsules touch laterally, so sweeping a vertical lift can report a
 // blocking start overlap even though no board-plane movement is requested.
 // Board-safe landing is resolved separately in FinishReaction.
 Target->SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);
 if(Alpha>=1.f)FinishReaction();
}
void UEGElysiaOriginKnockupComponent::FinishReaction()
{
 if(bAwaitingMovementRelease){CompleteLanding(false);return;}
 if(bWaitingLanding)
 {
  auto* Target=Cast<AEGChessUnit>(GetOwner());auto* Battle=BattleContext.Get();
  if(!Target||!IEGCombatant::Execute_IsAlive(Target)||!Battle||!Battle->IsBattleActive()||Battle->GetRoundId()!=RoundId)CompleteLanding(true);
  return;
 }
 if(!bOwnsReaction)return;
 bOwnsReaction=false;
 auto* Target=Cast<AEGChessUnit>(GetOwner());auto* Battle=BattleContext.Get();
 if(Target&&bOwnsMovement&&Target->GetCharacterMovement()&&Target->GetCharacterMovement()->MovementMode==MOVE_None&&
    IEGCombatant::Execute_IsAlive(Target)&&!Target->IsDeathPending()&&Battle&&Battle->IsBattleActive()&&Battle->GetRoundId()==RoundId)
 {
  FVector Location=Target->GetActorLocation();Location.Z=GroundZ;bWaitingLanding=true;
  TWeakObjectPtr<UEGElysiaOriginKnockupComponent> WeakThis(this);
  const bool bLanded=Target->GetCombat()->CompleteOrDeferBoardLanding(Location,RoundId,[WeakThis]()
   {if(WeakThis.IsValid())WeakThis->CompleteLanding(false);});
  // The callback may already have destroyed this component on an immediate landing.
  if(!bLanded&&WeakThis.IsValid()&&!Target->GetCombat()->IsBoardLandingPending())WeakThis->CompleteLanding(true);
  return;
 }
 CompleteLanding(true);
}
void UEGElysiaOriginKnockupComponent::CompleteLanding(bool bForced)
{
 const bool bHadPending=bWaitingLanding;bWaitingLanding=false;bOwnsReaction=false;bAwaitingMovementRelease=false;
 if(auto* Target=Cast<AEGChessUnit>(GetOwner()))
 {
  if(bForced&&bHadPending)Target->GetCombat()->CancelBoardLanding();
  if(auto* Move=Target->GetCharacterMovement();bOwnsMovement&&Move&&Move->MovementMode==MOVE_None)
  {
   if(bForced){FVector Location=Target->GetActorLocation();Location.Z=GroundZ;Target->SetActorLocation(Location,false,nullptr,ETeleportType::TeleportPhysics);}
   const bool bRecoverable=Target->GetHeroSkill()->IsRecoverable();
   if((IEGCombatant::Execute_IsAlive(Target)||bRecoverable)&&!Target->IsDeathPending())
   {
    const auto* Battle=BattleContext.Get();
    if(Target->GetCombat()->HasExternalActionBlockOtherThan(BlockToken))
    {
     // Another scoped controller must release its own action block before we
     // restore the movement mode that this reaction temporarily disabled.
     if(!bEndingPlay&&Battle&&Battle->IsBattleActive()&&Battle->GetRoundId()==RoundId)
     {bAwaitingMovementRelease=true;return;}
    }
    else Move->SetMovementMode(PreviousMode,PreviousCustomMode);
   }
  }
  if(auto* Combat=Target->GetCombat())Combat->RemoveExternalActionBlock(BlockToken);
 }
 if(IsRegistered()&&!bEndingPlay)DestroyComponent();
}
void UEGElysiaOriginKnockupComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 bEndingPlay=true;
 if(auto* Target=Cast<AEGChessUnit>(GetOwner()))Target->OnCombatStateChanged.RemoveDynamic(this,&ThisClass::OnTargetStateChanged);
 CompleteLanding(true);Super::EndPlay(Reason);
}
