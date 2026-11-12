#include "AbilitySystem/Effects/EGEdenManaEffect.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "EGGameplayTags.h"
UEGEdenManaEffect::UEGEdenManaEffect()
{
 DurationPolicy=EGameplayEffectDurationType::HasDuration;
 DurationMagnitude=FScalableFloat(6.001f);
 Period=FScalableFloat(1.f);bExecutePeriodicEffectOnApplication=false;
 FGameplayModifierInfo Modifier;Modifier.Attribute=UEGAttributeSet::GetManaAttribute();Modifier.ModifierOp=EGameplayModOp::Additive;
 FSetByCallerFloat Value;Value.DataTag=EGTags::Mana;Modifier.ModifierMagnitude=FGameplayEffectModifierMagnitude(Value);Modifiers.Add(Modifier);
}
