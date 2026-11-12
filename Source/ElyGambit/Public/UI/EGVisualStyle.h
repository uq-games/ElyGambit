#pragma once
#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Styling/SlateBrush.h"

class UTexture2D;
class UFont;

/**
 * Shared BoardTheme-style visual materials. Owns no gameplay or widget state.
 * Textures live under /Game/Assets/UI/Textures/BoardTheme (imported by Scripts/BoardTheme/import_textures.py).
 */
struct ELYGAMBIT_API FEGVisualStyle
{
    // Palette tokens (wxq_design_spec 2.1)
    static const FLinearColor Ink;
    static const FLinearColor Navy;
    static const FLinearColor GoldHi;
    static const FLinearColor Gold;
    static const FLinearColor GoldDeep;
    static const FLinearColor Paper;
    static const FLinearColor TextPrimary;
    static const FLinearColor TextSecondary;
    static const FLinearColor Cyan;
    static const FLinearColor Danger;

    static UFont* UiFont();
    static FSlateFontInfo Font(int32 Size, bool bOutline=false);
    static FSlateFontInfo TitleFont(int32 Size);
    static FSlateFontInfo NumberFont(int32 Size);

    // ---- Typography roles (2026-11-06 UI overhaul) -----------------------------
    // Four SIL OFL 1.1 faces imported as runtime UFonts under /Game/Assets/UI/Fonts;
    // sources, licenses and hashes live in SourceArt/UI/Fonts20261106/README.md.
    // Every accessor falls back to F_UI_CJK when its target asset cannot be loaded,
    // so callers never get a null font. The legacy helpers above map onto these:
    //   Font()=BodyFont, TitleFont()=SerifFont(outline), PearlFont()=BodyFont,
    //   NumberFont()=NumeralFont(outline).
    static UFont* BodyFontAsset();
    static UFont* SerifFontAsset();
    static UFont* NarrationFontAsset();
    static UFont* NumeralFontAsset();
    static FSlateFontInfo BodyFont(int32 Size, bool bOutline=false);
    static FSlateFontInfo SerifFont(int32 Size, bool bOutline=false);
    static FSlateFontInfo NarrationFont(int32 Size, bool bOutline=false);
    static FSlateFontInfo NumeralFont(int32 Size, bool bOutline=false);
    /** Champagne colour for RichText number emphasis (EGNumberHighlightDecorator). */
    static const FLinearColor NumberHighlight;

    static UTexture2D* Texture(const TCHAR* Name);
    /** Box (9-slice) brush; Margin is normalized (0..1) texture fractions. */
    static FSlateBrush Box(const TCHAR* TextureName, FMargin Margin, FVector2D SourceSize, FLinearColor Tint=FLinearColor::White);
    static FSlateBrush Image(const TCHAR* TextureName, FVector2D SourceSize, FLinearColor Tint=FLinearColor::White);

    static FSlateBrush Panel(bool bPortrait=true);
    static FSlateBrush Tray();
    static FSlateBrush Banner();
    static FSlateBrush CardFrame();
    static FSlateBrush SoftPanel(FLinearColor Tint);
    static FSlateBrush GoldLine();
    static FSlateBrush Divider();
    static FSlateBrush CloseIcon();
    static FSlateBrush Glow();

    static FButtonStyle Action();   // dark navy capsule
    static FButtonStyle Light();    // silver/white capsule
    static FButtonStyle GoldCta();  // champagne gold primary CTA
    static FButtonStyle Card();     // large card-frame button (rewards / pack choice)
    static FButtonStyle Ghost();    // transparent hit area
    static FProgressBarStyle Progress();

    // ---- Pearl-Gold (B) tokens -------------------------------------------------
    // These are intentionally asset-independent until the generated PearlGold
    // nine-slice set is imported; callers only depend on these helpers.
    static FSlateFontInfo PearlFont(int32 Size,bool bTitle=false);
    static FSlateBrush Rounded(FLinearColor Fill,float Radius,
        FLinearColor Outline=FLinearColor(0.f,0.f,0.f,0.f),float Thickness=0.f);
    static FSlateBrush PearlBanner();
    static FSlateBrush PearlPill();
    static FSlateBrush PearlPanel();
    static FSlateBrush PearlCardFrame();
    static FSlateBrush GoldOutlinePill();
    static FSlateBrush PearlOrb();
    static FSlateBrush PearlAvatarRing();
    static FSlateBrush PearlTray();
    static FButtonStyle PearlButton();
    static FButtonStyle GoldOutlineButton();
    static FButtonStyle OrbButton();
};
