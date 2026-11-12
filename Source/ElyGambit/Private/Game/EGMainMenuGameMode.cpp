#include "Game/EGMainMenuGameMode.h"

#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MovieSceneSequencePlayer.h"
#include "TimerManager.h"
#include "UI/Widget/EGMainMenuWidget.h"
#include "Game/EGOpeningGameInstance.h"

namespace
{
constexpr float MenuOpeningFadeSeconds = .30f;
constexpr float OpeningExitFadeSeconds = .25f;
constexpr float OpeningWatchdogSeconds = .20f;
constexpr float TravelTimeoutSeconds = 5.f;

ALevelSequenceActor* FindUniqueSequenceActor(UWorld* World, FName Tag)
{
    if (!World || Tag.IsNone()) return nullptr;
    ALevelSequenceActor* Found = nullptr;
    for (TActorIterator<ALevelSequenceActor> It(World); It; ++It)
    {
        if (!It->ActorHasTag(Tag)) continue;
        if (Found)
        {
            UE_LOG(LogTemp, Warning, TEXT("Main menu has multiple sequence actors tagged '%s'."), *Tag.ToString());
            return nullptr;
        }
        Found = *It;
    }
    return Found;
}

bool HasUsableSequence(ALevelSequenceActor* Actor, ULevelSequencePlayer* SequencePlayer)
{
    if (!IsValid(Actor) || !IsValid(SequencePlayer) || !IsValid(Actor->GetSequence()) ||
        SequencePlayer->GetSequence() != Actor->GetSequence()) return false;
    const double Duration = SequencePlayer->GetDuration().AsSeconds();
    return FMath::IsFinite(Duration) && Duration > KINDA_SMALL_NUMBER;
}
}

AEGMainMenuController::AEGMainMenuController()
{
    MenuWidgetClass = UEGMainMenuWidget::StaticClass();
    SettingsWidgetClass = UEGMainMenuSettingsWidget::StaticClass();
    bAutoManageActiveCameraTarget = false;
}

void AEGMainMenuController::BeginPlay()
{
    Super::BeginPlay();
    if (!IsLocalController()) return;

    // Bootstrap only. The front-road title scene and subsequent camera motion
    // share the gameplay world; bulk run preload waits for the Start click.
    if (bUseIntegratedOpening && !bPreviewOnly && BeginIntegratedEntry()) return;
    InitializeLegacyMenu();
}

bool AEGMainMenuController::BeginIntegratedEntry()
{
    if (!GetWorld() || GetWorld()->bIsTearingDown || GameMap.IsNone() || OpeningState != EEGMainMenuOpeningState::Idle) return false;
    OpeningState = EEGMainMenuOpeningState::Leaving;
    UEGMainMenuSettingsWidget::ApplySavedVolume(GetWorld());
    if (MenuWidget) MenuWidget->BeginStartFade();
    if (PlayerCameraManager)
        PlayerCameraManager->StartCameraFade(1.f, 1.f, 0.f, FLinearColor::Black, false, true);
    GetWorldTimerManager().SetTimer(TravelTimeoutTimer, this, &ThisClass::OnTravelTimeout, TravelTimeoutSeconds, false);
    if (GetWorld()->WorldType == EWorldType::PIE)
    {
        auto* OpeningInstance=Cast<UEGOpeningGameInstance>(GetGameInstance());
        auto* Viewport=GetWorld()->GetGameViewport();
        if (OpeningInstance && Viewport && OpeningInstance->BeginPIETravelCover(GetWorld()))
        {
            PIETravelViewport=Viewport;
            PIETravelDrawHandle=Viewport->OnDrawn().AddUObject(this,&ThisClass::OnPIETravelCoverDrawn);
            return true;
        }
        UE_LOG(LogTemp,Warning,TEXT("PIE opening travel cover unavailable; using the engine travel frame."));
    }
    FadeToGameTimer = GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
    {
        if (GetWorld() && !GetWorld()->bIsTearingDown)
            UGameplayStatics::OpenLevel(this, GameMap, true, TEXT("EGOpening=1"));
    }));
    return true;
}

