#include "UI/Widget/EGShopCard.h"
#include "Placement/EGBoardScene.h"
#include "UI/Widget/EGGoldCoin.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGRichText.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGCardPresentation.h"
#include "UI/EGPearlGoldLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Game/EGCardManager.h"
#include "Components/EGGameAudioComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"

namespace
{
FText FitShopSummary(FString Brief,const FSlateFontInfo& Font)
{
 Brief.ReplaceInline(TEXT("\r"),TEXT(""));Brief.ReplaceInline(TEXT("\n"),TEXT(" "));Brief.TrimStartAndEndInline();
 if(Brief.IsEmpty())return FText::GetEmpty();
 if(!FSlateApplication::IsInitialized())return FText::FromString(Brief.Left(28)+(Brief.Len()>28?TEXT("…"):TEXT("")));
 const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
 const float Width=EGPearlGold::ShopCardWidth*.87f;
 const float Height=EGPearlGold::ShopCardHeight*.27f;
 const int32 LinesAllowed=FMath::Max(1,FMath::FloorToInt(Height/(Measure->GetMaxCharacterHeight(Font)+2.f)));
 TArray<FString> Lines;FString Line;
 for(int32 Index=0;Index<Brief.Len();)
 {
  // Keep a UTF-16 surrogate pair together when truncating localized text.
  const bool bPair=Brief[Index]>=0xD800&&Brief[Index]<=0xDBFF&&Index+1<Brief.Len()&&Brief[Index+1]>=0xDC00&&Brief[Index+1]<=0xDFFF;
  const int32 Units=bPair?2:1;
  const FString Next=Line+Brief.Mid(Index,Units);
  if(!Line.IsEmpty()&&Measure->Measure(Next,Font).X>Width)
  {
   if(Lines.Num()+1>=LinesAllowed)
   {
    while(!Line.IsEmpty()&&Measure->Measure(Line+TEXT("…"),Font).X>Width)
    {
     const int32 Last=Line.Len()-1;
     const bool bEndPair=Last>0&&Line[Last]>=0xDC00&&Line[Last]<=0xDFFF&&Line[Last-1]>=0xD800&&Line[Last-1]<=0xDBFF;
     Line.LeftChopInline(bEndPair?2:1);
    }
    Lines.Add(Line+TEXT("…"));return FText::FromString(FString::Join(Lines,TEXT("\n")));
   }
   Lines.Add(Line);Line.Reset();continue;
  }
  Line=Next;Index+=Units;
 }
 if(!Line.IsEmpty())Lines.Add(Line);
 return FText::FromString(FString::Join(Lines,TEXT("\n")));
}
}

void UEGShopDragOperation::Complete(const FPointerEvent& E)
{
 if(bConsumed||HasFinished())return;bConsumed=true;
 auto* PC=Controller.Get();MarkFinished();
 if(IsValid(PC))PC->TryCompleteDragAtScreenPosition(E.GetScreenSpacePosition());
}
void UEGShopDragOperation::Drop_Implementation(const FPointerEvent& E){Complete(E);}
void UEGShopDragOperation::DragCancelled_Implementation(const FPointerEvent& E)
{
 if(bConsumed||HasFinished())return;
 if(Controller.IsValid())
 {
  if(!bConsumed&&E.GetEffectingButton()==EKeys::LeftMouseButton)Complete(E);
  else {auto* PC=Controller.Get();MarkFinished();PC->CancelActiveDrag();}
 }
 bConsumed=true;
 MarkFinished();
}

