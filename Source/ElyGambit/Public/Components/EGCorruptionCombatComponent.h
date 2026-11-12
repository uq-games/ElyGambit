#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/EGChessTypes.h"
#include "EGCorruptionCombatComponent.generated.h"
class AEGChessUnit;
class UEGBattleContextComponent;
class UStaticMeshComponent;
class AEGCorruptionBarrierVisual;
class AEGCorruptionBarrier;
class AEGSkillProjectile;
struct FEGThrownSummonFlightState;
class AEGCorruptionBossVisual;
class AEGCorruptionSeed;
class AEGCorruptionPhantomVisual;
class UEGSourcePreviewDefinition;
UENUM()
enum class EEGCorruptionRole : uint8 { Melee, Ranged, Support };
UENUM(BlueprintType)
enum class EEGCorruptionBossPhase : uint8 { First, Barrier, Second, Finished };
/** Native, read-only presentation notifications; combat owns timing and damage. */
enum class EEGCorruptionBossCast : uint8 { None, Spread, Mimic };
enum class EEGCorruptionBossCastEvent : uint8 { Started, Resolved, Cancelled };
DECLARE_MULTICAST_DELEGATE_OneParam(FEGCorruptionPhaseChanged, AEGChessUnit*);
DECLARE_MULTICAST_DELEGATE_FourParams(FEGCorruptionBossCastChanged,EEGCorruptionBossCast,EEGCorruptionBossCastEvent,FGuid,float);
/** Battle-local rules attached only to explicitly configured enemy units. */
UCLASS(config=Game)
class ELYGAMBIT_API UEGCorruptionCombatComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGCorruptionCombatComponent();
 static UEGCorruptionCombatComponent* AttachBoss(AEGChessUnit* Unit,UEGBattleContextComponent* Battle,float BaseHealth,float BaseAttack,int32 Corruption);
 static UEGCorruptionCombatComponent* AttachCopy(AEGChessUnit* Unit,UEGBattleContextComponent* Battle,EEGCorruptionRole Role);
 void UpdateCorruption(int32 Corruption);
 float FilterDamage(AEGChessUnit* Source,FGuid ActionId,float Damage,float ShieldDamageMultiplier=1.f,float* OutPhaseShieldAbsorbed=nullptr,bool bExecution=false);
 /** Install the remaining transition shield after the triggering damage GE resolves. */
 void CommitPhaseShield(bool bCommitTransition,bool bCommitBreak);
 bool HasPendingTransition() const {return bTransitionPending;}
 void RememberUltimate(FGuid ActionId);
 void RememberBasicAttack(FGuid ActionId);
 void UltimateCommitted(FGuid ActionId);
 void ObserveDamage(const FEGCombatResolution& Receipt);
 virtual void TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 void EndCombat();
 FEGCorruptionPhaseChanged OnPhaseTwo;
 FEGCorruptionBossCastChanged OnBossCastEvent;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float LowHealthMultiplier=.9f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float MediumHealthMultiplier=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float HighHealthBaseMultiplier=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float HighHealthStepMultiplier=.025f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float MaximumHealthMultiplier=1.25f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") int32 HighHealthStepSize=10;
 /** Each life derives independently from AttachBoss's unscaled health and corruption. */
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.01")) float FirstLifeHealthMultiplier=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.01")) float SecondLifeHealthMultiplier=1.f;
 /** Barrier max durability as a proportion of the first opening maximum health. */
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.01")) float BarrierHealthRatio=.4f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.1")) float BarrierDurationSeconds=8.f;
 /** Private source-style body and bow assembly; missing/incompatible assets use the short fallback. */
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> BarrierIntroAssembly;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.0")) float BarrierFailureHealthRatio=.2f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.0")) float BreakWeaknessSeconds=4.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="1.0")) float BreakDamageMultiplier=1.2f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="1")) int32 MarkHitThreshold=3;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.0")) float MarkDamageMultiplier=.6f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.1")) float SpreadIntervalSeconds=7.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.1")) float FirstSpreadDelaySeconds=3.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="1.0")) float SpecialSkillGapSeconds=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.0")) float SpreadDamageMultiplier=1.1f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.1")) float MimicIntervalSeconds=10.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.0")) float MimicDamageMultiplier=1.f;
 /** First-life targeted pulse; intentionally independent of player Elysia's skill tiers. */
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.0")) float FirstPulseDamageMultiplier=1.8f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.1")) float FirstPulseRadiusCells=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss|Balance",meta=(ClampMin="0.1")) float FirstPulseWarningSeconds=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float PhaseThresholdRatio=.5f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float PhaseOneSeedInterval=8.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float PhaseTwoSeedInterval=6.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Boss") float PhaseShieldHealthRatio=.15f;
 /** Serialized legacy setting; the phase now uses an ordinary source shield. */
 UPROPERTY() int32 PhaseShieldHits=0;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Copy") float SupportMana=10.f;
 UPROPERTY(Config,EditAnywhere,Category="Corruption|Copy") float SupportRadius=300.f;
 int32 GetShieldHitsRemaining() const {return 0;}
 float GetPhaseShieldRemaining() const;
 bool IsBoss() const {return bBoss;}
 EEGCorruptionRole GetCopyRole() const {return CopyRole;}
 bool IsPhaseTwo() const {return bPhaseTwo;}
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") EEGCorruptionBossPhase GetBossPhase() const {return BossPhase;}
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") float GetBarrierHealth() const {return BarrierHealth;}
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") float GetBarrierMaxHealth() const {return BarrierMaxHealth;}
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") float GetBarrierRemainingSeconds() const {return BarrierRemaining;}
 float GetBarrierIntroRemainingSeconds() const {return BarrierIntroRemaining;}
 float GetBarrierIntroDurationSeconds() const {return BarrierIntroDuration;}
 int32 GetBarrierPushbackFallbackCount() const {return BarrierPushbackFallbackCount;}
 EEGCorruptionBossCast GetBossCastKind() const {return ActiveBossCast;}
 FGuid GetBossCastActionId() const {return ActiveBossCastAction;}
 float GetBossCastProgress() const {return ActiveBossCastDuration>0.f?FMath::Clamp(ActiveBossCastElapsed/ActiveBossCastDuration,0.f,1.f):0.f;}
 bool IsBarrierIntroPresentationActive() const;
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") float GetPhaseHealth() const;
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") float GetPhaseMaxHealth() const;
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") float GetWeaknessRemainingSeconds() const {return WeaknessRemaining;}
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") bool IsEnraged() const {return bEnraged;}
 UFUNCTION(BlueprintPure,Category="Corruption|Boss") bool WasBarrierBroken() const {return bBarrierBroken;}
 /** Called by the authoritative encounter clock, in effective seconds. */
 void SetEffectiveBattleSeconds(float Seconds);
 /** Presentation notifications from the actual spawned and resolved seed; never calculate damage here. */
 void NotifySpreadStarted(AEGCorruptionSeed* Seed,FVector Location,float Radius,float Duration,FGuid DamageAction);
 void NotifySpreadResolved(AEGCorruptionSeed* Seed,FVector Location,float Radius);
 void NotifySpreadEnded(AEGCorruptionSeed* Seed);
 /** Used by the seed before and after damage; identity includes the current round. */
 bool IsCurrentSpread(const AEGCorruptionSeed* Seed,FGuid Round,FGuid Action) const;
 /** Board-local wall geometry, used by movement and projectiles. */
 bool BlocksMovement(FVector Start,FVector End,float Radius) const;
 bool AllowsPlayerLocation(FVector Point,float Radius) const;
 FVector ConstrainPlayerLocation(FVector Point,float Radius) const;
 FVector WallContactPoint(FVector From) const;
 bool IsWallActive() const {return bBoss&&BossPhase==EEGCorruptionBossPhase::Barrier;}
 float GetOutgoingDamageMultiplier() const;
