#include "AbilitySystem/Abilities/EGUltimateAbility.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGHeroSkillBehavior.h"

#include "EGGameplayTags.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/Effects/EGManaEffect.h"
#include "AbilitySystemComponent.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/Effects/EGDamageEffect.h"
#include "EngineUtils.h"
#include "AbilitySystem/EGCombatExecution.h"

UEGUltimateAbility::UEGUltimateAbility()
{
	SetAssetTags(FGameplayTagContainer(EGTags::Ultimate));
}

bool UEGUltimateAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
											const FGameplayAbilityActorInfo* Info, const FGameplayTagContainer* Source,
											const FGameplayTagContainer* Target, FGameplayTagContainer* Relevant) const
{
	if (!Super::CanActivateAbility(Handle, Info, Source, Target, Relevant))
	{
		return false;
	}
	const auto* Unit=Cast<AEGChessUnit>(Info->AvatarActor.Get());
    if(!Unit||!Unit->GetDefinition()||!Unit->GetDefinition()->bCardSkillsEnabled)return false;
    // Reject before GAS activation so full mana cannot starve enhanced basic attacks.
    if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia||Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher||Unit->GetDefinition()->MonsterAttack)
        if(const auto* Skill=Unit->GetHeroSkill();Skill&&Skill->GetBehavior()&&!Skill->GetBehavior()->AllowsAction(true))return false;
    const UEGAttributeSet* Stats=Unit->GetAttributes();
	return Stats->GetMaxMana() > 0.f && Stats->GetMana() >= Stats->GetMaxMana();
}

void UEGUltimateAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* Info,
										 const FGameplayAbilityActivationInfo ActivationInfo,
										 const FGameplayEventData* Event)
{
	AEGChessUnit* Unit = Cast<AEGChessUnit>(Info->AvatarActor.Get());
	// Activation has already installed ability-block tags. Re-running CanActivateAbility here
	// would reject this ability against its own tags; only revalidate execution inputs.
	if (!Unit || !Unit->GetDefinition() || !Unit->GetDefinition()->bCardSkillsEnabled || !Unit->GetCombat()->CanAct() || Unit->GetAttributes()->GetMaxMana() <= 0.f ||
		Unit->GetAttributes()->GetMana() < Unit->GetAttributes()->GetMaxMana() ||
		!CommitAbility(Handle, Info, ActivationInfo))
	{
		EndAbility(Handle, Info, ActivationInfo, true, true);
		return;
	}
	if(Unit->GetDefinition()->HeroKind!=EEGHeroKind::Generic||Unit->GetDefinition()->MonsterAttack)
	{bool Started=Unit->GetHeroSkill()->BeginAction(true);EndAbility(Handle,Info,ActivationInfo,true,!Started);return;}
	FGameplayEffectSpecHandle Spec = Unit->GetAbilitySystemComponent()->MakeOutgoingSpec(
		UEGManaEffect::StaticClass(), 1.f, MakeEffectContext(Handle, Info));
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(EGTags::Mana, -Unit->GetAttributes()->GetMana());
		Unit->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToSelf(*Spec.Data);
		const auto* Definition=Unit->GetDefinition();
        if(Definition && Definition->GetUltimateDamageMultiplier()>0.f)
        {
            AEGChessUnit* Center=Unit->GetCombat()->FindNearestEnemy();
            if(Center)
            {
                const FVector Origin=Center->GetActorLocation();
                Unit->PresentAttack(Center,true);
                for(AEGChessUnit* Other:Unit->GetCombat()->FindEnemiesInRange(Origin,Definition->GetUltimateRadius()))
                {
                    if(!Unit->GetCombat()->IsValidEnemy(Other)) continue;
                    FEGCombatExecution::Damage(Unit,Other,Unit->GetAttributes()->GetAttackPower()*Definition->GetUltimateDamageMultiplier()*Unit->GetUltimateBonus(),1.f,GetAbilityLevel(),MakeEffectContext(Handle,Info));
                }
            }
        }
	}
	EndAbility(Handle, Info, ActivationInfo, true, false);
}
