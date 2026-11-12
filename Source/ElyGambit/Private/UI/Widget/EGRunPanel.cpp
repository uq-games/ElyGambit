#include "UI/Widget/EGDemoPanel.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/HUD/EGHUD.h"
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"

namespace
{
 UTextBlock* Label(UWidgetTree* T,FName Name,const FString& Value,int32 Size,FLinearColor Color,bool bTitle=false)
 {
  auto* W=T->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
  W->SetText(FText::FromString(Value));
  FSlateFontInfo Info=FEGVisualStyle::PearlFont(Size);
  W->SetFont(Info);W->SetColorAndOpacity(FSlateColor(Color));
  W->SetJustification(ETextJustify::Center);W->SetAutoWrapText(true);
  return W;
 }
 UCanvasPanelSlot* Place(UCanvasPanel* C,UWidget* W,FAnchors A,FMargin O,FVector2D Align=FVector2D::ZeroVector,int32 Z=0)
 {auto* S=C->AddChildToCanvas(W);S->SetAnchors(A);S->SetOffsets(O);S->SetAlignment(Align);S->SetZOrder(Z);return S;}
 UBorder* Overlay(UWidgetTree* T,FName Name)
 {
  auto* B=T->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);
  B->SetBrushColor(FLinearColor(.055f,.035f,.070f,.58f));B->SetPadding(FMargin(0));
  B->SetVisibility(ESlateVisibility::Collapsed);return B;
 }
}

void UEGDemoPanel::BuildBoardLayout(UWidgetTree* T)
{
 auto* Root=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("BoardRunRoot"));
 T->RootWidget=Root;Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

 // ---- final result ----
 auto* Final=Overlay(T,TEXT("BoardFinalResult"));
 Place(Root,Final,FAnchors(0,0,1,1),FMargin(0));
 auto* FC=T->ConstructWidget<UCanvasPanel>();Final->SetContent(FC);
 auto* Frame=T->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BoardFinalFrame"));
 Frame->SetBrush(FEGVisualStyle::PearlPanel());Frame->SetPadding(FMargin(0));
 Place(FC,Frame,FAnchors(.5f,.5f),FMargin(0,0,820,480),FVector2D(.5f,.5f));
 auto* FrameCanvas=T->ConstructWidget<UCanvasPanel>();Frame->SetContent(FrameCanvas);
 Place(FrameCanvas,Label(T,TEXT("Text_RunStatus"),TEXT("本局胜利"),46,EGPearlGold::Plum,true),FAnchors(.5f,0),FMargin(0,58,700,72),FVector2D(.5f,0));
 Place(FrameCanvas,Label(T,TEXT("Text_RunHelp"),TEXT(""),18,EGPearlGold::PlumSoft),FAnchors(.5f,0),FMargin(0,156,700,90),FVector2D(.5f,0));
 auto* Restart=T->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("Button_RunAction"));
 Restart->SetStyle(FEGVisualStyle::PearlButton());
 auto* RestartLabel=Label(T,TEXT("Text_RunAction"),TEXT("重新开始"),24,EGPearlGold::Plum,true);
 RestartLabel->SetAutoWrapText(false);
 Restart->SetContent(RestartLabel);
 Place(FrameCanvas,Restart,FAnchors(.5f,1),FMargin(0,-62,330,74),FVector2D(.5f,1));
}

void UEGDemoPanel::ApplyBoardView(const FEGMatchView& V)
{
 const bool End=V.bHasMatch&&V.Phase==EEGMatchPhase::Finished;
 const APlayerController* PC=GetOwningPlayer();
 const AEGHUD* HUD=PC?PC->GetHUD<AEGHUD>():nullptr;
 const bool bShowFinal=End&&(!HUD||!HUD->ShouldHideFinalResult());
 if(FinalPanel)FinalPanel->SetVisibility(bShowFinal?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(Status)Status->SetText(FText::FromString(V.bLastVictory?TEXT("游戏胜利"):TEXT("游戏失败")));
 if(Help)Help->SetText(V.RunHint);
 if(ActionText)ActionText->SetText(FText::FromString(TEXT("重新开始")));
 if(Action)Action->SetIsEnabled(End&&V.bCanAdvance);
 const bool bRetry=End&&V.bCanRetryStage;
 if(RetryAction){RetryAction->SetVisibility(bRetry?ESlateVisibility::Visible:ESlateVisibility::Collapsed);RetryAction->SetIsEnabled(bRetry);}
 if(Action)if(auto* ActionSlot=Cast<UCanvasPanelSlot>(Action->Slot))ActionSlot->SetPosition(FVector2D(bRetry?180.f:0.f,-62.f));
}

void UEGDemoPanel::RefreshFinalResultGate()
{
 if(bBoardLayout&&Presentation.IsValid())ApplyBoardView(Presentation->GetMatchView());
}
