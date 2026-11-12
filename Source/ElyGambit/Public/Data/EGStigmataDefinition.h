#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EGStigmataDefinition.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGStigmataDefinition
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FName Id;
    UPROPERTY(BlueprintReadOnly) FText Name;
    UPROPERTY(BlueprintReadOnly) FText Category;
    UPROPERTY(BlueprintReadOnly) FText Description;
    UPROPERTY(BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Image;
};

UCLASS()
class ELYGAMBIT_API UEGStigmataCatalog : public UObject
{
    GENERATED_BODY()
public:
    static const TArray<FEGStigmataDefinition>& GetDefinitions();
    static const FEGStigmataDefinition* Find(FName Id);
};
