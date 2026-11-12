#include "UI/Widget/EGDemoPanel.h"

#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Character/EGChessUnit.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/CompositeFont.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGStigmataBalance.h"
#include "Data/EGStigmataDefinition.h"
#include "Engine/FontFace.h"
#include "Engine/Texture2D.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGRichText.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif

namespace
{
AEGDemoGameMode* StigmataMode(const UUserWidget* Widget)
{
 return Widget&&Widget->GetWorld()?Widget->GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
}

// All identities are explicit: unknown source art stays empty rather than showing a different hero.
TArray<FString> StigmataHeroes(FName Id)
{
 if(Id==TEXT("OriginFlower"))return {TEXT("Human")};
 if(Id==TEXT("LoveBeginning")||Id==TEXT("Flawless")||Id==TEXT("Story"))return {TEXT("Elysia")};
 if(Id==TEXT("Unity"))return {TEXT("Elysia"),TEXT("Hua"),TEXT("Griseo")};
 if(Id==TEXT("Discipline"))return {TEXT("Aponia")};
 if(Id==TEXT("Gold"))return {TEXT("Eden")};
 if(Id==TEXT("Helix"))return {TEXT("VillV")};
 if(Id==TEXT("Setsuna"))return {TEXT("Sakura")};
 if(Id==TEXT("Infinity"))return {TEXT("Mobius")};
 if(Id==TEXT("Stars"))return {TEXT("Griseo")};
 if(Id==TEXT("Vicissitude"))return {TEXT("Hua")};
 if(Id==TEXT("Reverie"))return {TEXT("Pardo")};
 if(Id==TEXT("Deliverance"))return {TEXT("Kevin")};
 if(Id==TEXT("Decimation"))return {TEXT("Kalpas")};
 if(Id==TEXT("Wisdom"))return {TEXT("Su")};
 if(Id==TEXT("Daybreak"))return {TEXT("Kosma")};
 if(Id==TEXT("DuskStreet"))return {TEXT("Pardo"),TEXT("Sakura"),TEXT("Aponia")};
 if(Id==TEXT("Kindred"))return {TEXT("VillV"),TEXT("Mobius")};
 if(Id==TEXT("LastBanquet"))return {TEXT("Aponia"),TEXT("Eden"),TEXT("Elysia")};
 return {};
}
UTexture2D* StigmataIdentityArt(const FString& Hero,bool bSignet)
{
 if(Hero==TEXT("Human"))
 {
  // The route creates Herrscher of Human. Her crest is Elysia's, but the
  // portrait must be the actual Human form rather than the original hero.
  return LoadObject<UTexture2D>(nullptr,bSignet
   ? TEXT("/Game/Assets/UI/Textures/StigmataSelection20261105/T_UI_Signet_Elysia.T_UI_Signet_Elysia")
   : TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Portrait_HumanOfficial.T_StigmaC_Portrait_HumanOfficial"),nullptr,LOAD_NoWarn);
 }
 if(!bSignet&&Hero==TEXT("VillV"))
 {
  auto* Portrait=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Portrait_VillVCompleteHat.T_StigmaC_Portrait_VillVCompleteHat"),nullptr,LOAD_NoWarn);
  auto* Backlight=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Backlight_VillVCompleteHat.T_StigmaC_Backlight_VillVCompleteHat"),nullptr,LOAD_NoWarn);
  if(Portrait&&Backlight)return Portrait;
 }
 const TCHAR* Folder=bSignet?TEXT("StigmataSelection20261105"):TEXT("StigmataSelectionC20261105");
 const FString Path=FString::Printf(TEXT("/Game/Assets/UI/Textures/%s/T_UI_%s_%s.T_UI_%s_%s"),Folder,
  bSignet?TEXT("Signet"):TEXT("Portrait"),*Hero,
  bSignet?TEXT("Signet"):TEXT("Portrait"),*Hero);
 return LoadObject<UTexture2D>(nullptr,*Path);
}

void UpdateStigmataHeroGlow(UWidgetTree* Tree,int32 ChoiceIndex,float Target,float BacklightTarget,float DeltaTime)
{
 if(!Tree)return;
 for(int32 Hero=0;Hero<3;++Hero)
  if(auto* Glow=Cast<UImage>(Tree->FindWidget(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),ChoiceIndex,Hero)))))
  {
   if(!Glow->GetBrush().GetResourceObject())
    {Glow->SetRenderOpacity(0.f);Glow->SetVisibility(ESlateVisibility::Collapsed);continue;}
   // The registered light pass uses a dynamic material; the remaining sample
   // glows still use texture brushes and keep their independent intensity.
   const float HeroTarget=Cast<UMaterialInstanceDynamic>(Glow->GetBrush().GetResourceObject())?BacklightTarget:Target;
   const float Opacity=FMath::FInterpTo(Glow->GetRenderOpacity(),HeroTarget,DeltaTime,7.f);
   Glow->SetRenderOpacity(Opacity);
   Glow->SetVisibility(HeroTarget>0.f||Opacity>.001f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  }
}

void AlignStigmataBacklight(UWidgetTree* Tree,int32 ChoiceIndex,int32 HeroIndex)
{
 if(!Tree)return;
 auto* ArtArea=Tree->FindWidget(FName(*FString::Printf(TEXT("StigmataArtArea%d"),ChoiceIndex)));
 auto* Portrait=Cast<UImage>(Tree->FindWidget(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),ChoiceIndex,HeroIndex))));
 auto* Glow=Cast<UImage>(Tree->FindWidget(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),ChoiceIndex,HeroIndex))));
 if(!ArtArea||!Portrait||!Glow||!Cast<UMaterialInstanceDynamic>(Glow->GetBrush().GetResourceObject()))return;
 auto* Fit=Glow->GetParent();auto* Slot=Fit?Cast<UCanvasPanelSlot>(Fit->Slot):nullptr;
 if(!Slot)return;
 const FBox2D PortraitUV=Portrait->GetBrush().GetUVRegion();
 const FBox2D GlowUV=Glow->GetBrush().GetUVRegion();
 const FVector2D UVSize=PortraitUV.Max-PortraitUV.Min;
 const FGeometry& PortraitGeometry=Portrait->GetCachedGeometry();
 if(!PortraitUV.bIsValid||!GlowUV.bIsValid||UVSize.X<=0||UVSize.Y<=0||PortraitGeometry.GetLocalSize().IsNearlyZero())return;
 const FGeometry& ArtGeometry=ArtArea->GetCachedGeometry();
 if(ArtGeometry.GetLocalSize().IsNearlyZero())return;
 const FVector2D TopLeft=ArtGeometry.AbsoluteToLocal(PortraitGeometry.LocalToAbsolute(FVector2D::ZeroVector));
 const FVector2D BottomRight=ArtGeometry.AbsoluteToLocal(PortraitGeometry.LocalToAbsolute(PortraitGeometry.GetLocalSize()));
 const FVector2D Extent=BottomRight-TopLeft;
 const FVector2D Position=TopLeft+FVector2D((GlowUV.Min.X-PortraitUV.Min.X)/UVSize.X*Extent.X,(GlowUV.Min.Y-PortraitUV.Min.Y)/UVSize.Y*Extent.Y);
 const FVector2D Size((GlowUV.Max.X-GlowUV.Min.X)/UVSize.X*Extent.X,(GlowUV.Max.Y-GlowUV.Min.Y)/UVSize.Y*Extent.Y);
 if(Size.X<=0||Size.Y<=0)return;
 if(!Slot->GetPosition().Equals(Position,.05f))Slot->SetPosition(Position);
 if(!Slot->GetSize().Equals(Size,.05f))Slot->SetSize(Size);
}

void UpdateStigmataSelectionDecor(UWidgetTree* Tree,int32 ChoiceIndex,bool bActive)
{
 if(!Tree)return;
 for(const TCHAR* Prefix:{TEXT("StigmataSelectionMarker"),TEXT("StigmataSelectionLightLine"),TEXT("StigmataSelectedCorners")})
  if(auto* Image=Cast<UImage>(Tree->FindWidget(FName(*FString::Printf(TEXT("%s%d"),Prefix,ChoiceIndex)))))
   Image->SetVisibility(bActive&&Image->GetBrush().GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}

void SetOwnedGroupTint(UWidgetTree* Tree,int32 Index,const FLinearColor& Color)
{
 if(!Tree||Index==INDEX_NONE)return;
 for(int32 Hero=0;Hero<3;++Hero)
  if(auto* Icon=Cast<UImage>(Tree->FindWidget(FName(*FString::Printf(TEXT("StigmataOwnedGroupIcon%d_%d"),Index,Hero)))))
   Icon->SetColorAndOpacity(Color);
}

void UpdateStigmataHoldProgress(UWidgetTree* Tree,int32 ChoiceIndex,float Progress,bool bVisible)
{
 if(!Tree)return;
 if(auto* Image=Cast<UImage>(Tree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldProgress%d"),ChoiceIndex)))))
 {
  auto* Material=Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject());
  if(Material)Material->SetScalarParameterValue(TEXT("Progress"),FMath::Clamp(Progress,0.f,1.f));
  Image->SetVisibility(bVisible&&Material?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 }
}

void ResetStigmataTransition(UWidgetTree* Tree,UBorder* Panel)
{
 if(Panel)
 {
  Panel->SetRenderOpacity(1.f);
  Panel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.009f,.012f,.022f,.86f),0.f));
 }
 if(Tree)if(auto* Blur=Cast<UBackgroundBlur>(Tree->FindWidget(TEXT("StigmataBoardBlur"))))Blur->SetApplyAlphaToBlur(false);
}

// Character-specific upper-body framing based on the official image's actual
// face position. Crop edges are softly feathered by the UI materials.
FBox2D StigmataPortraitRegion(const FString& Hero)
{
 static const TMap<FString,FBox2D> Regions{
  // 1198x1151 official Human art: keep the crown tip, face, raised hand and
  // upper body. This is already the final solo crop, not a full-height source.
  {TEXT("Human"),FBox2D(FVector2D(.28,.075),FVector2D(.80,.78))},
  {TEXT("Aponia"),FBox2D(FVector2D(.395,.089),FVector2D(.744,1))},
  {TEXT("Eden"),FBox2D(FVector2D(.350,.089),FVector2D(.780,1))},
  {TEXT("Elysia"),FBox2D(FVector2D(.349,.036),FVector2D(.796,1))},
  {TEXT("Griseo"),FBox2D(FVector2D(.430,.09),FVector2D(.770,.99))},
  {TEXT("Hua"),FBox2D(FVector2D(.399,.067),FVector2D(.790,1))},
  {TEXT("Kalpas"),FBox2D(FVector2D(.340,.04),FVector2D(.750,1))},
  {TEXT("Kevin"),FBox2D(FVector2D(.370,.075),FVector2D(.767,1))},
  {TEXT("Kosma"),FBox2D(FVector2D(.382,0),FVector2D(.758,1))},
  {TEXT("Mobius"),FBox2D(FVector2D(.390,.026),FVector2D(.800,1))},
  {TEXT("Pardo"),FBox2D(FVector2D(.360,.07),FVector2D(.750,1))},
  {TEXT("Sakura"),FBox2D(FVector2D(.340,0),FVector2D(.750,1))},
  {TEXT("Su"),FBox2D(FVector2D(.360,.048),FVector2D(.780,1))},
  {TEXT("VillV"),FBox2D(FVector2D(.395,0),FVector2D(.765,1))}
 };
 if(const FBox2D* Region=Regions.Find(Hero))return *Region;
 return FBox2D(FVector2D::ZeroVector,FVector2D(1,1));
}

// Group options use tighter, face-centered crops so three people remain
// legible at the same card size. These are source UVs, not stretched art.
FBox2D StigmataGroupPortraitRegion(const FString& Hero)
{
 static const TMap<FString,FBox2D> Regions{
  {TEXT("Pardo"),FBox2D(FVector2D(.42,.07),FVector2D(.67,.58))},
  {TEXT("Sakura"),FBox2D(FVector2D(.435,0),FVector2D(.685,.51))},
  {TEXT("Aponia"),FBox2D(FVector2D(.45,.089),FVector2D(.70,.60))},
  {TEXT("Eden"),FBox2D(FVector2D(.43,.089),FVector2D(.68,.60))},
  {TEXT("Elysia"),FBox2D(FVector2D(.46,.036),FVector2D(.71,.547))},
  {TEXT("VillV"),FBox2D(FVector2D(.455,0),FVector2D(.705,.51))},
  {TEXT("Mobius"),FBox2D(FVector2D(.51,.026),FVector2D(.76,.537))},
  {TEXT("Hua"),FBox2D(FVector2D(.43,.067),FVector2D(.68,.578))},
  {TEXT("Griseo"),FBox2D(FVector2D(.49,.09),FVector2D(.74,.601))}
 };
 if(const FBox2D* Region=Regions.Find(Hero))return *Region;
 const FBox2D Solo=StigmataPortraitRegion(Hero);
 const double Center=(Solo.Min.X+Solo.Max.X)*.5;
 return FBox2D(FVector2D(FMath::Max(0.,Center-.125),Solo.Min.Y),FVector2D(FMath::Min(1.,Center+.125),FMath::Min(1.,Solo.Min.Y+.511)));
}

