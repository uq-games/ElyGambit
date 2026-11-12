#pragma once
#include "Materials/MaterialExpressionCustomOutput.h"
#include "MaterialExpressionEGNativeOutput.generated.h"

/** Unclamped original MRT values and source homogeneous clip position.
 * These do not travel through UE's opaque opacity or base-pass lighting. */
UCLASS(CollapseCategories, HideCategories=Object)
class EGNATIVERENDERER_API UMaterialExpressionEGNativeOutput : public UMaterialExpressionCustomOutput
{
    GENERATED_BODY()
public:
    UPROPERTY() FExpressionInput Color;
    UPROPERTY() FExpressionInput Auxiliary;
    UPROPERTY() FExpressionInput ClipPosition;
    virtual int32 GetNumOutputs() const override { return 3; }
    virtual FString GetFunctionName() const override { return TEXT("GetEGSourceNative"); }
    virtual FString GetDisplayName() const override { return TEXT("Original source pass"); }
#if WITH_EDITOR
    virtual int32 Compile(FMaterialCompiler* Compiler, int32 OutputIndex) override;
    virtual EShaderFrequency GetShaderFrequency(uint32 Index) override { return Index == 2 ? SF_Vertex : SF_Pixel; }
    virtual EMaterialValueType GetInputValueType(int32 InputIndex) override { return MCT_Float4; }
    virtual void GetCaption(TArray<FString>& OutCaptions) const override { OutCaptions.Add(GetDisplayName()); }
#endif
};
