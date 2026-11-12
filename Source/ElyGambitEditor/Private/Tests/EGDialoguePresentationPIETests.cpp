#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/TextBlock.h"
#include "Game/EGDemoGameMode.h"
#include "GameFramework/PlayerController.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGResultDialogueWidget.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"

namespace
{
class FDialogueSizedPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FDialogueSizedPIE(FIntPoint InSize):Size(InSize){}
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(Size.X,Size.Y))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

struct FDialogueCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FDialogueCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

TSharedPtr<FDialogueCapture> CaptureDialogue(UWorld* World,const FString& Label)
{
 auto* Client=World?World->GetGameViewport():nullptr;
 if(!Client||!Client->Viewport)return nullptr;
 auto State=MakeShared<FDialogueCapture>();State->Client=Client;
 State->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/
  (Label+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(State->Path),true);
 TWeakPtr<FDialogueCapture> Weak=State;
 State->Handle=Client->OnDrawn().AddLambda([Weak]()
 {
  auto Capture=Weak.Pin();if(!Capture.IsValid())return;
  ++Capture->Draws;
  if((GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0)
  {Capture->Draws=0;return;}
  if(Capture->Draws>=3&&!Capture->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
  {Capture->bRequested=true;FScreenshotRequest::RequestScreenshot(Capture->Path,true,false,false,FIntRect(),true);}
 });
 return State;
}

bool CaptureReady(const TSharedPtr<FDialogueCapture>& Capture,FIntPoint Expected,FAutomationTestBase* Test)
{
 if(!Capture.IsValid()||!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
 TArray<uint8> Bytes;
 if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
 const int32 End=Bytes.Num()-12;
 if(Bytes[End]!=0||Bytes[End+1]!=0||Bytes[End+2]!=0||Bytes[End+3]!=0||
    Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
 const uint8 Signature[]={137,80,78,71,13,10,26,10};
 for(int32 I=0;I<8;++I)if(Bytes[I]!=Signature[I]){Test->AddError(TEXT("Dialogue capture is not a PNG"));return true;}
 const auto ReadBE=[&](int32 Offset)->uint32
 {return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
 Test->TestEqual(TEXT("Dialogue PNG has requested viewport dimensions"),FIntPoint(ReadBE(16),ReadBE(20)),Expected);
 Test->AddInfo(FString::Printf(TEXT("Simulated dialogue viewport capture: %s"),*Capture->Path));
 return true;
}

class FDialoguePresentationPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 TSharedPtr<FDialogueCapture> Capture;
 double Deadline=0.;
 double FadeStart=0.;
 double WaitUntil=0.;
 float LastOpacity=0.f;
 int32 Step=0;
 bool bOwnPause=false;
public:
 FDialoguePresentationPIE(FAutomationTestBase* InTest,FIntPoint InSize):Test(InTest),Size(InSize){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+120.;
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  APlayerController* PC=World?World->GetFirstPlayerController():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  if(FPlatformTime::Seconds()>Deadline)
  {
   if(bOwnPause&&PC)PC->SetPause(false);
   Test->AddError(FString::Printf(TEXT("Dialogue PIE timed out at step %d"),Step));return true;
  }
  auto* View=HUD?HUD->GetOverlayWidgetController():nullptr;
  auto* RunPanel=HUD?HUD->GetRunPanel():nullptr;
  auto* Audio=PC?PC->FindComponentByClass<UEGGameAudioComponent>():nullptr;
  if(!World||!Mode||!Mode->CanPrepare()||!Mode->AreStageResourcesReady()||!HUD||!HUD->GetMainWidget()||!View||!RunPanel||!Audio)return false;
  TArray<UUserWidget*> Widgets;
  UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UEGResultDialogueWidget::StaticClass(),false);
  UEGResultDialogueWidget* Dialogue=nullptr;
  for(UUserWidget* Widget:Widgets)
   if(Widget&&Widget->GetOwningPlayer()==PC){Dialogue=Cast<UEGResultDialogueWidget>(Widget);break;}
  if(!Dialogue)return false;
  auto* Speaker=Cast<UTextBlock>(Dialogue->GetWidgetFromName(TEXT("ResultDialogueSpeaker")));
  auto* Text=Cast<UTextBlock>(Dialogue->GetWidgetFromName(TEXT("ResultDialogueText")));
  auto* Divider=Dialogue->GetWidgetFromName(TEXT("ResultDialogueDivider"));
  if(!Test->TestNotNull(TEXT("Subtitle speaker widget"),Speaker)||!Test->TestNotNull(TEXT("Subtitle text widget"),Text)||!Test->TestNotNull(TEXT("Subtitle divider"),Divider))return true;
  if(Step==0)
  {
   if(!Test->TestTrue(TEXT("Existing result voice metadata exists"),!Audio->MasterVoiceLines.IsEmpty()))return true;
   if(!Test->TestTrue(TEXT("Pause controlled PIE fixture"),PC->SetPause(true)))return true;
   bOwnPause=true;
   Test->TestTrue(TEXT("Subtitle initially hidden"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
   const FEGMasterVoiceLine& Line=Audio->MasterVoiceLines[0];
   Audio->OnResultDialogueStarted.Broadcast(Line);
   Test->TestTrue(TEXT("Started subtitle is non-interactive"),Dialogue->GetVisibility()==ESlateVisibility::HitTestInvisible);
   Test->TestEqual(TEXT("Speaker matches selected record"),Speaker->GetText().ToString(),Line.Speaker.ToString());
   Test->TestEqual(TEXT("Text matches selected record"),Text->GetText().ToString(),Line.Text.ToString());
   Test->TestTrue(TEXT("New dialogue starts transparent"),FMath::IsNearlyZero(Dialogue->GetRenderOpacity()));
   FadeStart=FPlatformTime::Seconds();LastOpacity=0.f;
   Test->AddInfo(TEXT("Controlled PIE is paused. Dialogue starts and ends are simulated broadcasts; no device audio or natural battle is claimed."));
   Step=1;return false;
  }
  if(Step==1)
  {
   const float Opacity=Dialogue->GetRenderOpacity();
   if(Opacity>0.f&&Opacity<.999f)
   {
    FEGMasterVoiceLine Replacement=Audio->MasterVoiceLines[0];
    Replacement.Text=FText::FromString(TEXT("这是用于检查字幕切换与长句换行的测试文字，不代表已选剧情台词。"));
    Audio->OnResultDialogueStarted.Broadcast(Replacement);
    Test->TestTrue(TEXT("Replacing a partially visible line restarts the fade"),FMath::IsNearlyZero(Dialogue->GetRenderOpacity()));
    Test->TestEqual(TEXT("Replacement text is current"),Text->GetText().ToString(),Replacement.Text.ToString());
    Audio->OnResultDialogueEnded.Broadcast();
    Test->TestTrue(TEXT("Ending during fade hides immediately"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
    WaitUntil=FPlatformTime::Seconds()+.35;Step=2;return false;
   }
   if(FPlatformTime::Seconds()-FadeStart>2.){Test->AddError(TEXT("No intermediate opacity observed during paused fade"));return true;}
   return false;
  }
  if(Step==2)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   Test->TestTrue(TEXT("Ended line stays hidden after the former fade would finish"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
   Audio->OnResultDialogueStarted.Broadcast(Audio->MasterVoiceLines[0]);
   Test->TestTrue(TEXT("Fresh line restarts transparent after cancellation"),FMath::IsNearlyZero(Dialogue->GetRenderOpacity()));
   FadeStart=FPlatformTime::Seconds();LastOpacity=0.f;Step=3;return false;
  }
  if(Step==3)
  {
   const float Opacity=Dialogue->GetRenderOpacity();
   if(Opacity+KINDA_SMALL_NUMBER<LastOpacity){Test->AddError(TEXT("Fade opacity decreased while paused"));return true;}
   LastOpacity=Opacity;
   if(Opacity<.999f)
   {
    if(FPlatformTime::Seconds()-FadeStart>2.){Test->AddError(TEXT("Fade did not complete while paused"));return true;}
    return false;
   }
   Capture=CaptureDialogue(World,FString::Printf(TEXT("DialogueSimulatedNormal_%dx%d"),Size.X,Size.Y));
   if(!Test->TestTrue(TEXT("Normal screenshot capture initialized"),Capture.IsValid()))return true;
   Step=4;return false;
  }
  if(Step==4)
  {
   if(!CaptureReady(Capture,Size,Test))return false;
   Capture.Reset();
   const FSlateRect NameBounds=Speaker->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect LineBounds=Divider->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect TextBounds=Text->GetCachedGeometry().GetRenderBoundingRect();
   Test->TestTrue(TEXT("Speaker and dialogue share a left edge"),FMath::Abs(NameBounds.Left-TextBounds.Left)<=1.f);
   Test->TestTrue(TEXT("Speaker does not overlap divider"),NameBounds.Bottom<=LineBounds.Top-1.f);
   Test->TestTrue(TEXT("Text does not overlap divider"),TextBounds.Top>=LineBounds.Bottom+1.f);
   FEGMasterVoiceLine LongLine=Audio->MasterVoiceLines[0];
   LongLine.Text=FText::FromString(TEXT("这是用于检查字幕切换与长句换行的测试文字，不代表已选剧情台词。"));
   Audio->OnResultDialogueStarted.Broadcast(LongLine);
   Test->TestTrue(TEXT("Long line begins transparent"),FMath::IsNearlyZero(Dialogue->GetRenderOpacity()));
   FEGMatchView Finished=View->GetMatchView();Finished.Phase=EEGMatchPhase::Finished;Finished.bLastVictory=true;Finished.bCanAdvance=true;
   View->OnMatchViewChanged.Broadcast(Finished);
   FadeStart=FPlatformTime::Seconds();LastOpacity=0.f;Step=5;return false;
  }
  if(Step==5)
  {
   const float Opacity=Dialogue->GetRenderOpacity();
   if(Opacity+KINDA_SMALL_NUMBER<LastOpacity){Test->AddError(TEXT("Replacement fade opacity decreased"));return true;}
   LastOpacity=Opacity;
   if(Opacity<.999f)
   {
    if(FPlatformTime::Seconds()-FadeStart>2.){Test->AddError(TEXT("Final dialogue fade did not complete while paused"));return true;}
    return false;
   }
   Capture=CaptureDialogue(World,FString::Printf(TEXT("DialogueSimulatedFinished_%dx%d"),Size.X,Size.Y));
   if(!Test->TestTrue(TEXT("Finished screenshot capture initialized"),Capture.IsValid()))return true;
   Step=6;return false;
  }
  if(Step==7)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   Test->TestTrue(TEXT("Completed line stays hidden"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
   Dialogue->SetAudioSource(nullptr);
   Audio->OnResultDialogueStarted.Broadcast(Audio->MasterVoiceLines[0]);
   Test->TestTrue(TEXT("Detached widget ignores old source"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
   Dialogue->SetAudioSource(Audio);
   Test->TestTrue(TEXT("Rebind without an active voice stays hidden"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
   Audio->OnResultDialogueStarted.Broadcast(Audio->MasterVoiceLines[0]);
   Test->TestTrue(TEXT("Rebound widget receives new dialogue"),Dialogue->GetVisibility()==ESlateVisibility::HitTestInvisible);
   Audio->OnResultDialogueEnded.Broadcast();
   Test->TestTrue(TEXT("Rebound widget hides on end"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
   if(bOwnPause){PC->SetPause(false);bOwnPause=false;}
   return true;
  }
  if(!CaptureReady(Capture,Size,Test))return false;
  Capture.Reset();
  auto* ResultFrame=RunPanel->GetWidgetFromName(TEXT("BoardFinalFrame"));
  auto* ResultButton=RunPanel->GetWidgetFromName(TEXT("Button_RunAction"));
  if(Test->TestNotNull(TEXT("Formal final-result frame exists"),ResultFrame)&&Test->TestNotNull(TEXT("Formal restart button exists"),ResultButton))
  {
   const FSlateRect Subtitle=Text->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect Name=Speaker->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect Line=Divider->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect Frame=ResultFrame->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect Button=ResultButton->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect Screen=Dialogue->GetCachedGeometry().GetRenderBoundingRect();
   Test->TestTrue(TEXT("Simulated final card is visible"),ResultFrame->IsVisible());
   Test->TestTrue(TEXT("Subtitle speaker is below final card"),Name.Top>=Frame.Bottom-1.f);
   Test->TestTrue(TEXT("Subtitle is below restart button"),Subtitle.Top>=Button.Bottom-1.f);
   Test->TestTrue(TEXT("Final speaker and dialogue share a left edge"),FMath::Abs(Name.Left-Subtitle.Left)<=1.f);
   Test->TestTrue(TEXT("Final speaker does not overlap divider"),Name.Bottom<=Line.Top-1.f);
   Test->TestTrue(TEXT("Final dialogue does not overlap divider"),Subtitle.Top>=Line.Bottom+1.f);
   Test->TestTrue(TEXT("Both subtitle rows fit the viewport"),Name.Top>=Screen.Top-1.f&&Subtitle.Bottom<=Screen.Bottom+1.f);
  }
  Audio->OnResultDialogueEnded.Broadcast();
  Test->TestTrue(TEXT("Ended subtitle is hidden"),Dialogue->GetVisibility()==ESlateVisibility::Collapsed);
  WaitUntil=FPlatformTime::Seconds()+.35;Step=7;return false;
 }
};

bool QueueDialoguePIE(FAutomationTestBase* Test,FIntPoint Size)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Dialogue capture requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Dialogue capture requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FDialogueSizedPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FDialoguePresentationPIE(Test,Size));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDialoguePresentation1080,"ElyGambit.Integration.DialoguePresentation1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDialoguePresentation1080::RunTest(const FString&){return QueueDialoguePIE(this,FIntPoint(1920,1080));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDialoguePresentation720,"ElyGambit.Integration.DialoguePresentation720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDialoguePresentation720::RunTest(const FString&){return QueueDialoguePIE(this,FIntPoint(1280,720));}
#endif