#if WITH_DEV_AUTOMATION_TESTS
 void CompleteBarrierIntroForTests();
 void SetBarrierIntroAssemblyForTests(TSoftObjectPtr<UEGSourcePreviewDefinition> Assembly) {BarrierIntroAssembly=Assembly;}
#endif
private:
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> ShieldVisual;
 UPROPERTY(Transient) TObjectPtr<AEGCorruptionBarrierVisual> BarrierVisual;
 UPROPERTY(Transient) TObjectPtr<AEGCorruptionBarrier> BarrierActor;
 UPROPERTY(Transient) TObjectPtr<AEGCorruptionBossVisual> BossVisual;
 TWeakObjectPtr<AEGCorruptionSeed> VisualSeed;
 TWeakObjectPtr<AEGCorruptionPhantomVisual> CurrentPhantom;
 FGuid RoundId;
 EEGCorruptionRole CopyRole=EEGCorruptionRole::Melee;
 bool bBoss=false,bCopy=false,bPhaseTwo=false;
 bool bPhaseShieldPending=false;
 bool bBarrierBroken=false,bEnraged=false,bTransitionPending=false;
 EEGCorruptionBossPhase BossPhase=EEGCorruptionBossPhase::Finished;
 float BarrierHealth=0.f,BarrierMaxHealth=0.f,BarrierRemaining=0.f,WeaknessRemaining=0.f,EffectiveBattleSeconds=0.f;
 float BarrierIntroRemaining=0.f,BarrierIntroDuration=0.f;
 bool bBarrierIntroUsesSourcePose=false;
 bool bBarrierOpenSoundPlayed=false;
 double BarrierIntroStartReal=0.,BarrierIntroLastReal=0.;
 float SpreadClock=0.f,MimicClock=0.f,SpreadDelay=0.f;
 float MimicDelay=0.f;
 float MimicRecoveryRemaining=0.f,SpecialSkillGapRemaining=0.f;
 bool bFirstSpreadResolved=false,bMimicStarted=false,bJustCompletedSpecialSkill=false;
 EEGCorruptionBossCast ActiveBossCast=EEGCorruptionBossCast::None;
 FGuid ActiveBossCastAction;
 float ActiveBossCastDuration=0.f,ActiveBossCastElapsed=0.f;
 EEGHeroKind MimicKind=EEGHeroKind::Generic;
 FVector WallCenter=FVector::ZeroVector,WallWidthDirection=FVector::RightVector,WallNormal=FVector::ForwardVector,SpreadPoint=FVector::ZeroVector;
 float WallHalfWidth=0.f;
 FGuid BarrierBlockToken;
 struct FWallPushback
 {
  TWeakObjectPtr<AEGChessUnit> Unit;
  TWeakObjectPtr<AEGSkillProjectile> Flight;
  FVector Start=FVector::ZeroVector,End=FVector::ZeroVector,Landing=FVector::ZeroVector;
  FVector OriginalReserved=FVector::ZeroVector,OriginalPending=FVector::ZeroVector;
  TSharedPtr<FEGThrownSummonFlightState> OriginalFlight;
  bool bWidthFirst=false;
 };
 TArray<FWallPushback> WallPushbacks;
 int32 BarrierPushbackFallbackCount=0;
 float WallPushbackLastElapsed=0.f;
 FVector WallOriginalBossPosition=FVector::ZeroVector;
 TMap<TWeakObjectPtr<AEGChessUnit>,int32> MarkStacks;
 TSet<FGuid> MarkedActions;
 TWeakObjectPtr<AEGChessUnit> LastMimic;
 EEGHeroKind LastMimicKind=EEGHeroKind::Generic;
 int32 CurrentCorruption=50;
 float BaseMaxHealth=0.f,OpeningMaxHealth=0.f,OpeningThreshold=0.f,PendingPhaseShield=0.f,SeedClock=0.f,FallbackAttackClock=0.f;
 TSet<FGuid> UltimateActions;
 TArray<FGuid> UltimateActionOrder;
 FGuid LastCommittedAction;
 void BeginPhaseTwo();
 void EnterBarrier();
 void RecoverBarrierPushback();
 void BeginBossCast(EEGCorruptionBossCast Kind,float Duration,FGuid Action=FGuid());
 void EndBossCast(EEGCorruptionBossCastEvent Event);
 void FinishBarrier(bool bBroken);
 UFUNCTION() void AttributeChanged(FName Attribute,float OldValue,float NewValue);
 UFUNCTION() void BossDied(AEGChessUnit* Unit);
 bool IsCurrentBattle() const;
};
