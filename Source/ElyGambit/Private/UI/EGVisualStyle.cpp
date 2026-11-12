#include "UI/EGVisualStyle.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "UI/EGPearlGoldLayout.h"

const FLinearColor FEGVisualStyle::Ink(0.055f,0.065f,0.095f,1.f);
const FLinearColor FEGVisualStyle::Navy(0.020f,0.030f,0.052f,1.f);
const FLinearColor FEGVisualStyle::GoldHi(0.910f,0.874f,0.812f,1.f);
const FLinearColor FEGVisualStyle::Gold(0.784f,0.698f,0.549f,1.f);
const FLinearColor FEGVisualStyle::GoldDeep(0.541f,0.420f,0.208f,1.f);
const FLinearColor FEGVisualStyle::Paper(0.965f,0.949f,0.910f,1.f);
const FLinearColor FEGVisualStyle::TextPrimary(0.973f,0.961f,0.937f,1.f);
const FLinearColor FEGVisualStyle::TextSecondary(0.702f,0.725f,0.741f,1.f);
const FLinearColor FEGVisualStyle::Cyan(0.282f,0.718f,0.937f,1.f);
const FLinearColor FEGVisualStyle::Danger(0.824f,0.243f,0.243f,1.f);
// Champagne, reads as gold on the dark panels without competing with gold CTAs.
const FLinearColor FEGVisualStyle::NumberHighlight(0.94f,0.84f,0.63f,1.f);

namespace
{
    const TCHAR* const TextureRoot = TEXT("/Game/Assets/UI/Textures/BoardTheme/");
    // A process-wide cache is not a GC owner. Live widget brushes retain textures they use.
    TMap<FString,TWeakObjectPtr<UTexture2D>> GTextureCache;

    UTexture2D* FindTexture(const TCHAR* Name)
    {
        if(!Name||!*Name)return nullptr;
        const FString Key(Name);
        if(const auto* Found=GTextureCache.Find(Key);Found&&Found->IsValid())return Found->Get();
        const FString Path=Key.Contains(TEXT("/"))?Key:FString(TextureRoot)+TEXT("Chrome/")+Key;
        auto* Texture=LoadObject<UTexture2D>(nullptr,*Path);
        GTextureCache.Add(Key,Texture);
        return Texture;
    }

    const TCHAR* const UiFontAssetPath    = TEXT("/Game/Assets/UI/Fonts/F_UI_CJK");
    const TCHAR* const BodyFontAssetPath  = TEXT("/Game/Assets/UI/Fonts/F_EG_NotoSansSC");
    const TCHAR* const SerifFontAssetPath = TEXT("/Game/Assets/UI/Fonts/F_EG_NotoSerifSC");
    const TCHAR* const NarrationFontAssetPath = TEXT("/Game/Assets/UI/Fonts/F_EG_LXGWWenKai");
    const TCHAR* const NumeralFontAssetPath   = TEXT("/Game/Assets/UI/Fonts/F_EG_Cinzel");

    // Strong pointers pin the role faces so the style layer never reloads them between widgets.
    UFont* LoadCachedFont(const TCHAR* AssetPath,TStrongObjectPtr<UFont>& Cache)
    {
        if(!Cache.IsValid())Cache.Reset(LoadObject<UFont>(nullptr,AssetPath));
        return Cache.Get();
    }

    UFont* RoleFont(const TCHAR* AssetPath,TStrongObjectPtr<UFont>& Cache)
    {
        if(UFont* Font=LoadCachedFont(AssetPath,Cache))return Font;
        // Contract: a missing role asset falls back to F_UI_CJK rather than a null font.
        return FEGVisualStyle::UiFont();
    }

    /**
     * Address the default typeface's first entry instead of hardcoding a face name:
     * UFontFileImportFactory names the imported entry "Default" while legacy font assets
     * may use another name. An empty name still resolves through Slate's primary entry.
     */
    FName TypefaceNameFor(const UFont* Font)
    {
        if(!Font)return NAME_None;
        const FCompositeFont* Composite=Font->GetCompositeFont();
        if(!Composite||Composite->DefaultTypeface.Fonts.Num()==0)return NAME_None;
        return Composite->DefaultTypeface.Fonts[0].Name;
    }

