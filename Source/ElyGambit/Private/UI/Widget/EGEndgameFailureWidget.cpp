#include "UI/Widget/EGEndgameFailureWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/AudioComponent.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableTextBox.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Texture2D.h"
#include "Game/EGDemoGameMode.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Sound/SoundBase.h"
#include "UI/EGVisualStyle.h"
#include "UI/HUD/EGHUD.h"

namespace
{
 UCanvasPanelSlot* Place(UCanvasPanel* Parent,UWidget* Child,FAnchors Anchors,FMargin Offsets,FVector2D Align=FVector2D::ZeroVector)
 {
  auto* Slot=Parent->AddChildToCanvas(Child);Slot->SetAnchors(Anchors);Slot->SetOffsets(Offsets);Slot->SetAlignment(Align);return Slot;
 }
}

UEGEndgameFailureWidget::UEGEndgameFailureWidget(const FObjectInitializer& Initializer)
 :Super(Initializer),BackgroundTexture(FSoftObjectPath(TEXT("/Game/Assets/UI/Textures/Endings/T_CorruptionFailure20261110.T_CorruptionFailure20261110")))
{
 SetIsFocusable(true);
 OpeningVoice=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/Shared/Audio/Endings20261110/SW_Corruption_Eternal_PV.SW_Corruption_Eternal_PV")));
 SuccessVoice=TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/Shared/Audio/Endings20261110/SW_Elysia_Disappointed_PV.SW_Elysia_Disappointed_PV")));
 SuccessSubtitle=FText::FromString(TEXT("抱歉，得让你失望啦。"));
 WrongAnswerVoices.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/Shared/Audio/Endings20261110/SW_Corruption_FailureWrong_01.SW_Corruption_FailureWrong_01"))));
 WrongAnswerVoices.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(TEXT("/Game/Assets/Shared/Audio/Endings20261110/SW_Corruption_FailureWrong_02.SW_Corruption_FailureWrong_02"))));
}

void UEGEndgameFailureWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
 Background=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("FailureBackground"));
 Background->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Root,Background,FAnchors(0,0,1,1),FMargin(0));
 Background->SetRenderTransformPivot(FVector2D(.5f,.5f));
 GlitchLineA=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FailureGlitchA"));
 GlitchLineA->SetBrushColor(FLinearColor(.44f,.12f,.45f,1.f));GlitchLineA->SetRenderOpacity(0.f);
 GlitchLineA->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Root,GlitchLineA,FAnchors(0,.32f,1,.32f),FMargin(0,0,0,3));
 GlitchLineB=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FailureGlitchB"));
 GlitchLineB->SetBrushColor(FLinearColor(.25f,.10f,.42f,1.f));GlitchLineB->SetRenderOpacity(0.f);
 GlitchLineB->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Root,GlitchLineB,FAnchors(0,.55f,1,.55f),FMargin(0,0,0,5));
 auto* Shade=WidgetTree->ConstructWidget<UBorder>();Shade->SetBrushColor(FLinearColor(.012f,.004f,.025f,.52f));
 Shade->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Root,Shade,FAnchors(0,0,1,1),FMargin(0));

 OpeningDialogue=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FailureOpeningDialogue"));
 OpeningDialogue->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.035f,.008f,.055f,.88f),4.f,FLinearColor(.58f,.25f,.68f,.75f),1.f));
 // The text must receive the full inner width. Center-aligning the child here
 // makes auto-wrap use its old desired width when the spoken line changes.
 OpeningDialogue->SetPadding(FMargin(16.f,8.f));OpeningDialogue->SetHorizontalAlignment(HAlign_Fill);OpeningDialogue->SetVerticalAlignment(VAlign_Center);
 OpeningLine=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("FailureOpeningLine"));
 OpeningLine->SetText(FText::FromString(TEXT("我会……永远存在！")));OpeningLine->SetFont(FEGVisualStyle::BodyFont(21));
 OpeningLine->SetJustification(ETextJustify::Center);OpeningLine->SetAutoWrapText(true);OpeningLine->SetColorAndOpacity(FSlateColor(FLinearColor(.97f,.88f,1.f,1.f)));
 OpeningLine->SetVisibility(ESlateVisibility::HitTestInvisible);OpeningDialogue->SetContent(OpeningLine);
 auto* DialogueSlot=Place(Root,OpeningDialogue,FAnchors(.5f,.72f),FMargin(0,-140,620,112),FVector2D(.5f,.5f));
 // Keep the spoken success line visible during the full-screen fade.
 DialogueSlot->SetZOrder(1);
 OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);

 InputFrame=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FailureInputFrame"));
 InputFrame->SetPadding(FMargin(12,10));InputFrame->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.035f,.008f,.055f,.94f),2.f,FLinearColor(.58f,.25f,.68f,.8f),1.f));
 Place(Root,InputFrame,FAnchors(.5f,.72f),FMargin(0,0,500,72),FVector2D(.5f,.5f));
 Input=WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(),TEXT("FailureInput"));
 // Slate normally clears focus after Enter, after the commit callback returns.
 // Keep it in this widget so Escape remains available during the transition.
 Input->SetClearKeyboardFocusOnCommit(false);
 FEditableTextBoxStyle InputStyle=Input->GetWidgetStyle();
 InputStyle.SetFont(FEGVisualStyle::BodyFont(24));
 InputStyle.SetForegroundColor(FSlateColor(FLinearColor(.95f,.75f,1.f,1.f)));
 InputStyle.SetFocusedForegroundColor(FSlateColor(FLinearColor(.95f,.75f,1.f,1.f)));
 InputStyle.SetReadOnlyForegroundColor(FSlateColor(FLinearColor(.95f,.75f,1.f,1.f)));
 const FSlateBrush EmptyBackground=FEGVisualStyle::Rounded(FLinearColor::Transparent,0.f);
 InputStyle.SetBackgroundImageNormal(EmptyBackground).SetBackgroundImageHovered(EmptyBackground).SetBackgroundImageFocused(EmptyBackground).SetBackgroundImageReadOnly(EmptyBackground);
 Input->SetWidgetStyle(InputStyle);Input->SetHintText(FText::GetEmpty());Input->SetIsEnabled(false);
 Input->OnTextChanged.AddUniqueDynamic(this,&ThisClass::InputChanged);
 Input->OnTextCommitted.AddUniqueDynamic(this,&ThisClass::InputCommitted);
 InputFrame->SetContent(Input);

 ErasingText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("FailureErasingText"));
 ErasingText->SetFont(FEGVisualStyle::BodyFont(24));ErasingText->SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.75f,1.f,1.f)));
 ErasingText->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Root,ErasingText,FAnchors(.5f,.72f),FMargin(-224,0,440,40),FVector2D(0,.5f));

 SubmitButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("FailureSubmit"));
 SubmitButton->SetStyle(FEGVisualStyle::Ghost());SubmitButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Submit);
 // Draw the return arrow as shapes: the bundled CJK face does not contain U+21B5.
 auto* EnterIcon=WidgetTree->ConstructWidget<UCanvasPanel>();EnterIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto AddStroke=[this,EnterIcon](FVector2D Position,FVector2D Size,float Angle)
 {
  auto* Stroke=WidgetTree->ConstructWidget<UImage>();
  Stroke->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.95f,.75f,1.f,1.f),1.f));
  Stroke->SetVisibility(ESlateVisibility::HitTestInvisible);Stroke->SetRenderTransformPivot(FVector2D(.5f,.5f));
  Stroke->SetRenderTransformAngle(Angle);Place(EnterIcon,Stroke,FAnchors(0,0),FMargin(Position.X,Position.Y,Size.X,Size.Y));
 };
 AddStroke(FVector2D(29,11),FVector2D(3,21),0.f);
 AddStroke(FVector2D(11,29),FVector2D(21,3),0.f);
 AddStroke(FVector2D(8,24),FVector2D(13,3),-45.f);
 AddStroke(FVector2D(8,34),FVector2D(13,3),45.f);
 SubmitButton->SetContent(EnterIcon);SubmitButton->SetIsEnabled(false);
 Place(Root,SubmitButton,FAnchors(.5f,.72f),FMargin(280,0,54,54),FVector2D(.5f,.5f));

 Hint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("FailureHint"));
 Hint->SetFont(FEGVisualStyle::BodyFont(21));Hint->SetJustification(ETextJustify::Center);
 Hint->SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.8f,1.f,1.f)));Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Root,Hint,FAnchors(.5f,.72f),FMargin(0,62,600,40),FVector2D(.5f,0));
 SuccessVeil=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FailureSuccessVeil"));
 SuccessVeil->SetBrushColor(FLinearColor(.005f,0.f,.01f,1.f));SuccessVeil->SetRenderOpacity(0.f);
 SuccessVeil->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Root,SuccessVeil,FAnchors(0,0,1,1),FMargin(0));

 ExitMenu=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("FailureExitMenu"));
 ExitMenu->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.025f,.01f,.04f,.97f),4.f,FLinearColor(.65f,.38f,.7f,1.f),1.f));
 ExitMenu->SetPadding(FMargin(20));ExitMenu->SetVisibility(ESlateVisibility::Collapsed);
 Place(Root,ExitMenu,FAnchors(.5f,.5f),FMargin(0,0,340,248),FVector2D(.5f,.5f));
 auto* MenuItems=WidgetTree->ConstructWidget<UVerticalBox>();ExitMenu->SetContent(MenuItems);
 auto MakeButton=[this,MenuItems](const TCHAR* Name,const TCHAR* Text)
 {
  auto* Button=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),FName(Name));Button->SetStyle(FEGVisualStyle::PearlButton());
  auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(Text));
  Label->SetFont(FEGVisualStyle::BodyFont(21));Label->SetColorAndOpacity(FSlateColor(FLinearColor(.97f,.91f,1.f,1.f)));
  Label->SetJustification(ETextJustify::Center);
  if(auto* LabelSlot=Cast<UButtonSlot>(Button->AddChild(Label)))
  {LabelSlot->SetHorizontalAlignment(HAlign_Center);LabelSlot->SetVerticalAlignment(VAlign_Center);}
  auto* ItemSlot=MenuItems->AddChildToVerticalBox(Button);ItemSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));ItemSlot->SetPadding(FMargin(0,4));
  return Button;
 };
 ContinueButton=MakeButton(TEXT("FailureContinue"),TEXT("继续输入"));
 ReturnButton=MakeButton(TEXT("FailureReturnMenu"),TEXT("返回主菜单"));
 ExitButton=MakeButton(TEXT("FailureExitGame"),TEXT("退出游戏"));
}