void AEGMainMenuController::OnPIETravelCoverDrawn()
{
    if(OpeningState!=EEGMainMenuOpeningState::Leaving||!GetWorld()||GetWorld()->bIsTearingDown)return;
    UE_LOG(LogTemp,Display,TEXT("OpeningTravel PIE source viewport draw observed world=%s"),*GetNameSafe(GetWorld()));
    ClearPIETravelDrawCallback();
    // OnDrawn precedes the Slate window's presentation; travel on the next tick
    // so the viewport's static cover has a chance to be presented first.
    FadeToGameTimer=GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this]()
    {
        if(GetWorld()&&!GetWorld()->bIsTearingDown&&OpeningState==EEGMainMenuOpeningState::Leaving)
            UGameplayStatics::OpenLevel(this,GameMap,true,TEXT("EGOpening=1"));
    }));
}

void AEGMainMenuController::ClearPIETravelDrawCallback()
{
    if(PIETravelViewport.IsValid()&&PIETravelDrawHandle.IsValid())PIETravelViewport->OnDrawn().Remove(PIETravelDrawHandle);
    PIETravelDrawHandle.Reset();PIETravelViewport.Reset();
}

void AEGMainMenuController::InitializeLegacyMenu()
{
    for (TActorIterator<ACameraActor> It(GetWorld()); It; ++It)
    {
        if (It->ActorHasTag(MenuCameraTag))
        {
            MenuCamera = *It;
            break;
        }
    }
    if (MenuCamera.IsValid())
        SetViewTarget(MenuCamera.Get());
    else
        UE_LOG(LogTemp, Warning, TEXT("Main menu camera tagged '%s' was not found."), *MenuCameraTag.ToString());

    // A missing/late asset is not fatal: keep the menu interactive and allow retry.
    StartIdleSequence();

    UEGMainMenuSettingsWidget::ApplySavedVolume(GetWorld());
    if (!MenuWidget && MenuWidgetClass)
    {
        MenuWidget = CreateWidget<UEGMainMenuWidget>(this, MenuWidgetClass);
        if (MenuWidget)
        {
            MenuWidget->Configure(this, SettingsWidgetClass);
            MenuWidget->AddToViewport(20);
            bShowMouseCursor = true;
            FInputModeUIOnly Mode;
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            SetInputMode(Mode);
            // The root UUserWidget is not focusable. Focus the actual button instead.
            MenuWidget->FocusPrimaryAction();
        }
    }
}

bool AEGMainMenuController::StartIdleSequence()
{
    ALevelSequenceActor* Actor = FindUniqueSequenceActor(GetWorld(), IdleSequenceTag);
    ULevelSequencePlayer* SequencePlayer = IsValid(Actor) ? Actor->GetSequencePlayer() : nullptr;
    if (!HasUsableSequence(Actor, SequencePlayer) || Actor->ActorHasTag(OpeningSequenceTag))
    {
        UE_LOG(LogTemp, Warning, TEXT("Main menu idle sequence '%s' is unavailable or uninitialized."), *IdleSequenceTag.ToString());
        return false;
    }
    IdleSequenceActor = Actor;
    IdlePlayer = SequencePlayer;
    SequencePlayer->SetDisableCameraCuts(true);
    if (!SequencePlayer->IsPlaying()) SequencePlayer->PlayLooping(-1);
    if (!SequencePlayer->IsPlaying())
    {
        UE_LOG(LogTemp, Warning, TEXT("Main menu idle sequence '%s' could not start."), *IdleSequenceTag.ToString());
        return false;
    }
    return true;
}

