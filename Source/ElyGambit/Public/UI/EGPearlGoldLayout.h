#pragma once

#include "CoreMinimal.h"

/**
 * Pearl-Gold (B) design system for the board UI.
 *
 * All values are expressed in the 1920x1080 design space used by the art
 * hand-off (PearlGold_Implementation_Handoff.md). UMG scales by its normal DPI
 * rules; nothing here applies a second scale. Keep gameplay/state logic out of
 * this header: it only owns tokens and measured rectangles.
 */
namespace EGPearlGold
{
    // ---------------------------------------------------------------- palette
    // Generated art is authored in sRGB; convert explicitly so "deep plum" text is
    // actually deep (RGB 66/43/66), not the much lighter linear value it would be if
    // the numbers were used directly as FLinearColor.
    inline FLinearColor Srgb(int32 R, int32 G, int32 B, float A = 1.0f)
    {
        FLinearColor C = FLinearColor::FromSRGBColor(FColor(R, G, B, 255));
        C.A = A;
        return C;
    }

    inline const FLinearColor Pearl        = Srgb(231, 226, 247, .30f);
    inline const FLinearColor Ivory        = Srgb(44, 35, 68, .48f);
    inline const FLinearColor Parchment    = Srgb(52, 42, 77, .48f);
    inline const FLinearColor Champagne    = Srgb(233, 195, 241);
    inline const FLinearColor ChampagneDeep= Srgb(206, 193, 233);
    inline const FLinearColor GoldEdge     = Srgb(225, 218, 250, .48f);
    inline const FLinearColor Plum         = Srgb(252, 245, 255);
    inline const FLinearColor PlumSoft     = Srgb(220, 211, 234);
    inline const FLinearColor LilacGlow    = FLinearColor(0.780f, 0.690f, 0.870f, 0.35f);
    inline const FLinearColor Danger       = Srgb(179, 62, 74);
    inline const FLinearColor Shadow       = FLinearColor(0.243f, 0.145f, 0.267f, 0.22f);
    inline const FLinearColor Transparent  = FLinearColor(0.0f, 0.0f, 0.0f, 0.0f);

    // ----------------------------------------------------------- design space
    inline constexpr float DesignWidth  = 1920.0f;
    inline constexpr float DesignHeight = 1080.0f;
    inline constexpr float SafeMargin   = 32.0f;

    struct FLayoutRect
    {
        float X = 0.0f;
        float Y = 0.0f;
        float W = 0.0f;
        float H = 0.0f;

        FVector2D Position() const { return FVector2D(X, Y); }
        FVector2D Size() const { return FVector2D(W, H); }
        FVector2D Center() const { return FVector2D(X + W * 0.5f, Y + H * 0.5f); }
        bool Contains(const FVector2D& P) const
        {
            return P.X >= X && P.X <= X + W && P.Y >= Y && P.Y <= Y + H;
        }
    };

    // ------------------------------------------------------------- top banner
    // Normal round state and the in-place shop state share the same anchor so
    // opening the shop replaces the title instead of stacking a second one.
    inline const FLayoutRect BannerNormal{790.0f, 16.0f, 340.0f, 44.0f};
    inline const FLayoutRect BannerShop  {790.0f, 16.0f, 340.0f, 44.0f};

    inline constexpr float BannerTitleSize      = 22.0f;
    inline constexpr float BannerSecondarySize  = 18.0f;
    inline constexpr float BannerTitleTop       = 7.0f;
    inline constexpr float BannerSecondaryTop   = 48.0f;

    // -------------------------------------------------------- top-right pills
    inline const FLayoutRect StatisticsButton{1680.0f, 24.0f, 98.0f, 40.0f};
    inline constexpr float EntryPillHeight = 40.0f;
    inline constexpr float EntryPillGap    = 12.0f;
    inline constexpr float OwnedBadgeSize  = 40.0f;
    inline constexpr float OwnedBadgeGap   = 6.0f;
    inline constexpr float OwnedBadgeTop   = 72.0f;
    inline constexpr float OwnedBadgeRight = 32.0f;
    inline constexpr int32 OwnedBadgeCount = 3;

    // -------------------------------------------------------------- shop row
    inline constexpr float ShopCardWidth  = 270.0f;
    inline constexpr float ShopCardHeight = 378.0f;
    inline constexpr float ShopCardGap    = 24.0f;
    inline constexpr float ShopRowLeft    = 350.0f;
    inline constexpr float ShopRowTop     = 82.0f;
    inline constexpr float ShopPanelHeight = 520.0f;
    inline constexpr float ShopTransitionSeconds = .18f;
    inline const FLayoutRect ShopCard0    {350.0f, 82.0f, 270.0f, 378.0f};
    // Shared plate size for both shop controls (ShopMotion20261024).
    inline const FLayoutRect ShopLock     {1536.0f, 96.0f, 112.0f, 44.0f};
    inline const FLayoutRect ShopRefresh  {1536.0f, 384.0f, 112.0f, 44.0f};

