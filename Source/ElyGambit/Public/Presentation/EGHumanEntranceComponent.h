#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGHumanEntranceComponent.generated.h"

class AEGArtPiece;
class UAudioComponent;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;

/** Short, local light and sound accent over the human preparation animation. */
UCLASS(Transient, NotBlueprintable)
class ELYGAMBIT_API UEGHumanEntranceComponent final : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGHumanEntranceComponent();
 static bool Play(AEGArtPiece* Piece);
 void Cancel(bool bDiscardDeferredVoice=true);
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;

private:
#if WITH_DEV_AUTOMATION_TESTS
 friend class FEGHumanEntranceLifecycle;
 static bool bForceInitializationFailureForTests;
#endif
 bool BeginPresentation(AEGArtPiece* Piece);
 void ReleasePresentation(bool bDiscardDeferredVoice);
 void UpdatePresentation();

 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BurstCard;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BurstMaterial;
 UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> BurstSystem;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Burst;
 // The light cue belongs to this entrance and is stopped on every exit path.
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> RevealAudio;
 TWeakObjectPtr<AEGArtPiece> Target;
 float Elapsed=0.f;
 bool bReleased=false;
 bool bRevealSoundTriggered=false;
};
