#pragma once
#include "CoreMinimal.h"
#include "RHI.h"
#include "EGNativeRenderState.generated.h"

/** Unity enum values, retained until the fixed-function RHI boundary. */
USTRUCT(BlueprintType)
struct EGNATIVERENDERER_API FEGSourceNativeRenderState
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SourceBlend=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DestinationBlend=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SourceAlphaBlend=1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DestinationAlphaBlend=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 BlendOperation=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 AlphaBlendOperation=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 ColorMask=15;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 DepthCompare=4;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Cull=2;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bDepthWrite=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilCompare=8;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilPass=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilFail=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilDepthFail=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilReference=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilReadMask=255;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 StencilWriteMask=255;
};

struct FEGSourceNativeRHIDescriptors
{
    FBlendStateInitializerRHI Blend;
    FDepthStencilStateInitializerRHI DepthStencil;
    FRasterizerStateInitializerRHI Rasterizer;
    uint32 StencilReference=0;
};

EGNATIVERENDERER_API bool EGMakeSourceNativeRHIState(const FEGSourceNativeRenderState& Source,
    bool bReversedDepth, bool bReverseCull, FEGSourceNativeRHIDescriptors& Out, FString& Error);
