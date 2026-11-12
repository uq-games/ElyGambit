#pragma once

#include "CoreMinimal.h"
#include "UI/WidgetController/EGWidgetController.h"
#include "Types/EGChessTypes.h"
#include "UI/EGUnitInspectionView.h"
#include "EGOverlayWidgetController.generated.h"

class UEGCardDragOperation;
class UTexture2D;
class UEGCardDefinition;
class AEGDemoGameMode;
class UEGHeroDefinition;
class UEGHeroRoster;
class AEGChessUnit;

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGStagePreviewView
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly,Category="UI") int32 StageNumber=0;
 UPROPERTY(BlueprintReadOnly,Category="UI") FName StageId;
 UPROPERTY(BlueprintReadOnly,Category="UI") FText Title;
 UPROPERTY(BlueprintReadOnly,Category="UI") FText Status;
 UPROPERTY(BlueprintReadOnly,Category="UI") FText Details;
 UPROPERTY(BlueprintReadOnly,Category="UI") EEGStageType StageType=EEGStageType::Normal;
 UPROPERTY(BlueprintReadOnly,Category="UI") FText TypeLabel;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bCorruptedCopyEncounter=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bCorruptionBossEncounter=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") int32 PopulationLimit=0;
 UPROPERTY(BlueprintReadOnly,Category="UI") int32 CompletionGold=0;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bHasCompletionGold=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bStigmataChoice=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bHasCorruptionGain=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bPeriodicCorruption=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") int32 CorruptionGain=0;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bCurrent=false;
 UPROPERTY(BlueprintReadOnly,Category="UI") bool bPast=false;
};