    FSlateFontInfo MakeFont(UFont* Font,int32 Size,bool bOutline)
    {
        // Last-resort fallback is the engine default face; documented as "no empty font".
        if(!Font)return FCoreStyle::GetDefaultFontStyle(TEXT("Regular"),static_cast<float>(Size));
        FSlateFontInfo Info(Font,Size,TypefaceNameFor(Font));
        if(bOutline)
        {
            Info.OutlineSettings.OutlineSize=2;
            Info.OutlineSettings.OutlineColor=FLinearColor(0.055f,0.035f,0.015f,0.95f);
        }
        return Info;
    }

    void ApplyDefaultOutline(FSlateFontInfo& Info)
    {
        Info.OutlineSettings.OutlineSize=2;
        Info.OutlineSettings.OutlineColor=FLinearColor(0.055f,0.035f,0.015f,0.95f);
    }

    /**
     * Numerals (Cinzel) carry no CJK glyphs. Build the numeral composite with the Body
     * CJK typeface as a sub-font for Chinese ranges so labels that mix Chinese and
     * numbers ("金币 120") never render missing-glyph boxes, even when the text is
     * assigned after the font was resolved.
     */
    TSharedPtr<const FCompositeFont> NumeralComposite()
    {
        static TSharedPtr<const FCompositeFont> Cached;
        if(Cached.IsValid())return Cached;
        UFont* NumeralFont=FEGVisualStyle::NumeralFontAsset();
        UFont* CjkFont=FEGVisualStyle::BodyFontAsset();
        if(!NumeralFont||!CjkFont)return nullptr;
        const FCompositeFont* NumeralFontData=NumeralFont->GetCompositeFont();
        const FCompositeFont* CjkFontData=CjkFont->GetCompositeFont();
        if(!NumeralFontData||!CjkFontData||NumeralFontData->DefaultTypeface.Fonts.Num()==0)return nullptr;
        const TSharedRef<FCompositeFont> Composite=MakeShared<FCompositeFont>();
        Composite->DefaultTypeface=NumeralFontData->DefaultTypeface;
        FCompositeSubFont& SubFont=Composite->SubTypefaces.AddDefaulted_GetRef();
        SubFont.Typeface=CjkFontData->DefaultTypeface;
        SubFont.ScalingFactor=1.f;
        static const FInt32Range CjkRanges[]={
            FInt32Range(0x2E80,0x9FFF),FInt32Range(0xF900,0xFAFF),FInt32Range(0xFE30,0xFE4F),
            FInt32Range(0xFF00,0xFFEF),FInt32Range(0x3000,0x303F),FInt32Range(0x20000,0x2FA1F)};
        for(const FInt32Range& Range:CjkRanges)SubFont.CharacterRanges.Add(Range);
        Cached=Composite;
        return Cached;
    }
}

UFont* FEGVisualStyle::UiFont()
{
    static TStrongObjectPtr<UFont> Cached;
    return LoadCachedFont(UiFontAssetPath,Cached);
}

UFont* FEGVisualStyle::BodyFontAsset()
{
    static TStrongObjectPtr<UFont> Cached;
    return RoleFont(BodyFontAssetPath,Cached);
}

UFont* FEGVisualStyle::SerifFontAsset()
{
    static TStrongObjectPtr<UFont> Cached;
    return RoleFont(SerifFontAssetPath,Cached);
}

UFont* FEGVisualStyle::NarrationFontAsset()
{
    static TStrongObjectPtr<UFont> Cached;
    return RoleFont(NarrationFontAssetPath,Cached);
}

UFont* FEGVisualStyle::NumeralFontAsset()
{
    static TStrongObjectPtr<UFont> Cached;
    return RoleFont(NumeralFontAssetPath,Cached);
}

FSlateFontInfo FEGVisualStyle::Font(int32 Size,bool bOutline){return BodyFont(Size,bOutline);}
FSlateFontInfo FEGVisualStyle::TitleFont(int32 Size){return SerifFont(Size,true);}
FSlateFontInfo FEGVisualStyle::NumberFont(int32 Size){return NumeralFont(Size,true);}
FSlateFontInfo FEGVisualStyle::BodyFont(int32 Size,bool bOutline){return MakeFont(BodyFontAsset(),Size,bOutline);}
FSlateFontInfo FEGVisualStyle::SerifFont(int32 Size,bool bOutline){return MakeFont(SerifFontAsset(),Size,bOutline);}
FSlateFontInfo FEGVisualStyle::NarrationFont(int32 Size,bool bOutline){return MakeFont(NarrationFontAsset(),Size,bOutline);}
FSlateFontInfo FEGVisualStyle::NumeralFont(int32 Size,bool bOutline)
{
    // Cinzel + CJK sub-font: digits stay inscriptional, Chinese in mixed labels stays legible.
    if(TSharedPtr<const FCompositeFont> Composite=NumeralComposite())
    {
        FSlateFontInfo Info(Composite,Size,NAME_None);
        if(bOutline)ApplyDefaultOutline(Info);
        return Info;
    }
    return MakeFont(NumeralFontAsset(),Size,bOutline);
}

