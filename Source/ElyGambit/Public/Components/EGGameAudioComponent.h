#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TimerManager.h"
#include "Types/EGChessTypes.h"
#include "EGGameAudioComponent.generated.h"

class UAudioComponent;
class UEGMatchManager;
class USoundBase;
class UEGBattleContextComponent;
struct FStreamableHandle;
enum class EAudioComponentPlayState : uint8;

UENUM(BlueprintType)
enum class EEGGameAudioCue : uint8
{
    Preparation, CombatIntro, Battle, Boss, Victory, Defeat, Settlement,
    Hover, Drag, ValidTarget, Place, SellZone, Sell, Refresh, Lock, Unlock,
    Cancel, Error, Purchase, Merge,
    StigmaHover, StigmaDetails, StigmaHold, StigmaConfirm, StigmaLift, StigmaLand, StigmaTransition,
    SettlementVictory
};

/** Cosmetic stingers for authoritative barrier transitions. */
enum class EEGBossEffectSound : uint8 { BarrierOpen, BarrierBreak, BarrierFailure };

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGGameAudioBinding
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    EEGGameAudioCue Cue = EEGGameAudioCue::Preparation;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    TSoftObjectPtr<USoundBase> Sound;
};

// A single presentation record is passed to the dialogue UI for the exact
// sound selected by the result audio component.
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGMasterVoiceLine
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    TSoftObjectPtr<USoundBase> Sound;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    FText Speaker;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    FText Text;

    UPROPERTY(Transient, BlueprintReadOnly, Category="Audio")
    float DurationSeconds = 0.f;

    // Start within the original recording without modifying its SoundWave.
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio", meta=(ClampMin="0.0"))
    float StartOffsetSeconds = 0.f;
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGStageDialogueLine
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    int32 StageNumber = 0;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Audio")
    FEGMasterVoiceLine Line;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FEGResultDialogueStarted, const FEGMasterVoiceLine&);
DECLARE_MULTICAST_DELEGATE(FEGResultDialogueEnded);

UCLASS(ClassGroup=(Audio), Config=Game, meta=(BlueprintSpawnableComponent))
class ELYGAMBIT_API UEGGameAudioComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UEGGameAudioComponent();
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;

    UPROPERTY(Config, EditAnywhere, Category="Audio")
    TArray<FEGGameAudioBinding> Bindings;

    // These are the two board masters' local presentation voices, not music.
    UPROPERTY(Config, EditAnywhere, Category="Audio|Master Voice")
    TArray<TSoftObjectPtr<USoundBase>> ElysiaVictoryVoices;
    UPROPERTY(Config, EditAnywhere, Category="Audio|Master Voice")
    TArray<TSoftObjectPtr<USoundBase>> ElysiaDefeatVoices;
    UPROPERTY(Config, EditAnywhere, Category="Audio|Master Voice")
    TArray<TSoftObjectPtr<USoundBase>> CorruptionVictoryVoices;
    UPROPERTY(Config, EditAnywhere, Category="Audio|Master Voice")
    TArray<TSoftObjectPtr<USoundBase>> CorruptionDefeatVoices;

    // Text is keyed by the sound, so the UI never makes a second random choice.
    UPROPERTY(EditAnywhere, Category="Audio|Master Voice")
    TArray<FEGMasterVoiceLine> MasterVoiceLines;

    // One eligible record is selected for the current stage at combat start.
    UPROPERTY(EditAnywhere, Category="Audio|Master Voice")
    TArray<FEGStageDialogueLine> StageDialogueLines;

    FEGResultDialogueStarted OnResultDialogueStarted;
    FEGResultDialogueEnded OnResultDialogueEnded;
    const FEGMasterVoiceLine* GetActiveResultDialogue() const { return bResultDialogueActive ? &ActiveResultDialogue : nullptr; }
    bool IsStoryDialoguePlaying() const { return bStageDialogueActive; }
    void SkipStoryDialogue();
    /** Exclusive final presentation audio; released on leaving Finished. */
    void SetEndingAudioOverride(bool bEnabled);
    /** Called by the authoritative opening/phase queue, never by audio load completion. */
    bool StartStoryDialogue(int32 StageNumber, bool bPhaseTwo=false);
    FEGMasterVoiceLine GetStoryDialogue(int32 StageNumber, bool bPhaseTwo=false) const;

    UPROPERTY(EditDefaultsOnly, Category="Audio|Story")
    TArray<FEGStageDialogueLine> SelectedStoryLines;

    void InitializeForMatch(UEGMatchManager* Match);

    UFUNCTION(BlueprintCallable, Category="Audio", meta=(WorldContext="WorldContextObject"))
    static void PlayUI(const UObject* WorldContextObject, EEGGameAudioCue Cue);
    static void PlayBossEffect(const UObject* WorldContextObject, UEGBattleContextComponent* Battle, EEGBossEffectSound Cue, FGuid PresentationToken=FGuid());
    static void StopBossEffects(const UObject* WorldContextObject, const UEGBattleContextComponent* Battle);
    /** Shared read-only pause decision for Boss effects and device-independent tests. */
    static bool CanAdvanceBossEffect(const UEGBattleContextComponent* Battle,FGuid Round,FGuid PresentationToken);

    // The charge sound has a shorter lifetime than the screen: an interrupted
    // hold must not keep playing underneath details or the next interaction.
    static void StartStigmataHold(const UObject* WorldContextObject);
    static void StopStigmataHold(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="Audio", meta=(WorldContext="WorldContextObject"))
    static UEGGameAudioComponent* FindFor(const UObject* WorldContextObject);

    UFUNCTION(BlueprintPure, Category="Audio")
    float GetMusicVolume() const { return MusicVolume; }

    UFUNCTION(BlueprintCallable, Category="Audio")
    void SetMusicVolume(float Volume);

    UFUNCTION(BlueprintPure, Category="Audio")
    float GetUISoundVolume() const { return UISoundVolume; }

    UFUNCTION(BlueprintCallable, Category="Audio")
    void SetUISoundVolume(float Volume);

