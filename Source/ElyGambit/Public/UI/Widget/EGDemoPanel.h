#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "InputCoreTypes.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "UI/Widget/EGVitalsGraphics.h"
#include "EGDemoPanel.generated.h"

class UTextBlock;
class UEGRichTextBlock;
class UButton;
class UBorder;
class UProgressBar;
class AEGChessUnit;
class UEGHeroDefinition;
class UEGHeroRoster;
class UWidgetTree;
class UImage;
class UHorizontalBox;
class UVerticalBox;
class UEGBattleContextComponent;
class AEGDemoGameMode;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGStigmaSkipRequested,int32,Revision);

UCLASS()
class ELYGAMBIT_API UEGDemoPanel : public UUserWidget
{
 GENERATED_BODY()
public:
 static void BuildBoardLayout(UWidgetTree* Tree);
 virtual void NativeOnInitialized() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual void NativeTick(const FGeometry& MyGeometry,float InDeltaTime) override;
 virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry,const FKeyEvent& InKeyEvent) override;
 virtual FReply NativeOnKeyUp(const FGeometry& InGeometry,const FKeyEvent& InKeyEvent) override;
 virtual void NativeOnFocusLost(const FFocusEvent& InFocusEvent) override;
 void SetPresentationController(UEGOverlayWidgetController* Controller);
 void SetContextPanelsOpen(bool bOpen);
 /** Existing reward/collection interactions receive Escape before the pause menu. */
 bool HasStigmataEscapePriority() const;
 void RefreshFinalResultGate();
 void SetStigmataSidebarVisible(bool bVisible);
 void PresentStigmaSkip(int32 Revision);
 void HideStigmaSkip();
 void ForceHideStigmaSkip();
 FEGStigmaSkipRequested OnStigmaSkipRequested;
 UFUNCTION(BlueprintPure,Category="Demo") UButton* GetActionButton() const {return Action;}
 UFUNCTION(BlueprintPure,Category="Demo") bool BlocksPoint(FVector2D Position) const;
