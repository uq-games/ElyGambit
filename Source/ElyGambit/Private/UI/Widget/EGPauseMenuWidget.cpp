#include "UI/Widget/EGPauseMenuWidget.h"
#include "UI/Widget/EGMainMenuWidget.h"
#include "UI/EGVisualStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Game/EGChessGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

void UEGPauseMenuWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();BuildLayout();
}
void UEGPauseMenuWidget::BuildLayout()
{
 if(!WidgetTree||WidgetTree->RootWidget)return;
 SetIsFocusable(true);
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;Root->SetVisibility(ESlateVisibility::Visible);
 auto* Dim=WidgetTree->ConstructWidget<UBorder>();Dim->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.012f,.007f,.025f,.76f),0.f));
 Dim->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* DimSlot=Root->AddChildToCanvas(Dim);DimSlot->SetAnchors(FAnchors(0,0,1,1));DimSlot->SetOffsets(FMargin(0));
 auto* Panel=WidgetTree->ConstructWidget<UBorder>();
 Panel->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.045f,.022f,.075f,.97f),6.f,FLinearColor(.82f,.55f,.70f,.85f),1.f));Panel->SetPadding(FMargin(25.f,20.f));
 auto* PanelSlot=Root->AddChildToCanvas(Panel);PanelSlot->SetAnchors(FAnchors(.5f,.5f));PanelSlot->SetAlignment(FVector2D(.5f,.5f));PanelSlot->SetSize(FVector2D(360.f,350.f));
 auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Panel->SetContent(Column);
 auto* Title=WidgetTree->ConstructWidget<UTextBlock>();Title->SetText(FText::FromString(TEXT("暂停")));Title->SetFont(FEGVisualStyle::SerifFont(28));
 Title->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.91f,.97f)));Title->SetJustification(ETextJustify::Center);
 Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,0,0,12));
 auto MakeButton=[this,Column](const TCHAR* Name,const TCHAR* Text)
 {
  auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(Name));Button->SetStyle(FEGVisualStyle::PearlButton());
  auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(Text));Label->SetFont(FEGVisualStyle::PearlFont(20));
  Label->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.91f,.97f,1.f)));
  Label->SetJustification(ETextJustify::Center);Label->SetVisibility(ESlateVisibility::HitTestInvisible);Button->SetContent(Label);
  auto* ButtonSlot=Column->AddChildToVerticalBox(Button);ButtonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ButtonSlot->SetPadding(FMargin(0,3));return Button;
 };
 ContinueButton=MakeButton(TEXT("PauseContinue"),TEXT("继续"));SettingsButton=MakeButton(TEXT("PauseSettings"),TEXT("设置"));
 ReturnButton=MakeButton(TEXT("PauseReturnMenu"),TEXT("返回主菜单"));ExitButton=MakeButton(TEXT("PauseExit"),TEXT("退出游戏"));
 ContinueButton->OnClicked.AddDynamic(this,&ThisClass::ContinueClicked);SettingsButton->OnClicked.AddDynamic(this,&ThisClass::SettingsClicked);
 ReturnButton->OnClicked.AddDynamic(this,&ThisClass::ReturnMenuClicked);ExitButton->OnClicked.AddDynamic(this,&ThisClass::ExitClicked);
}
bool UEGPauseMenuWidget::OpenMenu()
{
 if(bMenuOpen)return true;
 auto* World=GetWorld();auto* PC=GetOwningPlayer();auto* Mode=World?World->GetAuthGameMode<AEGChessGameMode>():nullptr;
 if(!World||World->bIsTearingDown||World->IsPaused()||World->GetNetMode()!=NM_Standalone||!IsValid(PC)||!PC->IsLocalController()||!IsValid(Mode))return false;
 bOwnPause=true;const FCanUnpause Request=FCanUnpause::CreateUObject(this,&ThisClass::CanReleasePause);
 PauseMode=Mode;PauseHandle=Request.GetHandle();
 if(!Mode->SetPause(PC,Request)||!World->IsPaused())
 {ReleaseOwnedPause();return false;}
 bMenuOpen=true;AddToViewport(400);
 FInputModeGameAndUI Input;Input.SetHideCursorDuringCapture(false);Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Input);
 PC->bShowMouseCursor=true;if(ContinueButton)ContinueButton->SetUserFocus(PC);return true;
}
void UEGPauseMenuWidget::ReleaseOwnedPause()
{
 const FDelegateHandle Handle=PauseHandle;PauseHandle.Reset();bOwnPause=false;
 if(auto* Mode=PauseMode.Get();Mode&&Handle.IsValid())Mode->ReleasePauseRequest(Handle);
 PauseMode.Reset();
}
void UEGPauseMenuWidget::HandleEscape()
{
 if(!bMenuOpen)return;
 if(Settings&&Settings->IsVisible()){Settings->CloseSettings();return;}
 CloseMenu();
}
void UEGPauseMenuWidget::CloseMenu()
{
 if(!bMenuOpen&&!bOwnPause)return;
 bMenuOpen=false;ReleaseOwnedPause();if(IsInViewport())RemoveFromParent();
}
void UEGPauseMenuWidget::ContinueClicked(){CloseMenu();}
void UEGPauseMenuWidget::SettingsClicked()
{
 if(!bMenuOpen)return;
 if(!Settings&&GetOwningPlayer())
 {
  Settings=CreateWidget<UEGMainMenuSettingsWidget>(GetOwningPlayer(),UEGMainMenuSettingsWidget::StaticClass());
  if(Settings){Settings->OnSettingsClosed.AddUObject(this,&ThisClass::SettingsClosed);Settings->AddToViewport(410);}
 }
 if(Settings)Settings->OpenSettings();
}
void UEGPauseMenuWidget::SettingsClosed()
{if(bMenuOpen&&ContinueButton&&GetOwningPlayer())ContinueButton->SetUserFocus(GetOwningPlayer());}
void UEGPauseMenuWidget::ReturnMenuClicked()
{
 if(!bMenuOpen)return;auto* PC=GetOwningPlayer();CloseMenu();
 if(IsValid(PC)&&PC->GetWorld()&&!PC->GetWorld()->bIsTearingDown)UGameplayStatics::OpenLevel(PC,FName(TEXT("/Game/Assets/Maps/Gameplay/L_ElyMainMenu")));
}
void UEGPauseMenuWidget::ExitClicked()
{
 if(!bMenuOpen)return;auto* PC=GetOwningPlayer();CloseMenu();
 if(IsValid(PC))UKismetSystemLibrary::QuitGame(PC,PC,EQuitPreference::Quit,false);
}
void UEGPauseMenuWidget::NativeDestruct()
{
 bMenuOpen=false;ReleaseOwnedPause();
 if(Settings){Settings->OnSettingsClosed.RemoveAll(this);if(Settings->IsInViewport())Settings->RemoveFromParent();Settings=nullptr;}
 Super::NativeDestruct();
}
