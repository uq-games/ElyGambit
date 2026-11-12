#include "UI/Widget/EGVictoryEndingWidget.h"
#include "Presentation/EGVictoryEndingPresentation.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGGradientSurface.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "MediaTexture.h"
#include "MediaPlayer.h"

namespace
{
 UCanvasPanelSlot* Place(UCanvasPanel* Root,UWidget* Child,FAnchors Anchors,FMargin Offsets,int32 Z)
 {
  auto* Slot=Root->AddChildToCanvas(Child); Slot->SetAnchors(Anchors); Slot->SetOffsets(Offsets); Slot->SetZOrder(Z); return Slot;
 }
 UTextBlock* Label(UWidgetTree* Tree,int32 Size)
 {
  auto* Text=Tree->ConstructWidget<UTextBlock>(); Text->SetFont(FEGVisualStyle::BodyFont(Size,true));
  Text->SetColorAndOpacity(FSlateColor(FLinearColor::White)); Text->SetShadowOffset(FVector2D(1,1));
  Text->SetShadowColorAndOpacity(FLinearColor(0,0,0,.95f)); Text->SetAutoWrapText(true);
  Text->SetVisibility(ESlateVisibility::HitTestInvisible); return Text;
 }
 UButton* Button(UWidgetTree* Tree,const TCHAR* Text)
 {
  auto* B=Tree->ConstructWidget<UButton>(); B->SetStyle(FEGVisualStyle::Action());
  auto* L=Label(Tree,16); L->SetText(FText::FromString(Text));
  L->SetAutoWrapText(false);L->SetJustification(ETextJustify::Center); B->AddChild(L); return B;
 }
}

void UEGVictoryEndingWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(); WidgetTree->RootWidget=Root;
 auto* Surface=WidgetTree->ConstructWidget<UButton>(); Surface->SetStyle(FEGVisualStyle::Ghost());
 Surface->OnClicked.AddUniqueDynamic(this,&ThisClass::Advance); Place(Root,Surface,FAnchors(0,0,1,1),FMargin(0),0);
 VideoBackground=WidgetTree->ConstructWidget<UBorder>();VideoBackground->SetBrushColor(FLinearColor::Black);
 VideoBackground->SetVisibility(ESlateVisibility::Collapsed);Place(Root,VideoBackground,FAnchors(0,0,1,1),FMargin(0),1);
 VideoImage=WidgetTree->ConstructWidget<UImage>(); VideoImage->SetVisibility(ESlateVisibility::Collapsed);
 auto* VideoSlot=Place(Root,VideoImage,FAnchors(.5f,.5f),FMargin(0,0,1,1),2);VideoSlot->SetAlignment(FVector2D(.5f,.5f));
 DialogueBackdrop=WidgetTree->ConstructWidget<UEGGradientSurface>();
 DialogueBackdrop->TopColor=FLinearColor(0,0,0,0);DialogueBackdrop->BottomColor=FLinearColor(.012f,.006f,.02f,.64f);
 DialogueBackdrop->bSmoothFade=true;DialogueBackdrop->SetVisibility(ESlateVisibility::Collapsed);
 Place(Root,DialogueBackdrop,FAnchors(0,.62f,1,1),FMargin(0),3);
 SpeakerText=Label(WidgetTree,22); Place(Root,SpeakerText,FAnchors(.15f,.72f,.85f,.72f),FMargin(0,0,0,38),4);
 SubtitleText=Label(WidgetTree,25); Place(Root,SubtitleText,FAnchors(.15f,.77f,.85f,.77f),FMargin(0,0,0,110),4);
 ErrorText=Label(WidgetTree,22); ErrorText->SetJustification(ETextJustify::Center);
 Place(Root,ErrorText,FAnchors(.18f,.42f,.82f,.42f),FMargin(0,0,0,90),5);
 SegmentButton=Button(WidgetTree,TEXT("跳过片段")); SegmentButton->OnClicked.AddUniqueDynamic(this,&ThisClass::SkipSegment);
 Place(Root,SegmentButton,FAnchors(1,0),FMargin(-360,24,170,48),6);
 auto* All=Button(WidgetTree,TEXT("跳过全部")); All->OnClicked.AddUniqueDynamic(this,&ThisClass::SkipAll);
 Place(Root,All,FAnchors(1,0),FMargin(-180,24,170,48),6);
 RetryButton=Button(WidgetTree,TEXT("重试")); RetryButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Retry);
 Place(Root,RetryButton,FAnchors(.5f,.55f),FMargin(-75,0,150,48),6);
 TransitionVeil=WidgetTree->ConstructWidget<UBorder>(); TransitionVeil->SetBrushColor(FLinearColor::Black);
 TransitionVeil->SetVisibility(ESlateVisibility::HitTestInvisible); TransitionVeil->SetRenderOpacity(0.f);
 Place(Root,TransitionVeil,FAnchors(0,0,1,1),FMargin(0),7);
 ClearContent();
}
void UEGVictoryEndingWidget::SetTransitionOpacity(float Opacity)
{if(TransitionVeil)TransitionVeil->SetRenderOpacity(FMath::Clamp(Opacity,0.f,1.f));}
void UEGVictoryEndingWidget::BindEnding(AEGVictoryEndingPresentation* InEnding){Ending=InEnding;}
void UEGVictoryEndingWidget::ShowDialogue(const FText& Speaker,const FText& Line,bool bAnimate)
{
 if(SpeakerText)SpeakerText->SetText(Speaker);
 FullText=Line.ToString(); RevealedCharacters=bAnimate?0.f:static_cast<float>(FullText.Len()); bTyping=bAnimate&&!FullText.IsEmpty();
 bHasDialogue=!Line.IsEmpty();
 if(DialogueBackdrop)DialogueBackdrop->SetVisibility(bHasDialogue&&!CurrentVideo?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if(SubtitleText)SubtitleText->SetText(bTyping?FText::GetEmpty():Line);
}
void UEGVictoryEndingWidget::ShowVideo(UMediaTexture* Texture)
{
 CurrentVideo=Texture;
 if(VideoBackground)VideoBackground->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if(DialogueBackdrop)DialogueBackdrop->SetVisibility(bHasDialogue&&!Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if(!VideoImage)return; FSlateBrush Brush; Brush.SetResourceObject(Texture); Brush.DrawAs=ESlateBrushDrawType::Image;
 VideoImage->SetBrush(Brush);
 // NativeTick reveals the picture only after the media dimensions are known.
 VideoImage->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGVictoryEndingWidget::ClearContent()
{
 FullText.Reset(); RevealedCharacters=0; bTyping=false;bHasDialogue=false;
 if(SpeakerText)SpeakerText->SetText(FText::GetEmpty()); if(SubtitleText)SubtitleText->SetText(FText::GetEmpty());
 if(ErrorText)ErrorText->SetText(FText::GetEmpty()); if(RetryButton)RetryButton->SetVisibility(ESlateVisibility::Collapsed);
 if(SegmentButton)SegmentButton->SetVisibility(ESlateVisibility::Visible); ShowVideo(nullptr);
}
void UEGVictoryEndingWidget::ShowError(const FText& Message)
{
 ClearContent(); if(ErrorText)ErrorText->SetText(Message); if(RetryButton)RetryButton->SetVisibility(ESlateVisibility::Visible);
}
bool UEGVictoryEndingWidget::RevealOrIsComplete()
{
 if(!bTyping)return true; bTyping=false; RevealedCharacters=FullText.Len();
 if(SubtitleText)SubtitleText->SetText(FText::FromString(FullText)); return false;
}
void UEGVictoryEndingWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
 Super::NativeTick(Geometry,DeltaSeconds);
 if(CurrentVideo&&VideoImage)
 {
  const int32 Width=CurrentVideo->GetWidth(),Height=CurrentVideo->GetHeight();
  const FVector2D Available=Geometry.GetLocalSize();
  const UMediaPlayer* Player=CurrentVideo->GetMediaPlayer();
  if(Player&&Player->IsReady()&&Width>0&&Height>0&&Available.X>0&&Available.Y>0)
  {
   const double Scale=FMath::Min(Available.X/double(Width),Available.Y/double(Height));
   const FVector2D Size(Width*Scale,Height*Scale);
   if(auto* VideoSlot=Cast<UCanvasPanelSlot>(VideoImage->Slot))if(!VideoSlot->GetSize().Equals(Size,.01))VideoSlot->SetSize(Size);
   VideoImage->SetVisibility(ESlateVisibility::HitTestInvisible);
  }
  else VideoImage->SetVisibility(ESlateVisibility::Collapsed);
 }
 if(!bTyping)return;
 RevealedCharacters=FMath::Min(static_cast<float>(FullText.Len()),RevealedCharacters+FMath::Max(0.f,DeltaSeconds)*26.f);
 if(SubtitleText)SubtitleText->SetText(FText::FromString(FullText.Left(FMath::FloorToInt(RevealedCharacters))));
 if(RevealedCharacters>=FullText.Len())bTyping=false;
}
void UEGVictoryEndingWidget::Advance(){if(Ending.IsValid())Ending->AdvanceDialogue();}
void UEGVictoryEndingWidget::SkipSegment(){if(Ending.IsValid())Ending->SkipSegment();}
void UEGVictoryEndingWidget::SkipAll(){if(Ending.IsValid())Ending->SkipAll();}
void UEGVictoryEndingWidget::Retry(){if(Ending.IsValid())Ending->Retry();}
