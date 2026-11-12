#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/EGRunDefinition.h"
#include "Types/EGPlacementTypes.h"
#include "Types/EGChessTypes.h"
#include "EGCardManager.generated.h"

class AEGDeploymentBoard;
class AEGDeployedPiece;
class UEGCardDefinition;
class UEGHeroDefinition;
class UEGHeroRoster;

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGCardInstance
{
    GENERATED_BODY()
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") FGuid InstanceId;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") FName CardId=TEXT("Elysia");
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") TObjectPtr<UEGCardDefinition> Definition;
    /** Captured at grant; the roster owns progression after first deployment. */
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") int32 InitialLevel=1;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") int64 PaidCost=0;
};

/** Run checkpoint data keeps the asset references alive across the final encounter. */
USTRUCT()
struct FEGCheckpointPiece
{
    GENERATED_BODY()
    UPROPERTY() TSubclassOf<AEGDeployedPiece> PieceClass;
    UPROPERTY() TObjectPtr<UEGCardDefinition> Card;
    UPROPERTY() TObjectPtr<UEGHeroDefinition> Visual;
    UPROPERTY() FGuid InstanceId;
    UPROPERTY() FGuid RosterId;
    UPROPERTY() FIntPoint Cell=FIntPoint::ZeroValue;
    UPROPERTY() EEGDeploymentPart PartRole=EEGDeploymentPart::Hero;
};
USTRUCT()
struct FEGCardRunCheckpoint
{
    GENERATED_BODY()
    UPROPERTY() TArray<FEGCardInstance> Hand;
    UPROPERTY() TArray<FGuid> Order;
    UPROPERTY() TArray<TObjectPtr<UEGCardDefinition>> Shop;
    UPROPERTY() TArray<TObjectPtr<UEGCardDefinition>> RegisteredCards;
    UPROPERTY() TArray<FEGCheckpointPiece> Pieces;
    UPROPERTY() FEGCardRules Rules;
    UPROPERTY() int64 Gold=0;
    UPROPERTY() int32 CorruptionShopSlot=INDEX_NONE;
    UPROPERTY() int32 LastIncomeStage=0;
    UPROPERTY() int32 InitialRound=1;
    UPROPERTY() int32 PopulationFloor=0;
    UPROPERTY() int32 FreeRefreshesUsed=0;
    UPROPERTY() bool bShopLocked=false;
    UPROPERTY() FRandomStream Random;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEGCardManagerChanged);


