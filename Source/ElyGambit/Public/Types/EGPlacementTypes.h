#pragma once
#include "CoreMinimal.h"
#include "EGPlacementTypes.generated.h"

UENUM(BlueprintType)
enum class EEGPlacementAction : uint8 { Invalid, Deploy, Merge, Move };

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGPlacementEvaluation
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly) EEGPlacementAction Action = EEGPlacementAction::Invalid;
	UPROPERTY(BlueprintReadOnly) FText Message;
	/** Resolved by CardManager; merge feedback and commit use the same destination. */
	UPROPERTY(BlueprintReadOnly) FGuid TargetHeroId;
	UPROPERTY(BlueprintReadOnly) FIntPoint TargetCell = FIntPoint(INDEX_NONE,INDEX_NONE);
	bool IsAllowed() const { return Action != EEGPlacementAction::Invalid; }
};
