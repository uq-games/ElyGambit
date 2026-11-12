#pragma once
#include "CoreMinimal.h"
class AEGChessUnit;

// Board-aligned world-space combat geometry. A 1x1 unit retains its historical
// point/center semantics; a multi-cell unit occupies its full board rectangle.
namespace EGCombatGeometry
{
 double DistanceSquared(const AEGChessUnit* A,const AEGChessUnit* B);
 double PointDistanceSquared(FVector Point,const AEGChessUnit* Unit);
 FVector ClosestPoint(FVector Point,const AEGChessUnit* Unit);
 bool HasLargeFootprint(const AEGChessUnit* Unit);
 // A moving circular body against a large board-aligned rectangle, including
 // clearance. Already-overlapping movers may move toward the nearest exit.
 bool SegmentBlocked(FVector Start,FVector End,const AEGChessUnit* Obstacle,float Clearance);
 FVector ProjectOutside(FVector Point,const AEGChessUnit* Obstacle,float Clearance);
}
