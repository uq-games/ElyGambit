#include "UI/Widget/EGRichText.h"

#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/SlateTypes.h"
#include "UI/EGVisualStyle.h"
#include "UObject/UnrealType.h"

namespace
{
const TCHAR* const NumberRunTag=TEXT("num");

/**
 * Saved slot geometry. Every supported parent kind is captured before the old text
 * block is detached, because removing a child destroys the slot that owned the data.
 */
enum class EGParentSlotKind : uint8
{
    Unsupported,
    CanvasPanel,
    VerticalBox,
    HorizontalBox,
    Overlay,
    SizeBox,
    Button,
    ScrollBox
};

struct FEGSavedSlot
{
    EGParentSlotKind Kind=EGParentSlotKind::Unsupported;
    int32 ChildIndex=INDEX_NONE;
    FAnchorData CanvasLayout;
    bool bCanvasAutoSize=false;
    int32 CanvasZOrder=0;
    FSlateChildSize ChildSize;
    FMargin Padding=FMargin(0);
    EHorizontalAlignment HorizontalAlignment=HAlign_Fill;
    EVerticalAlignment VerticalAlignment=VAlign_Fill;
};

bool CaptureSlot(UPanelWidget* Parent,UPanelSlot* Slot,FEGSavedSlot& Out)
{
    if(!Parent||!Slot)return false;
    Out.ChildIndex=Parent->GetChildIndex(Slot->Content);
    if(const auto* CanvasSlot=Cast<UCanvasPanelSlot>(Slot))
    {
        if(!Cast<UCanvasPanel>(Parent))return false;
        Out.Kind=EGParentSlotKind::CanvasPanel;
        Out.CanvasLayout=CanvasSlot->GetLayout();
        Out.bCanvasAutoSize=CanvasSlot->GetAutoSize();
        Out.CanvasZOrder=CanvasSlot->GetZOrder();
        return true;
    }
    if(const auto* BoxSlot=Cast<UVerticalBoxSlot>(Slot))
    {
        if(!Cast<UVerticalBox>(Parent))return false;
        Out.Kind=EGParentSlotKind::VerticalBox;
        Out.Padding=BoxSlot->GetPadding();Out.ChildSize=BoxSlot->GetSize();
        Out.HorizontalAlignment=BoxSlot->GetHorizontalAlignment();Out.VerticalAlignment=BoxSlot->GetVerticalAlignment();
        return true;
    }
    if(const auto* RowSlot=Cast<UHorizontalBoxSlot>(Slot))
    {
        if(!Cast<UHorizontalBox>(Parent))return false;
        Out.Kind=EGParentSlotKind::HorizontalBox;
        Out.Padding=RowSlot->GetPadding();Out.ChildSize=RowSlot->GetSize();
        Out.HorizontalAlignment=RowSlot->GetHorizontalAlignment();Out.VerticalAlignment=RowSlot->GetVerticalAlignment();
        return true;
    }
    if(const auto* OverlaySlotPtr=Cast<UOverlaySlot>(Slot))
    {
        if(!Cast<UOverlay>(Parent))return false;
        Out.Kind=EGParentSlotKind::Overlay;
        Out.Padding=OverlaySlotPtr->GetPadding();
        Out.HorizontalAlignment=OverlaySlotPtr->GetHorizontalAlignment();Out.VerticalAlignment=OverlaySlotPtr->GetVerticalAlignment();
        return true;
    }
    if(const auto* SizeSlot=Cast<USizeBoxSlot>(Slot))
    {
        if(!Cast<USizeBox>(Parent))return false;
        Out.Kind=EGParentSlotKind::SizeBox;
        Out.Padding=SizeSlot->GetPadding();
        Out.HorizontalAlignment=SizeSlot->GetHorizontalAlignment();Out.VerticalAlignment=SizeSlot->GetVerticalAlignment();
        return true;
    }
    if(const auto* ButtonSlotPtr=Cast<UButtonSlot>(Slot))
    {
        if(!Cast<UButton>(Parent))return false;
        Out.Kind=EGParentSlotKind::Button;
        Out.Padding=ButtonSlotPtr->GetPadding();
        Out.HorizontalAlignment=ButtonSlotPtr->GetHorizontalAlignment();Out.VerticalAlignment=ButtonSlotPtr->GetVerticalAlignment();
        return true;
    }
    if(const auto* ScrollSlot=Cast<UScrollBoxSlot>(Slot))
    {
        if(!Cast<UScrollBox>(Parent))return false;
        Out.Kind=EGParentSlotKind::ScrollBox;
        Out.Padding=ScrollSlot->GetPadding();Out.ChildSize=ScrollSlot->GetSize();
        Out.HorizontalAlignment=ScrollSlot->GetHorizontalAlignment();Out.VerticalAlignment=ScrollSlot->GetVerticalAlignment();
        return true;
    }
    return false;
}

bool ApplySavedSlot(UPanelSlot* Slot,const FEGSavedSlot& Saved)
{
    if(!Slot)return false;
    switch(Saved.Kind)
    {
    case EGParentSlotKind::CanvasPanel:
        if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Slot))
        {
            CanvasSlot->SetLayout(Saved.CanvasLayout);
            CanvasSlot->SetAutoSize(Saved.bCanvasAutoSize);
            CanvasSlot->SetZOrder(Saved.CanvasZOrder);
            return true;
        }
        break;
    case EGParentSlotKind::VerticalBox:
        if(auto* BoxSlot=Cast<UVerticalBoxSlot>(Slot))
        {
            BoxSlot->SetPadding(Saved.Padding);BoxSlot->SetSize(Saved.ChildSize);
            BoxSlot->SetHorizontalAlignment(Saved.HorizontalAlignment);BoxSlot->SetVerticalAlignment(Saved.VerticalAlignment);
            return true;
        }
        break;
    case EGParentSlotKind::HorizontalBox:
        if(auto* RowSlot=Cast<UHorizontalBoxSlot>(Slot))
        {
            RowSlot->SetPadding(Saved.Padding);RowSlot->SetSize(Saved.ChildSize);
            RowSlot->SetHorizontalAlignment(Saved.HorizontalAlignment);RowSlot->SetVerticalAlignment(Saved.VerticalAlignment);
            return true;
        }
        break;
    case EGParentSlotKind::Overlay:
        if(auto* OverlaySlotPtr=Cast<UOverlaySlot>(Slot))
        {
            OverlaySlotPtr->SetPadding(Saved.Padding);
            OverlaySlotPtr->SetHorizontalAlignment(Saved.HorizontalAlignment);OverlaySlotPtr->SetVerticalAlignment(Saved.VerticalAlignment);
            return true;
        }
        break;
    case EGParentSlotKind::SizeBox:
        if(auto* SizeSlot=Cast<USizeBoxSlot>(Slot))
        {
            SizeSlot->SetPadding(Saved.Padding);
            SizeSlot->SetHorizontalAlignment(Saved.HorizontalAlignment);SizeSlot->SetVerticalAlignment(Saved.VerticalAlignment);
            return true;
        }
        break;
    case EGParentSlotKind::Button:
        if(auto* ButtonSlotPtr=Cast<UButtonSlot>(Slot))
        {
            ButtonSlotPtr->SetPadding(Saved.Padding);
            ButtonSlotPtr->SetHorizontalAlignment(Saved.HorizontalAlignment);ButtonSlotPtr->SetVerticalAlignment(Saved.VerticalAlignment);
            return true;
        }
        break;
    case EGParentSlotKind::ScrollBox:
        if(auto* ScrollSlot=Cast<UScrollBoxSlot>(Slot))
        {
            ScrollSlot->SetPadding(Saved.Padding);ScrollSlot->SetSize(Saved.ChildSize);
            ScrollSlot->SetHorizontalAlignment(Saved.HorizontalAlignment);ScrollSlot->SetVerticalAlignment(Saved.VerticalAlignment);
            return true;
        }
        break;
    default:
        break;
    }
    return false;
}

