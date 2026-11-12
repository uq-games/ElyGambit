#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "Tests/EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageHandCapacityTest,
    "ElyGambit.SevenStage.HandCapacityAndOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGSevenStageHandCapacityTest::RunTest(const FString& Parameters)
{
    auto* Seed=EGProductionTestAssets::ResolveRun(*this);
    if(!Seed||!GEngine)return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();
    UWorld* World=Instance->GetWorld();
    if(!TestNotNull(TEXT("World"),World))return false;
    ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
    FURL URL;World->SetGameMode(URL);
    auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
    if(!TestNotNull(TEXT("Mode"),Mode))return false;
    Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
    if(!TestNotNull(TEXT("Campaign"),Mode->RunDefinition.Get()))return false;
    auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
    auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
    auto* Cards=World->SpawnActor<AEGCardManager>();
    auto* CombatBoard=World->SpawnActor<AEGBoard>();
    if(!Scene||!Grid||!Cards||!CombatBoard)return false;
    Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
    Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=CombatBoard;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    for(int32 I=0;I<150&&!Mode->AreStageResourcesReady();++I){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}
    if(!TestTrue(TEXT("Resources ready"),Mode->AreStageResourcesReady()))return false;
    const auto Opening=Cards->GetHand();
    if(!TestEqual(TEXT("Two opening cards"),Opening.Num(),2))return false;
    auto* Ordinary=Opening[1].Definition.Get();
    if(!TestNotNull(TEXT("Ordinary opening card"),Ordinary))return false;
    const FGuid Protected=Opening[0].InstanceId;
    TestTrue(TEXT("Elysia protected"),Cards->IsHandOverflowProtected(Protected));
    if(!TestTrue(TEXT("Add ten ordinary cards"),Cards->GrantCard(Ordinary,10)))return false;
    auto Order=Cards->GetOrderedHandIds();
    TestEqual(TEXT("All cards in unified order"),Order.Num(),12);
    TestEqual(TEXT("Two overflow cards"),Cards->GetOverflowHandIds().Num(),2);
    const FGuid Last=Order.Last();
    const int32 Revision=Cards->GetHandOrderRevision();
    TestTrue(TEXT("Move last before another card"),Cards->TryReorderHandCard(Last,Order[1],Revision));
    TestFalse(TEXT("Old revision is rejected"),Cards->TryReorderHandCard(Last,FGuid(),Revision));
    TestFalse(TEXT("Moved card is no longer overflow"),Cards->GetOverflowHandIds().Contains(Last));
    TestTrue(TEXT("Move protected card to end"),Cards->TryReorderHandCard(Protected,FGuid(),Cards->GetHandOrderRevision()));
    TestFalse(TEXT("Protected card never overflow"),Cards->GetOverflowHandIds().Contains(Protected));
    TestTrue(TEXT("Deploy ordinary opening hero"),Cards->TryDeployHandCard(Opening[1].InstanceId,{2,1}));
    auto Pieces=Cards->GetPieces();
    if(!TestEqual(TEXT("One deployed hero"),Pieces.Num(),1))return false;
    TestTrue(TEXT("Withdraw hero to reserve"),Cards->TryWithdraw(Pieces[0]));
    const auto Reserve=Cards->GetReserveHeroes();
    if(!TestEqual(TEXT("One reserve hero"),Reserve.Num(),1))return false;
    const FGuid ReserveId=Reserve[0].InstanceId;
    TestEqual(TEXT("Withdraw appends reserve"),Cards->GetOrderedHandIds().Last(),ReserveId);
    TestTrue(TEXT("Newest reserve overflows"),Cards->GetOverflowHandIds().Contains(ReserveId));
    const auto BeforeCancel=Cards->GetOrderedHandIds();
    const int64 BeforeGold=Cards->GetGold();
    TestTrue(TEXT("Begin settlement"),Cards->BeginHandSettlement());
    TestFalse(TEXT("Reorder blocked while settlement locked"),Cards->TryReorderHandCard(ReserveId,FGuid(),Cards->GetHandOrderRevision()));
    Cards->EndHandSettlement(false);
    TestTrue(TEXT("Canceled settlement preserves order"),Cards->GetOrderedHandIds()==BeforeCancel);
    TestEqual(TEXT("Canceled settlement preserves gold"),Cards->GetGold(),BeforeGold);
    const int64 Proceeds=Cards->GetOverflowSaleValue();
    TestTrue(TEXT("Begin successful settlement"),Cards->BeginHandSettlement());
    Cards->EndHandSettlement(true);
    TestEqual(TEXT("Settlement retains ten"),Cards->GetOrderedHandIds().Num(),10);
    TestEqual(TEXT("Settlement credits displayed sale value"),Cards->GetGold(),BeforeGold+Proceeds);
    TestFalse(TEXT("Overflow reserve removed"),Cards->HasReserveHero(ReserveId));
    TestTrue(TEXT("Protected Elysia remains"),Cards->HasHandCard(Protected));
    TestTrue(TEXT("Add eleven protected cards"),Cards->GrantCard(Opening[0].Definition.Get(),11));
    TestTrue(TEXT("Protected overflow never blocks start"),Cards->CanResolveExcessHand());
    TestTrue(TEXT("Begin protected settlement"),Cards->BeginHandSettlement());
    Cards->EndHandSettlement(true);
    TestEqual(TEXT("All twelve protected cards retained"),Cards->GetOrderedHandIds().Num(),12);
    TestTrue(TEXT("No protected card marked overflow"),Cards->GetOverflowHandIds().IsEmpty());
    if(!TestTrue(TEXT("Deploy protected hero"),Cards->TryDeployHandCard(Protected,{2,1}))||
       !TestTrue(TEXT("Start actual combat"),Mode->EnterCombat()))return false;
    const auto BattleHand=Cards->GetOrderedHandIds();
    if(!BattleHand.IsEmpty())
        TestFalse(TEXT("Combat rejects reorder"),Cards->TryReorderHandCard(BattleHand[0],FGuid(),Cards->GetHandOrderRevision()));
    return true;
}
#endif
