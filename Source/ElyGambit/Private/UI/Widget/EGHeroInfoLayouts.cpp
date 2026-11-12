#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGGradientSurface.h"
#include "UI/Widget/EGRichText.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGPearlGoldLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ProgressBar.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"

namespace
{
UCanvasPanelSlot* Place(UCanvasPanel* Root,UWidget* Child,FVector2D Position,FVector2D Size)
{
 auto* Slot=Root->AddChildToCanvas(Child);Slot->SetPosition(Position);Slot->SetSize(Size);return Slot;
}
void Fill(UCanvasPanel* Root,UWidget* Child,FMargin Margin=FMargin(0))
{
 auto* Slot=Root->AddChildToCanvas(Child);Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(Margin);
}
UTextBlock* Text(UWidgetTree* T,FName Name,int32 Size)
{
 auto* W=T->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
 W->SetFont(FEGVisualStyle::Font(Size));W->SetAutoWrapText(true);
 W->SetColorAndOpacity(FLinearColor(.94f,.94f,.94f));W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
/** Heading role: serif face at the authored size. */
UTextBlock* Heading(UWidgetTree* T,FName Name,int32 Size)
{
 auto* W=Text(T,Name,Size);W->SetFont(FEGVisualStyle::SerifFont(Size));return W;
}
/** Body-role description with numeric emphasis generated at display time. */
UEGRichTextBlock* RichText(UWidgetTree* T,FName Name,int32 Size)
{
 auto* W=T->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass(),Name);
 W->ApplyTextStyle(FEGVisualStyle::BodyFont(Size),FLinearColor(.94f,.94f,.94f));
 W->SetAutoWrapText(true);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
void Add(UVerticalBox* Col,UWidget* W,float Bottom=12)
{Col->AddChildToVerticalBox(W)->SetPadding(FMargin(0,0,0,Bottom));}
UButton* CloseButton(UWidgetTree* T)
{
 auto* B=T->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("Button_CloseInfo"));
 FButtonStyle Style;FSlateBrush None;None.DrawAs=ESlateBrushDrawType::NoDrawType;
 Style.Normal=Style.Pressed=Style.Hovered=Style.Disabled=None;Style.NormalPadding=Style.PressedPadding=FMargin(0);B->SetStyle(Style);
 auto* Label=Text(T,TEXT("CloseInfoLabel"),26);Label->SetText(FText::FromString(TEXT("×")));Label->SetJustification(ETextJustify::Center);B->SetContent(Label);
 return B;
}
UEGGradientSurface* Background(UWidgetTree* T,FName Name,bool bHorizontal,float Solid)
{
 auto* W=T->ConstructWidget<UEGGradientSurface>(UEGGradientSurface::StaticClass(),Name);
 W->TopColor=EGPearlGold::Srgb(31,23,48,.96f);W->BottomColor=EGPearlGold::Srgb(20,16,33,.88f);
 W->bHorizontalGradient=bHorizontal;W->SolidFraction=Solid;
 W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
USizeBox* Fixed(UWidgetTree* T,UWidget* Child,FVector2D Size,FName Name=NAME_None)
{
 auto* W=T->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Name);W->SetWidthOverride(Size.X);W->SetHeightOverride(Size.Y);W->SetContent(Child);return W;
}
}

