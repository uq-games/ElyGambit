#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGOpeningWidget.generated.h"

class APlayerController;
class UButton;
class UImage;
class UCircularThrobber;
class UTextBlock;
class UEGMainHUD;
class UEGDemoPanel;
class UEGMainMenuSettingsWidget;
class AEGHUD;

/** Local display for the opening. Gameplay readiness and transactions remain on the GameMode. */
UCLASS()
class ELYGAMBIT_API UEGOpeningWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    static UEGOpeningWidget* Install(APlayerController* Player);
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual void NativeDestruct() override;

private:
    void BuildLayout();
    void HideGameHUD();
    void RestoreHUD();
    void OnSettingsClosed();
    UFUNCTION() void StartClicked();
    UFUNCTION() void RetryClicked();
    UFUNCTION() void SettingsClicked();
    UFUNCTION() void ReturnToMenuClicked();
    UFUNCTION() void QuitClicked();

    UPROPERTY(Transient) TObjectPtr<UImage> StartupCover;
    UPROPERTY(Transient) TArray<TObjectPtr<UWidget>> MenuDecoration;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> LoadingDetail;
    UPROPERTY(Transient) TObjectPtr<UCircularThrobber> LoadingSpinner;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> StatusText;
    UPROPERTY(Transient) TObjectPtr<UImage> StatusLine;
    UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
    UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
    UPROPERTY(Transient) TObjectPtr<UButton> SettingsButton;
    UPROPERTY(Transient) TObjectPtr<UButton> ReturnMenuButton;
    UPROPERTY(Transient) TObjectPtr<UButton> QuitButton;
    UPROPERTY(Transient) TObjectPtr<UEGMainMenuSettingsWidget> Settings;
    UPROPERTY(Transient) TWeakObjectPtr<AEGHUD> HiddenHUDHost;
    float Elapsed = 0.f;
    float FadeElapsed = 0.f;
    float StartupRevealElapsed = 0.f;
    float ResourceWaitElapsed = 0.f;
    bool bStartupSceneRevealed = false;
    bool bStarting = false;
    bool bFoundPresentation = false;
    bool bWaitForPreparation = false;
    bool bEndgameRecoveryMode = false;
    bool bRecoveryRetryInFlight = false;
};
