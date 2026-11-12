#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGCorruptionBarrier.generated.h"

class UBoxComponent;

/** Battle-owned blocking geometry. Damage and lifetime are owned by the boss component. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGCorruptionBarrier : public AActor
{
 GENERATED_BODY()
public:
 AEGCorruptionBarrier();
 static constexpr float HalfThickness=8.f;
 bool InitializeWall(FVector GroundCenter,FVector WidthDirection,float Width,float Height);
 void SetBlocking(bool bBlocking);
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UBoxComponent> Wall;
};
