#include "Skills/EGMonsterSkillBehavior.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatGeometry.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGMonsterSkillVisual.h"
#include "Actor/EGCorruptionSeed.h"

void UEGMonsterSkillBehavior::Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle)
{
 Super::Initialize(InSkill,InBattle);
 if(Unit.IsValid()&&Battle.IsValid())AEGMonsterSkillVisual::Attach(this,Unit.Get(),Battle.Get(),RoundId);
}
void UEGMonsterSkillBehavior::EndBattle()
{
 OnMonsterEffect.Clear();
 Super::EndBattle();
}
bool UEGMonsterSkillBehavior::AllowsAction(bool Ultimate) const
{
 auto* U=Unit.Get();if(!IsCurrentBattle()||!U||!U->GetDefinition()||!U->GetDefinition()->MonsterAttack)return false;
 const auto* P=U->GetDefinition()->MonsterAttack.Get();
 if(!Ultimate)return P->AttackType!=EEGMonsterAttackType::Heal||U->GetCombat()->FindHealingTarget()!=nullptr;
 if(P->Ultimate==EEGMonsterUltimate::Shield)return true;
 if(P->Ultimate==EEGMonsterUltimate::CrystalSeed)return U->GetCombat()->IsValidEnemy(U->GetCombat()->GetTarget());
 if(P->Ultimate==EEGMonsterUltimate::EnhancedShot)return U->GetCombat()->IsInAttackRange(U->GetCombat()->GetTarget());
 if(P->Ultimate==EEGMonsterUltimate::TeamSpeed)
  for(auto* T:Battle->Units())if(T!=U&&IEGCombatant::Execute_GetFaction(T)==IEGCombatant::Execute_GetFaction(U)&&IEGCombatant::Execute_IsAlive(T)&&T->GetHeroSkill()->IsTargetable())return true;
 return false;
}
void UEGMonsterSkillBehavior::ActionStarted(const FEGSkillActionContext& Context){bPendingUltimate=Context.bUltimate;}
void UEGMonsterSkillBehavior::ActionEnded(bool)
{if(bPendingUltimate&&Skill.IsValid())Skill->RefundUncommittedUltimate();bPendingUltimate=false;}
void UEGMonsterSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& Context)
{
 if(!IsCurrentBattle()||Context.RoundId!=RoundId||!Unit.IsValid())return;
 auto* U=Unit.Get();auto* T=Context.Target.Get();const auto* D=U->GetDefinition();const auto* P=D->MonsterAttack.Get();
 if(!U->HasAuthority()||!P||!IEGCombatant::Execute_IsAlive(U))return;
 if(Context.bUltimate)
 {
  if(Event!=TEXT("Release")||!bPendingUltimate)return;
  if(P->Ultimate==EEGMonsterUltimate::CrystalSeed)
  {
   if(!U->GetCombat()->IsValidEnemy(T)||!AEGCorruptionSeed::Spawn(Battle.Get(),U,T->GetActorLocation(),Context.Attack*P->UltimateMultiplier*U->GetAttributes()->GetSkillMultiplier(),P->SeedDelay,P->SeedRadiusCells,P->MaxActiveSeeds))Skill->RefundUncommittedUltimate();
   bPendingUltimate=false;return;
  }
  if(P->Ultimate==EEGMonsterUltimate::Shield)
  {
   const float Shield=Context.MaxHealth*P->ShieldHealthRatio*U->GetAttributes()->GetSkillMultiplier();
   const float Expected=Shield*U->GetAttributes()->GetShieldMultiplier();
   Skill->GrantSourceShield(TEXT("MonsterShield"),Shield,P->ShieldSeconds);
   if(IsCurrentBattle()&&Skill.IsValid()&&FMath::IsFinite(Expected)&&Expected>0.f&&FMath::IsFinite(P->ShieldSeconds)&&P->ShieldSeconds>0.f&&Skill->GetSourceShield(TEXT("MonsterShield"))>0.f)
    OnMonsterEffect.Broadcast(EEGMonsterEffectCue::Shield,U,U,RoundId,Context.ActionId,P->ShieldSeconds);
   bPendingUltimate=false;return;
  }
  if(P->Ultimate==EEGMonsterUltimate::TeamSpeed)
  {
   if(!AllowsAction(true)){Skill->RefundUncommittedUltimate();bPendingUltimate=false;return;}
   const float Bonus=P->TeamSpeedBonus*U->GetAttributes()->GetSkillMultiplier();
   const TWeakObjectPtr<UEGMonsterSkillBehavior> WeakThis(this);const TWeakObjectPtr<AEGChessUnit> WeakUnit(U);
   const FGuid EffectRound=RoundId,EffectAction=Context.ActionId;const float Seconds=P->TeamSpeedSeconds;
   Battle->ApplyTeamSpeed(U,Bonus,Seconds,true,false,[WeakThis,WeakUnit,EffectRound,EffectAction,Seconds](AEGChessUnit* Recipient)
   {
    auto* Self=WeakThis.Get();auto* Source=WeakUnit.Get();
    if(Self&&Self->IsCurrentBattle()&&Self->RoundId==EffectRound&&IsValid(Source)&&IEGCombatant::Execute_IsAlive(Source)&&IsValid(Recipient))
     Self->OnMonsterEffect.Broadcast(EEGMonsterEffectCue::TeamSpeed,Source,Recipient,EffectRound,EffectAction,Seconds);
   });
   bPendingUltimate=false;return;
  }
  if(P->Ultimate!=EEGMonsterUltimate::EnhancedShot)return;
  if(!U->GetCombat()->IsInAttackRange(T)){Skill->RefundUncommittedUltimate();bPendingUltimate=false;return;}
 }
 else
 {
  if(Event!=TEXT("Hit"))return;
  if(P->AttackType==EEGMonsterAttackType::Heal)
  {
   if(!U->GetCombat()->IsInHealingRange(T))return;
   const TWeakObjectPtr<AEGChessUnit> WeakTarget(T),WeakSource(U);
   const float Before=T->GetAttributes()->GetHealth();
   Battle->Heal(U,T,FMath::Min(T->GetAttributes()->GetMaxHealth()*P->HealHealthRatio,T->GetAttributes()->GetMaxHealth()-Before));
   if(!IsCurrentBattle()||!WeakSource.IsValid()||!WeakTarget.IsValid())return;
   const float Restored=WeakTarget->GetAttributes()->GetHealth()-Before;
   Battle->EffectiveBasicHeal(WeakSource.Get(),Context.ActionId,Restored);
   if(Restored>0.f&&IsCurrentBattle()&&WeakSource.IsValid()&&IEGCombatant::Execute_IsAlive(WeakSource.Get())&&WeakTarget.IsValid())
    OnMonsterEffect.Broadcast(EEGMonsterEffectCue::Heal,WeakSource.Get(),WeakTarget.Get(),RoundId,Context.ActionId,0.f);
   return;
  }
  if(!U->GetCombat()->IsValidEnemy(T))return;
  if(P->AttackType==EEGMonsterAttackType::Melee)
  {
   FVector ContactPoint=EGCombatGeometry::ClosestPoint(U->GetActorLocation(),T);
   const auto* Device=Cast<AEGVillVEmplacement>(T);
   if(Device)Device->DistanceToCombatSurface(U->GetActorLocation(),&ContactPoint);
   FVector Direction=(T->GetActorLocation()-U->GetActorLocation()).GetSafeNormal2D();
   if(Direction.IsNearlyZero())Direction=(ContactPoint-U->GetActorLocation()).GetSafeNormal2D();
   if(U->GetCombat()->IsInAttackReach(T,P->ContactTolerance)&&FVector::DotProduct(Direction,U->GetActorForwardVector())>=.5f)
   {
    const FVector Contact=ContactPoint+FVector(0,0,T->GetDefinition()->GetPresentationHeight()*.45f-88.f)-(Device||EGCombatGeometry::HasLargeFootprint(T)?FVector::ZeroVector:Direction*UEGCombatComponent::BodyRadius(T));
    if(Battle->Damage(U,T,Context.Attack,Context.ActionId,true))
    {Battle->BasicHit(U,T,Context.ActionId);AEGPresentationBurst::SpawnContact(U->GetWorld(),Contact,D->GetAttackMaterial().LoadSynchronous(),RoundId);}
   }
   return;
  }
  if(!U->GetCombat()->IsInAttackRange(T))return;
 }
 auto* Shot=U->GetWorld()->SpawnActor<AEGSkillProjectile>(U->GetActionOrigin(),FRotator::ZeroRotator);
 if(!Shot){if(Context.bUltimate){Skill->RefundUncommittedUltimate();bPendingUltimate=false;}return;}
 Shot->Battle=Battle;Shot->Source=U;Shot->Target=T;Shot->Round=RoundId;Shot->AttackId=Context.ActionId;
 Shot->Kind=EEGProjectileKind::MonsterBasic;Shot->bMonsterUltimate=Context.bUltimate;
 Shot->RawDamage=Context.Attack*(Context.bUltimate?P->UltimateMultiplier*U->GetAttributes()->GetSkillMultiplier():1.f);
 Shot->Start=Shot->GetActorLocation();Shot->Destination=T->GetActorLocation();bPendingUltimate=false;
}
