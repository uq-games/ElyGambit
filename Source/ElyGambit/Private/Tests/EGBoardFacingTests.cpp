#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardFacingTest,"ElyGambit.Board.FacingTransitions",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FEGBoardFacingTest::RunTest(const FString&)
{
 for (const float BoardYaw : {0.f, 73.f})
 {
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
  Instance->InitializeStandalone();
  UWorld* World=Instance->GetWorld();
  ON_SCOPE_EXIT
  {
   World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
   Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
  };
  World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Grid->SetActorRotation(FRotator(0,BoardYaw,0));
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!TestNotNull(TEXT("test game mode"),Mode))continue;
  AEGDemoCombatUnit* Player=nullptr;
  TArray<AEGChessUnit*> Enemies;
  for(const auto& Entry:Mode->GetBattleUnits())if(auto* Unit=Entry.Get())
  {
   if(Unit->GetFaction_Implementation()==EEGFaction::Player)Player=Cast<AEGDemoCombatUnit>(Unit);
   else if(Unit->GetFaction_Implementation()==EEGFaction::Enemy)Enemies.Add(Unit);
  }
  if(!TestNotNull(TEXT("player combat unit"),Player)||!TestTrue(TEXT("enemies spawned"),!Enemies.IsEmpty()))continue;
  const FVector Forward=(Grid->CellToWorld({0,1})-Grid->CellToWorld({0,0})).GetSafeNormal2D();
  TestTrue(TEXT("player initially faces the enemy side"),Player->GetActorForwardVector().Equals(Forward,.001f));
  for(const auto* Enemy:Enemies)
   TestTrue(TEXT("enemy initially faces the player side"),Enemy->GetActorForwardVector().Equals(-Forward,.001f));

  auto* Piece=World->SpawnActor<AEGDeployedPiece>();
  if(!TestNotNull(TEXT("preparation piece"),Piece))continue;
  Piece->ApplyHeroDefinition(Player->GetDefinition());
  const FQuat Offset=Player->GetDefinition()->GetPresentationRotation().Quaternion();
  Piece->SetActorLocation(Grid->CellToWorld({2,1}));
  Piece->SetActorRotation((Player->GetActorQuat()*Offset).Rotator());
  const FTransform Before=Piece->GetActorTransform();
  Player->AttachPiece(Piece);
  TestTrue(TEXT("attachment preserves visible preparation facing"),Piece->GetActorQuat().Equals(Before.GetRotation(),.001f));
  TestTrue(TEXT("model offset exists exactly once after attachment"),Piece->GetRootComponent()->GetRelativeRotation().Quaternion().Equals(Offset,.001f));
  TestTrue(TEXT("combat transition succeeds"),Mode->EnterCombat());
  TestTrue(TEXT("opening transition retains attached facing"),Piece->GetActorQuat().Equals(Before.GetRotation(),.001f));
  const FQuat NewFacing=FRotator(0,BoardYaw+135.f,0).Quaternion();
  Player->SetActorRotation(NewFacing);
  TestTrue(TEXT("attached model follows actor without a second offset"),Piece->GetActorQuat().Equals(NewFacing*Offset,.001f));
  Player->RestorePiece();
  TestTrue(TEXT("restoration returns the complete preparation transform"),Piece->GetActorTransform().Equals(Before,.001f));
 }
 return true;
}
#endif
