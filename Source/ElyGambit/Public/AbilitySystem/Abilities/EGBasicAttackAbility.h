#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/EGChessAbility.h"
#include "EGBasicAttackAbility.generated.h"

/** Minimal GAS hit. TODO: animation notify, line of sight and hero-specific effects. */
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGBasicAttackAbility : public UEGChessAbility
{
	GENERATED_BODY()

public:
	UEGBasicAttackAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
									const FGameplayTagContainer* SourceTags = nullptr,
									const FGameplayTagContainer* TargetTags = nullptr,
									FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;
};
