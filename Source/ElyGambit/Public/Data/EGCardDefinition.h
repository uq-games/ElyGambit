#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EGCardDefinition.generated.h"

class UTexture2D;
class UEGHeroDefinition;
class AEGDeployedPiece;
class AEGPlacementPreview;

/** Shared presentation/deployment configuration. Instances and transactions live in the manager. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGCardDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card") FName CardId;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card") FText DisplayName;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card") TObjectPtr<UTexture2D> Artwork;
	/** Optional link to the existing combat configuration; CardDeployment does not initialize GAS. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card") TObjectPtr<UEGHeroDefinition> HeroDefinition;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card") TSubclassOf<AEGDeployedPiece> PieceClass;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card") TSubclassOf<AEGPlacementPreview> PreviewClass;
	/** Development parameter, not a final economy rule. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Card", meta=(ClampMin="0")) int32 SaleReward = 0;
};
