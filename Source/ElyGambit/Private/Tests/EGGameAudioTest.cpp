#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/AudioComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "AudioDevice.h"
#include "Data/EGRunDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/EGMatchManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundWave.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGameAudioPhaseLifecycleTest,
    "ElyGambit.GameAudio.PhaseLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGGameAudioPhaseLifecycleTest::RunTest(const FString& Parameters)
{
    if (!GEngine) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World = Instance->GetWorld();
    if (!TestNotNull(TEXT("Standalone fixture world"), World)) return false;
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();
        World->EndPlay(EEndPlayReason::Quit);
        Instance->Shutdown();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };

    APlayerController* Controller = World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Local player controller"), Controller)) return false;
    TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
    if (!TestNotNull(TEXT("Fixture local player"), LocalPlayer.Get())) return false;
    Controller->SetPlayer(LocalPlayer.Get());
    TestTrue(TEXT("Controller owns the fixture local player"), Controller->GetLocalPlayer() == LocalPlayer.Get());
    if (!TestTrue(TEXT("Fixture controller is local"), Controller->IsLocalController())) return false;
    UEGGameAudioComponent* Audio = NewObject<UEGGameAudioComponent>(Controller);
    if (!TestNotNull(TEXT("Audio component"), Audio)) return false;
    Audio->Bindings.Reset();
    Audio->ElysiaVictoryVoices.Reset();
    Audio->ElysiaDefeatVoices.Reset();
    Audio->CorruptionVictoryVoices.Reset();
    Audio->CorruptionDefeatVoices.Reset();
    Audio->StageDialogueLines.Reset();
    Audio->RegisterComponent();
    if (!Controller->HasActorBegunPlay()) Controller->DispatchBeginPlay();
    if (!TestTrue(TEXT("Fixture component has begun play"), Audio->HasBegunPlay())) return false;

    TStrongObjectPtr<UEGRunDefinition> Run(NewObject<UEGRunDefinition>());
    Run->bSevenStageRules = true;
    Run->StartingCorruption = 50;
    Run->Stages.SetNum(7);
    TStrongObjectPtr<UEGMatchManager> Match(NewObject<UEGMatchManager>());
    if (!TestTrue(TEXT("Configure seven-stage match"), Match->ConfigureRunDefinition(Run.Get()))) return false;
    Audio->InitializeForMatch(Match.Get());
    TestTrue(TEXT("Empty binding fixture completes initial load"), Audio->bAssetsReady);
    TestFalse(TEXT("New run starts without a resolved result"), Audio->bResultKnown);

    // Delay the load callback while the real match delegates advance. Empty
    // bindings avoid depending on audio hardware or changing project assets.
    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Start first run"), Match->StartRun())) return false;
    if (!TestTrue(TEXT("Enter combat with load pending"), Match->EnterCombat())) return false;
    const uint64 FirstCombat = Audio->CombatRevision;
    UAudioComponent* PreviousMusic = NewObject<UAudioComponent>(Controller);
    UAudioComponent* PreviousFade = NewObject<UAudioComponent>(Controller);
    UAudioComponent* PreviousIntro = NewObject<UAudioComponent>(Controller);
    PreviousMusic->RegisterComponent();
    PreviousFade->RegisterComponent();
    PreviousIntro->RegisterComponent();
    Audio->MusicComponent = PreviousMusic;
    Audio->FadingMusic.Add(PreviousFade);
    Audio->IntroComponent = PreviousIntro;
    Audio->bBackgroundSet = true;
    if (!TestTrue(TEXT("Resolve victory before load"), Match->ResolveCombat(true))) return false;
    TestNull(TEXT("Combat background is cleared even before loading finishes"), Audio->MusicComponent.Get());
    TestTrue(TEXT("Fading background list is cleared"), Audio->FadingMusic.IsEmpty());
    TestNull(TEXT("Combat intro is cleared"), Audio->IntroComponent.Get());
    TestFalse(TEXT("Previous background component was destroyed"), PreviousMusic->IsRegistered());
    TestFalse(TEXT("Previous fading component was destroyed"), PreviousFade->IsRegistered());
    TestFalse(TEXT("Previous intro component was destroyed"), PreviousIntro->IsRegistered());
    TestFalse(TEXT("No background remains active"), Audio->bBackgroundSet);
    TestTrue(TEXT("Current victory waits for assets"), Audio->bPendingResultSound);
    TestTrue(TEXT("Pending result records victory"), Audio->bLastResultVictory);
    TestEqual(TEXT("Pending result is tied to combat"), Audio->ResolvedCombatRevision, FirstCombat);

    // A terminal corruption update emits another result for the same battle.
    // Its defeat must replace the pending victory before assets complete.
    Match->AddCorruption(150);
    TestEqual(TEXT("Corruption defeat enters finished"), Match->GetPhase(), EEGMatchPhase::Finished);
    TestFalse(TEXT("Defeat replaces earlier victory"), Audio->bLastResultVictory);
    TestTrue(TEXT("Corrected defeat is pending"), Audio->bPendingResultSound);
    Audio->AssetsLoaded(Audio->LoadSerial);
    TestFalse(TEXT("Current pending result is consumed after load"), Audio->bPendingResultSound);
    TestFalse(TEXT("Late load retains the corrected defeat result"), Audio->bLastResultVictory);
    TestNull(TEXT("Late load does not restart background music"), Audio->MusicComponent.Get());
    Match->OnBattleResolved.Broadcast(false, Match->GetCorruption());
    TestFalse(TEXT("Duplicate defeat does not queue again"), Audio->bPendingResultSound);

    {
        // Isolate persistent settings from the user's GameUserSettings.ini.
        const FString TestIni = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("ElysianAudio20261104/Integration/QA/audio-test-user-settings.ini")));
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(TestIni), true);
        if (!TestNotNull(TEXT("Config cache"), GConfig)) return false;
        GConfig->UnloadFile(TestIni);
        GConfig->Add(TestIni, FConfigFile());
        TGuardValue<FString> SettingsPathGuard(GGameUserSettingsIni, TestIni);
        ON_SCOPE_EXIT { if (GConfig) GConfig->UnloadFile(TestIni); };
        Audio->SetMusicVolume(0.21f);
        Audio->SetUISoundVolume(0.43f);
        GConfig->UnloadFile(TestIni);
        UEGGameAudioComponent* Reloaded = NewObject<UEGGameAudioComponent>(Controller);
        Reloaded->LoadVolumeSettings();
        TestEqual(TEXT("Music volume persists independently"), Reloaded->GetMusicVolume(), 0.21f);
        TestEqual(TEXT("UI volume persists independently"), Reloaded->GetUISoundVolume(), 0.43f);
        Audio->SetMusicVolume(0.f);
        TestEqual(TEXT("Muting music leaves UI volume intact"), Audio->GetUISoundVolume(), 0.43f);
    }

    if (!TestTrue(TEXT("Restart after finished"), Match->StartRun())) return false;
    if (!TestTrue(TEXT("Enter combat with assets ready"), Match->EnterCombat())) return false;
    if (!TestTrue(TEXT("Resolve a loaded victory"), Match->ResolveCombat(true))) return false;
    UAudioComponent* ExistingVictory = NewObject<UAudioComponent>(Controller);
    ExistingVictory->RegisterComponent();
    Audio->StingerComponent = ExistingVictory;
    Match->OnBattleResolved.Broadcast(true, Match->GetCorruption());
    TestTrue(TEXT("Duplicate result does not replace its existing one-shot"), Audio->StingerComponent == ExistingVictory);
    Match->AddCorruption(150);
    TestEqual(TEXT("Loaded correction enters finished"), Match->GetPhase(), EEGMatchPhase::Finished);
    TestFalse(TEXT("Loaded correction stores defeat"), Audio->bLastResultVictory);
    TestFalse(TEXT("Corrected victory one-shot is destroyed"), ExistingVictory->IsRegistered());
    TestNull(TEXT("Empty fixture does not produce a replacement one-shot"), Audio->StingerComponent.Get());
    TestNull(TEXT("Correction does not start background music"), Audio->MusicComponent.Get());

    if (!TestTrue(TEXT("Restart after corrected result"), Match->StartRun())) return false;
    TestFalse(TEXT("Restart clears the previous result"), Audio->bResultKnown);
    Audio->bAssetsReady = false;
    if (!TestTrue(TEXT("Enter next combat"), Match->EnterCombat())) return false;
    if (!TestTrue(TEXT("Resolve before second delayed load"), Match->ResolveCombat(true))) return false;
    TestTrue(TEXT("Second result is pending"), Audio->bPendingResultSound);
    if (!TestTrue(TEXT("Finish settlement"), Match->FinishSettlement())) return false;
    TestNull(TEXT("Resolution keeps background stopped"), Audio->MusicComponent.Get());
    if (!TestTrue(TEXT("Advance to next preparation"), Match->NextStage())) return false;
    TestFalse(TEXT("New preparation discards stale result"), Audio->bPendingResultSound);
    TestFalse(TEXT("New stage clears the prior result"), Audio->bResultKnown);
    Audio->AssetsLoaded(Audio->LoadSerial);
    TestFalse(TEXT("Late callback does not resurrect prior result"), Audio->bPendingResultSound);
    TestFalse(TEXT("Late load cannot restore the prior result"), Audio->bResultKnown);

    // Missing intro falls back immediately. A later completion callback after
    // the match leaves combat must not start a background for that old fight.
    if (!TestTrue(TEXT("Enter another combat"), Match->EnterCombat())) return false;
    if (!TestTrue(TEXT("Resolve that combat"), Match->ResolveCombat(false))) return false;
    TestFalse(TEXT("Ordinary defeat records defeat"), Audio->bLastResultVictory);
    TestNull(TEXT("Ordinary defeat has no background music"), Audio->MusicComponent.Get());
    TestEqual(TEXT("Intro identity cleared outside combat"), Audio->IntroCombatRevision, uint64(0));
    const bool bHadBackground = Audio->bBackgroundSet;
    Audio->HandleIntroFinished();
    TestEqual(TEXT("Late intro callback cannot change background"), Audio->bBackgroundSet, bHadBackground);

    // The last campaign stage enters Finished before broadcasting its result.
    for (int32 Stage = 2; Stage < 7; ++Stage)
    {
        if (!TestTrue(TEXT("Finish stage settlement"), Match->FinishSettlement())) return false;
        if (!TestTrue(TEXT("Advance toward final stage"), Match->NextStage())) return false;
        if (Stage < 6)
        {
            if (!TestTrue(TEXT("Enter intermediate combat"), Match->EnterCombat())) return false;
            if (!TestTrue(TEXT("Resolve intermediate combat"), Match->ResolveCombat(true))) return false;
        }
    }
    TestEqual(TEXT("Reached final stage"), Match->GetStageNumber(), 7);
    if (!TestTrue(TEXT("Enter final combat"), Match->EnterCombat())) return false;
    if (!TestTrue(TEXT("Win final combat"), Match->ResolveCombat(true))) return false;
    TestEqual(TEXT("Final victory enters finished"), Match->GetPhase(), EEGMatchPhase::Finished);
    TestTrue(TEXT("Final finished phase records victory"), Audio->bLastResultVictory);
    TestNull(TEXT("Final victory has no background music"), Audio->MusicComponent.Get());
    if (!TestTrue(TEXT("Restart after final victory"), Match->StartRun())) return false;
    TestFalse(TEXT("Restart after final win resets result"), Audio->bResultKnown);
    if (!TestTrue(TEXT("Enter combat before component end"), Match->EnterCombat())) return false;
    if (!TestTrue(TEXT("Resolve combat before component end"), Match->ResolveCombat(false))) return false;

    const EEGMatchPhase BeforeEnd = Audio->ObservedPhase;
    Audio->EndPlay(EEndPlayReason::Quit);
    TestFalse(TEXT("EndPlay clears match reference"), Audio->MatchManager.IsValid());
    TestFalse(TEXT("EndPlay clears load handle"), Audio->LoadHandle.IsValid());
    TestNull(TEXT("EndPlay clears intro component"), Audio->IntroComponent.Get());
    if (!TestTrue(TEXT("Finish after component end"), Match->FinishSettlement())) return false;
    TestEqual(TEXT("Ended component no longer observes phase changes"), Audio->ObservedPhase, BeforeEnd);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGameAudioStigmataHoldLifecycleTest,
    "ElyGambit.GameAudio.StigmataHoldLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGGameAudioStigmataHoldLifecycleTest::RunTest(const FString& Parameters)
{
    if (!GEngine) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World = Instance->GetWorld();
    if (!TestNotNull(TEXT("Stigmata sound fixture world"), World)) return false;
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();
        World->EndPlay(EEndPlayReason::Quit);
        Instance->Shutdown();
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };

    APlayerController* Controller = World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Stigmata sound fixture player"), Controller)) return false;
    TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));
    Controller->SetPlayer(LocalPlayer.Get());
    UEGGameAudioComponent* Audio = NewObject<UEGGameAudioComponent>(Controller);
    if (!TestNotNull(TEXT("Stigmata sound fixture component"), Audio)) return false;
    Audio->RegisterComponent();
    if (!Controller->HasActorBegunPlay()) Controller->DispatchBeginPlay();
    if (!TestTrue(TEXT("Stigmata sound component has begun play"), Audio->HasBegunPlay())) return false;

    USoundWave* HoldSound = LoadObject<USoundWave>(nullptr,
        TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Hold.SW_Stigma_Hold"));
    if (!TestNotNull(TEXT("Actual configured hold sound loads"), HoldSound)) return false;
    Audio->Bindings.Reset();
    FEGGameAudioBinding Binding;
    Binding.Cue = EEGGameAudioCue::StigmaHold;
    Binding.Sound = HoldSound;
    Audio->Bindings.Add(Binding);
    Audio->LoadedSounds.Add(HoldSound);
    Audio->bAssetsReady = true; // The already-loaded sound is the only binding in this fixture.

    const bool bCanPlay = GEngine->UseSound() && World->GetAudioDevice().IsValid();
    if (bCanPlay)
    {
        Audio->StartLocalStigmataHold();
        UAudioComponent* First = Audio->StigmataHoldVoice.Get();
        if (!TestTrue(TEXT("Starting the hold creates a playing audio component"), IsValid(First) && First->IsPlaying())) return false;
        TWeakObjectPtr<UAudioComponent> Previous(First);
        TestEqual(TEXT("The hold voice is registered once"), Audio->UIVoices.Num(), 1);
        Audio->StartLocalStigmataHold();
        TestTrue(TEXT("Starting again stops the previous voice"), !Previous.IsValid() || !Previous->IsPlaying());
        TestTrue(TEXT("Starting again creates a new playing voice"), IsValid(Audio->StigmataHoldVoice) && Audio->StigmataHoldVoice->IsPlaying());
        TestEqual(TEXT("Restart leaves only one tracked UI voice"), Audio->UIVoices.Num(), 1);
        Audio->StopLocalStigmataHold();
        TestNull(TEXT("Stop clears the active hold voice"), Audio->StigmataHoldVoice.Get());
        TestEqual(TEXT("Stop removes the hold from tracked voices"), Audio->UIVoices.Num(), 0);
        Audio->StopLocalStigmataHold();
        TestEqual(TEXT("Repeated stop remains safe"), Audio->UIVoices.Num(), 0);
        Audio->StartLocalStigmataHold();
        if (!TestTrue(TEXT("A hold can restart after cancellation"), IsValid(Audio->StigmataHoldVoice) && Audio->StigmataHoldVoice->IsPlaying())) return false;
    }
    else
    {
        AddWarning(TEXT("No audio device: real hold playback was not checked; only component cleanup is covered."));
        UAudioComponent* SeedVoice = NewObject<UAudioComponent>(Controller);
        SeedVoice->RegisterComponent();
        Audio->StigmataHoldVoice = SeedVoice;
        Audio->UIVoices.Add(SeedVoice);
    }

    TWeakObjectPtr<UAudioComponent> EndingVoice(Audio->StigmataHoldVoice.Get());
    Audio->EndPlay(EEndPlayReason::Quit);
    TestNull(TEXT("EndPlay clears the hold voice"), Audio->StigmataHoldVoice.Get());
    TestEqual(TEXT("EndPlay clears all tracked UI voices"), Audio->UIVoices.Num(), 0);
    TestTrue(TEXT("EndPlay stops and unregisters the hold component"), !EndingVoice.IsValid() || (!EndingVoice->IsPlaying() && !EndingVoice->IsRegistered()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGameAudioSelectedBindingsTest,
    "ElyGambit.GameAudio.SelectedBindings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGGameAudioSelectedBindingsTest::RunTest(const FString& Parameters)
{
    struct FExpectedBinding
    {
        EEGGameAudioCue Cue;
        const TCHAR* ObjectPath;
        bool bLoop;
        bool bPlayWhenSilent;
        float MaxDuration = 0.f;
    };
    const FExpectedBinding Expected[] = {
        {EEGGameAudioCue::Preparation, TEXT("/Game/Assets/Shared/Audio/Elysian20261104/SW_EG_Preparation.SW_EG_Preparation"), true, true},
        {EEGGameAudioCue::CombatIntro, TEXT("/Game/Assets/Shared/Audio/Elysian20261104/SW_EG_CombatIntro.SW_EG_CombatIntro"), false, true},
        {EEGGameAudioCue::Battle, TEXT("/Game/Assets/Shared/Audio/Elysian20261104/SW_EG_Battle.SW_EG_Battle"), true, true},
        {EEGGameAudioCue::Boss, TEXT("/Game/Assets/Shared/Audio/Elysian20261104/SW_EG_Boss.SW_EG_Boss"), true, true},
        {EEGGameAudioCue::Victory, TEXT("/Game/Assets/Shared/Audio/Elysian20261104/SW_EG_Victory.SW_EG_Victory"), false, true},
        {EEGGameAudioCue::Defeat, TEXT("/Game/Assets/Shared/Audio/Elysian20261104/SW_EG_Defeat.SW_EG_Defeat"), false, true},
        {EEGGameAudioCue::Hover, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Hover.SW_EG_Hover"), false, false},
        {EEGGameAudioCue::Drag, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Drag.SW_EG_Drag"), false, false},
        {EEGGameAudioCue::ValidTarget, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_ValidTarget.SW_EG_ValidTarget"), false, false},
        {EEGGameAudioCue::Place, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Place.SW_EG_Place"), false, false},
        {EEGGameAudioCue::SellZone, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_SellZone.SW_EG_SellZone"), false, false},
        {EEGGameAudioCue::Sell, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Sell.SW_EG_Sell"), false, false},
        {EEGGameAudioCue::Refresh, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Refresh.SW_EG_Refresh"), false, false},
        {EEGGameAudioCue::Lock, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Lock.SW_EG_Lock"), false, false},
        {EEGGameAudioCue::Unlock, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Unlock.SW_EG_Unlock"), false, false},
        {EEGGameAudioCue::Cancel, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Cancel.SW_EG_Cancel"), false, false},
        {EEGGameAudioCue::Merge, TEXT("/Game/Assets/UI/Audio/Elysian20261104/SW_EG_Merge.SW_EG_Merge"), false, false},
        {EEGGameAudioCue::StigmaHover, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Hover.SW_Stigma_Hover"), false, false, .625f},
        {EEGGameAudioCue::StigmaDetails, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Details.SW_Stigma_Details"), false, false, .625f},
        {EEGGameAudioCue::StigmaHold, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Hold.SW_Stigma_Hold"), false, false, .625f},
        {EEGGameAudioCue::StigmaConfirm, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Confirm.SW_Stigma_Confirm"), false, false, .625f},
        {EEGGameAudioCue::StigmaLift, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Lift.SW_Stigma_Lift"), false, false, .625f},
        {EEGGameAudioCue::StigmaLand, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Land.SW_Stigma_Land"), false, false, .625f},
        {EEGGameAudioCue::StigmaTransition, TEXT("/Game/Assets/UI/Audio/StigmataSelectionC20261105/SW_Stigma_Transition.SW_Stigma_Transition"), false, false, .625f},
    };
    const UEGGameAudioComponent* Defaults = GetDefault<UEGGameAudioComponent>();
    if (!TestNotNull(TEXT("Configured audio component defaults"), Defaults)) return false;
    TestEqual(TEXT("Exactly the configured bindings"), Defaults->Bindings.Num(), static_cast<int32>(UE_ARRAY_COUNT(Expected)));
    TSet<EEGGameAudioCue> Seen;
    for (const FEGGameAudioBinding& Binding : Defaults->Bindings)
    {
        TestFalse(TEXT("No duplicate cue"), Seen.Contains(Binding.Cue));
        Seen.Add(Binding.Cue);
    }
    for (const FExpectedBinding& Item : Expected)
    {
        const FEGGameAudioBinding* Binding = Defaults->Bindings.FindByPredicate(
            [&Item](const FEGGameAudioBinding& Candidate) { return Candidate.Cue == Item.Cue; });
        const FString Prefix = FString::Printf(TEXT("Cue %d "), static_cast<int32>(Item.Cue));
        if (!TestNotNull(*(Prefix + TEXT("configured")), Binding)) continue;
        TestEqual(*(Prefix + TEXT("selected source path")), Binding->Sound.ToSoftObjectPath().ToString(), FString(Item.ObjectPath));
        USoundWave* Wave = Cast<USoundWave>(Binding->Sound.LoadSynchronous());
        if (!TestNotNull(*(Prefix + TEXT("SoundWave loads")), Wave)) continue;
        TestEqual(*(Prefix + TEXT("loop flag")), bool(Wave->bLooping), Item.bLoop);
        if (Item.MaxDuration > 0.f)
        {
            const float Duration = Wave->Duration;
            TestTrue(*FString::Printf(TEXT("%sduration %.4fs is positive and at most %.3fs"), *Prefix, Duration, Item.MaxDuration),
                FMath::IsFinite(Duration) && Duration > 0.f && Duration <= Item.MaxDuration + KINDA_SMALL_NUMBER);
        }
        if (Item.bPlayWhenSilent)
            TestEqual(*(Prefix + TEXT("virtualization")), Wave->VirtualizationMode, EVirtualizationMode::PlayWhenSilent);
        if (Item.Cue == EEGGameAudioCue::CombatIntro)
            TestTrue(TEXT("Combat intro is six seconds"), FMath::IsNearlyEqual(Wave->Duration, 6.f, 0.03f));
        if (Item.Cue == EEGGameAudioCue::Victory)
            TestTrue(TEXT("Victory uses the complete native result cue"), FMath::IsNearlyEqual(Wave->Duration, 7.864512f, 0.03f));
        if (Item.Cue == EEGGameAudioCue::Defeat)
            TestTrue(TEXT("Defeat uses the complete native result cue"), FMath::IsNearlyEqual(Wave->Duration, 6.873107f, 0.03f));
    }
    TestFalse(TEXT("Unselected error has no binding"), Seen.Contains(EEGGameAudioCue::Error));
    TestFalse(TEXT("Unselected purchase has no binding"), Seen.Contains(EEGGameAudioCue::Purchase));
    TestFalse(TEXT("Historical settlement background has no binding"), Seen.Contains(EEGGameAudioCue::Settlement));
    TestFalse(TEXT("Historical victory background has no binding"), Seen.Contains(EEGGameAudioCue::SettlementVictory));
    return true;
}

#endif
