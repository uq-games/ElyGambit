#include "UI/Widget/EGResultDialogueWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGGradientSurface.h"

namespace
{
constexpr double DialogueFadeSeconds = .24;
}

void UEGResultDialogueWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Root;

    // Keep the subtitle light and unobtrusive over the game scene.
    auto* Panel = WidgetTree->ConstructWidget<UCanvasPanel>();
    Panel->SetVisibility(ESlateVisibility::HitTestInvisible);
    BackgroundSlot = Root->AddChildToCanvas(Panel);
    SetFinalResultVisible(false);

    // Use only the subtle faded portion of the original underlay, with a
    // restrained fallback if the optional texture cannot be loaded.
    UTexture2D* Underlay = nullptr;
    if (FPackageName::DoesPackageExist(UnderlayTexture.ToSoftObjectPath().GetLongPackageName()))
        Underlay = UnderlayTexture.LoadSynchronous();
    if (Underlay)
    {
        auto* Image = WidgetTree->ConstructWidget<UImage>();
        FSlateBrush Brush;
        Brush.SetResourceObject(Underlay);
        Brush.DrawAs = ESlateBrushDrawType::Image;
        Brush.SetUVRegion(FBox2f(FVector2f(.05f, .58f), FVector2f(.95f, .76f)));
        Image->SetBrush(Brush);
        auto* ImageSlot = Panel->AddChildToCanvas(Image);
        ImageSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        ImageSlot->SetOffsets(FMargin(0.f, 38.f, 0.f, 0.f));
    }
    else
    {
        auto* Gradient = WidgetTree->ConstructWidget<UEGGradientSurface>();
        Gradient->TopColor = FLinearColor(.01f, .01f, .015f, .18f);
        Gradient->BottomColor = FLinearColor(.01f, .01f, .015f, 0.f);
        Gradient->bHorizontalGradient = true;
        auto* GradientSlot = Panel->AddChildToCanvas(Gradient);
        GradientSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
        GradientSlot->SetOffsets(FMargin(0.f, 38.f, 0.f, 0.f));
    }

    // Let Slate measure the font rather than placing the divider at a fixed
    // pixel offset: CJK fonts and DPI scaling have different line heights.
    auto* Rows = WidgetTree->ConstructWidget<UVerticalBox>();
    auto* RowsSlot = Panel->AddChildToCanvas(Rows);
    RowsSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    RowsSlot->SetOffsets(FMargin(8.f, 0.f, 8.f, 0.f));

    SpeakerText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultDialogueSpeaker"));
    auto SpeakerFont = FEGVisualStyle::BodyFont(20, true);
    SpeakerFont.OutlineSettings.OutlineSize = 1;
    SpeakerFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, .9f);
    SpeakerText->SetFont(SpeakerFont);
    SpeakerText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f, .82f, .51f, 1.f)));
    SpeakerText->SetShadowOffset(FVector2D(1.f, 1.f));
    SpeakerText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .85f));
    Rows->AddChildToVerticalBox(SpeakerText)->SetPadding(FMargin(0.f, 0.f, 0.f, 4.f));

    auto* LineSize = WidgetTree->ConstructWidget<USizeBox>();
    LineSize->SetHeightOverride(1.5f);
    auto* Line = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ResultDialogueDivider"));
    Line->SetBrush(FSlateColorBrush(FLinearColor(.92f, .78f, .60f, .68f)));
    LineSize->AddChild(Line);
    Rows->AddChildToVerticalBox(LineSize)->SetPadding(FMargin(0.f, 0.f, 0.f, 7.f));

    DialogueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ResultDialogueText"));
    auto DialogueFont = FEGVisualStyle::BodyFont(23, true);
    DialogueFont.OutlineSettings.OutlineSize = 1;
    DialogueFont.OutlineSettings.OutlineColor = FLinearColor(0.f, 0.f, 0.f, .92f);
    DialogueText->SetFont(DialogueFont);
    DialogueText->SetColorAndOpacity(FSlateColor(FLinearColor(.98f, .97f, .95f, 1.f)));
    DialogueText->SetShadowOffset(FVector2D(1.f, 1.f));
    DialogueText->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .9f));
    DialogueText->SetAutoWrapText(true);
    Rows->AddChildToVerticalBox(DialogueText);

    StorySkipButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("StoryDialogueSkipButton"));
    StorySkipButton->SetBackgroundColor(FLinearColor(.17f,.12f,.23f,.8f));
    StorySkipButton->SetToolTipText(FText::FromString(TEXT("跳过本句对白，继续战斗")));
    StorySkipButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SkipStoryDialogue);
    auto* SkipText = WidgetTree->ConstructWidget<UTextBlock>();
    SkipText->SetText(FText::FromString(TEXT("跳过 »")));
    SkipText->SetFont(FEGVisualStyle::BodyFont(16, true));
    SkipText->SetJustification(ETextJustify::Center);
    SkipText->SetVisibility(ESlateVisibility::HitTestInvisible);
    StorySkipButton->AddChild(SkipText);
    auto* SkipSlot = Root->AddChildToCanvas(StorySkipButton);
    SkipSlot->SetAnchors(FAnchors(.79f,.67f));
    SkipSlot->SetAlignment(FVector2D(1.f,0.f));
    SkipSlot->SetOffsets(FMargin(0.f,-45.f,92.f,36.f));
    StorySkipButton->SetVisibility(ESlateVisibility::Collapsed);
    SetRenderOpacity(0.f);
    SetVisibility(ESlateVisibility::Collapsed);
}

