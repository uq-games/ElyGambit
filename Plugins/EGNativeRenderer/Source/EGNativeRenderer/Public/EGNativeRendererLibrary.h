#pragma once
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGNativeMeshPassComponent.h"
#include "EGNativeRendererLibrary.generated.h"
class UMaterialInterface;
UCLASS()
class EGNATIVERENDERER_API UEGNativeRendererLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="Source rendering") static FString GetRenderDiagnostics();
    UFUNCTION(BlueprintCallable,Category="Source rendering") static TArray<FString> GetNativeMaterialShaderTypes(UMaterialInterface* Material);
    UFUNCTION(BlueprintCallable,Category="Source rendering") static UEGNativeMeshPassComponent* AttachSourcePasses(
        UMeshComponent* Target,const TArray<FEGNativeMeshPass>& Passes,bool bReplaceMainPass);
    UFUNCTION(BlueprintCallable,Category="Source rendering") static UEGNativeMeshPassComponent* ApplySourceDefinition(
        UMeshComponent* Target,UEGNativeMeshPassDefinition* Definition,UObject* LifetimeOwner);
    UFUNCTION(BlueprintCallable,Category="Source rendering") static void ReleaseSourceDefinition(UMeshComponent* Target,UObject* LifetimeOwner);
};
