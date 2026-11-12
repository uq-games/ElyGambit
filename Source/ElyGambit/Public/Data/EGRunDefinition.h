#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/EGChessTypes.h"
#include "EGRunDefinition.generated.h"

class UEGCardDefinition;
class UEGHeroDefinition;
class AEGDeploymentBoard;
class UEGEnvironmentTrapDefinition;

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGTrapSpawn
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UEGEnvironmentTrapDefinition> Definition;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FIntPoint Cell=FIntPoint::ZeroValue;
};

/** Zero limits preserve the standalone card-placement sandbox. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGCardRules
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 PurchasePrice = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 HandLimit = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 DeploymentLimit = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 SameNameLimit = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bAllowSameNameMerge = false;
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGEnemySpawn
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UEGHeroDefinition> Definition;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 Level = 1;
	/** Same column/row coordinates as placement, preview and board rendering. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FIntPoint Cell = FIntPoint::ZeroValue;
	/** A disguise slot replaced from the deployed roster at combat start. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bCorruptedCopy = false;
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGStageDefinition
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FEGEnemySpawn> Enemies;
	/** Per-stage private ordinary definitions for replacement and boss reinforcements. */
	UPROPERTY(Transient) TArray<TObjectPtr<UEGHeroDefinition>> OrdinaryMonsterDefinitions;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 LowCorruptionRemoveIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bHasHighCorruptionEnemy = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(EditCondition="bHasHighCorruptionEnemy")) FEGEnemySpawn HighCorruptionEnemy;
	/** Zero means no time limit for the seven-stage rules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) float StageBattleTimeLimit = 0.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGTrapSpawn> Traps;
    /** Optional description of implemented stage rules, shown without applying gameplay changes. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Preview") FText EffectPreview;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 VictoryGold = 0;
	/** Paid once after settlement regardless of victory or defeat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="0")) int32 CompletionGold = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FName StageId;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bUseConfiguredStageType=false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) EEGStageType StageType=EEGStageType::Normal;
};

/** A deterministic, one-time opening card grant, independent of the shop. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGStartingCardGrant
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly) TObjectPtr<UEGCardDefinition> Card;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(ClampMin="1")) int32 InitialLevel = 1;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGRunDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	/** Opt in to the seven-stage campaign rules. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run") bool bSevenStageRules = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0", ClampMax="200")) int32 StartingCorruption = 50;
	/** Negative selects a fresh seed for each run; nonnegative values reproduce the encounter sequence. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run") int32 EncounterSeed = -1;
	/** Enables the two-stage round shop flow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Board") bool bRoundShopRules = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Board") int32 RoundIncome = 6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run") TObjectPtr<UEGCardDefinition> StartingCard;
	/** Opt-in temporary opening lineup; legacy runs retain their normal random opening. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Opening") bool bUseExplicitStartingCards = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Opening", meta=(EditCondition="bUseExplicitStartingCards")) TArray<FEGStartingCardGrant> ExplicitStartingCards;
	/** Disables the stage population curve for this run without removing that future rule. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Population") bool bUseFixedPopulation = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Population", meta=(ClampMin="1", EditCondition="bUseFixedPopulation")) int32 FixedPopulation = 3;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Run") TArray<TObjectPtr<UEGCardDefinition>> CardPool;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0")) int32 StartingGold = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="1")) int32 StartingCards = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="1")) float BattleTimeLimit = 90.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run") FEGCardRules CardRules;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run") TArray<FEGStageDefinition> Stages;
	bool ValidateStartingCards(FString& Error) const;
	bool ValidateConfiguration(const AEGDeploymentBoard* Board, FString& Error) const;
	FName GetStageId(int32 Index) const;
	TArray<FSoftObjectPath> GatherRuntimeResources() const;
	UFUNCTION(BlueprintCallable,Category="Validation") bool ValidateDefinitionGraph(FString& Error,bool bCheckAnimation=true) const;
	bool IsOptionalResource(const FSoftObjectPath& Path) const;
};
