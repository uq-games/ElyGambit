#include "AbilitySystem/Effects/EGTeamSpeedEffect.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "EGGameplayTags.h"
UEGTeamSpeedEffect::UEGTeamSpeedEffect()
{
 DurationPolicy=EGameplayEffectDurationType::HasDuration;
 DurationMagnitude=FScalableFloat(6.f);
 FGameplayModifierInfo Modifier;
 Modifier.Attribute=UEGAttributeSet::GetAttackSpeedAttribute();
 Modifier.ModifierOp=EGameplayModOp::Multiplicitive;
 FSetByCallerFloat Value;Value.DataTag=EGTags::TeamAttackSpeed;
 Modifier.ModifierMagnitude=FGameplayEffectModifierMagnitude(Value);
 Modifiers.Add(Modifier);
}
