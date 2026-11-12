#include "Components/EGGameAudioComponent.h"

#include "Components/AudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGRunDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/ConfigCacheIni.h"
#include "Sound/SoundBase.h"

namespace
{
constexpr float MusicFadeSeconds = 0.45f;
constexpr float ResultTransitionFadeSeconds = 0.2f;
constexpr double FeedbackIntervalSeconds = 0.12;
constexpr double StageDialogueStartWindowSeconds = 3.0;
constexpr int32 MaxUIVoices = 4;
const TCHAR* VolumeSection = TEXT("ElyGambit.Audio");

float SafeVolume(float Volume, float Default)
{
    return FMath::IsFinite(Volume) ? FMath::Clamp(Volume, 0.f, 1.f) : Default;
}

void DestroyAudio(UAudioComponent* Audio)
{
    if (IsValid(Audio))
    {
        Audio->Stop();
        Audio->DestroyComponent();
    }
}
}

UEGGameAudioComponent::UEGGameAudioComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bTickEvenWhenPaused = true;
    BossEffectSounds.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Entrance20261106/Audio/SW_HumanEntrance_Gather.SW_HumanEntrance_Gather"))));
    BossEffectSounds.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Entrance20261106/Audio/SW_HumanEntrance_Reveal.SW_HumanEntrance_Reveal"))));

    // Text from the original audition manifest; most lines are provisional
    // machine transcriptions and should be reviewed against the recordings.
    struct FVoiceText { const TCHAR* Asset; const TCHAR* Speaker; const TCHAR* Text; };
    const FVoiceText VoiceText[] = {
        {TEXT("SW_Master_Elysia_Victory_01"), TEXT("爱莉希雅"), TEXT("美丽的女孩子什么都能做到♪")},
        {TEXT("SW_Master_Elysia_Victory_07"), TEXT("爱莉希雅"), TEXT("嗯哼♪ 力量流走了哦。")},
        {TEXT("SW_Master_Elysia_Victory_08"), TEXT("爱莉希雅"), TEXT("哼，抓住你了。")},
        {TEXT("SW_Master_Elysia_Defeat_02"), TEXT("爱莉希雅"), TEXT("啊，失败了呢。")},
        {TEXT("SW_Master_Elysia_Defeat_09"), TEXT("爱莉希雅"), TEXT("见好就收哦。")},
        {TEXT("SW_Master_Corruption_Victory_03"), TEXT("侵蚀之律者"), TEXT("哇，力量流走了。")},
        {TEXT("SW_Master_Corruption_Victory_04"), TEXT("侵蚀之律者"), TEXT("这束鲜花就心怀感激地收下吧。")},
        {TEXT("SW_Master_Corruption_Defeat_05"), TEXT("侵蚀之律者"), TEXT("再打下去我会哭的。")},
        {TEXT("SW_Master_Corruption_Defeat_06"), TEXT("侵蚀之律者"), TEXT("好啦，可以了。")},
    };
    for (const FVoiceText& Entry : VoiceText)
    {
        FEGMasterVoiceLine& Line = MasterVoiceLines.AddDefaulted_GetRef();
        Line.Sound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(
            TEXT("/Game/Assets/Shared/Audio/Elysian20261104/MasterVoices20261105/%s.%s"), Entry.Asset, Entry.Asset)));
        Line.Speaker = FText::FromString(Entry.Speaker);
        Line.Text = FText::FromString(Entry.Text);
    }

    // Explicit user selection, separate from the retired random candidate pool.
    struct FStorySelection { int32 Stage; const TCHAR* Asset; const TCHAR* Text; float Seconds; float StartOffset; };
    const FStorySelection Story[] = {
        {3,TEXT("SW_Corruption_Stage07_01"),TEXT("我绝不会放过侵蚀你的机会。"),2.376f,.30f},
        {6,TEXT("SW_Corruption_Stage03_01"),TEXT("力量上的差距依然是无法弥补的，不是吗？"),4.981f,0.f},
        {7,TEXT("SW_Corruption_Stage06_02"),TEXT("永远不要挑战「无法战胜」的敌人……这才是生存的智慧，不是吗？"),7.085f,0.f},
        {8,TEXT("SW_Corruption_Stage06_01"),TEXT("提前声明，这一次，我不会手下留情了哦。"),4.8f,0.f}
    };
    for(const auto& Entry:Story)
    {
        auto& Record=SelectedStoryLines.AddDefaulted_GetRef();Record.StageNumber=Entry.Stage;
        Record.Line.Sound=TSoftObjectPtr<USoundBase>(FSoftObjectPath(FString::Printf(
            TEXT("/Game/Assets/Shared/Audio/Elysian20261104/CorruptionStory20261106/%s.%s"),Entry.Asset,Entry.Asset)));
        Record.Line.Speaker=FText::FromString(TEXT("侵蚀之律者"));
        Record.Line.Text=FText::FromString(Entry.Text);Record.Line.DurationSeconds=Entry.Seconds;
        Record.Line.StartOffsetSeconds=Entry.StartOffset;
    }
}

