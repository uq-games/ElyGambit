#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/EGCardPresentation.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGShopCard.h"
#include "Data/EGHeroDefinition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "TextureCompiler.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetToolsModule.h"
#include "Modules/ModuleManager.h"

namespace
{
const FString BoardFolder=TEXT("/Game/Blueprints/UI/Widgets/Board/");
bool Persist(UWidgetBlueprint* Blueprint,bool bCompile)
{
 if(bCompile)
 {
  TSet<FName> Names;
  Blueprint->ForEachSourceWidget([Blueprint,&Names](UWidget* Widget)
  {if(!Widget)return;Names.Add(Widget->GetFName());if(!Blueprint->WidgetVariableNameToGuidMap.Contains(Widget->GetFName()))Blueprint->WidgetVariableNameToGuidMap.Add(Widget->GetFName(),FGuid::NewDeterministicGuid(Widget->GetPathName()));});
  for(auto It=Blueprint->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
  FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
  FKismetEditorUtilities::CompileBlueprint(Blueprint);
 }
 if(Blueprint->Status==BS_Error)return false;
 // CDO edits after compilation must also reach newly constructed widget instances.
 if(auto* Class=Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
 Blueprint->MarkPackageDirty();return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(Blueprint,false);
}
template<class T>T* Make(UWidgetBlueprint* Blueprint,const TCHAR* Name)
{
 if(auto* Existing=Cast<T>(Blueprint->WidgetTree->FindWidget(Name)))return Existing;
 return Blueprint->WidgetTree->ConstructWidget<T>(T::StaticClass(),FName(Name));
}
void Place(UCanvasPanel* Root,UWidget* Widget,FVector2D Position,FVector2D Size,int32 Z=0)
{
 Widget->RemoveFromParent();auto* Slot=Root->AddChildToCanvas(Widget);Slot->SetPosition(Position);Slot->SetSize(Size);Slot->SetZOrder(Z);
}
UTextBlock* Caption(UWidgetBlueprint* Blueprint,const TCHAR* Name,const TCHAR* Text,int32 Size)
{
 auto* Widget=Make<UTextBlock>(Blueprint,Name);Widget->SetText(FText::FromString(Text));Widget->SetFont(FEGVisualStyle::PearlFont(Size));
 Widget->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Widget->SetJustification(ETextJustify::Center);Widget->SetVisibility(ESlateVisibility::HitTestInvisible);return Widget;
}
void PatchTree(UWidgetBlueprint* Blueprint,const TMap<FName,UTexture2D*>& Artwork)
{
 if(!Blueprint||!Blueprint->WidgetTree)return;
 // Replace identity artwork in existing Designer trees without rebuilding layouts.
 Blueprint->WidgetTree->ForEachWidget([&](UWidget* Widget)
 {
  if(auto* Image=Cast<UImage>(Widget))
  {
   if(auto* Texture=Cast<UTexture2D>(Image->GetBrush().GetResourceObject()))
    if(auto* Found=Artwork.Find(FEGCardPresentation::CharacterKey(Texture)))Image->SetBrushFromTexture(*Found,false);
  }
 });
 auto* Default=Blueprint->GeneratedClass?Blueprint->GeneratedClass->GetDefaultObject():nullptr;
 if(Cast<UEGHandCard>(Default)||Cast<UEGShopCard>(Default))
 {
  FEGCardPresentation::ApplyAuthored(Blueprint->WidgetTree,FEGCardPresentation::Appearance(Artwork.FindRef(TEXT("Pardo"))));
  if(auto* Size=Cast<USizeBox>(Blueprint->WidgetTree->FindWidget(TEXT("CardSize"))))
  {Size->SetWidthOverride(EGPearlGold::HandCardWidth);Size->SetHeightOverride(EGPearlGold::HandCardHeight);}
  if(auto* Description=Blueprint->WidgetTree->FindWidget(TEXT("ShopCardDescription")))Description->SetVisibility(ESlateVisibility::Collapsed);
 }
 for(const TCHAR* Name:{TEXT("UnitDetailPortrait"),TEXT("DrawCardArtwork")})
  if(auto* Widget=Blueprint->WidgetTree->FindWidget(Name))if(auto* Slot=Cast<UCanvasPanelSlot>(Widget->Slot))
  {const FVector2D Size=Slot->GetSize();if(Size.X>0)Slot->SetSize(FVector2D(Size.X,Size.X*1.4f));}
 for(int32 Index=0;Index<4;++Index)
  if(auto* Widget=Blueprint->WidgetTree->FindWidget(FName(*FString::Printf(TEXT("ShopSlot_%d"),Index))))if(auto* Slot=Cast<UCanvasPanelSlot>(Widget->Slot))
   Slot->SetSize(FVector2D(EGPearlGold::ShopCardWidth,EGPearlGold::ShopCardHeight));
}
}
#endif

bool FEGWidgetAuthoringService::RemoveCompanionCatalogue()
{
#if WITH_EDITOR
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(BoardFolder+TEXT("WBP_BoardHUD")));if(!HUD||!HUD->WidgetTree)return false;
 for(const TCHAR* Name:{TEXT("Button_Companions"),TEXT("CompanionCatalogue")})
  if(auto* Widget=HUD->WidgetTree->FindWidget(Name))HUD->WidgetTree->RemoveWidget(Widget);
 if(auto* Defaults=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject()))Defaults->InspectableCompanions.Reset();
 return Persist(HUD,true);
#else
 return false;
#endif
}

