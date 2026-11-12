#pragma once
#include "GameplayEffect.h"
#include "EGProtectionEffect.generated.h"

/** Source-owned protection receipt. The damage resolver takes the strongest active value. */
UCLASS()
class ELYGAMBIT_API UEGProtectionEffect : public UGameplayEffect
{
 GENERATED_BODY()
public:
 UEGProtectionEffect();
};
