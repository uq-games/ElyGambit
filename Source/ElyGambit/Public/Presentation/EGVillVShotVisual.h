#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGVillVShotVisual.generated.h"
class UStaticMesh;
class UStaticMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UEGHeroSkillComponent;
class AEGChessUnit;
class AEGVillVEmplacement;
class UEGSourceEffectDefinition;
class UEGVillVCombatFXDefinition;
class UNiagaraComponent;

USTRUCT()
struct FEGVillVShotV2Track
{
 GENERATED_BODY()
 UPROPERTY(Transient) TObjectPtr<UEGSourceEffectDefinition> Data;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Component;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
 int32 Index=INDEX_NONE;
 int32 Layer=0;
};

/** Cosmetic Vill-V bullet/shell flight. No damage, collision, GAS or replicated state. */
UCLASS()
class ELYGAMBIT_API AEGVillVShotVisual : public AActor
{
	GENERATED_BODY()
public:
	AEGVillVShotVisual();
	/** Cosmetic-only projectile presentation between fixed points; never submits damage. */
	static AEGVillVShotVisual* Spawn(UEGHeroSkillComponent* Skill,AEGChessUnit* Target,const FVector& Start,const FVector& End,
		UStaticMesh* Mesh,UMaterialInterface* Material,UStaticMesh* TrailMesh,UMaterialInterface* TrailMaterial,
		const FVector& MeshScale,const FRotator& MeshRotation,const FVector& TrailScale,float Duration,float ArcHeight);
    /** V2 visual-only shot, driven by an already authorized fire event. */
    static AEGVillVShotVisual* SpawnV2(UEGHeroSkillComponent* Skill,AEGVillVEmplacement* Device,
        UEGVillVCombatFXDefinition* Config,int32 PresetIndex,int32 BarrelIndex,FName MuzzleBone,
        const FTransform& Muzzle,const FVector& Target,const FGuid& ActionId,bool bIndependent=false);
	virtual void Tick(float DeltaSeconds) override;
protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	/** Actor root follows the flight direction; body/trail keep independent local transforms. */
	UPROPERTY(Transient) TObjectPtr<USceneComponent> FlightRoot;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Trail;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> TrailMaterialDynamic;
	UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> Skill;
	UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> OwnerUnit;
    UPROPERTY(Transient) TWeakObjectPtr<AEGVillVEmplacement> Device;
    UPROPERTY(Transient) TObjectPtr<UEGVillVCombatFXDefinition> V2Config;
    UPROPERTY(Transient) TArray<FEGVillVShotV2Track> V2Tracks;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> V2Spark;
    FGuid ActionId;
    FName MuzzleBone;
    FTransform FrozenMuzzle=FTransform::Identity;
    FTransform ReleaseAnchor=FTransform::Identity;
    float V2FlightTime=0.f;
    float V2SparkLifetime=0.f;
    bool bV2=false;
    bool bIndependentShot=false;
    bool InitializeV2(UEGVillVCombatFXDefinition* Config,int32 PresetIndex,int32 BarrelIndex);
    void TickV2(float DeltaSeconds);
    void ClearV2();
	FGuid RoundId;
	FVector StartPoint=FVector::ZeroVector;
	FVector Destination=FVector::ZeroVector;
	FVector Direction=FVector::ForwardVector;
	float Elapsed=0.f;
	float Lifetime=.1f;
	float Arc=.0f;
};
