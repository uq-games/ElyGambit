#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "AssetToolsModule.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Animation/WidgetAnimation.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_Variable.h"
#include "K2Node_ComponentBoundEvent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
bool CompileInformationBP(UWidgetBlueprint* BP)
{
 if(!BP)return false;
 BP->ForEachSourceWidget([&](UWidget* W)
 {
  if(W&&!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))
   BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));
 });
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
 FKismetEditorUtilities::CompileBlueprint(BP);
 return BP->Status!=BS_Error&&BP->GeneratedClass;
}
bool SaveInformationBP(UWidgetBlueprint* BP)
{
 if(auto* Class=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
 BP->MarkPackageDirty();return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
}
}
#endif

bool FEGWidgetAuthoringService::ApplyCardInformationViews()
{
#if WITH_EDITOR
 if(!GEditor||GEditor->PlayWorld)return false;
 const FString Folder=TEXT("/Game/Blueprints/UI/Widgets/Board");
 auto Make=[&](const TCHAR* Name,EEGHeroInfoPresentation Mode)->UWidgetBlueprint*
 {
  auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("/")+Name));
  if(!BP)
  {
   auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=UEGHeroInfoWidget::StaticClass();
   BP=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(Name,Folder,UWidgetBlueprint::StaticClass(),Factory));
   if(!BP)return nullptr;
   UEGHeroInfoWidget::BuildPresentation(BP->WidgetTree,Mode);
  }
  if(BP->ParentClass!=UEGHeroInfoWidget::StaticClass()||!CompileInformationBP(BP))return nullptr;
  auto* CDO=Cast<UEGHeroInfoWidget>(BP->GeneratedClass->GetDefaultObject());if(!CDO)return nullptr;
  CDO->Presentation=Mode;CDO->bDragLayout=false;
  return SaveInformationBP(BP)?BP:nullptr;
 };
 auto* Card=Make(TEXT("WBP_HeroCardPreview"),EEGHeroInfoPresentation::CardPreview);
 auto* Hover=Make(TEXT("WBP_UnitHoverInfo"),EEGHeroInfoPresentation::UnitHover);
 auto* Pinned=Make(TEXT("WBP_UnitPinnedInfo"),EEGHeroInfoPresentation::UnitDetails);
 if(!Card||!Hover||!Pinned)return false;
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(Folder/TEXT("WBP_BoardHUD")));
 if(!CompileInformationBP(HUD))return false;
 auto* CDO=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject());if(!CDO)return false;
 CDO->CardPreviewInformationClass=Card->GeneratedClass;
 CDO->UnitHoverInformationClass=Hover->GeneratedClass;
 CDO->UnitPinnedInformationClass=Pinned->GeneratedClass;
 return SaveInformationBP(HUD);
#else
 return false;
#endif
}

bool FEGWidgetAuthoringService::RebuildPinnedUnitInformation()
{
#if WITH_EDITOR
 if(!GEditor||GEditor->PlayWorld)return false;
 auto* BP=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_UnitPinnedInfo.WBP_UnitPinnedInfo"));
 if(!BP||BP->ParentClass!=UEGHeroInfoWidget::StaticClass()||!BP->WidgetTree)return false;
 // Do not discard graph, property binding or animation references to widgets
 // removed by this new layout. Preflight before touching the saved tree.
 TStrongObjectPtr<UWidgetTree> Probe(NewObject<UWidgetTree>());
 UEGHeroInfoWidget::BuildPresentation(Probe.Get(),EEGHeroInfoPresentation::UnitDetails);
 TSet<FName> NewNames;
 Probe->ForEachWidget([&](UWidget* W){if(W)NewNames.Add(W->GetFName());});
 for(const auto& Animation:BP->Animations)if(Animation)NewNames.Add(Animation->GetFName());
 const auto WouldLoseWidget=[&](FName Name)
 {return !Name.IsNone()&&BP->WidgetVariableNameToGuidMap.Contains(Name)&&!NewNames.Contains(Name);};
 TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
 for(const auto* Graph:Graphs)if(Graph)for(const TObjectPtr<UEdGraphNode>& NodeRef:Graph->Nodes)
 {
  const UEdGraphNode* Node=NodeRef.Get();
  if(const auto* Variable=Cast<UK2Node_Variable>(Node);Variable&&WouldLoseWidget(Variable->GetVarName()))
  {UE_LOG(LogTemp,Error,TEXT("Pinned info graph references removed widget %s"),*Variable->GetVarName().ToString());return false;}
  if(const auto* Event=Cast<UK2Node_ComponentBoundEvent>(Node);Event&&WouldLoseWidget(Event->GetComponentPropertyName()))
  {UE_LOG(LogTemp,Error,TEXT("Pinned info event references removed widget %s"),*Event->GetComponentPropertyName().ToString());return false;}
 }
 for(const auto& Binding:BP->Bindings)if(WouldLoseWidget(FName(*Binding.ObjectName)))
 {UE_LOG(LogTemp,Error,TEXT("Pinned info binding references removed widget %s"),*Binding.ObjectName);return false;}
 for(const auto& Animation:BP->Animations)if(Animation)for(const auto& Binding:Animation->GetBindings())
  if(!Binding.bIsRootWidget&&(WouldLoseWidget(Binding.WidgetName)||WouldLoseWidget(Binding.SlotWidgetName)))
  {UE_LOG(LogTemp,Error,TEXT("Pinned info animation references removed widget %s"),*Binding.WidgetName.ToString());return false;}
 BP->Modify();
 BP->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
 BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
 UEGHeroInfoWidget::BuildPresentation(BP->WidgetTree,EEGHeroInfoPresentation::UnitDetails);
 // The compiler requires a one-to-one map. Preserve stable GUIDs for surviving
 // names and animation GUIDs, drop only preflight-verified obsolete names.
 TSet<FName> LiveNames;
 BP->ForEachSourceWidget([&](UWidget* W){if(W)LiveNames.Add(W->GetFName());});
 for(const auto& Animation:BP->Animations)if(Animation)LiveNames.Add(Animation->GetFName());
 for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!LiveNames.Contains(It.Key()))It.RemoveCurrent();
 if(!CompileInformationBP(BP))return false;
 auto* CDO=Cast<UEGHeroInfoWidget>(BP->GeneratedClass->GetDefaultObject());if(!CDO)return false;
 CDO->Presentation=EEGHeroInfoPresentation::UnitDetails;CDO->bDragLayout=false;
 return SaveInformationBP(BP);
#else
 return false;
#endif
}