FString StigmataSummary(const FEGStigmataDefinition& Definition)
{
 const auto& B=UEGStigmataBalanceSettings::Get();
 auto P=[](float Value){return FMath::RoundToInt(Value*100.f);};
 const FName Id=Definition.Id;
 if(Id==TEXT("LoveBeginning"))return FString::Printf(TEXT("全体英桀攻速提高%d%%\n爱莉希雅与人律额外提高%d%%"),P(B.LoveTeamSpeed),P(B.LoveSpeed));
 if(Id==TEXT("Flawless"))return FString::Printf(TEXT("全体英桀技能效果提高%d%%\n爱莉强化射击延长%g秒，人律终结伤害提高%d%%"),P(B.FlawlessSkillBonus),B.FlawlessSeconds,P(B.FlawlessFinisherMultiplier-1.f));
 if(Id==TEXT("OriginFlower"))return TEXT("所有上阵英桀离场，获得一张人之律者\n等级为离场英桀等级总和的2倍");
 if(Id==TEXT("Story"))return FString::Printf(TEXT("上阵上限提高至9，获得九名英桀赠卡\n最终准备阶段基础英桀最低%d级"),B.StoryMinimumLevel);
 if(Id==TEXT("Unity"))return FString::Printf(TEXT("保留阵容与人口\n英桀、召唤物及机兵伤害提高%d%%，生命提高%d%%"),P(B.UnityDamageBonus),P(B.UnityHealthBonus));
 if(Id==TEXT("Deliverance"))return FString::Printf(TEXT("友方英桀成功必杀后\n自身伤害提高%d%%，持续%g秒"),P(B.DeliveranceBonus),B.DeliveranceDuration);
 if(Id==TEXT("Discipline"))return FString::Printf(TEXT("连续普攻同一目标时\n伤害逐次提高%d%%，最高%d%%"),P(B.DisciplineStep),P(B.DisciplineStep*3));
 if(Id==TEXT("Gold"))return FString::Printf(TEXT("开战获得%d%%最大法力\n成功必杀返还%d%%实际消耗法力"),P(B.GoldOpeningMana),P(B.GoldRefund));
 if(Id==TEXT("Helix"))return FString::Printf(TEXT("必杀后储存%d次炮击\n后续普攻造成%d%%攻击力范围伤害"),B.HelixCharges,P(B.HelixDamage));
 if(Id==TEXT("Decimation"))return FString::Printf(TEXT("损失生命提高伤害，最高%d%%\n直接伤害获得%d%%吸血"),P(B.DecimationMaxBonus),P(B.DecimationLifesteal));
 if(Id==TEXT("Wisdom"))return FString::Printf(TEXT("全队每%d次有效普攻\n触发%d%%平均攻击力的范围冲击"),B.WisdomAttacks,P(B.WisdomDamage));
 if(Id==TEXT("Setsuna"))return FString::Printf(TEXT("闪避下次直接攻击后\n攻速提高%d%%，持续%g秒，冷却%g秒"),P(B.SetsunaSpeed),B.SetsunaSpeedDuration,B.SetsunaCooldown);
 if(Id==TEXT("Daybreak"))return FString::Printf(TEXT("普攻施加%g秒撕裂，每秒造成%d%%攻击力伤害\n对护盾伤害提高%d%%"),B.DaybreakDuration,P(B.DaybreakDamage),P(B.DaybreakShieldBonus));
 if(Id==TEXT("Infinity"))return FString::Printf(TEXT("全体角色、战斗召唤物及机兵\n最大生命与攻击力提高%d%%"),P(B.InfinityBonus));
 if(Id==TEXT("Stars"))return FString::Printf(TEXT("格蕾修升变为天行格蕾修\n技能效果额外提高%d%%"),P(B.FormSkillBonus));
 if(Id==TEXT("Vicissitude"))return FString::Printf(TEXT("华升变为崩落华\n技能效果额外提高%d%%"),P(B.FormSkillBonus));
 if(Id==TEXT("Reverie"))return FString::Printf(TEXT("立即获得%d崩坏能\n后续准备阶段每次获得%d次免费刷新"),B.ReverieGold,B.ReverieExtraFreeRefreshes);
 if(Id==TEXT("DuskStreet"))return FString::Printf(TEXT("帕朵、樱、阿波尼亚同时上阵\n全队获%d%%最大生命护盾，本关首次有效结算+%d崩坏能"),P(B.DuskShield),B.DuskGold);
 if(Id==TEXT("Kindred"))return FString::Printf(TEXT("维尔薇、梅比乌斯同时上阵\n维尔薇必杀首次直接命中后追加%d%%攻击力炮击"),P(B.KindredDamage));
 if(Id==TEXT("LastBanquet"))return FString::Printf(TEXT("阿波尼亚、伊甸、爱莉同时上阵\n成员必杀为其他存活成员恢复%d%%最大法力，冷却%g秒"),P(B.BanquetMana),B.BanquetCooldown);
 return Definition.Description.ToString();
}

float StigmataChoiceLeft(int32 Index)
{
 static constexpr float Positions[]={.085f,.377f,.673f};
 return Positions[FMath::Clamp(Index,0,2)];
}

FString StigmataRunEconomy(const UEGMatchManager* Match,FName Id)
{
 if(!Match)return FString();
 const int64 Earned=Match->GetStigmataGoldEarned(Id);
 const int64 Saved=Match->GetStigmataRefreshSavings(Id);
 if(Earned<=0&&Saved<=0)return FString();
 FString Result=TEXT("本局经济：");
 if(Earned>0)Result+=FString::Printf(TEXT("获得%lld崩坏能"),static_cast<long long>(Earned));
 if(Earned>0&&Saved>0)Result+=TEXT("，");
 if(Saved>0)Result+=FString::Printf(TEXT("刷新节省%lld"),static_cast<long long>(Saved));
 return Result;
}

UTextBlock* StigmataText(UWidgetTree* Tree,const FString& Text,int32 Size)
{
 auto* Label=Tree->ConstructWidget<UTextBlock>();
 Label->SetText(FText::FromString(Text));
 Label->SetFont(FEGVisualStyle::BodyFont(Size));
 Label->SetColorAndOpacity(FSlateColor(FLinearColor(.96f,.91f,.97f)));
 Label->SetAutoWrapText(true);
 return Label;
}

/**
 * Stigmata prose uses the Narrative role; numeric tokens inside the text are rendered
 * with the Numeric face + emphasis colour by UEGNumberHighlightDecorator.
 */
UEGRichTextBlock* StigmataNarrationText(UWidgetTree* Tree,const FString& Text,int32 Size)
{
 auto* Label=Tree->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass());
 Label->ApplyTextStyle(FEGVisualStyle::NarrationFont(Size),FLinearColor(.96f,.91f,.97f));
 Label->SetAutoWrapText(true);
 Label->SetVisibility(ESlateVisibility::HitTestInvisible);
 Label->SetHighlightedText(FText::FromString(Text));
 return Label;
}

UTextBlock* StigmataSelectionText(UWidgetTree* Tree,const FString& Text,int32 Size)
{
 auto* Label=StigmataText(Tree,Text,Size);
 // Headings/titles use the Serif role (Noto Serif CJK SC) supplied by FEGVisualStyle.
 Label->SetFont(FEGVisualStyle::SerifFont(Size));
 return Label;
}

UButton* StigmataButton(UWidgetTree* Tree,const FString& Text,int32 Size,FName Name=NAME_None)
{
 auto* Button=Tree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
 FButtonStyle Style;
 Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.14f,.09f,.19f,.98f),6.f,FLinearColor(.55f,.44f,.58f),1.f));
 Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.25f,.16f,.28f,1.f),6.f,FLinearColor(.95f,.76f,.48f),2.f));
 Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.32f,.19f,.31f,1.f),6.f,FLinearColor(.98f,.82f,.58f),2.f));
 Button->SetStyle(Style);
 if(!Text.IsEmpty())
 {
  auto* Label=StigmataText(Tree,Text,Size);
  Label->SetJustification(ETextJustify::Center);
  Button->SetContent(Label);
 }
 return Button;
}

void Place(UCanvasPanel* Canvas,UWidget* Widget,FAnchors Anchors,FMargin Offsets,int32 Layer=0)
{
 auto* Slot=Canvas->AddChildToCanvas(Widget);
 Slot->SetAnchors(Anchors);Slot->SetOffsets(Offsets);Slot->SetZOrder(Layer);
}

void SetStigmataCroppedTexture(UImage* Image,UTexture2D* Texture,const FBox2D& UV)
{
 if(!Image)return;
 if(!Texture){Image->SetBrush(FSlateNoResource());return;}
#if WITH_EDITOR
 FTextureCompilingManager::Get().FinishCompilation({Texture});
#endif
 FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageType=ESlateBrushImageType::FullColor;
 Brush.SetResourceObject(Texture);Brush.SetUVRegion(UV);
 Brush.SetImageSize(FVector2D(Texture->GetSizeX()*(UV.Max.X-UV.Min.X),Texture->GetSizeY()*(UV.Max.Y-UV.Min.Y)));
 Image->SetBrush(Brush);
}

void PlaceStigmataDivider(UWidgetTree* Tree,UCanvasPanel* Canvas,UTexture2D* Texture,
 const TCHAR* Name,const FAnchors& Anchors,bool bMirror)
{
 if(!Texture)return;
 auto* Line=Tree->ConstructWidget<UImage>(UImage::StaticClass(),FName(Name));
 FSlateBrush Brush;Brush.ImageType=ESlateBrushImageType::FullColor;Brush.SetResourceObject(Texture);
 // The generated 2172×724 source has large transparent margins. Keep the
 // entire painted line and end diamond, while matching the cropped pixel ratio.
 const FBox2D UV(FVector2D(.135,.48),FVector2D(.965,.52));
 Brush.SetUVRegion(UV);
 Brush.SetImageSize(FVector2D(Texture->GetSizeX()*(UV.Max.X-UV.Min.X),Texture->GetSizeY()*(UV.Max.Y-UV.Min.Y)));
 Brush.Mirroring=bMirror?ESlateBrushMirrorType::Horizontal:ESlateBrushMirrorType::NoMirror;
 Line->SetBrush(Brush);Line->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* Fit=Tree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Line);
 Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
 if(auto* ContentSlot=Cast<UScaleBoxSlot>(Line->Slot))
  {ContentSlot->SetHorizontalAlignment(HAlign_Center);ContentSlot->SetVerticalAlignment(VAlign_Center);}
 Place(Canvas,Fit,Anchors,FMargin(0),1);
}

FString BondStatus(FName Id,const UEGHeroRoster* Roster)
{
 TArray<EEGHeroKind> Required;
 TArray<FString> Names;
 if(Id==TEXT("DuskStreet")){Required={EEGHeroKind::Pardofelis,EEGHeroKind::Sakura,EEGHeroKind::Aponia};Names={TEXT("帕朵菲莉丝"),TEXT("樱"),TEXT("阿波尼亚")};}
 else if(Id==TEXT("Kindred")){Required={EEGHeroKind::VillV,EEGHeroKind::Mobius};Names={TEXT("维尔薇"),TEXT("梅比乌斯")};}
 else if(Id==TEXT("LastBanquet")){Required={EEGHeroKind::Aponia,EEGHeroKind::Eden,EEGHeroKind::Elysia};Names={TEXT("阿波尼亚"),TEXT("伊甸"),TEXT("爱莉希雅")};}
 else return FString();
 TSet<EEGHeroKind> Deployed;
 if(Roster)for(const auto& Hero:Roster->GetHeroes())
 {
  if(!Hero.bDeployed||!Hero.Definition)continue;
  EEGHeroKind Kind=Hero.Definition->HeroKind;
  if(Kind==EEGHeroKind::ElysiaHerrscher)Kind=EEGHeroKind::Elysia;
  Deployed.Add(Kind);
 }
 FString Result;bool bAll=true;
 for(int32 Index=0;Index<Required.Num();++Index)
 {
  const bool bPresent=Deployed.Contains(Required[Index]);bAll&=bPresent;
  Result+=FString::Printf(TEXT("\n%s %s"),bPresent?TEXT("已上阵"):TEXT("未上阵"),*Names[Index]);
 }
 return FString(bAll?TEXT("已激活"):TEXT("未激活"))+Result;
}
}

