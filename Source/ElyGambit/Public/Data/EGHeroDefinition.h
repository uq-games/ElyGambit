#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/EGChessTypes.h"
#include "Presentation/EGChessAnimInstance.h"
#include "Misc/DataValidation.h"
#include "Data/EGHeroConfig.h"
#include "EGHeroDefinition.generated.h"

class UGameplayAbility;
class USkeletalMesh;
class UAnimInstance;
class UAnimSequence;
class UEGHeroCardInfoDefinition;
class UAnimMontage;
class UMaterialInterface;
class UStaticMesh;
class USoundBase;
class UTexture2D;
class UEGMonsterAttackDefinition;
class UEGSourceSummonDefinition;
class AEGChessUnit;

/** Presentation categories only; these do not grant abilities or define combat rules. */
UENUM(BlueprintType)
enum class EEGSkillInfoKind : uint8 { Basic, Ultimate, Passive };

/** Reuse this asset for heroes, enemy copies, mobs and bosses. Values are placeholders. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHeroDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
 UPROPERTY(Transient,BlueprintReadOnly,Category="Encounter") bool bEliteEnemy=false;
 /** Optional audio-only configuration for legacy summons; does not change stats or animation bindings. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio") TObjectPtr<UEGHeroPresentationDefinition> CombatAudioOverride;
 const UEGHeroPresentationDefinition* GetCombatAudioPresentation() const {return CombatAudioOverride?CombatAudioOverride.Get():bUseSplitConfiguration?PresentationDefinition.Get():nullptr;}
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Card Information") TObjectPtr<UEGHeroCardInfoDefinition> CardInformation;
 /** Optional presentation Blueprint for a combat incarnation; unset preserves the mode's native class. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Presentation") TSoftClassPtr<AEGChessUnit> CombatActorClass;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Presentation") TSoftObjectPtr<UEGSourceSummonDefinition> SummonPresentation;
 /** Basic-monster policy; absent on heroes and historical fixtures. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster") TObjectPtr<UEGMonsterAttackDefinition> MonsterAttack;
 /** Explicit opt-in for the four confirmed base-form skills. Legacy/test definitions retain their behavior. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill") bool bConfirmedSkills=false;

 /** Allows a roster to be reset for skill redesign without changing basic attacks or other rosters. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Skills") bool bCardSkillsEnabled=true;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Configuration") bool bUseSplitConfiguration=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Configuration") TObjectPtr<UEGHeroStatsDefinition> StatsDefinition;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Configuration") TObjectPtr<UEGHeroPresentationDefinition> PresentationDefinition;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hero|Configuration") TObjectPtr<UEGHeroSkillDefinition> SkillDefinition;
 TSubclassOf<UEGHeroSkillBehavior> GetBehaviorClass() const;
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") EEGGrowthType GetGrowthType() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->GrowthType;} return GrowthType; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") const FEGStatBlock& GetBaseStats() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->BaseStats;} return BaseStats; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") const FEGStatBlock& GetGrowthPerLevel() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->GrowthPerLevel;} return GrowthPerLevel; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") float GetAttackRange() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->AttackRange;} return AttackRange; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") float GetMoveSpeed() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->MoveSpeed;} return MoveSpeed; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") bool GetBoardMovement() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->bBoardMovement;} return bBoardMovement; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") float GetAwakenedUltimateBonus() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->AwakenedUltimateBonus;} return AwakenedUltimateBonus; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") int32 GetSameNameLevelGain() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->SameNameLevelGain;} return SameNameLevelGain; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") int32 GetAwakeningCardCount() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->AwakeningCardCount;} return AwakeningCardCount; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") int32 GetSkillTierStep() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->SkillTierStep;} return SkillTierStep; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") float GetManaOnHit() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->ManaOnHit;} return ManaOnHit; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Stats") float GetManaOnDamage() const { if(bUseSplitConfiguration){check(StatsDefinition);return StatsDefinition->ManaOnDamage;} return ManaOnDamage; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UTexture2D>& GetFormArtwork() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->FormArtwork;} return FormArtwork; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<USoundBase>& GetAttackSound() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->AttackSound;} return AttackSound; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<USoundBase>& GetSkillSound() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->SkillSound;} return SkillSound; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const FName& GetProjectileSocket() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->ProjectileSocket;} return ProjectileSocket; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TArray<FEGWeaponBinding>& GetWeapons() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->Weapons;} return Weapons; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TArray<FName>& GetHiddenPresentationBones() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->HiddenPresentationBones;} return HiddenPresentationBones; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UStaticMesh>& GetProjectileMesh() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->ProjectileMesh;} return ProjectileMesh; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<USkeletalMesh>& GetMesh() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->Mesh;} return Mesh; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftClassPtr<UAnimInstance>& GetAnimationClass() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->AnimationClass;} return AnimationClass; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetIdleAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->IdleAnimation;} return IdleAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetWalkAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->WalkAnimation;} return WalkAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetRunAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->RunAnimation;} return RunAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") float GetRunAnimationMinSpeed() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->RunAnimationMinSpeed;} return RunAnimationMinSpeed; }
 float GetWalkReferenceSpeed() const {return bUseSplitConfiguration&&PresentationDefinition?PresentationDefinition->WalkReferenceSpeed:WalkReferenceSpeed;}
 float GetRunReferenceSpeed() const {return bUseSplitConfiguration&&PresentationDefinition?PresentationDefinition->RunReferenceSpeed:RunReferenceSpeed;}
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetAttackAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->AttackAnimation;} return AttackAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetDeathAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->DeathAnimation;} return DeathAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetSkillAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->SkillAnimation;} return SkillAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimSequence>& GetHitAnimation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->HitAnimation;} return HitAnimation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimMontage>& GetAttackMontage() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->AttackMontage;} return AttackMontage; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimMontage>& GetSkillMontage() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->SkillMontage;} return SkillMontage; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimMontage>& GetHitMontage() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->HitMontage;} return HitMontage; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UAnimMontage>& GetDeathMontage() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->DeathMontage;} return DeathMontage; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UMaterialInterface>& GetAttackMaterial() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->AttackMaterial;} return AttackMaterial; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const TSoftObjectPtr<UMaterialInterface>& GetSkillMaterial() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->SkillMaterial;} return SkillMaterial; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const FVector& GetMeshScale() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->MeshScale;} return MeshScale; }
 float GetBoardModelScale() const {return bUseSplitConfiguration&&PresentationDefinition?PresentationDefinition->BoardModelScale:1.f;}
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") float GetPresentationGroundOffset() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->PresentationGroundOffset;} return PresentationGroundOffset; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") float GetPresentationHeight() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->PresentationHeight;} return PresentationHeight; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") float GetVitalsAnchorHeight() const
 {const float Height=bUseSplitConfiguration&&PresentationDefinition?PresentationDefinition->VitalsAnchorHeight:VitalsAnchorHeight;return Height>0?Height:GetPresentationHeight();}
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") float GetPresentationPickRadius() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->PresentationPickRadius;} return PresentationPickRadius; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Presentation") const FRotator& GetPresentationRotation() const { if(bUseSplitConfiguration){check(PresentationDefinition);return PresentationDefinition->PresentationRotation;} return PresentationRotation; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FVector& GetSkillMultipliers() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->SkillMultipliers;} return SkillMultipliers; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FVector& GetSkillSecondary() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->SkillSecondary;} return SkillSecondary; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FEGHeroSkillRules& GetSkillRules() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->SkillRules;} return SkillRules; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FText& GetCardSkillDescription() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->CardSkillDescription;} return CardSkillDescription; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FText& GetPassiveDescription() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->PassiveDescription;} return PassiveDescription; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FText& GetBasicDescription() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->BasicDescription;} return BasicDescription; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") const FText& GetUltimateDescription() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->UltimateDescription;} return UltimateDescription; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") TSubclassOf<UGameplayAbility> GetBasicAttackAbility() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->BasicAttackAbility;} return BasicAttackAbility; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") TSubclassOf<UGameplayAbility> GetUltimateAbility() const { if(!bCardSkillsEnabled)return nullptr; if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->UltimateAbility;} return UltimateAbility; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") float GetUltimateDamageMultiplier() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->UltimateDamageMultiplier;} return UltimateDamageMultiplier; }
 UFUNCTION(BlueprintPure,Category="Hero|Resolved Skill") float GetUltimateRadius() const { if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->UltimateRadius;} return UltimateRadius; }
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero")
	FName HeroId;
	/** Board cells centered on the deployment anchor; independent of population cost. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Placement") FIntPoint PlacementFootprint=FIntPoint(1,1);
	// Human Herrscher now occupies one cell; retain the reflected field for older saved definitions.
	UFUNCTION(BlueprintPure,Category="AutoChess|Placement") FIntPoint GetPlacementFootprint() const {return HeroKind==EEGHeroKind::ElysiaHerrscher?FIntPoint(1,1):PlacementFootprint;}
	/** Optional UI frame; absence keeps the existing card style. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|UI") TSoftObjectPtr<UTexture2D> CardFrameTexture;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UTexture2D> FormArtwork;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<USoundBase> AttackSound;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<USoundBase> SkillSound;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FName ProjectileSocket=TEXT("Bip001-L-Hand");
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TArray<FEGWeaponBinding> Weapons;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TArray<FName> HiddenPresentationBones;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Hero") EEGHeroKind HeroKind=EEGHeroKind::Generic;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Hero") bool bEnhancedForm=false;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Hero") TObjectPtr<UEGHeroDefinition> EnhancedDefinition;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Hero") TObjectPtr<UEGHeroDefinition> SummonDefinition;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FVector SkillMultipliers=FVector(1.6,2.3,3);
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FVector SkillSecondary=FVector(.15,.20,.25);
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FEGHeroSkillRules SkillRules;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UStaticMesh> ProjectileMesh;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FText CardSkillDescription;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FText PassiveDescription;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FText BasicDescription;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Skill", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FText UltimateDescription;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	TSoftClassPtr<UAnimInstance> AnimationClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	EEGGrowthType GrowthType = EEGGrowthType::Frontline;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	FEGStatBlock BaseStats;
	// Individual asset overrides supply the three growth archetypes without fixed balance values.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	FEGStatBlock GrowthPerLevel = MakeZeroGrowth();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin = "1"), Category = "AutoChess|Hero")
	float AttackRange = 150.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin = "0"), Category = "AutoChess|Hero")
	float MoveSpeed = 350.f;
	/** Board units travel directly on the clear board plane; other units retain NavMesh movement. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) bool bBoardMovement = false;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) float UltimateDamageMultiplier = 0.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) float UltimateRadius = 0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> IdleAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> WalkAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> RunAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="1")) float RunAnimationMinSpeed=250.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="0")) float WalkReferenceSpeed=0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="0")) float RunReferenceSpeed=0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> AttackAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> DeathAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> SkillAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimSequence> HitAnimation;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimMontage> AttackMontage;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimMontage> SkillMontage;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimMontage> HitMontage;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UAnimMontage> DeathMontage;
    UAnimMontage* GetPresentationMontage(EGPresentationAction Action) const;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UMaterialInterface> AttackMaterial;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) TSoftObjectPtr<UMaterialInterface> SkillMaterial;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FVector MeshScale=FVector(1.f);
    /** Vertical correction so the lowest foot bone of the rest pose sits on the actor origin. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) float PresentationGroundOffset=0.f;
    /** Measured presentation height in board centimetres; zero retains legacy widget placement. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) float PresentationHeight=0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="0")) float VitalsAnchorHeight=0.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) float PresentationPickRadius=48.f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Presentation", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration.")) FRotator PresentationRotation=FRotator(0,-90,0);
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Hero",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="1")) float AwakenedUltimateBonus=1.25f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Growth",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="1")) int32 SameNameLevelGain=5;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Growth",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="1")) int32 AwakeningCardCount=3;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="AutoChess|Growth",meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin="1")) int32 SkillTierStep=5;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	TSubclassOf<UGameplayAbility> BasicAttackAbility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Hero", meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ToolTip="Compatibility field. Split configuration is authoritative after migration."))
	TSubclassOf<UGameplayAbility> UltimateAbility;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin = "0"), Category = "AutoChess|Hero")
	float ManaOnHit = 10.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, meta=(EditCondition="!bUseSplitConfiguration",EditConditionHides,ClampMin = "0"), Category = "AutoChess|Hero")
	float ManaOnDamage = 5.f;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Hero")
	FEGStatBlock EvaluateStats(int32 Level) const;
	/** Clip set handed to the crossfading presentation anim instance. */
	UFUNCTION(BlueprintPure,Category="AutoChess|Presentation") FEGPresentationClips BuildPresentationClips() const;
	UFUNCTION(BlueprintPure,Category="AutoChess|Skill") FText GetSkillDetails(int32 Level,bool Awakened) const;
	UFUNCTION(BlueprintPure,Category="AutoChess|Skill") FText GetPlayerFacingSkillName() const;
 FText GetPlayerFacingSkillSectionName(EEGSkillInfoKind Kind) const;
	UFUNCTION(BlueprintPure,Category="AutoChess|Skill") FText GetPlayerFacingSkillEffect() const;
	UFUNCTION(BlueprintPure,Category="AutoChess|Skill") FText GetPlayerFacingSkillSection(EEGSkillInfoKind Kind) const;
	static FEGStatBlock MakeZeroGrowth();
	bool ValidateDefinition(FString& Error,bool bCheckAnimation=false) const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
