#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"

class UUserWidget;

/**
 * UI typography roles introduced by the 2026-11-06 UI overhaul.
 * Body = Noto Sans SC, Heading = Noto Serif CJK SC, Narrative = LXGW WenKai,
 * Numeric = Cinzel (numerals only; not used for CJK text).
 */
enum class EEGFontRole : uint8
{
    Body,
    Heading,
    Narrative,
    Numeric
};

/** Font info for a role at the given size; never returns an empty/legacy font when the assets load. */
FSlateFontInfo EGFontForRole(EEGFontRole Role, int32 Size, bool bOutline = false);

/**
 * Replace only the font asset/typeface of Current, keeping its size and every other
 * non-font setting (outline, letter spacing, monospacing, material).
 */
FSlateFontInfo EGResolveFont(const FSlateFontInfo& Current, EEGFontRole Role);

/**
 * Role implied by a widget name, falling back to the font size.
 * Numeric names (Gold/Price/Cost/Round/Count/Level/Number/Corruption/Mana/Health) win,
 * then Heading names (Title/Name/Heading/Phase/Stage/Round), then size >= 24, else Body.
 */
EEGFontRole EGResolveFontRole(FName WidgetName, int32 CurrentSize);

/**
 * Apply role fonts to every UTextBlock built inside Widget. Only the font itself is
 * touched: colour, size, layout and text are preserved. Numeric names whose current
 * text contains CJK are demoted to Heading because Cinzel has no CJK glyphs.
 * bIncludeChildren also walks nested UUserWidget widget trees (e.g. main-menu settings).
 */
void EGApplyTypography(UUserWidget* Widget, bool bIncludeChildren = true);
