#include "UI/EGCardPresentation.h"
#include "UI/EGVisualStyle.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ContentWidget.h"
#include "Components/ScaleBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"

namespace
{
struct FCardIdentity {const TCHAR* Key;const TCHAR* Name;};
// Specific forms precede base names when resolving legacy artwork paths.
const FCardIdentity Identities[]={
 {TEXT("GriseoAdult"),TEXT("天行·绘星之卷")},{TEXT("HuaEnhanced"),TEXT("浮生·渡尘之羽")},
 {TEXT("ElysiaHerrscherOfHuman"),TEXT("真我·人之律者")},
 {TEXT("Elysia"),TEXT("爱莉希雅")},{TEXT("Sakura"),TEXT("樱")},{TEXT("Hua"),TEXT("华")},
 {TEXT("Pardo"),TEXT("帕朵菲莉丝")},{TEXT("Griseo"),TEXT("格蕾修")},{TEXT("Aponia"),TEXT("阿波尼亚")},
 {TEXT("Eden"),TEXT("伊甸")},{TEXT("Mobius"),TEXT("梅比乌斯")},{TEXT("VillV"),TEXT("维尔薇")},
 {TEXT("Can"),TEXT("罐头")},{TEXT("Knight"),TEXT("骑士先生")}
};
UTexture2D* CardTexture(FName Key)
{
 if(Key.IsNone())return nullptr;
 const FString Folder=Key==TEXT("Pardo")?TEXT("Pardofelis"):Key==TEXT("Hua")?TEXT("FuHua"):Key==TEXT("HuaEnhanced")?TEXT("FuHuaEnhanced"):Key.ToString();
 return FEGVisualStyle::Texture(*(FString(TEXT("/Game/Assets/Cards/"))+Folder+TEXT("/T_Card_")+Key.ToString()));
}

// Missing optional artwork should not cause synchronous disk loads on every
// sidebar refresh. Existing textures use the style layer's weak cache.
UTexture2D* OptionalArtwork(const FString& Path)
{
 if(Path.IsEmpty())return nullptr;
 static TSet<FString> MissingPaths;
 if(MissingPaths.Contains(Path))return nullptr;
 if(UTexture2D* Texture=FEGVisualStyle::Texture(*Path))return Texture;
 MissingPaths.Add(Path);return nullptr;
}

int32 CorruptionPhaseForImage(const UImage* Image)
{
 const auto* Texture=Image?Cast<UTexture2D>(Image->GetBrush().GetResourceObject()):nullptr;
 if(!Texture)return 0;
 const FString Path=Texture->GetPathName();
 if(Path==TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase1.T_Card_Corruption_Phase1"))return 1;
 if(Path==TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase2.T_Card_Corruption_Phase2"))return 2;
 return 0;
}

/** Reusable frame over the original image. The two source dimensions are known;
 * imported platform textures may be resampled, so never infer their aspect here. */
void ApplyCorruptionPhase(UWidgetTree* Tree,UImage* Image)
{
 if(!Tree||!Image)return;
 const int32 Phase=CorruptionPhaseForImage(Image);
 const FString Prefix=Image->GetName()+TEXT("_CorruptionPhase");
 auto* Layer=Cast<UCanvasPanel>(Tree->FindWidget(FName(*(Prefix+TEXT("Layer")))));
 if(!Layer&&!Phase)return;
 if(!Layer)
 {
  auto* Parent=Image->GetParent();auto* CanvasParent=Cast<UCanvasPanel>(Parent);auto* ContentParent=Cast<UContentWidget>(Parent);
  if(!CanvasParent&&!ContentParent)return;
  FAnchors Anchors;FMargin Offsets;FVector2D Alignment=FVector2D::ZeroVector;int32 Z=0;
  if(CanvasParent)
  {
   const auto* Previous=Cast<UCanvasPanelSlot>(Image->Slot);if(!Previous)return;
   Anchors=Previous->GetAnchors();Offsets=Previous->GetOffsets();Alignment=Previous->GetAlignment();Z=Previous->GetZOrder();
  }
  Image->RemoveFromParent();
  Layer=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(*(Prefix+TEXT("Layer"))));
  Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Layer->SetClipping(EWidgetClipping::ClipToBounds);
  if(CanvasParent)
  {auto* Slot=CanvasParent->AddChildToCanvas(Layer);Slot->SetAnchors(Anchors);Slot->SetOffsets(Offsets);Slot->SetAlignment(Alignment);Slot->SetZOrder(Z);}
  else ContentParent->SetContent(Layer);
  auto* PortraitSlot=Layer->AddChildToCanvas(Image);PortraitSlot->SetAnchors(FAnchors(0,0,1,1));PortraitSlot->SetOffsets(FMargin(0));
  auto Place=[Layer](UWidget* Widget,FAnchors Area,int32 Order)
  {auto* Slot=Layer->AddChildToCanvas(Widget);Slot->SetAnchors(Area);Slot->SetOffsets(FMargin(0));Slot->SetZOrder(Order);return Slot;};
  auto* Bar=Tree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*(Prefix+TEXT("Bar"))));
  Bar->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.028f,.012f,.050f,1.f),0.f));Bar->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Bar,FAnchors(0,.9f,1,1),1);
  auto* Rule=Tree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*(Prefix+TEXT("Rule"))));
  Rule->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.73f,.56f,.32f,1.f),0.f));Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* RuleSlot=Place(Rule,FAnchors(.04f,.9f,.96f,.9f),2);RuleSlot->SetOffsets(FMargin(0,0,0,1.5f));
  auto* Fit=Tree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),FName(*(Prefix+TEXT("TitleFit"))));
  Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* Title=Tree->ConstructWidget<UTextBlock>();Title->SetFont(FEGVisualStyle::PearlFont(25));Title->SetText(FText::FromString(TEXT("侵蚀之律者")));
  Title->SetColorAndOpacity(FSlateColor(FLinearColor(.97f,.91f,.81f,1.f)));Title->SetJustification(ETextJustify::Center);
  Title->SetVisibility(ESlateVisibility::HitTestInvisible);Fit->SetContent(Title);Place(Fit,FAnchors(.1f,.912f,.9f,.985f),3);
  auto* Badge=Tree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*(Prefix+TEXT("Badge"))));
  Badge->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.026f,.012f,.052f,.95f),30.f,FLinearColor(.73f,.56f,.32f,1.f),1.f));
  Badge->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Badge,FAnchors(.04f,.03f,.18f,.13f),3);
  auto* Mark=Tree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*(Prefix+TEXT("Mark"))));
  FSlateBrush MarkBrush;MarkBrush.DrawAs=ESlateBrushDrawType::Image;MarkBrush.SetResourceObject(FEGVisualStyle::Texture(TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/T_EnemySkillAtlas")));
  MarkBrush.SetUVRegion(FBox2D(FVector2D(.75f,.75f),FVector2D(1.f,1.f)));Mark->SetBrush(MarkBrush);
  Mark->SetVisibility(ESlateVisibility::HitTestInvisible);Place(Mark,FAnchors(.055f,.04f,.165f,.12f),4);
 }
 const ESlateVisibility DecorationVisibility=Phase?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed;
 for(const TCHAR* Suffix:{TEXT("Bar"),TEXT("Rule"),TEXT("TitleFit"),TEXT("Badge"),TEXT("Mark")})
  if(auto* Widget=Tree->FindWidget(FName(*(Prefix+Suffix))))
  {
   const auto* Mark=Cast<UImage>(Widget);
   const bool bMissingMark=FString(Suffix)==TEXT("Mark")&&Mark&&!Mark->GetBrush().GetResourceObject();
   Widget->SetVisibility(bMissingMark?ESlateVisibility::Collapsed:DecorationVisibility);
  }
 if(auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot))
 {Slot->SetAnchors(Phase?FAnchors(0,0,1,.9f):FAnchors(0,0,1,1));Slot->SetOffsets(FMargin(0));}
 if(Phase)
 {
  // Image area aspect = (5/7)/0.9. UVs are in original-image coordinates.
  constexpr float AreaAspect=(5.f/7.f)/.9f;
  FSlateBrush Brush=Image->GetBrush();
  if(Phase==1)
  {
   const float Height=(2.f/3.f)/AreaAspect;
   Brush.SetUVRegion(FBox2D(FVector2D(0.f,1.f-Height),FVector2D(1.f,1.f)));
  }
  else
  {
   const float Width=AreaAspect/(16.f/9.f);constexpr float Center=650.f/1280.f;
   Brush.SetUVRegion(FBox2D(FVector2D(Center-Width*.5f,0.f),FVector2D(Center+Width*.5f,1.f)));
  }
  Image->SetBrush(Brush);
 }
}