void UEGGameAudioComponent::InitializeForMatch(UEGMatchManager* Match)
{
    const APlayerController* Controller = Cast<APlayerController>(GetOwner());
    if (!IsValid(Controller) || !Controller->IsLocalController() ||
        GetNetMode() == NM_DedicatedServer || !IsValid(Match))
    {
        return;
    }
    if (MatchManager.Get() == Match)
    {
        SyncPhase();
        return;
    }

    if (UEGMatchManager* Previous = MatchManager.Get())
    {
        Previous->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
        Previous->OnStagePrepared.RemoveDynamic(this, &ThisClass::HandleStagePrepared);
        Previous->OnBattleResolved.RemoveDynamic(this, &ThisClass::HandleBattleResolved);
    }
    ++LoadSerial;
    if (LoadHandle.IsValid())
    {
        LoadHandle->CancelHandle();
        LoadHandle.Reset();
    }
    StopIntro();
    CancelScheduledStageDialogue();
    StopResultVoices();
    DestroyAudio(MusicComponent);
    MusicComponent = nullptr;
    DestroyAudio(StingerComponent);
    StingerComponent = nullptr;
    for (UAudioComponent* Audio : FadingMusic) DestroyAudio(Audio);
    FadingMusic.Reset();
    StopLocalStigmataHold();
    StopLocalBossEffects();
    for (UAudioComponent* Audio : UIVoices) DestroyAudio(Audio);
    UIVoices.Reset();
    LoadedSounds.Reset();
    bEndingAudioOverride = false;
    bAssetsReady = false;
    bBackgroundSet = false;
    bObservedPhase = false;
    bResultKnown = false;
    bPendingResultSound = false;
    ObservedStage = INDEX_NONE;
    CombatRevision = 0;
    PlayedCombatRevision = 0;
    ResolvedCombatRevision = 0;
    ResultVoiceCombatRevision = 0;
    bResultVoiceStarted = false;
    StageDialogueCombatStartTime = -1.0;
    LastHoverAudioTime = -1.0;
    LastValidTargetAudioTime = -1.0;
    LastSellZoneAudioTime = -1.0;
    MatchManager = Match;
    if (!bSettingsLoaded) LoadVolumeSettings();

    Match->OnPhaseChanged.AddUniqueDynamic(this, &ThisClass::HandlePhaseChanged);
    Match->OnStagePrepared.AddUniqueDynamic(this, &ThisClass::HandleStagePrepared);
    Match->OnBattleResolved.AddUniqueDynamic(this, &ThisClass::HandleBattleResolved);
    if (Match->HasStarted()) HandlePhaseChanged(Match->GetPhase());

    TArray<FSoftObjectPath> Paths;
    for (const FEGGameAudioBinding& Binding : Bindings)
    {
        if (!Binding.Sound.IsNull()) Paths.AddUnique(Binding.Sound.ToSoftObjectPath());
    }
    const auto AddVoicePaths = [&Paths](const TArray<TSoftObjectPtr<USoundBase>>& Voices)
    {
        for (const TSoftObjectPtr<USoundBase>& Voice : Voices)
            if (!Voice.IsNull()) Paths.AddUnique(Voice.ToSoftObjectPath());
    };
    AddVoicePaths(ElysiaVictoryVoices);
    AddVoicePaths(ElysiaDefeatVoices);
    AddVoicePaths(CorruptionVictoryVoices);
    AddVoicePaths(CorruptionDefeatVoices);
    AddVoicePaths(BossEffectSounds);
    for (const FEGStageDialogueLine& Entry : StageDialogueLines)
        if (!Entry.Line.Sound.IsNull()) Paths.AddUnique(Entry.Line.Sound.ToSoftObjectPath());
    for (const auto& Entry : SelectedStoryLines)
        if (!Entry.Line.Sound.IsNull()) Paths.AddUnique(Entry.Line.Sound.ToSoftObjectPath());
    const uint64 Serial = LoadSerial;
    if (Paths.IsEmpty())
    {
        AssetsLoaded(Serial);
    }
    else
    {
        LoadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
            Paths, FStreamableDelegate::CreateWeakLambda(this, [this, Serial]() { AssetsLoaded(Serial); }));
    }
}

UEGGameAudioComponent* UEGGameAudioComponent::FindFor(const UObject* WorldContextObject)
{
    if (!GEngine || !WorldContextObject) return nullptr;
    UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
    APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
    return Controller && Controller->IsLocalController() && World->GetNetMode() != NM_DedicatedServer
        ? Controller->FindComponentByClass<UEGGameAudioComponent>() : nullptr;
}

void UEGGameAudioComponent::PlayUI(const UObject* WorldContextObject, EEGGameAudioCue Cue)
{
    if (UEGGameAudioComponent* Audio = FindFor(WorldContextObject)) Audio->PlayLocalUI(Cue);
}

void UEGGameAudioComponent::PlayBossEffect(const UObject* WorldContextObject,UEGBattleContextComponent* Battle,EEGBossEffectSound Cue,FGuid PresentationToken)
{
    if(auto* Audio=FindFor(WorldContextObject))Audio->PlayLocalBossEffect(Battle,Cue,PresentationToken);
}
void UEGGameAudioComponent::StopBossEffects(const UObject* WorldContextObject,const UEGBattleContextComponent* Battle)
{
    if(auto* Audio=FindFor(WorldContextObject);Audio&&Audio->BossEffectBattle.Get()==Battle)Audio->StopLocalBossEffects();
}

bool UEGGameAudioComponent::CanAdvanceBossEffect(const UEGBattleContextComponent* Battle,FGuid Round,FGuid PresentationToken)
{
    if(!IsValid(Battle)||!Battle->IsBattleActive()||!Round.IsValid()||Battle->GetRoundId()!=Round)return false;
    if(Battle->HasPresentationHold(PresentationToken))return Battle->CanAdvancePresentationVisualsForToken(PresentationToken);
    // Once our hold is gone, no unrelated dialogue/presentation hold should
    // be treated as permission for this sound to continue.
    return !Battle->HasPresentationHold()&&Battle->CanAdvancePresentationVisuals();
}

void UEGGameAudioComponent::StartStigmataHold(const UObject* WorldContextObject)
{
    if (UEGGameAudioComponent* Audio = FindFor(WorldContextObject)) Audio->StartLocalStigmataHold();
}

