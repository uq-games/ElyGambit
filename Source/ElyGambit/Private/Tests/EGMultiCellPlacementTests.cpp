#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMultiCellPlacementTest,"ElyGambit.CardDeployment.MultiCellAtomicity",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMultiCellPlacementTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Board=W->SpawnActor<AEGDeploymentBoard>();Board->Columns=7;Board->Rows=7;Board->PlayerRows=3;Board->NeutralRows=1;
 auto* Large=W->SpawnActor<AEGDeployedPiece>();auto* Other=W->SpawnActor<AEGDeployedPiece>();
 const FIntPoint LargeSize(3,3);
 TestFalse(TEXT("Even footprint rejected"),Board->CanOccupyFootprint({1,1},{2,3}));
 TestFalse(TEXT("Footprint outside board rejected"),Board->CanOccupyFootprint({0,1},LargeSize));
 TestFalse(TEXT("Footprint cannot enter neutral row"),Board->CanOccupyFootprint({1,2},LargeSize));
 TestTrue(TEXT("Other actor occupies candidate cell"),Board->TryOccupyCell({2,2},Other));
 TestFalse(TEXT("One occupied cell rejects whole footprint"),Board->TryOccupyFootprint({1,1},LargeSize,Large));
 TestEqual(TEXT("Failed placement did not partially occupy"),Board->GetOccupiedCount(),1);
 TestTrue(TEXT("Remove blocker"),Board->ReleaseCell({2,2},Other));
 TestTrue(TEXT("Full footprint can be placed"),Board->TryOccupyFootprint({1,1},LargeSize,Large));
 Large->AssignPlacement(Board,{1,1},FGuid::NewGuid());
 for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<3;++X)TestTrue(TEXT("Each cell refers to same actor"),Board->GetOccupant({X,Y})==Large);
 TestEqual(TEXT("Footprint occupies exactly nine cells"),Board->GetOccupiedCount(),9);
 TestFalse(TEXT("One actor cannot acquire a second footprint"),Board->TryOccupyCell({6,1},Large));
 TestFalse(TEXT("Wrong anchor cannot release"),Board->ReleaseFootprint({0,0},Large));
 TestTrue(TEXT("Place movement blocker"),Board->TryOccupyCell({4,1},Other));
 TestFalse(TEXT("Move into blocker is rejected atomically"),Board->TryMoveFootprint(Large,{1,1},{3,1},LargeSize));
 TestTrue(TEXT("Rejected move retains prior footprint"),Board->GetOccupant({0,0})==Large&&Board->GetOccupant({2,2})==Large);
 TestTrue(TEXT("Rejected move retains other actor"),Board->GetOccupant({4,1})==Other);
 TestTrue(TEXT("Overlapping move accepts its own cells"),Board->TryMoveFootprint(Large,{1,1},{2,1},LargeSize));
 Large->AssignPlacement(Board,{2,1},Large->InstanceId);
 TestNull(TEXT("Move releases vacated column"),Board->GetOccupant({0,1}));
 TestTrue(TEXT("Move occupies new column"),Board->GetOccupant({3,1})==Large);
 TestTrue(TEXT("Move rollback is supported"),Board->TryMoveFootprint(Large,{2,1},{1,1},LargeSize));
 Large->AssignPlacement(Board,{1,1},Large->InstanceId);
 TestTrue(TEXT("Release all occupied cells"),Board->ReleaseFootprint({1,1},Large));
 TestEqual(TEXT("Other actor remains"),Board->GetOccupiedCount(),1);
 TestTrue(TEXT("Same actor can redeploy"),Board->TryOccupyFootprint({1,1},LargeSize,Large));
 Large->Destroy();
 for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<3;++X)TestTrue(TEXT("Destroyed actor does not block any occupied cell"),Board->IsCellAvailable({X,Y}));
 TestTrue(TEXT("Surviving blocker remains occupied"),Board->GetOccupant({4,1})==Other);
 return true;
}
#endif
