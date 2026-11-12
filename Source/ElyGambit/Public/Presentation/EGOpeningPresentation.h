#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGOpeningPresentation.generated.h"

class ACameraActor;
class AEGArtPiece;
class AEGDemoGameMode;
class AEGChessUnit;
class AEGTeleportVisual;
class UAnimSequence;
class UAnimMontage;
class UStaticMesh;
class UEGHeroDefinition;
struct FStreamableHandle;

#if WITH_DEV_AUTOMATION_TESTS
struct FEGOpeningHandoffEvidence
{
 FTransform CameraTransform=FTransform::Identity;
 FVector SourceCenter=FVector::ZeroVector,DestinationCenter=FVector::ZeroVector;
 double SourceRadius=0.,DestinationRadius=0.;
 float FieldOfView=0.f,AspectRatio=0.f;
};
#endif

enum class EEGOpeningPresentationState : uint8
{
 Initializing, AwaitingStart, LoadingPreparing, Departure, StageTransition, StageEntrance, StageEntranceFailed, Completed, Failed
};

/** Local, cancellable presentation; the GameMode retains all run and stage authority. */
UCLASS(Config=Game)
class ELYGAMBIT_API UEGOpeningPresentation : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGOpeningPresentation();
 static UEGOpeningPresentation* Install(AEGDemoGameMode* Mode);
 bool RequestBegin();
 bool HasCompletedDeparture() const {return bDepartureCompleted;}
#if WITH_DEV_AUTOMATION_TESTS
 bool HasSafeCorruptionHandoffForTests() const {return bCorruptionHandoffSafe;}
 const FEGOpeningHandoffEvidence& GetCorruptionHandoffEvidenceForTests() const {return CorruptionHandoffEvidence;}
#endif
 bool HasCompletedStageTransition(FGuid Token) const {return Token.IsValid()&&Token==StageToken&&bStageTransitionCompleted;}
 bool HasCompletedStageEntrance(FGuid Token) const {return Token.IsValid()&&Token==StageToken&&bStageEntranceCompleted;}
 bool RetryOpeningScene();
 void FailOpening(const FString& Reason);
 void CancelTransition();
 void FailStageEntrance(const FString& Reason);
 FString GetStageEntranceError() const {return StageEntranceError;}
 void BeginStageTransition(FGuid Token);
 void BeginStageEntrance(FGuid Token);
 UFUNCTION(BlueprintPure,Category="Opening") bool IsLoading() const;
 bool IsLoadingSceneVisible() const;
 UFUNCTION(BlueprintPure,Category="Opening") bool IsAwaitingStart() const;
 UFUNCTION(BlueprintPure,Category="Opening") bool IsPreparingAfterStart() const;
 UFUNCTION(BlueprintPure,Category="Opening") bool IsDeparturePlaying() const;
 UFUNCTION(BlueprintPure,Category="Opening") bool IsWaitingForRunResources() const;
 UFUNCTION(BlueprintPure,Category="Opening") bool IsOpeningActive() const;
 UFUNCTION(BlueprintPure,Category="Opening") FString GetLoadingError() const;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;

 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing",meta=(ClampMin="0.1")) float MinimumWalkSeconds=2.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing",meta=(ClampMin="0.1")) float ApproachSeconds=4.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing",meta=(ClampMin="0.1",ClampMax="30.0")) float CameraRiseSeconds=15.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing",meta=(ClampMin="0.1")) float CorruptionTurnSeconds=.85f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing",meta=(ClampMin="0.1")) float StageTransitionSeconds=.65f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing",meta=(ClampMin="0.1")) float StageEntranceSeconds=1.1f;
 // Candidate world coordinates measured against the original stair at identity
 // rotation/location and uniform 1.3125 scale. Rebased to its actual transform.
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene") FVector SeatReferenceLocation=FVector(-140,3150,-1.3);
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene") FVector ElysiaReferenceLocation=FVector(140,3200,-1.3);
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene") FVector CameraReferenceLocation=FVector(240,3490,155);
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene") FVector CameraReferenceFocus=FVector(-100,3100,120);
 // Authored against the original stair at identity rotation/location and 1.3125 scale.
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Camera") FVector CameraInwardReferenceLocation=FVector(0,3000,1500);
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Camera") FRotator CameraInwardReferenceRotation=FRotator(-27,-90,0);
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Camera",meta=(ClampMin="0.1",ClampMax="0.9")) float CameraOverheadFraction=.5f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Camera",meta=(ClampMin="0")) float CameraOverheadLift=0.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Camera",meta=(ClampMin="-89",ClampMax="-1")) float CameraOverheadPitch=-78.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene",meta=(ClampMin="0.1")) float OpeningBodyWorldScale=1.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene",meta=(ClampMin="5",ClampMax="170")) float OpeningCameraFOV=58.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UEGHeroDefinition> ElysiaDefinitionAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> ElysiaIdleAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets",meta=(ClampMin="0.1",ClampMax="2.0")) float ElysiaIdlePlayRate=.4f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets",meta=(ClampMin="0.1",ClampMax="2.0")) float CorruptionIdlePlayRate=.4f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> ElysiaWalkAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> CorruptionSitAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> CorruptionRiseAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> CorruptionWalkAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> CorruptionWalkLoopAsset;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UAnimSequence> CorruptionStandAsset;
 // The stand clip's forward axis differs from the normal presentation pose.
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene") float CorruptionStandYawOffset=90.f;
 // Hidden handoff: only the final short platform approach is shown.
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Scene") FVector PlatformApproachOffset=FVector(0,700,0);
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Timing") float PlatformWalkSeconds=1.8f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Opening|Assets") TSoftObjectPtr<UStaticMesh> StairAsset;

