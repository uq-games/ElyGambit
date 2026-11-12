#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGShopDeploymentTransactions,"ElyGambit.ShopDrag.AtomicPurchasePlacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGShopDeploymentTransactions::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);if(!TestNotNull(TEXT("Run fixture"),Base))return false;
 auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);Run->bUseExplicitStartingCards=false;Run->bUseFixedPopulation=false;Run->StartingCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));Run->StartingGold=0;Run->StartingCards=1;Run->CardPool={Run->StartingCard};GM->RunDefinition=Run;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 const int32 Revision=Cards->GetShopSlotRevision(0),InitialHand=Cards->GetHand().Num();
 TestFalse(TEXT("Insufficient funds"),Cards->TryBuyShopAndDeploy(0,Revision,{2,1}));TestEqual(TEXT("No offer change on failure"),Cards->GetShopSlotRevision(0),Revision);
 Cards->AwardGold(100);const int64 Gold=Cards->GetGold();
 TestFalse(TEXT("Neutral row rejected"),Cards->TryBuyShopAndDeploy(0,Revision,{2,3}));
 TestEqual(TEXT("Invalid cell does not charge"),Cards->GetGold(),Gold);
 auto BlockHandle=W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([](AActor* A){if(A->IsA<AEGDeployedPiece>())A->Destroy();}));
 TestFalse(TEXT("Rejected spawn rolls back purchase"),Cards->TryBuyShopAndDeploy(0,Revision,{2,1}));W->RemoveOnActorSpawnedHandler(BlockHandle);
 TestEqual(TEXT("Failed spawn gold"),Cards->GetGold(),Gold);TestEqual(TEXT("Failed spawn hand"),Cards->GetHand().Num(),InitialHand);
 TestEqual(TEXT("Failed spawn roster"),GM->GetRoster()->GetHeroes().Num(),0);TestTrue(TEXT("Failed spawn releases cell"),Grid->IsCellAvailable({2,1}));
 TestEqual(TEXT("Failed spawn preserves offer"),Cards->GetShopSlotRevision(0),Revision);
 TestTrue(TEXT("Buy and deploy"),Cards->TryBuyShopAndDeploy(0,Revision,{2,1}));
 TestEqual(TEXT("One cost"),Cards->GetGold(),Gold-Cards->GetRules().PurchasePrice);TestEqual(TEXT("No intermediate hand"),Cards->GetHand().Num(),InitialHand);
 TestEqual(TEXT("One piece"),Cards->GetPieces().Num(),1);TestFalse(TEXT("Duplicate release"),Cards->TryBuyShopAndDeploy(0,Revision,{2,1}));
 auto* Piece=Cards->GetPieces()[0];FEGHeroInstance Before,After;GM->GetRoster()->GetHero(Piece->RosterId,Before);
 const int32 MergeRevision=Cards->GetShopSlotRevision(0);const auto Merge=Cards->EvaluateShopPlacement(0,MergeRevision,{4,0});
 TestEqual(TEXT("Same-name hover merges"),Merge.Action,EEGPlacementAction::Merge);TestEqual(TEXT("Merge destination agrees with existing position"),Merge.TargetCell,FIntPoint(2,1));
 TestTrue(TEXT("Purchase merge"),Cards->TryBuyShopAndDeploy(0,MergeRevision,{4,0}));GM->GetRoster()->GetHero(Piece->RosterId,After);
 TestEqual(TEXT("Same-name count increments once"),After.SameNameCards,Before.SameNameCards+1);TestEqual(TEXT("Merge retains position"),Piece->Cell,FIntPoint(2,1));
 TestEqual(TEXT("Merge has one actor"),Cards->GetPieces().Num(),1);
 auto* OtherCard=DuplicateObject<UEGCardDefinition>(Run->StartingCard,Cards);
 OtherCard->HeroDefinition=DuplicateObject<UEGHeroDefinition>(Run->StartingCard->HeroDefinition,Cards);OtherCard->HeroDefinition->HeroId=TEXT("ShopPopulationFixture");
 auto PopulationRules=Cards->GetRules();PopulationRules.DeploymentLimit=1;Cards->ConfigureRules(PopulationRules,GM->GetRoster());Cards->CardPool={OtherCard};Cards->RefreshShop();
 const int64 PopulationGold=Cards->GetGold();const int32 PopulationRevision=Cards->GetShopSlotRevision(0);
 TestFalse(TEXT("Population full rejects a new hero"),Cards->TryBuyShopAndDeploy(0,PopulationRevision,{4,0}));TestEqual(TEXT("Population failure does not charge"),Cards->GetGold(),PopulationGold);
 Cards->CardPool={Run->StartingCard};Cards->RefreshShop();
 auto Rules=Cards->GetRules();Rules.SameNameLimit=After.SameNameCards;Cards->ConfigureRules(Rules,GM->GetRoster());
 const int32 MaxRevision=Cards->GetShopSlotRevision(0);const int64 MaxGold=Cards->GetGold();
 TestFalse(TEXT("Same-name limit"),Cards->TryBuyShopAndDeploy(0,MaxRevision,{3,0}));TestEqual(TEXT("Limit does not charge"),Cards->GetGold(),MaxGold);
 TestTrue(TEXT("Refresh"),Cards->RefreshShop());TestFalse(TEXT("Stale offer"),Cards->TryBuyShopAndDeploy(0,MaxRevision,{3,0}));
 Cards->SetPreparationEnabled(false);const int32 CombatRevision=Cards->GetShopSlotRevision(0);
 TestFalse(TEXT("No direct deployment outside preparation"),Cards->TryBuyShopAndDeploy(0,CombatRevision,{3,0}));
 TestTrue(TEXT("Ordinary hand purchase still allowed"),Cards->BuyShopSlotExpected(0,CombatRevision));
 return true;
}
#endif