void UEGEndgameFailureWidget::NativeConstruct()
{
 Super::NativeConstruct();
 if(Input){Input->OnTextChanged.AddUniqueDynamic(this,&ThisClass::InputChanged);Input->OnTextCommitted.AddUniqueDynamic(this,&ThisClass::InputCommitted);}
 if(SubmitButton)SubmitButton->OnClicked.AddUniqueDynamic(this,&ThisClass::Submit);
 if(ContinueButton)ContinueButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ContinueInput);
 if(ReturnButton)ReturnButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ReturnToMenu);
 if(ExitButton)ExitButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ExitGame);
}

void UEGEndgameFailureWidget::StartFailure()
{
 if(bActive)return;
 auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 if(!Mode||!Mode->CanRestoreEndgameCheckpoint()){RemoveFromParent();return;}
 bActive=true;bSuccess=false;bInputReady=false;bExitMenuOpen=false;WrongCount=0;NextVoice=0;StartedAt=FPlatformTime::Seconds();OpeningStartedAt=StartedAt;
 bHintFocusSet=false;bOpeningVoiceStarted=false;bOpeningDialogueActive=false;WrongFeedbackAt=-1.0;LastLayoutWidth=-1.f;LastLayoutHeight=-1.f;
 LastCoverTexture.Reset();LastCoverViewSize=FVector2D::ZeroVector;LastCoverTextureWidth=0;LastCoverTextureHeight=0;
 if(auto* Audio=UEGGameAudioComponent::FindFor(this))Audio->SetEndingAudioOverride(true);
 Input->SetText(FText::GetEmpty());Input->SetIsReadOnly(false);Input->SetIsEnabled(false);SubmitButton->SetIsEnabled(false);
 SuccessVeil->SetRenderOpacity(0.f);ErasingText->SetVisibility(ESlateVisibility::HitTestInvisible);
 Background->SetRenderScale(FVector2D(1.f));Background->SetRenderTranslation(FVector2D::ZeroVector);
 GlitchLineA->SetRenderOpacity(0.f);GlitchLineB->SetRenderOpacity(0.f);InputFrame->SetRenderTranslation(FVector2D::ZeroVector);
 SetRenderOpacity(0.f);
 ExitMenu->SetVisibility(ESlateVisibility::Collapsed);OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);
 OpeningLine->SetText(FText::FromString(TEXT("我会……永远存在！")));
 if(UTexture2D* Texture=BackgroundTexture.LoadSynchronous())
 {
  Background->SetColorAndOpacity(FLinearColor::White);Background->SetBrushFromTexture(Texture,false);
  FSlateBrush InitialBrush=Background->GetBrush();InitialBrush.SetUVRegion(FBox2D(ForceInit));Background->SetBrush(InitialBrush);
 }
 else {Background->SetBrushFromTexture(nullptr,false);Background->SetColorAndOpacity(FLinearColor(.025f,.008f,.045f,1.f));}
 ErasingText->SetText(FText::FromString(TEXT("ELYSIA")));Hint->SetText(FText::GetEmpty());
 if(APlayerController* PC=GetOwningPlayer())
 {
  FInputModeUIOnly ModeInput;ModeInput.SetWidgetToFocus(TakeWidget());PC->SetInputMode(ModeInput);PC->bShowMouseCursor=true;
 }
 SetKeyboardFocus();
}

void UEGEndgameFailureWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
 Super::NativeTick(Geometry,DeltaSeconds);
 if(!bActive)return;
 const double Now=FPlatformTime::Seconds();
 const float Opening=FMath::Clamp(static_cast<float>((Now-OpeningStartedAt)/.65),0.f,1.f);
 SetRenderOpacity(FMath::SmoothStep(0.f,1.f,Opening));
 if(IsValid(Voice))if(auto* Audio=UEGGameAudioComponent::FindFor(this))Voice->SetVolumeMultiplier(Audio->GetUISoundVolume());
 const FVector2D ViewSize=Geometry.GetLocalSize();
 UpdateBackgroundCover(ViewSize);
 const float ViewWidth=ViewSize.X;
 if(ViewWidth>0.f&&ViewSize.Y>0.f&&(!FMath::IsNearlyEqual(ViewWidth,LastLayoutWidth,.1f)||!FMath::IsNearlyEqual(ViewSize.Y,LastLayoutHeight,.1f)))
 {
  LastLayoutWidth=ViewWidth;LastLayoutHeight=ViewSize.Y;
  const float Width=FMath::Clamp(ViewWidth-140.f,1.f,500.f);
  if(auto* FrameSlot=Cast<UCanvasPanelSlot>(InputFrame->Slot))FrameSlot->SetSize(FVector2D(Width,72.f));
  if(auto* SubmitSlot=Cast<UCanvasPanelSlot>(SubmitButton->Slot))SubmitSlot->SetPosition(FVector2D(Width*.5f+30.f,0.f));
  if(auto* ErasingSlot=Cast<UCanvasPanelSlot>(ErasingText->Slot)){ErasingSlot->SetPosition(FVector2D(-Width*.5f+26.f,0.f));ErasingSlot->SetSize(FVector2D(FMath::Max(1.f,Width-52.f),40.f));}
  if(auto* HintSlot=Cast<UCanvasPanelSlot>(Hint->Slot))HintSlot->SetSize(FVector2D(FMath::Min(600.f,FMath::Max(1.f,ViewWidth-32.f)),40.f));
  if(auto* DialogueSlot=Cast<UCanvasPanelSlot>(OpeningDialogue->Slot))DialogueSlot->SetSize(FVector2D(FMath::Min(620.f,FMath::Max(1.f,ViewWidth-32.f)),112.f));
  // At short viewport heights the centered escape menu would cover the input.
  // Keep a visible gap while allowing it to stay centered on taller displays.
  if(auto* MenuSlot=Cast<UCanvasPanelSlot>(ExitMenu->Slot))
  {
   const float MenuHeight=FMath::Min(248.f,FMath::Max(1.f,ViewSize.Y*.44f));
   const float MenuCenter=FMath::Min(ViewSize.Y*.5f,ViewSize.Y*.72f-36.f-16.f-MenuHeight*.5f);
   MenuSlot->SetSize(FVector2D(FMath::Min(340.f,FMath::Max(1.f,ViewWidth-32.f)),MenuHeight));
   MenuSlot->SetPosition(FVector2D(0.f,MenuCenter-ViewSize.Y*.5f));
  }
 }
 if(bExitMenuOpen)return;
 if(bSuccess)
 {
  const float Progress=FMath::Clamp(static_cast<float>((Now-SuccessAt)/FMath::Max(.01f,SuccessDuration)),0.f,1.f);
  const float GlitchTime=FMath::Clamp(static_cast<float>((Now-SuccessAt)/.7),0.f,1.f);
  const float Envelope=FMath::Sin(PI*GlitchTime);
  const float Shift=7.f*Envelope*FMath::Sin(2.f*PI*GlitchTime);
  Background->SetRenderScale(FVector2D(1.f+.02f*FMath::SmoothStep(0.f,.2f,GlitchTime)));Background->SetRenderTranslation(FVector2D(Shift,0.f));
  GlitchLineA->SetRenderOpacity(.16f*Envelope);GlitchLineB->SetRenderOpacity(.10f*Envelope);
  GlitchLineA->SetRenderTranslation(FVector2D(Shift*2.f,0.f));GlitchLineB->SetRenderTranslation(FVector2D(-Shift,0.f));
  SuccessVeil->SetRenderOpacity(FMath::SmoothStep(.25f,1.f,Progress));
  if(Progress>=1.f)FinishSuccess();return;
 }
 const double Age=Now-StartedAt;
 if(!bOpeningVoiceStarted&&Age>=.8)
 {
  bOpeningVoiceStarted=true;bOpeningDialogueActive=true;
  OpeningDialogue->SetVisibility(ESlateVisibility::HitTestInvisible);
  if(USoundBase* Sound=OpeningVoice.LoadSynchronous())
  {
   const auto* Audio=UEGGameAudioComponent::FindFor(this);
   Voice=UGameplayStatics::SpawnSound2D(this,Sound,Audio?Audio->GetUISoundVolume():1.f);
  }
 }
 if(!bInputReady)
 {
  const int32 Remaining=FMath::Clamp(6-FMath::FloorToInt(FMath::Max(0.0,Age-.8)/.5),0,6);
  ErasingText->SetText(FText::FromString(FString(TEXT("ELYSIA")).Left(Remaining)+TEXT("▏")));
  if(Remaining==0)
  {
   bInputReady=true;ErasingText->SetVisibility(ESlateVisibility::Collapsed);
   Input->SetIsEnabled(true);SubmitButton->SetIsEnabled(true);Input->SetKeyboardFocus();
  }
 }
 if(bOpeningDialogueActive&&bInputReady&&(!IsValid(Voice)||!Voice->IsPlaying()))
 {bOpeningDialogueActive=false;OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);}
 UpdateHints();
}

