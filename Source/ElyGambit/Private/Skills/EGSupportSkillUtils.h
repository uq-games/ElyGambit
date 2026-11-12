#pragma once
#include "Engine/World.h"
#include "Character/EGChessUnit.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
namespace EGSupport {
inline float Radius(AEGChessUnit* Unit,float Cells)
{
 const auto* Mode=Unit&&Unit->GetWorld()?Unit->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Board=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(!Board)return Cells*500.f;
 const FVector Scale=Board->GetActorScale3D();
 return Cells*FMath::Min(Board->CellWidth*FMath::Abs(Scale.X),Board->CellDepth*FMath::Abs(Scale.Y));
}
inline bool Ally(AEGChessUnit* Source,AEGChessUnit* Target)
{return IsValid(Source)&&IsValid(Target)&&Target->IsAlive_Implementation()&&Source->GetFaction_Implementation()!=EEGFaction::Neutral&&Source->GetFaction_Implementation()==Target->GetFaction_Implementation();}
}