void UEGHeroInfoWidget::BuildPresentation(UWidgetTree* T,EEGHeroInfoPresentation Mode)
{
 if(Mode==EEGHeroInfoPresentation::Tooltip||Mode==EEGHeroInfoPresentation::DragPreview)
 {BuildLayout(T,Mode==EEGHeroInfoPresentation::DragPreview);return;}
 auto* Root=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("InfoRoot"));T->RootWidget=Root;
 const bool bUnit=Mode==EEGHeroInfoPresentation::UnitHover||Mode==EEGHeroInfoPresentation::UnitDetails;
 if(!bUnit)
 {
  auto* Card=T->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("InfoCard"));
  Card->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Root,Card,FVector2D(0,40),FVector2D(300,420));
  auto* Panel=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("SkillPanel"));
  Place(Root,Panel,FVector2D(316,24),FVector2D(460,520));Fill(Panel,Background(T,TEXT("CardSkillGradient"),true,.66f));
  auto* Scroll=T->ConstructWidget<UScrollBox>();Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
  Fill(Panel,Scroll,FMargin(20,20,56,20));
  auto* Col=T->ConstructWidget<UVerticalBox>();Scroll->AddChild(Col);
  Add(Col,Heading(T,TEXT("CharacterName"),25),18);
  auto* Row=T->ConstructWidget<UHorizontalBox>();
  auto* Icon=T->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("SkillIcon"));Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
  Row->AddChildToHorizontalBox(Fixed(T,Icon,FVector2D(56)))->SetPadding(FMargin(0,0,12,0));
  Row->AddChildToHorizontalBox(Heading(T,TEXT("SkillTitle"),21))->SetSize(FSlateChildSize(ESlateSizeRule::Fill));Add(Col,Row,18);
  Add(Col,RichText(T,TEXT("SkillEffect"),18));
  Place(Root,CloseButton(T),FVector2D(780,0),FVector2D(32));return;
 }
 const bool bPinned=Mode==EEGHeroInfoPresentation::UnitDetails;
 if(bPinned)Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 if(bPinned)
 {
  auto* Surface=T->ConstructWidget<UEGGradientSurface>(UEGGradientSurface::StaticClass(),TEXT("UnitInfoSurface"));
  Surface->TopColor=FLinearColor(0,0,0,.88f);Surface->BottomColor=FLinearColor::Transparent;
  Surface->bHorizontalGradient=true;Surface->bSmoothFade=true;Surface->SolidFraction=376.f/(376.f+140.f);
  Surface->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* SurfaceSlot=Root->AddChildToCanvas(Surface);SurfaceSlot->SetAnchors(FAnchors(0,0,1,1));SurfaceSlot->SetOffsets(FMargin(0,-88,-140,-176));
 }
 else
 {
  auto* Surface=T->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("UnitInfoSurface"));
  Surface->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Srgb(31,23,48,.96f),10.f,EGPearlGold::GoldEdge,1.f));
  Surface->SetVisibility(ESlateVisibility::HitTestInvisible);Fill(Root,Surface);
 }
 if(!bPinned)
 {
  auto* HoverCol=T->ConstructWidget<UVerticalBox>();Fill(Root,HoverCol,FMargin(20,18,20,18));
  Add(HoverCol,Heading(T,TEXT("CharacterName"),22),10);
  for(const TCHAR* Name:{TEXT("InfoHealth"),TEXT("InfoMana")})
  {
   auto* Bar=T->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),Name);
   Bar->SetFillColorAndOpacity(FString(Name)==TEXT("InfoHealth")?FLinearColor(.17f,.68f,.35f):FLinearColor(.25f,.65f,.86f));
   Bar->SetVisibility(ESlateVisibility::HitTestInvisible);Add(HoverCol,Fixed(T,Bar,FVector2D(280,8)),6);
  }
  Add(HoverCol,RichText(T,TEXT("SkillExtra"),16),0);return;
 }
 auto* PinnedHeading=Heading(T,TEXT("CharacterName"),24);
 Place(Root,PinnedHeading,FVector2D(24,18),FVector2D(280,36));
 Place(Root,CloseButton(T),FVector2D(316,12),FVector2D(44));
 auto* Scroll=T->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("UnitInfoScroll"));
 Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
 auto* ScrollSlot=Root->AddChildToCanvas(Scroll);ScrollSlot->SetAnchors(FAnchors(0,0,0,1));ScrollSlot->SetOffsets(FMargin(24,64,328,72));
 auto* Flow=T->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("UnitInfoFlow"));Scroll->AddChild(Flow);
 auto* Col=T->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("UnitInfoBody"));Col->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Add(Flow,Col,12);
 auto* Card=T->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("InfoCard"));Card->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* CardSlot=Col->AddChildToVerticalBox(Fixed(T,Card,FVector2D(200,280),TEXT("InfoCardSize")));CardSlot->SetHorizontalAlignment(HAlign_Center);CardSlot->SetPadding(FMargin(0,0,0,16));
 auto* Summary=RichText(T,TEXT("SkillExtra"),18);Summary->SetLineHeightPercentage(.8f);Add(Col,Summary,8);
 const TCHAR* BarNames[]={TEXT("InfoHealth"),TEXT("InfoMana")};
 const TCHAR* BarRows[]={TEXT("InfoHealthRow"),TEXT("InfoManaRow")};
 const TCHAR* BarValues[]={TEXT("InfoHealthValue"),TEXT("InfoManaValue")};
 const TCHAR* BarLabels[]={TEXT("生命"),TEXT("法力")};
 for(int32 I=0;I<2;++I)
 {
  auto* Group=T->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),FName(BarRows[I]));
  auto* Values=T->ConstructWidget<UHorizontalBox>();
  auto* Label=Text(T,NAME_None,18);Label->SetText(FText::FromString(BarLabels[I]));Label->SetAutoWrapText(false);Label->SetLineHeightPercentage(.8f);Values->AddChildToHorizontalBox(Label)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  auto* Value=Text(T,FName(BarValues[I]),20);Value->SetFont(FEGVisualStyle::NumeralFont(20));Value->SetAutoWrapText(false);Value->SetLineHeightPercentage(.8f);Value->SetJustification(ETextJustify::Right);Values->AddChildToHorizontalBox(Value);
  Add(Group,Values,2);
  auto* Bar=T->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),FName(BarNames[I]));
  Bar->SetFillColorAndOpacity(I==0?FLinearColor(.17f,.68f,.35f):FLinearColor(.25f,.65f,.86f));
  Bar->SetVisibility(ESlateVisibility::HitTestInvisible);Add(Group,Fixed(T,Bar,FVector2D(328,8)),0);Add(Col,Group,8);
 }
 const TCHAR* StatLabels[]={TEXT("攻击"),TEXT("防御"),TEXT("攻速"),TEXT("护盾")};
 const TCHAR* StatValues[]={TEXT("InfoAttackValue"),TEXT("InfoDefenseValue"),TEXT("InfoSpeedValue"),TEXT("InfoShieldValue")};
 for(int32 R=0;R<2;++R)
 {
  auto* Stats=T->ConstructWidget<UHorizontalBox>();
  for(int32 C=0;C<2;++C)
  {
   const int32 I=R*2+C;auto* Pair=T->ConstructWidget<UHorizontalBox>();
   auto* Label=Text(T,NAME_None,18);Label->SetText(FText::FromString(StatLabels[I]));Label->SetAutoWrapText(false);Label->SetLineHeightPercentage(.8f);Pair->AddChildToHorizontalBox(Label)->SetPadding(FMargin(0,0,8,0));
   auto* Value=Text(T,FName(StatValues[I]),20);Value->SetFont(FEGVisualStyle::NumeralFont(20));Value->SetAutoWrapText(false);Value->SetLineHeightPercentage(.8f);Pair->AddChildToHorizontalBox(Value)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
   Stats->AddChildToHorizontalBox(Pair)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  }
  Add(Col,Stats,6);
 }
 auto* Row=T->ConstructWidget<UWrapBox>(UWrapBox::StaticClass(),TEXT("SkillActionRow"));
 Row->SetExplicitWrapSize(true);Row->SetWrapSize(316.f);Row->SetInnerSlotPadding(FVector2D(2,8));Row->SetHorizontalAlignment(HAlign_Center);
 const TCHAR* Buttons[]={TEXT("Button_BasicInfo"),TEXT("Button_UltimateInfo"),TEXT("Button_PassiveInfo")};
 const TCHAR* Icons[]={TEXT("BasicInfoIcon"),TEXT("UltimateInfoIcon"),TEXT("PassiveInfoIcon")};
 const TCHAR* Fallbacks[]={TEXT("BasicInfoFallback"),TEXT("UltimateInfoFallback"),TEXT("PassiveInfoFallback")};
 const TCHAR* Glows[]={TEXT("BasicInfoGlow"),TEXT("UltimateInfoGlow"),TEXT("PassiveInfoGlow")};
 const TCHAR* Underlines[]={TEXT("BasicInfoUnderline"),TEXT("UltimateInfoUnderline"),TEXT("PassiveInfoUnderline")};
 const TCHAR* Labels[]={TEXT("普攻"),TEXT("大招"),TEXT("被动")};
 for(int32 I=0;I<3;++I)
 {
  auto* Button=T->ConstructWidget<UButton>(UButton::StaticClass(),FName(Buttons[I]));Button->SetStyle(FEGVisualStyle::Ghost());
  Button->SetRenderOpacity(.85f);
  auto* Column=T->ConstructWidget<UVerticalBox>();Button->SetContent(Column);
  auto* IconBox=T->ConstructWidget<UOverlay>();
  auto* Glow=T->ConstructWidget<UImage>(UImage::StaticClass(),FName(Glows[I]));Glow->SetBrush(FEGVisualStyle::Glow());
  Glow->SetColorAndOpacity(FLinearColor(1.f,.72f,.94f,.18f));Glow->SetVisibility(ESlateVisibility::Hidden);
  auto* GlowSlot=IconBox->AddChildToOverlay(Glow);GlowSlot->SetHorizontalAlignment(HAlign_Fill);GlowSlot->SetVerticalAlignment(VAlign_Fill);
  auto* Icon=T->ConstructWidget<UImage>(UImage::StaticClass(),FName(Icons[I]));
  auto* ImageSlot=IconBox->AddChildToOverlay(Icon);ImageSlot->SetHorizontalAlignment(HAlign_Fill);ImageSlot->SetVerticalAlignment(VAlign_Fill);
  auto* Fallback=Text(T,FName(Fallbacks[I]),20);Fallback->SetText(FText::FromString(Labels[I]));Fallback->SetJustification(ETextJustify::Center);
  auto* FallbackSlot=IconBox->AddChildToOverlay(Fallback);FallbackSlot->SetHorizontalAlignment(HAlign_Center);FallbackSlot->SetVerticalAlignment(VAlign_Center);
  auto* IconSlot=Column->AddChildToVerticalBox(Fixed(T,IconBox,FVector2D(80),FName(*(FString(Icons[I])+TEXT("Size")))));IconSlot->SetHorizontalAlignment(HAlign_Center);
  auto* Label=Text(T,NAME_None,20);Label->SetText(FText::FromString(Labels[I]));Label->SetJustification(ETextJustify::Center);Label->SetLineHeightPercentage(.8f);
  auto* LabelSlot=Column->AddChildToVerticalBox(Fixed(T,Label,FVector2D(96,32)));LabelSlot->SetHorizontalAlignment(HAlign_Center);
  auto* Underline=T->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(Underlines[I]));Underline->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Champagne,0.f));Underline->SetVisibility(ESlateVisibility::Hidden);
  auto* UnderlineSlot=Column->AddChildToVerticalBox(Fixed(T,Underline,FVector2D(40,2)));UnderlineSlot->SetHorizontalAlignment(HAlign_Center);
  Row->AddChildToWrapBox(Fixed(T,Button,FVector2D(104,116)));
 }
 Add(Flow,Row,12);
 auto* Details=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("ActionDetails"));
 auto* DetailsSlot=Root->AddChildToCanvas(Details);DetailsSlot->SetAnchors(FAnchors(0,0,0,1));DetailsSlot->SetOffsets(FMargin(388,64,420,72));
 auto* DetailTitle=Heading(T,TEXT("ActionDetailsTitle"),26);DetailTitle->SetShadowColorAndOpacity(FLinearColor(0,0,0,.95f));DetailTitle->SetShadowOffset(FVector2D(1,1));
 auto* DetailTitleSlot=Details->AddChildToCanvas(DetailTitle);DetailTitleSlot->SetAnchors(FAnchors(0,0,1,0));DetailTitleSlot->SetOffsets(FMargin(0,0,56,48));
 auto* DetailClose=T->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("Button_CloseSkillDetails"));DetailClose->SetStyle(FEGVisualStyle::Ghost());
 auto* DetailCloseLabel=Text(T,NAME_None,26);DetailCloseLabel->SetText(FText::FromString(TEXT("×")));DetailCloseLabel->SetJustification(ETextJustify::Center);DetailClose->SetContent(DetailCloseLabel);
 auto* DetailCloseSlot=Place(Details,DetailClose,FVector2D(-44,0),FVector2D(44));DetailCloseSlot->SetAnchors(FAnchors(1,0));
 auto* DetailScroll=T->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("ActionDetailsScroll"));DetailScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);Fill(Details,DetailScroll,FMargin(0,56,0,0));
 auto* DetailsBody=RichText(T,TEXT("ActionDetailsBody"),22);
 DetailsBody->SetLineHeightPercentage(.8f);DetailsBody->SetDefaultShadowColorAndOpacity(FLinearColor(0,0,0,.95f));DetailsBody->SetDefaultShadowOffset(FVector2D(1,1));
 DetailScroll->AddChild(DetailsBody);Details->SetVisibility(ESlateVisibility::Collapsed);
 auto* Sell=T->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("Button_SellInfo"));
 Sell->SetStyle(FEGVisualStyle::Ghost());
 auto* Label=Text(T,TEXT("SellInfoLabel"),18);Label->SetText(FText::FromString(TEXT("出售")));Label->SetJustification(ETextJustify::Center);
 Sell->SetContent(Label);Sell->SetVisibility(ESlateVisibility::Collapsed);
  auto* SellSlot=Root->AddChildToCanvas(Sell);SellSlot->SetAnchors(FAnchors(0,1));SellSlot->SetOffsets(FMargin(24,-56,328,40));
}
