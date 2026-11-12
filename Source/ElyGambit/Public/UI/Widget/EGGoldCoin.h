#pragma once
#include "CoreMinimal.h"
#include "Components/Image.h"
#include "EGGoldCoin.generated.h"

/** Honkai energy source icon; class name retained for existing Widget Blueprint bindings. */
UCLASS()
class ELYGAMBIT_API UEGGoldCoin : public UImage
{
 GENERATED_BODY()
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
};
