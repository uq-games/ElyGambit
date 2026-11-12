#pragma once
#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
class UTexture2D;
class UEGHeroDefinition;

/** Optional editor-only implementation of the stable runtime reflection bridge. */
class ELYGAMBIT_API IEGWidgetAuthoringService : public IModularFeature
{
public:
 static FName FeatureName() { return TEXT("EGWidgetAuthoringService"); }
 virtual ~IEGWidgetAuthoringService() = default;
 virtual bool RenamePlannedAssets(const TArray<UObject*>& Assets,const TArray<FString>& Destinations) = 0;
 virtual bool MigrateWidgetNames(UObject* Object,const TMap<FName,FString>& Names) = 0;
 virtual bool BuildBoardUIAssets() = 0;
 virtual bool BuildWidgetAssets(bool bRebuild,bool bSaveAssets) = 0;
 virtual bool ApplyBattleUIAssets(bool bSaveAssets) = 0;
 virtual bool ApplyUnitVitalsAssets() = 0;
 virtual bool ApplyShopMotionAssets() = 0;
 virtual bool ApplyBoardCompositionAssets() = 0;
 virtual bool BuildRunPanelAsset(bool bSaveAssets) = 0;
 virtual bool ApplyCardInformationViews() = 0;
 virtual bool RebuildPinnedUnitInformation() = 0;
 virtual bool ApplyGeneratedBoardControls() = 0;
 virtual bool RenderGeneratedControlSheet(const FString& Filename,FVector2D Size,bool bShopOpen) = 0;
 virtual bool RemoveCompanionCatalogue() = 0;
 virtual bool ApplyOfficialCardAssets(const TMap<FName,UTexture2D*>& Artwork,const TArray<UEGHeroDefinition*>& Companions) = 0;
 virtual bool ApplyReferenceShopUI() = 0;
 virtual bool ApplyShopDeploymentAssets() = 0;
 virtual bool ApplyShopPriceBadge() = 0;
 virtual bool ApplyUIExperienceAssets() = 0;
};
