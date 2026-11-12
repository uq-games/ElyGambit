#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Presentation/EGSkillProjectile.h"
#include "Types/EGChessTypes.h"
#include "GameplayEffectTypes.h"
#include "EGBattleContextComponent.generated.h"
class AEGChessUnit;
class UEGHeroDefinition;
class AEGChessGameMode;
class UEGHeroSkillBehavior;
class UEGHeroSkillComponent;
class UEGStigmataBattleComponent;
class APlayerController;
class USkeletalMeshComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGStigmataTriggered, FName, Id, AEGChessUnit*, Source);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGCombatRunning, FGuid, RoundId);

/** One battle's roster rules, counters and summon registry; never a second combat simulator. */
UCLASS()
class ELYGAMBIT_API UEGBattleContextComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGBattleContextComponent();
 void Start(const TArray<FEGHeroInstance>& Heroes);
 void Stop();
 /** Set once before the opening starts; Stop clears the combat-only snapshot. */
 bool SetEnemyDamageCorruptionSnapshot(int32 Corruption,bool bEnabled);
 float GetEnemyDamageMultiplier() const {return EnemyDamageMultiplier;}
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") bool IsBattleActive() const {return bActive;}
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") bool IsCombatRunning() const {return bActive&&bRunning&&PresentationHolds.IsEmpty();}
 /** Cosmetic phase transitions may advance during our own story pause, never an independent pause. */
 bool CanAdvancePresentationVisuals() const;
 /** A staged visual may advance only while its own hold is the sole hold. */
 bool CanAdvancePresentationVisualsForToken(FGuid Token) const;
 /** Story can play before combat starts, subject to the same pause ownership check. */
 bool CanAdvancePresentationClock() const;
 /** Action-scoped presentation gate with a precisely owned standalone pause request. */
 void SetPresentationHold(FGuid Token,bool bHold);
 bool HasPresentationHold() const {return !PresentationHolds.IsEmpty();}
 bool HasPresentationHold(FGuid Token) const {return Token.IsValid()&&PresentationHolds.Contains(Token);}
 UPROPERTY(BlueprintAssignable,Category="AutoChess|Battle") FEGCombatRunning OnCombatRunning;
 /** Register before BeginOpening. Unit callbacks share one item; team callbacks each have one item. */
 void EnqueueOpening(AEGChessUnit* Unit,TFunction<void()> Action,bool bFirst=false);
 void EnqueueTeamOpening(TFunction<void()> Action);
 void EnqueueFinalOpening(TFunction<void()> Action);
 void BeginOpening();
#if WITH_DEV_AUTOMATION_TESTS
 /** Controlled legacy combat fixtures only; production and timing tests must use Tick. */
 void CompleteOpeningForTests();
 void AdvanceOpeningForTests(float RealSeconds);
#endif
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") FGuid GetRoundId() const {return RoundId;}
 /** Exclusive, action-scoped world pause for Human's form-entry cinematic. */
 bool BeginHumanEntryPause(AEGChessUnit* Unit,FGuid Round,FGuid Action);
 void EndHumanEntryPause(FGuid Round,FGuid Action);
 bool IsHumanEntryPauseOwner(const AEGChessUnit* Unit,FGuid Action) const;
 bool HasHumanEntryPause() const {return bHumanPauseOwned;}
 void PermitHumanEntryVisualTick(AActor* Visual,FGuid Round,FGuid Action);
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") int32 GetSummonBonus() const {return SummonBonus;}
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") int32 GetAttackCount() const {return AttackCount;}
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") int32 GetSuccessfulUltimateCount() const {return SuccessfulUltimateActions.Num();}
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") float GetFirstUltimateBattleSeconds() const {return FirstUltimateBattleSeconds;}
 void RecordSuccessfulUltimate(AEGChessUnit* Unit,FGuid ActionId);
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") TArray<AEGChessUnit*> Units() const;
 UPROPERTY(BlueprintAssignable,Category="AutoChess|Battle") FEGStigmataTriggered OnStigmataTriggered;
 UEGStigmataBattleComponent* GetStigmata() const {return Stigmata;}
 void BasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid AttackId);
 bool Damage(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,FGuid ActionId=FGuid(),bool bBasicAttack=false);
 void Heal(AEGChessUnit* Target,float Amount);
 void Heal(AEGChessUnit* Source,AEGChessUnit* Target,float Amount);
 void Visual(AEGChessUnit* Source,FVector Point,float Size,bool Area=false);
 void Area(AEGChessUnit* Source,FVector Point,float Radius,float Raw,bool Cone=false,bool Taunt=false,FGuid ActionId=FGuid());
 AEGChessUnit* SpawnSummon(UEGHeroDefinition* Definition,FGuid OwnerId,int32 OwnerLevel,FVector Point,bool InFlight=false);
 /** One successful summon per owner/round. Death spends the summon; failed placement rolls back. */
 AEGChessUnit* SpawnKnight(AEGChessUnit* Summoner,float HealthRatio,float AttackRatio);
 void RegisterAdultKnightAura(AEGChessUnit* Knight,float Bonus);
 AEGChessUnit* FindCan(FGuid OwnerId) const;
 int32 SummonCount() const;
 void RefreshTeam();
 void RefreshProtectionFields();
 void EnterUnit(AEGChessUnit* Unit);
 float TeamSpeed() const;
 bool HasLivingCan(FGuid Id) const;
 bool HasLivingSummoner(FGuid Id) const;
 bool AwardBattleGold(AEGChessUnit* Source,int32 Amount);
 /** Optional non-authoritative receipt, invoked after each successful GE application. */
 void ApplyTeamSpeed(AEGChessUnit* Source,float Bonus,float Duration,bool bSnapshotOnly=false,bool bHeroesOnly=false,TFunction<void(AEGChessUnit*)> OnApplied={});
 void EffectiveBasicHeal(AEGChessUnit* Source,FGuid ActionId,float RestoredHealth);
 void ApplyEdenSong(AEGChessUnit* Source,float ManaPerSecond,int32 Seconds);
 UFUNCTION(BlueprintPure,Category="AutoChess|Battle") float GetTeamBuffSeconds() const;
 float GetTeamBuffSecondsFor(EEGFaction Faction) const;
 int32 GetAwardedGold() const {return AwardedGold;}
 int32 ConsumeAwardedGold() {const int32 Result=AwardedGold;AwardedGold=0;return Result;}
 void Launch(AEGChessUnit* Source,AEGChessUnit* Target,FVector Point,float Damage,EEGProjectileKind Kind,FGuid AttackId,AEGChessUnit* Cargo=nullptr);
