#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "Game/EGMatchManager.h"
#include "Placement/EGDeploymentBoard.h"
#include "Game/EGCardManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGWidgetControllerLifecycleTest, "ElyGambit.UI.WidgetControllerLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGWidgetControllerLifecycleTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
    UWorld* World = GI->GetWorld();
    auto NewHand = [World]()
    {
        auto* Hand = World->SpawnActor<AEGCardManager>();
        Hand->Board = World->SpawnActor<AEGDeploymentBoard>();
        Hand->InitializeCards(); return Hand;
    };
    auto* First = NewHand(); auto* Second = NewHand();
    TStrongObjectPtr<UEGOverlayWidgetController> WC(NewObject<UEGOverlayWidgetController>(GI.Get()));
    TStrongObjectPtr<UEGMatchManager> Match(NewObject<UEGMatchManager>(GI.Get()));
    FEGWidgetControllerParams Params; Params.HandSource = First; Params.MatchSource = Match.Get();
    WC->SetWidgetControllerParams(Params);
    for (int32 I = 0; I < 4; ++I) WC->BindCallbacksToDependencies();
    TestEqual(TEXT("Repeated binding has exactly one hand listener"), First->OnHandChanged.GetAllObjects().Num(), 1);
    TestEqual(TEXT("Repeated binding has exactly one phase listener"), Match->OnPhaseChanged.GetAllObjects().Num(), 1);
    TestEqual(TEXT("Initial hand snapshot available before broadcast"), WC->GetHandView().Num(), 1);
    auto Snapshot = WC->GetHandView(); Snapshot.Reset();
    TestEqual(TEXT("Editing view snapshot cannot mutate gameplay hand"), First->GetHand().Num(), 1);
    TestTrue(TEXT("UI request uses existing manager transaction"), WC->RequestObtainElysia());
    TestEqual(TEXT("Snapshot follows gameplay source"), WC->GetHandView().Num(), 2);
    const int32 BeforeEconomy=WC->HandBroadcastCount;
    First->AwardGold(1);
    TestEqual(TEXT("Economy changes do not refresh hand"),WC->HandBroadcastCount,BeforeEconomy);
    First->OnHandChanged.Broadcast();
    TestEqual(TEXT("Unchanged hand events are coalesced"),WC->HandBroadcastCount,BeforeEconomy);
    Match->StartRun(); Match->EnterCombat(); Match->ResolveCombat(true); Match->NextStage();
    TestTrue(TEXT("Real match source is reported"), WC->GetMatchView().bHasMatch);
    TestEqual(TEXT("Stage read from existing match, no UI counter"), WC->GetMatchView().StageNumber, 2);

    Params.HandSource = Second; WC->SetWidgetControllerParams(Params); WC->BindCallbacksToDependencies();
    TestFalse(TEXT("Old hand unbound during rebind"), First->OnHandChanged.GetAllObjects().Contains(WC.Get()));
    TestEqual(TEXT("New hand bound once"), Second->OnHandChanged.GetAllObjects().Num(), 1);
    WC->RequestObtainElysia();
    TestEqual(TEXT("Rebound command targets only new hand"), Second->GetHand().Num(), 2);
    TestEqual(TEXT("Previous hand remains unchanged"), First->GetHand().Num(), 2);
    Second->Destroy();
    TestEqual(TEXT("Destroyed source yields empty view"), WC->GetHandView().Num(), 0);
    TestFalse(TEXT("Destroyed source rejects UI command"), WC->RequestObtainElysia());

    WC->Shutdown(); WC->Shutdown();
    TestFalse(TEXT("Shutdown unbinds real match"), Match->OnPhaseChanged.GetAllObjects().Contains(WC.Get()));
    TestFalse(TEXT("Shutdown clears dependency view"), WC->GetMatchView().bHasMatch);
    TestFalse(TEXT("Shutdown controller rejects mutation"), WC->RequestObtainElysia());
    Params.HandSource = First; Params.MatchSource = nullptr;
    WC->SetWidgetControllerParams(Params); WC->BindCallbacksToDependencies();
    TestTrue(TEXT("Controller can be initialized after shutdown"), WC->RequestObtainElysia());
    WC->Shutdown();
    TestFalse(TEXT("Final teardown leaves no hand listener"), First->OnHandChanged.GetAllObjects().Contains(WC.Get()));
    GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    return true;
}
#endif
