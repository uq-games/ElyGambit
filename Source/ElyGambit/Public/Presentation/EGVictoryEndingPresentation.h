#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGVictoryEndingPresentation.generated.h"

class AEGHUD;
class ACameraActor;
class ALevelSequenceActor;
class UAudioComponent;
class UAnimSequence;
class UEGVictoryEndingDefinition;
class UEGVictoryEndingWidget;
class UEGBoardMasterActionComponent;
class UEGBoardMasterPresentation;
class ULevelSequencePlayer;
class UMediaPlayer;
class UMediaSoundComponent;
class UMediaTexture;
struct FEGVictoryEndingNode;

enum class EEGEndingTransition : uint8 { None, Start, Next, Retry, Finish };

/** Local cosmetic playback. Match outcome and combat transactions stay in the game mode. */
UCLASS()
class ELYGAMBIT_API AEGVictoryEndingPresentation : public AActor
{
 GENERATED_BODY()
public:
 AEGVictoryEndingPresentation();
 void BeginEnding(AEGHUD* InHUD, TSoftObjectPtr<UEGVictoryEndingDefinition> InDefinition, float LeadInSeconds);
 void AdvanceDialogue();
 void SkipSegment();
 void SkipAll();
 void Retry();
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void StartContent();
 void StartNode();
 void StopNode();
 void StopMasterActions();
 void ReleaseMasterPresentation();
 bool StartMasterAction(bool bFriendly,TSoftObjectPtr<UAnimSequence> ClipPath);
 void FriendlyActionFinished(AActor* Requester,bool bInterrupted);
 void EnemyActionFinished(AActor* Requester,bool bInterrupted);
 void QueueNextNode(bool bForce=false);
 void BeginTransition(EEGEndingTransition Action,float FadeOutSeconds);
 void TickTransition(float DeltaSeconds);
 bool ApplyCamera(const FEGVictoryEndingNode& Node);
 void Complete();
 void Fail(const FString& Details);
 UFUNCTION() void SequenceFinished();
 UFUNCTION() void MediaOpened(FString OpenedUrl);
 UFUNCTION() void MediaFailed(FString FailedUrl);
 UFUNCTION() void MediaFinished();
 UFUNCTION() void MusicFinished();
 UPROPERTY(Transient) TObjectPtr<UEGVictoryEndingDefinition> Definition;
 UPROPERTY(Transient) TObjectPtr<UEGVictoryEndingWidget> Widget;
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> Music;
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> Voice;
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> Effect;
 UPROPERTY(Transient) TObjectPtr<UMediaPlayer> MediaPlayer;
 UPROPERTY(Transient) TObjectPtr<UMediaTexture> MediaTexture;
 UPROPERTY(Transient) TObjectPtr<UMediaSoundComponent> MediaAudio;
 UPROPERTY(Transient) TObjectPtr<ULevelSequencePlayer> SequencePlayer;
 UPROPERTY(Transient) TObjectPtr<ALevelSequenceActor> SequenceActor;
 UPROPERTY(Transient) TObjectPtr<ACameraActor> ShotCamera;
 UPROPERTY(Transient) TWeakObjectPtr<AEGHUD> HUD;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBoardMasterPresentation> MasterPresentation;
 UPROPERTY(Transient) TWeakObjectPtr<AActor> OriginalViewTarget;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBoardMasterActionComponent> FriendlyAction;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBoardMasterActionComponent> EnemyAction;
 FDelegateHandle FriendlyActionHandle;
 FDelegateHandle EnemyActionHandle;
 TSoftObjectPtr<UEGVictoryEndingDefinition> DefinitionPath;
 int32 NodeIndex = INDEX_NONE;
 float LeadInRemaining = 0.f;
 float MediaOpeningRemaining = 0.f;
 float TransitionElapsed = 0.f;
 float TransitionDuration = 0.f;
 float TransitionOpacity = 0.f;
 float TransitionStartOpacity = 0.f;
 float MusicDuck = 1.f;
 EEGEndingTransition Transition = EEGEndingTransition::None;
 bool bStarted = false;
 bool bFinished = false;
 bool bInError = false;
 bool bWaitingForMedia = false;
 bool bSequencePlaying = false;
 bool bAdvanceWhenSequenceEnds = false;
 bool bStoppingNode = false;
 bool bMediaAudioMuted = true;
 bool bTransitionFadingIn = false;
 bool bPendingNodeAdvance = false;
 bool bFriendlyActionPending = false;
 bool bEnemyActionPending = false;
 bool bAdvanceWhenActionsEnd = false;
};
