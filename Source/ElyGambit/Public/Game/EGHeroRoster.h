#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/EGChessTypes.h"
#include "EGHeroRoster.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGHeroChanged, FGuid, HeroId);

/** Complete run state for an isolated test session; roster remains the sole writer. */
USTRUCT()
struct FEGHeroRosterRunSnapshot
{
 GENERATED_BODY()
 UPROPERTY() TArray<FEGHeroInstance> Heroes;
 UPROPERTY() TSet<FName> FormPermissions;
 UPROPERTY() int32 MergeLevelOverride=0;
 UPROPERTY() bool bStartingCardsIssued=false;
};

/** Persists across combat Actor destruction and map travel, until explicitly reset for a new run. */
UCLASS()
class ELYGAMBIT_API UEGHeroRoster : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "AutoChess|Roster")
	FGuid AddHero(UEGHeroDefinition* Definition);

	UFUNCTION(BlueprintPure, Category = "AutoChess|Roster")
	bool GetHero(FGuid Id, FEGHeroInstance& OutHero) const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Roster")
	TArray<FEGHeroInstance> GetHeroes() const { return Heroes; }

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Roster")
	bool AddLevels(FGuid Id, int32 Amount);
	// Called by the future CardManager only after validating/consuming the source card.
	UFUNCTION(BlueprintCallable, Category = "AutoChess|Roster")
	bool ApplySameNameCard(FGuid Id);

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Roster")
	bool SellHero(FGuid Id);
	// Match coordinator only; Blueprint starts a new run via GameMode.StartGame.
	void ResetRun();
	UFUNCTION(BlueprintPure,Category="AutoChess|Roster") FGuid FindHeroByDefinition(UEGHeroDefinition* Definition) const;
	UFUNCTION(BlueprintPure,Category="AutoChess|Roster") UEGHeroDefinition* ResolveDefinition(FGuid Id) const;
	UFUNCTION(BlueprintCallable,Category="AutoChess|Roster") bool Withdraw(FGuid Id);
	UFUNCTION(BlueprintCallable,Category="AutoChess|Roster") bool SetForm(FGuid Id,EEGHeroForm Form);
	bool RemoveUncommittedHero(FGuid Id);
	bool UnlockForm(UEGHeroDefinition* Definition);
	bool IsFormUnlocked(const UEGHeroDefinition* Definition) const;
	void SetMergeLevelOverride(int32 Gain){MergeLevelOverride=FMath::Clamp(Gain,0,100);}
	int32 GetMergeLevelOverride() const {return MergeLevelOverride;}
	void RestoreSnapshot(const TArray<FEGHeroInstance>& Snapshot);
	FEGHeroRosterRunSnapshot CaptureRunState() const;
	void RestoreRunState(const FEGHeroRosterRunSnapshot& Snapshot);
	void BeginMutation(){++MutationDepth;}
	void EndMutation();

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Roster")
	FEGHeroChanged OnHeroChanged;

	// Board owns placement validation; deployed heroes cannot return to reserve.
	bool SetDeployment(FGuid Id, FIntPoint Cell);
 bool SetVillVLayout(FGuid Id,FIntPoint Person,FIntPoint Gatling,FIntPoint Cannon);

private:
	friend class AEGCardManager;
	// The card manager coordinates roster removal, board cleanup and payment.
	bool SellHeroFromCardManager(FGuid Id);
	bool bHandSettlementLocked=false;
	bool RemoveOverflowReserve(FGuid Id);
	UPROPERTY(Transient) TSet<FName> FormPermissions;
	int32 MergeLevelOverride=0;
	/** Run-scoped receipt; restored with roster snapshots so grants cannot be repeated. */
	UPROPERTY(SaveGame) bool bStartingCardsIssued=false;
	UPROPERTY()
	TArray<FEGHeroInstance> Heroes;
	FEGHeroInstance* FindMutable(FGuid Id);
	bool CanEdit() const;
	int32 MutationDepth=0;
	TArray<FGuid> PendingChanges;
	void NotifyHeroChanged(FGuid Id);
};

/** Defer roster notifications until the card transaction has updated hand and occupancy. */
struct FEGScopedRosterMutation
{
 explicit FEGScopedRosterMutation(UEGHeroRoster* InRoster):Roster(InRoster){if(Roster)Roster->BeginMutation();}
 ~FEGScopedRosterMutation(){if(Roster)Roster->EndMutation();}
 FEGScopedRosterMutation(const FEGScopedRosterMutation&)=delete;
 FEGScopedRosterMutation& operator=(const FEGScopedRosterMutation&)=delete;
private:
 UEGHeroRoster* Roster;
};
