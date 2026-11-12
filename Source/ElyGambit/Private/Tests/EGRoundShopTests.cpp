#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
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
#include "AbilitySystem/EGAttributeSet.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystemComponent.h"
#include "UI/EGUnitInformation.h"
#include "Tests/EGProductionTestAssets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGRoundShopTransactions,"ElyGambit.Board.ShopAndRoundTransactions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGRoundShopTransactions::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
    UWorld* World=GI->GetWorld();World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
    FURL URL;World->SetGameMode(URL);auto* GM=World->GetAuthGameMode<AEGDemoGameMode>();
    auto* Base=EGProductionTestAssets::ResolveRun(*this);
    if(!TestNotNull(TEXT("Existing run fixture"),Base))return false;
    auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,*this))return false;Run->StartingCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));Run->bRoundShopRules=true;Run->StartingGold=3;Run->StartingCards=2;
    Run->CardRules.PurchasePrice=3;Run->CardRules.HandLimit=8;Run->CardRules.DeploymentLimit=3;Run->CardRules.SameNameLimit=0;
    Run->CardPool={Run->StartingCard};
    Run->Stages.SetNum(2);
    for(auto& S:Run->Stages){S.Traps.Reset();for(int32 I=0;I<S.Enemies.Num();++I)S.Enemies[I].Cell={I,4};}
    for(auto& S:Run->Stages)S.VictoryGold=0;
    GM->RunDefinition=Run;
    auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
    Grid->Columns=5;Grid->Rows=6;Grid->PlayerRows=3;
    auto* Cards=World->SpawnActor<AEGCardManager>();Cards->Board=Grid;Cards->AvailableCardDefinition=Run->StartingCard;
    Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=World->SpawnActor<AEGBoard>();
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    TestTrue(TEXT("Immediate placement"),Cards->CanEditPlacement());
    TestEqual(TEXT("Two starting cards"),Cards->GetHand().Num(),2);TestEqual(TEXT("Starting gold"),Cards->GetGold(),int64(3));
    TestEqual(TEXT("Four random shop slots"),Cards->GetShop().Num(),4);
    const auto Before=Cards->GetShop();TestTrue(TEXT("Purchase current slot"),Cards->BuyShopSlot(1));
    TestEqual(TEXT("Purchase costs three"),Cards->GetGold(),int64(0));TestEqual(TEXT("Purchase grants one card"),Cards->GetHand().Num(),3);
    const auto After=Cards->GetShop();TestTrue(TEXT("Purchase preserves other slots"),Before[0]==After[0]&&Before[2]==After[2]&&Before[3]==After[3]);
    TestFalse(TEXT("Insufficient gold cannot refresh"),Cards->RefreshShop());
    TestFalse(TEXT("Legacy purchase cannot bypass shop"),Cards->RequestObtainCard(Run->StartingCard));
    Cards->AwardGold(50);for(int32 I=0;I<7;++I)TestTrue(TEXT("Temporarily exceed eight cards"),Cards->BuyShopSlot(I%4));
    const int32 BeforeFailedStart=Cards->GetHand().Num();TestFalse(TEXT("Empty army cannot start"),GM->EnterCombat());
    TestEqual(TEXT("Failed start does not sell overflow"),Cards->GetHand().Num(),BeforeFailedStart);
    TestTrue(TEXT("Deploy gifted Pardo"),Cards->TryDeployHandCard(Cards->GetHand()[0].InstanceId,{2,1}));
    TestTrue(TEXT("Start combat"),GM->EnterCombat());TestEqual(TEXT("Only eight retained"),Cards->GetHand().Num(),8);
    TestFalse(TEXT("No placement during combat"),Cards->CanEditPlacement());
    TestTrue(TEXT("Purchase during combat"),Cards->BuyShopSlot(0));TestTrue(TEXT("Refresh during combat"),Cards->RefreshShop());
    TestTrue(TEXT("Resolve nonfinal defeat"),GM->ResolveCombat(false));
    TestEqual(TEXT("Defeat remains in run"),GM->GetMatch()->GetPhase(),EEGMatchPhase::Resolution);
    TestEqual(TEXT("Defeat adds one corruption"),GM->GetMatch()->GetCorruption(),1);
    ++GFrameCounter;World->Tick(LEVELTICK_All,.2f);
    TestEqual(TEXT("First defeat advances directly"),GM->GetMatch()->GetStageNumber(),2);
    World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGRoundShopProductionRun,"ElyGambit.Board.TwoStagesNoRewardsAndRestart",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGRoundShopProductionRun::RunTest(const FString&)
{
 TStrongObjectPtr<UEGRunDefinition> ProductionRun(EGProductionTestAssets::ResolveRun(*this));
 auto* Run=ProductionRun.Get();
 if(!TestNotNull(TEXT("Formal two-stage data"),Run))return false;
 TestEqual(TEXT("Exactly two"),Run->Stages.Num(),2);
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->RunDefinition=Run;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 TSet<FName> Allowed;
 for(const auto& Card:Run->CardPool)if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind!=EEGHeroKind::Elysia)Allowed.Add(Card->HeroDefinition->HeroId);
 TestTrue(TEXT("Configured production card pool is nonempty"),!Allowed.IsEmpty());
 TestEqual(TEXT("Eight ordinary production heroes"),Allowed.Num(),8);
 TestEqual(TEXT("Production starting gold"),Run->StartingGold,3);
 TestEqual(TEXT("Production starting count"),Run->StartingCards,2);
 TestTrue(TEXT("Production uses the explicit test opening"),Run->bUseExplicitStartingCards);
 TestTrue(TEXT("Production uses fixed population"),Run->bUseFixedPopulation);
 TestEqual(TEXT("Fixed population is three"),Run->FixedPopulation,3);
 TestEqual(TEXT("Exactly two configured opening grants"),Run->ExplicitStartingCards.Num(),2);
 TestEqual(TEXT("Configured hero identities are unique shop candidates"),Cards->CardPool.Num(),Allowed.Num());
 TestEqual(TEXT("Opening card is Elysia"),Run->StartingCard->HeroDefinition->HeroKind,EEGHeroKind::Elysia);
 for(const auto& Card:Cards->CardPool)TestTrue(TEXT("No unfinished hero in configured pool"),Card&&Card->HeroDefinition&&Allowed.Contains(Card->HeroDefinition->HeroId));
 const auto Opening=Cards->GetHand();
 FGuid CrossStageElysiaId;FEGHeroInstance CrossStageElysia;
 TestEqual(TEXT("Exactly two opening cards"),Opening.Num(),2);
 if(Opening.Num()==2)
 {
  TestEqual(TEXT("First opening card is Elysia"),Opening[0].Definition->HeroDefinition->HeroKind,EEGHeroKind::Elysia);
  TestTrue(TEXT("Second opening card is the independent Herrscher"),Opening[1].Definition&&Opening[1].Definition->HeroDefinition&&Opening[1].Definition->HeroDefinition->HeroId==FName(TEXT("ElysiaHerrscherOfHuman")));
  TestFalse(TEXT("Herrscher is absent from the ordinary shop pool"),Allowed.Contains(FName(TEXT("ElysiaHerrscherOfHuman"))));
  TestFalse(TEXT("Corruption enemy is absent from the ordinary shop pool"),Allowed.Contains(FName(TEXT("ElysiaCorruption"))));
  const auto OpeningRules=Cards->GetRules();auto TooSmall=OpeningRules;TooSmall.HandLimit=1;
  Cards->ConfigureRules(TooSmall,GM->GetRoster());
  TestFalse(TEXT("Opening human cannot be silently discarded by overflow settlement"),Cards->CanResolveExcessHand());
  Cards->ConfigureRules(OpeningRules,GM->GetRoster());
  const auto IssuedSnapshot=GM->GetRoster()->CaptureRunState();
  TestTrue(TEXT("Opening issue receipt is held with the roster"),IssuedSnapshot.bStartingCardsIssued);
  GM->GetRoster()->RestoreRunState(IssuedSnapshot);
  Cards->ConfigureRoundShop(Run);
  TestEqual(TEXT("Restored roster does not reissue opening cards"),Cards->GetHand().Num(),2);
  TestTrue(TEXT("Opening identities survive repeated initialization"),Cards->GetHand().Num()==2&&Cards->GetHand()[0].InstanceId==Opening[0].InstanceId&&Cards->GetHand()[1].InstanceId==Opening[1].InstanceId);
  Cards->SetPopulationFloor(9);
  TestEqual(TEXT("Fixed population ignores external floor"),Cards->GetRules().DeploymentLimit,3);
  Cards->BeginRoundShopRound(5,true);
  TestEqual(TEXT("Fixed population ignores later-stage growth"),Cards->GetRules().DeploymentLimit,3);
  Cards->LastIncomeStage=1;Cards->InitialRound=1;
  const FGuid CardId=Opening[0].InstanceId;const int64 GoldBefore=Cards->GetGold();
  TestTrue(TEXT("Opening Elysia is protected"),Cards->IsSaleProtected(CardId));
  TestFalse(TEXT("Opening Elysia cannot be sold"),Cards->TrySellHandCard(CardId));
  TestEqual(TEXT("Rejected sale gives no gold"),Cards->GetGold(),GoldBefore);
  TestTrue(TEXT("Deploy Elysia"),Cards->TryDeployHandCard(CardId,{2,1}));
  auto* Piece=Cards->GetPieces().IsEmpty()?nullptr:Cards->GetPieces()[0];
  if(TestNotNull(TEXT("Elysia piece"),Piece))
  {
   const FGuid HeroId=Piece->RosterId;
   const FGuid HumanCardId=Opening[1].InstanceId;
   TestEqual(TEXT("Herrscher footprint is one cell"),Opening[1].Definition->HeroDefinition->GetPlacementFootprint(),FIntPoint(1,1));
   TestFalse(TEXT("Single-cell placement cannot overlap Elysia"),Cards->TryDeployHandCard(HumanCardId,{2,1}));
   TestTrue(TEXT("Blocked placement leaves Elysia in place"),Grid->GetOccupant({2,1})==Piece);
   if(TestTrue(TEXT("Both Elysias can be deployed in adjacent cells"),Cards->TryDeployHandCard(HumanCardId,{3,1})))
   {
    AEGDeployedPiece* Human=nullptr;for(auto* P:Cards->GetPieces())if(P&&P->CardDefinition==Opening[1].Definition)Human=P;
    if(TestNotNull(TEXT("Herrscher has one deployment actor"),Human))
    {
     TestEqual(TEXT("Two heroes consume two population"),Cards->GetPieces().Num(),2);
     TestTrue(TEXT("The chosen cell belongs to the human"),Grid->GetOccupant({3,1})==Human);
     TestNull(TEXT("An adjacent cell remains available"),Grid->GetOccupant({4,1}));
     FEGHeroInstance HumanBefore,HumanAfter;const FGuid HumanId=Human->RosterId;
     TestTrue(TEXT("Independent human roster entry"),GM->GetRoster()->GetHero(HumanId,HumanBefore));
     TestEqual(TEXT("Configured initial human level"),HumanBefore.Level,Opening[1].InitialLevel);
     FEGHeroInstance ElysiaBeforeHumanGrowth,ElysiaAfterHumanGrowth;
     TestTrue(TEXT("Normal Elysia remains an independent roster entry"),GM->GetRoster()->GetHero(HeroId,ElysiaBeforeHumanGrowth));
     TestTrue(TEXT("Temporary independent growth fixture"),GM->GetRoster()->AddLevels(HumanId,2));
     TestTrue(TEXT("Normal Elysia still exists after human growth"),GM->GetRoster()->GetHero(HeroId,ElysiaAfterHumanGrowth));
     TestEqual(TEXT("Human growth never raises normal Elysia level"),ElysiaAfterHumanGrowth.Level,ElysiaBeforeHumanGrowth.Level);
     TestTrue(TEXT("Give a duplicate human only to exercise disabled fusion"),Cards->GrantCard(Opening[1].Definition));
     const FGuid DuplicateHumanCard=Cards->GetHand().Last().InstanceId;
     TestFalse(TEXT("This test run cannot merge the human card"),Cards->TryMergeIntoHero(DuplicateHumanCard,HumanId));
     TestTrue(TEXT("Rejected human fusion does not consume the card"),Cards->HasHandCard(DuplicateHumanCard));
     TestTrue(TEXT("Human card retains its ordinary sale rule"),Cards->TrySellHandCard(DuplicateHumanCard));
     TestTrue(TEXT("Human can return to hand"),Cards->TryWithdraw(Human));
     TestNull(TEXT("Withdrawal releases the occupied cell"),Grid->GetOccupant({3,1}));
     TestTrue(TEXT("Withdrawal preserves the adjacent hero"),Grid->GetOccupant({2,1})==Piece);
     TestTrue(TEXT("Human redeploys from same persistent instance"),Cards->TryDeployReserve(HumanId,{3,1}));
     TestTrue(TEXT("Human progress remains in roster"),GM->GetRoster()->GetHero(HumanId,HumanAfter));
     TestEqual(TEXT("Human independent level survives redeploy"),HumanAfter.Level,HumanBefore.Level+2);
     TestEqual(TEXT("Human identity survives redeploy"),HumanAfter.InstanceId,HumanId);
    }
   }
   TestTrue(TEXT("Merge same-name card"),Cards->GrantCard(Run->StartingCard));
   TestTrue(TEXT("Same-name merge succeeds"),Cards->TryMergeIntoHero(Cards->GetHand().Last().InstanceId,HeroId));
   FEGHeroInstance Before,After;GM->GetRoster()->GetHero(HeroId,Before);
   for(int32 I=0;I<10&&!Before.bAwakened;++I)
   {
    if(!Cards->GrantCard(Run->StartingCard)||!Cards->TryMergeIntoHero(Cards->GetHand().Last().InstanceId,HeroId))break;
    GM->GetRoster()->GetHero(HeroId,Before);
   }
   TestTrue(TEXT("Cross-stage fixture has actually awakened"),Before.bAwakened);
   TestFalse(TEXT("Deployed Elysia cannot be sold"),Cards->TrySellPlacedPiece(Piece));
   TestTrue(TEXT("Withdraw Elysia"),Cards->TryWithdraw(Piece));
   TestTrue(TEXT("Withdraw retains identity"),Cards->HasReserveHero(HeroId));
   TestEqual(TEXT("Registered Elysia resolves outside shop"),Cards->FindHeroCardDefinition(HeroId),Run->StartingCard.Get());
   TestFalse(TEXT("Reserve Elysia cannot be sold"),Cards->TrySellReserveHero(HeroId));
   TestTrue(TEXT("Redeploy same Elysia"),Cards->TryDeployReserve(HeroId,{2,1}));
   TestTrue(TEXT("Hero remains registered"),GM->GetRoster()->GetHero(HeroId,After));
   TestEqual(TEXT("Level retained"),After.Level,Before.Level);
   TestEqual(TEXT("Merge count retained"),After.SameNameCards,Before.SameNameCards);
   TestEqual(TEXT("Awakening retained"),After.bAwakened,Before.bAwakened);
   AEGDeployedPiece* Redeployed=nullptr;
   for(auto* Candidate:Cards->GetPieces())if(Candidate&&Candidate->RosterId==HeroId)Redeployed=Candidate;
   if(TestNotNull(TEXT("Redeployed Elysia piece"),Redeployed))
   {
    const int32 Population=Cards->GetPieces().Num();
    TestTrue(TEXT("Withdraw cultivated Elysia before stage transition"),Cards->TryWithdraw(Redeployed));
    TestEqual(TEXT("Reserve Elysia releases one population"),Cards->GetPieces().Num(),Population-1);
    CrossStageElysiaId=HeroId;CrossStageElysia=After;
   }
  }
 }
 TestEqual(TEXT("Stage 1 population"),AEGCardManager::PopulationForStage(1),3);
 TestEqual(TEXT("Stage 2 population"),AEGCardManager::PopulationForStage(2),3);
 TestEqual(TEXT("Stage 3 population"),AEGCardManager::PopulationForStage(3),4);
 TestEqual(TEXT("Stage 4 population"),AEGCardManager::PopulationForStage(4),4);
 TestEqual(TEXT("Stage 5 population"),AEGCardManager::PopulationForStage(5),5);
 const auto OriginalPool=Cards->CardPool;
 Cards->CardPool.Reset();Cards->CardPool.Add(Run->StartingCard);
 if(Opening.Num()>1)Cards->CardPool.Add(Opening[1].Definition);
 Cards->AwardGold(1);
 TestTrue(TEXT("Refresh tolerates a mutated pool"),Cards->RefreshShop());
 for(const auto* Offer:Cards->GetShop())TestNull(TEXT("Final draw guard rejects both Elysias"),Offer);
 Cards->CardPool=OriginalPool;
 Cards->ShopRandom.Initialize(20261021);Cards->AwardGold(300);TMap<FName,int32> Seen;
 for(int32 Draw=0;Draw<256;++Draw)
 {
  TestTrue(TEXT("Refresh unified shop"),Cards->RefreshShop());
  for(const auto* Offer:Cards->GetShop())if(Offer){TestTrue(TEXT("Refresh never offers unfinished heroes or summons"),Offer->HeroDefinition&&Allowed.Contains(Offer->HeroDefinition->HeroId));++Seen.FindOrAdd(Offer->CardId);}
 }
 TestEqual(TEXT("Every maintained hero is offered"),Seen.Num(),Allowed.Num());
 const float Expected=1024.f/Allowed.Num();
 for(const auto& Pair:Seen)TestTrue(TEXT("Fixed-seed sample remains uniformly drawn without pack weighting"),FMath::Abs(Pair.Value-Expected)<Expected*.35f);
 const auto Hand=Cards->GetHand();for(const auto& C:Hand)Cards->TryDeployHandCard(C.InstanceId,{2,1});
 for(int32 Stage=1;Stage<=2;++Stage)
 {
  TestEqual(TEXT("Expected stage"),GM->GetMatch()->GetStageNumber(),Stage);
  auto CountCorruption=[&](){int32 N=0;for(auto* Unit:GM->GetCombatUnits())if(Unit&&Unit->GetDefinition()&&Unit->GetDefinition()->HeroId==FName(TEXT("ElysiaCorruption"))&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)++N;return N;};
  TestEqual(TEXT("Corruption Elysia appears only in stage two"),CountCorruption(),Stage==2?1:0);
  TestTrue(TEXT("Combat"),GM->EnterCombat());
  if(CrossStageElysiaId.IsValid())
  {
   if(Stage==1)
   {
    TestTrue(TEXT("Elysia stays in reserve during combat"),Cards->HasReserveHero(CrossStageElysiaId));
    for(const auto& Unit:GM->GetBattleUnits())if(Unit.IsValid())
     TestNotEqual(TEXT("Reserve Elysia has no combat unit"),Unit->GetInstanceId(),CrossStageElysiaId);
   }
   else
   {
    AEGDeployedPiece* ElysiaPiece=nullptr;
    for(auto* Piece:Cards->GetPieces())if(Piece&&Piece->RosterId==CrossStageElysiaId)ElysiaPiece=Piece;
    if(TestNotNull(TEXT("Elysia deployed for second combat"),ElysiaPiece))
     TestFalse(TEXT("Cannot withdraw Elysia during combat"),Cards->TryWithdraw(ElysiaPiece));
   }
  }
  if(Stage==2)
  {
   const int32 HandBeforeRetry=Cards->GetHand().Num();
   TestTrue(TEXT("Lose the final stage"),GM->ResolveCombat(false));
   TestTrue(TEXT("The lost stage is retryable"),GM->CanRetryCurrentStage());
   TestTrue(TEXT("Retry the same stage"),GM->RetryCurrentStage());
   TestEqual(TEXT("Retry stays at stage two"),GM->GetMatch()->GetStageNumber(),2);
   TestEqual(TEXT("Retry retains hand without another opening grant"),Cards->GetHand().Num(),HandBeforeRetry);
   TestEqual(TEXT("Retry produces exactly one corruption Elysia"),CountCorruption(),1);
   TestFalse(TEXT("Repeated retry request is rejected"),GM->RetryCurrentStage());
   TestTrue(TEXT("Retried stage can enter combat"),GM->EnterCombat());
   const auto Units=GM->GetCombatUnits();
   for(auto* Unit:Units)if(Unit&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Unit->Die();
   ++GFrameCounter;W->Tick(LEVELTICK_All,.2f);
   TestTrue(TEXT("Enemy deaths resolve the final encounter as victory"),GM->GetMatch()->GetPhase()==EEGMatchPhase::Finished&&GM->WasVictory());
  }
  else TestTrue(TEXT("Resolve first stage"),GM->ResolveCombat(true));
  if(Stage==1)
  {
   const int32 HandBeforeTransition=Cards->GetHand().Num();
   TestTrue(TEXT("Direct transition"),GM->AdvanceDemo());
   if(CrossStageElysiaId.IsValid())
   {
    TestEqual(TEXT("Stage transition grants no extra hand cards"),Cards->GetHand().Num(),HandBeforeTransition);
    const auto Reserve=Cards->GetReserveHeroes();
    TestEqual(TEXT("One reserve Elysia after transition"),Reserve.FilterByPredicate([&](const FEGHeroInstance& H){return H.InstanceId==CrossStageElysiaId;}).Num(),1);
    for(const auto& Card:Cards->GetHand())
     TestFalse(TEXT("No additional Elysia card granted"),Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::Elysia);
    FEGHeroInstance AfterTransition;
    if(TestTrue(TEXT("Cultivated Elysia survives transition"),GM->GetRoster()->GetHero(CrossStageElysiaId,AfterTransition)))
    {
     TestFalse(TEXT("Elysia remains undeployed"),AfterTransition.bDeployed);
     TestEqual(TEXT("Cross-stage identity retained"),AfterTransition.InstanceId,CrossStageElysia.InstanceId);
     TestEqual(TEXT("Cross-stage level retained"),AfterTransition.Level,CrossStageElysia.Level);
     TestEqual(TEXT("Cross-stage same-name count retained"),AfterTransition.SameNameCards,CrossStageElysia.SameNameCards);
     TestEqual(TEXT("Cross-stage awakening retained"),AfterTransition.bAwakened,CrossStageElysia.bAwakened);
    }
    const int32 Population=Cards->GetPieces().Num();bool bRedeployed=false;
    for(int32 Y=0;Y<Grid->PlayerRows&&!bRedeployed;++Y)for(int32 X=0;X<Grid->Columns&&!bRedeployed;++X)
     if(Cards->EvaluateReservePlacement(CrossStageElysiaId,{X,Y}).IsAllowed())bRedeployed=Cards->TryDeployReserve(CrossStageElysiaId,{X,Y});
    TestTrue(TEXT("Redeploy same Elysia in next stage"),bRedeployed);
    TestEqual(TEXT("Redeployment uses one population"),Cards->GetPieces().Num(),Population+1);
    FEGHeroInstance AfterRedeploy;
    if(TestTrue(TEXT("Redeployment retains roster identity"),GM->GetRoster()->GetHero(CrossStageElysiaId,AfterRedeploy)))
    {
     TestTrue(TEXT("Elysia is deployed again"),AfterRedeploy.bDeployed);
     TestEqual(TEXT("Redeployed identity retained"),AfterRedeploy.InstanceId,CrossStageElysia.InstanceId);
     TestEqual(TEXT("Redeployed level retained"),AfterRedeploy.Level,CrossStageElysia.Level);
     TestEqual(TEXT("Redeployed same-name count retained"),AfterRedeploy.SameNameCards,CrossStageElysia.SameNameCards);
     TestEqual(TEXT("Redeployed awakening retained"),AfterRedeploy.bAwakened,CrossStageElysia.bAwakened);
    }
   }
  }
 }
 TestEqual(TEXT("Final result"),GM->GetMatch()->GetPhase(),EEGMatchPhase::Finished);TestFalse(TEXT("No third"),GM->NextStage());
 TestTrue(TEXT("Restart"),GM->AdvanceDemo());TestEqual(TEXT("Restart grants starting hand once"),Cards->GetHand().Num(),2);TestEqual(TEXT("Restart resets gold"),Cards->GetGold(),int64(3));TestEqual(TEXT("Reset stage"),GM->GetMatch()->GetStageNumber(),1);
 const auto RestartHand=Cards->GetHand();
 if(RestartHand.Num()==2)
 {
  const FGuid ProtectedId=RestartHand[0].InstanceId;
  auto* OrdinaryCard=Run->CardPool.IsEmpty()?nullptr:Run->CardPool[0].Get();
  if(!TestNotNull(TEXT("Ordinary shop fixture"),OrdinaryCard))return false;
  TestTrue(TEXT("Human remains manually sellable"),Cards->TrySellHandCard(RestartHand[1].InstanceId));
  auto* DynamicCard=DuplicateObject<UEGCardDefinition>(OrdinaryCard,Cards);
  DynamicCard->HeroDefinition=DuplicateObject<UEGHeroDefinition>(OrdinaryCard->HeroDefinition,Cards);
  DynamicCard->HeroDefinition->HeroId=TEXT("DynamicRegistryFixture");
  auto* Equivalent=DuplicateObject<UEGHeroDefinition>(DynamicCard->HeroDefinition,Cards);
  TestTrue(TEXT("Grant dynamic registry card"),Cards->GrantCard(DynamicCard));
  const FGuid DynamicCardId=Cards->GetHand().Last().InstanceId;
  const FGuid DynamicHero=GM->GetRoster()->AddHero(Equivalent);
  TestTrue(TEXT("Dynamic equivalent hero registered"),DynamicHero.IsValid());
  TestEqual(TEXT("Dynamic card found by hero identity"),Cards->FindHeroCardDefinition(DynamicHero),DynamicCard);
  TestTrue(TEXT("Clean dynamic reserve"),Cards->TrySellReserveHero(DynamicHero));
  TestTrue(TEXT("Clean dynamic card"),Cards->TrySellHandCard(DynamicCardId));
  const FGuid OrdinaryReserve=GM->GetRoster()->AddHero(OrdinaryCard->HeroDefinition);
  TestTrue(TEXT("Ordinary reserve fixture"),OrdinaryReserve.IsValid());
  TestTrue(TEXT("Ordinary hand fixture"),Cards->GrantCard(OrdinaryCard));
  const auto OriginalRules=Cards->GetRules();auto TightRules=OriginalRules;TightRules.HandLimit=1;Cards->ConfigureRules(TightRules,GM->GetRoster());
  TestTrue(TEXT("Ordinary reserve permits settlement"),Cards->CanResolveExcessHand());
  const int32 LockedCount=Cards->GetHand().Num();const int64 LockedGold=Cards->GetGold();
  TestTrue(TEXT("Begin atomic settlement"),Cards->BeginHandSettlement());
  TestFalse(TEXT("Reentrant protected grant blocked"),Cards->GrantCard(Run->StartingCard));
  TestFalse(TEXT("Reentrant roster edit blocked"),GM->GetRoster()->Withdraw(OrdinaryReserve));
  Cards->EndHandSettlement(false);
  TestEqual(TEXT("Cancelled settlement keeps cards"),Cards->GetHand().Num(),LockedCount);
  TestEqual(TEXT("Cancelled settlement gives no gold"),Cards->GetGold(),LockedGold);
  TestTrue(TEXT("Cancelled settlement keeps reserve"),Cards->HasReserveHero(OrdinaryReserve));
  Cards->SellExcessHand();
  TestEqual(TEXT("Overflow includes ordinary reserve"),Cards->GetHand().Num(),1);
  TestFalse(TEXT("Ordinary reserve recycled"),Cards->HasReserveHero(OrdinaryReserve));
  TestEqual(TEXT("Ordinary card and reserve pay once each"),Cards->GetGold(),LockedGold+2);
  Cards->ConfigureRules(OriginalRules,GM->GetRoster());
  TestTrue(TEXT("Restore ordinary hand fixture"),Cards->GrantCard(OrdinaryCard));
  const FGuid OrdinaryHandId=Cards->GetHand().Last().InstanceId;
  TestTrue(TEXT("Grant ordinary overflow"),Cards->GrantCard(OrdinaryCard,9));
  const int64 BeforeOverflowGold=Cards->GetGold();Cards->SellExcessHand();
  TestEqual(TEXT("Overflow retains eight"),Cards->GetHand().Num(),8);
  TestTrue(TEXT("Overflow retains Elysia"),Cards->HasHandCard(ProtectedId));
  TestEqual(TEXT("Only ordinary overflow pays"),Cards->GetGold(),BeforeOverflowGold+3);
  TestTrue(TEXT("Deploy ordinary hero"),Cards->TryDeployHandCard(OrdinaryHandId,{2,1}));
  TestTrue(TEXT("Grant protected overflow fixture"),Cards->GrantCard(Run->StartingCard,8));
  TestFalse(TEXT("Protected overflow cannot be automatically resolved"),Cards->CanResolveExcessHand());
  const int32 BeforeCount=Cards->GetHand().Num();const int64 BeforeGold=Cards->GetGold();
  TestFalse(TEXT("Protected overflow blocks combat"),GM->EnterCombat());
  TestEqual(TEXT("Rejected combat keeps all cards"),Cards->GetHand().Num(),BeforeCount);
  TestEqual(TEXT("Rejected combat pays nothing"),Cards->GetGold(),BeforeGold);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGExplicitOpeningValidation,"ElyGambit.Board.ExplicitOpeningValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGExplicitOpeningValidation::RunTest(const FString&)
{
 auto* Run=NewObject<UEGRunDefinition>();
 auto* A=NewObject<UEGCardDefinition>(Run);auto* B=NewObject<UEGCardDefinition>(Run);
 A->HeroDefinition=NewObject<UEGHeroDefinition>(Run);B->HeroDefinition=NewObject<UEGHeroDefinition>(Run);
 A->CardId=TEXT("C1");A->HeroDefinition->HeroId=TEXT("Elysia");A->PieceClass=AEGDeployedPiece::StaticClass();
 B->CardId=TEXT("C2");B->HeroDefinition->HeroId=TEXT("ElysiaHerrscherOfHuman");B->PieceClass=AEGDeployedPiece::StaticClass();
 Run->StartingCard=A;Run->bRoundShopRules=true;Run->bUseExplicitStartingCards=true;
 FEGStartingCardGrant First;First.Card=A;FEGStartingCardGrant Second;Second.Card=B;Second.InitialLevel=3;
 Run->ExplicitStartingCards={First,Second};Run->CardRules.HandLimit=2;FString Error;
 TestTrue(TEXT("Distinct opening grants validate"),Run->ValidateStartingCards(Error));
 Run->ExplicitStartingCards[1].InitialLevel=0;TestFalse(TEXT("Invalid level is rejected"),Run->ValidateStartingCards(Error));
 Run->ExplicitStartingCards[1].InitialLevel=3;B->HeroDefinition->HeroId=A->HeroDefinition->HeroId;
 TestFalse(TEXT("Duplicate hero identity is rejected"),Run->ValidateStartingCards(Error));
 B->HeroDefinition->HeroId=TEXT("ElysiaHerrscherOfHuman");B->CardId=A->CardId;
 TestFalse(TEXT("Duplicate card identity is rejected"),Run->ValidateStartingCards(Error));
 B->CardId=TEXT("C2");Run->CardRules.HandLimit=1;
 TestFalse(TEXT("Opening hand exceeding capacity is rejected"),Run->ValidateStartingCards(Error));
 return true;
}
#endif
