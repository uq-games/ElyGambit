#pragma once
#include "Data/EGHeroConfig.h"
#include "EGGriseoSkillDefinition.generated.h"

/** Child Griseo: one combined source performance, prismatic protection and a fixed painted field. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGGriseoSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo|Support") float ShieldRatio=.2f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo|Support") float ShieldRatioAdvanced=.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo|Support") float SupportRadiusCells=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo|Support") float SupportSeconds=6.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo|Support") float AttackBonus=.2f;
 virtual bool HasValidSkillRules() const override;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0.01")) float KnightHealthRatio=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0")) float KnightAttackRatio=.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="1")) int32 BubbleHitCount=5;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0.01")) float BubbleSeconds=10.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0",ClampMax="1")) float BubbleBreakReduction=.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0.01")) float BubbleBreakSeconds=3.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0",ClampMax="1")) float FieldReduction=.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0.01")) float FieldSeconds=6.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo",meta=(ClampMin="0")) float FieldRadius=750.f;
 /** Short-lived Knight sections must be ready before the first third-stage attack. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo|Presentation") TArray<int32> PrewarmMaterialSlots={11,12};
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo") TSoftObjectPtr<UEGSourceEffectDefinition> BubbleEffect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo") TSoftObjectPtr<UEGSourceEffectDefinition> BubbleBurstEffect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Griseo") TSoftObjectPtr<UEGSourceEffectDefinition> UltimateField;
};