UPanelSlot* AttachChild(UPanelWidget* Parent,UWidget* Child,int32 ChildIndex)
{
    if(!Parent||!Child)return nullptr;
    if(ChildIndex!=INDEX_NONE)
    {
        if(UPanelSlot* Slot=Parent->InsertChildAt(ChildIndex,Child))return Slot;
    }
    return Parent->AddChild(Child);
}
}

FEGNumberHighlightDecorator::FEGNumberHighlightDecorator(URichTextBlock* InOwner)
    :FRichTextDecorator(InOwner)
{
}

bool FEGNumberHighlightDecorator::Supports(const FTextRunParseResults& RunParseResult,const FString& Text) const
{
    (void)Text;
    return RunParseResult.Name.Equals(NumberRunTag,ESearchCase::CaseSensitive)||RunParseResult.Name==TEXT("skillready")||RunParseResult.Name==TEXT("skilllocked");
}

void FEGNumberHighlightDecorator::CreateDecoratorText(const FTextRunInfo& RunInfo,FTextBlockStyle& InOutTextStyle,FString& InOutString) const
{
    // FRichTextDecorator::Create delegates text insertion to this override; changing
    // only the style would create an empty run and silently hide every number.
    InOutString+=RunInfo.Content.ToString();
    if(RunInfo.Name==TEXT("skillready")||RunInfo.Name==TEXT("skilllocked"))
    {
        const bool Ready=RunInfo.Name==TEXT("skillready");
        if(!Ready)InOutTextStyle.Font.Size=FMath::Max(18.f,InOutTextStyle.Font.Size-2.f);
        InOutTextStyle.ColorAndOpacity=FSlateColor(Ready?FLinearColor(1.f,.78f,.38f,1.f):FLinearColor(.62f,.65f,.70f,1.f));
        return;
    }
    const int32 FontSize=FMath::Max(1,FMath::RoundToInt(InOutTextStyle.Font.Size));
    FSlateFontInfo NumeralFont=FEGVisualStyle::NumeralFont(FontSize);
    // Keep the outline/shadow treatment of the surrounding body run; only the face and
    // the emphasis colour change.
    NumeralFont.OutlineSettings=InOutTextStyle.Font.OutlineSettings;
    InOutTextStyle.Font=NumeralFont;
    // Skill text can appear on both dark tooltips and light card surfaces. Select
    // an accent from the surrounding text's contrast direction: pale gold on
    // dark surfaces, deep rose on light ones. Keep the owner's opacity.
    const FLinearColor BaseColor=InOutTextStyle.ColorAndOpacity.GetSpecifiedColor();
    FLinearColor Highlight=BaseColor.GetLuminance()<0.35f
        ?FLinearColor(0.40f,0.025f,0.12f,1.f):FEGVisualStyle::NumberHighlight;
    Highlight.A=BaseColor.A;
    InOutTextStyle.ColorAndOpacity=FSlateColor(Highlight);
}

