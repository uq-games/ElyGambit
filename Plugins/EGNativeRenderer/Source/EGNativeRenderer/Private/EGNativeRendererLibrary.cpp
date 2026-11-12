#include "EGNativeRendererLibrary.h"
#include "EGNativeRendererInternal.h"
#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "Shader.h"
#include "RenderingThread.h"
#include "VertexFactory.h"
#include "Components/MeshComponent.h"
#include "GameFramework/Actor.h"

UEGNativeMeshPassComponent* UEGNativeRendererLibrary::ApplySourceDefinition(UMeshComponent* Target,
    UEGNativeMeshPassDefinition* Definition,UObject* LifetimeOwner)
{
    if (!IsValid(Target) || !Target->GetOwner() || !IsValid(Definition) || !IsValid(LifetimeOwner)) return nullptr;
    AActor* Owner=Target->GetOwner();TInlineComponentArray<UEGNativeMeshPassComponent*> Existing(Owner);
    UEGNativeMeshPassComponent* Selected=nullptr;
    for (auto* Component:Existing)
    {
        // A PIE/transaction duplicate can retain an instance attachment while
        // its transient target/owner fields have correctly been reset. It must
        // not accumulate as an unbound component on every preview rebuild.
        if (!IsValid(Component->GetTarget())) { Component->DestroyComponent();continue; }
        if (Component->GetTarget()==Target) Selected=Component;
    }
    if (Selected) return Selected->ApplyDefinition(Target,Definition,LifetimeOwner) ? Selected : nullptr;
    auto* Component=NewObject<UEGNativeMeshPassComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
    Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    if (!Component->ApplyDefinition(Target,Definition,LifetimeOwner)) { Component->DestroyComponent();return nullptr; }
    return Component;
}
void UEGNativeRendererLibrary::ReleaseSourceDefinition(UMeshComponent* Target,UObject* LifetimeOwner)
{
    if (!IsValid(Target) || !Target->GetOwner()) return;
    TInlineComponentArray<UEGNativeMeshPassComponent*> Existing(Target->GetOwner());
    for (auto* Component:Existing)
        if (Component->GetTarget()==Target && Component->IsBindingOwnedBy(LifetimeOwner)) Component->DestroyComponent();
}

UEGNativeMeshPassComponent* UEGNativeRendererLibrary::AttachSourcePasses(UMeshComponent* Target,
    const TArray<FEGNativeMeshPass>& Passes,bool bReplaceMainPass)
{
    if (!IsValid(Target) || !Target->GetOwner()) return nullptr;
    AActor* Owner=Target->GetOwner();
    TInlineComponentArray<UEGNativeMeshPassComponent*> Existing(Owner);
    UEGNativeMeshPassComponent* Selected=nullptr;
    for (auto* Component:Existing)
    {
        if (!IsValid(Component->GetTarget())) { Component->DestroyComponent();continue; }
        if (Component->GetTarget()==Target) Selected=Component;
    }
    if (Selected) return Selected->Configure(Target,Passes,bReplaceMainPass) ? Selected : nullptr;
    auto* Component=NewObject<UEGNativeMeshPassComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
    Owner->AddInstanceComponent(Component);Component->RegisterComponent();
    if (!Component->Configure(Target,Passes,bReplaceMainPass)) { Component->DestroyComponent();return nullptr; }
    return Component;
}

FString UEGNativeRendererLibrary::GetRenderDiagnostics()
{
    return FString::Printf(TEXT("{\"frames\":%llu,\"submitted_draws\":%llu,\"missing_shaders\":%llu,\"registrations\":%llu,\"depth_commits\":%llu}"),
        GEGNativeFrames.load(),GEGNativeDraws.load(),GEGNativeMissingShaders.load(),GEGNativeRegistrationCount.load(),GEGNativeDepthCommits.load());
}
TArray<FString> UEGNativeRendererLibrary::GetNativeMaterialShaderTypes(UMaterialInterface* Material)
{
    TArray<FString> Out;
    if (!IsValid(Material)) return Out;
    const FMaterialResource* Resource=Material->GetMaterialResource(GMaxRHIShaderPlatform);
    const FMaterialShaderMap* Map=Resource ? Resource->GetGameThreadShaderMap() : nullptr;
    if (!Map) return Out;
    TMap<FShaderId,TShaderRef<FShader>> Shaders;
    Map->GetShaderList(Shaders);
    for (const auto& Pair:Shaders)
    {
        const FString Name=Pair.Value.GetType()->GetName();
        if (Name.StartsWith(TEXT("FEGNative")))
        {
            const FString Factory=Pair.Value.GetVertexFactoryType() ? Pair.Value.GetVertexFactoryType()->GetName() : TEXT("none");
            Out.AddUnique(Name+TEXT(":")+Factory);
        }
    }
    Out.Sort();return Out;
}
