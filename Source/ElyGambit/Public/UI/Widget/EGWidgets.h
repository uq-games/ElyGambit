#pragma once
#include "CoreMinimal.h"
#include "UI/Widget/EGUserWidget.h"
#include "UI/EGPearlGoldLayout.h"
#include "Components/Button.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "UI/EGCardPresentation.h"
#include "EGWidgets.generated.h"

class UBorder;
class UButton;
class UImage;
class UScrollBox;
class UTextBlock;
class UTexture2D;
class USizeBox;
class UCanvasPanel;
class AEGChessUnit;
class UEGHeroRoster;
class UEGHeroDefinition;
class UEGDragFeedback;
class UEGHeroInfoWidget;
class UEGCardDefinition;
class USlider;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGBoardAction,int32,Action,int32,Index);
UCLASS()
class ELYGAMBIT_API UEGBoardButton : public UButton
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Board") int32 Action=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Board") int32 Index=0;
 FEGBoardAction OnRequested;
 UFUNCTION() void Invoke(){OnRequested.Broadcast(Action,Index);}
 UFUNCTION() void Preview(){OnRequested.Broadcast(1000+Action,Index);}
 UFUNCTION() void Leave(){OnRequested.Broadcast(-Action,Index);}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGStagePreviewSelected,int32,StageNumber);
UCLASS()
class ELYGAMBIT_API UEGStagePreviewButton : public UButton
{
 GENERATED_BODY()
public:
 int32 StageNumber=0;
 FEGStagePreviewSelected OnSelected;
 UFUNCTION() void SelectPreview(){OnSelected.Broadcast(StageNumber);}
};

/** Card visuals are authored in WBP_HandCard; its ID refers to manager-owned data. */
UCLASS(Abstract,Blueprintable)
class ELYGAMBIT_API UEGHandCard : public UEGUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="UI") void Configure(FGuid Id,UTexture2D* Artwork);
    void SetDefinitionAppearance(UEGHeroDefinition* Definition,int32 Level=0);
    /** Presentation only: called once when a new manager-owned card joins the hand. */
    UFUNCTION(BlueprintCallable,Category="UI|Motion") void PlayAcquisition(float Delay=0.f);
    UFUNCTION(BlueprintCallable,Category="UI|Motion") void SettleMotion();
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="UI|Motion",meta=(ClampMin="0.05",ClampMax="1.0")) float AcquisitionSeconds=.32f;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="UI|Motion",meta=(ClampMin="0.05",ClampMax="0.5")) float HoverSeconds=.14f;
    UPROPERTY(Transient,BlueprintReadOnly,Category="UI") FGuid InstanceId;
    void SetIdentity(FText Text);
    void SetOverflow(bool bOverflow,bool bProtected=false);
    void SetCompactPresentation(bool bCompact,const FText& Name=FText::GetEmpty());
    /** Hide only the painted card while retaining its pointer hit geometry. */
    void SetHoverSourceHidden(bool bHidden);
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Card Appearance") FEGCardAppearance Appearance;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Appearance") bool bAuthoredAppearance=false;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> IdentityText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> OverflowText;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UImage> CardImage;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<UBorder> CardFrame;
protected:
    virtual TSharedRef<SWidget> RebuildWidget() override;
    virtual void NativePreConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Artwork;
    UPROPERTY(Transient) TWeakObjectPtr<UEGHeroDefinition> CardHeroDefinition;
    UPROPERTY(Transient) TObjectPtr<USizeBox> CompactViewport;
    UPROPERTY(Transient) TObjectPtr<UBorder> CompactNameBar;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CompactNameText;
    UPROPERTY(Transient) FText CompactName;
    bool bCompactPresentation=false;
    bool bHoverSourceHidden=false;
    float HoverSourcePreviousOpacity=1.f;
    int32 DisplayLevel=0;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
    virtual void NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& Operation) override;
    virtual void NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
    bool bInformationClick=false;
    bool bPressTracking=false;
    FVector2D PressPosition=FVector2D::ZeroVector;
    float ArrivalElapsed=-1.f;
    float MotionHover=0.f;
    bool bMotionHovered=false;
};

