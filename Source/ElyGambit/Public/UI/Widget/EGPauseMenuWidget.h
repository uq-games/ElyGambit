#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGPauseMenuWidget.generated.h"

class AEGChessGameMode;
class UButton;
class UEGMainMenuSettingsWidget;

/** Single-player pause UI. Its pause request is independent of combat presentation holds. */
UCLASS()
class ELYGAMBIT_API UEGPauseMenuWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 bool OpenMenu();
 bool IsMenuOpen() const { return bMenuOpen; }
 void HandleEscape();
 void CloseMenu();
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeDestruct() override;
private:
 void BuildLayout();
 bool CanReleasePause() const { return !bOwnPause; }
 void ReleaseOwnedPause();
 void SettingsClosed();
 UFUNCTION() void ContinueClicked();
 UFUNCTION() void SettingsClicked();
 UFUNCTION() void ReturnMenuClicked();
 UFUNCTION() void ExitClicked();
 UPROPERTY(Transient) TObjectPtr<UButton> ContinueButton;
 UPROPERTY(Transient) TObjectPtr<UButton> SettingsButton;
 UPROPERTY(Transient) TObjectPtr<UButton> ReturnButton;
 UPROPERTY(Transient) TObjectPtr<UButton> ExitButton;
 UPROPERTY(Transient) TObjectPtr<UEGMainMenuSettingsWidget> Settings;
 TWeakObjectPtr<AEGChessGameMode> PauseMode;
 FDelegateHandle PauseHandle;
 bool bOwnPause=false;
 bool bMenuOpen=false;
};
