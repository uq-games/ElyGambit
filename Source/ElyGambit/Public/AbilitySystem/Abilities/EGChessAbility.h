#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "EGChessAbility.generated.h"

/** Shared execution gate for native and Blueprint chess abilities. */
UCLASS(Abstract, Blueprintable)
class ELYGAMBIT_API UEGChessAbility : public UGameplayAbility
{
	GENERATED_BODY()

public:
	UEGChessAbility();
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
									const FGameplayTagContainer* SourceTags = nullptr,
									const FGameplayTagContainer* TargetTags = nullptr,
									FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
};
