#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Misc/ScopeExit.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Data/EGRunDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNeutralBoardTest,"ElyGambit.Board.NeutralRowDeployment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNeutralBoardTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();auto* Board=W->SpawnActor<AEGBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Board->UseLayout(Grid);
 for(int32 Y=0;Y<7;++Y)for(int32 X=0;X<7;++X)
 {
  const FIntPoint Cell(X,Y);FIntPoint RoundTrip;
  TestTrue(TEXT("All 49 cells remain navigable board geometry"),Grid->IsCellValid(Cell));
  TestTrue(TEXT("World-to-cell mapping includes neutral row"),Grid->TryWorldToCell(Grid->CellToWorld(Cell),RoundTrip)&&RoundTrip==Cell);
  TestEqual(TEXT("Player has exactly three rows"),Board->IsDeploymentCell(Cell,EEGFaction::Player),Y<3);
  TestEqual(TEXT("Enemy has exactly three rows"),Board->IsDeploymentCell(Cell,EEGFaction::Enemy),Y>3);
 }
 auto* Original=EGProductionTestAssets::ResolveRun(*this);
 if(!TestNotNull(TEXT("Production run exists"),Original))return false;
 auto* Run=DuplicateObject<UEGRunDefinition>(Original,GetTransientPackage());FString Error;
 TestTrue(TEXT("Existing enemy placements valid with neutral row"),Run->ValidateConfiguration(Grid,Error));
 Run->Stages[0].Enemies[0].Cell=FIntPoint(1,3);
 TestFalse(TEXT("Run validation rejects enemy spawning in neutral row"),Run->ValidateConfiguration(Grid,Error));
 Grid->Rows=6;Grid->Columns=6;Grid->NeutralRows=0;
 TestTrue(TEXT("Legacy enemy row remains deployable when neutral count is zero"),Board->IsDeploymentCell(FIntPoint(1,3),EEGFaction::Enemy));
 TestFalse(TEXT("Out-of-grid cell rejected"),Grid->IsEnemyCell(FIntPoint(6,4)));
 return true;
}
#endif