private:
#if WITH_DEV_AUTOMATION_TESTS
 friend class FEGOpeningFlowTransactionTest;
#endif
 bool InitializeScene(bool bSynchronous=false);
 void PlayClip(AEGArtPiece* Actor,UAnimSequence* Clip,bool bLoop);
 void MoveCamera(const FVector& Location,const FVector& Focus);
 void StartDeparture();
 void RestoreCamera();
 void ReleasePuppets();
 void RestoreHiddenUnits();
 bool SpawnArrival(const FVector& Location,bool bEnemy,float Radius=95.f);
 UPROPERTY(Transient) TObjectPtr<ACameraActor> Camera;
 UPROPERTY(Transient) TObjectPtr<AEGArtPiece> Elysia;
 UPROPERTY(Transient) TObjectPtr<AEGArtPiece> Corruption;
 UPROPERTY(Transient) TObjectPtr<AEGArtPiece> PendingCorruption;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> ElysiaIdle;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> ElysiaWalk;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> CorruptionSit;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> CorruptionRise;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> CorruptionWalk;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> CorruptionWalkLoop;
 UPROPERTY(Transient) TObjectPtr<UAnimMontage> CorruptionWalkLoopMontage;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> CorruptionStand;
 UPROPERTY(Transient) TArray<TObjectPtr<AActor>> OpeningSetPieces;
 TArray<TPair<TWeakObjectPtr<AEGChessUnit>,bool>> HiddenUnits;
 TArray<TWeakObjectPtr<AEGTeleportVisual>> ActiveEffects;
 EEGOpeningPresentationState State=EEGOpeningPresentationState::Initializing;
 FGuid OpeningToken;
 FGuid StageToken;
 FVector FriendlyGround=FVector::ZeroVector;
 FVector EnemyGround=FVector::ZeroVector;
 FVector SeatGround=FVector::ZeroVector;
 FVector SeatRootLocation=FVector::ZeroVector;
 FVector RiseHorizontalOffset=FVector::ZeroVector;
 FVector RiseEndRootLocation=FVector::ZeroVector;
 // Optional three-axis actor displacement curves are authored in the original
 // stair reference frame, in centimetres. The first sample is the origin.
 FTransform RiseOffsetReferenceFrame=FTransform::Identity;
 FVector RiseOffsetCurveStart=FVector::ZeroVector;
 FVector ApproachEnd=FVector::ZeroVector;
 TArray<FVector> CorruptionRoutePoints;
 TArray<float> CorruptionRouteDistances;
 float CorruptionRouteLength=0.f;
 float CorruptionRouteDistance=0.f;
 float CorruptionWalkStartTime=0.f;
 FVector LastCameraLocation=FVector::ZeroVector;
 FVector LastCameraFocus=FVector::ZeroVector;
 FVector CameraTargetLocation=FVector::ZeroVector;
 FVector DepartureCameraLocation=FVector::ZeroVector;
 FVector CameraInwardLocation=FVector::ZeroVector;
 FVector CameraOrbitCenter=FVector::ZeroVector;
 FRotator CameraTargetRotation=FRotator::ZeroRotator;
 FQuat DepartureCameraRotation=FQuat::Identity;
 FQuat CameraInwardRotation=FQuat::Identity;
 FQuat CorruptionTurnFrom=FQuat::Identity;
 FQuat CorruptionTurnTo=FQuat::Identity;
 FQuat CorruptionStopRotation=FQuat::Identity;
 float Elapsed=0.f;
 float ElysiaRevealStart=-1.f;
 float StageEffectsStart=-1.f;
 float StageEffectsDelay=0.f;
 float RiseDuration=1.f;
 float InitialRiseCurveCm=0.f;
 float FinalRiseCurveCm=0.f;
 float TurnStartElapsed=0.f;
 float WalkStartElapsed=0.f;
 float WalkDuration=3.f;
 float ElysiaWalkSpeed=95.f;
 float PreviousSitTime=0.f;
 float ControllerWaitSeconds=0.f;
 float CorruptionOpeningModelScale=1.f;
 float CorruptionMasterModelScale=1.f;
 float CameraTargetFOV=52.f;
 uint8 CameraLogMilestone=0;
 bool bWidgetInstalled=false;
 bool bTeleportSpawned=false;
 bool bTeleported=false;
 bool bWalkStarted=false;
 bool bTurnStarted=false;
 bool bCorruptionSettled=false;
 bool bCorruptionHandoffSafe=false;
#if WITH_DEV_AUTOMATION_TESTS
 FEGOpeningHandoffEvidence CorruptionHandoffEvidence;
#endif
 bool bWaitingForSitBoundary=false;
 bool bUseRiseOffsetCurves=false;
 bool bUseLegacyRiseHeight=false;
 bool bStartWasRequested=false;
 bool bUnitsRevealed=false;
 bool bFadedForTransition=false;
 bool bDepartureCompleted=false;
 bool bStageTransitionCompleted=false;
 bool bStageEntranceCompleted=false;
 TSharedPtr<FStreamableHandle> SceneLoad;
 TSharedPtr<FStreamableHandle> PuppetLoad;
 TSharedPtr<FStreamableHandle> DepartureLoad;
 FString Error;
 FString StageEntranceError;
};
