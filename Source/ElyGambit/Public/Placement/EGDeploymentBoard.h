#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Types/EGChessTypes.h"
#include "EGDeploymentBoard.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Presentation-only local-space geometry; never used for placement or collision. */
struct FEGBoardPerimeterSegment
{
    FVector2D Start;
    FVector2D End;
};

struct FEGBoardPerimeterGeometry
{
    TArray<FEGBoardPerimeterSegment> Segments;
    FVector2D FriendlyCenter = FVector2D::ZeroVector;
    FVector2D EnemyCenter = FVector2D::ZeroVector;
};

UCLASS(Blueprintable)
class ELYGAMBIT_API AEGDeploymentBoard : public AActor
{
    GENERATED_BODY()
public:
    AEGDeploymentBoard();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable,Category="Placement") void RefreshVisuals();
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Grid",meta=(ClampMin="1",ClampMax="64")) int32 Columns=5;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Grid",meta=(ClampMin="1",ClampMax="64")) int32 Rows=6;
    /** Local negative Y is the near/player half. Full board hit tests include both halves. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Grid",meta=(ClampMin="1",ClampMax="64")) int32 PlayerRows=3;
    /** Non-deployable rows between factions; combat can still cross them. Zero preserves existing maps. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Grid",meta=(ClampMin="0",ClampMax="64")) int32 NeutralRows=0;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Visual") bool bAlwaysShowGrid=true;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Grid",meta=(ClampMin="1")) float CellWidth=160.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Grid",meta=(ClampMin="1")) float CellDepth=170.f;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Visual") TObjectPtr<UMaterialInterface> GhostMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Visual") TObjectPtr<UMaterialInterface> HighlightMaterial;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Placement") TObjectPtr<UBoxComponent> Query;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Placement") TObjectPtr<UStaticMeshComponent> Ghost;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Placement") TObjectPtr<UStaticMeshComponent> Highlight;
    /** Cosmetic only: the exact playable footprint, never a collision or occupancy boundary. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Visual") TObjectPtr<UMaterialInterface> PerimeterMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Placement|Visual") TObjectPtr<UMaterialInterface> BoardSurfaceMaterial;

    UFUNCTION(BlueprintPure,Category="Placement") bool IsCellValid(FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") bool IsPlayerCell(FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") bool IsEnemyCell(FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") bool TryWorldToCell(FVector WorldPosition,FIntPoint& Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") FVector CellToWorld(FIntPoint Cell) const;
    static FEGBoardPerimeterGeometry BuildPerimeterGeometry(int32 InColumns,int32 InRows,
        float InCellWidth,float InCellDepth,bool bIncreasingColumnIsScreenRight);
    FEGBoardPerimeterGeometry GetPerimeterGeometry() const;
    UFUNCTION(BlueprintPure,Category="Placement") AActor* GetOccupant(FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") bool IsCellAvailable(FIntPoint Cell) const;
    /** Footprints use an odd-sized rectangle centered on the supplied cell. */
    static bool MakeFootprintCells(FIntPoint Center,FIntPoint Size,TArray<FIntPoint>& OutCells);
    UFUNCTION(BlueprintPure,Category="Placement") bool CanOccupyFootprint(FIntPoint Center,FIntPoint Size,AActor* IgnorePiece=nullptr) const;
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryOccupyFootprint(FIntPoint Center,FIntPoint Size,AActor* Piece);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryMoveFootprint(AActor* Piece,FIntPoint Previous,FIntPoint Target,FIntPoint Size);
    UFUNCTION(BlueprintCallable,Category="Placement") bool ReleaseFootprint(FIntPoint Center,AActor* Piece);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryOccupyCell(FIntPoint Cell,AActor* Piece);
    UFUNCTION(BlueprintCallable,Category="Placement") bool ReleaseCell(FIntPoint Cell,AActor* Piece);
    UFUNCTION(BlueprintCallable,Category="Placement") void SetBoardVisualsVisible(bool bVisible);
    UFUNCTION(BlueprintCallable,Category="Placement") void SetHoveredCell(FIntPoint Cell,bool bLegal);
    UFUNCTION(BlueprintCallable,Category="Placement") void SetHoveredFootprint(FIntPoint Center,FIntPoint Size,bool bLegal);
    UFUNCTION(BlueprintCallable,Category="Placement") void ClearHoveredCell();
    UFUNCTION(BlueprintPure,Category="Placement") int32 GetOccupiedCount() const;

private:
    UPROPERTY(VisibleAnywhere, Category="Placement|Visual") TArray<TObjectPtr<UStaticMeshComponent>> Perimeter;
    UPROPERTY(VisibleAnywhere, Category="Placement|Visual") TArray<TObjectPtr<UStaticMeshComponent>> PerimeterDetails;
    UPROPERTY(VisibleAnywhere, Category="Placement|Visual") TArray<TObjectPtr<UStaticMeshComponent>> MasterPads;
    UPROPERTY(VisibleAnywhere, Category="Placement|Visual") TArray<TObjectPtr<UStaticMeshComponent>> FactionBoundaries;
    UFUNCTION() void RefreshPhaseVisuals(EEGMatchPhase Phase);
    UPROPERTY(Transient) TMap<FIntPoint,TWeakObjectPtr<AActor>> Occupants;
    UPROPERTY(Transient) TMap<TWeakObjectPtr<AActor>,FIntPoint> OccupiedAnchors;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HighlightMID;
    bool bVisualsVisible=false;
};
