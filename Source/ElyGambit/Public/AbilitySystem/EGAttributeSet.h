#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "EGAttributeSet.generated.h"

#define EG_ATTRIBUTE(Name)                                                                                             \
	GAMEPLAYATTRIBUTE_PROPERTY_GETTER(UEGAttributeSet, Name)                                                           \
	GAMEPLAYATTRIBUTE_VALUE_GETTER(Name)                                                                               \
	GAMEPLAYATTRIBUTE_VALUE_SETTER(Name)                                                                               \
	GAMEPLAYATTRIBUTE_VALUE_INITTER(Name)

UCLASS()
class ELYGAMBIT_API UEGAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData Health;
	EG_ATTRIBUTE(Health)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData MaxHealth;
	EG_ATTRIBUTE(MaxHealth)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData Mana;
	EG_ATTRIBUTE(Mana)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData MaxMana;
	EG_ATTRIBUTE(MaxMana)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData AttackPower;
	EG_ATTRIBUTE(AttackPower)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData AttackSpeed;
	EG_ATTRIBUTE(AttackSpeed)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes")
	FGameplayAttributeData Defense;
	EG_ATTRIBUTE(Defense)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData AttackRange;
	EG_ATTRIBUTE(AttackRange)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData MoveSpeed;
	EG_ATTRIBUTE(MoveSpeed)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData ManaOnHit;
	EG_ATTRIBUTE(ManaOnHit)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData ManaOnDamage;
	EG_ATTRIBUTE(ManaOnDamage)
	// Shield is the source ledger's aggregate, maintained by HeroSkill. Apply
	// shields through its source API, not GE modifiers on this derived value.
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData Shield;
	EG_ATTRIBUTE(Shield)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData DamageMultiplier;
	EG_ATTRIBUTE(DamageMultiplier)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData HealingMultiplier;
	EG_ATTRIBUTE(HealingMultiplier)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData ShieldMultiplier;
	EG_ATTRIBUTE(ShieldMultiplier)
	UPROPERTY(BlueprintReadOnly, Category = "AutoChess|Attributes") FGameplayAttributeData SkillMultiplier;
	EG_ATTRIBUTE(SkillMultiplier)

	virtual void OnAttributeAggregatorCreated(const FGameplayAttribute& Attribute, FAggregator* NewAggregator) const override;
	virtual bool PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data) override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

private:
	void Clamp(const FGameplayAttribute& Attribute, float& Value) const;
};
#undef EG_ATTRIBUTE
