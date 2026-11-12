#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Data/EGRunDefinition.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettledDeploymentTest,"ElyGambit.Board.SettledDeploymentPrecision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettledDeploymentTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);
 if(!TestNotNull(TEXT("Production run"),Base))return false;
 GM->RunDefinition=DuplicateObject<UEGRunDefinition>(Base,GM);
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->SetActorLocation(FVector(-6.929577026367269,3.73639099121095,7.875));
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestTrue(TEXT("Deploy starting hero"),!Cards->GetHand().IsEmpty()&&Cards->TryDeployHandCard(Cards->GetHand()[0].InstanceId,{3,2})))return false;
 auto* Piece=Cards->GetPieces()[0];auto* Unit=GM->SpawnRosterHero(Piece->RosterId);auto* Board=Scene->CombatBoard.Get();
 if(!TestNotNull(TEXT("Owned combat incarnation"),Unit))return false;
 const FVector Destination=Board->CellToWorld({3,2});
 Unit->SetActorLocation(Destination+FVector(1,0,0),false,nullptr,ETeleportType::TeleportPhysics);
 Unit->SetActorLocation(Destination+FVector(1.e-8,0,0),false,nullptr,ETeleportType::TeleportPhysics);
 const double Error=FVector::Distance(Unit->GetActorLocation(),Destination);
 TestTrue(TEXT("Fixture reproduces nonzero sub-tolerance settled offset"),Error>0&&Error<UE_KINDA_SMALL_NUMBER);
 TestTrue(TEXT("Settled unit can synchronize without a physical move"),Board->PlaceUnit(Unit,{3,2}));
 TestTrue(TEXT("Repeated synchronization is idempotent"),Board->PlaceUnit(Unit,{3,2}));
 TestTrue(TEXT("Occupancy retains the same unit"),Board->GetOccupant({3,2})==Unit);
 TestFalse(TEXT("Precision handling does not permit neutral deployment"),Board->PlaceUnit(Unit,{3,3}));
 TestFalse(TEXT("Precision handling does not permit enemy deployment"),Board->PlaceUnit(Unit,{3,4}));
 TestTrue(TEXT("Move to seventh column"),Cards->TryMovePlacedPiece(Piece,{6,2}));
 TestTrue(TEXT("Return to settled cell"),Cards->TryMovePlacedPiece(Piece,{3,2}));
 TestTrue(TEXT("Production combat starts after synchronization"),GM->EnterCombat());
 return true;
}
#endif
