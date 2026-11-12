#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#include "Game/EGCardManager.h"
#include "Actor/EGArtPiece.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Components/EGBattleContextComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#if WITH_EDITOR
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "WidgetBlueprintOperationUtils.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/CoreDelegates.h"
#include "Internationalization/Text.h"
#include "Misc/App.h"
#include "UObject/CoreRedirects.h"
bool EGBuildPearlWidgetAssets(bool bRebuild,bool bSaveAssets);
bool EGBuildRunPanelAsset(bool bSaveAssets);
bool EGApplyBattleUIAssets(bool bSaveAssets);
bool EGApplyBoardCompositionAssets();
bool EGBuildBoardUIAssets();
#endif
bool FEGWidgetAuthoringService::RenamePlannedAssets(const TArray<UObject*>& Assets,const TArray<FString>& Destinations)
{
#if WITH_EDITOR
 // Only the source-reference reminder is acknowledged: the caller supplies an audited rename manifest.
 if(IsRunningCommandlet()||FApp::IsUnattended()||Assets.Num()!=Destinations.Num()||Assets.IsEmpty())return false;
 TArray<FAssetRenameData> Jobs;
 TArray<FCoreRedirect> PackageRedirects;
 for(int32 I=0;I<Assets.Num();++I)
 {
  if(!Assets[I]||!Assets[I]->GetPathName().StartsWith(TEXT("/Game/"))||!Destinations[I].StartsWith(TEXT("/Game/")))return false;
  FString Folder,Name;Destinations[I].Split(TEXT("/"),&Folder,&Name,ESearchCase::CaseSensitive,ESearchDir::FromEnd);
  if(Name.IsEmpty())return false;Jobs.Emplace(Assets[I],Folder,Name);
  PackageRedirects.Emplace(ECoreRedirectFlags::Type_Package,Assets[I]->GetOutermost()->GetName(),Destinations[I]);
 }
 auto Previous=FCoreDelegates::ModalMessageDialog;
 FCoreDelegates::ModalMessageDialog.BindLambda([](EAppMsgCategory Category,EAppMsgType::Type Type,const FText& Message,const FText& Title)
 {
  const auto Key=FTextInspector::GetKey(Message);
  if(Type==EAppMsgType::OkCancel&&Key&&*Key==TEXT("RenameCDOReferences"))return EAppReturnType::Ok;
  TArray<FHistoricTextFormatData> History;FTextInspector::GetHistoricFormatData(Message,History);
  for(const auto& Format:History)
  {
   const auto FormatKey=FTextInspector::GetKey(Format.SourceFmt.GetSourceText());
   if(Type==EAppMsgType::OkCancel&&FormatKey&&*FormatKey==TEXT("RenameCDOReferences"))return EAppReturnType::Ok;
  }
  UE_LOG(LogTemp,Error,TEXT("Unanticipated migration dialog: %s"),*Message.ToString());return EAppReturnType::Cancel;
 });
 TGuardValue<bool> ScriptGuard(GIsRunningUnattendedScript,false);
 const bool Result=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RenameAssets(Jobs);
 FCoreDelegates::ModalMessageDialog=Previous;
 // AssetTools may remove a source redirector while an unloaded referencer still
 // serializes its old package. Keep explicit aliases alive across GC and loads
 // until cold resaving repairs those references; the driver persists the same list.
 if(Result)FCoreRedirects::AddRedirectList(PackageRedirects,TEXT("ElyGambitAuditedAssetMigration"));
 return Result;
#else
 return false;
#endif
}
bool FEGWidgetAuthoringService::MigrateWidgetNames(UObject* Object,const TMap<FName,FString>& Names)
{
#if WITH_EDITOR
 auto* BP=Cast<UWidgetBlueprint>(Object);if(!BP||!BP->WidgetTree)return false;
 for(const auto& Pair:Names)
  if(BP->WidgetTree->FindWidget(Pair.Key)&&BP->WidgetTree->FindWidget(FName(*Pair.Value)))return false;
 for(const auto& Pair:Names)
  if(auto* Widget=BP->WidgetTree->FindWidget(Pair.Key))
   if(!FWidgetBlueprintOperationUtils::RenameWidget(BP,Widget,Pair.Value))return false;
 return true;
#else
 return false;
#endif
}
bool FEGWidgetAuthoringService::BuildBoardUIAssets()
{
#if WITH_EDITOR
 return EGBuildBoardUIAssets();
#else
 return false;
#endif
}
bool FEGWidgetAuthoringService::BuildWidgetAssets(bool bRebuild,bool bSaveAssets)
{
#if WITH_EDITOR
    return EGBuildPearlWidgetAssets(bRebuild,bSaveAssets);
#else
    return false;
#endif
}
bool FEGWidgetAuthoringService::ApplyBattleUIAssets(bool bSaveAssets)
{
#if WITH_EDITOR
 return EGApplyBattleUIAssets(bSaveAssets);
#else
 return false;
#endif
}
bool EGApplyUnitVitalsAssets();
bool FEGWidgetAuthoringService::ApplyUnitVitalsAssets()
{
#if WITH_EDITOR
 return EGApplyUnitVitalsAssets();
#else
 return false;
#endif
}
bool EGApplyShopMotionAssets();
bool FEGWidgetAuthoringService::ApplyShopMotionAssets()
{
#if WITH_EDITOR
 return EGApplyShopMotionAssets();
#else
 return false;
#endif
}
bool FEGWidgetAuthoringService::ApplyBoardCompositionAssets()
{
#if WITH_EDITOR
 return EGApplyBoardCompositionAssets();
#else
 return false;
#endif
}

bool FEGWidgetAuthoringService::BuildRunPanelAsset(bool bSaveAssets)
{
#if WITH_EDITOR
 return EGBuildRunPanelAsset(bSaveAssets);
#else
 return false;
#endif
}

