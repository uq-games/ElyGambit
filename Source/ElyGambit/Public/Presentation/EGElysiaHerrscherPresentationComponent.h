#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGElysiaHerrscherPresentationComponent.generated.h"

class AEGChessUnit;
class AEGElysiaStarSeaVisual;
class UEGHeroSkillComponent;
class UEGElysiaHerrscherSkillBehavior;
class UNiagaraComponent;
class UNiagaraSystem;
class UEGSourceEffectDefinition;
class AEGSourceEffect;
class AEGCombatVisual;
class UEGSourcePreviewDefinition;
class USkeletalMeshComponent;
class UFileMediaSource;
class UMediaPlayer;
class UMediaTexture;
class UEGElysiaEntryMovieWidget;
class UEGHumanCameraShake;
class APlayerCameraManager;
struct FStreamableHandle;
enum class EEGElysiaHerrscherPresentationPhase : uint8;

/** Per-incarnation cosmetics. Combat remains the only owner of phase and shield state. */
UCLASS(ClassGroup=(Presentation), BlueprintType, meta=(BlueprintSpawnableComponent))
class ELYGAMBIT_API UEGElysiaHerrscherPresentationComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGElysiaHerrscherPresentationComponent();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
 /** Called only by the authoritative arrow's confirmed contact callback. */
 void ShowArrowImpact(const FVector& WorldPoint,const FRotator& FlightRotation,UEGSourceEffectDefinition* SourceImpact,bool bCharged=false);
 /** Called at the authoritative origin-attack Hit, once per action. */
 void ShowOriginAttackImpact(const FVector& WorldPoint,bool bWave,FGuid Action);
 /** Cosmetic response to a successful combat-owned contact; ContactIndex is 0..2. */
 void ShowOriginAttackContact(const FVector& WorldPoint,FGuid Round,FGuid Action,int32 ContactIndex,int32 AttackIndex);
 /** A local cosmetic request, never an independent combat state. */
 bool IsEntryMoviePending() const {return bEntryMoviePending;}
 bool IsEntryMoviePlaying() const {return bEntryMoviePlaying;}
 bool IsEntryMoviePendingFor(FGuid Round,FGuid Action) const {return bEntryMoviePending&&EntryMovieRound==Round&&EntryMovieAction==Action;}
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Entry Movie") bool bEntryMovieEnabled=false;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Entry Movie") TSoftObjectPtr<UFileMediaSource> EntryMovieSource;

 /** All systems are configured on the private combat actor. Empty fields are valid. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraSystem> CrystalMirrorSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraSystem> OriginStarfieldSystem;
 /** Optional private board-space domain visual; never attached to the character. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TSubclassOf<AEGElysiaStarSeaVisual> StarSeaClass;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraSystem> EntryBurstSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraSystem> EntryStartSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraSystem> FinisherStartSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") TObjectPtr<UNiagaraSystem> FinisherBurstSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Bow") TObjectPtr<UNiagaraSystem> BowChargeSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Bow") TObjectPtr<UNiagaraSystem> ArrowImpactSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Origin") TObjectPtr<UNiagaraSystem> ScepterImpactSystem;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Origin") TObjectPtr<UNiagaraSystem> OriginWaveSystem;
 /** Optional per-attack overrides, indexed by the authority-owned three-hit cursor. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Origin") TArray<TObjectPtr<UNiagaraSystem>> OriginAttackSystems;
 /** Source-derived contact candidates, indexed by the authority-owned three-hit Origin cursor. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Origin") TArray<TObjectPtr<UEGSourceEffectDefinition>> SourceOriginAttackImpacts;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") FTransform MirrorTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") FTransform StarfieldTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") FTransform EntryTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia") FTransform FinisherTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Bow") FTransform BowChargeTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Bow") FVector ArrowImpactScale=FVector::OneVector;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Origin") FTransform ScepterImpactTransform=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Origin") FTransform OriginWaveTransform=FTransform::Identity;

 /** A private manta-only source assembly; the character's action assembly remains on the unit. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Finisher") TSoftObjectPtr<UEGSourcePreviewDefinition> BoardFinisherAssembly;
 /** Presentation-only local offset applied to the centred manta assembly. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Presentation|Elysia|Finisher") FTransform BoardFinisherOffset=FTransform::Identity;

private:
 TWeakObjectPtr<UEGHumanCameraShake> FeedbackShake;
 TWeakObjectPtr<APlayerCameraManager> FeedbackCamera;
 FGuid FeedbackLease;
 double FeedbackStartedAt=0.;
 void PlayFeedbackShake(float Scale);
 void StopFeedbackShake();
 void UpdateFeedbackShake();
 friend class FEGHumanEntryMovieCleanup;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> BoundSkill;
 UPROPERTY(Transient) TWeakObjectPtr<UEGElysiaHerrscherSkillBehavior> BoundBehavior;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> CrystalMirror;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> OriginStarfield;
 UPROPERTY(Transient) TObjectPtr<AEGElysiaStarSeaVisual> StarSea;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> BowCharge;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<UNiagaraComponent>> Bursts;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<UNiagaraComponent>> BoardBursts;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<UNiagaraComponent>> ArrowImpacts;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> SourceArrowImpacts;
 /** Runtime-only target-ground copies; source assets and their samples remain untouched. */
 UPROPERTY(Transient) TArray<TObjectPtr<UEGSourceEffectDefinition>> RuntimeOriginImpacts;
 UPROPERTY(Transient) TArray<TObjectPtr<UEGSourceEffectDefinition>> RuntimeOriginImpactSources;
 TSharedPtr<FStreamableHandle> OriginImpactPreload;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> BoardFinisher;
 UPROPERTY(Transient) TObjectPtr<UFileMediaSource> LoadedEntryMovie;
 UPROPERTY(Transient) TObjectPtr<UMediaPlayer> EntryMoviePlayer;
 UPROPERTY(Transient) TObjectPtr<UMediaTexture> EntryMovieTexture;
 UPROPERTY(Transient) TObjectPtr<UEGElysiaEntryMovieWidget> EntryMovieWidget;
 TSharedPtr<FStreamableHandle> EntryMovieLoad;
 FGuid EntryMovieRound;
 FGuid EntryMovieAction;
 uint64 EntryMovieGeneration=0;
 double EntryMovieRequestedAt=0.0;
 bool bEntryMoviePending=false;
 bool bEntryMoviePlaying=false;
 bool bEntryMovieOpened=false;
 bool bEntryMovieFailed=false;
 bool bEntryMovieHadPause=false;
 TWeakObjectPtr<USkeletalMeshComponent> BoardFinisherClockMesh;
 FGuid BoardFinisherAction;
 FTransform BoardFinisherStart=FTransform::Identity;
 FVector BoardFinisherTravel=FVector::ZeroVector;
 FVector BoardFinisherUp=FVector::UpVector;
 float BoardFinisherLandingTime=0.f;
 float BoardFinisherLeapHeight=0.f;
 FGuid BoundRound;
 FGuid PendingBurstAction;
 FGuid LastOriginCueAction;
 // Only presentation deduplication: the combat behavior owns whether a hit occurs.
 TMap<FGuid,uint8> OriginContactMasks;
 int32 PendingBurstPhase=-1;
 bool bStarSeaDismissing=false;
 bool bStarSeaInitFailed=false;

 void RefreshBinding();
 void PreloadOriginImpacts();
 void Unbind();
 void HandlePhase(EEGElysiaHerrscherPresentationPhase Phase,FGuid Round,FGuid Action);
 void ApplySnapshot(EEGElysiaHerrscherPresentationPhase Phase);
 UFUNCTION() void HandleStatusChanged();
 UFUNCTION() void HandleOwnerDied(AEGChessUnit* Unit);
 UNiagaraComponent* SpawnEffect(UNiagaraSystem* System,const FTransform& Transform,bool bOneShot,bool bDuringPause=false);
 UNiagaraComponent* SpawnBoardEffect(UNiagaraSystem* System,const FTransform& Transform);
 bool SpawnPhaseBurst(EEGElysiaHerrscherPresentationPhase Phase);
 void RefreshEffectAttachments();
 void DestroyEffect(UNiagaraComponent* Component);
 void ClearBursts();
 void ClearEffects();
 void BeginBoardFinisher(FGuid Action);
 void UpdateBoardFinisher();
 void EndBoardFinisher();
 void RefreshTickRate();
 void PreloadEntryMovie();
 void OpenLoadedEntryMovie(uint64 Generation);
 void BeginEntryMovie(FGuid Round,FGuid Action);
 void UpdateEntryMovie();
 void StopEntryMovie();
 UFUNCTION() void OnEntryMovieOpened(FString Url);
 UFUNCTION() void OnEntryMovieOpenFailed(FString Url);
 UFUNCTION() void OnEntryMovieEnded();
};
