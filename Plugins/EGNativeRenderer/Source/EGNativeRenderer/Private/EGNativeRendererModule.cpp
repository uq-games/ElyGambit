#include "EGNativeRendererInternal.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"
#include "MeshMaterialShader.h"
#include "MaterialShaderType.h"
#include "MeshMaterialShaderType.h"
#include "MeshPassProcessor.h"
#include "MeshPassProcessor.inl"
#include "MeshDrawShaderBindings.h"
#include "Materials/MaterialRenderProxy.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveSceneInfo.h"
#include "SkeletalMeshSceneProxy.h"
#include "RendererInterface.h"
#include "EngineModule.h"
#include "RenderGraphUtils.h"
#include "SimpleMeshDrawCommandPass.h"
#include "ShaderParameterStruct.h"
#include "VertexFactory.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RHIStaticStates.h"

TMap<uint64,FEGNativeRegistration> GEGNativeRegistrations;
std::atomic<uint64> GEGNativeFrames{0},GEGNativeDraws{0},GEGNativeMissingShaders{0},GEGNativeRegistrationCount{0};
std::atomic<uint64> GEGNativeDepthCommits{0};
DEFINE_LOG_CATEGORY_STATIC(LogEGNativeRenderer,Log,All);

static bool CompileNativeMesh(const FMeshMaterialShaderPermutationParameters& P)
{
    return IsPCPlatform(P.Platform) && P.MaterialParameters.MaterialDomain==MD_Surface &&
        P.MaterialParameters.bIsUsedWithSkeletalMesh && P.MaterialParameters.ShadingModels.HasShadingModel(MSM_Unlit) &&
        !P.VertexFactoryType->SupportsNaniteRendering();
}

class FEGNativeVS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FEGNativeVS,MeshMaterial);
public:
    FEGNativeVS()=default;
    FEGNativeVS(const ShaderMetaType::CompiledShaderInitializerType& I):FMeshMaterialShader(I) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& P) { return CompileNativeMesh(P); }
};
class FEGNativePS : public FMeshMaterialShader
{
    DECLARE_SHADER_TYPE(FEGNativePS,MeshMaterial);
public:
    FEGNativePS()=default;
    FEGNativePS(const ShaderMetaType::CompiledShaderInitializerType& I):FMeshMaterialShader(I) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& P) { return CompileNativeMesh(P); }
};
IMPLEMENT_MATERIAL_SHADER_TYPE(,FEGNativeVS,TEXT("/Plugin/EGNativeRenderer/Private/EGNativeMesh.usf"),TEXT("MainVS"),SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(,FEGNativePS,TEXT("/Plugin/EGNativeRenderer/Private/EGNativeMesh.usf"),TEXT("MainPS"),SF_Pixel);

class FEGNativeDepthCommitPS : public FGlobalShader
{
    DECLARE_GLOBAL_SHADER(FEGNativeDepthCommitPS);
    SHADER_USE_PARAMETER_STRUCT(FEGNativeDepthCommitPS,FGlobalShader);
    BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D<float>,SourceDepth)
        RENDER_TARGET_BINDING_SLOTS()
    END_SHADER_PARAMETER_STRUCT()
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& P)
    { return IsPCPlatform(P.Platform) && IsFeatureLevelSupported(P.Platform,ERHIFeatureLevel::SM5); }
};
IMPLEMENT_GLOBAL_SHADER(FEGNativeDepthCommitPS,"/Plugin/EGNativeRenderer/Private/EGNativeDepthCommit.usf","MainPS",SF_Pixel);

/** One ordered source draw per RDG pass. A global mesh sort cannot reorder
 * the stencil-only and the back/front translucent depth-writing operations. */