void UEGShopCard::BuildLayout(UWidgetTree* WidgetTree)
{
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("ShopCardRoot"));
 WidgetTree->RootWidget=Root;Root->SetVisibility(ESlateVisibility::Visible);
 auto* HoverGlow=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("ShopCardGlow"));
 HoverGlow->SetBrush(FEGVisualStyle::Glow());HoverGlow->SetColorAndOpacity(FLinearColor(.9f,.78f,.57f,0));
 HoverGlow->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* GlowSlot=Root->AddChildToCanvas(HoverGlow);GlowSlot->SetAnchors(FAnchors(0,0,1,1));GlowSlot->SetOffsets(FMargin(-12));

 auto* Face=FEGCardPresentation::Build(WidgetTree,true);
 auto* FaceSlot=Root->AddChildToCanvas(Face);FaceSlot->SetAnchors(FAnchors(0,0,1,1));FaceSlot->SetOffsets(FMargin(0));FaceSlot->SetZOrder(1);
 Face->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* Image=Cast<UImage>(WidgetTree->FindWidget(TEXT("CardImage")));
 auto* Name=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("CardName")));
 auto* Description=WidgetTree->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass(),TEXT("ShopCardDescription"));
 Description->ApplyTextStyle(FEGVisualStyle::BodyFont(17),EGPearlGold::Plum);
 Description->SetAutoWrapText(false);Description->SetJustification(ETextJustify::Left);
 Description->SetClipping(EWidgetClipping::ClipToBounds);Description->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* DescriptionSlot=Face->AddChildToCanvas(Description);DescriptionSlot->SetAnchors(FAnchors(.065f,.70f,.935f,.97f));DescriptionSlot->SetOffsets(FMargin(0));DescriptionSlot->SetZOrder(4);
 auto* Price=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ShopCardPrice"));
 Price->SetFont(FEGVisualStyle::NumeralFont(21));Price->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Price->SetJustification(ETextJustify::Center);
 Price->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* PriceSlot=Face->AddChildToCanvas(Price);PriceSlot->SetAnchors(FAnchors(.78f,.064f,.88f,.13f));PriceSlot->SetOffsets(FMargin(0));PriceSlot->SetZOrder(5);
 auto* CostBadge=WidgetTree->ConstructWidget<UEGGoldCoin>(UEGGoldCoin::StaticClass(),TEXT("ShopPriceCoin"));
 CostBadge->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* CoinSlot=Face->AddChildToCanvas(CostBadge);CoinSlot->SetAnchors(FAnchors(.66f,.064f,.76f,.124f));CoinSlot->SetOffsets(FMargin(0));CoinSlot->SetZOrder(5);
 BuildPriceBadge(WidgetTree);
 auto* Back=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/BoardTheme/Cards/card_back_03"));
 auto* BackImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("ShopCardBack"));BackImage->SetBrushFromTexture(Back,false);
 BackImage->SetVisibility(ESlateVisibility::Collapsed);
 auto* BackSlot=Root->AddChildToCanvas(BackImage);BackSlot->SetAnchors(FAnchors(0,0,1,1));BackSlot->SetOffsets(FMargin(0));BackSlot->SetZOrder(2);
}
void UEGShopCard::BuildPriceBadge(UWidgetTree* Tree)
{
 if(!Tree||Tree->FindWidget(TEXT("ShopPriceTag")))return;
 auto* Face=Cast<UCanvasPanel>(Tree->FindWidget(TEXT("CardCanvas")));
 auto* Price=Cast<UTextBlock>(Tree->FindWidget(TEXT("ShopCardPrice")));
 auto* Icon=Cast<UImage>(Tree->FindWidget(TEXT("ShopPriceCoin")));
 if(!Face||!Price||!Icon)return;
 auto* Tag=Tree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ShopPriceTag"));
 Tag->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.035f,.018f,.055f,.94f),9.f,FLinearColor(.46f,.20f,.36f,.85f),1.f));
 Tag->SetPadding(FMargin(8,4));Tag->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* Slot=Face->AddChildToCanvas(Tag);Slot->SetAnchors(FAnchors(1,0));Slot->SetAlignment(FVector2D(1,0));Slot->SetPosition(FVector2D(-10,12));Slot->SetSize(FVector2D(76,34));Slot->SetZOrder(6);
 auto* Row=Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("ShopPriceTagRow"));Tag->SetContent(Row);
 auto* IconSize=Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("ShopPriceTagIconSize"));IconSize->SetWidthOverride(22);IconSize->SetHeightOverride(22);
 Icon->RemoveFromParent();IconSize->SetContent(Icon);
 auto* IconSlot=Row->AddChildToHorizontalBox(IconSize);IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0,0,5,0));
 Price->RemoveFromParent();Price->SetFont(FEGVisualStyle::NumeralFont(20));Price->SetColorAndOpacity(FSlateColor(FLinearColor(.96f,.91f,1.f)));
 auto* PriceSlot=Row->AddChildToHorizontalBox(Price);PriceSlot->SetVerticalAlignment(VAlign_Center);PriceSlot->SetHorizontalAlignment(HAlign_Center);PriceSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
}
void UEGShopCard::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 // The saved Blueprint tree is authoritative. Native fallback is only for old hosts.
 if(!WidgetTree->RootWidget)BuildLayout(WidgetTree);
 HoverGlow=Cast<UImage>(WidgetTree->FindWidget(TEXT("ShopCardGlow")));
 Face=Cast<UCanvasPanel>(WidgetTree->FindWidget(TEXT("CardCanvas")));
 Image=Cast<UImage>(WidgetTree->FindWidget(TEXT("CardImage")));
 Name=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("CardName")));
 // The saved card blueprint still authors ShopCardDescription as a UTextBlock; convert it
 // once so numeric emphasis comes from rich text while the slot/layout stays identical.
 if(auto* FoundDescription=WidgetTree->FindWidget(TEXT("ShopCardDescription")))
 {
  if(auto* AlreadyRich=Cast<UEGRichTextBlock>(FoundDescription))Description=AlreadyRich;
  else if(auto* SourceText=Cast<UTextBlock>(FoundDescription))
  {
   const FSlateFontInfo SourceFont=SourceText->GetFont();
   DescriptionFontSize=SourceFont.Size>1.f?FMath::RoundToInt(SourceFont.Size):17;
   const FLinearColor SourceColor=SourceText->GetColorAndOpacity().GetSpecifiedColor();
   if(auto* Converted=EGReplaceTextBlockWithRichText(WidgetTree,SourceText))
   {Converted->ApplyTextStyle(FEGVisualStyle::BodyFont(DescriptionFontSize),SourceColor);Description=Converted;}
   else DescriptionPlainText=SourceText;
  }
 }
 if(Description)Description->SetVisibility(ESlateVisibility::Collapsed);
 else if(DescriptionPlainText)DescriptionPlainText->SetVisibility(ESlateVisibility::Collapsed);
 Price=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopCardPrice")));
 CostBadge=Cast<UImage>(WidgetTree->FindWidget(TEXT("ShopPriceCoin")));
 BackImage=Cast<UImage>(WidgetTree->FindWidget(TEXT("ShopCardBack")));
}
void UEGShopCard::NativePreConstruct()
{Super::NativePreConstruct();if(bAuthoredAppearance)FEGCardPresentation::ApplyAuthored(WidgetTree,Appearance);}

