#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMovingTargetContinuity,
 "ElyGambit.CombatMovement.MovingTargetContinuity",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMovingTargetContinuity::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* Mode=W->GetAuthGameMode<AEGChessGameMode>();if(!TestNotNull(TEXT("Game mode"),Mode))return false;
 Mode->bAutoStart=false;auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 if(!TestNotNull(TEXT("Grid"),Grid))return false;
 Grid->Columns=20;Grid->Rows=8;Grid->PlayerRows=3;Grid->CellWidth=500.f;Grid->CellDepth=500.f;
 W->InitializeActorsForPlay(URL);W->BeginPlay();if(!TestNotNull(TEXT("Board"),Mode->GetBoard()))return false;
 Mode->GetBoard()->UseLayout(Grid);
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());Definition->bBoardMovement=true;Definition->MoveSpeed=300.f;Definition->AttackRange=150.f;
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Mover=W->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),FVector(0,0,88),FRotator::ZeroRotator,Params);
 auto* Enemy=W->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),FVector(1400,0,88),FRotator::ZeroRotator,Params);
 if(!TestTrue(TEXT("Initialized units"),Mover&&Enemy&&Mover->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Player,1)&&Enemy->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 Mover->GetCombat()->SetComponentTickEnabled(false);Enemy->GetCombat()->SetComponentTickEnabled(false);
 Mover->BeginCombat_Implementation();Enemy->BeginCombat_Implementation();
 TArray<TWeakObjectPtr<AEGChessUnit>> Candidates{Mover,Enemy};Mover->GetCombat()->SetCombatants(Candidates);
 // The target advances 10 cm per tick. The original contact point is reached
 // after roughly 30 ticks, while the target remains outside attack range.
 float TotalTravel=0.f,WorstTravel=MAX_flt;
 for(int32 Tick=0;Tick<34;++Tick)
 {
  Enemy->SetActorLocation(Enemy->GetActorLocation()+FVector(10,0,0),false);
  const FVector Before=Mover->GetActorLocation();Mover->GetCombat()->TickComponent(.1f,LEVELTICK_All,nullptr);
  const float Travel=FVector::Dist2D(Before,Mover->GetActorLocation());
  TotalTravel+=Travel;WorstTravel=FMath::Min(WorstTravel,Travel);
  if(!TestTrue(FString::Printf(TEXT("Moving target tick %d stays continuous (%.2f cm)"),Tick,Travel),Travel>25.f&&Travel<30.1f))return false;
 }
 AddInfo(FString::Printf(TEXT("Configured 300 cm/s; moving-target actual %.2f cm/s; slowest frame %.2f cm over 0.1 s."),TotalTravel/3.4f,WorstTravel));
 // A segment accepted by the planner must not squeeze through a capsule edge
 // that the full-size physical sweep rejects.
 const FVector Center(500,0,88);Enemy->SetActorLocation(Center,false);
 const FVector Start(0,499.7f,88),End(1000,499.7f,88);
 TestFalse(TEXT("A sub-centimetre capsule overlap is not planned as free"),Mover->GetCombat()->IsBoardPathClear(Start,End));
 const FVector Border(500,499.7f,88);
 TestFalse(TEXT("The same overlapping boundary cannot be a landing"),Mover->GetCombat()->IsBoardLocationFree(Border));
 TestFalse(TEXT("The same overlapping boundary cannot be a route point"),Mover->GetCombat()->IsBoardPathClear(Border,Border));
 Mover->SetActorLocation(FVector(0,0,88),false);Enemy->SetActorLocation(FVector(499,0,88),false);
 TArray<FVector> Escape{FVector(-50,0,88)};
 TestTrue(TEXT("Initial overlap may move outward"),Mover->GetCombat()->AdvanceBoardRoute(Escape,50.f));
 TestTrue(TEXT("Outward escape actually moves"),FMath::IsNearlyEqual(Mover->GetActorLocation().X,-50.f,.1f));
 Enemy->SetActorLocation(FVector(500,0,88),false);
 Mover->SetActorLocation(FVector(0,0,88),false);
 TArray<FVector> TangentOutward{FVector(-50,0,88)};
 TestTrue(TEXT("Exactly touching capsules may move outward"),Mover->GetCombat()->AdvanceBoardRoute(TangentOutward,50.f));
 TestTrue(TEXT("Touching outward move covers 50 cm"),Mover->GetActorLocation().Equals(FVector(-50,0,88),.1f));
 Mover->SetActorLocation(FVector(0,0,88),false);
 TArray<FVector> TangentSideways{FVector(0,50,88)};
 TestTrue(TEXT("Exactly touching capsules may move along the tangent"),Mover->GetCombat()->AdvanceBoardRoute(TangentSideways,50.f));
 TestTrue(TEXT("Tangential move covers 50 cm"),Mover->GetActorLocation().Equals(FVector(0,50,88),.1f));
 Mover->SetActorLocation(FVector(0,0,88),false);Enemy->SetActorLocation(FVector(1000,1000,88),false);
 TArray<FVector> Waypoints{FVector(10,0,88),FVector(200,0,88)};
 TestTrue(TEXT("Sub-millimetre remaining travel does not report a block"),Mover->GetCombat()->AdvanceBoardRoute(Waypoints,10.005f));
 TestTrue(TEXT("Next waypoint survives the exhausted budget"),Waypoints.Num()==1&&Waypoints[0].Equals(FVector(200,0,88),.1f));
 TestTrue(TEXT("First waypoint reached without a reset"),Mover->GetActorLocation().Equals(FVector(10,0,88),.02f));
 TestTrue(TEXT("Following frame continues the retained route"),Mover->GetCombat()->AdvanceBoardRoute(Waypoints,30.f));
 TestTrue(TEXT("Following frame travels its full budget"),Mover->GetActorLocation().Equals(FVector(40,0,88),.05f));
 Mover->SetActorLocation(FVector(0,0,88),false);
 TArray<FVector> LastWaypoint{FVector(10,0,88)};float Residual=-1.f;
 TestTrue(TEXT("Last waypoint consumes the budget successfully"),Mover->GetCombat()->AdvanceBoardRoute(LastWaypoint,10.005f,nullptr,nullptr,&Residual));
 TestTrue(TEXT("Last waypoint leaves no false sub-millimetre retry budget"),LastWaypoint.IsEmpty()&&Residual==0.f);
 // Production chase Tick, not injected velocity: changing Delta and live GAS
 // movement speed must produce the corresponding continuous displacement.
 for(const float Speed:{350.f,550.f,650.f})
 {
  const FVector RouteStart(-3000.37f,-700.21f,88);const FVector Direction=FVector(.937f,.349f,0).GetSafeNormal2D();
  Mover->SetActorLocation(RouteStart,false);Enemy->SetActorLocation(FVector(2500.12f,100.42f,88),false);
  TArray<FVector> Multiple;
  for(const float Length:{300.13f,600.67f,900.19f,1300.83f,1800.71f,2700.23f})Multiple.Add(RouteStart+Direction*Length);
  float MultiActual=0.f,MultiExpected=0.f;
  for(int32 Tick=0;Tick<120;++Tick)
  {
   const float Delta=Tick%3==0?1.f/60.f:Tick%3==1?1.f/37.f:1.f/53.f;
   const float Expected=Speed*Delta;const FVector Before=Mover->GetActorLocation();
   if(!TestTrue(TEXT("Variable-frame multi-waypoint advancement succeeds"),Mover->GetCombat()->AdvanceBoardRoute(Multiple,Expected)))return false;
   const float Actual=FVector::Dist2D(Before,Mover->GetActorLocation());MultiActual+=Actual;MultiExpected+=Expected;
   if(!TestTrue(FString::Printf(TEXT("Route %.0f cm/s frame %d preserves budget (%.3f/%.3f)"),Speed,Tick,Actual,Expected),Actual>Expected*.95f&&Actual<Expected*1.001f))return false;
  }
  TestTrue(TEXT("Multi-waypoint total remains within 1 cm"),FMath::Abs(MultiActual-MultiExpected)<1.f);
  Mover->SetActorLocation(FVector(-2500.37f,-700.21f,88),false);
  Enemy->SetActorLocation(FVector(2500.12f,100.42f,88),false);
  Mover->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMoveSpeedAttribute(),Speed);
  Mover->GetCombat()->ResetCombat();float ActualTotal=0.f,ExpectedTotal=0.f,MinRatio=MAX_flt;
  for(int32 Tick=0;Tick<120;++Tick)
  {
   const float Delta=Tick%3==0?1.f/60.f:Tick%3==1?1.f/37.f:1.f/53.f;
   const float Expected=Speed*Delta;const FVector Before=Mover->GetActorLocation();
   Mover->GetCombat()->TickComponent(Delta,LEVELTICK_All,nullptr);
   const float Actual=FVector::Dist2D(Before,Mover->GetActorLocation());
   ActualTotal+=Actual;ExpectedTotal+=Expected;MinRatio=FMath::Min(MinRatio,Actual/Expected);
   if(!TestTrue(FString::Printf(TEXT("Live %.0f cm/s chase frame %d preserves budget (%.3f/%.3f)"),Speed,Tick,Actual,Expected),Actual>Expected*.95f&&Actual<Expected*1.001f))return false;
  }
  AddInfo(FString::Printf(TEXT("Live %.0f cm/s variable-frame pursuit actual %.2f cm expected %.2f cm; worst ratio %.4f."),Speed,ActualTotal,ExpectedTotal,MinRatio));
  TestTrue(TEXT("Variable-frame chase total remains within 1 cm"),FMath::Abs(ActualTotal-ExpectedTotal)<1.f);
 }
 return true;
}
#endif