void ApplyCorruptionAspect(UWidgetTree* Tree,UImage* Image)
{
 if(!Tree||!Image)return;
 const auto* Texture=Cast<UTexture2D>(Image->GetBrush().GetResourceObject());
 const bool bSpecial=Texture&&Texture->GetPathName()==TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption.T_Card_Corruption");
 const FName BackplateName(*(Image->GetName()+TEXT("_AspectBackplate")));
 auto* Backplate=Cast<UImage>(Tree->FindWidget(BackplateName));
 if(!bSpecial&&!Backplate)return;
 if(bSpecial&&!Backplate)
 {
  UPanelWidget* Parent=Image->GetParent();
  auto* Canvas=Cast<UCanvasPanel>(Parent);
  if(!Canvas)
  {
   // The pinned sidebar image is the content of a SizeBox. Add one reusable
   // canvas without changing that SizeBox's desired dimensions or its siblings.
   auto* Content=Cast<UContentWidget>(Parent);
   if(!Content)return;
   Image->RemoveFromParent();
   Canvas=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(*(Image->GetName()+TEXT("_AspectLayer"))));
   Canvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Content->SetContent(Canvas);
   auto* ImageSlot=Canvas->AddChildToCanvas(Image);ImageSlot->SetAnchors(FAnchors(0,0,1,1));ImageSlot->SetOffsets(FMargin(0));
  }
  const auto* ImageSlot=Cast<UCanvasPanelSlot>(Image->Slot);
  if(!ImageSlot)return;
  Backplate=Tree->ConstructWidget<UImage>(UImage::StaticClass(),BackplateName);
  Backplate->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.012f,.008f,.025f,1.f),0.f));
  auto* BackSlot=Canvas->AddChildToCanvas(Backplate);
  BackSlot->SetAnchors(ImageSlot->GetAnchors());BackSlot->SetOffsets(ImageSlot->GetOffsets());
  BackSlot->SetAlignment(ImageSlot->GetAlignment());BackSlot->SetZOrder(ImageSlot->GetZOrder()-1);
 }
 if(Backplate)Backplate->SetVisibility(bSpecial?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 // Source art is 1139 x 1381; the platform texture is resampled to 2048², so
 // reading the platform dimensions would introduce the distortion again.
 constexpr float SourceAspect=1139.f/1381.f;
 constexpr float CardAspect=5.f/7.f;
 Image->SetRenderTransformPivot(FVector2D(.5f,.5f));
 Image->SetRenderScale(bSpecial?FVector2D(1.f,CardAspect/SourceAspect):FVector2D(1.f));
}
}

