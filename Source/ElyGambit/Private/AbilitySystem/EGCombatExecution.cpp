#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/Effects/EGDamageEffect.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "EGGameplayTags.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Game/EGChessGameMode.h"
#include "Data/EGStigmataBalance.h"
#include "Engine/World.h"

namespace
{
 bool Apply(AEGChessUnit* Source,AEGChessUnit* Target,float Amount,float Minimum,bool bHealing,float Level,FGameplayEffectContextHandle Context,FGuid ActionId,bool bLinkedDamage=false,float DefenseScale=1.f,bool bEnvironment=false,bool bExecution=false,bool bStigmata=false,bool bPeriodic=false,FName StigmataId=NAME_None,bool bBasicAttack=false)
 {
  if(!IsValid(Target)||!Target->HasAuthority()||!FMath::IsFinite(Amount)||Amount<=0||!FMath::IsFinite(DefenseScale)||DefenseScale<0||DefenseScale>1||!IEGCombatant::Execute_IsAlive(Target)||(!bHealing&&!Target->GetHeroSkill()->IsTargetable()))return false;
  if(IsValid(Source)&&(!Source->HasAuthority()||Source->GetWorld()!=Target->GetWorld()))return false;
  auto* Battle=Target->GetHeroSkill()->Battle.Get();
  if(!Battle)if(auto* Mode=Target->GetWorld()->GetAuthGameMode<AEGChessGameMode>())Battle=Mode->BattleRules.Get();
  if(Battle&&Battle->IsBattleActive()&&!Battle->IsCombatRunning())return false;
  auto* Stigmata=Battle&&Battle->IsBattleActive()?Battle->GetStigmata():nullptr;
  // Keep the unmodified hit and each explicit multiplier in application order.
  // These are only used after the hit has actually settled.
  TArray<TPair<FName,float>> BonusParts;
  float UnmodifiedAmount=0.f;
  const FGuid EffectiveAction=ActionId.IsValid()?ActionId:IsValid(Source)?Source->GetHeroSkill()->ActionId:FGuid();
  if(!bHealing&&!bLinkedDamage&&IsValid(Source))
  {
   if(!bPeriodic&&!bStigmata&&!bExecution&&!bEnvironment)
   {
    auto* Behavior=Target->GetHeroSkill()->GetBehavior();bool bNewlyEvaded=false;
    if(Behavior&&Behavior->EvadeDirectAttack(Source,EffectiveAction,bBasicAttack,bNewlyEvaded))
    {
     if(bNewlyEvaded)
     {
      TWeakObjectPtr<UEGHeroSkillBehavior> WeakBehavior(Behavior);
      TWeakObjectPtr<AEGChessUnit> WeakSource(Source),WeakTarget(Target);
      if(Stigmata)Stigmata->OnSuccessfulEvade(Source,Target,Amount,DefenseScale);
      if(WeakBehavior.IsValid()&&WeakSource.IsValid()&&WeakTarget.IsValid())WeakBehavior->OnEvadeCommitted(WeakSource.Get(),EffectiveAction);
     }
     return false;
    }
   }
   if(!bStigmata&&Stigmata&&Stigmata->TryEvade(Source,Target,bPeriodic,Amount,DefenseScale))return false;
  }
  // Linked damage is already resolved from an earlier hit. Environment and
  // executions have no ordinary outgoing damage multiplier.
  if(IsValid(Source))
  {
   if(bHealing)Amount*=Source->GetAttributes()->GetHealingMultiplier();
   else if(!bLinkedDamage&&!bEnvironment&&!bExecution)
   {
    Amount*=Source->GetAttributes()->GetDamageMultiplier();
    // A round snapshot applies once to enemy-originated damage. Linked damage
    // is already settled and therefore intentionally bypasses this branch.
    if(Battle&&Battle->IsBattleActive()&&IEGCombatant::Execute_GetFaction(Source)==EEGFaction::Enemy)
     Amount*=Battle->GetEnemyDamageMultiplier();
    if(const auto* Boss=Source->FindComponentByClass<UEGCorruptionCombatComponent>())Amount*=Boss->GetOutgoingDamageMultiplier();
    UnmodifiedAmount=Amount;
    if(Stigmata)
    {
     Amount*=Stigmata->UnityFinalDamageMultiplier(Source);
     if(Amount>UnmodifiedAmount)BonusParts.Emplace(TEXT("Unity"),Amount-UnmodifiedAmount);
    }
   }
  }
  if(!FMath::IsFinite(Amount))return false;
  if(!bHealing&&!bExecution&&Stigmata)Amount=Stigmata->ModifyDamage(Source,Target,EffectiveAction,Amount,bLinkedDamage,bStigmata,&BonusParts);
  if(!FMath::IsFinite(Amount))return false;
  auto* ASC=IsValid(Source)?Source->GetAbilitySystemComponent():Target->GetAbilitySystemComponent();
  auto Spec=ASC->MakeOutgoingSpec(UEGDamageEffect::StaticClass(),Level,Context.IsValid()?Context:ASC->MakeEffectContext());if(!Spec.IsValid())return false;
  if(bExecution)
  {
   // Re-read after hit interception. Only this spec bypasses shield absorption in AttributeSet.
   Amount=Target->GetAttributes()->GetHealth();
   if(!FMath::IsFinite(Amount)||Amount<=0)return false;
   Spec.Data->AddDynamicAssetTag(EGTags::DamageExecution);
  }
  FEGCombatResolution Receipt;Receipt.SourceId=IsValid(Source)?Source->GetInstanceId():FGuid();Receipt.TargetId=Target->GetInstanceId();
  Receipt.bLinkedDamage=bLinkedDamage;
  Receipt.bStigmataDamage=bStigmata;
  Receipt.bPeriodicDamage=bPeriodic;
  Receipt.bEnvironmentDamage=bEnvironment;
  Receipt.bExecution=bExecution;
  Receipt.ActionId=EffectiveAction;Receipt.RawAmount=Amount;Receipt.bHealing=bHealing;
  const float DefenseConstant=FMath::Max(.01f,UEGStigmataBalanceSettings::Get().DefenseConstant);
  const float DefenseFactor=DefenseConstant/(DefenseConstant+FMath::Max(0.f,Target->GetAttributes()->GetDefense())*DefenseScale);
  Receipt.AfterDefense=(bHealing||bLinkedDamage||bExecution)?Amount:FMath::Max(Minimum,Amount*DefenseFactor);
  const float Before=Target->GetAttributes()->GetHealth(),Shield=Target->GetAttributes()->GetShield();
  TWeakObjectPtr<AEGChessUnit> WeakTarget(Target);
  float ProtectedAmount=(bHealing||bExecution)?Amount:Target->GetHeroSkill()->ResolveProtection(Receipt.AfterDefense,Receipt);
  if(!WeakTarget.IsValid())return true;
  const float PreFilterProtected=ProtectedAmount;
  const bool bDaybreakShield=!bHealing&&!bExecution&&!bLinkedDamage&&Stigmata&&Stigmata->Has(TEXT("Daybreak"))&&Stigmata->IsPlayerHero(Source);
  const float ShieldMultiplier=bDaybreakShield?FMath::Max(1.f,1.f+Stigmata->DaybreakShieldBonus):1.f;
  TWeakObjectPtr<UEGCorruptionCombatComponent> Corruption=!bHealing?Target->FindComponentByClass<UEGCorruptionCombatComponent>():nullptr;
  const bool bHadCorruption=Corruption.IsValid();
  const bool bWasBoss=bHadCorruption&&Corruption->IsBoss();
  const bool bBossPhaseBefore=bWasBoss&&Corruption->IsPhaseTwo();
  const bool bTransitionWasPending=bWasBoss&&Corruption->HasPendingTransition();
  const float BarrierBefore=bWasBoss?Corruption->GetBarrierHealth():0.f;
  bool bOwnsTransition=false,bOwnsBarrierBreak=false;
  float PhaseShieldAbsorbed=0.f;
  if(Corruption.IsValid())
  {
   ProtectedAmount=Corruption->FilterDamage(Source,Receipt.ActionId,ProtectedAmount,ShieldMultiplier,&PhaseShieldAbsorbed,bExecution);
   if(!WeakTarget.IsValid())return true;
   bOwnsTransition=!bTransitionWasPending&&Corruption->HasPendingTransition();
   bOwnsBarrierBreak=BarrierBefore>0.f&&Corruption->GetBossPhase()==EEGCorruptionBossPhase::Barrier&&Corruption->GetBarrierHealth()<=0.f;
  }
  if(bDaybreakShield&&ProtectedAmount>0&&Target->GetAttributes()->GetShield()>0)
  {
   const float Absorbed=Target->GetHeroSkill()->AbsorbShield(ProtectedAmount*ShieldMultiplier);
   if(!WeakTarget.IsValid())return true;
   ProtectedAmount=FMath::Max(0.f,ProtectedAmount-Absorbed/ShieldMultiplier);
   Spec.Data->AddDynamicAssetTag(EGTags::DamageExecution);
  }
  // A blocked contact is still a committed attack, but causes no Health GE or victim mana.
  if(ProtectedAmount>0&&IsValid(ASC))
  {
   Spec.Data->SetSetByCallerMagnitude(EGTags::Damage,bHealing?Amount:-ProtectedAmount);
   ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data,Target->GetAbilitySystemComponent());
  }
  if(WeakTarget.IsValid())
  {
   Receipt.HealthDelta=WeakTarget->GetAttributes()->GetHealth()-Before;
   // Death clears status effects; that cleanup is not shield absorption by an execution.
   Receipt.ShieldAbsorbed=(bExecution?0.f:FMath::Max(0.f,Shield-WeakTarget->GetAttributes()->GetShield()))+PhaseShieldAbsorbed;
   if(Corruption.IsValid())Corruption->CommitPhaseShield(bOwnsTransition,bOwnsBarrierBreak);
   Receipt.bFatal=!IEGCombatant::Execute_IsAlive(WeakTarget.Get())&&!WeakTarget->GetHeroSkill()->IsRecoverable();
   const bool bStablePhase=!bHadCorruption||(Corruption.IsValid()&&Corruption->IsBoss()==bWasBoss&&(!bWasBoss||Corruption->IsPhaseTwo()==bBossPhaseBefore));
   const float Actual=FMath::Max(0.f,-Receipt.HealthDelta)+Receipt.ShieldAbsorbed;
   float DaybreakExtra=0.f;
   if(bDaybreakShield&&BarrierBefore>0.f&&PhaseShieldAbsorbed>0.f)
   {
    // The wall is an independent pool, so cap both the boosted and ordinary
    // absorption at its pre-hit durability. This also works on the break hit.
    DaybreakExtra=FMath::Max(0.f,PhaseShieldAbsorbed-FMath::Min(BarrierBefore,PreFilterProtected));
   }
   else if(bDaybreakShield&&bStablePhase&&PhaseShieldAbsorbed<=0.f)
   {
    const float ExpectedShield=FMath::Min(FMath::Max(0.f,Shield),PreFilterProtected*ShieldMultiplier);
    const float ExpectedHealth=FMath::Min(FMath::Max(0.f,Before),FMath::Max(0.f,PreFilterProtected-ExpectedShield/ShieldMultiplier));
    const float WithoutDaybreak=FMath::Min(FMath::Max(0.f,Before)+FMath::Max(0.f,Shield),PreFilterProtected);
    if(FMath::IsNearlyEqual(Actual,ExpectedShield+ExpectedHealth,.05f))DaybreakExtra=FMath::Max(0.f,Actual-WithoutDaybreak);
   }
   const bool bRecordSeparateDaybreak=StigmataId!=TEXT("Daybreak")&&DaybreakExtra>0.f;
   if(Stigmata&&bRecordSeparateDaybreak)Stigmata->RecordAdditionalDamage(TEXT("Daybreak"),DaybreakExtra);
   // Allocate only the actual additional loss caused by explicit hit multipliers.
   // Base damage is applied first, followed by the modifiers in their existing
   // order. Defense, protection, remaining shield and health all cap the result.
   // Boss phase transitions and Daybreak shield damage have different
   // semantics. Do not infer a modifier contribution for those cases.
   float BonusAttributed=0.f;
   if(Stigmata&&!bHealing&&!bLinkedDamage&&!bExecution&&!bEnvironment&&
      bStablePhase&&PhaseShieldAbsorbed<=0.f&&!(bDaybreakShield&&Shield>0.f)&&BonusParts.Num()>0&&Receipt.BubbleAbsorbed<=0.f&&UnmodifiedAmount>0.f)
   {
    const float ProtectionFactor=Receipt.AfterDefense>0.f?FMath::Clamp(1.f-Receipt.ProtectionPrevented/Receipt.AfterDefense,0.f,1.f):0.f;
    const auto Settled=[&](float Raw){return FMath::Min(FMath::Max(0.f,Before)+FMath::Max(0.f,Shield),FMath::Max(Minimum,Raw*DefenseFactor)*ProtectionFactor);};
    float CheckRaw=UnmodifiedAmount;
    for(const auto& Part:BonusParts)CheckRaw+=Part.Value;
    if(FMath::IsNearlyEqual(CheckRaw,Amount,.01f)&&FMath::IsNearlyEqual(Settled(CheckRaw),Actual,.05f))
    {
     float Raw=UnmodifiedAmount,Previous=Settled(Raw);
     for(const auto& Part:BonusParts)
     {
      Raw+=Part.Value;const float Current=Settled(Raw);
      const float Attributed=FMath::Max(0.f,Current-Previous);
      Stigmata->RecordAdditionalDamage(Part.Key,Attributed);BonusAttributed+=Attributed;Previous=Current;
     }
    }
   }
   if(Stigmata&&bStigmata)Stigmata->RecordDamage(StigmataId,Receipt,(bRecordSeparateDaybreak?DaybreakExtra:0.f)+BonusAttributed);
   WeakTarget->LastCombatResolution=Receipt;WeakTarget->OnCombatResolved.Broadcast(Receipt);
  }
  return true;
 }
}
bool FEGCombatExecution::Damage(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,float Minimum,float Level,FGameplayEffectContextHandle Context,FGuid ActionId,bool bLinkedDamage,float DefenseScale,bool bBasicAttack)
{return Apply(Source,Target,Raw,FMath::Max(0.f,Minimum),false,Level,Context,ActionId,bLinkedDamage,DefenseScale,false,false,false,false,NAME_None,bBasicAttack);}
bool FEGCombatExecution::StigmataDamage(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,FGuid ActionId,bool bPeriodic,FName StigmataId)
{return Apply(Source,Target,Raw,0.f,false,1.f,FGameplayEffectContextHandle(),ActionId,false,1.f,false,false,true,bPeriodic,StigmataId);}
bool FEGCombatExecution::Heal(AEGChessUnit* Target,float Amount){return Apply(Target,Target,Amount,0,true,1,FGameplayEffectContextHandle(),FGuid());}
bool FEGCombatExecution::Heal(AEGChessUnit* Source,AEGChessUnit* Target,float Amount){return Apply(Source,Target,Amount,0,true,1,FGameplayEffectContextHandle(),FGuid());}
bool FEGCombatExecution::EnvironmentDamage(AEGChessUnit* Target,float Raw,FGuid Id)
{return Apply(nullptr,Target,Raw,0,false,1,FGameplayEffectContextHandle(),Id,false,1,true);}
bool FEGCombatExecution::Execute(AEGChessUnit* Source,AEGChessUnit* Target,FGuid ActionId)
{
 if(!IsValid(Source)||!IsValid(Target)||!ActionId.IsValid()||!IEGCombatant::Execute_IsAlive(Source)
    ||!Source->GetCombat()->IsValidEnemy(Target))return false;
 return Apply(Source,Target,Target->GetAttributes()->GetHealth(),0,false,1,FGameplayEffectContextHandle(),ActionId,false,1,false,true);
}
