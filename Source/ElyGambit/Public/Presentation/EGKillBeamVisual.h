#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGKillBeamVisual.generated.h"

class UNiagaraComponent;
class UEGMatchManager;

/** Cosmetic death tail, independent of the dead unit and battle-action cancellation. */
UCLASS()
class ELYGAMBIT_API AEGKillBeamVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGKillBeamVisual();
 static FSoftObjectPath GetPreloadPath();
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UNiagaraComponent> Beam;
 UPROPERTY(Transient) TWeakObjectPtr<UEGMatchManager> Match;
};
