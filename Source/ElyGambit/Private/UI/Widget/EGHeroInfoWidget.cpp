#include "UI/Widget/EGHeroInfoWidget.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroCardInfoDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGGriseoAdultSkillDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "Skills/EGGriseoAdultSkillBehavior.h"
#include "Skills/EGSakuraVillVSkillBehaviors.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGCardPresentation.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/EGUnitInspectionView.h"
#include "UI/Widget/EGRichText.h"
#include "UI/Widget/EGGradientSurface.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/ScrollBox.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Engine/Texture2D.h"

namespace
{
enum class EInspectionGlyph : uint8 { Neutral, Melee, Projectile, Heal, Shield, Speed, Seed };

EInspectionGlyph InspectionGlyph(const UEGHeroDefinition* Definition,int32 Index)
{
 const auto* Monster=Definition?Definition->MonsterAttack.Get():nullptr;
 if(!Monster||Index==2)return EInspectionGlyph::Neutral;
 if(Index==0)
 {
  switch(Monster->AttackType)
  {
   case EEGMonsterAttackType::Melee:return EInspectionGlyph::Melee;
   case EEGMonsterAttackType::Projectile:return EInspectionGlyph::Projectile;
   case EEGMonsterAttackType::Heal:return EInspectionGlyph::Heal;
  }
 }
 if(Index==1)
 {
  switch(Monster->Ultimate)
  {
   case EEGMonsterUltimate::Shield:return EInspectionGlyph::Shield;
   case EEGMonsterUltimate::EnhancedShot:return EInspectionGlyph::Projectile;
   case EEGMonsterUltimate::TeamSpeed:return EInspectionGlyph::Speed;
   case EEGMonsterUltimate::CrystalSeed:return EInspectionGlyph::Seed;
   default:break;
  }
 }
 return EInspectionGlyph::Neutral;
}

void SetInspectionFallback(UWidgetTree* Tree,UWidget* Fallback,int32 Index,const UEGHeroDefinition* Definition,bool bShow)
{
 if(!Tree||!Fallback)return;
 // The authored WBP stores fallback text in the icon overlay. Keep its lower
 // category label and replace only this duplicate label with a vector glyph.
 auto* Parent=Cast<UOverlay>(Fallback->GetParent());
 if(!Parent){Fallback->SetVisibility(ESlateVisibility::Collapsed);return;}
 const FName GlyphName(*FString::Printf(TEXT("InspectionFallbackGlyph_%d"),Index));
 auto* Glyph=Cast<UCanvasPanel>(Tree->FindWidget(GlyphName));
 if(!Glyph&&bShow)
 {
  Glyph=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),GlyphName);
  if(auto* GlyphSlot=Parent->AddChildToOverlay(Glyph)){GlyphSlot->SetHorizontalAlignment(HAlign_Fill);GlyphSlot->SetVerticalAlignment(VAlign_Fill);}
 }
 Fallback->SetVisibility(ESlateVisibility::Collapsed);
 if(!Glyph)return;
 Glyph->SetVisibility(bShow?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if(!bShow)return;
 const EInspectionGlyph Type=InspectionGlyph(Definition,Index);
 const FName VariantName(*FString::Printf(TEXT("InspectionFallbackGlyph_%d_Type_%d"),Index,static_cast<int32>(Type)));
 UCanvasPanel* Variant=nullptr;
 for(int32 ChildIndex=0;ChildIndex<Glyph->GetChildrenCount();++ChildIndex)
 {
  UWidget* Child=Glyph->GetChildAt(ChildIndex);
  const bool bMatch=Child&&Child->GetFName()==VariantName;
  if(Child)Child->SetVisibility(bMatch?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  if(bMatch)Variant=Cast<UCanvasPanel>(Child);
 }
 if(Variant)return;
 // Each slot/type is built at most once per widget tree; switching inspected
 // units only toggles the already-created variant's visibility.
 Variant=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),VariantName);
 Variant->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* VariantSlot=Glyph->AddChildToCanvas(Variant);VariantSlot->SetPosition(FVector2D::ZeroVector);VariantSlot->SetSize(FVector2D(80.f));
 const FLinearColor Ink(.94f,.82f,.68f,1.f);
 auto Stroke=[Tree,Variant,Ink](FVector2D Position,FVector2D Size,float Angle=0.f)
 {
  auto* Line=Tree->ConstructWidget<UImage>();Line->SetBrush(FEGVisualStyle::Rounded(Ink,1.f));
  Line->SetVisibility(ESlateVisibility::HitTestInvisible);Line->SetRenderTransformPivot(FVector2D(.5f,.5f));Line->SetRenderTransformAngle(Angle);
  auto* LineSlot=Variant->AddChildToCanvas(Line);LineSlot->SetPosition(Position);LineSlot->SetSize(Size);
 };
 switch(Type)
 {
  case EInspectionGlyph::Melee:
   Stroke(FVector2D(18,38),FVector2D(44,4),-45.f);Stroke(FVector2D(18,38),FVector2D(44,4),45.f);
   Stroke(FVector2D(19,52),FVector2D(16,3),-45.f);Stroke(FVector2D(45,52),FVector2D(16,3),45.f);break;
  case EInspectionGlyph::Projectile:
   Stroke(FVector2D(17,38),FVector2D(44,4));Stroke(FVector2D(47,31),FVector2D(16,3),45.f);Stroke(FVector2D(47,45),FVector2D(16,3),-45.f);break;
  case EInspectionGlyph::Heal:
   Stroke(FVector2D(36,19),FVector2D(8,42));Stroke(FVector2D(19,36),FVector2D(42,8));break;
  case EInspectionGlyph::Shield:
  {
   auto* Mark=Tree->ConstructWidget<UImage>();Mark->SetBrush(FEGVisualStyle::Rounded(FLinearColor::Transparent,8.f,Ink,3.f));
   Mark->SetVisibility(ESlateVisibility::HitTestInvisible);auto* MarkSlot=Variant->AddChildToCanvas(Mark);MarkSlot->SetPosition(FVector2D(22,17));MarkSlot->SetSize(FVector2D(36,46));break;
  }
  case EInspectionGlyph::Speed:
   Stroke(FVector2D(21,28),FVector2D(23,4),-45.f);Stroke(FVector2D(21,46),FVector2D(23,4),45.f);
   Stroke(FVector2D(38,28),FVector2D(23,4),-45.f);Stroke(FVector2D(38,46),FVector2D(23,4),45.f);break;
  case EInspectionGlyph::Seed:
   Stroke(FVector2D(23,27),FVector2D(25,3),-45.f);Stroke(FVector2D(33,27),FVector2D(25,3),45.f);
   Stroke(FVector2D(23,50),FVector2D(25,3),45.f);Stroke(FVector2D(33,50),FVector2D(25,3),-45.f);break;
  default:
   Stroke(FVector2D(27,38),FVector2D(26,4));break;
 }
}
}

