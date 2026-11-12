#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EGManaEffect.generated.h"

/** Instant signed mana delta supplied by AutoChess.Data.Mana. */
UCLASS()
class ELYGAMBIT_API UEGManaEffect : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UEGManaEffect();
};
