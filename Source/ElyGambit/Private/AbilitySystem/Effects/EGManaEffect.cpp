#include "AbilitySystem/Effects/EGManaEffect.h"

#include "EGGameplayTags.h"
#include "AbilitySystem/EGAttributeSet.h"

UEGManaEffect::UEGManaEffect()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UEGAttributeSet::GetManaAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Caller;
	Caller.DataTag = EGTags::Mana;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Caller);
	Modifiers.Add(Modifier);
}