UTexture2D* FEGVisualStyle::Texture(const TCHAR* Name){return FindTexture(Name);}

FSlateBrush FEGVisualStyle::Box(const TCHAR* TextureName,FMargin Margin,FVector2D SourceSize,FLinearColor Tint)
{
    FSlateBrush B;
    B.SetResourceObject(FindTexture(TextureName));
    B.DrawAs=ESlateBrushDrawType::Box;
    B.Margin=Margin;
    B.ImageSize=SourceSize;
    B.TintColor=Tint;
    return B;
}

FSlateBrush FEGVisualStyle::Image(const TCHAR* TextureName,FVector2D SourceSize,FLinearColor Tint)
{
    FSlateBrush B;
    B.SetResourceObject(FindTexture(TextureName));
    B.DrawAs=ESlateBrushDrawType::Image;
    B.ImageSize=SourceSize;
    B.TintColor=Tint;
    return B;
}

FSlateBrush FEGVisualStyle::Panel(bool bPortrait)
{
    FSlateBrush B=Box(TEXT("T_UI_Board_Panel_Dark"),FMargin(48.f/512.f),FVector2D(512,512));
    if(bPortrait)B.Margin=FMargin(48.f/512.f);
    return B;
}

FSlateBrush FEGVisualStyle::Tray()
{
    return Box(TEXT("T_UI_Board_HandTray"),FMargin(64.f/1024.f,0.f,64.f/1024.f,0.f),FVector2D(1024,256));
}

FSlateBrush FEGVisualStyle::Banner()
{
    return Box(TEXT("T_UI_Board_RoundBanner"),FMargin(64.f/640.f,32.f/128.f),FVector2D(640,128));
}

FSlateBrush FEGVisualStyle::CardFrame()
{
    // card_frame_03 is a thin blue-white frame; tinted to champagne for our quality tier.
    return Box(TEXT("/Game/Assets/UI/Textures/BoardTheme/Cards/card_frame_03"),FMargin(24.f/326.f),FVector2D(326,408),FLinearColor(0.93f,0.86f,0.72f,1.f));
}

FSlateBrush FEGVisualStyle::SoftPanel(FLinearColor Tint)
{
    return Box(TEXT("T_UI_Board_Panel_Soft"),FMargin(56.f/256.f),FVector2D(256,256),Tint);
}

FSlateBrush FEGVisualStyle::GoldLine()
{
    return Box(TEXT("T_UI_Board_GoldLine"),FMargin(8.f/256.f,0.f,8.f/256.f,0.f),FVector2D(256,16));
}

FSlateBrush FEGVisualStyle::Divider()
{
    return Image(TEXT("/Game/Assets/UI/Textures/BoardTheme/Ornaments/ornament_divider_103"),FVector2D(103,28));
}

FSlateBrush FEGVisualStyle::CloseIcon()
{
    return Image(TEXT("/Game/Assets/UI/Textures/BoardTheme/Other/close_button_wide_270"),FVector2D(270,150));
}

FSlateBrush FEGVisualStyle::Glow()
{
    return Image(TEXT("/Game/Assets/UI/Textures/BoardTheme/Effects/fx_glow_star_256"),FVector2D(256,256));
}

FButtonStyle FEGVisualStyle::Action()
{
    FButtonStyle S;
    FSlateBrush Normal=Box(TEXT("/Game/Assets/UI/Textures/BoardTheme/Buttons/btn_dark_295"),FMargin(54.f/295.f,0.f,54.f/295.f,0.f),FVector2D(295,50));
    FSlateBrush Hover=Normal;Hover.TintColor=FLinearColor(1.22f,1.10f,0.86f,1.f);
    FSlateBrush Pressed=Normal;Pressed.TintColor=FLinearColor(0.72f,0.66f,0.54f,1.f);
    FSlateBrush Disabled=Normal;Disabled.TintColor=FLinearColor(0.45f,0.45f,0.5f,0.62f);
    S.SetNormal(Normal);S.SetHovered(Hover);S.SetPressed(Pressed);S.SetDisabled(Disabled);
    S.NormalPadding=FMargin(18,6);S.PressedPadding=FMargin(18,8,18,4);
    return S;
}

