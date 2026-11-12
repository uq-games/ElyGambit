#pragma once

#include "CoreMinimal.h"

class UEGHeroDefinition;
class UObject;

/** Model-only scale for board pieces. This never changes the actor, collision or source-FX scale. */
struct ELYGAMBIT_API FEGBoardModelSizing
{
 static float Resolve(const UEGHeroDefinition* Definition,const UObject* WorldContext);
 /** Discard an instance's resolved value when its presentation is reconfigured. */
 static void Forget(const UObject* WorldContext);
 static float Fit(float CellWidth,float CellDepth,float BodyWidth,float BodyHeight,float HeightCells,float AuthoredScale);
};
