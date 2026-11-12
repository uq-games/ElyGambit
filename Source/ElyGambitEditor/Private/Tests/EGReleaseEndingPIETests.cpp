#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "AudioDeviceManager.h"
#include "AudioMixerBlueprintLibrary.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "MediaPlayer.h"
#include "UObject/UnrealType.h"
#include "Presentation/EGVictoryEndingDefinition.h"
#include "Presentation/EGVictoryEndingPresentation.h"
#include "Sound/SoundBase.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGEndgameFailureWidget.h"
#include "UI/Widget/EGVictoryEndingWidget.h"

namespace
{
template<typename T> T* EndingObject(UObject* Owner,const TCHAR* PropertyName)
{
 auto* Property=Owner?FindFProperty<FObjectPropertyBase>(Owner->GetClass(),PropertyName):nullptr;
 return Property?Cast<T>(Property->GetObjectPropertyValue_InContainer(Owner)):nullptr;
}

UButton* FindEndingAction(UEGVictoryEndingWidget* Widget,FName Action,int32& OutCount)
{
 OutCount=0;UButton* Found=nullptr;
 if(!Widget||!Widget->WidgetTree)return nullptr;
 Widget->WidgetTree->ForEachWidget([&](UWidget* Child)
 {
  auto* Button=Cast<UButton>(Child);
  if(Button&&Button->OnClicked.Contains(Widget,Action)){Found=Button;++OutCount;}
 });
 return Found;
}

bool EndingSyntheticClick(UButton* Button)
{
 if(!Button||!Button->IsVisible()||!Button->GetIsEnabled())return false;
 auto& Slate=FSlateApplication::Get();
 const auto Window=Slate.FindWidgetWindow(Button->TakeWidget());if(!Window.IsValid())return false;
 const auto& Geometry=Button->GetCachedGeometry();
 if(Geometry.GetLocalSize().X<=0.f||Geometry.GetLocalSize().Y<=0.f)return false;
 const FVector2D Point=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
 Slate.SetCursorPos(Point);
 Slate.ProcessMouseMoveEvent(FPointerEvent(0,Point,Point,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
 const bool bDown=Slate.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
  FPointerEvent(0,Point,Point,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
 Slate.ProcessMouseButtonUpEvent(FPointerEvent(0,Point,Point,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
 return bDown;
}

bool EndingSyntheticKey(FKey Key)
{
 auto& Slate=FSlateApplication::Get();
 const bool bDown=Slate.ProcessKeyDownEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
 Slate.ProcessKeyUpEvent(FKeyEvent(Key,FModifierKeysState(),0,false,0,0));
 return bDown;
}

struct FEndingViewportCapture
{
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle DrawHandle;
 FString Path;
 int32 ReadyDraws=0;
 bool bRequested=false;
 ~FEndingViewportCapture(){if(Client.IsValid()&&DrawHandle.IsValid())Client->OnDrawn().Remove(DrawHandle);}
};

class FReleaseEndingStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1600;Settings->NewWindowHeight=900;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1600,900))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

class FReleaseEndingPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.;
 int32 Step=0,LastResolvedStage=0,OriginalRevision=INDEX_NONE;
 TArray<FGuid> OriginalDeployed;
 int64 OriginalGold=0;
 bool bSentInput=false;
 bool bFailureAssetsChecked=false;
 FString CaptureDirectory;
 FString AudioPath;
 TWeakObjectPtr<UWorld> RecordingWorld;
 bool bRecording=false;
 double RecordingStartedAt=0.,ExportDeadline=0.;
 TSharedPtr<FEndingViewportCapture> PendingCapture;
 TWeakObjectPtr<AEGVictoryEndingPresentation> Victory;
 int32 DialogueIndex=1;
 double NodeSeenAt=0.;
 int32 FailureStep=0;
 double FailurePauseAt=0.,FailurePauseDuration=0.;
 bool bFailurePauseCaptured=false;
 void EnsureCaptureDirectory()
 {
  if(!CaptureDirectory.IsEmpty())return;
  CaptureDirectory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ReleaseAudit20261110/EndingTimeline")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
  IFileManager::Get().MakeDirectory(*CaptureDirectory,true);
 }
 void StopRecording(const TCHAR* Name)
 {
  if(!bRecording)return;
  bRecording=false;
  if(!RecordingWorld.IsValid())
  {
   Test->AddError(TEXT("Ending recording world was lost before recording could be stopped"));
   return;
  }
  UAudioMixerBlueprintLibrary::StopRecordingOutput(RecordingWorld.Get(),EAudioRecordingExportType::WavFile,Name,CaptureDirectory);
  Test->AddInfo(FString::Printf(TEXT("Master mix stop requested at %.2f s: %s"),FPlatformTime::Seconds()-RecordingStartedAt,Name));
  RecordingWorld.Reset();
 }
 bool Capture(UWorld* World,const TCHAR* Label)
 {
  EnsureCaptureDirectory();
  if(!PendingCapture.IsValid())
  {
   auto* Client=World?World->GetGameViewport():nullptr;
   if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return false;
   PendingCapture=MakeShared<FEndingViewportCapture>();PendingCapture->World=World;PendingCapture->Client=Client;
   PendingCapture->Path=CaptureDirectory/(FString(Label)+TEXT(".png"));
   const TWeakPtr<FEndingViewportCapture> Weak=PendingCapture;
   PendingCapture->DrawHandle=Client->OnDrawn().AddLambda([Weak]()
   {
    auto Capture=Weak.Pin();if(!Capture.IsValid()||!Capture->World.IsValid()||!Capture->Client.IsValid())return;
    auto* Viewport=Capture->Client->Viewport;
    if(!Viewport||Capture->World->GetGameViewport()!=Capture->Client.Get()||Viewport->GetSizeXY()!=FIntPoint(1600,900))
    {Capture->ReadyDraws=0;return;}
    ++Capture->ReadyDraws;
    if(Capture->ReadyDraws>=3&&!Capture->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
    {Capture->bRequested=true;FScreenshotRequest::RequestScreenshot(Capture->Path,true,false,false,FIntRect(),true);}
   });
   return false;
  }
  if(!PendingCapture->bRequested||PendingCapture->ReadyDraws<5||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*PendingCapture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;
  if(Bytes[End]!=0||Bytes[End+1]!=0||Bytes[End+2]!=0||Bytes[End+3]!=0||
     Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const uint8 Signature[]={137,80,78,71,13,10,26,10};
  for(int32 I=0;I<8;++I)if(Bytes[I]!=Signature[I])
  {Test->AddError(TEXT("Ending capture is not a PNG: ")+PendingCapture->Path);PendingCapture.Reset();return true;}
  const auto ReadBE=[&](int32 Offset)->uint32
   {return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
  Test->TestEqual(TEXT("Ending screenshot matches the actual PIE viewport"),FIntPoint(ReadBE(16),ReadBE(20)),FIntPoint(1600,900));
  Test->AddInfo(TEXT("Ending viewport capture: ")+PendingCapture->Path);PendingCapture.Reset();return true;
 }
public:
 explicit FReleaseEndingPIE(FAutomationTestBase* InTest):Test(InTest){}
 ~FReleaseEndingPIE(){StopRecording(TEXT("InterruptedMasterMix"));}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+300.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(FString::Printf(TEXT("Release ending PIE timed out at step %d after stage %d"),Step,LastResolvedStage));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Match=Mode?Mode->GetMatch():nullptr;
  auto* Cards=Mode?Mode->GetCards():nullptr;
  if(!World||!Mode||!PC||!HUD||!HUD->GetRunPanel()||!Match||!Cards)return false;

  if(Step==0)
  {
   if(Match->GetPhase()==EEGMatchPhase::Reward)
   {
    const int32 Stage=Match->GetStageNumber();
    if(Stage==6)
    {
     for(const auto& Hero:Mode->GetRoster()->GetHeroes())if(Hero.bDeployed)OriginalDeployed.Add(Hero.InstanceId);
     OriginalGold=Cards->GetGold();OriginalRevision=Mode->GetStigmataRevision();
     if(!Test->TestTrue(TEXT("Pre-route checkpoint has deployed heroes"),!OriginalDeployed.IsEmpty())||
        !Test->TestTrue(TEXT("Select OriginFlower using production transaction"),Mode->ChooseEndgameRoute(TEXT("OriginFlower"),OriginalRevision)))return true;
     Step=1;return false;
    }
    const auto Offer=Mode->GetStigmataOffer();
    if(!Test->TestTrue(TEXT("Ordinary reward has candidates"),!Offer.IsEmpty()))return true;
    FName Choice=Offer[0];for(const FName Id:Offer)if(Id!=TEXT("Reverie")&&Id!=TEXT("Stars")&&Id!=TEXT("Vicissitude")){Choice=Id;break;}
    if(!Test->TestTrue(TEXT("Select ordinary reward"),Mode->ChooseStigmata(Choice,Mode->GetStigmataRevision())))return true;
    return false;
   }
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const int32 Stage=Match->GetStageNumber();if(Stage<=LastResolvedStage)return false;
   if(Cards->GetPieces().IsEmpty())
   {
    const auto Hand=Cards->GetHand();
    if(!Test->TestTrue(TEXT("Starting hand is available"),!Hand.IsEmpty())||
       !Test->TestTrue(TEXT("Deploy starting card"),Cards->TryDeployHandCard(Hand[0].InstanceId,{3,1})))return true;
   }
   if(!Test->TestTrue(TEXT("Enter actual stage combat"),Mode->EnterCombat())||
      !Test->TestTrue(TEXT("Resolve controlled stage victory"),Mode->ResolveCombat(true)))return true;
   LastResolvedStage=Stage;return false;
  }

  if(Step==1)
  {
   if(Match->GetStageNumber()!=7||!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Hand=Cards->GetHand();
   const auto* Human=Hand.FindByPredicate([](const FEGCardInstance& Card)
    {return Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;});
   if(!Test->TestNotNull(TEXT("Origin grants human card"),Human)||
      !Test->TestTrue(TEXT("Deploy human"),Cards->TryDeployHandCard(Human->InstanceId,{3,1}))||
      !Test->TestTrue(TEXT("Enter actual boss combat"),Mode->EnterCombat())||
      !Test->TestTrue(TEXT("Resolve actual boss loss"),Mode->ResolveCombat(false)))return true;
   Test->TestTrue(TEXT("Final result is initially gated"),HUD->ShouldHideFinalResult());
   Step=2;return false;
  }

  if(Step==2)
  {
   TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UEGEndgameFailureWidget::StaticClass(),false);
   UEGEndgameFailureWidget* Failure=nullptr;
   for(auto* Widget:Widgets)if(Widget&&Widget->GetOwningPlayer()==PC){Failure=Cast<UEGEndgameFailureWidget>(Widget);break;}
   if(!Failure||!Failure->IsFailureActive())return false;
   auto* Input=Cast<UEditableTextBox>(Failure->GetWidgetFromName(TEXT("FailureInput")));
   if(!Test->TestNotNull(TEXT("Failure input exists"),Input))return true;
   if(FailureStep==0&&!Input->GetIsEnabled())return false;
   if(!bFailureAssetsChecked)
   {
    Test->TestTrue(TEXT("Checkpoint becomes available after resolution guard"),Mode->CanRestoreEndgameCheckpoint());
    Test->TestTrue(TEXT("Failure interface gates normal result"),HUD->ShouldHideFinalResult());
    Test->TestNotNull(TEXT("Failure background resolves"),Failure->BackgroundTexture.LoadSynchronous());
    Test->TestTrue(TEXT("Wrong-answer voices configured"),!Failure->WrongAnswerVoices.IsEmpty());
    for(const auto& Voice:Failure->WrongAnswerVoices)Test->TestNotNull(TEXT("Wrong-answer voice resolves"),Voice.LoadSynchronous());
    Test->TestNotNull(TEXT("Success voice resolves"),Failure->SuccessVoice.LoadSynchronous());
    bFailureAssetsChecked=true;
   }
   auto* ExitMenu=Cast<UBorder>(Failure->GetWidgetFromName(TEXT("FailureExitMenu")));
   auto* Continue=Cast<UButton>(Failure->GetWidgetFromName(TEXT("FailureContinue")));
   if(!Test->TestNotNull(TEXT("Failure exit menu exists"),ExitMenu)||
      !Test->TestNotNull(TEXT("Failure continue button exists"),Continue))return true;
   if(FailureStep==0)
   {
    if(!bRecording)
    {
     if(!FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World))
     {Test->AddError(TEXT("No PIE audio mixer device; cannot capture the actual master mix"));return true;}
     EnsureCaptureDirectory();
     AudioPath=CaptureDirectory/TEXT("FailureToEndingMasterMix.wav");
     UAudioMixerBlueprintLibrary::StartRecordingOutput(World,120.f);
     RecordingWorld=World;bRecording=true;RecordingStartedAt=FPlatformTime::Seconds();
     Test->AddInfo(TEXT("Started PIE master-submix recording at the active failure input; audio device, user volumes, synthetic actions and controlled battle results apply."));
    }
    if(!Capture(World,TEXT("FailureInput")))return false;
    Input->SetKeyboardFocus();
    if(!Test->TestTrue(TEXT("Synthetic Escape opens failure menu"),EndingSyntheticKey(EKeys::Escape)))return true;
    if(!Test->TestTrue(TEXT("Failure menu visible and input disabled"),ExitMenu->IsVisible()&&!Input->GetIsEnabled()))return true;
    FailureStep=1;return false;
   }
   if(FailureStep==1)
   {
    if(!Capture(World,TEXT("FailureEscapeMenu")))return false;
    if(!Test->TestTrue(TEXT("Synthetic continue button closes failure menu"),EndingSyntheticClick(Continue)))return true;
    if(!Test->TestTrue(TEXT("Input restored after continue"),!ExitMenu->IsVisible()&&Input->GetIsEnabled()))return true;
    FailureStep=2;return false;
   }
   if(FailureStep==2&&!bSentInput)
   {
    Input->SetKeyboardFocus();auto& Slate=FSlateApplication::Get();
    Input->SetText(FText::FromString(TEXT("eLySiA")));
    Test->TestEqual(TEXT("Mixed case is normalized"),Input->GetText().ToString(),FString(TEXT("ELYSIA")));
    Input->SetText(FText::GetEmpty());
    for(const TCHAR C:FString(TEXT("ELYSIA")))Slate.ProcessKeyCharEvent(FCharacterEvent(C,FModifierKeysState(),uint32(0),false));
    if(!Test->TestEqual(TEXT("Synthetic typing reaches the name field"),Input->GetText().ToString(),FString(TEXT("ELYSIA"))))return true;
    Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
    Slate.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
    bSentInput=true;FailureStep=3;return false;
   }
   if(FailureStep==3)
   {
    auto* SuccessAudio=EndingObject<UAudioComponent>(Failure,TEXT("Voice"));
    if(!SuccessAudio||!SuccessAudio->IsPlaying())return false;
    auto* SuccessSound=Failure->SuccessVoice.Get();
    if(!Test->TestNotNull(TEXT("Success sound is loaded before pausing"),SuccessSound))return true;
    FailurePauseDuration=FMath::Max(1.2f,SuccessSound->GetDuration())+.4;
    if(!Test->TestTrue(TEXT("Escape during success voice opens menu"),EndingSyntheticKey(EKeys::Escape)))return true;
    if(!Test->TestTrue(TEXT("Success voice paused in Escape menu"),ExitMenu->IsVisible()&&SuccessAudio->GetPlayState()==EAudioComponentPlayState::Paused))return true;
    FailurePauseAt=FPlatformTime::Seconds();FailureStep=4;return false;
   }
   if(FailureStep==4)
   {
    if(Match->GetPhase()!=EEGMatchPhase::Finished)
    {Test->AddError(TEXT("Checkpoint restored while success voice was paused"));return true;}
    if(!bFailurePauseCaptured){if(!Capture(World,TEXT("FailureSuccessPaused")))return false;bFailurePauseCaptured=true;}
    if(FPlatformTime::Seconds()-FailurePauseAt<FailurePauseDuration)return false;
    auto* SuccessAudio=EndingObject<UAudioComponent>(Failure,TEXT("Voice"));
    if(!Test->TestTrue(TEXT("Voice remains paused beyond its original duration"),SuccessAudio&&SuccessAudio->GetPlayState()==EAudioComponentPlayState::Paused))return true;
    if(!Test->TestTrue(TEXT("Second Escape resumes success voice"),EndingSyntheticKey(EKeys::Escape)))return true;
    if(!Test->TestFalse(TEXT("Success pause menu closed"),ExitMenu->IsVisible()))return true;
    FailurePauseAt=FPlatformTime::Seconds();FailureStep=5;return false;
   }
   if(FailureStep==5)
   {
    if(FPlatformTime::Seconds()-FailurePauseAt<.1)return false;
    auto* SuccessAudio=EndingObject<UAudioComponent>(Failure,TEXT("Voice"));
    if(!Test->TestTrue(TEXT("Success voice resumes and recovery remains pending"),
       SuccessAudio&&SuccessAudio->IsPlaying()&&SuccessAudio->GetPlayState()!=EAudioComponentPlayState::Paused&&
       Match->GetPhase()==EEGMatchPhase::Finished))return true;
    Step=3;
   }
   return false;
  }

  if(Step==3)
  {
   if(Match->GetPhase()!=EEGMatchPhase::Reward)return false;
   if(!Test->TestEqual(TEXT("Recovery returns to stage six"),Match->GetStageNumber(),6)||
      !Test->TestTrue(TEXT("Final route choice is reopened"),Mode->IsChoosingEndgameRoute())||
      !Test->TestTrue(TEXT("Old route cleared"),Match->GetEndgameRoute().IsNone()))return true;
   Test->TestEqual(TEXT("Pre-route gold restored"),Cards->GetGold(),OriginalGold);
   for(const FGuid Id:OriginalDeployed)
    Test->TestTrue(TEXT("Original deployed hero restored"),Mode->GetRoster()->GetHeroes().ContainsByPredicate([Id](const FEGHeroInstance& Hero){return Hero.InstanceId==Id&&Hero.bDeployed;}));
   Test->TestFalse(TEXT("Old route revision rejected"),Mode->ChooseEndgameRoute(TEXT("Unity"),OriginalRevision));
   if(!Test->TestTrue(TEXT("Choose another final route"),Mode->ChooseEndgameRoute(TEXT("Story"),Mode->GetStigmataRevision())))return true;
   Step=4;return false;
  }

  if(Step==4)
  {
   if(Match->GetStageNumber()!=7||!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(!Test->TestTrue(TEXT("Restored team enters a new boss battle"),Mode->EnterCombat())||
      !Test->TestTrue(TEXT("Resolve controlled boss victory"),Mode->ResolveCombat(true)))return true;
   Test->TestTrue(TEXT("Victory ending gates final result"),HUD->ShouldHideFinalResult());
   Step=5;return false;
  }

  if(Step==5)
  {
   AEGVictoryEndingPresentation* Ending=nullptr;
   for(TActorIterator<AEGVictoryEndingPresentation> It(World);It;++It){Ending=*It;break;}
   if(!Ending)return false;
   auto* Definition=HUD->VictoryEndingDefinition.LoadSynchronous();
   if(!Test->TestNotNull(TEXT("Victory ending definition resolves"),Definition))return true;
   if(!Test->TestEqual(TEXT("Victory ending contains two videos and four dialogues"),Definition->Nodes.Num(),6))return true;
   {
    Test->TestNotNull(TEXT("Victory music resolves"),Definition->BackgroundMusic.ToSoftObjectPath().TryLoad());
    for(int32 Index=0;Index<Definition->Nodes.Num();++Index)
    {
     const auto& Node=Definition->Nodes[Index];
     Test->TestTrue(TEXT("Node sequence matches authored ending"),Node.Type==(Index==0||Index==5?EEGVictoryEndingNodeType::Video:EEGVictoryEndingNodeType::Dialogue));
     if(Node.Type==EEGVictoryEndingNodeType::Dialogue)
     {Test->TestTrue(TEXT("Dialogue text and speaker are present"),!Node.Subtitle.IsEmpty()&&!Node.Speaker.IsEmpty());Test->TestNotNull(TEXT("Dialogue voice resolves"),Node.Voice.ToSoftObjectPath().TryLoad());}
     if(Node.Type==EEGVictoryEndingNodeType::Sequence)Test->TestNotNull(TEXT("Ending sequence resolves"),Node.Sequence.ToSoftObjectPath().TryLoad());
     if(Node.Type==EEGVictoryEndingNodeType::Video)Test->TestNotNull(TEXT("Ending video resolves"),Node.MediaSource.ToSoftObjectPath().TryLoad());
    }
   }
   Victory=Ending;Step=6;return false;
  }
  auto* Ending=Victory.Get();
  if(Step==15)
  {
   // WavFile export is asynchronous. Wait for a complete RIFF before ending PIE.
   const int64 FileSize=IFileManager::Get().FileSize(*AudioPath);
   if(FileSize>=44)
   {
    TUniquePtr<FArchive> Reader(IFileManager::Get().CreateFileReader(*AudioPath));
    if(Reader)
    {
     uint8 Header[12]={};Reader->Serialize(Header,12);
     const uint32 RiffSize=uint32(Header[4])|(uint32(Header[5])<<8)|(uint32(Header[6])<<16)|(uint32(Header[7])<<24);
     if(!Reader->IsError()&&Header[0]=='R'&&Header[1]=='I'&&Header[2]=='F'&&Header[3]=='F'&&
        Header[8]=='W'&&Header[9]=='A'&&Header[10]=='V'&&Header[11]=='E'&&RiffSize>=36&&int64(RiffSize)+8<=FileSize)
     {
      Test->AddInfo(FString::Printf(TEXT("Complete WAV container (%lld bytes), for listening review: %s"),FileSize,*AudioPath));
      Test->AddInfo(TEXT("Recording existence and a valid container do not establish audibility, mix quality, natural play, or audiovisual acceptance."));
      return true;
     }
    }
   }
   if(FPlatformTime::Seconds()>ExportDeadline){Test->AddError(TEXT("Ending master-mix WAV did not finish exporting: ")+AudioPath);return true;}
   return false;
  }
  if(Step==14)
  {
   // SkipAll fades out for .6s, restores the view/gate, then fades in before
   // destroying the presentation. Do not assert either result on the same frame.
   if(HUD->ShouldHideFinalResult()||IsValid(Ending))return false;
   if(!Capture(World,TEXT("FinalResultMenu")))return false;
   Test->AddInfo(TEXT("Formal board PIE, controlled combat outcomes and synthetic Slate keyboard/pointer events. Not natural play, physical input, audiovisual acceptance or packaged-game validation."));
   StopRecording(TEXT("FailureToEndingMasterMix"));
   ExportDeadline=FPlatformTime::Seconds()+30.;Deadline=FMath::Max(Deadline,ExportDeadline+1.);Step=15;return false;
  }
  if(!IsValid(Ending)){Test->AddError(TEXT("Victory presentation ended before its expected timeline"));return true;}
  auto* Definition=HUD->VictoryEndingDefinition.Get();
  auto* Widget=EndingObject<UEGVictoryEndingWidget>(Ending,TEXT("Widget"));
  auto* Media=EndingObject<UMediaPlayer>(Ending,TEXT("MediaPlayer"));
  auto* Voice=EndingObject<UAudioComponent>(Ending,TEXT("Voice"));
  if(!Definition||!Widget||!Media||!Voice)return false;
  int32 SurfaceCount=0;
  auto* Surface=FindEndingAction(Widget,TEXT("Advance"),SurfaceCount);
  if(SurfaceCount!=1){Test->AddError(FString::Printf(TEXT("Expected one bound Advance surface, found %d"),SurfaceCount));return true;}
  auto* Subtitle=EndingObject<UTextBlock>(Widget,TEXT("SubtitleText"));
  auto* Error=EndingObject<UTextBlock>(Widget,TEXT("ErrorText"));
  if(Error&&!Error->GetText().IsEmpty()){Test->AddError(TEXT("Victory ending error: ")+Error->GetText().ToString());return true;}

  if(Step==6)
  {
   if(!Media->IsPlaying()||Media->GetTime().GetTotalSeconds()<.5)return false;
   Test->TestTrue(TEXT("First video is playing while result is gated"),HUD->ShouldHideFinalResult());
   if(!Capture(World,TEXT("FirstVideo")))return false;
   if(!Test->TestTrue(TEXT("Ordinary synthetic click reaches first video"),EndingSyntheticClick(Surface)))return true;
   NodeSeenAt=FPlatformTime::Seconds();Step=7;return false;
  }
  if(Step==7)
  {
   if(FPlatformTime::Seconds()-NodeSeenAt<.2)return false;
   if(!Test->TestTrue(TEXT("Ordinary click does not skip a video"),Media->IsPlaying()))return true;
   Step=8;return false;
  }
  if(Step==8)
  {
   // No skip is used: the first video's EndReached must open dialogue 1.
   auto* Expected=Definition->Nodes[DialogueIndex].Voice.Get();
   if(Voice->Sound!=Expected||!Voice->IsPlaying())return false;
   if(!Test->TestFalse(TEXT("First video ended before dialogue"),Media->IsPlaying()))return true;
   NodeSeenAt=FPlatformTime::Seconds();Step=9;return false;
  }
  if(Step==9)
  {
   if(FPlatformTime::Seconds()-NodeSeenAt<.22)return false;
   if(!Subtitle||!Surface)return false;
   const FString Full=Definition->Nodes[DialogueIndex].Subtitle.ToString();
   if(!Test->TestTrue(TEXT("Dialogue is typing before first click"),Subtitle->GetText().ToString()!=Full))return true;
   if(!Test->TestTrue(TEXT("First synthetic dialogue click reaches the surface"),EndingSyntheticClick(Surface)))return true;
   if(!Test->TestEqual(TEXT("First click reveals full dialogue without advancing"),Subtitle->GetText().ToString(),Full))return true;
   Test->TestTrue(TEXT("First click retains the same dialogue voice"),Voice->Sound==Definition->Nodes[DialogueIndex].Voice.Get());
   Step=10;return false;
  }
  if(Step==10)
  {
   // Capture after the real-time shot and voice have had time to progress.
   if(FPlatformTime::Seconds()-NodeSeenAt<.9)return false;
   const FString Label=FString::Printf(TEXT("Dialogue%d"),DialogueIndex);
   if(!Capture(World,*Label))return false;
   Step=11;return false;
  }
  if(Step==11)
  {
   if(FPlatformTime::Seconds()-NodeSeenAt<2.0)return false;
   if(DialogueIndex<4)
   {
    if(!Test->TestTrue(TEXT("Second synthetic click advances dialogue"),EndingSyntheticClick(Surface)))return true;
    ++DialogueIndex;NodeSeenAt=0.;Step=12;return false;
   }
   auto* Segment=EndingObject<UButton>(Widget,TEXT("SegmentButton"));
   if(!Test->TestTrue(TEXT("Explicit skip segment reaches final video"),EndingSyntheticClick(Segment)))return true;
   Step=13;return false;
  }
  if(Step==12)
  {
   auto* Expected=Definition->Nodes[DialogueIndex].Voice.Get();
   if(Voice->Sound!=Expected||!Voice->IsPlaying())return false;
   NodeSeenAt=FPlatformTime::Seconds();Step=9;return false;
  }
  if(Step==13)
  {
   if(!Media->IsPlaying()||Media->GetTime().GetTotalSeconds()<.5)return false;
   if(!Capture(World,TEXT("FinalVideo")))return false;
   int32 AllCount=0;auto* All=FindEndingAction(Widget,TEXT("SkipAll"),AllCount);
   if(!Test->TestEqual(TEXT("Exactly one Skip All control is bound"),AllCount,1))return true;
   if(!Test->TestTrue(TEXT("Explicit skip all reaches ending control"),EndingSyntheticClick(All)))return true;
   Step=14;return false;
  }
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGReleaseEndingGraphical,
 "ElyGambit.Integration.ReleaseEndingGraphical",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGReleaseEndingGraphical::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Release ending test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Release ending test requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FReleaseEndingStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FReleaseEndingPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