void UEGGameAudioComponent::StopStigmataHold(const UObject* WorldContextObject)
{
    if (UEGGameAudioComponent* Audio = FindFor(WorldContextObject)) Audio->StopLocalStigmataHold();
}

void UEGGameAudioComponent::AssetsLoaded(uint64 Serial)
{
    if (Serial != LoadSerial || !MatchManager.IsValid()) return;
    LoadedSounds.Reset();
    for (const FEGGameAudioBinding& Binding : Bindings)
    {
        if (USoundBase* Sound = Binding.Sound.Get())
        {
            LoadedSounds.AddUnique(Sound);
        }
        else if (!Binding.Sound.IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("Game audio failed to load %s"), *Binding.Sound.ToSoftObjectPath().ToString());
        }
    }
    const auto HoldVoices = [this](const TArray<TSoftObjectPtr<USoundBase>>& Voices)
    {
        for (const TSoftObjectPtr<USoundBase>& Voice : Voices)
        {
            if (USoundBase* Sound = Voice.Get()) LoadedSounds.AddUnique(Sound);
            else if (!Voice.IsNull())
                UE_LOG(LogTemp, Warning, TEXT("Master voice failed to load %s"), *Voice.ToSoftObjectPath().ToString());
        }
    };
    HoldVoices(ElysiaVictoryVoices);
    HoldVoices(ElysiaDefeatVoices);
    HoldVoices(CorruptionVictoryVoices);
    HoldVoices(CorruptionDefeatVoices);
    HoldVoices(BossEffectSounds);
    for (const FEGStageDialogueLine& Entry : StageDialogueLines)
    {
        if (USoundBase* Sound = Entry.Line.Sound.Get()) LoadedSounds.AddUnique(Sound);
        else if (!Entry.Line.Sound.IsNull())
            UE_LOG(LogTemp, Warning, TEXT("Stage dialogue failed to load %s"), *Entry.Line.Sound.ToSoftObjectPath().ToString());
    }
    bAssetsReady = true;
    for(const auto& Entry:SelectedStoryLines)
        if(auto* Sound=Entry.Line.Sound.Get())LoadedSounds.AddUnique(Sound);
    SyncPhase();
    UEGMatchManager* Match = MatchManager.Get();
    if (!bEndingAudioOverride && bPendingResultSound && Match && Match->GetStageNumber() == ResultStage &&
        Match->GetPhase() != EEGMatchPhase::Preparation && Match->GetPhase() != EEGMatchPhase::Combat &&
        ResolvedCombatRevision == CombatRevision)
    {
        bPendingResultSound = false;
        PlayResultStinger(bLastResultVictory);
        PlayResultVoices(bLastResultVictory);
    }
}

USoundBase* UEGGameAudioComponent::GetSound(EEGGameAudioCue Cue) const
{
    for (const FEGGameAudioBinding& Binding : Bindings)
    {
        if (Binding.Cue == Cue) return Binding.Sound.Get();
    }
    return nullptr;
}

void UEGGameAudioComponent::HandlePhaseChanged(EEGMatchPhase Phase)
{
    UEGMatchManager* Match = MatchManager.Get();
    if (!Match || !Match->HasStarted()) return;
    const int32 Stage = Match->GetStageNumber();
    if (Phase != EEGMatchPhase::Finished) bEndingAudioOverride = false;
    const bool bChanged = !bObservedPhase || Phase != ObservedPhase || Stage != ObservedStage;
    if (!bChanged) return;

    if (Phase == EEGMatchPhase::Combat && (!bObservedPhase || ObservedPhase != Phase || Stage != ObservedStage))
    {
        ++CombatRevision;
        bResultKnown = false;
        // A component first attached in the middle of combat has no reliable
        // start time; do not guess and play an entrance line late.
        UWorld* World = GetWorld();
        StageDialogueCombatStartTime = World && bObservedPhase && ObservedPhase == EEGMatchPhase::Preparation && ObservedStage == Stage
            ? World->GetTimeSeconds() : -1.0;
    }
    if (Phase == EEGMatchPhase::Preparation || Phase == EEGMatchPhase::Combat)
    {
        // Let an already-started result line finish across the next
        // preparation; a new fight or a restart from Finished cancels it.
        // Never start a result late in preparation: clear the pending load.
        if (Phase == EEGMatchPhase::Combat || !bObservedPhase || ObservedPhase == EEGMatchPhase::Finished)
            StopResultVoices();
        if (Phase == EEGMatchPhase::Preparation) bResultKnown = false;
        bPendingResultSound = false;
        if (IsValid(StingerComponent) && StingerComponent->IsPlaying())
        {
            StingerComponent->FadeOut(ResultTransitionFadeSeconds, 0.f);
            FadingMusic.Add(StingerComponent);
        }
        else DestroyAudio(StingerComponent);
        StingerComponent = nullptr;
    }
    if (Phase != EEGMatchPhase::Combat)
    {
        CancelScheduledStageDialogue();
        if (bStageDialogueActive) StopResultVoices();
        StopIntro();
    }
    if (Phase != EEGMatchPhase::Preparation && Phase != EEGMatchPhase::Combat) StopBackground();
    ObservedPhase = Phase;
    ObservedStage = Stage;
    bObservedPhase = true;
    SyncPhase();
}

void UEGGameAudioComponent::HandleStagePrepared(int32 StageNumber, EEGStageType StageType)
{
    if (UEGMatchManager* Match = MatchManager.Get()) HandlePhaseChanged(Match->GetPhase());
}

