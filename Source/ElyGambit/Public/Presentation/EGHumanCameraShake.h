#pragma once
#include "Presentation/EGBarrierCameraShake.h"
#include "EGHumanCameraShake.generated.h"

/** A separate single-instance channel: Human cues never restart the barrier shake. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API UEGHumanCameraShake : public UEGBarrierCameraShake
{
 GENERATED_BODY()
public:
 UEGHumanCameraShake(const FObjectInitializer& Init):Super(Init){bSingleInstance=true;}
};
