#pragma once
#include "CoreMinimal.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "EGCombatVisual.generated.h"

/** Runtime source rig. Every animated part evaluates an AnimBlueprint. No gameplay authority. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGCombatVisual : public AActor
{
	GENERATED_BODY()
public:
	AEGCombatVisual();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Presentation") TObjectPtr<UEGSourcePreviewDefinition> Definition;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Presentation", meta=(ClampMin="0")) int32 MotionIndex = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Presentation", meta=(ClampMin="0.01", ClampMax="4")) float PlayRate = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Presentation") bool bLoop = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Combat Presentation") bool bAutoPlay = true;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Combat Presentation") float SourceTime = 0.f;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category="Combat Presentation") bool bPlaying = false;

	UFUNCTION(BlueprintCallable, CallInEditor, Category="Combat Presentation") void Play();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Combat Presentation") void Pause();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Combat Presentation") void Stop();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Combat Presentation") void NextMotion();
	UFUNCTION(BlueprintCallable, CallInEditor, Category="Combat Presentation") void PreviousMotion();
	UFUNCTION(BlueprintCallable, Category="Combat Presentation") void SetSourceTime(float Time);
	bool GetVisiblePartBoneLocation(FName PartId,FName Bone,FVector& Location) const;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
 /** The real displayed chess mesh. It retains the authoritative montage and AnimGraph. */
 UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> BodyMesh;
 /** Passive/seated/device bodies use their source pose; attacks retain the main montage. */
 UPROPERTY(Transient) bool bDriveBodyPose=false;
 UPROPERTY(Transient) int32 BodyState=0;
 /** Apply to model/weapon parts only; the actor transform and effects remain at authored size. */
 void SetBoardModelScale(float Scale,float GroundOffset=0.f);
 void BindBody(USkeletalMeshComponent* Mesh,bool bSourcePose,int32 State=0);
 /** Return the borrowed body synchronously before destroying or replacing this visual. */
 void ReleaseBoundBody(bool bRestore=true);
private:
 float BoardModelScale=1.f;
 float BoardGroundOffset=0.f;
 UPROPERTY(Transient) TObjectPtr<USkeletalMesh> OriginalMesh;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;
 FTransform OriginalBodyTransform;
 bool bOriginalVisibility=true;
 bool bOriginalRenderInMainPass=true;
 bool bOriginalRenderInDepthPass=true;
 int32 OriginalTranslucencySortPriority=0;
 TArray<FName> OriginalHiddenBones;
 TArray<uint8> OriginalSectionVisibility;
 int32 OriginalSectionSlotCount=0;
 void SamplePart(USkeletalMeshComponent* Part,UAnimSequence* Clip,float Time);
	UPROPERTY(Transient) TArray<TObjectPtr<USkeletalMeshComponent>> PreviewParts;
	/** INDEX_NONE is actor-rooted, -2 is an invalid attachment, otherwise an earlier parent. */
	TArray<int32> AttachmentParents;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMaterials;
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> PreviewEffects;
	UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewEffectMaterials;
	TArray<FIntPoint> EffectTrackSources;
	TMap<int32, FTransform> EffectBirthAnchors;
	UPROPERTY(Transient) TObjectPtr<UEGSourcePreviewDefinition> BuiltDefinition;
	int32 BuiltMotion = INDEX_NONE;
	void ClearPreview();
	void RebuildPreview();
	void EvaluatePreview();
	void EvaluateEffects(const FEGSourcePreviewMotion& Motion);
	const FEGSourcePreviewMotion* CurrentMotion() const;
};
