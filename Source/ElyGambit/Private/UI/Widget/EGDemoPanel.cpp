#include "UI/Widget/EGDemoPanel.h"
#include "Components/HorizontalBoxSlot.h"
#include "UI/EGVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Border.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/HorizontalBox.h"

#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Font.h"
#include "TimerManager.h"
#include "Misc/PackageName.h"

namespace
{
 UTextBlock* Text(UWidgetTree* Tree,const FString& Value,int32 Size)
 {
  auto* T=Tree->ConstructWidget<UTextBlock>();
  T->SetText(FText::FromString(Value));T->SetAutoWrapText(false);
  FSlateFontInfo Font(LoadObject<UFont>(nullptr,TEXT("/Game/Assets/UI/Fonts/F_UI_CJK")),Size);
  T->SetFont(Font);T->SetColorAndOpacity(FSlateColor(FLinearColor(.96f,.92f,1.f)));
  return T;
 }
 void Add(UVerticalBox* Box,UWidget* Widget,float Padding=6.f)
 {
  UWidget* Content=Widget;
  if(Widget->IsA<UButton>())
  {
   auto* Frame=CastChecked<UWidgetTree>(Box->GetOuter())->ConstructWidget<USizeBox>();
   Frame->SetHeightOverride(46.f);Frame->AddChild(Widget);Content=Frame;
  }
  auto* Slot=Box->AddChildToVerticalBox(Content);Slot->SetPadding(FMargin(Padding));
 }
 UButton* Button(UWidgetTree* Tree,const FString& Value,UTextBlock*& Label)
 {
  UButton* B=Tree->ConstructWidget<UButton>();
  FButtonStyle Style;
  const FLinearColor Edge(.018f,.024f,.025f,1.f);
  Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.8f,.79f,.66f,1.f),3.f,Edge,3.f));
  Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.96f,.88f,.63f,1.f),3.f,FLinearColor(.68f,.47f,.19f,1.f),3.f));
  Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.62f,.57f,.4f,1.f),3.f,Edge,3.f));
  Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.3f,.32f,.29f,1.f),3.f,Edge,2.f));
  Style.NormalPadding=FMargin(8,2);Style.PressedPadding=FMargin(8,3,8,1);
  B->SetStyle(Style);B->SetBackgroundColor(FLinearColor::White);
  Label=Text(Tree,Value,18);Label->SetColorAndOpacity(Edge);Label->SetAutoWrapText(false);Label->SetJustification(ETextJustify::Center);B->AddChild(Label);
  return B;
 }
 bool Over(const UWidget* W,FVector2D P)
 {return W&&W->IsVisible()&&W->GetCachedGeometry().IsUnderLocation(P);}
}

