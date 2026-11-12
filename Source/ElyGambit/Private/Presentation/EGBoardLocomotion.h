#pragma once

#include "CoreMinimal.h"

// Presentation only: hold the measured gait across short movement gaps and
// use separate run thresholds to avoid rebuilding clips around the boundary.
struct FEGBoardLocomotion
{
 void Reset()
 {
  bHasPosition=false;bMoving=false;bRunning=false;
  StopHold=0.f;MeasuredSpeed=0.f;
 }

 float Sample(const FVector& Position,float DeltaSeconds,float RunThreshold)
 {
  if(!bHasPosition)
  {
   PreviousPosition=Position;bHasPosition=true;
   return 0.f;
  }
  const FVector Before=PreviousPosition;PreviousPosition=Position;
  if(!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<=SMALL_NUMBER||Position.ContainsNaN()||Before.ContainsNaN())
  {Reset();return 0.f;}
  const float ActualSpeed=FVector::Dist2D(Before,Position)/DeltaSeconds;
  const float EnterOrKeep=bMoving?2.f:4.f;
  if(FMath::IsFinite(ActualSpeed)&&ActualSpeed>=EnterOrKeep)
  {
   bMoving=true;StopHold=.18f;MeasuredSpeed=ActualSpeed;
   if(FMath::IsFinite(RunThreshold)&&RunThreshold>1.f)
    bRunning=ActualSpeed>=RunThreshold*(bRunning?.9f:1.1f);
   else bRunning=false;
  }
  else if(bMoving)
  {
   StopHold=FMath::Max(0.f,StopHold-DeltaSeconds);
   if(StopHold<=0.f){bMoving=false;bRunning=false;MeasuredSpeed=0.f;}
  }
  if(!bMoving)return 0.f;
  // The AnimInstance accepts speed and applies its own run threshold.
  if(FMath::IsFinite(RunThreshold)&&RunThreshold>1.f)
   return bRunning?FMath::Max(MeasuredSpeed,RunThreshold):FMath::Max(1.f,FMath::Min(MeasuredSpeed,RunThreshold-.01f));
  return FMath::Max(MeasuredSpeed,1.f);
 }

 bool IsRunning() const {return bMoving&&bRunning;}

private:
 FVector PreviousPosition=FVector::ZeroVector;
 float StopHold=0.f,MeasuredSpeed=0.f;
 bool bHasPosition=false,bMoving=false,bRunning=false;
};
