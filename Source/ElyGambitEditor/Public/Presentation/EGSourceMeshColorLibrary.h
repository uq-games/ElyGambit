#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGSourceMeshColorLibrary.generated.h"
class UStaticMesh;

/** Editor repair of source FX attributes and the verified OBJ transport reflection. */
UCLASS()
class ELYGAMBITEDITOR_API UEGSourceMeshColorLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable, Category="Art|Authoring")
 static bool RestoreSourceVertexColors(UStaticMesh* Mesh,const TArray<FVector>& Positions,
  const TArray<FVector2D>& UVs,const TArray<FLinearColor>& Colors,bool bNormalizeTransport,FString& Result);
 UFUNCTION(BlueprintPure, Category="Art|Authoring")
 static TArray<FLinearColor> SourceVertexInstanceColors(UStaticMesh* Mesh);
 UFUNCTION(BlueprintPure, Category="Art|Authoring")
 static TArray<FVector> SourceVertexPositions(UStaticMesh* Mesh);
};
