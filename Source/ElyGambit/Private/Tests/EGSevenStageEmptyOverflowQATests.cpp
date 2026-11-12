#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageEmptyOverflowQA,
    "ElyGambit.SevenStage.EmptyBoardOverflowTransaction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGSevenStageEmptyOverflowQA::RunTest(const FString&)
{
    UEGRunDefinition* Seed=EGProductionTestAssets::ResolveRun(*this);
    if(!Seed||!GEngine)return false;
    TStrongObjectPtr<UGameInstance> GameInstance(NewObject<UGameInstance>(GEngine));
    GameInstance->InitializeStandalone();
    UWorld* World=GameInstance->GetWorld();
    if(!TestNotNull(TEXT("Fixture world"),World))return false;
    ON_SCOPE_EXIT
    {
        World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
        GameInstance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    };
    World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
    FURL URL;World->SetGameMode(URL);
    auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
    if(!TestNotNull(TEXT("Fixture game mode"),Mode))return false;
    Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
    if(!TestNotNull(TEXT("Production-seeded campaign"),Mode->RunDefinition.Get()))return false;
    auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
    auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
    auto* Cards=World->SpawnActor<AEGCardManager>();
    auto* CombatBoard=World->SpawnActor<AEGBoard>();
    if(!Scene||!Grid||!Cards||!CombatBoard){AddError(TEXT("Fixture actors could not spawn"));return false;}
    Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
    Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=CombatBoard;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    for(int32 I=0;I<150&&!Mode->AreStageResourcesReady();++I){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}
    if(!TestTrue(TEXT("Stage ready"),Mode->AreStageResourcesReady()))return false;
    const auto Opening=Cards->GetHand();
    if(!TestEqual(TEXT("Two opening cards"),Opening.Num(),2))return false;
    UEGCardDefinition* Ordinary=Opening[1].Definition;
    if(!TestNotNull(TEXT("Ordinary opening card"),Ordinary)||
       !TestTrue(TEXT("Grant ten ordinary cards"),Cards->GrantCard(Ordinary,10)))return false;
    const FGuid Protected=Opening[0].InstanceId;
    const TArray<FGuid> Before=Cards->GetOrderedHandIds();
    const TArray<FGuid> Excess=Cards->GetOverflowHandIds();
    const int64 BeforeGold=Cards->GetGold();
    const int64 ExpectedProceeds=Cards->GetOverflowSaleValue();
    TestEqual(TEXT("No friendly unit deployed"),Cards->GetPieces().Num(),0);
    TestEqual(TEXT("Two ordinary cards are marked excess"),Excess.Num(),2);
    TestTrue(TEXT("Protected Elysia remains protected"),Cards->IsHandOverflowProtected(Protected));
    TestFalse(TEXT("Protected Elysia is never marked excess"),Excess.Contains(Protected));

    // A locked settlement rejects the empty-board start. No sale may happen
    // until a later successful start, and releasing the lock must be harmless.
    if(!TestTrue(TEXT("Hold settlement lock"),Cards->BeginHandSettlement()))return false;
    TestFalse(TEXT("Start while settlement locked is rejected"),Mode->EnterCombat());
    Cards->EndHandSettlement(false);
    TestTrue(TEXT("Failed start preserves ordered hand"),Cards->GetOrderedHandIds()==Before);
    TestEqual(TEXT("Failed start gives no sale proceeds"),Cards->GetGold(),BeforeGold);
    TestEqual(TEXT("Failed start remains in preparation"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Preparation);

    if(!TestTrue(TEXT("Empty board enters and resolves as a loss"),Mode->EnterCombat()))return false;
    TestEqual(TEXT("Successful empty-board start retains ten cards"),Cards->GetOrderedHandIds().Num(),10);
    TestEqual(TEXT("Exactly the previewed sale proceeds are paid"),Cards->GetGold(),BeforeGold+ExpectedProceeds);
    for(const FGuid Id:Excess)TestFalse(TEXT("Each marked ordinary card is sold"),Cards->HasHandCard(Id));
    TestTrue(TEXT("Protected Elysia remains in hand"),Cards->HasHandCard(Protected));
    TestEqual(TEXT("Loss entered settlement"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Settlement);
    TestFalse(TEXT("Repeated start rejected"),Mode->EnterCombat());
    TestEqual(TEXT("Repeated start pays nothing"),Cards->GetGold(),BeforeGold+ExpectedProceeds);
    return true;
}
#endif
