#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/EditableTextBox.h"
#include "EGEndgameFailureWidget.generated.h"

class UAudioComponent;
class UBorder;
class UButton;
class UEditableTextBox;
class UImage;
class USoundBase;
class UTextBlock;
class UTexture2D;

/** Presentation for the recoverable final-boss defeat. The GameMode owns recovery. */
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGEndgameFailureWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 UEGEndgameFailureWidget(const FObjectInitializer& ObjectInitializer);
 void StartFailure();
 bool IsFailureActive() const { return bActive; }

 UPROPERTY(EditDefaultsOnly, Category="Endgame|Presentation") TSoftObjectPtr<UTexture2D> BackgroundTexture;
 UPROPERTY(EditDefaultsOnly, Category="Endgame|Audio") TArray<TSoftObjectPtr<USoundBase>> WrongAnswerVoices;
 UPROPERTY(EditDefaultsOnly, Category="Endgame|Audio") TSoftObjectPtr<USoundBase> SuccessVoice;
 /** Transcript paired with SuccessVoice, displayed only after the correct answer. */
 UPROPERTY(EditDefaultsOnly, Category="Endgame|Presentation") FText SuccessSubtitle;
 /** Original corruption voice heard while the name is erased. */
 UPROPERTY(EditDefaultsOnly, Category="Endgame|Audio") TSoftObjectPtr<USoundBase> OpeningVoice;

protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeConstruct() override;
 virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
 virtual FReply NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event) override;
 virtual void NativeDestruct() override;

private:
 UFUNCTION() void InputChanged(const FText& Text);
 UFUNCTION() void InputCommitted(const FText& Text,ETextCommit::Type CommitMethod);
 UFUNCTION() void Submit();
 UFUNCTION() void ContinueInput();
 UFUNCTION() void ReturnToMenu();
 UFUNCTION() void ExitGame();
 void SetExitMenuOpen(bool bOpen);
 void FinishSuccess();
 void CloseFailure(bool bRestoreInput);
 void PlayWrongAnswer();
 void UpdateHints();
 void UpdateBackgroundCover(FVector2D ViewSize);

 UPROPERTY(Transient) TObjectPtr<UImage> Background;
 UPROPERTY(Transient) TWeakObjectPtr<UTexture2D> LastCoverTexture;
 UPROPERTY(Transient) TObjectPtr<UBorder> GlitchLineA;
 UPROPERTY(Transient) TObjectPtr<UBorder> GlitchLineB;
 UPROPERTY(Transient) TObjectPtr<UBorder> InputFrame;
 UPROPERTY(Transient) TObjectPtr<UBorder> OpeningDialogue;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> OpeningLine;
 UPROPERTY(Transient) TObjectPtr<UBorder> SuccessVeil;
 UPROPERTY(Transient) TObjectPtr<UEditableTextBox> Input;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> ErasingText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Hint;
 UPROPERTY(Transient) TObjectPtr<UButton> SubmitButton;
 UPROPERTY(Transient) TObjectPtr<UBorder> ExitMenu;
 UPROPERTY(Transient) TObjectPtr<UButton> ContinueButton;
 UPROPERTY(Transient) TObjectPtr<UButton> ReturnButton;
 UPROPERTY(Transient) TObjectPtr<UButton> ExitButton;
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> Voice;
 double StartedAt=0.0;
 double OpeningStartedAt=0.0;
 double SuccessAt=0.0;
 double MenuOpenedAt=0.0;
 double WrongFeedbackAt=-1.0;
 float SuccessDuration=0.f;
 float LastLayoutWidth=-1.f;
 float LastLayoutHeight=-1.f;
 FVector2D LastCoverViewSize=FVector2D::ZeroVector;
 int32 LastCoverTextureWidth=0;
 int32 LastCoverTextureHeight=0;
 int32 WrongCount=0;
 int32 NextVoice=0;
 bool bActive=false;
 bool bInputReady=false;
 bool bUpdatingText=false;
 bool bHintFocusSet=false;
 bool bSuccess=false;
 bool bExitMenuOpen=false;
 bool bOpeningVoiceStarted=false;
 bool bOpeningDialogueActive=false;
};
