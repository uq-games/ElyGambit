#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/EGStigmataDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGMatchManager.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataCatalogTest,
    "ElyGambit.Stigmata.CatalogAndReceipt",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGStigmataCatalogTest::RunTest(const FString& Parameters)
{
    const auto& Catalog=UEGStigmataCatalog::GetDefinitions();
    TestEqual(TEXT("Seventeen ordinary stigmata and three route definitions"),Catalog.Num(),20);
    TSet<FName> Ids;
    for(const auto& Entry:Catalog)
    {
        TestFalse(TEXT("Every ID is valid"),Entry.Id.IsNone());
        TestFalse(TEXT("Every ID is unique"),Ids.Contains(Entry.Id));
        TestFalse(TEXT("Every name is visible"),Entry.Name.IsEmpty());
        TestFalse(TEXT("Every effect is visible"),Entry.Description.IsEmpty());
        Ids.Add(Entry.Id);
        TestNotNull(TEXT("ID resolves"),UEGStigmataCatalog::Find(Entry.Id));
    }
    int32 RouteCount=0;
    for(const FName Id:{FName(TEXT("OriginFlower")),FName(TEXT("Story")),FName(TEXT("Unity"))})
        if(Ids.Contains(Id))++RouteCount;
    TestEqual(TEXT("All three independent final routes exist"),RouteCount,3);
    TestEqual(TEXT("Ordinary catalog has seventeen entries"),Catalog.Num()-RouteCount,17);
    TStrongObjectPtr<UEGRunDefinition> Run(NewObject<UEGRunDefinition>());
    Run->Stages.SetNum(7);
    Run->bRoundShopRules=true;
    Run->bSevenStageRules=true;
    TStrongObjectPtr<UEGMatchManager> Match(NewObject<UEGMatchManager>());
    TestTrue(TEXT("Configure run"),Match->ConfigureRunDefinition(Run.Get()));
    TestTrue(TEXT("Start run"),Match->StartRun());
    TestFalse(TEXT("Cannot start receipt outside reward"),Match->BeginStigmataCommit(TEXT("Gold")));
    TestTrue(TEXT("Enter combat"),Match->EnterCombat());
    TestTrue(TEXT("Resolve combat"),Match->ResolveCombat(true));
    TestTrue(TEXT("Finish settlement"),Match->FinishSettlement());
    TestTrue(TEXT("Enter reward"),Match->EnterReward());
    TestTrue(TEXT("Begin receipt transaction"),Match->BeginStigmataCommit(TEXT("Gold")));
    TestFalse(TEXT("Reentrant completion is rejected"),Match->CompleteReward());
    TestFalse(TEXT("Reentrant next stage is rejected"),Match->NextStage());
    TestFalse(TEXT("Reentrant duplicate receipt is rejected"),Match->BeginStigmataCommit(TEXT("Gold")));
    TestEqual(TEXT("Reentrant corruption mutation is rejected"),Match->AddCorruption(10),0);
    TestTrue(TEXT("Commit receipt and complete reward"),Match->CommitStigmata());
    TestTrue(TEXT("Owned is observable"),Match->HasStigmata(TEXT("Gold")));
    TestFalse(TEXT("Duplicate completion rejected"),Match->CompleteReward());
    TestTrue(TEXT("Advance"),Match->NextStage());
    TestTrue(TEXT("Receipt survives stage change"),Match->HasStigmata(TEXT("Gold")));
    return true;
}
#endif