FName FEGCardPresentation::CharacterKey(UTexture2D* Artwork)
{
 if(!Artwork)return NAME_None;
 const FString Path=Artwork->GetPathName();
 // Stigmata and unrelated illustrations may mention a character in their names.
 if(!Path.Contains(TEXT("/Assets/Cards/"))&&!Path.Contains(TEXT("/UI/Portraits/"))&&!Path.Contains(TEXT("/PearlReplica/Portraits/")))return NAME_None;
 if(Path.Contains(TEXT("Stigmata"))||Path.Contains(TEXT("Talent")))return NAME_None;
 for(const auto& Identity:Identities)if(Path.Contains(Identity.Key)||Path.Contains(Identity.Name))return Identity.Key;
 return NAME_None;
}
FName FEGCardPresentation::CharacterKey(const UEGHeroDefinition* Definition)
{
 if(!Definition)return NAME_None;
 switch(Definition->HeroKind)
 {
 case EEGHeroKind::Pardofelis:return TEXT("Pardo");
 case EEGHeroKind::Hua:return TEXT("Hua");
 case EEGHeroKind::Mobius:return TEXT("Mobius");
 case EEGHeroKind::CollapsedHua:return TEXT("HuaEnhanced");
 case EEGHeroKind::Sakura:return TEXT("Sakura");
 case EEGHeroKind::VillV:return TEXT("VillV");
 case EEGHeroKind::Can:return TEXT("Can");
 case EEGHeroKind::Eden:return TEXT("Eden");
 case EEGHeroKind::Aponia:return TEXT("Aponia");
 case EEGHeroKind::Griseo:return TEXT("Griseo");
 case EEGHeroKind::Knight:return TEXT("Knight");
 case EEGHeroKind::ElysiaHerrscher:return TEXT("ElysiaHerrscherOfHuman");
 default:break;
 }
 for(const auto& Identity:Identities)if(Definition->DisplayName.ToString()==Identity.Name)return Identity.Key;
 return NAME_None;
}
UTexture2D* FEGCardPresentation::ArtworkForHero(const UEGHeroDefinition* Definition)
{
 if(!Definition)return nullptr;
 // This authored identity belongs to the Corruption encounter; ordinary and
 // copied Elysia use their own HeroId and retain their existing card art.
 if(Definition->HeroId==FName(TEXT("ElysiaCorruption")))
 {
  if(auto* Art=OptionalArtwork(TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase1")))return Art;
  if(auto* Art=OptionalArtwork(TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption")))return Art;
 }
 if(const auto* Monster=Definition->MonsterAttack.Get())
 {
  const TCHAR* Path=nullptr;
  switch(Monster->Ultimate)
  {
   case EEGMonsterUltimate::Shield:Path=TEXT("/Game/Assets/Cards/Enemies/T_Card_Chest");break;
   case EEGMonsterUltimate::EnhancedShot:Path=TEXT("/Game/Assets/Cards/Enemies/T_Card_Eye");break;
   case EEGMonsterUltimate::TeamSpeed:Path=TEXT("/Game/Assets/Cards/Enemies/T_Card_Spore");break;
   case EEGMonsterUltimate::CrystalSeed:Path=TEXT("/Game/Assets/Cards/Enemies/T_Card_Flower");break;
   default:break;
  }
  if(Path)if(auto* Art=OptionalArtwork(Path))return Art;
 }
 else if(auto* Art=CardTexture(CharacterKey(Definition)))return Art;
 // Runtime variants without an approved identity can carry authored form art.
 // Guard incomplete split definitions before calling their checked accessor.
 if(!Definition->bUseSplitConfiguration||Definition->PresentationDefinition)
  return OptionalArtwork(Definition->GetFormArtwork().ToSoftObjectPath().ToString());
 return nullptr;
}
UTexture2D* FEGCardPresentation::ResolveArtwork(UTexture2D* Artwork)
{if(auto* Approved=CardTexture(CharacterKey(Artwork)))return Approved;return Artwork;}

UCanvasPanel* FEGCardPresentation::Build(UWidgetTree* Tree,bool bShop)
{
 if(!Tree)return nullptr;
 auto* Root=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("CardCanvas"));
 Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 Root->SetClipping(EWidgetClipping::ClipToBounds);
 auto* Image=Tree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("CardImage"));
 Image->SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* Slot=Root->AddChildToCanvas(Image);Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(FMargin(0));
 return Root;
}
FEGCardAppearance FEGCardPresentation::Appearance(UTexture2D* Artwork,bool bShop)
{
 FEGCardAppearance Result;Result.Portrait=PortraitBrush(Artwork,FVector2D(2400,3360));
 Result.PortraitBounds=FVector4(0,0,1,1);
 return Result;
}
void FEGCardPresentation::ApplyAuthored(UWidgetTree* Tree,const FEGCardAppearance& Appearance)
{
 if(!Tree)return;
 if(auto* Image=Cast<UImage>(Tree->FindWidget(TEXT("CardImage"))))
 {
  auto* Texture=Cast<UTexture2D>(Appearance.Portrait.GetResourceObject());
  Image->SetBrush(PortraitBrush(Texture,FVector2D(2400,3360)));
  if(auto* Slot=Cast<UCanvasPanelSlot>(Image->Slot)){Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(FMargin(0));}
  ApplyCorruptionAspect(Tree,Image);
  ApplyCorruptionPhase(Tree,Image);
 }
 // The approved raster already contains its signet and full-width name strip.
 for(const TCHAR* Name:{TEXT("CardGlassBlur"),TEXT("CardBackdrop"),TEXT("CardDecoration"),TEXT("CardNameStrip"),TEXT("CardSignetFit"),TEXT("CardSignet"),TEXT("CardNameFit"),TEXT("CardName")})
  if(auto* Widget=Tree->FindWidget(Name))Widget->SetVisibility(ESlateVisibility::Collapsed);
}
void FEGCardPresentation::Apply(UWidgetTree* Tree,UTexture2D* Artwork,bool bShop)
{ApplyAuthored(Tree,Appearance(Artwork,bShop));}

void FEGCardPresentation::ApplyDefinitionLayers(UWidgetTree* Tree,UImage* Image,const UEGHeroDefinition* Definition,int32 Level)
{
 if(!Tree||!Image)return;
 const FString Prefix=Image->GetName()+TEXT("_Definition");
 const FName ContainerName(*(Prefix+TEXT("Layer")));
 const FName FrameName(*(Prefix+TEXT("Frame")));
 const FName NameName(*(Prefix+TEXT("Name")));
 const FName LevelName(*(Prefix+TEXT("Level")));
 auto* Frame=Cast<UImage>(Tree->FindWidget(FrameName));
 auto* Name=Cast<UTextBlock>(Tree->FindWidget(NameName));
 auto* LevelText=Cast<UTextBlock>(Tree->FindWidget(LevelName));
 UTexture2D* FrameTexture=Definition&&!CorruptionPhaseForImage(Image)&&!Definition->CardFrameTexture.IsNull()?Definition->CardFrameTexture.LoadSynchronous():nullptr;
 if(FrameTexture&&!Frame)
 {
  UPanelWidget* Parent=Image->GetParent();
  auto* CanvasParent=Cast<UCanvasPanel>(Parent);
  auto* ContentParent=Cast<UContentWidget>(Parent);
  if(!CanvasParent&&!ContentParent)return;
  FAnchors Anchors;FMargin Offsets;FVector2D Alignment=FVector2D::ZeroVector;int32 Z=0;
  if(CanvasParent)
  {
   const auto* Old=Cast<UCanvasPanelSlot>(Image->Slot);if(!Old)return;
   Anchors=Old->GetAnchors();Offsets=Old->GetOffsets();Alignment=Old->GetAlignment();Z=Old->GetZOrder();
  }
  Image->RemoveFromParent();
  auto* Layer=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),ContainerName);
  Layer->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Layer->SetClipping(EWidgetClipping::ClipToBounds);
  if(CanvasParent)
  {
   auto* Slot=CanvasParent->AddChildToCanvas(Layer);Slot->SetAnchors(Anchors);Slot->SetOffsets(Offsets);Slot->SetAlignment(Alignment);Slot->SetZOrder(Z);
  }
  else ContentParent->SetContent(Layer);
  auto Fill=[Layer](UWidget* Widget,int32 Order)
  {auto* Slot=Layer->AddChildToCanvas(Widget);Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(FMargin(0));Slot->SetZOrder(Order);};
  Fill(Image,0);
  Frame=Tree->ConstructWidget<UImage>(UImage::StaticClass(),FrameName);Frame->SetVisibility(ESlateVisibility::HitTestInvisible);Fill(Frame,1);
  auto AddFittedLabel=[Tree,Layer](FName LabelName,FAnchors Area,int32 FontSize)
  {
   auto* Fit=Tree->ConstructWidget<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetStretchDirection(EStretchDirection::DownOnly);
   Fit->SetVisibility(ESlateVisibility::HitTestInvisible);
   auto* Label=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),LabelName);
   Label->SetFont(FEGVisualStyle::PearlFont(FontSize));Label->SetColorAndOpacity(FSlateColor(FLinearColor(.98f,.94f,.91f)));
   Label->SetAutoWrapText(false);Label->SetJustification(ETextJustify::Center);Label->SetVisibility(ESlateVisibility::HitTestInvisible);
   Fit->SetContent(Label);auto* Slot=Layer->AddChildToCanvas(Fit);Slot->SetAnchors(Area);Slot->SetOffsets(FMargin(0));Slot->SetZOrder(2);return Label;
  };
  Name=AddFittedLabel(NameName,FAnchors(.13f,.89f,.87f,.975f),30);
  LevelText=AddFittedLabel(LevelName,FAnchors(.73f,.04f,.92f,.12f),18);
 }
 if(Frame){Frame->SetBrushFromTexture(FrameTexture,false);Frame->SetVisibility(FrameTexture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
 if(Name){Name->SetText(Definition?Definition->DisplayName:FText::GetEmpty());if(auto* Parent=Name->GetParent())Parent->SetVisibility(FrameTexture?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
 if(LevelText){LevelText->SetText(Level>0?FText::FromString(FString::Printf(TEXT("Lv.%d"),Level)):FText::GetEmpty());if(auto* Parent=LevelText->GetParent())Parent->SetVisibility(FrameTexture&&Level>0?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
 ApplyCorruptionAspect(Tree,Image);
 ApplyCorruptionPhase(Tree,Image);
}
FSlateBrush FEGCardPresentation::PortraitBrush(UTexture2D* OriginalArtwork,FVector2D DisplaySize)
{
 FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageSize=DisplaySize;
 Brush.SetResourceObject(ResolveArtwork(OriginalArtwork));
 // No UV crop. The destination widgets use the same 5:7 ratio.
 return Brush;
}
