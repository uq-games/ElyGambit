#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"
#include "EGCardPresentation.generated.h"

class UCanvasPanel;
class UTexture2D;
class UWidgetTree;
class UEGHeroDefinition;
class UImage;

/** Per-character Blueprint defaults. Child cards inherit one Designer tree. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGCardAppearance
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Card") FSlateBrush Portrait;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Card") TObjectPtr<UTexture2D> Signet=nullptr;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Card") FText Name;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Card") FVector4 PortraitBounds=FVector4(0,0,1,.82);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Card") FVector4 SignetBounds=FVector4(.04,.04,.23,.23);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Card") FVector4 NameBounds=FVector4(.04,.79,.96,.98);
};

/** Presentation-only composition. The supplied card artwork remains the identity;
 * manager-owned price, progression and transactions never live in this layer. */
struct ELYGAMBIT_API FEGCardPresentation
{
    static FName CharacterKey(UTexture2D* Artwork);
    static FName CharacterKey(const UEGHeroDefinition* Definition);
    static UTexture2D* ArtworkForHero(const UEGHeroDefinition* Definition);
    static UTexture2D* ResolveArtwork(UTexture2D* Artwork);
    static FEGCardAppearance Appearance(UTexture2D* Artwork,bool bShop=false);
    static void ApplyAuthored(UWidgetTree* Tree,const FEGCardAppearance& Appearance);
    static UCanvasPanel* Build(UWidgetTree* Tree, bool bShop = false);
    static void Apply(UWidgetTree* Tree, UTexture2D* OriginalArtwork, bool bShop = false);
    /** Optional frame/name overlay, built only for definitions that opt in. */
    static void ApplyDefinitionLayers(UWidgetTree* Tree,UImage* Image,const UEGHeroDefinition* Definition,int32 Level=0);
    static FSlateBrush PortraitBrush(UTexture2D* OriginalArtwork, FVector2D DisplaySize);
};
