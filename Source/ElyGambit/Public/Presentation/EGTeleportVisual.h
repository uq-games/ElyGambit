#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGTeleportVisual.generated.h"

class UNiagaraComponent;

/** Short-lived pack-based entrance accent; no gameplay or reveal authority. */
UCLASS()
class ELYGAMBIT_API AEGTeleportVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGTeleportVisual();
 static FSoftObjectPath GetPreloadPath();
 void Initialize(bool bEnemy, float Radius = 95.f, float Duration = 1.35f);
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Arrival;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Floor;
};