void UEGDemoPanel::EnsureStigmaPanel()
{
 if(StigmaPanel)return;
 SetIsFocusable(true);
 auto* Root=WidgetTree?Cast<UCanvasPanel>(WidgetTree->RootWidget):nullptr;
 if(!Root)return;

 for(int32 Index=0;Index<3;++Index)
 {
  const FName Name=Index==0?FName(TEXT("Button_StigmataCollection")):FName(*FString::Printf(TEXT("Button_StigmataOwned%d"),Index));
  auto* Row=StigmataButton(WidgetTree,FString(),18,Name);
  FButtonStyle Transparent;Transparent.SetNormal(FSlateNoResource());Transparent.SetHovered(FSlateNoResource());
  Transparent.SetPressed(FSlateNoResource());Transparent.SetDisabled(FSlateNoResource());Row->SetStyle(Transparent);
  auto* RowCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();
  if(auto* RowSlot=Cast<UButtonSlot>(Row->SetContent(RowCanvas)))
  {RowSlot->SetHorizontalAlignment(HAlign_Fill);RowSlot->SetVerticalAlignment(VAlign_Fill);RowSlot->SetPadding(FMargin(0));}
  auto* NameLabel=StigmataText(WidgetTree,TEXT(""),18);
  NameLabel->SetJustification(ETextJustify::Right);NameLabel->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(RowCanvas,NameLabel,FAnchors(0,0,1,1),FMargin(0,8,76,8));
  auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataOwnedIcon%d"),Index)));
  Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(RowCanvas,Icon,FAnchors(1,.5f),FMargin(-56,-28,56,56));
  // Group bonds have no single catalogue image. Compose their actual signets
  // into a larger footprint so the separate identities stay legible.
  auto* Group=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(*FString::Printf(TEXT("StigmataOwnedGroup%d"),Index)));
  Group->SetVisibility(ESlateVisibility::Collapsed);
  Place(RowCanvas,Group,FAnchors(1,.5f),FMargin(-64,-32,64,64));
  for(int32 Hero=0;Hero<3;++Hero)
  {
   auto* GroupIcon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataOwnedGroupIcon%d_%d"),Index,Hero)));
   GroupIcon->SetVisibility(ESlateVisibility::Collapsed);
   Place(Group,GroupIcon,FAnchors(0,0,.5f,.5f),FMargin(0),1);
  }
  auto* GroupGradient=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataOwnedGroupGradient%d"),Index)));
  if(auto* Light=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_SelectionLightLine.T_StigmaC_SelectionLightLine"),nullptr,LOAD_NoWarn))GroupGradient->SetBrushFromTexture(Light,true);
  else GroupGradient->SetBrush(FSlateNoResource());
  GroupGradient->SetColorAndOpacity(FLinearColor(.98f,.82f,.61f,.48f));
  GroupGradient->SetRenderTransformPivot(FVector2D(.5f,.5f));GroupGradient->SetRenderTransformAngle(-35.f);
  GroupGradient->SetVisibility(ESlateVisibility::Collapsed);
  Place(Group,GroupGradient,FAnchors(.32f,.36f,.68f,.64f),FMargin(0),2);
  auto* GroupSlash=StigmataText(WidgetTree,TEXT("/"),18);
  GroupSlash->Rename(*FString::Printf(TEXT("StigmataOwnedGroupSlash%d"),Index));
  GroupSlash->SetJustification(ETextJustify::Center);GroupSlash->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.94f,.82f)));
  {auto Font=GroupSlash->GetFont();Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(.04f,.02f,.08f,1.f);GroupSlash->SetFont(Font);}
  GroupSlash->SetVisibility(ESlateVisibility::Collapsed);
  Place(Group,GroupSlash,FAnchors(.34f,.25f,.66f,.75f),FMargin(0),3);
  Place(Root,Row,FAnchors(1,0),FMargin(-264,280.f+64.f*Index,240,56),40);
  Row->SetVisibility(ESlateVisibility::Collapsed);StigmataRows.Add(Row);StigmataRowNames.Add(NameLabel);
 }
 StigmataCollectionButton=StigmataRows[0];
 StigmataRows[0]->OnClicked.AddUniqueDynamic(this,&ThisClass::OpenStigmataDetails0);
 StigmataRows[1]->OnClicked.AddUniqueDynamic(this,&ThisClass::OpenStigmataDetails1);
 StigmataRows[2]->OnClicked.AddUniqueDynamic(this,&ThisClass::OpenStigmataDetails2);
 StigmataRows[0]->OnHovered.AddUniqueDynamic(this,&ThisClass::PreviewStigmata0);
 StigmataRows[1]->OnHovered.AddUniqueDynamic(this,&ThisClass::PreviewStigmata1);
 StigmataRows[2]->OnHovered.AddUniqueDynamic(this,&ThisClass::PreviewStigmata2);
 for(const auto& Row:StigmataRows)if(Row)Row->OnUnhovered.AddUniqueDynamic(this,&ThisClass::HideStigmataPreview);

 StigmataPreviewPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StigmataPreviewPanel"));
 StigmataPreviewPanel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.05f,.035f,.075f,.94f),6.f));
 StigmataPreviewPanel->SetPadding(FMargin(14));StigmataPreviewPanel->SetVisibility(ESlateVisibility::Collapsed);
 StigmataPreviewText=StigmataNarrationText(WidgetTree,TEXT(""),17);StigmataPreviewText->SetWrapTextAt(360.f);
 StigmataPreviewPanel->SetContent(StigmataPreviewText);
 Place(Root,StigmataPreviewPanel,FAnchors(1,0),FMargin(-680,280,400,220),45);
 StigmataPreviewPanel->SetVisibility(ESlateVisibility::Collapsed);

 StigmataCollectionPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StigmataCollectionPanel"));
 StigmataCollectionPanel->SetBrushColor(FLinearColor(.04f,.025f,.06f,.94f));
 StigmataCollectionPanel->SetVisibility(ESlateVisibility::Collapsed);
 Place(Root,StigmataCollectionPanel,FAnchors(1,0),FMargin(-824,280,544,440),80);
 auto* CollectionCanvas=WidgetTree->ConstructWidget<UCanvasPanel>();StigmataCollectionPanel->SetContent(CollectionCanvas);
 Place(CollectionCanvas,StigmataText(WidgetTree,TEXT("本局往世刻印"),28),FAnchors(.06f,.03f,.75f,.11f),FMargin(0));
 auto* Close=StigmataButton(WidgetTree,TEXT("×"),22,TEXT("Button_CloseStigmataCollection"));
 Close->OnClicked.AddUniqueDynamic(this,&ThisClass::CloseStigmataCollection);
 Place(CollectionCanvas,Close,FAnchors(.83f,.025f,.97f,.11f),FMargin(0));
 auto* CollectionScroll=WidgetTree->ConstructWidget<UScrollBox>();
 Place(CollectionCanvas,CollectionScroll,FAnchors(.06f,.13f,.94f,.94f),FMargin(0));
 StigmataCollectionList=WidgetTree->ConstructWidget<UVerticalBox>();CollectionScroll->AddChild(StigmataCollectionList);

 // Combat events have a short, non-interactive caption. Settlement results
 // fade after the next preparation starts and never intercept input.
 StigmataFeedbackText=StigmataText(WidgetTree,TEXT(""),16);
 StigmataFeedbackText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.83f,.54f)));
 StigmataFeedbackText->SetJustification(ETextJustify::Right);
 StigmataFeedbackText->SetVisibility(ESlateVisibility::Collapsed);
 Place(Root,StigmataFeedbackText,FAnchors(1,0),FMargin(-440,472,416,50),42);
 StigmataResultsPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StigmataBattleResults"));
 StigmataResultsPanel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.04f,.025f,.06f,.87f),6.f));
 StigmataResultsPanel->SetPadding(FMargin(12));StigmataResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
 StigmataResultsText=StigmataNarrationText(WidgetTree,TEXT(""),15);
 StigmataResultsText->SetWrapTextAt(350.f);StigmataResultsPanel->SetContent(StigmataResultsText);
 Place(Root,StigmataResultsPanel,FAnchors(0,0),FMargin(24,150,380,235),36);

 StigmaPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("StigmaSkipPanel"));
 StigmaPanel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.009f,.012f,.022f,.86f),0.f));StigmaPanel->SetPadding(FMargin(0));
 StigmaPanel->SetVisibility(ESlateVisibility::Collapsed);
 Place(Root,StigmaPanel,FAnchors(0,0,1,1),FMargin(0),100);
 auto* Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();StigmaPanel->SetContent(Canvas);
 // Blur the live board beneath the translucent reward overlay; the portrait
 // and generated components are later siblings and stay crisp.
 auto* BoardBlur=WidgetTree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(),TEXT("StigmataBoardBlur"));
 BoardBlur->SetVisibility(ESlateVisibility::HitTestInvisible);
 BoardBlur->SetApplyAlphaToBlur(false);BoardBlur->SetBlurStrength(6.f);
 BoardBlur->SetOverrideAutoRadiusCalculation(true);BoardBlur->SetBlurRadius(12);
 Place(Canvas,BoardBlur,FAnchors(0,0,1,1),FMargin(0),0);
 // The reward UI is assembled from separately generated transparent assets.
 // Their UImage/brush owners keep the texture resources alive for Slate.
 auto* PlateTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_InfoPlateV2.T_StigmaC_InfoPlateV2"));
 auto* IdlePlateTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_InfoPlateIdleV2.T_StigmaC_InfoPlateIdleV2"),nullptr,LOAD_NoWarn);
 const bool bRevisedIdlePlate=IdlePlateTexture!=nullptr;
 if(!IdlePlateTexture)IdlePlateTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_InfoPlateIdle.T_StigmaC_InfoPlateIdle"),nullptr,LOAD_NoWarn);
 auto* CornersTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_SelectedCorners.T_StigmaC_SelectedCorners"),nullptr,LOAD_NoWarn);
 auto* CornersTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_StigmaC_Backlight.M_UI_StigmaC_Backlight"),nullptr,LOAD_NoWarn);
 // The single-rule generated plate keeps the long, scrollable details legible;
 // the V2 two-section plate is reserved for the three summary presentations.
 auto* DetailsTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_InfoPlate.T_StigmaC_InfoPlate"));
 auto* RingTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_ProgressRing.T_StigmaC_ProgressRing"));
 auto* RevealTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_StigmaC_ProgressReveal.M_UI_StigmaC_ProgressReveal"),nullptr,LOAD_NoWarn);
 auto* DividerTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Divider.T_StigmaC_Divider"));
 auto* MarkerTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_SelectedMarker.T_StigmaC_SelectedMarker"),nullptr,LOAD_NoWarn);
 auto* OwnedTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_OwnedCapsule.T_StigmaC_OwnedCapsule"),nullptr,LOAD_NoWarn);
 auto* SelectionLineTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_SelectionLightLine.T_StigmaC_SelectionLightLine"),nullptr,LOAD_NoWarn);
#if WITH_EDITOR
 if(DividerTexture)FTextureCompilingManager::Get().FinishCompilation({DividerTexture});
