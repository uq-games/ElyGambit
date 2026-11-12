#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "Placement/EGBoardScene.h"
#include "Game/EGDemoGameMode.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGCombatTextLayer.h"
#include "UI/Widget/EGResultDialogueWidget.h"
#include "UI/Widget/EGEncounterNoticeWidget.h"
#include "Components/EGGameAudioComponent.h"
#include "Game/EGMatchManager.h"
#include "Presentation/EGVictoryEndingPresentation.h"
#include "Presentation/EGVictoryEndingDefinition.h"
#include "UI/Widget/EGEndgameFailureWidget.h"
#include "Kismet/GameplayStatics.h"

AEGHUD::AEGHUD()
{
 OverlayWidgetControllerClass=UEGOverlayWidgetController::StaticClass();
 UnitVitalsAsset=TSoftClassPtr<UEGUnitVitals>(FSoftObjectPath(TEXT("/Game/Blueprints/UI/Widgets/WBP_UnitVitals.WBP_UnitVitals_C")));
 VictoryEndingDefinition=TSoftObjectPtr<UEGVictoryEndingDefinition>(FSoftObjectPath(TEXT("/Game/Blueprints/Game/Endings/DA_EGVictoryEnding.DA_EGVictoryEnding")));
}
void AEGHUD::BeginPlay()
{
    Super::BeginPlay();
    // HUD and PC BeginPlay ordering differs between initial login and later spawns.
    if (auto* PC = Cast<AEGBoardPlayerController>(GetOwningPlayerController())) PC->InitializeUI(this);
}
bool AEGHUD::InitOverlay(const FEGWidgetControllerParams& Params, TSubclassOf<UEGMainHUD> WidgetClassOverride)
{
    auto* PC = GetOwningPlayerController();
    const TSubclassOf<UEGMainHUD> ViewClass = OverlayWidgetClass ? OverlayWidgetClass : WidgetClassOverride;
    if (!PC || Params.PlayerController != PC || !PC->IsLocalController() || !ViewClass ||
        ViewClass->HasAnyClassFlags(CLASS_Abstract) || !OverlayWidgetControllerClass ||
        OverlayWidgetControllerClass->HasAnyClassFlags(CLASS_Abstract)) return false;
    if(const auto* Mode=PC->GetWorld()->GetAuthGameMode<AEGDemoGameMode>())
    {
        // HUD BeginPlay can precede the GameMode's opening-token creation.
        const bool bFormalEntry=UGameplayStatics::ParseOption(Mode->OptionsString,TEXT("EGOpening"))==TEXT("1")&&
            (!Mode->GetMatch()||!Mode->GetMatch()->HasStarted());
        if(bFormalEntry||Mode->GetOpeningToken().IsValid()||Mode->IsOpeningStartupFailure()||Mode->IsStageEntranceRetryNeeded())bOpeningHUDSuppressed=true;
    }
    if (OverlayWidget)
    {
        if (auto* Input = Cast<AEGBoardPlayerController>(PC)) Input->CancelActiveDrag();
        OverlayWidget->SetWidgetController(nullptr);
        OverlayWidget->RemoveFromParent();
        OverlayWidget = nullptr;
    }
    if (!OverlayWidgetController)
        OverlayWidgetController = NewObject<UEGOverlayWidgetController>(this, OverlayWidgetControllerClass);
    OverlayWidgetController->SetWidgetControllerParams(Params);
    OverlayWidgetController->BindCallbacksToDependencies();
    OverlayWidget = CreateWidget<UEGMainHUD>(PC, ViewClass);
    if (!OverlayWidget) { OverlayWidgetController->Shutdown(); return false; }
    if(bOpeningHUDSuppressed)OverlayWidget->SetVisibility(ESlateVisibility::Collapsed);
    OverlayWidget->SetWidgetController(OverlayWidgetController);
    OverlayWidgetController->BroadcastInitialValues();
    OverlayWidget->AddToViewport(10);
    if(!CombatText)CombatText=CreateWidget<UEGCombatTextLayer>(PC);
    // Combat feedback stays behind interactive inspection and skill details.
    if(CombatText&&!CombatText->IsInViewport())CombatText->AddToViewport(5);
    if(!ResultDialogue)ResultDialogue=CreateWidget<UEGResultDialogueWidget>(PC);
    if(ResultDialogue)
    {
        if(!ResultDialogue->IsInViewport())ResultDialogue->AddToViewport(30);
        ResultDialogue->SetFinalResultVisible(OverlayWidgetController->GetMatchView().Phase==EEGMatchPhase::Finished);
        ResultDialogue->SetAudioSource(PC->FindComponentByClass<UEGGameAudioComponent>());
    }
    if(!EncounterNotice)EncounterNotice=CreateWidget<UEGEncounterNoticeWidget>(PC);
    if(EncounterNotice)
    {
        if(!EncounterNotice->IsInViewport())EncounterNotice->AddToViewport(25);
        const auto View=OverlayWidgetController->GetMatchView();
        EncounterNotice->ApplyMatchView(View.bRoundShopRun&&View.StageCount==7,View.StageNumber,View.Phase);
    }
    OverlayWidgetController->OnMatchViewChanged.AddUniqueDynamic(this,&ThisClass::CombatViewChanged);
    if(PC->GetWorld()->GetAuthGameMode<AEGDemoGameMode>())
    {
        if(!RunPanel)RunPanel=CreateWidget<UEGDemoPanel>(PC,RunPanelClass?RunPanelClass.Get():UEGDemoPanel::StaticClass());
        if(RunPanel){if(bOpeningHUDSuppressed)RunPanel->SetVisibility(ESlateVisibility::Collapsed);RunPanel->SetPresentationController(OverlayWidgetController);if(!RunPanel->IsInViewport())RunPanel->AddToViewport(20);}
    }
    CombatViewChanged(OverlayWidgetController->GetMatchView());
    return true;
}
void AEGHUD::ShutdownOverlay()
{
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(FailureCheckTimer);
    bFailureAwaiting=false;
    if(IsValid(VictoryEnding)){VictoryEnding->Destroy();VictoryEnding=nullptr;}
    if(FailureEnding)
    {
        UEGEndgameFailureWidget* Ending=FailureEnding;FailureEnding=nullptr;
        if(IsValid(Ending)&&Ending->IsInViewport())Ending->RemoveFromParent();
    }
    if(OverlayWidgetController)OverlayWidgetController->OnMatchViewChanged.RemoveDynamic(this,&ThisClass::CombatViewChanged);
    if(CombatText){CombatText->ClearNumbers();CombatText->RemoveFromParent();CombatText=nullptr;}
    if(ResultDialogue){ResultDialogue->SetAudioSource(nullptr);ResultDialogue->RemoveFromParent();ResultDialogue=nullptr;}
    if(EncounterNotice){EncounterNotice->RemoveFromParent();EncounterNotice=nullptr;}
    if (auto* Input = Cast<AEGBoardPlayerController>(GetOwningPlayerController())) Input->CancelActiveDrag();
    if(RunPanel){RunPanel->SetPresentationController(nullptr);RunPanel->RemoveFromParent();RunPanel=nullptr;}
    if (OverlayWidget)
    {
        OverlayWidget->SetWidgetController(nullptr);
        OverlayWidget->RemoveFromParent();
        OverlayWidget = nullptr;
    }
    if (OverlayWidgetController) { OverlayWidgetController->Shutdown(); OverlayWidgetController = nullptr; }
}
void AEGHUD::PresentCombatText(AEGChessUnit* Target,const FEGCombatResolution& Result)
{if(CombatText)CombatText->Present(Target,Result);}
void AEGHUD::CombatViewChanged(const FEGMatchView& View)
{
    if(CombatText&&View.Phase!=EEGMatchPhase::Combat)CombatText->ClearNumbers();
    if(ResultDialogue)ResultDialogue->SetFinalResultVisible(View.Phase==EEGMatchPhase::Finished);
    if(EncounterNotice)EncounterNotice->ApplyMatchView(View.bRoundShopRun&&View.StageCount==7,View.StageNumber,View.Phase);
    auto* PC=GetOwningPlayerController();
    auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    auto* Match=Mode?Mode->GetMatch():nullptr;
    const bool bFinished=Match&&Match->GetPhase()==EEGMatchPhase::Finished;
    if(!bFinished)
    {
        bVictoryEndingComplete=false;
        bFailureAwaiting=false;bFailureCheckComplete=false;FailureCheckAttempts=0;
        if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(FailureCheckTimer);
        if(IsValid(VictoryEnding)){VictoryEnding->Destroy();VictoryEnding=nullptr;}
        if(FailureEnding)
        {
            // Removing the widget may synchronously trigger UI callbacks. Clear
            // the owner's reference before removal so re-entry cannot remove twice.
            UEGEndgameFailureWidget* Ending=FailureEnding;FailureEnding=nullptr;
            if(IsValid(Ending)&&Ending->IsInViewport())Ending->RemoveFromParent();
        }
    }
    else if(Match->DidWinRun()&&View.bRoundShopRun&&View.StageCount==7&&View.StageNumber==7)
    {
        if(PC&&!bVictoryEndingComplete&&!IsValid(VictoryEnding))
        {
            if(auto* Audio=PC->FindComponentByClass<UEGGameAudioComponent>())Audio->SetEndingAudioOverride(true);
            VictoryEnding=GetWorld()->SpawnActor<AEGVictoryEndingPresentation>();
            if(VictoryEnding)VictoryEnding->BeginEnding(this,VictoryEndingDefinition,AEGDemoGameMode::SettlementDurationSeconds);
            else {bVictoryEndingComplete=true;UE_LOG(LogTemp,Error,TEXT("Could not spawn victory ending presentation"));}
        }
    }
    else if(!Match->DidWinRun()&&View.bRoundShopRun&&View.StageCount==7&&View.StageNumber==7&&PC&&!bFailureCheckComplete&&!bFailureAwaiting)
    {
        // Finished may be broadcast inside a guarded battle resolution, before
        // the checkpoint becomes available. Recheck on later world ticks.
        if(auto* Audio=PC->FindComponentByClass<UEGGameAudioComponent>())Audio->SetEndingAudioOverride(true);
        bFailureAwaiting=true;FailureCheckAttempts=0;
        GetWorld()->GetTimerManager().SetTimer(FailureCheckTimer,this,&ThisClass::CheckFailureEnding,.1f,true);
    }
    RefreshFinalResultGate();
}
bool AEGHUD::ShouldHideFinalResult() const
{
    const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    const auto* Match=Mode?Mode->GetMatch():nullptr;
    if(!Match||Match->GetPhase()!=EEGMatchPhase::Finished)return false;
    if(Match->DidWinRun()&&OverlayWidgetController)
    {
        const auto View=OverlayWidgetController->GetMatchView();
        if(View.bRoundShopRun&&View.StageCount==7&&View.StageNumber==7)return !bVictoryEndingComplete;
    }
    return bFailureAwaiting||(FailureEnding&&FailureEnding->IsFailureActive());
}
void AEGHUD::CheckFailureEnding()
{
    auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    auto* Match=Mode?Mode->GetMatch():nullptr;
    if(!Mode||!Match||Match->GetPhase()!=EEGMatchPhase::Finished||Match->DidWinRun())
    {if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(FailureCheckTimer);bFailureAwaiting=false;RefreshFinalResultGate();return;}
    if(Mode->CanRestoreEndgameCheckpoint())
    {
        if(auto* PC=GetOwningPlayerController())FailureEnding=CreateWidget<UEGEndgameFailureWidget>(PC);
        if(FailureEnding){FailureEnding->AddToViewport(90);FailureEnding->StartFailure();}
        bFailureCheckComplete=true;bFailureAwaiting=false;GetWorld()->GetTimerManager().ClearTimer(FailureCheckTimer);
        RefreshFinalResultGate();return;
    }
    if(++FailureCheckAttempts>=20)
    {
        UE_LOG(LogTemp,Warning,TEXT("Final defeat checkpoint unavailable; retaining the ordinary result screen"));
        bFailureCheckComplete=true;bFailureAwaiting=false;GetWorld()->GetTimerManager().ClearTimer(FailureCheckTimer);
        RefreshFinalResultGate();
    }
}
void AEGHUD::RefreshFinalResultGate()
{
    const bool bHideBoard=bOpeningHUDSuppressed||ShouldHideFinalResult();
    if(OverlayWidget)OverlayWidget->SetVisibility(bHideBoard?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
    if(RunPanel)
    {
        RunPanel->SetVisibility(bHideBoard?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        RunPanel->RefreshFinalResultGate();
    }
}
void AEGHUD::SetOpeningHUDSuppressed(bool bSuppressed)
{
    if(bOpeningHUDSuppressed==bSuppressed)return;
    bOpeningHUDSuppressed=bSuppressed;
    RefreshFinalResultGate();
}
void AEGHUD::VictoryEndingFinished(AEGVictoryEndingPresentation* Ending)
{
    if(VictoryEnding!=Ending)return;
    VictoryEnding=nullptr; bVictoryEndingComplete=true; RefreshFinalResultGate();
}
void AEGHUD::EndPlay(const EEndPlayReason::Type Reason)
{
    ShutdownOverlay();
    Super::EndPlay(Reason);
}
