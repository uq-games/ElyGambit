#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGArtPiece.generated.h"

class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UAnimSequence;
class UMaterialInterface;
class UEGHeroDefinition;
class AEGCombatVisual;
class UEGSourcePreviewDefinition;
class USoundBase;
class UAudioComponent;

/** Reusable visual piece for the art demo; gameplay rules belong to future systems. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGArtPiece : public AActor
{
	GENERATED_BODY()

public:
	AEGArtPiece();
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostRegisterAllComponents() override;
	virtual void PostUnregisterAllComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetActorHiddenInGame(bool bNewHidden) override;
	void SetBattlePresentation(bool bBattle);
 void PlayPreparationEntrance(UEGSourcePreviewDefinition* Entrance, USoundBase* Voice=nullptr, bool bDeferVoice=false);
 /** A deferred voice starts only after the human card or its visual fallback. */
 void StartDeferredPreparationEntranceVoice();
 void DiscardDeferredPreparationEntranceVoice();
 /** Called only by the short-lived human entrance effect; preserves existing component visibility. */
 void SetHumanEntranceSuppressed(bool bSuppressed);
 void CancelHumanEntrance();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Art")
	TObjectPtr<USceneComponent> ArtRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Art")
	TObjectPtr<USkeletalMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Art")
	TObjectPtr<USkeletalMeshComponent> Outline;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art")
	TObjectPtr<USkeletalMesh> CharacterMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art")
	TObjectPtr<UAnimSequence> IdleAnimation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art")
	TObjectPtr<UMaterialInterface> OutlineMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art")
	FVector DirectionToKeyLight = FVector(0.3, -0.6, 0.74);

	/** Optional source-model parts that share a material with the visible body. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Art")
	TArray<FName> HiddenPresentationBones;

	UFUNCTION(BlueprintCallable, Category = "Art")
	void RefreshAppearance();
	UFUNCTION(BlueprintCallable,Category="Art") void ApplyHeroDefinition(UEGHeroDefinition* Definition);
	UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Art") TObjectPtr<UEGHeroDefinition> HeroVisualDefinition;
 UFUNCTION(BlueprintPure,Category="Diagnostics") int32 GetAppearanceRevision() const { return AppearanceRevision; }
 float GetBoardModelScale() const {return BoardModelScale;}
 /** Set the board master's model and equipment size without scaling source effects. */
 void SetMasterModelScale(float Scale);

private:
#if WITH_DEV_AUTOMATION_TESTS
 friend class FEGHumanEntranceLifecycle;
 int32 PreparationVoicePlayCallsForTests=0;
#endif
 void EnsureAppearance();
 uint32 AppearanceKey() const;
 uint32 AppliedAppearanceKey=0;
 bool bAppearanceApplied=false;
 int32 AppearanceRevision=0;
 float BoardModelScale=1.f;
 bool bHasMasterModelScale=false;
	UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> PreparationAssembly;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> PreparationEntranceAudio;
	bool bPreparationEntranceVoicePending=false;
	bool bBattlePresentation=false;
 FTimerHandle PreparationEntranceTimer;
 bool bHumanEntranceSuppressed=false;
 bool bHumanEntranceAssemblyWasHidden=false;
 bool bHumanEntranceAssemblyWasPlaying=false;
 bool bHumanEntranceTimerPaused=false;
 bool bHumanEntranceAudioPaused=false;
 TWeakObjectPtr<AEGCombatVisual> HumanEntranceHiddenAssembly;
 TArray<TPair<TWeakObjectPtr<USceneComponent>,bool>> HumanEntranceHiddenComponents;
 void ShowPreparationAssembly(UEGSourcePreviewDefinition* Definition,bool bLoop);
 void FinishPreparationEntrance();
 void StopPreparationEntranceAudio();
	// Source reference axes are corrected by the current head pose, then sent per primitive.
	void UpdateFaceDirections();
	FDelegateHandle FacePoseHandle;
	int32 FaceBoneIndex = INDEX_NONE;
	FQuat FaceReferenceInverse = FQuat::Identity;

	UPROPERTY(Transient)
	TArray<FName> AppliedHiddenPresentationBones;
};
