#pragma once
#include "CoreMinimal.h"
#include "Game/EGChessGameMode.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Types/EGPhaseClock.h"
#include "EGDemoGameMode.generated.h"

class AEGCardManager;
class AEGBoardSceneSettings;
class UEGDemoPanel;
class UEGRunDefinition;
class UEGCardDefinition;
class UEGDeploymentComponent;
class UEGRunResourceComponent;
class UGameplayEffect;
class AEGCorruptionMine;
class AEGSettlementPresentation;
class UEGOpeningPresentation;
struct FStreamableHandle;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEGRunViewChanged);
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGHeroCombatStatistics
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FGuid HeroId;
 UPROPERTY(BlueprintReadOnly) FText Name;
 UPROPERTY(BlueprintReadOnly) float Damage=0;
 UPROPERTY(BlueprintReadOnly) float Taken=0;
 UPROPERTY(BlueprintReadOnly) float Healing=0;
 UPROPERTY(BlueprintReadOnly) float Shield=0;
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGRouteStigmataPreview
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FName StigmataId;
 UPROPERTY(BlueprintReadOnly) FText Status;
 UPROPERTY(BlueprintReadOnly) FText Explanation;
};

/** Run orchestration. Card transactions, deployment synchronization and GAS execution are separate. */
UCLASS()
class ELYGAMBIT_API AEGDemoGameMode : public AEGChessGameMode
{
    GENERATED_BODY()
public:
    AEGDemoGameMode();
    /** Unpaused, undilated duration of the seven-stage settlement phase. */
    static constexpr float SettlementDurationSeconds=6.f;
    virtual void StartPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual bool StartGame(bool bResetRoster=true) override;
    virtual bool EnterCombat() override;
    virtual bool NextStage() override;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Demo") TObjectPtr<UEGRunDefinition> RunDefinition;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Demo|Reward") TSoftObjectPtr<UEGCardDefinition> HumanFusionCard;
    UPROPERTY(BlueprintAssignable,Category="Demo") FEGRunViewChanged OnRunViewChanged;
    UPROPERTY(BlueprintAssignable,Category="Demo") FEGRunViewChanged OnBattleClockChanged;
    UPROPERTY(BlueprintAssignable,Category="Demo") FEGRunViewChanged OnPhaseClockChanged;
    UPROPERTY(BlueprintAssignable,Category="Demo") FEGRunViewChanged OnCombatStatisticsChanged;
    UFUNCTION(BlueprintPure,Category="Demo") TArray<FEGHeroCombatStatistics> GetCombatStatistics() const;
    UFUNCTION(BlueprintPure,Category="Demo") bool CanPrepare() const;
    UFUNCTION(BlueprintPure,Category="Demo") bool AreResourcesReady() const {return bResourcesReady;}
    bool AreStageResourcesReady() const {return bStageReady&&!IsPresentationInputBlocked();}
    /** The formal entry opts into this flow through ?EGOpening=1. */
    UFUNCTION(BlueprintPure,Category="Opening") bool IsPresentationInputBlocked() const;
    UFUNCTION(BlueprintPure,Category="Opening") bool IsOpeningPreloadReady() const;
    UFUNCTION(BlueprintPure,Category="Opening") FString GetOpeningPreloadError() const;
    UFUNCTION(BlueprintPure,Category="Opening") bool IsOpeningStartupFailure() const {return !OpeningStartupError.IsEmpty();}
    UFUNCTION(BlueprintCallable,Category="Opening") bool RetryOpeningPreload();
    UFUNCTION(BlueprintCallable,Category="Opening") bool RequestOpeningStart();
    UFUNCTION(BlueprintPure,Category="Opening") bool IsStageEntranceRetryNeeded() const {return bStageEntrancePending&&!StageEntranceError.IsEmpty();}
    UFUNCTION(BlueprintPure,Category="Opening") FString GetStageEntranceError() const {return StageEntranceError;}
    UFUNCTION(BlueprintCallable,Category="Opening") bool RetryStageEntrance();
    FGuid GetOpeningToken() const {return OpeningToken;}
    void CompleteOpeningPresentation(FGuid Token);
    void CompleteStageTransition(FGuid Token);
    void CompleteStageEntrance(FGuid Token);
    /** Queue optional background presentation work; never blocks a valid deployment. */
    UFUNCTION(BlueprintCallable,Category="Demo") bool EnsureHeroResources(UEGHeroDefinition* Definition);
    UFUNCTION(BlueprintPure,Category="Demo") bool WasVictory() const {return bLastVictory;}
    UFUNCTION(BlueprintPure,Category="Demo|Reward") int32 GetStigmaSkipRevision() const {return StigmaSkipRevision;}
    UFUNCTION(BlueprintCallable,Category="Demo|Reward") bool SkipStigma(int32 ExpectedRevision);
    UFUNCTION(BlueprintPure,Category="Demo|Reward") TArray<FName> GetStigmataOffer() const { return StigmataOffer; }
    UFUNCTION(BlueprintPure,Category="Demo|Reward") int32 GetStigmataRevision() const { return StigmaSkipRevision; }
    UFUNCTION(BlueprintCallable,Category="Demo|Reward") bool ChooseStigmata(FName Id,int32 ExpectedRevision);
    UFUNCTION(BlueprintPure,Category="Demo|Reward") bool IsChoosingEndgameRoute() const { return bChoosingEndgameRoute; }
    /** A route must not be offered until the pre-final recovery checkpoint exists. */
    UFUNCTION(BlueprintPure,Category="Demo|Reward") bool IsEndgameCheckpointRetryNeeded() const { return bEndgameCheckpointRetryNeeded; }
    UFUNCTION(BlueprintCallable,Category="Demo|Reward") bool RetryEndgameCheckpoint();
    UFUNCTION(BlueprintPure,Category="Demo|Reward") TArray<FName> GetEndgameRouteOffer() const;
    UFUNCTION(BlueprintPure,Category="Demo|Reward") TArray<FEGRouteStigmataPreview> GetEndgameRoutePreview(FName RouteId) const;
    UFUNCTION(BlueprintCallable,Category="Demo|Reward") bool ChooseEndgameRoute(FName Id,int32 ExpectedRevision);
    UFUNCTION(BlueprintPure,Category="Demo") float GetBattleSeconds() const {return BattleSeconds;}
    UFUNCTION(BlueprintPure,Category="Demo") int32 GetBalanceSeed() const {return BalanceSeed;}
    UFUNCTION(BlueprintPure,Category="Demo") FEGPhaseClockView GetPhaseClock() const;
    UFUNCTION(BlueprintPure,Category="Demo") FString GetHint() const {return Hint;}
    UFUNCTION(BlueprintPure,Category="Demo") AEGCardManager* GetCards() const {return Cards;}
    UFUNCTION(BlueprintPure,Category="Demo") UEGDemoPanel* GetDemoPanel() const;
    UFUNCTION(BlueprintPure,Category="Demo") TArray<AEGChessUnit*> GetCombatUnits() const;
    UFUNCTION(BlueprintCallable,Category="Demo") bool AdvanceDemo();
    /** Separate from restarting the run: retries only a finished, lost stage. */
    UFUNCTION(BlueprintPure,Category="Demo") bool CanRetryCurrentStage() const;
    UFUNCTION(BlueprintCallable,Category="Demo") bool RetryCurrentStage();
    /** Only a failed final battle can consume the pre-route checkpoint. */
    UFUNCTION(BlueprintPure,Category="Demo|Reward") bool CanRestoreEndgameCheckpoint() const;
    UFUNCTION(BlueprintCallable,Category="Demo|Reward") bool RestoreEndgameCheckpoint();
    void SetHint(const FString& Message);
    /** Recover a failed encounter setup without awarding a result or charging corruption. */
    void RequestEncounterRecovery(FGuid ExpectedRound);
