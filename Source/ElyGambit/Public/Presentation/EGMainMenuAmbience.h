#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGMainMenuAmbience.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** Small, deterministic field of camera-facing-independent 3D petals for a menu scene. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGMainMenuAmbience : public AActor
{
    GENERATED_BODY()

public:
    AEGMainMenuAmbience();

    /** A 100 cm plane is the intended input; no mesh means no instances. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience")
    TObjectPtr<UStaticMesh> PetalMesh;

    /** Prefer a private, two-sided masked petal material. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience")
    TObjectPtr<UMaterialInterface> PetalMaterial;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience", meta=(ClampMin="0", ClampMax="64", UIMin="0", UIMax="64"))
    int32 ParticleCount = 40;

    /** Local half extents of the falling field, in centimetres. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience")
    FVector BoxExtent = FVector(500.f, 350.f, 260.f);

    /** Horizontal direction of the periodic breeze in actor-local space. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience")
    FVector WindDirection = FVector(1.f, 0.3f, 0.f);

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience", meta=(ClampMin="0", UIMin="0", UIMax="200", Units="cm"))
    float WindAmplitude = 70.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience", meta=(ClampMin="2", UIMin="2", UIMax="60", Units="s"))
    float CycleSeconds = 24.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience")
    int32 Seed = 514;

    /** Approximate width for the intended 100 cm engine plane. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience", meta=(ClampMin="0.1", UIMin="1", UIMax="30", Units="cm"))
    float PetalSizeCm = 7.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Menu Ambience", meta=(ClampMin="10", ClampMax="30", UIMin="10", UIMax="30"))
    float UpdateRateHz = 24.f;

    /** Rebuild after changing exposed properties at runtime; keeps one component. */
    UFUNCTION(BlueprintCallable, Category="Menu Ambience")
    void RebuildPetals();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;

protected:
    virtual void BeginPlay() override;

private:
    struct FPetalState
    {
        FVector2D Centre = FVector2D::ZeroVector;
        float Phase = 0.f;
        float Size = 1.f;
        float Yaw = 0.f;
        float Flutter = 0.f;
    };

    UPROPERTY(VisibleAnywhere, Category="Menu Ambience", meta=(AllowPrivateAccess="true"))
    TObjectPtr<UInstancedStaticMeshComponent> Petals;

    TArray<FPetalState> States;
    TArray<FTransform> FrameTransforms;
    float ElapsedSeconds = 0.f;

    FTransform MakePetalTransform(const FPetalState& State, float TimeSeconds) const;
    void RefreshTransforms();
};