void UEGResultDialogueWidget::SetFinalResultVisible(bool bVisible)
{
    if (!BackgroundSlot) return;
    // The final result card occupies 480 logical pixels about screen centre.
    // Keep the voice below it so it does not cover the restart/retry controls.
    BackgroundSlot->SetAnchors(bVisible ? FAnchors(.30f, .5f, .70f, .5f) : FAnchors(.30f, .67f, .70f, .67f));
    BackgroundSlot->SetOffsets(FMargin(0.f, bVisible ? 255.f : 0.f, 0.f, 140.f));
}

void UEGResultDialogueWidget::SetAudioSource(UEGGameAudioComponent* Source)
{
    if (AudioSource.IsValid())
    {
        AudioSource->OnResultDialogueStarted.RemoveAll(this);
        AudioSource->OnResultDialogueEnded.RemoveAll(this);
    }
    AudioSource = Source;
    HideLine();
    if (!IsValid(Source)) return;
    Source->OnResultDialogueStarted.AddUObject(this, &ThisClass::ShowLine);
    Source->OnResultDialogueEnded.AddUObject(this, &ThisClass::HideLine);
    // A reconstructed HUD can attach mid-line; do not delay text until the
    // end of a new fade while the existing recording continues playing.
    if (const FEGMasterVoiceLine* Active = Source->GetActiveResultDialogue()) PresentLine(*Active, false);
}

void UEGResultDialogueWidget::ShowLine(const FEGMasterVoiceLine& Line)
{
    PresentLine(Line, true);
}

void UEGResultDialogueWidget::PresentLine(const FEGMasterVoiceLine& Line, bool bAnimate)
{
    if (!SpeakerText || !DialogueText || Line.Text.IsEmpty()) { HideLine(); return; }
    SpeakerText->SetText(Line.Speaker);
    DialogueText->SetText(Line.Text);
    bFadingIn = bAnimate;
    FadeStartedAt = bAnimate ? FPlatformTime::Seconds() : 0.0;
    SetRenderOpacity(bAnimate ? 0.f : 1.f);
    SetVisibility(AudioSource.IsValid() && AudioSource->IsStoryDialoguePlaying()
        ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::HitTestInvisible);
}

void UEGResultDialogueWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (StorySkipButton)
        StorySkipButton->SetVisibility(AudioSource.IsValid() && AudioSource->IsStoryDialoguePlaying() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (!bFadingIn) return;
    // This UI fade follows Slate's real-time tick even when gameplay is paused;
    // it does not gate, extend or own the audio component's lifetime.
    const float T = FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - FadeStartedAt) / DialogueFadeSeconds), 0.f, 1.f);
    SetRenderOpacity(1.f - (1.f - T) * (1.f - T));
    if (T >= 1.f) bFadingIn = false;
}

void UEGResultDialogueWidget::HideLine()
{
    bFadingIn = false;
    FadeStartedAt = 0.0;
    SetRenderOpacity(0.f);
    SetVisibility(ESlateVisibility::Collapsed);
    if (SpeakerText) SpeakerText->SetText(FText::GetEmpty());
    if (DialogueText) DialogueText->SetText(FText::GetEmpty());
    if (StorySkipButton) StorySkipButton->SetVisibility(ESlateVisibility::Collapsed);
}

void UEGResultDialogueWidget::SkipStoryDialogue()
{
    if (AudioSource.IsValid() && AudioSource->IsStoryDialoguePlaying()) AudioSource->SkipStoryDialogue();
}

void UEGResultDialogueWidget::NativeDestruct()
{
    SetAudioSource(nullptr);
    Super::NativeDestruct();
}
