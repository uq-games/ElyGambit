#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGVictoryEndingWidget.generated.h"

class AEGVictoryEndingPresentation;
class UButton;
class UBorder;
class UImage;
class UMediaTexture;
class UTextBlock;
class UEGGradientSurface;

/** Owns only local input and presentation; the ending actor owns playback. */
UCLASS()
class ELYGAMBIT_API UEGVictoryEndingWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual void NativeOnInitialized() override;
 virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
 void BindEnding(AEGVictoryEndingPresentation* InEnding);
 void ShowDialogue(const FText& Speaker, const FText& Line, bool bAnimate);
 void ShowVideo(UMediaTexture* Texture);
 void ClearContent();
 void ShowError(const FText& Message);
 bool RevealOrIsComplete();
 void SetTransitionOpacity(float Opacity);
private:
 UFUNCTION() void Advance();
 UFUNCTION() void SkipSegment();
 UFUNCTION() void SkipAll();
 UFUNCTION() void Retry();
 UPROPERTY(Transient) TObjectPtr<UTextBlock> SpeakerText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> SubtitleText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> ErrorText;
 UPROPERTY(Transient) TObjectPtr<UImage> VideoImage;
 UPROPERTY(Transient) TObjectPtr<UBorder> VideoBackground;
 UPROPERTY(Transient) TObjectPtr<UMediaTexture> CurrentVideo;
 UPROPERTY(Transient) TObjectPtr<UEGGradientSurface> DialogueBackdrop;
 UPROPERTY(Transient) TObjectPtr<UButton> RetryButton;
 UPROPERTY(Transient) TObjectPtr<UButton> SegmentButton;
 UPROPERTY(Transient) TObjectPtr<UBorder> TransitionVeil;
 TWeakObjectPtr<AEGVictoryEndingPresentation> Ending;
 FString FullText;
 float RevealedCharacters = 0.f;
 bool bTyping = false;
 bool bHasDialogue = false;
};
