#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGAponiaDisciplineVisual.generated.h"

class AEGChessUnit;
class UEGAponiaSkillBehavior;
class UEGBattleContextComponent;
class UNiagaraSystem;
class UNiagaraComponent;

/** Cosmetic display of the authoritative discipline count; never submits combat events. */
UCLASS()
class ELYGAMBIT_API AEGAponiaDisciplineVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGAponiaDisciplineVisual();
 static AEGAponiaDisciplineVisual* Spawn(AEGChessUnit* Owner,UEGAponiaSkillBehavior* Behavior,UNiagaraSystem* Definition);
 virtual void Tick(float DeltaSeconds) override;
private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Caster;
 UPROPERTY(Transient) TWeakObjectPtr<UEGAponiaSkillBehavior> State;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> Data;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> Nodes;
 int32 PreviousCount=0;
 FGuid Round;
 float Age=0.f;
};
