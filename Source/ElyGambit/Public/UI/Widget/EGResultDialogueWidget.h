#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGResultDialogueWidget.generated.h"

class UEGGameAudioComponent;
class UTextBlock;
class UTexture2D;
class UCanvasPanelSlot;
class UButton;
struct FEGMasterVoiceLine;

/** Local, non-interactive subtitle layer driven by the result voice's lifetime. */
UCLASS()
class ELYGAMBIT_API UEGResultDialogueWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual void NativeDestruct() override;
    void SetAudioSource(UEGGameAudioComponent* Source);
    void SetFinalResultVisible(bool bVisible);

private:
    void ShowLine(const FEGMasterVoiceLine& Line);
    void PresentLine(const FEGMasterVoiceLine& Line, bool bAnimate);
    void HideLine();
    UFUNCTION() void SkipStoryDialogue();

    UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeakerText;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> DialogueText;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanelSlot> BackgroundSlot;
    UPROPERTY(Transient) TObjectPtr<UButton> StorySkipButton;
    UPROPERTY(EditDefaultsOnly, Category="UI") TSoftObjectPtr<UTexture2D> UnderlayTexture = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/Assets/UI/Textures/Dialogue20261106/T_DialogueUnderlay.T_DialogueUnderlay")));
    TWeakObjectPtr<UEGGameAudioComponent> AudioSource;
    double FadeStartedAt = 0.0;
    bool bFadingIn = false;
};