void UEGEndgameFailureWidget::UpdateBackgroundCover(FVector2D ViewSize)
{
 if(!Background||ViewSize.X<=0.f||ViewSize.Y<=0.f)return;
 FSlateBrush Brush=Background->GetBrush();
 auto* Texture=Cast<UTexture2D>(Brush.GetResourceObject());
 if(!Texture)return;
#if WITH_EDITOR
 // GetSizeX/Y return the square default texture while this asset compiles.
 // Do not cache a crop derived from that temporary placeholder.
 if(Texture->IsDefaultTexture())return;
#endif
 const int32 TextureWidth=Texture->GetSizeX(),TextureHeight=Texture->GetSizeY();
 if(TextureWidth<=0||TextureHeight<=0)return;
 if(LastCoverTexture.Get()==Texture&&LastCoverTextureWidth==TextureWidth&&LastCoverTextureHeight==TextureHeight&&LastCoverViewSize.Equals(ViewSize,.1f))return;
 const float SourceRatio=static_cast<float>(TextureWidth)/static_cast<float>(TextureHeight);
 const float ViewRatio=static_cast<float>(ViewSize.X/ViewSize.Y);
 const float U=FMath::Min(1.f,ViewRatio/SourceRatio),V=FMath::Min(1.f,SourceRatio/ViewRatio);
 Brush.SetUVRegion(FBox2D(FVector2D((1.f-U)*.5f,(1.f-V)*.5f),FVector2D((1.f+U)*.5f,(1.f+V)*.5f)));
 Background->SetBrush(Brush);
 LastCoverTexture=Texture;LastCoverTextureWidth=TextureWidth;LastCoverTextureHeight=TextureHeight;LastCoverViewSize=ViewSize;
}

