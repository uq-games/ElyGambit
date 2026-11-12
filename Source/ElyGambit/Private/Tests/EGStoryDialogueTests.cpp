#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Sound/SoundWave.h"
#include "HAL/PlatformProcess.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStoryDialogueBindingsTest,"ElyGambit.StoryDialogue.SelectedBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStoryDialogueBindingsTest::RunTest(const FString&)
{
 const auto* Audio=GetDefault<UEGGameAudioComponent>();
 if(!TestNotNull(TEXT("Configured audio defaults"),Audio))return false;
 struct FExpected {int32 Stage;bool bPhaseTwo;const TCHAR* Asset;const TCHAR* Text;float Seconds;float Start;};
 const FExpected Expected[]={
  {3,false,TEXT("SW_Corruption_Stage07_01"),TEXT("我绝不会放过侵蚀你的机会。"),2.376f,.30f},
  {6,false,TEXT("SW_Corruption_Stage03_01"),TEXT("力量上的差距依然是无法弥补的，不是吗？"),4.981f,0.f},
  {7,false,TEXT("SW_Corruption_Stage06_02"),TEXT("永远不要挑战「无法战胜」的敌人……这才是生存的智慧，不是吗？"),7.085f,0.f},
  {7,true,TEXT("SW_Corruption_Stage06_01"),TEXT("提前声明，这一次，我不会手下留情了哦。"),4.8f,0.f}
 };
 TestEqual(TEXT("Exactly four explicitly selected story records"),Audio->SelectedStoryLines.Num(),4);
 TestTrue(TEXT("Legacy random story pool remains disabled"),Audio->StageDialogueLines.IsEmpty());
 TSet<FSoftObjectPath> Seen;
 for(const auto& Item:Expected)
 {
  const FEGMasterVoiceLine Line=Audio->GetStoryDialogue(Item.Stage,Item.bPhaseTwo);
  const FString Name=FString::Printf(TEXT("stage %d phase two %d"),Item.Stage,Item.bPhaseTwo);
  TestEqual(*(Name+TEXT(" speaker")),Line.Speaker.ToString(),FString(TEXT("侵蚀之律者")));
  TestEqual(*(Name+TEXT(" subtitle")),Line.Text.ToString(),FString(Item.Text));
  const FString Path=FString::Printf(TEXT("/Game/Assets/Shared/Audio/Elysian20261104/CorruptionStory20261106/%s.%s"),Item.Asset,Item.Asset);
  TestEqual(*(Name+TEXT(" asset path")),Line.Sound.ToSoftObjectPath().ToString(),Path);
  TestFalse(*(Name+TEXT(" asset is distinct")),Seen.Contains(Line.Sound.ToSoftObjectPath()));
  Seen.Add(Line.Sound.ToSoftObjectPath());
  TestTrue(*(Name+TEXT(" configured fallback duration")),FMath::IsNearlyEqual(Line.DurationSeconds,Item.Seconds,.03f));
  TestTrue(*(Name+TEXT(" configured start offset")),FMath::IsNearlyEqual(Line.StartOffsetSeconds,Item.Start,.001f));
  auto* Wave=Cast<USoundWave>(Line.Sound.LoadSynchronous());
  if(TestNotNull(*(Name+TEXT(" SoundWave loads")),Wave))
  {
   TestFalse(*(Name+TEXT(" wave does not loop")),bool(Wave->bLooping));
   TestTrue(*(Name+TEXT(" unmodified wave duration")),FMath::IsNearlyEqual(Wave->Duration,Item.Seconds+Item.Start,.05f));
  }
 }
 TestTrue(TEXT("Unselected ordinary stage has no story subtitle"),Audio->GetStoryDialogue(1,false).Text.IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStoryDialogueLifecycleTest,"ElyGambit.StoryDialogue.Lifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStoryDialogueLifecycleTest::RunTest(const FString&)
{
 if(!GEngine)return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();
 if(!TestNotNull(TEXT("Standalone world"),World))return false;
 ON_SCOPE_EXIT
 {
  World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
  Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 };
 World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);World->InitializeActorsForPlay(URL);
 auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
 if(!TestNotNull(TEXT("Demo game mode"),Mode)||!TestNotNull(TEXT("Authoritative match"),Mode->GetMatch())||!TestNotNull(TEXT("Battle context"),Mode->BattleRules.Get()))return false;
 auto* Match=Mode->GetMatch();auto* Battle=Mode->BattleRules.Get();
 if(!TestTrue(TEXT("Configure seven stages"),Match->ConfigureRun(7,false)))return false;
 APlayerController* Controller=World->SpawnActor<APlayerController>();
 if(!TestNotNull(TEXT("Local controller"),Controller))return false;
 TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));Controller->SetPlayer(LocalPlayer.Get());
 if(!TestTrue(TEXT("Controller is local"),Controller->IsLocalController()))return false;
 if(!Controller->PlayerState)Controller->SetPlayerState(World->SpawnActor<APlayerState>());
 if(!TestNotNull(TEXT("Controller has a pause owner state"),Controller->PlayerState.Get()))return false;
 UEGGameAudioComponent* Audio=NewObject<UEGGameAudioComponent>(Controller);
 // Exercise the actual selected subtitle code path with missing SoundWaves:
 // no audio device, async streaming or synthetic sound-completion callback.
 // Shorten only fixture durations; production defaults are verified separately.
 for(auto& Entry:Audio->SelectedStoryLines){Entry.Line.Sound.Reset();Entry.Line.DurationSeconds=Entry.StageNumber==6?1.f:.5f;}
 Audio->Bindings.Reset();Audio->ElysiaVictoryVoices.Reset();Audio->ElysiaDefeatVoices.Reset();
 Audio->CorruptionVictoryVoices.Reset();Audio->CorruptionDefeatVoices.Reset();Audio->StageDialogueLines.Reset();
 Audio->RegisterComponent();if(!Controller->HasActorBegunPlay())Controller->DispatchBeginPlay();
 Audio->InitializeForMatch(Match);
 int32 Started=0,Ended=0;FString LastText;bool bLastMissingSound=false;
 const FDelegateHandle StartedHandle=Audio->OnResultDialogueStarted.AddLambda([&](const FEGMasterVoiceLine& Line)
 {++Started;LastText=Line.Text.ToString();bLastMissingSound=Line.Sound.IsNull();});
 const FDelegateHandle EndedHandle=Audio->OnResultDialogueEnded.AddLambda([&](){++Ended;});
 ON_SCOPE_EXIT {Audio->OnResultDialogueStarted.Remove(StartedHandle);Audio->OnResultDialogueEnded.Remove(EndedHandle);};
 auto WaitAndTick=[&](float Seconds)
 {
  FPlatformProcess::SleepNoStats(Seconds);
  Audio->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
 };
 if(!TestTrue(TEXT("Start run"),Match->StartRun()))return false;
 TestFalse(TEXT("Dialogue cannot start during preparation"),Audio->StartStoryDialogue(3));
 for(int32 Stage=1;Stage<=2;++Stage)
 {
  if(!TestTrue(TEXT("Enter earlier stage"),Match->EnterCombat())||!TestTrue(TEXT("Resolve earlier stage"),Match->ResolveCombat(true))||!TestTrue(TEXT("Advance earlier stage"),Match->NextStage()))return false;
 }
 if(!TestEqual(TEXT("Third stage"),Match->GetStageNumber(),3)||!TestTrue(TEXT("Enter third combat"),Match->EnterCombat()))return false;
 Battle->Start({});Battle->CompleteOpeningForTests();
 if(!TestTrue(TEXT("Combat is running before story hold"),Battle->IsCombatRunning()))return false;
 TestFalse(TEXT("Wrong stage cannot start a story line"),Audio->StartStoryDialogue(6));
 if(!TestTrue(TEXT("Missing sound still starts selected subtitle"),Audio->StartStoryDialogue(3)))return false;
 TestTrue(TEXT("Subtitle has no loaded sound"),bLastMissingSound);
 TestEqual(TEXT("Third stage subtitle is delivered"),LastText,FString(TEXT("我绝不会放过侵蚀你的机会。")));
 TestTrue(TEXT("Story holds combat"),Battle->HasPresentationHold());
 TestFalse(TEXT("Battle waits during story dialogue"),Battle->IsCombatRunning());
 TestTrue(TEXT("Story pauses world time"),World->IsPaused());
 TestTrue(TEXT("Audio component ticks while paused"),Audio->PrimaryComponentTick.bTickEvenWhenPaused);
 TestNotNull(TEXT("Active dialogue is queryable"),Audio->GetActiveResultDialogue());
 Audio->SkipStoryDialogue();
 TestTrue(TEXT("Skip resumes combat"),Battle->IsCombatRunning());
 TestFalse(TEXT("Skip leaves no story hold"),Battle->HasPresentationHold());
 TestFalse(TEXT("Skip leaves world running"),World->IsPaused());
 TestNull(TEXT("Skip clears subtitle"),Audio->GetActiveResultDialogue());
 TestEqual(TEXT("Skip emits one end event"),Ended,1);
 Audio->SkipStoryDialogue();TestEqual(TEXT("Repeated skip is idempotent"),Ended,1);

 // Real monotonic time crosses the first line's deadline after replacement,
 // but stays before the replacement deadline. No fake game-time clock is used.
 if(!TestTrue(TEXT("Start an old line"),Audio->StartStoryDialogue(3)))return false;
 WaitAndTick(.45f);Audio->SkipStoryDialogue();
 if(!TestTrue(TEXT("Start a replacement line"),Audio->StartStoryDialogue(3)))return false;
 WaitAndTick(.30f);
 TestTrue(TEXT("Old deadline does not end replacement"),Audio->IsStoryDialoguePlaying()&&Battle->HasPresentationHold());
 WaitAndTick(.40f);
 TestFalse(TEXT("Missing sound completes on real-time deadline"),Audio->IsStoryDialoguePlaying());
 TestTrue(TEXT("Fallback completion resumes combat"),Battle->IsCombatRunning());
 TestFalse(TEXT("Fallback completion unpauses world time"),World->IsPaused());

 if(!TestTrue(TEXT("Start third-stage line before leaving combat"),Audio->StartStoryDialogue(3)))return false;
 if(!TestTrue(TEXT("Resolve while dialogue is active"),Match->ResolveCombat(true)))return false;
 TestFalse(TEXT("Leaving combat clears story"),Audio->IsStoryDialoguePlaying());
 TestFalse(TEXT("Leaving combat clears holds"),Battle->HasPresentationHold());
 TestNull(TEXT("Leaving combat clears subtitle"),Audio->GetActiveResultDialogue());
 if(!TestTrue(TEXT("Return to next preparation"),Match->NextStage()))return false;
 for(int32 Stage=4;Stage<=5;++Stage)
 {
  if(!TestTrue(TEXT("Enter intervening stage"),Match->EnterCombat())||!TestTrue(TEXT("Resolve intervening stage"),Match->ResolveCombat(true))||!TestTrue(TEXT("Advance intervening stage"),Match->NextStage()))return false;
 }
 if(!TestEqual(TEXT("Sixth stage"),Match->GetStageNumber(),6)||!TestTrue(TEXT("Enter sixth combat"),Match->EnterCombat()))return false;
 Battle->Start({});Battle->CompleteOpeningForTests();
 if(!TestTrue(TEXT("Start sixth-stage story"),Audio->StartStoryDialogue(6)))return false;
 WaitAndTick(.72f);
 TestTrue(TEXT("Old battle callback cannot end new story"),Audio->IsStoryDialoguePlaying()&&Battle->HasPresentationHold());
 TestEqual(TEXT("New battle displays its own subtitle"),LastText,FString(TEXT("力量上的差距依然是无法弥补的，不是吗？")));
 WaitAndTick(.60f);
 TestFalse(TEXT("Sixth stage ends on its own real-time deadline"),Audio->IsStoryDialoguePlaying());
 TestTrue(TEXT("Sixth stage resumes after timeout"),Battle->IsCombatRunning());
 if(!TestTrue(TEXT("Finish sixth stage"),Match->ResolveCombat(true))||!TestTrue(TEXT("Advance to boss"),Match->NextStage())||!TestTrue(TEXT("Enter boss combat"),Match->EnterCombat()))return false;
 Battle->Start({});Battle->CompleteOpeningForTests();
 if(!TestTrue(TEXT("Start phase-two story"),Audio->StartStoryDialogue(7,true)))return false;
 TestEqual(TEXT("Phase-two subtitle selected"),LastText,FString(TEXT("提前声明，这一次，我不会手下留情了哦。")));
 if(!TestTrue(TEXT("Finish run while phase-two story active"),Match->ResolveCombat(true)))return false;
 TestFalse(TEXT("Finished run has no active story"),Audio->IsStoryDialoguePlaying());
 TestFalse(TEXT("Finished run has no story hold"),Battle->HasPresentationHold());
 TestNull(TEXT("Finished run has no story subtitle"),Audio->GetActiveResultDialogue());
 TestTrue(TEXT("Several production starts were observed"),Started>=5);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStoryDialoguePauseTest,"ElyGambit.StoryDialogue.StoryPause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStoryDialoguePauseTest::RunTest(const FString&)
{
 if(!GEngine)return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 if(!TestNotNull(TEXT("Standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);World->InitializeActorsForPlay(URL);
 auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
 if(!TestNotNull(TEXT("Demo mode"),Mode)||!TestNotNull(TEXT("Match"),Mode->GetMatch())||!TestNotNull(TEXT("Battle"),Mode->BattleRules.Get()))return false;
 auto* Match=Mode->GetMatch();auto* Battle=Mode->BattleRules.Get();
 if(!TestTrue(TEXT("Configure run"),Match->ConfigureRun(7,false)))return false;
 auto* Controller=World->SpawnActor<APlayerController>();if(!TestNotNull(TEXT("Controller"),Controller))return false;
 TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));Controller->SetPlayer(LocalPlayer.Get());
 if(!Controller->PlayerState)Controller->SetPlayerState(World->SpawnActor<APlayerState>());
 if(!TestTrue(TEXT("Controller can own pauses"),Controller->IsLocalController()&&Controller->PlayerState!=nullptr))return false;
 auto* Audio=NewObject<UEGGameAudioComponent>(Controller);
 for(auto& Entry:Audio->SelectedStoryLines){Entry.Line.Sound.Reset();Entry.Line.DurationSeconds=.5f;}
 Audio->Bindings.Reset();Audio->StageDialogueLines.Reset();Audio->RegisterComponent();
 if(!Controller->HasActorBegunPlay())Controller->DispatchBeginPlay();Audio->InitializeForMatch(Match);
 auto WaitAndTick=[&](float Seconds){FPlatformProcess::SleepNoStats(Seconds);Audio->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);};
 if(!TestTrue(TEXT("Start run"),Match->StartRun()))return false;
 for(int32 Stage=1;Stage<=2;++Stage)
  if(!Match->EnterCombat()||!Match->ResolveCombat(true)||!Match->NextStage()){AddError(TEXT("Unable to advance to elite opening"));return false;}
 if(!TestTrue(TEXT("Enter elite combat"),Match->EnterCombat()))return false;
 Battle->Start({});
 TestFalse(TEXT("Opening has not started combat clock"),Battle->IsCombatRunning());
 if(!TestTrue(TEXT("Start missing-audio opening dialogue"),Audio->StartStoryDialogue(3)))return false;
 TestTrue(TEXT("Opening story clock can advance despite combat not running"),Battle->CanAdvancePresentationClock());
 TestTrue(TEXT("Opening dialogue owns its pause"),World->IsPaused()&&Battle->HasPresentationHold()&&Audio->GetActiveResultDialogue()!=nullptr);
 WaitAndTick(.75f);
 TestFalse(TEXT("Missing-audio opening completes without a running combat clock"),Audio->IsStoryDialoguePlaying());
 TestFalse(TEXT("Opening completion releases its pause"),World->IsPaused());
 TestNull(TEXT("Opening completion clears subtitle"),Audio->GetActiveResultDialogue());
 Battle->CompleteOpeningForTests();
 if(!TestTrue(TEXT("Combat starts after opening"),Battle->IsCombatRunning()))return false;

 if(!TestTrue(TEXT("Start missing-audio dialogue during combat"),Audio->StartStoryDialogue(3)))return false;
 if(!TestTrue(TEXT("External pause overlaps the dialogue"),Mode->SetPause(Controller)))return false;
 TestFalse(TEXT("External pause stops story clock"),Battle->CanAdvancePresentationClock());
 WaitAndTick(.8f);
 TestTrue(TEXT("Dialogue and subtitle survive external pause beyond fallback duration"),Audio->IsStoryDialoguePlaying()&&Audio->GetActiveResultDialogue()!=nullptr);
 Controller->SetPause(false);
 TestTrue(TEXT("Releasing external pause preserves dialogue hold"),World->IsPaused()&&Battle->HasPresentationHold());
 TestTrue(TEXT("Dialogue clock resumes after external release"),Battle->CanAdvancePresentationClock());
 WaitAndTick(.2f);
 TestTrue(TEXT("Paused duration was not consumed"),Audio->IsStoryDialoguePlaying());
 WaitAndTick(.55f);
 TestFalse(TEXT("Missing-audio fallback ends after resumed time"),Audio->IsStoryDialoguePlaying());
 TestTrue(TEXT("Fallback releases its hold and restores combat"),!World->IsPaused()&&Battle->IsCombatRunning());

 if(!TestTrue(TEXT("Start dialogue before a separate presentation hold"),Audio->StartStoryDialogue(3)))return false;
 const FGuid OtherHold=FGuid::NewGuid();Battle->SetPresentationHold(OtherHold,true);
 WaitAndTick(.1f);
 TestTrue(TEXT("Separate presentation hold coexists with dialogue"),Battle->HasPresentationHold()&&Audio->IsStoryDialoguePlaying());
 Audio->SkipStoryDialogue();
 TestTrue(TEXT("Skipping dialogue preserves the separate hold"),Battle->HasPresentationHold());
 Battle->SetPresentationHold(OtherHold,false);
 TestTrue(TEXT("Releasing the separate hold restores combat"),!World->IsPaused()&&Battle->IsCombatRunning());

 if(!TestTrue(TEXT("Start dialogue before a new battle round"),Audio->StartStoryDialogue(3)))return false;
 const FGuid OldRound=Battle->GetRoundId();Battle->Stop();Battle->Start({});Battle->CompleteOpeningForTests();
 TestTrue(TEXT("The reused battle context has a new round"),Battle->GetRoundId().IsValid()&&Battle->GetRoundId()!=OldRound);
 Audio->TickComponent(0.f,LEVELTICK_All,nullptr);
 TestFalse(TEXT("Old-round dialogue is cancelled on a reused context"),Audio->IsStoryDialoguePlaying());
 TestTrue(TEXT("Old-round cancellation leaves the new battle running"),Battle->IsCombatRunning());

 if(!TestTrue(TEXT("Resolve elite"),Match->ResolveCombat(true))||!TestTrue(TEXT("Advance to stage four"),Match->NextStage()))return false;
 for(int32 Stage=4;Stage<=6;++Stage)
  if(!Match->EnterCombat()||!Match->ResolveCombat(true)||!Match->NextStage()){AddError(TEXT("Unable to advance to boss"));return false;}
 if(!TestTrue(TEXT("Enter boss combat"),Match->EnterCombat()))return false;
 Battle->Start({});Battle->CompleteOpeningForTests();
 if(!TestTrue(TEXT("Start phase-two dialogue"),Audio->StartStoryDialogue(7,true)))return false;
 if(!TestTrue(TEXT("External pause overlaps phase-two line"),Mode->SetPause(Controller)))return false;
 WaitAndTick(.8f);
 TestTrue(TEXT("Phase-two line also freezes under external pause"),Audio->IsStoryDialoguePlaying());
 Audio->SkipStoryDialogue();
 TestFalse(TEXT("Skip ends phase-two dialogue"),Audio->IsStoryDialoguePlaying());
 TestNull(TEXT("Skip clears phase-two subtitle"),Audio->GetActiveResultDialogue());
 TestFalse(TEXT("Skip removes phase-two story hold"),Battle->HasPresentationHold());
 TestTrue(TEXT("Skip preserves the independent pause"),World->IsPaused());
 Controller->SetPause(false);
 TestTrue(TEXT("Releasing the independent pause resumes combat"),!World->IsPaused()&&Battle->IsCombatRunning());
 Battle->Stop();TestFalse(TEXT("Stopped battle cannot advance story clock"),Battle->CanAdvancePresentationClock());
 return true;
}
#endif
