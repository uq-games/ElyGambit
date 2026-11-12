#pragma once
#include "Data/EGHeroConfig.h"
#include "EGEdenAponiaSkillDefinition.generated.h"

/** Board adaptations of Eden's musical support and Aponia's airborne judgment. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGEdenSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 virtual bool HasValidSkillRules() const override;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden",meta=(ClampMin="1")) int32 AttacksPerNote=3;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden") float HealingMultiplier=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden") float HealingRadiusCells=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden") float AdvancedSongManaPerSecond=5.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden",meta=(ClampMin="0")) float NoteMana=10.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden",meta=(ClampMin="0")) float SongManaPerSecond=3.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden",meta=(ClampMin="1")) int32 SongSeconds=6;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden") TSoftObjectPtr<UEGSourceEffectDefinition> BasicImpact;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden") TSoftObjectPtr<UEGSourceEffectDefinition> NoteEffect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Eden") TSoftObjectPtr<UEGSourceEffectDefinition> UltimateImpact;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGAponiaSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 virtual bool HasValidSkillRules() const override;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia",meta=(ClampMin="0")) float AllyAttackMana=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia|Discipline",meta=(ClampMin="1",ClampMax="6")) int32 DisciplineLimit=6;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia|Discipline",meta=(ClampMin="0",ClampMax="1")) float PenaltyDamageShare=.3f; // Legacy serialized field; no longer splits the damage budget.
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia") float PenaltyMultiplier=.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia") float AdvancedPenaltyMultiplier=.75f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia") float JudgmentSeconds=4.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia|Discipline",meta=(ClampMin="0.05")) float PenaltyInterval=.2f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia|Presentation") TSoftObjectPtr<UNiagaraSystem> DisciplineNode;

 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia",meta=(ClampMin="0.05")) float LightningInterval=.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia",meta=(ClampMin="0")) float LightningMultiplier=.6f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia") TSoftObjectPtr<UEGSourceEffectDefinition> BasicImpact;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia") TSoftObjectPtr<UEGSourceEffectDefinition> LightningEffect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Aponia") TSoftObjectPtr<UEGSourceEffectDefinition> VerdictEffect;
};
