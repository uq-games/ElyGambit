#include "Skills/EGCorruptionBossBehavior.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatGeometry.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Game/EGChessGameMode.h"

bool UEGCorruptionBossBehavior::AllowsAction(bool bUltimate) const
{
 const auto* Boss=Unit.IsValid()?Unit->FindComponentByClass<UEGCorruptionCombatComponent>():nullptr;
 return Super::AllowsAction(bUltimate)&&(!bUltimate||(!bPulsePending&&!bPulseResolving&&Boss&&Boss->GetBossPhase()==EEGCorruptionBossPhase::First));
}

int32 UEGCorruptionBossBehavior::PresentationIndex(bool bUltimate,int32 DefaultIndex) const
{
 // Index four is the player's charged attack. The boss cycles the four ordinary shots.
 return bUltimate?0:(DefaultIndex==4?0:DefaultIndex);
}

void UEGCorruptionBossBehavior::ActionStarted(const FEGSkillActionContext& Context)
{
 bCurrentUltimate=Context.bUltimate;
 if(Context.bUltimate)
 {
  CurrentUltimateAction=Context.ActionId;bPulseCommitted=false;bHasLockedPoint=false;
  if(Unit.IsValid()&&Unit->GetCombat()->IsValidEnemy(Context.Target.Get()))
  {LockedPoint=Context.Target->GetActorLocation();bHasLockedPoint=!LockedPoint.ContainsNaN();}
  // Initialize only common action bookkeeping; never initialize the player rain.
  FEGSkillActionContext BasicContext=Context;BasicContext.bUltimate=false;
  Super::ActionStarted(BasicContext);
  return;
 }
 Super::ActionStarted(Context);
 if(Unit.IsValid())if(auto* Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>())Boss->RememberBasicAttack(Context.ActionId);
}

void UEGCorruptionBossBehavior::ActionEvent(FName Event,const FEGSkillActionContext& Context)
{
 if(!Context.bUltimate){if(Event!=TEXT("BoardBackstep"))Super::ActionEvent(Event,Context);return;}
 if(Event!=TEXT("Release")||!bCurrentUltimate||bPulseCommitted||Context.ActionId!=CurrentUltimateAction||
    !bHasLockedPoint||!IsCurrentBattle()||Context.RoundId!=RoundId||!Unit.IsValid()||!Unit->HasAuthority()||
    !IEGCombatant::Execute_IsAlive(Unit.Get())||!Skill.IsValid())return;
 auto* Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
 const auto* Mode=Unit->GetWorld()?Unit->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Board=Mode?Mode->GetBoard():nullptr;
 if(!Boss||Boss->GetBossPhase()!=EEGCorruptionBossPhase::First||!Board)return;
 const float Spacing=FVector::Dist2D(Board->CellToWorld(FIntPoint(0,0)),Board->CellToWorld(FIntPoint(0,1)));
 const float Damage=Context.Attack*Boss->FirstPulseDamageMultiplier*Unit->GetAttributes()->GetSkillMultiplier();
 const float Radius=Spacing*Boss->FirstPulseRadiusCells,Duration=Boss->FirstPulseWarningSeconds;
 if(!FMath::IsFinite(Damage)||Damage<0.f||!FMath::IsFinite(Radius)||Radius<=0.f||!FMath::IsFinite(Duration)||Duration<=0.f)return;
 if(!Skill->CommitStigmataUltimate(Context.ActionId)||!IsCurrentBattle()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get())||
    !bCurrentUltimate||CurrentUltimateAction!=Context.ActionId||!Skill.IsValid()||Skill->ActionId!=Context.ActionId)return;
 Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
 if(!Boss||Boss->GetBossPhase()!=EEGCorruptionBossPhase::First)return;
 bPulseCommitted=true;bPulsePending=true;PulseAction=Context.ActionId;PulsePoint=LockedPoint;
 PulseRadius=Radius;PulseDuration=PulseRemaining=Duration;PulseDamage=Damage;
 OnFirstPulse.Broadcast(EEGCorruptionFirstPulseEvent::Started,RoundId,PulseAction,PulsePoint,PulseRadius,PulseDuration);
}

void UEGCorruptionBossBehavior::ActionEnded(bool bCompleted)
{
 if(bCurrentUltimate)
 {
  if(!bPulseCommitted&&Skill.IsValid())Skill->RefundUncommittedUltimate();
  if(!bCompleted&&bPulsePending&&PulseAction==CurrentUltimateAction)CancelPulse();
  bCurrentUltimate=false;CurrentUltimateAction.Invalidate();bHasLockedPoint=false;
 }
 Super::ActionEnded(bCompleted);
}

void UEGCorruptionBossBehavior::TickBattle(float DeltaSeconds)
{
 Super::TickBattle(DeltaSeconds);
 if(!bPulsePending||!IsCurrentBattle()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const auto* Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
 if(!Boss||Boss->GetBossPhase()!=EEGCorruptionBossPhase::First){CancelPulse();return;}
 PulseRemaining-=FMath::Max(0.f,DeltaSeconds);if(PulseRemaining>0.f)return;
 const FGuid CastRound=RoundId,CastAction=PulseAction;
 bPulsePending=false;bPulseResolving=true;
 const auto Victims=Battle->Units();TSet<AEGChessUnit*> Visited;
 for(auto* Target:Victims)
 {
  if(!bPulseResolving||!IsCurrentBattle()||RoundId!=CastRound||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
  if(!Unit->GetCombat()->IsValidEnemy(Target)||Visited.Contains(Target)||EGCombatGeometry::PointDistanceSquared(PulsePoint,Target)>FMath::Square(PulseRadius))continue;
  Visited.Add(Target);Battle->Damage(Unit.Get(),Target,PulseDamage,CastAction);
 }
 if(!bPulseResolving||!IsCurrentBattle()||RoundId!=CastRound||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 bPulseResolving=false;PulseAction.Invalidate();
 OnFirstPulse.Broadcast(EEGCorruptionFirstPulseEvent::Resolved,CastRound,CastAction,PulsePoint,PulseRadius,PulseDuration);
}

void UEGCorruptionBossBehavior::CancelPulse()
{
 if(!bPulsePending&&!bPulseResolving)return;
 const FGuid Action=PulseAction;bPulsePending=bPulseResolving=false;PulseAction.Invalidate();
 OnFirstPulse.Broadcast(EEGCorruptionFirstPulseEvent::Cancelled,RoundId,Action,PulsePoint,PulseRadius,PulseDuration);
}

void UEGCorruptionBossBehavior::EndBattle()
{
 CancelPulse();OnFirstPulse.Clear();bCurrentUltimate=false;bPulseCommitted=false;CurrentUltimateAction.Invalidate();
 Super::EndBattle();
}
