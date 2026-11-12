#include "AbilitySystem/Effects/EGProtectionEffect.h"
UEGProtectionEffect::UEGProtectionEffect()
{
 DurationPolicy=EGameplayEffectDurationType::HasDuration;
 DurationMagnitude=FScalableFloat(1.f);
}
