#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/AudioComponent.h"
#include "UObject/UnrealType.h"
#include "Sound/SoundBase.h"
#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/TextBlock.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Character/EGChessUnit.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGEncounterNoticeWidget.h"
#include "UI/Widget/EGEndgameFailureWidget.h"
#include "UI/Widget/EGDemoPanel.h"

namespace
{
class FFinalPolishStartPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FFinalPolishStartPIE(FIntPoint InSize):Size(InSize){}
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
struct FFinalPolishCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;FDelegateHandle Handle;FString Path;FIntPoint Size;int32 Draws=0;bool bRequested=false;
 ~FFinalPolishCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};
class FFinalPolishUI final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;FIntPoint Size;int32 Step=0,LastStage=0;double Deadline=0.,NextDiagnostic=0.;TSharedPtr<FFinalPolishCapture> CaptureState;
 FGuid Hold;TWeakObjectPtr<AEGDemoGameMode> HeldMode;bool bBossGeometryChecked=false,bMenuGeometryChecked=false;
 TWeakObjectPtr<UAudioComponent> OpeningAudio,SuccessAudio;
 FString FirstErase,PausedErase;double PauseStarted=0.,ResizeStarted=0.,NextResize=0.;int32 StableViewportTicks=0;
 FString Dir;
 UAudioComponent* GetVoice(UEGEndgameFailureWidget* Widget) const
 {
  auto* Property=Widget?FindFProperty<FObjectPropertyBase>(Widget->GetClass(),TEXT("Voice")):nullptr;
  return Property?Cast<UAudioComponent>(Property->GetObjectPropertyValue_InContainer(Widget)):nullptr;
 }
 bool KeyEscape()
 {
  auto& Slate=FSlateApplication::Get();
  const bool Handled=Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
  Slate.ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));return Handled;
 }
 bool Capture(UWorld* World,const TCHAR* Name)
 {
  if(!CaptureState.IsValid())
  {
   auto* Client=World?World->GetGameViewport():nullptr;if(!Client||!Client->Viewport)return false;
   CaptureState=MakeShared<FFinalPolishCapture>();CaptureState->Client=Client;CaptureState->Size=Size;CaptureState->Path=Dir/Name;
   const TWeakPtr<FFinalPolishCapture> Weak=CaptureState;
   CaptureState->Handle=Client->OnDrawn().AddLambda([Weak]()
   {
    auto C=Weak.Pin();if(!C.IsValid()||!C->Client.IsValid()||!C->Client->Viewport)return;
    if(C->Client->Viewport->GetSizeXY()!=C->Size){C->Draws=0;return;}
    if(++C->Draws>=3&&!C->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
    {C->bRequested=true;FScreenshotRequest::RequestScreenshot(C->Path,true,false,false,FIntRect(),true);}
   });return false;
  }
  if(!CaptureState->bRequested||CaptureState->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*CaptureState->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;
  if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto ReadBE=[&](int32 Offset)->uint32{return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
  Test->TestEqual(TEXT("Screenshot has requested viewport dimensions"),FIntPoint(ReadBE(16),ReadBE(20)),Size);
  Test->AddInfo(TEXT("Controlled UI screenshot: ")+CaptureState->Path);CaptureState.Reset();return true;
 }
 bool Separated(UWidget* Upper,UWidget* Lower,const TCHAR* Label)
 {
  if(!Test->TestNotNull(FString(Label)+TEXT(" upper"),Upper)||!Test->TestNotNull(FString(Label)+TEXT(" lower"),Lower))return false;
  const auto A=Upper->GetCachedGeometry().GetLayoutBoundingRect(),B=Lower->GetCachedGeometry().GetLayoutBoundingRect();
  return Test->TestTrue(Label,A.Bottom>0.f&&B.Bottom>B.Top&&A.Bottom+1.f<=B.Top);
 }
 template<class T> T* Find(UWorld* World,APlayerController* PC)
 {
  TArray<UUserWidget*> Found;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Found,T::StaticClass(),false);
  for(auto* View:Found)if(View&&View->GetOwningPlayer()==PC)return Cast<T>(View);return nullptr;
 }
public:
 FFinalPolishUI(FAutomationTestBase* InTest,FIntPoint InSize):Test(InTest),Size(InSize)
 {
  Dir=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FinalPolish20261111/UI")/FString::Printf(TEXT("%dx%d"),Size.X,Size.Y));
  IFileManager::Get().MakeDirectory(*Dir,true);
 }
 ~FFinalPolishUI(){if(Hold.IsValid()&&HeldMode.IsValid()&&HeldMode->BattleRules)HeldMode->BattleRules->SetPresentationHold(Hold,false);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+110.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(FString::Printf(TEXT("Final UI timed out at step %d stage %d"),Step,LastStage));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Match=Mode?Mode->GetMatch():nullptr;auto* Cards=Mode?Mode->GetCards():nullptr;
  if(!World||!Mode||!PC||!HUD||!HUD->GetRunPanel()||!Match||!Cards)return false;
  // Force only the test's own PIE window, never persistent game settings.
  // Some test sequences asynchronously restore a different window size.
  auto* ViewClient=World->GetGameViewport();
  if(!ViewClient||!ViewClient->Viewport||!ViewClient->GetWindow().IsValid())return false;
  const FIntPoint ActualViewport=ViewClient->Viewport->GetSizeXY();
  if(ActualViewport!=Size)
  {
   StableViewportTicks=0;if(ResizeStarted==0.)ResizeStarted=FPlatformTime::Seconds();
   if(FPlatformTime::Seconds()-ResizeStarted>8.)
   {Test->AddError(FString::Printf(TEXT("PIE viewport could not reach %dx%d; actual %dx%d"),Size.X,Size.Y,ActualViewport.X,ActualViewport.Y));return true;}
   if(FPlatformTime::Seconds()>=NextResize)
   {
    NextResize=FPlatformTime::Seconds()+.5;
    ViewClient->GetWindow()->Resize(FVector2D(Size.X,Size.Y));
    Test->AddInfo(FString::Printf(TEXT("Resizing test PIE window from %dx%d to %dx%d"),ActualViewport.X,ActualViewport.Y,Size.X,Size.Y));
   }
   return false;
  }
  ResizeStarted=0.;if(++StableViewportTicks<3)return false;
  if(FPlatformTime::Seconds()>=NextDiagnostic)
  {
   NextDiagnostic=FPlatformTime::Seconds()+6.;
   auto* Client=World->GetGameViewport();const FIntPoint Actual=Client&&Client->Viewport?Client->Viewport->GetSizeXY():FIntPoint::ZeroValue;
   UE_LOG(LogTemp,Display,TEXT("FinalPolishUI requested=%dx%d actual=%dx%d step=%d lastStage=%d stage=%d phase=%d prepare=%d resources=%d captureDraws=%d requestedCapture=%d hold=%d"),
    Size.X,Size.Y,Actual.X,Actual.Y,Step,LastStage,Match->GetStageNumber(),int32(Match->GetPhase()),Mode->CanPrepare(),Mode->AreStageResourcesReady(),
    CaptureState.IsValid()?CaptureState->Draws:-1,CaptureState.IsValid()&&CaptureState->bRequested,Hold.IsValid());
  }
  if(Step==0)
  {
   if(Match->GetPhase()==EEGMatchPhase::Reward)
   {
    if(Match->GetStageNumber()==6)
    {if(!Test->TestTrue(TEXT("Choose final route"),Mode->ChooseEndgameRoute(TEXT("OriginFlower"),Mode->GetStigmataRevision())))return true;Step=1;return false;}
    const auto Offer=Mode->GetStigmataOffer();if(!Test->TestTrue(TEXT("Ordinary reward available"),!Offer.IsEmpty()))return true;
    FName Choice=Offer[0];for(const FName Id:Offer)if(Id!=TEXT("Reverie")&&Id!=TEXT("Stars")&&Id!=TEXT("Vicissitude")){Choice=Id;break;}
    if(!Test->TestTrue(TEXT("Choose ordinary reward"),Mode->ChooseStigmata(Choice,Mode->GetStigmataRevision())))return true;
    return false;
   }
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const int32 Stage=Match->GetStageNumber();if(Stage<=LastStage)return false;
   if(Cards->GetPieces().IsEmpty())
   {const auto Hand=Cards->GetHand();if(!Test->TestTrue(TEXT("Starting hand"),!Hand.IsEmpty())||!Test->TestTrue(TEXT("Deploy hero"),Cards->TryDeployHandCard(Hand[0].InstanceId,{3,1})))return true;}
   if(!Test->TestTrue(TEXT("Enter stage"),Mode->EnterCombat())||!Test->TestTrue(TEXT("Controlled stage win"),Mode->ResolveCombat(true)))return true;
   LastStage=Stage;return false;
  }
  if(Step==1)
  {
   if(Match->GetStageNumber()!=7||!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   // The route grants Human to hand; an empty board is treated as an immediate
   // loss by EnterCombat even though that method returns true. Deploy the
   // route hero through the same production transaction as the ending test.
   const auto Hand=Cards->GetHand();
   const auto* Human=Hand.FindByPredicate([](const FEGCardInstance& Card)
    {return Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;});
   if(!Test->TestNotNull(TEXT("Origin route grants Human"),Human)||
      !Test->TestTrue(TEXT("Deploy route Human"),Cards->TryDeployHandCard(Human->InstanceId,{3,1}))||
      !Test->TestTrue(TEXT("Enter actual final battle"),Mode->EnterCombat())||
      !Test->TestTrue(TEXT("Final battle really entered combat"),Match->GetPhase()==EEGMatchPhase::Combat))return true;
   bool bPlayer=false;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetFaction_Implementation()==EEGFaction::Player){bPlayer=true;break;}
   if(!Test->TestTrue(TEXT("Final battle contains a player unit"),bPlayer))return true;
   // Keep the real encounter alive while its opening finishes and the boss
   // component is attached by EncounterCombatRunning. This is a controlled fixture.
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetAbilitySystemComponent())
   {
    auto* ASC=Unit->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000000.f);
   }
   Step=2;return false;
  }
  if(Step==2)
  {
   auto* Notice=Find<UEGEncounterNoticeWidget>(World,PC);if(!Notice)return false;
   UEGCorruptionCombatComponent* Boss=nullptr;
   for(const auto& WeakUnit:Mode->GetBattleUnits())if(auto* Unit=WeakUnit.Get())
    if(auto* C=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();C&&C->IsBoss()){Boss=C;break;}
   if(!Boss)return false;
   // Only freeze after the real boss has entered active combat; holding earlier
   // would prevent its component from being attached by the opening callback.
   if(!Hold.IsValid())
   {
    if(!Mode->BattleRules){Test->AddError(TEXT("Final battle context missing"));return true;}
    Hold=FGuid::NewGuid();HeldMode=Mode;Mode->BattleRules->SetPresentationHold(Hold,true);
   }
   Boss->SetEffectiveBattleSeconds(60.f);Notice->NativeTick(Notice->GetCachedGeometry(),0.f);
   auto* Panel=Notice->GetWidgetFromName(TEXT("CorruptionBossPanel"));auto* Title=Cast<UTextBlock>(Notice->GetWidgetFromName(TEXT("CorruptionBossTitle")));
   auto* Bar=Notice->GetWidgetFromName(TEXT("CorruptionBossHealth"));
   if(!Panel||!Panel->IsVisible()||!Title||!Title->GetText().ToString().Contains(TEXT("狂暴"))||!Bar||Title->GetCachedGeometry().GetLocalSize().Y<=0.f||Bar->GetCachedGeometry().GetLocalSize().Y<=0.f)return false;
   if(!bBossGeometryChecked)
   {
    if(!Separated(Title,Bar,TEXT("Boss title clears health bar")))return true;
    if(!Separated(HUD->GetRunPanel()->GetWidgetFromName(TEXT("PhaseClockNumber")),Title,TEXT("Boss title clears phase clock")))return true;
    Test->TestTrue(TEXT("Boss title text fits its height"),Title->GetDesiredSize().Y<=Title->GetCachedGeometry().GetLocalSize().Y+1.f);
    bBossGeometryChecked=true;
   }
   if(!Capture(World,TEXT("BossEnraged.png")))return false;
   Mode->BattleRules->SetPresentationHold(Hold,false);Hold.Invalidate();HeldMode.Reset();
   if(!Test->TestTrue(TEXT("Controlled boss loss"),Mode->ResolveCombat(false)))return true;
   Step=3;return false;
  }
  if(Step==7)
  {
   if(Match->GetPhase()!=EEGMatchPhase::Reward)return false;
   Test->TestEqual(TEXT("Successful input restores stage six once"),Match->GetStageNumber(),6);
   Test->TestTrue(TEXT("Route choice is reopened"),Mode->IsChoosingEndgameRoute());
   Test->TestFalse(TEXT("Old checkpoint is no longer reusable"),Mode->CanRestoreEndgameCheckpoint());
   Test->AddInfo(TEXT("Controlled sequence resolves battles programmatically; keyboard events are synthetic, not natural-play or listening acceptance."));return true;
  }
  auto* Failure=Find<UEGEndgameFailureWidget>(World,PC);if(!Failure||!Failure->IsFailureActive())return false;
  auto* Input=Cast<UEditableTextBox>(Failure->GetWidgetFromName(TEXT("FailureInput")));
  auto* Frame=Failure->GetWidgetFromName(TEXT("FailureInputFrame"));auto* Hint=Failure->GetWidgetFromName(TEXT("FailureHint"));
  auto* Menu=Cast<UBorder>(Failure->GetWidgetFromName(TEXT("FailureExitMenu")));
  if(!Test->TestNotNull(TEXT("Failure input"),Input)||!Test->TestNotNull(TEXT("Failure menu"),Menu))return true;
  auto* Dialogue=Cast<UBorder>(Failure->GetWidgetFromName(TEXT("FailureOpeningDialogue")));
  auto* Line=Cast<UTextBlock>(Failure->GetWidgetFromName(TEXT("FailureOpeningLine")));
  auto* Erasing=Cast<UTextBlock>(Failure->GetWidgetFromName(TEXT("FailureErasingText")));
  if(!Test->TestNotNull(TEXT("Failure dialogue"),Dialogue)||!Test->TestNotNull(TEXT("Failure subtitle"),Line)||
     !Test->TestNotNull(TEXT("Failure erasing text"),Erasing))return true;
  if(Step==3)
  {
   // Capture during the actual slow erasure rather than skipping straight to input.
   if(Input->GetIsEnabled()){Test->AddError(TEXT("Missed the opening voice and erase window"));return true;}
   if(!Dialogue->IsVisible())return false;
   auto* Voice=GetVoice(Failure);if(!Voice||!Voice->IsPlaying())return false;
   if(!OpeningAudio.IsValid())
   {
    OpeningAudio=Voice;FirstErase=Erasing->GetText().ToString();
    if(!Test->TestTrue(TEXT("Opening source is original corruption PV clip"),Failure->OpeningVoice.ToSoftObjectPath().ToString().Contains(TEXT("/SW_Corruption_Eternal_PV.SW_Corruption_Eternal_PV")))||
       !Test->TestTrue(TEXT("Opening voice uses configured source"),Failure->OpeningVoice.Get()&&Voice->GetSound()==Failure->OpeningVoice.Get())||
       !Test->TestEqual(TEXT("Opening subtitle matches corruption line"),Line->GetText().ToString(),FString(TEXT("我会……永远存在！")))||
       !Test->TestTrue(TEXT("Name is initially visible before erasing"),FirstErase.StartsWith(TEXT("ELY")))||
       !Separated(Dialogue,Frame,TEXT("Opening dialogue clears input frame")))return true;
   }
   if(!Test->TestTrue(TEXT("Opening voice is not restarted during erasure"),GetVoice(Failure)==OpeningAudio.Get()))return true;
   if(!Capture(World,TEXT("FailureOpening.png")))return false;
   const auto OpeningTextRect=Line->GetCachedGeometry().GetLayoutBoundingRect();
   const auto OpeningBoxRect=Dialogue->GetCachedGeometry().GetLayoutBoundingRect();
   const FVector2D OpeningTextSize=Line->GetCachedGeometry().GetLocalSize();
   if(!Test->TestTrue(TEXT("Opening subtitle height fits its allocated area"),OpeningTextSize.Y>0.f&&Line->GetDesiredSize().Y<=OpeningTextSize.Y+1.f)||
      !Test->TestTrue(TEXT("Opening subtitle stays within dialogue frame"),OpeningTextRect.Top>=OpeningBoxRect.Top&&OpeningTextRect.Bottom<=OpeningBoxRect.Bottom&&OpeningTextRect.Bottom>OpeningTextRect.Top))return true;
   PausedErase=Erasing->GetText().ToString();
   if(!Test->TestTrue(TEXT("Opening Escape handled"),KeyEscape())||
      !Test->TestTrue(TEXT("Opening menu visible and dialogue hidden"),Menu->IsVisible()&&!Dialogue->IsVisible())||
      !Test->TestTrue(TEXT("Opening voice pauses"),OpeningAudio.IsValid()&&OpeningAudio->GetPlayState()==EAudioComponentPlayState::Paused))return true;
   PauseStarted=FPlatformTime::Seconds();Step=31;return false;
  }
  if(Step==31)
  {
   if(FPlatformTime::Seconds()-PauseStarted<.65)return false;
   if(!Test->TestEqual(TEXT("Erasure remains frozen while menu open"),Erasing->GetText().ToString(),PausedErase)||
      !Test->TestTrue(TEXT("Same opening audio remains paused without restart"),GetVoice(Failure)==OpeningAudio.Get()&&OpeningAudio.IsValid()&&OpeningAudio->GetPlayState()==EAudioComponentPlayState::Paused)||
      !Test->TestTrue(TEXT("Resume opening by Escape"),KeyEscape())||
      !Test->TestTrue(TEXT("Opening dialogue resumes"),!Menu->IsVisible()&&Dialogue->IsVisible())||
      !Test->TestTrue(TEXT("Original opening voice resumes"),GetVoice(Failure)==OpeningAudio.Get()&&OpeningAudio->IsPlaying()))return true;
   Step=32;return false;
  }
  if(Step==32)
  {
   if(Erasing->GetText().ToString()==PausedErase&&!Input->GetIsEnabled())return false;
   if(!Test->TestTrue(TEXT("Name erases gradually after resuming"),Erasing->GetText().ToString().Len()<PausedErase.Len()&&!Input->GetIsEnabled()))return true;
   Step=33;return false;
  }
  if(Step==33)
  {
   if(!Input->GetIsEnabled())
   {
    auto* Current=GetVoice(Failure);
    if(Current&&Current->IsPlaying()&&Current!=OpeningAudio.Get()){Test->AddError(TEXT("Opening voice restarted during the same failure"));return true;}
    return false;
   }
   // The 1080 case observes the complete six-second opening line; the 720
   // case submits early and exercises interruption by the success voice.
   if(Size.X>=1920)
   {
    auto* Current=GetVoice(Failure);
    if(Current&&Current->IsPlaying())
    {
     if(Current!=OpeningAudio.Get()){Test->AddError(TEXT("Opening voice restarted after input became ready"));return true;}
     return false;
    }
    if(Dialogue->IsVisible())return false;
   }
   if(!Separated(Frame,Hint,TEXT("Failure input clears hint")))return true;
   Input->SetText(FText::FromString(TEXT("ELYSIA")));
   if(!Capture(World,TEXT("FailureInput.png")))return false;
   Input->SetKeyboardFocus();
   if(!Test->TestTrue(TEXT("Escape opens menu"),KeyEscape())||!Test->TestTrue(TEXT("Failure menu visible"),Menu->IsVisible()))return true;
   Step=4;return false;
  }
  if(Step==4)
  {
   if(Menu->GetCachedGeometry().GetLocalSize().Y<=0.f)return false;
   if(!bMenuGeometryChecked)
   {
    if(!Separated(Menu,Frame,TEXT("Failure menu clears input")))return true;
    for(const TCHAR* ButtonName:{TEXT("FailureContinue"),TEXT("FailureReturnMenu"),TEXT("FailureExitGame")})
    {
     auto* Button=Cast<UButton>(Failure->GetWidgetFromName(ButtonName));
     if(!Test->TestNotNull(TEXT("Failure menu button"),Button))return true;
     const auto Rect=Button->GetCachedGeometry().GetLayoutBoundingRect();const auto Box=Menu->GetCachedGeometry().GetLayoutBoundingRect();
     Test->TestTrue(TEXT("Menu button remains inside frame"),Rect.Top>=Box.Top&&Rect.Bottom<=Box.Bottom&&Rect.Bottom>Rect.Top);
     auto* Label=Cast<UTextBlock>(Button->GetContent());
     if(Test->TestNotNull(TEXT("Menu button label"),Label))
      Test->TestTrue(TEXT("Menu label fits its button"),Label->GetDesiredSize().Y<=Button->GetCachedGeometry().GetLocalSize().Y+1.f);
    }
    bMenuGeometryChecked=true;
   }
   if(!Capture(World,TEXT("FailureMenu.png")))return false;
   if(!Test->TestTrue(TEXT("Resume input by Escape"),KeyEscape())||!Test->TestTrue(TEXT("Input restored"),!Menu->IsVisible()&&Input->GetIsEnabled()))return true;
   if(Size.X<1920&&!Test->TestTrue(TEXT("Early submission interrupts the still-playing opening"),OpeningAudio.IsValid()&&OpeningAudio->IsPlaying()))return true;
   Input->SetText(FText::FromString(TEXT("ELYSIA")));Input->SetKeyboardFocus();
   auto& Slate=FSlateApplication::Get();
   Slate.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
   Slate.ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
   auto* Voice=GetVoice(Failure);SuccessAudio=Voice;
   if(!Test->TestTrue(TEXT("Success source is original Elysia PV clip"),Failure->SuccessVoice.ToSoftObjectPath().ToString().Contains(TEXT("/SW_Elysia_Disappointed_PV.SW_Elysia_Disappointed_PV")))||
      !Test->TestTrue(TEXT("Successful answer starts configured Elysia voice"),Voice&&Voice->IsPlaying()&&Failure->SuccessVoice.Get()&&Voice->GetSound()==Failure->SuccessVoice.Get())||
      !Test->TestTrue(TEXT("Success voice differs from corruption voice"),Failure->SuccessVoice.Get()!=Failure->OpeningVoice.Get())||
      !Test->TestTrue(TEXT("Successful answer displays its configured subtitle"),Dialogue->IsVisible()&&!Failure->SuccessSubtitle.IsEmpty()&&Line->GetText().EqualTo(Failure->SuccessSubtitle))||
      !Test->TestEqual(TEXT("Success subtitle matches Elysia line"),Line->GetText().ToString(),FString(TEXT("抱歉，得让你失望啦。"))))return true;
   if(Size.X<1920&&!Test->TestTrue(TEXT("Opening voice stops when success voice begins"),!OpeningAudio.IsValid()||!OpeningAudio->IsPlaying()))return true;
   // Explicitly send duplicate transactions while the success voice is active.
   Input->OnTextCommitted.Broadcast(Input->GetText(),ETextCommit::OnEnter);
   if(auto* Button=Cast<UButton>(Failure->GetWidgetFromName(TEXT("FailureSubmit"))))Button->OnClicked.Broadcast();
   if(!Test->TestTrue(TEXT("Duplicate submit preserves one success audio"),GetVoice(Failure)==SuccessAudio.Get()&&SuccessAudio.IsValid()&&SuccessAudio->IsPlaying())||
      !Test->TestTrue(TEXT("Checkpoint does not resolve before success voice"),Match->GetPhase()==EEGMatchPhase::Finished))return true;
   Step=5;return false;
  }
  if(Step==5)
  {
   // Capture has waited for multiple viewport draws after changing the text;
   // inspect the resulting layout rather than the preceding opening subtitle.
   if(!Capture(World,TEXT("FailureSuccessDialogue.png")))return false;
   const auto LineRect=Line->GetCachedGeometry().GetLayoutBoundingRect();
   const auto FrameRect=Dialogue->GetCachedGeometry().GetLayoutBoundingRect();
   const FVector2D LineSize=Line->GetCachedGeometry().GetLocalSize();
   const FVector2D Desired=Line->GetDesiredSize();
   Test->AddInfo(FString::Printf(TEXT("Success subtitle desired %.1fx%.1f allocated %.1fx%.1f"),Desired.X,Desired.Y,LineSize.X,LineSize.Y));
   if(!Test->TestTrue(TEXT("Success subtitle height fits its allocated area"),LineSize.Y>0.f&&Desired.Y<=LineSize.Y+1.f)||
      !Test->TestTrue(TEXT("Success subtitle stays within dialogue frame"),LineRect.Top>=FrameRect.Top&&LineRect.Bottom<=FrameRect.Bottom&&LineRect.Bottom>LineRect.Top))return true;
   if(!Test->TestTrue(TEXT("Success Escape handled"),KeyEscape())||
      !Test->TestTrue(TEXT("Success voice pauses"),SuccessAudio.IsValid()&&SuccessAudio->GetPlayState()==EAudioComponentPlayState::Paused)||
      !Test->TestTrue(TEXT("Success dialogue hidden for menu"),Menu->IsVisible()&&!Dialogue->IsVisible()))return true;
   PauseStarted=FPlatformTime::Seconds();Step=6;return false;
  }
  if(Step==6)
  {
   if(FPlatformTime::Seconds()-PauseStarted<.4)return false;
   if(!Test->TestTrue(TEXT("Paused success cannot restore checkpoint"),Match->GetPhase()==EEGMatchPhase::Finished)||
      !Test->TestTrue(TEXT("Same success voice remains paused"),GetVoice(Failure)==SuccessAudio.Get()&&SuccessAudio.IsValid()&&SuccessAudio->GetPlayState()==EAudioComponentPlayState::Paused)||
      !Test->TestTrue(TEXT("Resume success by Escape"),KeyEscape())||
      !Test->TestTrue(TEXT("Success dialogue resumes"),!Menu->IsVisible()&&Dialogue->IsVisible())||
      !Test->TestTrue(TEXT("Same success voice resumes"),GetVoice(Failure)==SuccessAudio.Get()&&SuccessAudio->IsPlaying()))return true;
   Step=7;return false;
  }

  return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFinalPolishUI720,"ElyGambit.Integration.FinalPolishUI720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFinalPolishUI1080,"ElyGambit.Integration.FinalPolishUI1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
#define FINAL_UI_TEST(ClassName,W,H) bool ClassName::RunTest(const FString&) { \
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender()){AddError(TEXT("Final UI needs an idle editor and rendering RHI"));return false;} \
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7"))); \
 ADD_LATENT_AUTOMATION_COMMAND(FFinalPolishStartPIE(FIntPoint(W,H))); \
 ADD_LATENT_AUTOMATION_COMMAND(FFinalPolishUI(this,FIntPoint(W,H))); \
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true; }
FINAL_UI_TEST(FFinalPolishUI720,1280,720)
FINAL_UI_TEST(FFinalPolishUI1080,1920,1080)
#undef FINAL_UI_TEST
#endif