void UEGShopCard::Configure(int32 Index,AEGCardManager* Source,UEGMainHUD* MainHUD)
{
 // Keep SeenRevision/FlipTime across recurring view refreshes. Reset only when the
 // widget is re-pointed at another slot, otherwise every RefreshBoardView would
 // restart the buy flip and lose the revision guard.
 if(SlotIndex!=Index){SeenRevision=INDEX_NONE;FlipTime=0.f;}
 SlotIndex=Index;
 Manager=Source;
 HUD=MainHUD;
 RefreshOffer();
}

void UEGShopCard::NativeOnMouseEnter(const FGeometry& G,const FPointerEvent& E)
{
 Super::NativeOnMouseEnter(G,E);HoverSeconds=0.f;bHoverInformationShown=false;
 if(Manager.IsValid()&&Manager->CanTrade()&&!Manager->IsCorruptionShopSlot(SlotIndex)&&!UWidgetBlueprintLibrary::IsDragDropping())
 {
  const auto Offers=Manager->GetShop();
  const auto* PC=Cast<AEGBoardPlayerController>(GetOwningPlayer());
  if(PC&&PC->GetDragState()==EEGPlacementDragState::Idle&&Offers.IsValidIndex(SlotIndex)&&IsValid(Offers[SlotIndex]))
   UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Hover);
 }
}
void UEGShopCard::NativeOnMouseLeave(const FPointerEvent& E)
{
 Super::NativeOnMouseLeave(E);HoverSeconds=0.f;bHoverInformationShown=false;
 if(HUD.IsValid())HUD->HideCardInformation();
}
FReply UEGShopCard::NativeOnMouseWheel(const FGeometry& G,const FPointerEvent& E)
{
 if(bHoverInformationShown&&HUD.IsValid()&&HUD->ScrollShopInformation(E.GetWheelDelta()))return FReply::Handled();
 return Super::NativeOnMouseWheel(G,E);
}

