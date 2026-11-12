#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGAnimationEditLibrary.generated.h"

class UAnimSequence;

/**
 * Editor-side bridge that lets Python round-trip complete bone animation data.
 *
 * The Python bindings cannot read bone keys back out of IAnimationDataModel, so ground fitting,
 * clip repair and hand authored keys are all done through this plain text document:
 *
 *   v1
 *   fps <numerator> <denominator>
 *   frames <count>
 *   bone <name>
 *   k <px,py,pz> <rx,ry,rz,rw> <sx,sy,sz>
 *   k ...
 *   bone <name>
 *   ...
 */
UCLASS()
class ELYGAMBITEDITOR_API UEGAnimationEditLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Serialize every bone track of a sequence to the plain text document above. */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static FString ExportTrackDocument(UAnimSequence* Animation);

	/** Replace every bone track of a sequence from a document produced by ExportTrackDocument. */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static bool ImportTrackDocument(UAnimSequence* Animation, const FString& Document);

	/** Adapt only the common carrier root; leave every internal/source track untouched. */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static bool LockPlanarRootTranslation(UAnimSequence* Animation);

	/** True when the sequence stores plain bone tracks only (no notifies, curves or attributes). */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static bool IsPlainBoneAnimation(UAnimSequence* Animation);

	/** Number of frames and frame rate of an existing sequence, "frames,fps" for quick logging. */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static FString DescribeTiming(UAnimSequence* Animation);

	/** Repoint every bone track at the given skeletal mesh' skeleton (same bone names required). */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static bool RebindSkeleton(UAnimSequence* Animation, class USkeletalMesh* Mesh);
};