/** Authority for prototype hand, sale rewards and deployed registrations; no combat. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGCardManager : public AActor
{
    GENERATED_BODY()
public:
    AEGCardManager();
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement|Cards") TArray<TObjectPtr<UEGCardDefinition>> CardPool;
    UFUNCTION(BlueprintPure,Category="Placement") bool HasReserveHero(FGuid Id) const;
    UFUNCTION(BlueprintPure,Category="Placement") TArray<FEGHeroInstance> GetReserveHeroes() const;
    UFUNCTION(BlueprintPure,Category="Placement") UEGCardDefinition* FindHeroCardDefinition(FGuid Id) const;
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryDeployReserve(FGuid Id,FIntPoint Cell);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryWithdraw(AEGDeployedPiece* Piece);
    UFUNCTION(BlueprintPure,Category="Placement") bool CanWithdraw(AEGDeployedPiece* Piece) const;
    UFUNCTION(BlueprintPure,Category="Placement") bool IsSaleProtected(FGuid Id) const;
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryMergeIntoHero(FGuid CardId,FGuid HeroId);
    UFUNCTION(BlueprintCallable,Category="Placement") bool SelectShopCard(int32 Index);
    void RefreshHeroPresentations();
    UFUNCTION(BlueprintPure,Category="Placement") TArray<AEGDeployedPiece*> GetDeploymentParts() const;
    void SetDevicePreparationVisible(FGuid HeroId,bool bVisible);
    bool EnsureVillVLayout(AEGDeployedPiece* Main);
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="Placement") TObjectPtr<AEGDeploymentBoard> Board;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Placement") TSubclassOf<AEGDeployedPiece> PieceClass;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement|Cards") TObjectPtr<UEGCardDefinition> AvailableCardDefinition;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement|Round",meta=(ClampMin="1")) int32 InitialRound=1;
    UPROPERTY(BlueprintAssignable,Category="Placement") FEGCardManagerChanged OnHandChanged;
    UPROPERTY(BlueprintAssignable,Category="Placement") FEGCardManagerChanged OnBoardChanged;
    UPROPERTY(BlueprintAssignable,Category="Placement") FEGCardManagerChanged OnEconomyChanged;
    UFUNCTION(BlueprintCallable,Category="Placement") bool InitializeCards();
    UFUNCTION(BlueprintCallable,Category="Placement") bool ObtainDebugElysiaCard();
    UFUNCTION(BlueprintCallable,Category="Placement") bool RequestObtainCard(UEGCardDefinition* Definition);
    UFUNCTION(BlueprintPure,Category="Placement") UEGCardDefinition* FindHandCardDefinition(FGuid Id) const;
    UFUNCTION(BlueprintPure,Category="Placement") int64 GetGold() const {return Gold;}
    UFUNCTION(BlueprintPure,Category="Placement") int32 GetRoundNumber() const {return FMath::Max(1,InitialRound);}
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryDeployHandCard(FGuid InstanceId,FIntPoint TargetCell);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryMovePlacedPiece(AEGDeployedPiece* Piece,FIntPoint TargetCell);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TrySellHandCard(FGuid InstanceId);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TrySellPlacedPiece(AEGDeployedPiece* Piece);
    UFUNCTION(BlueprintCallable,Category="Placement") bool TrySellReserveHero(FGuid Id);
    UFUNCTION(BlueprintPure,Category="Placement") int64 GetSaleValue(FGuid Id) const;
    UFUNCTION(BlueprintPure,Category="Placement") TArray<FEGCardInstance> GetHand() const {return Hand;}
    /** Unified visible hand order, including heroes returned to reserve. */
    UFUNCTION(BlueprintPure,Category="Placement") TArray<FGuid> GetOrderedHandIds() const {return OrderedHandIds;}
    UFUNCTION(BlueprintPure,Category="Placement") int32 GetHandOrderRevision() const {return HandOrderRevision;}
    UFUNCTION(BlueprintPure,Category="Placement") TArray<FGuid> GetOverflowHandIds() const;
    UFUNCTION(BlueprintPure,Category="Placement") int64 GetOverflowSaleValue() const;
    UFUNCTION(BlueprintPure,Category="Placement") bool IsHandOverflowProtected(FGuid Id) const;
    UFUNCTION(BlueprintCallable,Category="Placement") bool TryReorderHandCard(FGuid Id,FGuid BeforeId,int32 ExpectedRevision);
    UFUNCTION(BlueprintPure,Category="Placement") bool HasHandCard(FGuid Id) const;
    UFUNCTION(BlueprintPure,Category="Placement") bool OwnsPiece(const AEGDeployedPiece* Piece) const;
    UFUNCTION(BlueprintPure,Category="Placement") TArray<AEGDeployedPiece*> GetPieces() const;
    UFUNCTION(BlueprintPure,Category="Placement") bool IsInitialized() const {return bInitialized;}
    UFUNCTION(BlueprintPure,Category="Placement") bool CanEditPlacement() const;
    void ConfigureDemoHand(int32 InitialGold, int32 InitialCards);
    /** Test/UI-prep hook: same bookkeeping as the round income path; never changes combat rules. */
    UFUNCTION(BlueprintCallable,Category="Placement") void AwardGold(int32 Amount);
    /** Reverses only this attempt's skill income when the existing retry flow rolls back. */
    void RollbackSkillGold(int32 Amount);
    void ConfigureRules(const FEGCardRules& InRules, UEGHeroRoster* InRoster);
    void SetPreparationEnabled(bool bEnabled);
    bool ResetCards(int32 InitialGold,int32 InitialCards,UEGCardDefinition* Definition);
    void ConfigureRoundShop(UEGRunDefinition* Definition);
    UFUNCTION(BlueprintPure,Category="Board") bool UsesRoundShopRules() const {return RoundShopDefinition!=nullptr;}
    UFUNCTION(BlueprintPure,Category="Board") TArray<UEGCardDefinition*> GetShop() const;
    UFUNCTION(BlueprintPure,Category="Board") bool IsCorruptionShopSlot(int32 Index) const {return Index==CorruptionShopSlot;}
    UFUNCTION(BlueprintCallable,Category="Board") bool BuyShopSlot(int32 Index);
    bool BuyShopSlotExpected(int32 Index,int32 Revision);
    UFUNCTION(BlueprintPure,Category="Board") int32 GetShopSlotRevision(int32 Index) const {return ShopSlotRevisions.IsValidIndex(Index)?ShopSlotRevisions[Index]:INDEX_NONE;}
    UFUNCTION(BlueprintPure,Category="Board") FEGPlacementEvaluation EvaluateShopPlacement(int32 Index,int32 Revision,FIntPoint Cell) const;
    UFUNCTION(BlueprintCallable,Category="Board") bool TryBuyShopAndDeploy(int32 Index,int32 Revision,FIntPoint Cell);
    UFUNCTION(BlueprintCallable,Category="Board") bool RefreshShop();
    /** Single source of truth for the fee the shop refresh plate displays. */
    UFUNCTION(BlueprintPure,Category="Board") int32 GetRefreshCost() const;
    UFUNCTION(BlueprintCallable,Category="Board") bool ToggleShopLock();
    UFUNCTION(BlueprintPure,Category="Board") bool IsShopLocked() const {return bShopLocked;}
    UFUNCTION(BlueprintPure,Category="Board") bool CanTrade() const;
    bool GrantCard(UEGCardDefinition* Definition,int32 Count=1);
    bool GrantCardsBatch(const TArray<UEGCardDefinition*>& Definitions,bool bNotify=true,int32 InitialLevel=1);
    /** Validate mandatory card resources before committing a supported form unlock. */
    static bool CanUnlockFormCard(const UEGHeroDefinition* Definition);
    void PromoteHeldHeroesToLevel(int32 MinimumLevel);
    void BeginRoundShopRound(int32 Stage,bool bFirst=false);
    void SellExcessHand();
    bool CanResolveExcessHand() const;
    bool BeginHandSettlement();
    void EndHandSettlement(bool bCommit);
    /** Battle setup recovery only; the coordinator verifies the active RoundId. */
    bool HasEncounterHandSnapshot() const {return bEncounterHandSnapshot;}
    bool RestoreEncounterHandSnapshot();
    bool CaptureRunCheckpoint(FEGCardRunCheckpoint& Out) const;
    bool RestoreRunCheckpoint(const FEGCardRunCheckpoint& In);
    void NotifyRunCheckpointRestored();
    static int32 PopulationForStage(int32 Stage){return Stage>=5?5:Stage>=3?4:3;}
    void SetPopulationFloor(int32 Count,bool bNotify=true);
    bool FuseDeployedHeroes(UEGCardDefinition* HumanCard,bool bNotify=true,FString* OutError=nullptr);
    bool AwardStigmataGold(int32 Amount);
    void NotifyStigmataChanged();
    UFUNCTION(BlueprintPure,Category="Placement") FEGPlacementEvaluation EvaluateHandPlacement(FGuid Id,FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") FEGPlacementEvaluation EvaluatePieceMove(AEGDeployedPiece* Piece,FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") FEGPlacementEvaluation EvaluateReservePlacement(FGuid Id,FIntPoint Cell) const;
    UFUNCTION(BlueprintPure,Category="Placement") FEGCardRules GetRules() const {return Rules;}
    UFUNCTION(BlueprintPure,Category="Placement") FText GetLastMessage() const {return LastMessage;}
    UPROPERTY(BlueprintAssignable,Category="Placement") FEGCardManagerChanged OnFeedbackChanged;
