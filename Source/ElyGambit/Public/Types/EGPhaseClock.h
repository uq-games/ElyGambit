#pragma once

#include "CoreMinimal.h"
#include "Types/EGChessTypes.h"
#include "EGPhaseClock.generated.h"

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGPhaseClockView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) EEGMatchPhase Phase=EEGMatchPhase::Finished;
    UPROPERTY(BlueprintReadOnly) float RemainingRealSeconds=0.f;
    UPROPERTY(BlueprintReadOnly) float Normalized=0.f;
    UPROPERTY(BlueprintReadOnly) bool bOvertime=false;
    UPROPERTY(BlueprintReadOnly) float Speed=1.f;
    UPROPERTY(BlueprintReadOnly) bool bHasCountdown=false;
    UPROPERTY(BlueprintReadOnly) float NextCorruptionRemaining=0.f;
    UPROPERTY(BlueprintReadOnly) float CorruptionNormalized=0.f;
    UPROPERTY(BlueprintReadOnly) bool bHasCorruptionCountdown=false;
};
