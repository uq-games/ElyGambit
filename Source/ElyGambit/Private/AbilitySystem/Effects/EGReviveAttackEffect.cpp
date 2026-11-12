#include "AbilitySystem/Effects/EGReviveAttackEffect.h"
#include "AbilitySystem/EGAttributeSet.h"
UEGReviveAttackEffect::UEGReviveAttackEffect()
{
 DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo Modifier;
 Modifier.Attribute=UEGAttributeSet::GetAttackPowerAttribute();
 Modifier.ModifierOp=EGameplayModOp::Multiplicitive;
 FSetByCallerFloat Magnitude;Magnitude.DataName=TEXT("ReviveAttackMultiplier");
 Modifier.ModifierMagnitude=FGameplayEffectModifierMagnitude(Magnitude);Modifiers.Add(Modifier);
}
