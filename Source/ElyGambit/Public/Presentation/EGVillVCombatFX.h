#pragma once
#include "CoreMinimal.h"
#include "Presentation/EGVillVFXsPreview.h"
#include "EGVillVCombatFX.generated.h"
class UEGHeroSkillComponent;
class UEGBattleContextComponent;
class AEGChessUnit;
class AEGVillVEmplacement;
class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class USoundBase;
class USoundAttenuation;
class USoundConcurrency;

UENUM(BlueprintType)
enum class EEGVillVCombatFXStage : uint8 { Charge, Shot, Release, SecondaryImpact };

USTRUCT(BlueprintType)
struct FEGVillVCombatFXPreset
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGVillVFXsLayer> Charge;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGVillVFXsLayer> Shot;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGVillVFXsLayer> Release;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGVillVFXsLayer> SecondaryImpact;
};

/** Private V2 single-shot assets. Muzzle/release arrays follow Fire1/2/3 order. */
USTRUCT(BlueprintType)
struct FEGVillVShotV2Preset
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TArray<TObjectPtr<UEGSourceEffectDefinition>> MuzzleEffects;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TArray<TObjectPtr<UEGSourceEffectDefinition>> ReleaseEffects;
 /** Authored local up for each release. Corrected around muzzle +X at birth. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TArray<FVector> ReleaseLocalUps;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TObjectPtr<UEGSourceEffectDefinition> ProjectileEffect;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TObjectPtr<UEGSourceEffectDefinition> ImpactEffect;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TObjectPtr<UNiagaraSystem> SparkSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2",meta=(ClampMin="0",ClampMax="0.5")) float SparkLifetime=.4f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2",meta=(ClampMin="1")) float FlightSpeed=5500.f;
};

/** Source systems are shared with the independent preview; board adaptation is
    stored here. Order: normal gun, EX gun, normal cannon, EX cannon. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGVillVCombatFXDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGVillVCombatFXPreset> Presets;
 /** Normal gun, enhanced gun, normal cannon, enhanced cannon. Empty keeps old behaviour. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2") TArray<FEGVillVShotV2Preset> ShotPresetsV2;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|V2",meta=(ClampMin="1",ClampMax="24")) int32 MaxConcurrentV2Shots=24;
 /** Vill-V-only hard references keep combat audio ready with the hero definition. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Audio") TArray<TObjectPtr<USoundBase>> ResidentSounds;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Audio") TArray<TObjectPtr<USoundAttenuation>> ResidentAttenuations;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Audio") TArray<TObjectPtr<USoundConcurrency>> ResidentConcurrencies;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0",ClampMax="1")) float GatlingImpactHeightFraction=.45f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0",ClampMax="1")) float CannonImpactHeightFraction=.2f;
 /** Optional board-camera adjustment for the cannon visual impact only. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Cannon") bool bCannonImpactViewOffset=false;
 /** Optional cosmetic shell shown on the cannon projectile path. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Cannon") TObjectPtr<UStaticMesh> CannonProjectileMesh;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Cannon") TObjectPtr<UMaterialInterface> CannonProjectileMaterial;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="VillV|Cannon",meta=(ClampMin="1")) float CannonProjectileLength=80.f;
 /** Board-only presentation adjustment; never changes the hit or damage point. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0",ClampMax="1")) float GatlingImpactViewOffsetFraction=.4f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0")) float GatlingImpactViewOffsetMax=150.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FString SourceAndLimitations;
};

/** Round-owned cosmetic playback. Has no damage, targeting, ability or RPC path. */
UCLASS()
class ELYGAMBIT_API AEGVillVCombatFX : public AActor
{
 GENERATED_BODY()
public:
 AEGVillVCombatFX();
 static AEGVillVCombatFX* Spawn(UEGHeroSkillComponent* Skill,AEGVillVEmplacement* Device,UEGVillVCombatFXDefinition* Definition,int32 Mode,bool Enhanced,EEGVillVCombatFXStage Stage,const FVector& ImpactPoint);
 void SampleAction(float Position);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void Destroyed() override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) EEGVillVCombatFXStage Stage=EEGVillVCombatFXStage::Shot;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 PresetIndex=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) float SourceTime=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) FVector ShotOrigin=FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) FVector ImpactPoint=FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 VisibleLayers=0;
private:
 UPROPERTY(Transient) TObjectPtr<UEGVillVCombatFXDefinition> Definition;
 UPROPERTY(Transient) TArray<FEGVillVFXsLayer> EffectLayers;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Components;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CannonProjectileComponent;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> Skill;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Pilot;
 UPROPERTY(Transient) TWeakObjectPtr<AEGVillVEmplacement> Device;
 FGuid Round,Action;
 FTransform Launch;
 TMap<int32,FTransform> BirthAnchors;
 float Duration=0;
 bool IsCurrent() const;
 void Evaluate();
 void ClearComponents();
};
