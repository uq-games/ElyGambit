#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/EGChessAbility.h"
#include "EGUltimateAbility.generated.h"

/** Target-free verification ability: consumes mana and logs. TODO: actual hero effect. */
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGUltimateAbility : public UEGChessAbility
{
	GENERATED_BODY()

public:
	UEGUltimateAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
									const FGameplayTagContainer* SourceTags = nullptr,
									const FGameplayTagContainer* TargetTags = nullptr,
									FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;
};
