#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGWidgetEditorLibrary.generated.h"

class UWidget;
class APlayerController;
class AEGChessUnit;
class UUserWidget;
class UEGHeroDefinition;
class AEGArtPiece;
class UTexture2D;
class AEGCardManager;

/** Editor bridge for generating editable Widget Blueprint trees; inert in packaged builds. */
UCLASS()
class ELYGAMBIT_API UEGWidgetEditorLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyShopPriceBadge();
    /** Targeted currency brushes, labels and red sale art; preserves authored trees. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyUIExperienceAssets();
    /** Targeted GPT Image control art and centered live typography. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyGeneratedBoardControls();
    /** Re-author only the shop/sell widgets with the ShopMotion20261024 plates. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyShopMotionAssets();
    /** Creates the distinct card-preview, unit-hover and pinned-unit layouts. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyCardInformationViews();
    /** Rebuild and save only WBP_UnitPinnedInfo; caller must back up the asset first. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool RebuildPinnedUnitInformation();
    /** Render only the saved control widgets for typography review; never starts gameplay. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool RenderGeneratedControlSheet(const FString& Filename,FVector2D Size,bool bShopOpen=false);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyShopDeploymentAssets();
    /** Upgrade just the vitals tree, preserving other HUD and shop authoring. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyUnitVitalsAssets();
    /** Targeted Wanxiang-reference header/shop styling; preserves all card/data bindings. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyReferenceShopUI();
    /** Remove the cancelled companion catalogue from the saved HUD. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool RemoveCompanionCatalogue();
    /** Patch card artwork/layout and add the read-only companion catalogue, preserving other UI. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyOfficialCardAssets(const TMap<FName,UTexture2D*>& Artwork,const TArray<UEGHeroDefinition*>& Companions);
    /** Explicit migration helpers; preserve widget bindings and repair asset referencers. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor|Migration") static bool MigrateWidgetNames(UObject* Blueprint,const TMap<FName,FString>& Names);
    UFUNCTION(BlueprintCallable,Category="UI|Editor|Migration") static bool RenamePlannedAssets(const TArray<UObject*>& Assets,const TArray<FString>& Destinations);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool BuildBoardUIAssets();
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool BuildWidgetAssets(bool bRebuild=false,bool bSaveAssets=true);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool BuildRunPanelAsset(bool bSaveAssets=true);
    /** Targeted update of named controls; preserves existing Blueprint trees and graphs. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyBattleUIAssets(bool bSaveAssets=true);
    /** Patch only board sizing, preserving graphs, names and existing visual styles. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ApplyBoardCompositionAssets();
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool CapturePIE(const FString& Filename);
    /** Routes editor test input through Slate, exercising the real widget callbacks. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool TestPointer(FVector2D AbsolutePosition,FName Action);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool TestKey(FName KeyName);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool StartPIEWindow();
    /** Transient visual-only fixture in the owned PIE world; never enters gameplay registries. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static AEGArtPiece* SpawnPresentationProbe(UEGHeroDefinition* Definition,FVector Position,FRotator Rotation);
    UFUNCTION(BlueprintPure,Category="UI|Editor") static FVector2D WidgetScreenPosition(UWidget* Widget,FVector2D NormalizedPosition);
    UFUNCTION(BlueprintPure,Category="UI|Editor") static FVector2D WidgetLocalSize(UWidget* Widget);
    UFUNCTION(BlueprintPure,Category="UI|Editor") static FVector2D WorldScreenPosition(APlayerController* PC,FVector Position);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool TestApplicationActivation(bool bActive);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool SetCombatProbeAttributes(AEGChessUnit* Unit,float MaxHealth,float Health,float Mana);
    /** PIE-only test population; never changes a run asset or production population rules. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool SetProbePopulationFloor(AEGCardManager* Cards,int32 Count);
    /** Fix the actual PIE render target size independently from desktop window decoration. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static FVector2D SetProbeRenderSize(int32 Width,int32 Height);
    UFUNCTION(BlueprintPure,Category="UI|Editor") static FVector2D GetProbeRenderSize();
    /** Capture the render target pixels, independently from the Slate widget's displayed size. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool CaptureProbeRender(const FString& Filename);
    /** Editor-only concurrency fixture, registered through the ordinary battle lifetime. */
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static AEGChessUnit* SpawnCombatProbeDuplicate(AEGChessUnit* Source,FVector Point);
    UFUNCTION(BlueprintPure,Category="UI|Editor") static UWidget* FindNamedWidget(UUserWidget* View,FName Name);
    UFUNCTION(BlueprintCallable,Category="UI|Editor") static bool ProbeCombatFeedback(AEGChessUnit* Source,AEGChessUnit* Target,float Damage,float Healing,float Shield);
};
