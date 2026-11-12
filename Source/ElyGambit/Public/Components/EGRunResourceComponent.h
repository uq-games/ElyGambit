#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGRunResourceComponent.generated.h"

class UEGHeroDefinition;
class UDataAsset;
class AEGPlacementPreview;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EEGRunStartupPhase : uint8 { LoadingResources, PreparingPreviews, WarmingRendering, Ready, Failed };

/** World-owned residency. Formal opening waits for the configured run graph; sandbox entry remains immediate. */
UCLASS()
class ELYGAMBIT_API UEGRunResourceComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGRunResourceComponent();
 void SetRequiredHeroes(const TArray<UEGHeroDefinition*>& Heroes);
 /** Prepare cosmetic models with the scene; skill/effect graphs are requested separately in the background. */
 void SetRunHeroes(const TArray<UEGHeroDefinition*>& Heroes,UDataAsset* Run=nullptr);
 /** Formal opening: build at most one scene preview per poll while resources load asynchronously. */
 void SetRunHeroesDeferred(const TArray<UEGHeroDefinition*>& Heroes,UDataAsset* Run=nullptr);
 /** Configured meshes, idle assets, animation classes and unit classes of the opening lineup. */
 void RequireOpeningHero(const UEGHeroDefinition* Hero);
 void FinishOpeningPreload();
 /** Compatibility entry point for scene presentation setup. */
 void SetModelPreloadHeroes(const TArray<UEGHeroDefinition*>& Heroes);
 UFUNCTION(BlueprintPure,Category="Diagnostics") TArray<FString> GetPreloadedModelHeroPaths() const;
 void Poll();
 bool IsReady() const { return bReady; }
 bool HasCompletedStartupPreload() const {return bStartupComplete;}
 bool IsHeroReady(const UEGHeroDefinition* Hero) const;
 const FString& GetError() const { return Error; }
 UFUNCTION(BlueprintPure,Category="Loading") EEGRunStartupPhase GetStartupPhase() const { return StartupPhase; }
 UFUNCTION(BlueprintPure,Category="Loading") int32 GetCompletedWork() const;
 UFUNCTION(BlueprintPure,Category="Loading") int32 GetTotalWork() const;
 UFUNCTION(BlueprintPure,Category="Loading") FString GetStartupDetail() const;
 UFUNCTION(BlueprintPure,Category="Diagnostics") AEGPlacementPreview* GetPooledPreview(const UEGHeroDefinition* Hero) const;
 AEGPlacementPreview* AcquirePreview(UEGHeroDefinition* Hero);
 void RequestHeroResources(UEGHeroDefinition* Hero);
 void RequestRuntimeAsset(const FSoftObjectPath& Path);
 UFUNCTION(BlueprintPure,Category="Diagnostics") bool IsBackgroundReady() const { return !bBackgroundPending; }
 UFUNCTION(BlueprintPure,Category="Diagnostics") int32 GetPreviewCount() const { return Previews.Num(); }
 uint64 GetBackgroundGraphTraversalCount() const {return BackgroundGraphTraversalCount;}
 void ResetResources();
 UFUNCTION(BlueprintPure,Category="Diagnostics") TArray<FString> GetHeldResourcePaths() const;
 UFUNCTION(BlueprintPure,Category="Diagnostics") TArray<FString> GetRequiredHeroPaths() const;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 /** No loads: discovers soft edges of loaded data assets, including nested arrays/maps/structs. */
 static void GatherLoadedGraph(const UObject* Root,TSet<FSoftObjectPath>& Paths,
     TSet<const UEGHeroDefinition*>* Definitions=nullptr,TSet<FSoftObjectPath>* Optional=nullptr);
private:
 UPROPERTY(Transient) TArray<TObjectPtr<UEGHeroDefinition>> RequiredHeroes;
 UPROPERTY(Transient) TArray<TObjectPtr<UEGHeroDefinition>> ModelPreloadHeroes;
 UPROPERTY(Transient) TArray<TObjectPtr<UEGHeroDefinition>> RunDefinitions;
 UPROPERTY(Transient) TObjectPtr<UDataAsset> RunRoot;
 UPROPERTY(Transient) TMap<TObjectPtr<UEGHeroDefinition>,TObjectPtr<AEGPlacementPreview>> Previews;
 UPROPERTY(Transient) EEGRunStartupPhase StartupPhase=EEGRunStartupPhase::LoadingResources;
 void InitializePresentations();
 void PollDeferredPresentations();
 void PollBackgroundResources();
 void FailStartup(const FString& Message);
 bool bRunConfigured=false;
 bool bDeferredPresentations=false;
 bool bFormalOpeningPreload=false;
 int32 DeferredPresentationIndex=0;
 int32 CompletedResourceCount=0;
 TSet<FSoftObjectPath> OpeningRequiredPaths;
 bool bBackgroundPending=false;
 TMap<FSoftObjectPath,TSharedPtr<FStreamableHandle>> Handles;
 // Each root stays here only until all successive soft-reference layers have
 // been requested. RequiredHeroes owns the roots; this set cannot extend GC.
 TSet<TWeakObjectPtr<UDataAsset>> PendingBackgroundRoots;
 TSet<FSoftObjectPath> PendingRuntimeAssets;
 uint64 BackgroundGraphTraversalCount=0;
 // Paths only: this cache cannot keep an unused hero's meshes/materials alive.
 TMap<FSoftObjectPath,TSet<FSoftObjectPath>> GraphCache;
 TSet<FSoftObjectPath> Validated;
 bool bReady=false;
 bool bStartupComplete=false;
 bool bDirty=true;
 bool bPolling=false;
 FString Error;
};
