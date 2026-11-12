#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/Widget/EGGradientSurface.h"
#include "UI/Widget/EGGoldCoin.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGPearlGoldLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"
#include "UObject/Linker.h"

namespace
{
template<class T>T* FindOrMake(UWidgetBlueprint* BP,const TCHAR* Name)
{if(auto* Widget=Cast<T>(BP->WidgetTree->FindWidget(Name)))return Widget;auto* Widget=BP->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name);BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(),FGuid::NewDeterministicGuid(Widget->GetPathName()));return Widget;}
void PlaceReference(UCanvasPanel* Canvas,UWidget* Widget,FAnchors Anchors,FVector2D Position,FVector2D Size,FVector2D Alignment=FVector2D::ZeroVector,int32 Z=0)
{Widget->RemoveFromParent();auto* Slot=Canvas->AddChildToCanvas(Widget);Slot->SetAnchors(Anchors);Slot->SetPosition(Position);Slot->SetSize(Size);Slot->SetAlignment(Alignment);Slot->SetZOrder(Z);}
bool SaveReference(UWidgetBlueprint* BP)
{
 TSet<FName> Names;BP->ForEachSourceWidget([&](UWidget* W){if(!W)return;Names.Add(W->GetFName());if(!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));});
 for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
 if(BP->Status==BS_Error)return false;
 if(auto* Class=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
 ResetLoaders(BP->GetOutermost());BP->MarkPackageDirty();return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
}
void ReplaceCurrencyIcons(UWidgetBlueprint* BP)
{
 TArray<UImage*> Images;
 BP->WidgetTree->ForEachWidget([&](UWidget* W){if(auto* Image=Cast<UImage>(W);Image&&!Image->IsA<UEGGoldCoin>())if(auto* Resource=Image->GetBrush().GetResourceObject();Resource&&Resource->GetPathName().Contains(TEXT("icon_currency_mission_coin")))Images.Add(Image);});
 for(auto* Image:Images)
 {
  auto* Parent=Image->GetParent();if(!Parent)continue;const int32 Index=Parent->GetChildIndex(Image);const FName Name=Image->GetFName();
  Image->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
  auto* Coin=BP->WidgetTree->ConstructWidget<UEGGoldCoin>(UEGGoldCoin::StaticClass(),Name);Coin->SetVisibility(ESlateVisibility::HitTestInvisible);
  Parent->ReplaceChildAt(Index,Coin);
  if(!BP->WidgetVariableNameToGuidMap.Contains(Name))BP->WidgetVariableNameToGuidMap.Add(Name,FGuid::NewDeterministicGuid(Coin->GetPathName()));
 }
}
void GradientButton(UWidgetBlueprint* BP,const TCHAR* ButtonName,const TCHAR* SurfaceName,float Radius)
{
 auto* Button=Cast<UButton>(BP->WidgetTree->FindWidget(ButtonName));if(!Button)return;
 auto* Surface=FindOrMake<UEGGradientSurface>(BP,SurfaceName);
 if(auto* Old=Button->GetContent();Old&&Old!=Surface){Old->RemoveFromParent();Surface->SetContent(Old);}
 Surface->CornerRadius=Radius;Surface->TopColor=EGPearlGold::Srgb(18,30,55,.94f);Surface->BottomColor=EGPearlGold::Srgb(5,10,24,.98f);Surface->SetVisibility(ESlateVisibility::HitTestInvisible);
 Button->SetContent(Surface);
 if(auto* ContentSlot=Cast<UButtonSlot>(Surface->Slot)){ContentSlot->SetPadding(FMargin(0));ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);}
 FButtonStyle Style;Style.SetNormal(FEGVisualStyle::Rounded(FLinearColor::Transparent,Radius,EGPearlGold::Srgb(172,195,231,.55f),1.f));
 Style.SetHovered(FEGVisualStyle::Rounded(FLinearColor(1,1,1,.09f),Radius,EGPearlGold::Srgb(205,224,255,.85f),1.f));
 Style.SetPressed(FEGVisualStyle::Rounded(FLinearColor(0,0,0,.16f),Radius,EGPearlGold::Srgb(205,224,255,.65f),1.f));Style.SetDisabled(Style.Normal);
 Style.NormalPadding=Style.PressedPadding=FMargin(0);Button->SetStyle(Style);
 if(auto* Blur=BP->WidgetTree->FindWidget(FName(*(FString(ButtonName)+TEXT("GlassBlur")))))Blur->SetVisibility(ESlateVisibility::Collapsed);
}
}
#endif