private:
    friend class FEGVillVDeploymentPolicy;
    UPROPERTY(Transient) TMap<FGuid,TWeakObjectPtr<AEGDeployedPiece>> DevicePieces;
    TSet<FGuid> PendingDeviceEntrances;
    TSet<FSoftObjectPath> LoadingDeviceEntrances;
    void PlayPendingDeviceEntrances();
    bool FindVillVCells(FIntPoint Person,FIntPoint& Gatling,FIntPoint& Cannon) const;
    bool SpawnVillVParts(AEGDeployedPiece* Main);
    void DestroyDeploymentGroup(FGuid HeroId);
    friend class FEGRoundShopProductionRun;
    friend class FEGEvolvedCardsRunTest;
    UPROPERTY(Transient) TObjectPtr<UEGRunDefinition> RoundShopDefinition;
    UPROPERTY(Transient) TArray<TObjectPtr<UEGCardDefinition>> RegisteredCards;
    UPROPERTY(Transient) TArray<TObjectPtr<UEGCardDefinition>> Shop;
    FRandomStream ShopRandom;
    int32 ShopRevision=0;
    int32 CorruptionShopSlot=INDEX_NONE;
    void GenerateShop();
    TArray<int32> ShopSlotRevisions;
    bool bShopLocked=false;
    int32 LastIncomeStage=0;
    int32 PopulationFloor=0;
    int32 FreeRefreshesUsed=0;
    bool bHandSettlement=false;
    UPROPERTY(Transient) TArray<FEGCardInstance> EncounterRecoveryHand;
    UPROPERTY(Transient) TArray<FGuid> EncounterRecoveryOrder;
    int64 EncounterRecoveryGold=0;
    bool bEncounterHandSnapshot=false;
    UEGCardDefinition* DrawShopCard();
    UEGCardDefinition* ResolveUnlockedCard(UEGCardDefinition* Card) const;
    bool SynchronizeUnlockedCards();
    bool IsOverflowProtected(FGuid Id) const;
    bool SynchronizeHandOrder();
    void NotifyHandChanged();
    UFUNCTION() void HandleRosterChanged(FGuid Id);
    UPROPERTY(Transient) TArray<FGuid> OrderedHandIds;
    int32 HandOrderRevision=0;
    UPROPERTY(Transient) TArray<FEGCardInstance> Hand;
    UPROPERTY(Transient) TArray<TObjectPtr<AEGDeployedPiece>> PendingStigmataDestruction;
    UPROPERTY(Transient) TMap<FGuid,TWeakObjectPtr<AEGDeployedPiece>> Pieces;
    UPROPERTY(Transient) int64 Gold=0;
    UPROPERTY(Transient) FEGCardRules Rules;
    UPROPERTY(Transient) TWeakObjectPtr<UEGHeroRoster> Roster;
    UPROPERTY(Transient) FText LastMessage;
    bool bPreparationEnabled=true;
    void Feedback(const FText& Message);
    bool bInitialized=false;
    bool bCommitting=false;
    bool bEnding=false;
    bool IsStigmataTransactionActive() const;
    int32 FindCard(FGuid Id) const;
    FEGPlacementEvaluation EvaluateDefinitionPlacement(const UEGCardDefinition* Definition,FIntPoint Cell) const;
    bool CommitDefinitionPlacement(UEGCardDefinition* Definition,FGuid Id,FIntPoint Cell,const FEGPlacementEvaluation& Result,int32 InitialLevel=1,int64 PaidCost=0);
    UFUNCTION() void PieceDestroyed(AActor* Actor);
};