protected:
    virtual void PrepareStageEnemies() override;
    virtual void PrepareUnitForCombat(AEGChessUnit* Unit,const FEGHeroInstance& Hero) override;
    virtual void PrepareSummonForCombat(AEGChessUnit* Unit) override;
    virtual void PrepareBattleOpening() override;
private:
#if WITH_DEV_AUTOMATION_TESTS
 friend class FEGSevenStageBossWaveRetryTest;
 friend class FEGOpeningFlowTransactionTest;
#endif
 UPROPERTY(Transient) TArray<TObjectPtr<AEGCorruptionMine>> StageTraps;
 void ClearStageTraps();
    UPROPERTY(Transient) TMap<FGuid,FEGHeroCombatStatistics> CombatStatistics;
    UFUNCTION() void TrackCombatResolution(const FEGCombatResolution& Result);
    UPROPERTY(VisibleAnywhere) TObjectPtr<UEGDeploymentComponent> Deployment;
    UPROPERTY(Transient) TObjectPtr<AEGCardManager> Cards;
    UPROPERTY(Transient) TObjectPtr<AEGBoardSceneSettings> Scene;
    UPROPERTY(Transient) TObjectPtr<UEGDemoPanel> Panel;
    bool bConfigured=false;
    bool bLastVictory=false;
    bool bDuskStreetSettlementEligible=false;
    bool bStageReady=false;
    bool bPreparingStage=false;
    UPROPERTY(Transient) TObjectPtr<UEGOpeningPresentation> OpeningPresentation;
    UPROPERTY(Transient) TArray<TObjectPtr<UEGHeroDefinition>> OpeningModelHeroes;
    UPROPERTY(Transient) TArray<TObjectPtr<UEGHeroDefinition>> OpeningPreloadHeroes;
    bool bPresentationFlowActive=false;
    bool bOpeningErrorWidgetPending=false;
    bool bOpeningPending=false;
    bool bOpeningLoadRequested=false;
    bool bOpeningPreloadStarted=false;
    bool bOpeningCommitInProgress=false;
    bool bStageTransitionPending=false;
    bool bStageEntrancePending=false;
    bool bRetryingStageEntrance=false;
    int32 TransitionFromStage=INDEX_NONE;
    FGuid OpeningToken;
    FGuid TransitionToken;
    FGuid EntranceToken;
    FString OpeningStartupError;
    FString StageEntranceError;
    void BeginStageEntranceOrWait();
    void TryInstallOpeningErrorWidget();
    void StartOpeningPreload();
    bool CommitNextStage();
    UPROPERTY(VisibleAnywhere) TObjectPtr<UEGRunResourceComponent> Resources;
    bool bAppliedFrameCap=false;
    float PreviousFrameCap=0.f;
    UPROPERTY(Transient) bool bResourcesReady=false;
    void RefreshBackgroundResourceRequests();
    void SpawnPreparedStageEnemies();
    UPROPERTY(Transient) TArray<FEGHeroInstance> StageSnapshot;
    UPROPERTY(Transient) FEGHeroRosterRunSnapshot EndgameRosterCheckpoint;
    UPROPERTY(Transient) FEGCardRunCheckpoint EndgameCardsCheckpoint;
    UPROPERTY(Transient) TArray<FEGHeroInstance> EndgameEncounterHistory;
    UPROPERTY(Transient) TMap<FGuid,FEGHeroCombatStatistics> EndgameCombatStatistics;
    UPROPERTY(Transient) TArray<FName> EndgameOrdinaryStigmata;
    UPROPERTY(Transient) TMap<FName,int64> EndgameStigmataGold;
    UPROPERTY(Transient) TMap<FName,int64> EndgameStigmataSavings;
    TArray<FEGStigmataContribution> EndgameContributions;
    FRandomStream EndgameEncounterRandom,EndgameStigmataRandom;
    int32 EndgameCorruption=0;
    bool bEndgameCheckpointValid=false;
    bool bEndgameCheckpointRetryNeeded=false;
    bool bFinalBossCombatEntered=false;
    bool bEndgameLastVictory=false;
    bool CaptureEndgameCheckpoint();
    int32 LastClockSecond=INDEX_NONE;
    float BattleSeconds=0.f;
    float PreparationRealSeconds=0.f;
    float CombatRealSeconds=0.f;
    float CorruptionPeriodStartRealSeconds=0.f;
    double LastAudioClockSeconds=-1.0;
    float OriginalGlobalTimeDilation=1.f;
    float AppliedGlobalTimeDilation=1.f;
    bool bOwnsOvertimeDilation=false;
    bool bPreparationAutoStartAttempted=false;
    int32 ClockStageNumber=INDEX_NONE;
    FGuid RunningRoundId;
    UFUNCTION() void EncounterCombatRunning(FGuid RoundId);
    void RestoreOvertimeDilation();
    void ResetPhaseClock();
    int32 BossCorruptionTicks=0;
    uint64 BossDeathFrame=MAX_uint64;
    UFUNCTION() void RecordBossDeath(AEGChessUnit* Unit);
    int32 StigmaSkipRevision=0;
    UPROPERTY(Transient) TArray<FName> StigmataOffer;
    bool bChoosingStigmata=false;
    bool bChoosingEndgameRoute=false;
    void GenerateStigmataOffer(int32 Stage);
    int32 PresentedStigmaRevision=INDEX_NONE;
    void BeginStigmaChoice();
    void SyncStigmaPanel();
    UFUNCTION() void StigmaSkipRequested(int32 Revision);
    UPROPERTY(Transient) TObjectPtr<AEGSettlementPresentation> SettlementPresentation;
    double SettlementStartAudioSeconds=-1.0;
    float PreviousSettlementTickInterval=0.f;
    bool bSettlementClockActive=false;
    float GetSettlementElapsedSeconds() const;
    void ResetSettlementClock();
    void BeginSettlementPresentation(bool bVictory);
    void SettlementImpact();
    void SettlementFinished();
    void ClearSettlementPresentation();
    bool TransformEncounterCopies();
    void ActivateEncounterCombat();
    void ActivateEncounterCopy(AEGChessUnit* Unit);
    void UpdateBossCorruption();
    void SpawnBossWave();
    void RetryBossWave();
    void ClearBossWave();
    UFUNCTION() void BossWaveBossDied(AEGChessUnit* Unit);
    struct FBossWaveSpawn
    {
        TWeakObjectPtr<UEGHeroDefinition> Definition;
        int32 Level=1;
        FIntPoint Cell=FIntPoint::ZeroValue;
        bool bFlower=false;
        bool bPhantom=false;
    };
    TArray<FBossWaveSpawn> PendingBossWave;
    FGuid BossWaveRound;
    FGuid BossWaveStartedRound;
    FTimerHandle BossWaveRetryTimer;
    void ResetEncounterHistory();
    UPROPERTY(Transient) TArray<FEGHeroInstance> EncounterHistory;
    UPROPERTY(Transient) TMap<FGuid,TObjectPtr<AEGChessUnit>> EncounterDisguises;
    TSet<FIntPoint> EncounterDeviceCells;
    void ClearEncounterDisguises();
    void RestoreEncounterAfterFailedStart();
    FRandomStream EncounterRandom;
    FRandomStream StigmataRandom;
    int32 BalanceSeed=0;
    TWeakObjectPtr<AEGChessUnit> EncounterBoss;
    TArray<TWeakObjectPtr<AEGChessUnit>> EncounterCopies;
    FString Hint;
    UFUNCTION() void BattleResolved(bool bVictory,int32 Corruption);
    UFUNCTION() void RunPhaseChanged(EEGMatchPhase Phase);
    UFUNCTION() void CardFeedback();
};
