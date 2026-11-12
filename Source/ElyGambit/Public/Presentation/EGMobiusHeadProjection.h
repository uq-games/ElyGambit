#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGMobiusHeadProjection.generated.h"
class AEGChessUnit;
class AEGCombatVisual;
class UEGBattleContextComponent;
class UMaterialInstanceDynamic;

/** Brief cropped pose of the existing serpent. No collision or gameplay authority. */
UCLASS()
class ELYGAMBIT_API AEGMobiusHeadProjection : public AActor
{
 GENERATED_BODY()
public:
 AEGMobiusHeadProjection();
 static AEGMobiusHeadProjection* Spawn(AEGChessUnit* Caster,AEGChessUnit* Target);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> Rig;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Caster;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Target;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
 FGuid Round;
 FVector Start,Finish,HeadOffset;
 float Age=0.f;
};
