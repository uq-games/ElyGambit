#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGRunPerformanceProbe.generated.h"

/** Opt-in Development benchmark. It never runs without the explicit EGProfile argument. */
UCLASS()
class ELYGAMBIT_API UEGRunPerformanceProbe : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGRunPerformanceProbe();
 virtual void TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 double StartTime=0,CaptureStart=0,CompletedTime=0,LastMemorySample=0;
 float Seconds=60;
 bool bPrepared=false,bScenarioSetup=false,bCapturing=false,bCompleted=false;
 FString Folder,Scenario,Quality,Rows;
 int32 Frame=0;
 uint64 Memory=0;
};
