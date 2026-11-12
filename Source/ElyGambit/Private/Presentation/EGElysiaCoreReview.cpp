#include "Presentation/EGElysiaCoreReview.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "UnrealClient.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "EGNativeRendererLibrary.h"

void AEGElysiaCoreReview::BeginPlay()
{
 Super::BeginPlay();
 if(GetNetMode()==NM_DedicatedServer)return;
 auto* PC=UGameplayStatics::GetPlayerController(this,0);if(!PC||!PC->IsLocalController())return;
 PreviousCamera=PC->GetViewTarget();
 ReviewCamera=GetWorld()->SpawnActor<ACameraActor>();
 if(ReviewCamera){ReviewCamera->SetOwner(this);ReviewCamera->GetCameraComponent()->SetFieldOfView(35.f);CloseView();}
#if !UE_BUILD_SHIPPING
 if(FParse::Param(FCommandLine::Get(),TEXT("EGElysiaRecord"))){StartOffscreenRecording();return;}
#endif
 EnableInput(PC);
 if(InputComponent)
 {
  InputComponent->BindKey(EKeys::Right,IE_Pressed,this,&ThisClass::NextMotion);
  InputComponent->BindKey(EKeys::Left,IE_Pressed,this,&ThisClass::PreviousMotion);
  InputComponent->BindKey(EKeys::SpaceBar,IE_Pressed,this,&ThisClass::TogglePlayback);
  InputComponent->BindKey(EKeys::R,IE_Pressed,this,&ThisClass::RestartMotion);
  InputComponent->BindKey(EKeys::One,IE_Pressed,this,&ThisClass::BoardView);
  InputComponent->BindKey(EKeys::Two,IE_Pressed,this,&ThisClass::CloseView);
 }
 PC->bShowMouseCursor=true;PC->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));
 const TWeakObjectPtr<AEGElysiaCoreReview> Weak(this);
 auto Button=[Weak](const TCHAR* Label,void(AEGElysiaCoreReview::*Action)())
 {
  return SNew(SButton).Text(FText::FromString(Label)).OnClicked_Lambda([Weak,Action]()
  {if(auto* V=Weak.Get())(V->*Action)();return FReply::Handled();});
 };
 Controls=SNew(SVerticalBox)
 +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(12.f)
 [SNew(SBorder).Padding(12.f)[SNew(STextBlock).Text_Lambda([Weak]()
 {
  const auto* V=Weak.Get();if(!V||!V->Definition||!V->Definition->Motions.IsValidIndex(V->MotionIndex))return FText::GetEmpty();
  return FText::FromString(FString::Printf(TEXT("爱莉希雅 · %d / %d · %s"),V->MotionIndex+1,V->Definition->Motions.Num(),*V->Definition->Motions[V->MotionIndex].Label.ToString()));
 })]]
 +SVerticalBox::Slot().FillHeight(1.f)
 +SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(12.f)
 [SNew(SBorder).Padding(8.f)[SNew(SHorizontalBox)
  +SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("← 上一个"),&ThisClass::PreviousMotion)]
  +SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("暂停 / 播放"),&ThisClass::TogglePlayback)]
  +SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("重新播放"),&ThisClass::RestartMotion)]
  +SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("下一个 →"),&ThisClass::NextMotion)]
  +SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("俯视"),&ThisClass::BoardView)]
  +SHorizontalBox::Slot().AutoWidth().Padding(4.f)[Button(TEXT("近景"),&ThisClass::CloseView)]
 ]];
 if(auto* Viewport=GetWorld()->GetGameViewport())Viewport->AddViewportWidgetContent(Controls.ToSharedRef(),10);
}
void AEGElysiaCoreReview::TogglePlayback(){if(bPlaying)Pause();else Play();}
void AEGElysiaCoreReview::RestartMotion(){SetPreviewTime(0);Play();}
void AEGElysiaCoreReview::CloseView(){SetReviewCamera(false);}
void AEGElysiaCoreReview::BoardView(){SetReviewCamera(true);}
void AEGElysiaCoreReview::SetReviewCamera(bool TopDown)
{
 if(!ReviewCamera)return;
 const FVector Focus=GetActorLocation()+FVector(0,0,95);
 const FVector Position=Focus+(TopDown?FVector(470,470,700):FVector(20,600,155));
 ReviewCamera->SetActorLocationAndRotation(Position,(Focus-Position).Rotation());
 if(auto* PC=UGameplayStatics::GetPlayerController(this,0))PC->SetViewTarget(ReviewCamera);
}
void AEGElysiaCoreReview::EndPlay(const EEndPlayReason::Type Reason)
{
 if(bOffscreenRecording)WriteRecordingStatus(TEXT("INTERRUPTED"),TEXT("Review world ended before recording completed"));
 bOffscreenRecording=false;
 if(ScreenshotHandle.IsValid()){FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset();}
 if(bCapturePending&&FScreenshotRequest::GetFilename()==PendingScreenshot)FScreenshotRequest::Reset();
 if(bRestoreClock){FApp::SetFixedDeltaTime(PreviousFixedDelta);FApp::SetUseFixedTimeStep(bPreviousFixedStep);bRestoreClock=false;}
 if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
 {DisableInput(PC);if(PC->GetViewTarget()==ReviewCamera&&PreviousCamera.IsValid())PC->SetViewTarget(PreviousCamera.Get());}
 if(Controls&&GetWorld())if(auto* Viewport=GetWorld()->GetGameViewport())Viewport->RemoveViewportWidgetContent(Controls.ToSharedRef());
 Controls.Reset();if(IsValid(ReviewCamera))ReviewCamera->Destroy();ReviewCamera=nullptr;PreviousCamera.Reset();
 Super::EndPlay(Reason);
}

