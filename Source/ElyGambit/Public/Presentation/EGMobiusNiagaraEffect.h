#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGMobiusNiagaraEffect.generated.h"

class UNiagaraComponent;
class UNiagaraSystem;
class UEGHeroSkillComponent;
class UEGBattleContextComponent;
class AEGChessUnit;

/** Cosmetic one-shot. Its ownership and clock follow the battle, not damage. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGMobiusNiagaraEffect : public AActor
{
 GENERATED_BODY()
public:
 AEGMobiusNiagaraEffect();
 static AEGMobiusNiagaraEffect* Spawn(UEGHeroSkillComponent* Skill,UNiagaraSystem* System,const FVector& Location,const FRotator& Rotation,float Duration,bool bActionBound=false,AEGChessUnit* Target=nullptr);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Effect;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> SkillOwner;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> BattleOwner;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Target;
 FGuid Round,Action;
 float Age=0.f,Duration=0.f;
 bool bActionBound=false;
 bool bTargetBound=false;
};
