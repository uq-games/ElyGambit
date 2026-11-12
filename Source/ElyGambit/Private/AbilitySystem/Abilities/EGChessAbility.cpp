#include "AbilitySystem/Abilities/EGChessAbility.h"

#include "EGGameplayTags.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"

UEGChessAbility::UEGChessAbility()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerOnly;
	ActivationBlockedTags.AddTag(EGTags::Dead);
	BlockAbilitiesWithTag.AddTag(EGTags::BasicAttack);
	BlockAbilitiesWithTag.AddTag(EGTags::Ultimate);
}

bool UEGChessAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
										 const FGameplayTagContainer* Source, const FGameplayTagContainer* Target,
										 FGameplayTagContainer* Relevant) const
{
	const AEGChessUnit* Unit = Info ? Cast<AEGChessUnit>(Info->AvatarActor.Get()) : nullptr;
	return Unit && Unit->GetCombat()->CanAct() && Super::CanActivateAbility(Handle, Info, Source, Target, Relevant);
}