void UEGShopCard::RefreshOffer()
{
 if(!Manager.IsValid()||!Image)return;
 const auto Shop=Manager->GetShop();
 auto* Card=Shop.IsValidIndex(SlotIndex)?Shop[SlotIndex]:nullptr;
 const bool bCorruption=Manager->IsCorruptionShopSlot(SlotIndex);
 const int32 Cost=Manager->GetRules().PurchasePrice;
 if(Price)
 {
  Price->SetText(FText::FromString(FString::FromInt(Cost)));
  {Price->SetColorAndOpacity(FSlateColor(Manager->GetGold()>=Cost?FLinearColor(.96f,.91f,1.f):FLinearColor(1.f,.28f,.32f)));}
 }
 if(auto* Tag=WidgetTree->FindWidget(TEXT("ShopPriceTag")))Tag->SetVisibility(bCorruption?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
 auto* Interference=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ShopCorruptionLabel")));
 if(!Interference&&Face)
 {
  Interference=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ShopCorruptionLabel"));
  Interference->SetText(FText::FromString(TEXT("侵蚀之律者\n干扰卡\n\n不可购买")));
  Interference->SetFont(FEGVisualStyle::PearlFont(24));Interference->SetJustification(ETextJustify::Center);
  Interference->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.8f,.94f)));
  Interference->SetShadowColorAndOpacity(FLinearColor(0.f,0.f,0.f,1.f));Interference->SetShadowOffset(FVector2D(2.f,2.f));
  auto* LabelSlot=Face->AddChildToCanvas(Interference);LabelSlot->SetAnchors(FAnchors(.08f,.3f,.92f,.8f));LabelSlot->SetOffsets(FMargin(0));LabelSlot->SetZOrder(20);
 }
 if(Interference)Interference->SetVisibility(bCorruption?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 const int32 Revision=Manager->GetShopSlotRevision(SlotIndex);
 const bool bRevisionChanged=Revision!=SeenRevision;
 if(bRevisionChanged)
 {
  // Never animate while the shop is collapsed: un-ticked widgets would freeze at the
  // squashed mid-flip scale. Initial stocking shows the real artwork immediately.
  const bool bAnimate=SeenRevision!=INDEX_NONE&&HUD.IsValid()&&HUD->IsShopExpanded();
  FlipTime=bAnimate?.36f:0.f;
  SeenRevision=Revision;
 }
 auto* NewArtwork=Card?Card->Artwork.Get():nullptr;
 if(Card&&Card->HeroDefinition)if(auto* Official=FEGCardPresentation::ArtworkForHero(Card->HeroDefinition))NewArtwork=Official;
 NewArtwork=FEGCardPresentation::ResolveArtwork(NewArtwork);
 const bool bCardChanged=Artwork.Get()!=NewArtwork;
 if(bCardChanged||bRevisionChanged)
 {
  Artwork=NewArtwork;
  const auto* Variant=HUD.IsValid()?HUD->ShopCardClasses.Find(FEGCardPresentation::CharacterKey(Artwork)):nullptr;
  if(Variant&&*Variant)FEGCardPresentation::ApplyAuthored(WidgetTree,(*Variant)->GetDefaultObject<UEGShopCard>()->Appearance);
  else FEGCardPresentation::Apply(WidgetTree,Artwork,true);
 }
 Image->SetColorAndOpacity(bCorruption?FLinearColor(.40f,.12f,.45f,1.f):FLinearColor::White);
 if(bRevisionChanged||FlipTime<=0.f)
 {
  if(Face){Face->SetVisibility(FlipTime>0.f?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);Face->SetRenderScale(FVector2D(1));}
  if(BackImage){BackImage->SetVisibility(FlipTime>0.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);BackImage->SetRenderScale(FVector2D(1));}
 }
 if(Description||DescriptionPlainText)
 if(bCardChanged||bRevisionChanged)
 {
  const FString Brief=Card&&Card->HeroDefinition?(Card->HeroDefinition->bConfirmedSkills?Card->HeroDefinition->GetPassiveDescription():Card->HeroDefinition->GetCardSkillDescription()).ToString():FString();
  // FitShopSummary measures plain text with the Body role; the widget then renders the
  // same string through the numeric-highlight decorator.
  const FText Summary=FitShopSummary(Brief,FEGVisualStyle::BodyFont(DescriptionFontSize));
  if(Description){Description->SetHighlightedText(Summary);Description->SetRenderOpacity(1.f);}
  else if(DescriptionPlainText){DescriptionPlainText->SetText(Summary);DescriptionPlainText->SetRenderOpacity(1.f);}
 }
 if(bRevisionChanged){HoverSeconds=0.f;bHoverInformationShown=false;}
 SetToolTipText(FText::GetEmpty());
}

