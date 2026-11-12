#include "UI/Widget/EGPieceTestPanel.h"
#include "Game/EGPieceTestGameMode.h"
#include "Game/EGMatchManager.h"
#include "Data/EGHeroDefinition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "UI/EGVisualStyle.h"

namespace
{
 UTextBlock* TestText(UWidgetTree* Tree,const FString& Value,int32 Size)
 {
  auto* Text=Tree->ConstructWidget<UTextBlock>();Text->SetText(FText::FromString(Value));
  // The panel heading is a title; controls, status and hints stay on the body face.
  Text->SetFont(Size>=24?FEGVisualStyle::SerifFont(Size):FEGVisualStyle::BodyFont(Size));
  Text->SetColorAndOpacity(FLinearColor(.97f,.92f,1.f));return Text;
 }
}

UEGPieceTestSelector::UEGPieceTestSelector()
{InitFont(FEGVisualStyle::BodyFont(20));}

void UEGPieceTestPanel::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
 Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 auto* Frame=WidgetTree->ConstructWidget<UBorder>();Frame->SetBrushColor(FLinearColor(.035f,.025f,.065f,.94f));Frame->SetPadding(FMargin(18));
 auto* CanvasSlot=Canvas->AddChildToCanvas(Frame);CanvasSlot->SetAnchors(FAnchors(1,0));CanvasSlot->SetAlignment(FVector2D(1,0));CanvasSlot->SetPosition(FVector2D(-24,24));CanvasSlot->SetAutoSize(true);
 auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Frame->SetContent(Box);
 auto Add=[Box](UWidget* Widget){Box->AddChildToVerticalBox(Widget)->SetPadding(FMargin(0,6));};
 Add(TestText(WidgetTree,TEXT("棋子测试场"),24));
 Add(TestText(WidgetTree,TEXT("测试棋子"),16));
 Selector=WidgetTree->ConstructWidget<UEGPieceTestSelector>(UEGPieceTestSelector::StaticClass(),TEXT("PieceSelector"));
 auto* GM=GetWorld()->GetAuthGameMode<AEGPieceTestGameMode>();
 if(GM)
 {
  for(const auto& Definition:GM->AvailablePieces)Selector->AddOption(Definition?Definition->DisplayName.ToString():TEXT("配置缺失"));
  Selector->SetSelectedIndex(GM->GetSelectedIndex());
 }
 Selector->OnSelectionChanged.AddDynamic(this,&ThisClass::PieceChanged);Add(Selector);
 Status=TestText(WidgetTree,TEXT("准备场景…"),15);Add(Status);
 StartButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("StartBattleButton"));
 StartButton->SetBackgroundColor(FLinearColor(.22f,.10f,.32f));
 StartButton->AddChild(TestText(WidgetTree,TEXT("开始战斗"),20));StartButton->OnClicked.AddDynamic(this,&ThisClass::StartBattle);Add(StartButton);
 auto* Reset=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("ResetSceneButton"));
 Reset->SetBackgroundColor(FLinearColor(.22f,.10f,.32f));
 Reset->AddChild(TestText(WidgetTree,TEXT("重置场景"),20));Reset->OnClicked.AddDynamic(this,&ThisClass::ResetScene);Add(Reset);
 Add(TestText(WidgetTree,TEXT("重置会恢复等级、血量与技能状态"),14));
}

void UEGPieceTestPanel::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
 Super::NativeTick(Geometry,DeltaSeconds);
 if(auto* GM=GetWorld()->GetAuthGameMode<AEGPieceTestGameMode>())
 {
  Status->SetText(FText::FromString(GM->GetTestStatus()));
  StartButton->SetIsEnabled(GM->GetMatch()&&GM->GetMatch()->GetPhase()==EEGMatchPhase::Preparation&&GM->GetBattleUnits().Num()==3);
 }
}
void UEGPieceTestPanel::PieceChanged(FString Item,ESelectInfo::Type SelectionType)
{if(auto* GM=GetWorld()->GetAuthGameMode<AEGPieceTestGameMode>())GM->SelectPiece(Selector->FindOptionIndex(Item));}
void UEGPieceTestPanel::StartBattle()
{if(auto* GM=GetWorld()->GetAuthGameMode<AEGPieceTestGameMode>())GM->EnterCombat();}
void UEGPieceTestPanel::ResetScene()
{if(auto* GM=GetWorld()->GetAuthGameMode<AEGPieceTestGameMode>())GM->ResetTest();}

void AEGPieceTestPlayerController::BeginPlay()
{
 Super::BeginPlay();if(!IsLocalController())return;
 bAutoManageActiveCameraTarget=false;
 for(TActorIterator<ACameraActor> It(GetWorld());It;++It)if(It->ActorHasTag(TEXT("PieceTestCamera"))){SetViewTarget(*It);break;}
 bShowMouseCursor=true;
 Panel=CreateWidget<UEGPieceTestPanel>(this,UEGPieceTestPanel::StaticClass());
 if(Panel)Panel->AddToViewport();
 FInputModeGameAndUI Input;Input.SetHideCursorDuringCapture(false);Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Input);
}
void AEGPieceTestPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{if(Panel)Panel->RemoveFromParent();Panel=nullptr;Super::EndPlay(Reason);}
