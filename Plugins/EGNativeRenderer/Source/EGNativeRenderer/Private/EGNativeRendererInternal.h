#pragma once
#include "CoreMinimal.h"
#include "PrimitiveComponentId.h"
#include "EGNativeRenderState.h"
#include <atomic>
class FSceneInterface;
class FMaterialRenderProxy;
struct FEGNativeRegisteredPass
{
    const FMaterialRenderProxy* Material=nullptr;
    const FMaterialRenderProxy* SlotMaterial=nullptr;
    TMap<int32,TSet<int32>> SectionsByLOD;
    int32 MaterialSlot=0,Queue=2000,SortingOrder=0,PassIndex=0;
    FEGSourceNativeRenderState State;
    FEGSourceNativeRenderState AuxiliaryState;
    FBlendStateRHIRef Blend;
    FDepthStencilStateRHIRef DepthStencil;
    bool bWritesAuxiliary=false;
};
struct FEGNativeRegistration
{
    FPrimitiveComponentId Primitive;
    const FSceneInterface* Scene=nullptr;
    bool bSkeletal=false;
    TArray<FEGNativeRegisteredPass> Passes;
};
// Render-thread only. No UObject pointer is read by render callbacks.
extern TMap<uint64,FEGNativeRegistration> GEGNativeRegistrations;
extern std::atomic<uint64> GEGNativeFrames,GEGNativeDraws,GEGNativeMissingShaders,GEGNativeRegistrationCount;
extern std::atomic<uint64> GEGNativeDepthCommits;
