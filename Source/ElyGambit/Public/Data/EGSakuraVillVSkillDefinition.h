#pragma once
#include "Data/EGHeroConfig.h"
#include "EGSakuraVillVSkillDefinition.generated.h"
class UEGHeroDefinition;
class UEGSourcePreviewDefinition;
class UCurveFloat;
class UEGSourceEffectDefinition;
class UStaticMesh;
class UMaterialInterface;
class UEGVillVCombatFXDefinition;
class UNiagaraSystem;
class UEGSakuraReworkPresentationConfig;
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGSakuraSkillDefinition : public UEGHeroSkillDefinition
{
	GENERATED_BODY()
public:
	virtual bool HasValidSkillRules() const override;
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UEGSakuraReworkPresentationConfig> ReworkPresentation;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UNiagaraSystem> ChessStrikeFX;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UNiagaraSystem> ChessReleaseFX;
	/** Optional short, world-space accents; these never determine combat outcomes. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UEGSourceEffectDefinition> BasicHitPresentation;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UEGSourceEffectDefinition> SlashReleasePresentation;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UEGSourceEffectDefinition> NormalImpactPresentation;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura|Presentation") TSoftObjectPtr<UEGSourceEffectDefinition> ExecutionImpactPresentation;
	/** Current target health at or below this fraction is executed before the ordinary slash. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura",meta=(ClampMin="0.01",ClampMax="1.0")) float ExecuteHealthFraction=.2f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura") float CounterMultiplier=1.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura") float KillMana=30.f;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura",meta=(ClampMin="0.01")) float EvadeCooldownSeconds=8.f;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura",meta=(ClampMin="0.01")) float DelayedSlashSeconds=.35f;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Sakura",meta=(ClampMin="-1.0",ClampMax="1.0")) float FrontalEvadeDot=.5f;
};
/** Cosmetic bullet/shell flight for Vill-V's two weapons. Never part of damage or hit rules. */
USTRUCT(BlueprintType)
struct FEGVillVShotPresentation
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UMaterialInterface> Material;
	UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector MeshScale=FVector(.3f);
	UPROPERTY(EditAnywhere,BlueprintReadWrite) FRotator MeshRotation=FRotator::ZeroRotator;
	UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UStaticMesh> TrailMesh;
	UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UMaterialInterface> TrailMaterial;
	/** Trail quad length/width/height in world centimetres. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite) FVector TrailScale=FVector(120.f,8.f,1.f);
	UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01")) float Duration=.08f;
	UPROPERTY(EditAnywhere,BlueprintReadWrite) float ArcHeight=0.f;
};
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGVillVSkillDefinition : public UEGHeroSkillDefinition
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") float CannonRadius=500.f;
	/** Fresh source Niagara playback; does not use the retired projectile switch. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV|Presentation") TObjectPtr<UEGVillVCombatFXDefinition> SourceCombatFX;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") float CannonMultiplier=1.6f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") float AutoFireSeconds=6.f;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TSoftObjectPtr<UEGHeroDefinition> GatlingDefinition;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TSoftObjectPtr<UEGHeroDefinition> CannonDefinition;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<FEGAttackPresentation> EntryActions;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> EntryBodies;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> SeatedBodies;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> DeviceSpawns;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> DeviceAttacks;
	/** Original EX weapon timelines. Optional during migration; order is gatling/cannon. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV|Presentation") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> EnhancedDeviceAttacks;
	/** Original source impacts, indexed by gatling/cannon. No procedural fallback. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV|Presentation") TArray<TSoftObjectPtr<UEGSourceEffectDefinition>> ShotImpacts;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV|Presentation") TArray<TSoftObjectPtr<UEGSourceEffectDefinition>> EnhancedShotImpacts;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> DeviceIdles;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<FVector> EntryApproachOffsets;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<FVector> ExitGroundOffsets;
	/** Original departure root displacement. Private leave clips have matching XY removed. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV|Presentation") TArray<TObjectPtr<UCurveFloat>> ExitProgressCurves;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") float TravelSpeed=750.f;
	/** Original Teleport entry clips; index is destination weapon. Separate from first deployment entry. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<FEGAttackPresentation> JumpActions;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> JumpBodies;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<float> JumpWarpStarts;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<float> JumpWarpEnds;
	/** Native root timing normalized to board distance; no extra ground locomotion. */
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV") TArray<TObjectPtr<UCurveFloat>> JumpProgressCurves;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV",meta=(ClampMin="1")) float AimDegreesPerSecond=240.f;
	UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="VillV",meta=(ClampMin="0.1")) float AimTolerance=8.f;
	/** Cosmetic-only original bullet/shell flight and muzzle launch layers. */
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") bool bPresentShots=true;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") FEGVillVShotPresentation GatlingShot;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") FEGVillVShotPresentation CannonShot;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") TSoftObjectPtr<UEGSourceEffectDefinition> GatlingMuzzleEffect;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") TSoftObjectPtr<UEGSourceEffectDefinition> CannonMuzzleEffect;
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") FName GatlingMuzzleBone=TEXT("FirePoint1");
	UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV|Shots") FName CannonMuzzleBone=TEXT("CannonFire");
};
