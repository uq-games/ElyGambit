#pragma once
#include "GameplayEffect.h"
#include "EGEdenManaEffect.generated.h"

/** Target-owned periodic mana; its lifetime does not depend on the singer. */
UCLASS()
class ELYGAMBIT_API UEGEdenManaEffect : public UGameplayEffect
{
 GENERATED_BODY()
public:
 UEGEdenManaEffect();
};
