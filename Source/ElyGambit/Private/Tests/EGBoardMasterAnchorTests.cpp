#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Actor/EGBoard.h"
#include "Actor/EGArtPiece.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/EGChessGameMode.h"
#include "GameFramework/WorldSettings.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardMasterAnchors,
 "ElyGambit.Presentation.BoardMasterAnchors",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardMasterAnchors::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;
 auto* Board=W->SpawnActor<AEGBoard>();Board->UseLayout(Grid);
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();Scene->Board=Grid;Scene->CombatBoard=Board;
 auto* Camera=W->SpawnActor<ACameraActor>();Scene->FixedCamera=Camera;
 FVector Friendly,Enemy;
 Camera->SetActorRotation(FRotator(0.f,-90.f,0.f));
 TestTrue(TEXT("Anchors resolve against scene camera"),UEGBoardMasterPresentation::GetMasterAnchors(Board,Friendly,Enemy));
 const auto Expected=[&](FVector2D P){return Grid->GetActorTransform().TransformPosition(FVector(P.X,P.Y,0))+FVector(0,0,Board->UnitCenterHeight);};
 auto Geometry=Grid->GetPerimeterGeometry();
 TestTrue(TEXT("Friendly master uses station center"),Friendly.Equals(Expected(Geometry.FriendlyCenter),.1f));
 TestTrue(TEXT("Enemy master uses station center"),Enemy.Equals(Expected(Geometry.EnemyCenter),.1f));
 TestTrue(TEXT("Friendly and enemy are on opposite screen sides"),FVector::DotProduct(Friendly-Grid->GetActorLocation(),Camera->GetActorRightVector())<0&&FVector::DotProduct(Enemy-Grid->GetActorLocation(),Camera->GetActorRightVector())>0);
 Camera->SetActorRotation(FRotator(0.f,90.f,0.f));
 TestTrue(TEXT("Anchors resolve with opposite camera right"),UEGBoardMasterPresentation::GetMasterAnchors(Board,Friendly,Enemy));
 Geometry=Grid->GetPerimeterGeometry();
 TestTrue(TEXT("Friendly follows mirrored station center"),Friendly.Equals(Expected(Geometry.FriendlyCenter),.1f));
 TestTrue(TEXT("Enemy follows mirrored station center"),Enemy.Equals(Expected(Geometry.EnemyCenter),.1f));
 TestTrue(TEXT("Friendly remains screen left when camera reverses"),FVector::DotProduct(Friendly-Grid->GetActorLocation(),Camera->GetActorRightVector())<0&&FVector::DotProduct(Enemy-Grid->GetActorLocation(),Camera->GetActorRightVector())>0);
 Grid->SetActorTransform(FTransform(FRotator(0,37,0),FVector(300,-150,30)));
 TestTrue(TEXT("Anchors resolve for rotated and translated board"),UEGBoardMasterPresentation::GetMasterAnchors(Board,Friendly,Enemy));
 Geometry=Grid->GetPerimeterGeometry();
 TestTrue(TEXT("Friendly follows transformed station"),Friendly.Equals(Expected(Geometry.FriendlyCenter),.1f));
 TestTrue(TEXT("Enemy follows transformed station"),Enemy.Equals(Expected(Geometry.EnemyCenter),.1f));
 TestFalse(TEXT("Missing board is rejected"),UEGBoardMasterPresentation::GetMasterAnchors(nullptr,Friendly,Enemy));
 Board->UseLayout(nullptr);
 Board->UnitCenterHeight=88.f;
 TestTrue(TEXT("Legacy board anchors resolve"),UEGBoardMasterPresentation::GetMasterAnchors(Board,Friendly,Enemy));
 TestTrue(TEXT("Legacy anchor supplies capsule-center height"),FMath::IsNearlyEqual(Friendly.Z,Board->GetActorLocation().Z+88.f));
 TestEqual(TEXT("Missing definition preserves neutral scale"),UEGBoardMasterPresentation::GetDisplayScale(nullptr,nullptr),1.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardMasterRelativeScale,
 "ElyGambit.Presentation.BoardMasterRelativeScale",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardMasterRelativeScale::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();
 FURL URL;W->SetGameMode(URL);
 auto* Mode=W->GetAuthGameMode<AEGChessGameMode>();
 if(!TestNotNull(TEXT("Master fixture game mode"),Mode))return false;
 Mode->bAutoStart=false;
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->CellWidth=500.f;Grid->CellDepth=500.f;
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestNotNull(TEXT("Master fixture board"),Mode->GetBoard()))return false;
 Mode->GetBoard()->UseLayout(Grid);
 auto* Hero=NewObject<UEGHeroDefinition>(GI.Get());
 Hero->MeshScale=FVector(3.f);Hero->PresentationGroundOffset=20.f;
 Hero->PresentationPickRadius=1000.f;Hero->PresentationHeight=100.f;
 auto* Master=W->SpawnActor<AEGArtPiece>();
 if(!TestNotNull(TEXT("Master visual"),Master))return false;
 Master->ApplyHeroDefinition(Hero);
 const float BoardScale=FEGBoardModelSizing::Resolve(Hero,Master);
 const float MasterScale=UEGBoardMasterPresentation::GetDisplayScale(Hero,Master);
 TestTrue(TEXT("Master is 1.5 times this hero's fitted board size"),FMath::IsNearlyEqual(MasterScale,1.5f*BoardScale));
 TestTrue(TEXT("Fixture actually exercises a fitted board size"),BoardScale<.99f);
 Master->SetMasterModelScale(MasterScale);
 TestTrue(TEXT("Model uses source scale multiplied once"),Master->Body->GetRelativeScale3D().Equals(Hero->GetMeshScale()*MasterScale,.001f));
 TestTrue(TEXT("Scaled ground offset follows the model"),FMath::IsNearlyEqual(Master->Body->GetRelativeLocation().Z,Hero->GetPresentationGroundOffset()*MasterScale,.001f));
 TestTrue(TEXT("Master actor root stays at unit scale"),Master->GetActorScale3D().Equals(FVector::OneVector,.001f));
 Master->RefreshAppearance();
 TestTrue(TEXT("Refreshing the appearance retains the master model scale"),Master->Body->GetRelativeScale3D().Equals(Hero->GetMeshScale()*MasterScale,.001f));
 return true;
}
namespace
{
double DistanceToSegment(FVector2D Point,const FEGBoardPerimeterSegment& Segment)
{
 const FVector2D D=Segment.End-Segment.Start;
 const double Squared=D.SizeSquared();
 const double T=Squared>0?FMath::Clamp(FVector2D::DotProduct(Point-Segment.Start,D)/Squared,0.,1.):0.;
 return FVector2D::Distance(Point,Segment.Start+D*T);
}
bool OnAnyPerimeter(FVector2D Point,const TArray<FEGBoardPerimeterSegment>& Segments,double Tolerance=.1)
{
 for(const auto& Segment:Segments)if(DistanceToSegment(Point,Segment)<=Tolerance)return true;
 return false;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardPerimeterGeometryTest,
 "ElyGambit.Presentation.BoardPerimeterGeometryAndLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardPerimeterGeometryTest::RunTest(const FString&)
{
 const float CellWidth=160.f,CellDepth=170.f,HalfWidth=7*CellWidth*.5f,HalfDepth=7*CellDepth*.5f;
 for(const bool bIncreasingRight:{false,true})
 {
  const auto Shape=AEGDeploymentBoard::BuildPerimeterGeometry(7,7,CellWidth,CellDepth,bIncreasingRight);
  if(!TestTrue(TEXT("Perimeter has geometry"),Shape.Segments.Num()>8))continue;
  const float Direction=bIncreasingRight?1.f:-1.f;
  const FVector2D FriendlyExpected(-Direction*(HalfWidth+CellWidth*.5f),-HalfDepth+CellDepth*.5f);
  const FVector2D EnemyExpected(Direction*(HalfWidth+CellWidth*.5f),HalfDepth-CellDepth*.5f);
  TestTrue(TEXT("Friendly station centered outside near screen-left edge"),Shape.FriendlyCenter.Equals(FriendlyExpected,.1f));
  TestTrue(TEXT("Enemy station centered outside far screen-right edge"),Shape.EnemyCenter.Equals(EnemyExpected,.1f));
  for(const auto& Center:{FriendlyExpected,EnemyExpected})
  {
   // Each station is one full square sharing one side with the playable board.
   for(const float X:{-1.f,1.f})for(const float Y:{-1.f,1.f})
    TestTrue(TEXT("Station corner joins the boundary"),OnAnyPerimeter(Center+FVector2D(X*CellWidth*.5f,Y*CellDepth*.5f),Shape.Segments));
   for(const float S:{-1.f,1.f})
   {
    TestTrue(TEXT("Station horizontal side is present"),OnAnyPerimeter(Center+FVector2D(0,S*CellDepth*.5f),Shape.Segments));
    TestTrue(TEXT("Station vertical side is present"),OnAnyPerimeter(Center+FVector2D(S*CellWidth*.5f,0),Shape.Segments));
   }
  }
  const FVector2D RoundedFarLeft(-Direction*HalfWidth,HalfDepth),RoundedNearRight(Direction*HalfWidth,-HalfDepth);
  TestFalse(TEXT("Far screen-left corner is rounded rather than square"),OnAnyPerimeter(RoundedFarLeft,Shape.Segments,1.f));
  TestFalse(TEXT("Near screen-right corner is rounded rather than square"),OnAnyPerimeter(RoundedNearRight,Shape.Segments,1.f));
  for(int32 I=0;I<Shape.Segments.Num();++I)
  {
   const auto& Segment=Shape.Segments[I];
   TestTrue(TEXT("Perimeter segments are finite and nonzero"),!Segment.Start.ContainsNaN()&&!Segment.End.ContainsNaN()&&FVector2D::Distance(Segment.Start,Segment.End)>.01f);
   for(const FVector2D Endpoint:{Segment.Start,Segment.End})
   {
    bool bConnected=false;
    for(int32 J=0;J<Shape.Segments.Num();++J)if(I!=J&&DistanceToSegment(Endpoint,Shape.Segments[J])<.1){bConnected=true;break;}
    TestTrue(TEXT("Each boundary segment connects to another segment"),bConnected);
   }
  }
 }
 TestTrue(TEXT("Invalid geometry input produces no perimeter"),AEGDeploymentBoard::BuildPerimeterGeometry(0,7,CellWidth,CellDepth,true).Segments.IsEmpty());
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();ON_SCOPE_EXIT{GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Grid->CellWidth=CellWidth;Grid->CellDepth=CellDepth;Grid->RefreshVisuals();
 TInlineComponentArray<UStaticMeshComponent*> Before;Grid->GetComponents(Before);
 TArray<TWeakObjectPtr<UStaticMeshComponent>> Boundary;
 UStaticMeshComponent* Pads[2]={nullptr,nullptr};
 for(auto* Mesh:Before)if(Mesh)
 {
  if(Mesh->GetName()==TEXT("BoardMasterPad0"))Pads[0]=Mesh;
  if(Mesh->GetName()==TEXT("BoardMasterPad1"))Pads[1]=Mesh;
 }
 const auto PadGeometry=Grid->GetPerimeterGeometry();
 for(int32 I=0;I<2;++I)if(TestNotNull(TEXT("Station pad exists"),Pads[I]))
 {
  const FVector2D Center=I==0?PadGeometry.FriendlyCenter:PadGeometry.EnemyCenter;
  TestTrue(TEXT("Station pad is centered on shared anchor geometry"),Pads[I]->GetRelativeLocation().Equals(FVector(Center.X,Center.Y,.5f),.1f));
  TestTrue(TEXT("Station pad spans exactly one cell"),Pads[I]->GetRelativeScale3D().Equals(FVector(CellWidth/100.f,CellDepth/100.f,1.f),.01f));
  TestEqual(TEXT("Station pad never collides"),Pads[I]->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
  TestFalse(TEXT("Station pad does not affect navigation"),Pads[I]->CanEverAffectNavigation());
 }
 for(auto* Mesh:Before)if(Mesh&&(Mesh->GetName().StartsWith(TEXT("BoardBoundary"))||Mesh->GetName().StartsWith(TEXT("BoardPerimeterDetail"))))
 {
  Boundary.Add(Mesh);
  TestEqual(TEXT("Presentation boundary never collides"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
  TestFalse(TEXT("Presentation boundary does not affect navigation"),Mesh->CanEverAffectNavigation());
 }
 TestTrue(TEXT("Boundary uses a bounded set of components"),Boundary.Num()>=8&&Boundary.Num()<=64);
 for(int32 I=0;I<3;++I){Grid->OnConstruction(Grid->GetActorTransform());Grid->RefreshVisuals();}
 TInlineComponentArray<UStaticMeshComponent*> After;Grid->GetComponents(After);
 TestEqual(TEXT("Repeated construction does not leak components"),After.Num(),Before.Num());
 for(const auto& Mesh:Boundary)TestTrue(TEXT("Repeated construction retains boundary component identity"),Mesh.IsValid()&&After.Contains(Mesh.Get()));
 for(auto* Pad:Pads)TestTrue(TEXT("Repeated construction retains station pad identity"),IsValid(Pad)&&After.Contains(Pad));
 TestTrue(TEXT("Board query still spans exactly seven by seven"),Grid->Query->GetUnscaledBoxExtent().Equals(FVector(HalfWidth,HalfDepth,2.f),.1f));
 for(int32 Y=0;Y<7;++Y)for(int32 X=0;X<7;++X)
 {
  const FIntPoint C(X,Y);FIntPoint Found;
  TestTrue(TEXT("Playable cell remains addressable"),Grid->TryWorldToCell(Grid->CellToWorld(C),Found)&&Found==C);
  TestEqual(TEXT("Player deployment remains first three rows"),Grid->IsPlayerCell(C),Y<3);
  TestEqual(TEXT("Enemy deployment remains last three rows"),Grid->IsEnemyCell(C),Y>=4);
 }
 const auto Shape=Grid->GetPerimeterGeometry();
 for(const FVector2D Center:{Shape.FriendlyCenter,Shape.EnemyCenter})
 {
  FIntPoint Found;
  TestFalse(TEXT("Decorative station is not a deployable cell"),Grid->TryWorldToCell(Grid->GetActorTransform().TransformPosition(FVector(Center.X,Center.Y,0)),Found));
 }
 return true;
}
#endif
