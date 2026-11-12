#include "AbilitySystem/Effects/EGDamageEffect.h"

#include "EGGameplayTags.h"
#include "AbilitySystem/EGAttributeSet.h"

UEGDamageEffect::UEGDamageEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UEGAttributeSet::GetHealthAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Caller;
	Caller.DataTag = EGTags::Damage;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Caller);
	Modifiers.Add(Modifier);
}
