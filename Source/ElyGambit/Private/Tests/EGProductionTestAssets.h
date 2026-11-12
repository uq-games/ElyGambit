#pragma once

#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMainMenuGameMode.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "UObject/StrongObjectPtr.h"

namespace EGProductionTestAssets
{
// Resolve the configured entry through any actual main-menu transition to the
// board WorldSettings -> GameMode CDO -> Run.
// Keep callers' fixture worlds separate from the loaded production map.
inline UEGRunDefinition* ResolveRun(FAutomationTestBase& Test,TSoftObjectPtr<UEGCardDefinition>* OutHumanFusionCard=nullptr)
{
    if(OutHumanFusionCard)OutHumanFusionCard->Reset();
    FString MapPath;
    if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),MapPath,GEngineIni))
    {Test.AddError(TEXT("GameDefaultMap is not configured"));return nullptr;}
    TSet<FString> SeenPackages;
    FString Binding;
    constexpr int32 MaxMaps=4;
    for(int32 Hop=0;Hop<MaxMaps;++Hop)
    {
        FString PackageName=FPackageName::ObjectPathToPackageName(MapPath);
        if(FPackageName::IsShortPackageName(PackageName))
        {
            FString Resolved;
            if(!FPackageName::SearchForPackageOnDisk(PackageName,&Resolved))
            {Test.AddError(FString::Printf(TEXT("Cannot resolve configured map %s"),*MapPath));return nullptr;}
            PackageName=Resolved;
        }
        if(!FPackageName::IsValidLongPackageName(PackageName))
        {Test.AddError(FString::Printf(TEXT("Invalid configured map path %s"),*MapPath));return nullptr;}
        if(SeenPackages.Contains(PackageName))
        {Test.AddError(FString::Printf(TEXT("Production map transition loops at %s"),*PackageName));return nullptr;}
        SeenPackages.Add(PackageName);
        // The editor initializes newly loaded inactive worlds. Destroy only an
        // inspection world we created and only if no editor context owns it.
        UPackage* ExistingPackage=FindPackage(nullptr,*PackageName);
        const bool bWorldAlreadyLoaded=ExistingPackage&&UWorld::FindWorldInPackage(ExistingPackage);
        const FString ObjectPath=PackageName+TEXT(".")+FPackageName::GetLongPackageAssetName(PackageName);
        TStrongObjectPtr<UWorld> Map(LoadObject<UWorld>(nullptr,*ObjectPath));
        if(!Test.TestNotNull(TEXT("Configured production map"),Map.Get()))return nullptr;
        ON_SCOPE_EXIT
        {
            if(!bWorldAlreadyLoaded && Map->WorldType==EWorldType::Inactive && Map->IsInitialized() &&
               GEngine && !GEngine->GetWorldContextFromWorld(Map.Get()))
                Map->DestroyWorld(false);
        };
        const auto* Settings=Map->GetWorldSettings();
        if(!Test.TestNotNull(TEXT("Production WorldSettings"),Settings))return nullptr;
        const auto* Mode=Settings->DefaultGameMode.GetDefaultObject();
        if(!Test.TestNotNull(TEXT("Production map GameMode"),Mode))return nullptr;
        Binding+=FString::Printf(TEXT("%s%s -> %s"),Binding.IsEmpty()?TEXT(""):TEXT(" -> "),*PackageName,*Mode->GetClass()->GetPathName());
        if(const auto* BoardMode=Cast<AEGDemoGameMode>(Mode))
        {
            if(OutHumanFusionCard)*OutHumanFusionCard=BoardMode->HumanFusionCard;
            auto* Run=BoardMode->RunDefinition.Get();
            if(!Test.TestNotNull(TEXT("Production GameMode RunDefinition"),Run))return nullptr;
            Test.AddInfo(FString::Printf(TEXT("Production binding: %s -> %s"),*Binding,*Run->GetPathName()));
            return Run;
        }
        if(!Cast<AEGMainMenuGameMode>(Mode))
        {Test.AddError(FString::Printf(TEXT("Unsupported production GameMode at %s: %s"),*PackageName,*Mode->GetClass()->GetPathName()));return nullptr;}
        const auto* MenuController=Cast<AEGMainMenuController>(Mode->PlayerControllerClass.GetDefaultObject());
        if(!Test.TestNotNull(TEXT("Production menu PlayerController derives from EGMainMenuController"),MenuController))return nullptr;
        if(MenuController->bPreviewOnly||MenuController->GameMap.IsNone())
        {Test.AddError(FString::Printf(TEXT("Production menu %s has no playable game transition"),*PackageName));return nullptr;}
        Binding+=FString::Printf(TEXT(" -> %s"),*MenuController->GetClass()->GetPathName());
        MapPath=MenuController->GameMap.ToString();
    }
    Test.AddError(FString::Printf(TEXT("Production map transition exceeded %d maps"),MaxMaps));
    return nullptr;
}
// Keep behavior tests deterministic even when the production encounter changes.
inline bool ConfigureBehaviorEnemies(UEGRunDefinition* Run, FAutomationTestBase& Test)
{
    if(!Run || Run->Stages.IsEmpty()) {Test.AddError(TEXT("Run fixture has no stage"));return false;}
    // These fixtures exercise isolated hero behavior, not the production test lineup.
    // Tests of explicit starting grants/fixed population opt back in after this call.
    Run->bUseExplicitStartingCards=false;
    Run->bUseFixedPopulation=false;
    auto* Melee=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Enemies/Melee/Data/DA_Monster_Melee"));
    auto* Ranged=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Enemies/Ranged/Data/DA_Monster_Ranged"));
    if(!Test.TestNotNull(TEXT("Melee enemy fixture"),Melee)||!Test.TestNotNull(TEXT("Ranged enemy fixture"),Ranged))return false;
    FEGEnemySpawn First;First.Definition=Melee;First.Level=1;First.Cell={1,4};
    FEGEnemySpawn Second;Second.Definition=Ranged;Second.Level=1;Second.Cell={3,4};
    for(auto& Stage:Run->Stages){Stage.Enemies={First,Second};Stage.Traps.Reset();}
    return true;
}
}
