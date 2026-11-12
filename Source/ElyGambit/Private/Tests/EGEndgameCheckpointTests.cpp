#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/EGProductionTestAssets.h"
#include "UObject/StrongObjectPtr.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEndgameCheckpointTest,
 "ElyGambit.SevenStage.EndgameCheckpointRoutes",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGEndgameCheckpointTest::RunTest(const FString&)
{
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);if(!Seed||!GEngine)return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("World"),World))return false;
 ON_SCOPE_EXIT {World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Mode"),Mode))return false;
 Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
 auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 auto* Cards=World->SpawnActor<AEGCardManager>();auto* Board=World->SpawnActor<AEGBoard>();
 if(!Scene||!Grid||!Cards||!Board||!Mode->RunDefinition)return false;
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Cards->Board=Grid;
 Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto TickUntil=[&](TFunctionRef<bool()> Condition)
 {for(int32 Frame=0;Frame<200&&!Condition();++Frame){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}return Condition();};
 if(!TestTrue(TEXT("Initial resources"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
 const auto Opening=Cards->GetHand();if(!TestTrue(TEXT("Initial hand"),Opening.Num()>0))return false;
 if(!TestTrue(TEXT("Deploy original hero"),Cards->TryDeployHandCard(Opening[0].InstanceId,{3,1})))return false;
 TestFalse(TEXT("No checkpoint before final battle"),Mode->CanRestoreEndgameCheckpoint());
 for(int32 Stage=1;Stage<=6;++Stage)
 {
  if(!TestTrue(TEXT("Stage resources"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
  if(!TestTrue(TEXT("Enter stage"),Mode->GetMatch()->EnterCombat())||!TestTrue(TEXT("Resolve stage"),Mode->ResolveCombat(true)))return false;
  if(Stage==6)
  {
   // Force the real snapshot validation to reject one deployed piece; a failed
   // checkpoint must not allow the automatic resolution tick to reach the boss.
   const auto Parts=Cards->GetDeploymentParts();
   if(!TestTrue(TEXT("A deployed piece can exercise checkpoint validation"),!Parts.IsEmpty()&&IsValid(Parts[0])&&Parts[0]->CardDefinition))return false;
   auto* Piece=Parts[0];auto* SavedCard=Piece->CardDefinition.Get();
   Piece->CardDefinition=nullptr;
   AddExpectedError(TEXT("Unable to capture the endgame retry checkpoint"),EAutomationExpectedErrorFlags::Contains,2);
   if(!TestTrue(TEXT("Checkpoint failure is exposed"),TickUntil([&](){return Mode->IsEndgameCheckpointRetryNeeded();})))return false;
   const int64 GoldAfterSettlement=Cards->GetGold();
   TestEqual(TEXT("Failed checkpoint remains in stage six"),Mode->GetMatch()->GetStageNumber(),6);
   TestEqual(TEXT("Failed checkpoint remains before reward"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Resolution);
   TestFalse(TEXT("No route before checkpoint"),Mode->IsChoosingEndgameRoute());
   TestFalse(TEXT("Failed checkpoint cannot advance"),Mode->NextStage());
   TestFalse(TEXT("Retry remains blocked while invalid"),Mode->RetryEndgameCheckpoint());
   for(int32 I=0;I<4;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}
   TestEqual(TEXT("Automatic advance remains blocked"),Mode->GetMatch()->GetStageNumber(),6);
   TestEqual(TEXT("Retry does not award income twice"),Cards->GetGold(),GoldAfterSettlement);
   Piece->CardDefinition=SavedCard;
   if(!TestTrue(TEXT("Valid checkpoint retries into route choice"),Mode->RetryEndgameCheckpoint()))return false;
   TestFalse(TEXT("Retry flag clears after success"),Mode->IsEndgameCheckpointRetryNeeded());
   TestFalse(TEXT("Completed retry cannot be repeated"),Mode->RetryEndgameCheckpoint());
   TestEqual(TEXT("Retry did not duplicate income"),Cards->GetGold(),GoldAfterSettlement);
  }
  if(Stage==2||Stage==4||Stage==6)
  {
   if(!TestTrue(TEXT("Reward opened"),TickUntil([&](){return Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward;})))return false;
   if(Stage==6)break;
   const auto Offer=Mode->GetStigmataOffer();if(!TestTrue(TEXT("Ordinary offer"),!Offer.IsEmpty()))return false;
   FName Choice=Offer[0];for(const auto Id:Offer)if(Id!=TEXT("Reverie")){Choice=Id;break;}
   if(!TestTrue(TEXT("Choose ordinary engraving"),Mode->ChooseStigmata(Choice,Mode->GetStigmataRevision())))return false;
  }
  else if(!TestTrue(TEXT("Settlement finished"),TickUntil([&](){return Mode->GetMatch()->GetPhase()==EEGMatchPhase::Resolution;})))return false;
  if(!TestTrue(TEXT("Advance stage"),Mode->NextStage()))return false;
 }
 const auto OriginalRoster=Mode->GetRoster()->GetHeroes();const auto OriginalHand=Cards->GetHand();
 const auto OriginalOrder=Cards->GetOrderedHandIds();const int64 OriginalGold=Cards->GetGold();
 const int32 OriginalPopulation=Cards->GetRules().DeploymentLimit,OriginalCorruption=Mode->GetMatch()->GetCorruption();
 const auto OriginalStigmata=Mode->GetMatch()->GetStigmataOwned();const int32 OriginalRevision=Mode->GetStigmataRevision();
 auto* Context=Mode->FindComponentByClass<UEGBattleContextComponent>();
 if(!TestNotNull(TEXT("Battle context"),Context))return false;
 TestNull(TEXT("Checkpoint is valid before any battle created stigmata statistics"),Context->GetStigmata());
 if(!TestTrue(TEXT("One deployed hero retained in checkpoint"),OriginalRoster.Num()>0&&OriginalRoster[0].bDeployed))return false;
 for(const FName Route:{FName(TEXT("OriginFlower")),FName(TEXT("Story")),FName(TEXT("Unity"))})
 {
  if(!TestTrue(TEXT("Select route"),Mode->ChooseEndgameRoute(Route,Mode->GetStigmataRevision())))return false;
  if(!TestTrue(TEXT("Advance to boss"),Mode->NextStage()))return false;
  if(!TestTrue(TEXT("Boss resources"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
  if(!TestTrue(TEXT("Enter boss combat"),Mode->GetMatch()->EnterCombat()))return false;
  if(Route==TEXT("Story"))
  {
   // Some games create the component for the first time in the final battle.
   // Its accumulated totals must still roll back to the empty checkpoint.
   Context->Start(Mode->GetRoster()->GetHeroes());
   if(!TestNotNull(TEXT("Battle created statistics"),Context->GetStigmata()))return false;
   Context->GetStigmata()->RecordGold(TEXT("CheckpointTest"),123);
  }
  if(!TestTrue(TEXT("Lose boss combat"),Mode->ResolveCombat(false)))return false;
  if(!TestTrue(TEXT("Final failure can restore"),Mode->CanRestoreEndgameCheckpoint()))return false;
  TestFalse(TEXT("Final failure cannot bypass puzzle by restarting"),Mode->AdvanceDemo());
  if(!TestTrue(TEXT("Restore checkpoint"),Mode->RestoreEndgameCheckpoint()))return false;
  TestFalse(TEXT("Restore cannot be repeated"),Mode->RestoreEndgameCheckpoint());
  TestEqual(TEXT("Returns to sixth stage"),Mode->GetMatch()->GetStageNumber(),6);
  TestEqual(TEXT("Returns to reward"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Reward);
  TestTrue(TEXT("Route choice reopened"),Mode->IsChoosingEndgameRoute());
  TestTrue(TEXT("Prior route cleared"),Mode->GetMatch()->GetEndgameRoute().IsNone());
  TestFalse(TEXT("Stale route revision rejected"),Mode->ChooseEndgameRoute(TEXT("Unity"),OriginalRevision));
  TestEqual(TEXT("Gold restored"),Cards->GetGold(),OriginalGold);
  TestEqual(TEXT("Population restored"),Cards->GetRules().DeploymentLimit,OriginalPopulation);
  TestEqual(TEXT("Corruption restored"),Mode->GetMatch()->GetCorruption(),OriginalCorruption);
  TestTrue(TEXT("Ordinary engravings restored"),Mode->GetMatch()->GetStigmataOwned()==OriginalStigmata);
  if(Context->GetStigmata())TestTrue(TEXT("Post-checkpoint statistics rolled back"),Context->GetStigmata()->GetContributions().IsEmpty());
  TestTrue(TEXT("Hand order restored"),Cards->GetOrderedHandIds()==OriginalOrder);
  const auto Hand=Cards->GetHand();TestEqual(TEXT("Hand count restored"),Hand.Num(),OriginalHand.Num());
  for(int32 I=0;I<FMath::Min(Hand.Num(),OriginalHand.Num());++I)
   {TestEqual(TEXT("Hand identity restored"),Hand[I].InstanceId,OriginalHand[I].InstanceId);TestEqual(TEXT("Hand level restored"),Hand[I].InitialLevel,OriginalHand[I].InitialLevel);}
  const auto Roster=Mode->GetRoster()->GetHeroes();TestEqual(TEXT("Roster count restored"),Roster.Num(),OriginalRoster.Num());
  for(int32 I=0;I<FMath::Min(Roster.Num(),OriginalRoster.Num());++I)
   {TestEqual(TEXT("Hero identity restored"),Roster[I].InstanceId,OriginalRoster[I].InstanceId);TestEqual(TEXT("Hero level restored"),Roster[I].Level,OriginalRoster[I].Level);TestEqual(TEXT("Hero deployed restored"),Roster[I].bDeployed,OriginalRoster[I].bDeployed);}
  TestEqual(TEXT("Deployment restored"),Cards->GetPieces().Num(),1);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEndgameCheckpointTransactionsTest,
 "ElyGambit.SevenStage.EndgameCheckpointTransactions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGEndgameCheckpointTransactionsTest::RunTest(const FString&)
{
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);if(!Seed||!GEngine)return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("World"),World))return false;
 ON_SCOPE_EXIT {World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Mode"),Mode))return false;
 Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
 auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 auto* Cards=World->SpawnActor<AEGCardManager>();auto* Board=World->SpawnActor<AEGBoard>();
 if(!Scene||!Grid||!Cards||!Board||!Mode->RunDefinition)return false;
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Cards->Board=Grid;
 Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto TickUntil=[&](TFunctionRef<bool()> Condition)
 {for(int32 Frame=0;Frame<200&&!Condition();++Frame){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}return Condition();};
 if(!TestTrue(TEXT("Initial resources"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
 const auto Opening=Cards->GetHand();if(!TestTrue(TEXT("Opening card present"),!Opening.IsEmpty()))return false;
 if(!TestTrue(TEXT("Deploy starting hero"),Cards->TryDeployHandCard(Opening[0].InstanceId,{3,1})))return false;
 UEGCardDefinition* VillV=nullptr;
 for(const auto& Candidate:Mode->RunDefinition->CardPool)
  if(Candidate&&Candidate->HeroDefinition&&Candidate->HeroDefinition->HeroKind==EEGHeroKind::VillV){VillV=Candidate;break;}
 if(!TestNotNull(TEXT("Production VillV card"),VillV))return false;
 // Guarantee this fixture's setup; deployment and fusion still use production transactions.
 if(!TestTrue(TEXT("Fixture grants two VillV cards"),Cards->GrantCard(VillV,2)))return false;
 TArray<FGuid> VillVCards;
 for(const auto& Card:Cards->GetHand())if(Card.Definition==VillV)VillVCards.Add(Card.InstanceId);
 if(!TestTrue(TEXT("Two VillV cards available"),VillVCards.Num()>=2))return false;
 if(!TestTrue(TEXT("Deploy VillV and devices"),Cards->TryDeployHandCard(VillVCards[0],{0,1})))return false;
 const FGuid VillVId=Mode->GetRoster()->FindHeroByDefinition(VillV->HeroDefinition);
 if(!TestTrue(TEXT("VillV roster identity"),VillVId.IsValid()))return false;
 FEGHeroInstance BeforeMerge;if(!Mode->GetRoster()->GetHero(VillVId,BeforeMerge))return false;
 if(!TestTrue(TEXT("Merge VillV"),Cards->TryMergeIntoHero(VillVCards[1],VillVId)))return false;
 FEGHeroInstance AfterMerge;if(!Mode->GetRoster()->GetHero(VillVId,AfterMerge))return false;
 TestTrue(TEXT("Merge increases VillV level"),AfterMerge.Level>BeforeMerge.Level);
 TestTrue(TEXT("VillV includes two devices"),AfterMerge.bVillVLayout&&Cards->GetDeploymentParts().Num()==4);

 int64 StageSixGoldBefore=0;
 for(int32 Stage=1;Stage<=6;++Stage)
 {
  if(!TestTrue(TEXT("Stage resources"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
  if(Stage==6)
  {
   TestEqual(TEXT("Sixth-stage first refresh is free"),Cards->GetRefreshCost(),0);
   if(!TestTrue(TEXT("Use sixth-stage refresh"),Cards->RefreshShop()))return false;
   TestEqual(TEXT("Sixth-stage free refresh consumed"),Cards->GetRefreshCost(),1);
   if(!Cards->IsShopLocked()&&!TestTrue(TEXT("Lock sixth-stage shop"),Cards->ToggleShopLock()))return false;
   StageSixGoldBefore=Cards->GetGold();
  }
  if(!TestTrue(TEXT("Enter stage"),Mode->GetMatch()->EnterCombat())||!TestTrue(TEXT("Resolve stage"),Mode->ResolveCombat(true)))return false;
  if(Stage==2||Stage==4||Stage==6)
  {
   if(!TestTrue(TEXT("Reward opened"),TickUntil([&](){return Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward;})))return false;
   if(Stage==6)break;
   const auto Offer=Mode->GetStigmataOffer();FName Choice=NAME_None;
   for(const FName Id:Offer)if(Id!=TEXT("Reverie")&&Id!=TEXT("DuskStreet")){Choice=Id;break;}
   if(!TestFalse(TEXT("Non-economic ordinary engraving"),Choice.IsNone())||
      !TestTrue(TEXT("Choose ordinary engraving"),Mode->ChooseStigmata(Choice,Mode->GetStigmataRevision())))return false;
  }
  else if(!TestTrue(TEXT("Settlement finished"),TickUntil([&](){return Mode->GetMatch()->GetPhase()==EEGMatchPhase::Resolution;})))return false;
  if(!TestTrue(TEXT("Advance stage"),Mode->NextStage()))return false;
 }

 const auto OriginalRoster=Mode->GetRoster()->GetHeroes();const auto OriginalHand=Cards->GetHand();
 const auto OriginalOrder=Cards->GetOrderedHandIds();const auto OriginalShop=Cards->GetShop();
 const int64 OriginalGold=Cards->GetGold();const auto OriginalRules=Cards->GetRules();
 const int32 OriginalCorruption=Mode->GetMatch()->GetCorruption();const auto OriginalStigmata=Mode->GetMatch()->GetStigmataOwned();
 TestEqual(TEXT("Sixth-stage completion income applied once"),OriginalGold,StageSixGoldBefore+Mode->RunDefinition->Stages[5].CompletionGold);
 TestTrue(TEXT("Shop remains locked at checkpoint"),Cards->IsShopLocked());
 struct FPieceRecord {FGuid Id,HeroId;FIntPoint Cell;EEGDeploymentPart Role;};
 TArray<FPieceRecord> OriginalParts;
 for(const auto* P:Cards->GetDeploymentParts())OriginalParts.Add({P->InstanceId,P->RosterId,P->Cell,P->PartRole});
 if(!TestEqual(TEXT("Two heroes and VillV devices saved"),OriginalParts.Num(),4))return false;

 if(!TestTrue(TEXT("Choose Story"),Mode->ChooseEndgameRoute(TEXT("Story"),Mode->GetStigmataRevision()))||
    !TestTrue(TEXT("Enter final preparation"),Mode->NextStage())||
    !TestTrue(TEXT("Final resources"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
 TestEqual(TEXT("No repeated sixth-stage income when entering final stage"),Cards->GetGold(),OriginalGold);
 TestEqual(TEXT("Final stage resets one free refresh"),Cards->GetRefreshCost(),0);
 TestTrue(TEXT("Final stage preserves locked shop"),Cards->IsShopLocked());
 auto Shop=Cards->GetShop();int32 Slot=INDEX_NONE;
 for(int32 I=0;I<Shop.Num();++I)
  if(Shop[I]&&Shop[I]->HeroDefinition&&Shop[I]->HeroDefinition->HeroKind!=EEGHeroKind::Elysia){Slot=I;break;}
 if(!TestTrue(TEXT("A sellable shop card is offered"),Slot!=INDEX_NONE))return false;
 const auto HandBeforeBuy=Cards->GetHand();const int64 GoldBeforeBuy=Cards->GetGold();
 if(!TestTrue(TEXT("Purchase from final shop"),Cards->BuyShopSlot(Slot)))return false;
 TestEqual(TEXT("Purchase spends the configured cost"),Cards->GetGold(),GoldBeforeBuy-Cards->GetRules().PurchasePrice);
 FGuid Purchased;
 for(const auto& Card:Cards->GetHand())
  if(!HandBeforeBuy.ContainsByPredicate([&](const FEGCardInstance& Previous){return Previous.InstanceId==Card.InstanceId;})){Purchased=Card.InstanceId;break;}
 if(!TestTrue(TEXT("Purchased card identified"),Purchased.IsValid()))return false;
 const int64 Sale=Cards->GetSaleValue(Purchased),BeforeSale=Cards->GetGold();
 if(!TestTrue(TEXT("Sell purchased card"),Cards->TrySellHandCard(Purchased)))return false;
 TestEqual(TEXT("Sale applies once"),Cards->GetGold(),BeforeSale+Sale);
 if(!TestTrue(TEXT("Refresh final-stage shop"),Cards->RefreshShop()))return false;
 TestEqual(TEXT("Final-stage free refresh consumed"),Cards->GetRefreshCost(),1);
 if(!TestTrue(TEXT("Enter final combat"),Mode->GetMatch()->EnterCombat())||
    !TestTrue(TEXT("Lose final combat"),Mode->ResolveCombat(false))||
    !TestTrue(TEXT("Restore final checkpoint"),Mode->RestoreEndgameCheckpoint()))return false;

 TestEqual(TEXT("Checkpoint gold restored"),Cards->GetGold(),OriginalGold);
 TestEqual(TEXT("Checkpoint population restored"),Cards->GetRules().DeploymentLimit,OriginalRules.DeploymentLimit);
 TestEqual(TEXT("Checkpoint corruption restored"),Mode->GetMatch()->GetCorruption(),OriginalCorruption);
 TestTrue(TEXT("Checkpoint engravings restored"),Mode->GetMatch()->GetStigmataOwned()==OriginalStigmata);
 TestTrue(TEXT("Checkpoint shop lock restored"),Cards->IsShopLocked());
 TestTrue(TEXT("Checkpoint shop offers restored"),Cards->GetShop()==OriginalShop);
 TestTrue(TEXT("Checkpoint hand order restored"),Cards->GetOrderedHandIds()==OriginalOrder);
 const auto Hand=Cards->GetHand();TestEqual(TEXT("Checkpoint hand size"),Hand.Num(),OriginalHand.Num());
 for(int32 I=0;I<FMath::Min(Hand.Num(),OriginalHand.Num());++I)
  {TestEqual(TEXT("Card identity restored"),Hand[I].InstanceId,OriginalHand[I].InstanceId);TestEqual(TEXT("Card level restored"),Hand[I].InitialLevel,OriginalHand[I].InitialLevel);TestEqual(TEXT("Card paid cost restored"),Hand[I].PaidCost,OriginalHand[I].PaidCost);}
 const auto Roster=Mode->GetRoster()->GetHeroes();TestEqual(TEXT("Checkpoint roster size"),Roster.Num(),OriginalRoster.Num());
 for(int32 I=0;I<FMath::Min(Roster.Num(),OriginalRoster.Num());++I)
  {TestEqual(TEXT("Hero identity restored"),Roster[I].InstanceId,OriginalRoster[I].InstanceId);TestEqual(TEXT("Hero level restored"),Roster[I].Level,OriginalRoster[I].Level);TestEqual(TEXT("Hero cell restored"),Roster[I].DeploymentCell,OriginalRoster[I].DeploymentCell);TestEqual(TEXT("Hero merge count restored"),Roster[I].SameNameCards,OriginalRoster[I].SameNameCards);}
 const auto Parts=Cards->GetDeploymentParts();TestEqual(TEXT("All deployed parts restored"),Parts.Num(),OriginalParts.Num());
 for(const auto& Saved:OriginalParts)
 {
  const auto* Found=Parts.FindByPredicate([&](const AEGDeployedPiece* P){return P&&P->InstanceId==Saved.Id;});
  if(TestNotNull(TEXT("Restored part identity"),Found))
   {TestEqual(TEXT("Part owner restored"),(*Found)->RosterId,Saved.HeroId);TestEqual(TEXT("Part cell restored"),(*Found)->Cell,Saved.Cell);TestEqual(TEXT("Part role restored"),(*Found)->PartRole,Saved.Role);TestTrue(TEXT("Board occupancy restored"),Grid->GetOccupant(Saved.Cell)==*Found);}
 }
 if(!TestTrue(TEXT("Choose another route"),Mode->ChooseEndgameRoute(TEXT("Unity"),Mode->GetStigmataRevision()))||
    !TestTrue(TEXT("Re-enter final preparation"),Mode->NextStage())||
    !TestTrue(TEXT("Final resources after retry"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
 TestEqual(TEXT("Retry does not reapply stage-six income"),Cards->GetGold(),OriginalGold);
 TestEqual(TEXT("Final preparation refresh resets once"),Cards->GetRefreshCost(),0);
 return true;
}
#endif