private:
 void ApplyBoardView(const FEGMatchView& View);
 void EnsureRetryButton();
 void EnsureStigmaPanel();
 void RefreshStigmataOffer();
 void RefreshStigmataCollection();
 void SelectStigmata(int32 Index);
 void OpenStigmataChoiceDetails(int32 Index);
 void ResetStigmataChoiceDetails();
 UFUNCTION() void CloseStigmataChoiceDetails();
 void BeginStigmataHold(int32 Index, bool bKeyboard);
 void CancelStigmataHold(bool bPlaySound);
 void CompleteStigmataHold();
 void FinishStigmataCelebration();
 void RefreshStigmataResults();
 void UpdateStigmataFeedback(float DeltaSeconds);
 UFUNCTION() void PressStigmata0();
 UFUNCTION() void PressStigmata1();
 UFUNCTION() void PressStigmata2();
 UFUNCTION() void ReleaseStigmata();
 UFUNCTION() void UnhoverStigmata();
 UFUNCTION() void HoverStigmata0();
 UFUNCTION() void HoverStigmata1();
 UFUNCTION() void HoverStigmata2();
 void BindStigmataFeedback();
 void EnsurePhaseClock();
 void BindPhaseClock();
 UFUNCTION() void PhaseClockChanged();
 void PreviewStigmata(int32 Index);
 void OpenStigmataDetails(int32 Index);
 UFUNCTION() void PreviewStigmata0();
 UFUNCTION() void PreviewStigmata1();
 UFUNCTION() void PreviewStigmata2();
 UFUNCTION() void HideStigmataPreview();
 UFUNCTION() void OpenStigmataDetails0();
 UFUNCTION() void OpenStigmataDetails1();
 UFUNCTION() void OpenStigmataDetails2();
 UFUNCTION() void SelectStigmata0();
 UFUNCTION() void SelectStigmata1();
 UFUNCTION() void SelectStigmata2();
 UFUNCTION() void ConfirmStigmata();
 UFUNCTION() void ToggleStigmataCollection();
 UFUNCTION() void CloseStigmataCollection();
 UFUNCTION() void StigmataTriggered(FName Id, AEGChessUnit* Source);
 void ClearStigmataFeedback();
 UFUNCTION() void SkipStigma();
 UPROPERTY(Transient) TObjectPtr<UBorder> StigmaPanel;
 UPROPERTY(Transient) TObjectPtr<UButton> StigmaSkipButton;
 UPROPERTY(Transient) TArray<TObjectPtr<UButton>> StigmataChoices;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StigmataChoiceTitles;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StigmataChoiceDescriptions;
 /** Card summaries (numbers emphasised at display time); index-aligned with StigmataChoices. */
 UPROPERTY(Transient) TArray<TObjectPtr<UEGRichTextBlock>> StigmataChoiceSummaries;
 UPROPERTY(Transient) TArray<TObjectPtr<UImage>> StigmataChoiceImages;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> StigmataHeading;
 UPROPERTY(Transient) TObjectPtr<UButton> StigmataConfirm;
 UPROPERTY(Transient) TObjectPtr<UBorder> StigmataDetailsPanel;
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> StigmataDetailsText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> StigmataInstruction;
 UPROPERTY(Transient) TObjectPtr<UButton> StigmataCollectionButton;
 UPROPERTY(Transient) TArray<TObjectPtr<UButton>> StigmataRows;
 UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> StigmataRowNames;
 UPROPERTY(Transient) TObjectPtr<UBorder> StigmataPreviewPanel;
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> StigmataPreviewText;
 UPROPERTY(Transient) TObjectPtr<UImage> PhaseClockLeft;
 UPROPERTY(Transient) TObjectPtr<UImage> PhaseClockRight;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PhaseClockNumber;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PhaseClockStage;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PhaseClockCorruption;
 UPROPERTY(Transient) TWeakObjectPtr<AEGDemoGameMode> PhaseClockSource;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> StigmataCollectionLabel;
 UPROPERTY(Transient) TObjectPtr<UHorizontalBox> StigmataIconBar;
 UPROPERTY(Transient) TMap<FName,TObjectPtr<UImage>> StigmataIcons;
 UPROPERTY(Transient) TObjectPtr<UBorder> StigmataCollectionPanel;
 UPROPERTY(Transient) TObjectPtr<UVerticalBox> StigmataCollectionList;
 UPROPERTY(Transient) TObjectPtr<UBorder> StigmataResultsPanel;
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> StigmataResultsText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> StigmataFeedbackText;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> StigmataFeedbackSource;
 TArray<FName> StigmataOfferedIds;
 TArray<FName> StigmataOwnedIds;
 FName StigmataDetailId;
 int32 PreviewedStigmata=INDEX_NONE;
 int32 SelectedStigmata=INDEX_NONE;
 int32 DetailedStigmata=INDEX_NONE;
 // Each icon owns its expiry; a frequent trigger cannot keep another lit.
 TMap<FName,float> StigmataHighlightRemaining;
 float StigmataFeedbackRemaining=0.f;
 float StigmataCaptionCooldown=0.f;
 // Results belong to the preceding battle. Keep them during settlement,
 // then fade once when the next preparation begins.
 float StigmataResultsFadeElapsed=0.f;
 bool bStigmataResultsFromSettlement=false;
 int32 StigmaRevision=INDEX_NONE;
 int32 HeldStigmata=INDEX_NONE;
 int32 HeldRevision=INDEX_NONE;
 FName HeldStigmataId;
 FKey StigmataHeldKey;
 float StigmataHoldElapsed=0.f;
 double StigmataHoldStartSeconds=0.0;
 uint64 StigmataHoldGeneration=0;
 bool bStigmataHoldSoundStarted=false;
 float StigmataSuccessElapsed=0.f;
 bool bSuppressStigmataClick=false;
 bool bStigmataPanelConstructed=false;
 bool bStigmataKeyboardHold=false;
 bool bStigmataCommitting=false;
 bool bStigmataDeferredHide=false;
 bool bStigmataCelebrating=false;
 bool bStigmataOfferIsRoute=false;
 bool bHeldStigmataIsRoute=false;
 bool bPendingStigmataRefresh=false;
 bool bStigmataSidebarVisible=true;
 bool bBoardLayout=false;
 UPROPERTY(Transient) TObjectPtr<UBorder> FinalPanel;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Help;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> ActionText;
 UPROPERTY(Transient) TObjectPtr<UButton> Action;
 UPROPERTY(Transient) TObjectPtr<UButton> RetryAction;
 UFUNCTION() void OnAction();
 UFUNCTION() void OnRetry();
 UPROPERTY(Transient) TWeakObjectPtr<UEGOverlayWidgetController> Presentation;
 UFUNCTION() void ApplyMatchView(const FEGMatchView& View);
 UFUNCTION() void ApplyClockHint(const FText& Hint);
 bool bContextPanelsOpen=false;
};

