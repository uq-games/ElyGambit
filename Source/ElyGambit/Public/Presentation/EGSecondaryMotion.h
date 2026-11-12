#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGSecondaryMotion.generated.h"

class USkeletalMesh;
class USkeletalMeshComponent;
class USkeleton;
class UAnimationAsset;
class UAnimSequence;

/** Parent of per-skeleton post-process graphs. All controls are cosmetic and instance-local. */
UCLASS(Blueprintable, BlueprintType)
class ELYGAMBIT_API UEGSecondaryMotionAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Secondary Motion") bool bSecondaryMotionEnabled = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Secondary Motion", meta=(ClampMin="0", ClampMax="1")) float BreastStrength = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Secondary Motion", meta=(ClampMin="0", ClampMax="1")) float GluteStrength = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Secondary Motion", meta=(ClampMin="0", ClampMax="1")) float ClothingStrength = 1.f;
	UFUNCTION(BlueprintCallable, Category="Secondary Motion") void ResetSecondaryMotion();
	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
private:
	bool bWasEnabled = true;
	FVector PreviousStrengths = FVector::OneVector;
	double LastUpdateTime = -1.;
};

UCLASS()
class ELYGAMBIT_API UEGSecondaryMotionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Secondary Motion")
	static bool SetSecondaryMotion(USkeletalMeshComponent* Mesh, bool bEnabled, float Breast=1.f, float Glute=1.f, float Clothing=1.f);
	UFUNCTION(BlueprintCallable, Category="Secondary Motion") static void ResetSecondaryMotion(USkeletalMeshComponent* Mesh);
	/** Editor-only JSON inventory, including source vertices and their actual weights when requested. */
	UFUNCTION(BlueprintCallable, Category="Secondary Motion|Authoring") static FString AuditMesh(USkeletalMesh* Mesh, bool bIncludeVertices=false);
	UFUNCTION(BlueprintCallable, Category="Secondary Motion|Authoring") static bool AssignDerivedSkeleton(UObject* Asset, USkeleton* Skeleton);
	/** Copies plain bone-only prototype sequences into a fresh model; rejects event/curve-bearing sources. */
	UFUNCTION(BlueprintCallable, Category="Secondary Motion|Authoring") static bool CopyBoneAnimation(UAnimSequence* Source, UAnimSequence* Destination);
	/** Synchronous editor validation of the actual post-process graph, against an identical disabled component. */
	UFUNCTION(BlueprintCallable, Category="Secondary Motion|Authoring") static FString ValidateMotion(USkeletalMesh* Mesh, UAnimationAsset* Animation, int32 FPS=60);
	UFUNCTION(BlueprintCallable, Category="Secondary Motion|Authoring") static void TickPreview(USkeletalMeshComponent* Mesh, float DeltaSeconds);
};
