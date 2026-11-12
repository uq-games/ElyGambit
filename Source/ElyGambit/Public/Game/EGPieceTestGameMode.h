#pragma once

#include "CoreMinimal.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGHeroRoster.h"
#include "EGPieceTestGameMode.generated.h"

/** Isolated sandbox using the normal roster, GAS, animation and battle services. */
UCLASS()
class ELYGAMBIT_API AEGPieceTestGameMode : public AEGChessGameMode
{
 GENERATED_BODY()
public:
 AEGPieceTestGameMode();
 virtual void StartPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual bool StartGame(bool bResetRoster=true) override;
 UFUNCTION(BlueprintCallable, Category="Piece Test") bool ResetTest();
 UFUNCTION(BlueprintCallable, Category="Piece Test") bool SelectPiece(int32 Index);
 UFUNCTION(BlueprintPure, Category="Piece Test") int32 GetSelectedIndex() const {return SelectedIndex;}
 UFUNCTION(BlueprintPure, Category="Piece Test") FString GetTestStatus() const;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Piece Test") TArray<TObjectPtr<UEGHeroDefinition>> AvailablePieces;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Piece Test") TObjectPtr<UEGHeroDefinition> FirstMonster;
 UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Piece Test") TObjectPtr<UEGHeroDefinition> SecondMonster;
protected:
 virtual void PrepareStageEnemies() override;
private:
 void TickMonsterReview();
 double MonsterReviewStarted=-1;
 FString MonsterReviewFolder,MonsterReviewRows;
 int32 MonsterReviewCapture=0;
 bool bMonsterReviewFast=false,bMonsterReviewSawProjectile=false;
 bool PopulatePlayer();
 int32 SelectedIndex=0;
 bool bResetting=false;
 UPROPERTY(Transient) FEGHeroRosterRunSnapshot PreviousRoster;
};
