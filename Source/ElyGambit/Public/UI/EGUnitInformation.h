#pragma once
#include "CoreMinimal.h"
class AEGChessUnit;

/** Formats authoritative battle counters for hover and persistent details. */
namespace EGUnitInformation
{
    FString Status(const AEGChessUnit* Unit);
}