void UEGGameAudioComponent::SyncPhase()
{
    if (bEndingAudioOverride) return;
    UEGMatchManager* Match = MatchManager.Get();
    if (!bAssetsReady || !Match || !Match->HasStarted() || !bObservedPhase) return;
    switch (Match->GetPhase())
    {
    case EEGMatchPhase::Preparation:
        StartBackground(EEGGameAudioCue::Preparation);
        break;
    case EEGMatchPhase::Combat:
        if (PlayedCombatRevision != CombatRevision)
        {
            PlayedCombatRevision = CombatRevision;
            StartCombatIntro();
            // Do not start stage lines before the user's selection, even if a
            // serialized Blueprint component still contains older candidates.
            // The playback machinery is retained for the later integration.
        }
        break;
    case EEGMatchPhase::Settlement:
    case EEGMatchPhase::Resolution:
    case EEGMatchPhase::Reward:
    case EEGMatchPhase::Finished:
        StopBackground();
        break;
    }
}

void UEGGameAudioComponent::StartBackground(EEGGameAudioCue Cue)
{
    if (bBackgroundSet && BackgroundCue == Cue && IsValid(MusicComponent) && MusicComponent->IsPlaying()) return;
    if (IsValid(MusicComponent))
    {
        MusicComponent->FadeOut(MusicFadeSeconds, 0.f);
        FadingMusic.Add(MusicComponent);
        MusicComponent = nullptr;
    }
    FadingMusic.RemoveAll([](const TObjectPtr<UAudioComponent>& Audio)
    {
        return !IsValid(Audio) || !Audio->IsPlaying();
    });
    bBackgroundSet = false;
    USoundBase* Sound = GetSound(Cue);
    if (!Sound) return;
    MusicComponent = UGameplayStatics::CreateSound2D(this, Sound, MusicVolume, 1.f, 0.f, nullptr, false, true);
    if (MusicComponent)
    {
        BackgroundCue = Cue;
        bBackgroundSet = true;
        MusicComponent->FadeIn(MusicFadeSeconds, 1.f);
    }
}

void UEGGameAudioComponent::StopBackground()
{
    DestroyAudio(MusicComponent);
    MusicComponent = nullptr;
    for (UAudioComponent* Audio : FadingMusic) DestroyAudio(Audio);
    FadingMusic.Reset();
    bBackgroundSet = false;
}

void UEGGameAudioComponent::StartCombatIntro()
{
    StopIntro();
    if (IsValid(MusicComponent))
    {
        MusicComponent->FadeOut(MusicFadeSeconds, 0.f);
        FadingMusic.Add(MusicComponent);
        MusicComponent = nullptr;
    }
    bBackgroundSet = false;
    IntroCombatRevision = CombatRevision;
    IntroStage = ObservedStage;
    USoundBase* Sound = GetSound(EEGGameAudioCue::CombatIntro);
    if (Sound)
    {
        IntroComponent = UGameplayStatics::CreateSound2D(this, Sound, MusicVolume, 1.f, 0.f, nullptr, false, true);
        if (IntroComponent)
        {
            IntroComponent->OnAudioFinished.AddDynamic(this, &ThisClass::HandleIntroFinished);
            IntroComponent->OnAudioPlayStateChangedNative.AddUObject(this, &ThisClass::HandleIntroPlayState);
            IntroComponent->Play();
            return;
        }
    }
    HandleIntroFinished();
}

void UEGGameAudioComponent::StopIntro()
{
    if (IsValid(IntroComponent))
    {
        IntroComponent->OnAudioFinished.RemoveDynamic(this, &ThisClass::HandleIntroFinished);
        IntroComponent->OnAudioPlayStateChangedNative.RemoveAll(this);
        DestroyAudio(IntroComponent);
    }
    IntroComponent = nullptr;
    IntroCombatRevision = 0;
    IntroStage = INDEX_NONE;
}

void UEGGameAudioComponent::HandleIntroFinished()
{
    if (IsValid(IntroComponent))
    {
        IntroComponent->OnAudioFinished.RemoveDynamic(this, &ThisClass::HandleIntroFinished);
        IntroComponent->OnAudioPlayStateChangedNative.RemoveAll(this);
    }
    IntroComponent = nullptr;
    UEGMatchManager* Match = MatchManager.Get();
    if (!Match || Match->GetPhase() != EEGMatchPhase::Combat ||
        Match->GetStageNumber() != IntroStage || IntroCombatRevision != CombatRevision) return;

    bool bBoss = Match->GetStageType() == EEGStageType::Boss || Match->GetStageType() == EEGStageType::Corruption;
    const AEGDemoGameMode* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AEGDemoGameMode>() : nullptr;
    const UEGRunDefinition* Run = Mode ? Mode->RunDefinition.Get() : nullptr;
    const int32 StageIndex = Match->GetStageNumber() - 1;
    if (Run && Run->bSevenStageRules && Run->Stages.IsValidIndex(StageIndex))
    {
        bBoss |= Match->GetStageNumber() == 7;
        for (const FEGEnemySpawn& Enemy : Run->Stages[StageIndex].Enemies) bBoss |= Enemy.bCorruptedCopy;
    }
    StartBackground(bBoss ? EEGGameAudioCue::Boss : EEGGameAudioCue::Battle);
}

void UEGGameAudioComponent::HandleIntroPlayState(const UAudioComponent* Audio, EAudioComponentPlayState State)
{
    if (Audio == IntroComponent && State == EAudioComponentPlayState::Stopped) HandleIntroFinished();
}

