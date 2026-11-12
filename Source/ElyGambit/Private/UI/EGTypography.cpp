#include "UI/EGTypography.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "UI/EGVisualStyle.h"

namespace
{
    bool EGTextHasCjk(const FString& Text)
    {
        for (int32 Index = 0; Index < Text.Len(); ++Index)
        {
            const uint32 Code = static_cast<uint32>(Text[Index]);
            if ((Code >= 0x2E80 && Code <= 0x9FFF) ||
                (Code >= 0xF900 && Code <= 0xFAFF) ||
                (Code >= 0xFE30 && Code <= 0xFE4F) ||
                (Code >= 0xFF00 && Code <= 0xFFEF) ||
                (Code >= 0x3000 && Code <= 0x303F) ||
                (Code >= 0xD800 && Code <= 0xDFFF))
            {
                return true;
            }
        }
        return false;
    }

    bool EGNameHasToken(const FString& LowerName, const TCHAR* const* Tokens, int32 Count)
    {
        for (int32 Index = 0; Index < Count; ++Index)
        {
            if (LowerName.Contains(Tokens[Index]))
            {
                return true;
            }
        }
        return false;
    }

    void EGApplyTypographyToTree(UWidgetTree* Tree, bool bIncludeChildren, int32 Depth)
    {
        if (!Tree || Depth > 8)
        {
            return;
        }

        TArray<UWidget*> Widgets;
        Tree->ForEachWidget([&Widgets](UWidget* Widget)
        {
            if (Widget)
            {
                Widgets.Add(Widget);
            }
        });

        for (UWidget* Widget : Widgets)
        {
            UTextBlock* Text = Cast<UTextBlock>(Widget);
            if (!Text)
            {
                continue;
            }
            const FSlateFontInfo Current = Text->GetFont();
            EEGFontRole Role = EGResolveFontRole(Text->GetFName(), FMath::RoundToInt(Current.Size));
            // Cinzel (Numeric) carries no CJK glyphs; keep Chinese labels on a CJK face.
            if (Role == EEGFontRole::Numeric && EGTextHasCjk(Text->GetText().ToString()))
            {
                Role = EEGFontRole::Heading;
            }
            Text->SetFont(EGResolveFont(Current, Role));
        }

        if (!bIncludeChildren)
        {
            return;
        }
        for (UWidget* Widget : Widgets)
        {
            UUserWidget* Child = Cast<UUserWidget>(Widget);
            if (Child && Child->WidgetTree && Child->WidgetTree.Get() != Tree)
            {
                EGApplyTypographyToTree(Child->WidgetTree.Get(), true, Depth + 1);
            }
        }
    }
}

FSlateFontInfo EGFontForRole(EEGFontRole Role, int32 Size, bool bOutline)
{
    switch (Role)
    {
    case EEGFontRole::Heading:
        return FEGVisualStyle::SerifFont(Size, bOutline);
    case EEGFontRole::Narrative:
        return FEGVisualStyle::NarrationFont(Size, bOutline);
    case EEGFontRole::Numeric:
        return FEGVisualStyle::NumeralFont(Size, bOutline);
    case EEGFontRole::Body:
    default:
        return FEGVisualStyle::BodyFont(Size, bOutline);
    }
}

FSlateFontInfo EGResolveFont(const FSlateFontInfo& Current, EEGFontRole Role)
{
    FSlateFontInfo Result = EGFontForRole(Role, FMath::RoundToInt(Current.Size));
    // Keep the caller's typography exactly as authored; only the font asset/typeface changes.
    Result.Size = Current.Size;
    Result.OutlineSettings = Current.OutlineSettings;
    Result.LetterSpacing = Current.LetterSpacing;
    Result.SkewAmount = Current.SkewAmount;
    Result.bForceMonospaced = Current.bForceMonospaced;
    Result.MonospacedWidth = Current.MonospacedWidth;
    Result.bMaterialIsStencil = Current.bMaterialIsStencil;
    Result.FontMaterial = Current.FontMaterial;
    return Result;
}

EEGFontRole EGResolveFontRole(FName WidgetName, int32 CurrentSize)
{
    static const TCHAR* const NumericTokens[] = {TEXT("gold"), TEXT("price"), TEXT("cost"), TEXT("round"),
        TEXT("count"), TEXT("level"), TEXT("number"), TEXT("corruption"), TEXT("mana"), TEXT("health")};
    static const TCHAR* const HeadingTokens[] = {TEXT("title"), TEXT("name"), TEXT("heading"),
        TEXT("phase"), TEXT("stage"), TEXT("round")};

    FString Name = WidgetName.ToString();
    Name.ToLowerInline();

    if (EGNameHasToken(Name, NumericTokens, UE_ARRAY_COUNT(NumericTokens)))
    {
        return EEGFontRole::Numeric;
    }
    if (EGNameHasToken(Name, HeadingTokens, UE_ARRAY_COUNT(HeadingTokens)))
    {
        return EEGFontRole::Heading;
    }
    if (CurrentSize >= 24)
    {
        return EEGFontRole::Heading;
    }
    return EEGFontRole::Body;
}

void EGApplyTypography(UUserWidget* Widget, bool bIncludeChildren)
{
    if (!Widget || !Widget->WidgetTree)
    {
        return;
    }
    EGApplyTypographyToTree(Widget->WidgetTree.Get(), bIncludeChildren, 0);
}