protected:
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    bool bEndingAudioOverride = false;
#if WITH_DEV_AUTOMATION_TESTS
    friend class FEGGameAudioPhaseLifecycleTest;
    friend class FEGGameAudioStigmataHoldLifecycleTest;
    friend class FEGMasterVoiceLifecycleTest;
#endif
    UFUNCTION() void HandlePhaseChanged(EEGMatchPhase Phase);
    UFUNCTION() void HandleStagePrepared(int32 StageNumber, EEGStageType StageType);
    UFUNCTION() void HandleBattleResolved(bool bVictory, int32 Corruption);
    UFUNCTION() void HandleIntroFinished();
    UFUNCTION() void HandleResultVoiceFinished();
    void HandleIntroPlayState(const UAudioComponent* Audio, EAudioComponentPlayState State);

    void AssetsLoaded(uint64 Serial);
    void SyncPhase();
    void StartBackground(EEGGameAudioCue Cue);
    void StopBackground();
    void StartCombatIntro();
    void StopIntro();
    void PlayLocalUI(EEGGameAudioCue Cue);
    void PlayLocalBossEffect(UEGBattleContextComponent* Battle, EEGBossEffectSound Cue,FGuid PresentationToken);
    void StopLocalBossEffects();
    void StartLocalStigmataHold();
    void StopLocalStigmataHold();
    void PlayResultStinger(bool bVictory);
    void QueueResultVoices(bool bVictory);
    void PlayResultVoices(bool bVictory);
    void PlayNextResultVoice();
    void StopResultVoices();
    void ScheduleStageDialogue();
    void PlayStageDialogue(int32 Stage, uint64 Revision);
    bool StartDialogueVoice(USoundBase* Sound, const FEGMasterVoiceLine& Line, bool bStageDialogue);
    void CancelScheduledStageDialogue();
    void ReleaseStoryContext();
    void LoadVolumeSettings();
    void SaveVolumeSettings() const;
    USoundBase* GetSound(EEGGameAudioCue Cue) const;

    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> LoadedSounds;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MusicComponent;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> IntroComponent;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> StingerComponent;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> FadingMusic;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> UIVoices;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> BossEffectVoices;
    UPROPERTY(Transient) TArray<TSoftObjectPtr<USoundBase>> BossEffectSounds;
    TWeakObjectPtr<UEGBattleContextComponent> BossEffectBattle;
    FGuid BossEffectRound;
    FGuid BossEffectPresentationToken;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> StigmataHoldVoice;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> ResultVoiceComponent;
    UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> PendingResultVoices;
    UPROPERTY(Transient) FEGMasterVoiceLine ActiveResultDialogue;
    TWeakObjectPtr<UEGMatchManager> MatchManager;
    TSharedPtr<FStreamableHandle> LoadHandle;
    FTimerHandle StageDialogueTimer;
    FTimerHandle StoryCompletionTimer;
    TWeakObjectPtr<UEGBattleContextComponent> StoryBattle;
    FGuid StoryRound;
    FGuid StoryHoldToken;
    double StoryDeadlineRealSeconds=0.,StoryLastRealSeconds=0.;
    uint64 LoadSerial = 0;
    uint64 CombatRevision = 0;
    uint64 IntroCombatRevision = 0;
    uint64 ResolvedCombatRevision = 0;
    uint64 PlayedCombatRevision = 0;
    uint64 ResultVoiceCombatRevision = 0;
    int32 ObservedStage = INDEX_NONE;
    int32 IntroStage = INDEX_NONE;
    int32 ResultStage = INDEX_NONE;
    EEGMatchPhase ObservedPhase = EEGMatchPhase::Finished;
    EEGGameAudioCue BackgroundCue = EEGGameAudioCue::Preparation;
    bool bObservedPhase = false;
    bool bBackgroundSet = false;
    bool bAssetsReady = false;
    bool bSettingsLoaded = false;
    bool bResultKnown = false;
    bool bLastResultVictory = false;
    bool bPendingResultSound = false;
    bool bResultDialogueActive = false;
    bool bStageDialogueActive = false;
    bool bResultVoiceStarted = false;
    double StageDialogueCombatStartTime = -1.0;
    float MusicVolume = 0.35f;
    float UISoundVolume = 0.65f;
    double LastHoverAudioTime = -1.0;
    double LastValidTargetAudioTime = -1.0;
    double LastSellZoneAudioTime = -1.0;
};