UENUM(BlueprintType)
enum class EEGVitalsMode : uint8 { Unbound, Preparation, Combat };

UCLASS()
class ELYGAMBIT_API UEGUnitVitals : public UUserWidget
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Vitals") void SetUnit(AEGChessUnit* InUnit);
 UFUNCTION(BlueprintCallable,Category="Vitals") void SetPreparationHero(UEGHeroRoster* Roster,FGuid HeroId);
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Vitals") FEGUnitVitalsStyle VitalsStyle;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Vitals") EEGVitalsMode DisplayMode=EEGVitalsMode::Unbound;
 UFUNCTION(BlueprintPure,Category="Vitals") UEGAwakeningRing* GetAwakeningRing() const {return RingGraphic;}
 UFUNCTION(BlueprintPure,Category="Vitals") UProgressBar* GetHealthBar() const {return Health;}
 UFUNCTION(BlueprintPure,Category="Vitals") UProgressBar* GetManaBar() const {return Mana;}
 void SynchronizePresentationState();
 static void BuildLayout(UWidgetTree* Tree);
 static void ApplyLayoutMetrics(UWidgetTree* Tree,const FEGUnitVitalsStyle& Style=FEGUnitVitalsStyle(),bool bHasMana=true);
 static UClass* ResolveWidgetClass();
 virtual void NativeOnInitialized() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual void NativeTick(const FGeometry& MyGeometry,float InDeltaTime) override;
private:
 UFUNCTION() void AttributeChanged(FName Attribute,float OldValue,float NewValue);
 UFUNCTION() void StateChanged(EEGUnitState State);
 UFUNCTION() void StatusChanged();
 UFUNCTION() void HealthSourceAttributeChanged(FName Attribute,float OldValue,float NewValue);
 UFUNCTION() void HealthSourceStateChanged(EEGUnitState State);
 UFUNCTION() void HealthSourceStatusChanged();
 UFUNCTION() void HealthSourceDestroyed(AActor* DestroyedActor);
 UFUNCTION() void RosterChanged(FGuid HeroId);
 void SetHealthSource(AEGChessUnit* NewSource);
 void SynchronizeVillVVitals();
 void Refresh();
 void RefreshHealth();
 void RefreshMana();
 void RefreshIdentity();
 void RefreshPassive();
 void ClearPassive();
 void RefreshStatusCaption();
 void RefreshSkillTooltip(const UEGHeroDefinition* Definition,const FText& Extra=FText::GetEmpty());
 UPROPERTY(Transient) TObjectPtr<class UEGHeroInfoWidget> SkillTooltipView;
 void ApplyMode(EEGVitalsMode Mode);
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroDefinition> TooltipDefinition;
 int32 TooltipLevel=INDEX_NONE;
 bool bTooltipAwakened=false;
 bool bLastHadMana=true;
 FTimerHandle StatusTimer;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Unit;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> HealthSource;
 bool bOccupiedDeviceVitalsHidden=false;
 bool bPresentationConstructed=false;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroRoster> PreparationRoster;
 FGuid PreparationId;
 UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> PreparationContainer;
 UPROPERTY(Transient) TObjectPtr<class UCanvasPanel> CombatContainer;
 UPROPERTY(Transient) TObjectPtr<UEGAwakeningRing> RingGraphic;
 // Native member names must not equal generated UMG widget names (LevelMedal /
 // PreparationMedal) or the widget blueprint compiler collides on the variable.
 UPROPERTY(Transient) TObjectPtr<UEGLevelMedal> CombatMedal;
 UPROPERTY(Transient) TObjectPtr<UEGLevelMedal> PrepMedal;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PreparationLabel;
 UPROPERTY(Transient) TObjectPtr<UProgressBar> Health;
 UPROPERTY(Transient) TObjectPtr<UProgressBar> Mana;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> PassiveText;
 UPROPERTY(Transient) TObjectPtr<UProgressBar> PassiveBar;
 TWeakObjectPtr<UObject> PassiveBehavior;
 FGuid PassiveRound;
 int32 PreviousPassivePhase=0;
 bool bPreviousPassiveReady=false;
 bool bHavePassiveSnapshot=false;
 float PassiveRefreshElapsed=0.f;
 float PassiveCaptionRemaining=0.f;
 FString PassiveCaption;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Label;
};
