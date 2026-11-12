#include "AbilitySystem/Abilities/EGBasicAttackAbility.h"
#include "Components/EGHeroSkillComponent.h"

#include "EGGameplayTags.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/Effects/EGDamageEffect.h"
#include "AbilitySystemComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "AbilitySystem/EGCombatExecution.h"

UEGBasicAttackAbility::UEGBasicAttackAbility()
{
	SetAssetTags(FGameplayTagContainer(EGTags::BasicAttack));
}

bool UEGBasicAttackAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
											   const FGameplayAbilityActorInfo* Info,
											   const FGameplayTagContainer* Source, const FGameplayTagContainer* Target,
											   FGameplayTagContainer* Relevant) const
{
	if (!Super::CanActivateAbility(Handle, Info, Source, Target, Relevant))
	{
		return false;
	}
	const AEGChessUnit* Unit = Cast<AEGChessUnit>(Info->AvatarActor.Get());
	if(!Unit)return false;
 const bool Healer=Unit->GetDefinition()->MonsterAttack&&Unit->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Heal;
 return Healer?Unit->GetCombat()->FindHealingTarget()!=nullptr:Unit->GetCombat()->IsInAttackRange(Unit->GetCombat()->GetTarget());
}

void UEGBasicAttackAbility::ActivateAbility(const FGameplayAbilitySpecHandle Handle,
											const FGameplayAbilityActorInfo* Info,
											const FGameplayAbilityActivationInfo ActivationInfo,
											const FGameplayEventData* Event)
{
	AEGChessUnit* Unit = Cast<AEGChessUnit>(Info->AvatarActor.Get());
	AEGChessUnit* Target = Unit ? Unit->GetCombat()->GetTarget() : nullptr;
 const bool Healer=Unit&&Unit->GetDefinition()->MonsterAttack&&Unit->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Heal;
	if (!Unit || !Unit->GetCombat()->CanAct() || !(Healer?Unit->GetCombat()->FindHealingTarget()!=nullptr:Unit->GetCombat()->IsInAttackRange(Target)) ||
		!CommitAbility(Handle, Info, ActivationInfo))
	{
		EndAbility(Handle, Info, ActivationInfo, true, true);
		return;
	}
	if(Unit->GetDefinition()->HeroKind!=EEGHeroKind::Generic||Unit->GetDefinition()->MonsterAttack)
	{bool Started=Unit->GetHeroSkill()->BeginAction(false);EndAbility(Handle,Info,ActivationInfo,true,!Started);return;}
	if (FEGCombatExecution::Damage(Unit,Target,Unit->GetAttributes()->GetAttackPower(),0,GetAbilityLevel(),MakeEffectContext(Handle,Info),FGuid(),false,1.f,true))
	{
		if(IsValid(Unit)){Unit->RestoreMana(Unit->GetAttributes()->GetManaOnHit());if(IsValid(Target))Unit->PresentAttack(Target, false);}
	}
	EndAbility(Handle, Info, ActivationInfo, true, false);
}
