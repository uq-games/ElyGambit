#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/Effects/EGTeamSpeedEffect.h"
#include "AbilitySystemComponent.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGStigmataBalance.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGMatchManager.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "EGGameplayTags.h"

namespace
{
 EEGHeroKind BaseKind(EEGHeroKind Kind)
 {
  if(Kind==EEGHeroKind::ElysiaHerrscher)return EEGHeroKind::Elysia;
  if(Kind==EEGHeroKind::CollapsedHua)return EEGHeroKind::Hua;
  if(Kind==EEGHeroKind::GriseoAdult)return EEGHeroKind::Griseo;
  return Kind;
 }
}
UEGStigmataAttributeEffect::UEGStigmataAttributeEffect()
{
 DurationPolicy=EGameplayEffectDurationType::Infinite;
 auto Add=[this](FGameplayAttribute Attribute,FName Name)
 {
  FGameplayModifierInfo Modifier;Modifier.Attribute=Attribute;Modifier.ModifierOp=EGameplayModOp::Multiplicitive;
  FSetByCallerFloat Value;Value.DataName=Name;Modifier.ModifierMagnitude=FGameplayEffectModifierMagnitude(Value);Modifiers.Add(Modifier);
 };
 Add(UEGAttributeSet::GetMaxHealthAttribute(),TEXT("Stigmata.Health"));
 Add(UEGAttributeSet::GetAttackPowerAttribute(),TEXT("Stigmata.Attack"));
 Add(UEGAttributeSet::GetSkillMultiplierAttribute(),TEXT("Stigmata.Skill"));
 Add(UEGAttributeSet::GetDamageMultiplierAttribute(),TEXT("Stigmata.Damage"));
}
UEGStigmataBattleComponent::UEGStigmataBattleComponent(){PrimaryComponentTick.bCanEverTick=true;}
bool UEGStigmataBattleComponent::Current() const
{return Battle.IsValid()&&Battle->IsBattleActive()&&RoundId.IsValid()&&Battle->GetRoundId()==RoundId;}
double UEGStigmataBattleComponent::Now() const{return GetWorld()?GetWorld()->GetTimeSeconds():0.;}
bool UEGStigmataBattleComponent::Has(FName Id) const{return Current()&&Selected.Contains(Id);}
bool UEGStigmataBattleComponent::NeedsUltimateEvents() const
{return Has(TEXT("Deliverance"))||Has(TEXT("Decimation"))||Has(TEXT("Gold"))||Has(TEXT("Helix"))||Has(TEXT("Kindred"))||Has(TEXT("LastBanquet"));}
bool UEGStigmataBattleComponent::IsPlayerHero(const AEGChessUnit* Unit) const
{
 return Current()&&IsValid(Unit)&&Unit->HasAuthority()&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player
  &&Deployed.Contains(Unit->GetInstanceId())&&!Unit->IsAuxiliaryEmplacement()&&!Unit->GetHeroSkill()->bSummon;
}
float UEGStigmataBattleComponent::UnityFinalDamageMultiplier(const AEGChessUnit* Unit) const
{return Current()&&bUnity&&IsValid(Unit)&&Unit->HasAuthority()&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player?FMath::Max(0.f,1.f+UnityDamageBonus):1.f;}
void UEGStigmataBattleComponent::Trigger(FName Id,AEGChessUnit* Unit)
{if(Current()&&!Id.IsNone()){auto& C=Contributions.FindOrAdd(Id);C.Id=Id;++C.TriggerCount;PendingTriggers.Add({Id,Unit});}}
TArray<FEGStigmataContribution> UEGStigmataBattleComponent::GetContributions() const
{TArray<FEGStigmataContribution> Result;Contributions.GenerateValueArray(Result);return Result;}
bool UEGStigmataBattleComponent::RestoreContributions(const TArray<FEGStigmataContribution>& Snapshot)
{
 if(!GetOwner()||!GetOwner()->HasAuthority()||Current())return false;
 TMap<FName,FEGStigmataContribution> Restored;
 for(const auto& Entry:Snapshot)
 {
  if(Entry.Id.IsNone()||Restored.Contains(Entry.Id))return false;
  Restored.Add(Entry.Id,Entry);
 }
 Contributions=MoveTemp(Restored);return true;
}
void UEGStigmataBattleComponent::RecordGold(FName Id,int32 Amount)
{if(!Id.IsNone()&&Amount>0){auto& C=Contributions.FindOrAdd(Id);C.Id=Id;C.GoldEarned+=Amount;}}
void UEGStigmataBattleComponent::RecordMana(FName Id,float Amount)
{if(Current()&&!Id.IsNone()&&Amount>0){auto& C=Contributions.FindOrAdd(Id);C.Id=Id;C.ManaRestored+=Amount;}}
void UEGStigmataBattleComponent::RecordDamage(FName Id,const FEGCombatResolution& Receipt,float AttributedElsewhere)
{RecordAdditionalDamage(Id,FMath::Max(0.f,FMath::Max(0.f,-Receipt.HealthDelta)+Receipt.ShieldAbsorbed-FMath::Max(0.f,AttributedElsewhere)));}
void UEGStigmataBattleComponent::RecordAdditionalDamage(FName Id,float Amount)
{if(Current()&&!Id.IsNone()&&Amount>0){auto& C=Contributions.FindOrAdd(Id);C.Id=Id;C.AdditionalDamage+=Amount;}}
void UEGStigmataBattleComponent::RecordPrevented(FName Id,float Amount)
{if(Current()&&!Id.IsNone()&&Amount>0){auto& C=Contributions.FindOrAdd(Id);C.Id=Id;C.DamagePrevented+=Amount;}}
void UEGStigmataBattleComponent::RecordShieldAbsorbed(FName Id,float Amount)
{if(Current()&&!Id.IsNone()&&Amount>0){auto& C=Contributions.FindOrAdd(Id);C.Id=Id;C.ShieldAbsorbed+=Amount;}}
void UEGStigmataBattleComponent::Start(UEGBattleContextComponent* Context,const TArray<FEGHeroInstance>& Heroes)
{
 Stop();Contributions.Reset();Battle=Context;RoundId=Context?Context->GetRoundId():FGuid();
 if(!Current())return;
 const auto& S=UEGStigmataBalanceSettings::Get();
#define EG_BALANCE(Name) Name=S.Name
 EG_BALANCE(LoveTeamSpeed);EG_BALANCE(LoveSpeed);EG_BALANCE(FlawlessSkillBonus);EG_BALANCE(FlawlessSeconds);EG_BALANCE(FlawlessFinisherMultiplier);
 EG_BALANCE(DeliveranceBonus);EG_BALANCE(DeliveranceDuration);EG_BALANCE(DisciplineStep);EG_BALANCE(GoldOpeningMana);EG_BALANCE(GoldRefund);
 EG_BALANCE(HelixCharges);EG_BALANCE(HelixDamage);EG_BALANCE(HelixRadiusCells);EG_BALANCE(DecimationMissingHealthScale);EG_BALANCE(DecimationMaxBonus);EG_BALANCE(DecimationLifesteal);
 EG_BALANCE(WisdomAttacks);EG_BALANCE(WisdomDamage);EG_BALANCE(WisdomRadiusCells);EG_BALANCE(SetsunaSpeed);EG_BALANCE(SetsunaSpeedDuration);EG_BALANCE(SetsunaCooldown);
 EG_BALANCE(DaybreakDamage);EG_BALANCE(DaybreakDuration);EG_BALANCE(DaybreakShieldBonus);EG_BALANCE(InfinityBonus);EG_BALANCE(FormSkillBonus);EG_BALANCE(DuskShield);
 EG_BALANCE(KindredDamage);EG_BALANCE(BanquetMana);EG_BALANCE(BanquetCooldown);EG_BALANCE(UnityDamageBonus);EG_BALANCE(UnityHealthBonus);EG_BALANCE(InitialHeroMana);
#undef EG_BALANCE
 for(const auto& Hero:Heroes)if(Hero.bDeployed)Deployed.Add(Hero.InstanceId);
 const auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Match=GM?GM->GetMatch():nullptr;
 if(!Match)return;
 for(const FName Id:Match->GetStigmataOwned()){auto& Entry=Contributions.FindOrAdd(Id);Entry.Id=Id;}
 for(const FName Id:{FName(TEXT("LoveBeginning")),FName(TEXT("Flawless")),FName(TEXT("Deliverance")),FName(TEXT("Discipline")),FName(TEXT("Gold")),FName(TEXT("Helix")),FName(TEXT("Decimation")),FName(TEXT("Wisdom")),FName(TEXT("Setsuna")),FName(TEXT("Daybreak")),FName(TEXT("Infinity")),FName(TEXT("Stars")),FName(TEXT("Vicissitude")),FName(TEXT("DuskStreet")),FName(TEXT("Kindred")),FName(TEXT("LastBanquet"))})
  if(Match->HasStigmata(Id))Selected.Add(Id);
 bUnity=Match->GetEndgameRoute()==TEXT("Unity");
 for(auto* Unit:Battle->Units())if(IsPlayerHero(Unit))ApplyUnitBonuses(Unit);
 ApplyOpening();
}
void UEGStigmataBattleComponent::ApplyOpening()
{
 if(!Current())return;
 const FGuid ExpectedRound=RoundId;
 if(Has(TEXT("LoveBeginning")))
 {
  TFunction<void()> Apply=[this,ExpectedRound]()
  {
   if(!Current()||RoundId!=ExpectedRound)return;
   for(auto* Unit:Battle->Units())if(IsPlayerHero(Unit)&&Unit->GetDefinition())
    {SetSpeed(Unit,1.f+LoveTeamSpeed+(BaseKind(Unit->GetDefinition()->HeroKind)==EEGHeroKind::Elysia?LoveSpeed:0.f),0.f);Trigger(TEXT("LoveBeginning"),Unit);}
  };
  if(Battle->IsCombatRunning())Apply();else Battle->EnqueueTeamOpening(MoveTemp(Apply));
 }
 if(Has(TEXT("DuskStreet"))&&HasMembers({EEGHeroKind::Pardofelis,EEGHeroKind::Sakura,EEGHeroKind::Aponia}))
 {
  TFunction<void()> Apply=[this,ExpectedRound]()
  {
   if(!Current()||RoundId!=ExpectedRound)return;
   for(auto* Unit:Battle->Units())if(IsValid(Unit)&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player&&IEGCombatant::Execute_IsAlive(Unit))
    Unit->GetHeroSkill()->GrantSourceShield(TEXT("StigmataDuskStreet"),Unit->GetAttributes()->GetMaxHealth()*DuskShield,TNumericLimits<float>::Max());
   Trigger(TEXT("DuskStreet"),FindHero(EEGHeroKind::Pardofelis,true));
  };
  if(Battle->IsCombatRunning())Apply();else Battle->EnqueueTeamOpening(MoveTemp(Apply));
 }
}
void UEGStigmataBattleComponent::Stop()
{
 for(const auto& Handle:SpeedEffects)if(Handle.IsValid())if(auto* ASC=Handle.GetOwningAbilitySystemComponent())ASC->RemoveActiveGameplayEffect(Handle);
 for(const auto& Handle:AttributeEffects)if(Handle.IsValid())if(auto* ASC=Handle.GetOwningAbilitySystemComponent())ASC->RemoveActiveGameplayEffect(Handle);
 AttributeEffects.Reset();BuffedUnits.Reset();bUnity=false;
 SpeedEffects.Reset();Battle.Reset();RoundId.Invalidate();Deployed.Reset();Selected.Reset();Actions.Reset();Chains.Reset();
 DeliveranceEnds.Reset();EvadeReady.Reset();HelixReady.Reset();KindredUsed.Reset();VillVCasts.Reset();Bleeds.Reset();PendingTriggers.Reset();DeferredMana.Reset();WisdomCount=0;BanquetReady=0;
}
void UEGStigmataBattleComponent::ApplyUnitBonuses(AEGChessUnit* Unit,bool bInheritedHealth,bool bInheritedAttack)
{
 if(!Current()||!IsValid(Unit)||!Unit->HasAuthority()||IEGCombatant::Execute_GetFaction(Unit)!=EEGFaction::Player||BuffedUnits.Contains(Unit))return;
 auto* ASC=Unit->GetAbilitySystemComponent();if(!ASC)return;
 const bool bInfinity=Has(TEXT("Infinity"));
 const float HealthFactor=(bInheritedHealth?1.f:(bInfinity?1.f+InfinityBonus:1.f)*(bUnity?1.f+UnityHealthBonus:1.f));
 const float AttackFactor=bInheritedAttack?1.f:(bInfinity?1.f+InfinityBonus:1.f);
 const auto Kind=Unit->GetDefinition()?Unit->GetDefinition()->HeroKind:EEGHeroKind::Generic;
 const bool bForm=(Kind==EEGHeroKind::GriseoAdult&&Has(TEXT("Stars")))||(Kind==EEGHeroKind::CollapsedHua&&Has(TEXT("Vicissitude")));
 const float SkillFactor=IsPlayerHero(Unit)?1.f+(Has(TEXT("Flawless"))?FlawlessSkillBonus:0.f)+(bForm?FormSkillBonus:0.f):1.f;
 const float OldMax=Unit->GetAttributes()->GetMaxHealth(),OldHealth=Unit->GetAttributes()->GetHealth();
 auto Spec=ASC->MakeOutgoingSpec(UEGStigmataAttributeEffect::StaticClass(),1,ASC->MakeEffectContext());if(!Spec.IsValid())return;
 Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Stigmata.Health")),HealthFactor);
 Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Stigmata.Attack")),AttackFactor);
 Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Stigmata.Skill")),SkillFactor);
 Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Stigmata.Damage")),1.f);
 const FActiveGameplayEffectHandle Handle=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 if(!Handle.IsValid()||!IsValid(Unit))return;
 AttributeEffects.Add(Handle);BuffedUnits.Add(Unit);
 if(OldMax>0&&OldHealth>0)ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),OldHealth*Unit->GetAttributes()->GetMaxHealth()/OldMax);
 if(bInfinity)Trigger(TEXT("Infinity"),Unit);
 if(bUnity)Trigger(TEXT("Unity"),Unit);
}
void UEGStigmataBattleComponent::ApplyInitialMana(AEGChessUnit* Unit)
{
 if(!IsPlayerHero(Unit)||!Unit->GetDefinition())return;
 auto* ASC=Unit->GetAbilitySystemComponent();const auto* Stats=Unit->GetAttributes();
 if(Unit->GetDefinition()->HeroKind!=EEGHeroKind::ElysiaHerrscher&&Unit->GetDefinition()->HeroKind!=EEGHeroKind::Generic)
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Stats->GetMaxMana()*InitialHeroMana);
 if(Has(TEXT("Gold")))
 {
  const float Before=Stats->GetMana();Unit->GetHeroSkill()->RestoreStigmataMana(Stats->GetMaxMana()*GoldOpeningMana);
  RecordMana(TEXT("Gold"),FMath::Max(0.f,Stats->GetMana()-Before));Trigger(TEXT("Gold"),Unit);
 }
}
void UEGStigmataBattleComponent::GrantStigmataMana(AEGChessUnit* Unit,FName Id,float Amount)
{
 if(!Current()||!IsValid(Unit)||Unit->IsDeathPending()||!IEGCombatant::Execute_IsAlive(Unit)||Amount<=0)return;
 if(Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher&&Unit->GetHeroSkill()->BlocksManaGain())
 {return;} // Origin rejects mana outright; it is never banked for the next bow phase.
 const float Before=Unit->GetAttributes()->GetMana();Unit->GetHeroSkill()->RestoreStigmataMana(Amount);
 RecordMana(Id,FMath::Max(0.f,Unit->GetAttributes()->GetMana()-Before));
}
void UEGStigmataBattleComponent::DeliverDeferredMana(AEGChessUnit* Unit)
{
 if(!Current()||!IsValid(Unit)||Unit->IsDeathPending()||!IEGCombatant::Execute_IsAlive(Unit))return;
 TMap<FName,float> Pending;if(!DeferredMana.RemoveAndCopyValue(Unit->GetInstanceId(),Pending))return;
 for(const auto& Entry:Pending)GrantStigmataMana(Unit,Entry.Key,Entry.Value);
}
void UEGStigmataBattleComponent::DiscardDeferredMana(AEGChessUnit* Unit)
{if(IsValid(Unit))DeferredMana.Remove(Unit->GetInstanceId());}
void UEGStigmataBattleComponent::EndPlay(const EEndPlayReason::Type Reason){Stop();Super::EndPlay(Reason);}
void UEGStigmataBattleComponent::SetSpeed(AEGChessUnit* Unit,float Multiplier,float Seconds)
{
 if(!Current()||!IsValid(Unit)||!FMath::IsFinite(Multiplier)||Multiplier<=0)return;
 auto* ASC=Unit->GetAbilitySystemComponent();auto Spec=ASC->MakeOutgoingSpec(UEGTeamSpeedEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(!Spec.IsValid())return;
 Spec.Data->SetDuration(Seconds>0?Seconds:FGameplayEffectConstants::INFINITE_DURATION,true);
 Spec.Data->SetSetByCallerMagnitude(EGTags::TeamAttackSpeed,Multiplier);
 SpeedEffects.Add(ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data));
}
AEGChessUnit* UEGStigmataBattleComponent::FindHero(EEGHeroKind Kind,bool bAlive) const
{
 if(!Current())return nullptr;
 for(auto* Unit:Battle->Units())if(IsPlayerHero(Unit)&&Unit->GetDefinition()&&BaseKind(Unit->GetDefinition()->HeroKind)==Kind&&(!bAlive||IEGCombatant::Execute_IsAlive(Unit)))return Unit;
 return nullptr;
}
bool UEGStigmataBattleComponent::HasMembers(std::initializer_list<EEGHeroKind> Members) const
{for(const auto Kind:Members)if(!FindHero(Kind,false))return false;return true;}
void UEGStigmataBattleComponent::RegisterAction(AEGChessUnit* Unit,FGuid Id,AEGChessUnit* Target,bool bUltimate)
{
 if(!IsPlayerHero(Unit)||!Id.IsValid())return;
 FAction& Action=Actions.FindOrAdd(Id);Action.Unit=Unit;Action.Target=Target;Action.bUltimate=bUltimate;
 if(!bUltimate&&Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::VillV)
  if(const auto* Mechanism=Unit->FindComponentByClass<UEGVillVMechanismComponent>();Mechanism&&Mechanism->HasEnhancedShot())
   if(const auto* Cast=VillVCasts.Find(Unit->GetInstanceId())){Action.bEnhancedVillV=true;Action.VillVCast=*Cast;}
}
void UEGStigmataBattleComponent::UpdateActionTarget(AEGChessUnit* Unit,FGuid Id,AEGChessUnit* Target)
{if(auto* Action=Actions.Find(Id);Action&&Action->Unit.Get()==Unit&&!Action->bUltimate&&Action->Chain==0)Action->Target=Target;}
void UEGStigmataBattleComponent::RegisterUltimateContinuation(AEGChessUnit* Unit,FGuid Id,AEGChessUnit* Target)
{
 if(!IsPlayerHero(Unit)||!Unit->GetDefinition()||Unit->GetDefinition()->HeroKind!=EEGHeroKind::ElysiaHerrscher||!Id.IsValid())return;
 RegisterAction(Unit,Id,Target,true);
 if(auto* Action=Actions.Find(Id))Action->bCommitted=true;
}
AEGChessUnit* UEGStigmataBattleComponent::DamageOwner(AEGChessUnit* Source,FGuid ActionId) const
{
 if(IsPlayerHero(Source))return Source;
 if(!IsValid(Source)||!Source->GetHeroSkill()->bSummon)return nullptr;
 const auto* Action=Actions.Find(ActionId);auto* Owner=Action?Action->Unit.Get():nullptr;
 return Action&&Action->bUltimate&&Action->bCommitted&&IsPlayerHero(Owner)&&!Owner->IsDeathPending()&&IEGCombatant::Execute_IsAlive(Owner)&&Source->GetHeroSkill()->SummonerId==Owner->GetInstanceId()?Owner:nullptr;
}
void UEGStigmataBattleComponent::CommitUltimate(AEGChessUnit* Unit,FGuid Id,float SpentMana)
{
 auto* Action=Actions.Find(Id);
 if(!Current()||!IsPlayerHero(Unit)||!Action||Action->Unit.Get()!=Unit||!Action->bUltimate||Action->bCommitted||SpentMana<=0)return;
 const bool bGuardAction=Unit->GetHeroSkill()->IsActionRunning()&&Unit->GetHeroSkill()->ActionId==Id;
 Action->bCommitted=true;
 Battle->RecordSuccessfulUltimate(Unit,Id);
 if(Has(TEXT("Deliverance"))){DeliveranceEnds.Add(Unit->GetInstanceId(),Now()+DeliveranceDuration);Trigger(TEXT("Deliverance"),Unit);}
 if(!Current()||!IsValid(Unit)||(bGuardAction&&(!Unit->GetHeroSkill()->IsActionRunning()||Unit->GetHeroSkill()->ActionId!=Id)))return;
 if(Has(TEXT("Gold"))){GrantStigmataMana(Unit,TEXT("Gold"),SpentMana*GoldRefund);Trigger(TEXT("Gold"),Unit);}
 if(!Current()||!IsValid(Unit)||(bGuardAction&&(!Unit->GetHeroSkill()->IsActionRunning()||Unit->GetHeroSkill()->ActionId!=Id)))return;
 if(Has(TEXT("Helix")))HelixReady.Add(Unit->GetInstanceId(),FMath::Max(0,HelixCharges));
 if(Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::VillV)VillVCasts.Add(Unit->GetInstanceId(),Id);
 if(Has(TEXT("LastBanquet"))&&Now()>=BanquetReady&&HasMembers({EEGHeroKind::Aponia,EEGHeroKind::Eden,EEGHeroKind::Elysia}))
 {
  const auto Kind=Unit->GetDefinition()?BaseKind(Unit->GetDefinition()->HeroKind):EEGHeroKind::Generic;
  if(Kind==EEGHeroKind::Aponia||Kind==EEGHeroKind::Eden||Kind==EEGHeroKind::Elysia)
  {
   BanquetReady=Now()+BanquetCooldown;
   for(const auto Member:{EEGHeroKind::Aponia,EEGHeroKind::Eden,EEGHeroKind::Elysia})
    if(auto* Other=FindHero(Member,true);Other&&Other!=Unit)GrantStigmataMana(Other,TEXT("LastBanquet"),Other->GetAttributes()->GetMaxMana()*BanquetMana);
   Trigger(TEXT("LastBanquet"),Unit);
  }
 }
}
bool UEGStigmataBattleComponent::TryEvade(AEGChessUnit* Source,AEGChessUnit* Target,bool bPeriodic,float Raw,float DefenseScale)
{
 return !bPeriodic&&OnSuccessfulEvade(Source,Target,Raw,DefenseScale);
}
bool UEGStigmataBattleComponent::OnSuccessfulEvade(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,float DefenseScale)
{
 if(!Has(TEXT("Setsuna"))||!IsPlayerHero(Target)||!IsValid(Source)||IEGCombatant::Execute_GetFaction(Source)!=EEGFaction::Enemy)return false;
 const FGuid Id=Target->GetInstanceId();if(const double* Ready=EvadeReady.Find(Id);Ready&&*Ready>Now())return false;
 const float K=FMath::Max(.01f,UEGStigmataBalanceSettings::Get().DefenseConstant);
 const float Prevented=FMath::Max(0.f,Raw)*Source->GetAttributes()->GetDamageMultiplier()*K/(K+FMath::Max(0.f,Target->GetAttributes()->GetDefense())*DefenseScale);
 EvadeReady.Add(Id,Now()+SetsunaCooldown);
 TWeakObjectPtr<AEGChessUnit> WeakTarget(Target);
 SetSpeed(Target,1.f+SetsunaSpeed,SetsunaSpeedDuration);
 if(Current()&&WeakTarget.IsValid()){Trigger(TEXT("Setsuna"),WeakTarget.Get());RecordPrevented(TEXT("Setsuna"),Prevented);}
 return true;
}
float UEGStigmataBattleComponent::ModifyDamage(AEGChessUnit* Source,AEGChessUnit* Target,FGuid Id,float Raw,bool bLinked,bool bExtra,TArray<TPair<FName,float>>* BonusParts)
{
 Source=DamageOwner(Source,Id);
 if(bLinked||!Source||!IsValid(Target))return Raw;
 float Bonus=0;
 auto AddBonus=[&](FName Name,float Gain){Bonus+=Gain;if(BonusParts&&Gain>0.f)BonusParts->Emplace(Name,Raw*Gain);};
 if(Has(TEXT("Deliverance")))
 {
  const auto* End=DeliveranceEnds.Find(Source->GetInstanceId());
  const auto* Action=Actions.Find(Id);
  if((End&&*End>Now())||(Action&&Action->bUltimate&&Action->bCommitted&&Action->Unit.Get()==Source))
   AddBonus(TEXT("Deliverance"),DeliveranceBonus);
 }
 if(Has(TEXT("Decimation")))
 {
  const auto* Stats=Source->GetAttributes();const float Max=Stats->GetMaxHealth();
  if(Max>0){const float Gain=FMath::Clamp((1.f-Stats->GetHealth()/Max)*DecimationMissingHealthScale,0.f,DecimationMaxBonus);AddBonus(TEXT("Decimation"),Gain);}
 }
 if(!bExtra&&Has(TEXT("Discipline")))
  if(auto* Action=Actions.Find(Id);Action&&!Action->bUltimate&&Action->Unit.Get()==Source&&Action->Target.Get()==Target)
  {
   if(Action->Chain==0)
   {
    const auto* Previous=Chains.Find(Source->GetInstanceId());
    Action->Chain=Previous&&Previous->Target==Target->GetInstanceId()?FMath::Min(4,Previous->Count+1):1;
   }
   const float Gain=(Action->Chain-1)*DisciplineStep;AddBonus(TEXT("Discipline"),Gain);
  }
 return Raw*FMath::Max(0.f,1.f+Bonus);
}
void UEGStigmataBattleComponent::ExtraArea(AEGChessUnit* Source,FVector Point,float Radius,float Raw,FName Id)
{
 if(!Current()||!IsValid(Source)||Raw<=0)return;
 const auto Targets=Source->GetCombat()->FindEnemiesInRange(Point,Radius);
 const FGuid Action=FGuid::NewGuid(),Round=RoundId;
 for(auto* Target:Targets){if(!Current()||RoundId!=Round)break;FEGCombatExecution::StigmataDamage(Source,Target,Raw,Action,false,Id);}
 if(Current()){Battle->Visual(Source,Point-FVector(0,0,88),Radius,true);Trigger(Id,Source);}
}
void UEGStigmataBattleComponent::BasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid Id)
{
 if(!IsPlayerHero(Source)||!IEGCombatant::Execute_IsAlive(Source)||!IsValid(Target)||!Id.IsValid())return;
 if(Has(TEXT("Discipline")))if(auto* Action=Actions.Find(Id);Action&&!Action->bUltimate&&Action->Target.Get()==Target&&Action->Chain>0)
  Chains.Add(Source->GetInstanceId(),{Target->GetInstanceId(),Action->Chain});
 float Cell=170.f;
 if(const auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())if(const auto* Board=GM->GetBoard())if(Board->LayoutSource)Cell=Board->LayoutSource->CellDepth;
 if(Has(TEXT("Daybreak")))
 {
  FBleed* Entry=Bleeds.FindByPredicate([&](const FBleed& B){return B.Source.Get()==Source&&B.Target.Get()==Target;});
  if(!Entry){Entry=&Bleeds.AddDefaulted_GetRef();Entry->Source=Source;Entry->Target=Target;Entry->Next=Now()+1.;}
  Entry->End=Now()+DaybreakDuration;Trigger(TEXT("Daybreak"),Source);
 }
 if(Has(TEXT("Helix")))if(auto* Charges=HelixReady.Find(Source->GetInstanceId());Charges&&*Charges>0)
 {
  --*Charges;ExtraArea(Source,Target->GetActorLocation(),Cell*HelixRadiusCells,Source->GetAttributes()->GetAttackPower()*HelixDamage,TEXT("Helix"));
 }
 if(!Current())return;
 if(Has(TEXT("Wisdom"))&&++WisdomCount>=FMath::Max(1,WisdomAttacks))
 {
  WisdomCount=0;float Total=0;int32 Count=0;
  for(auto* Unit:Battle->Units())if(IsPlayerHero(Unit)&&IEGCombatant::Execute_IsAlive(Unit)){Total+=Unit->GetAttributes()->GetAttackPower();++Count;}
  if(Count>0)ExtraArea(Source,Target->GetActorLocation(),Cell*WisdomRadiusCells,Total/Count*WisdomDamage,TEXT("Wisdom"));
 }
}
void UEGStigmataBattleComponent::ApplySummon(AEGChessUnit* Unit)
{
 if(!IsValid(Unit)||!Unit->GetDefinition())return;
 const bool bKnight=Unit->GetDefinition()->HeroKind==EEGHeroKind::Knight;
 ApplyUnitBonuses(Unit,bKnight,bKnight);
}
void UEGStigmataBattleComponent::TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Tick,Function);if(!Current())return;
 const auto Triggers=MoveTemp(PendingTriggers);PendingTriggers.Reset();
 for(const auto& Entry:Triggers)
 {
  if(!Current())return;
  Battle->OnStigmataTriggered.Broadcast(Entry.Id,Entry.Source.Get());
 }
 if(!Current())return;
 const double Time=Now();
 for(int32 I=Bleeds.Num()-1;I>=0;--I)
 {
  auto& Entry=Bleeds[I];
  if(!Entry.Source.IsValid()||!Entry.Target.IsValid()||!IEGCombatant::Execute_IsAlive(Entry.Target.Get())){Bleeds.RemoveAtSwap(I);continue;}
  while(Current()&&Entry.Next<=Time&&Entry.Next<=Entry.End)
  {
   Entry.Next+=1.;FEGCombatExecution::StigmataDamage(Entry.Source.Get(),Entry.Target.Get(),Entry.Source->GetAttributes()->GetAttackPower()*DaybreakDamage,FGuid::NewGuid(),true,TEXT("Daybreak"));
  }
  if(!Current())return;
  if(Entry.End<=Time)Bleeds.RemoveAtSwap(I);
 }
}
void UEGStigmataBattleComponent::AfterDamage(const FEGCombatResolution& Receipt)
{
 if(!Current()||Receipt.bHealing||Receipt.bLinkedDamage||Receipt.bStigmataDamage)return;
 AEGChessUnit* Source=nullptr;
 for(auto* Unit:Battle->Units())if(IsValid(Unit)&&Unit->GetInstanceId()==Receipt.SourceId){Source=Unit;break;}
 Source=DamageOwner(Source,Receipt.ActionId);
 if(Source)
 {
  if(Has(TEXT("Decimation"))&&!Receipt.bPeriodicDamage&&!Receipt.bEnvironmentDamage&&!Receipt.bExecution&&IEGCombatant::Execute_IsAlive(Source))
  {
   const float Damage=FMath::Max(0.f,-Receipt.HealthDelta)+Receipt.ShieldAbsorbed;
   const float Before=Source->GetAttributes()->GetHealth();TWeakObjectPtr<AEGChessUnit> WeakSource=Source;
   if(Damage>0)FEGCombatExecution::Heal(Source,Source,Damage*DecimationLifesteal);
   if(!Current())return;
   if(WeakSource.IsValid()){auto& C=Contributions.FindOrAdd(TEXT("Decimation"));C.Id=TEXT("Decimation");C.HealingRestored+=FMath::Max(0.f,WeakSource->GetAttributes()->GetHealth()-Before);}
   else return;
  }
  if(Has(TEXT("Decimation"))&&Source->GetAttributes()->GetHealth()<Source->GetAttributes()->GetMaxHealth())Trigger(TEXT("Decimation"),Source);
  if(Has(TEXT("Discipline")))if(const auto* Basic=Actions.Find(Receipt.ActionId);Basic&&!Basic->bUltimate&&Basic->Chain>1)Trigger(TEXT("Discipline"),Source);
 }
 // Kindred is dispatched after the primary receipt, never by stigmata damage.
 if(!Has(TEXT("Kindred")))return;
 auto* Action=Actions.Find(Receipt.ActionId);if(!Action||!Action->Unit.IsValid())return;
 auto* Vill=Action->Unit.Get();if(!IEGCombatant::Execute_IsAlive(Vill)||!Vill->GetDefinition()||Vill->GetDefinition()->HeroKind!=EEGHeroKind::VillV)return;
 if(Action->bUltimate&&!Action->bCommitted)return;
 const FGuid CastId=Action->bUltimate?Receipt.ActionId:Action->bEnhancedVillV?Action->VillVCast:FGuid();
 if(!CastId.IsValid()||KindredUsed.Contains(CastId)||!HasMembers({EEGHeroKind::VillV,EEGHeroKind::Mobius}))return;
 auto* Mobius=FindHero(EEGHeroKind::Mobius,true);auto* Behavior=Mobius?Cast<UEGMobiusSkillBehavior>(Mobius->GetHeroSkill()->GetBehavior()):nullptr;
 AEGChessUnit* Hit=nullptr;for(auto* Unit:Battle->Units())if(Unit->GetInstanceId()==Receipt.TargetId){Hit=Unit;break;}
 auto* Lowest=Behavior?Behavior->LowestLinkedTarget():nullptr;
 if(!Lowest)Lowest=Hit;
 if(!IsValid(Lowest)||!IEGCombatant::Execute_IsAlive(Lowest))return;
 KindredUsed.Add(CastId);FEGCombatExecution::StigmataDamage(Vill,Lowest,Vill->GetAttributes()->GetAttackPower()*KindredDamage,FGuid::NewGuid(),false,TEXT("Kindred"));
 if(Current())Trigger(TEXT("Kindred"),Vill);
}