#endif
 if(!PlateTexture||!RingTexture||!DividerTexture)
  UE_LOG(LogTemp,Warning,TEXT("Stigmata selection UI textures missing: InfoPlate=%s ProgressRing=%s Divider=%s."),
   PlateTexture?TEXT("loaded"):TEXT("missing"),RingTexture?TEXT("loaded"):TEXT("missing"),
   DividerTexture?TEXT("loaded"):TEXT("missing"));
 if(!LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_StigmaPortraitClean.M_UI_StigmaPortraitClean")))
  UE_LOG(LogTemp,Warning,TEXT("Stigmata selection portrait cleanup material is missing; original portraits will be shown."));
 auto* Emblem=WidgetTree->ConstructWidget<UImage>();
 Emblem->SetBrush(FSlateNoResource());Emblem->SetVisibility(ESlateVisibility::Collapsed);
 UObject* EmblemResource=nullptr;
 if(auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_FlameMoth.M_UI_FlameMoth")))
  EmblemResource=Material;
 else if(auto* Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_UI_FlameMoth.T_UI_FlameMoth")))
  EmblemResource=Texture;
 if(EmblemResource)
 {
  // UImage::SetBrushFromMaterial/Texture changes only the resource and leaves
  // a previous NoResource draw type intact. Provide a complete image brush;
  // SetDesiredSizeOverride is also ineffective before the Slate image exists.
  FSlateBrush EmblemBrush;EmblemBrush.DrawAs=ESlateBrushDrawType::Image;
  EmblemBrush.ImageType=ESlateBrushImageType::FullColor;EmblemBrush.SetResourceObject(EmblemResource);
  EmblemBrush.SetImageSize(FVector2D(300.f,222.1f));Emblem->SetBrush(EmblemBrush);
  Emblem->SetVisibility(ESlateVisibility::HitTestInvisible);
 }
 auto* EmblemFit=WidgetTree->ConstructWidget<UScaleBox>();
 EmblemFit->SetStretch(EStretch::ScaleToFit);EmblemFit->SetContent(Emblem);EmblemFit->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Canvas,EmblemFit,FAnchors(.025f,.018f,.061f,.085f),FMargin(0),1);
 auto* Section=StigmataSelectionText(WidgetTree,TEXT("英桀之铭"),22);
 Section->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Canvas,Section,FAnchors(.067f,.030f,.22f,.075f),FMargin(0),1);
 StigmataHeading=StigmataSelectionText(WidgetTree,TEXT("选择刻印"),32);StigmataHeading->SetJustification(ETextJustify::Center);
 StigmataHeading->SetColorAndOpacity(FSlateColor(FLinearColor(.96f,.90f,.80f)));
 StigmataHeading->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Canvas,StigmataHeading,FAnchors(.31f,.020f,.69f,.085f),FMargin(0),1);
 if(OwnedTexture)
 {
  auto* OwnedPlate=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("StigmataRewardOwnedPlate"));
  SetStigmataCroppedTexture(OwnedPlate,OwnedTexture,FBox2D(FVector2D(.05,.29),FVector2D(.95,.71)));
  OwnedPlate->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* OwnedFit=WidgetTree->ConstructWidget<UScaleBox>();OwnedFit->SetStretch(EStretch::ScaleToFit);OwnedFit->SetContent(OwnedPlate);
  OwnedFit->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Canvas,OwnedFit,FAnchors(.85f,.022f,.98f,.067f),FMargin(0),1);
 }
 auto* OwnedCount=StigmataSelectionText(WidgetTree,TEXT(""),16);
 OwnedCount->Rename(TEXT("StigmataRewardOwnedCount"));
 OwnedCount->SetJustification(ETextJustify::Right);OwnedCount->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Canvas,OwnedCount,FAnchors(.86f,.030f,.965f,.070f),FMargin(0),2);
 StigmataInstruction=StigmataSelectionText(WidgetTree,TEXT("长按选择"),17);
 StigmataInstruction->SetJustification(ETextJustify::Center);StigmataInstruction->SetColorAndOpacity(FSlateColor(FLinearColor(.78f,.76f,.73f)));
 StigmataInstruction->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(Canvas,StigmataInstruction,FAnchors(.35f,.94f,.65f,.99f),FMargin(0),1);
 PlaceStigmataDivider(WidgetTree,Canvas,DividerTexture,TEXT("StigmataTitleLineLeft"),FAnchors(.18f,.025f,.43f,.065f),false);
 PlaceStigmataDivider(WidgetTree,Canvas,DividerTexture,TEXT("StigmataTitleLineRight"),FAnchors(.57f,.025f,.82f,.065f),true);
 PlaceStigmataDivider(WidgetTree,Canvas,DividerTexture,TEXT("StigmataFooterLineLeft"),FAnchors(.18f,.94f,.40f,.98f),false);
 PlaceStigmataDivider(WidgetTree,Canvas,DividerTexture,TEXT("StigmataFooterLineRight"),FAnchors(.60f,.94f,.82f,.98f),true);
 for(int32 Index=0;Index<3;++Index)
 {
  auto* Choice=StigmataButton(WidgetTree,FString(),18,FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),Index)));StigmataChoices.Add(Choice);
  FButtonStyle Style;
  Style.SetNormal(FSlateNoResource());Style.SetHovered(FSlateNoResource());Style.SetPressed(FSlateNoResource());Style.SetDisabled(FSlateNoResource());
  Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0);
  Choice->SetStyle(Style);Choice->SetRenderTransformPivot(FVector2D(.5f,.5f));
  const float Left=StigmataChoiceLeft(Index);
  Place(Canvas,Choice,FAnchors(Left,.12f,Left+.242f,.844f),FMargin(0),2);
  auto* Marker=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataSelectionMarker%d"),Index)));
  SetStigmataCroppedTexture(Marker,MarkerTexture,FBox2D(FVector2D(.40,.33),FVector2D(.60,.67)));
  Marker->SetVisibility(ESlateVisibility::Collapsed);
  auto* MarkerFit=WidgetTree->ConstructWidget<UScaleBox>();MarkerFit->SetStretch(EStretch::ScaleToFit);MarkerFit->SetContent(Marker);
  MarkerFit->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Canvas,MarkerFit,FAnchors(Left+.096f,.067f,Left+.146f,.157f),FMargin(0),3);
  auto* SelectionLine=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataSelectionLightLine%d"),Index)));
  SetStigmataCroppedTexture(SelectionLine,SelectionLineTexture,FBox2D(FVector2D(.05,.34),FVector2D(.95,.66)));
  SelectionLine->SetVisibility(ESlateVisibility::Collapsed);
  auto* LineFit=WidgetTree->ConstructWidget<UScaleBox>();LineFit->SetStretch(EStretch::ScaleToFit);LineFit->SetContent(SelectionLine);
  LineFit->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Canvas,LineFit,FAnchors(Left,.56f,Left+.242f,.64f),FMargin(0),3);
  auto* Card=WidgetTree->ConstructWidget<UCanvasPanel>();
  if(auto* ChoiceSlot=Cast<UButtonSlot>(Choice->SetContent(Card)))
   {ChoiceSlot->SetHorizontalAlignment(HAlign_Fill);ChoiceSlot->SetVerticalAlignment(VAlign_Fill);ChoiceSlot->SetPadding(FMargin(0));}
  auto* ArtArea=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(*FString::Printf(TEXT("StigmataArtArea%d"),Index)));
  ArtArea->SetClipping(EWidgetClipping::Inherit);
  ArtArea->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Card,ArtArea,FAnchors(0,0,1,.663f),FMargin(0));
  for(int32 Hero=0;Hero<3;++Hero)
  {
   auto* Glow=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),Index,Hero)));
   Glow->SetBrush(FSlateNoResource());Glow->SetRenderOpacity(0.f);Glow->SetVisibility(ESlateVisibility::Collapsed);
   Glow->SetColorAndOpacity(FLinearColor(1.f,.86f,.70f,1.f));
   auto* GlowFit=WidgetTree->ConstructWidget<UScaleBox>();
   GlowFit->SetStretch(EStretch::ScaleToFit);GlowFit->SetContent(Glow);GlowFit->SetVisibility(ESlateVisibility::HitTestInvisible);
   if(auto* GlowSlot=Cast<UScaleBoxSlot>(Glow->Slot))
    {GlowSlot->SetHorizontalAlignment(HAlign_Center);GlowSlot->SetVerticalAlignment(VAlign_Center);}
   Place(ArtArea,GlowFit,FAnchors(0,0,1,1),FMargin(0));
  }
  for(int32 Hero=0;Hero<3;++Hero)
  {
   auto* Portrait=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),Index,Hero)));
   Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
   auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Portrait);
   Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
   Place(ArtArea,Fit,FAnchors(0,0,1,1),FMargin(0));
  }
  auto* InfoPlate=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataInfoPlate%d"),Index)));
  if(bRevisedIdlePlate)SetStigmataCroppedTexture(InfoPlate,IdlePlateTexture,FBox2D(FVector2D(0,.158),FVector2D(1,1)));
  else if(IdlePlateTexture||PlateTexture)InfoPlate->SetBrushFromTexture(IdlePlateTexture?IdlePlateTexture:PlateTexture,true);
  else InfoPlate->SetBrush(FSlateNoResource());
  InfoPlate->SetVisibility(IdlePlateTexture||PlateTexture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  // Crop only the revised plate's area above its top rule; mapping below
  // that rule remains identical to the former .60..1.0 full-image layout.
  const float PlateTop=bRevisedIdlePlate?.6632f:.626f;
  const float CornersTop=bRevisedIdlePlate?.60f:.626f;
  Place(Card,InfoPlate,FAnchors(0,PlateTop,1,1.f),FMargin(0),1);
  auto* Corners=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataSelectedCorners%d"),Index)));
  if(auto* CornersMaterial=CornersTexture&&CornersTemplate?UMaterialInstanceDynamic::Create(CornersTemplate,this):nullptr)
  {
   CornersMaterial->SetTextureParameterValue(TEXT("LightTexture"),CornersTexture);
   CornersMaterial->SetVectorParameterValue(TEXT("LightCropUV"),FLinearColor(0.f,1.f,.001f,1.1f));
   FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageType=ESlateBrushImageType::FullColor;
   Brush.SetResourceObject(CornersMaterial);Brush.SetImageSize(FVector2D(1659.f,948.f));Corners->SetBrush(Brush);
  }
  else Corners->SetBrush(FSlateNoResource());
  Corners->SetVisibility(ESlateVisibility::Collapsed);
  Place(Card,Corners,FAnchors(0,CornersTop,1,1.f),FMargin(0),1);
  if(PlateTexture)
  {
   // Reuse the actual generated divider, rotated to separate sigil and title.
   // This is a tiny UV slice of the supplied artwork, not a drawn rectangle.
   auto* Divider=WidgetTree->ConstructWidget<UImage>();
   FSlateBrush DividerBrush;DividerBrush.ImageType=ESlateBrushImageType::FullColor;
   DividerBrush.SetResourceObject(PlateTexture);
   DividerBrush.SetUVRegion(FBox2D(FVector2D(.20,.495),FVector2D(.80,.505)));
   DividerBrush.SetImageSize(FVector2D(160.f,6.f));Divider->SetBrush(DividerBrush);
   Divider->SetRenderTransformPivot(FVector2D(.5f,.5f));Divider->SetRenderTransformAngle(90.f);
   Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
   Place(Card,Divider,FAnchors(.22f,.715f,.40f,.72f),FMargin(0),2);
  }
  for(int32 Hero=0;Hero<3;++Hero)
  {
   auto* Symbol=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataSignet%d_%d"),Index,Hero)));
   Symbol->SetVisibility(ESlateVisibility::HitTestInvisible);
   auto* SymbolFit=WidgetTree->ConstructWidget<UScaleBox>();SymbolFit->SetStretch(EStretch::ScaleToFit);SymbolFit->SetContent(Symbol);
   SymbolFit->SetVisibility(ESlateVisibility::HitTestInvisible);
   Place(Card,SymbolFit,FAnchors(.06f+Hero*.065f,.66f,.12f+Hero*.065f,.78f),FMargin(0),2);
   if(Hero==0)StigmataChoiceImages.Add(Symbol);
  }
  // The artwork's soft light forms the diagonal blend between a two-member
  // bond's symbols. Both widgets are purely decorative and never intercept input.
  auto* BondGradient=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataBondGradient%d"),Index)));
  if(SelectionLineTexture)BondGradient->SetBrushFromTexture(SelectionLineTexture,true);
  else BondGradient->SetBrush(FSlateNoResource());
  BondGradient->SetColorAndOpacity(FLinearColor(.98f,.82f,.61f,.48f));
  BondGradient->SetRenderTransformPivot(FVector2D(.5f,.5f));BondGradient->SetRenderTransformAngle(-35.f);
  BondGradient->SetVisibility(ESlateVisibility::Collapsed);
  Place(Card,BondGradient,FAnchors(.145f,.694f,.235f,.754f),FMargin(0),2);
  auto* BondSlash=StigmataSelectionText(WidgetTree,TEXT("/"),22);
  BondSlash->Rename(*FString::Printf(TEXT("StigmataBondSlash%d"),Index));
  BondSlash->SetJustification(ETextJustify::Center);BondSlash->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.94f,.82f)));
  {auto Font=BondSlash->GetFont();Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(.04f,.02f,.08f,1.f);BondSlash->SetFont(Font);}
  BondSlash->SetVisibility(ESlateVisibility::Collapsed);
  Place(Card,BondSlash,FAnchors(.16f,.686f,.22f,.770f),FMargin(0),3);
  auto* Title=StigmataSelectionText(WidgetTree,TEXT(""),28);Title->SetVisibility(ESlateVisibility::HitTestInvisible);
  Title->SetColorAndOpacity(FSlateColor(FLinearColor(.96f,.92f,.86f)));
  Place(Card,Title,FAnchors(.39f,.675f,.96f,.73f),FMargin(0),2);StigmataChoiceTitles.Add(Title);
  auto* Category=StigmataSelectionText(WidgetTree,TEXT(""),16);Category->SetVisibility(ESlateVisibility::HitTestInvisible);
  Category->SetColorAndOpacity(FSlateColor(FLinearColor(.82f,.76f,.67f,1.f)));
  Place(Card,Category,FAnchors(.39f,.735f,.96f,.785f),FMargin(0),2);StigmataChoiceDescriptions.Add(Category);
  auto* Summary=StigmataNarrationText(WidgetTree,TEXT(""),16);
  Summary->Rename(*FString::Printf(TEXT("StigmataChoiceSummary%d"),Index));
  Summary->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Card,Summary,FAnchors(.08f,.82f,.94f,.935f),FMargin(0),2);StigmataChoiceSummaries.Add(Summary);
  auto* DetailHint=StigmataSelectionText(WidgetTree,TEXT("查看详情"),14);
  DetailHint->SetJustification(ETextJustify::Right);DetailHint->SetVisibility(ESlateVisibility::HitTestInvisible);
  DetailHint->SetColorAndOpacity(FSlateColor(FLinearColor(.82f,.77f,.68f,1.f)));
  Place(Card,DetailHint,FAnchors(.52f,.925f,.915f,.975f),FMargin(0),2);
  auto* HoldRing=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataHoldRing%d"),Index)));
  if(RingTexture)HoldRing->SetBrushFromTexture(RingTexture,true);
  else HoldRing->SetBrush(FSlateNoResource());
  HoldRing->SetRenderOpacity(.24f);HoldRing->SetVisibility(ESlateVisibility::Collapsed);
  // Place outside the button, inside the full-screen overlay, so the ring
  // cannot be clipped by the choice button's own allocated geometry.
  Place(Canvas,HoldRing,FAnchors(Left+.121f,.8626f),FMargin(-60.f,-60.f,120.f,120.f),5);
  auto* HoldProgress=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StigmataHoldProgress%d"),Index)));
  if(auto* Reveal=RevealTemplate?UMaterialInstanceDynamic::Create(RevealTemplate,this):nullptr)
  {
   Reveal->SetScalarParameterValue(TEXT("Progress"),0.f);
   FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageType=ESlateBrushImageType::FullColor;
   Brush.SetResourceObject(Reveal);Brush.SetImageSize(FVector2D(120.f,120.f));HoldProgress->SetBrush(Brush);
  }
  else HoldProgress->SetBrush(FSlateNoResource());
  HoldProgress->SetVisibility(ESlateVisibility::Collapsed);
  Place(Canvas,HoldProgress,FAnchors(Left+.121f,.8626f),FMargin(-60.f,-60.f,120.f,120.f),6);
  auto* HoldLabel=StigmataSelectionText(WidgetTree,TEXT("0%"),14);
  HoldLabel->Rename(*FString::Printf(TEXT("StigmataHoldLabel%d"),Index));
  HoldLabel->SetJustification(ETextJustify::Center);HoldLabel->SetVisibility(ESlateVisibility::Collapsed);
  Place(Canvas,HoldLabel,FAnchors(Left+.121f,.8626f),FMargin(-60.f,-13.f,120.f,26.f),7);
 }
 StigmataChoices[0]->OnClicked.AddUniqueDynamic(this,&ThisClass::SelectStigmata0);
 StigmataChoices[1]->OnClicked.AddUniqueDynamic(this,&ThisClass::SelectStigmata1);
 StigmataChoices[2]->OnClicked.AddUniqueDynamic(this,&ThisClass::SelectStigmata2);
 StigmataChoices[0]->OnHovered.AddUniqueDynamic(this,&ThisClass::HoverStigmata0);
 StigmataChoices[1]->OnHovered.AddUniqueDynamic(this,&ThisClass::HoverStigmata1);
 StigmataChoices[2]->OnHovered.AddUniqueDynamic(this,&ThisClass::HoverStigmata2);
 StigmataChoices[0]->OnPressed.AddUniqueDynamic(this,&ThisClass::PressStigmata0);
 StigmataChoices[1]->OnPressed.AddUniqueDynamic(this,&ThisClass::PressStigmata1);
 StigmataChoices[2]->OnPressed.AddUniqueDynamic(this,&ThisClass::PressStigmata2);
 for(const auto& Choice:StigmataChoices)
  {Choice->OnUnhovered.AddUniqueDynamic(this,&ThisClass::UnhoverStigmata);Choice->OnReleased.AddUniqueDynamic(this,&ThisClass::ReleaseStigmata);}
 for(int32 Index=0;Index<StigmataChoices.Num();++Index)
 {
  if(Index>0)StigmataChoices[Index]->SetNavigationRuleExplicit(EUINavigation::Left,StigmataChoices[Index-1]);
  if(Index+1<StigmataChoices.Num())StigmataChoices[Index]->SetNavigationRuleExplicit(EUINavigation::Right,StigmataChoices[Index+1]);
 }
 StigmataDetailsPanel=WidgetTree->ConstructWidget<UBorder>();
 FSlateBrush DetailsBrush;
 if(DetailsTexture||PlateTexture){DetailsBrush.ImageType=ESlateBrushImageType::FullColor;DetailsBrush.SetResourceObject(DetailsTexture?DetailsTexture:PlateTexture);}
 else DetailsBrush=FSlateRoundedBoxBrush(FLinearColor(.025f,.029f,.040f,.98f),0.f,FLinearColor(.7f,.65f,.56f,.7f),1.f);
 // A dark backing keeps the underlying offer text from showing through the
 // existing translucent image, while the image remains the visual surface.
 StigmataDetailsPanel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.009f,.012f,.020f,.98f),0.f));
 StigmataDetailsPanel->SetPadding(FMargin(0));StigmataDetailsPanel->SetVisibility(ESlateVisibility::Collapsed);
 auto* DetailsLayout=WidgetTree->ConstructWidget<UCanvasPanel>();
 auto* DetailsBackdrop=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("StigmataChoiceDetailsBackdrop"));
 DetailsBackdrop->SetBrush(DetailsBrush);DetailsBackdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
 Place(DetailsLayout,DetailsBackdrop,FAnchors(0,0,1,1),FMargin(0),0);
 auto* DetailsScroll=WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("StigmataChoiceDetailsScroll"));
 StigmataDetailsText=StigmataNarrationText(WidgetTree,TEXT(""),16);
 StigmataDetailsText->Rename(TEXT("StigmataChoiceDetailsText"));
 DetailsScroll->AddChild(StigmataDetailsText);
 Place(DetailsLayout,DetailsScroll,FAnchors(.055f,.12f,.945f,.79f),FMargin(0),1);
 auto* CloseDetails=StigmataButton(WidgetTree,TEXT("关闭"),14,TEXT("Button_CloseStigmataChoiceDetails"));
 FButtonStyle CloseStyle;
 FSlateBrush CloseNormal=DetailsBrush;CloseNormal.TintColor=FSlateColor(FLinearColor(1.f,1.f,1.f,.40f));
 FSlateBrush CloseHover=DetailsBrush;CloseHover.TintColor=FSlateColor(FLinearColor(1.f,1.f,1.f,.75f));
 FSlateBrush ClosePressed=DetailsBrush;ClosePressed.TintColor=FSlateColor(FLinearColor::White);
 CloseStyle.SetNormal(CloseNormal);CloseStyle.SetHovered(CloseHover);CloseStyle.SetPressed(ClosePressed);CloseStyle.SetDisabled(CloseNormal);
 CloseStyle.NormalPadding=FMargin(0);CloseStyle.PressedPadding=FMargin(0);CloseDetails->SetStyle(CloseStyle);
 CloseDetails->OnClicked.AddUniqueDynamic(this,&ThisClass::CloseStigmataChoiceDetails);
 Place(DetailsLayout,CloseDetails,FAnchors(.38f,.83f,.62f,.95f),FMargin(0),2);StigmataDetailsPanel->SetContent(DetailsLayout);
 Place(Canvas,StigmataDetailsPanel,FAnchors(.29f,.31f,.71f,.69f),FMargin(0),4);
 StigmataConfirm=nullptr;

}

void UEGDemoPanel::PresentStigmaSkip(int32 Revision)
{
 EnsureStigmaPanel();
 if(!StigmaPanel||Revision==INDEX_NONE)return;
 // The next reward page can be published during the first selection's
 // synchronous commit or its celebration. Defer it without losing its revision.
 if(bStigmataCommitting||bStigmataCelebrating){bPendingStigmataRefresh=true;return;}
 if(StigmataCollectionPanel)StigmataCollectionPanel->SetVisibility(ESlateVisibility::Collapsed);
 RefreshStigmataOffer();
}

