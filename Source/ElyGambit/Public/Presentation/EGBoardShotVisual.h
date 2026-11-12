#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGBoardShotVisual.generated.h"
class AEGChessUnit;
class UEGBattleContextComponent;
class UStaticMeshComponent;
/** Cosmetic hitscan flash. Gameplay damage is submitted by the montage event. */
UCLASS()
class ELYGAMBIT_API AEGBoardShotVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGBoardShotVisual();
 static void Spawn(AEGChessUnit* Source,AEGChessUnit* Target,bool bCannon);
 virtual void Tick(float DeltaSeconds) override;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Beam;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 FGuid Round;
 float Age=0;
};
