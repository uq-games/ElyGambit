#pragma once
#include "UI/EGWidgetAuthoringService.h"

class FEGWidgetAuthoringService final : public IEGWidgetAuthoringService
{
public:
 bool RenamePlannedAssets(const TArray<UObject*>& Assets,const TArray<FString>& Destinations) override;
 bool MigrateWidgetNames(UObject* Object,const TMap<FName,FString>& Names) override;
 bool BuildBoardUIAssets() override;
 bool BuildWidgetAssets(bool bRebuild,bool bSaveAssets) override;
 bool ApplyBattleUIAssets(bool bSaveAssets) override;
 bool ApplyUnitVitalsAssets() override;
 bool ApplyShopMotionAssets() override;
 bool ApplyBoardCompositionAssets() override;
 bool BuildRunPanelAsset(bool bSaveAssets) override;
 bool ApplyCardInformationViews() override;
 bool RebuildPinnedUnitInformation() override;
 bool ApplyGeneratedBoardControls() override;
 bool RenderGeneratedControlSheet(const FString& Filename,FVector2D Size,bool bShopOpen) override;
 bool RemoveCompanionCatalogue() override;
 bool ApplyOfficialCardAssets(const TMap<FName,UTexture2D*>& Artwork,const TArray<UEGHeroDefinition*>& Companions) override;
 bool ApplyReferenceShopUI() override;
 bool ApplyShopDeploymentAssets() override;
 bool ApplyShopPriceBadge() override;
 bool ApplyUIExperienceAssets() override;
};
