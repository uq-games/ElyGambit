#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGSettlementPresentation.generated.h"

class AEGChessUnit;
class AEGCombatVisual;
class UAnimSequence;
class USceneComponent;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class UEGCombatAnimInstance;
class UEGChessAnimInstance;
class UAnimInstance;
class UAnimSingleNodeInstance;
class UAnimationAsset;
class USkeletalMesh;
class UEGSourcePreviewDefinition;
class UEGHeroDefinition;
class UNiagaraComponent;
class UNiagaraSystem;
class UEGBoardMasterPresentation;
class UAudioComponent;
class USoundBase;
class AEGArtPiece;
class UEGBoardMasterActionComponent;

USTRUCT()
struct FEGSettlementHiddenComponent
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<USceneComponent> Component;
 bool bWasVisible = false;
};

USTRUCT()
struct FEGSettlementPose
{
 GENERATED_BODY()
 UPROPERTY() TWeakObjectPtr<AEGChessUnit> Unit;
 UPROPERTY() TObjectPtr<UAnimSequence> Clip;
 UPROPERTY() TWeakObjectPtr<USkeletalMeshComponent> Body;
 UPROPERTY() TWeakObjectPtr<UEGCombatAnimInstance> AnimInstance;
 UPROPERTY() TWeakObjectPtr<UAnimInstance> OriginalInstance;
 UPROPERTY() TWeakObjectPtr<UEGChessAnimInstance> LegacyInstance;
 UPROPERTY() TWeakObjectPtr<UAnimSingleNodeInstance> SingleInstance;
 UPROPERTY() TObjectPtr<UAnimationAsset> OriginalSingleAsset;
 UPROPERTY() TWeakObjectPtr<USkeletalMesh> BodyAsset;
 UPROPERTY() TObjectPtr<AEGCombatVisual> Assembly;
 UPROPERTY() TArray<FEGSettlementHiddenComponent> HiddenWeapons;
 FDelegateHandle LeaseHandle;
 bool bBodyHidden = false;
 bool bAnimate = true;
 bool bOriginalPauseAnims = false;
 bool bAppliedPauseAnims = false;
 bool bPauseWasChanged = false;
 bool bSingleWasPlaying = false;
 bool bSingleWasLooping = false;
 float SingleOriginalTime = 0.f;
 float SingleOriginalRate = 1.f;
 float LegacyOriginalSpeed = 0.f;
 int32 OriginalAnimationMode = 0;
};

/** Visual-only settlement. The game mode owns outcome, timing callbacks and interruption. */
UCLASS()
class ELYGAMBIT_API AEGSettlementPresentation : public AActor
{
 GENERATED_BODY()
public:
 AEGSettlementPresentation();
 static TArray<FSoftObjectPath> GetPreloadPaths();
 static void PresentCopyTransform(AEGChessUnit* Unit);
 void StartPresentation(const TArray<AEGChessUnit*>& Units, AEGChessUnit* Elysia,
  const FVector& ElysiaFallback, const FVector& CorruptionLocation, bool bVictory);
 float GetElapsedSeconds() const { return Age; }
 float GetDurationSeconds() const;
 AEGArtPiece* GetMasterActor(bool bFriendly) const;
 FSimpleMulticastDelegate OnImpact;
 FSimpleMulticastDelegate OnFinished;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
 UPROPERTY(Transient) TWeakObjectPtr<UEGBoardMasterPresentation> MasterPresentation;
 UPROPERTY(Transient) TWeakObjectPtr<AEGArtPiece> FriendlyMaster;
 UPROPERTY(Transient) TWeakObjectPtr<AEGArtPiece> EnemyMaster;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBoardMasterActionComponent> AttackerAction;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBoardMasterActionComponent> DefenderAction;
 FDelegateHandle MasterReleaseHandle;
 void OnMasterRelease();
 UPROPERTY() TObjectPtr<UAudioComponent> LaunchAudio;
 UPROPERTY() TObjectPtr<UAudioComponent> ImpactAudio;
 UPROPERTY(EditDefaultsOnly,Category="Settlement|Audio") TSoftObjectPtr<USoundBase> LaunchSound;
 UPROPERTY(EditDefaultsOnly,Category="Settlement|Audio") TSoftObjectPtr<USoundBase> ImpactSound;
 void OnUnitPresentationWillRelease(AEGChessUnit* Unit);
 UPROPERTY() TObjectPtr<USceneComponent> Root;
 UPROPERTY() TObjectPtr<USkeletalMeshComponent> FriendlyBody;
 UPROPERTY() TObjectPtr<USkeletalMeshComponent> EnemyBody;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Projectile;
 UPROPERTY() TObjectPtr<UNiagaraComponent> MasterProjectileFX;
 UPROPERTY() TObjectPtr<UNiagaraComponent> MasterHitFX;
 UPROPERTY() TObjectPtr<UNiagaraComponent> MasterFlashFX;
 // Soft references are authored defaults so the cooker can discover both sides,
 // and a missing candidate can fall back to the existing source presentation.
 UPROPERTY(EditDefaultsOnly, Category="Settlement|Effects") TSoftObjectPtr<UNiagaraSystem> FriendlyProjectileSystem;
 UPROPERTY(EditDefaultsOnly, Category="Settlement|Effects") TSoftObjectPtr<UNiagaraSystem> FriendlyHitSystem;
 UPROPERTY(EditDefaultsOnly, Category="Settlement|Effects") TSoftObjectPtr<UNiagaraSystem> FriendlyFlashSystem;
 UPROPERTY(EditDefaultsOnly, Category="Settlement|Effects") TSoftObjectPtr<UNiagaraSystem> EnemyProjectileSystem;
 UPROPERTY(EditDefaultsOnly, Category="Settlement|Effects") TSoftObjectPtr<UNiagaraSystem> EnemyHitSystem;
 UPROPERTY(EditDefaultsOnly, Category="Settlement|Effects") TSoftObjectPtr<UNiagaraSystem> EnemyFlashSystem;
 UPROPERTY() TObjectPtr<AEGCombatVisual> ArrowVisual;
 UPROPERTY() TObjectPtr<AEGCombatVisual> ImpactVisual;
 UPROPERTY() TArray<FEGSettlementPose> Poses;
 FVector From = FVector::ZeroVector;
 FVector To = FVector::ZeroVector;
 float Age = 0.f;
 double LastAudioClockSeconds = -1.0;
 float ImpactTime = 1.f;
 float FlightStart = .7f;
 float FlightDuration=.6f;
 bool bMasterFlightStarted = false;
 bool bStarted = false;
 bool bImpacted = false;
 bool bFinished = false;
 bool bVictory = false;
};





