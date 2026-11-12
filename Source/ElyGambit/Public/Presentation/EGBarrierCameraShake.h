#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "EGBarrierCameraShake.generated.h"

/** Camera modifier pattern sampled from the wall's presentation clock. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API UEGBarrierShakePattern : public UCameraShakePattern
{
 GENERATED_BODY()
public:
 UEGBarrierShakePattern(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer){}
 void SetVisualElapsed(float Seconds){Elapsed=FMath::Clamp(Seconds,0.f,.32f);}
 void SetLease(FGuid Token){Lease=Token;}
 bool HasLease(FGuid Token) const{return Token.IsValid()&&Lease==Token;}
private:
 virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
 virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
 virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& OutResult) override;
 virtual bool IsFinishedImpl() const override;
 float Elapsed=0.f;
 FGuid Lease;
};

UCLASS(NotBlueprintable)
class ELYGAMBIT_API UEGBarrierCameraShake : public UCameraShakeBase
{
 GENERATED_BODY()
public:
 UEGBarrierCameraShake(const FObjectInitializer& ObjectInitializer);
 void SetVisualElapsed(float Seconds);
 void SetLease(FGuid Token);
 bool HasLease(FGuid Token) const;
};