class FEGNativeMeshProcessor : public FMeshPassProcessor
{
public:
    FEGNativeMeshProcessor(const FScene* Scene,const FSceneView* View,FMeshPassDrawListContext* Context,
        const FEGNativeRegisteredPass& InPass)
        :FMeshPassProcessor(EMeshPass::Num,Scene,Scene->GetFeatureLevel(),View,Context),Pass(InPass) {}
    virtual void AddMeshBatch(const FMeshBatch& Mesh,uint64 Mask,const FPrimitiveSceneProxy* Proxy,int32 StaticMeshId=INDEX_NONE) override
    {
        const FMaterial* Mat=Pass.Material->GetMaterialNoFallback(FeatureLevel);
        if (!Mat || !Mat->GetRenderingThreadShaderMap()) { ++GEGNativeMissingShaders; return; }
        FMaterialShaderTypes Types; Types.AddShaderType<FEGNativeVS>(); Types.AddShaderType<FEGNativePS>();
        FMaterialShaders Found;
        if (!Mat->TryGetShaders(Types,Mesh.VertexFactory->GetType(),Found)) { ++GEGNativeMissingShaders; return; }
        TMeshProcessorShaders<FEGNativeVS,FEGNativePS> Shaders;
        Found.TryGetVertexShader(Shaders.VertexShader); Found.TryGetPixelShader(Shaders.PixelShader);
        FMeshMaterialShaderElementData Data;
        Data.InitializeMeshMaterialData(ViewIfDynamicMeshCommand,Proxy,Mesh,StaticMeshId,false);
        FMeshPassProcessorRenderState State;
        State.SetBlendState(Pass.Blend); State.SetDepthStencilState(Pass.DepthStencil);
        State.SetStencilRef(Pass.State.StencilReference);
        State.SetDepthStencilAccess(FExclusiveDepthStencil::DepthWrite_StencilWrite);
        FEGSourceNativeRHIDescriptors Native; FString Error;
        if (!EGMakeSourceNativeRHIState(Pass.State,true,Mesh.ReverseCulling ^ ViewIfDynamicMeshCommand->bReverseCulling,Native,Error)) return;
        FMeshBatch NativeMesh=Mesh; NativeMesh.MaterialRenderProxy=Pass.Material;
        BuildMeshDrawCommands(NativeMesh,Mask,Proxy,*Pass.Material,*Mat,State,Shaders,FM_Solid,
            Native.Rasterizer.CullMode,FMeshDrawCommandSortKey::Default,EMeshPassFeatures::Default,Data);
        ++GEGNativeDraws;
    }
private:
    const FEGNativeRegisteredPass& Pass;
};

BEGIN_SHADER_PARAMETER_STRUCT(FEGNativePassParameters,)
    SHADER_PARAMETER_STRUCT_INCLUDE(FViewShaderParameters,View)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters,Scene)
    SHADER_PARAMETER_STRUCT_INCLUDE(FInstanceCullingDrawParams,InstanceCullingDrawParams)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

struct FEGNativeDraw
{
    const FMeshBatch* Mesh=nullptr;
    const FPrimitiveSceneProxy* Proxy=nullptr;
    FEGNativeRegisteredPass Pass;
    uint64 Key=0;
    double DistanceSquared=0;
};