    // ------------------------------------------------ shop motion 2026-10-24
    // The open/close channel owns only the panel translation and alpha; the
    // compact channel owns card/control layout and the gradient. They are
    // composed once, in this 1920x1080 design space, never through DPI twice.
    inline constexpr float ShopOpenSeconds    = .18f;
    inline constexpr float ShopCloseSeconds   = .16f;
    inline constexpr float ShopOpenOffsetY    = -48.0f;
    inline constexpr float ShopCompactSeconds = .15f;
    inline constexpr float ShopCompactScale   = .45f;
    inline constexpr float ShopCompactTop     = 24.0f;
    inline constexpr float ShopCompactMargin  = 24.0f;
    inline constexpr float ShopCompactButtonWidth  = 112.0f;
    inline constexpr float ShopCompactButtonHeight = 44.0f;
    inline constexpr float ShopCompactButtonGap    = 16.0f;
    inline constexpr float ShopCompactHintGap      = 6.0f;
    inline constexpr float ShopCompactHintHeight   = 24.0f;
    inline constexpr float ShopCompactHintPadding  = 24.0f;
    inline constexpr float ShopPlateFontSize  = 16.0f;
    inline constexpr float ShopPlateGlyphSize = 20.0f;
    inline constexpr float ShopPlateGlyphGap  = 4.0f;
    /** Corner radius of the shared control plate, preserved by the 9-slice. */
    inline constexpr float ShopPlateCorner    = 8.0f;
    // Slate clamps vertex tint above 1, so feedback is authored as a legal ramp:
    // normal <1, hover 1 (maximum legal), pressed/disabled darker.
    inline constexpr float ShopPlateTintBase     = .86f;
    inline constexpr float ShopPlateTintHover    = 1.00f;
    inline constexpr float ShopPlateTintPressed  = .72f;
    inline constexpr float ShopPlateTintDisabled = .62f;
    inline constexpr float SellPlateTintBase     = .88f;
    inline constexpr float SellPlateTintHover    = 1.00f;

    // Half-clipped sale plate: the full 192x320 shape is authored, only the
    // 96-unit half that stays on screen is ever visible or hoverable.
    inline constexpr float SellPlateWidth        = 192.0f;
    inline constexpr float SellPlateHeight       = 320.0f;
    inline constexpr float SellPlateRadius       = 28.0f;
    inline constexpr float SellPlateVisibleWidth = 96.0f;
    inline constexpr float SellPlateFontSize     = 16.0f;
    inline constexpr float SellLabelWidth        = 96.0f;
    inline constexpr float SellLabelHeight       = 72.0f;
    inline constexpr float SellLabelPadding      = 8.0f;

    /** Measured compact layout shared by rendering, the gradient and return hits. */
    struct FShopCompactLayout
    {
        float Fit = 1.0f;
        float CardScale = ShopCompactScale;
        float CardWidth = 0.0f;
        float CardHeight = 0.0f;
        float CardGap = 0.0f;
        float ButtonWidth = 0.0f;
        float ButtonHeight = 0.0f;
        float ButtonGap = 0.0f;
        float GroupWidth = 0.0f;
        float GroupLeft = 0.0f;
        float GroupRight = 0.0f;
        float LockLeft = 0.0f;
        float CardRowLeft = 0.0f;
        float RefreshLeft = 0.0f;
        float CardTop = ShopCompactTop;
        float ButtonTop = 0.0f;
        float GroupBottom = 0.0f;
        float HintTop = 0.0f;
    };

