#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetToolsModule.h"
#include "Engine/BlueprintGeneratedClass.h"

namespace
{
bool SaveShopBP(UWidgetBlueprint* BP)
{
 BP->ForEachSourceWidget([&](UWidget* W){if(W&&!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));});
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
 if(BP->Status==BS_Error)return false;
 if(auto* C=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))C->UpdateCustomPropertyListForPostConstruction();
 BP->MarkPackageDirty();return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
}
}
#endif
bool FEGWidgetAuthoringService::ApplyShopDeploymentAssets()
{
#if WITH_EDITOR
 if(!GEditor||GEditor->PlayWorld)return false;
 const FString Folder=TEXT("/Game/Blueprints/UI/Widgets/Board/");
 auto MakeInfo=[&](const TCHAR* Name,bool bDrag)->UWidgetBlueprint*
 {
  auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+Name));
  if(!BP)
  {
   auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=UEGHeroInfoWidget::StaticClass();
   BP=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(Name,Folder.LeftChop(1),UWidgetBlueprint::StaticClass(),Factory));
   if(!BP)return nullptr;
   UEGHeroInfoWidget::BuildLayout(BP->WidgetTree,bDrag);
  }
  if(!SaveShopBP(BP))return nullptr;
  auto* CDO=Cast<UEGHeroInfoWidget>(BP->GeneratedClass->GetDefaultObject());CDO->bDragLayout=bDrag;
  if(auto* C=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))C->UpdateCustomPropertyListForPostConstruction();
  BP->MarkPackageDirty();if(!GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false))return nullptr;
  return BP;
 };
 auto* Tip=MakeInfo(TEXT("WBP_HeroSkillTooltip"),false);auto* Drag=MakeInfo(TEXT("WBP_ShopDragPreview"),true);if(!Tip||!Drag)return false;
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("WBP_BoardHUD")));if(!HUD)return false;
 auto* Scroll=Cast<UScrollBox>(HUD->WidgetTree->FindWidget(TEXT("UnitDetailScroll")));
 if(Scroll&&!HUD->WidgetTree->FindWidget(TEXT("DetailSkillInformation")))
 {
  // Preserve the original named text and bindings; add the shared icon-based view.
  auto* Info=HUD->WidgetTree->ConstructWidget<UEGHeroInfoWidget>(Tip->GeneratedClass.Get(),TEXT("DetailSkillInformation"));
  auto* Size=HUD->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("DetailSkillSize"));Size->SetHeightOverride(420);Size->SetContent(Info);Scroll->AddChild(Size);
  if(auto* S=Cast<UCanvasPanelSlot>(Scroll->Slot)){S->SetAnchors(FAnchors(0,0,1,1));S->SetOffsets(FMargin(18,370,18,64));}
  if(auto* Stats=HUD->WidgetTree->FindWidget(TEXT("Text_UnitStats")))if(auto* S=Cast<UCanvasPanelSlot>(Stats->Slot))S->SetSize(FVector2D(360,80));
 }
 if(!SaveShopBP(HUD))return false;
 auto* CDO=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject());CDO->SkillInformationClass=Tip->GeneratedClass;CDO->ShopDragInformationClass=Drag->GeneratedClass;
 if(auto* C=Cast<UBlueprintGeneratedClass>(HUD->GeneratedClass))C->UpdateCustomPropertyListForPostConstruction();HUD->MarkPackageDirty();
 if(!GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(HUD,false))return false;
 for(const TCHAR* Name:{TEXT("WBP_Shop"),TEXT("WBP_ShopCard")})
 {
  auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+Name));if(!BP)return false;
  if(auto* Text=BP->WidgetTree->FindWidget(TEXT("ShopCardDescription")))Text->SetVisibility(ESlateVisibility::Collapsed);
  if(!SaveShopBP(BP))return false;
 }
 return true;
#else
 return false;
#endif
}