void UEGInspectionSkillButtonHandler::Click()
{if(Owner.IsValid())Owner->OpenEnemySkill(Index);}

void UEGHeroInfoWidget::BuildLayout(UWidgetTree* T,bool bDrag)
{
 auto* Root=T->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("InfoRoot"));T->RootWidget=Root;
 if(bDrag)
 {
  auto* Card=T->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("InfoCard"));
  auto* S=Root->AddChildToCanvas(Card);S->SetPosition(FVector2D(0,0));S->SetSize(FVector2D(300,420));
 }
 auto* Panel=T->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("SkillPanel"));Panel->SetPadding(FMargin(16));
 Panel->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Srgb(12,12,14,.92f),4,EGPearlGold::Srgb(156,150,139,.65f),1));
 auto* PS=Root->AddChildToCanvas(Panel);PS->SetPosition(FVector2D(bDrag?316:0,0));PS->SetSize(FVector2D(bDrag?300:380,270));
 auto* Col=T->ConstructWidget<UVerticalBox>();Panel->SetContent(Col);
 auto* Row=T->ConstructWidget<UHorizontalBox>();Col->AddChildToVerticalBox(Row)->SetPadding(FMargin(0,0,0,12));
 auto* Icon=T->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("SkillIcon"));
 auto* IS=T->ConstructWidget<USizeBox>();IS->SetWidthOverride(56);IS->SetHeightOverride(56);IS->SetContent(Icon);Row->AddChildToHorizontalBox(IS)->SetPadding(FMargin(0,0,12,0));
 auto* Title=T->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("SkillTitle"));Title->SetFont(FEGVisualStyle::SerifFont(21));Title->SetAutoWrapText(true);Title->SetColorAndOpacity(FSlateColor(FLinearColor(.96f,.94f,.90f)));
 Row->AddChildToHorizontalBox(Title)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
 for(const FName Name:{FName(TEXT("SkillEffect")),FName(TEXT("SkillExtra"))})
 {
  auto* Text=T->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass(),Name);
  Text->ApplyTextStyle(FEGVisualStyle::BodyFont(18),FLinearColor(.9f,.88f,.84f));
  Text->SetAutoWrapText(true);Text->SetVisibility(ESlateVisibility::HitTestInvisible);
  Col->AddChildToVerticalBox(Text)->SetPadding(FMargin(0,0,0,12));
 }
}
void UEGHeroInfoWidget::EnsureRichTextTexts()
{
 if(bRichTextEnsured)return;
 bRichTextEnsured=true;
 if(!WidgetTree)return;
 auto Convert=[this](FName Name,TObjectPtr<UEGRichTextBlock>& OutRich,TObjectPtr<UTextBlock>& OutPlain)
 {
  UWidget* Existing=WidgetTree->FindWidget(Name);
  if(!Existing)return;
  if(auto* AlreadyRich=Cast<UEGRichTextBlock>(Existing)){OutRich=AlreadyRich;return;}
  auto* SourceText=Cast<UTextBlock>(Existing);
  if(!SourceText)return;
  // Keep the authored size, switch the face to the Body role. Complex markup never
  // reaches this layer: data assets only carry plain FText.
  const FSlateFontInfo SourceFont=SourceText->GetFont();
  const FLinearColor SourceColor=SourceText->GetColorAndOpacity().GetSpecifiedColor();
  const int32 SourceSize=FMath::Max(1,FMath::RoundToInt(SourceFont.Size));
  const bool bOutline=SourceFont.OutlineSettings.OutlineSize>0;
  if(auto* Converted=EGReplaceTextBlockWithRichText(WidgetTree,SourceText))
  {
   Converted->ApplyTextStyle(FEGVisualStyle::BodyFont(SourceSize,bOutline),SourceColor);
   OutRich=Converted;
  }
  else
  {
   // Replacement failed: keep the plain block visible and render unhighlighted text.
   OutPlain=SourceText;
  }
 };
 Convert(TEXT("SkillEffect"),SkillEffectRichText,SkillEffectPlainText);
 Convert(TEXT("SkillExtra"),SkillExtraRichText,SkillExtraPlainText);
 Convert(TEXT("SkillToggleHint"),SkillHintRichText,SkillHintPlainText);
}
void UEGHeroInfoWidget::SetSkillText(UEGRichTextBlock* Rich,UTextBlock* Plain,const FText& Value)
{
 if(Rich){Rich->SetHighlightedText(Value);return;}
 if(Plain)Plain->SetText(Value);
}
void UEGHeroInfoWidget::NativeOnInitialized()
{Super::NativeOnInitialized();if(!WidgetTree->RootWidget)BuildPresentation(WidgetTree,bDragLayout?EEGHeroInfoPresentation::DragPreview:Presentation);EnsureRichTextTexts();SetVisibility(ESlateVisibility::HitTestInvisible);}
void UEGHeroInfoWidget::NativeConstruct()
{
 Super::NativeConstruct();
 for(int32 I=0;I<EnemySkillButtons.Num()&&I<EnemySkillHandlers.Num();++I)
  if(EnemySkillButtons[I]&&EnemySkillHandlers[I])EnemySkillButtons[I]->OnClicked.AddUniqueDynamic(EnemySkillHandlers[I],&UEGInspectionSkillButtonHandler::Click);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_SkillInfo"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleSkill);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_CloseInfo"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::RequestClose);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_SellInfo"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::RequestSell);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_BasicInfo"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::OpenBasicDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_UltimateInfo"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::OpenUltimateDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_PassiveInfo"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::OpenPassiveDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_CloseSkillDetails"))))B->OnClicked.AddUniqueDynamic(this,&ThisClass::CloseSkillDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_BasicInfo")))){B->OnHovered.AddUniqueDynamic(this,&ThisClass::HoverBasic);B->OnUnhovered.AddUniqueDynamic(this,&ThisClass::UnhoverBasic);}
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_UltimateInfo")))){B->OnHovered.AddUniqueDynamic(this,&ThisClass::HoverUltimate);B->OnUnhovered.AddUniqueDynamic(this,&ThisClass::UnhoverUltimate);}
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_PassiveInfo")))){B->OnHovered.AddUniqueDynamic(this,&ThisClass::HoverPassive);B->OnUnhovered.AddUniqueDynamic(this,&ThisClass::UnhoverPassive);}
}
void UEGHeroInfoWidget::NativeDestruct()
{
 PendingBossScrollPhase=0;
 for(int32 I=0;I<EnemySkillButtons.Num()&&I<EnemySkillHandlers.Num();++I)
  if(EnemySkillButtons[I]&&EnemySkillHandlers[I])EnemySkillButtons[I]->OnClicked.RemoveDynamic(EnemySkillHandlers[I],&UEGInspectionSkillButtonHandler::Click);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_SkillInfo"))))B->OnClicked.RemoveDynamic(this,&ThisClass::ToggleSkill);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_CloseInfo"))))B->OnClicked.RemoveDynamic(this,&ThisClass::RequestClose);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_SellInfo"))))B->OnClicked.RemoveDynamic(this,&ThisClass::RequestSell);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_BasicInfo"))))B->OnClicked.RemoveDynamic(this,&ThisClass::OpenBasicDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_UltimateInfo"))))B->OnClicked.RemoveDynamic(this,&ThisClass::OpenUltimateDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_PassiveInfo"))))B->OnClicked.RemoveDynamic(this,&ThisClass::OpenPassiveDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_CloseSkillDetails"))))B->OnClicked.RemoveDynamic(this,&ThisClass::CloseSkillDetails);
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_BasicInfo")))){B->OnHovered.RemoveDynamic(this,&ThisClass::HoverBasic);B->OnUnhovered.RemoveDynamic(this,&ThisClass::UnhoverBasic);}
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_UltimateInfo")))){B->OnHovered.RemoveDynamic(this,&ThisClass::HoverUltimate);B->OnUnhovered.RemoveDynamic(this,&ThisClass::UnhoverUltimate);}
 if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_PassiveInfo")))){B->OnHovered.RemoveDynamic(this,&ThisClass::HoverPassive);B->OnUnhovered.RemoveDynamic(this,&ThisClass::UnhoverPassive);}
 FinishCloseSkillDetails();
 PresentedDefinition.Reset();bSkillExpanded=false;DisplayedBossArtworkPhase=0;BossPhaseArtwork=nullptr;bInspectedEnemy=false;Super::NativeDestruct();
}
void UEGHeroInfoWidget::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
 Super::NativeTick(MyGeometry,InDeltaTime);
 if(!HasSkillDetailsOpen()&&!bClosingSkillDetails)return;
 const float Delta=FMath::Max(0.f,InDeltaTime);
 if(bClosingSkillDetails||bSwitchingSkillDetails)
 {
  const float Duration=bSwitchingSkillDetails?.04f:.08f;
  SkillDetailsReveal=FMath::Max(0.f,SkillDetailsReveal-Delta/Duration);
  if(SkillDetailsReveal<=0.f)
  {
   if(bClosingSkillDetails){FinishCloseSkillDetails();return;}
   OpenSkillIndex=PendingSkillIndex;PendingSkillIndex=INDEX_NONE;bSwitchingSkillDetails=false;
   SkillOpeningDuration=.04f;RefreshSkillDetails();UpdateSkillSelection();
   if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("ActionDetailsScroll"))))Scroll->ScrollToStart();
  }
 }
 else SkillDetailsReveal=FMath::Min(1.f,SkillDetailsReveal+Delta/FMath::Max(.001f,SkillOpeningDuration));
 if(auto* Details=GetWidgetFromName(TEXT("ActionDetails")))
 {
  const float Smooth=SkillDetailsReveal*SkillDetailsReveal*(3.f-2.f*SkillDetailsReveal);
  Details->SetRenderOpacity(Smooth);Details->SetRenderTranslation(FVector2D(8.f*(1.f-Smooth),0.f));
 }
}
void UEGHeroInfoWidget::Present(const UEGHeroDefinition* D,const FText& Extra)
{
 const bool bDefinitionChanged=PresentedDefinition.Get()!=D;
 if(bDefinitionChanged)
 {
  PendingBossScrollPhase=0;
  bSkillExpanded=false;FinishCloseSkillDetails();PresentedDefinition=D;NormalInfoScrollOffset=0.f;
  if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("UnitInfoScroll"))))Scroll->ScrollToStart();
  const auto* CardInfo=D?D->CardInformation.Get():nullptr;
  const FName Icons[]={TEXT("BasicInfoIcon"),TEXT("UltimateInfoIcon"),TEXT("PassiveInfoIcon")};
  const FName Fallbacks[]={TEXT("BasicInfoFallback"),TEXT("UltimateInfoFallback"),TEXT("PassiveInfoFallback")};
  UTexture2D* Textures[3]={nullptr,nullptr,nullptr};
  if(CardInfo&&(!D||!D->MonsterAttack))
  {
   Textures[0]=CardInfo->BasicIcon.LoadSynchronous();Textures[1]=CardInfo->UltimateIcon.LoadSynchronous();Textures[2]=CardInfo->PassiveIcon.LoadSynchronous();
   if(!Textures[1])Textures[1]=CardInfo->Icon;
  }
  for(int32 I=0;I<3;++I)
  {
   if(auto* Icon=Cast<UImage>(GetWidgetFromName(Icons[I]))){Icon->SetBrushFromTexture(Textures[I],false);Icon->SetVisibility(Textures[I]?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
   if(auto* Fallback=GetWidgetFromName(Fallbacks[I]))SetInspectionFallback(WidgetTree,Fallback,I,D,Textures[I]==nullptr);
  }
 }
 // Saved tooltip Blueprints also use SkillPanel. Wrap their existing content so
 // a viewport-sized panel clips cleanly while the pinned card view provides scrolling.
 if(Presentation==EEGHeroInfoPresentation::Tooltip&&!bDragLayout&&!GetWidgetFromName(TEXT("TooltipContentScroll")))
 if(auto* Panel=Cast<UBorder>(GetWidgetFromName(TEXT("SkillPanel"))))if(auto* Content=Panel->GetContent())
 {
  Content->RemoveFromParent();
  auto* Container=WidgetTree->ConstructWidget<UVerticalBox>();
  auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("TooltipContentScroll"));
  Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);Scroll->AddChild(Content);
  Container->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
  auto* Hint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("TooltipReadHint"));
  Hint->SetFont(FEGVisualStyle::PearlFont(15));Hint->SetColorAndOpacity(FSlateColor(FLinearColor(.9f,.88f,.84f)));
  Hint->SetText(FText::FromString(TEXT("滚动鼠标滚轮查看完整说明")));Hint->SetVisibility(ESlateVisibility::Collapsed);
  Container->AddChildToVerticalBox(Hint)->SetPadding(FMargin(0,8,0,0));Panel->SetContent(Container);
 }
 auto* Info=D?D->CardInformation.Get():nullptr;
 EnsureRichTextTexts();
 auto Set=[&](FName Name,FText Value){if(auto* Text=Cast<UTextBlock>(GetWidgetFromName(Name)))Text->SetText(Value);};
 Set(TEXT("CharacterName"),D?D->DisplayName:FText::GetEmpty());
 Set(TEXT("SkillTitle"),D?D->GetPlayerFacingSkillName():FText::FromString(TEXT("无独立大招")));
 SetSkillText(SkillEffectRichText.Get(),SkillEffectPlainText.Get(),D?D->GetPlayerFacingSkillEffect():FText::GetEmpty());
 SetSkillText(SkillExtraRichText.Get(),SkillExtraPlainText.Get(),Extra);
 // Keep old saved trees compatible while removing the role row everywhere.
 if(auto* Roles=GetWidgetFromName(TEXT("SkillRoles")))Roles->SetVisibility(ESlateVisibility::Collapsed);
 if(auto* ExtraWidget=SkillExtraRichText?static_cast<UWidget*>(SkillExtraRichText.Get()):static_cast<UWidget*>(SkillExtraPlainText.Get()))
  ExtraWidget->SetVisibility(Extra.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
 const bool bSkillIcon=Info&&Info->Icon&&Info->bHasUltimate&&Info->Icon->GetPathName().StartsWith(TEXT("/Game/Assets/UI/SkillIcons/"));
 if(auto* Icon=Cast<UImage>(GetWidgetFromName(TEXT("SkillIcon")))){Icon->SetBrushFromTexture(bSkillIcon?Info->Icon.Get():nullptr,false);Icon->SetVisibility(bSkillIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
 const bool bUnit=Presentation==EEGHeroInfoPresentation::UnitHover||Presentation==EEGHeroInfoPresentation::UnitDetails;
 if(auto* Button=GetWidgetFromName(TEXT("Button_SkillInfo")))Button->SetVisibility(bSkillIcon&&Presentation!=EEGHeroInfoPresentation::UnitHover?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 SetSkillText(SkillHintRichText.Get(),SkillHintPlainText.Get(),FText::FromString(bSkillIcon?(Presentation==EEGHeroInfoPresentation::UnitHover?TEXT("点击棋子查看技能"):(bSkillExpanded?TEXT("点击图标收起技能"):TEXT("点击图标查看技能"))):
  (Info&&!Info->bHasUltimate?TEXT("无独立大招")+(!D||D->GetPlayerFacingSkillEffect().IsEmpty()?FString():TEXT("\n")+D->GetPlayerFacingSkillEffect().ToString()):TEXT("技能说明"))));
 // Some production heroes have no approved skill icon. Keep their description
 // readable in the existing scroll panel instead of hiding it behind a missing button.
 if(auto* Body=GetWidgetFromName(TEXT("SkillContent")))Body->SetVisibility(bUnit&&Presentation!=EEGHeroInfoPresentation::UnitHover&&Info&&Info->bHasUltimate&&(!bSkillIcon||bSkillExpanded)?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
 if(auto* Card=Cast<UImage>(GetWidgetFromName(TEXT("InfoCard"))))
 {
  if(bDefinitionChanged||!bInspectedEnemy||DisplayedBossArtworkPhase<=0||!BossPhaseArtwork)
  {Card->SetBrush(FEGCardPresentation::PortraitBrush(FEGCardPresentation::ArtworkForHero(D),FVector2D(300,420)));FEGCardPresentation::ApplyDefinitionLayers(WidgetTree,Card,D);}
  if(Presentation==EEGHeroInfoPresentation::UnitHover)Card->SetVisibility(ESlateVisibility::Collapsed);
 }
 if(auto* CardSize=GetWidgetFromName(TEXT("InfoCardSize")))CardSize->SetVisibility(Presentation!=EEGHeroInfoPresentation::UnitHover&&FEGCardPresentation::ArtworkForHero(D)?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 // Text wraps without clipping; the panel grows for live attributes in world hover.
 if(Presentation==EEGHeroInfoPresentation::Tooltip||Presentation==EEGHeroInfoPresentation::DragPreview)if(auto* Panel=GetWidgetFromName(TEXT("SkillPanel")))if(auto* S=Cast<UCanvasPanelSlot>(Panel->Slot))
 {const int32 N=D?D->GetPlayerFacingSkillEffect().ToString().Len():0;S->SetSize(FVector2D(bDragLayout?300:380,FMath::Max(220.f,160.f+FMath::CeilToFloat(N/(bDragLayout?14.f:19.f))*25.f+(Extra.IsEmpty()?0.f:120.f))));}
 if(HasSkillDetailsOpen())RefreshSkillDetails();
}
void UEGHeroInfoWidget::PresentUnit(const UEGHeroDefinition* D,const FText& Summary,float Health,float MaxHealth,float Mana,float MaxMana)
{
 Present(D,Summary);
 if(auto* Bar=Cast<UProgressBar>(GetWidgetFromName(TEXT("InfoHealth"))))Bar->SetPercent(FMath::Clamp(Health/FMath::Max(1.f,MaxHealth),0.f,1.f));
 if(auto* Bar=Cast<UProgressBar>(GetWidgetFromName(TEXT("InfoMana"))))
 {Bar->SetPercent(FMath::Clamp(Mana/FMath::Max(1.f,MaxMana),0.f,1.f));Bar->SetVisibility(MaxMana>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
}
void UEGHeroInfoWidget::PresentAttributes(const FEGUnitInspectionView& View)
{
 const int32 PreviousBossPhase=DisplayedBossArtworkPhase;
 const bool bWasReadingSkill=HasSkillDetailsOpen()||bClosingSkillDetails;
 if(View.BossArtworkPhase!=PreviousBossPhase)PendingBossScrollPhase=0;
 bInspectedEnemy=View.bEnemy;
 UpdateEnemySkills(View);
 if(View.BossArtworkPhase!=DisplayedBossArtworkPhase)
 {
  DisplayedBossArtworkPhase=View.BossArtworkPhase;BossPhaseArtwork=nullptr;
  if(DisplayedBossArtworkPhase>0)
   BossPhaseArtwork=FEGVisualStyle::Texture(DisplayedBossArtworkPhase==2?TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase2"):TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase1"));
 }
 if(BossPhaseArtwork)if(auto* Card=Cast<UImage>(GetWidgetFromName(TEXT("InfoCard"))))
  if(Card->GetBrush().GetResourceObject()!=BossPhaseArtwork.Get())
  {Card->SetBrush(FEGCardPresentation::PortraitBrush(BossPhaseArtwork,FVector2D(300,420)));FEGCardPresentation::ApplyDefinitionLayers(WidgetTree,Card,View.Definition);}
 if(DisplayedBossArtworkPhase==0)PendingBossScrollPhase=0;
 else if((PreviousBossPhase>0&&PreviousBossPhase!=DisplayedBossArtworkPhase)||(PreviousBossPhase==0&&DisplayedBossArtworkPhase==2))
 {
  PendingBossScrollPhase=DisplayedBossArtworkPhase;
  if(!bWasReadingSkill||(!HasSkillDetailsOpen()&&!bClosingSkillDetails))ApplyPendingBossScroll();
 }
 InspectedLevel=FMath::Max(1,View.Level);
 bHasSkillOverride=View.bHasSkillOverride;SkillBasicOverride=View.SkillBasicOverride;
 SkillUltimateOverride=View.SkillUltimateOverride;SkillPassiveOverride=View.SkillPassiveOverride;
 if(auto* Button=Cast<UButton>(GetWidgetFromName(TEXT("Button_UltimateInfo"))))
 {
  const bool bMonsterNoUltimate=View.Definition&&View.Definition->MonsterAttack&&View.Definition->MonsterAttack->Ultimate==EEGMonsterUltimate::None;
  Button->SetIsEnabled(!View.bSummonUltimateUnavailable&&!bMonsterNoUltimate);
  Button->SetToolTipText(View.bSummonUltimateUnavailable?FText::FromString(TEXT("该召唤物不能释放独立大招")):bMonsterNoUltimate?FText::FromString(TEXT("无独立大招")):FText::GetEmpty());
 }
 if(EnemySkills.IsEmpty()&&View.bSummonUltimateUnavailable&&(OpenSkillIndex==1||PendingSkillIndex==1))CloseSkillDetails();
 if(HasSkillDetailsOpen())RefreshSkillDetails();
 auto Set=[this](const TCHAR* Name,const FString& Value)
 {if(auto* Text=Cast<UTextBlock>(GetWidgetFromName(Name)))Text->SetText(FText::FromString(Value));};
 Set(TEXT("InfoHealthValue"),FString::Printf(TEXT("%.0f / %.0f"),View.Health,View.Stats.MaxHealth));
 Set(TEXT("InfoManaValue"),FString::Printf(TEXT("%.0f / %.0f"),View.Mana,View.Stats.MaxMana));
 Set(TEXT("InfoAttackValue"),FString::Printf(TEXT("%.0f"),View.Stats.AttackPower));
 Set(TEXT("InfoDefenseValue"),FString::Printf(TEXT("%.0f"),View.Stats.Defense));
 Set(TEXT("InfoSpeedValue"),FString::Printf(TEXT("%.2f"),View.Stats.AttackSpeed));
 Set(TEXT("InfoShieldValue"),FString::Printf(TEXT("%.0f"),View.Shield));
 if(auto* Row=GetWidgetFromName(TEXT("InfoManaRow")))Row->SetVisibility(View.Stats.MaxMana>0.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}
void UEGHeroInfoWidget::UpdateEnemySkills(const FEGUnitInspectionView& View)
{
 auto* Row=Cast<UWrapBox>(GetWidgetFromName(TEXT("SkillActionRow")));
 if(!Row)return;
 if(EnemySkills.IsEmpty()&&View.EnemySkills.IsEmpty()&&View.BossArtworkPhase==0){EnemySkillsLayoutKey=TEXT("0");return;}
 FString Key=FString::FromInt(View.BossArtworkPhase);
 for(const auto& Skill:View.EnemySkills)Key+=TEXT("|")+Skill.Id.ToString()+TEXT(":")+Skill.IconPath;
 const bool bChanged=Key!=EnemySkillsLayoutKey;
 const FName PreviouslyOpen=EnemySkills.IsValidIndex(OpenSkillIndex)?EnemySkills[OpenSkillIndex].Id:NAME_None;
 EnemySkills=View.EnemySkills;
 if(!bChanged)return;
 EnemySkillsLayoutKey=Key;
 if(HasSkillDetailsOpen())FinishCloseSkillDetails();
 const bool bUseEnemy=!EnemySkills.IsEmpty();
 for(const FName Name:{FName(TEXT("Button_BasicInfo")),FName(TEXT("Button_UltimateInfo")),FName(TEXT("Button_PassiveInfo"))})
  if(auto* Button=GetWidgetFromName(Name))if(auto* Parent=Button->GetParent())Parent->SetVisibility(bUseEnemy?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
 while(EnemySkillButtons.Num()<EnemySkills.Num())
 {
  const int32 Index=EnemySkillButtons.Num();
  auto* Button=WidgetTree->ConstructWidget<UButton>();Button->SetStyle(FEGVisualStyle::Ghost());
  auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Button->SetContent(Column);
  auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("EnemyInspectionIcon_%d"),Index)));
  auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(80);IconSize->SetHeightOverride(80);IconSize->SetContent(Icon);
  Column->AddChildToVerticalBox(IconSize)->SetHorizontalAlignment(HAlign_Center);
  auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("EnemyInspectionLabel_%d"),Index)));
  Label->SetFont(FEGVisualStyle::Font(17));Label->SetAutoWrapText(true);Label->SetJustification(ETextJustify::Center);
  auto* LabelSize=WidgetTree->ConstructWidget<USizeBox>();LabelSize->SetWidthOverride(100);LabelSize->SetHeightOverride(34);LabelSize->SetContent(Label);
  Column->AddChildToVerticalBox(LabelSize)->SetHorizontalAlignment(HAlign_Center);
  auto* Underline=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("EnemyInspectionUnderline_%d"),Index)));
  Underline->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Champagne,0.f));Underline->SetVisibility(ESlateVisibility::Hidden);
  auto* UnderlineSize=WidgetTree->ConstructWidget<USizeBox>();UnderlineSize->SetWidthOverride(40);UnderlineSize->SetHeightOverride(2);UnderlineSize->SetContent(Underline);
  Column->AddChildToVerticalBox(UnderlineSize)->SetHorizontalAlignment(HAlign_Center);
  auto* ButtonSize=WidgetTree->ConstructWidget<USizeBox>();ButtonSize->SetWidthOverride(104);ButtonSize->SetHeightOverride(116);ButtonSize->SetContent(Button);Row->AddChildToWrapBox(ButtonSize);
  auto* Handler=NewObject<UEGInspectionSkillButtonHandler>(this);Handler->Owner=this;Handler->Index=Index;
  EnemySkillButtons.Add(Button);EnemySkillHandlers.Add(Handler);Button->OnClicked.AddUniqueDynamic(Handler,&UEGInspectionSkillButtonHandler::Click);
 }
 // The authored three player buttons stay in place. Reorder only the reusable
 // enemy containers and headers when the inspected skill set changes.
 for(const auto& Entry:EnemySkillButtons)if(auto* Button=Entry.Get())if(Button->GetParent())Button->GetParent()->RemoveFromParent();
 for(const auto& Entry:EnemySkillGroupHeaders)if(auto* Header=Entry.Get())Header->RemoveFromParent();
 if(View.BossArtworkPhase>0&&EnemySkillGroupHeaders.IsEmpty())
 {
  const TCHAR* Titles[]={TEXT("第一阶段"),TEXT("第二阶段"),TEXT("全场")};
  for(int32 I=0;I<3;++I)
  {
   auto* Header=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),FName(*FString::Printf(TEXT("EnemySkillGroup_%d"),I)));
   Header->SetWidthOverride(316.f);Header->SetHeightOverride(30.f);
   auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetFont(FEGVisualStyle::SerifFont(18));
   Label->SetColorAndOpacity(FSlateColor(EGPearlGold::Champagne));Label->SetText(FText::FromString(Titles[I]));
   Label->SetVisibility(ESlateVisibility::HitTestInvisible);Header->SetContent(Label);
   Header->SetVisibility(ESlateVisibility::HitTestInvisible);EnemySkillGroupHeaders.Add(Header);
  }
 }
 for(int32 I=0;I<EnemySkillButtons.Num();++I)
 {
  auto* Button=EnemySkillButtons[I].Get();if(!Button)continue;
  const bool bVisible=EnemySkills.IsValidIndex(I);
  if(bVisible&&View.BossArtworkPhase>0&&EnemySkillGroupHeaders.Num()==3)
  {
   const FName Id=EnemySkills[I].Id;
   if(I==0)Row->AddChildToWrapBox(EnemySkillGroupHeaders[0]);
   else if(Id==TEXT("mark"))Row->AddChildToWrapBox(EnemySkillGroupHeaders[1]);
   else if(Id==TEXT("enrage"))Row->AddChildToWrapBox(EnemySkillGroupHeaders[2]);
  }
  if(auto* Parent=Button->GetParent())Row->AddChildToWrapBox(Parent);
  if(auto* Parent=Button->GetParent())Parent->SetVisibility(bVisible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
  if(!bVisible)continue;
  const auto& Skill=EnemySkills[I];
  if(auto* Icon=Cast<UImage>(GetWidgetFromName(FName(*FString::Printf(TEXT("EnemyInspectionIcon_%d"),I)))))
  {
   const TCHAR* Names[]={TEXT("T_Enemy_Melee"),TEXT("T_Enemy_Shot"),TEXT("T_Enemy_Heal"),TEXT("T_Enemy_Shield"),TEXT("T_Enemy_EnhancedShot"),TEXT("T_Enemy_Speed"),TEXT("T_Enemy_Seed"),TEXT("T_Boss_Arrow"),TEXT("T_Boss_Charged"),TEXT("T_Boss_Pulse"),TEXT("T_Boss_Barrier"),TEXT("T_Boss_Mark"),TEXT("T_Boss_Spread"),TEXT("T_Boss_Mimic"),TEXT("T_Boss_Enrage")};
   int32 Cell=INDEX_NONE;for(int32 N=0;N<UE_ARRAY_COUNT(Names);++N)if(Skill.IconPath.EndsWith(Names[N])){Cell=N;break;}
   auto* Texture=FEGVisualStyle::Texture(TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/T_EnemySkillAtlas"));
   if(Texture&&Cell!=INDEX_NONE)
   {
    FSlateBrush Brush;Brush.SetResourceObject(Texture);Brush.ImageType=ESlateBrushImageType::FullColor;
    const float X=float(Cell%4)*.25f,Y=float(Cell/4)*.25f;
    Brush.SetUVRegion(FBox2D(FVector2D(X,Y),FVector2D(X+.25f,Y+.25f)));Icon->SetBrush(Brush);
   }
   Icon->SetVisibility(Texture&&Cell!=INDEX_NONE?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  }
  if(auto* Label=Cast<UTextBlock>(GetWidgetFromName(FName(*FString::Printf(TEXT("EnemyInspectionLabel_%d"),I)))))Label->SetText(Skill.Title);
  const bool bOtherPhase=(View.BossArtworkPhase==1&&Skill.Id==TEXT("mark"))||(View.BossArtworkPhase==1&&Skill.Id==TEXT("spread"))||(View.BossArtworkPhase==1&&Skill.Id==TEXT("mimic"))||(View.BossArtworkPhase==2&&(Skill.Id==TEXT("arrow")||Skill.Id==TEXT("pulse")));
  Button->SetRenderOpacity(bOtherPhase?.55f:1.f);
 }
 LastSkillLayoutViewport=-1.f;
 if(!PreviouslyOpen.IsNone())
  for(int32 I=0;I<EnemySkills.Num();++I)if(EnemySkills[I].Id==PreviouslyOpen){OpenSkillDetails(I);break;}
}
void UEGHeroInfoWidget::OpenEnemySkill(int32 Index){OpenSkillDetails(Index);}
void UEGHeroInfoWidget::ApplyPendingBossScroll()
{
 if(PendingBossScrollPhase==0||PendingBossScrollPhase!=DisplayedBossArtworkPhase||HasSkillDetailsOpen()||bClosingSkillDetails)return;
 const int32 Phase=PendingBossScrollPhase;PendingBossScrollPhase=0;
 if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("UnitInfoScroll"))))
  if(auto* Group=GetWidgetFromName(Phase==2?TEXT("EnemySkillGroup_1"):TEXT("EnemySkillGroup_0")))
   Scroll->ScrollWidgetIntoView(Group,false,EDescendantScrollDestination::TopOrLeft);
}
void UEGHeroInfoWidget::ToggleSkill()
{
 const auto* D=PresentedDefinition.Get();const auto* Info=D?D->CardInformation.Get():nullptr;
 if(!Info||!Info->bHasUltimate||!Info->Icon||!Info->Icon->GetPathName().StartsWith(TEXT("/Game/Assets/UI/SkillIcons/")))return;
 bSkillExpanded=!bSkillExpanded;
 SetSkillText(SkillHintRichText.Get(),SkillHintPlainText.Get(),FText::FromString(bSkillExpanded?TEXT("点击图标收起技能"):TEXT("点击图标查看技能")));
 if(auto* Body=GetWidgetFromName(TEXT("SkillContent")))Body->SetVisibility(bSkillExpanded?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
}
void UEGHeroInfoWidget::RequestClose(){OnCloseRequested.Broadcast();}
void UEGHeroInfoWidget::RequestSell(){OnSellRequested.Broadcast();}
void UEGHeroInfoWidget::CollapseSkill()
{bSkillExpanded=false;FinishCloseSkillDetails();if(auto* W=GetWidgetFromName(TEXT("SkillContent")))W->SetVisibility(ESlateVisibility::Collapsed);}

float UEGHeroInfoWidget::GetSkillDetailsWidth(float ViewWidth) const
{return (HasSkillDetailsOpen()||bClosingSkillDetails)&&808.f<=ViewWidth*.65f?808.f:FMath::Min(376.f,FMath::Max(1.f,ViewWidth));}
void UEGHeroInfoWidget::SetSkillLayoutWidth(float ViewWidth)
{
 const bool bInline=(HasSkillDetailsOpen()||bClosingSkillDetails)&&808.f>ViewWidth*.65f;
 const float ContentWidth=GetSkillDetailsWidth(ViewWidth);
 const float MainWidth=FMath::Min(376.f,FMath::Max(1.f,ViewWidth));
 const float ScrollWidth=FMath::Max(104.f,FMath::Min(328.f,MainWidth-48.f));
 // Reserve room for the scroll bar so the third 104px button is not clipped
 // or wrapped merely because the content becomes scrollable.
 const float ActionWidth=FMath::Max(104.f,ScrollWidth-12.f);
 const int32 Columns=FMath::Clamp(FMath::FloorToInt((ActionWidth+2.f)/106.f),1,3);
 const int32 Count=EnemySkills.IsEmpty()?3:EnemySkills.Num();
 int32 Rows=FMath::DivideAndRoundUp(Count,Columns);
 int32 GroupCount=0;
 if(DisplayedBossArtworkPhase>0&&!EnemySkills.IsEmpty())
 {
  int32 First=0,Second=0,All=0;int32 Group=0;
  for(const auto& Skill:EnemySkills)
  {if(Skill.Id==TEXT("mark"))Group=1;else if(Skill.Id==TEXT("enrage"))Group=2;if(Group==0)++First;else if(Group==1)++Second;else ++All;}
  Rows=FMath::DivideAndRoundUp(First,Columns)+FMath::DivideAndRoundUp(Second,Columns)+FMath::DivideAndRoundUp(All,Columns);GroupCount=3;
 }
 const float ActionHeight=FMath::Min(Rows*116.f+FMath::Max(0,Rows+GroupCount-1)*8.f+GroupCount*30.f,248.f);
 if(auto* Surface=Cast<UEGGradientSurface>(GetWidgetFromName(TEXT("UnitInfoSurface"))))
  Surface->SetSolidFraction(ContentWidth/(ContentWidth+140.f));
 if(FMath::IsNearlyEqual(LastSkillLayoutViewport,ViewWidth,.1f)&&bInline==bSkillDetailsInline)return;
 const bool bWasInline=bSkillDetailsInline;
 LastSkillLayoutViewport=ViewWidth;
 bSkillDetailsInline=bInline;
 if(auto* Actions=Cast<UWrapBox>(GetWidgetFromName(TEXT("SkillActionRow"))))Actions->SetWrapSize(ActionWidth);
 for(const auto& Entry:EnemySkillGroupHeaders)if(auto* Header=Entry.Get())Header->SetWidthOverride(ActionWidth);
 if(auto* Body=GetWidgetFromName(TEXT("UnitInfoBody")))Body->SetVisibility(bInline?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
 if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("UnitInfoScroll"))))
 {
  if(!bWasInline&&bInline)NormalInfoScrollOffset=Scroll->GetScrollOffset();
  if(auto* ScrollSlot=Cast<UCanvasPanelSlot>(Scroll->Slot))
  {
   ScrollSlot->SetAnchors(bInline?FAnchors(0,0):FAnchors(0,0,0,1));
   ScrollSlot->SetOffsets(FMargin(24,64,ScrollWidth,bInline?ActionHeight+12.f:72.f));
  }
  Scroll->SetVisibility(ESlateVisibility::Visible);
  // Inline mode sizes this view to the complete action row(s); it is not a
  // separate reading area and should not display or consume a scroll bar.
  const bool bActionNeedsScroll=Rows*116.f+FMath::Max(0,Rows+GroupCount-1)*8.f+GroupCount*30.f>ActionHeight;
  Scroll->SetScrollBarVisibility(bInline&&!bActionNeedsScroll?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
  Scroll->SetConsumeMouseWheel(bInline&&!bActionNeedsScroll?EConsumeMouseWheel::Never:EConsumeMouseWheel::WhenScrollingPossible);
  if(!bWasInline&&bInline)Scroll->ScrollToStart();
  else if(bWasInline&&!bInline)Scroll->SetScrollOffset(NormalInfoScrollOffset);
 }
 if(auto* Details=GetWidgetFromName(TEXT("ActionDetails")))if(auto* DetailsSlot=Cast<UCanvasPanelSlot>(Details->Slot))
  DetailsSlot->SetOffsets(FMargin(bInline?20.f:388.f,bInline?64.f+ActionHeight+12.f:64.f,bInline?FMath::Max(1.f,FMath::Min(420.f,MainWidth-40.f)):420.f,72.f));
}
void UEGHeroInfoWidget::CloseSkillDetails()
{
 if(!HasSkillDetailsOpen())return;
 OpenSkillIndex=INDEX_NONE;PendingSkillIndex=INDEX_NONE;bSwitchingSkillDetails=false;bClosingSkillDetails=true;UpdateSkillSelection();
 if(auto* Details=GetWidgetFromName(TEXT("ActionDetails")))Details->SetVisibility(ESlateVisibility::HitTestInvisible);
 if(SkillDetailsReveal<=0.f)FinishCloseSkillDetails();
}
void UEGHeroInfoWidget::FinishCloseSkillDetails()
{
 const float PreviousViewport=LastSkillLayoutViewport;
 OpenSkillIndex=INDEX_NONE;PendingSkillIndex=INDEX_NONE;bClosingSkillDetails=false;bSwitchingSkillDetails=false;SkillDetailsReveal=0.f;LastSkillLayoutViewport=-1.f;
 if(auto* Details=GetWidgetFromName(TEXT("ActionDetails")))Details->SetVisibility(ESlateVisibility::Collapsed);
 if(PreviousViewport>0.f)SetSkillLayoutWidth(PreviousViewport);
 else {bSkillDetailsInline=false;if(auto* Body=GetWidgetFromName(TEXT("UnitInfoBody")))Body->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
 UpdateSkillSelection();
 ApplyPendingBossScroll();
}
void UEGHeroInfoWidget::UpdateSkillSelection()
{
 const FName Names[]={TEXT("BasicInfoUnderline"),TEXT("UltimateInfoUnderline"),TEXT("PassiveInfoUnderline")};
 for(int32 I=0;I<3;++I)if(auto* W=GetWidgetFromName(Names[I]))W->SetVisibility(EnemySkills.IsEmpty()&&OpenSkillIndex==I?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
 for(int32 I=0;I<EnemySkillButtons.Num();++I)
  if(auto* W=GetWidgetFromName(FName(*FString::Printf(TEXT("EnemyInspectionUnderline_%d"),I))))W->SetVisibility(!EnemySkills.IsEmpty()&&OpenSkillIndex==I?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
}
void UEGHeroInfoWidget::OpenBasicDetails(){OpenSkillDetails(0);}
void UEGHeroInfoWidget::OpenUltimateDetails(){OpenSkillDetails(1);}
void UEGHeroInfoWidget::OpenPassiveDetails(){OpenSkillDetails(2);}
void UEGHeroInfoWidget::SetActionHover(int32 Index,bool bHovered)
{
 const FName Buttons[]={TEXT("Button_BasicInfo"),TEXT("Button_UltimateInfo"),TEXT("Button_PassiveInfo")};
 const FName Glows[]={TEXT("BasicInfoGlow"),TEXT("UltimateInfoGlow"),TEXT("PassiveInfoGlow")};
 if(Index>=0&&Index<3)
 {
  if(auto* Button=GetWidgetFromName(Buttons[Index]))Button->SetRenderOpacity(bHovered?1.f:.85f);
  if(auto* Glow=GetWidgetFromName(Glows[Index]))Glow->SetVisibility(bHovered?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
 }
}
void UEGHeroInfoWidget::HoverBasic(){SetActionHover(0,true);}
void UEGHeroInfoWidget::UnhoverBasic(){SetActionHover(0,false);}
void UEGHeroInfoWidget::HoverUltimate(){SetActionHover(1,true);}
void UEGHeroInfoWidget::UnhoverUltimate(){SetActionHover(1,false);}
void UEGHeroInfoWidget::HoverPassive(){SetActionHover(2,true);}
void UEGHeroInfoWidget::UnhoverPassive(){SetActionHover(2,false);}
void UEGHeroInfoWidget::OpenSkillDetails(int32 Index)
{
 if(!PresentedDefinition.IsValid()||Index<0||Index>=(EnemySkills.IsEmpty()?3:EnemySkills.Num()))return;
 if(bSwitchingSkillDetails&&PendingSkillIndex==Index){CloseSkillDetails();return;}
 if(OpenSkillIndex==Index){CloseSkillDetails();return;}
 if(HasSkillDetailsOpen()) {PendingSkillIndex=Index;bSwitchingSkillDetails=true;return;}
 bClosingSkillDetails=false;bSwitchingSkillDetails=false;PendingSkillIndex=INDEX_NONE;SkillDetailsReveal=0.f;SkillOpeningDuration=.12f;
 OpenSkillIndex=Index;LastSkillLayoutViewport=-1.f;RefreshSkillDetails();UpdateSkillSelection();
 if(auto* Details=GetWidgetFromName(TEXT("ActionDetails"))){Details->SetRenderOpacity(0.f);Details->SetVisibility(ESlateVisibility::SelfHitTestInvisible);}
 if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("ActionDetailsScroll"))))Scroll->ScrollToStart();
}
void UEGHeroInfoWidget::RefreshSkillDetails()
{
 const auto* D=PresentedDefinition.Get();if(!D||!HasSkillDetailsOpen())return;
 if(!EnemySkills.IsEmpty())
 {
  if(!EnemySkills.IsValidIndex(OpenSkillIndex))return;
  if(auto* Title=Cast<UTextBlock>(GetWidgetFromName(TEXT("ActionDetailsTitle"))))Title->SetText(EnemySkills[OpenSkillIndex].Title);
  if(auto* Body=Cast<UEGRichTextBlock>(GetWidgetFromName(TEXT("ActionDetailsBody"))))Body->SetHighlightedText(EnemySkills[OpenSkillIndex].Description);
  return;
 }
 const TCHAR* Titles[]={TEXT("普攻"),TEXT("大招"),TEXT("被动")};
 const EEGSkillInfoKind Kinds[]={EEGSkillInfoKind::Basic,EEGSkillInfoKind::Ultimate,EEGSkillInfoKind::Passive};
 FString TitleText=bHasSkillOverride&&D->HeroKind!=EEGHeroKind::ElysiaHerrscher?FString(Titles[OpenSkillIndex]):D->GetPlayerFacingSkillSectionName(Kinds[OpenSkillIndex]).ToString();

 if(auto* Title=Cast<UTextBlock>(GetWidgetFromName(TEXT("ActionDetailsTitle"))))Title->SetText(FText::FromString(TitleText));
 const FText Overrides[]={SkillBasicOverride,SkillUltimateOverride,SkillPassiveOverride};
 FText Description=bHasSkillOverride?Overrides[OpenSkillIndex]:D->GetPlayerFacingSkillSection(Kinds[OpenSkillIndex]);
 if(Description.IsEmpty())Description=FText::FromString(TEXT("暂无说明"));
 if(auto* Body=Cast<UEGRichTextBlock>(GetWidgetFromName(TEXT("ActionDetailsBody"))))
  {if(bInspectedEnemy)Body->SetHighlightedText(Description);else Body->SetSkillLevelText(Description,InspectedLevel);}
}
void UEGHeroInfoWidget::SetSaleAvailable(bool bAvailable)
{if(auto* Label=Cast<UTextBlock>(GetWidgetFromName(TEXT("SellInfoLabel"))))Label->SetText(FText::FromString(TEXT("出售")));if(auto* W=GetWidgetFromName(TEXT("Button_SellInfo")))W->SetVisibility(bAvailable?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
void UEGHeroInfoWidget::SetReturnAvailable(bool bAvailable)
{if(!bAvailable)return;if(auto* Label=Cast<UTextBlock>(GetWidgetFromName(TEXT("SellInfoLabel"))))Label->SetText(FText::FromString(TEXT("返回手牌")));if(auto* W=GetWidgetFromName(TEXT("Button_SellInfo")))W->SetVisibility(ESlateVisibility::Visible);}