    /**
     * Centers the whole compact group (lock plate, four cards, refresh plate)
     * using its measured total width. Only a viewport narrower than the group
     * plus both screen margins scales the group down, and it does so uniformly.
     */
    inline FShopCompactLayout ShopCompactLayout(float ViewWidth)
    {
        FShopCompactLayout L;
        const float Natural = 4.0f * ShopCardWidth * ShopCompactScale +
            3.0f * ShopCardGap * ShopCompactScale +
            2.0f * ShopCompactButtonWidth + 2.0f * ShopCompactButtonGap;
        const float Available = FMath::Max(1.0f, ViewWidth - 2.0f * ShopCompactMargin);
        L.Fit = FMath::Min(1.0f, Available / FMath::Max(1.0f, Natural));
        L.CardScale = ShopCompactScale * L.Fit;
        L.CardWidth = ShopCardWidth * L.CardScale;
        L.CardHeight = ShopCardHeight * L.CardScale;
        L.CardGap = ShopCardGap * L.CardScale;
        L.ButtonWidth = ShopCompactButtonWidth * L.Fit;
        L.ButtonHeight = ShopCompactButtonHeight * L.Fit;
        L.ButtonGap = ShopCompactButtonGap * L.Fit;
        L.GroupWidth = 4.0f * L.CardWidth + 3.0f * L.CardGap + 2.0f * L.ButtonWidth + 2.0f * L.ButtonGap;
        L.GroupLeft = FMath::Max(ShopCompactMargin, (ViewWidth - L.GroupWidth) * .5f);
        L.GroupRight = L.GroupLeft + L.GroupWidth;
        L.LockLeft = L.GroupLeft;
        L.CardRowLeft = L.GroupLeft + L.ButtonWidth + L.ButtonGap;
        L.RefreshLeft = L.GroupRight - L.ButtonWidth;
        L.CardTop = ShopCompactTop;
        L.ButtonTop = L.CardTop + (L.CardHeight - L.ButtonHeight) * .5f;
        L.GroupBottom = L.CardTop + L.CardHeight;
        L.HintTop = L.GroupBottom + ShopCompactHintGap;
        return L;
    }

    inline FLayoutRect LerpRect(const FLayoutRect& A, const FLayoutRect& B, float T)
    {
        return FLayoutRect{FMath::Lerp(A.X, B.X, T), FMath::Lerp(A.Y, B.Y, T),
                           FMath::Lerp(A.W, B.W, T), FMath::Lerp(A.H, B.H, T)};
    }

    inline FLayoutRect ShopCardRect(int32 Index)
    {
        return FLayoutRect{ShopRowLeft + Index * (ShopCardWidth + ShopCardGap), ShopRowTop, ShopCardWidth, ShopCardHeight};
    }

    // ------------------------------------------------------------------ hand
    inline constexpr float HandCardWidth  = 140.0f;
    inline constexpr float HandCardHeight = 196.0f;
    inline constexpr float HandCompactHeight = 132.0f;
    inline constexpr float HandPeekHeight = HandCardHeight;
    inline constexpr float HandBelowScreen = HandCardHeight-HandPeekHeight;
    inline constexpr float HandCardGap    = 2.0f;
    // Space for the 15% hover scale (including the acquisition overshoot).
    inline constexpr float HandEdgePadding = 14.0f;
    inline constexpr float HandTopPadding = 0.0f;
    inline constexpr float HandMaxWidth   = 10.0f * HandCardWidth + 9.0f * HandCardGap + 2.0f * HandEdgePadding;
    inline constexpr float HandBottom     = 6.0f;
    inline constexpr float HandPanelHeight = HandCardHeight + HandTopPadding;

    inline float HandWidthForCount(int32 Count)
    {
        if (Count <= 0) return 0.0f;
        // Horizontal padding is part of each card's scrolling footprint.
        const float Natural = Count * (HandCardWidth + HandCardGap) + 2.0f * (HandEdgePadding - HandCardGap * .5f);
        return FMath::Min(HandMaxWidth, Natural);
    }

    // --------------------------------------------------------- bottom-left
    inline const FLayoutRect Avatar{32.0f, 944.0f, 104.0f, 104.0f};
    inline constexpr float AvatarDecor = 16.0f;

    // -------------------------------------------------------- bottom-right
    inline const FLayoutRect Orb       {1760.0f, 924.0f, 124.0f, 124.0f};
    inline const FLayoutRect Battle    {1722.0f, 834.0f, 168.0f, 56.0f};
    inline const FLayoutRect Population{1750.0f, 798.0f, 124.0f, 32.0f};
    inline const FLayoutRect Income    {1784.0f, 896.0f, 72.0f, 24.0f};
    inline constexpr float OrbSize        = 124.0f;
    inline constexpr float BattleWidth    = 168.0f;
    inline constexpr float BattleHeight   = 56.0f;
    inline constexpr float IncomeTextSize = 18.0f;

    // ------------------------------------------------------------- typography
    inline constexpr float FontBody      = 20.0f;
    inline constexpr float FontSecondary = 18.0f;
    inline constexpr float FontCardName  = 18.0f;
    inline constexpr float FontTitle     = 28.0f;
    inline constexpr float FontGold      = 32.0f;

    // ---------------------------------------------------------------- shapes
    inline constexpr float RadiusPill  = 28.0f;
    inline constexpr float RadiusPanel = 10.0f;
    inline constexpr float RadiusCard  = 8.0f;
    inline constexpr float Hairline    = 1.5f;

    // ------------------------------------------------------- hover/detail
    inline constexpr float HoverMinWidth  = 280.0f;
    inline constexpr float HoverMaxWidth  = 340.0f;
    inline constexpr float DetailWidth    = 340.0f;
}