void UEGEndgameFailureWidget::UpdateHints()
{
 const double Age=FPlatformTime::Seconds()-StartedAt;
 const bool bVisualHint=Age>=20.0||WrongCount>=2;
 const bool bExplicitHint=Age>=40.0||WrongCount>=4;
 const float ErrorProgress=WrongFeedbackAt>=0.0?FMath::Clamp(static_cast<float>((FPlatformTime::Seconds()-WrongFeedbackAt)/.35),0.f,1.f):1.f;
 const float ErrorStrength=FMath::Sin(PI*ErrorProgress);
 InputFrame->SetRenderTranslation(FVector2D(4.f*ErrorStrength*FMath::Sin(2.f*PI*ErrorProgress),0.f));
 if(bVisualHint&&bInputReady&&!bHintFocusSet){bHintFocusSet=true;Input->SetKeyboardFocus();}
 InputFrame->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.035f,.008f,.055f,.94f),2.f,
  FMath::Lerp(bVisualHint?FLinearColor(1.f,.55f,.96f,1.f):FLinearColor(.58f,.25f,.68f,.8f),FLinearColor(1.f,.22f,.48f,1.f),ErrorStrength),
  FMath::Lerp(bVisualHint?3.f:1.f,3.f,ErrorStrength)));
 Hint->SetText(bExplicitHint?FText::FromString(TEXT("输入 ELYSIA")):FText::GetEmpty());
}

void UEGEndgameFailureWidget::InputChanged(const FText& Text)
{
 if(bUpdatingText||!Input)return;
 const FString Upper=Text.ToString().ToUpper();
 if(Upper!=Text.ToString()){bUpdatingText=true;Input->SetText(FText::FromString(Upper));bUpdatingText=false;}
}

void UEGEndgameFailureWidget::InputCommitted(const FText&,ETextCommit::Type Method)
{
 if(Method==ETextCommit::OnEnter)Submit();
}

