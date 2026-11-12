#include "AbilitySystem/EGAttributeSet.h"

#include "GameplayEffectExtension.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "GameplayEffectAggregator.h"
#include "EGGameplayTags.h"

void UEGAttributeSet::OnAttributeAggregatorCreated(const FGameplayAttribute& Attribute,FAggregator* NewAggregator) const
{
 Super::OnAttributeAggregatorCreated(Attribute,NewAggregator);
 if(Attribute!=GetManaAttribute()||!NewAggregator)return;
 // Metadata has static lifetime: the aggregator retains a raw pointer. Each mod
 // resolves its own live owner; no actor or UObject is captured in the delegate.
 static FAggregatorEvaluateMetaData ManaQualification([](const FAggregatorEvaluateParameters&,const FAggregator* Aggregator)
 {
  Aggregator->ForEachMod([](const FAggregatorModInfo& Info)
  {
   if(!Info.Mod||!Info.Mod->ActiveHandle.IsValid())return;
   const auto Op=Info.Op;const float Magnitude=Info.Mod->EvaluatedMagnitude;
   const bool Positive=((Op==EGameplayModOp::AddBase||Op==EGameplayModOp::AddFinal||Op==EGameplayModOp::Override)&&Magnitude>0.f)||
    ((Op==EGameplayModOp::MultiplyAdditive||Op==EGameplayModOp::MultiplyCompound)&&Magnitude>1.f)||
    (Op==EGameplayModOp::DivideAdditive&&Magnitude>0.f&&Magnitude<1.f);
   if(!Positive)return;
   auto* ASC=Info.Mod->ActiveHandle.GetOwningAbilitySystemComponent();
   auto* Unit=ASC?Cast<AEGChessUnit>(ASC->GetAvatarActor()):nullptr;
   auto* Behavior=Unit&&Unit->GetHeroSkill()?Cast<UEGElysiaHerrscherSkillBehavior>(Unit->GetHeroSkill()->GetBehavior()):nullptr;
   if(Behavior&&Behavior->SuppressManaModifier(Info.Mod->ActiveHandle))Info.Mod->SetExplicitQualifies(false);
  });
 });
 NewAggregator->EvaluationMetaData=&ManaQualification;
}

void UEGAttributeSet::Clamp(const FGameplayAttribute& Attribute, float& Value) const
{
	if (!FMath::IsFinite(Value))
	{
		Value = 0.f;
	}
	if (Attribute == GetHealthAttribute())
	{
		Value = FMath::Clamp(Value, 0.f, GetMaxHealth());
	}
	else if (Attribute == GetManaAttribute())
	{
		Value = FMath::Clamp(Value, 0.f, GetMaxMana());
		if(const auto* Unit=Cast<AEGChessUnit>(GetOwningActor());Unit&&Unit->GetHeroSkill()&&Unit->GetHeroSkill()->BlocksManaGain()&&Value>GetMana())Value=GetMana();
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		Value = FMath::Max(1.f, Value);
	}
	else
	{
		Value = FMath::Max(0.f, Value);
	}
}

void UEGAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	Clamp(Attribute, NewValue);
}

void UEGAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	Clamp(Attribute, NewValue);
}

void UEGAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (Attribute == GetMaxHealthAttribute() && GetHealth() > NewValue)
	{
		SetHealth(NewValue);
	}
	if (Attribute == GetMaxManaAttribute() && GetMana() > NewValue)
	{
		SetMana(NewValue);
	}
}

void UEGAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	if (AEGChessUnit* Unit = Cast<AEGChessUnit>(GetOwningActor()))
	{
		if (Data.EvaluatedData.Attribute == GetHealthAttribute() && Data.EvaluatedData.Magnitude < 0.f)
		{
			Unit->NotifyDamageReceived(-Data.EvaluatedData.Magnitude);
		}
	}
}

bool UEGAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
 if(!Super::PreGameplayEffectExecute(Data))return false;
 // Execution calculations can emit a modifier even when the effect definition
 // has no static modifiers; shield totals belong exclusively to the ledger.
 if(Data.EvaluatedData.Attribute==GetShieldAttribute())return false;
 if(Data.EvaluatedData.Attribute==GetManaAttribute()&&Data.EvaluatedData.Magnitude>0.f)
  if(const auto* U=Cast<AEGChessUnit>(GetOwningActor());U&&U->GetHeroSkill()->BlocksManaGain())return false;
 if(Data.EvaluatedData.Attribute==GetHealthAttribute()&&Data.EvaluatedData.Magnitude<0)
  if(auto* U=Cast<AEGChessUnit>(GetOwningActor()))
  {
   auto* S=U->GetHeroSkill();if(!S->IsTargetable())return false;
   FGameplayTagContainer AssetTags;
   Data.EffectSpec.GetAllAssetTags(AssetTags);
   if(!AssetTags.HasTagExact(EGTags::DamageExecution))
    Data.EvaluatedData.Magnitude+=S->AbsorbShield(-Data.EvaluatedData.Magnitude);
  }
 return true;
}