FButtonStyle FEGVisualStyle::Light()
{
    FButtonStyle S;
    FSlateBrush Normal=Box(TEXT("/Game/Assets/UI/Textures/BoardTheme/Buttons/btn_light_small_200"),FMargin(60.f/200.f,0.f,60.f/200.f,0.f),FVector2D(200,50));
    FSlateBrush Hover=Normal;Hover.TintColor=FLinearColor(1.06f,1.02f,0.94f,1.f);
    FSlateBrush Pressed=Normal;Pressed.TintColor=FLinearColor(0.76f,0.73f,0.68f,1.f);
    FSlateBrush Disabled=Normal;Disabled.TintColor=FLinearColor(0.5f,0.5f,0.52f,0.6f);
    S.SetNormal(Normal);S.SetHovered(Hover);S.SetPressed(Pressed);S.SetDisabled(Disabled);
    S.NormalPadding=FMargin(16,5);S.PressedPadding=FMargin(16,7,16,3);
    return S;
}

FButtonStyle FEGVisualStyle::GoldCta()
{
    FButtonStyle S;
    FSlateBrush Normal=Box(TEXT("/Game/Assets/UI/Textures/BoardTheme/Buttons/btn_gold_fill_446x80"),FMargin(34.f/446.f,0.f,34.f/446.f,0.f),FVector2D(446,80));
    FSlateBrush Hover=Normal;Hover.TintColor=FLinearColor(1.10f,1.05f,0.94f,1.f);
    FSlateBrush Pressed=Normal;Pressed.TintColor=FLinearColor(0.82f,0.76f,0.62f,1.f);
    FSlateBrush Disabled=Normal;Disabled.TintColor=FLinearColor(0.48f,0.46f,0.42f,0.6f);
    S.SetNormal(Normal);S.SetHovered(Hover);S.SetPressed(Pressed);S.SetDisabled(Disabled);
    S.NormalPadding=FMargin(20,6);S.PressedPadding=FMargin(20,8,20,4);
    return S;
}

FButtonStyle FEGVisualStyle::Card()
{
    FButtonStyle S;
    FSlateBrush Normal=Panel(false);
    FSlateBrush Hover=Normal;Hover.TintColor=FLinearColor(1.14f,1.10f,1.02f,1.f);
    FSlateBrush Pressed=Normal;Pressed.TintColor=FLinearColor(0.84f,0.80f,0.74f,1.f);
    S.SetNormal(Normal);S.SetHovered(Hover);S.SetPressed(Pressed);S.SetDisabled(Normal);
    S.NormalPadding=FMargin(7);S.PressedPadding=FMargin(7);return S;
}

FButtonStyle FEGVisualStyle::Ghost()
{
    FButtonStyle S;FSlateBrush None;None.DrawAs=ESlateBrushDrawType::NoDrawType;
    S.SetNormal(None);S.SetHovered(None);S.SetPressed(None);S.SetDisabled(None);
    S.NormalPadding=FMargin(0);S.PressedPadding=FMargin(0);return S;
}

FProgressBarStyle FEGVisualStyle::Progress()
{
    FProgressBarStyle S;
    S.SetBackgroundImage(Rounded(FLinearColor(0.86f,0.83f,0.78f,0.55f),4.f));
    S.SetFillImage(Rounded(FLinearColor::White,4.f));
    S.SetMarqueeImage(FSlateBrush());
    return S;
}

// ---------------------------------------------------------------- Pearl-Gold
// Generated component textures (ready-manifest 2026-10-20). Native pixels are
// preserved; every brush crops to the manifest content bounds so transparent
// padding never makes a plaque look smaller than its widget.
namespace
{
    const TCHAR* const PearlRoot = TEXT("/Game/Assets/UI/Textures/PearlGold/");
    TMap<FString,TWeakObjectPtr<UTexture2D>> GPearlTextures;

    UTexture2D* PearlTexture(const TCHAR* Relative)
    {
        const FString Key(Relative);
        if(const auto* Found=GPearlTextures.Find(Key);Found&&Found->IsValid())return Found->Get();
        auto* Texture=LoadObject<UTexture2D>(nullptr,*(FString(PearlRoot)+Key));
        GPearlTextures.Add(Key,Texture);
        return Texture;
    }

    FBox2f Uv(float X0,float Y0,float X1,float Y1){return FBox2f(FVector2f(X0,Y0),FVector2f(X1,Y1));}

