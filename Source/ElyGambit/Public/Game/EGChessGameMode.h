#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Types/EGChessTypes.h"
#include "EGChessGameMode.generated.h"

class UEGMatchManager;
class UEGHeroRoster;
class UEGHeroDefinition;
class AEGBoard;
class AEGChessUnit;
class UEGBattleContextComponent;

/** Composition root only: match, placement, persistence and unit execution live in separate modules. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGChessGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AEGChessGameMode();
	/** Release one registered pause request without clearing other systems' pausers. */
	bool ReleasePauseRequest(FDelegateHandle Handle);
	/** Read-only identity check; never calls another owner's unpause delegate. */
	bool IsOnlyPauseRequest(FDelegateHandle Handle) const;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AutoChess|Battle") TObjectPtr<UEGBattleContextComponent> BattleRules;
	const TArray<TWeakObjectPtr<AEGChessUnit>>& GetBattleUnits() const {return Units;}
	/** Human form at resolution, captured before combat teardown resets behavior. */
	bool WasHumanOriginAtSettlement(AEGChessUnit* Unit) const;
	AEGChessUnit* SpawnBattleSummon(UEGHeroDefinition* Definition,int32 Level,FVector Point,EEGFaction Faction=EEGFaction::Player,bool bRequireFreeSpace=false);
 AEGChessUnit* SpawnBattleEmplacement(UEGHeroDefinition* Definition,AEGChessUnit* Hero,int32 Index,FVector Point);
	void RemoveReserveIncarnations();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void StartPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Match")
	virtual bool StartGame(bool bResetRoster = true);

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Match")
	virtual bool EnterCombat();

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Match")
	bool ResolveCombat(bool bVictory);

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Match")
	virtual bool NextStage();

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	UEGMatchManager* GetMatch() const { return Match; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	UEGHeroRoster* GetRoster() const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	AEGBoard* GetBoard() const { return Board; }

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Match")
	AEGChessUnit* SpawnRosterHero(FGuid Id);

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Match")
	AEGChessUnit* SpawnEnemy(UEGHeroDefinition* Definition, int32 Level, FIntPoint Cell);

	UFUNCTION(BlueprintPure, Category = "AutoChess|Match")
	bool OwnsUnit(const AEGChessUnit* Unit) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Match")
	TSubclassOf<AEGChessUnit> UnitClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Match")
	TSubclassOf<AEGBoard> BoardClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AutoChess|Match")
	bool bAutoStart = true;
	// TODO EnemySpawner: use stage type + corruption; copies read roster snapshots, never share instance IDs.
	UFUNCTION(BlueprintImplementableEvent, Category = "AutoChess|Match")
	void PrepareEnemies(int32 StageNumber, EEGStageType StageType, int32 Corruption);

protected:
	/** Called after entering the combat phase, before opening items start. */
	virtual void PrepareBattleOpening() {}
	UFUNCTION() void CombatRunning(FGuid RoundId);
	virtual void PrepareStageEnemies();
    virtual void PrepareUnitForCombat(AEGChessUnit* Unit,const FEGHeroInstance& Hero);
	virtual void PrepareSummonForCombat(AEGChessUnit* Unit) {}
	const TArray<TWeakObjectPtr<AEGChessUnit>>& GetManagedUnits() const { return Units; }
	UPROPERTY(Transient)
	TObjectPtr<UEGMatchManager> Match;

	UPROPERTY(Transient)
	TObjectPtr<AEGBoard> Board;

	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AEGChessUnit>> Units;
	TMap<TWeakObjectPtr<AEGChessUnit>,bool> SettlementHumanOrigin;
	bool bCoordinating = false;
	AEGChessUnit* SpawnUnit(UEGHeroDefinition* Definition, FGuid Id, EEGFaction Faction, int32 Level, FIntPoint Cell);
	/** Detach a preparation disguise for presentation; caller owns its destruction. */
	bool DetachPreparedUnitForOpening(AEGChessUnit* Unit);
	/** Remove one newly prepared unit without disturbing other placements. */
	void DiscardPreparedUnit(AEGChessUnit* Unit);
	void ClearUnits();

	UFUNCTION()
	void PhaseChanged(EEGMatchPhase Phase);

	UFUNCTION()
	void HeroChanged(FGuid Id);
};
