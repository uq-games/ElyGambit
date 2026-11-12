#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "EGMainMenuGameMode.generated.h"

class UEGMainMenuWidget;
class UEGMainMenuSettingsWidget;
class ACameraActor;
class ALevelSequenceActor;
class ULevelSequencePlayer;
class UGameViewportClient;

UENUM(BlueprintType)
enum class EEGMainMenuOpeningState : uint8
{
    Idle,
    Playing,
    Leaving
};

UCLASS(Blueprintable)
class ELYGAMBIT_API AEGMainMenuController : public APlayerController
{
    GENERATED_BODY()
public:
    AEGMainMenuController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    UFUNCTION(BlueprintCallable, Category="Main Menu") bool RequestStartGame();
    UFUNCTION(BlueprintPure, Category="Main Menu") EEGMainMenuOpeningState GetOpeningState() const { return OpeningState; }
    UFUNCTION(BlueprintPure, Category="Main Menu") UEGMainMenuWidget* GetMenuWidget() const { return MenuWidget; }

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    TSubclassOf<UEGMainMenuWidget> MenuWidgetClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    TSubclassOf<UEGMainMenuSettingsWidget> SettingsWidgetClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    FName MenuCameraTag = TEXT("ElyMenuCamera");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    FName IdleSequenceTag = TEXT("ElyMenuIdle");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    FName OpeningSequenceTag = TEXT("ElyMenuOpening");
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    FName GameMap = TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7");
    /** For isolated preview only: hold the final pose instead of opening the game map. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Main Menu|Preview")
    bool bPreviewOnly = false;
    /** Normal entry uses the continuous opening inside the board world. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Main Menu")
    bool bUseIntegratedOpening = true;

private:
    bool BeginIntegratedEntry();
    void InitializeLegacyMenu();
    bool StartIdleSequence();
    bool PrepareOpeningSequence();
    void StartOpeningAfterFade();
    void PlayOpeningAfterIdlePause();
    void CheckOpeningPlayback();
    void CheckInterruptedOpening();
    void RestoreMenuAfterFailure();
    void UnbindOpeningEvents();
    UFUNCTION() void OpeningFinished();
    UFUNCTION() void OpeningPausedOrStopped();
    void OpenGameMap();
    void OnTravelTimeout();
    void OnPIETravelCoverDrawn();
    void ClearPIETravelDrawCallback();
    UPROPERTY(Transient) TObjectPtr<UEGMainMenuWidget> MenuWidget;
    UPROPERTY(Transient) TWeakObjectPtr<ACameraActor> MenuCamera;
    UPROPERTY(Transient) TWeakObjectPtr<ALevelSequenceActor> IdleSequenceActor;
    UPROPERTY(Transient) TWeakObjectPtr<ALevelSequenceActor> OpeningSequenceActor;
    UPROPERTY(Transient) TWeakObjectPtr<ULevelSequencePlayer> IdlePlayer;
    UPROPERTY(Transient) TWeakObjectPtr<ULevelSequencePlayer> OpeningPlayer;
    UPROPERTY(Transient) EEGMainMenuOpeningState OpeningState = EEGMainMenuOpeningState::Idle;
    FTimerHandle FadeToOpeningTimer;
    FTimerHandle PlayOpeningTimer;
    FTimerHandle StopCheckTimer;
    FTimerHandle PlaybackWatchdogTimer;
    FTimerHandle FadeToGameTimer;
    FTimerHandle TravelTimeoutTimer;
    TWeakObjectPtr<UGameViewportClient> PIETravelViewport;
    FDelegateHandle PIETravelDrawHandle;
    bool bOpeningPlaybackStarted = false;
};

UCLASS(Blueprintable)
class ELYGAMBIT_API AEGMainMenuGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEGMainMenuGameMode();
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
    virtual void InitializeHUDForPlayer_Implementation(APlayerController* NewPlayer) override;
};
