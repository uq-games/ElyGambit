#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGBoardScene.h"
#include "Game/EGCardManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDeploymentGridTest,"ElyGambit.CardDeployment.BoardCoordinates",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDeploymentGridTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
    UWorld* World=GI->GetWorld();
    AEGDeploymentBoard* Board=World->SpawnActor<AEGDeploymentBoard>();
    Board->SetActorTransform(FTransform(FRotator(0,37,0),FVector(123,-456,80),FVector(1.2,1.2,1.2)));
    for(int32 X=0;X<5;++X)for(int32 Y=0;Y<6;++Y)
    {
        FIntPoint Actual;
        TestTrue(TEXT("Rotated/translated grid hit"),Board->TryWorldToCell(Board->CellToWorld({X,Y}),Actual));
        TestEqual(TEXT("Cell round trip"),Actual,FIntPoint(X,Y));
    }
    FIntPoint Invalid;
    TestFalse(TEXT("Upper edge excluded"),Board->TryWorldToCell(Board->GetActorTransform().TransformPosition({400,0,0}),Invalid));
    TestFalse(TEXT("Lower edge excluded"),Board->TryWorldToCell(Board->GetActorTransform().TransformPosition({-401,0,0}),Invalid));
    AActor* Piece=World->SpawnActor<AEGDeployedPiece>();
    AActor* Decoration=World->SpawnActor<AEGArtPiece>();
    TestFalse(TEXT("Decoration cannot occupy a cell"),Board->TryOccupyCell({0,0},Decoration));
    TestTrue(TEXT("First occupancy"),Board->TryOccupyCell({1,1},Piece));
    TestFalse(TEXT("No double occupancy"),Board->TryOccupyCell({1,1},Piece));
    TestFalse(TEXT("One actor cannot occupy two cells"),Board->TryOccupyCell({2,1},Piece));
    TestTrue(TEXT("Release only matching occupant"),Board->ReleaseCell({1,1},Piece));
    TestTrue(TEXT("Cell reusable"),Board->IsCellAvailable({1,1}));
    TestTrue(TEXT("Occupy again"),Board->TryOccupyCell({1,1},Piece));
    Piece->Destroy();
    TestTrue(TEXT("Destroyed actors do not block"),Board->IsCellAvailable({1,1}));
    Board->HighlightMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessCellHighlight"));
    TestNotNull(TEXT("Generated highlight material exists"),Board->HighlightMaterial.Get());
    Board->RefreshVisuals();Board->SetBoardVisualsVisible(true);
    // A Blueprint component reconstruction can swap its slot while an old MID is cached.
    auto* DisplayedMID=Board->Highlight->CreateDynamicMaterialInstance(0,Board->HighlightMaterial);
    Board->SetHoveredCell({1,1},false);
    TestEqual(TEXT("Occupied cell drives displayed MID red"),DisplayedMID->K2_GetScalarParameterValue(TEXT("IsLegal")),0.f);
    Board->SetHoveredCell({1,1},true);
    TestEqual(TEXT("Empty cell drives displayed MID green"),DisplayedMID->K2_GetScalarParameterValue(TEXT("IsLegal")),1.f);
    GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCardTransactionsTest,"ElyGambit.CardDeployment.CardTransactions",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCardTransactionsTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 auto* B=W->SpawnActor<AEGDeploymentBoard>();auto* M=W->SpawnActor<AEGCardManager>();M->Board=B;
 TestTrue(TEXT("Initialize"),M->InitializeCards());M->ObtainDebugElysiaCard();M->ObtainDebugElysiaCard();
 const auto Hand=M->GetHand();TestFalse(TEXT("Invalid drop preserves card"),M->TryDeployHandCard(Hand[0].InstanceId,{0,5}));
 TestTrue(TEXT("Deploy first"),M->TryDeployHandCard(Hand[0].InstanceId,{0,0}));TestTrue(TEXT("Deploy second"),M->TryDeployHandCard(Hand[1].InstanceId,{1,0}));
 auto* A=Cast<AEGDeployedPiece>(B->GetOccupant({0,0}));auto* Other=B->GetOccupant({1,0});
 TestTrue(TEXT("Exchange friendly occupants"),M->TryMovePlacedPiece(A,{1,0}));TestTrue(TEXT("Both cells remain occupied"),B->GetOccupant({0,0})==Other&&B->GetOccupant({1,0})==A);
 TestFalse(TEXT("Duplicate spent source"),M->TryDeployHandCard(Hand[0].InstanceId,{2,0}));
 TestEqual(TEXT("Free sandbox card has zero sale value"),M->GetSaleValue(Hand[2].InstanceId),int64(0));
 M->SetPreparationEnabled(false);TestFalse(TEXT("Combat locks movement"),M->TryMovePlacedPiece(A,{2,0}));M->SetPreparationEnabled(true);
 A->Destroy();TestTrue(TEXT("Destroy releases occupancy"),B->IsCellAvailable({1,0}));
 TestTrue(TEXT("Remaining hand reusable"),M->TryDeployHandCard(Hand[2].InstanceId,{1,0}));
 auto* Sold=Cast<AEGDeployedPiece>(B->GetOccupant({1,0}));TestTrue(TEXT("Free sandbox piece can be sold"),M->TrySellPlacedPiece(Sold));
 TestTrue(TEXT("Sale releases grid cell"),B->IsCellAvailable({1,0}));TestFalse(TEXT("Duplicate sale rejected"),M->TrySellPlacedPiece(Sold));
 GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return true;
}
#endif
