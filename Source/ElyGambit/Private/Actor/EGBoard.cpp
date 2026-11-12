#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"

#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

AEGBoard::AEGBoard()
{
	PrimaryActorTick.bCanEverTick = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool AEGBoard::CanEdit() const
{
	const AEGChessGameMode* GM = GetWorld()->GetAuthGameMode<AEGChessGameMode>();
	return GM && GM->GetMatch() && GM->GetMatch()->GetPhase() == EEGMatchPhase::Preparation;
}

bool AEGBoard::IsValidCell(FIntPoint Cell) const
{
	return IsValid(LayoutSource)?LayoutSource->IsCellValid(Cell):Cell.X >= 0 && Cell.Y >= 0 && Cell.X < Dimensions.X && Cell.Y < Dimensions.Y;
}

bool AEGBoard::IsDeploymentCell(FIntPoint Cell, EEGFaction Team) const
{
	if (!IsValidCell(Cell))
	{
		return false;
	}
	if(IsValid(LayoutSource)) return Team==EEGFaction::Player?LayoutSource->IsPlayerCell(Cell):Team==EEGFaction::Enemy&&LayoutSource->IsEnemyCell(Cell);
	return Team == EEGFaction::Player ? Cell.X < Dimensions.X / 2
									  : Team == EEGFaction::Enemy && Cell.X >= Dimensions.X / 2;
}

FVector AEGBoard::CellToWorld(FIntPoint Cell) const
{
	if(IsValid(LayoutSource)) return LayoutSource->CellToWorld(Cell)+FVector(0,0,UnitCenterHeight);
	// Legacy origin remains the capsule center of cell (0,0).
	return GetActorTransform().TransformPosition(FVector(Cell.X * CellSize, Cell.Y * CellSize, 0.f));
}

AEGChessUnit* AEGBoard::GetOccupant(FIntPoint Cell) const
{
	const TWeakObjectPtr<AEGChessUnit>* Entry = Occupancy.Find(Cell);
	return Entry ? Entry->Get() : nullptr;
}

bool AEGBoard::PlaceUnit(AEGChessUnit* Unit, FIntPoint Cell)
{
	AEGChessGameMode* GM = GetWorld()->GetAuthGameMode<AEGChessGameMode>();
	if (bEditing || !CanEdit() || !IsValid(Unit) || !GM->OwnsUnit(Unit) || !IEGCombatant::Execute_IsAlive(Unit) ||
		!IsDeploymentCell(Cell, IEGCombatant::Execute_GetFaction(Unit)))
	{
		return false;
	}
	const UEGHeroDefinition* Definition=Unit->GetDefinition();
	const FIntPoint Footprint=Definition?Definition->GetPlacementFootprint():FIntPoint(1,1);
	TArray<FIntPoint> Cells;
	if(!AEGDeploymentBoard::MakeFootprintCells(Cell,Footprint,Cells))return false;
	const EEGFaction Team=IEGCombatant::Execute_GetFaction(Unit);
	for(const FIntPoint OccupiedCell:Cells)
	{
		if(!IsDeploymentCell(OccupiedCell,Team))return false;
		if(AEGChessUnit* Existing=GetOccupant(OccupiedCell);Existing&&Existing!=Unit)return false;
	}
	TGuardValue<bool> Guard(bEditing, true);
	// Teleport the incarnation, then commit its preparation cell. Never read position back from movement.
	const FVector Destination = CellToWorld(Cell);
	// Settled characters may differ by floating-point noise. A no-op move can return false.
	if (!Unit->GetActorLocation().Equals(Destination, UE_KINDA_SMALL_NUMBER) &&
		!Unit->SetActorLocation(Destination, false, nullptr, ETeleportType::TeleportPhysics))
	{
		return false;
	}
	for (auto It = Occupancy.CreateIterator(); It; ++It)
	{
		if (!It.Value().IsValid() || It.Value().Get() == Unit)
		{
			It.RemoveCurrent();
		}
	}
	for(const FIntPoint OccupiedCell:Cells)Occupancy.Add(OccupiedCell,Unit);
	if (IEGCombatant::Execute_GetFaction(Unit) == EEGFaction::Player)
	{
		GM->GetRoster()->SetDeployment(Unit->GetInstanceId(), Cell);
	}
	return true;
}

bool AEGBoard::RemoveUnit(AEGChessUnit* Unit)
{
	if (bEditing || !CanEdit() || !IsValid(Unit))
	{
		return false;
	}
	bool bRemoved = false;
	for (auto It = Occupancy.CreateIterator(); It; ++It)
	{
		if (It.Value().Get() == Unit)
		{
			It.RemoveCurrent();
			bRemoved = true;
		}
	}
	return bRemoved;
}
void AEGBoard::ClearBindings()
{
	Occupancy.Reset();
}

void AEGBoard::UseLayout(AEGDeploymentBoard* Source)
{
 LayoutSource=Source; if(IsValid(Source))Dimensions=FIntPoint(Source->Columns,Source->Rows);
}