void UEGDemoPanel::RefreshStigmataOffer()
{
 auto* Mode=StigmataMode(this);
 if(!Mode||!Mode->GetMatch()||Mode->GetMatch()->GetPhase()!=EEGMatchPhase::Reward){HideStigmaSkip();return;}
 CancelStigmataHold(false);bStigmataCelebrating=false;bStigmataDeferredHide=false;bPendingStigmataRefresh=false;
 ResetStigmataTransition(WidgetTree,StigmaPanel);
 ResetStigmataChoiceDetails();
 bStigmataOfferIsRoute=Mode->IsChoosingEndgameRoute();
 StigmataOfferedIds=bStigmataOfferIsRoute?Mode->GetEndgameRouteOffer():Mode->GetStigmataOffer();
 StigmaRevision=Mode->GetStigmataRevision();
 StigmaPanel->SetVisibility(ESlateVisibility::Visible);
 StigmataHeading->SetText(FText::FromString(bStigmataOfferIsRoute?TEXT("选择终局刻印"):TEXT("选择刻印")));
 if(auto* Owned=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("StigmataRewardOwnedCount"))))
  Owned->SetText(FText::FromString(FString::Printf(TEXT("已获刻印  %d"),Mode->GetMatch()->GetStigmataOwned().Num())));
 if(StigmataDetailsText)StigmataDetailsText->SetHighlightedText(FText::GetEmpty());
 if(StigmataInstruction)StigmataInstruction->SetText(FText::FromString(TEXT("长按选择")));
 auto* PortraitTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_StigmaPortraitClean.M_UI_StigmaPortraitClean"));
 auto* BacklightTemplate=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_StigmaC_Backlight.M_UI_StigmaC_Backlight"),nullptr,LOAD_NoWarn);
 for(int32 Index=0;Index<StigmataChoices.Num();++Index)
 {
  UpdateStigmataSelectionDecor(WidgetTree,Index,false);
  const auto* Definition=StigmataOfferedIds.IsValidIndex(Index)?UEGStigmataCatalog::Find(StigmataOfferedIds[Index]):nullptr;
  auto* Choice=StigmataChoices[Index].Get();Choice->SetVisibility(Definition?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
  Choice->SetIsEnabled(true);Choice->SetRenderScale(FVector2D(1));Choice->SetRenderOpacity(1.f);Choice->SetColorAndOpacity(FLinearColor::White);
  for(int32 Hero=0;Hero<3;++Hero)
   if(auto* Glow=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),Index,Hero)))))
    {Glow->SetBrush(FSlateNoResource());Glow->SetRenderOpacity(0.f);Glow->SetVisibility(ESlateVisibility::Collapsed);}
  if(auto* Ring=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),Index))))Ring->SetVisibility(ESlateVisibility::Collapsed);
  UpdateStigmataHoldProgress(WidgetTree,Index,0.f,false);
  if(auto* Label=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldLabel%d"),Index))))Label->SetVisibility(ESlateVisibility::Collapsed);
  if(!Definition)continue;
  StigmataChoiceTitles[Index]->SetText(Definition->Name);
  FString CharacterName=Definition->Category.ToString();
  if(CharacterName.RemoveFromStart(TEXT("刻印")))CharacterName=CharacterName.TrimStartAndEnd();
  if(CharacterName==TEXT("爱莉"))CharacterName=TEXT("爱莉希雅");
  StigmataChoiceDescriptions[Index]->SetText(FText::FromString(CharacterName));
  if(auto* Summary=StigmataChoiceSummaries.IsValidIndex(Index)?StigmataChoiceSummaries[Index].Get():nullptr)
  {
   FString SummaryText=StigmataSummary(*Definition);
   if(bStigmataOfferIsRoute)
   {
    if(Definition->Id==TEXT("OriginFlower")&&Mode->GetRoster()&&!Mode->GetRoster()->GetHeroes().ContainsByPredicate([](const FEGHeroInstance& Hero){return Hero.bDeployed;}))
     SummaryText=TEXT("当前没有上阵英桀，无法汇聚。\n请选择其他终局刻印。");
   }
   else
   {
    const FString BondState=BondStatus(Definition->Id,Mode->GetRoster());
    if(!BondState.IsEmpty())SummaryText=BondState.Left(BondState.Find(TEXT("\n")))+TEXT("  ")+SummaryText;
   }
   Summary->SetHighlightedText(FText::FromString(SummaryText));
  }
  const TArray<FString> Heroes=StigmataHeroes(Definition->Id);
  if(auto* Gradient=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataBondGradient%d"),Index)))))
   Gradient->SetVisibility(Heroes.Num()==2&&Gradient->GetBrush().GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  if(auto* Slash=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataBondSlash%d"),Index))))
   Slash->SetVisibility(Heroes.Num()==2?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  // Solos can extend gently into the gaps, as in the approved mockup.
  // Bond choices keep separate, non-overlapping subslots to protect faces.
  if(auto* ArtArea=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataArtArea%d"),Index))))
   if(auto* ArtSlot=Cast<UCanvasPanelSlot>(ArtArea->Slot))
   {
    ArtSlot->SetAnchors(Heroes.Num()==1?FAnchors(-.075f,0,1.075f,.663f):FAnchors(0,0,1,.663f));
    ArtSlot->SetOffsets(FMargin(0));
   }
  for(int32 Hero=0;Hero<3;++Hero)
  {
   auto* Portrait=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),Index,Hero))));
   auto* Glow=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),Index,Hero))));
   auto* Symbol=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataSignet%d_%d"),Index,Hero))));
   const bool bHasHero=Heroes.IsValidIndex(Hero);
   UTexture2D* PortraitArt=bHasHero?StigmataIdentityArt(Heroes[Hero],false):nullptr;
   UTexture2D* SignetArt=bHasHero?StigmataIdentityArt(Heroes[Hero],true):nullptr;
   const bool bHuman=bHasHero&&Heroes[Hero]==TEXT("Human");
   const bool bKevin=bHasHero&&Heroes[Hero]==TEXT("Kevin");
   const bool bVillVComplete=PortraitArt&&PortraitArt->GetFName()==TEXT("T_StigmaC_Portrait_VillVCompleteHat");
   const bool bSoloMobius=bHasHero&&Heroes.Num()==1&&Heroes[Hero]==TEXT("Mobius");
   const bool bGroup=Heroes.Num()>1;
   FBox2D UV=bHasHero?(bGroup?StigmataGroupPortraitRegion(Heroes[Hero]):StigmataPortraitRegion(Heroes[Hero])):FBox2D(FVector2D::ZeroVector,FVector2D(1,1));
   if(bVillVComplete)
    UV=bGroup?FBox2D(FVector2D(.38,.05),FVector2D(.70,.67)):FBox2D(FVector2D(.25,.05),FVector2D(.83,.80));
   // Human's official-art region is already upper-body framed; do not apply
   // the legacy full-height portraits' second vertical crop to her crown/torso.
   else if(!bGroup&&!bHuman)
   {
    if(bSoloMobius){UV.Min.X=.28;UV.Max.X=.83;}
    UV.Max.Y=bKevin?.71:(bHasHero&&Heroes[Hero]==TEXT("Elysia")?.69:UV.Min.Y+(UV.Max.Y-UV.Min.Y)*.76);
   }
#if WITH_EDITOR
   if(PortraitArt)FTextureCompilingManager::Get().FinishCompilation({PortraitArt});
#endif
   if(Glow)
   {
    UTexture2D* GlowTexture=nullptr;
    if(bHasHero&&BacklightTemplate&&PortraitArt)
    {
     if(bVillVComplete)
      GlowTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Backlight_VillVCompleteHat.T_StigmaC_Backlight_VillVCompleteHat"),nullptr,LOAD_NoWarn);
     if(Heroes[Hero]==TEXT("Mobius"))
      GlowTexture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Backlight_MobiusV2.T_StigmaC_Backlight_MobiusV2"),nullptr,LOAD_NoWarn);
     const FString Path=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Backlight_%s.T_StigmaC_Backlight_%s"),*Heroes[Hero],*Heroes[Hero]);
     if(!GlowTexture&&!bVillVComplete)GlowTexture=LoadObject<UTexture2D>(nullptr,*Path,nullptr,LOAD_NoWarn);
    }
    const bool bUseBacklight=GlowTexture!=nullptr;
    // Human uses only her registered official-art light pass, never a legacy
    // silhouette. Missing resources remain hidden and fail production checks.
    if(!bUseBacklight&&bHasHero&&!bVillVComplete&&!bHuman)
    {
     const FString Path=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Glow_%s.T_StigmaC_Glow_%s"),*Heroes[Hero],*Heroes[Hero]);
     GlowTexture=LoadObject<UTexture2D>(nullptr,*Path,nullptr,LOAD_NoWarn);
    }
    if(GlowTexture)
    {
#if WITH_EDITOR
     FTextureCompilingManager::Get().FinishCompilation({GlowTexture});
#endif
     FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageType=ESlateBrushImageType::FullColor;
     if(bUseBacklight)
     {
      auto* Backlight=UMaterialInstanceDynamic::Create(BacklightTemplate,this);
      if(Backlight)
      {
       Backlight->SetTextureParameterValue(TEXT("LightTexture"),GlowTexture);
       // Human's wide hair and bow continue beyond this portrait crop. Keep
       // their generated occlusion edges inside the same soft side fade.
       const double LightMargin=bHuman?.015:(bGroup?.01:.06);
       Backlight->SetVectorParameterValue(TEXT("LightCropUV"),FLinearColor(float(FMath::Max(0.,UV.Min.X-LightMargin)),float(FMath::Min(1.,UV.Max.X+LightMargin)),bHuman?.08f:(bGroup?.04f:.06f),float(UV.Max.Y)));
       const FBox2D ExpandedUV(FVector2D(FMath::Max(0.,UV.Min.X-.12),FMath::Max(0.,UV.Min.Y-.06)),
        FVector2D(FMath::Min(1.,UV.Max.X+.12),FMath::Min(1.,UV.Max.Y+.08)));
       Brush.SetResourceObject(Backlight);Brush.SetUVRegion(ExpandedUV);
       Brush.SetImageSize(FVector2D(PortraitArt->GetSizeX()*(ExpandedUV.Max.X-ExpandedUV.Min.X),PortraitArt->GetSizeY()*(ExpandedUV.Max.Y-ExpandedUV.Min.Y)));
      }
      else Brush=FSlateNoResource();
     }
     else {Brush.SetResourceObject(GlowTexture);Brush.SetImageSize(FVector2D(GlowTexture->GetSizeX(),GlowTexture->GetSizeY()));}
     Glow->SetBrush(Brush);
    }
    else Glow->SetBrush(FSlateNoResource());
    Glow->SetColorAndOpacity(bUseBacklight?FLinearColor::White:FLinearColor(1.f,.86f,.70f,1.f));
    Glow->SetRenderOpacity(0.f);Glow->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* ContentSlot=Cast<UScaleBoxSlot>(Glow->Slot))
     {ContentSlot->SetHorizontalAlignment(HAlign_Center);ContentSlot->SetVerticalAlignment(bUseBacklight?VAlign_Bottom:VAlign_Center);}
    if(auto* Fit=Glow->GetParent())if(auto* FitSlot=Cast<UCanvasPanelSlot>(Fit->Slot))
    {
     const float Width=Heroes.Num()>1?1.f/Heroes.Num():1.f;
     const float Left=Heroes.Num()>1?Hero*Width:0.f;
     // A registered pass shares the portrait's entire layout; other heroes
     // retain the older independently centered sample light.
     Fit->SetRenderTransformPivot(FVector2D(.5f,1.f));
     Fit->SetRenderScale(FVector2D(1.f));
     FitSlot->SetAnchors(bUseBacklight?FAnchors(0,0,0,0):FAnchors(Left-.075f*Width,.20f,Left+1.075f*Width,1.20f));FitSlot->SetOffsets(FMargin(0));
    }
   }
   if(Portrait)
   {
    if(PortraitArt)
    {
     // Keep the head and shoulders comfortably inside the character-specific
     // framing. Hair, clothing and props at horizontal crop edges feather out.
     const FLinearColor CropUV(float(UV.Min.X),float(UV.Max.X),bHuman?.06f:(bGroup?.035f:(bSoloMobius?.05f:.015f)),float(UV.Max.Y));
     // The official art includes a near-black, 20%-alpha background watermark.
     // A private UI material suppresses that watermark without repainting the
     // portrait. When unavailable, retain the original texture as a fallback.
     UObject* PortraitResource=PortraitArt;
     auto* CleanMaterial=PortraitTemplate?Cast<UMaterialInstanceDynamic>(Portrait->GetBrush().GetResourceObject()):nullptr;
     if(!CleanMaterial&&PortraitTemplate)CleanMaterial=UMaterialInstanceDynamic::Create(PortraitTemplate,this);
     if(CleanMaterial)
     {
      CleanMaterial->SetTextureParameterValue(TEXT("PortraitTexture"),PortraitArt);
      CleanMaterial->SetVectorParameterValue(TEXT("CropUV"),CropUV);
      PortraitResource=CleanMaterial;
     }
     FSlateBrush Brush;Brush.ImageType=ESlateBrushImageType::FullColor;Brush.SetResourceObject(PortraitResource);Brush.SetUVRegion(UV);
     Brush.SetImageSize(FVector2D(PortraitArt->GetSizeX()*(UV.Max.X-UV.Min.X),PortraitArt->GetSizeY()*(UV.Max.Y-UV.Min.Y)));
     Portrait->SetBrush(Brush);
     if(auto* PortraitSlot=Cast<UScaleBoxSlot>(Portrait->Slot))
     {
      PortraitSlot->SetHorizontalAlignment(HAlign_Center);
      PortraitSlot->SetVerticalAlignment(VAlign_Bottom);
     }
    }
    Portrait->SetVisibility(PortraitArt?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Fit=Portrait->GetParent())if(auto* FitSlot=Cast<UCanvasPanelSlot>(Fit->Slot))
    {
     // Group heads overlap at their shoulders; their face centers stay apart.
     // The middle member is painted last, without changing member identity.
     const float Width=Heroes.Num()>=3?.52f:(Heroes.Num()==2?.62f:1.f);
     const float Center=Heroes.Num()>=3?.17f+Hero*.33f:(Heroes.Num()==2?.27f+Hero*.46f:.5f);
     const float Left=Center-Width*.5f;
     if(auto* Scale=Cast<UScaleBox>(Fit))Scale->SetStretch(EStretch::ScaleToFit);
     Fit->SetRenderTransformPivot(FVector2D(.5f,1.f));
     Fit->SetRenderScale(FVector2D(bKevin?.93f:1.f));
     FitSlot->SetAnchors(FAnchors(Left,0,Left+Width,1));FitSlot->SetOffsets(FMargin(0));
     FitSlot->SetZOrder(Heroes.Num()>=3&&Hero==1?1:0);
    }
   }
   if(Symbol)
   {
    Symbol->SetBrushFromTexture(SignetArt,true);Symbol->SetVisibility(SignetArt?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* ContentSlot=Cast<UScaleBoxSlot>(Symbol->Slot))
     {ContentSlot->SetHorizontalAlignment(HAlign_Center);ContentSlot->SetVerticalAlignment(VAlign_Center);}
    if(auto* Fit=Symbol->GetParent())if(auto* SymbolSlot=Cast<UCanvasPanelSlot>(Fit->Slot))
    {
     FAnchors SymbolAnchors(.09f,.66f,.27f,.785f);
     if(Heroes.Num()==2)
      SymbolAnchors=Hero==0?FAnchors(.025f,.650f,.170f,.740f):FAnchors(.205f,.710f,.350f,.800f);
     else if(Heroes.Num()>=3)
     {
      static const FAnchors Triangle[]={FAnchors(.125f,.655f,.215f,.715f),FAnchors(.055f,.724f,.145f,.784f),FAnchors(.215f,.724f,.305f,.784f)};
      SymbolAnchors=Triangle[Hero];
     }
     SymbolSlot->SetAnchors(SymbolAnchors);
     SymbolSlot->SetOffsets(FMargin(0));
    }
   }
  }
 }
 SelectStigmata(StigmataOfferedIds.IsEmpty()?INDEX_NONE:0);
}