private:
 friend class FEGOpeningQueueTest;
#if WITH_DEV_AUTOMATION_TESTS
 friend class FEGStigmataCombatTest;
#endif
 UPROPERTY(Transient) TObjectPtr<UEGStigmataBattleComponent> Stigmata;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessGameMode> Mode;
 UPROPERTY(Transient) TArray<FGuid> DeployedIds;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGChessUnit>> Summons;
 TSet<FGuid> KnightOwners;
 TMap<TWeakObjectPtr<AEGChessUnit>,float> KnightAuras;
 TMap<TWeakObjectPtr<AEGChessUnit>,FActiveGameplayEffectHandle> KnightAuraEffects;
 TMap<TWeakObjectPtr<AEGChessUnit>,float> KnightAuraBonuses;
 void RefreshKnightAuras();
 AEGChessUnit* SpawnSummonConfigured(UEGHeroDefinition* Definition,FGuid OwnerId,int32 OwnerLevel,FVector Point,bool InFlight,float HealthRatio,float AttackRatio);
 UFUNCTION() void SummonDestroyed(AActor* Actor);
 FGuid RoundId;
 TSet<FGuid> CountedAttacks;
 TSet<FGuid> SuccessfulUltimateActions;
 float FirstUltimateBattleSeconds=-1.f;
 double CombatStartedAtWorld=0.;
 TSet<FGuid> Entered;
 TSet<FGuid> GoldRecipients;
 int32 AwardedGold=0;
 int32 SummonBonus=0,AttackCount=0;
 float EnemyDamageMultiplier=1.f;
 bool bActive=false,bRefreshing=false;
 TSet<FGuid> PresentationHolds;
 TWeakObjectPtr<APlayerController> PresentationPauseController;
 FDelegateHandle PresentationPauseRequestHandle;
 bool bPresentationPauseOwned=false;
 bool CanReleasePresentationPause() const {return PresentationHolds.IsEmpty();}
 void SyncPresentationPause();
 TWeakObjectPtr<AEGChessUnit> HumanPauseUnit;
 TWeakObjectPtr<APlayerController> HumanPauseController;
 TWeakObjectPtr<USkeletalMeshComponent> HumanPauseMesh;
 TWeakObjectPtr<UEGHeroSkillComponent> HumanPauseSkill;
 FGuid HumanPauseRound,HumanPauseAction;
 bool bHumanPauseOwned=false,bHumanPausePending=false,bHumanPauseContextTick=false,bHumanPauseMeshTick=false,bHumanPauseSkillTick=false;
 FDelegateHandle HumanPausePendingDelegate;
 FDelegateHandle HumanPauseRequestHandle;
 double HumanPauseStartedReal=0.;
 struct FHumanPauseVisualTick {TWeakObjectPtr<AActor> Actor;bool bOriginal=false;};
 TArray<FHumanPauseVisualTick> HumanPauseVisualTicks;
 bool CanReleaseHumanEntryPause() const {return !bHumanPauseOwned;}
 void OnHumanPauseWorldTick(UWorld* World,ELevelTick TickType,float DeltaSeconds);
 struct FOpeningItem
 {
  TWeakObjectPtr<AEGChessUnit> Unit;
  TArray<TFunction<void()>> Actions;
  int32 Group=0,Row=0,Column=0,Sequence=0;
 };
 TArray<FOpeningItem> OpeningItems;
 int32 OpeningIndex=0;
 bool bRunning=false,bOpeningStarted=false;
 double OpeningElapsed=0;
 double OpeningLastAudioTime=0;
 void AccumulateOpeningTime(double Delta);
 void AdvanceOpening();
 bool bRefreshingProtection=false;
 TMap<TWeakObjectPtr<AEGChessUnit>,FActiveGameplayEffectHandle> TeamSpeedEffects;
 TMap<TWeakObjectPtr<AEGChessUnit>,FActiveGameplayEffectHandle> EdenSongEffects;
 struct FTeamSpeedState {double End=0;float Bonus=0;bool bSnapshotOnly=false,bHeroesOnly=false;};
 TMap<EEGFaction,FTeamSpeedState> TeamSpeedStates;
 bool ApplyTeamSpeedTo(AEGChessUnit* Receiver,float Duration,float Bonus);
 UFUNCTION() void CombatResolved(const FEGCombatResolution& Receipt);
 UFUNCTION() void SummonDied(AEGChessUnit* Unit);
};
