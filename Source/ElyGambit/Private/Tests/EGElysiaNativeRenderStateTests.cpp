#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Presentation/EGSourceNativeRenderState.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaNativeSourceFixedStateTest,"ElyGambit.ElysiaNative.SourceFixedFunctionState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaNativeSourceFixedStateTest::RunTest(const FString&)
{
 FEGSourceNativeRenderState S;FEGSourceNativeRHIDescriptors R;FString Error;
 // Source BodyA_Alpha pass 0: ColorMask 0, ZWrite 0, Stencil Replace 4.
 S.ColorMask=0;S.bDepthWrite=false;S.Cull=0;S.StencilPass=2;S.StencilReference=4;
 TestTrue(TEXT("Stencil-only source state is representable"),EGMakeSourceNativeRHIState(S,true,false,R,Error));
 TestEqual(TEXT("No accidental color from source mask-only draw"),int32(R.Blend.RenderTargets[0].ColorWriteMask),int32(CW_NONE));
 TestFalse(TEXT("Mask pass does not accidentally write depth"),R.DepthStencil.bEnableDepthWrite);
 TestTrue(TEXT("Both source faces update stencil"),R.DepthStencil.bEnableFrontFaceStencil&&R.DepthStencil.bEnableBackFaceStencil);
 TestEqual(TEXT("Original replace operation survives"),int32(R.DepthStencil.FrontFacePassStencilOp),int32(SO_Replace));TestEqual(TEXT("Original reference survives"),R.StencilReference,4u);
 TestEqual(TEXT("Depth compare reverses with actual UE reversed Z"),int32(R.DepthStencil.DepthTest),int32(CF_GreaterEqual));
 // Source color passes: source-alpha blending AND depth writes. A normal UE
 // translucent material would lose this depth-writing requirement.
 S.ColorMask=15;S.bDepthWrite=true;S.SourceBlend=S.SourceAlphaBlend=5;S.DestinationBlend=S.DestinationAlphaBlend=10;S.StencilPass=0;S.StencilReference=0;S.Cull=1;
 TestTrue(TEXT("Back-face color state is representable"),EGMakeSourceNativeRHIState(S,true,false,R,Error));
 TestTrue(TEXT("Translucent source depth writes must be retained"),R.DepthStencil.bEnableDepthWrite);
 TestEqual(TEXT("Original source-alpha factor"),int32(R.Blend.RenderTargets[0].ColorSrcBlend),int32(BF_SourceAlpha));TestEqual(TEXT("Original inverse-alpha factor"),int32(R.Blend.RenderTargets[0].ColorDestBlend),int32(BF_InverseSourceAlpha));
 TestEqual(TEXT("Original back surface culling"),int32(R.Rasterizer.CullMode),int32(CM_CCW));
 S.Cull=2;TestTrue(TEXT("Front-face color state is representable"),EGMakeSourceNativeRHIState(S,true,false,R,Error));TestEqual(TEXT("Original front surface culling"),int32(R.Rasterizer.CullMode),int32(CM_CW));
 S.StencilCompare=2;S.ColorMask=8;TestTrue(TEXT("Selective native state"),EGMakeSourceNativeRHIState(S,true,true,R,Error));TestEqual(TEXT("Stencil comparisons do not reverse with device Z"),int32(R.DepthStencil.FrontFaceStencilTest),int32(CF_Less));TestEqual(TEXT("Source red mask converts rather than copying enum bits"),int32(R.Blend.RenderTargets[0].ColorWriteMask),int32(CW_RED));TestEqual(TEXT("Negative determinant/view flips cull"),int32(R.Rasterizer.CullMode),int32(CM_CCW));
 S.DepthCompare=0;TestTrue(TEXT("Disabled native depth state"),EGMakeSourceNativeRHIState(S,true,false,R,Error));TestFalse(TEXT("Disabling depth also disables depth writes"),R.DepthStencil.bEnableDepthWrite);
 S.SourceBlend=9;TestFalse(TEXT("Unrepresentable source factor is rejected, never approximated"),EGMakeSourceNativeRHIState(S,true,false,R,Error));
 return true;
}
#endif
