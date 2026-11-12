#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EGMonsterAttackDefinition.generated.h"

class UMaterialInterface;
UENUM(BlueprintType)
enum class EEGMonsterAttackType : uint8 { Melee, Projectile, Heal };

UENUM(BlueprintType)
enum class EEGMonsterUltimate : uint8 { None, Shield, EnhancedShot, TeamSpeed, CrystalSeed };

/** Explicit opt-in for basic monsters. All distances are measured in board centimetres. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGMonsterAttackDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill") EEGMonsterUltimate Ultimate=EEGMonsterUltimate::None;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0")) float UltimateMultiplier=1.8f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0",ClampMax="1")) float ShieldHealthRatio=.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0.01")) float ShieldSeconds=5.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0",ClampMax="1")) float HealHealthRatio=.08f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0")) float TeamSpeedBonus=.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0.01")) float TeamSpeedSeconds=5.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0.01")) float SeedDelay=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="0.01")) float SeedRadiusCells=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Skill",meta=(ClampMin="1")) int32 MaxActiveSeeds=6;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Presentation") TSoftObjectPtr<UMaterialInterface> SkillMaterial;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Presentation",meta=(ClampMin="1")) float EnhancedProjectileScale=1.65f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster") EEGMonsterAttackType AttackType=EEGMonsterAttackType::Melee;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster",meta=(ClampMin="1")) float BodyRadius=50.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster",meta=(ClampMin="0")) float ContactTolerance=12.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster",meta=(ClampMin="1")) float ProjectileSpeed=650.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster",meta=(ClampMin="1")) float ProjectileRadius=9.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster") FName LaunchBone;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster") FVector LaunchOffset=FVector(12,0,0);
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster") TSoftObjectPtr<UMaterialInterface> HitOverlay;
 /** Projectile reach in row spacings from the active board; zero uses the existing cm rule. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster",meta=(ClampMin="0")) float AttackRangeCells=0.f;
 /** Slot-aligned death variants; never replace the shared live material asset. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Death") TArray<TSoftObjectPtr<UMaterialInterface>> DeathMaterials;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Death",meta=(ClampMin="0")) float DeathHoldSeconds=.15f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Monster|Death",meta=(ClampMin="0.01")) float DissolveSeconds=1.2f;
};
