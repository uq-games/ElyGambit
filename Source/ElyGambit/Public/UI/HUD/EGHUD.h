#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "TimerManager.h"
#include "UI/WidgetController/EGWidgetController.h"
#include "EGHUD.generated.h"

class UEGMainHUD;
class UEGOverlayWidgetController;
class UEGDemoPanel;
class UEGCombatTextLayer;
class UEGResultDialogueWidget;
class UEGEncounterNoticeWidget;
class UEGUnitVitals;
class AEGChessUnit;
class AEGVictoryEndingPresentation;
class UEGVictoryEndingDefinition;
class UEGEndgameFailureWidget;
struct FEGCombatResolution;
struct FEGMatchView;

/** Composition root: owns the overlay and its single presentation controller. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGHUD : public AHUD
{
    GENERATED_BODY()
public:
    AEGHUD();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="UI") bool InitOverlay(const FEGWidgetControllerParams& Params, TSubclassOf<UEGMainHUD> WidgetClassOverride);
    UFUNCTION(BlueprintCallable, Category="UI") void ShutdownOverlay();
    UFUNCTION(BlueprintPure, Category="UI") UEGMainHUD* GetMainWidget() const { return OverlayWidget; }
    UFUNCTION(BlueprintPure, Category="UI") UEGOverlayWidgetController* GetOverlayWidgetController() const { return OverlayWidgetController; }
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI") TSubclassOf<UEGMainHUD> OverlayWidgetClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI") TSubclassOf<UEGOverlayWidgetController> OverlayWidgetControllerClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI") TSubclassOf<UEGDemoPanel> RunPanelClass;
    /** Authored layout; the widget directory is included by project packaging settings. */
    UPROPERTY(EditDefaultsOnly,Category="UI") TSoftClassPtr<UEGUnitVitals> UnitVitalsAsset;
    UFUNCTION(BlueprintPure,Category="UI") UEGDemoPanel* GetRunPanel() const {return RunPanel;}
    void PresentCombatText(AEGChessUnit* Target,const FEGCombatResolution& Result);
    UFUNCTION(BlueprintPure,Category="UI") UEGCombatTextLayer* GetCombatTextLayer() const {return CombatText;}
    bool ShouldHideFinalResult() const;
    void SetOpeningHUDSuppressed(bool bSuppressed);
    void RefreshFinalResultGate();
    void VictoryEndingFinished(AEGVictoryEndingPresentation* Ending);
    UPROPERTY(EditDefaultsOnly,Category="UI|Ending") TSoftObjectPtr<UEGVictoryEndingDefinition> VictoryEndingDefinition;
private:
    UFUNCTION() void CombatViewChanged(const FEGMatchView& View);
    void CheckFailureEnding();
    UPROPERTY(Transient) TObjectPtr<UEGCombatTextLayer> CombatText;
    UPROPERTY(Transient) TObjectPtr<UEGResultDialogueWidget> ResultDialogue;
    UPROPERTY(Transient) TObjectPtr<UEGEncounterNoticeWidget> EncounterNotice;
    UPROPERTY(Transient) TObjectPtr<UEGDemoPanel> RunPanel;
    UPROPERTY(Transient) TObjectPtr<UEGMainHUD> OverlayWidget;
    UPROPERTY(Transient) TObjectPtr<UEGOverlayWidgetController> OverlayWidgetController;
    UPROPERTY(Transient) TObjectPtr<AEGVictoryEndingPresentation> VictoryEnding;
    UPROPERTY(Transient) TObjectPtr<UEGEndgameFailureWidget> FailureEnding;
    bool bVictoryEndingComplete=false;
    bool bOpeningHUDSuppressed=false;
    bool bFailureAwaiting=false;
    bool bFailureCheckComplete=false;
    int32 FailureCheckAttempts=0;
    FTimerHandle FailureCheckTimer;
};
