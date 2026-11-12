#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGCorruptionBarrierVisual.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class AEGChessUnit;
class UEGBattleContextComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class APlayerCameraManager;
class UEGBarrierCameraShake;

/** Local display only. The combat component owns wall collision and durability. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGCorruptionBarrierVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGCorruptionBarrierVisual();
 // Center is the ground point at the middle of the wall; dimensions are in cm.
 bool InitializeVisual(FVector Center, FVector WidthDirection, float Width, float Height);
 // Set for the staged entrance; clear before its owning hold is released.
 void SetPresentationToken(FGuid Token);
 // The staged entrance consumes the authoritative monotonic clock.
 void SetIntroElapsed(float Seconds);
 // Presentation-only offset from the start of the staged entrance.
 void SetIntroRevealDelay(float Seconds);
 void SetIntegrity(float Fraction);
 void Dissolve(bool bBroken);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
#if WITH_DEV_AUTOMATION_TESTS
 bool HasRevealShakeForTests() const;
 bool HasStartedRevealEffectsForTests() const {return bRevealEffectsStarted;}
#endif
private:
 void SpawnWallAccent(FVector RelativeLocation,float Scale);
 void StopRevealShake();
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Surface;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Backplane;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Bars;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Fragments;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Cracks;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> SurfaceMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BackplaneMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BarMaterial;
 UPROPERTY() TObjectPtr<UNiagaraSystem> WallImpactSystem;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> WallParticles;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Source;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TWeakObjectPtr<APlayerCameraManager> ShakeManager;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBarrierCameraShake> RevealShake;
 UPROPERTY(Transient) TWeakObjectPtr<AActor> ShakeViewTarget;
 FGuid Round;
 FGuid PresentationToken;
 FGuid ShakeLease;
 double LastRealSeconds=0.0;
 uint64 LastShakeCameraFrame=MAX_uint64;
 float WallWidth=0.f, WallHeight=0.f, Integrity=1.f, Age=0.f, HitFlash=0.f, EndAge=-1.f, IntroRevealDelay=0.f;
 float LastParticleAge=0.f, LastHitAccentAge=-1.f;
 bool bInitialized=false, bBroken=false, bContextBound=false, bRevealEffectsStarted=false;
};