    // manifest content_bounds_uv
    const FBox2f PanelUv   = Uv(0.080178f,0.059932f,0.920564f,0.934932f);
    const FBox2f ButtonUv  = Uv(0.035912f,0.201657f,0.964088f,0.761050f);
    const FBox2f CardUv    = Uv(0.051758f,0.049479f,0.949219f,0.947266f);
    const FBox2f HeaderUv  = Uv(0.004215f,0.286685f,0.996253f,0.697011f);
    const FBox2f OrbUv     = Uv(0.058214f,0.059011f,0.938596f,0.943381f);
    const FBox2f AvatarUv  = Uv(0.020734f,0.011962f,0.978469f,0.969697f);

    FSlateBrush PearlImageBrush(const TCHAR* Relative,const FBox2f& UvRegion,FVector2D Size,FLinearColor Tint=FLinearColor::White)
    {
        FSlateBrush B;
        B.SetResourceObject(PearlTexture(Relative));
        B.DrawAs=ESlateBrushDrawType::Image;
        B.ImageSize=Size;
        B.TintColor=Tint;
        B.SetUVRegion(UvRegion);
        return B;
    }

    FSlateBrush PearlButtonBrush(FLinearColor Tint=FLinearColor::White)
    {
        return PearlImageBrush(TEXT("Buttons/T_UI_Pearl_Button"),ButtonUv,FVector2D(2172,724),Tint);
    }
}

FSlateFontInfo FEGVisualStyle::PearlFont(int32 Size,bool bTitle)
{
    // Signature kept for existing callers; Pearl family now uses the Body role.
    return BodyFont(Size,false);
}

FSlateBrush FEGVisualStyle::Rounded(FLinearColor Fill,float Radius,FLinearColor Outline,float Thickness)
{
    FSlateRoundedBoxBrush Brush(Fill,Radius,Outline,Thickness);
    return Brush;
}

FSlateBrush FEGVisualStyle::PearlBanner()
{return Rounded(EGPearlGold::Ivory,16.f,EGPearlGold::GoldEdge,1.f);}
FSlateBrush FEGVisualStyle::PearlPill()
{return Rounded(EGPearlGold::Ivory,14.f,EGPearlGold::GoldEdge,1.f);}
FSlateBrush FEGVisualStyle::PearlPanel()
{return Rounded(EGPearlGold::Ivory,12.f,EGPearlGold::GoldEdge,1.f);}
FSlateBrush FEGVisualStyle::PearlCardFrame()
{return Rounded(FLinearColor::Transparent,5.f,EGPearlGold::GoldEdge,.7f);}
FSlateBrush FEGVisualStyle::GoldOutlinePill(){return PearlPill();}
FSlateBrush FEGVisualStyle::PearlOrb()
{return Rounded(EGPearlGold::Ivory,62.f,EGPearlGold::GoldEdge,1.f);}
FSlateBrush FEGVisualStyle::PearlAvatarRing()
{return Rounded(FLinearColor::Transparent,52.f,EGPearlGold::GoldEdge,1.f);}
FSlateBrush FEGVisualStyle::PearlTray(){return Rounded(FLinearColor::Transparent,4.f);}
FButtonStyle FEGVisualStyle::PearlButton()
{
 FButtonStyle S;S.SetNormal(PearlPill());
 S.SetHovered(Rounded(EGPearlGold::Srgb(93,73,122,.63f),14.f,EGPearlGold::Champagne,1.f));
 S.SetPressed(Rounded(EGPearlGold::Srgb(47,34,69,.72f),14.f,EGPearlGold::GoldEdge,1.f));
 S.SetDisabled(Rounded(EGPearlGold::Srgb(41,33,53,.3f),14.f,EGPearlGold::GoldEdge,.6f));
 S.NormalPadding=FMargin(8,2);S.PressedPadding=FMargin(8,3,8,1);return S;
}
FButtonStyle FEGVisualStyle::GoldOutlineButton(){return PearlButton();}
FButtonStyle FEGVisualStyle::OrbButton()
{
 FButtonStyle S=PearlButton();S.SetNormal(PearlOrb());
 S.SetHovered(Rounded(EGPearlGold::Srgb(93,73,122,.63f),62.f,EGPearlGold::Champagne,1.2f));
 S.SetPressed(Rounded(EGPearlGold::Srgb(47,34,69,.72f),62.f,EGPearlGold::GoldEdge,1.f));
 S.SetDisabled(PearlOrb());S.NormalPadding=FMargin(0);S.PressedPadding=FMargin(0);return S;
}
