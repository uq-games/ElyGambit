#pragma once
#include "CoreMinimal.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "EGElysiaCoreReview.generated.h"
class ACameraActor;
class SWidget;

/** Private, playable action-review entry. Uses the existing source viewer and no combat state. */
UCLASS()
class ELYGAMBIT_API AEGElysiaCoreReview : public AEGSourceCharacterPreview
{
 GENERATED_BODY()
protected:
 virtual void BeginPlay() override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(Transient) TObjectPtr<ACameraActor> ReviewCamera;
 UPROPERTY(Transient) TWeakObjectPtr<AActor> PreviousCamera;
 TSharedPtr<SWidget> Controls;
 void TogglePlayback();
 void RestartMotion();
 void CloseView();
 void BoardView();
 void SetReviewCamera(bool TopDown);
 // Explicit development-only capture mode; no input window or battle behavior.
 void StartOffscreenRecording();
 void ScreenshotProcessed();
 void FinishOffscreenRecording(bool Success,const FString& Error=FString());
 void WriteRecordingStatus(const TCHAR* Status,const FString& Error=FString());
 FDelegateHandle ScreenshotHandle;
 FString RecordingDirectory,RecordingCSV,PendingScreenshot;
 int32 RecordingView=0,RecordingMotion=0,RecordingFrame=0,RecordingViewFrame=0,RecordedFrames=0,RecordingWarmup=0;
 bool bOffscreenRecording=false,bCapturePending=false,bCaptureProcessed=false,bRestoreClock=false,bPreviousFixedStep=false;
 double RecordingStarted=0,PreviousFixedDelta=0;
};
