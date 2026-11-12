#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateBrush.h"

class UImage;
class UTexture2D;

/**
 * ShopMotion20261024 integration of the GPT Image hand-off.
 *
 * Native originals live in SourceArt/UI/ShopMotion20261024 and are imported
 * without pixel changes. Every brush below crops the source's transparent
 * padding with a UV region measured from manifest.json visible_alpha_bbox plus
 * the alpha-mask corner extents, so the drawn silhouette keeps its authored
 * corners, reaches the specified design radius, and never shows an inset
 * transparent gap against a screen edge.
 *
 * Both plates are integrated as a 9-slice: corners scale uniformly (shape and
 * target radius preserved), edges stretch along their straight axis only.
 */
namespace EGShopMotion
{
    inline const TCHAR* const ArtFolder = TEXT("/Game/Assets/UI/Textures/ShopMotion20261024/");

    /** Imported texture names (the .png base names). */
    inline const TCHAR* const ButtonPlateTexture = TEXT("T_UI_SM_ButtonPlate");
    inline const TCHAR* const SellPlateTexture   = TEXT("T_UI_SM_SellPlate");
    inline const TCHAR* const LockClosedTexture  = TEXT("T_UI_SM_LockClosed");
    inline const TCHAR* const LockOpenTexture    = TEXT("T_UI_SM_LockOpen");
    inline const TCHAR* const RefreshTexture      = TEXT("T_UI_SM_Refresh");

    // ---- measured source rectangles ------------------------------------------
    // T_UI_SM_ButtonPlate.png 2002x786; visible plate 12,165 - 1990,621; measured
    // corner ~74 px. Target 112x44 / radius 8.
    inline constexpr float ButtonPlateTexWidth  = 2002.0f;
    inline constexpr float ButtonPlateTexHeight = 786.0f;
    inline constexpr float ButtonPlateU0 = 12.0f,  ButtonPlateV0 = 165.0f;
    inline constexpr float ButtonPlateU1 = 1990.0f, ButtonPlateV1 = 621.0f;
    inline constexpr float ButtonPlateCornerU = 74.0f, ButtonPlateCornerV = 74.0f;

    // T_UI_SM_SellPlate.png 971x1619; visible plate 22,21 - 950,1598; measured
    // corner 58-62 px! A direct stretch would only reach ~12 design pixels, so the
    // 9-slice corners are uniform-scaled to the specified radius 28. Full plate
    // 192x320 is authored; only half of it stays on screen.
    inline constexpr float SellPlateTexWidth  = 971.0f;
    inline constexpr float SellPlateTexHeight = 1619.0f;
    inline constexpr float SellPlateU0 = 22.0f, SellPlateV0 = 21.0f;
    inline constexpr float SellPlateU1 = 950.0f, SellPlateV1 = 1598.0f;
    inline constexpr float SellPlateCornerSource = 62.0f;

    /** 20x20 design glyphs, cropped to the main visible glyph bounds. */
    enum class EGlyph : uint8 { LockClosed, LockOpen, Refresh };

    /** 9-slice ordering: row-major, top-left first. */
    enum class EPlateSlice : uint8
    {
        TopLeft, Top, TopRight, Left, Center, Right, BottomLeft, Bottom, BottomRight
    };

    ELYGAMBIT_API UTexture2D* LoadTexture(const TCHAR* AssetName);
    ELYGAMBIT_API bool AreArtworkAssetsAvailable();

    ELYGAMBIT_API FSlateBrush ButtonPlateBrush(EPlateSlice Slice);
    ELYGAMBIT_API FVector2D ButtonPlateSliceSize(EPlateSlice Slice);
    ELYGAMBIT_API FVector2D ButtonPlateSlicePosition(EPlateSlice Slice);

    ELYGAMBIT_API FSlateBrush SellPlateBrush(EPlateSlice Slice);
    ELYGAMBIT_API FVector2D SellPlateSliceSize(EPlateSlice Slice);
    ELYGAMBIT_API FVector2D SellPlateSlicePosition(EPlateSlice Slice);

    ELYGAMBIT_API FSlateBrush GlyphBrush(EGlyph Glyph);
    ELYGAMBIT_API FVector2D GlyphSize(EGlyph Glyph);
    ELYGAMBIT_API const TCHAR* GlyphTextureName(EGlyph Glyph);
    /** Stable authored image names for the two lock states and refresh glyph. */
    ELYGAMBIT_API const TCHAR* LockClosedImageName();
    ELYGAMBIT_API const TCHAR* LockOpenImageName();
    ELYGAMBIT_API const TCHAR* RefreshImageName();
};
