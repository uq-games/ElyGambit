#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffect.h"
#include <initializer_list>
#include "Types/EGChessTypes.h"
#include "EGStigmataBattleComponent.generated.h"

class AEGChessUnit;
class UEGBattleContextComponent;

/** Battle-only authoritative totals. Damage includes actual health and shield loss. */
struct ELYGAMBIT_API FEGStigmataContribution
{
 FName Id;
 int32 TriggerCount=0,GoldEarned=0;
 float AdditionalDamage=0,ManaRestored=0,HealingRestored=0,ShieldAbsorbed=0,DamagePrevented=0;
};

UCLASS()
class ELYGAMBIT_API UEGStigmataAttributeEffect : public UGameplayEffect
{
 GENERATED_BODY()
public:
 UEGStigmataAttributeEffect();
};

/** Per-round state only. The match owns the selected stigmata. */
UCLASS(config=Game)
class ELYGAMBIT_API UEGStigmataBattleComponent : public UActorComponent
{
 friend class FEGHumanReworkContract;
 GENERATED_BODY()
public:
 UEGStigmataBattleComponent();
 void Start(UEGBattleContextComponent* Context,const TArray<FEGHeroInstance>& Heroes);
 void Stop();
 virtual void TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 bool Has(FName Id) const; bool NeedsUltimateEvents() const; void EmitTrigger(FName Id,AEGChessUnit* Unit) {Trigger(Id,Unit);}
 bool IsPlayerHero(const AEGChessUnit* Unit) const;
 float UnityFinalDamageMultiplier(const AEGChessUnit* Unit) const;
 void RegisterAction(AEGChessUnit* Unit,FGuid Id,AEGChessUnit* Target,bool bUltimate); void UpdateActionTarget(AEGChessUnit* Unit,FGuid Id,AEGChessUnit* Target);
 void RegisterUltimateContinuation(AEGChessUnit* Unit,FGuid Id,AEGChessUnit* Target);
 void CommitUltimate(AEGChessUnit* Unit,FGuid Id,float SpentMana);
 bool TryEvade(AEGChessUnit* Source,AEGChessUnit* Target,bool bPeriodic,float Raw=0.f,float DefenseScale=1.f);
 /** Consume the selected signet's ready benefit for one already-successful evade. */
 bool OnSuccessfulEvade(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,float DefenseScale);
 float ModifyDamage(AEGChessUnit* Source,AEGChessUnit* Target,FGuid ActionId,float Raw,bool bLinked,bool bExtra,TArray<TPair<FName,float>>* BonusParts=nullptr);
 void AfterDamage(const FEGCombatResolution& Receipt);
 void BasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid Id);
 void ApplySummon(AEGChessUnit* Unit);
 void ApplyUnitBonuses(AEGChessUnit* Unit,bool bInheritedHealth=false,bool bInheritedAttack=false);
 void ApplyInitialMana(AEGChessUnit* Unit);
 TArray<FEGStigmataContribution> GetContributions() const;
 /** Restore accumulated run totals after a finished battle has been stopped. */
 bool RestoreContributions(const TArray<FEGStigmataContribution>& Snapshot);
 void ClearContributions() { Contributions.Reset(); }
 void RecordGold(FName Id,int32 Amount);
 void RecordMana(FName Id,float Amount);
 void RecordDamage(FName Id,const FEGCombatResolution& Receipt,float AttributedElsewhere=0.f);
 void RecordAdditionalDamage(FName Id,float Amount);
 void RecordPrevented(FName Id,float Amount);
 void RecordShieldAbsorbed(FName Id,float Amount);
 void DeliverDeferredMana(AEGChessUnit* Unit);
 void DiscardDeferredMana(AEGChessUnit* Unit);
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float LoveSpeed=0.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float FlawlessSeconds=2.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float FlawlessFinisherMultiplier=1.33333333f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DeliveranceBonus=0.25f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DeliveranceDuration=6.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DisciplineStep=0.10f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float GoldRefund=0.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float HelixDamage=0.80f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float HelixRadiusCells=1.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DecimationStep=0.05f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") int32 WisdomAttacks=12;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float WisdomDamage=1.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float WisdomRadiusCells=1.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float SetsunaSpeed=0.30f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float SetsunaSpeedDuration=3.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float SetsunaCooldown=8.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DaybreakDamage=0.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DaybreakDuration=3.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DaybreakShieldBonus=0.30f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float InfinityBonus=0.30f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DuskShield=0.15f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float KindredDamage=2.40f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float BanquetMana=0.30f;
 float LoveTeamSpeed=.15f,FlawlessSkillBonus=.25f,GoldOpeningMana=.2f;
 float DecimationMissingHealthScale=1.2f,DecimationMaxBonus=.6f,DecimationLifesteal=.1f;
 float BanquetCooldown=8.f,UnityDamageBonus=.35f,UnityHealthBonus=.25f,InitialHeroMana=.2f,FormSkillBonus=.25f;
 int32 HelixCharges=3;
 friend class FEGStigmataCombatTest;


private:
 struct FAction
 {
  TWeakObjectPtr<AEGChessUnit> Unit,Target;
  bool bUltimate=false,bCommitted=false,bEnhancedVillV=false;
  int32 Chain=0;
  FGuid VillVCast;
 };
 struct FChain {FGuid Target;int32 Count=0;};
 struct FPendingTrigger {FName Id;TWeakObjectPtr<AEGChessUnit> Source;};
 struct FBleed
 {
  TWeakObjectPtr<AEGChessUnit> Source,Target;
  double Next=0,End=0;
 };
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 FGuid RoundId;
 TSet<FGuid> Deployed;
 TSet<FName> Selected;
 TMap<FGuid,FAction> Actions;
 TMap<FGuid,FChain> Chains;
 TMap<FGuid,double> DeliveranceEnds;
 TMap<FGuid,double> EvadeReady;
 TMap<FGuid,int32> HelixReady;
 TSet<FGuid> KindredUsed;
 TMap<FGuid,FGuid> VillVCasts;
 TArray<FBleed> Bleeds;
 TArray<FPendingTrigger> PendingTriggers;
 TArray<FActiveGameplayEffectHandle> SpeedEffects;
 TArray<FActiveGameplayEffectHandle> AttributeEffects;
 TSet<TWeakObjectPtr<AEGChessUnit>> BuffedUnits;
 TMap<FName,FEGStigmataContribution> Contributions;
 TMap<FGuid,TMap<FName,float>> DeferredMana;
 bool bUnity=false;
 int32 WisdomCount=0;
 double BanquetReady=0;
 bool Current() const; void ApplyOpening();
 double Now() const;
 void Trigger(FName Id,AEGChessUnit* Unit);
 void SetSpeed(AEGChessUnit* Unit,float Multiplier,float Seconds);
 bool HasMembers(std::initializer_list<EEGHeroKind> Members) const;
 AEGChessUnit* FindHero(EEGHeroKind Kind,bool bAlive) const;
 void ExtraArea(AEGChessUnit* Source,FVector Point,float Radius,float Raw,FName Id);
 void GrantStigmataMana(AEGChessUnit* Unit,FName Id,float Amount);
 AEGChessUnit* DamageOwner(AEGChessUnit* Source,FGuid ActionId) const;
};
