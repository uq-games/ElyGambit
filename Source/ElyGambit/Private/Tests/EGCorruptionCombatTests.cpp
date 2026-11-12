#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGPieceTestGameMode.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Character/EGVillVEmplacement.h"
#include "Actor/EGCorruptionSeed.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Skills/EGCorruptionBossBehavior.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGSourceSummonDefinition.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGCorruptionPhantomVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "UObject/StrongObjectPtr.h"

// One controlled production-chain regression covers warning/impact, the real
// special-action token, spacing, and synchronous stop callbacks. It does not
// assert cinematic appearance or natural-battle balance.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionBossSpecialTimingTest,"ElyGambit.Corruption.BossSpecialTimingAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionBossSpecialTimingTest::RunTest(const FString&)
{
 for(int32 Case=0;Case<6;++Case)
 {
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
  UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("Standalone world"),World))return false;
  ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
  World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500.f;Grid->CellDepth=500.f;
  World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!TestNotNull(TEXT("Test mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
  auto* Battle=Mode->BattleRules.Get();Battle->CompleteOpeningForTests();
  AEGChessUnit* Hero=nullptr;AEGChessUnit* Boss=nullptr;
  for(auto* Unit:Battle->Units())if(IsValid(Unit))
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->CancelAction();
   if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else if(!Boss)Boss=Unit;
  }
  if(!TestNotNull(TEXT("Surviving target"),Hero)||!TestNotNull(TEXT("Boss source"),Boss))return false;
  auto* HeroASC=Hero->GetAbilitySystemComponent();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),5000.f);
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Battle,1000.f,100.f,50);
  if(!TestNotNull(TEXT("Boss authority"),Rules))return false;
  Rules->FirstSpreadDelaySeconds=3.f;Rules->SpreadIntervalSeconds=7.f;Rules->MimicIntervalSeconds=10.f;
  if(!TestTrue(TEXT("Deplete first life"),FEGCombatExecution::Damage(Hero,Boss,100000.f)))return false;
  Rules->CompleteBarrierIntroForTests();
  if(!TestTrue(TEXT("Break actual wall"),FEGCombatExecution::Damage(Hero,Boss,100000.f))||
     !TestEqual(TEXT("Second life starts"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Second)))return false;
  Boss->GetHeroSkill()->CancelAction();
  if(!TestTrue(TEXT("Idle Boss may begin a special action"),Boss->GetCombat()->CanAct()))return false;
  AEGChessUnit* LaterTarget=nullptr;
  if(Case>=4)
  {
   LaterTarget=Mode->SpawnBattleSummon(Hero->GetDefinition(),1,Hero->GetActorLocation()+FVector(0,20,0),EEGFaction::Player);
   if(!TestNotNull(TEXT("Second target in the same impact area"),LaterTarget))return false;
   LaterTarget->GetCombat()->SetComponentTickEnabled(false);LaterTarget->GetHeroSkill()->CancelAction();
   // BeginCombat resolves initial overlap, so install the intentionally close
   // controlled geometry only after spawning and assert both are targetable.
   LaterTarget->SetActorLocation(Hero->GetActorLocation()+FVector(0,200.f,0),false,nullptr,ETeleportType::TeleportPhysics);
   if(!TestTrue(TEXT("Both targets remain in either targeted spread or Pardo mimic radius"),
      FVector::Dist2D(Hero->GetActorLocation(),LaterTarget->GetActorLocation())<=200.1f&&
      Boss->GetCombat()->IsValidEnemy(Hero)&&Boss->GetCombat()->IsValidEnemy(LaterTarget)))return false;
   auto* LaterASC=LaterTarget->GetAbilitySystemComponent();
   LaterASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),5000.f);
   LaterASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
   LaterASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   const auto Victims=Battle->Units();
   TestTrue(TEXT("Original target is visited before the second target"),Victims.IndexOfByKey(Hero)>=0&&Victims.IndexOfByKey(LaterTarget)>Victims.IndexOfByKey(Hero));
  }
  struct FObservedCast{EEGCorruptionBossCast Kind;EEGCorruptionBossCastEvent Event;FGuid Id;float At,Duration;};
  TArray<FObservedCast> Events;float Clock=0.f,HealthAtReentrantStop=-1.f;
  TWeakObjectPtr<AEGCorruptionSeed> LastSeed;TWeakObjectPtr<AEGCorruptionPhantomVisual> RealPhantom;
  bool bSeedExistedAtStart=false,bPhantomExistedAtStart=false,bActionBlockObserved=false,bStoppedInsideDamage=false;
  float LaterHealthAtStop=-1.f;
  const FDelegateHandle HealthListener=HeroASC->GetGameplayAttributeValueChangeDelegate(UEGAttributeSet::GetHealthAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
  {
   const auto InterruptedCast=Case==4?EEGCorruptionBossCast::Spread:EEGCorruptionBossCast::Mimic;
   if(Case>=4&&!bStoppedInsideDamage&&Change.NewValue<Change.OldValue&&Battle->IsBattleActive()&&
      Rules->GetBossCastKind()==InterruptedCast)
   {
    bStoppedInsideDamage=true;HealthAtReentrantStop=Change.NewValue;
    LaterHealthAtStop=LaterTarget->GetAttributes()->GetHealth();Battle->Stop();
   }
  });
  Rules->OnBossCastEvent.AddLambda([&](EEGCorruptionBossCast Kind,EEGCorruptionBossCastEvent Event,FGuid Id,float Duration)
  {
   Events.Add({Kind,Event,Id,Clock,Duration});
   if(Event==EEGCorruptionBossCastEvent::Started)
   {
    bActionBlockObserved|=Boss->GetCombat()->HasExternalActionBlock()&&Rules->GetBossCastActionId()==Id;
    if(Kind==EEGCorruptionBossCast::Spread)
     for(TActorIterator<AEGCorruptionSeed> It(World);It;++It)if(It->BelongsTo(Battle)&&!It->IsActorBeingDestroyed()){LastSeed=*It;bSeedExistedAtStart=true;break;}
    if(Kind==EEGCorruptionBossCast::Mimic)
     TestNull(TEXT("A different action cannot create a phantom"),AEGCorruptionPhantomVisual::Spawn(Boss,Hero->GetDefinition(),Boss->GetActorLocation(),Boss->GetActorRotation(),1.f,FGuid::NewGuid()));
   }
   const bool bStopNow=(Case==1&&Kind==EEGCorruptionBossCast::Spread&&Event==EEGCorruptionBossCastEvent::Started)||
    (Case==2&&Kind==EEGCorruptionBossCast::Mimic&&Event==EEGCorruptionBossCastEvent::Started)||
    (Case==3&&Kind==EEGCorruptionBossCast::Spread&&Event==EEGCorruptionBossCastEvent::Resolved);
   if(bStopNow&&Battle->IsBattleActive()){HealthAtReentrantStop=Hero->GetAttributes()->GetHealth();Battle->Stop();}
  });
  const float InitialHealth=Hero->GetAttributes()->GetHealth();
  for(int32 Frame=0;Frame<140&&Battle->IsBattleActive();++Frame)
  {
   Clock+=.1f;
   TArray<TWeakObjectPtr<AEGCorruptionSeed>> Seeds;
   for(TActorIterator<AEGCorruptionSeed> It(World);It;++It)if(It->BelongsTo(Battle)&&!It->IsActorBeingDestroyed())Seeds.Add(*It);
   for(const auto& Seed:Seeds)if(Seed.IsValid()&&!Seed->IsActorBeingDestroyed())Seed->Tick(.1f);
   if(Battle->IsBattleActive())Rules->TickComponent(.1f,LEVELTICK_All,nullptr);
   if(Case==0&&Battle->IsBattleActive()&&!bPhantomExistedAtStart&&Rules->GetBossCastKind()==EEGCorruptionBossCast::Mimic)
   {
    for(TActorIterator<AEGCorruptionPhantomVisual> It(World);It;++It)if(It->GetOwner()==Boss&&!It->IsActorBeingDestroyed()){RealPhantom=*It;bPhantomExistedAtStart=true;break;}
    if(TestTrue(TEXT("Real active mimic creates its phantom after Started"),RealPhantom.IsValid()))
    {
     auto* Phantom=RealPhantom.Get();auto* Beam=FindObject<UStaticMeshComponent>(Phantom,TEXT("PhantomCastBeam"));
     if(TestNotNull(TEXT("Phantom cast visual"),Beam))
     {
      Phantom->Tick(3.f);
      TestTrue(TEXT("Long windup holds phantom without an automatic cast"),IsValid(Phantom)&&!Phantom->IsActorBeingDestroyed()&&!Beam->IsVisible());
      TestEqual(TEXT("Phantom cast visual never blocks"),Beam->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
     }
     TArray<AEGCombatVisual*> Rigs;
     for(TActorIterator<AEGCombatVisual> It(World);It;++It)if(It->GetOwner()==Phantom&&!It->IsActorBeingDestroyed())Rigs.Add(*It);
     TestTrue(TEXT("At most one owned appearance rig"),Rigs.Num()<=1);
     for(auto* Rig:Rigs){TArray<USkeletalMeshComponent*> Parts;Rig->GetComponents<USkeletalMeshComponent>(Parts);TestTrue(TEXT("Assembly builds source parts"),Parts.Num()>0);for(auto* Part:Parts)TestEqual(TEXT("Phantom parts never block"),Part->GetCollisionEnabled(),ECollisionEnabled::NoCollision);}
     TArray<UNiagaraComponent*> Accents;Phantom->GetComponents<UNiagaraComponent>(Accents);
     const FGuid Hold=FGuid::NewGuid();Battle->SetPresentationHold(Hold,true);Phantom->Tick(.1f);
     for(auto* Accent:Accents)TestTrue(TEXT("Phantom particles pause with presentation"),Accent->IsPaused());
     Battle->SetPresentationHold(Hold,false);Phantom->Tick(.1f);
     for(auto* Accent:Accents)TestFalse(TEXT("Phantom particles resume with presentation"),Accent->IsPaused());
    }
   }
   // In the ordinary case stop during the second real warning to verify
   // pending effects and the currently owned action are cancelled.
   if(Case==0&&Battle->IsBattleActive())
   {
    int32 Starts=0;for(const auto& E:Events)if(E.Kind==EEGCorruptionBossCast::Spread&&E.Event==EEGCorruptionBossCastEvent::Started)++Starts;
    if(Starts>=2){HealthAtReentrantStop=Hero->GetAttributes()->GetHealth();Battle->Stop();}
   }
  }
  const FObservedCast* SpreadStart=nullptr;const FObservedCast* SpreadResolved=nullptr;const FObservedCast* MimicStart=nullptr;const FObservedCast* MimicResolved=nullptr;
  int32 Cancelled=0;
  for(const auto& E:Events)
  {
   if(E.Event==EEGCorruptionBossCastEvent::Cancelled)++Cancelled;
   if(E.Kind==EEGCorruptionBossCast::Spread&&E.Event==EEGCorruptionBossCastEvent::Started&&!SpreadStart)SpreadStart=&E;
   if(E.Kind==EEGCorruptionBossCast::Spread&&E.Event==EEGCorruptionBossCastEvent::Resolved&&!SpreadResolved)SpreadResolved=&E;
   if(E.Kind==EEGCorruptionBossCast::Mimic&&E.Event==EEGCorruptionBossCastEvent::Started&&!MimicStart)MimicStart=&E;
   if(E.Kind==EEGCorruptionBossCast::Mimic&&E.Event==EEGCorruptionBossCastEvent::Resolved&&!MimicResolved)MimicResolved=&E;
  }
  if(TestNotNull(TEXT("First spread starts"),SpreadStart))
  {
   TestTrue(TEXT("First warning begins after three effective seconds"),SpreadStart->At>=2.9f&&SpreadStart->At<=3.2f);
   TestTrue(TEXT("Warning exposes valid action and real seed"),SpreadStart->Id.IsValid()&&bSeedExistedAtStart&&bActionBlockObserved);
  }
  if(Case!=1&&Case!=4&&TestNotNull(TEXT("First spread resolves"),SpreadResolved)&&TestNotNull(TEXT("Its start is recorded"),SpreadStart))
  {
   TestTrue(TEXT("Spread resolves its own action after the warning"),SpreadStart->Id==SpreadResolved->Id&&SpreadResolved->At-SpreadStart->At>=1.9f);
   TestTrue(TEXT("Real spread damage resolves"),Hero->GetAttributes()->GetHealth()<InitialHealth);
  }
  if(Case==0||Case==2||Case==5)
  {
   if(TestNotNull(TEXT("Mimic starts"),MimicStart)&&TestNotNull(TEXT("Spread precedes mimic"),SpreadResolved))
    TestTrue(TEXT("Mimic starts at least one second after spread impact"),MimicStart->At-SpreadResolved->At>=.99f&&MimicStart->Id.IsValid());
  }
  if(Case==0&&TestNotNull(TEXT("Mimic resolves"),MimicResolved)&&TestNotNull(TEXT("Mimic start"),MimicStart))
   TestTrue(TEXT("Mimic releases its own action after one second"),MimicResolved->Id==MimicStart->Id&&MimicResolved->At-MimicStart->At>=.99f);
  if(Case>=4)
  {
   TestTrue(TEXT("Health change listener stops during actual damage"),bStoppedInsideDamage);
   TestNull(TEXT("Interrupted damage does not publish cast resolution"),Case==4?SpreadResolved:MimicResolved);
   TestTrue(TEXT("Later target receives no damage after the interrupted first target"),
    IsValid(LaterTarget)&&LaterHealthAtStop>=0.f&&FMath::IsNearlyEqual(LaterTarget->GetAttributes()->GetHealth(),LaterHealthAtStop,.01f));
  }
  TestFalse(TEXT("Stop clears active special token"),Boss->GetCombat()->HasExternalActionBlock());
  TestTrue(TEXT("Expected synchronous or pending stop was reached"),HealthAtReentrantStop>=0.f&&!Battle->IsBattleActive());
  TestEqual(TEXT("Stop cancels only an outstanding cast"),Cancelled,Case==3?0:1);
  if(Case==2){bool bLivePhantom=false;for(TActorIterator<AEGCorruptionPhantomVisual> It(World);It;++It)if(It->GetOwner()==Boss&&!It->IsActorBeingDestroyed())bLivePhantom=true;TestFalse(TEXT("Same-frame Started cancellation leaves no phantom"),bLivePhantom);}
  if(Case==0)TestFalse(TEXT("Ending round destroys phantom"),RealPhantom.IsValid());
  const int32 EventCount=Events.Num();const float HealthAfterStop=Hero->GetAttributes()->GetHealth();
  if(LastSeed.IsValid()&&!LastSeed->IsActorBeingDestroyed())LastSeed->Tick(10.f);
  Rules->TickComponent(10.f,LEVELTICK_All,nullptr);
  TestTrue(TEXT("Stopped round produces no late special damage"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetHealth(),HealthAfterStop,.01f));
  TestEqual(TEXT("Stopped round produces no late cast notifications"),Events.Num(),EventCount);
  HeroASC->GetGameplayAttributeValueChangeDelegate(UEGAttributeSet::GetHealthAttribute()).Remove(HealthListener);
  Rules->OnBossCastEvent.Clear();
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionWallThrownSummonTest,"ElyGambit.Corruption.WallThrownSummonRelocation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionWallThrownSummonTest::RunTest(const FString&)
{
 for(int32 Case=0;Case<5;++Case)
 {
  const bool bSourceCurve=(Case%2)!=0,bInitiallyInFront=Case>=2,bRollbackCase=Case==4;
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
  Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
  if(!TestNotNull(TEXT("standalone world"),World))return false;
  World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Grid->CellWidth=499.96054f;Grid->CellDepth=500.67874f;
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!TestNotNull(TEXT("fixture mode"),Mode)||!TestTrue(TEXT("fixture combat"),Mode->EnterCombat()))return false;
  auto* Battle=Mode->BattleRules.Get();Battle->CompleteOpeningForTests();
  AEGChessUnit* Hero=nullptr;AEGChessUnit* Boss=nullptr;
  for(auto* Unit:Battle->Units())if(IsValid(Unit))
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);
   if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else if(!Boss)Boss=Unit;
  }
  if(!TestNotNull(TEXT("player"),Hero)||!TestNotNull(TEXT("boss"),Boss))return false;
  Hero->SetActorLocation(Grid->CellToWorld(FIntPoint(3,1))+FVector(0,0,88));
  const FVector Ground=Grid->CellToWorld(FIntPoint(0,bInitiallyInFront?3:6))+FVector(0,0,88);
  auto* Cargo=Battle->SpawnSummon(Hero->GetDefinition(),Hero->GetInstanceId(),1,Ground,true);
  if(!TestNotNull(TEXT("in-flight summon"),Cargo))return false;
  Cargo->GetCombat()->SetComponentTickEnabled(false);
  FVector OriginalReservation;
  if(!TestTrue(TEXT("in-flight summon holds a real landing reservation"),Cargo->GetCombat()->ReserveBoardLanding(Ground,OriginalReservation)))return false;
  auto* Flight=World->SpawnActor<AEGSkillProjectile>();
  if(!TestNotNull(TEXT("thrown summon projectile"),Flight))return false;
  Flight->Battle=Battle;Flight->Round=Battle->GetRoundId();Flight->Source=Hero;Flight->Target=Boss;Flight->Cargo=Cargo;
  Flight->Kind=EEGProjectileKind::ThrownSummon;Flight->AttackId=FGuid::NewGuid();Flight->RawDamage=1.f;
  Flight->Start=Ground+FVector(0,0,500);Flight->Destination=OriginalReservation;Flight->Duration=1.f;Flight->Elapsed=.25f;
  Flight->FlightHeight=500.f;Flight->FlightBasis=FQuat::Identity;
  const FVector Air=Ground+FVector(0,0,350);Flight->SetActorLocation(Air);Cargo->SetActorLocation(Air);
  if(bSourceCurve)
  {
   auto* Motion=NewObject<UEGSourceSummonDefinition>(Flight);Motion->DescentTimeExponent=1.f;
   // A deliberately large rearward lateral source offset exercises clamping.
   Motion->NormalizedOffsets={FVector(0,0,1),FVector(0,15,.7f),FVector::ZeroVector};Flight->SummonMotion=Motion;
  }
  auto* Ring=AEGDescentRing::SpawnTelegraph(World,Ground-FVector(0,0,88),nullptr,100.f,5.f);Flight->DescentRing=Ring;
  TestNotNull(TEXT("descent warning ring"),Ring);
  const FEGThrownSummonFlightState Before=Flight->CaptureThrownSummonFlight();
  TestTrue(TEXT("rebase flight to a candidate position"),Flight->RebaseThrownSummon(Grid->CellToWorld(FIntPoint(1,1))+FVector(0,0,350)));
  Flight->RestoreThrownSummonFlight(Before);
  const auto Restored=Flight->CaptureThrownSummonFlight();
  TestTrue(TEXT("recovery restores full trajectory, clock and visual position"),
   Restored.Start.Equals(Before.Start,.01f)&&Restored.Destination.Equals(Before.Destination,.01f)&&
   Restored.Location.Equals(Before.Location,.01f)&&Restored.CargoLocation.Equals(Before.CargoLocation,.01f)&&Restored.RingLocation.Equals(Before.RingLocation,.01f)&&
   FMath::IsNearlyEqual(Restored.Elapsed,Before.Elapsed,.001f)&&FMath::IsNearlyEqual(Restored.Duration,Before.Duration,.001f)&&
   Restored.bRebased==Before.bRebased&&Restored.bHadRing==Before.bHadRing&&Restored.bHadCargo==Before.bHadCargo&&
   FMath::IsNearlyEqual(Restored.FlightHeight,Before.FlightHeight,.001f)&&FMath::IsNearlyEqual(Restored.RebasedCurveStart,Before.RebasedCurveStart,.001f)&&Restored.FlightBasis.Equals(Before.FlightBasis,.001f));
  Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Battle,1000.f,100.f,50);
  if(!TestNotNull(TEXT("boss phase rules"),Rules))return false;
  const FVector HeroBefore=Hero->GetActorLocation();
  TestTrue(TEXT("depleting first pool opens barrier"),FEGCombatExecution::Damage(Hero,Boss,100000.f));
  TestTrue(TEXT("barrier becomes active"),Rules->IsWallActive());
  if(bRollbackCase)
  {
   // Insert a real hostile blocking base on the planned landing after planning.
   // The encounter must reject the stale plan and restore the flight transaction.
   auto* Blocker=Mode->SpawnBattleSummon(Hero->GetDefinition(),1,Flight->Destination,EEGFaction::Enemy,false);
   if(!TestNotNull(TEXT("dynamic landing blocker"),Blocker))return false;
   Blocker->GetCombat()->SetComponentTickEnabled(false);
   AddExpectedError(TEXT("Corruption wall: formation changed during transition; recovering encounter"),EAutomationExpectedErrorFlags::Contains,1);
   Rules->CompleteBarrierIntroForTests();
   const FEGThrownSummonFlightState Recovered=Flight->CaptureThrownSummonFlight();
   FVector RecoveredReservation,RecoveredPending;Cargo->GetCombat()->GetBoardLandingCoordinates(RecoveredReservation,RecoveredPending);
   TestTrue(TEXT("failed formation restores player's original position"),Hero->GetActorLocation().Equals(HeroBefore,.01f));
   TestTrue(TEXT("failed formation restores original landing reservation"),RecoveredReservation.Equals(OriginalReservation,.01f));
   TestTrue(TEXT("failed formation restores in-flight position, trajectory and clock"),Recovered.Start.Equals(Before.Start,.01f)&&
    Recovered.Destination.Equals(Before.Destination,.01f)&&Recovered.Location.Equals(Before.Location,.01f)&&
    Recovered.CargoLocation.Equals(Before.CargoLocation,.01f)&&Recovered.RingLocation.Equals(Before.RingLocation,.01f)&&
    FMath::IsNearlyEqual(Recovered.Elapsed,Before.Elapsed,.001f)&&FMath::IsNearlyEqual(Recovered.Duration,Before.Duration,.001f)&&
    Recovered.bRebased==Before.bRebased&&FMath::IsNearlyEqual(Recovered.RebasedCurveStart,Before.RebasedCurveStart,.001f));
   TestFalse(TEXT("recovery clears the wall"),Rules->IsWallActive());
   TestFalse(TEXT("recovery clears the transition hold"),Battle->HasPresentationHold());
   Battle->Stop();World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
   Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
   continue;
  }
  Rules->CompleteBarrierIntroForTests();
  const float Radius=UEGCombatComponent::BlockingRadius(Cargo);
  TestTrue(TEXT("in-flight cargo moves to player side"),Rules->AllowsPlayerLocation(Cargo->GetActorLocation(),Radius));
  TestTrue(TEXT("relocation preserves original remaining flight time"),FMath::IsNearlyEqual(Flight->Duration-Flight->Elapsed,.75f,.001f));
  TestTrue(TEXT("relocation preserves ground height"),FMath::IsNearlyEqual(Flight->Destination.Z,Ground.Z,.01f));
  TestTrue(TEXT("every in-flight summon receives a legal landing"),Rules->AllowsPlayerLocation(Flight->Destination,Radius));
  FVector RetargetedReservation,Pending;Cargo->GetCombat()->GetBoardLandingCoordinates(RetargetedReservation,Pending);
  TestTrue(TEXT("landing reservation follows destination at ground height"),RetargetedReservation.Equals(Flight->Destination,.01f)&&
   FMath::IsNearlyEqual(RetargetedReservation.Z,OriginalReservation.Z,.01f));
  Flight->Tick(.05f);
  TestTrue(TEXT("next flight frame cannot pull cargo behind wall"),Rules->AllowsPlayerLocation(Cargo->GetActorLocation(),Radius));
  TestTrue(TEXT("remainder of flight clock advances normally"),FMath::IsNearlyEqual(Flight->Duration-Flight->Elapsed,.70f,.001f));
  Flight->Tick(.35f);
  AddInfo(FString::Printf(TEXT("WallFlight case=%d sourceCurve=%d initiallyFront=%d cargo=%s destination=%s start=%s elapsed=%.4f duration=%.4f radius=%.3f allowed=%d"),
   Case,bSourceCurve?1:0,bInitiallyInFront?1:0,*Cargo->GetActorLocation().ToCompactString(),*Flight->Destination.ToCompactString(),
   *Flight->Start.ToCompactString(),Flight->Elapsed,Flight->Duration,Radius,Rules->AllowsPlayerLocation(Cargo->GetActorLocation(),Radius)?1:0));
  TestTrue(TEXT("remaining source-curve excursion cannot cross wall"),Rules->AllowsPlayerLocation(Cargo->GetActorLocation(),Radius));
  const int32 EnteredBefore=Battle->SummonCount();
  Flight->Tick(.36f);
  TestFalse(TEXT("summon completes flight"),Cargo->GetHeroSkill()->bInFlight);
  TestTrue(TEXT("summon lands on player side"),Rules->AllowsPlayerLocation(Cargo->GetActorLocation(),Radius));
  TestEqual(TEXT("summon enters battle once"),Battle->SummonCount(),EnteredBefore+1);
  TestTrue(TEXT("flight retires after landing"),Flight->IsActorBeingDestroyed());
  Battle->Stop();World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
  Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionBossShieldTest,"ElyGambit.Corruption.BossThresholdAndShield",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionBossShieldTest::RunTest(const FString&)
{
 auto* Bow=NewObject<UEGCorruptionBossBehavior>();
 TestFalse(TEXT("unbound Boss cannot cast its independent phase-one pulse"),Bow->AllowsAction(true));
 TestTrue(TEXT("boss can fire a normal bow attack"),Bow->AllowsAction(false));
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();Mode->EnterCombat();
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Boss=nullptr;AEGChessUnit* OverflowBoss=nullptr;
 for(const auto& Entry:Mode->GetBattleUnits())if(auto* Unit=Entry.Get())
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else if(!Boss)Boss=Unit;else if(!OverflowBoss)OverflowBoss=Unit;
 }
 if(TestNotNull(TEXT("hero"),Hero)&&TestNotNull(TEXT("boss"),Boss))
 {
  Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Mode->BattleRules,1000.f,100.f,50);
  if(TestNotNull(TEXT("boss rules"),Rules))
  {
   const auto* Defaults=GetDefault<UEGCorruptionCombatComponent>();
   TestTrue(TEXT("Boss uses current configured independent life multipliers"),Defaults&&Rules->FirstLifeHealthMultiplier>0.f&&Rules->SecondLifeHealthMultiplier>0.f&&
    FMath::IsNearlyEqual(Rules->FirstLifeHealthMultiplier,Defaults->FirstLifeHealthMultiplier,.001f)&&
    FMath::IsNearlyEqual(Rules->SecondLifeHealthMultiplier,Defaults->SecondLifeHealthMultiplier,.001f));
   const float OpeningHealth=1000.f*Rules->FirstLifeHealthMultiplier*Rules->MediumHealthMultiplier;
   TestTrue(TEXT("First pool applies the configured multiplier"),FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),OpeningHealth,.01f));
   Rules->SecondLifeHealthMultiplier=.75f;
   Rules->BarrierHealthRatio=.2f;
   const auto ConfiguredHighMultiplier=[Rules](int32 Corruption)
   {
    const float Unclamped=Rules->HighHealthBaseMultiplier+Rules->HighHealthStepMultiplier*
     FMath::FloorToInt((Corruption-70)/float(FMath::Max(1,Rules->HighHealthStepSize)));
    return FMath::Clamp(Unclamped,FMath::Max(.01f,Rules->LowHealthMultiplier),FMath::Max(Rules->LowHealthMultiplier,Rules->MaximumHealthMultiplier));
   };
   const float HealthAt90=1000.f*Rules->FirstLifeHealthMultiplier*ConfiguredHighMultiplier(90);
   const float SecondPoolAt90=1000.f*Rules->SecondLifeHealthMultiplier*ConfiguredHighMultiplier(90);
   const float SecondPoolAt200=1000.f*Rules->SecondLifeHealthMultiplier*ConfiguredHighMultiplier(200);
   int32 PhaseCount=0;Rules->OnPhaseTwo.AddLambda([&](AEGChessUnit*){++PhaseCount;});
   Rules->UpdateCorruption(90);
   TestTrue(TEXT("corruption updates maximum without healing"),FMath::IsNearlyEqual(Boss->GetAttributes()->GetMaxHealth(),HealthAt90,.01f)&&FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),OpeningHealth,.01f));
   TestTrue(TEXT("opening damage accepted"),FEGCombatExecution::Damage(Hero,Boss,500.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid()));
   TestTrue(TEXT("first pool remains active at half health"),Rules->GetBossPhase()==EEGCorruptionBossPhase::First);
   TestTrue(TEXT("first pool health reflects damage"),FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),OpeningHealth-500.f,.01f));
   TestEqual(TEXT("no phase-two notification at half health"),PhaseCount,0);
   TestTrue(TEXT("fatal overflow intercepted"),FEGCombatExecution::Damage(Hero,Boss,5000.f));
   TestTrue(TEXT("first pool transitions into barrier"),Rules->GetBossPhase()==EEGCorruptionBossPhase::Barrier);
   TestTrue(TEXT("boss remains alive until second pool"),IEGCombatant::Execute_IsAlive(Boss));
   TestTrue(TEXT("cross-pool overflow never damages barrier"),FMath::IsNearlyEqual(Rules->GetBarrierHealth(),Rules->GetBarrierMaxHealth(),.01f));
   TestTrue(TEXT("configured barrier ratio uses opening first-pool max"),FMath::IsNearlyEqual(Rules->GetBarrierMaxHealth(),OpeningHealth*.2f,.01f));
   TestEqual(TEXT("phase two not yet published"),PhaseCount,0);
   Rules->CompleteBarrierIntroForTests();
   const float InitialWall=Rules->GetBarrierHealth();
   TestTrue(TEXT("normal hit accepted against barrier"),FEGCombatExecution::Damage(Hero,Boss,50.f));
   TestTrue(TEXT("normal hit reduces wall and preserves first pool sentinel"),FMath::IsNearlyEqual(Rules->GetBarrierHealth(),InitialWall-50.f,.01f)&&FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),1.f,.01f));
   FEGCombatExecution::StigmataDamage(Hero,Boss,10000.f,FGuid::NewGuid(),true);
   TestTrue(TEXT("periodic hit can break barrier"),Rules->GetBossPhase()==EEGCorruptionBossPhase::Second&&Rules->WasBarrierBroken());
   TestTrue(TEXT("second pool starts full despite wall overflow"),FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),Boss->GetAttributes()->GetMaxHealth(),.01f));
   TestTrue(TEXT("second pool uses its independent life multiplier and current corruption"),FMath::IsNearlyEqual(Rules->GetPhaseMaxHealth(),SecondPoolAt90,.01f));
   TestTrue(TEXT("break grants four seconds of weakness"),FMath::IsNearlyEqual(Rules->GetWeaknessRemainingSeconds(),4.f,.01f));
   TestEqual(TEXT("second pool published once"),PhaseCount,1);
   TestTrue(TEXT("second-pool hit accepted"),FEGCombatExecution::Damage(Hero,Boss,100.f));
   TestTrue(TEXT("weakness increases received damage 20 percent"),FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),SecondPoolAt90-120.f,.1f));
   TestEqual(TEXT("phase does not repeat"),PhaseCount,1);
   Rules->UpdateCorruption(200);
   TestTrue(TEXT("corruption cap updates second pool without restoring health"),FMath::IsNearlyEqual(Boss->GetAttributes()->GetMaxHealth(),SecondPoolAt200,.01f)&&FMath::IsNearlyEqual(Boss->GetAttributes()->GetHealth(),SecondPoolAt90-120.f,.1f));
   if(TestNotNull(TEXT("overflow boss fixture"),OverflowBoss))
   {
    OverflowBoss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
    auto* OverflowRules=UEGCorruptionCombatComponent::AttachBoss(OverflowBoss,Mode->BattleRules,1000.f,100.f,50);
    if(TestNotNull(TEXT("overflow boss rules"),OverflowRules))
    {
     const float OriginalFirstPool=1000.f*OverflowRules->FirstLifeHealthMultiplier*OverflowRules->MediumHealthMultiplier;
     const float ExpectedSecondPool=1000.f*OverflowRules->SecondLifeHealthMultiplier*OverflowRules->MediumHealthMultiplier;
     OverflowRules->FirstLifeHealthMultiplier=1.5f;
     OverflowRules->UpdateCorruption(50);
     TestTrue(TEXT("first-pool multiplier changes max without automatically healing"),FMath::IsNearlyEqual(OverflowBoss->GetAttributes()->GetMaxHealth(),1500.f*OverflowRules->MediumHealthMultiplier,.01f)&&FMath::IsNearlyEqual(OverflowBoss->GetAttributes()->GetHealth(),OriginalFirstPool,.01f));
     int32 OverflowPhases=0;
     OverflowRules->OnPhaseTwo.AddLambda([&](AEGChessUnit* Unit)
     {
      ++OverflowPhases;
      TestTrue(TEXT("phase subscriber sees second pool already committed"),FMath::IsNearlyEqual(Unit->GetAttributes()->GetHealth(),Unit->GetAttributes()->GetMaxHealth(),.01f));
      FEGCombatExecution::Damage(Hero,Unit,25.f);
     });
     TestTrue(TEXT("overwhelming hit accepted"),FEGCombatExecution::Damage(Hero,OverflowBoss,10000.f));
     TestTrue(TEXT("fatal overflow does not break the wall"),OverflowRules->GetBossPhase()==EEGCorruptionBossPhase::Barrier&&FMath::IsNearlyEqual(OverflowRules->GetBarrierHealth(),OverflowRules->GetBarrierMaxHealth(),.01f));
     OverflowRules->CompleteBarrierIntroForTests();
     OverflowRules->TickComponent(8.1f,LEVELTICK_All,nullptr);
     TestTrue(TEXT("timeout proceeds to second pool"),OverflowRules->GetBossPhase()==EEGCorruptionBossPhase::Second&&!OverflowRules->WasBarrierBroken());
     TestEqual(TEXT("second-pool notification is unique"),OverflowPhases,1);
     TestTrue(TEXT("subscriber damage applies to new pool without weakness"),FMath::IsNearlyEqual(OverflowBoss->GetAttributes()->GetHealth(),ExpectedSecondPool-25.f,.01f));
    }
   }
   Mode->BattleRules->Stop();
   TestEqual(TEXT("stopped round cannot block damage"),Rules->FilterDamage(Hero,FGuid::NewGuid(),10.f),10.f);
  }
 }
 World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionVillVDevicesTest,"ElyGambit.Corruption.VillVDevices",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionVillVDevicesTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 const bool bCombat=Mode&&Mode->EnterCombat();
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
 if(TestTrue(TEXT("fixture enters combat"),bCombat)&&TestNotNull(TEXT("Vill-V card"),Card)&&TestNotNull(TEXT("Vill-V hero"),Card->HeroDefinition.Get()))
 {
  auto* Copy=Mode->SpawnBattleSummon(Card->HeroDefinition,3,Grid->CellToWorld({6,6})+FVector(0,0,88),EEGFaction::Enemy);
  if(TestNotNull(TEXT("independent enemy copy"),Copy))
  {
   Copy->GetHeroSkill()->Configure(Mode->BattleRules,3,false);
   auto* Rules=UEGCorruptionCombatComponent::AttachCopy(Copy,Mode->BattleRules,EEGCorruptionRole::Ranged);
   auto* Mechanism=Copy->FindComponentByClass<UEGVillVMechanismComponent>();
   if(TestNotNull(TEXT("copy passive"),Rules)&&TestNotNull(TEXT("mechanism"),Mechanism))
   {
    TestFalse(TEXT("same cell is rejected"),Mechanism->StartEnemyLayout(Mode->BattleRules,{0,6},{0,6}));
    TestTrue(TEXT("failed layout retains a targetable copy"),Copy->GetHeroSkill()->IsTargetable());
    TestNull(TEXT("failed layout leaves no device"),Mechanism->GetDevice(0));
    if(TestTrue(TEXT("two vacant enemy cells create devices"),Mechanism->StartEnemyLayout(Mode->BattleRules,{0,6},{1,6})))
    {
     auto* Gun=Mechanism->GetDevice(0);auto* Cannon=Mechanism->GetDevice(1);
     TestTrue(TEXT("both devices belong to the enemy"),Gun&&Cannon&&IEGCombatant::Execute_GetFaction(Gun)==EEGFaction::Enemy&&IEGCombatant::Execute_GetFaction(Cannon)==EEGFaction::Enemy);
     TestTrue(TEXT("copy operates a device"),Mechanism->IsOperating());
     TestFalse(TEXT("pilot is protected while devices exist"),Copy->GetHeroSkill()->IsTargetable());
     TestTrue(TEXT("devices have independent health"),Gun&&Cannon&&Gun->GetAbilitySystemComponent()!=Cannon->GetAbilitySystemComponent()&&Gun->GetAbilitySystemComponent()!=Copy->GetAbilitySystemComponent());
     TWeakObjectPtr<AEGVillVEmplacement> WeakGun=Gun,WeakCannon=Cannon;
     Mechanism->Stop();
     TestFalse(TEXT("gun is destroyed on stop"),WeakGun.IsValid());
     TestFalse(TEXT("cannon is destroyed on stop"),WeakCannon.IsValid());
     TestTrue(TEXT("stopped copy becomes targetable"),Copy->GetHeroSkill()->IsTargetable());
     if(TestTrue(TEXT("devices can be created after cleanup"),Mechanism->StartEnemyLayout(Mode->BattleRules,{3,6},{4,6})))
     {
      AEGChessUnit* Attacker=nullptr;
      for(auto* Unit:Mode->BattleRules->Units())
       if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player){Attacker=Unit;break;}
      auto* First=Mechanism->GetDevice(0);auto* Second=Mechanism->GetDevice(1);
      if(TestNotNull(TEXT("player attacker"),Attacker)&&TestNotNull(TEXT("first device"),First)&&TestNotNull(TEXT("second device"),Second))
      {
       FEGCombatExecution::Damage(Attacker,First,100000.f);
       TestTrue(TEXT("pilot survives loss of one device"),IEGCombatant::Execute_IsAlive(Copy));
       TestTrue(TEXT("other device survives"),IEGCombatant::Execute_IsAlive(Second));
       FEGCombatExecution::Damage(Attacker,Second,100000.f);
       TestFalse(TEXT("pilot dies when both devices are destroyed"),IEGCombatant::Execute_IsAlive(Copy));
      }
     }
    }
   }
  }
 }
 if(Mode)Mode->BattleRules->Stop();
 World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return true;
}
#endif