void AEGElysiaCoreReview::StartOffscreenRecording()
{
 const FString Base=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ElysiaNative20261025/OffscreenColorRecording"));
 RecordingDirectory=Base/FString::Printf(TEXT("Run_%s_%u"),*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),FPlatformProcess::GetCurrentProcessId());
 IFileManager::Get().MakeDirectory(*(RecordingDirectory/TEXT("close")),true);
 IFileManager::Get().MakeDirectory(*(RecordingDirectory/TEXT("board")),true);
 RecordingCSV=TEXT("view,index,label,duration,frames\n");
 if(!FParse::Param(FCommandLine::Get(),TEXT("RenderOffscreen"))||!Definition||Definition->Motions.IsEmpty())
 {FinishOffscreenRecording(false,TEXT("Capture requires RenderOffscreen and a populated review catalog"));return;}
 PreviousFixedDelta=FApp::GetFixedDeltaTime();bPreviousFixedStep=FApp::UseFixedTimeStep();bRestoreClock=true;
 FApp::SetFixedDeltaTime(1.0/12.0);FApp::SetUseFixedTimeStep(true);
 bOffscreenRecording=true;RecordingStarted=FPlatformTime::Seconds();RecordingWarmup=36;
 ScreenshotHandle=FScreenshotRequest::OnScreenshotRequestProcessed().AddUObject(this,&ThisClass::ScreenshotProcessed);
 Pause();MotionIndex=0;SetPreviewTime(0);CloseView();WriteRecordingStatus(TEXT("RUNNING"));
 UE_LOG(LogTemp,Display,TEXT("Elysia offscreen recording: %s"),*RecordingDirectory);
}
void AEGElysiaCoreReview::ScreenshotProcessed()
{if(bOffscreenRecording&&bCapturePending)bCaptureProcessed=true;}
void AEGElysiaCoreReview::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!bOffscreenRecording)return;
 if(FPlatformTime::Seconds()-RecordingStarted>600){FinishOffscreenRecording(false,TEXT("Offscreen capture timeout"));return;}
 if(bCapturePending)
 {
  if(!bCaptureProcessed||IFileManager::Get().FileSize(*PendingScreenshot)<=0)return;
  bCapturePending=bCaptureProcessed=false;++RecordedFrames;++RecordingViewFrame;++RecordingFrame;
  const auto& Motion=Definition->Motions[RecordingMotion];
  const int32 Count=FMath::CeilToInt(Motion.Duration*12.f)+1;
  if(RecordingFrame>=Count)
  {
   const FString Label=Motion.Label.ToString().Replace(TEXT("\""),TEXT("\"\""));
   RecordingCSV+=FString::Printf(TEXT("%s,%d,\"%s\",%.9f,%d\n"),RecordingView==0?TEXT("close"):TEXT("board"),RecordingMotion,*Label,Motion.Duration,Count);
   ++RecordingMotion;RecordingFrame=0;WriteRecordingStatus(TEXT("RUNNING"));
   if(RecordingMotion>=Definition->Motions.Num())
   {
    RecordingMotion=0;RecordingViewFrame=0;++RecordingView;
    if(RecordingView>=2){FinishOffscreenRecording(true);return;}
    BoardView();RecordingWarmup=12;
   }
   else RecordingWarmup=2;
   MotionIndex=RecordingMotion;SetPreviewTime(0);
  }
  else {SetPreviewTime(FMath::Min(Motion.Duration,RecordingFrame/12.f));RecordingWarmup=1;}
  return;
 }
 if(RecordingWarmup>0){--RecordingWarmup;return;}
 PendingScreenshot=RecordingDirectory/(RecordingView==0?TEXT("close"):TEXT("board"))/FString::Printf(TEXT("frame_%05d.png"),RecordingViewFrame);
 if(IFileManager::Get().FileExists(*PendingScreenshot)){FinishOffscreenRecording(false,TEXT("Refusing to overwrite an existing frame"));return;}
 bCapturePending=true;bCaptureProcessed=false;
 FScreenshotRequest::RequestScreenshot(PendingScreenshot,false,false);
}
void AEGElysiaCoreReview::WriteRecordingStatus(const TCHAR* Status,const FString& Error)
{
 if(RecordingDirectory.IsEmpty())return;
 const FString SafeError=Error.Replace(TEXT("\\"),TEXT("/ ")).Replace(TEXT("\""),TEXT("'"));
 const FString Renderer=UEGNativeRendererLibrary::GetRenderDiagnostics();
 const FString JSON=FString::Printf(TEXT("{\"status\":\"%s\",\"visual_status\":\"待用户验收\",\"fps\":12,\"frames\":%d,\"view\":%d,\"motion\":%d,\"error\":\"%s\",\"native_renderer\":%s}"),Status,RecordedFrames,RecordingView,RecordingMotion,*SafeError,*Renderer);
 FFileHelper::SaveStringToFile(JSON,*(RecordingDirectory/TEXT("result.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 FFileHelper::SaveStringToFile(RecordingCSV,*(RecordingDirectory/TEXT("actions.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
void AEGElysiaCoreReview::FinishOffscreenRecording(bool Success,const FString& Error)
{
 const FString Renderer=UEGNativeRendererLibrary::GetRenderDiagnostics();
 const bool Missing=!(Renderer.Contains(TEXT("\"missing_shaders\":0"))||Renderer.Contains(TEXT("\"missing_shaders\": 0")));
 const bool Passed=Success&&!Missing&&RecordedFrames>0;
 WriteRecordingStatus(Passed?TEXT("PASS"):TEXT("FAILED"),Missing?TEXT("Native source shader missing during capture"):Error);
 bOffscreenRecording=false;
 if(ScreenshotHandle.IsValid()){FScreenshotRequest::OnScreenshotRequestProcessed().Remove(ScreenshotHandle);ScreenshotHandle.Reset();}
 if(bRestoreClock){FApp::SetFixedDeltaTime(PreviousFixedDelta);FApp::SetUseFixedTimeStep(bPreviousFixedStep);bRestoreClock=false;}
 FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
}
