#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGPresentationBurst.generated.h"
class UStaticMeshComponent;
class UMaterialInterface;

/** Short-lived cosmetic only; never participates in collision, occupancy or damage. */
UCLASS()
class ELYGAMBIT_API AEGPresentationBurst : public AActor
{
 GENERATED_BODY()
public:
 AEGPresentationBurst();
 FGuid RoundId;
 float FixedAreaRadius=0;
 virtual void Tick(float DeltaSeconds) override;
 static AEGPresentationBurst* Spawn(UWorld* World,FVector Position,UMaterialInterface* Material,float Size=1.f);
 static AEGPresentationBurst* SpawnContact(UWorld* World,FVector Position,UMaterialInterface* Material,FGuid Round);
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Ring;
 UPROPERTY(VisibleAnywhere) TArray<TObjectPtr<UStaticMeshComponent>> Sparks;
 float Age=0.f;
 float Radius=1.f;
 bool bContact=false;
};

/**
 * Flat ground ring used for the ultimate descent telegraph and the impact
 * shockwave. Purely cosmetic: no collision, no damage, self-destroying.
 */
UCLASS()
class ELYGAMBIT_API AEGDescentRing : public AActor
{
 GENERATED_BODY()
public:
 AEGDescentRing();
 virtual void Tick(float DeltaSeconds) override;
 /** Growing/pulsing warning ring at a fixed landing point for Duration seconds. */
 static AEGDescentRing* SpawnTelegraph(UWorld* World,FVector Position,UMaterialInterface* Material,float Radius,float Duration);
 /** One-shot expanding ground shockwave. */
 static AEGDescentRing* SpawnShockwave(UWorld* World,FVector Position,UMaterialInterface* Material,float Radius,float Duration=.45f);
 void SetTint(const FLinearColor& Color);
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Outer;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Inner;
 float Age=0.f;
 float Duration=1.f;
 float Radius=1.f;
 bool bShockwave=false;
};
