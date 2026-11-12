#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGEnvironmentTrapDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Actor/EGBoard.h"
#include "Actor/EGCorruptionMine.h"
#include "Character/EGChessUnit.h"
#include "Materials/Material.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"

namespace
{
struct FRunOptimizationWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* W=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 UEGRunDefinition* Run=nullptr;
 AEGDeploymentBoard* Grid=nullptr;
 AEGBoard* Board=nullptr;
 AEGCardManager* Cards=nullptr;
 explicit FRunOptimizationWorld(FAutomationTestBase& Test,bool bRoundShop=true):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();
  if(!W)return;
  W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
  Mode=W->GetAuthGameMode<AEGDemoGameMode>();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Mode||!Base||Base->Stages.IsEmpty()||Base->Stages[0].Enemies.IsEmpty())return;
  Run=DuplicateObject<UEGRunDefinition>(Base,Mode);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return;Run->StartingCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));Run->bRoundShopRules=bRoundShop;
  Run->StartingGold=10;Run->StartingCards=1;Run->RoundIncome=6;Run->CardPool={Run->StartingCard};
  Run->CardRules.bAllowSameNameMerge=true;Run->CardRules.SameNameLimit=0;
  auto Stage=Run->Stages[0];Stage.Enemies.SetNum(2);Stage.Enemies[1]=Stage.Enemies[0];
  Stage.Enemies[0].Cell={0,4};Stage.Enemies[1].Cell={1,4};Stage.Traps.Reset();
  Stage.StageId=TEXT("OptimizationStage1");Run->Stages={Stage,Stage};Run->Stages[1].StageId=TEXT("OptimizationStage2");
  Mode->RunDefinition=Run;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Board=W->SpawnActor<AEGBoard>();Scene->CombatBoard=Board;
 }
 bool Ready() const {return W&&Mode&&Run&&Grid&&Board&&Cards;}
 void Begin(){FURL URL;W->InitializeActorsForPlay(URL);W->BeginPlay();}
 ~FRunOptimizationWorld()
 {
  if(W){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStageSpawnRollback,"ElyGambit.RunOptimization.StageSpawnRollbackAndRetry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStageSpawnRollback::RunTest(const FString&)
{
 // Fail the second enemy after the first has already occupied its combat cell.
 {
  FRunOptimizationWorld F(*this);if(!TestTrue(TEXT("Enemy fixture available"),F.Ready()))return false;
  int32 Spawned=0;
  auto Handle=F.W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* A)
  {if(A->IsA<AEGChessUnit>()&&++Spawned==2)A->Destroy();}));
  F.Begin();F.W->RemoveOnActorSpawnedHandler(Handle);
  TestFalse(TEXT("Stage reports failure"),F.Mode->AreStageResourcesReady());
  TestEqual(TEXT("Initial hand still initialized"),F.Cards->GetHand().Num(),1);
  TestEqual(TEXT("Enemy batch rolled back"),F.Mode->GetCombatUnits().Num(),0);
  TestNull(TEXT("First enemy cell released"),F.Board->GetOccupant({0,4}));
  TestNull(TEXT("Second enemy cell released"),F.Board->GetOccupant({1,4}));
  TestFalse(TEXT("Retry without deployed hero does not enter combat"),F.Mode->EnterCombat());
  TestTrue(TEXT("Normal start request retries preparation"),F.Mode->AreStageResourcesReady());
  TestEqual(TEXT("Retry creates complete enemy batch"),F.Mode->GetCombatUnits().Num(),2);
  if(!TestTrue(TEXT("Hand remains available after retry"),!F.Cards->GetHand().IsEmpty()))return false;
  TestTrue(TEXT("Deploy after retry"),F.Cards->TryDeployHandCard(F.Cards->GetHand()[0].InstanceId,{2,1}));
  TestTrue(TEXT("Combat starts after recovery"),F.Mode->EnterCombat());
 }
 // Fail the second trap after both enemies and the first trap were created.
 {
  FRunOptimizationWorld F(*this);if(!TestTrue(TEXT("Trap fixture available"),F.Ready()))return false;
  auto* Def=NewObject<UEGEnvironmentTrapDefinition>(F.Run);Def->Material=UMaterial::GetDefaultMaterial(MD_Surface);
  FEGTrapSpawn A;A.Definition=Def;A.Cell={2,4};FEGTrapSpawn B=A;B.Cell={3,4};
  F.Run->Stages[0].Traps={A,B};
  int32 Spawned=0;
  auto Handle=F.W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* Actor)
  {if(Actor->IsA<AEGCorruptionMine>()&&++Spawned==2)Actor->Destroy();}));
  F.Begin();F.W->RemoveOnActorSpawnedHandler(Handle);
  TestFalse(TEXT("Trap failure leaves stage unready"),F.Mode->AreStageResourcesReady());
  TestEqual(TEXT("Trap failure rolls back enemies"),F.Mode->GetCombatUnits().Num(),0);
  TestNull(TEXT("Trap failure releases combat cell"),F.Board->GetOccupant({0,4}));
  int32 Remaining=0;for(TActorIterator<AEGCorruptionMine> It(F.W);It;++It)if(!It->IsActorBeingDestroyed())++Remaining;
  TestEqual(TEXT("No live trap from failed batch"),Remaining,0);
  F.Mode->EnterCombat();TestTrue(TEXT("Retry recreates full stage"),F.Mode->AreStageResourcesReady());
  Remaining=0;for(TActorIterator<AEGCorruptionMine> It(F.W);It;++It)if(!It->IsActorBeingDestroyed())++Remaining;
  TestEqual(TEXT("Retry creates both traps"),Remaining,2);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGRoundShopValidation,"ElyGambit.RunOptimization.EconomyValidationAndIdempotence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGRoundShopValidation::RunTest(const FString&)
{
 FRunOptimizationWorld F(*this);if(!TestTrue(TEXT("Fixture available"),F.Ready()))return false;
 FString Error;const auto Pool=F.Run->CardPool;
 F.Run->CardPool.Reset();TestFalse(TEXT("Round shop rejects empty pool"),F.Run->ValidateConfiguration(F.Grid,Error));
 F.Run->CardPool=Pool;F.Run->RoundIncome=-1;TestFalse(TEXT("Round shop rejects negative income"),F.Run->ValidateConfiguration(F.Grid,Error));
 F.Run->RoundIncome=0;TestTrue(TEXT("Zero round income is valid"),F.Run->ValidateConfiguration(F.Grid,Error));
 F.Run->RoundIncome=6;F.Begin();
 const int64 InitialGold=F.Cards->GetGold();F.Cards->BeginRoundShopRound(2);TestEqual(TEXT("Income applied once"),F.Cards->GetGold(),InitialGold+6);
 F.Cards->BeginRoundShopRound(2);TestEqual(TEXT("Duplicate round cannot pay again"),F.Cards->GetGold(),InitialGold+6);
 F.Cards->CardPool.Reset();const int64 BeforeRefresh=F.Cards->GetGold();
 TestFalse(TEXT("No paid refresh of empty pool"),F.Cards->RefreshShop());TestEqual(TEXT("Empty pool does not charge"),F.Cards->GetGold(),BeforeRefresh);
 F.Run->RoundIncome=-3;F.Cards->BeginRoundShopRound(3);TestEqual(TEXT("Runtime negative income cannot deduct"),F.Cards->GetGold(),BeforeRefresh);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGReserveIdentity,"ElyGambit.RunOptimization.SameHeroIdentityInReserve",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGReserveIdentity::RunTest(const FString&)
{
 FRunOptimizationWorld F(*this,false);if(!TestTrue(TEXT("Fixture available"),F.Ready()))return false;
 F.Begin();if(!TestTrue(TEXT("Initial card exists"),!F.Cards->GetHand().IsEmpty()))return false;
 if(!TestTrue(TEXT("Deploy original hero"),F.Cards->TryDeployHandCard(F.Cards->GetHand()[0].InstanceId,{2,1})))return false;
 auto* Piece=F.Cards->GetPieces()[0];const FGuid Id=Piece->RosterId;
 if(!TestTrue(TEXT("Move hero to reserve"),F.Cards->TryWithdraw(Piece)))return false;
 auto* Alternate=DuplicateObject<UEGCardDefinition>(F.Run->StartingCard,F.Cards);
 Alternate->HeroDefinition=DuplicateObject<UEGHeroDefinition>(F.Run->StartingCard->HeroDefinition,F.Cards);
 TestTrue(TEXT("Same HeroId uses a different definition"),Alternate->HeroDefinition!=F.Run->StartingCard->HeroDefinition&&Alternate->HeroDefinition->HeroId==F.Run->StartingCard->HeroDefinition->HeroId);
 TestTrue(TEXT("Give same identity card"),F.Cards->GrantCard(Alternate));
 const FGuid Card=F.Cards->GetHand().Last().InstanceId;FEGHeroInstance Before,After;
 F.Mode->GetRoster()->GetHero(Id,Before);
 TestTrue(TEXT("Reserve merge accepts same HeroId"),F.Cards->TryMergeIntoHero(Card,Id));
 F.Mode->GetRoster()->GetHero(Id,After);TestEqual(TEXT("Exactly one card added"),After.SameNameCards,Before.SameNameCards+1);
 TestFalse(TEXT("Consumed card cannot merge twice"),F.Cards->TryMergeIntoHero(Card,Id));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVDeploymentRollback,"ElyGambit.RunOptimization.VillVDeploymentRollback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVDeploymentRollback::RunTest(const FString&)
{
 FRunOptimizationWorld F(*this);if(!TestTrue(TEXT("Fixture available"),F.Ready()))return false;
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
 if(!TestNotNull(TEXT("VillV fixture card"),Card))return false;
 F.Begin();if(!TestTrue(TEXT("Add VillV card"),F.Cards->GrantCard(Card)))return false;
 const FGuid CardId=F.Cards->GetHand().Last().InstanceId;const int32 Hand=F.Cards->GetHand().Num();const int64 Gold=F.Cards->GetGold();
 int32 Pieces=0;auto Handle=F.W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor* A)
 {if(A->IsA<AEGDeployedPiece>()&&++Pieces==3)A->Destroy();}));
 TestFalse(TEXT("Second device spawn failure rejects deployment"),F.Cards->TryDeployHandCard(CardId,{3,1}));F.W->RemoveOnActorSpawnedHandler(Handle);
 TestEqual(TEXT("Failure preserves hand"),F.Cards->GetHand().Num(),Hand);TestEqual(TEXT("Failure preserves gold"),F.Cards->GetGold(),Gold);
 TestEqual(TEXT("Failure removes all pieces"),F.Cards->GetDeploymentParts().Num(),0);TestEqual(TEXT("Failure releases all cells"),F.Grid->GetOccupiedCount(),0);
 TestEqual(TEXT("Failure removes uncommitted roster hero"),F.Mode->GetRoster()->GetHeroes().Num(),0);
 if(!TestTrue(TEXT("Retry deploys all three parts"),F.Cards->TryDeployHandCard(CardId,{3,1})))return false;
 TestEqual(TEXT("Three parts committed"),F.Cards->GetDeploymentParts().Num(),3);TestEqual(TEXT("Three cells occupied"),F.Grid->GetOccupiedCount(),3);
 TestFalse(TEXT("One card cannot deploy twice"),F.Cards->TryDeployHandCard(CardId,{4,1}));
 if(!TestTrue(TEXT("Main actor is available"),!F.Cards->GetPieces().IsEmpty()))return false;
 TestTrue(TEXT("Selling main actor removes group"),F.Cards->TrySellPlacedPiece(F.Cards->GetPieces()[0]));
 TestEqual(TEXT("Sell clears all three parts"),F.Cards->GetDeploymentParts().Num(),0);TestEqual(TEXT("Sell clears cells"),F.Grid->GetOccupiedCount(),0);
 return true;
}
#endif
