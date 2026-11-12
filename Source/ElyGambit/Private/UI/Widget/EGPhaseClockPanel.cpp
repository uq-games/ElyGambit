#include "UI/Widget/EGDemoPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/EGRunDefinition.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Types/EGPhaseClock.h"
#include "UI/EGTypography.h"
#include "UI/EGVisualStyle.h"

namespace
{
void ClockPlace(UCanvasPanel* Parent,UWidget* Widget,FMargin Bounds)
{
 auto* CanvasSlot=Parent->AddChildToCanvas(Widget);
 CanvasSlot->SetPosition(FVector2D(Bounds.Left,Bounds.Top));
 CanvasSlot->SetSize(FVector2D(Bounds.Right,Bounds.Bottom));
}
UTextBlock* ClockText(UWidgetTree* Tree,FName Name,int32 Size,EEGFontRole Role)
{
 auto* Text=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
 Text->SetFont(EGFontForRole(Role,Size));Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
 Text->SetJustification(ETextJustify::Center);Text->SetVisibility(ESlateVisibility::HitTestInvisible);
 return Text;
}
}

void UEGDemoPanel::EnsurePhaseClock()
{
 if(WidgetTree->FindWidget(TEXT("PhaseClockRoot")))return;
 auto* Parent=Cast<UCanvasPanel>(WidgetTree->RootWidget);if(!Parent)return;
 auto* Clock=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("PhaseClockRoot"));
 Clock->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* ClockSlot=Parent->AddChildToCanvas(Clock);ClockSlot->SetAnchors(FAnchors(.5f,0.f));
 ClockSlot->SetPosition(FVector2D(-330.f,4.f));ClockSlot->SetSize(FVector2D(660.f,76.f));ClockSlot->SetZOrder(35);
 auto MakeLine=[this,Clock](FName Name,float X,float Pivot)
 {
  auto* Line=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
  Line->SetBrush(FSlateColorBrush(FLinearColor::White));
  Line->SetVisibility(ESlateVisibility::HitTestInvisible);Line->SetRenderTransformPivot(FVector2D(Pivot,.5f));
  ClockPlace(Clock,Line,FMargin(X,0,330,4));return Line;
 };
 PhaseClockLeft=MakeLine(TEXT("PhaseClockLeft"),0.f,1.f);
 PhaseClockRight=MakeLine(TEXT("PhaseClockRight"),330.f,0.f);
 auto* Node=WidgetTree->ConstructWidget<UImage>();Node->SetBrush(FSlateRoundedBoxBrush(FLinearColor(1.f,.88f,.72f),3.f));
 Node->SetVisibility(ESlateVisibility::HitTestInvisible);ClockPlace(Clock,Node,FMargin(327,-1,6,6));
 auto* Texture=FEGVisualStyle::Texture(TEXT("/Game/Assets/UI/Textures/BoardTheme/Panels/T_PhaseHexPlate_GPT20261103"));
 auto MakePlate=[this,Clock,Texture](FName Name,float X)
 {
  auto* Plate=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name);
  if(Texture)
  {
   FSlateBrush PlateBrush;PlateBrush.SetResourceObject(Texture);PlateBrush.DrawAs=ESlateBrushDrawType::Image;
   PlateBrush.SetUVRegion(FBox2D(FVector2D(52.f/2172.f,225.f/724.f),FVector2D(2120.f/2172.f,498.f/724.f)));
   Plate->SetBrush(PlateBrush);
  }
  else Plate->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.08f,.045f,.13f,.85f),5.f,FLinearColor(.8f,.66f,.88f,.8f),1.f));
  Plate->SetVisibility(ESlateVisibility::HitTestInvisible);ClockPlace(Clock,Plate,FMargin(X,14,Name==TEXT("PhaseClockStagePlate")?144.f:136.f,32.f));
 };
 MakePlate(TEXT("PhaseClockStagePlate"),146.f);MakePlate(TEXT("PhaseClockCorruptionPlate"),378.f);
 PhaseClockStage=ClockText(WidgetTree,TEXT("PhaseClockStage"),18,EEGFontRole::Heading);ClockPlace(Clock,PhaseClockStage,FMargin(146,15,144,32));
 PhaseClockNumber=ClockText(WidgetTree,TEXT("PhaseClockNumber"),26,EEGFontRole::Numeric);ClockPlace(Clock,PhaseClockNumber,FMargin(302,11,64,36));
 // "侵蚀度 %d" carries Chinese text, so it stays on the serif CJK face rather than Cinzel.
 PhaseClockCorruption=ClockText(WidgetTree,TEXT("PhaseClockCorruption"),18,EEGFontRole::Heading);ClockPlace(Clock,PhaseClockCorruption,FMargin(378,15,136,32));
}