TSharedPtr<ITextDecorator> UEGNumberHighlightDecorator::CreateDecorator(URichTextBlock* InOwner)
{
    return MakeShared<FEGNumberHighlightDecorator>(InOwner);
}

UEGRichTextBlock::UEGRichTextBlock(const FObjectInitializer& ObjectInitializer)
    :Super(ObjectInitializer)
{
    SetAutoWrapText(true);
    SetVisibility(ESlateVisibility::HitTestInvisible);
    // Must be present before the first RebuildWidget so CreateDecorators sees it.
    DecoratorClasses.Add(UEGNumberHighlightDecorator::StaticClass());
}

void UEGRichTextBlock::EnsureNumberDecorator()
{
    const UClass* RequiredDecorator=UEGNumberHighlightDecorator::StaticClass();
    const bool bAlreadyPresent=DecoratorClasses.ContainsByPredicate(
        [RequiredDecorator](const TSubclassOf<URichTextBlockDecorator>& Entry){return Entry.Get()==RequiredDecorator;});
    if(bAlreadyPresent)return;
    TArray<TSubclassOf<URichTextBlockDecorator>> Classes=DecoratorClasses;
    Classes.Add(UEGNumberHighlightDecorator::StaticClass());
    SetDecorators(Classes);
}

TSharedRef<SWidget> UEGRichTextBlock::RebuildWidget()
{
    EnsureNumberDecorator();
    return Super::RebuildWidget();
}

FString UEGRichTextBlock::EscapeRichText(const FString& PlainText)
{
    FString Escaped=PlainText;
    // "&" first so the entities introduced below are not escaped a second time.
    Escaped.ReplaceInline(TEXT("&"),TEXT("&amp;"),ESearchCase::CaseSensitive);
    Escaped.ReplaceInline(TEXT("<"),TEXT("&lt;"),ESearchCase::CaseSensitive);
    Escaped.ReplaceInline(TEXT(">"),TEXT("&gt;"),ESearchCase::CaseSensitive);
    return Escaped;
}

