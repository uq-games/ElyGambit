#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "EGGameplayTags.h"
#include "GameplayEffectComponents/TargetTagsGameplayEffectComponent.h"
namespace {
FGameplayModifierInfo Bonus(FGameplayAttribute Attribute)
{
 FGameplayModifierInfo M;M.Attribute=Attribute;M.ModifierOp=EGameplayModOp::Multiplicitive;
 FSetByCallerFloat Value;Value.DataName=TEXT("Support.Multiplier");M.ModifierMagnitude=FGameplayEffectModifierMagnitude(Value);return M;
}
}
UEGPaintAttackEffect::UEGPaintAttackEffect()
{
 DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(6.f);
 StackingType=EGameplayEffectStackingType::AggregateByTarget;StackLimitCount=1;
 StackDurationRefreshPolicy=EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
 Modifiers.Add(Bonus(UEGAttributeSet::GetAttackPowerAttribute()));
}
UEGHuaArmorEffect::UEGHuaArmorEffect()
{
 DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(6.f);
 StackingType=EGameplayEffectStackingType::AggregateByTarget;StackLimitCount=1;
 StackDurationRefreshPolicy=EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
 Modifiers.Add(Bonus(UEGAttributeSet::GetDefenseAttribute()));
}
UEGMeteorStunEffect::UEGMeteorStunEffect()
{
 DurationPolicy=EGameplayEffectDurationType::HasDuration;DurationMagnitude=FScalableFloat(1.5f);
 StackingType=EGameplayEffectStackingType::AggregateByTarget;StackLimitCount=1;
 StackDurationRefreshPolicy=EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication;
 FInheritedTagContainer Tags;Tags.AddTag(EGTags::ControlStun);
 auto* Granted=CreateDefaultSubobject<UTargetTagsGameplayEffectComponent>(TEXT("MeteorStunTags"));
 GEComponents.Add(Granted);Granted->SetAndApplyTargetTagChanges(Tags);
}
UEGCompanionSpeedEffect::UEGCompanionSpeedEffect()
{DurationPolicy=EGameplayEffectDurationType::Infinite;Modifiers.Add(Bonus(UEGAttributeSet::GetAttackSpeedAttribute()));}
