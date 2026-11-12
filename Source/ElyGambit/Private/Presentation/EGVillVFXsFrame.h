#pragma once
#include "CoreMinimal.h"
class UNiagaraComponent;
struct FEGVillVFXsLayer;
// Private presentation helper; never submits hits or owns gameplay state.
void EGEvaluateVillVFXsFrame(const FEGVillVFXsLayer& Layer,UNiagaraComponent* Component,float Age,const FTransform& Anchor,const FTransform& Muzzle,const FVector& Target,float Scale,const FVector& CameraLocation,const FRotator& ViewRotation,TMap<int32,FTransform>& BirthAnchors,int32 KeyOffset=0);
