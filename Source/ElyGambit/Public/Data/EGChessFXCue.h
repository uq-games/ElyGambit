#pragma once
#include "CoreMinimal.h"
#include "EGChessFXCue.generated.h"

class UNiagaraSystem;

/** A resident cosmetic cue. No targeting, collision or damage is driven by this data. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGChessFXCue
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UNiagaraSystem> System;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector Offset=FVector::ZeroVector;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FRotator Rotation=FRotator::ZeroRotator;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector Scale=FVector::OneVector;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.01")) float Duration=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName Bone;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bAttachToOwner=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bActionBound=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bActionClock=false;
};