void UEGDemoPanel::HideStigmaSkip()
{
 // Match phase changes synchronously during the transaction; defer only this
 // re-entrant hide until the actual result is known, not on arbitrary closes.
 if((bStigmataCommitting||bStigmataCelebrating)&&bStigmataPanelConstructed&&GetWorld()&&!GetWorld()->bIsTearingDown&&GetOwningPlayer())
  {bStigmataDeferredHide=true;return;}
 ForceHideStigmaSkip();
}
void UEGDemoPanel::ForceHideStigmaSkip()
{
 CancelStigmataHold(false);bStigmataCelebrating=false;bStigmataDeferredHide=false;bPendingStigmataRefresh=false;
 ResetStigmataTransition(WidgetTree,StigmaPanel);
 ResetStigmataChoiceDetails();
 StigmaRevision=INDEX_NONE;SelectedStigmata=INDEX_NONE;StigmataOfferedIds.Reset();bStigmataOfferIsRoute=false;
 if(StigmaPanel)StigmaPanel->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGDemoPanel::SkipStigma(){}
void UEGDemoPanel::SelectStigmata0(){if(bSuppressStigmataClick){bSuppressStigmataClick=false;return;}OpenStigmataChoiceDetails(0);}
void UEGDemoPanel::SelectStigmata1(){if(bSuppressStigmataClick){bSuppressStigmataClick=false;return;}OpenStigmataChoiceDetails(1);}
void UEGDemoPanel::SelectStigmata2(){if(bSuppressStigmataClick){bSuppressStigmataClick=false;return;}OpenStigmataChoiceDetails(2);}
void UEGDemoPanel::HoverStigmata0(){if(!bStigmataCelebrating){SelectStigmata(0);UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaHover);}}
void UEGDemoPanel::HoverStigmata1(){if(!bStigmataCelebrating){SelectStigmata(1);UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaHover);}}
void UEGDemoPanel::HoverStigmata2(){if(!bStigmataCelebrating){SelectStigmata(2);UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaHover);}}
void UEGDemoPanel::PressStigmata0(){BeginStigmataHold(0,false);}
void UEGDemoPanel::PressStigmata1(){BeginStigmataHold(1,false);}
void UEGDemoPanel::PressStigmata2(){BeginStigmataHold(2,false);}
void UEGDemoPanel::ReleaseStigmata()
{
 if(!bStigmataKeyboardHold)CancelStigmataHold(false);
}
void UEGDemoPanel::UnhoverStigmata()
{
 if(!bStigmataKeyboardHold&&HeldStigmata!=INDEX_NONE)CancelStigmataHold(true);
}
void UEGDemoPanel::SelectStigmata(int32 Index)
{
 if(bStigmataCelebrating||bStigmataCommitting||HeldStigmata!=INDEX_NONE)return;
 if(DetailedStigmata!=INDEX_NONE&&DetailedStigmata!=Index)ResetStigmataChoiceDetails();
 SelectedStigmata=StigmataOfferedIds.IsValidIndex(Index)?Index:INDEX_NONE;
 for(int32 I=0;I<StigmataChoices.Num();++I)
 {
  StigmataChoices[I]->SetRenderTranslation(I==SelectedStigmata?FVector2D(0,-5.f):FVector2D::ZeroVector);
 }
 if(auto* Player=GetOwningPlayer();Player&&StigmataChoices.IsValidIndex(SelectedStigmata)&&!StigmataChoices[SelectedStigmata]->HasUserFocus(Player))
  StigmataChoices[SelectedStigmata]->SetUserFocus(Player);
}
void UEGDemoPanel::OpenStigmataChoiceDetails(int32 Index)
{
 if(bStigmataCelebrating||bStigmataCommitting||!StigmataOfferedIds.IsValidIndex(Index))return;
 if(DetailedStigmata==Index&&StigmataDetailsPanel&&StigmataDetailsPanel->IsVisible()){CloseStigmataChoiceDetails();return;}
 SelectStigmata(Index);
 const auto* Definition=UEGStigmataCatalog::Find(StigmataOfferedIds[Index]);if(!Definition||!StigmataDetailsText)return;
 FString Details=Definition->Name.ToString()+TEXT("\n")+Definition->Category.ToString()+TEXT("\n")+Definition->Description.ToString();
 if(auto* Mode=StigmataMode(this))
 {
  if(bStigmataOfferIsRoute)
  {
   if(Definition->Id==TEXT("OriginFlower")&&Mode->GetRoster()&&!Mode->GetRoster()->GetHeroes().ContainsByPredicate([](const FEGHeroInstance& Hero){return Hero.bDeployed;}))
    Details+=TEXT("\n当前没有上阵英桀，无法完成汇聚，请选择其他终局刻印。");
  }
  else
  {
   const FString Bond=BondStatus(Definition->Id,Mode->GetRoster());if(!Bond.IsEmpty())Details+=TEXT("\n")+Bond;
   if(Definition->Id!=TEXT("Reverie")&&Mode->GetRoster()&&!Mode->GetRoster()->GetHeroes().ContainsByPredicate([](const FEGHeroInstance& Hero){return Hero.bDeployed;}))
    Details+=TEXT("\n需要先上阵英桀才能在战斗中生效。");
   if((Definition->Id==TEXT("Stars")||Definition->Id==TEXT("Vicissitude"))&&Mode->GetRoster())
   {
    const auto Required=Definition->Id==TEXT("Stars")?EEGHeroKind::Griseo:EEGHeroKind::Hua;
    const bool bOnBoard=Mode->GetRoster()->GetHeroes().ContainsByPredicate([Required](const FEGHeroInstance& Hero)
     {return Hero.bDeployed&&Hero.Definition&&Hero.Definition->HeroKind==Required;});
    Details+=bOnBoard?TEXT("\n对应英桀已上阵，升变后可立即受益。"):TEXT("\n对应英桀当前未上阵，升变后上阵可受益。");
   }
  }
 }
 StigmataDetailsText->SetHighlightedText(FText::FromString(Details));DetailedStigmata=Index;
 if(StigmataDetailsPanel)
 {
  StigmataDetailsPanel->SetVisibility(ESlateVisibility::Visible);
 }
 if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("StigmataChoiceDetailsScroll"))))Scroll->ScrollToStart();
 UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaDetails);
}
void UEGDemoPanel::ResetStigmataChoiceDetails()
{
 DetailedStigmata=INDEX_NONE;
 if(StigmataDetailsPanel)StigmataDetailsPanel->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGDemoPanel::CloseStigmataChoiceDetails()
{
 auto* Player=GetOwningPlayer();
 auto* Mode=StigmataMode(this);
 const bool bReturnFocus=Player&&StigmataDetailsPanel&&StigmataDetailsPanel->IsVisible()&&
  StigmataDetailsPanel->HasUserFocusedDescendants(Player)&&StigmaPanel&&StigmaPanel->IsVisible()&&
  bStigmataPanelConstructed&&!bStigmataCommitting&&!bStigmataCelebrating&&Mode&&Mode->GetMatch()&&
  Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward&&StigmataChoices.IsValidIndex(SelectedStigmata)&&
  StigmataChoices[SelectedStigmata]&&StigmataChoices[SelectedStigmata]->IsVisible();
 ResetStigmataChoiceDetails();
 if(bReturnFocus)StigmataChoices[SelectedStigmata]->SetUserFocus(Player);
}
void UEGDemoPanel::BeginStigmataHold(int32 Index,bool bKeyboard)
{
 if(bStigmataCelebrating||bStigmataCommitting||HeldStigmata!=INDEX_NONE||!StigmataOfferedIds.IsValidIndex(Index))return;
 bSuppressStigmataClick=false;SelectStigmata(Index);HeldStigmata=Index;HeldStigmataId=StigmataOfferedIds[Index];HeldRevision=StigmaRevision;
 bHeldStigmataIsRoute=bStigmataOfferIsRoute;
 ++StigmataHoldGeneration;bStigmataHoldSoundStarted=false;StigmataHoldStartSeconds=FPlatformTime::Seconds();
 StigmataHoldElapsed=0.f;bStigmataKeyboardHold=bKeyboard;InvalidateLayoutAndVolatility();
 if(auto* Ring=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),Index)))))
  if(Ring->GetBrush().GetResourceObject())
  {
   Ring->SetVisibility(ESlateVisibility::HitTestInvisible);
   UpdateStigmataHoldProgress(WidgetTree,Index,0.f,true);
   if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldLabel%d"),Index)))))
    {Label->SetText(FText::FromString(TEXT("0%")));Label->SetVisibility(ESlateVisibility::HitTestInvisible);}
  }
}
void UEGDemoPanel::CancelStigmataHold(bool bPlaySound)
{
 const bool bWasHolding=HeldStigmata!=INDEX_NONE;
 const int32 CancelledIndex=HeldStigmata;
 const bool bStopSound=bStigmataHoldSoundStarted;
 const uint64 CancelGeneration=++StigmataHoldGeneration;
 bStigmataHoldSoundStarted=false;StigmataHoldStartSeconds=0.0;
 HeldStigmata=INDEX_NONE;HeldStigmataId=NAME_None;HeldRevision=INDEX_NONE;StigmataHoldElapsed=0.f;bStigmataKeyboardHold=false;bHeldStigmataIsRoute=false;StigmataHeldKey=FKey();
 if(bStopSound)UEGGameAudioComponent::StopStigmataHold(this);
 // A stop callback can begin a new hold. Clear the old visuals unless the
 // new hold has already reused that same choice.
 if(bWasHolding&&WidgetTree&&HeldStigmata!=CancelledIndex)
 {
  if(auto* Ring=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),CancelledIndex))))Ring->SetVisibility(ESlateVisibility::Collapsed);
  UpdateStigmataHoldProgress(WidgetTree,CancelledIndex,0.f,false);
  if(auto* Label=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldLabel%d"),CancelledIndex))))Label->SetVisibility(ESlateVisibility::Collapsed);
 }
 if(StigmataHoldGeneration!=CancelGeneration)return;
 if(bWasHolding){InvalidateLayoutAndVolatility();if(bPlaySound)UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Cancel);}
}
void UEGDemoPanel::CompleteStigmataHold()
{
 auto* Mode=StigmataMode(this);
 const int32 Index=HeldStigmata;const FName Id=HeldStigmataId;const int32 Revision=HeldRevision;
 const bool bRoute=bHeldStigmataIsRoute;
 if(!Mode||!StigmataOfferedIds.IsValidIndex(Index)||StigmataOfferedIds[Index]!=Id||Revision!=StigmaRevision||
    Revision!=Mode->GetStigmataRevision()||bRoute!=Mode->IsChoosingEndgameRoute())
  {CancelStigmataHold(false);return;}
 const uint64 CompletingGeneration=StigmataHoldGeneration;
 bSuppressStigmataClick=true;CancelStigmataHold(false);
 // Stopping audio can synchronously run callbacks. Do not commit an old
 // choice if one of those callbacks already replaced or invalidated the hold.
 if(StigmataHoldGeneration!=CompletingGeneration+1){bSuppressStigmataClick=false;return;}
 bStigmataCommitting=true;bStigmataDeferredHide=false;
 const bool bSucceeded=bRoute?Mode->ChooseEndgameRoute(Id,Revision):Mode->ChooseStigmata(Id,Revision);
 bStigmataCommitting=false;
 if(!bStigmataPanelConstructed||!GetWorld()||GetWorld()->bIsTearingDown||!GetOwningPlayer()){ForceHideStigmaSkip();return;}
 if(bSucceeded)
 {
  ResetStigmataChoiceDetails();
  SelectedStigmata=Index;bStigmataCelebrating=true;StigmataSuccessElapsed=0.f;bStigmataDeferredHide=false;
  // The match has already committed. Hide any immediate board change while
  // the confirmation plays, then fade the whole overlay and its blur away.
  if(StigmaPanel){StigmaPanel->SetRenderOpacity(1.f);StigmaPanel->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.009f,.012f,.022f,1.f),0.f));}
  if(WidgetTree)if(auto* Blur=Cast<UBackgroundBlur>(WidgetTree->FindWidget(TEXT("StigmataBoardBlur"))))Blur->SetApplyAlphaToBlur(true);
  if(StigmataInstruction)StigmataInstruction->SetText(FText::FromString(bRoute?TEXT("终局路线已确定"):TEXT("记忆已铭刻")));
  UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaConfirm);
  UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaLift);
 }
 else
 {
  const FString Failure=Mode->GetHint();UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Cancel);
  if(!Mode->GetMatch()||Mode->GetMatch()->GetPhase()!=EEGMatchPhase::Reward){HideStigmaSkip();return;}
  RefreshStigmataOffer();const int32 Previous=StigmataOfferedIds.IndexOfByKey(Id);
  if(Previous!=INDEX_NONE)SelectStigmata(Previous);
  if(StigmataDetailsText)StigmataDetailsText->SetHighlightedText(FText::FromString(TEXT("未能领取：")+(Failure.IsEmpty()?FString(TEXT("条件尚未满足，请调整阵容或选择其他刻印。")):Failure)));
  DetailedStigmata=Previous;
  if(StigmataDetailsPanel)
  {
   StigmataDetailsPanel->SetVisibility(ESlateVisibility::Visible);
  }
  if(auto* Scroll=Cast<UScrollBox>(WidgetTree->FindWidget(TEXT("StigmataChoiceDetailsScroll"))))Scroll->ScrollToStart();
 }
}
void UEGDemoPanel::FinishStigmataCelebration()
{
 auto* Mode=StigmataMode(this);
 if(Mode&&Mode->GetMatch()&&Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward&&
    (bPendingStigmataRefresh||Mode->GetStigmataRevision()!=StigmaRevision))
 {
  RefreshStigmataOffer();
  return;
 }
 ForceHideStigmaSkip();
}
void UEGDemoPanel::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
 Super::NativeTick(MyGeometry,InDeltaTime);
 UpdateStigmataFeedback(InDeltaTime);
 if(!PhaseClockSource.IsValid())BindPhaseClock();else PhaseClockChanged();
 auto* Player=GetOwningPlayer();
 if(StigmaPanel&&StigmaPanel->IsVisible())
 {
  for(int32 I=0;I<StigmataChoices.Num();++I)
   for(int32 Hero=0;Hero<3;++Hero)AlignStigmataBacklight(WidgetTree,I,Hero);
  if(bStigmataCelebrating)
  {
   if(!bStigmataPanelConstructed||!GetWorld()||GetWorld()->bIsTearingDown||!Player){ForceHideStigmaSkip();return;}
   const float PreviousSuccessElapsed=StigmataSuccessElapsed;
   StigmataSuccessElapsed+=InDeltaTime;
   if(PreviousSuccessElapsed<.35f&&StigmataSuccessElapsed>=.35f)UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaLand);
   if(PreviousSuccessElapsed<.85f&&StigmataSuccessElapsed>=.85f)UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::StigmaTransition);
   const float T=FMath::Clamp(StigmataSuccessElapsed/.35f,0.f,1.f);
   const float Fade=FMath::SmoothStep(0.f,1.f,FMath::Clamp((StigmataSuccessElapsed-.85f)/.65f,0.f,1.f));
   StigmaPanel->SetRenderOpacity(1.f-Fade);
   for(int32 I=0;I<StigmataChoices.Num();++I)if(auto* Choice=StigmataChoices[I].Get())
   {
    UpdateStigmataSelectionDecor(WidgetTree,I,I==SelectedStigmata);
    if(I==SelectedStigmata)
    {
     // A brief lift, landing and small rebound; scale stays uniform so the
     // portrait, signet and their shared presentation move as one.
     float Scale=1.f;float OffsetY=0.f;
     if(StigmataSuccessElapsed<.16f)
     {
      const float P=FMath::SmoothStep(0.f,1.f,StigmataSuccessElapsed/.16f);
      Scale=1.f+.12f*P;OffsetY=-5.f-18.f*P;
     }
     else if(StigmataSuccessElapsed<.35f)
     {
      const float P=FMath::SmoothStep(0.f,1.f,(StigmataSuccessElapsed-.16f)/.19f);
      Scale=1.12f-.14f*P;OffsetY=-23.f+23.f*P;
     }
     else if(StigmataSuccessElapsed<.50f)
     {
      const float P=FMath::SmoothStep(0.f,1.f,(StigmataSuccessElapsed-.35f)/.15f);
      Scale=.98f+.02f*P;OffsetY=-2.f*FMath::Sin(P*PI);
     }
     Choice->SetRenderScale(FVector2D(Scale));Choice->SetRenderTranslation(FVector2D(0,OffsetY));
     Choice->SetColorAndOpacity(FLinearColor(1.f+.5f*T,1.f+.3f*T,1.f+.1f*T,1.f));
     UpdateStigmataHeroGlow(WidgetTree,I,.85f+.15f*T,.75f+.25f*T,InDeltaTime);
    }
    else Choice->SetRenderOpacity(1.f-.75f*T);
   }
   if(StigmataSuccessElapsed>=1.5f)FinishStigmataCelebration();return;
  }
  for(int32 I=0;I<StigmataChoices.Num();++I)
  {
   const auto* Choice=StigmataChoices[I].Get();
   const float Target=I==HeldStigmata?.85f:(Choice&&Choice->IsVisible()&&Choice->IsHovered()?.62f:0.f);
   UpdateStigmataSelectionDecor(WidgetTree,I,Target>0.f);
   const float BacklightTarget=I==HeldStigmata?.75f:(Choice&&Choice->IsVisible()&&Choice->IsHovered()?.5f:0.f);
   UpdateStigmataHeroGlow(WidgetTree,I,Target,BacklightTarget,InDeltaTime);
  }
  if(HeldStigmata!=INDEX_NONE)
  {
   const bool bActive=FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive();
   const bool bStillPressed=bStigmataKeyboardHold?(Player&&HasUserFocus(Player)):
    (StigmataChoices.IsValidIndex(HeldStigmata)&&StigmataChoices[HeldStigmata]->IsPressed()&&StigmataChoices[HeldStigmata]->IsHovered());
   if(!bActive||!bStillPressed)
   {
    CancelStigmataHold(bActive);
   }
   else
   {
    StigmataHoldElapsed+=InDeltaTime;InvalidateLayoutAndVolatility();
    // Use monotonic held time for audio so a long frame before the press
    // cannot turn a short click into an audible charge.
    if(!bStigmataHoldSoundStarted&&FPlatformTime::Seconds()-StigmataHoldStartSeconds>=.25)
    {
     const uint64 StartedGeneration=StigmataHoldGeneration;
     bStigmataHoldSoundStarted=true;
     UEGGameAudioComponent::StartStigmataHold(this);
     if(StigmataHoldGeneration!=StartedGeneration)
     {
      if(!bStigmataHoldSoundStarted)UEGGameAudioComponent::StopStigmataHold(this);
      return;
     }
    }
    UpdateStigmataHoldProgress(WidgetTree,HeldStigmata,StigmataHoldElapsed/.9f,true);
    if(auto* Label=Cast<UTextBlock>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataHoldLabel%d"),HeldStigmata)))))
     Label->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::Clamp(FMath::FloorToInt(StigmataHoldElapsed/.9f*100.f),0,100))));
    if(StigmataHoldElapsed>=.9f)CompleteStigmataHold();
   }
  }
  if(Player&&HeldStigmata==INDEX_NONE)for(int32 Index=0;Index<StigmataChoices.Num();++Index)
   if(Index!=SelectedStigmata&&StigmataChoices[Index]->HasUserFocus(Player)){SelectStigmata(Index);break;}
  return;
 }
 if(!Player)return;
 if(StigmataCollectionPanel&&StigmataCollectionPanel->IsVisible())return;
 int32 Focused=INDEX_NONE;
 for(int32 Index=0;Index<StigmataRows.Num();++Index)
  if(StigmataRows[Index]->IsVisible()&&StigmataRows[Index]->IsHovered()){Focused=Index;break;}
 if(Focused==INDEX_NONE)for(int32 Index=0;Index<StigmataRows.Num();++Index)
  if(StigmataRows[Index]->IsVisible()&&StigmataRows[Index]->HasUserFocus(Player)){Focused=Index;break;}
 if(Focused!=PreviewedStigmata){if(Focused==INDEX_NONE)HideStigmataPreview();else PreviewStigmata(Focused);}
}
bool UEGDemoPanel::HasStigmataEscapePriority() const
{
 return HeldStigmata!=INDEX_NONE||bStigmataKeyboardHold||
  (StigmaPanel&&StigmaPanel->IsVisible()&&StigmataDetailsPanel&&StigmataDetailsPanel->IsVisible())||
  (StigmataCollectionPanel&&StigmataCollectionPanel->IsVisible());
}
FReply UEGDemoPanel::NativeOnPreviewKeyDown(const FGeometry& InGeometry,const FKeyEvent& InKeyEvent)
{
 const FKey Key=InKeyEvent.GetKey();
 if(StigmaPanel&&StigmaPanel->IsVisible()&&StigmataDetailsPanel&&StigmataDetailsPanel->IsVisible()&&
    (Key==EKeys::Escape||Key==EKeys::Gamepad_FaceButton_Right))
  {CloseStigmataChoiceDetails();return FReply::Handled();}
 if(StigmataCollectionPanel&&StigmataCollectionPanel->IsVisible()&&(Key==EKeys::Escape||Key==EKeys::Gamepad_FaceButton_Right))
  {CloseStigmataCollection();return FReply::Handled();}
 if(StigmaPanel&&StigmaPanel->IsVisible()&&(Key==EKeys::Enter||Key==EKeys::SpaceBar||Key==EKeys::Gamepad_FaceButton_Bottom))
 {
  // Only a focused candidate can start selection. Detail controls must receive
  // their own confirm keys; the panel itself owns focus solely during a hold.
  bool bChoiceFocused=false;
  if(auto* Player=GetOwningPlayer())for(const auto& Choice:StigmataChoices)
   if(Choice&&Choice->HasUserFocus(Player)){bChoiceFocused=true;break;}
  if(!bChoiceFocused&&!bStigmataKeyboardHold)return Super::NativeOnPreviewKeyDown(InGeometry,InKeyEvent);
  if(!InKeyEvent.IsRepeat()&&!bStigmataCelebrating)
  {
   if(auto* Player=GetOwningPlayer())for(int32 Index=0;Index<StigmataChoices.Num();++Index)
    if(StigmataChoices[Index]->HasUserFocus(Player)){SelectStigmata(Index);break;}
   BeginStigmataHold(SelectedStigmata,true);
   if(HeldStigmata!=INDEX_NONE)
   {
    StigmataHeldKey=Key;
    // UWidget::SetUserFocus may queue a delayed request even when the choice
    // was already focused. Cancel only a stale request targeting this panel's
    // choices before the hold takes focus; preserve unrelated Slate operations.
    if(auto* LocalPlayer=GetOwningLocalPlayer())
    {
     FReply& Pending=LocalPlayer->GetSlateOperations();
     const TSharedPtr<SWidget> PendingFocus=Pending.GetUserFocusRecepient();
     if(PendingFocus.IsValid())for(const auto& Choice:StigmataChoices)
      if(Choice&&Choice->GetCachedWidget()==PendingFocus){Pending.CancelFocusRequest();break;}
    }
    return FReply::Handled().SetUserFocus(TakeWidget(),EFocusCause::SetDirectly);
   }
  }
  return FReply::Handled();
 }
 return Super::NativeOnPreviewKeyDown(InGeometry,InKeyEvent);
}
FReply UEGDemoPanel::NativeOnKeyUp(const FGeometry& InGeometry,const FKeyEvent& InKeyEvent)
{
 if(bStigmataKeyboardHold&&InKeyEvent.GetKey()==StigmataHeldKey)
 {
  const int32 Index=HeldStigmata;CancelStigmataHold(false);OpenStigmataChoiceDetails(Index);return FReply::Handled();
 }
 return Super::NativeOnKeyUp(InGeometry,InKeyEvent);
}
void UEGDemoPanel::NativeOnFocusLost(const FFocusEvent& InFocusEvent)
{
 if(bStigmataKeyboardHold)CancelStigmataHold(true);
 Super::NativeOnFocusLost(InFocusEvent);
}
void UEGDemoPanel::ConfirmStigmata(){}

