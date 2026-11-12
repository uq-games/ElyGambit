#pragma once

#include "CoreMinimal.h"
#include "Skills/EGHumanArrowProjectile.h"
#include "EGElysiaHumanArrowVisual.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UEGSourceEffectDefinition;

/** Human arrow cosmetics consume the projectile's actual flight and contacts. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGElysiaHumanArrowVisual : public AEGHumanArrowProjectile
{
 GENERATED_BODY()
public:
 AEGElysiaHumanArrowVisual();
 virtual void BeginPlay() override;
 virtual void OnArrowImpact_Implementation(AEGChessUnit* HitUnit,FVector ImpactPoint) override;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraComponent> FlightEffect;
 /** A short flash fixed at the spawn point, owned and cleaned by this projectile. */
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraComponent> LaunchEffect;
 /** Per-projectile original-source impact candidate; gameplay remains authoritative. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UEGSourceEffectDefinition> SourceImpact;
};
