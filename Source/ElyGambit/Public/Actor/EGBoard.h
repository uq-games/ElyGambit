#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/EGChessTypes.h"
#include "EGBoard.generated.h"

class AEGChessUnit;
class AEGDeploymentBoard;

/** Occupancy represents preparation deployment only; combat movement never updates cells. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGBoard : public AActor
{
	GENERATED_BODY()

public:
	AEGBoard();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "2"), Category = "AutoChess|Board")
	FIntPoint Dimensions = FIntPoint(8, 8);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"), Category = "AutoChess|Board")
	float CellSize = 200.f;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Board")
	bool IsValidCell(FIntPoint Cell) const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Board")
	bool IsDeploymentCell(FIntPoint Cell, EEGFaction Faction) const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Board")
	FVector CellToWorld(FIntPoint Cell) const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Board")
	AEGChessUnit* GetOccupant(FIntPoint Cell) const;

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Board")
	bool PlaceUnit(AEGChessUnit* Unit, FIntPoint Cell);
	// Only removes the Actor's board binding. Deployed roster state is retained (no return to reserve).
	UFUNCTION(BlueprintCallable, Category = "AutoChess|Board")
	bool RemoveUnit(AEGChessUnit* Unit);
	void ClearBindings();
    /** Shared geometry; occupancy remains specific to preparation combat incarnations. */
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="AutoChess|Board") TObjectPtr<AEGDeploymentBoard> LayoutSource;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="AutoChess|Board") float UnitCenterHeight=88.f;
    void UseLayout(AEGDeploymentBoard* Source);

private:
	UPROPERTY(Transient)
	TMap<FIntPoint, TWeakObjectPtr<AEGChessUnit>> Occupancy;
	bool bEditing = false;
	bool CanEdit() const;
};