FString UEGRichTextBlock::BuildHighlightedMarkup(const FString& PlainText)
{
    const FString Escaped=EscapeRichText(PlainText);
    if(Escaped.IsEmpty())return Escaped;
    FString Markup;
    Markup.Reserve(Escaped.Len()+32);
    int32 Index=0;
    while(Index<Escaped.Len())
    {
        if(!FChar::IsDigit(Escaped[Index]))
        {
            Markup.AppendChar(Escaped[Index]);
            ++Index;
            continue;
        }
        const int32 Begin=Index;
        while(Index<Escaped.Len()&&FChar::IsDigit(Escaped[Index]))++Index;
        // Decimal part only when the dot is followed by another digit: "1.5" colours,
        // a sentence-final "1." does not swallow the full stop.
        if(Index+1<Escaped.Len()&&Escaped[Index]==TEXT('.')&&FChar::IsDigit(Escaped[Index+1]))
        {
            ++Index;
            while(Index<Escaped.Len()&&FChar::IsDigit(Escaped[Index]))++Index;
        }
        if(Index<Escaped.Len()&&Escaped[Index]==TEXT('%'))++Index;
        Markup+=TEXT("<");
        Markup+=NumberRunTag;
        Markup+=TEXT(">");
        Markup+=Escaped.Mid(Begin,Index-Begin);
        Markup+=TEXT("</>");
    }
    return Markup;
}

void UEGRichTextBlock::SetHighlightedText(const FText& InPlainText)
{
    StyledSkillLevel=INDEX_NONE;
    PlainText=InPlainText;
    Super::SetText(FText::FromString(BuildHighlightedMarkup(InPlainText.ToString())));
}

FString UEGRichTextBlock::BuildSkillLevelMarkup(const FString& Text,int32 Level)
{
    TArray<FString> Lines;Text.ParseIntoArrayLines(Lines,false);
    FString Result;int32 Required=0;
    for(int32 I=0;I<Lines.Num();++I)
    {
        if(I)Result+=TEXT("\n");
        if(Lines[I]==TEXT("6级"))Required=6;
        else if(Lines[I]==TEXT("11级"))Required=11;
        if(!Required)Result+=BuildHighlightedMarkup(Lines[I]);
        else if(!Lines[I].IsEmpty())Result+=FString::Printf(TEXT("<%s>%s</>"),Level>=Required?TEXT("skillready"):TEXT("skilllocked"),*EscapeRichText(Lines[I]));
    }
    return Result;
}
void UEGRichTextBlock::SetSkillLevelText(const FText& InPlainText,int32 Level)
{
    if(StyledSkillLevel==Level&&PlainText.EqualTo(InPlainText))return;
    StyledSkillLevel=Level;PlainText=InPlainText;
    Super::SetText(FText::FromString(BuildSkillLevelMarkup(PlainText.ToString(),Level)));
}

void UEGRichTextBlock::SetText(const FText& InText)
{
    // All widget text enters as plain FText; markup is generated here only.
    SetHighlightedText(InText);
}

void UEGRichTextBlock::ApplyTextStyle(const FSlateFontInfo& Font,const FLinearColor& Color)
{
    SetDefaultFont(Font);
    SetDefaultColorAndOpacity(FSlateColor(Color));
}

