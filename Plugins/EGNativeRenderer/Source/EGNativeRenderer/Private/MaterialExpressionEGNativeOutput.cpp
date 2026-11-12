#include "MaterialExpressionEGNativeOutput.h"
#include "MaterialCompiler.h"

#if WITH_EDITOR
int32 UMaterialExpressionEGNativeOutput::Compile(FMaterialCompiler* Compiler, int32 OutputIndex)
{
    FExpressionInput* Input = OutputIndex == 0 ? &Color : OutputIndex == 1 ? &Auxiliary : &ClipPosition;
    if (OutputIndex < 0 || OutputIndex > 2) return Compiler->Errorf(TEXT("Invalid source pass output"));
    if (!Input->IsConnected() && OutputIndex != 1)
        return Compiler->Errorf(TEXT("Original source color and clip position are required"));
    return Compiler->CustomOutput(this, OutputIndex, Input->IsConnected() ? Input->Compile(Compiler) : Compiler->Constant4(0,0,0,0));
}
#endif