static void RenderNativePasses(FPostOpaqueRenderParameters& Input)
{
    check(IsInRenderingThread());
    if (GEGNativeRegistrations.IsEmpty() || !Input.View || !Input.ColorTexture || !Input.DepthTexture) return;
    const FViewInfo& View=*Input.View;
    // This implementation initially targets the existing deferred 7x7 renderer.
    if (View.GetFeatureLevel()<ERHIFeatureLevel::SM5 || Input.DepthTexture->Desc.NumSamples!=1) return;
    FRDGBuilder& Graph=*Input.GraphBuilder;
    const FScene* Scene=static_cast<const FScene*>(View.Family->Scene);
    TArray<FEGNativeDraw> Draws;
    for (const auto& Pair:GEGNativeRegistrations)
    {
        const auto& R=Pair.Value;
        if (R.Scene!=View.Family->Scene) continue;
        const int32 Before=Draws.Num();
        for (const auto& Batch:View.DynamicMeshElements)
        {
            if (Batch.PrimitiveSceneProxy->GetPrimitiveComponentId()!=R.Primitive) continue;
            for (const auto& P:R.Passes)
            {
                const auto* Sections=P.SectionsByLOD.Find(Batch.Mesh->LODIndex);
                if (!Sections || !Sections->Contains(Batch.Mesh->SegmentIndex)) continue;
                FEGNativeDraw& D=Draws.AddDefaulted_GetRef();
                D.Mesh=Batch.Mesh; D.Proxy=Batch.PrimitiveSceneProxy; D.Pass=P; D.Key=Pair.Key;
                D.DistanceSquared=FVector::DistSquared(D.Proxy->GetBounds().Origin,View.ViewMatrices.GetViewOrigin());
            }
        }
        // UE5.8 normally caches skeletal mesh batches. They do not appear in
        // DynamicMeshElements, even while their bones/morph streams animate.
        if (Draws.Num()==Before)
        {
            FPrimitiveSceneInfo* Info=R.Scene->GetPrimitiveSceneInfo(R.Primitive);
            if (!Info || !View.PrimitiveVisibilityMap[Info->GetIndex()]) continue;
            FPrimitiveSceneProxy* Proxy=Info->Proxy;
            const int32 LOD=R.bSkeletal ? static_cast<FSkeletalMeshSceneProxy*>(Proxy)->GetCurrentLODIndex() : Proxy->GetLOD(&View);
            for (const auto& Batch:Info->StaticMeshes)
            {
                if (Batch.LODIndex!=LOD) continue;
                for (const auto& P:R.Passes)
                {
                    const auto* Sections=P.SectionsByLOD.Find(LOD);
                    if (!Sections || !Sections->Contains(Batch.SegmentIndex)) continue;
                    FEGNativeDraw& D=Draws.AddDefaulted_GetRef();
                    D.Mesh=&Batch;D.Proxy=Proxy;D.Pass=P;D.Key=Pair.Key;
                    D.DistanceSquared=FVector::DistSquared(Proxy->GetBounds().Origin,View.ViewMatrices.GetViewOrigin());
                }
            }
        }
    }
    if (Draws.IsEmpty()) return;
    ++GEGNativeFrames;
    Draws.StableSort([](const FEGNativeDraw& A,const FEGNativeDraw& B)
    {
        if (A.Pass.Queue!=B.Pass.Queue) return A.Pass.Queue<B.Pass.Queue;
        if (A.Pass.SortingOrder!=B.Pass.SortingOrder) return A.Pass.SortingOrder<B.Pass.SortingOrder;
        if (A.Key!=B.Key && A.DistanceSquared!=B.DistanceSquared)
            return A.Pass.Queue<=2500 ? A.DistanceSquared<B.DistanceSquared : A.DistanceSquared>B.DistanceSquared;
        if (A.Key!=B.Key) return A.Key<B.Key;
        if (A.Pass.MaterialSlot!=B.Pass.MaterialSlot) return A.Pass.MaterialSlot<B.Pass.MaterialSlot;
        return A.Pass.PassIndex<B.Pass.PassIndex;
    });
    // Source stencil is private, so reference 4 never overwrites UE reserved bits.
    FRDGTextureDesc DepthDesc=Input.DepthTexture->Desc;
    DepthDesc.ClearValue=FClearValueBinding(0.0f,0);
    FRDGTextureRef Depth=Graph.CreateTexture(DepthDesc,TEXT("EGNative.PrivateDepthStencil"));
    AddCopyTexturePass(Graph,Input.DepthTexture,Depth);
    auto* Clear=Graph.AllocParameters<FEGNativePassParameters>();
    Clear->RenderTargets.DepthStencil=FDepthStencilBinding(Depth,ERenderTargetLoadAction::ELoad,ERenderTargetLoadAction::EClear,FExclusiveDepthStencil::DepthRead_StencilWrite);
    Graph.AddPass(RDG_EVENT_NAME("EGNative.ClearSourceStencil"),Clear,ERDGPassFlags::Raster,
        [](FRHICommandList&){});
    FRDGTextureRef Auxiliary=Graph.CreateTexture(FRDGTextureDesc::Create2D(Input.ColorTexture->Desc.Extent,
        PF_FloatRGBA,FClearValueBinding::Transparent,TexCreate_RenderTargetable|TexCreate_ShaderResource),TEXT("EGNative.SourceMRT1"));
    AddClearRenderTargetPass(Graph,Auxiliary,FLinearColor::Transparent);
    for (const auto& Row:Draws)
    {
        auto* D=Graph.AllocObject<FEGNativeDraw>(Row);
        auto* Params=Graph.AllocParameters<FEGNativePassParameters>();
        Params->View.View=View.ViewUniformBuffer;
        Params->View.InstancedView=View.GetInstancedViewUniformBuffer();
        Params->Scene=View.GetSceneUniforms().GetBuffer(Graph);
        Params->RenderTargets[0]=FRenderTargetBinding(Input.ColorTexture,ERenderTargetLoadAction::ELoad);
        Params->RenderTargets[1]=FRenderTargetBinding(Auxiliary,ERenderTargetLoadAction::ELoad);
        Params->RenderTargets.DepthStencil=FDepthStencilBinding(Depth,ERenderTargetLoadAction::ELoad,ERenderTargetLoadAction::ELoad,FExclusiveDepthStencil::DepthWrite_StencilWrite);
        AddSimpleMeshPass(Graph,Params,Scene,View,nullptr,RDG_EVENT_NAME("EGNative.Q%d.P%d",D->Pass.Queue,D->Pass.PassIndex),View.ViewRect,
            [Scene,&View,D](FDynamicPassMeshDrawListContext* Context)
            {
                FEGNativeMeshProcessor Processor(Scene,&View,Context,D->Pass);
                Processor.AddMeshBatch(*D->Mesh,~0ull,D->Proxy);
            });
    }
    // Interleaved source draws completed against private depth+stencil. Make
    // their final depth available to later UE transparency without copying any
    // source stencil reference into UE's reserved stencil bits.
    auto* Commit=Graph.AllocParameters<FEGNativeDepthCommitPS::FParameters>();
    Commit->SourceDepth=Depth;
    Commit->RenderTargets.DepthStencil=FDepthStencilBinding(Input.DepthTexture,ERenderTargetLoadAction::ELoad,
        ERenderTargetLoadAction::ELoad,FExclusiveDepthStencil::DepthWrite_StencilRead);
    TShaderMapRef<FEGNativeDepthCommitPS> CommitShader(View.ShaderMap);
    FPixelShaderUtils::AddFullscreenPass(Graph,View.ShaderMap,RDG_EVENT_NAME("EGNative.CommitDepthPreserveUEStencil"),
        CommitShader,Commit,View.ViewRect,nullptr,nullptr,TStaticDepthStencilState<true,CF_Always>::GetRHI());
    ++GEGNativeDepthCommits;
    // Final source MRT/bloom composition remains separate from these mesh draws.
    static uint32 LoggedFrames=0;
    if (LoggedFrames++<3) UE_LOG(LogEGNativeRenderer,Display,TEXT("Native ordered draws=%d; private source stencil; depth-only commit; source bloom composition pending"),Draws.Num());
}

class FEGNativeRendererModule : public IModuleInterface
{
    FDelegateHandle Handle;
public:
    virtual void StartupModule() override
    {
        const auto Plugin=IPluginManager::Get().FindPlugin(TEXT("EGNativeRenderer"));
        check(Plugin.IsValid());
        AddShaderSourceDirectoryMapping(TEXT("/Plugin/EGNativeRenderer"),FPaths::Combine(Plugin->GetBaseDir(),TEXT("Shaders")));
        Handle=GetRendererModule().RegisterPostOpaqueRenderDelegate(FPostOpaqueRenderDelegate::CreateStatic(&RenderNativePasses));
    }
    virtual void ShutdownModule() override
    {
        if (FModuleManager::Get().IsModuleLoaded(TEXT("Renderer"))) GetRendererModule().RemovePostOpaqueRenderDelegate(Handle);
        FlushRenderingCommands();
        GEGNativeRegistrations.Reset();
    }
};
IMPLEMENT_MODULE(FEGNativeRendererModule,EGNativeRenderer)
