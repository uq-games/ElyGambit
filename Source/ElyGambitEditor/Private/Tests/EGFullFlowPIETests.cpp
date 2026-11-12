#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMemory.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Layout/WidgetPath.h"
#include "InputCoreTypes.h"
#include "Widgets/SWindow.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Game/EGOpeningGameInstance.h"
#include "Character/EGChessUnit.h"
#include "Placement/EGBoardScene.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Presentation/EGOpeningPresentation.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGOpeningWidget.h"
#include "UI/Widget/EGVictoryEndingWidget.h"
#include "UI/Widget/EGPauseMenuWidget.h"
#include "UI/Widget/EGWidgets.h"

namespace
{
class FFullFlowStartPIE final : public IAutomationLatentCommand
{
public:
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1920,1080))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(!GUnrealEd->CheckForPlayerStart())FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};
struct FFullFlowCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FFullFlowCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};
struct FFullFlowDrawWait
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 int32 Draws=0;
 ~FFullFlowDrawWait(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};
struct FFullFlowFrameSample
{
 double Seconds=0.,FrameMs=0.,ProcessMemoryMiB=-1.;
 int32 Stage=0,Phase=0,CombatUnits=0;
};
class FFullFlowNatural final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FName Route;
 int32 RequestedSeed;
 int32 Step=0,StartedStage=0,RecordedStage=0,LastReportedStage=0,LastReportedPhase=-1;
 double StartedAt=0.,Deadline=0.,LastProgress=0.,StateSince=0.;
 FString Directory,Journal;
 bool bSawLoading=false,bSkipRequested=false,bCombatPauseDone=false;
 float FrozenPreparationSeconds=0.f,FrozenBattleSeconds=0.f;
 TSharedPtr<FFullFlowCapture> Capture;
 TSharedPtr<FFullFlowDrawWait> PauseDrawWait;
 FString LastPointerDiagnostic;
 TWeakObjectPtr<UWorld> VictoryWorld;
 TWeakObjectPtr<AEGDemoGameMode> VictoryMode;
 TArray<FFullFlowFrameSample> FrameSamples;
 double PreviousUpdate=0.,NextMemorySample=0.;
 int32 PreviousSampleStage=0;
 bool bPreviousSampleEligible=false,bPerformanceWritten=false;
 void SamplePerformance(double Now,UWorld* World,AEGDemoGameMode* Mode,UEGMatchManager* Match)
 {
  // Collect only uninterrupted natural combat; preparation, screenshots,
  // automation pause/resume are deliberately excluded. Natural presentation
  // holds and shader compilation remain visible in combat frame times.
  const bool bEligible=Step==3&&World&&Mode&&Match&&!World->IsPaused()&&!Capture.IsValid()&&
   Match->GetPhase()==EEGMatchPhase::Combat&&Mode->BattleRules;
  const int32 Stage=Match?Match->GetStageNumber():0;
  if(bEligible&&bPreviousSampleEligible&&Stage==PreviousSampleStage&&PreviousUpdate>0.&&Now>PreviousUpdate)
  {
   FFullFlowFrameSample Sample;Sample.Seconds=Now-StartedAt;Sample.FrameMs=(Now-PreviousUpdate)*1000.;
   Sample.Stage=Stage;Sample.Phase=int32(Match->GetPhase());
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))++Sample.CombatUnits;
   if(Now>=NextMemorySample)
   {
    Sample.ProcessMemoryMiB=double(FPlatformMemory::GetStats().UsedPhysical)/(1024.*1024.);
    NextMemorySample=Now+1.;
   }
   FrameSamples.Add(Sample);
  }
  PreviousUpdate=Now;PreviousSampleStage=Stage;bPreviousSampleEligible=bEligible;
 }
 void WritePerformance()
 {
  if(bPerformanceWritten||Directory.IsEmpty())return;
  bPerformanceWritten=true;
  FString Csv=TEXT("elapsed_seconds,stage,phase,combat_units,frame_ms,process_used_physical_mib\n");
  for(const auto& Sample:FrameSamples)
  {
   Csv+=FString::Printf(TEXT("%.3f,%d,%d,%d,%.3f,"),Sample.Seconds,Sample.Stage,Sample.Phase,Sample.CombatUnits,Sample.FrameMs);
   if(Sample.ProcessMemoryMiB>=0.)Csv+=FString::Printf(TEXT("%.2f"),Sample.ProcessMemoryMiB);
   Csv+=TEXT("\n");
  }
  FString Summary=TEXT("PIE editor process; natural AI with programmatic normal transactions; not Shipping performance.\n");
  Summary+=TEXT("Only combat sampled; preparation, screenshots and test pauses excluded. Natural presentation holds and shader compilation are included. Frame time is wall time between eligible latent updates, not GPU time. Memory is editor process working set sampled about once per second.\n");
  Summary+=FString::Printf(TEXT("route=%s seed=%d total_samples=%d\n"),*Route.ToString(),RequestedSeed,FrameSamples.Num());
  for(int32 Stage=1;Stage<=7;++Stage)
  {
   TArray<double> Times;double Sum=0.;
   for(const auto& Sample:FrameSamples)if(Sample.Stage==Stage){Times.Add(Sample.FrameMs);Sum+=Sample.FrameMs;}
   Times.Sort();
   if(Times.IsEmpty()){Summary+=FString::Printf(TEXT("stage=%d samples=0 insufficient data\n"),Stage);continue;}
   const int32 P95=FMath::Clamp(FMath::CeilToInt(Times.Num()*.95)-1,0,Times.Num()-1);
   Summary+=FString::Printf(TEXT("stage=%d phase=combat samples=%d mean_ms=%.3f p95_ms=%.3f max_ms=%.3f%s\n"),
    Stage,Times.Num(),Sum/Times.Num(),Times[P95],Times.Last(),Times.Num()<100?TEXT(" insufficient sample count"):TEXT(""));
  }
  FFileHelper::SaveStringToFile(Csv,*(Directory/TEXT("CombatFrameSamples.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  FFileHelper::SaveStringToFile(Summary,*(Directory/TEXT("CombatFrameSummary.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
 void Log(const FString& Message)
 {
  const FString Line=FString::Printf(TEXT("%.2f %s"),FPlatformTime::Seconds()-StartedAt,*Message);
  Journal+=Line+TEXT("\n");Test->AddInfo(Line);
  FFileHelper::SaveStringToFile(Journal,*(Directory/TEXT("FullFlow.log")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
 bool Fail(const FString& Reason)
 {Capture.Reset();Log(TEXT("BLOCKED: ")+Reason);Test->AddError(Reason);WritePerformance();return true;}
 bool Snapshot(UWorld* World,const TCHAR* Name)
 {
  if(!Capture.IsValid())
  {
   auto* Client=World?World->GetGameViewport():nullptr;if(!Client||!Client->Viewport)return false;
   Capture=MakeShared<FFullFlowCapture>();Capture->Client=Client;Capture->Path=Directory/FString(Name);
   const TWeakPtr<FFullFlowCapture> Weak=Capture;
   Capture->Handle=Client->OnDrawn().AddLambda([Weak]()
   {
    auto State=Weak.Pin();if(!State.IsValid())return;++State->Draws;
    if((GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0){State->Draws=0;return;}
    if(State->Draws>=3&&!State->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
    {State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);}
   });
   return false;
  }
  if(!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto BE=[&](int32 Offset)->uint32{return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
  Test->TestEqual(TEXT("Actual PIE screenshot dimensions"),FIntPoint(BE(16),BE(20)),FIntPoint(1920,1080));
  Log(TEXT("SCREENSHOT: ")+Capture->Path);Capture.Reset();return true;
 }
 UEGOpeningWidget* Opening(UWorld* World,APlayerController* Player)const
 {
  TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UEGOpeningWidget::StaticClass(),false);
  for(auto* Candidate:Widgets)if(auto* Widget=Cast<UEGOpeningWidget>(Candidate))if(Widget->GetOwningPlayer()==Player&&Widget->IsInViewport())return Widget;
  return nullptr;
 }
 UEGPauseMenuWidget* PauseWidget(UWorld* World,APlayerController* Player)const
 {
  TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UEGPauseMenuWidget::StaticClass(),false);
  for(auto* Candidate:Widgets)if(auto* Widget=Cast<UEGPauseMenuWidget>(Candidate))if(Widget->GetOwningPlayer()==Player&&Widget->IsInViewport())return Widget;
  return nullptr;
 }
 bool PointerClick(UButton* Button)
 {
  if(!Button||!Button->IsVisible()||!Button->GetIsEnabled()){LastPointerDiagnostic=TEXT("button missing, hidden or disabled");return false;}
  auto& App=FSlateApplication::Get();const auto Target=Button->GetCachedWidget();
  if(!Target.IsValid()){LastPointerDiagnostic=TEXT("button has no cached Slate widget");return false;}
  const auto Window=App.FindWidgetWindow(Target.ToSharedRef());if(!Window.IsValid()){LastPointerDiagnostic=TEXT("button has no Slate window");return false;}
  const auto& Geometry=Button->GetCachedGeometry();if(Geometry.GetLocalSize().X<=0.f||Geometry.GetLocalSize().Y<=0.f){LastPointerDiagnostic=TEXT("button has empty geometry");return false;}
  const FVector2D Previous=App.GetCursorPos(),Point=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
  const FWidgetPath Hit=App.LocateWindowUnderMouse(Point,App.GetInteractiveTopLevelWindows(),false,0);
  FString Types;for(int32 I=0;I<Hit.Widgets.Num();++I){if(I)Types+=TEXT(" > ");Types+=Hit.Widgets[I].Widget->GetTypeAsString();}
  const bool bTargetHit=Hit.IsValid()&&Hit.ContainsWidget(Target.Get());
  LastPointerDiagnostic=FString::Printf(TEXT("button=%s pos=(%.1f,%.1f) local=(%.1f,%.1f) visible=%d enabled=%d targetHit=%d path=%s"),
   *Button->GetName(),Point.X,Point.Y,Geometry.GetLocalSize().X,Geometry.GetLocalSize().Y,Button->IsVisible()?1:0,Button->GetIsEnabled()?1:0,bTargetHit?1:0,*Types);
  if(!bTargetHit)return false;
  App.SetCursorPos(Point);App.ProcessMouseMoveEvent(FPointerEvent(0,Point,Previous,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
  const bool bDown=App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,Point,Point,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
  const bool bUp=App.ProcessMouseButtonUpEvent(FPointerEvent(0,Point,Point,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
  App.SetCursorPos(Previous);
  Log(FString::Printf(TEXT("pointer dispatched: %s down=%d up=%d"),*LastPointerDiagnostic,bDown?1:0,bUp?1:0));
  Test->TestTrue(TEXT("Pointer down and up were handled after targeting the button"),bDown&&bUp);
  return true; // A dispatched click is never retried; later state confirms its effect.
 }
 bool Escape()
 {
  auto& App=FSlateApplication::Get();
  const bool Handled=App.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
  App.ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));return Handled;
 }
 int32 HeroRank(EEGHeroKind Kind)const
 {
  const EEGHeroKind Preferred[]={EEGHeroKind::Eden,EEGHeroKind::Aponia,EEGHeroKind::VillV};
  for(int32 Index=0;Index<UE_ARRAY_COUNT(Preferred);++Index)if(Kind==Preferred[Index])return Index;
  return 100;
 }
 bool Prepare(AEGDemoGameMode* Mode)
 {
  auto* Cards=Mode->GetCards();auto* Roster=Mode->GetRoster();
  if(!Cards||!Roster)return false;
  const int32 Goal=Cards->GetRules().DeploymentLimit;
  for(int32 Attempts=0;Attempts<100;++Attempts)
  {
   bool bProgress=false;
   // Merge already deployed heroes before buying units or refreshing.
   for(const auto& Card:Cards->GetHand())
   {
    if(!Card.Definition||!Card.Definition->HeroDefinition)continue;
    const FGuid HeroId=Roster->FindHeroByDefinition(Card.Definition->HeroDefinition);FEGHeroInstance Hero;
    if(HeroId.IsValid()&&Roster->GetHero(HeroId,Hero)&&Hero.bDeployed&&Cards->TryMergeIntoHero(Card.InstanceId,HeroId))
    {Log(FString::Printf(TEXT("stage=%d merged=%s gold=%lld"),Mode->GetMatch()->GetStageNumber(),*HeroId.ToString(),Cards->GetGold()));bProgress=true;break;}
   }
   if(bProgress)continue;
   if(Cards->GetPieces().Num()<Goal)
   {
    const auto Hand=Cards->GetHand();
    for(int32 Rank=0;Rank<=101&&!bProgress;++Rank)for(const auto& Card:Hand)
    {
     if(!Card.Definition||!Card.Definition->HeroDefinition)continue;
     const auto Kind=Card.Definition->HeroDefinition->HeroKind;
     const bool bHumanPriority=Route==TEXT("OriginFlower")&&Mode->GetMatch()->GetStageNumber()==7;
     const int32 CardRank=bHumanPriority?(Kind==EEGHeroKind::ElysiaHerrscher?0:HeroRank(Kind)+1):HeroRank(Kind);
     if(CardRank!=Rank||Roster->FindHeroByDefinition(Card.Definition->HeroDefinition).IsValid())continue;
     for(int32 Y=0;Y<3&&!bProgress;++Y)for(int32 X=0;X<7&&!bProgress;++X)
      if(Cards->EvaluateHandPlacement(Card.InstanceId,{X,Y}).IsAllowed()&&Cards->TryDeployHandCard(Card.InstanceId,{X,Y}))
      {Log(FString::Printf(TEXT("stage=%d deploy=%s at=%d,%d"),Mode->GetMatch()->GetStageNumber(),*Card.Definition->GetPathName(),X,Y));bProgress=true;}
    }
    if(bProgress)continue;
   }
   const bool bNeedPopulation=Cards->GetPieces().Num()<Goal;
   if(Cards->GetGold()>=Cards->GetRules().PurchasePrice)
   {
    const auto Shop=Cards->GetShop();
    for(int32 Rank=0;Rank<=100&&!bProgress;++Rank)for(int32 Slot=0;Slot<Shop.Num()&&!bProgress;++Slot)
    {
     auto* Def=Shop[Slot];if(!Def||!Def->HeroDefinition||HeroRank(Def->HeroDefinition->HeroKind)!=Rank)continue;
     const FGuid HeroId=Roster->FindHeroByDefinition(Def->HeroDefinition);FEGHeroInstance Hero;
     const bool bDeployed=HeroId.IsValid()&&Roster->GetHero(HeroId,Hero)&&Hero.bDeployed;
     const bool bInHand=Cards->GetHand().ContainsByPredicate([&](const FEGCardInstance& Card){return Card.Definition==Def;});
     if((bNeedPopulation&&(HeroId.IsValid()||bInHand))||(!bNeedPopulation&&!bDeployed))continue;
     if(Cards->BuyShopSlotExpected(Slot,Cards->GetShopSlotRevision(Slot)))
     {Log(FString::Printf(TEXT("stage=%d buy=%s slot=%d gold=%lld"),Mode->GetMatch()->GetStageNumber(),*Def->GetPathName(),Slot,Cards->GetGold()));bProgress=true;}
    }
   }
   if(bProgress)continue;
   const int32 Cost=Cards->GetRefreshCost();
   if(Cards->GetGold()<Cost+Cards->GetRules().PurchasePrice||!Cards->RefreshShop())break;
   Log(FString::Printf(TEXT("stage=%d refresh cost=%d gold=%lld"),Mode->GetMatch()->GetStageNumber(),Cost,Cards->GetGold()));
  }
  for(const auto& Hero:Roster->GetHeroes())if(Hero.bDeployed&&Hero.Definition)
   Log(FString::Printf(TEXT("stage=%d formation=%s level=%d cell=%d,%d"),Mode->GetMatch()->GetStageNumber(),*Hero.Definition->GetPathName(),Hero.Level,Hero.DeploymentCell.X,Hero.DeploymentCell.Y));
  Log(FString::Printf(TEXT("stage=%d enter gold=%lld hand=%d pieces=%d"),Mode->GetMatch()->GetStageNumber(),Cards->GetGold(),Cards->GetHand().Num(),Cards->GetPieces().Num()));
  return Mode->EnterCombat();
 }
public:
 FFullFlowNatural(FAutomationTestBase* InTest,FName InRoute,int32 InSeed):Test(InTest),Route(InRoute),RequestedSeed(InSeed){}
 ~FFullFlowNatural(){WritePerformance();}
 bool Update() override
 {
  const double Now=FPlatformTime::Seconds();
  if(StartedAt==0.)
  {
   StartedAt=LastProgress=StateSince=Now;Deadline=Now+1200.;
   Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("FullFlow20261110")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
   IFileManager::Get().MakeDirectory(*Directory,true);
   Log(FString::Printf(TEXT("route=%s requested_seed=%d; natural AI, no injected resources/outcomes; viewport 1920x1080"),*Route.ToString(),RequestedSeed));
  }
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Player=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=Player?Cast<AEGHUD>(Player->GetHUD()):nullptr;
  auto* Match=Mode?Mode->GetMatch():nullptr;
  SamplePerformance(Now,World,Mode,Match);
  if(Now>Deadline)return Fail(FString::Printf(TEXT("20 minute limit reached: stage=%d step=%d"),Match?Match->GetStageNumber():0,Step));
  if(Now-LastProgress>180.)return Fail(FString::Printf(TEXT("No progress for three minutes: stage=%d step=%d"),Match?Match->GetStageNumber():0,Step));
  if(!World||!Mode||!Player||!HUD||!Match||!Mode->GetCards()||!Mode->GetRoster())return false;
  auto* Presentation=Mode->FindComponentByClass<UEGOpeningPresentation>();
  auto* Widget=Opening(World,Player);
  if(Step==0)
  {
   if(!Presentation||!Widget||!Presentation->IsAwaitingStart())return false;
   if(!Test->TestNotNull(TEXT("Formal loading game instance"),Cast<UEGOpeningGameInstance>(World->GetGameInstance()))||
      !Test->TestTrue(TEXT("Menu bootstrap entered formal opening gate"),Mode->IsPresentationInputBlocked())||
      !Test->TestFalse(TEXT("No combat before starting"),Match->HasStarted()))return true;
   auto* Start=Cast<UButton>(Widget->GetWidgetFromName(TEXT("OpeningStart")));if(!Start||!Start->IsVisible())return false;
   if(!Snapshot(World,TEXT("01_Opening.png")))return false;
   Log(TEXT("opening displayed"));Start->OnClicked.Broadcast();
   if(!Test->TestTrue(TEXT("Start enters preparation"),Presentation->IsPreparingAfterStart()))return true;
   Step=1;LastProgress=StateSince=Now;return false;
  }
  if(Step==1)
  {
   if(Widget)if(auto* Status=Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("LoadingStatus"))))
    if(Status->IsVisible()&&Status->GetText().ToString().Contains(TEXT("加载")))bSawLoading=true;
   if(Presentation&&!Presentation->GetLoadingError().IsEmpty())return Fail(TEXT("Opening/loading error: ")+Presentation->GetLoadingError());
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(RequestedSeed>=0&&!Test->TestEqual(TEXT("Requested campaign seed after StartGame"),Mode->GetBalanceSeed(),RequestedSeed))return true;
   Log(FString::Printf(TEXT("campaign seed=%d"),Mode->GetBalanceSeed()));
   Log(FString::Printf(TEXT("opening ready; loading indicator observed=%d (hot loading can complete between frames)"),bSawLoading?1:0));
   if(!Test->TestEqual(TEXT("Initial cards issued once"),Mode->GetCards()->GetHand().Num(),2))return true;
   Step=2;LastProgress=StateSince=Now;return false;
  }
  if(Step==2)
  {
   if(!Snapshot(World,TEXT("02_Preparation.png")))return false;
   auto* Main=HUD->GetMainWidget();auto* Button=Main?Cast<UButton>(Main->GetWidgetFromName(TEXT("Button_PauseMenu"))):nullptr;
   if(!Test->TestNotNull(TEXT("Preparation pause button"),Button))return true;
   if(!PointerClick(Button)){if(Now-StateSince>8.)return Fail(TEXT("Pause HUD button was not hit-test ready: ")+LastPointerDiagnostic);return false;}
   Step=10;LastProgress=StateSince=Now;return false;
  }
  if(Step==10)
  {
   const auto* Controller=Cast<AEGBoardPlayerController>(Player);
   if(!Controller||!Controller->IsPauseMenuOpen()||!World->IsPaused())
   {if(Now-StateSince>5.)return Fail(TEXT("Pause button did not open the pause menu"));return false;}
   const auto Clock=Mode->GetPhaseClock();
   if(!Test->TestTrue(TEXT("Preparation has a countdown"),Clock.bHasCountdown))return true;
   FrozenPreparationSeconds=Clock.RemainingRealSeconds;Log(TEXT("preparation paused through the HUD button"));
   Step=11;StateSince=Now;return false;
  }
  if(Step==11)
  {
   if(Now-StateSince<1.2)return false;
   Test->TestTrue(TEXT("Preparation countdown remains frozen while paused"),FMath::IsNearlyEqual(FrozenPreparationSeconds,Mode->GetPhaseClock().RemainingRealSeconds,.15f));
   auto* Pause=PauseWidget(World,Player);auto* Continue=Pause?Cast<UButton>(Pause->GetWidgetFromName(TEXT("PauseContinue"))):nullptr;
   if(!Test->TestNotNull(TEXT("Pause continue button"),Continue))return true;
   if(!PointerClick(Continue)){if(Now-StateSince>8.)return Fail(TEXT("Continue button was not hit-test ready: ")+LastPointerDiagnostic);return false;}
   Step=12;StateSince=Now;return false;
  }
  if(Step==12)
  {
   if(World->IsPaused()){if(Now-StateSince>5.)return Fail(TEXT("Continue did not release the pause"));return false;}
   if(Now-StateSince<1.)return false;
   Test->TestTrue(TEXT("Preparation countdown resumes after continue"),Mode->GetPhaseClock().RemainingRealSeconds<FrozenPreparationSeconds-.25f);
   Log(TEXT("preparation resumed and countdown advanced"));Step=3;LastProgress=Now;return false;
  }
  if(Step==20)
  {
   const auto* Controller=Cast<AEGBoardPlayerController>(Player);
   if(!Controller||!Controller->IsPauseMenuOpen()||!World->IsPaused())
   {if(Now-StateSince>5.)return Fail(TEXT("Escape did not pause combat"));return false;}
   FrozenBattleSeconds=Mode->GetBattleSeconds();Log(TEXT("combat paused with synthetic Escape"));Step=21;StateSince=Now;return false;
  }
  if(Step==21)
  {
   if(Now-StateSince<1.2)return false;
   Test->TestTrue(TEXT("Battle clock remains frozen while paused"),FMath::IsNearlyEqual(FrozenBattleSeconds,Mode->GetBattleSeconds(),.1f));
   if(!Escape())return Fail(TEXT("Escape did not reach the pause menu"));
   Step=22;StateSince=Now;return false;
  }
  if(Step==22)
  {
   if(World->IsPaused()){if(Now-StateSince>5.)return Fail(TEXT("Escape did not resume combat"));return false;}
   if(Now-StateSince<.6)return false;
   Test->TestTrue(TEXT("Battle clock resumes after Escape"),Mode->GetBattleSeconds()>FrozenBattleSeconds+.1f);
   Log(TEXT("combat resumed and clock advanced"));bCombatPauseDone=true;Step=3;LastProgress=Now;return false;
  }
  if(Step==4)
  {
   if(!Match->DidWinRun())
   {
    if(!Snapshot(World,TEXT("03_NaturalDefeat.png")))return false;
    Log(FString::Printf(TEXT("OUTCOME=natural_defeat stage=%d corruption=%d; full victory route not established"),Match->GetStageNumber(),Match->GetCorruption()));
    Test->AddWarning(TEXT("Natural policy lost; not a product failure, but a full victory playthrough is not established."));return true;
   }
   TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UEGVictoryEndingWidget::StaticClass(),false);
   bool bEndingInViewport=false;
   for(auto* Entry:Widgets)if(auto* Ending=Cast<UEGVictoryEndingWidget>(Entry))if(Ending->IsInViewport())
   {
    bEndingInViewport=true;
    if(Ending->WidgetTree)
    {
     UButton* Skip=nullptr;Ending->WidgetTree->ForEachWidget([&](UWidget* Child){if(auto* Button=Cast<UButton>(Child))if(Button->OnClicked.Contains(Ending,TEXT("SkipAll")))Skip=Button;});
     if(Skip&&Skip->IsVisible()&&!bSkipRequested){Skip->OnClicked.Broadcast();bSkipRequested=true;LastProgress=Now;Log(TEXT("Victory ending: invoked SkipAll via its UI button"));}
    }
   }
   if(bEndingInViewport||HUD->ShouldHideFinalResult())return false;
   if(!bSkipRequested)return Fail(TEXT("Victory result became visible without observing the victory-ending UI"));
   Step=5;LastProgress=StateSince=Now;return false;
  }
  if(Step==5)
  {
   TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UEGVictoryEndingWidget::StaticClass(),false);
   for(auto* Entry:Widgets)if(Entry&&Entry->IsInViewport())return Fail(TEXT("Victory ending reappeared before result capture"));
   if(HUD->ShouldHideFinalResult()){Step=4;StateSince=Now;return false;}
   if(Now-StateSince<.4)return false;
   if(!Snapshot(World,TEXT("03_NaturalVictory.png")))return false;
   Log(FString::Printf(TEXT("OUTCOME=natural_victory stage=%d route=%s corruption=%d"),Match->GetStageNumber(),*Route.ToString(),Match->GetCorruption()));
   if(!Test->TestEqual(TEXT("Final victory belongs to stage seven"),Match->GetStageNumber(),7))return true;
   VictoryWorld=World;VictoryMode=Mode;
   if(!Escape())return Fail(TEXT("Escape did not reach the victory result viewport"));
   auto* DrawClient=World->GetGameViewport();if(!DrawClient)return Fail(TEXT("Victory pause viewport unavailable"));
   PauseDrawWait=MakeShared<FFullFlowDrawWait>();PauseDrawWait->Client=DrawClient;
   const TWeakPtr<FFullFlowDrawWait> WeakDraw=PauseDrawWait;
   PauseDrawWait->Handle=DrawClient->OnDrawn().AddLambda([WeakDraw](){if(auto State=WeakDraw.Pin())++State->Draws;});
   Step=30;LastProgress=StateSince=Now;return false;
  }
  if(Step==30)
  {
   const auto* Controller=Cast<AEGBoardPlayerController>(Player);
   if(!Controller||!Controller->IsPauseMenuOpen()||!World->IsPaused())
   {if(Now-StateSince>5.)return Fail(TEXT("Escape did not open pause menu on victory result"));return false;}
   if(!PauseDrawWait.IsValid()||PauseDrawWait->Draws<2)
   {if(Now-StateSince>8.)return Fail(TEXT("Pause menu did not receive two viewport draws"));return false;}
   auto* Pause=PauseWidget(World,Player);auto* Return=Pause?Cast<UButton>(Pause->GetWidgetFromName(TEXT("PauseReturnMenu"))):nullptr;
   if(!Return)return Fail(TEXT("Pause return-to-menu button missing"));
   if(!PointerClick(Return)){if(Now-StateSince>8.)return Fail(TEXT("Return-to-menu button was not hit-test ready: ")+LastPointerDiagnostic);return false;}
   Log(FString::Printf(TEXT("return-to-menu dispatched, paused=%d menuOpen=%d viewportDraws=%d"),World->IsPaused()?1:0,Controller->IsPauseMenuOpen()?1:0,PauseDrawWait->Draws));
   PauseDrawWait.Reset();Step=31;LastProgress=StateSince=Now;return false;
  }
  if(Step==31)
  {
   if(World==VictoryWorld.Get()||Mode==VictoryMode.Get()||!Presentation||!Widget||!Presentation->IsAwaitingStart())
   {if(Now-StateSince>30.)return Fail(FString::Printf(TEXT("Return-to-menu did not reach a new opening world; sameWorld=%d sameMode=%d paused=%d; %s"),World==VictoryWorld.Get()?1:0,Mode==VictoryMode.Get()?1:0,World->IsPaused()?1:0,*LastPointerDiagnostic));return false;}
   if(!Test->TestTrue(TEXT("Returned to a new game world"),World!=VictoryWorld.Get())||
      !Test->TestTrue(TEXT("Returned to a new game mode"),Mode!=VictoryMode.Get())||
      !Test->TestTrue(TEXT("Opening gate blocks input before restarting"),Mode->IsPresentationInputBlocked())||
      !Test->TestFalse(TEXT("New run has not started at the opening"),Match->HasStarted()))return true;
   if(!Snapshot(World,TEXT("04_ReturnedOpening.png")))return false;
   auto* Start=Cast<UButton>(Widget->GetWidgetFromName(TEXT("OpeningStart")));
   if(!PointerClick(Start)){if(Now-StateSince>30.)return Fail(TEXT("Restart opening start button was not hit-test ready: ")+LastPointerDiagnostic);return false;}
   Log(TEXT("returned to opening and clicked start with synthetic Slate pointer"));Step=32;LastProgress=StateSince=Now;return false;
  }
  if(Step==32)
  {
   if(Presentation&&!Presentation->GetLoadingError().IsEmpty())return Fail(TEXT("Restart loading error: ")+Presentation->GetLoadingError());
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Run=Mode->RunDefinition.Get();auto* Cards=Mode->GetCards();auto* Roster=Mode->GetRoster();
   if(!Test->TestNotNull(TEXT("Restart run definition"),Run))return true;
   Test->TestEqual(TEXT("Restart begins at stage one"),Match->GetStageNumber(),1);
   Test->TestEqual(TEXT("Restart gold matches formal configuration"),Cards->GetGold(),int64(Run->StartingGold));
   Test->TestEqual(TEXT("Restart hand has two cards"),Cards->GetHand().Num(),2);
   Test->TestEqual(TEXT("Restart has no deployed pieces"),Cards->GetPieces().Num(),0);
   if(!Run->bUseExplicitStartingCards)
   {
    for(const auto& Card:Cards->GetHand())Test->TestEqual(TEXT("Restart random opening card level"),Card.InitialLevel,1);
    for(const auto& Hero:Roster->GetHeroes())Test->TestEqual(TEXT("Restart hero level"),Hero.Level,1);
   }
   for(const auto& Hero:Roster->GetHeroes())Test->TestFalse(TEXT("Restart hero is not deployed"),Hero.bDeployed);
   Test->TestEqual(TEXT("Restart corruption matches formal configuration"),Match->GetCorruption(),FMath::Clamp(Run->StartingCorruption,0,200));
   Test->TestEqual(TEXT("Restart has no owned stigmata"),Match->GetStigmataOwned().Num(),0);
   Test->TestTrue(TEXT("Restart has no endgame route"),Match->GetEndgameRoute().IsNone());
   if(!Snapshot(World,TEXT("05_RestartPreparation.png")))return false;
   Log(FString::Printf(TEXT("ROUND_TRIP=checked stage=%d gold=%lld hand=%d pieces=%d corruption=%d stigmata=%d route=%s"),
    Match->GetStageNumber(),Cards->GetGold(),Cards->GetHand().Num(),Cards->GetPieces().Num(),Match->GetCorruption(),Match->GetStigmataOwned().Num(),*Match->GetEndgameRoute().ToString()));
   return true;
  }
  const int32 Stage=Match->GetStageNumber();const EEGMatchPhase Phase=Match->GetPhase();
  if(Stage>1&&!bCombatPauseDone)return Fail(TEXT("First combat ended without exercising the pause/resume path"));
  if(Stage!=LastReportedStage||int32(Phase)!=LastReportedPhase)
  {
   LastReportedStage=Stage;LastReportedPhase=int32(Phase);LastProgress=Now;
   Log(FString::Printf(TEXT("stage=%d phase=%d corruption=%d gold=%lld"),Stage,int32(Phase),Match->GetCorruption(),Mode->GetCards()->GetGold()));
  }
  if(StartedStage>RecordedStage&&Phase!=EEGMatchPhase::Preparation&&Phase!=EEGMatchPhase::Combat)
  {RecordedStage=StartedStage;Log(FString::Printf(TEXT("stage=%d natural_result=%s"),RecordedStage,Mode->WasVictory()?TEXT("victory"):TEXT("defeat")));}
  if(Phase==EEGMatchPhase::Preparation)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady()||StartedStage==Stage)return false;
   if(!Prepare(Mode))return Fail(FString::Printf(TEXT("Could not enter natural combat at stage %d"),Stage));
   StartedStage=Stage;LastProgress=Now;return false;
  }
  if(Phase==EEGMatchPhase::Combat&&Stage==1&&!bCombatPauseDone&&Mode->BattleRules&&Mode->BattleRules->IsCombatRunning())
  {
   if(!Escape())return Fail(TEXT("Escape did not reach the combat viewport"));
   Step=20;StateSince=Now;return false;
  }
  if(Phase==EEGMatchPhase::Reward)
  {
   const int32 Revision=Mode->GetStigmataRevision();
   if(Mode->IsChoosingEndgameRoute())
   {
    if(!Mode->ChooseEndgameRoute(Route,Revision))return Fail(TEXT("Final route transaction was rejected"));
    Log(TEXT("final route selected=")+Route.ToString());LastProgress=Now;return false;
   }
   const auto Offer=Mode->GetStigmataOffer();if(Offer.IsEmpty())return false;
   const FName Preferred[]={TEXT("Deliverance"),TEXT("Gold"),TEXT("Helix"),TEXT("Flawless")};
   FName Choice=Offer[0];for(const FName Name:Preferred)if(Offer.Contains(Name)){Choice=Name;break;}
   if(!Mode->ChooseStigmata(Choice,Revision))return Fail(TEXT("Ordinary reward transaction was rejected"));
   Log(FString::Printf(TEXT("stage=%d reward=%s"),Stage,*Choice.ToString()));LastProgress=Now;return false;
  }
  if(Phase==EEGMatchPhase::Finished){Step=4;LastProgress=StateSince=Now;return false;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFullFlowEntryConfiguration,"ElyGambit.Integration.FullFlow.EntryMapConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFullFlowEntryConfiguration::RunTest(const FString&)
{
 FString GameMap,EditorMap,GameInstance;
 if(!GConfig||!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),GameMap,GEngineIni)||
    !GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("EditorStartupMap"),EditorMap,GEngineIni))
 {AddError(TEXT("Missing configured game/editor entry map"));return false;}
 TestEqual(TEXT("Game begins from formal menu"),GameMap,FString(TEXT("/Game/Assets/Maps/Gameplay/L_ElyMainMenu")));
 TestEqual(TEXT("Opening the project in editor uses the same formal entry"),EditorMap,GameMap);
 if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameInstanceClass"),GameInstance,GEngineIni))
 {AddError(TEXT("Missing configured loading game instance"));return false;}
 TestEqual(TEXT("Formal loading game instance is bound"),GameInstance,FString(TEXT("/Script/ElyGambit.EGOpeningGameInstance")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFullFlowNaturalPIE,"ElyGambit.Integration.FullFlow.Natural",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFullFlowNaturalPIE::RunTest(const FString&)
{
 if(!GEditor||!GUnrealEd||GEditor->PlayWorld||!FApp::CanEverRender()){AddError(TEXT("Requires an idle graphical editor"));return false;}
 FString DefaultMap,RouteString=TEXT("Unity");int32 Seed=-1;
 if(!GConfig||!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),DefaultMap,GEngineIni)||DefaultMap.IsEmpty())
 {AddError(TEXT("Missing formal default game map"));return false;}
 FParse::Value(FCommandLine::Get(),TEXT("EGBalanceRoute="),RouteString);
 if(RouteString!=TEXT("Unity")&&RouteString!=TEXT("Story")&&RouteString!=TEXT("OriginFlower")){AddError(TEXT("Unknown EGBalanceRoute"));return false;}
 FParse::Value(FCommandLine::Get(),TEXT("EGBalanceSeed="),Seed);
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(DefaultMap));
 ADD_LATENT_AUTOMATION_COMMAND(FFullFlowStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FFullFlowNatural(this,FName(*RouteString),Seed));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