void UEGGameAudioComponent::HandleBattleResolved(bool bVictory, int32 Corruption)
{
    UEGMatchManager* Match = MatchManager.Get();
    if (!Match) return;
    if (bResultKnown && ResolvedCombatRevision == CombatRevision)
    {
        const bool bCorrectedToDefeat = bLastResultVictory && !bVictory && Match->GetPhase() == EEGMatchPhase::Finished;
        if (!bCorrectedToDefeat) return;
    }
    bResultKnown = true;
    bLastResultVictory = bVictory;
    ResolvedCombatRevision = CombatRevision;
    ResultStage = Match->GetStageNumber();
    StopIntro();
    StopBackground();
    StopResultVoices();
    if (!bAssetsReady)
    {
        bPendingResultSound = true;
        return;
    }
    bPendingResultSound = false;
    PlayResultStinger(bVictory);
    PlayResultVoices(bVictory);
}

void UEGGameAudioComponent::PlayResultStinger(bool bVictory)
{
    if (bEndingAudioOverride) return;
    DestroyAudio(StingerComponent);
    StingerComponent = nullptr;
    if (USoundBase* Sound = GetSound(bVictory ? EEGGameAudioCue::Victory : EEGGameAudioCue::Defeat))
        StingerComponent = UGameplayStatics::SpawnSound2D(this, Sound, MusicVolume);
}

void UEGGameAudioComponent::QueueResultVoices(bool bVictory)
{
    PendingResultVoices.Reset();
    TArray<USoundBase*> Available;
    const auto AddLoaded = [&Available](const TArray<TSoftObjectPtr<USoundBase>>& Voices)
    {
        for (const TSoftObjectPtr<USoundBase>& Voice : Voices)
            if (USoundBase* Sound = Voice.Get()) Available.Add(Sound);
    };

    // Every available eligible line has one chance; either side can speak,
    // but a result never queues a second line.
    AddLoaded(bVictory ? ElysiaVictoryVoices : CorruptionVictoryVoices);
    AddLoaded(bVictory ? CorruptionDefeatVoices : ElysiaDefeatVoices);
    if (!Available.IsEmpty()) PendingResultVoices.Add(Available[FMath::RandRange(0, Available.Num() - 1)]);
}

void UEGGameAudioComponent::PlayResultVoices(bool bVictory)
{
    if (bEndingAudioOverride) return;
    StopResultVoices();
    // A rare external corruption update may correct a prior victory after
    // audio has begun. Stop that line but never start a second one in the
    // same combat; an already-audible fragment cannot be taken back.
    if (bResultVoiceStarted && ResultVoiceCombatRevision == CombatRevision) return;
    QueueResultVoices(bVictory);
    PlayNextResultVoice();
}

void UEGGameAudioComponent::ScheduleStageDialogue()
{
    CancelScheduledStageDialogue();
    UWorld* World = GetWorld();
    UEGMatchManager* Match = MatchManager.Get();
    if (!World || !Match || Match->GetPhase() != EEGMatchPhase::Combat) return;
    const double Now = World->GetTimeSeconds();
    if (StageDialogueCombatStartTime < 0.0 || Now < StageDialogueCombatStartTime ||
        Now - StageDialogueCombatStartTime > StageDialogueStartWindowSeconds) return;
    const int32 Stage = Match->GetStageNumber();
    const bool bHasCandidate = StageDialogueLines.ContainsByPredicate([Stage](const FEGStageDialogueLine& Entry)
    {
        return Entry.StageNumber == Stage && Entry.Line.Sound.Get() != nullptr;
    });
    if (!bHasCandidate) return;
    const uint64 Revision = CombatRevision;
    // An empty board enters and resolves Combat synchronously. Waiting one
    // tick avoids beginning a line for a fight that never actually ran.
    StageDialogueTimer = World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this, Stage, Revision]()
    {
        PlayStageDialogue(Stage, Revision);
    }));
}

void UEGGameAudioComponent::CancelScheduledStageDialogue()
{
    if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(StageDialogueTimer);
    StageDialogueTimer.Invalidate();
}

void UEGGameAudioComponent::PlayStageDialogue(int32 Stage, uint64 Revision)
{
    UEGMatchManager* Match = MatchManager.Get();
    UWorld* World = GetWorld();
    if (!bAssetsReady || !World || !Match || Match->GetPhase() != EEGMatchPhase::Combat ||
        Match->GetStageNumber() != Stage || CombatRevision != Revision) return;
    StageDialogueTimer.Invalidate();
    const double Now = World->GetTimeSeconds();
    if (StageDialogueCombatStartTime < 0.0 || Now < StageDialogueCombatStartTime ||
        Now - StageDialogueCombatStartTime > StageDialogueStartWindowSeconds) return;
    if (bResultDialogueActive) return;
    TArray<const FEGMasterVoiceLine*> Available;
    for (const FEGStageDialogueLine& Entry : StageDialogueLines)
        if (Entry.StageNumber == Stage && Entry.Line.Sound.Get()) Available.Add(&Entry.Line);
    if (Available.IsEmpty()) return;
    const FEGMasterVoiceLine& Line = *Available[FMath::RandRange(0, Available.Num() - 1)];
    StartDialogueVoice(Line.Sound.Get(), Line, true);
}

FEGMasterVoiceLine UEGGameAudioComponent::GetStoryDialogue(int32 StageNumber,bool bPhaseTwo) const
{
    const int32 Key=bPhaseTwo?8:StageNumber;
    for(const auto& Entry:SelectedStoryLines)if(Entry.StageNumber==Key)return Entry.Line;
    return FEGMasterVoiceLine();
}