void UEGDemoPanel::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 if(WidgetTree->FindWidget(TEXT("BoardFinalResult")))
 {
  bBoardLayout=true;
  FinalPanel=Cast<UBorder>(WidgetTree->FindWidget(TEXT("BoardFinalResult")));
  Status=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Text_RunStatus")));
  Help=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Text_RunHelp")));
  Action=Cast<UButton>(WidgetTree->FindWidget(TEXT("Button_RunAction")));
  ActionText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Text_RunAction")));
  if(Action)Action->OnClicked.AddUniqueDynamic(this,&ThisClass::OnAction);
  EnsureRetryButton();
  return;
 }
 if(WidgetTree->RootWidget)
 {
  Status=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Text_RunStatus")));Help=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Text_RunHelp")));
  ActionText=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("Text_RunAction")));Action=Cast<UButton>(WidgetTree->FindWidget(TEXT("Button_RunAction")));
  if(!Status||!Help||!ActionText||!Action){UE_LOG(LogTemp,Error,TEXT("Run panel layout is missing required named widgets."));Status=nullptr;return;}
  Action->OnClicked.AddUniqueDynamic(this,&ThisClass::OnAction);EnsureRetryButton();return;
 }
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
 Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();
 auto* Background=WidgetTree->ConstructWidget<UBorder>();
 Background->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.018f,.022f,.055f,.94f),8.f,FLinearColor(.38f,.28f,.45f,.65f),1.f));Background->SetPadding(FMargin(12));Background->AddChild(Box);
 auto* Top=Root->AddChildToCanvas(Background);Top->SetAnchors(FAnchors(0,0));Top->SetPosition(FVector2D(28,122));Top->SetSize(FVector2D(420,242));
 Status=Text(WidgetTree,TEXT("往世的涟漪  两关试炼"),17);Add(Box,Status);
 Help=Text(WidgetTree,TEXT("布阵后开始作战"),14);Help->SetWrapTextAt(372.f);Help->SetColorAndOpacity(FSlateColor(FLinearColor(.67f,.62f,.77f,1.f)));Add(Box,Help);
 UTextBlock* ActionLabel=nullptr;Action=Button(WidgetTree,TEXT("开始作战"),ActionLabel);ActionText=ActionLabel;Add(Box,Action);
 Action->OnClicked.AddDynamic(this,&ThisClass::OnAction);
 EnsureRetryButton();

}

void UEGDemoPanel::EnsureRetryButton()
{
 if(!WidgetTree||!Action)return;
 RetryAction=Cast<UButton>(WidgetTree->FindWidget(TEXT("Button_RetryCurrentStage")));
 if(!RetryAction)
 {
  if(auto* Canvas=Cast<UCanvasPanel>(Action->GetParent()))
  {
   RetryAction=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("Button_RetryCurrentStage"));
   RetryAction->SetStyle(FEGVisualStyle::PearlButton());
   auto* Label=Text(WidgetTree,TEXT("重试本关"),24);Label->SetColorAndOpacity(FSlateColor(FLinearColor(.15f,.10f,.18f)));
   Label->SetJustification(ETextJustify::Center);RetryAction->SetContent(Label);
   auto* RetrySlot=Canvas->AddChildToCanvas(RetryAction);RetrySlot->SetAnchors(FAnchors(.5f,1));
   RetrySlot->SetAlignment(FVector2D(.5f,1));RetrySlot->SetPosition(FVector2D(-180,-62));RetrySlot->SetSize(FVector2D(330,74));
  }
  else
  {
   auto* Box=Cast<UVerticalBox>(Action->GetParent());
   if(!Box&&Action->GetParent())Box=Cast<UVerticalBox>(Action->GetParent()->GetParent());
   if(Box)
   {
    UTextBlock* Label=nullptr;RetryAction=Button(WidgetTree,TEXT("重试本关"),Label);
    RetryAction->Rename(TEXT("Button_RetryCurrentStage"));Add(Box,RetryAction);
   }
  }
 }
 if(RetryAction){RetryAction->SetVisibility(ESlateVisibility::Collapsed);RetryAction->OnClicked.AddUniqueDynamic(this,&ThisClass::OnRetry);}
}

