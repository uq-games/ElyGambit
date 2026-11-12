#pragma once

#include "CoreMinimal.h"
#include "Types/EGChessTypes.h"
#include "EGMatchManager.generated.h"
class UEGRunDefinition;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGPhaseChanged, EEGMatchPhase, Phase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGStagePrepared, int32, StageNumber, EEGStageType, StageType);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGBattleResolved, bool, bVictory, int32, Corruption);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGCorruptionChanged, int32, Current, int32, Delta);

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGMatchManager : public UObject
{
	GENERATED_BODY()

public:
	bool ConfigureRun(int32 InStageCount, bool bInEndOnDefeat);
	bool ConfigureRunDefinition(UEGRunDefinition* Definition);
	bool CompleteRun();
	bool RetryStage();
	bool StartRun();
	bool EnterCombat();
	bool ResolveCombat(bool bVictory);
	bool NextStage();
	bool ApplySettlementImpact();
	bool FinishSettlement();
	bool EnterReward();
	bool CompleteReward();
	int32 AddCorruption(int32 Delta);
	UFUNCTION(BlueprintPure, Category="AutoChess|Stigmata") bool HasStigmata(FName Id) const { return StigmataOwned.Contains(Id); }
	UFUNCTION(BlueprintPure, Category="AutoChess|Stigmata") TArray<FName> GetStigmataOwned() const { return StigmataOwned; }
	UFUNCTION(BlueprintPure, Category="AutoChess|Stigmata") FName GetEndgameRoute() const { return EndgameRoute; }
	UFUNCTION(BlueprintPure, Category="AutoChess|Stigmata") int64 GetStigmataGoldEarned(FName Id) const { return StigmataGoldEarned.FindRef(Id); }
	UFUNCTION(BlueprintPure, Category="AutoChess|Stigmata") int64 GetStigmataRefreshSavings(FName Id) const { return StigmataRefreshSavings.FindRef(Id); }
	/** Called only by the authority that has completed the corresponding transaction. */
	void RecordStigmataGoldEarned(FName Id,int32 Amount);
	void RecordStigmataRefreshSavings(FName Id,int32 Amount);
#if WITH_DEV_AUTOMATION_TESTS
	/** Controlled A/B fixture only; never grants cards, currency or route effects. */
	bool SetStigmataForBalanceTests(const TArray<FName>& Ids);
#endif
	bool IsStigmataCommitting() const { return bStigmataCommitting; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	EEGMatchPhase GetPhase() const { return Phase; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	int32 GetStageNumber() const { return StageIndex + 1; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	EEGStageType GetStageType() const;
    UFUNCTION(BlueprintPure,Category="AutoChess|Match") EEGStageType GetStageTypeAt(int32 StageNumber) const;
    UFUNCTION(BlueprintPure,Category="AutoChess|Match") int32 GetStageCount() const {return StageCount;}

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	int32 GetCorruption() const { return Corruption; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	bool HasStarted() const { return bStarted; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	bool DidWinRun() const { return bWonRun; }

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Match")
	FEGPhaseChanged OnPhaseChanged;

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Match")
	FEGStagePrepared OnStagePrepared;

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Match")
	FEGBattleResolved OnBattleResolved;

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Match")
	FEGCorruptionChanged OnCorruptionChanged;

private:
	friend class AEGDemoGameMode;
	friend class FEGStigmataCatalogTest;
	friend class FEGSevenStageMatchFlowTest;
	bool BeginStigmataCommit(FName Id);
	bool CommitStigmata(bool bStayInReward=false);
	bool CommitEndgameRoute(FName Id);
	void AbortStigmataCommit();
	UPROPERTY(Transient) TObjectPtr<UEGRunDefinition> RunDefinition;
	UPROPERTY()
	EEGMatchPhase Phase = EEGMatchPhase::Finished;

	UPROPERTY()
	int32 StageIndex = 0;

	UPROPERTY()
	int32 Corruption = 0;

	UPROPERTY()
	bool bStarted = false;

	UPROPERTY()
	bool bWonRun = false;
	int32 StageCount = 2;
	bool bEndOnDefeat = false;
	bool bTransitioning = false;
	int32 PendingCorruptionDelta = 0;
	bool bSettlementImpactApplied = false;
	UPROPERTY(Transient) TArray<FName> StigmataOwned;
	UPROPERTY(Transient) FName EndgameRoute;
	UPROPERTY(Transient) TMap<FName,int64> StigmataGoldEarned;
	UPROPERTY(Transient) TMap<FName,int64> StigmataRefreshSavings;
	FName PendingStigmataId;
	bool bStigmataCommitting = false;
	void SetPhase(EEGMatchPhase NewPhase);
	void PrepareStage();
};
