#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EGTeamSpeedEffect.generated.h"

/** One timed multiplier per receiver. Handles and refresh policy belong to the battle context. */
UCLASS()
class ELYGAMBIT_API UEGTeamSpeedEffect : public UGameplayEffect
{
 GENERATED_BODY()
public:
 UEGTeamSpeedEffect();
};
