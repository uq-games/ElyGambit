#include "Skills/EGConfirmedSkillBehaviors.h"
#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"

namespace
{
 bool Basic(AEGChessUnit* U,UEGBattleContextComponent* B,FName Event,const FEGSkillActionContext& C,bool Projectile=false)
 {
  if(C.bUltimate)return false;
  auto* T=C.Target.Get();
  const bool Split=C.BasicAttackIndex==2&&U->GetDefinition()->HeroKind==EEGHeroKind::Pardofelis;
  if((Event==TEXT("Hit")||(Split&&Event==TEXT("HitSecond")))&&U->GetCombat()->IsValidEnemy(T)&&U->GetCombat()->IsInAttackRange(T))
  {
   if(Projectile)B->Launch(U,T,T->GetActorLocation(),C.Attack,EEGProjectileKind::BasicAttack,C.ActionId);
   else if(FEGCombatExecution::Damage(U,T,C.Attack*(Split?.5f:1.f),0,1,FGameplayEffectContextHandle(),C.ActionId,false,1.f,true))
   {if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(Event,C.ActionId,T);
    AEGChessNiagaraBurst::Swing(U->GetHeroSkill(),C.BasicAttackIndex);
    AEGChessNiagaraBurst::Play(U->GetHeroSkill(),TEXT("VisibleHit"),T->GetActorLocation(),U->GetActorRotation());
    B->BasicHit(U,T,C.ActionId);}
  }
  return true;
 }
}
void UEGPardoSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId||!C.ActionId.IsValid())return;
 auto* U=Unit.Get();auto* B=Battle.Get();
 if(Basic(U,B,Event,C)||Event!=TEXT("Release"))return;
 auto* T=C.Target.Get();if(!U->GetCombat()->IsValidEnemy(T))T=U->GetCombat()->FindNearestEnemy();
 if(!T)return;
 auto* Cat=B->FindCan(U->GetInstanceId());
 const bool FirstThrow=Cat==nullptr;
 if(!Cat)Cat=B->SpawnSummon(U->GetDefinition()->SummonDefinition,U->GetInstanceId(),Skill->GetLevel(),U->GetActorLocation(),true);
 if(!Cat||(!FirstThrow&&Cat->GetHeroSkill()->bInFlight))return;
 if(!Skill->CommitStigmataUltimate(C.ActionId)){if(FirstThrow&&IsValid(Cat))Cat->Destroy();return;}
 Cat->GetHeroSkill()->CancelAction();Cat->GetHeroSkill()->bInFlight=true;Cat->RefreshBoardCollision();Cat->SetActorHiddenInGame(false);
 B->Launch(FirstThrow?U:Cat,T,T->GetActorLocation(),C.Attack*C.Multiplier*U->GetAttributes()->GetSkillMultiplier(),EEGProjectileKind::ThrownSummon,C.ActionId,Cat);
}
void UEGPardoSkillBehavior::OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId)
{
 if(!IsCurrentBattle()||bCoinAwarded||Skill->bSummon||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const auto& R=Unit->GetDefinition()->GetSkillRules();
 CoinProgress=FMath::Min(CoinProgress+1,FMath::Max(1,R.CoinAttackThreshold));
 if(CoinProgress>=FMath::Max(1,R.CoinAttackThreshold))
 {
  // Consume before economy delegates can reenter. The battle also guards by instance ID.
  bCoinAwarded=true;
  if(!Battle->AwardBattleGold(Unit.Get(),R.CoinReward))bCoinAwarded=false;
 }
 Skill->OnStatusChanged.Broadcast();
}

void UEGCanSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId||!C.ActionId.IsValid()||C.bUltimate||Event!=TEXT("Hit"))return;
 auto* U=Unit.Get();auto* T=C.Target.Get();
 if(U->GetCombat()->IsValidEnemy(T)&&U->GetCombat()->IsInAttackRange(T))
  if(Battle->Damage(U,T,C.Attack,C.ActionId,true))
  {if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(Event,C.ActionId,T);Battle->BasicHit(U,T,C.ActionId);}
}

void UEGPardoSkillBehavior::TickBattle(float DeltaSeconds)
{
 if(!IsCurrentBattle()||!Unit->HasAuthority())return;
 auto* ASC=Unit->GetAbilitySystemComponent();
 const bool Active=Unit->IsAlive_Implementation()&&Skill->GetLevel()>=11&&Battle->HasLivingCan(Unit->GetInstanceId());
 if(!Active){if(CompanionSpeed.IsValid())ASC->RemoveActiveGameplayEffect(CompanionSpeed);CompanionSpeed.Invalidate();return;}
 if(CompanionSpeed.IsValid())return;
 auto Spec=ASC->MakeOutgoingSpec(UEGCompanionSpeedEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(Spec.IsValid()){Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Support.Multiplier")),1.f+Unit->GetDefinition()->GetSkillRules().LivingCanAttackSpeed);CompanionSpeed=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
}
void UEGPardoSkillBehavior::EndBattle()
{if(Unit.IsValid()&&CompanionSpeed.IsValid())Unit->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(CompanionSpeed);CompanionSpeed.Invalidate();Super::EndBattle();}