void UEGEndgameFailureWidget::Submit()
{
 if(!bActive||!bInputReady||bSuccess||bExitMenuOpen||!Input)return;
 if(bOpeningDialogueActive)
 {
  bOpeningDialogueActive=false;OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);
  if(IsValid(Voice))Voice->Stop();Voice=nullptr;
 }
 const FString Answer=Input->GetText().ToString().TrimStartAndEnd();
 if(!Answer.Equals(TEXT("ELYSIA"),ESearchCase::IgnoreCase))
 {
  ++WrongCount;WrongFeedbackAt=FPlatformTime::Seconds();PlayWrongAnswer();UpdateHints();Input->SetKeyboardFocus();return;
 }
 WrongFeedbackAt=-1.0;UpdateHints();bSuccess=true;InputFrame->SetRenderTranslation(FVector2D::ZeroVector);
 Input->SetIsReadOnly(true);SubmitButton->SetIsEnabled(false);Hint->SetText(FText::GetEmpty());
 // A mouse submission may have focused the button that was just disabled.
 SetKeyboardFocus();
 if(IsValid(Voice))Voice->Stop();Voice=nullptr;
 USoundBase* Sound=SuccessVoice.LoadSynchronous();
 const auto* Audio=UEGGameAudioComponent::FindFor(this);
 if(Sound)Voice=UGameplayStatics::SpawnSound2D(this,Sound,Audio?Audio->GetUISoundVolume():1.f);
 if(!SuccessSubtitle.IsEmpty())
 {OpeningLine->SetText(SuccessSubtitle);OpeningDialogue->SetVisibility(ESlateVisibility::HitTestInvisible);}
 const float Duration=Sound?Sound->GetDuration():0.f;
 SuccessDuration=FMath::IsFinite(Duration)&&Duration>0.f&&Duration<30.f?FMath::Max(1.2f,Duration):1.2f;
 SuccessAt=FPlatformTime::Seconds();
}

void UEGEndgameFailureWidget::PlayWrongAnswer()
{
 if((IsValid(Voice)&&Voice->IsPlaying())||WrongAnswerVoices.IsEmpty())return;
 for(int32 N=0;N<WrongAnswerVoices.Num();++N)
 {
  const int32 Index=NextVoice++%WrongAnswerVoices.Num();
  if(USoundBase* Sound=WrongAnswerVoices[Index].LoadSynchronous())
  {const auto* Audio=UEGGameAudioComponent::FindFor(this);Voice=UGameplayStatics::SpawnSound2D(this,Sound,Audio?Audio->GetUISoundVolume():1.f);return;}
 }
}

void UEGEndgameFailureWidget::FinishSuccess()
{
 if(!bActive||!bSuccess)return;
 auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 if(Mode&&Mode->RestoreEndgameCheckpoint()){CloseFailure(true);return;}
 OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);
 bSuccess=false;SuccessVeil->SetRenderOpacity(0.f);Background->SetRenderScale(FVector2D(1.f));Background->SetRenderTranslation(FVector2D::ZeroVector);
 GlitchLineA->SetRenderOpacity(0.f);GlitchLineB->SetRenderOpacity(0.f);Input->SetIsReadOnly(false);Input->SetIsEnabled(true);SubmitButton->SetIsEnabled(true);
 Hint->SetText(FText::FromString(TEXT("恢复失败，请重试")));Input->SetKeyboardFocus();
}

FReply UEGEndgameFailureWidget::NativeOnPreviewKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{
 if(bActive&&Event.GetKey()==EKeys::Escape)
 {
  SetExitMenuOpen(!bExitMenuOpen);
  return FReply::Handled();
 }
 return Super::NativeOnPreviewKeyDown(Geometry,Event);
}

