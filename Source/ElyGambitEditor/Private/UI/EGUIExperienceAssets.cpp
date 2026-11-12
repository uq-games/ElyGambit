#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/Widget/EGGoldCoin.h"
#include "UI/Widget/EGShopCard.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "WidgetBlueprint.h"
#include "UObject/Linker.h"
#endif

bool FEGWidgetAuthoringService::ApplyShopPriceBadge()
{
#if WITH_EDITOR
 if(!GEditor||GEditor->PlayWorld)return false;
 auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_ShopCard"));
 if(!BP||!BP->WidgetTree||BP->GetOutermost()->IsDirty())return false;
 UEGShopCard::BuildPriceBadge(BP->WidgetTree);
 for(const TCHAR* Name:{TEXT("ShopPriceTag"),TEXT("ShopPriceTagRow"),TEXT("ShopPriceTagIconSize")})
 {
  auto* W=BP->WidgetTree->FindWidget(Name);if(!W)return false;
  BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
 if(BP->Status==BS_Error)return false;
 if(auto* Class=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
 ResetLoaders(BP->GetOutermost());BP->MarkPackageDirty();
 return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
#else
 return false;
#endif
}

bool FEGWidgetAuthoringService::ApplyUIExperienceAssets()
{
#if WITH_EDITOR
 if(!GEditor||GEditor->PlayWorld)return false;
 auto* Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/UIExperience20261026/T_HonkaiEnergy"));
 if(!Texture)return false;
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
 TArray<FAssetData> Assets;Registry.GetAssetsByPath(TEXT("/Game/Blueprints/UI"),Assets,true);
 for(const auto& Asset:Assets)
 {
  if(Asset.AssetClassPath!=UWidgetBlueprint::StaticClass()->GetClassPathName())continue;
  auto* BP=Cast<UWidgetBlueprint>(Asset.GetAsset());if(!BP||!BP->WidgetTree)continue;
  // The owned editor starts clean; never save a target with user edits in memory.
  if(BP->GetOutermost()->IsDirty())return false;
  bool bChanged=false;
  BP->WidgetTree->ForEachWidget([&](UWidget* W)
  {
   if(auto* Image=Cast<UImage>(W))
   {
    const auto* Resource=Image->GetBrush().GetResourceObject();
    if(Image->IsA<UEGGoldCoin>()||(Resource&&Resource->GetPathName().Contains(TEXT("icon_currency_mission_coin"))))
    {Image->SetBrushFromTexture(Texture,false);Image->SetColorAndOpacity(FLinearColor::White);bChanged=true;}
    if(W->GetName().StartsWith(TEXT("SellPlateArt")))
    {Image->SetColorAndOpacity(FLinearColor(.72f,.12f,.16f,1));bChanged=true;}
   }
   if(auto* Label=Cast<UTextBlock>(W))
   {
    FString Text=Label->GetText().ToString();
    if(Text.Contains(TEXT("金币"))){Text.ReplaceInline(TEXT("金币"),TEXT("崩坏能"));Label->SetText(FText::FromString(Text));bChanged=true;}
   }
  });
  // The active HUD presents its balance inside the shop orb. Keep its bound text
  // and button, adding a small source icon beside the existing value.
  if(auto* Column=Cast<UVerticalBox>(BP->WidgetTree->FindWidget(TEXT("GeneratedShopColumn"))))
  if(auto* Value=Cast<UTextBlock>(BP->WidgetTree->FindWidget(TEXT("Text_Gold"))))
  if(!BP->WidgetTree->FindWidget(TEXT("EnergyValueRow")))
  {
   auto* Row=BP->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("EnergyValueRow"));
   auto* Size=BP->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("EnergyBalanceIconSize"));
   Size->SetWidthOverride(22);Size->SetHeightOverride(22);
   auto* Icon=BP->WidgetTree->ConstructWidget<UEGGoldCoin>(UEGGoldCoin::StaticClass(),TEXT("EnergyBalanceIcon"));
   Icon->SetBrushFromTexture(Texture,false);Icon->SetVisibility(ESlateVisibility::HitTestInvisible);Size->SetContent(Icon);
   auto* IconSlot=Row->AddChildToHorizontalBox(Size);IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0,0,4,0));
   Value->RemoveFromParent();Row->AddChildToHorizontalBox(Value)->SetVerticalAlignment(VAlign_Center);
   Row->SetVisibility(ESlateVisibility::HitTestInvisible);
   if(auto* RowSlot=Cast<UVerticalBoxSlot>(Column->InsertChildAt(0,Row))){RowSlot->SetHorizontalAlignment(HAlign_Center);RowSlot->SetPadding(FMargin(0,0,0,2));}
   for(auto* W:TArray<UWidget*>{Row,Size,Icon})BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));
   Value->SetToolTipText(FText::FromString(TEXT("崩坏能")));bChanged=true;
  }
  if(!bChanged)continue;
  FBlueprintEditorUtils::MarkBlueprintAsModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
  if(BP->Status==BS_Error)return false;
  if(auto* Class=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
  ResetLoaders(BP->GetOutermost());BP->MarkPackageDirty();
  if(!GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false))return false;
 }
 return true;
#else
 return false;
#endif
}