bool UEGGameAudioComponent::StartStoryDialogue(int32 StageNumber,bool bPhaseTwo)
{
    auto* Match=MatchManager.Get();
    auto* World=GetWorld();
    auto* Mode=World?Cast<AEGDemoGameMode>(World->GetAuthGameMode()):nullptr;
    if(!Match||!World||!Mode||Match->GetPhase()!=EEGMatchPhase::Combat||Match->GetStageNumber()!=StageNumber)return false;
    auto* Context=Mode->FindComponentByClass<UEGBattleContextComponent>();
    if(!Context||!Context->IsBattleActive())return false;
    const auto Line=GetStoryDialogue(StageNumber,bPhaseTwo);
    if(Line.Text.IsEmpty())return false;
    StopResultVoices();
    ActiveResultDialogue=Line;
    auto* Sound=Line.Sound.Get();
    if(Sound)
    {
        LoadedSounds.AddUnique(Sound);
        ResultVoiceComponent=UGameplayStatics::CreateSound2D(this,Sound,1.f,1.f,0.f,nullptr,false,true);
        ActiveResultDialogue.StartOffsetSeconds=FMath::Clamp(Line.StartOffsetSeconds,0.f,FMath::Max(0.f,Sound->GetDuration()-.01f));
        ActiveResultDialogue.DurationSeconds=Sound->GetDuration()-ActiveResultDialogue.StartOffsetSeconds;
        if(ResultVoiceComponent)ResultVoiceComponent->OnAudioFinished.AddDynamic(this,&ThisClass::HandleResultVoiceFinished);
    }
    bStageDialogueActive=true;bResultDialogueActive=true;
    // Story dialogue owns a separate hold; completion releases only this token.
    StoryBattle=Context;StoryRound=Context->GetRoundId();
    StoryHoldToken=FGuid::NewGuid();Context->SetPresentationHold(StoryHoldToken,true);
    // Also completes without an audio device or a successfully loaded SoundWave.
    const float Duration=FMath::Clamp(ActiveResultDialogue.DurationSeconds,.5f,12.f);
    StoryLastRealSeconds=FPlatformTime::Seconds();StoryDeadlineRealSeconds=StoryLastRealSeconds+Duration+.15;
    const TWeakObjectPtr<UAudioComponent> StartingVoice=ResultVoiceComponent;
    OnResultDialogueStarted.Broadcast(ActiveResultDialogue);
    if(bStageDialogueActive&&StartingVoice.IsValid()&&ResultVoiceComponent==StartingVoice.Get())
    {
        StartingVoice->Play(ActiveResultDialogue.StartOffsetSeconds);
        if(StartingVoice.IsValid()&&StoryBattle.IsValid())StartingVoice->SetPaused(!StoryBattle->CanAdvancePresentationClock());
    }
    return true;
}

void UEGGameAudioComponent::ReleaseStoryContext()
{
    StoryDeadlineRealSeconds=StoryLastRealSeconds=0.;
    if(auto* World=GetWorld())World->GetTimerManager().ClearTimer(StoryCompletionTimer);
    if(auto* Context=StoryBattle.Get();Context&&Context->GetRoundId()==StoryRound)Context->SetPresentationHold(StoryHoldToken,false);
    StoryBattle.Reset();StoryRound.Invalidate();StoryHoldToken.Invalidate();
}

void UEGGameAudioComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(DeltaTime,TickType,TickFunction);
    if(!BossEffectVoices.IsEmpty())
    {
        const auto* Context=BossEffectBattle.Get();
        if(!Context||!Context->IsBattleActive()||Context->GetRoundId()!=BossEffectRound)StopLocalBossEffects();
        else
        {
            const bool bPause=!CanAdvanceBossEffect(Context,BossEffectRound,BossEffectPresentationToken);
            for(const auto& Voice:BossEffectVoices)if(IsValid(Voice.Get()))Voice->SetPaused(bPause);
            BossEffectVoices.RemoveAll([](const TObjectPtr<UAudioComponent>& Voice)
                {return !IsValid(Voice)||(Voice->GetPlayState()!=EAudioComponentPlayState::Paused&&!Voice->IsPlaying());});
        }
    }
    if(bStageDialogueActive&&StoryDeadlineRealSeconds>0.)
    {
        auto* Context=StoryBattle.Get();
        if(!Context||!Context->IsBattleActive()||!StoryRound.IsValid()||Context->GetRoundId()!=StoryRound)SkipStoryDialogue();
        else
        {
            const double Now=FPlatformTime::Seconds();
            const bool bCanAdvance=Context->CanAdvancePresentationClock();
            if(!bCanAdvance)StoryDeadlineRealSeconds+=FMath::Max(0.,Now-StoryLastRealSeconds);
            StoryLastRealSeconds=Now;
            if(IsValid(ResultVoiceComponent))ResultVoiceComponent->SetPaused(!bCanAdvance);
            if(bCanAdvance&&Now>=StoryDeadlineRealSeconds)SkipStoryDialogue();
        }
    }
}