bool AEGMainMenuController::PrepareOpeningSequence()
{
    if (!IsLocalController() || !IsValid(MenuWidget) || !MenuCamera.IsValid() ||
        !GetWorld() || (!bPreviewOnly && GameMap.IsNone())) return false;
    if (!StartIdleSequence()) return false;

    ALevelSequenceActor* Actor = FindUniqueSequenceActor(GetWorld(), OpeningSequenceTag);
    ULevelSequencePlayer* SequencePlayer = IsValid(Actor) ? Actor->GetSequencePlayer() : nullptr;
    if (!HasUsableSequence(Actor, SequencePlayer) || Actor == IdleSequenceActor.Get() ||
        Actor->PlaybackSettings.bAutoPlay || SequencePlayer->IsPlaying() || SequencePlayer->IsPaused())
    {
        UE_LOG(LogTemp, Warning, TEXT("Main menu opening sequence '%s' is missing, invalid, autoplaying or already active."), *OpeningSequenceTag.ToString());
        return false;
    }
    OpeningSequenceActor = Actor;
    OpeningPlayer = SequencePlayer;
    SetViewTarget(MenuCamera.Get());
    return true;
}

bool AEGMainMenuController::RequestStartGame()
{
    if (bUseIntegratedOpening && !bPreviewOnly) return BeginIntegratedEntry();
    if (OpeningState != EEGMainMenuOpeningState::Idle || !PrepareOpeningSequence()) return false;
    OpeningState = EEGMainMenuOpeningState::Playing;
    bOpeningPlaybackStarted = false;
    MenuWidget->BeginStartFade();
    GetWorldTimerManager().SetTimer(FadeToOpeningTimer, this, &ThisClass::StartOpeningAfterFade,
                                    MenuOpeningFadeSeconds, false);
    return true;
}

void AEGMainMenuController::StartOpeningAfterFade()
{
    if (OpeningState != EEGMainMenuOpeningState::Playing) return;
    ULevelSequencePlayer* Idle = IdlePlayer.Get();
    if (!MenuCamera.IsValid() || !IdleSequenceActor.IsValid() || !IsValid(Idle) || !Idle->IsPlaying() ||
        !HasUsableSequence(OpeningSequenceActor.Get(), OpeningPlayer.Get()))
    {
        RestoreMenuAfterFailure();
        return;
    }
    Idle->Pause();
    // Let the idle player's final evaluation finish before the opening takes over its bindings.
    PlayOpeningTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::PlayOpeningAfterIdlePause);
}

void AEGMainMenuController::PlayOpeningAfterIdlePause()
{
    if (OpeningState != EEGMainMenuOpeningState::Playing) return;
    ALevelSequenceActor* Actor = OpeningSequenceActor.Get();
    ULevelSequencePlayer* SequencePlayer = OpeningPlayer.Get();
    if (!MenuCamera.IsValid() || !IdleSequenceActor.IsValid() || !IdlePlayer.IsValid() ||
        !IdlePlayer->IsPaused() || !HasUsableSequence(Actor, SequencePlayer) ||
        Actor->PlaybackSettings.bAutoPlay || SequencePlayer->IsPlaying() || SequencePlayer->IsPaused())
    {
        RestoreMenuAfterFailure();
        return;
    }

    // Pause at a natural end so the final pose survives through the fade. A deliberate
    // Stop restores the pre-opening scene, which permits a clean return to the menu.
    FMovieSceneSequencePlaybackSettings Settings = Actor->PlaybackSettings;
    Settings.LoopCount.Value = 0;
    Settings.PlayRate = 1.f;
    Settings.StartTime = 0.f;
    Settings.bRandomStartTime = false;
    Settings.bAutoPlay = false;
    Settings.bPauseAtEnd = true;
    Settings.bDisableCameraCuts = true;
    Settings.FinishCompletionStateOverride = EMovieSceneCompletionModeOverride::ForceRestoreState;
    SequencePlayer->SetPlaybackSettings(Settings);
    SequencePlayer->SetPlaybackPosition(FMovieSceneSequencePlaybackParams(SequencePlayer->GetStartTime().Time, EUpdatePositionMethod::Jump));
    SequencePlayer->OnFinished.AddUniqueDynamic(this, &ThisClass::OpeningFinished);
    SequencePlayer->OnStop.AddUniqueDynamic(this, &ThisClass::OpeningPausedOrStopped);
    SequencePlayer->OnPause.AddUniqueDynamic(this, &ThisClass::OpeningPausedOrStopped);
    bOpeningPlaybackStarted = true;
    bShowMouseCursor = false;
    SetViewTarget(MenuCamera.Get());
    SequencePlayer->Play();
    if (!SequencePlayer->IsPlaying())
    {
        UE_LOG(LogTemp, Warning, TEXT("Main menu opening sequence did not start."));
        RestoreMenuAfterFailure();
        return;
    }
    GetWorldTimerManager().SetTimer(PlaybackWatchdogTimer, this, &ThisClass::CheckOpeningPlayback,
                                    OpeningWatchdogSeconds, true);
}