/** Screen-space HUD. Layout stays in Widget Blueprints; this class binds data/events. */
UCLASS(Abstract,Blueprintable)
class ELYGAMBIT_API UEGMainHUD : public UEGUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="UI") void ToggleDrawPanel();
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Board") bool bBoardPresentation=false;
    void HandleOutsideClick(FVector2D Position);
    void ShowHandInformation(FGuid Id,bool bPin=false);
    void ShowHandHover(UEGHandCard* Card);
    void HideHandHover(UEGHandCard* Card=nullptr);
    bool CanEditHand() const;
    bool BeginHandReorder(FGuid Id, int32& Revision);
    void RegisterHandReorder(UEGCardDragOperation* Operation);
    void UnregisterHandReorder(UEGCardDragOperation* Operation);
    bool CancelHandReorder();
    void UpdateHandReorder(FVector2D Position);
    void FinishHandReorder(FGuid Id,int32 Revision,FVector2D Position,bool bCommit);
    void ShowHeroInformation(UEGHeroDefinition* Definition,bool bPin=false,FName Owner=NAME_None,const UWidget* Anchor=nullptr,const FText& Extra=FText::GetEmpty());
    void ShowWorldInformation(UEGHeroDefinition* Definition,const FText& Summary,float Health,float MaxHealth,float Mana,float MaxMana);
    void SetShopDragPresentation(int32 Slot,UEGCardDefinition* Definition);
    /** Shared visual mode for shop offers and owned cards dragged while the shop is open. */
    void SetShopContextDragPresentation(UEGHeroDefinition* Definition,UTexture2D* Artwork=nullptr);
    /** Animated compact-shop bounds in the HUD design space; drives the cancel target. */
    bool GetShopReturnBoundsLocal(FVector2D& Min,FVector2D& Max) const;
    /** Screen-space test of the animating shop rectangle; never the full DrawPanel. */
    bool IsInShopArea(FVector2D Position) const;
    /** Explicit shop-offer origin, never inferred from the compact presentation. */
    bool IsShopOfferDrag() const {return bShopOfferDrag;}
    /** True only for a shop-offer drag over the animated return target. */
    bool IsInShopReturnArea(FVector2D AbsoluteScreenPosition) const;
    /** Sale labels: "出售" then the real amount, fitted inside the visible plate half. */
    void SetSellZoneSaleValue(int64 Value);
    void ResetSellZoneSaleLabels();
    void HideShopReturnHint();
    bool IsAnyDragActive() const {return bDragActive;}
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI|Information") TSubclassOf<UEGHeroInfoWidget> SkillInformationClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI|Information") TSubclassOf<UEGHeroInfoWidget> ShopDragInformationClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI|Information") TSubclassOf<UEGHeroInfoWidget> CardPreviewInformationClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI|Information") TSubclassOf<UEGHeroInfoWidget> UnitHoverInformationClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI|Information") TSubclassOf<UEGHeroInfoWidget> UnitPinnedInformationClass;
    void ShowCardInformation(UTexture2D* Art,const FText& Heading,const FText& Body,bool bPin=false,FName Owner=NAME_None);
    void HideCardInformation(bool bForce=false,FName Owner=NAME_None);
    bool ScrollShopInformation(float WheelDelta);
    bool IsShopExpanded() const { return bDrawExpanded; }
    UFUNCTION(BlueprintCallable,Category="UI") void ObtainElysia();
    UFUNCTION() void NextShopCard();
    UFUNCTION(BlueprintPure,Category="UI") UEGHandCard* FindCardWidget(FGuid Id) const;
    UFUNCTION(BlueprintPure,Category="UI") bool IsDrawExpanded() const {return bDrawExpanded;}
    UFUNCTION(BlueprintPure,Category="UI") int32 GetSellZoneAt(FVector2D AbsoluteScreenPosition) const;
    /** False until both authored sale slots have a usable parent-canvas layout. */
    bool HasSellZoneHitGeometry() const;
    /** Presentation only: the centred drag card touches a visible sale plate. */
    bool IsDragCardTouchingSellZone(FVector2D AbsoluteCenter,FVector2D AbsoluteCardSize) const;
    UFUNCTION(BlueprintPure,Category="UI") bool IsPointBlocked(FVector2D AbsoluteScreenPosition) const;
    bool IsInCardArea(FVector2D Position) const;
    bool IsAboveHandForDeployment(FVector2D Position) const;
    /** Leave the hand, with a small margin, to turn sorting into an owned-card drag. */
    bool IsOutsideHandReorderArea(FVector2D Position) const;
    FGuid ReserveTargetAt(FVector2D Position) const;
    void SetSellZoneFeedback(int32 Zone);
    void SetDragActive(bool bActive);
    void UpdateDragFeedback(UEGHeroDefinition* Definition,UTexture2D* Artwork,FVector2D Origin,FVector2D Pointer,bool bAllowed,bool bShowCardAndArrow);
    void UpdateBoardMoveFeedback(const TArray<FVector2D>& CellCorners,FVector2D Origin,FVector2D Pointer,bool bAllowed,bool bShowArrow);
    UFUNCTION(BlueprintCallable,Category="UI") void InspectUnit(AEGChessUnit* Unit);
    UFUNCTION(BlueprintCallable,Category="UI") void InspectHero(UEGHeroRoster* Roster,FGuid HeroId);
    UFUNCTION(BlueprintCallable,Category="UI") void CloseHeroDetails();
    UFUNCTION(BlueprintPure,Category="UI") bool HasHeroSelection() const {return InspectedUnit.IsValid()||InspectedHero.IsValid();}
    UFUNCTION(BlueprintCallable,Category="UI") void SelectStagePreview(int32 StageNumber);
    UFUNCTION(BlueprintPure,Category="UI") int32 GetSelectedPreviewStage() const {return SelectedPreviewStage;}
    UFUNCTION(BlueprintCallable,Category="UI") void ToggleStagePanel();
    UFUNCTION(BlueprintCallable,Category="UI") void RequestBattle();
    UFUNCTION(BlueprintCallable,Category="UI") bool DismissPanels();
    bool CloseContextPanels();
    UFUNCTION(BlueprintPure,Category="UI") bool IsStageExpanded() const {return bStageExpanded;}
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI") TSubclassOf<UEGHandCard> CardWidgetClass;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI") TObjectPtr<UTexture2D> CardArtwork;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI") TMap<FName,TSubclassOf<UEGHandCard>> CharacterCardClasses;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI") TMap<FName,TSubclassOf<class UEGShopCard>> ShopCardClasses;
    /** Read-only catalogue entries. These never become shop offers or roster heroes. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI") TArray<TObjectPtr<UEGHeroDefinition>> InspectableCompanions;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UTextBlock> Text_Round;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UTextBlock> Text_HandCount;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UScrollBox> HandScrollBox;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UBorder> HandPanel;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UUserWidget> DrawPanel;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UButton> Button_ToggleDrawPanel;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UTextBlock> Text_DrawToggle;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UUserWidget> SellZone_Left;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidget)) TObjectPtr<UUserWidget> SellZone_Right;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<UButton> Button_Stage;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<UButton> Button_Battle;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<UBorder> StagePanel;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<UBorder> ModalBlocker;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<USizeBox> HandViewport;
    UPROPERTY(BlueprintReadOnly,Category="UI",meta=(BindWidgetOptional)) TObjectPtr<UScrollBox> ReserveScrollBox;
protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    virtual void BindWidgetControllerCallbacks() override;
    virtual void UnbindWidgetControllerCallbacks() override;
    virtual bool NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation) override;
    virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
    virtual FReply NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
private:
    friend class FEGUIStageNodes;
    void SetupAudioControls();
    void SetupRightSideControls();
    UFUNCTION() void ToggleAudioControls();
    UFUNCTION() void MusicVolumeChanged(float Value);
    UFUNCTION() void UISoundVolumeChanged(float Value);
    UPROPERTY(Transient) TObjectPtr<UButton> AudioButton;
    UPROPERTY(Transient) TObjectPtr<UBorder> AudioPanel;
    UPROPERTY(Transient) TObjectPtr<USlider> MusicVolumeSlider;
    UPROPERTY(Transient) TObjectPtr<USlider> UISoundVolumeSlider;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> MusicVolumeText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> UISoundVolumeText;
    UFUNCTION() void RefreshBattleStatistics();
    UFUNCTION() void MarkBattleStatisticsDirty();
    UPROPERTY(Transient) TWeakObjectPtr<class AEGDemoGameMode> BoardRun;
    UPROPERTY(Transient) TObjectPtr<UEGDragFeedback> DragFeedback;
    UPROPERTY(Transient) TObjectPtr<UEGHeroInfoWidget> SkillInformation;
    UPROPERTY(Transient) TObjectPtr<UEGHeroInfoWidget> CardPreviewInformation;
    UPROPERTY(Transient) TObjectPtr<UEGHeroInfoWidget> UnitHoverInformation;
    UPROPERTY(Transient) TObjectPtr<UEGHeroInfoWidget> UnitPinnedInformation;
    UPROPERTY(Transient) TObjectPtr<UEGHeroInfoWidget> ShopDragInformation;
    int32 ShopDragSlot=INDEX_NONE;
    bool bShopContextDragPresentationActive=false;
    float ShopDragBlend=0.f;
    int32 StatisticsMetric=0;
    bool bBattleStatisticsDirty=false;
    bool bStatisticsSidebarOpen=false;
    bool bUnitSidebarOpen=false;
    float StatisticsSidebarReveal=0.f;
    float UnitSidebarReveal=0.f;
    FVector2D InformationLayoutViewport=FVector2D(-1.f,-1.f);
    bool bInformationPinned=false;
    FName InformationOwner=NAME_None;
    void SetupBoardView();
    void RefreshBoardView();
    UFUNCTION() void OnBoardAction(int32 Action,int32 Index);
    void ClearInspectionBindings();
    void RefreshInspection();
    void LayoutInformationViews();
    void UpdateSidebarMotion(float DeltaSeconds);
    UFUNCTION() void CloseInformationViews();
    UFUNCTION() void SellInspectedInformation();
    void RebindInspectionSource();
    void BindInspectedUnit(AEGChessUnit* Unit);
    void RefreshStagePreviews();
    UFUNCTION() void InspectedActorDestroyed(AActor* Actor);
    UFUNCTION() void InspectedAttributeChanged(FName Attribute,float OldValue,float NewValue);
    UFUNCTION() void InspectedStatusChanged();
    UFUNCTION() void InspectedHeroChanged(FGuid HeroId);
    UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> InspectedUnit;
    UPROPERTY(Transient) TWeakObjectPtr<UEGHeroRoster> InspectedRoster;
    FGuid InspectedHero;
    UPROPERTY(Transient) TArray<TObjectPtr<UEGStagePreviewButton>> PreviewButtons;
    UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> SimpleStageHosts;
    UPROPERTY(Transient) TArray<TObjectPtr<UCanvasPanel>> SimpleStageColumns;
    UPROPERTY(Transient) TArray<FEGStagePreviewView> StagePreviews;
    FString StagePreviewSignature;
    int32 SelectedPreviewStage=0;
    int32 LastMatchStage=0;
    UPROPERTY(Transient) TMap<FGuid,TObjectPtr<UEGHandCard>> Cards;
    UPROPERTY(Transient) TMap<FGuid,FEGHandCardView> AppliedCards;
    UPROPERTY(Transient) TObjectPtr<UEGHandCard> HoverCard;
    UPROPERTY(Transient) TObjectPtr<UBorder> HandInsertionMarker;
    TWeakObjectPtr<UEGHandCard> HoverSource;
    UPROPERTY(Transient) TWeakObjectPtr<UEGCardDragOperation> ActiveHandReorder;
    bool bHoverCardDirty=false;
    bool bHasReorderPointer=false;
    FVector2D ReorderPointer=FVector2D::ZeroVector;
    FGuid ReorderBefore;
    bool bReorderTargetValid=false;
    float HandCombatBlend=0.f;
    float StageReveal=0.f;
    float StageLayoutWidth=-1.f;
    int32 LaidOutStageCount=INDEX_NONE;
    TSet<FGuid> ReserveIds;
    bool bDrawExpanded=false;
    bool bStageExpanded=false;
    bool bDragActive=false;
    /** Shop-offer origin of the active drag; sale plates stay collapsed for it. */
    bool bShopOfferDrag=false;
    /** Shop-open state captured when a drag begins so cancellation restores it. */
    bool bShopWasExpandedOnDragStart=false;
    /** Animated compact-shop rectangle recomputed every transition tick. */
    bool bShopGroupRectValid=false;
    FVector2D ShopGroupMinLocal=FVector2D::ZeroVector;
    FVector2D ShopGroupMaxLocal=FVector2D::ZeroVector;
    /** Last measured viewport width; a change forces a static-transition layout update. */
    float ShopLayoutWidth=-1.f;
    /** Shop content interactivity captured for the plate tint ramp. */
    bool bShopPlateInteractive=false;
    bool bShopLockPressed=false;
    bool bShopRefreshPressed=false;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ShopReturnHint;
    /** Last sale value pushed into both plates; avoids per-frame font measuring. */
    int64 CachedSellValue=MIN_int64;
    bool bSellValueApplied=false;
    float InspectionRefreshElapsed=0.f;
    float HandLayoutViewportWidth=0.f;
    float ShopReveal=0.f;
    float SellReveal=0.f;
    float SellHover[2]={0.f,0.f};
    int32 SellFeedbackZone=0;
    int64 PresentedEnergy=MIN_int64;
    float EnergyPulse=0.f;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> CorruptionDeltaText;
    float CorruptionDeltaRemaining=0.f;
    UFUNCTION() void ShowCorruptionDelta(int32 Current, int32 Delta);
    void UpdateFeedbackMotion(float DeltaSeconds);
    void UpdateSellZoneSaleLabels();
    void UpdateResourceLoading();
    float ResourceLoadingElapsed=0.f;
    UPROPERTY(Transient) TObjectPtr<UBorder> ResourceLoadingPanel;
    UPROPERTY(Transient) TObjectPtr<UImage> ResourceLoadingScrim;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ResourceLoadingTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> ResourceLoadingDetail;
    void UpdateShopTransition(float DeltaSeconds);
    void UpdateStageTransition(float DeltaSeconds);
    void UpdateShopReturnHint(float CompactBlend,float ViewWidth);
    void CreateShopReturnHint();
    void SetupShopReturnHintSlot();
    void BindShopPlateHover();
    void UnbindShopPlateHover();
    void SetShopPlateTint(const TCHAR* Prefix,FLinearColor Tint);
    void RefreshShopPlateTint();
    void SetShopLockGlyphState(bool bLocked);
    float ShopOpenTranslationY() const;
    UFUNCTION() void ShopLockHovered();
    UFUNCTION() void ShopLockUnhovered();
    UFUNCTION() void ShopRefreshHovered();
    UFUNCTION() void ShopRefreshUnhovered();
    UFUNCTION() void ShopLockPressed();
    UFUNCTION() void ShopLockReleased();
    UFUNCTION() void ShopRefreshPressed();
    UFUNCTION() void ShopRefreshReleased();
    void RefreshHandLayout();
    void RefreshPanelVisibility();
    UFUNCTION() void ClosePanels();
    UFUNCTION() void ShopHovered();
    UFUNCTION() void ShopUnhovered();
    UFUNCTION() void ApplyHandView(const TArray<FEGHandCardView>& Hand);
    UFUNCTION() void ApplyMatchView(const FEGMatchView& Match);
    UFUNCTION() void ApplyPlacementHint(const FText& Hint);
    static bool IsOver(const UWidget* Widget,FVector2D Position);
    bool IsOverUnitInspectionContent(FVector2D Position) const;
};