bool FEGWidgetAuthoringService::ApplyOfficialCardAssets(const TMap<FName,UTexture2D*>& Artwork,const TArray<UEGHeroDefinition*>& Companions)
{
#if WITH_EDITOR
 if(!GEditor||Artwork.Num()!=13||Companions.Num()!=1){UE_LOG(LogTemp,Error,TEXT("OfficialCard: invalid catalogue inputs"));return false;}
 TArray<UTexture*> PendingTextures;
 for(const auto& Pair:Artwork)if(Pair.Value)PendingTextures.Add(Pair.Value);
 FTextureCompilingManager::Get().FinishCompilation(PendingTextures);
 for(const auto& Pair:Artwork)if(!Pair.Value||Pair.Value->Source.GetSizeX()!=2400||Pair.Value->Source.GetSizeY()!=3360)
 {UE_LOG(LogTemp,Error,TEXT("OfficialCard: invalid source dimensions for %s"),*Pair.Key.ToString());return false;}
 for(auto* Definition:Companions)if(!Definition||Definition->HeroKind!=EEGHeroKind::Can)return false;
 auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(BoardFolder+TEXT("WBP_BoardHUD")));
 auto* Card=LoadObject<UWidgetBlueprint>(nullptr,*(BoardFolder+TEXT("WBP_HandCard")));
 auto* ShopCard=LoadObject<UWidgetBlueprint>(nullptr,*(BoardFolder+TEXT("WBP_ShopCard")));
 if(!HUD||!Card||!ShopCard){UE_LOG(LogTemp,Error,TEXT("OfficialCard: missing Board Widget Blueprints"));return false;}
 UE_LOG(LogTemp,Display,TEXT("OfficialCard: patching saved widget trees"));
 for(const auto& Path:Assets->ListAssets(TEXT("/Game/Blueprints/UI/Widgets"),true,false))
  if(auto* Blueprint=LoadObject<UWidgetBlueprint>(nullptr,*Path);Blueprint&&Blueprint->WidgetTree&&Blueprint->WidgetTree->RootWidget)
  {PatchTree(Blueprint,Artwork);if(!Persist(Blueprint,true)){UE_LOG(LogTemp,Error,TEXT("OfficialCard: compile/save failed for %s"),*Path);return false;}}

 // The companion catalogue was cancelled; remove its saved entry and subtree.
 for(const TCHAR* Name:{TEXT("Button_Companions"),TEXT("CompanionCatalogue")})
  if(auto* Widget=HUD->WidgetTree->FindWidget(Name))HUD->WidgetTree->RemoveWidget(Widget);
 if(!Persist(HUD,true))return false;
 TMap<FName,TSubclassOf<UEGHandCard>> HandClasses;
 TMap<FName,TSubclassOf<UEGShopCard>> ShopClasses;
 for(const auto& Pair:Artwork)
  if(Pair.Key==TEXT("Pardo"))for(bool bShop:{false,true})
  {
   auto* Parent=bShop?ShopCard:Card;
   const FString Name=FString(bShop?TEXT("WBP_ShopCard_"):TEXT("WBP_Card_"))+Pair.Key.ToString(),Folder=BoardFolder+TEXT("Cards");
   auto* Child=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("/")+Name));
   if(!Child)
   {auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=Parent->GeneratedClass;Child=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateAsset(Name,Folder,UWidgetBlueprint::StaticClass(),Factory));}
   if(!Child||Child->ParentClass!=Parent->GeneratedClass||!Persist(Child,true))return false;
   if(bShop)
   {auto* Defaults=Cast<UEGShopCard>(Child->GeneratedClass->GetDefaultObject());Defaults->bAuthoredAppearance=true;Defaults->Appearance=FEGCardPresentation::Appearance(Pair.Value,true);ShopClasses.Add(Pair.Key,Child->GeneratedClass.Get());}
   else
   {auto* Defaults=Cast<UEGHandCard>(Child->GeneratedClass->GetDefaultObject());Defaults->bAuthoredAppearance=true;Defaults->Appearance=FEGCardPresentation::Appearance(Pair.Value);HandClasses.Add(Pair.Key,Child->GeneratedClass.Get());}
   if(!Persist(Child,false))return false;
  }
 // Child compilation may reinstance dependent HUD classes; acquire its final CDO now.
 auto* HUDDefaults=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject());if(!HUDDefaults)return false;
 HUDDefaults->InspectableCompanions.Reset();
 HUDDefaults->CardArtwork=Artwork.FindRef(TEXT("Pardo"));HUDDefaults->CharacterCardClasses=MoveTemp(HandClasses);HUDDefaults->ShopCardClasses=MoveTemp(ShopClasses);
 return Persist(HUD,false);
#else
 return false;
#endif
}
