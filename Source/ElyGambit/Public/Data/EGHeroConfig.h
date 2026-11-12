#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/EGChessTypes.h"
#include "Data/EGHeroConfigTypes.h"
#include "Data/EGChessFXCue.h"
#include "EGHeroConfig.generated.h"
class UGameplayAbility; class UEGHeroSkillBehavior; class USkeletalMesh; class UAnimInstance; class UAnimSequence; class UAnimMontage; class UMaterialInterface; class UStaticMesh; class USoundBase; class UTexture2D;
class UEGSourceEffectDefinition;
class UEGHeroDefinition;
class UEGSourcePreviewDefinition;
class USoundAttenuation;
class USoundConcurrency;

/** Authoritative stats configuration shared by hero definitions. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHeroStatsDefinition : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") EEGGrowthType GrowthType= EEGGrowthType::Frontline;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") FEGStatBlock BaseStats;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") FEGStatBlock GrowthPerLevel= ZeroGrowth();
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") float AttackRange= 150.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") float MoveSpeed= 350.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") bool bBoardMovement= false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") float AwakenedUltimateBonus=1.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") int32 SameNameLevelGain=5;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") int32 AwakeningCardCount=3;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") int32 SkillTierStep=5;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") float ManaOnHit= 10.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Stats") float ManaOnDamage= 5.f;
 static FEGStatBlock ZeroGrowth(){ FEGStatBlock S;S.MaxHealth=S.MaxMana=S.AttackPower=S.AttackSpeed=S.Defense=0;return S;}
};

/** A death pose and its cosmetic parts must describe the same source motion. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGDeathPresentationVariant
{
 GENERATED_BODY()
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> Animation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> Assembly;
};

/** A result idle pose and its matching private cosmetic parts. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGSettlementIdleVariant
{
 GENERATED_BODY()
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> Animation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> Assembly;
};

/** Authoritative presentation configuration shared by hero definitions. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHeroPresentationDefinition : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 /** Absolute cosmetic offset from the authored transform; never moves the capsule. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Hover",meta=(ClampMin="0")) float HoverHeight=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Hover",meta=(ClampMin="0")) float HoverAmplitude=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Hover",meta=(ClampMin="0.01")) float HoverFrequency=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UTexture2D> FormArtwork;
 /** Runtime source parts are evaluated by real AnimGraphs on the displayed body. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Animation") bool bUseAnimationBlueprintVisuals=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<USoundBase> AttackSound;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<USoundBase> SkillSound;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio") bool bUseEventAudio=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio") TArray<FEGCombatAudioEvent> AudioEvents;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio",meta=(ClampMin="0")) float CombatAudioVolume=.35f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio") TSoftObjectPtr<USoundAttenuation> AudioAttenuation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio") TSoftObjectPtr<USoundConcurrency> AudioConcurrency;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") FName ProjectileSocket=TEXT("Bip001-L-Hand");
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<FEGWeaponBinding> Weapons;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<FName> HiddenPresentationBones;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UStaticMesh> ProjectileMesh;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<USkeletalMesh> Mesh;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftClassPtr<UAnimInstance> AnimationClass;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> IdleAnimation;
 /** Optional one-shot for settlement; an absent or incompatible clip retains the existing fallback. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Settlement") TSoftObjectPtr<UAnimSequence> SettlementIdleAnimation;
 /** Optional synchronized parts for the settlement clip. Used only when the displayed rig can be borrowed safely. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Settlement") TSoftObjectPtr<UEGSourcePreviewDefinition> SettlementIdleAssembly;
 /** Optional form-specific idle pairs. Human index 0 is bow, index 1 Origin. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Settlement") TArray<FEGSettlementIdleVariant> SettlementIdleVariants;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> WalkAnimation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> RunAnimation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") float RunAnimationMinSpeed=250.f;
 /** World cm/s covered by one second of the unmodified, scaled gait. Zero keeps legacy tuning. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation",meta=(ClampMin="0")) float WalkReferenceSpeed=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation",meta=(ClampMin="0")) float RunReferenceSpeed=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> AttackAnimation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> DeathAnimation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> SkillAnimation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimSequence> HitAnimation;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimMontage> AttackMontage;
 /** Empty preserves the existing single-attack binding. Damage rules remain in the skill behavior. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<FEGAttackPresentation> AttackSequence;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimMontage> SkillMontage;
 /** Cosmetic assemblies indexed like AttackSequence; sampled from the active montage clock. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> AttackAssemblies;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> SkillAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<FEGAttackPresentation> UltimateSequence;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> UltimateAssemblies;
 /** Fixed, seated assemblies; entry zero is also used by preparation pieces. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> IdleAssemblies;
 /** Optional preparation assembly; empty preserves the combat-ready idle above. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> PreparationIdleAssembly;
 /** Optional one-shot played only when a preparation piece is newly deployed. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> PreparationEntranceAssembly;
 /** Optional voice played once with the preparation entrance. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Audio") TSoftObjectPtr<USoundBase> PreparationEntranceVoice;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> WalkAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> RunAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> DeathAssembly;
 /** Optional paired variants. Empty or incomplete entries use the legacy pair. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TArray<FEGDeathPresentationVariant> DeathVariants;
 /** Optional native renderer-off endpoint; negative preserves the ordinary death pose. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation",meta=(ClampMin="-1")) float DeathVisualHideDelay=-1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> HitAssembly;
 /** Cosmetic device at the cast's locked target point, driven by the same montage clock. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> SkillTargetAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation",meta=(ClampMin="0.1",ClampMax="4")) float UltimatePlaybackRate=1.f;
 /** Cosmetic triggered by the actual summon landing, at the projectile's fixed destination. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourceEffectDefinition> SummonImpactEffect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimMontage> HitMontage;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UAnimMontage> DeathMontage;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UMaterialInterface> AttackMaterial;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UMaterialInterface> SkillMaterial;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") FVector MeshScale=FVector(1.f);
 /** Additional board-only size adjustment. Source effects, commanders and settlement retain MeshScale. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Board",meta=(ClampMin="0.01",ClampMax="1.0")) float BoardModelScale=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") float PresentationGroundOffset=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") float PresentationHeight=0.f;
 /** Cosmetic head anchor; zero retains the legacy height used by projectile rules. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation",meta=(ClampMin="0")) float VitalsAnchorHeight=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") float PresentationPickRadius=48.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") FRotator PresentationRotation=FRotator(0,-90,0);
};

/** Authoritative skill configuration shared by hero definitions. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHeroSkillDefinition : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 /** Validate this skill asset without loading cosmetic resources. */
 virtual bool HasValidSkillRules() const { return true; }
 /** Hard references keep the selected board effects resident with the skill asset. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|ChessFX") TMap<FName,FEGChessFXCue> ChessFX;
 /** Exact character signet, cosmetic only. Hard reference is cooked with this skill. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Signet") TObjectPtr<UTexture2D> UltimateSignet;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Signet") FLinearColor UltimateSignetColor=FLinearColor::White;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Signet",meta=(ClampMin="40",ClampMax="500")) float UltimateSignetSize=230.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|ChessFX") TArray<FEGChessFXCue> BasicSwingFX;
 /** Resident counterparts of legacy soft Niagara fields; populated by the FX authoring pass. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|ChessFX") TArray<TObjectPtr<UNiagaraSystem>> ResidentChessSystems;
 /** Optional fixed-world area presentation; cosmetic only and never controls damage. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Area") TSoftObjectPtr<UEGSourceEffectDefinition> AreaPresentation;
 /** World-space radius in cm represented by the authored area's default size. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Area",meta=(ClampMin="0.01")) float AreaPresentationReferenceRadius=100.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") TSubclassOf<UEGHeroSkillBehavior> BehaviorClass;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FVector SkillMultipliers=FVector(1.6,2.3,3);
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FVector SkillSecondary=FVector(.15,.20,.25);
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FEGHeroSkillRules SkillRules;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FText CardSkillDescription;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FText PassiveDescription;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FText BasicDescription;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") FText UltimateDescription;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") TSubclassOf<UGameplayAbility> BasicAttackAbility;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") TSubclassOf<UGameplayAbility> UltimateAbility;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") float UltimateDamageMultiplier= 0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Skill") float UltimateRadius= 0.f;
};
