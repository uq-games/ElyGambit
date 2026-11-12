#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"

// This exercises the production GAS damage path with a controlled lightweight
// board. It does not assert graphics, real input or balance acceptance.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteBossIndependentTransitions,
 "ElyGambit.EliteBoss.IndependentTransitions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteBossIndependentTransitions::RunTest(const FString&)
{
 for(int32 Case=0;Case<2;++Case)
 {
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
  Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
  if(!TestNotNull(TEXT("Controlled standalone world"),World))return false;
  ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
  World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Grid->CellWidth=499.96054f;Grid->CellDepth=500.67874f;
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!TestNotNull(TEXT("Authoritative test mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
  Mode->BattleRules->CompleteOpeningForTests();
  AEGChessUnit* Hero=nullptr;AEGChessUnit* Boss=nullptr;
  for(auto* Unit:Mode->BattleRules->Units())
  {
   if(!IsValid(Unit))continue;
   Unit->GetCombat()->SetComponentTickEnabled(false);
   if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;
   else if(!Boss)Boss=Unit;
  }
  if(!TestNotNull(TEXT("Player attacker"),Hero)||!TestNotNull(TEXT("Enemy boss fixture"),Boss))return false;
  // Force a wall-behind friendly using actual board coordinates; transition
  // must relocate it without changing its current health or status.
  const float PlayerHealthBefore=Hero->GetAttributes()->GetHealth();
  Hero->SetActorLocation(Grid->CellToWorld(FIntPoint(0,6))+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
  Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Mode->BattleRules,1000.f,100.f,50);
  if(!TestNotNull(TEXT("Attached boss rules"),Rules))return false;
  const float ExpectedSecondPool=1000.f*Rules->SecondLifeHealthMultiplier*Rules->MediumHealthMultiplier;
  int32 Transitions=0;float HealthAtCallback=-1.f;bool bAppliedReentrantHit=false;
  Rules->OnPhaseTwo.AddLambda([&](AEGChessUnit* Target)
  {
   ++Transitions;HealthAtCallback=Rules->GetPhaseHealth();
   if(Case==0)bAppliedReentrantHit=FEGCombatExecution::Damage(Hero,Target,25.f);
  });
  TestTrue(TEXT("First-pool lethal hit is accepted"),FEGCombatExecution::Damage(Hero,Boss,100000.f));
  TestTrue(TEXT("First pool depletion leaves boss alive"),IEGCombatant::Execute_IsAlive(Boss));
  TestEqual(TEXT("Depletion starts wall stage"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier));
  TestEqual(TEXT("No second phase before breaking wall"),Transitions,0);
  TestTrue(TEXT("First pool is displayed as depleted"),FMath::IsNearlyZero(Rules->GetPhaseHealth()));
  TestTrue(TEXT("First-pool overkill never damages wall"),FMath::IsNearlyEqual(Rules->GetBarrierHealth(),Rules->GetBarrierMaxHealth(),.01f));
  TestTrue(TEXT("Wall begins with full eight seconds"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
  const FVector Front=Grid->CellToWorld(FIntPoint(0,3));
  const FVector Back=Grid->CellToWorld(FIntPoint(0,6));
  TestTrue(TEXT("Wall blocks a path across the board"),Rules->BlocksMovement(Front,Back,UEGCombatComponent::BlockingRadius(Hero)));
  TestTrue(TEXT("Wall blocks a projectile across the board"),Rules->BlocksMovement(Front,Back,0.f));
  TestTrue(TEXT("Wall opening holds combat"),Mode->BattleRules->HasPresentationHold());
  // The opening uses monotonic real time so it can finish while world time is paused.
  FPlatformProcess::SleepNoStats(.72f);
  Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
  TestFalse(TEXT("Wall opening releases its hold"),Mode->BattleRules->HasPresentationHold());
  TestTrue(TEXT("Wall challenge timer did not run during intro"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
  const FVector FriendlyLocal=Grid->GetActorTransform().InverseTransformPosition(Hero->GetActorLocation());
  const float WallLocalY=Grid->GetActorTransform().InverseTransformPosition(Grid->CellToWorld(FIntPoint(0,4))).Y;
  TestTrue(TEXT("Friendly behind the wall is moved completely to its near side"),FriendlyLocal.Y+UEGCombatComponent::BlockingRadius(Hero)+8.f<=WallLocalY+.1f);
  TestTrue(TEXT("Wall migration preserves friendly health"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetHealth(),PlayerHealthBefore,.01f));
  if(Case==0)
  {
   TestTrue(TEXT("Massive wall hit accepted"),FEGCombatExecution::Damage(Hero,Boss,100000.f));
   TestEqual(TEXT("Breaking wall triggers second phase exactly once"),Transitions,1);
   TestTrue(TEXT("Second pool is full before subscriber acts"),FMath::IsNearlyEqual(HealthAtCallback,ExpectedSecondPool,.01f));
   TestTrue(TEXT("Synchronous phase subscriber can damage second pool"),bAppliedReentrantHit);
   TestTrue(TEXT("Only subscriber damage reaches second pool; wall overflow does not"),FMath::IsNearlyEqual(Rules->GetPhaseHealth(),ExpectedSecondPool-30.f,.1f));
   TestTrue(TEXT("Successful break applies weakness"),Rules->WasBarrierBroken()&&Rules->GetWeaknessRemainingSeconds()>3.9f);
  }
  else
  {
   auto* ASC=Hero->GetAbilitySystemComponent();
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),1000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   Hero->GetHeroSkill()->GrantSourceShield(TEXT("IndependentWallTest"),100.f,30.f);
   Rules->TickComponent(8.1f,LEVELTICK_All,nullptr);
   TestEqual(TEXT("Timeout enters second phase once"),Transitions,1);
   TestFalse(TEXT("Timeout is not a successful break"),Rules->WasBarrierBroken());
   TestTrue(TEXT("Timeout restores full independent second pool"),FMath::IsNearlyEqual(Rules->GetPhaseHealth(),ExpectedSecondPool,.01f));
   TestTrue(TEXT("Timeout damage consumes shield before health"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetShield(),0.f,.1f)&&FMath::IsNearlyEqual(Hero->GetAttributes()->GetHealth(),900.f,.1f));
   TestTrue(TEXT("Timeout grants no break weakness"),FMath::IsNearlyZero(Rules->GetWeaknessRemainingSeconds()));
  }
  Mode->BattleRules->Stop();
  TestFalse(TEXT("Round stop clears presentation hold"),Mode->BattleRules->HasPresentationHold());
  TestFalse(TEXT("Round stop removes wall collision"),Rules->IsWallActive());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteBossIndependentHolds,
 "ElyGambit.EliteBoss.IndependentPresentationHolds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteBossIndependentHolds::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 if(!TestNotNull(TEXT("Controlled standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Authoritative test mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Enemy=nullptr;
 for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
 {Unit->GetCombat()->SetComponentTickEnabled(false);if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else if(!Enemy)Enemy=Unit;}
 if(!TestNotNull(TEXT("Player"),Hero)||!TestNotNull(TEXT("Enemy"),Enemy))return false;
 const float Before=Enemy->GetAttributes()->GetHealth();const FGuid First=FGuid::NewGuid(),Second=FGuid::NewGuid();
 Mode->BattleRules->SetPresentationHold(First,true);Mode->BattleRules->SetPresentationHold(Second,true);
 TestFalse(TEXT("Combat is suspended while two independent owners hold it"),Mode->BattleRules->IsCombatRunning());
 TestFalse(TEXT("GAS damage is rejected while suspended"),FEGCombatExecution::Damage(Hero,Enemy,100.f));
 TestTrue(TEXT("Suspension does not consume health"),FMath::IsNearlyEqual(Enemy->GetAttributes()->GetHealth(),Before,.01f));
 Mode->BattleRules->SetPresentationHold(First,false);
 TestFalse(TEXT("Releasing one owner cannot release the other"),Mode->BattleRules->IsCombatRunning());
 Mode->BattleRules->SetPresentationHold(Second,false);
 TestTrue(TEXT("Combat resumes when all owners release"),Mode->BattleRules->IsCombatRunning());
 TestTrue(TEXT("Normal damage resumes"),FEGCombatExecution::Damage(Hero,Enemy,10.f));
 Mode->BattleRules->SetPresentationHold(First,true);Mode->BattleRules->Stop();
 TestFalse(TEXT("Stopping combat clears outstanding holds"),Mode->BattleRules->HasPresentationHold());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteBossIndependentPauseEffects,
 "ElyGambit.EliteBoss.IndependentPauseEffects",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteBossIndependentPauseEffects::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 if(!TestNotNull(TEXT("Controlled standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Authoritative mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 auto* PC=World->SpawnActor<APlayerController>();
 if(!TestNotNull(TEXT("Local standalone controller"),PC))return false;
 if(!PC->PlayerState)PC->SetPlayerState(World->SpawnActor<APlayerState>());
 if(!TestNotNull(TEXT("Pause owner state"),PC->PlayerState.Get()))return false;
 AEGChessUnit* Hero=nullptr;
 for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
 {Unit->GetCombat()->SetComponentTickEnabled(false);if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;}
 if(!TestNotNull(TEXT("Player hero"),Hero))return false;
 const float BaseSpeed=Hero->GetAttributes()->GetAttackSpeed();
 Mode->BattleRules->ApplyTeamSpeed(Hero,.5f,6.f);
 const float BoostedSpeed=Hero->GetAttributes()->GetAttackSpeed();
 const float Before=Mode->BattleRules->GetTeamBuffSeconds();
 if(!TestTrue(TEXT("Real active GE modifies speed"),BoostedSpeed>BaseSpeed))return false;
 TestTrue(TEXT("Presentation can advance during unpaused combat"),Mode->BattleRules->CanAdvancePresentationVisuals());
 const FGuid Story=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(Story,true);
 if(!TestTrue(TEXT("Story hold acquires world pause"),World->IsPaused()))return false;
 TestTrue(TEXT("Presentation can advance during its own hold"),Mode->BattleRules->CanAdvancePresentationVisuals());
 PC->SetPause(false);
 TestTrue(TEXT("Unrelated unpause cannot release the active story"),World->IsPaused());
 for(int32 Frame=0;Frame<20;++Frame){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}
 TestTrue(TEXT("Active GE remains applied while paused"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetAttackSpeed(),BoostedSpeed,.001f));
 TestTrue(TEXT("Battle buff remaining time does not advance while paused"),FMath::IsNearlyEqual(Mode->BattleRules->GetTeamBuffSeconds(),Before,.01f));
 TestTrue(TEXT("Another owner can register its own pause"),Mode->SetPause(PC));
 TestFalse(TEXT("Another pause stops presentation advancement"),Mode->BattleRules->CanAdvancePresentationVisuals());
 Mode->BattleRules->SetPresentationHold(Story,false);
 TestTrue(TEXT("Finishing story leaves the other owner's pause intact"),World->IsPaused());
 TestFalse(TEXT("Presentation remains frozen under external-only pause"),Mode->BattleRules->CanAdvancePresentationVisuals());
 PC->SetPause(false);TestFalse(TEXT("Other owner can release its own pause"),World->IsPaused());
 TestTrue(TEXT("Preexisting external pause acquired"),PC->SetPause(true));
 const FGuid OtherStory=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(OtherStory,true);
 PC->SetPause(false);
 TestTrue(TEXT("Releasing the external pause cannot release the outstanding story hold"),World->IsPaused());
 const float BeforeHandoff=Mode->BattleRules->GetTeamBuffSeconds();
 for(int32 Frame=0;Frame<20;++Frame){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}
 TestTrue(TEXT("GE remains applied after external pause is released"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetAttackSpeed(),BoostedSpeed,.001f));
 TestTrue(TEXT("Buff time remains frozen until story hold ends"),FMath::IsNearlyEqual(Mode->BattleRules->GetTeamBuffSeconds(),BeforeHandoff,.01f));
 Mode->BattleRules->SetPresentationHold(OtherStory,false);
 TestFalse(TEXT("Releasing the last story hold resumes after external pause has ended"),World->IsPaused());
 TestTrue(TEXT("Another preexisting external pause acquired"),PC->SetPause(true));
 const FGuid ThirdStory=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(ThirdStory,true);
 Mode->BattleRules->SetPresentationHold(ThirdStory,false);
 TestTrue(TEXT("Story never releases a pause it did not own"),World->IsPaused());
 PC->SetPause(false);TestFalse(TEXT("Preexisting pause remains separately releasable"),World->IsPaused());
 Mode->BattleRules->Stop();
 TestFalse(TEXT("Presentation cannot advance after combat ends"),Mode->BattleRules->CanAdvancePresentationVisuals());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteBossWallIntroPause,
 "ElyGambit.EliteBoss.WallIntroPause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteBossWallIntroPause::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 if(!TestNotNull(TEXT("Controlled standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Grid->CellWidth=499.96054f;Grid->CellDepth=500.67874f;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Authoritative mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 auto* PC=World->SpawnActor<APlayerController>();
 if(!TestNotNull(TEXT("Standalone pause controller"),PC))return false;
 if(!PC->PlayerState)PC->SetPlayerState(World->SpawnActor<APlayerState>());
 if(!TestNotNull(TEXT("Pause owner state"),PC->PlayerState.Get()))return false;
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Boss=nullptr;
 for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else if(!Boss)Boss=Unit;
 }
 if(!TestNotNull(TEXT("Player"),Hero)||!TestNotNull(TEXT("Boss fixture"),Boss))return false;
 TestTrue(TEXT("Ordinary combat permits presentation progress"),Mode->BattleRules->CanAdvancePresentationVisuals());
 auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Mode->BattleRules,1000.f,100.f,50);
 if(!TestNotNull(TEXT("Boss rules"),Rules)||!TestTrue(TEXT("Damage starts the wall"),FEGCombatExecution::Damage(Hero,Boss,100000.f)))return false;
 if(!TestEqual(TEXT("Wall phase entered"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier)))return false;
 if(!TestTrue(TEXT("Wall intro owns a world pause"),World->IsPaused()&&Mode->BattleRules->HasPresentationHold()))return false;
 TestTrue(TEXT("Wall intro alone permits presentation progress"),Mode->BattleRules->CanAdvancePresentationVisuals());

 // Introduce another real pause request while the intro owns its hold. A
 // longer-than-intro wall-clock wait must not consume either phase of play.
 if(!TestTrue(TEXT("External pause can coexist with intro"),Mode->SetPause(PC)))return false;
 TestFalse(TEXT("External pause freezes presentation progress"),Mode->BattleRules->CanAdvancePresentationVisuals());
 FPlatformProcess::SleepNoStats(.85f);
 Rules->TickComponent(3.f,LEVELTICK_PauseTick,nullptr);
 TestTrue(TEXT("Intro remains held after a longer external pause"),Mode->BattleRules->HasPresentationHold());
 TestTrue(TEXT("Challenge retains all eight seconds during external pause"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
 PC->SetPause(false);
 TestTrue(TEXT("Only intro remains paused after external release"),World->IsPaused()&&Mode->BattleRules->HasPresentationHold());
 TestTrue(TEXT("Releasing external pause allows presentation progress"),Mode->BattleRules->CanAdvancePresentationVisuals());

 // Elapsed real time before release cannot complete the intro immediately.
 Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
 TestTrue(TEXT("Intro does not skip its resumed duration"),Mode->BattleRules->HasPresentationHold());
 const double Until=FPlatformTime::Seconds()+2.;
 while(Mode->BattleRules->HasPresentationHold()&&FPlatformTime::Seconds()<Until)
 {
  FPlatformProcess::SleepNoStats(.04f);
  Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
 }
 if(!TestFalse(TEXT("Intro resumes and releases its own hold"),Mode->BattleRules->HasPresentationHold()))return false;
 TestFalse(TEXT("World resumes after final hold"),World->IsPaused());
 TestTrue(TEXT("Full eight seconds begins after intro"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
 Rules->TickComponent(1.25f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Challenge countdown advances with combat time"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),6.75f,.02f));
 Rules->TickComponent(6.8f,LEVELTICK_All,nullptr);
 TestEqual(TEXT("Challenge times out into second phase"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Second));
 Mode->BattleRules->Stop();
 TestFalse(TEXT("Ended battle cannot advance presentation"),Mode->BattleRules->CanAdvancePresentationVisuals());
 return true;
}
#endif
