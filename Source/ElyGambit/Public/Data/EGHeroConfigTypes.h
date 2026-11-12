#pragma once
#include "CoreMinimal.h"
#include "EGHeroConfigTypes.generated.h"
class UStaticMesh;
class USkeletalMesh;
class UAnimInstance;
class UAnimSequence;
class UAnimMontage;
class USoundBase;
class USoundConcurrency;

/** Event names are cosmetic; they never submit gameplay hits. INDEX_NONE matches every stroke. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGCombatAudioEvent
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName Node;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bUltimate=false;
 /** Spoken character lines are permitted only during an ultimate action. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bVoice=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 AttackIndex=INDEX_NONE;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<TSoftObjectPtr<USoundBase>> Sounds;
 /** Optional per-event voice budget; empty retains the character's sound group. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<USoundConcurrency> ConcurrencyOverride;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0")) float Volume=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bAtTarget=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bStopOnCancel=true;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bAvoidImmediateRepeat=false;
};

/** Optional cosmetic attack rotation. One entry still represents one gameplay action. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGAttackPresentation
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UAnimSequence> Animation;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UAnimMontage> Montage;
};
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGWeaponBinding
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<USkeletalMesh> DeformableMesh;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName Bone;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FTransform Offset;
 /** Optional animated weapon. Empty retains legacy poseable weapon behavior. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftClassPtr<UAnimInstance> AnimationClass;
 /** Asset-authored local transform includes import scale once; no runtime bone-scale inversion. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bUseAssetBinding=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> NockedArrow;
};

/** Shared rule values. Each hero definition owns the values relevant to its kind. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGHeroSkillRules
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 CoinAttackThreshold=5;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 CoinReward=1;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float LivingCanAttackSpeed=.3f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float AreaRadius=300.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float TauntDuration=2.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float ProjectileDuration=.3f;
};

