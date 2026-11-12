#pragma once
#include "CoreMinimal.h"

/** Local presentation only: fit ground and unit-head samples into the HUD safe area. */
struct ELYGAMBIT_API FEGBoardFraming
{
 static bool Solve(TConstArrayView<FVector> Points,FVector Focus,FRotator Rotation,
     float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation);
 /** Separate decorative ground framing from the playable cells and unit-head safe area. */
 static bool SolveReference(TConstArrayView<FVector> Ground,TConstArrayView<FVector> Units,
     FVector Focus,FRotator Rotation,float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation);
 static bool SolvePlayableGrid(TConstArrayView<FVector> GridEdges,TConstArrayView<FVector> CellSamples,
     FVector Focus,FRotator Rotation,float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation);
};
