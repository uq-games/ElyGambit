#pragma once
#include "Data/EGHeroConfig.h"
#include "EGGriseoAdultSkillDefinition.generated.h"
class UEGSourceEffectDefinition;

/** Adult Griseo: a single independent Knight and a meteor with a team speed buff. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGGriseoAdultSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 UEGGriseoAdultSkillDefinition() {UltimateRadius=650.f;SkillMultipliers=FVector(2.5,3.5,4.5);}
 virtual bool HasValidSkillRules() const override;
 /** Attack speed bonus for allied heroes while this battle's Knight is alive. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult",meta=(ClampMin="0")) float PassiveSpeedBonus=.10f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult",meta=(ClampMin="0.01")) float KnightHealthRatio=1.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult",meta=(ClampMin="0")) float KnightAttackRatio=.35f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult",meta=(ClampMin="0")) float TeamSpeedBonus=.2f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult") float AdvancedTeamSpeedBonus=.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult") float MeteorStunSeconds=1.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult",meta=(ClampMin="0.01")) float TeamSpeedSeconds=6.f;
 /** Source meteor impact; instantiated by Release at the action's locked ground point. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="GriseoAdult|Presentation") TSoftObjectPtr<UEGSourceEffectDefinition> MeteorImpact;
};
