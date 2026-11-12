#pragma once

#include "CoreMinimal.h"
#include "Presentation/EGSkillProjectile.h"
#include "EGHumanArrowProjectile.generated.h"

class UEGElysiaHerrscherSkillBehavior;

/** One released Human bow arrow. Gameplay moves and resolves this actor; its blueprint supplies visuals. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGHumanArrowProjectile : public AEGSkillProjectile
{
 GENERATED_BODY()
public:
 virtual void Tick(float DeltaSeconds) override;
 void InitializeArrow(UEGElysiaHerrscherSkillBehavior* InBehavior, FVector InDirection,
  float InSpeed, float InRadius, float InHalfWidth, float InLifetime, bool bInPiercing,float InLevelDistance);

 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Human Arrow") bool bPiercing=false;
 /** Captured at release; impact can occur after a newer action starts. */
 int32 SourceAttackIndex=INDEX_NONE;
 /** The point lies on the actual swept flight path. This event is presentation only. */
 UFUNCTION(BlueprintNativeEvent,Category="Human Arrow")
 void OnArrowImpact(AEGChessUnit* HitUnit,FVector ImpactPoint);
 virtual void OnArrowImpact_Implementation(AEGChessUnit* HitUnit,FVector ImpactPoint) {}
 UFUNCTION(BlueprintNativeEvent,Category="Human Arrow")
 void OnArrowExpired();
 virtual void OnArrowExpired_Implementation() {}

private:
 TWeakObjectPtr<UEGElysiaHerrscherSkillBehavior> Behavior;
 TSet<TWeakObjectPtr<AEGChessUnit>> HitUnits;
 FVector Direction=FVector::ForwardVector;
 float Speed=0.f,Radius=0.f,HalfWidth=0.f,MaxLifetime=0.f;
 float DistanceUntilLevel=0.f;
 bool bInitialized=false;
 bool CanContinue() const;
};