void UEGDemoPanel::SetStigmataSidebarVisible(bool bVisible)
{
 if(bStigmataSidebarVisible==bVisible)return;
 bStigmataSidebarVisible=bVisible;
 if(!bVisible)
 {
  HideStigmataPreview();
  if(StigmataCollectionPanel)StigmataCollectionPanel->SetVisibility(ESlateVisibility::Collapsed);
  if(StigmataFeedbackText)StigmataFeedbackText->SetVisibility(ESlateVisibility::Collapsed);
 }
 RefreshStigmataCollection();
}

void UEGDemoPanel::RefreshStigmataCollection()
{
 if(StigmataRows.IsEmpty())return;
 auto* Mode=StigmataMode(this);auto* Match=Mode?Mode->GetMatch():nullptr;
 const auto Phase=Match?Match->GetPhase():EEGMatchPhase::Finished;
 const bool bAvailable=Match&&(Phase==EEGMatchPhase::Preparation||Phase==EEGMatchPhase::Combat);
 StigmataOwnedIds=Match?Match->GetStigmataOwned():TArray<FName>();
 StigmataIcons.Empty();
 for(int32 Index=0;Index<StigmataRows.Num();++Index)
 {
  const auto* Definition=StigmataOwnedIds.IsValidIndex(Index)?UEGStigmataCatalog::Find(StigmataOwnedIds[Index]):nullptr;
  StigmataRows[Index]->SetVisibility(bStigmataSidebarVisible&&bAvailable&&Definition?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
  if(!Definition)continue;
  if(Index>0)StigmataRows[Index]->SetNavigationRuleExplicit(EUINavigation::Up,StigmataRows[Index-1]);
  else StigmataRows[Index]->SetNavigationRuleBase(EUINavigation::Up,EUINavigationRule::Escape);
  if(Index+1<StigmataOwnedIds.Num()&&StigmataRows.IsValidIndex(Index+1))
   StigmataRows[Index]->SetNavigationRuleExplicit(EUINavigation::Down,StigmataRows[Index+1]);
  else StigmataRows[Index]->SetNavigationRuleBase(EUINavigation::Down,EUINavigationRule::Escape);
  StigmataRowNames[Index]->SetText(Definition->Name);
  const FName IconName(*FString::Printf(TEXT("StigmataOwnedIcon%d"),Index));
  const TArray<FString> Heroes=StigmataHeroes(Definition->Id);
  const bool bGroup=Heroes.Num()>1;
  if(auto* Icon=Cast<UImage>(WidgetTree->FindWidget(IconName)))
  {
   UTexture2D* Texture=Heroes.Num()==1?StigmataIdentityArt(Heroes[0],true):nullptr;
   Icon->SetBrushFromTexture(Texture,true);
   Icon->SetVisibility(!bGroup&&Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
   Icon->SetColorAndOpacity(StigmataHighlightRemaining.Contains(Definition->Id)?FLinearColor(2.f,1.55f,.6f,1.f):FLinearColor::White);
   StigmataIcons.Add(Definition->Id,Icon);
  }
  if(auto* Group=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataOwnedGroup%d"),Index))))
   Group->SetVisibility(bGroup?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  for(int32 Hero=0;Hero<3;++Hero)
  {
   auto* GroupIcon=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataOwnedGroupIcon%d_%d"),Index,Hero))));
   if(!GroupIcon)continue;
   UTexture2D* Texture=bGroup&&Heroes.IsValidIndex(Hero)?StigmataIdentityArt(Heroes[Hero],true):nullptr;
   GroupIcon->SetBrushFromTexture(Texture,true);GroupIcon->SetVisibility(Texture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
   GroupIcon->SetColorAndOpacity(StigmataHighlightRemaining.Contains(Definition->Id)?FLinearColor(2.f,1.55f,.6f,1.f):FLinearColor::White);
   if(auto* GroupSlot=Cast<UCanvasPanelSlot>(GroupIcon->Slot))
   {
    FAnchors Anchors;
    if(Heroes.Num()==2)Anchors=Hero==0?FAnchors(0,0,.59f,.59f):FAnchors(.41f,.41f,1,1);
    else {static const FAnchors Triangle[]={FAnchors(.26f,0,.74f,.48f),FAnchors(0,.52f,.48f,1),FAnchors(.52f,.52f,1,1)};Anchors=Triangle[Hero];}
    GroupSlot->SetAnchors(Anchors);GroupSlot->SetOffsets(FMargin(0));
   }
  }
  if(auto* Gradient=Cast<UImage>(WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataOwnedGroupGradient%d"),Index)))))
   Gradient->SetVisibility(Heroes.Num()==2&&Gradient->GetBrush().GetResourceObject()?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  if(auto* Slash=WidgetTree->FindWidget(FName(*FString::Printf(TEXT("StigmataOwnedGroupSlash%d"),Index))))
   Slash->SetVisibility(Heroes.Num()==2?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 }
 if(!bStigmataSidebarVisible||!bAvailable||!StigmataOwnedIds.Contains(StigmataDetailId))
 {
  if(StigmataCollectionPanel)StigmataCollectionPanel->SetVisibility(ESlateVisibility::Collapsed);
  if(!bAvailable)HideStigmataPreview();
 }
 if(!StigmataCollectionList)return;
 StigmataCollectionList->ClearChildren();
 const auto* Definition=UEGStigmataCatalog::Find(StigmataDetailId);
 if(!Definition||!StigmataOwnedIds.Contains(StigmataDetailId))return;
 FString Details=Definition->Name.ToString()+TEXT("  ")+Definition->Category.ToString()+TEXT("\n")+Definition->Description.ToString();
 const FString Bond=BondStatus(Definition->Id,Mode->GetRoster());if(!Bond.IsEmpty())Details+=TEXT("\n")+Bond;
 const FString Economy=StigmataRunEconomy(Match,Definition->Id);if(!Economy.IsEmpty())Details+=TEXT("\n")+Economy;
 auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
 auto* ArtSize=WidgetTree->ConstructWidget<USizeBox>();ArtSize->SetWidthOverride(120.f);ArtSize->SetHeightOverride(120.f);
 auto* Symbols=WidgetTree->ConstructWidget<UCanvasPanel>();ArtSize->SetContent(Symbols);
 const TArray<FString> Heroes=StigmataHeroes(Definition->Id);
 for(int32 Hero=0;Hero<Heroes.Num()&&Hero<3;++Hero)
 {
  auto* Symbol=WidgetTree->ConstructWidget<UImage>();Symbol->SetBrushFromTexture(StigmataIdentityArt(Heroes[Hero],true),true);
  Symbol->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* Fit=WidgetTree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Symbol);
  FAnchors Anchors(0,0,1,1);
  if(Heroes.Num()==2)Anchors=Hero==0?FAnchors(0,0,.59f,.59f):FAnchors(.41f,.41f,1,1);
  else if(Heroes.Num()>=3){static const FAnchors Triangle[]={FAnchors(.26f,0,.74f,.48f),FAnchors(0,.52f,.48f,1),FAnchors(.52f,.52f,1,1)};Anchors=Triangle[Hero];}
  Place(Symbols,Fit,Anchors,FMargin(0),1);
 }
 if(Heroes.Num()==2)
 {
  auto* Gradient=WidgetTree->ConstructWidget<UImage>();
  if(auto* Light=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_SelectionLightLine.T_StigmaC_SelectionLightLine"),nullptr,LOAD_NoWarn))
   Gradient->SetBrushFromTexture(Light,true);
  else Gradient->SetBrush(FSlateNoResource());
  Gradient->SetColorAndOpacity(FLinearColor(.98f,.82f,.61f,.48f));Gradient->SetRenderTransformPivot(FVector2D(.5f,.5f));Gradient->SetRenderTransformAngle(-35.f);
  Gradient->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Symbols,Gradient,FAnchors(.30f,.38f,.70f,.62f),FMargin(0),2);
  auto* Slash=StigmataText(WidgetTree,TEXT("/"),24);Slash->SetJustification(ETextJustify::Center);
  Slash->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.94f,.82f)));
  {auto Font=Slash->GetFont();Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(.04f,.02f,.08f,1.f);Slash->SetFont(Font);}
  Slash->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Symbols,Slash,FAnchors(.35f,.32f,.65f,.68f),FMargin(0),3);
 }
 Row->AddChildToHorizontalBox(ArtSize);
 auto* Label=StigmataNarrationText(WidgetTree,Details,18);Label->SetWrapTextAt(320.f);
 Row->AddChildToHorizontalBox(Label);StigmataCollectionList->AddChildToVerticalBox(Row);
}
void UEGDemoPanel::PreviewStigmata0(){PreviewStigmata(0);}
void UEGDemoPanel::PreviewStigmata1(){PreviewStigmata(1);}
void UEGDemoPanel::PreviewStigmata2(){PreviewStigmata(2);}
void UEGDemoPanel::PreviewStigmata(int32 Index)
{
 auto* Mode=StigmataMode(this);
 if(!bStigmataSidebarVisible||!Mode||!StigmataOwnedIds.IsValidIndex(Index)||!StigmataPreviewPanel||!StigmataPreviewText)return;
 const auto* Definition=UEGStigmataCatalog::Find(StigmataOwnedIds[Index]);if(!Definition)return;
 PreviewedStigmata=Index;
 FString Description=Definition->Description.ToString();
 if(Description.Len()>95)Description=Description.Left(95)+TEXT("…");
 FString Text=Definition->Name.ToString()+TEXT("\n")+Description;
 const FString Bond=BondStatus(Definition->Id,Mode->GetRoster());if(!Bond.IsEmpty())Text+=TEXT("\n")+Bond;
 const FString Economy=StigmataRunEconomy(Mode->GetMatch(),Definition->Id);if(!Economy.IsEmpty())Text+=TEXT("\n")+Economy;
 StigmataPreviewText->SetHighlightedText(FText::FromString(Text));
 if(auto* PreviewSlot=Cast<UCanvasPanelSlot>(StigmataPreviewPanel->Slot))PreviewSlot->SetPosition(FVector2D(-680,280.f+64.f*Index));
 StigmataPreviewPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UEGDemoPanel::HideStigmataPreview()
{
 PreviewedStigmata=INDEX_NONE;
 if(StigmataPreviewPanel)StigmataPreviewPanel->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGDemoPanel::OpenStigmataDetails0(){OpenStigmataDetails(0);}
void UEGDemoPanel::OpenStigmataDetails1(){OpenStigmataDetails(1);}
void UEGDemoPanel::OpenStigmataDetails2(){OpenStigmataDetails(2);}
void UEGDemoPanel::OpenStigmataDetails(int32 Index)
{
 if(!bStigmataSidebarVisible||!StigmataCollectionPanel||!StigmataOwnedIds.IsValidIndex(Index))return;
 StigmataDetailId=StigmataOwnedIds[Index];HideStigmataPreview();RefreshStigmataCollection();
 StigmataCollectionPanel->SetVisibility(ESlateVisibility::Visible);
 if(GetOwningPlayer())if(auto* Close=WidgetTree->FindWidget(TEXT("Button_CloseStigmataCollection")))Close->SetUserFocus(GetOwningPlayer());
}
void UEGDemoPanel::ToggleStigmataCollection()
{
 if(StigmataCollectionPanel&&StigmataCollectionPanel->IsVisible())CloseStigmataCollection();
 else OpenStigmataDetails(0);
}
void UEGDemoPanel::CloseStigmataCollection()
{
 if(StigmataCollectionPanel)StigmataCollectionPanel->SetVisibility(ESlateVisibility::Collapsed);
 const int32 Index=StigmataOwnedIds.IndexOfByKey(StigmataDetailId);
 if(GetOwningPlayer()&&StigmataRows.IsValidIndex(Index))StigmataRows[Index]->SetUserFocus(GetOwningPlayer());
}

void UEGDemoPanel::BindStigmataFeedback()
{
 auto* Mode=StigmataMode(this);auto* Source=Mode?Mode->BattleRules.Get():nullptr;
 if(StigmataFeedbackSource.Get()==Source)return;
 if(StigmataFeedbackSource.IsValid())StigmataFeedbackSource->OnStigmataTriggered.RemoveDynamic(this,&ThisClass::StigmataTriggered);
 StigmataFeedbackSource=Source;
 if(Source)Source->OnStigmataTriggered.AddUniqueDynamic(this,&ThisClass::StigmataTriggered);
}
void UEGDemoPanel::StigmataTriggered(FName Id,AEGChessUnit* Source)
{
 const auto* Definition=UEGStigmataCatalog::Find(Id);
 if(!Definition)return;
 StigmataHighlightRemaining.Add(Id,1.f);
 if(auto* Icon=StigmataIcons.Find(Id);Icon&&*Icon)(*Icon)->SetColorAndOpacity(FLinearColor(2.f,1.55f,.6f,1.f));
 SetOwnedGroupTint(WidgetTree,StigmataOwnedIds.IndexOfByKey(Id),FLinearColor(2.f,1.55f,.6f,1.f));
 // A nine-unit build can emit many events in one frame; icon state remains
 // per-stigmata, while the text is capped at five updates per combat second.
 if(StigmataCaptionCooldown>0.f)return;
 StigmataCaptionCooldown=.2f;
 FString Caption=Definition->Name.ToString()+TEXT(" · 已触发");
 if(IsValid(Source)&&Source->GetDefinition())Caption+=TEXT("  ")+Source->GetDefinition()->DisplayName.ToString();
 if(auto* Battle=StigmataFeedbackSource.Get())if(auto* Stigmata=Battle->GetStigmata())
 {
  const auto Rows=Stigmata->GetContributions();
  if(const auto* Row=Rows.FindByPredicate([Id](const FEGStigmataContribution& Entry){return Entry.Id==Id;}))
  {
   // The event carries no damage receipt. Show measured cumulative values,
   // never an invented amount for this one trigger.
   if(Row->AdditionalDamage>0)Caption+=FString::Printf(TEXT("  伤害累计 %.0f"),Row->AdditionalDamage);
   else if(Row->ManaRestored>0)Caption+=FString::Printf(TEXT("  返蓝累计 %.0f"),Row->ManaRestored);
   else if(Row->ShieldAbsorbed>0)Caption+=FString::Printf(TEXT("  护盾累计 %.0f"),Row->ShieldAbsorbed);
  }
 }
 if(StigmataFeedbackText){StigmataFeedbackText->SetText(FText::FromString(Caption));if(bStigmataSidebarVisible)StigmataFeedbackText->SetVisibility(ESlateVisibility::HitTestInvisible);}
 StigmataFeedbackRemaining=1.5f;
}
void UEGDemoPanel::ClearStigmataFeedback()
{
 for(const auto& Entry:StigmataIcons)if(Entry.Value)Entry.Value->SetColorAndOpacity(FLinearColor::White);
 for(int32 Index=0;Index<StigmataRows.Num();++Index)SetOwnedGroupTint(WidgetTree,Index,FLinearColor::White);
 StigmataHighlightRemaining.Reset();StigmataFeedbackRemaining=0.f;StigmataCaptionCooldown=0.f;
 if(StigmataFeedbackText)StigmataFeedbackText->SetVisibility(ESlateVisibility::Collapsed);
}

void UEGDemoPanel::UpdateStigmataFeedback(float DeltaSeconds)
{
 const float Delta=FMath::Max(0.f,DeltaSeconds);
 if(StigmataResultsPanel)
 {
  const auto* Mode=StigmataMode(this);const auto* Match=Mode?Mode->GetMatch():nullptr;
  if(!Match||Match->GetPhase()==EEGMatchPhase::Combat)
  {
   bStigmataResultsFromSettlement=false;StigmataResultsFadeElapsed=0.f;
   StigmataResultsPanel->SetRenderOpacity(1.f);
   StigmataResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
  }
  else if(Match->GetPhase()==EEGMatchPhase::Preparation&&bStigmataResultsFromSettlement)
  {
   StigmataResultsFadeElapsed=FMath::Min(.75f,StigmataResultsFadeElapsed+Delta);
   const float Fade=FMath::SmoothStep(0.f,1.f,StigmataResultsFadeElapsed/.75f);
   StigmataResultsPanel->SetRenderOpacity(1.f-Fade);
   if(StigmataResultsFadeElapsed>=.75f)StigmataResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
  }
 }
 StigmataCaptionCooldown=FMath::Max(0.f,StigmataCaptionCooldown-Delta);
 for(auto It=StigmataHighlightRemaining.CreateIterator();It;++It)
 {
  It.Value()-=Delta;
  if(It.Value()<=0.f)
  {
   if(auto* Icon=StigmataIcons.Find(It.Key());Icon&&*Icon)(*Icon)->SetColorAndOpacity(FLinearColor::White);
   SetOwnedGroupTint(WidgetTree,StigmataOwnedIds.IndexOfByKey(It.Key()),FLinearColor::White);
   It.RemoveCurrent();
  }
 }
 if(StigmataFeedbackRemaining>0.f)
 {
  StigmataFeedbackRemaining=FMath::Max(0.f,StigmataFeedbackRemaining-Delta);
  if(StigmataFeedbackRemaining<=0.f&&StigmataFeedbackText)StigmataFeedbackText->SetVisibility(ESlateVisibility::Collapsed);
 }
}

void UEGDemoPanel::RefreshStigmataResults()
{
 if(!StigmataResultsPanel||!StigmataResultsText)return;
 auto* Mode=StigmataMode(this);auto* Match=Mode?Mode->GetMatch():nullptr;
 const auto Phase=Match?Match->GetPhase():EEGMatchPhase::Combat;
 auto* Battle=Mode?Mode->BattleRules.Get():nullptr;
 auto* Stigmata=Battle?Battle->GetStigmata():nullptr;
 if(!Match||!Stigmata||Phase==EEGMatchPhase::Combat)
 {
  bStigmataResultsFromSettlement=false;StigmataResultsFadeElapsed=0.f;
  StigmataResultsPanel->SetRenderOpacity(1.f);
  StigmataResultsPanel->SetVisibility(ESlateVisibility::Collapsed);return;
 }
 // A rebuilt widget in preparation must not resurrect the previous battle's
 // cached rows. Repeated view refreshes also must not restart the fade.
 if(Phase==EEGMatchPhase::Preparation)
 {
  if(!bStigmataResultsFromSettlement||StigmataResultsFadeElapsed>=.75f)
   StigmataResultsPanel->SetVisibility(ESlateVisibility::Collapsed);
  return;
 }
 auto Rows=Stigmata->GetContributions();
 // The battle component keeps the last result through settlement.  A full
 // run restart may occur before another battle replaces it; do not show rows
 // from a previous run whose stigmata are no longer owned.
 Rows.RemoveAll([Match](const FEGStigmataContribution& Row){return !Match->HasStigmata(Row.Id);});
 if(Rows.IsEmpty())
 {
  bStigmataResultsFromSettlement=false;StigmataResultsFadeElapsed=0.f;
  StigmataResultsPanel->SetVisibility(ESlateVisibility::Collapsed);return;
 }
 FString Text=TEXT("上场刻印贡献（伤害含破盾）");
 for(const auto& Row:Rows)
 {
  const auto* Definition=UEGStigmataCatalog::Find(Row.Id);
  Text+=TEXT("\n")+(Definition?Definition->Name.ToString():Row.Id.ToString());
  if(Row.TriggerCount==0&&Row.AdditionalDamage<=0.f&&Row.ManaRestored<=0.f&&Row.HealingRestored<=0.f&&Row.ShieldAbsorbed<=0.f&&Row.GoldEarned<=0)
  {
   Text+=TEXT("：本场未触发");
   const FString Economy=StigmataRunEconomy(Match,Row.Id);if(!Economy.IsEmpty())Text+=TEXT("\n  ")+Economy;
   continue;
  }
  Text+=FString::Printf(TEXT("：触发%d"),Row.TriggerCount);
  if(Row.AdditionalDamage>0.f)Text+=FString::Printf(TEXT(" 伤害%.0f"),Row.AdditionalDamage);
  if(Row.ManaRestored>0.f)Text+=FString::Printf(TEXT(" 回蓝%.0f"),Row.ManaRestored);
  if(Row.HealingRestored>0.f)Text+=FString::Printf(TEXT(" 实际回复%.0f"),Row.HealingRestored);
  if(Row.ShieldAbsorbed>0.f)Text+=FString::Printf(TEXT(" 护盾吸收%.0f"),Row.ShieldAbsorbed);
  if(Row.GoldEarned>0)Text+=FString::Printf(TEXT(" 本场获得%d崩坏能"),Row.GoldEarned);
  const FString Economy=StigmataRunEconomy(Match,Row.Id);if(!Economy.IsEmpty())Text+=TEXT("\n  ")+Economy;
 }
 StigmataResultsText->SetHighlightedText(FText::FromString(Text));
 bStigmataResultsFromSettlement=true;StigmataResultsFadeElapsed=0.f;
 StigmataResultsPanel->SetRenderOpacity(1.f);
 StigmataResultsPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
}
