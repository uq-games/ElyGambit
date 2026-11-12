#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGResourceAuditLibrary.generated.h"

/** Explicit diagnostics only; never loads assets or changes their settings. */
UCLASS()
class ELYGAMBIT_API UEGResourceAuditLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Diagnostics")
 static FString CaptureMemorySample(const FString& Label,bool bWriteTextureInventory=false);
 UFUNCTION(BlueprintPure,Category="Diagnostics") static FString TextureSourceId(class UTexture2D* Texture);
 UFUNCTION(BlueprintCallable,Category="Diagnostics|Editor") static bool PrepareCardCompression(class UTexture2D* Texture);
};
