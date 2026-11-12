#pragma once
#include "CoreMinimal.h"
#include "Components/RichTextBlock.h"
#include "Components/RichTextBlockDecorator.h"
#include "EGRichText.generated.h"

class UUserWidget;
class UWidgetTree;
class UTextBlock;

/**
 * Rich-text contract for the 2026-11-06 UI overhaul (Docs/UIOverhaul20261106.md).
 *
 * Data assets keep plain FText. Numeric emphasis is generated only at the display
 * layer: plain text is escaped (&, <, >) and numeric tokens are wrapped in a <num>
 * run, which UEGNumberHighlightDecorator renders with the Numeric font role and
 * FEGVisualStyle::NumberHighlight.
 *
 * NOTE: URichTextBlock derives from UTextLayoutWidget, not from UTextBlock, so a
 * replaced control is *not* Cast<UTextBlock> compatible. Callers that need the
 * displayed text must use UEGRichTextBlock::GetPlainText() (GetText() returns the
 * engine serialized markup, e.g. "Lv.<num>1</>").
 */
UCLASS()
class ELYGAMBIT_API UEGNumberHighlightDecorator : public URichTextBlockDecorator
{
    GENERATED_BODY()
public:
    virtual TSharedPtr<ITextDecorator> CreateDecorator(URichTextBlock* InOwner) override;
};

/** Supports the "num" tag; swaps only font + colour on the decorated run. */
class FEGNumberHighlightDecorator : public FRichTextDecorator
{
public:
    explicit FEGNumberHighlightDecorator(URichTextBlock* InOwner);
    virtual bool Supports(const FTextRunParseResults& RunParseResult,const FString& Text) const override;
protected:
    virtual void CreateDecoratorText(const FTextRunInfo& RunInfo,FTextBlockStyle& InOutTextStyle,FString& InOutString) const override;
};

/**
 * Rich text block with plain-text input. Auto-wraps by default, is transparent to
 * hit testing, and always registers UEGNumberHighlightDecorator before RebuildWidget.
 */
UCLASS()
class ELYGAMBIT_API UEGRichTextBlock : public URichTextBlock
{
    GENERATED_BODY()
public:
    UEGRichTextBlock(const FObjectInitializer& ObjectInitializer);

    /** Escapes rich-text markup characters so authored "<num>" text cannot inject tags. */
    static FString EscapeRichText(const FString& PlainText);
    /** Escapes PlainText and wraps integers / decimals / percentages in <num> runs. */
    static FString BuildHighlightedMarkup(const FString& PlainText);

    /** Display PlainText with numeric emphasis. Empty text is safe. */
    void SetHighlightedText(const FText& PlainText);
    static FString BuildSkillLevelMarkup(const FString& PlainText,int32 Level);
    void SetSkillLevelText(const FText& InPlainText,int32 Level);
    /** Plain text passed to SetHighlightedText, without layout markup. */
    FText GetPlainText() const { return PlainText; }
    /** Default font/colour for undecorated runs; numeric runs still use the Numeric role. */
    void ApplyTextStyle(const FSlateFontInfo& Font,const FLinearColor& Color);

    // URichTextBlock / UWidget
    virtual void SetText(const FText& InText) override;
    virtual TSharedRef<SWidget> RebuildWidget() override;

protected:
    void EnsureNumberDecorator();

private:
    FText PlainText;
    int32 StyledSkillLevel=INDEX_NONE;
};

/**
 * Replaces Old in its parent panel with a UEGRichTextBlock, copying the slot
 * (anchors/offsets/alignment/size/padding/ZOrder), text style and widget state so
 * layout does not move. Supported parents: UCanvasPanel, UVerticalBox,
 * UHorizontalBox, UOverlay, USizeBox, UButton and UScrollBox.
 *
 * Returns the new widget when the old one was renamed out of the way and the new one
 * took its name; returns nullptr when replacement is unsafe (root widget, unsupported
 * parent, designer preview, or parent with an existing Slate widget). Callers must
 * keep rendering plain text through Old then.
 */
ELYGAMBIT_API UEGRichTextBlock* EGReplaceTextBlockWithRichText(UWidgetTree* Tree,UTextBlock* Old);

/** Finds WidgetName in Owner's tree; returns it when already rich, converts a UTextBlock, else nullptr. */
ELYGAMBIT_API UEGRichTextBlock* EGEnsureNamedRichText(UUserWidget* Owner,FName WidgetName);