void UEGGameAudioComponent::PlayLocalBossEffect(UEGBattleContextComponent* Context,EEGBossEffectSound Cue,FGuid PresentationToken)
{
    if(!bAssetsReady||!IsValid(Context)||!Context->IsBattleActive()||!Context->GetRoundId().IsValid()||!GetWorld())return;
    if(BossEffectBattle.Get()!=Context||BossEffectRound!=Context->GetRoundId())StopLocalBossEffects();
    const int32 Index=Cue==EEGBossEffectSound::BarrierBreak?0:1;
    if(!BossEffectSounds.IsValidIndex(Index))return;
    auto* Sound=BossEffectSounds[Index].Get();if(!Sound)return;
    BossEffectVoices.RemoveAll([](const TObjectPtr<UAudioComponent>& Voice)
        {return !IsValid(Voice)||(Voice->GetPlayState()!=EAudioComponentPlayState::Paused&&!Voice->IsPlaying());});
    if(BossEffectVoices.Num()>=4)return;
    const float Pitch=Cue==EEGBossEffectSound::BarrierFailure?.8f:1.f;
    if(auto* Voice=UGameplayStatics::SpawnSound2D(this,Sound,UISoundVolume,Pitch))
    {
        BossEffectBattle=Context;BossEffectRound=Context->GetRoundId();BossEffectPresentationToken=PresentationToken;BossEffectVoices.Add(Voice);
        Voice->SetPaused(!CanAdvanceBossEffect(Context,BossEffectRound,BossEffectPresentationToken));
    }
}
void UEGGameAudioComponent::StopLocalBossEffects()
{
    for(const auto& Voice:BossEffectVoices)DestroyAudio(Voice.Get());
    BossEffectVoices.Reset();BossEffectBattle.Reset();BossEffectRound.Invalidate();BossEffectPresentationToken.Invalidate();
}

void UEGGameAudioComponent::SkipStoryDialogue()
{
    if(bStageDialogueActive)StopResultVoices();
}

bool UEGGameAudioComponent::StartDialogueVoice(USoundBase* Sound, const FEGMasterVoiceLine& Line, bool bStageDialogue)
{
    if (!IsValid(Sound) || bResultDialogueActive) return false;
    // The dialogue slider remains separate from music and UI as before.
    ResultVoiceComponent = UGameplayStatics::CreateSound2D(this, Sound, 1.f, 1.f, 0.f, nullptr, false, true);
    if (!IsValid(ResultVoiceComponent)) return false;
    ResultVoiceComponent->OnAudioFinished.AddDynamic(this, &ThisClass::HandleResultVoiceFinished);
    ActiveResultDialogue = Line;
    ActiveResultDialogue.Sound = Sound;
    ActiveResultDialogue.DurationSeconds = Sound->GetDuration();
    bResultDialogueActive = true;
    bStageDialogueActive = bStageDialogue;
    if (!bStageDialogue)
    {
        bResultVoiceStarted = true;
        ResultVoiceCombatRevision = CombatRevision;
    }
    TWeakObjectPtr<UAudioComponent> StartingVoice = ResultVoiceComponent;
    OnResultDialogueStarted.Broadcast(ActiveResultDialogue);
    // A listener may synchronously cancel or replace the result.
    if (!StartingVoice.IsValid() || ResultVoiceComponent != StartingVoice.Get() || !bResultDialogueActive) return false;
    ResultVoiceComponent->Play();
    return true;
}

void UEGGameAudioComponent::PlayNextResultVoice()
{
    while (!PendingResultVoices.IsEmpty())
    {
        USoundBase* Sound = PendingResultVoices[0];
        PendingResultVoices.RemoveAt(0);
        if (!IsValid(Sound)) continue;
        const FEGMasterVoiceLine* Line = MasterVoiceLines.FindByPredicate([Sound](const FEGMasterVoiceLine& Candidate)
        {
            return Candidate.Sound.ToSoftObjectPath() == FSoftObjectPath(Sound);
        });
        if (StartDialogueVoice(Sound, Line ? *Line : FEGMasterVoiceLine(), false)) return;
    }
}

void UEGGameAudioComponent::HandleResultVoiceFinished()
{
    ReleaseStoryContext();
    if (IsValid(ResultVoiceComponent))
        ResultVoiceComponent->OnAudioFinished.RemoveDynamic(this, &ThisClass::HandleResultVoiceFinished);
    ResultVoiceComponent = nullptr;
    bStageDialogueActive = false;
    if (bResultDialogueActive)
    {
        bResultDialogueActive = false;
        ActiveResultDialogue = FEGMasterVoiceLine();
        OnResultDialogueEnded.Broadcast();
    }
    PlayNextResultVoice();
}

void UEGGameAudioComponent::SetEndingAudioOverride(bool bEnabled)
{
    bEndingAudioOverride = bEnabled;
    if (!bEnabled) return;
    bPendingResultSound = false;
    CancelScheduledStageDialogue();
    StopResultVoices();
    StopIntro();
    StopBackground();
    DestroyAudio(StingerComponent);
    StingerComponent = nullptr;
    for (UAudioComponent* Audio : FadingMusic) DestroyAudio(Audio);
    FadingMusic.Reset();
    StopLocalBossEffects();
    StopLocalStigmataHold();
}

void UEGGameAudioComponent::StopResultVoices()
{
    ReleaseStoryContext();
    PendingResultVoices.Reset();
    if (IsValid(ResultVoiceComponent))
    {
        // Stop() can broadcast completion: unbind before stopping so a
        // cancelled result cannot start the next line or revive a stale queue.
        ResultVoiceComponent->OnAudioFinished.RemoveDynamic(this, &ThisClass::HandleResultVoiceFinished);
        DestroyAudio(ResultVoiceComponent);
    }
    ResultVoiceComponent = nullptr;
    bStageDialogueActive = false;
    if (bResultDialogueActive)
    {
        bResultDialogueActive = false;
        ActiveResultDialogue = FEGMasterVoiceLine();
        OnResultDialogueEnded.Broadcast();
    }
}

