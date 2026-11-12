#pragma once
#include "CoreMinimal.h"
#include "EGElysiaPassiveState.generated.h"

/** Read-only view of the current combat behavior; it does not own gameplay state. */
UENUM(BlueprintType)
enum class EEGElysiaPassivePhase : uint8
{
 Inactive, Normal, Empowered, Bow, Entering, Origin, FinisherPending, Finishing
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGElysiaPassiveState
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) EEGElysiaPassivePhase Phase=EEGElysiaPassivePhase::Inactive;
 /** Completed attacks in the current cycle, starting at zero. */
 UPROPERTY(BlueprintReadOnly) int32 Progress=0;
 UPROPERTY(BlueprintReadOnly) int32 Threshold=0;
 UPROPERTY(BlueprintReadOnly) bool bNextAttackEnhanced=false;
 UPROPERTY(BlueprintReadOnly) float RemainingSeconds=0.f;
 UPROPERTY(BlueprintReadOnly) FGuid RoundId;
 UPROPERTY(BlueprintReadOnly) FGuid ActionId;
};
