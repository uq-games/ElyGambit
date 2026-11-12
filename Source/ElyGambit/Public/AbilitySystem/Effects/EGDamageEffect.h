#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EGDamageEffect.generated.h"

/** Instant health delta supplied by AutoChess.Data.Damage (negative values deal damage). */
UCLASS()
class ELYGAMBIT_API UEGDamageEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEGDamageEffect();
};
