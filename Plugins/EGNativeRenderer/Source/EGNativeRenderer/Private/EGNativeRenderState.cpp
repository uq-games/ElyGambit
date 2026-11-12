#include "EGNativeRenderState.h"

namespace
{
 bool BlendFactor(int32 Value,EBlendFactor& Out)
 {
  switch(Value)
  {
   case 0:Out=BF_Zero;return true;case 1:Out=BF_One;return true;
   case 2:Out=BF_DestColor;return true;case 3:Out=BF_SourceColor;return true;
   case 4:Out=BF_InverseDestColor;return true;case 5:Out=BF_SourceAlpha;return true;
   case 6:Out=BF_InverseSourceColor;return true;case 7:Out=BF_DestAlpha;return true;
   case 8:Out=BF_InverseDestAlpha;return true;case 10:Out=BF_InverseSourceAlpha;return true;
   default:return false; // Unity SrcAlphaSaturate has no equivalent EBlendFactor.
  }
 }
 ECompareFunction Compare(int32 Value,bool bReverse)
 {
  switch(Value)
  {
   case 1:return CF_Never;case 2:return bReverse?CF_Greater:CF_Less;
   case 3:return CF_Equal;case 4:return bReverse?CF_GreaterEqual:CF_LessEqual;
   case 5:return bReverse?CF_Less:CF_Greater;case 6:return CF_NotEqual;
   case 7:return bReverse?CF_LessEqual:CF_GreaterEqual;default:return CF_Always;
  }
 }
 EBlendOperation BlendOperation(int32 Value)
 {
  const EBlendOperation Values[]={BO_Add,BO_Subtract,BO_ReverseSubtract,BO_Min,BO_Max};return Values[Value];
 }
 EStencilOp StencilOperation(int32 Value)
 {
  const EStencilOp Values[]={SO_Keep,SO_Zero,SO_Replace,SO_SaturatedIncrement,SO_SaturatedDecrement,SO_Invert,SO_Increment,SO_Decrement};return Values[Value];
 }
}
bool EGMakeSourceNativeRHIState(const FEGSourceNativeRenderState& S,bool bReverseDepth,bool bReverseCull,FEGSourceNativeRHIDescriptors& Out,FString& Error)
{
 Error.Reset();
 if(S.DepthCompare<0||S.DepthCompare>8||S.StencilCompare<0||S.StencilCompare>8||S.Cull<0||S.Cull>2||
  S.BlendOperation<0||S.BlendOperation>4||S.AlphaBlendOperation<0||S.AlphaBlendOperation>4||
  S.ColorMask<0||S.ColorMask>15||S.StencilPass<0||S.StencilPass>7||S.StencilFail<0||S.StencilFail>7||S.StencilDepthFail<0||S.StencilDepthFail>7||
  S.StencilReference<0||S.StencilReference>255||S.StencilReadMask<0||S.StencilReadMask>255||S.StencilWriteMask<0||S.StencilWriteMask>255)
 {Error=TEXT("Invalid original source raster/depth/stencil/blend value");return false;}
 EBlendFactor Src,Dst,SrcAlpha,DstAlpha;
 if(!BlendFactor(S.SourceBlend,Src)||!BlendFactor(S.DestinationBlend,Dst)||!BlendFactor(S.SourceAlphaBlend,SrcAlpha)||!BlendFactor(S.DestinationAlphaBlend,DstAlpha))
 {Error=TEXT("Unsupported original source blend factor; no approximate replacement");return false;}
 // Unity ColorWriteMask uses R=8,G=4,B=2,A=1; UE RHI uses R=1,G=2,B=4,A=8.
 const EColorWriteMask Mask=static_cast<EColorWriteMask>(((S.ColorMask&8)?CW_RED:0)|((S.ColorMask&4)?CW_GREEN:0)|((S.ColorMask&2)?CW_BLUE:0)|((S.ColorMask&1)?CW_ALPHA:0));
 FEGSourceNativeRHIDescriptors Candidate;
 Candidate.Blend=FBlendStateInitializerRHI(FBlendStateInitializerRHI::FRenderTarget(BlendOperation(S.BlendOperation),Src,Dst,BlendOperation(S.AlphaBlendOperation),SrcAlpha,DstAlpha,Mask));
 const bool StencilEnabled=S.StencilCompare!=0;
 Candidate.DepthStencil=FDepthStencilStateInitializerRHI(S.bDepthWrite&&S.DepthCompare!=0,Compare(S.DepthCompare,bReverseDepth),
  StencilEnabled,Compare(S.StencilCompare,false),StencilOperation(S.StencilFail),StencilOperation(S.StencilDepthFail),StencilOperation(S.StencilPass),
  StencilEnabled,Compare(S.StencilCompare,false),StencilOperation(S.StencilFail),StencilOperation(S.StencilDepthFail),StencilOperation(S.StencilPass),uint8(S.StencilReadMask),uint8(S.StencilWriteMask));
 Candidate.Rasterizer.FillMode=FM_Solid;
 Candidate.Rasterizer.CullMode=S.Cull==0?CM_None:((S.Cull==1)^bReverseCull?CM_CCW:CM_CW);
 Candidate.StencilReference=S.StencilReference;
 Out=Candidate;return true;
}