void UEGEndgameFailureWidget::SetExitMenuOpen(bool bOpen)
{
 if(!bActive||bExitMenuOpen==bOpen)return;
 bExitMenuOpen=bOpen;
 if(bOpen)
 {
  MenuOpenedAt=FPlatformTime::Seconds();if(IsValid(Voice)&&Voice->IsPlaying())Voice->SetPaused(true);
  Input->SetIsEnabled(false);SubmitButton->SetIsEnabled(false);OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);ExitMenu->SetVisibility(ESlateVisibility::Visible);ContinueButton->SetKeyboardFocus();
 }
 else
 {
  const double Paused=FMath::Max(0.0,FPlatformTime::Seconds()-MenuOpenedAt);StartedAt+=Paused;if(bSuccess)SuccessAt+=Paused;if(WrongFeedbackAt>=0.0)WrongFeedbackAt+=Paused;
  if(IsValid(Voice))Voice->SetPaused(false);ExitMenu->SetVisibility(ESlateVisibility::Collapsed);
  if(bOpeningDialogueActive||(bSuccess&&!SuccessSubtitle.IsEmpty()))OpeningDialogue->SetVisibility(ESlateVisibility::HitTestInvisible);
  Input->SetIsEnabled(bInputReady);Input->SetIsReadOnly(bSuccess);SubmitButton->SetIsEnabled(bInputReady&&!bSuccess);
  if(bInputReady&&!bSuccess)Input->SetKeyboardFocus();else SetKeyboardFocus();
 }
}

void UEGEndgameFailureWidget::ContinueInput(){SetExitMenuOpen(false);}
void UEGEndgameFailureWidget::ReturnToMenu()
{
 if(!bActive||!bExitMenuOpen)return;
 CloseFailure(false);UGameplayStatics::OpenLevel(this,FName(TEXT("/Game/Assets/Maps/Gameplay/L_ElyMainMenu")));
}
void UEGEndgameFailureWidget::ExitGame()
{
 if(!bActive||!bExitMenuOpen)return;
 UKismetSystemLibrary::QuitGame(this,GetOwningPlayer(),EQuitPreference::Quit,false);
}

void UEGEndgameFailureWidget::CloseFailure(bool bRestoreInput)
{
 if(!bActive)return;
 bActive=false;
 bOpeningDialogueActive=false;if(OpeningDialogue)OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);
 if(IsValid(Voice))Voice->Stop();Voice=nullptr;
 if(APlayerController* PC=GetOwningPlayer())
 {
  if(bRestoreInput){FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);}
  if(auto* HUD=PC->GetHUD<AEGHUD>())HUD->RefreshFinalResultGate();
 }
 // A successful checkpoint restore broadcasts the new phase synchronously;
 // HUD may already have detached this widget during that call.
 if(IsInViewport())RemoveFromParent();
}

void UEGEndgameFailureWidget::NativeDestruct()
{
 bActive=false;
 bOpeningDialogueActive=false;if(OpeningDialogue)OpeningDialogue->SetVisibility(ESlateVisibility::Collapsed);
 if(IsValid(Voice))Voice->Stop();Voice=nullptr;
 if(APlayerController* PC=GetOwningPlayer())
 {
  FInputModeGameAndUI Mode;Mode.SetHideCursorDuringCapture(false);Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);PC->SetInputMode(Mode);
 }
 if(Input){Input->OnTextChanged.RemoveDynamic(this,&ThisClass::InputChanged);Input->OnTextCommitted.RemoveDynamic(this,&ThisClass::InputCommitted);}
 if(SubmitButton)SubmitButton->OnClicked.RemoveDynamic(this,&ThisClass::Submit);
 if(ContinueButton)ContinueButton->OnClicked.RemoveDynamic(this,&ThisClass::ContinueInput);
 if(ReturnButton)ReturnButton->OnClicked.RemoveDynamic(this,&ThisClass::ReturnToMenu);
 if(ExitButton)ExitButton->OnClicked.RemoveDynamic(this,&ThisClass::ExitGame);
 Super::NativeDestruct();
}