/** Value-only view data; the gameplay manager retains the authoritative hand. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGHandCardView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="UI") FGuid InstanceId;
    UPROPERTY(BlueprintReadOnly, Category="UI") FName CardId;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bReserve=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bOverflow=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bOverflowProtected=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") int32 Level=1;
    UPROPERTY(BlueprintReadOnly,Category="UI") FText Identity;
    UPROPERTY(BlueprintReadOnly, Category="UI") FText DisplayName;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UTexture2D> Artwork;
    UPROPERTY(BlueprintReadOnly, Category="UI") TObjectPtr<UEGHeroDefinition> HeroDefinition;
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGMatchView
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="UI") bool bHasMatch = false;
    UPROPERTY(BlueprintReadOnly, Category="UI") int32 StageNumber = 1;
    UPROPERTY(BlueprintReadOnly, Category="UI") int32 StageCount = 2;
    UPROPERTY(BlueprintReadOnly, Category="UI") EEGMatchPhase Phase = EEGMatchPhase::Preparation;
    UPROPERTY(BlueprintReadOnly, Category="UI") int32 RoundNumber = 1;
    UPROPERTY(BlueprintReadOnly, Category="UI") int64 Gold = 0;
    UPROPERTY(BlueprintReadOnly, Category="UI") bool bCanRequestCombat = false;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bIsRun=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") int32 PurchasePrice=0;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bCanPurchase=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bCanAdvance=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bCanRetryStage=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") FText RunStatus;
    UPROPERTY(BlueprintReadOnly,Category="UI") FText RunHint;
    UPROPERTY(BlueprintReadOnly,Category="UI") FText RunAction;
    UPROPERTY(BlueprintReadOnly,Category="UI") FText RunName;
    UPROPERTY(BlueprintReadOnly,Category="UI") FText HeroSummary;
    UPROPERTY(BlueprintReadOnly,Category="UI") int32 SameNameLevelGain=5;
    UPROPERTY(BlueprintReadOnly,Category="UI") TArray<EEGStageType> StageTypes;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bRoundShopRun=false;
    UPROPERTY(BlueprintReadOnly,Category="UI") bool bLastVictory=false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGHandViewChanged, const TArray<FEGHandCardView>&, Hand);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGMatchViewChanged, const FEGMatchView&, Match);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGPlacementHintChanged, const FText&, Hint);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGBattleClockViewChanged, const FText&, Hint);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGCorruptionViewChanged, int32, Current, int32, Delta);

UCLASS(Blueprintable)
class ELYGAMBIT_API UEGOverlayWidgetController : public UEGWidgetController
{
    GENERATED_BODY()
public:
    virtual void BindCallbacksToDependencies() override;
    virtual void UnbindCallbacksFromDependencies() override;
    virtual void BroadcastInitialValues() override;
    virtual void Shutdown() override;
    UPROPERTY(BlueprintAssignable, Category="UI") FEGHandViewChanged OnHandViewChanged;
    UPROPERTY(BlueprintAssignable, Category="UI") FEGMatchViewChanged OnMatchViewChanged;
    UPROPERTY(BlueprintAssignable, Category="UI") FEGPlacementHintChanged OnPlacementHintChanged;
    UPROPERTY(BlueprintAssignable, Category="UI") FEGBattleClockViewChanged OnBattleClockViewChanged;
    UPROPERTY(BlueprintAssignable, Category="UI") FEGCorruptionViewChanged OnCorruptionViewChanged;
    UPROPERTY(Transient,BlueprintReadOnly,Category="UI|Diagnostics") int32 HandBroadcastCount=0;
    UPROPERTY(Transient,BlueprintReadOnly,Category="UI|Diagnostics") int32 ClockBroadcastCount=0;
    UFUNCTION(BlueprintCallable,Category="UI") bool RequestNextShopCard();
    UFUNCTION(BlueprintPure, Category="UI") TArray<FEGHandCardView> GetHandView() const;
    UFUNCTION(BlueprintPure, Category="UI") FEGMatchView GetMatchView() const;
    UFUNCTION(BlueprintPure,Category="UI") TArray<FEGStagePreviewView> GetStagePreviews() const;
    UTexture2D* GetHeroArtwork(const UEGHeroDefinition* Definition) const;
    FEGUnitInspectionView GetInspectionView(const AEGChessUnit* Unit,const UEGHeroRoster* Roster,FGuid HeroId) const;
    AEGChessUnit* FindInspectedCombatUnit(FGuid HeroId) const;
    bool CanSellInspectedHero(FGuid HeroId) const;
    bool RequestSellInspectedHero(FGuid HeroId);
    bool CanWithdrawInspectedHero(FGuid HeroId) const;
    bool RequestWithdrawInspectedHero(FGuid HeroId);
    UFUNCTION(BlueprintCallable, Category="UI") bool RequestObtainElysia();
    UFUNCTION(BlueprintPure, Category="UI") UEGCardDefinition* GetAvailableCardDefinition() const;
    UFUNCTION(BlueprintCallable, Category="UI") bool RequestObtainCard(UEGCardDefinition* Definition);
    UFUNCTION(BlueprintCallable, Category="UI") bool RequestCombat();
    UFUNCTION(BlueprintCallable,Category="UI") bool RequestAdvanceRun();
    UFUNCTION(BlueprintCallable,Category="UI") bool RequestRetryStage();
    void PresentRuleFeedback(const FText& Message);
    /** Input presentation only; no gameplay state is stored here. */
    void PresentPlacementFeedback(uint8 DragState);
    UFUNCTION(BlueprintCallable, Category="UI") void CancelActiveDrag();
    bool BeginHandCardDrag(FGuid Id,UEGCardDragOperation* ExpectedOperation=nullptr);
    bool AttachHandCardDrag(UEGCardDragOperation* Operation, FVector2D ScreenPosition);
    int32 GetHandOrderRevision() const;
    bool ReorderHandCard(FGuid Id, FGuid BeforeId, int32 Revision);
    int64 GetOverflowSaleValue() const;
private:
    UPROPERTY(Transient) TArray<FEGHandCardView> LastHandView;
    UPROPERTY(Transient) FText LastPlacementHint;
    bool bHasHandView=false;
    void PublishHand(bool bForce=false);
    UFUNCTION() void EconomyChanged();
    UFUNCTION() void ClockChanged();
    UPROPERTY(Transient) TWeakObjectPtr<AEGDemoGameMode> RunSource;
    UFUNCTION() void RunChanged();
    UFUNCTION() void RosterChanged(FGuid Id);
    UFUNCTION() void HandChanged();
    UFUNCTION() void HandSourceDestroyed(AActor* Actor);
    UFUNCTION() void PhaseChanged(EEGMatchPhase Phase);
    UFUNCTION() void CorruptionChanged(int32 Current, int32 Delta);
    UFUNCTION() void StagePrepared(int32 StageNumber, EEGStageType StageType);
};
