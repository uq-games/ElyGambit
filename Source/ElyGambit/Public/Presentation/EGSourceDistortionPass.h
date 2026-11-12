#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGSourceDistortionPass.generated.h"
class UPrimitiveComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;

/** Per-world cosmetic accumulation of recovered source distortion primitives.
    The original material writes signed displacement, never visible geometry.
    No combat state, ownership transfer, collision or replication is involved. */
UCLASS(Transient,NotBlueprintable)
class ELYGAMBIT_API AEGSourceDistortionPass : public AActor
{
 GENERATED_BODY()
public:
 AEGSourceDistortionPass();
 static bool RegisterSource(UPrimitiveComponent* Part,UMaterialInterface* Composite);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 int32 GetSourceCount() const {return Sources.Num();}
 bool IsCompositeActive() const;
private:
 bool Configure(UMaterialInterface* Composite);
 UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> MapCapture;
 UPROPERTY(Transient) TObjectPtr<USceneCaptureComponent2D> DepthCapture;
 UPROPERTY(Transient) TObjectPtr<UPostProcessComponent> CompositeVolume;
 UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> Displacement;
 UPROPERTY(Transient) TObjectPtr<UTextureRenderTarget2D> SceneDepth;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> CompositeParent;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CompositeMID;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<UPrimitiveComponent>> Sources;
};