void UEGGameAudioComponent::PlayLocalUI(EEGGameAudioCue Cue)
{
    if (!bAssetsReady || static_cast<uint8>(Cue) < static_cast<uint8>(EEGGameAudioCue::Hover) ||
        Cue == EEGGameAudioCue::SettlementVictory) return;
    USoundBase* Sound = GetSound(Cue);
    UWorld* World = GetWorld();
    if (!Sound || !World) return;
    const double Now = World->GetAudioTimeSeconds();
    double* LastFeedbackTime = nullptr;
    switch (Cue)
    {
    case EEGGameAudioCue::Hover:
    case EEGGameAudioCue::StigmaHover: LastFeedbackTime = &LastHoverAudioTime; break;
    case EEGGameAudioCue::ValidTarget: LastFeedbackTime = &LastValidTargetAudioTime; break;
    case EEGGameAudioCue::SellZone: LastFeedbackTime = &LastSellZoneAudioTime; break;
    default: break;
    }
    if (LastFeedbackTime && *LastFeedbackTime >= 0.0 && Now - *LastFeedbackTime < FeedbackIntervalSeconds) return;
    UIVoices.RemoveAll([](const TObjectPtr<UAudioComponent>& Audio) { return !IsValid(Audio) || !Audio->IsPlaying(); });
    if (UIVoices.Num() >= MaxUIVoices) return;
    if (UAudioComponent* Audio = UGameplayStatics::SpawnSound2D(this, Sound, UISoundVolume))
    {
        UIVoices.Add(Audio);
        if (LastFeedbackTime) *LastFeedbackTime = Now;
    }
}

void UEGGameAudioComponent::StartLocalStigmataHold()
{
    // Replacing an active hold must not leave a second charge sound alive.
    StopLocalStigmataHold();
    if (!bAssetsReady || !GetWorld()) return;
    USoundBase* Sound = GetSound(EEGGameAudioCue::StigmaHold);
    if (!Sound) return;

    UIVoices.RemoveAll([](const TObjectPtr<UAudioComponent>& Audio) { return !IsValid(Audio) || !Audio->IsPlaying(); });
    if (UIVoices.Num() >= MaxUIVoices) return;
    StigmataHoldVoice = UGameplayStatics::SpawnSound2D(this, Sound, UISoundVolume);
    if (IsValid(StigmataHoldVoice)) UIVoices.Add(StigmataHoldVoice);
}

void UEGGameAudioComponent::StopLocalStigmataHold()
{
    UIVoices.Remove(StigmataHoldVoice);
    DestroyAudio(StigmataHoldVoice);
    StigmataHoldVoice = nullptr;
}

void UEGGameAudioComponent::LoadVolumeSettings()
{
    float SavedMusic = MusicVolume;
    float SavedUI = UISoundVolume;
    if (GConfig)
    {
        GConfig->GetFloat(VolumeSection, TEXT("MusicVolume"), SavedMusic, GGameUserSettingsIni);
        GConfig->GetFloat(VolumeSection, TEXT("UISoundVolume"), SavedUI, GGameUserSettingsIni);
    }
    MusicVolume = SafeVolume(SavedMusic, 0.35f);
    UISoundVolume = SafeVolume(SavedUI, 0.65f);
    bSettingsLoaded = true;
}

void UEGGameAudioComponent::SaveVolumeSettings() const
{
    if (!GConfig) return;
    GConfig->SetFloat(VolumeSection, TEXT("MusicVolume"), MusicVolume, GGameUserSettingsIni);
    GConfig->SetFloat(VolumeSection, TEXT("UISoundVolume"), UISoundVolume, GGameUserSettingsIni);
    GConfig->Flush(false, GGameUserSettingsIni);
}

void UEGGameAudioComponent::SetMusicVolume(float Volume)
{
    MusicVolume = SafeVolume(Volume, 0.35f);
    if (IsValid(MusicComponent)) MusicComponent->SetVolumeMultiplier(MusicVolume);
    if (IsValid(IntroComponent)) IntroComponent->SetVolumeMultiplier(MusicVolume);
    if (IsValid(StingerComponent)) StingerComponent->SetVolumeMultiplier(MusicVolume);
    for (UAudioComponent* Audio : FadingMusic) if (IsValid(Audio)) Audio->SetVolumeMultiplier(MusicVolume);
    SaveVolumeSettings();
}

void UEGGameAudioComponent::SetUISoundVolume(float Volume)
{
    UISoundVolume = SafeVolume(Volume, 0.65f);
    for (UAudioComponent* Audio : UIVoices) if (IsValid(Audio)) Audio->SetVolumeMultiplier(UISoundVolume);
    for (UAudioComponent* Audio : BossEffectVoices) if (IsValid(Audio)) Audio->SetVolumeMultiplier(UISoundVolume);
    SaveVolumeSettings();
}

void UEGGameAudioComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ++LoadSerial;
    if (LoadHandle.IsValid())
    {
        LoadHandle->CancelHandle();
        LoadHandle.Reset();
    }
    if (UEGMatchManager* Match = MatchManager.Get())
    {
        Match->OnPhaseChanged.RemoveDynamic(this, &ThisClass::HandlePhaseChanged);
        Match->OnStagePrepared.RemoveDynamic(this, &ThisClass::HandleStagePrepared);
        Match->OnBattleResolved.RemoveDynamic(this, &ThisClass::HandleBattleResolved);
    }
    MatchManager.Reset();
    StopIntro();
    CancelScheduledStageDialogue();
    StopResultVoices();
    DestroyAudio(MusicComponent);
    DestroyAudio(StingerComponent);
    for (UAudioComponent* Audio : FadingMusic) DestroyAudio(Audio);
    StopLocalStigmataHold();
    StopLocalBossEffects();
    for (UAudioComponent* Audio : UIVoices) DestroyAudio(Audio);
    MusicComponent = nullptr;
    StingerComponent = nullptr;
    FadingMusic.Reset();
    UIVoices.Reset();
    LoadedSounds.Reset();
    Super::EndPlay(EndPlayReason);
}
