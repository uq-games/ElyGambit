#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGDeploymentComponent.generated.h"

class AEGChessGameMode;
class AEGCardManager;
class AEGBoard;
class AEGChessUnit;

/** Bridges persistent placement to disposable battle actors. Owns no hand or roster copies. */
UCLASS()
class ELYGAMBIT_API UEGDeploymentComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UEGDeploymentComponent();
	void Configure(AEGChessGameMode* Mode, AEGCardManager* Cards, AEGBoard* Board);
	UFUNCTION() void Synchronize();
	bool TrySynchronize();
	void AttachPresentation(AEGChessUnit* Unit);
	void RestorePresentations();
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	UPROPERTY(Transient) TWeakObjectPtr<AEGChessGameMode> OwnerMode;
	UPROPERTY(Transient) TWeakObjectPtr<AEGCardManager> CardSource;
	UPROPERTY(Transient) TWeakObjectPtr<AEGBoard> CombatBoard;
	bool bSynchronizing = false;
};
