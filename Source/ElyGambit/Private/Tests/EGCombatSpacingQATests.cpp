#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGPieceTestGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/CapsuleComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGBoardModelSizing.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSpacingVillVBarrels,
 "ElyGambit.CombatSpacing.VillVDevicesSingleCell",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSpacingVillVBarrels::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 // Values read from the production layout. This is a standalone controlled fixture,
 // not a production-map or visible-rendering test.
 Grid->CellWidth=499.9605407714844f;Grid->CellDepth=500.6787414550781f;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Authoritative fixture game mode"),Mode)||!TestTrue(TEXT("Enter fixture battle"),Mode->EnterCombat()))return false;
 // Opening is completed explicitly to isolate the combat geometry and hit path.
 Mode->BattleRules->CompleteOpeningForTests();
 for(auto* Unit:Mode->BattleRules->Units())if(Unit&&Unit->GetCombat())Unit->GetCombat()->SetComponentTickEnabled(false);
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
 if(!TestNotNull(TEXT("Production Vill-V card"),Card)||!TestNotNull(TEXT("Vill-V definition"),Card->HeroDefinition.Get()))return false;
 auto* Pilot=Mode->SpawnBattleSummon(Card->HeroDefinition,3,Grid->CellToWorld({6,6})+FVector(0,0,88),EEGFaction::Enemy);
 if(!TestNotNull(TEXT("Controlled enemy pilot"),Pilot))return false;
 const float PilotBoardScale=FEGBoardModelSizing::Resolve(Pilot->GetDefinition(),Pilot);
 AddInfo(FString::Printf(TEXT("Production Vill-V pilot source MeshScale %s, resolved board multiplier %.5f."),
  *Pilot->GetDefinition()->GetMeshScale().ToString(),PilotBoardScale));
 TestTrue(TEXT("Vill-V pilot retains original board multiplier"),FMath::IsNearlyEqual(PilotBoardScale,1.f,.0001f));
 TestTrue(TEXT("Vill-V pilot production source MeshScale remains three"),Pilot->GetDefinition()->GetMeshScale().Equals(FVector(3.f),.001f));
 Pilot->GetHeroSkill()->Configure(Mode->BattleRules,3,false);
 auto* Mechanism=Pilot->FindComponentByClass<UEGVillVMechanismComponent>();
 if(!TestNotNull(TEXT("Pilot mechanism"),Mechanism)||
    !TestTrue(TEXT("Distinct free cells create devices"),Mechanism->StartEnemyLayout(Mode->BattleRules,{0,6},{1,6})))return false;
 Mechanism->SetComponentTickEnabled(false);
 TestFalse(TEXT("Vill-V body never occupies a combat cell"),UEGCombatComponent::IsSpaceOccupant(Pilot));
 TestEqual(TEXT("Vill-V pilot collision stays disabled"),Pilot->GetCapsuleComponent()->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
 auto* Rules=Cast<UEGVillVSkillDefinition>(Pilot->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Production mechanism rules"),Rules))return false;
 AEGChessUnit* Blocker=nullptr;
 for(auto* Unit:Mode->BattleRules->Units())
  if(Unit&&Unit!=Pilot&&!Unit->IsAuxiliaryEmplacement()&&Unit->GetFaction_Implementation()==EEGFaction::Player){Blocker=Unit;break;}
 if(!TestNotNull(TEXT("Living hero used as a controlled blocker"),Blocker))return false;
 Blocker->GetCombat()->SetComponentTickEnabled(false);
 const float OneCellRadius=FMath::Min(Grid->CellWidth,Grid->CellDepth)*.5f;
 TestTrue(TEXT("Hero uses half a cell as blocking radius"),FMath::IsNearlyEqual(UEGCombatComponent::BlockingRadius(Blocker),OneCellRadius,.1f));
 TestTrue(TEXT("Hero capsule fits one cell"),FMath::IsNearlyEqual(Blocker->GetCapsuleComponent()->GetScaledCapsuleRadius(),OneCellRadius,.1f));
 for(int32 Index=0;Index<2;++Index)
 {
  auto* Device=Mechanism->GetDevice(Index);
  if(!TestNotNull(TEXT("Independent device"),Device))continue;
  TestTrue(TEXT("Device remains a blocking occupant"),UEGCombatComponent::IsSpaceOccupant(Device));
  const float DeviceBoardScale=FEGBoardModelSizing::Resolve(Device->GetDefinition(),Device);
  AddInfo(FString::Printf(TEXT("Production device %d source MeshScale %s, resolved board multiplier %.5f."),Index,
   *Device->GetDefinition()->GetMeshScale().ToString(),DeviceBoardScale));
  TestTrue(TEXT("Production device retains original board multiplier"),FMath::IsNearlyEqual(DeviceBoardScale,1.f,.0001f));
  TestTrue(TEXT("Production device source MeshScale remains three"),Device->GetDefinition()->GetMeshScale().Equals(FVector(3.f),.001f));
  Device->SetActorRotation(FRotator::ZeroRotator);
  TestTrue(TEXT("Device uses one-cell blocking radius"),FMath::IsNearlyEqual(UEGCombatComponent::BlockingRadius(Device),OneCellRadius,.1f));
  TestTrue(TEXT("Device capsule fits one cell"),FMath::IsNearlyEqual(Device->GetCapsuleComponent()->GetScaledCapsuleRadius(),OneCellRadius,.1f));
  if(!TestTrue(TEXT("Device has production idle"),Rules->DeviceIdles.IsValidIndex(Index)))continue;
  auto* Idle=Rules->DeviceIdles[Index].LoadSynchronous();
  if(!TestNotNull(TEXT("Device idle loads"),Idle))continue;
  Device->ShowDevice(Idle,0.f);
  // Pose evaluation is advanced naturally; no damage or animation notify is injected.
  ++GFrameCounter;World->Tick(LEVELTICK_All,.025f);
  FVector Muzzle;
  if(!TestTrue(TEXT("Source device muzzle is available"),Device->Muzzle(Muzzle)))continue;
  // Isolate the device on the board. Its visible muzzle may extend beyond the
  // base, but only the one-cell capsule participates in placement and routes.
  const FVector Center=Grid->CellToWorld({3,3})+FVector(0,0,88);
  for(auto* Unit:Mode->BattleRules->Units())
   if(Unit&&Unit!=Device&&Unit!=Blocker)
    Unit->SetActorLocation(FVector(8000,8000,88),false,nullptr,ETeleportType::TeleportPhysics);
  Device->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
  ++GFrameCounter;World->Tick(LEVELTICK_All,.025f);
  if(!TestTrue(TEXT("Source muzzle remains available after moving"),Device->Muzzle(Muzzle)))continue;
  FVector Forward=(Muzzle-Center).GetSafeNormal2D();
  if(!TestTrue(TEXT("Muzzle direction is nonzero"),!Forward.IsNearlyZero()))continue;
  TArray<TWeakObjectPtr<AEGChessUnit>> Obstacles;Obstacles.Add(Device);
  auto* Combat=Blocker->GetCombat();Combat->SetCombatants(Obstacles);
  Blocker->SetActorLocation(Center-Forward*900.f,false,nullptr,ETeleportType::TeleportPhysics);
  const float BothRadii=OneCellRadius+UEGCombatComponent::BlockingRadius(Blocker);
  const FVector Near=Center+Forward*(BothRadii-10.f);
  const FVector Clear=Center+Forward*(BothRadii+15.f);
  const FVector Further=Center+Forward*(BothRadii+100.f);
  TestFalse(TEXT("Device base prevents overlapping placement"),Combat->IsBoardLocationFree(Near));
  TestTrue(TEXT("Placement beyond one-cell base is clear in muzzle direction"),Combat->IsBoardLocationFree(Clear));
  TestTrue(TEXT("Route along the muzzle outside the base is clear"),Combat->IsBoardPathClear(Clear,Further));
  TestFalse(TEXT("Route through device base remains blocked"),Combat->IsBoardPathClear(Center-Forward*900.f,Further));
  const float AttackRange=Combat->GetEffectiveAttackRange(Device);
  Blocker->SetActorLocation(Center+Forward*(AttackRange-1.f),false,nullptr,ETeleportType::TeleportPhysics);
  TestTrue(TEXT("Device is in normal attack range inside the center-distance limit"),Combat->IsInAttackRange(Device));
  Blocker->SetActorLocation(Center+Forward*(AttackRange+2.f),false,nullptr,ETeleportType::TeleportPhysics);
  TestFalse(TEXT("Exposed muzzle does not extend the attack range"),Combat->IsInAttackRange(Device));

 }
 // Controlled positions and candidate list; no health, damage, action or notify
 // injection. The actual AI, movement, montage and combat execution must hit.
 auto* TargetDevice=Mechanism->GetDevice(0);
 if(TestNotNull(TEXT("Device for natural chase"),TargetDevice)&&Rules->DeviceIdles.IsValidIndex(0))
 {
  const FVector Center=Grid->CellToWorld({3,3})+FVector(0,0,88);
  TargetDevice->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
  TargetDevice->SetActorRotation(FRotator::ZeroRotator);
  TargetDevice->ShowDevice(Rules->DeviceIdles[0].LoadSynchronous(),0.f);
  for(auto* Unit:Mode->BattleRules->Units())
   if(Unit&&Unit!=Blocker&&Unit!=TargetDevice&&Unit!=Pilot)
    Unit->SetActorLocation(FVector(8000.f,8000.f,88.f),false,nullptr,ETeleportType::TeleportPhysics);
  FVector Muzzle;
  FVector Forward=FVector::ForwardVector;
  if(TargetDevice->Muzzle(Muzzle))
  {Forward=(Muzzle-Center).GetSafeNormal2D();if(Forward.IsNearlyZero())Forward=FVector::ForwardVector;}
  Blocker->SetActorLocation(Center+Forward*(2.f*OneCellRadius+350.f),false,nullptr,ETeleportType::TeleportPhysics);
  TArray<TWeakObjectPtr<AEGChessUnit>> Candidates;Candidates.Add(TargetDevice);
  Blocker->GetCombat()->SetCombatants(Candidates);Blocker->GetCombat()->ResetCombat();
  Blocker->GetCombat()->SetComponentTickEnabled(true);
  const float Before=TargetDevice->GetAttributes()->GetHealth();
  for(int32 Frame=0;Frame<600&&TargetDevice->GetAttributes()->GetHealth()>=Before;++Frame)
  {++GFrameCounter;World->Tick(LEVELTICK_All,.025f);}
  TestTrue(TEXT("Natural chase and montage can damage the device at its base"),TargetDevice->GetAttributes()->GetHealth()<Before);
 }
 // Probe the same board, endpoints, and obstacles with an ordinary unit and
 // the Vill-V pilot. Only the destination device is ignored for docking;
 // the other device base remains an obstacle throughout.
 auto* Destination=Mechanism->GetDevice(0);
 auto* IdleDevice=Mechanism->GetDevice(1);
 if(TestNotNull(TEXT("Route destination device"),Destination)&&TestNotNull(TEXT("Idle route blocker"),IdleDevice))
 {
  const FVector Center=Grid->CellToWorld({3,3})+FVector(0,0,88);
  const FVector Start=Center+FVector(-1000,0,0),Goal=Center+FVector(1000,0,0);
  Destination->SetActorLocation(Goal,false,nullptr,ETeleportType::TeleportPhysics);
  IdleDevice->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
  TArray<TWeakObjectPtr<AEGChessUnit>> Obstacles;Obstacles.Add(Destination);Obstacles.Add(IdleDevice);
  for(AEGChessUnit* Mover:{Blocker,Pilot})
  {
   Mover->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
   Mover->GetCombat()->SetCombatants(Obstacles);
   auto* Combat=Mover->GetCombat();
   TestFalse(TEXT("Every other device blocks the direct route"),Combat->IsBoardPathClear(Start,Goal,Destination));
   TArray<FVector> Route;FVector Chosen;
   if(TestTrue(TEXT("Mover can route around an idle device"),Combat->PlanBoardRoute({Goal},Route,Chosen,Destination)))
   {
    TestTrue(TEXT("Route reaches the requested destination"),Chosen.Equals(Goal,.1f)&&!Route.IsEmpty()&&Route.Last().Equals(Goal,.1f));
    FVector Previous=Start;
    for(const FVector Point:Route)
    {
     TestTrue(TEXT("Route segment avoids idle device base"),Combat->IsBoardPathClear(Previous,Point,Destination));
     Previous=Point;
    }
   }
   TArray<FVector> Blocked;FVector Unused;
   TestFalse(TEXT("An occupied device base is not a legal destination"),Combat->PlanBoardRoute({Center},Blocked,Unused,Destination));
  }
  IdleDevice->SetActorLocation(Center+FVector(0,1200,0),false,nullptr,ETeleportType::TeleportPhysics);
  for(AEGChessUnit* Mover:{Blocker,Pilot})
  {
   TestTrue(TEXT("Moving the obstacle clears the direct route"),Mover->GetCombat()->IsBoardPathClear(Start,Goal,Destination));
   TArray<FVector> Route;FVector Chosen;
   TestTrue(TEXT("Both movers can replan after obstacle changes"),Mover->GetCombat()->PlanBoardRoute({Goal},Route,Chosen,Destination));
  }
 }
 // Confirm physical sweeps as well as route queries in both directions.
 // Ordinary same-side heroes continue to block each other.
 if(IdleDevice)
 {
  auto* Ally=Mode->SpawnBattleSummon(Blocker->GetDefinition(),3,Grid->CellToWorld({5,5})+FVector(0,0,88),EEGFaction::Enemy);
  auto* Ally2=Mode->SpawnBattleSummon(Blocker->GetDefinition(),3,Grid->CellToWorld({5,4})+FVector(0,0,88),EEGFaction::Enemy);
  if(TestNotNull(TEXT("Friendly ordinary unit"),Ally)&&TestNotNull(TEXT("Second friendly ordinary unit"),Ally2))
  {
   Ally->GetCombat()->SetComponentTickEnabled(false);Ally2->GetCombat()->SetComponentTickEnabled(false);
   for(auto* Unit:Mode->BattleRules->Units())if(Unit&&Unit!=Ally&&Unit!=Ally2&&Unit!=IdleDevice&&Unit!=Blocker)
    Unit->SetActorLocation(FVector(8000,8000,88),false,nullptr,ETeleportType::TeleportPhysics);
   const FVector Center=Grid->CellToWorld({3,3})+FVector(0,0,88);
   IdleDevice->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
   // The earlier natural chase leaves this enemy near the centre; keep it out
   // of the friendly sweep until the explicit enemy-blocking check below.
   Blocker->SetActorLocation(Center+FVector(0,-1200,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally2->SetActorLocation(Center+FVector(0,1200,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally->SetActorLocation(Center+FVector(-1000,0,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally->RefreshBoardCollision();IdleDevice->RefreshBoardCollision();Ally2->RefreshBoardCollision();Blocker->RefreshBoardCollision();
   FHitResult Hit;
   Ally->SetActorLocation(Center+FVector(1000,0,0),true,&Hit);
   AddInfo(FString::Printf(TEXT("Friendly hero sweep blocker=%s at %s"),*GetNameSafe(Hit.GetActor()),*Ally->GetActorLocation().ToString()));
   TestTrue(TEXT("Friendly hero is physically blocked by device"),Hit.bBlockingHit&&Hit.GetActor()==IdleDevice);
   TestFalse(TEXT("Friendly hero cannot reach the other side through device"),Ally->GetActorLocation().Equals(Center+FVector(1000,0,0),.1f));
   IdleDevice->SetActorLocation(Center+FVector(-1000,0,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
   IdleDevice->SetActorLocation(Center+FVector(1000,0,0),true,&Hit);
   AddInfo(FString::Printf(TEXT("Friendly device sweep blocker=%s at %s"),*GetNameSafe(Hit.GetActor()),*IdleDevice->GetActorLocation().ToString()));
   TestTrue(TEXT("Friendly device is physically blocked by hero"),Hit.bBlockingHit&&Hit.GetActor()==Ally);
   TestFalse(TEXT("Friendly device cannot reach the other side through hero"),IdleDevice->GetActorLocation().Equals(Center+FVector(1000,0,0),.1f));
   IdleDevice->SetActorLocation(Center+FVector(0,-1200,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally2->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
   Ally->SetActorLocation(Center+FVector(-1000,0,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally->SetActorLocation(Center+FVector(1000,0,0),true,&Hit);
   TestTrue(TEXT("Ordinary friendly heroes still physically block"),Hit.bBlockingHit&&Hit.GetActor()==Ally2);
   // The stopped ordinary hero is still on the next sweep's path. Move it
   // aside so the enemy test identifies the device itself as the blocker.
   Ally->SetActorLocation(Center+FVector(0,1800,0),false,nullptr,ETeleportType::TeleportPhysics);
   Ally2->SetActorLocation(Center+FVector(0,1200,0),false,nullptr,ETeleportType::TeleportPhysics);
   IdleDevice->SetActorLocation(Center,false,nullptr,ETeleportType::TeleportPhysics);
   Blocker->SetActorLocation(Center+FVector(-1000,0,0),false,nullptr,ETeleportType::TeleportPhysics);
   Blocker->SetActorLocation(Center+FVector(1000,0,0),true,&Hit);
   AddInfo(FString::Printf(TEXT("Enemy hero sweep blocker=%s at %s"),*GetNameSafe(Hit.GetActor()),*Blocker->GetActorLocation().ToString()));
   TestTrue(TEXT("Enemy hero is physically blocked by device"),Hit.bBlockingHit&&Hit.GetActor()==IdleDevice);
   TestFalse(TEXT("Ordinary ally has no device movement ignore"),
    Ally->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(IdleDevice)||IdleDevice->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Ally));
   Pilot->RefreshBoardCollision();IdleDevice->RefreshBoardCollision();
   TestTrue(TEXT("Pilot and owned device register movement ignore on both sides"),
    Pilot->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(IdleDevice)&&IdleDevice->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Pilot));
   Pilot->SetRuntimeState(EEGUnitState::Idle);IdleDevice->RefreshBoardCollision();
   TestFalse(TEXT("Pilot leaving combat removes its owned device ignore"),Pilot->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(IdleDevice));
   TestFalse(TEXT("Device drops its ignore when its pilot leaves combat"),IdleDevice->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Pilot));
  }
 }
 const FGuid Round=Mode->BattleRules->GetRoundId();
 TestTrue(TEXT("Battle has an authoritative round identity"),Round.IsValid()&&Pilot->HasAuthority());
 TWeakObjectPtr<AEGVillVEmplacement> First=Mechanism->GetDevice(0),Second=Mechanism->GetDevice(1);
 Mode->BattleRules->Stop();
 TestFalse(TEXT("Round identity invalidated on stop"),Mode->BattleRules->GetRoundId().IsValid());
 TestFalse(TEXT("First device released on stop"),First.IsValid());
 TestFalse(TEXT("Second device released on stop"),Second.IsValid());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSpacingDeferredLanding,
 "ElyGambit.CombatSpacing.DeferredLandingLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSpacingDeferredLanding::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Grid->CellWidth=500;Grid->CellDepth=500;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Landing fixture mode"),Mode)||!TestTrue(TEXT("Landing fixture battle"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 AEGChessUnit* Unit=nullptr;AEGChessUnit* Other=nullptr;
 for(auto* U:Mode->BattleRules->Units())
 {
  if(!U)continue;
  U->GetCombat()->SetComponentTickEnabled(false);
  if(U->GetFaction_Implementation()==EEGFaction::Player)Unit=U;
  else if(!Other)Other=U;
 }
 if(!TestNotNull(TEXT("Landing unit"),Unit)||!TestNotNull(TEXT("Other combatant"),Other))return false;
 auto* Blocker=World->SpawnActor<AActor>();auto* Capsule=NewObject<UCapsuleComponent>(Blocker);
 Blocker->SetRootComponent(Capsule);Capsule->InitCapsuleSize(10000.f,10000.f);
 Capsule->SetCollisionObjectType(ECC_Pawn);Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Capsule->SetCollisionResponseToAllChannels(ECR_Block);Capsule->RegisterComponent();Blocker->SetActorLocation(FVector(0,0,88));
 auto* Combat=Unit->GetCombat();const FGuid OldRound=Mode->BattleRules->GetRoundId();
 const FVector Desired=Grid->CellToWorld({3,3})+FVector(0,0,88);int32 Completed=0;
 Unit->GetHeroSkill()->bInFlight=true;Unit->RefreshBoardCollision();
 TestFalse(TEXT("Fully blocked board defers landing"),Combat->CompleteOrDeferBoardLanding(Desired,OldRound,[&](){++Completed;Unit->GetHeroSkill()->bInFlight=false;}));
 TestTrue(TEXT("Blocked landing remains pending"),Combat->IsBoardLandingPending());
 Combat->TickComponent(.24f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("No callback while board remains blocked"),Completed,0);
 Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Combat->TickComponent(.02f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("A freed landing runs its callback exactly once"),Completed,1);
 TestFalse(TEXT("Completed landing clears pending"),Combat->IsBoardLandingPending());
 Combat->TickComponent(.5f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Further ticks cannot repeat the callback"),Completed,1);

 Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 Unit->GetHeroSkill()->bInFlight=true;Unit->RefreshBoardCollision();
 TestFalse(TEXT("Second blocked landing waits"),Combat->CompleteOrDeferBoardLanding(Desired,OldRound,[&](){++Completed;}));
 TestTrue(TEXT("Second request is pending"),Combat->IsBoardLandingPending());
 Mode->BattleRules->Stop();
 TestFalse(TEXT("Stopping the round cancels pending landing"),Combat->IsBoardLandingPending());
 Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Combat->TickComponent(.5f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Stopped round cannot deliver callback"),Completed,1);

 Mode->BattleRules->Start({});Mode->BattleRules->CompleteOpeningForTests();
 const FGuid NewRound=Mode->BattleRules->GetRoundId();
 TestTrue(TEXT("New round has a distinct identity"),NewRound.IsValid()&&NewRound!=OldRound);
 Unit->GetHeroSkill()->bInFlight=true;Unit->RefreshBoardCollision();
 FVector Reservation;
 if(TestTrue(TEXT("New round reserves a free landing"),Combat->ReserveBoardLanding(Desired,Reservation)))
 {
  Combat->ReleaseBoardLanding(OldRound);
  TestFalse(TEXT("Stale round cannot release new reservation"),Other->GetCombat()->IsBoardLocationFree(Reservation));
  Combat->ReleaseBoardLanding(NewRound);
  TestTrue(TEXT("Current round releases its reservation"),Other->GetCombat()->IsBoardLocationFree(Reservation));
 }
 Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
 TestFalse(TEXT("Death fixture waits when blocked"),Combat->CompleteOrDeferBoardLanding(Desired,NewRound,[&](){++Completed;}));
 TestTrue(TEXT("Death fixture is pending"),Combat->IsBoardLandingPending());
 Unit->Die();
 TestFalse(TEXT("Death clears pending landing"),Combat->IsBoardLandingPending());
 Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Combat->TickComponent(.5f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Death cannot deliver pending callback"),Completed,1);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVBlockedTransferLifecycle,
 "ElyGambit.CombatSpacing.VillVBlockedTransferLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVBlockedTransferLifecycle::RunTest(const FString&)
{
 // Three independent controlled worlds: release the obstruction, destroy the
 // intended destination, or stop the round. The production animations and
 // montage notifies drive the switch; the obstruction alone is injected.
 for(int32 Case=0;Case<3;++Case)
 {
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
  UWorld* World=Instance->GetWorld();
  ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
  World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Grid->CellWidth=499.9605407714844f;Grid->CellDepth=500.6787414550781f;
  World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!TestNotNull(TEXT("Transfer fixture mode"),Mode)||!TestTrue(TEXT("Transfer fixture enters combat"),Mode->EnterCombat()))return false;
  Mode->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:Mode->BattleRules->Units())if(Unit)
  {Unit->GetCombat()->SetComponentTickEnabled(false);Unit->SetActorLocation(FVector(8000.f,8000.f,88.f),false);}
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
  if(!TestNotNull(TEXT("Transfer uses production Vill-V"),Card)||!Card->HeroDefinition)return false;
  auto* Pilot=Mode->SpawnBattleSummon(Card->HeroDefinition,3,Grid->CellToWorld({6,6})+FVector(0,0,88),EEGFaction::Enemy);
  if(!TestNotNull(TEXT("Transfer pilot"),Pilot))return false;
  Pilot->GetCombat()->SetComponentTickEnabled(false);Pilot->GetHeroSkill()->Configure(Mode->BattleRules,3,false);
  auto* Mechanism=Pilot->FindComponentByClass<UEGVillVMechanismComponent>();
  if(!TestNotNull(TEXT("Transfer mechanism"),Mechanism)||
     !TestTrue(TEXT("Independent devices created"),Mechanism->StartEnemyLayout(Mode->BattleRules,{1,6},{4,6})))return false;
  auto* Destination=Mechanism->GetDevice(1);
  if(!TestNotNull(TEXT("Cannon destination"),Destination))return false;
  auto* Blocker=World->SpawnActor<AActor>();auto* Capsule=NewObject<UCapsuleComponent>(Blocker);
  Blocker->SetRootComponent(Capsule);Capsule->InitCapsuleSize(50.f,100.f);
  Capsule->SetCollisionObjectType(ECC_Pawn);Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
  Capsule->SetCollisionResponseToAllChannels(ECR_Block);Capsule->RegisterComponent();
  Blocker->SetActorLocation(Destination->GetActorLocation());
  auto* Skill=Pilot->GetHeroSkill();
  auto DumpState=[&](const TCHAR* Label,int32 Ticks)
  {
   const auto* Gun=Mechanism->GetDevice(0);const auto* Cannon=Mechanism->GetDevice(1);
   const auto* Active=Skill->GetActiveMontage();
   const auto* Mesh=Pilot->GetVisiblePresentationMesh();
   const auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
   const auto* Combat=Pilot->GetCombat();
   AddInfo(FString::Printf(TEXT("Blocked transfer case=%d %s ticks=%d phase=%d mode=%d running=%d montage=%s montagePos=%.3f pilot=%s gun=%s cannon=%s gunAlive=%d cannonAlive=%d freeStart=%d freeStartNoBounds=%d freeGun=%d freeCannon=%d mechanismTick=%d skillTick=%d combatTick=%d battleRunning=%d blockerEnabled=%d"),
    Case,Label,Ticks,int32(Mechanism->Phase),Mechanism->Mode(),Skill->IsActionRunning()?1:0,*GetNameSafe(Active),
    Active&&Anim?Anim->Montage_GetPosition(Active):-1.f,*Pilot->GetActorLocation().ToString(),
    Gun?*Gun->GetActorLocation().ToString():TEXT("missing"),Cannon?*Cannon->GetActorLocation().ToString():TEXT("missing"),
    Gun&&IEGCombatant::Execute_IsAlive(Gun)?1:0,Cannon&&IEGCombatant::Execute_IsAlive(Cannon)?1:0,
    Combat->IsBoardLocationFree(Pilot->GetActorLocation(),Gun,Cannon)?1:0,
    Combat->IsBoardLocationFree(Pilot->GetActorLocation(),Gun,Cannon,false)?1:0,
    Gun&&Combat->IsBoardLocationFree(Gun->GetActorLocation(),Gun)?1:0,
    Cannon&&Combat->IsBoardLocationFree(Cannon->GetActorLocation(),Cannon)?1:0,
    Mechanism->IsComponentTickEnabled()?1:0,Skill->IsComponentTickEnabled()?1:0,Combat->IsComponentTickEnabled()?1:0,
    Mode->BattleRules->IsCombatRunning()?1:0,Capsule->GetCollisionEnabled()!=ECollisionEnabled::NoCollision?1:0));
  };
  DumpState(TEXT("before switch"),0);
  Pilot->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Pilot->GetAttributes()->GetMaxMana());
  if(!TestTrue(TEXT("Switch ultimate starts"),Skill->BeginAction(true)))return false;
  auto Tick=[&](){++GFrameCounter;World->Tick(LEVELTICK_All,.05f);};
  for(int32 I=0;I<100&&Mechanism->Phase!=EEGVillVPhase::Jumping;++I)Tick();
  if(!TestEqual(TEXT("Transfer reaches jump with blocked destination"),Mechanism->Phase,EEGVillVPhase::Jumping))return false;
  DumpState(TEXT("jump entered"),0);
  const FVector Waiting=Pilot->GetActorLocation();
  for(int32 I=0;I<30;++I)Tick();
  TestEqual(TEXT("Blocked destination cannot dock"),Mechanism->Phase,EEGVillVPhase::Jumping);
  TestTrue(TEXT("Blocked retry preserves the pilot position"),Pilot->GetActorLocation().Equals(Waiting,1.f));
  DumpState(TEXT("blocked wait"),30);
  if(Case==0)
  {
   // The departure seat may overlap the pilot. Clearing the destination but
   // leaving a pawn at takeoff must not prevent the nonblocking pilot jump.
   Blocker->SetActorLocation(Waiting);
   int32 Ticks=0;for(;Ticks<200&&!(Mechanism->IsOperating()&&Mechanism->Mode()==1&&!Skill->IsActionRunning());++Ticks)Tick();
   DumpState(TEXT("after release"),Ticks);
   TestTrue(TEXT("Clear destination docks despite occupied pilot takeoff"),Mechanism->IsOperating()&&Mechanism->Mode()==1&&!Skill->IsActionRunning());
  }
  else if(Case==1)
  {
   Destination->Die();Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   int32 Ticks=0;for(;Ticks<200&&!(Mechanism->IsOperating()&&Mechanism->Mode()==0&&!Skill->IsActionRunning());++Ticks)Tick();
   DumpState(TEXT("after destination death"),Ticks);
   TestTrue(TEXT("Destroyed blocked destination reroutes to surviving gun"),Mechanism->IsOperating()&&Mechanism->Mode()==0&&!Skill->IsActionRunning());
  }
  else
  {
   TWeakObjectPtr<AEGVillVEmplacement> Gun=Mechanism->GetDevice(0),Cannon=Destination;
   TWeakObjectPtr<UEGHeroSkillComponent> SkillAfterStop=Skill;
   TWeakObjectPtr<UEGVillVMechanismComponent> MechanismAfterStop=Mechanism;
   Mode->BattleRules->Stop();Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   for(int32 I=0;I<15;++I)Tick();
   TestFalse(TEXT("Stopped round releases gun"),Gun.IsValid());
   TestFalse(TEXT("Stopped round releases cannon"),Cannon.IsValid());
   TestTrue(TEXT("Stopped round clears traversal action"),!SkillAfterStop.IsValid()||!SkillAfterStop->IsActionRunning());
   TestTrue(TEXT("Stopped round cannot restart jump"),!MechanismAfterStop.IsValid()||MechanismAfterStop->Phase!=EEGVillVPhase::Jumping);
  }
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGridChaseCellReservations,
 "ElyGambit.CombatSpacing.GridChaseCellReservations",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGridChaseCellReservations::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Grid->CellWidth=500.f;Grid->CellDepth=500.f;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Grid fixture mode"),Mode)||!TestTrue(TEXT("Enter grid battle"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 UEGHeroDefinition* Definition=nullptr;
 for(auto* Unit:Mode->BattleRules->Units())if(Unit)
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  if(Unit->GetFaction_Implementation()==EEGFaction::Player&&!Definition&&Unit->GetDefinition()->HeroKind!=EEGHeroKind::VillV)Definition=Unit->GetDefinition();
  Unit->SetActorLocation(FVector(10000.f+Unit->GetUniqueID()*600.f,10000.f,88.f));
 }
 auto* Corruption=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
 if(!TestNotNull(TEXT("Ordinary hero definition"),Definition)||!TestNotNull(TEXT("Production corruption definition"),Corruption)||
    !TestTrue(TEXT("Corruption definition occupies one cell"),Corruption->GetPlacementFootprint()==FIntPoint(1,1)))return false;
 // Use the real character data without installing the scripted final-boss
 // component: this isolates pursuit from wall casts and the dual-health fight.
 auto* Target=Mode->SpawnBattleSummon(Corruption,1,Grid->CellToWorld({3,6})+FVector(0,0,88),EEGFaction::Enemy);
 if(!TestNotNull(TEXT("Production corruption target"),Target))return false;
 Target->GetCombat()->SetComponentTickEnabled(false);
 TArray<AEGChessUnit*> Attackers;
 for(int32 I=0;I<4;++I)
 {
  auto* Unit=Mode->SpawnBattleSummon(Definition,1,Grid->CellToWorld({I*2,0})+FVector(0,0,88),EEGFaction::Player);
  if(!TestNotNull(TEXT("Grid attacker"),Unit))return false;
  Unit->GetCombat()->SetComponentTickEnabled(false);
  Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),0.f);
  Attackers.Add(Unit);
 }
 ++GFrameCounter;World->Tick(LEVELTICK_All,.05f);
 TSet<FIntPoint> Claimed;TArray<FVector> Goals;int32 Adjacent=0;
 for(auto* Unit:Attackers)
 {
  Unit->GetCombat()->RequestChase(Target);FVector Goal;FIntPoint Cell;
  if(!TestTrue(TEXT("Reachable cell is reserved"),Unit->GetCombat()->GetChaseGoalForTests(Goal))||
     !TestTrue(TEXT("Reserved destination is on board"),Grid->TryWorldToCell(Goal,Cell)))continue;
  TestFalse(TEXT("No two pursuers claim the same cell"),Claimed.Contains(Cell));Claimed.Add(Cell);
  Goals.Add(Goal);
  TestTrue(TEXT("Pursuit destination is cell centre"),Grid->CellToWorld(Cell).Equals(FVector(Goal.X,Goal.Y,Grid->CellToWorld(Cell).Z),.1f));
  if(FMath::Abs(Cell.X-3)+FMath::Abs(Cell.Y-6)==1)++Adjacent;
  if(Unit==Attackers.Last())
   TestTrue(TEXT("Fourth pursuer chooses a nearest diagonal fallback"),FMath::Abs(
    FVector::DistSquared2D(Goal,Target->GetActorLocation())-2.0*FMath::Square(double(Grid->CellWidth)))<1.0);
 }
 TestEqual(TEXT("Three available orthogonal edge contact cells are claimed"),Adjacent,3);
 TestEqual(TEXT("Four pursuers reserve four distinct cells"),Claimed.Num(),4);
 if(Goals.Num()!=4)return false;
 bool bAllArrived=false;
 TArray<FVector> LastObservedGoals=Goals;
 for(int32 Frame=0;Frame<600&&!bAllArrived;++Frame)
 {
  ++GFrameCounter;World->Tick(LEVELTICK_All,.05f);
  for(auto* Unit:Attackers)Unit->GetCombat()->RequestChase(Target);
  for(int32 Index=0;Index<Attackers.Num();++Index)
  {
   FVector Destination;
   if(Attackers[Index]->GetCombat()->GetChaseGoalForTests(Destination)&&!Destination.Equals(LastObservedGoals[Index],.1f))
   {
    AddInfo(FString::Printf(TEXT("Grid goal changed frame=%d actor=%d previous=%s next=%s %s"),Frame,Index,
     *LastObservedGoals[Index].ToCompactString(),*Destination.ToCompactString(),*Attackers[Index]->GetCombat()->GetChaseDebugForTests()));
    LastObservedGoals[Index]=Destination;
   }
  }
  bAllArrived=true;
  for(auto* Unit:Attackers)
  {
   FVector Destination;
   bAllArrived&=Unit->GetCombat()->GetChaseGoalForTests(Destination)&&FVector::DistSquared2D(Destination,Unit->GetActorLocation())<4.f;
  }
  if(Frame%100==99||bAllArrived)for(int32 Index=0;Index<Attackers.Num();++Index)
  {
   auto* Unit=Attackers[Index];FVector Destination=FVector::ZeroVector;
   const bool HasGoal=Unit->GetCombat()->GetChaseGoalForTests(Destination);
   AddInfo(FString::Printf(TEXT("Grid chase frame=%d actor=%d name=%s pos=%s goal=%s hasGoal=%d speed=%.1f canAct=%d alive=%d battle=%d running=%d busy=%d %s"),
    Frame,Index,*Unit->GetName(),*Unit->GetActorLocation().ToCompactString(),*Destination.ToCompactString(),HasGoal?1:0,
    Unit->GetAttributes()->GetMoveSpeed(),Unit->GetCombat()->CanAct()?1:0,IEGCombatant::Execute_IsAlive(Unit)?1:0,
    Mode->BattleRules->IsBattleActive()?1:0,Mode->BattleRules->IsCombatRunning()?1:0,Unit->GetHeroSkill()->IsBusy()?1:0,
    *Unit->GetCombat()->GetChaseDebugForTests()));
  }
 }
 TestTrue(TEXT("All four pursuers reach their reserved cells"),bAllArrived);
 TestTrue(TEXT("Fourth pursuer actually leaves its starting position"),FVector::DistSquared2D(
  Attackers[3]->GetActorLocation(),Grid->CellToWorld({6,0}))>FMath::Square(100.f));
 TSet<FIntPoint> Arrived;
 for(auto* Unit:Attackers)
 {
  FIntPoint Cell;TestTrue(TEXT("Pursuer remains on the board"),Grid->TryWorldToCell(Unit->GetActorLocation(),Cell));
  TestFalse(TEXT("Pursuers finish in distinct cells"),Arrived.Contains(Cell));Arrived.Add(Cell);
  const FVector Center=Grid->CellToWorld(Cell);
  TestTrue(TEXT("Pursuer reaches its cell centre"),FVector::DistSquared2D(Center,Unit->GetActorLocation())<4.f);
 }
 Attackers[0]->GetCombat()->ResetCombat();FVector Released;
 TestFalse(TEXT("Reset releases chase cell"),Attackers[0]->GetCombat()->GetChaseGoalForTests(Released));
 Attackers[0]->SetActorLocation(FVector(12000.f,12000.f,88.f));
 Attackers[3]->GetCombat()->ResetCombat();Attackers[3]->GetCombat()->RequestChase(Target);
 if(TestTrue(TEXT("Waiting pursuer replans after a slot opens"),Attackers[3]->GetCombat()->GetChaseGoalForTests(Released)))
  TestTrue(TEXT("Released contact slot is claimable"),FVector::DistSquared2D(Released,Goals[0])<1.f);
 return true;
}
#endif
