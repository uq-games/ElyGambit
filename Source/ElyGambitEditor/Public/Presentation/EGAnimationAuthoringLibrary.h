#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGAnimationAuthoringLibrary.generated.h"
class USkeletalMesh;
class UAnimSequence;
class UAnimMontage;
class UCurveFloat;
class USkeletalMeshComponent;
USTRUCT(BlueprintType)
struct FEGSourceFloatCurveSamples
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadWrite) FName CurveName;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> Times;
 UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<float> Values;
};
UCLASS()
class ELYGAMBITEDITOR_API UEGAnimationAuthoringLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintPure,Category="Art|Authoring") static TArray<FName> ReferenceBoneNames(USkeletalMesh* Mesh);
 UFUNCTION(BlueprintPure,Category="Art|Authoring") static TArray<FTransform> ReferenceBonePoses(USkeletalMesh* Mesh);
 UFUNCTION(BlueprintPure,Category="Art|Authoring") static TArray<int32> ReferenceBoneParents(USkeletalMesh* Mesh);
 /** Creates a new, unpopulated sequence. The caller must author keys before saving. Never replaces assets. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static UAnimSequence* CreateSourceAnimation(const FString& PackageName, USkeletalMesh* Mesh);
 /** Replace one sampled source curve in one controller operation. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool SetSourceFloatCurve(UAnimSequence* Animation, FName CurveName, const TArray<float>& Times, const TArray<float>& Values);
 /** Authors all curves together, waiting for any worker reading the old model. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool SetSourceFloatCurves(UAnimSequence* Animation, const TArray<FEGSourceFloatCurveSamples>& Curves);
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool SetSourceCurveKeys(UCurveFloat* Curve, const TArray<float>& Times, const TArray<float>& Values, bool bConstant = false);
 /** CPU skinning bound for a posed import fixture, in component space. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static FBox SourceSkinnedBounds(USkeletalMeshComponent* Component, bool bVisibleSectionsOnly = false);
 /** Read-only bounds of one material section; never toggles render visibility. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static FBox SourceSkinnedMaterialBounds(USkeletalMeshComponent* Component, int32 MaterialIndex);
 /** Read-only reference-pose vertices for authoring a weapon sweep from its real blade geometry. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static TArray<FVector> SourceReferenceMaterialVertices(USkeletalMesh* Mesh, int32 MaterialIndex);
 /** Returns None unless every weighted vertex in this material uses one identical bone. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static FName SourceRigidMaterialBone(USkeletalMesh* Mesh, int32 MaterialIndex);
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool SetSourceMeshBounds(USkeletalMesh* Mesh, FBox Bounds);
 /** Fix only the redirectors produced by the approved Vill-V action-folder move. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool FixupVillVActionRedirectors(const TArray<FString>& PackageNames);
 /** Save a fully loaded editor asset after releasing its package reader. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool SaveSourceEditorAsset(UObject* Asset);
 /** Retime uniquely identified skill notifies in-place; validates all other metadata and never saves. */
 UFUNCTION(BlueprintCallable,Category="Art|Authoring") static bool RetimeSkillMontageNotifies(UAnimMontage* Montage,const TArray<FName>& EventNames,float OldTime,float NewTime);
};