UEGRichTextBlock* EGReplaceTextBlockWithRichText(UWidgetTree* Tree,UTextBlock* Old)
{
    if(!Tree||!Old||!IsValid(Old))return nullptr;
    // Designer previews keep the authored tree; never rename widgets there.
    if(Old->IsDesignTime())return nullptr;
    UPanelWidget* Parent=Old->GetParent();
    UPanelSlot* OldSlot=Old->Slot;
    if(!Parent||!OldSlot)return nullptr;
    // InsertChildAt only changes UMG's slot order after AddChild has appended to
    // the live Slate container. Do not attempt a replacement once that container
    // exists: callers retain the original, correctly ordered plain text instead.
    if(Parent->GetCachedWidget().IsValid())return nullptr;
    FEGSavedSlot Saved;
    if(!CaptureSlot(Parent,OldSlot,Saved)||Saved.ChildIndex==INDEX_NONE)return nullptr;
    // UE 5.8 exposes setters but no public getters for these reflected layout
    // fields. Copy the shared base-class properties before Slate is constructed.
    static const TCHAR* const LayoutNames[]={TEXT("Justification"),TEXT("WrappingPolicy"),TEXT("LineHeightPercentage"),
        TEXT("ApplyLineHeightToBottomLine"),TEXT("Margin"),TEXT("ShapedTextOptions"),TEXT("FontFacesLoadingPaintPolicy")};
    TArray<FProperty*,TInlineAllocator<UE_ARRAY_COUNT(LayoutNames)>> LayoutProperties;
    for(const TCHAR* Name:LayoutNames)
    {
        FProperty* Property=FindFProperty<FProperty>(UTextLayoutWidget::StaticClass(),FName(Name));
        if(!Property)return nullptr;
        LayoutProperties.Add(Property);
    }

    // Cache the authored style before the old block is removed from the tree.
    const FSlateFontInfo OldFont=Old->GetFont();
    const FLinearColor OldColor=Old->GetColorAndOpacity().GetSpecifiedColor();
    const FText OldText=Old->GetText();
    const FLinearColor OldShadowColor=Old->GetShadowColorAndOpacity();
    const FVector2D OldShadowOffset=Old->GetShadowOffset();
    const bool bOldAutoWrap=Old->GetAutoWrapText();
    const float OldWrapTextAt=Old->GetWrapTextAt();
    const float OldMinDesiredWidth=Old->GetMinDesiredWidth();
    const ESlateVisibility OldVisibility=Old->GetVisibility();
    const EWidgetClipping OldClipping=Old->GetClipping();
    const float OldRenderOpacity=Old->GetRenderOpacity();
    const FWidgetTransform OldRenderTransform=Old->GetRenderTransform();
    const FVector2D OldRenderTransformPivot=Old->GetRenderTransformPivot();
    const bool bOldEnabled=Old->GetIsEnabled();
    const FText OldToolTip=Old->GetToolTipText();
    const FName TargetName=Old->GetFName();

    const FName TempName=MakeUniqueObjectName(Tree,UEGRichTextBlock::StaticClass(),TEXT("EGRichTextBlock"));
    UEGRichTextBlock* New=Tree->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass(),TempName);
    if(!New)return nullptr;
    for(const FProperty* Property:LayoutProperties)Property->CopyCompleteValue_InContainer(New,Old);

    New->ApplyTextStyle(OldFont,OldColor);
    New->SetDefaultShadowColorAndOpacity(OldShadowColor);
    New->SetDefaultShadowOffset(OldShadowOffset);
    New->SetAutoWrapText(bOldAutoWrap);
    New->SetWrapTextAt(OldWrapTextAt);
    New->SetMinDesiredWidth(OldMinDesiredWidth);
    New->SetVisibility(OldVisibility);
    New->SetClipping(OldClipping);
    New->SetRenderOpacity(OldRenderOpacity);
    New->SetRenderTransform(OldRenderTransform);
    New->SetRenderTransformPivot(OldRenderTransformPivot);
    New->SetIsEnabled(bOldEnabled);
    New->SetToolTipText(OldToolTip);
    New->SetHighlightedText(OldText);

    // Hand the public widget name to the rich block so FindWidget/GetWidgetFromName
    // keep resolving while the old block becomes a detached, ignored object.
    const FName RetiredName=MakeUniqueObjectName(Tree,Old->GetClass(),TEXT("RetiredTextBlock"));
    if(!Old->Rename(*RetiredName.ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional))
    {
        New->MarkAsGarbage();
        return nullptr;
    }
    Old->RemoveFromParent();

    UPanelSlot* NewSlot=AttachChild(Parent,New,Saved.ChildIndex);
    if(!NewSlot||!ApplySavedSlot(NewSlot,Saved))
    {
        // Restore the plain block (same slot geometry) and let callers keep using it.
        New->RemoveFromParent();
        if(UPanelSlot* RestoredSlot=AttachChild(Parent,Old,Saved.ChildIndex))ApplySavedSlot(RestoredSlot,Saved);
        Old->Rename(*TargetName.ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional);
        New->MarkAsGarbage();
        return nullptr;
    }

    if(!New->Rename(*TargetName.ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional))
    {
        New->RemoveFromParent();
        if(UPanelSlot* RestoredSlot=AttachChild(Parent,Old,Saved.ChildIndex))ApplySavedSlot(RestoredSlot,Saved);
        Old->Rename(*TargetName.ToString(),nullptr,REN_DontCreateRedirectors|REN_NonTransactional);
        New->MarkAsGarbage();
        return nullptr;
    }
    return New;
}

UEGRichTextBlock* EGEnsureNamedRichText(UUserWidget* Owner,FName WidgetName)
{
    if(!Owner||WidgetName.IsNone())return nullptr;
    UWidgetTree* Tree=Owner->WidgetTree;
    if(!Tree)return nullptr;
    UWidget* Existing=Tree->FindWidget(WidgetName);
    if(!Existing)return nullptr;
    if(auto* ExistingRich=Cast<UEGRichTextBlock>(Existing))return ExistingRich;
    auto* ExistingText=Cast<UTextBlock>(Existing);
    if(!ExistingText)return nullptr;
    return EGReplaceTextBlockWithRichText(Tree,ExistingText);
}
