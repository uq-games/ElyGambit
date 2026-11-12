#pragma once
#include "Components/ActorComponent.h"
#include "EGNativeRenderState.h"
#include "Engine/DataAsset.h"
#include "EGNativeMeshPassComponent.generated.h"

class UMeshComponent;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FEGNativeMeshPass
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UMaterialInterface> Material;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 MaterialSlot=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Queue=2000;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SortingOrder=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 PassIndex=0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FEGSourceNativeRenderState State;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) FEGSourceNativeRenderState AuxiliaryState;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool bWritesAuxiliary=false;
};

/** Private source-pass configuration. It contains rendering data only. */
UCLASS(BlueprintType)
class EGNATIVERENDERER_API UEGNativeMeshPassDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FEGNativeMeshPass> Passes;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bReplaceMainPass=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString SourceDescription;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString Limitations;
};

/** Borrows the existing posed mesh without a second animation/gameplay system. */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent))
class EGNATIVERENDERER_API UEGNativeMeshPassComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UEGNativeMeshPassComponent();
    UFUNCTION(BlueprintCallable, Category="Source rendering")
    bool Configure(UMeshComponent* Mesh, const TArray<FEGNativeMeshPass>& Passes, bool bReplaceMainPass);
    UFUNCTION(BlueprintCallable, Category="Source rendering") void ReleaseBindings();
    UFUNCTION(BlueprintPure,Category="Source rendering") UMeshComponent* GetTarget() const { return Target; }
    bool ApplyDefinition(UMeshComponent* Mesh,UEGNativeMeshPassDefinition* Definition,UObject* LifetimeOwner);
    bool IsBindingOwnedBy(const UObject* Owner) const { return BindingOwner.Get()==Owner && bHasLifetimeOwner; }
    void SetNativeScalarParameter(FName Name,float Value);
    virtual void OnUnregister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
private:
    UPROPERTY(Transient) TObjectPtr<UMeshComponent> Target;
    UPROPERTY(Transient) TArray<FEGNativeMeshPass> NativePasses;
    UPROPERTY(Transient) TObjectPtr<UEGNativeMeshPassDefinition> BoundDefinition;
    UPROPERTY(Transient) TWeakObjectPtr<UObject> BindingOwner;
    bool bHasLifetimeOwner=false;
    TWeakObjectPtr<UObject> SubmittedMesh;
    bool bReplaced=false, bOriginalMainPass=true, bOriginalDepthPass=true;
    uint64 RegistryKey=0;
    bool bSubmitted=false;
    void SubmitBindings();
};
