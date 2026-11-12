#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGAnimationRetargetLibrary.generated.h"

class UAnimSequence;
class USkeletalMesh;

/** Editor authoring bridge for imported skeletons with non-unit reference scale. */
UCLASS()
class ELYGAMBITEDITOR_API UEGAnimationRetargetLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Copy a unit-scale retarget bake into the original, identically shaped reference hierarchy. */
	UFUNCTION(BlueprintCallable, Category="Animation|Authoring")
	static bool CopyUnitScaleRetarget(UAnimSequence* Source, UAnimSequence* Destination, USkeletalMesh* TargetMesh);
};
