#include "EGNativeMeshPassComponent.h"
#include "EGNativeRendererInternal.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Misc/App.h"

UEGNativeMeshPassComponent::UEGNativeMeshPassComponent()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.TickGroup=TG_PostUpdateWork;
    SetIsReplicatedByDefault(false);
}
static UObject* SourceMeshAsset(UMeshComponent* Mesh)
{
    if (auto* Skeletal=Cast<USkeletalMeshComponent>(Mesh)) return Skeletal->GetSkeletalMeshAsset();
    if (auto* Static=Cast<UStaticMeshComponent>(Mesh)) return Static->GetStaticMesh();
    return nullptr;
}
bool UEGNativeMeshPassComponent::Configure(UMeshComponent* Mesh,const TArray<FEGNativeMeshPass>& Passes,bool bReplaceMainPass)
{
    if (!IsValid(Mesh) || Mesh->GetOwner()!=GetOwner() || Passes.IsEmpty()) return false;
    UObject* Asset=SourceMeshAsset(Mesh);
    if (auto* Skeletal=Cast<USkeletalMesh>(Asset))
    {
#if WITH_EDITOR
        if (Skeletal->IsNaniteEnabled()) return false;
#endif
        const FSkeletalMeshRenderData* Data=Skeletal->GetResourceForRendering();
        if (!Data || Data->HasValidNaniteData()) return false;
    }
    else if (auto* Static=Cast<UStaticMesh>(Asset))
    {
#if WITH_EDITOR
        if (Static->IsNaniteEnabled()) return false;
#endif
        if (Static->HasValidNaniteData() || !Static->GetRenderData()) return false;
    }
    else return false;
    for (const auto& Pass:Passes)
    {
        FEGSourceNativeRHIDescriptors State; FString Error;
        if (!IsValid(Pass.Material) || Pass.MaterialSlot<0 || Pass.MaterialSlot>=Mesh->GetNumMaterials() || !Mesh->GetMaterial(Pass.MaterialSlot) ||
            !EGMakeSourceNativeRHIState(Pass.State,true,false,State,Error) ||
            !EGMakeSourceNativeRHIState(Pass.AuxiliaryState,true,false,State,Error)) return false;
    }
    // UObject residency may complete before the editor's asynchronous shader
    // map has reached the render thread. Native passes have no fallback art.
    // Resolve compilation once at binding creation, before ordinary rendering
    // is disabled or any source draw is registered. Packaged shaders are cooked.
    if (FApp::CanEverRender()) for (const auto& Pass:Passes) Pass.Material->EnsureIsComplete();
    ReleaseBindings();
    Target=Mesh; NativePasses=Passes;
    for (auto& Pass:NativePasses)
        Pass.Material=UMaterialInstanceDynamic::Create(Pass.Material,this);
    bOriginalMainPass=Mesh->bRenderInMainPass; bOriginalDepthPass=Mesh->bRenderInDepthPass;
    bReplaced=bReplaceMainPass;
    if (bReplaced) { Mesh->SetRenderInMainPass(false); Mesh->SetRenderInDepthPass(false); }
    static uint64 NextKey=0;
    RegistryKey=++NextKey;
    SubmitBindings();
    return true;
}
bool UEGNativeMeshPassComponent::ApplyDefinition(UMeshComponent* Mesh,UEGNativeMeshPassDefinition* Definition,UObject* LifetimeOwner)
{
    if (!IsValid(Definition) || !IsValid(LifetimeOwner)) return false;
    if (Mesh==Target && Definition==BoundDefinition && IsBindingOwnedBy(LifetimeOwner)) return true;
    if (!Configure(Mesh,Definition->Passes,Definition->bReplaceMainPass)) return false;
    BoundDefinition=Definition;BindingOwner=LifetimeOwner;bHasLifetimeOwner=true;
    return true;
}
void UEGNativeMeshPassComponent::SetNativeScalarParameter(FName Name,float Value)
{
    for (const auto& Pass:NativePasses)
        if (auto* Material=Cast<UMaterialInstanceDynamic>(Pass.Material)) Material->SetScalarParameterValue(Name,Value);
}
void UEGNativeMeshPassComponent::SubmitBindings()
{
    if (!Target || !Target->IsRegistered() || !GetWorld() || !GetWorld()->Scene) return;
    FEGNativeRegistration Registration;
    Registration.Primitive=Target->GetPrimitiveSceneId(); Registration.Scene=GetWorld()->Scene;
    UObject* MeshAsset=SourceMeshAsset(Target);
    const auto* Asset=Cast<USkeletalMesh>(MeshAsset);
    const auto* StaticAsset=Cast<UStaticMesh>(MeshAsset);
    const FSkeletalMeshRenderData* RenderData=Asset ? Asset->GetResourceForRendering() : nullptr;
    const FStaticMeshRenderData* StaticData=StaticAsset ? StaticAsset->GetRenderData() : nullptr;
    if (!RenderData && !StaticData) return;
    Registration.bSkeletal=Asset!=nullptr;
    for (const auto& Pass:NativePasses)
    {
        auto& P=Registration.Passes.AddDefaulted_GetRef();
        P.Material=Pass.Material->GetRenderProxy(); P.MaterialSlot=Pass.MaterialSlot;
        P.SlotMaterial=Target->GetMaterial(Pass.MaterialSlot)->GetRenderProxy();
        P.Queue=Pass.Queue; P.SortingOrder=Pass.SortingOrder; P.PassIndex=Pass.PassIndex; P.State=Pass.State;
        P.AuxiliaryState=Pass.AuxiliaryState;
        P.bWritesAuxiliary=Pass.bWritesAuxiliary;
        if (RenderData) for (int32 LOD=0;LOD<RenderData->LODRenderData.Num();++LOD)
        {
            const FSkeletalMeshLODInfo* Info=Asset->GetLODInfo(LOD);
            const auto& Sections=RenderData->LODRenderData[LOD].RenderSections;
            for (int32 Section=0;Section<Sections.Num();++Section)
            {
                int32 Material=Sections[Section].MaterialIndex;
                if (Info && Info->LODMaterialMap.IsValidIndex(Section) && Info->LODMaterialMap[Section]>=0)
                    Material=Info->LODMaterialMap[Section];
                if (Material==Pass.MaterialSlot) P.SectionsByLOD.FindOrAdd(LOD).Add(Section);
            }
        }
        if (StaticData) for (int32 LOD=0;LOD<StaticData->LODResources.Num();++LOD)
        {
            const auto& Sections=StaticData->LODResources[LOD].Sections;
            for (int32 Section=0;Section<Sections.Num();++Section)
                if (Sections[Section].MaterialIndex==Pass.MaterialSlot) P.SectionsByLOD.FindOrAdd(LOD).Add(Section);
        }
    }
    const uint64 Key=RegistryKey;
    ENQUEUE_RENDER_COMMAND(EGNativeRegister)([Key,Registration=MoveTemp(Registration)](FRHICommandListImmediate&) mutable
    {
        for (auto& Pass:Registration.Passes)
        {
            FEGSourceNativeRHIDescriptors State,Aux; FString Error;
            if (!EGMakeSourceNativeRHIState(Pass.State,true,false,State,Error) ||
                !EGMakeSourceNativeRHIState(Pass.AuxiliaryState,true,false,Aux,Error)) return;
            State.Blend.bUseIndependentRenderTargetBlendStates=true;
            State.Blend.RenderTargets[1]=Aux.Blend.RenderTargets[0];
            if (!Pass.bWritesAuxiliary) State.Blend.RenderTargets[1].ColorWriteMask=CW_NONE;
            Pass.Blend=RHICreateBlendState(State.Blend);
            Pass.DepthStencil=RHICreateDepthStencilState(State.DepthStencil);
        }
        GEGNativeRegistrations.Add(Key,MoveTemp(Registration));
        GEGNativeRegistrationCount.store(GEGNativeRegistrations.Num());
    });
    bSubmitted=true;
    SubmittedMesh=MeshAsset;
}
void UEGNativeMeshPassComponent::ReleaseBindings()
{
    if (RegistryKey)
    {
        const uint64 Key=RegistryKey;
        ENQUEUE_RENDER_COMMAND(EGNativeUnregister)([Key](FRHICommandListImmediate&)
        { GEGNativeRegistrations.Remove(Key); GEGNativeRegistrationCount.store(GEGNativeRegistrations.Num()); });
    }
    if (IsValid(Target) && bReplaced)
    { Target->SetRenderInMainPass(bOriginalMainPass); Target->SetRenderInDepthPass(bOriginalDepthPass); }
    RegistryKey=0; bSubmitted=false; bReplaced=false; Target=nullptr; NativePasses.Reset(); SubmittedMesh.Reset();
    BoundDefinition=nullptr;BindingOwner.Reset();bHasLifetimeOwner=false;
}
void UEGNativeMeshPassComponent::OnUnregister() { ReleaseBindings(); Super::OnUnregister(); }
void UEGNativeMeshPassComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    if ((Target && !IsValid(Target)) || (bHasLifetimeOwner && !BindingOwner.IsValid())) ReleaseBindings();
    else if (Target)
    {
        // Existing action presentation may refresh the borrowed body's flags.
        // Enforce the explicit native replacement after that presentation tick.
        if (bReplaced)
        {
            if (Target->bRenderInMainPass) Target->SetRenderInMainPass(false);
            if (Target->bRenderInDepthPass) Target->SetRenderInDepthPass(false);
        }
        if (!bSubmitted || SubmittedMesh.Get()!=SourceMeshAsset(Target)) SubmitBindings();
    }
}