void UEGDemoPanel::SetPresentationController(UEGOverlayWidgetController* Controller)
{
 if(Presentation.IsValid()){Presentation->OnMatchViewChanged.RemoveDynamic(this,&ThisClass::ApplyMatchView);Presentation->OnBattleClockViewChanged.RemoveDynamic(this,&ThisClass::ApplyClockHint);}
 Presentation=Controller;
 if(Controller){Controller->OnMatchViewChanged.AddUniqueDynamic(this,&ThisClass::ApplyMatchView);Controller->OnBattleClockViewChanged.AddUniqueDynamic(this,&ThisClass::ApplyClockHint);ApplyMatchView(Controller->GetMatchView());}
}
void UEGDemoPanel::NativeConstruct(){Super::NativeConstruct();bStigmataPanelConstructed=true;EnsureStigmaPanel();EnsurePhaseClock();BindPhaseClock();BindStigmataFeedback();if(Action)Action->OnClicked.AddUniqueDynamic(this,&ThisClass::OnAction);if(RetryAction)RetryAction->OnClicked.AddUniqueDynamic(this,&ThisClass::OnRetry);SetPresentationController(Presentation.Get());}
void UEGDemoPanel::SetContextPanelsOpen(bool bOpen)
{
 bContextPanelsOpen=bOpen;
 if(bBoardLayout)return;
 if(Status)Status->SetVisibility(bOpen?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
 if(Help)Help->SetVisibility(bOpen?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
 if(Action){Action->SetVisibility(bOpen?ESlateVisibility::Collapsed:ESlateVisibility::Visible);Action->SetIsEnabled(!bOpen&&Presentation.IsValid()&&Presentation->GetMatchView().bCanAdvance);}
 if(RetryAction){const bool bShow=!bOpen&&Presentation.IsValid()&&Presentation->GetMatchView().bCanRetryStage;RetryAction->SetVisibility(bShow?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
}

void UEGDemoPanel::ApplyClockHint(const FText& Hint){if(Help&&!bBoardLayout)Help->SetText(Hint);}
void UEGDemoPanel::NativeDestruct()
{
 if(Presentation.IsValid()){Presentation->OnMatchViewChanged.RemoveDynamic(this,&ThisClass::ApplyMatchView);Presentation->OnBattleClockViewChanged.RemoveDynamic(this,&ThisClass::ApplyClockHint);}
 if(Action)Action->OnClicked.RemoveDynamic(this,&ThisClass::OnAction);
 if(RetryAction)RetryAction->OnClicked.RemoveDynamic(this,&ThisClass::OnRetry);
 if(StigmataFeedbackSource.IsValid())StigmataFeedbackSource->OnStigmataTriggered.RemoveDynamic(this,&ThisClass::StigmataTriggered);
 if(PhaseClockSource.IsValid())PhaseClockSource->OnPhaseClockChanged.RemoveDynamic(this,&ThisClass::PhaseClockChanged);
 PhaseClockSource.Reset();
 StigmataFeedbackSource.Reset();
 ClearStigmataFeedback();
 bStigmataPanelConstructed=false;ForceHideStigmaSkip();
 Super::NativeDestruct();
}
void UEGDemoPanel::ApplyMatchView(const FEGMatchView& View)
{
 RefreshStigmataCollection();
 RefreshStigmataResults();
 if(bBoardLayout){ApplyBoardView(View);return;}
 if(!Status)return;
 Status->SetText(View.RunStatus);Help->SetText(View.RunHint);ActionText->SetText(View.RunAction);
 Action->SetIsEnabled(View.bCanAdvance&&!bContextPanelsOpen);
 if(RetryAction){const bool bShow=View.bCanRetryStage&&!bContextPanelsOpen;RetryAction->SetVisibility(bShow?ESlateVisibility::Visible:ESlateVisibility::Collapsed);RetryAction->SetIsEnabled(bShow);}
}
bool UEGDemoPanel::BlocksPoint(FVector2D Position) const
{
 if((StigmaPanel&&StigmaPanel->IsVisible())||Over(StigmataCollectionPanel,Position))return true;
 for(const auto& Row:StigmataRows)if(Over(Row,Position))return true;
 return (bBoardLayout&&FinalPanel&&FinalPanel->IsVisible())||(!bBoardLayout&&(Over(Action,Position)||Over(RetryAction,Position)||Over(Status,Position)||Over(Help,Position)));
}
void UEGDemoPanel::OnAction(){if((bBoardLayout||!bContextPanelsOpen)&&Presentation.IsValid())Presentation->RequestAdvanceRun();}
void UEGDemoPanel::OnRetry(){if((bBoardLayout||!bContextPanelsOpen)&&Presentation.IsValid())Presentation->RequestRetryStage();}

