#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGMainMenuWidget.generated.h"

class AEGMainMenuController;
class UButton;
class UCanvasPanel;
class UComboBoxString;
class USlider;
class UTextBlock;
class UWidget;

/** Actual audio and graphics settings, embedded inside the main menu. */
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGMainMenuSettingsWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    void OpenSettings();
    UFUNCTION() void CloseSettings();
    static void ApplySavedVolume(UWorld* World);

    DECLARE_MULTICAST_DELEGATE(FOnSettingsClosed);
    FOnSettingsClosed OnSettingsClosed;

private:
    void BuildLayout();
    void RefreshVolumeLabel(float Value);
    UFUNCTION() void OnVolumeChanged(float Value);
    UFUNCTION() void ApplySettings();

    UPROPERTY(Transient) TObjectPtr<USlider> VolumeSlider;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> VolumeValue;
    UPROPERTY(Transient) TObjectPtr<UComboBoxString> QualityCombo;
    UPROPERTY(Transient) TObjectPtr<UButton> ApplyButton;
    UPROPERTY(Transient) TObjectPtr<UButton> BackButton;
};

/** Real UMG main menu; owns one settings widget and never spawns gameplay actors. */
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGMainMenuWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    void Configure(AEGMainMenuController* Controller, TSubclassOf<UEGMainMenuSettingsWidget> InSettingsClass);
    virtual void NativeOnInitialized() override;
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    void BeginStartFade();
    void RestoreAfterStartFailure();
    void FocusPrimaryAction();

private:
    void BuildLayout();
    void RestoreMenuFocus();
    UFUNCTION() void HandleStart();
    UFUNCTION() void HandleSettings();
    UFUNCTION() void HandleQuit();
    UFUNCTION() void StartHovered();
    UFUNCTION() void StartUnhovered();
    UFUNCTION() void SettingsHovered();
    UFUNCTION() void SettingsUnhovered();
    UFUNCTION() void QuitHovered();
    UFUNCTION() void QuitUnhovered();

    UPROPERTY(Transient) TWeakObjectPtr<AEGMainMenuController> MenuController;
    UPROPERTY(Transient) TSubclassOf<UEGMainMenuSettingsWidget> SettingsClass;
    UPROPERTY(Transient) TObjectPtr<UEGMainMenuSettingsWidget> Settings;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> MainCanvas;
    UPROPERTY(Transient) TObjectPtr<UWidget> MenuGroup;
    UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
    UPROPERTY(Transient) TObjectPtr<UButton> SettingsButton;
    UPROPERTY(Transient) TObjectPtr<UButton> QuitButton;
    float FadeElapsed = 0.f;
    float FadeDuration = .65f;
    float FadeStartOpacity = 0.f;
    float FadeTargetOpacity = 1.f;
    bool bFadingOut = false;
};
