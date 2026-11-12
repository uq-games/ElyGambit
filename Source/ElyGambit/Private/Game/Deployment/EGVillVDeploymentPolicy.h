#pragma once

#include "CoreMinimal.h"

class AEGCardManager;
class AEGDeployedPiece;

/** Vill-V-specific deployment. The manager remains owner of all transaction state. */
class FEGVillVDeploymentPolicy
{
public:
 static void PlayEntrances(AEGCardManager& Manager);
 static bool FindCells(const AEGCardManager& Manager,FIntPoint Person,FIntPoint& Gatling,FIntPoint& Cannon);
 static bool EnsureLayout(AEGCardManager& Manager,AEGDeployedPiece* Main);
 static bool SpawnParts(AEGCardManager& Manager,AEGDeployedPiece* Main);
 static void DestroyGroup(AEGCardManager& Manager,FGuid HeroId);
 static void SetVisible(AEGCardManager& Manager,FGuid HeroId,bool bVisible);
};
