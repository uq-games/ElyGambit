#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "EGSourceCharacterPreview.generated.h"

class UAnimSequence;
class UAnimInstance;
class UCurveFloat;
class USkeletalMesh;
class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UEGSourceEffectDefinition;
class UEGNativeMeshPassDefinition;

/** A source character or device on the same absolute preview timeline. */
USTRUCT(BlueprintType)
struct FEGSourcePreviewPart
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName PartId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UAnimSequence> Animation;
	/** Runtime secondary graph. Source previews deliberately ignore this binding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<UAnimInstance> AnimationBlueprintClass;
	/** Optional earlier part and bone/socket for a static preview attachment. Empty preserves actor-root behaviour. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName ParentPartId = NAME_None;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName AttachBone = NAME_None;
	/** For an attachment, translation is socket-local centimetres, ignoring socket bone scale; scale is relative to the actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Transform;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float StartTime = 0.f;
	/** Zero uses the animation length. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Duration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bLoop = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bVisible = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UCurveFloat> Visibility;
	/** Original Unity boolean bindings treat every nonzero curve value as true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bNonzeroSourceVisibility = false;
	bool EvaluateVisibility(const UCurveFloat* Curve, float Time) const;
	/** Curves use clip-local seconds and apply to every material on this part. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<FName, TObjectPtr<UCurveFloat>> MaterialCurves;
	/** Source renderer enable/disable, keyed by the imported material slot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<int32, TObjectPtr<UCurveFloat>> SectionVisibility;
	/** Opt-in native multipass draws reuse the source mesh and pose. Existing assemblies retain their render state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bUseSourceRenderBindings = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TMap<int32, TObjectPtr<UMaterialInterface>> MaterialOverrides;
	/** Original ColorMask 0 pass: retain depth participation, suppress main scene color. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bSourceDepthOnly = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SourceTranslucencySortPriority = 0;
	/** Original ordered native passes. Empty keeps the established renderer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UEGNativeMeshPassDefinition> NativePassDefinition;
	void ApplyRenderBindings(USkeletalMeshComponent* Component,UObject* LifetimeOwner=nullptr,float SampleTime=0.f) const;
};

/** A cosmetic source effect evaluated on the same seekable action timeline. */
USTRUCT(BlueprintType)
struct FEGSourcePreviewEffect
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UEGSourceEffectDefinition> Effect;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float StartTime = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Duration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FTransform Transform;
	/** Empty uses the viewer origin. A named part must be visible at this time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName AnchorPartId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName AnchorBone;
};

USTRUCT(BlueprintType)
struct FEGSourcePreviewMotion
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SourceDescription;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString Limitations;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float Duration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FEGSourcePreviewPart> Parts;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FEGSourcePreviewEffect> Effects;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGSourcePreviewDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText CharacterName;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FString SourceDescription;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<FEGSourcePreviewMotion> Motions;
};

/** Cosmetic source viewer. No gameplay abilities, damage, collision or replication. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGSourceCharacterPreview : public AActor
{
	GENERATED_BODY()
public:
	AEGSourceCharacterPreview();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source Preview") TObjectPtr<UEGSourcePreviewDefinition> Definition;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source Preview", meta=(ClampMin="0")) int32 MotionIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source Preview", meta=(ClampMin="0.01", ClampMax="4")) float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source Preview") bool bLoop = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Source Preview") bool bAutoPlay = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Source Preview") float PreviewTime = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Source Preview") bool bPlaying = false;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Source Preview") void Play();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Source Preview") void Pause();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Source Preview") void Stop();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Source Preview") void NextMotion();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Source Preview") void PreviousMotion();
	UFUNCTION(BlueprintCallable, Category="Source Preview") void SetPreviewTime(float Time);
	bool GetVisiblePartBoneLocation(FName PartId,FName Bone,FVector& Location) const;
	bool GetVisiblePartBoneTransform(FName PartId,FName Bone,FTransform& Transform) const;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> PreviewParts;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> PreviewEffects;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewEffectMaterials;
	TArray<FIntPoint> EffectTrackSources;
	/** INDEX_NONE: root part; nonnegative: earlier parent index; -2: invalid attachment. */
	TArray<int32> PreviewAttachmentParents;
	TMap<int32, FTransform> EffectBirthAnchors;
	UPROPERTY(Transient) TObjectPtr<UEGSourcePreviewDefinition> BuiltDefinition;
	int32 BuiltMotion = INDEX_NONE;
	void ClearPreview();
	void RebuildPreview();
	void EvaluatePreview();
	void EvaluateEffects(const FEGSourcePreviewMotion& Motion);
	const FEGSourcePreviewMotion* CurrentMotion() const;
};
