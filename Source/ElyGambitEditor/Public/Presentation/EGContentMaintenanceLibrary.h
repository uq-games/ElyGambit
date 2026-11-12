#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGContentMaintenanceLibrary.generated.h"

class UBlueprint;
class USkeletalMesh;
class UMaterial;
class UMaterialExpression;
class UTexture;

/** Editor-only operations required by the approved asset migration. */
UCLASS()
class ELYGAMBITEDITOR_API UEGContentMaintenanceLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static bool CleanPardoWidget(UBlueprint* Blueprint);
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static bool SetPreviewMesh(UObject* Asset,USkeletalMesh* Mesh);
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static bool RepairReferences(const TArray<UObject*>& Assets,const TMap<FString,FString>& PackagePaths);
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static void AddPackageAliases(const TMap<FString,FString>& PackagePaths);
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static FString MaterialGraphSignature(UMaterial* Material);
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static TArray<UMaterialExpression*> MaterialExpressions(UMaterial* Material);
 UFUNCTION(BlueprintCallable,Category="Editor|Content") static bool RestoreMaterialTextures(UMaterial* Material,const TMap<FString,UTexture*>& Textures);
};