void AEGMainMenuController::OpeningPausedOrStopped()
{
    if (OpeningState != EEGMainMenuOpeningState::Playing || !bOpeningPlaybackStarted || !GetWorld()) return;
    // Natural finish broadcasts pause/stop first and OnFinished afterwards. Do not
    // treat that first notification as an interruption until the following tick.
    GetWorldTimerManager().ClearTimer(StopCheckTimer);
    StopCheckTimer = GetWorldTimerManager().SetTimerForNextTick(this, &ThisClass::CheckInterruptedOpening);
}

void AEGMainMenuController::CheckInterruptedOpening()
{
    if (OpeningState == EEGMainMenuOpeningState::Playing && bOpeningPlaybackStarted &&
        (!OpeningPlayer.IsValid() || !OpeningPlayer->IsPlaying()))
    {
        UE_LOG(LogTemp, Warning, TEXT("Main menu opening was interrupted; restoring the menu."));
        RestoreMenuAfterFailure();
    }
}

void AEGMainMenuController::CheckOpeningPlayback()
{
    if (OpeningState != EEGMainMenuOpeningState::Playing || !bOpeningPlaybackStarted) return;
    if (!MenuCamera.IsValid() || !IdleSequenceActor.IsValid() || !OpeningSequenceActor.IsValid() ||
        !OpeningPlayer.IsValid() || !OpeningPlayer->IsPlaying())
    {
        UE_LOG(LogTemp, Warning, TEXT("Main menu opening lost its camera or player; restoring the menu."));
        RestoreMenuAfterFailure();
    }
}

void AEGMainMenuController::OpeningFinished()
{
    if (OpeningState != EEGMainMenuOpeningState::Playing || !bOpeningPlaybackStarted) return;
    if (!MenuCamera.IsValid() || !OpeningPlayer.IsValid())
    {
        RestoreMenuAfterFailure();
        return;
    }
    SetViewTarget(MenuCamera.Get());
    GetWorldTimerManager().ClearTimer(StopCheckTimer);
    GetWorldTimerManager().ClearTimer(PlaybackWatchdogTimer);
    UnbindOpeningEvents();
    OpeningState = EEGMainMenuOpeningState::Leaving;
    if (bPreviewOnly) return;
    if (PlayerCameraManager)
        PlayerCameraManager->StartCameraFade(0.f, 1.f, OpeningExitFadeSeconds, FLinearColor::Black, false, true);
    GetWorldTimerManager().SetTimer(FadeToGameTimer, this, &ThisClass::OpenGameMap,
                                    OpeningExitFadeSeconds, false);
}

void AEGMainMenuController::UnbindOpeningEvents()
{
    if (ULevelSequencePlayer* SequencePlayer = OpeningPlayer.Get())
    {
        SequencePlayer->OnFinished.RemoveDynamic(this, &ThisClass::OpeningFinished);
        SequencePlayer->OnStop.RemoveDynamic(this, &ThisClass::OpeningPausedOrStopped);
        SequencePlayer->OnPause.RemoveDynamic(this, &ThisClass::OpeningPausedOrStopped);
    }
}

