#include "UI/Widget/EGElysiaEntryMovieWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "MediaTexture.h"

void UEGElysiaEntryMovieWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
 auto* Background=WidgetTree->ConstructWidget<UBorder>();
 Background->SetBrush(FSlateColorBrush(FLinearColor::Black));Background->SetPadding(FMargin(0.f));
 auto* BackgroundSlot=Root->AddChildToCanvas(Background);
 BackgroundSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f));BackgroundSlot->SetOffsets(FMargin(0.f));
 auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);
 auto* FitSlot=Root->AddChildToCanvas(Fit);
 FitSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f));FitSlot->SetOffsets(FMargin(0.f));
 auto* VideoSize=WidgetTree->ConstructWidget<USizeBox>();
 VideoSize->SetWidthOverride(1920.f);VideoSize->SetHeightOverride(1080.f);
 MovieImage=WidgetTree->ConstructWidget<UImage>();MovieImage->SetDesiredSizeOverride(FVector2D(1920.f,1080.f));
 VideoSize->AddChild(MovieImage);Fit->AddChild(VideoSize);
 SetVisibility(ESlateVisibility::HitTestInvisible);SetRenderOpacity(0.f);
}

void UEGElysiaEntryMovieWidget::SetMovieTexture(UMediaTexture* Texture)
{
 if(MovieImage)MovieImage->SetBrushResourceObject(Texture);
}

void UEGElysiaEntryMovieWidget::SetMovieVisible(bool bVisible)
{
 SetRenderOpacity(bVisible?1.f:0.f);
}

void UEGElysiaEntryMovieWidget::NativeDestruct()
{
 if(MovieImage)MovieImage->SetBrushResourceObject(nullptr);
 Super::NativeDestruct();
}
