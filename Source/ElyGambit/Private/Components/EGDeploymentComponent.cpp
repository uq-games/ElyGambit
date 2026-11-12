#include "Components/EGDeploymentComponent.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Placement/EGPlacementVisuals.h"
#include "EngineUtils.h"

UEGDeploymentComponent::UEGDeploymentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}
void UEGDeploymentComponent::Configure(AEGChessGameMode* Mode, AEGCardManager* Cards, AEGBoard* Board)
{
	if (CardSource.IsValid()) CardSource->OnBoardChanged.RemoveDynamic(this, &ThisClass::Synchronize);
	OwnerMode=Mode; CardSource=Cards; CombatBoard=Board;
	if (Cards) Cards->OnBoardChanged.AddUniqueDynamic(this, &ThisClass::Synchronize);
}
void UEGDeploymentComponent::Synchronize()
{TrySynchronize();}

bool UEGDeploymentComponent::TrySynchronize()
{
	if (bSynchronizing || !OwnerMode.IsValid() || !CardSource.IsValid() || !CombatBoard.IsValid() || !CardSource->CanEditPlacement()) return false;
	TGuardValue<bool> Guard(bSynchronizing, true);
	OwnerMode->RemoveReserveIncarnations();
	CardSource->RefreshHeroPresentations();
	for (auto* Piece : CardSource->GetPieces())
	{
		if (!Piece->RosterId.IsValid()) return false;
		if (auto* Unit=OwnerMode->SpawnRosterHero(Piece->RosterId))
		{
			if(!CombatBoard->PlaceUnit(Unit, Piece->Cell))return false;
			Unit->SetActorHiddenInGame(true);
		}
		else return false;
	}
	return true;
}
void UEGDeploymentComponent::AttachPresentation(AEGChessUnit* Unit)
{
	if (!Unit || !CardSource.IsValid()) return;
	for (auto* Piece : CardSource->GetPieces())
	{
		if (Piece->RosterId != Unit->GetInstanceId()) continue;
		Unit->AttachPresentation(Piece);
        CardSource->SetDevicePreparationVisible(Unit->GetInstanceId(),false);
		Unit->SetActorHiddenInGame(false);
		return;
	}
}
void UEGDeploymentComponent::RestorePresentations()
{
	if (!OwnerMode.IsValid()) return;
	for (TActorIterator<AEGChessUnit> It(GetWorld()); It; ++It)
		if (OwnerMode->OwnsUnit(*It)){It->RestorePresentation();if(CardSource.IsValid())CardSource->SetDevicePreparationVisible(It->GetInstanceId(),true);}
}
void UEGDeploymentComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	RestorePresentations();
	if (CardSource.IsValid()) CardSource->OnBoardChanged.RemoveDynamic(this, &ThisClass::Synchronize);
	CardSource.Reset(); OwnerMode.Reset(); CombatBoard.Reset();
	Super::EndPlay(Reason);
}