void UEGShopCard::NativeTick(const FGeometry& G,float DT)
{
 Super::NativeTick(G,DT);
 if(IsHovered()&&!bHoverInformationShown&&HUD.IsValid()&&!HUD->IsAnyDragActive())
 {
  HoverSeconds+=DT;if(HoverSeconds>=.12f&&Manager.IsValid())
  {const auto Offers=Manager->GetShop();if(Offers.IsValidIndex(SlotIndex)&&Offers[SlotIndex])HUD->ShowHeroInformation(Offers[SlotIndex]->HeroDefinition,false,TEXT("Shop"),this);bHoverInformationShown=true;}
 }
 if(!IsVisible())
 {
  // hidden shop: settle any pending flip so it cannot resume half-scaled
  if(FlipTime>0.f){FlipTime=0.f;if(Face){Face->SetRenderScale(FVector2D(1));Face->SetVisibility(ESlateVisibility::HitTestInvisible);}if(BackImage)BackImage->SetVisibility(ESlateVisibility::Collapsed);}
  return;
 }
 const float TargetHover=IsHovered()?1.f:0.f;
 if(HUD.IsValid()&&HUD->IsAnyDragActive())
 {HoverAlpha=0.f;SetRenderScale(FVector2D(1));if(HoverGlow)HoverGlow->SetColorAndOpacity(FLinearColor(0,0,0,0));}
 else if(!FMath::IsNearlyEqual(HoverAlpha,TargetHover,.002f))
 {
  HoverAlpha=FMath::FInterpTo(HoverAlpha,TargetHover,DT,10.f);
  if(HoverGlow)HoverGlow->SetColorAndOpacity(FLinearColor(0.78f,0.69f,0.87f,0.42f*HoverAlpha));
  SetRenderScale(FVector2D(1.f+0.022f*HoverAlpha));
  SetRenderTransformPivot(FVector2D(0.5f,0.5f));
 }
 if(FlipTime<=0.f)return;
 FlipTime=FMath::Max(0.f,FlipTime-DT);
 const float P=1.f-FlipTime/.36f;
 const FVector2D Scale(FMath::Max(.02f,FMath::Abs(2.f*P-1.f)),1.f);
 if(Face){Face->SetRenderTransformPivot(FVector2D(.5f,.5f));Face->SetRenderScale(Scale);Face->SetVisibility(P<.5f?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
 if(BackImage){BackImage->SetRenderTransformPivot(FVector2D(.5f,.5f));BackImage->SetRenderScale(Scale);BackImage->SetVisibility(P<.5f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
 if(FlipTime==0.f&&Face)Face->SetRenderScale(FVector2D(1));
}

FReply UEGShopCard::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{
 if(E.GetEffectingButton()==EKeys::LeftMouseButton&&Manager.IsValid()&&Manager->CanTrade()&&!Manager->IsCorruptionShopSlot(SlotIndex))
 {PressedRevision=Manager->GetShopSlotRevision(SlotIndex);return FReply::Handled().CaptureMouse(TakeWidget()).DetectDrag(TakeWidget(),EKeys::LeftMouseButton);}
 return Super::NativeOnMouseButtonDown(G,E);
}
FReply UEGShopCard::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
 if(E.GetEffectingButton()==EKeys::LeftMouseButton&&PressedRevision!=INDEX_NONE)
 {const int32 R=PressedRevision;PressedRevision=INDEX_NONE;if(G.IsUnderLocation(E.GetScreenSpacePosition())&&Manager.IsValid()&&Manager->BuyShopSlotExpected(SlotIndex,R))UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Purchase);return FReply::Handled().ReleaseMouseCapture();}
 return Super::NativeOnMouseButtonUp(G,E);
}
void UEGShopCard::NativeOnDragDetected(const FGeometry& G,const FPointerEvent& E,UDragDropOperation*& Operation)
{
 if(!Manager.IsValid()||!HUD.IsValid()||PressedRevision==INDEX_NONE||Manager->IsCorruptionShopSlot(SlotIndex))return;
 auto* PC=Cast<AEGBoardPlayerController>(GetOwningPlayer());
 if(!PC||!PC->BeginShopCardDrag(SlotIndex,PressedRevision)){PressedRevision=INDEX_NONE;return;}
 auto* Drag=NewObject<UEGShopDragOperation>(this);Drag->Controller=PC;PC->AttachCardOperation(Drag);
 Drag->Manager=Manager;Drag->HUD=HUD;Drag->SlotIndex=SlotIndex;Drag->Revision=PressedRevision;PressedRevision=INDEX_NONE;
 if(auto* Visual=CreateWidget<UEGHandCard>(GetOwningPlayer(),HUD->CardWidgetClass))
 {Visual->Configure(FGuid(),Artwork);Visual->SetVisibility(ESlateVisibility::HitTestInvisible);Drag->DefaultDragVisual=Visual;Drag->Pivot=EDragPivot::CenterCenter;}
 PC->UpdateDragAtScreenPosition(E.GetScreenSpacePosition());
 if(!Drag->HasFinished()&&PC->GetDragState()!=EEGPlacementDragState::Idle)Operation=Drag;
}