void UEGDemoPanel::BindPhaseClock()
{
 auto* Source=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 if(PhaseClockSource.Get()!=Source)
 {
  if(PhaseClockSource.IsValid())PhaseClockSource->OnPhaseClockChanged.RemoveDynamic(this,&ThisClass::PhaseClockChanged);
  PhaseClockSource=Source;
  if(Source)Source->OnPhaseClockChanged.AddUniqueDynamic(this,&ThisClass::PhaseClockChanged);
 }
 PhaseClockChanged();
}

void UEGDemoPanel::PhaseClockChanged()
{
 auto* Source=PhaseClockSource.Get();
 auto* Root=WidgetTree?WidgetTree->FindWidget(TEXT("PhaseClockRoot")):nullptr;
 const bool bVisible=Source&&Source->RunDefinition&&Source->RunDefinition->bSevenStageRules&&Source->GetMatch();
 if(Root)Root->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if(!bVisible||!PhaseClockLeft||!PhaseClockRight||!PhaseClockNumber||!PhaseClockStage||!PhaseClockCorruption)return;
 const float Width=GetCachedGeometry().GetLocalSize().X;
 if(Width>0.f)
 {
  const float Fit=FMath::Min(1.f,FMath::Max(.1f,(Width-24.f)/660.f));
  Root->SetRenderTransformPivot(FVector2D::ZeroVector);Root->SetRenderScale(FVector2D(Fit));
  if(auto* RootSlot=Cast<UCanvasPanelSlot>(Root->Slot))RootSlot->SetPosition(FVector2D(-330.f*Fit,4.f));
 }
 const FEGPhaseClockView Clock=Source->GetPhaseClock();
 const bool bBoss=Source->GetMatch()->GetStageNumber()==7&&Clock.Phase==EEGMatchPhase::Combat;
 const TCHAR* Phase=bBoss?(Clock.bOvertime?TEXT("首领狂暴"):TEXT("首领战斗")):Clock.Phase==EEGMatchPhase::Preparation?TEXT("准备阶段"):
  Clock.Phase==EEGMatchPhase::Combat?TEXT("战斗阶段"):
  Clock.Phase==EEGMatchPhase::Settlement?TEXT("结算阶段"):
  Clock.Phase==EEGMatchPhase::Reward?TEXT("刻印阶段"):TEXT("结束阶段");
 const FString Stage(Phase);
 if(PhaseClockStage->GetText().ToString()!=Stage)PhaseClockStage->SetText(FText::FromString(Stage));
 FString Number=TEXT("—");
 if(Clock.bHasCountdown)Number=FString::FromInt(FMath::CeilToInt(FMath::Max(0.f,Clock.RemainingRealSeconds)));
 if(PhaseClockNumber->GetText().ToString()!=Number)PhaseClockNumber->SetText(FText::FromString(Number));
 const FString Corruption=FString::Printf(TEXT("侵蚀度 %d"),Source->GetMatch()->GetCorruption());
 if(PhaseClockCorruption->GetText().ToString()!=Corruption)PhaseClockCorruption->SetText(FText::FromString(Corruption));
 const float Fraction=Clock.bHasCountdown?FMath::Clamp(Clock.Normalized,0.f,1.f):1.f;
 const bool bActiveClock=Clock.bHasCountdown;
 const float Current=static_cast<float>(PhaseClockLeft->GetRenderTransform().Scale.X);
 const float DeltaSeconds=GetWorld()?GetWorld()->GetDeltaSeconds():0.f;
 const float DisplayFraction=!bActiveClock||Fraction>=Current?Fraction:FMath::FInterpTo(Current,Fraction,DeltaSeconds,18.f);
 PhaseClockLeft->SetRenderScale(FVector2D(DisplayFraction,1.f));PhaseClockRight->SetRenderScale(FVector2D(DisplayFraction,1.f));
 PhaseClockLeft->SetRenderOpacity(bActiveClock?1.f:.35f);PhaseClockRight->SetRenderOpacity(bActiveClock?1.f:.35f);
 const FLinearColor LineColor=Clock.bOvertime?FLinearColor(1.f,.52f,.32f):FLinearColor(.84f,.7f,.82f,.95f);
 PhaseClockLeft->SetColorAndOpacity(LineColor);PhaseClockRight->SetColorAndOpacity(LineColor);
}
