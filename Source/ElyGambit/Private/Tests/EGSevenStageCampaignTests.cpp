#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "HAL/PlatformProcess.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Game/EGHeroRoster.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGBoardScene.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageProductionConfigurationTest,
    "ElyGambit.SevenStage.ProductionConfiguration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGSevenStageProductionConfigurationTest::RunTest(const FString& Parameters)
{
    auto* Seed = EGProductionTestAssets::ResolveRun(*this);
    if (!Seed) return false;
    TStrongObjectPtr<UEGRunDefinition> Run(EGSevenStage::Build(GetTransientPackage(), Seed));
    if (!TestNotNull(TEXT("Build seven-stage run from formal map seed"), Run.Get())) return false;
    TestTrue(TEXT("Seven-stage rules selected"), Run->bSevenStageRules);
    TestTrue(TEXT("Round shop enabled"), Run->bRoundShopRules);
    TestEqual(TEXT("Starting corruption"), Run->StartingCorruption, 50);
    TestEqual(TEXT("Opening hand count"), Run->StartingCards, 2);
    TestFalse(TEXT("Opening uses randomized second hero"), Run->bUseExplicitStartingCards);
    TestFalse(TEXT("Population follows the campaign curve"), Run->bUseFixedPopulation);
    if (!TestNotNull(TEXT("Opening card"), Run->StartingCard.Get()) ||
        !TestNotNull(TEXT("Opening hero"), Run->StartingCard->HeroDefinition.Get())) return false;
    TestEqual(TEXT("Opening hero is ordinary Elysia"), Run->StartingCard->HeroDefinition->HeroKind, EEGHeroKind::Elysia);
    const TSet<EEGHeroKind> ExpectedHeroes{
        EEGHeroKind::Elysia, EEGHeroKind::Hua, EEGHeroKind::Pardofelis,
        EEGHeroKind::Eden, EEGHeroKind::Sakura, EEGHeroKind::VillV,
        EEGHeroKind::Griseo, EEGHeroKind::Mobius, EEGHeroKind::Aponia
    };
    TSet<EEGHeroKind> FoundHeroes;
    for (const auto& Card : Run->CardPool)
    {
        if (!TestNotNull(TEXT("Every shop card exists"), Card.Get()) ||
            !TestNotNull(TEXT("Every shop hero exists"), Card->HeroDefinition.Get())) continue;
        FoundHeroes.Add(Card->HeroDefinition->HeroKind);
    }
    TestEqual(TEXT("Exactly nine shop cards"), Run->CardPool.Num(), 9);
    TestEqual(TEXT("Shop contains nine distinct hero identities"),FoundHeroes.Num(),ExpectedHeroes.Num());
    for (const auto Kind : ExpectedHeroes)
        TestTrue(TEXT("Shop contains required base hero"),FoundHeroes.Contains(Kind));
    if (!TestEqual(TEXT("Seven configured stages"), Run->Stages.Num(), 7)) return false;

    const int32 ExpectedPopulation[] = {3,3,4,4,5,5,5};
    const float ExpectedSeconds[] = {25.f,25.f,25.f,25.f,25.f,25.f,0.f};
    const int32 ExpectedIncome[] = {5,5,6,6,7,8,0};
    const int32 ExpectedEnemyLevel[] = {1,2,3,5,7,8,6};
    int32 TotalIncome=Run->StartingGold;
    // Per stage: chest, eye, spore, flower, copied hero, boss.
    const int32 Expected[7][6] = {
        {2,1,0,0,0,0}, {2,1,1,0,0,0}, {1,1,0,0,1,0},
        {1,2,1,1,0,0}, {2,2,1,2,0,0}, {1,0,1,0,2,0}, {0,0,0,0,0,1}
    };
    for (int32 StageIndex=0; StageIndex<7; ++StageIndex)
    {
        const auto& Stage=Run->Stages[StageIndex];
        const FString Prefix=FString::Printf(TEXT("Stage %d "),StageIndex+1);
        TestEqual(*(Prefix+TEXT("population")),AEGCardManager::PopulationForStage(StageIndex+1),ExpectedPopulation[StageIndex]);
        TestEqual(*(Prefix+TEXT("time limit")),Stage.StageBattleTimeLimit,ExpectedSeconds[StageIndex]);
        TestEqual(*(Prefix+TEXT("completion income")),Stage.CompletionGold,ExpectedIncome[StageIndex]);
        TotalIncome+=Stage.CompletionGold;
        TestEqual(*(Prefix+TEXT("no test traps")),Stage.Traps.Num(),0);
        TSet<FIntPoint> Used;
        int32 Count[6]={};
        for (const auto& Slot : Stage.Enemies)
        {
            if (!TestNotNull(*(Prefix+TEXT("enemy definition")),Slot.Definition.Get())) continue;
            // Elite copy levels are resolved from the selected player during battle
            // preparation; the authored value here is only the fallback placeholder.
            if (!Slot.bCorruptedCopy)
                TestEqual(*(Prefix+TEXT("authored non-copy enemy level")),Slot.Level,ExpectedEnemyLevel[StageIndex]);
            TestTrue(*(Prefix+TEXT("cell in enemy rows")),Slot.Cell.X>=0 && Slot.Cell.X<7 && Slot.Cell.Y>=4 && Slot.Cell.Y<7);
            TArray<FIntPoint> Footprint;
            TestTrue(*(Prefix+TEXT("valid footprint")),AEGDeploymentBoard::MakeFootprintCells(Slot.Cell,Slot.Definition->GetPlacementFootprint(),Footprint));
            for (const FIntPoint Cell : Footprint)
            {
                TestTrue(*(Prefix+TEXT("footprint in enemy rows")),Cell.X>=0 && Cell.X<7 && Cell.Y>=4 && Cell.Y<7);
                TestFalse(*(Prefix+TEXT("footprint unoccupied")),Used.Contains(Cell));
                Used.Add(Cell);
            }
            if (Slot.bCorruptedCopy) ++Count[4];
            else if (!Slot.Definition->MonsterAttack) ++Count[5];
            else switch (Slot.Definition->MonsterAttack->Ultimate)
            {
            case EEGMonsterUltimate::Shield: ++Count[0]; break;
            case EEGMonsterUltimate::EnhancedShot: ++Count[1]; break;
            case EEGMonsterUltimate::TeamSpeed: ++Count[2]; break;
            case EEGMonsterUltimate::CrystalSeed: ++Count[3]; break;
            default: AddError(Prefix+TEXT("unexpected monster skill")); break;
            }
        }
        for (int32 Kind=0;Kind<6;++Kind)
            TestEqual(*(Prefix+FString::Printf(TEXT("enemy kind %d count"),Kind)),Count[Kind],Expected[StageIndex][Kind]);
        if (StageIndex<6)
        {
            TestTrue(*(Prefix+TEXT("reserved low-corruption slot remains valid")),Stage.Enemies.IsValidIndex(Stage.LowCorruptionRemoveIndex));
            if (Stage.Enemies.IsValidIndex(Stage.LowCorruptionRemoveIndex))
                TestFalse(*(Prefix+TEXT("reserved slot is not a copy")),Stage.Enemies[Stage.LowCorruptionRemoveIndex].bCorruptedCopy);
            TestTrue(*(Prefix+TEXT("high corruption extra exists")),Stage.bHasHighCorruptionEnemy);
            TestNotNull(*(Prefix+TEXT("extra enemy definition")),Stage.HighCorruptionEnemy.Definition.Get());
            TestFalse(*(Prefix+TEXT("extra is ordinary")),Stage.HighCorruptionEnemy.bCorruptedCopy);
            const auto Cell=Stage.HighCorruptionEnemy.Cell;
            TestTrue(*(Prefix+TEXT("extra cell in enemy rows")),Cell.X>=0 && Cell.X<7 && Cell.Y>=4 && Cell.Y<7);
            if (Stage.HighCorruptionEnemy.Definition)
            {
                TArray<FIntPoint> ExtraFootprint;
                TestTrue(*(Prefix+TEXT("extra footprint valid")),AEGDeploymentBoard::MakeFootprintCells(Cell,Stage.HighCorruptionEnemy.Definition->GetPlacementFootprint(),ExtraFootprint));
                for (const FIntPoint ExtraCell : ExtraFootprint)
                {
                    TestTrue(*(Prefix+TEXT("extra footprint in enemy rows")),ExtraCell.X>=0 && ExtraCell.X<7 && ExtraCell.Y>=4 && ExtraCell.Y<7);
                    TestFalse(*(Prefix+TEXT("extra footprint reserved")),Used.Contains(ExtraCell));
                }
            }
        }
        else TestFalse(*(Prefix+TEXT("no ordinary corruption extra")),Stage.bHasHighCorruptionEnemy);
    }
    TestEqual(TEXT("Opening and six settlements total 43 before optional sources and spending"),TotalIncome,43);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageMatchFlowTest,
    "ElyGambit.SevenStage.MatchFlow",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGSevenStageMatchFlowTest::RunTest(const FString& Parameters)
{
    auto* Seed=EGProductionTestAssets::ResolveRun(*this);
    if (!Seed) return false;
    TStrongObjectPtr<UEGRunDefinition> Run(EGSevenStage::Build(GetTransientPackage(),Seed));
    if (!TestNotNull(TEXT("Campaign run"),Run.Get())) return false;
    TStrongObjectPtr<UEGMatchManager> Match(NewObject<UEGMatchManager>());
    if (!TestTrue(TEXT("Configure campaign"),Match->ConfigureRunDefinition(Run.Get()))) return false;
    const int32 Deltas[]={5,5,10,10,15,15};
    for (int32 Pattern=0;Pattern<3;++Pattern)
    {
        TestTrue(TEXT("Start fresh run"),Match->StartRun());
        TestEqual(TEXT("Fresh corruption"),Match->GetCorruption(),50);
        int32 ExpectedCorruption=50;
        for (int32 Stage=1;Stage<=7;++Stage)
        {
            const FString Prefix=FString::Printf(TEXT("Pattern %d stage %d "),Pattern,Stage);
            TestEqual(*(Prefix+TEXT("stage number")),Match->GetStageNumber(),Stage);
            TestEqual(*(Prefix+TEXT("preparation")),Match->GetPhase(),EEGMatchPhase::Preparation);
            TestTrue(*(Prefix+TEXT("enter combat")),Match->EnterCombat());
            TestFalse(*(Prefix+TEXT("duplicate enter rejected")),Match->EnterCombat());
            const bool Won=Pattern==0 || (Pattern==2 && Stage%2==1);
            TestTrue(*(Prefix+TEXT("resolve once")),Match->ResolveCombat(Won));
            TestFalse(*(Prefix+TEXT("duplicate resolve rejected")),Match->ResolveCombat(!Won));
            if (Stage==7)
            {
                TestEqual(*(Prefix+TEXT("final finished")),Match->GetPhase(),EEGMatchPhase::Finished);
                TestEqual(*(Prefix+TEXT("final result")),Match->DidWinRun(),Won);
                TestFalse(*(Prefix+TEXT("no eighth stage")),Match->NextStage());
                continue;
            }
            TestEqual(*(Prefix+TEXT("settlement")),Match->GetPhase(),EEGMatchPhase::Settlement);
            TestEqual(*(Prefix+TEXT("corruption unchanged before impact")),Match->GetCorruption(),ExpectedCorruption);
            TestFalse(*(Prefix+TEXT("advance during settlement rejected")),Match->NextStage());
            TestTrue(*(Prefix+TEXT("apply impact once")),Match->ApplySettlementImpact());
            ExpectedCorruption=FMath::Clamp(ExpectedCorruption+(Won?-Deltas[Stage-1]:Deltas[Stage-1]),0,200);
            TestEqual(*(Prefix+TEXT("correct corruption")),Match->GetCorruption(),ExpectedCorruption);
            TestFalse(*(Prefix+TEXT("second impact rejected")),Match->ApplySettlementImpact());
            TestTrue(*(Prefix+TEXT("finish settlement")),Match->FinishSettlement());
            TestEqual(*(Prefix+TEXT("impact not duplicated")),Match->GetCorruption(),ExpectedCorruption);
            TestFalse(*(Prefix+TEXT("duplicate settlement finish rejected")),Match->FinishSettlement());
            if (Stage==2 || Stage==4 || Stage==6)
            {
                TestTrue(*(Prefix+TEXT("reward phase accepts either result")),Match->EnterReward());
                TestFalse(*(Prefix+TEXT("cannot advance before reward")),Match->NextStage());
                TestFalse(*(Prefix+TEXT("cannot bypass campaign choice")),Match->CompleteReward());
                if(Stage==6)
                {
                    TestFalse(*(Prefix+TEXT("cannot advance before route")),Match->NextStage());
                    TestTrue(*(Prefix+TEXT("begin separate route transaction")),Match->BeginStigmataCommit(TEXT("Unity")));
                    TestTrue(*(Prefix+TEXT("commit separate route")),Match->CommitEndgameRoute(TEXT("Unity")));
                    TestEqual(*(Prefix+TEXT("route stored independently")),Match->GetEndgameRoute(),FName(TEXT("Unity")));
                    TestFalse(*(Prefix+TEXT("route is not an ordinary stigmata")),Match->HasStigmata(TEXT("Unity")));
                }
                else
                {
                    const FName Id=Stage==2?FName(TEXT("LoveBeginning")):FName(TEXT("Flawless"));
                    TestTrue(*(Prefix+TEXT("begin receipt")),Match->BeginStigmataCommit(Id));
                    TestTrue(*(Prefix+TEXT("commit receipt once")),Match->CommitStigmata(false));
                }
                TestFalse(*(Prefix+TEXT("duplicate completion rejected")),Match->CompleteReward());
            }
            TestTrue(*(Prefix+TEXT("advance after settlement")),Match->NextStage());
        }
    }
    TestTrue(TEXT("Restart before settlement threshold scenario"),Match->StartRun());
    TestEqual(TEXT("Set up 195 corruption"),Match->AddCorruption(145),145);
    TestTrue(TEXT("Threshold scenario enters combat"),Match->EnterCombat());
    TestTrue(TEXT("Threshold scenario resolves a loss"),Match->ResolveCombat(false));
    TestTrue(TEXT("Settlement impact applies the terminal delta"),Match->ApplySettlementImpact());
    TestEqual(TEXT("Settlement reaches 200"),Match->GetCorruption(),200);
    TestEqual(TEXT("Settlement loss remains finished"),Match->GetPhase(),EEGMatchPhase::Finished);
    TestFalse(TEXT("Terminal impact cannot repeat"),Match->ApplySettlementImpact());
    TestFalse(TEXT("Finished settlement cannot advance"),Match->FinishSettlement());
    TestFalse(TEXT("Corruption defeat cannot be retried at 200"),Match->RetryStage());
    TestFalse(TEXT("Corruption defeat cannot advance"),Match->NextStage());

    TestTrue(TEXT("Restart after completed run"),Match->StartRun());
    TestEqual(TEXT("Restart resets stage"),Match->GetStageNumber(),1);
    TestEqual(TEXT("Restart resets corruption"),Match->GetCorruption(),50);
    TestEqual(TEXT("Clamp at zero"),Match->AddCorruption(-1000),-50);
    TestEqual(TEXT("Zero is retained"),Match->GetCorruption(),0);
    TestEqual(TEXT("Clamp at 200"),Match->AddCorruption(1000),200);
    TestEqual(TEXT("200 is retained"),Match->GetCorruption(),200);
    TestEqual(TEXT("Reaching 200 immediately finishes the run"),Match->GetPhase(),EEGMatchPhase::Finished);
    TestFalse(TEXT("Corruption loss cannot become victory"),Match->DidWinRun());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageRewardsTest,
    "ElyGambit.SevenStage.RewardsOnDefeat",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGSevenStageRewardsTest::RunTest(const FString& Parameters)
{
    auto* Seed=EGProductionTestAssets::ResolveRun(*this);
    if (!Seed || !GEngine) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World=Instance->GetWorld();
    if (!TestNotNull(TEXT("Fixture world"),World)) return false;
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
        Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    };
    World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
    FURL URL;World->SetGameMode(URL);
    auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
    if (!TestNotNull(TEXT("Fixture mode"),Mode)) return false;
    Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
    if (!TestNotNull(TEXT("Seven-stage runtime configuration"),Mode->RunDefinition.Get())) return false;
    auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
    auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
    auto* Cards=World->SpawnActor<AEGCardManager>();
    auto* CombatBoard=World->SpawnActor<AEGBoard>();
    if (!Scene || !Grid || !Cards || !CombatBoard) {AddError(TEXT("Fixture actors could not spawn"));return false;}
    Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
    Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=CombatBoard;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    if (!TestNotNull(TEXT("Run match"),Mode->GetMatch()) ||
        !TestEqual(TEXT("Campaign starts in preparation"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Preparation)) return false;
    auto TickUntil=[&](TFunctionRef<bool()> Done)
    {
        for (int32 Frame=0;Frame<150 && !Done();++Frame)
        {
            ++GFrameCounter;
            World->Tick(LEVELTICK_All,.1f);
        }
        return Done();
    };
    if(!TestTrue(TEXT("Shop fixture stage ready"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
    TestEqual(TEXT("Campaign starts with six gold"),Cards->GetGold(),int64(6));
    TestEqual(TEXT("First preparation refresh is free"),Cards->GetRefreshCost(),0);
    TestTrue(TEXT("Free refresh succeeds"),Cards->RefreshShop());
    TestEqual(TEXT("Free refresh spends nothing"),Cards->GetGold(),int64(6));
    TestEqual(TEXT("Next refresh costs one"),Cards->GetRefreshCost(),1);
    TestTrue(TEXT("Paid refresh succeeds"),Cards->RefreshShop());
    TestEqual(TEXT("Paid refresh spends exactly one"),Cards->GetGold(),int64(5));
    Cards->AwardGold(600);
    TestEqual(TEXT("Raise corruption over shop threshold"),Mode->GetMatch()->AddCorruption(21),21);
    for(int32 Draw=0;Draw<12;++Draw)
    {
        if(!TestTrue(TEXT("Shop refresh succeeds"),Cards->RefreshShop()))return false;
        const auto Offers=Cards->GetShop();
        TestEqual(TEXT("Shop keeps four slots at high corruption"),Offers.Num(),4);
        for(int32 Slot=0;Slot<Offers.Num();++Slot)
        {
            TestFalse(TEXT("High corruption never blocks a shop slot"),Cards->IsCorruptionShopSlot(Slot));
            TestNotNull(TEXT("High corruption shop slot remains purchasable"),Offers[Slot]);
        }
    }
    TestEqual(TEXT("Restore initial corruption"),Mode->GetMatch()->AddCorruption(-21),-21);
    TestTrue(TEXT("Refresh below threshold"),Cards->RefreshShop());
    for(int32 Slot=0;Slot<4;++Slot)TestFalse(TEXT("No interference at 50"),Cards->IsCorruptionShopSlot(Slot));
    for(int32 Pattern=0;Pattern<3;++Pattern)
    {
        if(Pattern==2)
        {
            if(!TestTrue(TEXT("Fusion fixture resources ready"),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
            const auto Opening=Cards->GetHand();
            if(!TestTrue(TEXT("Fusion fixture has opening cards"),Opening.Num()>=2))return false;
            if(!TestTrue(TEXT("Fusion participant deployed"),Cards->TryDeployHandCard(Opening[0].InstanceId,{3,1})))return false;
            const auto Pieces=Cards->GetPieces();
            if(!TestEqual(TEXT("Fusion fixture has one participant"),Pieces.Num(),1))return false;
            if(!TestTrue(TEXT("Fusion participant gains permanent levels"),Mode->GetRoster()->AddLevels(Pieces[0]->RosterId,2)))return false;
            if(!TestTrue(TEXT("Fusion fixture overfills hand"),Cards->GrantCard(Opening[1].Definition.Get(),10)))return false;
        }
        for(int32 Stage=1;Stage<=6;++Stage)
        {
            const FString Prefix=FString::Printf(TEXT("Pattern %d stage %d "),Pattern,Stage);
            TestEqual(*(Prefix+TEXT("stage number")),Mode->GetMatch()->GetStageNumber(),Stage);
            if(!TestTrue(*(Prefix+TEXT("stage resources are ready")),TickUntil([&](){return Mode->AreStageResourcesReady();})))return false;
            int32 EnemyCount=0;
            for (auto* Unit : Mode->GetCombatUnits())
                if (IsValid(Unit) && !Unit->IsActorBeingDestroyed() &&
                    IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy) ++EnemyCount;
            TestEqual(*(Prefix+TEXT("prepared enemy count is independent of corruption")),
                EnemyCount,Mode->RunDefinition->Stages[Stage-1].Enemies.Num());
            if(Stage>1||Pattern>0)
            {
                TestEqual(*(Prefix+TEXT("new preparation has one free refresh")),Cards->GetRefreshCost(),0);
                const int64 BeforeFree=Cards->GetGold();
                TestTrue(*(Prefix+TEXT("new preparation free refresh succeeds")),Cards->RefreshShop());
                TestEqual(*(Prefix+TEXT("free refresh does not spend gold")),Cards->GetGold(),BeforeFree);
                TestEqual(*(Prefix+TEXT("next refresh costs one")),Cards->GetRefreshCost(),1);
            }
            const int64 BeforeGold=Cards->GetGold();
            const int32 BeforeHand=Cards->GetHand().Num();
            const int32 BeforeRoster=Mode->GetRoster()->GetHeroes().Num();
            if(!TestTrue(*(Prefix+TEXT("enter combat")),Mode->GetMatch()->EnterCombat()) ||
               !TestTrue(*(Prefix+TEXT("resolve combat")),Mode->ResolveCombat(Pattern==0)))return false;
            if(Stage==2||Stage==4||Stage==6)
            {
                if(!TestTrue(*(Prefix+TEXT("reaches stigmata choice")),TickUntil([&](){return Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward;})))return false;
                const int32 Revision=Mode->GetStigmataRevision();
                TestEqual(*(Prefix+TEXT("completion income applied once")),Cards->GetGold(),BeforeGold+Mode->RunDefinition->Stages[Stage-1].CompletionGold);
                const auto Offers=Mode->GetStigmataOffer();
                if(Stage==6)
                {
                    TestTrue(TEXT("Stage six opens final route directly"),Mode->IsChoosingEndgameRoute());
                    TestTrue(TEXT("Stage six has no ordinary offer"),Offers.IsEmpty());
                    TestFalse(TEXT("Cannot skip final route"),Mode->SkipStigma(Revision));
                    const auto Routes=Mode->GetEndgameRouteOffer();
                    TestTrue(TEXT("Route page offers all three endings"),Routes.Num()==3&&Routes.Contains(TEXT("OriginFlower"))&&Routes.Contains(TEXT("Story"))&&Routes.Contains(TEXT("Unity")));
                    const FName Route=Pattern==0?FName(TEXT("Story")):Pattern==1?FName(TEXT("Unity")):FName(TEXT("OriginFlower"));
                    TestFalse(TEXT("Stale revision cannot select route"),Mode->ChooseEndgameRoute(Route,Revision-1));
                    TestTrue(TEXT("Final route commits"),Mode->ChooseEndgameRoute(Route,Revision));
                    TestFalse(TEXT("Final route cannot be claimed twice"),Mode->ChooseEndgameRoute(Route,Revision));
                    TestEqual(TEXT("Final route is stored separately"),Mode->GetMatch()->GetEndgameRoute(),Route);
                    TestFalse(TEXT("Route does not occupy an ordinary stigmata slot"),Mode->GetMatch()->HasStigmata(Route));
                    if(Pattern==0)
                    {
                        TestEqual(TEXT("Story grants nine cards"),Cards->GetHand().Num(),BeforeHand+9);
                        TestEqual(TEXT("Story sets nine population"),Cards->GetRules().DeploymentLimit,9);
                        for(const auto& Card:Cards->GetHand())TestTrue(TEXT("Story held cards have minimum level six"),Card.InitialLevel>=6);
                    }
                    if(Pattern==2)
                    {
                        TestEqual(TEXT("Fusion grants one hand card"),Cards->GetHand().Num(),BeforeHand+1);
                        TestTrue(TEXT("Fusion removes all deployed pieces"),Cards->GetPieces().IsEmpty());
                        const auto& Hand=Cards->GetHand();
                        const auto* Human=Hand.FindByPredicate([](const FEGCardInstance& Card)
                            {return Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;});
                        if(!TestNotNull(TEXT("Fusion human card exists"),Human))return false;
                        TestEqual(TEXT("Human card level doubles participant levels"),Human->InitialLevel,6);
                        TestTrue(TEXT("Human card protected from automatic overflow"),Cards->IsHandOverflowProtected(Human->InstanceId));
                        TestFalse(TEXT("Human card not listed as overflow"),Cards->GetOverflowHandIds().Contains(Human->InstanceId));
                        const FGuid HumanCardId=Human->InstanceId;
                        TestTrue(TEXT("Full hand settlement starts"),Cards->BeginHandSettlement());
                        Cards->EndHandSettlement(true);
                        TestTrue(TEXT("Human card survives excess hand settlement"),Cards->HasHandCard(HumanCardId));
                    }
                }
                else
                {
                TestEqual(*(Prefix+TEXT("three options")),Offers.Num(),3);
                if(Offers.Num()!=3)return false;
                FName Selection=NAME_None;
                for(const FName Offer:Offers)
                {
                    TestFalse(*(Prefix+TEXT("ordinary offer excludes final route")),Offer==TEXT("OriginFlower")||Offer==TEXT("Story")||Offer==TEXT("Unity"));
                    if(Offer!=TEXT("Reverie")&&Selection.IsNone())Selection=Offer;
                }
                if(!TestFalse(*(Prefix+TEXT("non-gold ordinary option available")),Selection.IsNone()))return false;
                TestFalse(*(Prefix+TEXT("stale choice rejected")),Mode->ChooseStigmata(Selection,Revision-1));
                TestFalse(*(Prefix+TEXT("skip rejected")),Mode->SkipStigma(Revision));
                TestTrue(*(Prefix+TEXT("choice accepted")),Mode->ChooseStigmata(Selection,Revision));
                TestFalse(*(Prefix+TEXT("duplicate choice rejected")),Mode->ChooseStigmata(Selection,Revision));
                TestTrue(*(Prefix+TEXT("choice persists")),Mode->GetMatch()->HasStigmata(Selection));
                TestEqual(*(Prefix+TEXT("ordinary choice does not grant cards")),Cards->GetHand().Num(),BeforeHand);
                TestEqual(*(Prefix+TEXT("selection preserves roster count")),Mode->GetRoster()->GetHeroes().Num(),BeforeRoster);
                TestEqual(*(Prefix+TEXT("selection adds no gold")),Cards->GetGold(),BeforeGold+Mode->RunDefinition->Stages[Stage-1].CompletionGold);
                }
            }
            if(!TestTrue(*(Prefix+TEXT("automatic advance")),TickUntil([&](){return Mode->GetMatch()->GetStageNumber()==Stage+1;})))return false;
            TestEqual(*(Prefix+TEXT("completion economy independent of result")),Cards->GetGold(),BeforeGold+Mode->RunDefinition->Stages[Stage-1].CompletionGold);
            if(Stage>=5)TestEqual(*(Prefix+TEXT("expected final population")),Cards->GetRules().DeploymentLimit,Stage==6&&Pattern==0?9:5);
        }
        // Drive the real world clock with AI and boss seed ticks disabled. This isolates event ordering, not balance.
        const auto Hand=Cards->GetHand();
        const FEGCardInstance* FinalCard=Pattern==2?Hand.FindByPredicate([](const FEGCardInstance& Card)
            {return Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;}):Hand.IsEmpty()?nullptr:&Hand[0];
        if(!TestFalse(TEXT("Final fixture has opening cards"),Hand.IsEmpty()) ||
           !TestNotNull(TEXT("Final fixture card exists"),FinalCard) ||
           !TestTrue(TEXT("Deploy final clock fixture"),Cards->TryDeployHandCard(FinalCard->InstanceId,{3,1})))return false;
        if(Pattern==2)
        {
            const auto Pieces=Cards->GetPieces();
            FEGHeroInstance Human;
            if(!TestEqual(TEXT("Human card deploys as one piece"),Pieces.Num(),1)||
               !TestTrue(TEXT("Human roster instance exists"),Mode->GetRoster()->GetHero(Pieces[0]->RosterId,Human)))return false;
            TestEqual(TEXT("Human keeps earned level on deployment"),Human.Level,6);
        }
        const int32 Initial=Pattern==0 ? 180 : 190;
        Mode->GetMatch()->AddCorruption(Initial-Mode->GetMatch()->GetCorruption());
        if(!TestTrue(TEXT("Final fixture combat"),Mode->EnterCombat()))return false;
        if(!TestTrue(TEXT("Opening finishes before clock fixture"),TickUntil([&](){return Mode->BattleRules->IsCombatRunning();})))return false;
        AEGChessUnit* Boss=nullptr;
        AEGChessUnit* Player=nullptr;
        for(auto* Unit:Mode->GetCombatUnits())
        {
            if(Unit->FindComponentByClass<UEGCorruptionCombatComponent>())Boss=Unit;
            if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Player=Unit;
        }
        if(!TestNotNull(TEXT("Final stage boss"),Boss)||!TestNotNull(TEXT("Clock fixture player"),Player))return false;
        for(auto* Unit:Mode->GetCombatUnits())
        {
            if(!Unit)continue;
            Unit->GetCombat()->SetComponentTickEnabled(false);
            Unit->GetHeroSkill()->SetComponentTickEnabled(false);
            if(auto* Corruption=Unit->FindComponentByClass<UEGCorruptionCombatComponent>())Corruption->SetComponentTickEnabled(false);
        }
        if(auto* Corruption=Boss->FindComponentByClass<UEGCorruptionCombatComponent>())Corruption->SetComponentTickEnabled(false);
        auto AdvanceTo=[&](float Target)
        {
            for(int32 Frame=0;Frame<20000&&Mode->GetBattleSeconds()<Target&&Mode->GetMatch()->GetPhase()==EEGMatchPhase::Combat;++Frame)
            {++GFrameCounter;World->Tick(LEVELTICK_All,.01f);}
            return Mode->GetBattleSeconds()>=Target;
        };
        if(!TestTrue(TEXT("Advance to before enrage"),AdvanceTo(59.9f)))return false;
        TestEqual(TEXT("Boss corruption remains unchanged before enrage"),Mode->GetMatch()->GetCorruption(),Initial);
        if(!TestTrue(TEXT("Advance beyond enrage"),AdvanceTo(65.1f)))return false;
        TestEqual(TEXT("Enrage does not increment corruption"),Mode->GetMatch()->GetCorruption(),Initial);
        auto* BossRules=Boss->FindComponentByClass<UEGCorruptionCombatComponent>();
        if(!TestNotNull(TEXT("Production boss phase rules"),BossRules))return false;
        TestTrue(TEXT("Boss enrages with a progressive damage multiplier"),BossRules->IsEnraged()&&BossRules->GetOutgoingDamageMultiplier()>=1.2f);
        if(Pattern==0)
        {
            // Real same-frame deaths are resolved together, with loss taking precedence.
            ++GFrameCounter;
            Boss->Die();
            Player->Die();
            World->Tick(LEVELTICK_All,.1f);
            TestEqual(TEXT("Same-frame deaths do not resolve before the next frame"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Combat);
            ++GFrameCounter;
            World->Tick(LEVELTICK_All,.1f);
            TestEqual(TEXT("Same-frame double death finishes exactly once"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Finished);
            TestFalse(TEXT("Simultaneous player and boss death is a loss"),Mode->GetMatch()->DidWinRun());
        }
        else
        {
            const int32 BeforeWave=Mode->GetCombatUnits().Num();
            FEGCombatExecution::Damage(Player,Boss,1.e8f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid());
            TestTrue(TEXT("First-pool lethal hit leaves boss alive at the wall"),IEGCombatant::Execute_IsAlive(Boss)&&BossRules->GetBossPhase()==EEGCorruptionBossPhase::Barrier);
            TestEqual(TEXT("Stage seven wall spawns no old reinforcement wave"),Mode->GetCombatUnits().Num(),BeforeWave);
            BossRules->CompleteBarrierIntroForTests();
            FEGCombatExecution::Damage(Player,Boss,1.e8f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid());
            TestTrue(TEXT("Breaking wall enters independent second pool"),BossRules->GetBossPhase()==EEGCorruptionBossPhase::Second&&BossRules->GetPhaseHealth()>0.f);
            TestEqual(TEXT("Second phase spawns no old reinforcement wave"),Mode->GetCombatUnits().Num(),BeforeWave);
            Boss->Die();
            ++GFrameCounter;
            World->Tick(LEVELTICK_All,.01f);
            TestTrue(TEXT("Earlier boss death with living player wins"),Mode->GetMatch()->DidWinRun());
            TestEqual(TEXT("Boss victory leaves corruption unchanged"),Mode->GetMatch()->GetCorruption(),Initial);
        }
        TestTrue(TEXT("Restart fixture"),Mode->StartGame());
        TestEqual(TEXT("Restart restores population"),Cards->GetRules().DeploymentLimit,3);
        TestEqual(TEXT("Restart restores corruption"),Mode->GetMatch()->GetCorruption(),50);
        TestTrue(TEXT("Restart clears roster"),Mode->GetRoster()->GetHeroes().IsEmpty());
        TestTrue(TEXT("Restart clears acquired stigmata"),Mode->GetMatch()->GetStigmataOwned().IsEmpty());
    }
    return true;
}
#endif