void AEGMainMenuController::RestoreMenuAfterFailure()
{
    if (!GetWorld() || GetWorld()->bIsTearingDown) return;
    OpeningState = EEGMainMenuOpeningState::Idle;
    bOpeningPlaybackStarted = false;
    GetWorldTimerManager().ClearTimer(FadeToOpeningTimer);
    GetWorldTimerManager().ClearTimer(PlayOpeningTimer);
    GetWorldTimerManager().ClearTimer(StopCheckTimer);
    GetWorldTimerManager().ClearTimer(PlaybackWatchdogTimer);
    GetWorldTimerManager().ClearTimer(FadeToGameTimer);
    GetWorldTimerManager().ClearTimer(TravelTimeoutTimer);
    UnbindOpeningEvents();
    if (ULevelSequencePlayer* SequencePlayer = OpeningPlayer.Get())
        if (SequencePlayer->IsPlaying() || SequencePlayer->IsPaused()) SequencePlayer->Stop();
    if (PlayerCameraManager) PlayerCameraManager->StopCameraFade();
    if (MenuCamera.IsValid()) SetViewTarget(MenuCamera.Get());
    StartIdleSequence();
    bShowMouseCursor = true;
    FInputModeUIOnly Mode;
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    if (MenuWidget) MenuWidget->RestoreAfterStartFailure();
}

void AEGMainMenuController::OpenGameMap()
{
    if (OpeningState != EEGMainMenuOpeningState::Leaving || bPreviewOnly) return;
    if (!GetWorld() || GameMap.IsNone())
    {
        RestoreMenuAfterFailure();
        return;
    }
    SetInputMode(FInputModeGameOnly());
    GetWorldTimerManager().SetTimer(TravelTimeoutTimer, this, &ThisClass::OnTravelTimeout,
                                    TravelTimeoutSeconds, false);
    UGameplayStatics::OpenLevel(this, GameMap);
}

void AEGMainMenuController::OnTravelTimeout()
{
    if (OpeningState != EEGMainMenuOpeningState::Leaving) return;
    ClearPIETravelDrawCallback();
    if(auto* OpeningInstance=Cast<UEGOpeningGameInstance>(GetGameInstance()))OpeningInstance->AbortPIETravelCover(GetWorld());
    UE_LOG(LogTemp, Warning, TEXT("Main menu travel did not complete; restoring the menu."));
    if (!MenuWidget) InitializeLegacyMenu();
    RestoreMenuAfterFailure();
}

void AEGMainMenuController::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearPIETravelDrawCallback();
    GetWorldTimerManager().ClearTimer(FadeToOpeningTimer);
    GetWorldTimerManager().ClearTimer(PlayOpeningTimer);
    GetWorldTimerManager().ClearTimer(StopCheckTimer);
    GetWorldTimerManager().ClearTimer(PlaybackWatchdogTimer);
    GetWorldTimerManager().ClearTimer(FadeToGameTimer);
    GetWorldTimerManager().ClearTimer(TravelTimeoutTimer);
    UnbindOpeningEvents();
    if (ULevelSequencePlayer* SequencePlayer = OpeningPlayer.Get())
        if (SequencePlayer->IsPlaying() || SequencePlayer->IsPaused()) SequencePlayer->Stop();
    if (ULevelSequencePlayer* SequencePlayer = IdlePlayer.Get())
        if (SequencePlayer->IsPlaying() || SequencePlayer->IsPaused()) SequencePlayer->Stop();
    OpeningPlayer.Reset();
    IdlePlayer.Reset();
    OpeningSequenceActor.Reset();
    IdleSequenceActor.Reset();
    MenuCamera.Reset();
    if (MenuWidget)
    {
        MenuWidget->RemoveFromParent();
        MenuWidget = nullptr;
    }
    Super::EndPlay(Reason);
}

AEGMainMenuGameMode::AEGMainMenuGameMode()
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = AEGMainMenuController::StaticClass();
    HUDClass = nullptr;
}

void AEGMainMenuGameMode::HandleStartingNewPlayer_Implementation(APlayerController* /*NewPlayer*/)
{
    // PostLogin has already initialized the controller; this menu deliberately has no pawn.
    // The base implementation only attempts RestartPlayer, which is unnecessary here.
}

void AEGMainMenuGameMode::InitializeHUDForPlayer_Implementation(APlayerController* /*NewPlayer*/)
{
    // The local menu widget is owned by the controller; no AHUD actor is needed.
    // The base implementation unconditionally tries to spawn HUDClass, even when null.
}
