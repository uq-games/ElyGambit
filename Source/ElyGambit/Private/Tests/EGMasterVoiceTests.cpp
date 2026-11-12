#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/AudioComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Data/EGRunDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/EGMatchManager.h"
#include "GameFramework/PlayerController.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/UnrealType.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMasterVoiceSelectedBindingsTest,
    "ElyGambit.MasterVoice.SelectedBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGMasterVoiceSelectedBindingsTest::RunTest(const FString& Parameters)
{
    const UEGGameAudioComponent* Defaults = GetDefault<UEGGameAudioComponent>();
    if (!TestNotNull(TEXT("Configured game audio defaults"), Defaults)) return false;

    struct FExpectedVoice
    {
        const TArray<TSoftObjectPtr<USoundBase>>* Pool;
        const TCHAR* AssetName;
        float Duration;
    };
    const FExpectedVoice Expected[] = {
        {&Defaults->ElysiaVictoryVoices, TEXT("SW_Master_Elysia_Victory_01"), 3.783356f},
        {&Defaults->ElysiaVictoryVoices, TEXT("SW_Master_Elysia_Victory_07"), 3.366893f},
        {&Defaults->ElysiaVictoryVoices, TEXT("SW_Master_Elysia_Victory_08"), 2.530975f},
        {&Defaults->ElysiaDefeatVoices, TEXT("SW_Master_Elysia_Defeat_02"), 2.f},
        {&Defaults->ElysiaDefeatVoices, TEXT("SW_Master_Elysia_Defeat_09"), 1.880816f},
        {&Defaults->CorruptionVictoryVoices, TEXT("SW_Master_Corruption_Victory_03"), 4.156372f},
        {&Defaults->CorruptionVictoryVoices, TEXT("SW_Master_Corruption_Victory_04"), 4.342132f},
        {&Defaults->CorruptionDefeatVoices, TEXT("SW_Master_Corruption_Defeat_05"), 2.902494f},
        {&Defaults->CorruptionDefeatVoices, TEXT("SW_Master_Corruption_Defeat_06"), 3.134694f},
    };
    TestEqual(TEXT("Elysia victory has the three chosen lines"), Defaults->ElysiaVictoryVoices.Num(), 3);
    TestEqual(TEXT("Elysia defeat has the two chosen lines"), Defaults->ElysiaDefeatVoices.Num(), 2);
    TestEqual(TEXT("Corruption victory has the two chosen lines"), Defaults->CorruptionVictoryVoices.Num(), 2);
    TestEqual(TEXT("Corruption defeat has the two chosen lines"), Defaults->CorruptionDefeatVoices.Num(), 2);

    TSet<FSoftObjectPath> Seen;
    for (const FExpectedVoice& Item : Expected)
    {
        const FString Path = FString::Printf(TEXT("/Game/Assets/Shared/Audio/Elysian20261104/MasterVoices20261105/%s.%s"), Item.AssetName, Item.AssetName);
        const TSoftObjectPtr<USoundBase>* Found = Item.Pool->FindByPredicate([&Path](const TSoftObjectPtr<USoundBase>& Voice)
        {
            return Voice.ToSoftObjectPath().ToString() == Path;
        });
        if (!TestNotNull(*FString::Printf(TEXT("%s is in the intended pool"), Item.AssetName), Found)) continue;
        const FEGMasterVoiceLine* Line = Defaults->MasterVoiceLines.FindByPredicate([Found](const FEGMasterVoiceLine& Candidate)
        {
            return Candidate.Sound.ToSoftObjectPath() == Found->ToSoftObjectPath();
        });
        if (TestNotNull(*FString::Printf(TEXT("%s has a matching dialogue record"), Item.AssetName), Line))
        {
            TestFalse(TEXT("Dialogue record has speaker"), Line->Speaker.IsEmpty());
            TestFalse(TEXT("Dialogue record has text"), Line->Text.IsEmpty());
        }
        TestFalse(*FString::Printf(TEXT("%s occurs once"), Item.AssetName), Seen.Contains(Found->ToSoftObjectPath()));
        Seen.Add(Found->ToSoftObjectPath());
        USoundWave* Wave = Cast<USoundWave>(Found->LoadSynchronous());
        if (!TestNotNull(*FString::Printf(TEXT("%s loads as SoundWave"), Item.AssetName), Wave)) continue;
        TestFalse(*FString::Printf(TEXT("%s does not loop"), Item.AssetName), bool(Wave->bLooping));
        TestTrue(*FString::Printf(TEXT("%s has the intended duration %.3f"), Item.AssetName, Item.Duration),
            FMath::IsNearlyEqual(Wave->Duration, Item.Duration, .03f));
    }
    TestEqual(TEXT("The selected pools contain nine distinct voice assets"), Seen.Num(), 9);
    TestTrue(TEXT("Stage dialogue has no default selection before user choice"), Defaults->StageDialogueLines.IsEmpty());
    // The previous four imports are preserved as candidates, not played by the
    // default configuration. Verify that removing the binding did not delete or
    // alter those existing assets.
    struct FRetainedCandidate { const TCHAR* Asset; float Duration; };
    const FRetainedCandidate Candidates[] = {
        {TEXT("SW_Corruption_Stage03_01"), 4.981f},
        {TEXT("SW_Corruption_Stage06_01"), 4.8f},
        {TEXT("SW_Corruption_Stage06_02"), 7.085f},
        {TEXT("SW_Corruption_Stage07_01"), 2.676f},
    };
    for (const FRetainedCandidate& Candidate : Candidates)
    {
        const FString Path = FString::Printf(TEXT("/Game/Assets/Shared/Audio/Elysian20261104/CorruptionStory20261106/%s.%s"), Candidate.Asset, Candidate.Asset);
        TSoftObjectPtr<USoundBase> Sound{FSoftObjectPath(Path)};
        USoundWave* Wave = Cast<USoundWave>(Sound.LoadSynchronous());
        if (!TestNotNull(*FString::Printf(TEXT("Retained candidate %s loads"), Candidate.Asset), Wave)) continue;
        TestFalse(TEXT("Retained candidate does not loop"), bool(Wave->bLooping));
        TestTrue(TEXT("Retained candidate keeps its duration"), FMath::IsNearlyEqual(Wave->Duration, Candidate.Duration, .05f));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMasterVoiceLifecycleTest,
    "ElyGambit.MasterVoice.Lifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGMasterVoiceLifecycleTest::RunTest(const FString& Parameters)
{
    if (!GEngine) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World = Instance->GetWorld();
    if (!TestNotNull(TEXT("Standalone world"), World)) return false;
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();
        World->EndPlay(EEndPlayReason::Quit);
        Instance->Shutdown();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };

    APlayerController* Controller = World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Controller"), Controller)) return false;
    TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
    Controller->SetPlayer(LocalPlayer.Get());
    if (!TestTrue(TEXT("Local player controller"), Controller->IsLocalController())) return false;
    UEGGameAudioComponent* Audio = NewObject<UEGGameAudioComponent>(Controller);
    Audio->Bindings.Reset();
    Audio->ElysiaVictoryVoices.Reset();
    Audio->ElysiaDefeatVoices.Reset();
    Audio->CorruptionVictoryVoices.Reset();
    Audio->CorruptionDefeatVoices.Reset();
    Audio->StageDialogueLines.Reset();
    // This fixture manually invokes load completion with synthetic voices;
    // production Boss sounds are covered by the real async test below.
    Audio->BossEffectSounds.Reset();
    Audio->RegisterComponent();
    if (!Controller->HasActorBegunPlay()) Controller->DispatchBeginPlay();
    if (!TestTrue(TEXT("Audio component began play"), Audio->HasBegunPlay())) return false;

    TStrongObjectPtr<USoundWave> ElysiaWin(NewObject<USoundWave>());
    TStrongObjectPtr<USoundWave> ElysiaLoss(NewObject<USoundWave>());
    TStrongObjectPtr<USoundWave> CorruptionWin(NewObject<USoundWave>());
    TStrongObjectPtr<USoundWave> CorruptionLoss(NewObject<USoundWave>());
    Audio->ElysiaVictoryVoices.Add(TSoftObjectPtr<USoundBase>(ElysiaWin.Get()));
    Audio->ElysiaDefeatVoices.Add(TSoftObjectPtr<USoundBase>(ElysiaLoss.Get()));
    Audio->CorruptionVictoryVoices.Add(TSoftObjectPtr<USoundBase>(CorruptionWin.Get()));
    Audio->CorruptionDefeatVoices.Add(TSoftObjectPtr<USoundBase>(CorruptionLoss.Get()));

    // Both results draw exactly one eligible line. Repetition checks never
    // require a particular random distribution or an audio device.
    for (int32 I = 0; I < 32; ++I)
    {
        Audio->QueueResultVoices(true);
        if (!TestEqual(TEXT("Victory queues exactly one line"), Audio->PendingResultVoices.Num(), 1)) return false;
        TestTrue(TEXT("Victory only allows Elysia win or Corruption loss"),
            Audio->PendingResultVoices[0] == ElysiaWin.Get() || Audio->PendingResultVoices[0] == CorruptionLoss.Get());
        Audio->QueueResultVoices(false);
        if (!TestEqual(TEXT("Defeat queues exactly one line"), Audio->PendingResultVoices.Num(), 1)) return false;
        TestTrue(TEXT("Defeat only allows Corruption win or Elysia loss"),
            Audio->PendingResultVoices[0] == CorruptionWin.Get() || Audio->PendingResultVoices[0] == ElysiaLoss.Get());
    }
    Audio->ElysiaVictoryVoices.Reset();
    Audio->QueueResultVoices(true);
    TestTrue(TEXT("Missing winner falls back to the eligible loser"), Audio->PendingResultVoices.Num() == 1 && Audio->PendingResultVoices[0] == CorruptionLoss.Get());
    Audio->CorruptionDefeatVoices.Reset();
    Audio->QueueResultVoices(true);
    TestTrue(TEXT("No eligible sound queues nothing"), Audio->PendingResultVoices.IsEmpty());
    Audio->StopResultVoices();

    int32 EndedNotifications = 0;
    const FDelegateHandle EndedHandle = Audio->OnResultDialogueEnded.AddLambda([&EndedNotifications]() { ++EndedNotifications; });
    Audio->ActiveResultDialogue.Sound = ElysiaWin.Get();
    Audio->bResultDialogueActive = true;
    TestNotNull(TEXT("Active result can be queried by a late UI"), Audio->GetActiveResultDialogue());
    Audio->StopResultVoices();
    TestEqual(TEXT("Cancellation sends one end notification"), EndedNotifications, 1);
    TestNull(TEXT("Cancellation clears the active dialogue"), Audio->GetActiveResultDialogue());
    Audio->StopResultVoices();
    TestEqual(TEXT("Repeated cancellation cannot send another end notification"), EndedNotifications, 1);
    Audio->ActiveResultDialogue.Sound = ElysiaWin.Get();
    Audio->bResultDialogueActive = true;
    Audio->HandleResultVoiceFinished();
    TestEqual(TEXT("Completion sends one end notification"), EndedNotifications, 2);
    TestNull(TEXT("Completion clears the active dialogue"), Audio->GetActiveResultDialogue());
    Audio->OnResultDialogueEnded.Remove(EndedHandle);

    TStrongObjectPtr<UEGRunDefinition> Run(NewObject<UEGRunDefinition>());
    Run->bSevenStageRules = true;
    Run->StartingCorruption = 50;
    Run->Stages.SetNum(7);
    TStrongObjectPtr<UEGMatchManager> Match(NewObject<UEGMatchManager>());
    if (!TestTrue(TEXT("Configure seven-stage run"), Match->ConfigureRunDefinition(Run.Get()))) return false;
    // Initialize without attempting asynchronous loads in the fixture. The
    // identity and ordering tests above separately exercise real queue logic.
    Audio->ElysiaVictoryVoices.Reset();
    Audio->ElysiaDefeatVoices.Reset();
    Audio->CorruptionVictoryVoices.Reset();
    Audio->CorruptionDefeatVoices.Reset();
    Audio->InitializeForMatch(Match.Get());
    Audio->ElysiaVictoryVoices.Add(TSoftObjectPtr<USoundBase>(ElysiaWin.Get()));
    Audio->ElysiaDefeatVoices.Add(TSoftObjectPtr<USoundBase>(ElysiaLoss.Get()));
    Audio->CorruptionVictoryVoices.Add(TSoftObjectPtr<USoundBase>(CorruptionWin.Get()));
    Audio->CorruptionDefeatVoices.Add(TSoftObjectPtr<USoundBase>(CorruptionLoss.Get()));
    if (!TestTrue(TEXT("Start run"), Match->StartRun())) return false;
    if (!TestTrue(TEXT("Enter first combat"), Match->EnterCombat())) return false;
    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Victory while loading"), Match->ResolveCombat(true))) return false;
    TestTrue(TEXT("Victory awaits assets"), Audio->bPendingResultSound);
    TestTrue(TEXT("Pending outcome is victory"), Audio->bLastResultVictory);

    UAudioComponent* VoiceBeforeCorrection = NewObject<UAudioComponent>(Controller);
    VoiceBeforeCorrection->RegisterComponent();
    Audio->ResultVoiceComponent = VoiceBeforeCorrection;
    Audio->bResultVoiceStarted = true;
    Audio->ResultVoiceCombatRevision = Audio->CombatRevision;
    Audio->PendingResultVoices.Add(CorruptionLoss.Get());
    Match->OnBattleResolved.Broadcast(true, Match->GetCorruption());
    TestTrue(TEXT("Duplicate victory leaves current voice unchanged"), Audio->ResultVoiceComponent == VoiceBeforeCorrection);
    TestEqual(TEXT("Duplicate victory leaves queued reply unchanged"), Audio->PendingResultVoices.Num(), 1);

    Match->AddCorruption(150);
    TestEqual(TEXT("Corruption corrects result and ends run"), Match->GetPhase(), EEGMatchPhase::Finished);
    TestFalse(TEXT("Corrected outcome is defeat"), Audio->bLastResultVictory);
    TestFalse(TEXT("Correction destroys previous voice"), VoiceBeforeCorrection->IsRegistered());
    TestNull(TEXT("Correction clears active voice"), Audio->ResultVoiceComponent.Get());
    TestTrue(TEXT("Correction clears previous reply"), Audio->PendingResultVoices.IsEmpty());
    TestTrue(TEXT("Corrected outcome still waits for loading"), Audio->bPendingResultSound);
    Audio->AssetsLoaded(Audio->LoadSerial);
    TestFalse(TEXT("Loading consumes only current outcome"), Audio->bPendingResultSound);
    TestFalse(TEXT("Loading retains corrected defeat"), Audio->bLastResultVictory);
    TestNull(TEXT("Correction never starts a second voice after one has begun"), Audio->ResultVoiceComponent.Get());
    TestTrue(TEXT("Correction never leaves a second line queued"), Audio->PendingResultVoices.IsEmpty());

    UAudioComponent* VoiceBeforeRestart = NewObject<UAudioComponent>(Controller);
    VoiceBeforeRestart->RegisterComponent();
    Audio->ResultVoiceComponent = VoiceBeforeRestart;
    Audio->PendingResultVoices.Add(ElysiaLoss.Get());
    if (!TestTrue(TEXT("Restart finished run"), Match->StartRun())) return false;
    TestFalse(TEXT("Restart stops previous voice"), VoiceBeforeRestart->IsRegistered());
    TestTrue(TEXT("Restart discards previous reply"), Audio->PendingResultVoices.IsEmpty());
    if (!TestTrue(TEXT("Enter new combat"), Match->EnterCombat())) return false;
    TestNotEqual(TEXT("A new combat has a different voice revision"), Audio->CombatRevision, Audio->ResultVoiceCombatRevision);
    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Unplayed victory waits for loading"), Match->ResolveCombat(true))) return false;
    Match->AddCorruption(150);
    TestTrue(TEXT("Unplayed correction remains pending"), Audio->bPendingResultSound);
    TestFalse(TEXT("Unplayed correction updates the result"), Audio->bLastResultVictory);
    int32 StartedNotifications = 0;
    USoundBase* StartedSound = nullptr;
    const FDelegateHandle StartedHandle = Audio->OnResultDialogueStarted.AddLambda([&](const FEGMasterVoiceLine& Line)
    {
        ++StartedNotifications;
        StartedSound = Line.Sound.Get();
    });
    Audio->AssetsLoaded(Audio->LoadSerial);
    TestFalse(TEXT("Loading consumes the corrected unplayed result"), Audio->bPendingResultSound);
    TestTrue(TEXT("Loading starts at most one corrected line"), StartedNotifications <= 1);
    if (StartedNotifications == 1)
    {
        TestTrue(TEXT("Corrected line belongs to the defeat pools"), StartedSound == CorruptionWin.Get() || StartedSound == ElysiaLoss.Get());
        TestEqual(TEXT("Corrected line belongs to this combat"), Audio->ResultVoiceCombatRevision, Audio->CombatRevision);
    }
    Audio->OnResultDialogueStarted.Remove(StartedHandle);
    if (!TestTrue(TEXT("Restart after unplayed correction"), Match->StartRun())) return false;
    if (!TestTrue(TEXT("Enter combat after unplayed correction"), Match->EnterCombat())) return false;
    if (!TestTrue(TEXT("Resolve normal victory"), Match->ResolveCombat(true))) return false;

    // A line already started before settlement ends survives ordinary stage
    // advancement into Preparation, but not a new combat.
    Audio->StopResultVoices();
    UAudioComponent* ContinuingVoice = NewObject<UAudioComponent>(Controller);
    ContinuingVoice->RegisterComponent();
    Audio->ResultVoiceComponent = ContinuingVoice;
    Audio->PendingResultVoices.Add(CorruptionLoss.Get());
    if (!TestTrue(TEXT("Finish settlement"), Match->FinishSettlement())) return false;
    if (!TestTrue(TEXT("Advance to next preparation"), Match->NextStage())) return false;
    TestTrue(TEXT("Already started voice survives ordinary preparation"), Audio->ResultVoiceComponent == ContinuingVoice && ContinuingVoice->IsRegistered());
    TestEqual(TEXT("Its reply survives ordinary preparation"), Audio->PendingResultVoices.Num(), 1);
    if (!TestTrue(TEXT("Enter next combat"), Match->EnterCombat())) return false;
    TestFalse(TEXT("New combat stops previous voice"), ContinuingVoice->IsRegistered());
    TestTrue(TEXT("New combat discards previous reply"), Audio->PendingResultVoices.IsEmpty());

    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Resolve while loading"), Match->ResolveCombat(false))) return false;
    TestTrue(TEXT("Result waits for assets"), Audio->bPendingResultSound);
    if (!TestTrue(TEXT("Finish second settlement"), Match->FinishSettlement())) return false;
    if (!TestTrue(TEXT("Advance beyond pending result"), Match->NextStage())) return false;
    TestFalse(TEXT("Preparation cancels a result that never started"), Audio->bPendingResultSound);
    Audio->AssetsLoaded(Audio->LoadSerial);
    TestNull(TEXT("Late loading cannot play obsolete voice"), Audio->ResultVoiceComponent.Get());
    TestTrue(TEXT("Late loading cannot restore obsolete reply"), Audio->PendingResultVoices.IsEmpty());

    // Stage seven transitions straight to Finished before its result event.
    for (int32 Stage = Match->GetStageNumber(); Stage <= 7; ++Stage)
    {
        if (!TestTrue(TEXT("Enter campaign combat"), Match->EnterCombat())) return false;
        if (!TestTrue(TEXT("Resolve campaign combat"), Match->ResolveCombat(true))) return false;
        if (Stage < 7)
        {
            if (!TestTrue(TEXT("Finish campaign settlement"), Match->FinishSettlement())) return false;
            if (!TestTrue(TEXT("Advance campaign stage"), Match->NextStage())) return false;
        }
    }
    TestEqual(TEXT("Final battle is finished"), Match->GetPhase(), EEGMatchPhase::Finished);
    TestTrue(TEXT("Final battle result is received"), Audio->bResultKnown && Audio->bLastResultVictory);

    // Exercise actual match phase notifications with staged mock voice lines.
    // The next-tick handle is observable even in NullRHI without an audio device.
    Audio->ElysiaVictoryVoices.Reset();
    Audio->ElysiaDefeatVoices.Reset();
    Audio->CorruptionVictoryVoices.Reset();
    Audio->CorruptionDefeatVoices.Reset();
    TStrongObjectPtr<UEGMatchManager> StageMatch(NewObject<UEGMatchManager>());
    if (!TestTrue(TEXT("Configure stage dialogue match"), StageMatch->ConfigureRunDefinition(Run.Get()))) return false;
    Audio->InitializeForMatch(StageMatch.Get());
    FEGStageDialogueLine& StageThree = Audio->StageDialogueLines.AddDefaulted_GetRef();
    StageThree.StageNumber = 3;
    StageThree.Line.Sound = CorruptionWin.Get();
    StageThree.Line.Speaker = FText::FromString(TEXT("speaker"));
    StageThree.Line.Text = FText::FromString(TEXT("stage line"));
    FEGStageDialogueLine& StageSix = Audio->StageDialogueLines.AddDefaulted_GetRef();
    StageSix.StageNumber = 6;
    StageSix.Line.Sound = CorruptionWin.Get();
    FEGStageDialogueLine& StageSeven = Audio->StageDialogueLines.AddDefaulted_GetRef();
    StageSeven.StageNumber = 7;
    StageSeven.Line.Sound = CorruptionWin.Get();
    if (!TestTrue(TEXT("Start stage dialogue match"), StageMatch->StartRun())) return false;
    for (int32 Stage = 1; Stage < 3; ++Stage)
    {
        if (!TestTrue(TEXT("Enter earlier stage"), StageMatch->EnterCombat())) return false;
        TestFalse(TEXT("Other stages do not schedule a line"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
        if (!TestTrue(TEXT("Resolve earlier stage"), StageMatch->ResolveCombat(true))) return false;
        if (!TestTrue(TEXT("Finish earlier settlement"), StageMatch->FinishSettlement())) return false;
        if (!TestTrue(TEXT("Advance earlier stage"), StageMatch->NextStage())) return false;
    }
    if (!TestTrue(TEXT("Enter configured third stage"), StageMatch->EnterCombat())) return false;
    TestFalse(TEXT("Old configured candidates do not automatically play before user selection"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    TestNull(TEXT("Entering combat with stored candidates starts no stage voice"), Audio->GetActiveResultDialogue());
    Audio->SyncPhase();
    TestFalse(TEXT("Repeated phase sync cannot schedule a stored candidate"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    TestNull(TEXT("Repeated phase sync starts no stage voice"), Audio->GetActiveResultDialogue());
    // Drive the retained mechanism explicitly in this test fixture; production
    // phase sync remains disabled until an actual user selection is integrated.
    Audio->ScheduleStageDialogue();
    TestTrue(TEXT("Configured stage schedules one next-tick line"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    const FTimerHandle ThirdStageTimer = Audio->StageDialogueTimer;
    const uint64 ThirdStageRevision = Audio->CombatRevision;
    Audio->SyncPhase();
    TestTrue(TEXT("Duplicate phase sync does not replace the scheduled line"), Audio->StageDialogueTimer == ThirdStageTimer);
    if (!TestTrue(TEXT("Immediate empty-board loss resolves"), StageMatch->ResolveCombat(false))) return false;
    TestFalse(TEXT("Immediate resolution cancels scheduled stage line"), World->GetTimerManager().TimerExists(ThirdStageTimer));
    Audio->PlayStageDialogue(3, ThirdStageRevision);
    TestNull(TEXT("Stale callback cannot start a line after resolution"), Audio->GetActiveResultDialogue());
    if (!TestTrue(TEXT("Finish third settlement"), StageMatch->FinishSettlement())) return false;
    if (!TestTrue(TEXT("Advance to fourth"), StageMatch->NextStage())) return false;
    for (int32 Stage = 4; Stage < 6; ++Stage)
    {
        if (!TestTrue(TEXT("Enter intervening stage"), StageMatch->EnterCombat())) return false;
        if (!TestTrue(TEXT("Resolve intervening stage"), StageMatch->ResolveCombat(true))) return false;
        if (!TestTrue(TEXT("Finish intervening settlement"), StageMatch->FinishSettlement())) return false;
        if (!TestTrue(TEXT("Advance intervening stage"), StageMatch->NextStage())) return false;
    }
    if (!TestTrue(TEXT("Enter configured sixth stage"), StageMatch->EnterCombat())) return false;
    TestFalse(TEXT("Sixth stage does not auto-schedule stored candidates"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    Audio->ScheduleStageDialogue();
    TestTrue(TEXT("Sixth stage schedules its own line"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    const FTimerHandle SixthStageTimer = Audio->StageDialogueTimer;
    int32 StageEndNotifications = 0;
    const FDelegateHandle StageEndHandle = Audio->OnResultDialogueEnded.AddLambda([&]() { ++StageEndNotifications; });
    UAudioComponent* ActiveStageVoice = NewObject<UAudioComponent>(Controller);
    ActiveStageVoice->RegisterComponent();
    Audio->ResultVoiceComponent = ActiveStageVoice;
    Audio->bStageDialogueActive = true;
    Audio->bResultDialogueActive = true;
    if (!TestTrue(TEXT("Resolve sixth stage"), StageMatch->ResolveCombat(true))) return false;
    TestFalse(TEXT("Leaving combat cancels scheduled stage dialogue"), World->GetTimerManager().TimerExists(SixthStageTimer));
    TestFalse(TEXT("Leaving combat stops an active stage voice"), ActiveStageVoice->IsRegistered());
    TestEqual(TEXT("Leaving combat clears its subtitle once"), StageEndNotifications, 1);
    TestNull(TEXT("No stage dialogue persists into settlement"), Audio->GetActiveResultDialogue());
    Audio->OnResultDialogueEnded.Remove(StageEndHandle);
    if (!TestTrue(TEXT("Finish sixth settlement"), StageMatch->FinishSettlement())) return false;
    if (!TestTrue(TEXT("Advance to final stage"), StageMatch->NextStage())) return false;
    if (!TestTrue(TEXT("Enter final stage"), StageMatch->EnterCombat())) return false;
    TestFalse(TEXT("Final stage does not auto-schedule stored candidates"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    Audio->ScheduleStageDialogue();
    TestTrue(TEXT("Final stage schedules its own line"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    if (!TestTrue(TEXT("Finish final stage"), StageMatch->ResolveCombat(true))) return false;
    if (!TestTrue(TEXT("Restart stage dialogue run"), StageMatch->StartRun())) return false;
    for (int32 Stage = 1; Stage < 3; ++Stage)
    {
        if (!TestTrue(TEXT("Enter stage after restart"), StageMatch->EnterCombat())) return false;
        if (!TestTrue(TEXT("Resolve stage after restart"), StageMatch->ResolveCombat(true))) return false;
        if (!TestTrue(TEXT("Finish settlement after restart"), StageMatch->FinishSettlement())) return false;
        if (!TestTrue(TEXT("Advance after restart"), StageMatch->NextStage())) return false;
    }
    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Re-enter stage three after restart"), StageMatch->EnterCombat())) return false;
    TestNotEqual(TEXT("Restarted stage uses a new combat revision"), Audio->CombatRevision, ThirdStageRevision);
    TestFalse(TEXT("Stage line waits while assets are loading"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    Audio->AssetsLoaded(Audio->LoadSerial);
    TestFalse(TEXT("Loading never starts a stored candidate before user selection"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    TestNull(TEXT("Loading stored candidates starts no stage voice"), Audio->GetActiveResultDialogue());
    Audio->ScheduleStageDialogue();
    const FTimerHandle RestartTimer = Audio->StageDialogueTimer;
    TestTrue(TEXT("Retained mechanism schedules after loading when explicitly invoked"), World->GetTimerManager().TimerExists(RestartTimer));
    Audio->PlayStageDialogue(3, ThirdStageRevision);
    TestTrue(TEXT("Stale prior revision cannot cancel the new timer"), Audio->StageDialogueTimer == RestartTimer && World->GetTimerManager().TimerExists(RestartTimer));
    if (!TestTrue(TEXT("Resolve restarted stage"), StageMatch->ResolveCombat(true))) return false;
    if (!TestTrue(TEXT("Finish restarted third settlement"), StageMatch->FinishSettlement())) return false;
    if (!TestTrue(TEXT("Advance after restarted third stage"), StageMatch->NextStage())) return false;
    for (int32 Stage = 4; Stage < 6; ++Stage)
    {
        if (!TestTrue(TEXT("Enter restarted intervening stage"), StageMatch->EnterCombat())) return false;
        if (!TestTrue(TEXT("Resolve restarted intervening stage"), StageMatch->ResolveCombat(true))) return false;
        if (!TestTrue(TEXT("Finish restarted intervening settlement"), StageMatch->FinishSettlement())) return false;
        if (!TestTrue(TEXT("Advance restarted intervening stage"), StageMatch->NextStage())) return false;
    }
    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Enter sixth stage while loading"), StageMatch->EnterCombat())) return false;
    const double SavedWorldTime = World->TimeSeconds;
    World->TimeSeconds += 4.0; // Controlled world clock: simulate a late load without sleeping.
    Audio->AssetsLoaded(Audio->LoadSerial);
    Audio->ScheduleStageDialogue();
    TestFalse(TEXT("Late stage assets do not schedule an entrance line"), World->GetTimerManager().TimerExists(Audio->StageDialogueTimer));
    TestNull(TEXT("Late stage assets do not start dialogue"), Audio->GetActiveResultDialogue());
    World->TimeSeconds = SavedWorldTime;
    if (!TestTrue(TEXT("Resolve sixth stage after late load"), StageMatch->ResolveCombat(true))) return false;
    Audio->StageDialogueLines.Reset();

    // RetryStage is supported for the non-seven-stage run; its Finished →
    // Preparation transition must not retain the previous run's voices.
    Audio->ElysiaVictoryVoices.Reset();
    Audio->ElysiaDefeatVoices.Reset();
    Audio->CorruptionVictoryVoices.Reset();
    Audio->CorruptionDefeatVoices.Reset();
    TStrongObjectPtr<UEGMatchManager> RetryMatch(NewObject<UEGMatchManager>());
    if (!TestTrue(TEXT("Configure retryable match"), RetryMatch->ConfigureRun(2, true))) return false;
    Audio->InitializeForMatch(RetryMatch.Get());
    if (!TestTrue(TEXT("Start retryable match"), RetryMatch->StartRun())) return false;
    if (!TestTrue(TEXT("Enter retryable combat"), RetryMatch->EnterCombat())) return false;
    if (!TestTrue(TEXT("Lose retryable match"), RetryMatch->ResolveCombat(false))) return false;
    TestEqual(TEXT("Retryable loss ends the match"), RetryMatch->GetPhase(), EEGMatchPhase::Finished);
    UAudioComponent* VoiceBeforeRetry = NewObject<UAudioComponent>(Controller);
    VoiceBeforeRetry->RegisterComponent();
    Audio->ResultVoiceComponent = VoiceBeforeRetry;
    Audio->PendingResultVoices.Add(ElysiaLoss.Get());
    if (!TestTrue(TEXT("Retry defeated stage"), RetryMatch->RetryStage())) return false;
    TestFalse(TEXT("Retry stops previous voice"), VoiceBeforeRetry->IsRegistered());
    TestTrue(TEXT("Retry clears previous reply"), Audio->PendingResultVoices.IsEmpty());

    Audio->StopResultVoices();
    UAudioComponent* VoiceBeforeEnd = NewObject<UAudioComponent>(Controller);
    VoiceBeforeEnd->RegisterComponent();
    Audio->ResultVoiceComponent = VoiceBeforeEnd;
    Audio->PendingResultVoices.Add(ElysiaWin.Get());
    Audio->EndPlay(EEndPlayReason::Quit);
    TestFalse(TEXT("EndPlay unregisters active voice"), VoiceBeforeEnd->IsRegistered());
    TestNull(TEXT("EndPlay releases active voice"), Audio->ResultVoiceComponent.Get());
    TestTrue(TEXT("EndPlay clears queued replies"), Audio->PendingResultVoices.IsEmpty());
    RetryMatch->OnBattleResolved.Broadcast(false, RetryMatch->GetCorruption());
    TestTrue(TEXT("Ended component ignores later result"), Audio->PendingResultVoices.IsEmpty());
    return true;
}

namespace
{
// Uses the production InitializeForMatch async request and observes the
// reflected strong-reference array only after its actual completion callback.
class FMasterBossAudioAsyncRun final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TUniquePtr<TStrongObjectPtr<UGameInstance>> Instance;
 TUniquePtr<TStrongObjectPtr<UEGMatchManager>> Match;
 TWeakObjectPtr<UEGGameAudioComponent> Audio;
 double Deadline=0.;
 bool bStarted=false;
public:
 explicit FMasterBossAudioAsyncRun(FAutomationTestBase* InTest):Test(InTest){}
 virtual ~FMasterBossAudioAsyncRun() override
 {
  UGameInstance* Game=Instance?Instance->Get():nullptr;UWorld* World=Game?Game->GetWorld():nullptr;
  if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);}
  Match.Reset();if(Game)Game->Shutdown();if(World){World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}Instance.Reset();
 }
 virtual bool Update() override
 {
  if(!bStarted)
  {
   if(!GEngine){Test->AddError(TEXT("Engine unavailable"));return true;}
   Instance=MakeUnique<TStrongObjectPtr<UGameInstance>>(NewObject<UGameInstance>(GEngine));(*Instance)->InitializeStandalone();
   UWorld* World=(*Instance)->GetWorld();if(!Test->TestNotNull(TEXT("Async audio world"),World))return true;
   auto* Controller=World->SpawnActor<APlayerController>();if(!Test->TestNotNull(TEXT("Async audio controller"),Controller))return true;
   auto* Local=NewObject<ULocalPlayer>(GEngine);Controller->SetPlayer(Local);
   if(!Test->TestTrue(TEXT("Async audio is local"),Controller->IsLocalController()))return true;
   auto* Component=NewObject<UEGGameAudioComponent>(Controller);Audio=Component;
   Component->Bindings.Reset();Component->ElysiaVictoryVoices.Reset();Component->ElysiaDefeatVoices.Reset();
   Component->CorruptionVictoryVoices.Reset();Component->CorruptionDefeatVoices.Reset();
   Component->StageDialogueLines.Reset();Component->SelectedStoryLines.Reset();Component->RegisterComponent();
   if(!Controller->HasActorBegunPlay())Controller->DispatchBeginPlay();
   Match=MakeUnique<TStrongObjectPtr<UEGMatchManager>>(NewObject<UEGMatchManager>());
   Component->InitializeForMatch(Match->Get());
   bStarted=true;Deadline=FPlatformTime::Seconds()+30.;return false;
  }
  if(!Audio.IsValid()){Test->AddError(TEXT("Audio component expired before async completion"));return true;}
  const auto* Array=FindFProperty<FArrayProperty>(Audio->GetClass(),TEXT("LoadedSounds"));
  const auto* Item=Array?CastField<FObjectPropertyBase>(Array->Inner):nullptr;
  if(!Array||!Item){Test->AddError(TEXT("LoadedSounds reference property unavailable"));return true;}
  FScriptArrayHelper Values(Array,Array->ContainerPtrToValuePtr<void>(Audio.Get()));
  bool bGather=false,bReveal=false;
  for(int32 Index=0;Index<Values.Num();++Index)
  {
   const UObject* Object=Item->GetObjectPropertyValue(Values.GetRawPtr(Index));
   if(Object&&Object->GetName()==TEXT("SW_HumanEntrance_Gather"))bGather=true;
   if(Object&&Object->GetName()==TEXT("SW_HumanEntrance_Reveal"))bReveal=true;
  }
  if(bGather&&bReveal)
  {
   Test->TestEqual(TEXT("Only two requested Boss effects are strongly held"),Values.Num(),2);
   Test->AddInfo(TEXT("Production async initialization completed and retains both Boss effect SoundWaves; no device playback was tested."));return true;
  }
  if(FPlatformTime::Seconds()>Deadline)
  {
   Test->AddError(FString::Printf(TEXT("Boss effects not retained after async load: gather=%d reveal=%d"),bGather,bReveal));return true;
  }
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMasterBossAudioAsyncTest,"ElyGambit.MasterVoice.BossAudioAsync",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMasterBossAudioAsyncTest::RunTest(const FString&)
{
 ADD_LATENT_AUTOMATION_COMMAND(FMasterBossAudioAsyncRun(this));return true;
}
#endif