bool FEGWidgetAuthoringService::ApplyReferenceShopUI()
{
#if WITH_EDITOR
 if(!GEditor||GEditor->PlayWorld)return false;
 // Retain the original mip and a filtered reduction chain: drawing a 2400px
 // image into a 140-270px card with only four mip-0 samples aliases fine lines.
 auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();TArray<UTexture*> Pending;
 for(const auto& Path:Assets->ListAssets(TEXT("/Game/Assets/Cards"),true,false))
  if(auto* Texture=LoadObject<UTexture2D>(nullptr,*Path);Texture&&Texture->Source.GetSizeX()==2400&&Texture->Source.GetSizeY()==3360)
  {Texture->MipGenSettings=TMGS_Sharpen4;Texture->Filter=TF_Trilinear;Texture->LODBias=0;Texture->MaxTextureSize=0;Texture->NeverStream=true;Texture->PostEditChange();Pending.Add(Texture);}
 FTextureCompilingManager::Get().FinishCompilation(Pending);
 for(auto* Texture:Pending){ResetLoaders(Texture->GetOutermost());if(!Assets->SaveLoadedAsset(Texture,false))return false;}
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_BoardHUD"));
 auto* Shop=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_Shop"));
 if(!HUD||!Shop||!HUD->WidgetTree||!Shop->WidgetTree)return false;
 auto* Root=Cast<UCanvasPanel>(HUD->WidgetTree->RootWidget);auto* ShopRoot=Cast<UCanvasPanel>(Shop->WidgetTree->FindWidget(TEXT("ShopRoot")));
 auto* Round=Cast<UTextBlock>(HUD->WidgetTree->FindWidget(TEXT("Text_Round")));auto* Corruption=Cast<UTextBlock>(HUD->WidgetTree->FindWidget(TEXT("Text_Corruption")));
 if(!Root||!ShopRoot||!Round||!Corruption)return false;
 for(const TCHAR* Name:{TEXT("BoardThemeBanner"),TEXT("RoundGlass"),TEXT("CorruptionDock"),TEXT("CorruptionBlur"),TEXT("BoardThemeBannerGlassBlur"),TEXT("Text_ShopTitle"),TEXT("Text_ShopProbability")})
  if(auto* Widget=HUD->WidgetTree->FindWidget(Name))Widget->SetVisibility(ESlateVisibility::Collapsed);
 auto* Header=FindOrMake<UCanvasPanel>(HUD,TEXT("ReferenceRoundHeader"));Header->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 PlaceReference(Root,Header,FAnchors(.5f,0),FVector2D(0,12),FVector2D(760,50),FVector2D(.5f,0),30);
 PlaceReference(Header,Round,FAnchors(0,0),FVector2D(150,2),FVector2D(210,30));Round->SetFont(FEGVisualStyle::PearlFont(21));Round->SetJustification(ETextJustify::Right);Round->SetText(FText::FromString(TEXT("第 1 回合")));
 PlaceReference(Header,Corruption,FAnchors(0,0),FVector2D(400,2),FVector2D(210,30));Corruption->SetFont(FEGVisualStyle::PearlFont(21));Corruption->SetJustification(ETextJustify::Left);
 auto* Divider=FindOrMake<UImage>(HUD,TEXT("ReferenceHeaderDivider"));Divider->SetBrush(FEGVisualStyle::Rounded(FLinearColor(1,1,1,.75f),0));Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
 PlaceReference(Header,Divider,FAnchors(0,0),FVector2D(380,6),FVector2D(1,22));
 auto* Rule=FindOrMake<UImage>(HUD,TEXT("ReferenceHeaderRule"));Rule->SetBrush(FEGVisualStyle::Rounded(FLinearColor(1,1,1,.22f),0));Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
 PlaceReference(Header,Rule,FAnchors(0,0),FVector2D(0,42),FVector2D(760,1));
 if(auto* Button=Cast<UButton>(HUD->WidgetTree->FindWidget(TEXT("Button_Stage"))))
 {PlaceReference(Root,Button,FAnchors(.5f,0),FVector2D(0,12),FVector2D(760,50),FVector2D(.5f,0),31);Button->SetStyle(FEGVisualStyle::Ghost());Button->SetContent(nullptr);}
 GradientButton(HUD,TEXT("Button_ToggleDrawPanel"),TEXT("ShopOrbGradient"),62.f);
 GradientButton(HUD,TEXT("Button_Battle"),TEXT("BattleButtonGradient"),28.f);

 auto* Surface=FindOrMake<UEGGradientSurface>(Shop,TEXT("ShopGradientSurface"));
 Surface->TopColor=FLinearColor(0,0,0,.88f);Surface->BottomColor=FLinearColor::Transparent;Surface->bTrapezoidReveal=true;Surface->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 if(Surface->GetContent()!=ShopRoot){ShopRoot->RemoveFromParent();Surface->SetContent(ShopRoot);}Shop->WidgetTree->RootWidget=Surface;
 for(int32 I=0;I<4;++I)
  if(auto* Offer=Shop->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("ShopSlot_%d"),I))))
  {const auto R=EGPearlGold::ShopCardRect(I);PlaceReference(ShopRoot,Offer,FAnchors(.5f,0),FVector2D(R.X-960,R.Y),R.Size(),FVector2D::ZeroVector,3);}
 for(const auto& Pair:TArray<TPair<FName,EGPearlGold::FLayoutRect>>{{TEXT("Button_ShopLock"),EGPearlGold::ShopLock},{TEXT("Button_RefreshShop"),EGPearlGold::ShopRefresh}})
  if(auto* Button=Shop->WidgetTree->FindWidget(Pair.Key))PlaceReference(ShopRoot,Button,FAnchors(.5f,0),FVector2D(Pair.Value.X-960,Pair.Value.Y),Pair.Value.Size(),FVector2D::ZeroVector,4);
 for(const TCHAR* Name:{TEXT("Button_ShopLockGlassBlur"),TEXT("Button_RefreshShopGlassBlur")})if(auto* Widget=Shop->WidgetTree->FindWidget(Name))Widget->SetVisibility(ESlateVisibility::Collapsed);
 ReplaceCurrencyIcons(Shop);ReplaceCurrencyIcons(HUD);
 for(const TCHAR* Name:{TEXT("WBP_ShopCard"),TEXT("WBP_SellLeft"),TEXT("WBP_SellRight")})
  if(auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(FString(TEXT("/Game/Blueprints/UI/Widgets/Board/"))+Name)))
  {ReplaceCurrencyIcons(BP);if(!SaveReference(BP))return false;}
 if(!SaveReference(Shop))return false;
 // Compiling the nested shop can reinstance HUD; reacquire the current tree.
 auto* Draw=HUD->WidgetTree->FindWidget(TEXT("DrawPanel"));if(!Draw)return false;
 PlaceReference(Cast<UCanvasPanel>(HUD->WidgetTree->RootWidget),Draw,FAnchors(0,0,1,0),FVector2D::ZeroVector,FVector2D(0,EGPearlGold::ShopPanelHeight),FVector2D::ZeroVector,26);
 Draw->SetVisibility(ESlateVisibility::Collapsed);Draw->SetRenderOpacity(1.f);Draw->SetRenderScale(FVector2D(1));Draw->SetRenderTranslation(FVector2D::ZeroVector);
 return SaveReference(HUD);
#else
 return false;
#endif
}
