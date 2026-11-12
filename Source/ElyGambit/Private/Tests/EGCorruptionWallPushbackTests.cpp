#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
#include "Actor/EGCorruptionBarrier.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/BoxComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"

// Exercises the actual phase transition in a controlled battle. Position,
// separation and state are observed independently of the placement algorithm.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionWallPushbackTest,
 "ElyGambit.Corruption.WallPushbackFormation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionWallPushbackTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("Standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Grid->CellWidth=499.96054f;Grid->CellDepth=500.67874f;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Fixture mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
 auto* Battle=Mode->BattleRules.Get();Battle->CompleteOpeningForTests();
 auto* PC=World->SpawnActor<APlayerController>();if(!TestNotNull(TEXT("Pause controller"),PC))return false;
 if(!PC->PlayerState)PC->SetPlayerState(World->SpawnActor<APlayerState>());
 if(!TestNotNull(TEXT("Pause player state"),PC->PlayerState.Get()))return false;
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Boss=nullptr;
 for(auto* Unit:Battle->Units())if(IsValid(Unit))
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else if(!Boss)Boss=Unit;
 }
 if(!TestNotNull(TEXT("Player"),Hero)||!TestNotNull(TEXT("Boss fixture"),Boss))return false;
 Hero->SetActorLocation(Grid->CellToWorld({3,3})+FVector(0,0,88));
 TArray<AEGChessUnit*> Players;Players.Add(Hero);
 for(int32 I=0;I<8;++I)
 {
  auto* Copy=Mode->SpawnBattleSummon(Hero->GetDefinition(),1,Grid->CellToWorld({I%7,1+I/7})+FVector(0,0,88),EEGFaction::Player,false);
  if(!TestNotNull(TEXT("Additional player"),Copy))return false;
  Copy->GetHeroSkill()->Configure(Battle,1,false);Battle->EnterUnit(Copy);Copy->GetCombat()->SetComponentTickEnabled(false);Players.Add(Copy);
 }
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV.DA_Card_VillV"));
 auto* VillRules=Card&&Card->HeroDefinition?Cast<UEGVillVSkillDefinition>(Card->HeroDefinition->SkillDefinition):nullptr;
 if(!TestNotNull(TEXT("Formal Vill-V device definitions"),VillRules))return false;
 UEGHeroDefinition* DeviceDefs[2]={VillRules->GatlingDefinition.LoadSynchronous(),VillRules->CannonDefinition.LoadSynchronous()};
 for(int32 I=0;I<2;++I)
 {
  if(!TestNotNull(TEXT("Device definition"),DeviceDefs[I]))return false;
  auto* Device=Mode->SpawnBattleEmplacement(DeviceDefs[I],Hero,I,Grid->CellToWorld({I?6:0,3})+FVector(0,0,88));
  if(!TestNotNull(TEXT("Real player emplacement"),Device))return false;
  Device->GetCombat()->SetComponentTickEnabled(false);Players.Add(Device);
 }
 auto* Summon=Battle->SpawnSummon(Hero->GetDefinition(),Hero->GetInstanceId(),1,Grid->CellToWorld({2,3})+FVector(0,0,88),false);
 if(!TestNotNull(TEXT("Live friendly summon"),Summon))return false;
 Summon->GetCombat()->SetComponentTickEnabled(false);Players.Add(Summon);
 // Spawn/BeginCombat may resolve overlaps and move units already created.
 // Set the complete controlled formation only after all actors exist, then
 // check their actual positions and blocking radii before the transition.
 Hero->SetActorLocation(Grid->CellToWorld({3,3})+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
 for(int32 I=0;I<8;++I)
  Players[I+1]->SetActorLocation(Grid->CellToWorld({I%7,1+I/7})+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
 Players[9]->SetActorLocation(Grid->CellToWorld({0,3})+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
 Players[10]->SetActorLocation(Grid->CellToWorld({6,3})+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
 Summon->SetActorLocation(Grid->CellToWorld({2,3})+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
 struct FState{AEGChessUnit* Unit;float Health,Mana,Shield;FVector Location;};TArray<FState> Before;
 Hero->GetHeroSkill()->GrantSourceShield(TEXT("WallPushbackTest"),30.f,30.f);
 for(auto* Player:Players)Before.Add({Player,Player->GetAttributes()->GetHealth(),Player->GetAttributes()->GetMana(),Player->GetAttributes()->GetShield(),Player->GetActorLocation()});
 for(int32 I=0;I<Players.Num();++I)
 {
  AddInfo(FString::Printf(TEXT("FormationStart actor=%s definition=%s position=%s radius=%.3f"),*GetNameSafe(Players[I]),
   *GetNameSafe(Players[I]->GetDefinition()),*Before[I].Location.ToCompactString(),UEGCombatComponent::BlockingRadius(Players[I])));
  for(int32 J=0;J<I;++J)
   TestTrue(TEXT("Initial physical bases are separated"),FVector::Dist2D(Before[I].Location,Before[J].Location)+.1f>=
    UEGCombatComponent::BlockingRadius(Players[I])+UEGCombatComponent::BlockingRadius(Players[J]));
 }
 Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Battle,1000.f,100.f,50);
 if(!TestNotNull(TEXT("Boss rules"),Rules)||!TestTrue(TEXT("Damage begins wall transition"),FEGCombatExecution::Damage(Hero,Boss,100000.f)))return false;
 if(!TestTrue(TEXT("Transition keeps combat held"),Battle->HasPresentationHold()))return false;
 AEGCorruptionBarrier* Wall=nullptr;
 for(TActorIterator<AEGCorruptionBarrier> It(World);It;++It)if(It->GetOwner()==Boss&&!It->IsActorBeingDestroyed()){Wall=*It;break;}
 if(!TestNotNull(TEXT("Collision wall"),Wall))return false;
 auto* Box=FindObject<UBoxComponent>(Wall,TEXT("Wall"));if(!TestNotNull(TEXT("Wall collision box"),Box))return false;
 const FTransform Basis=Grid->GetActorTransform();const FVector WallLocal=Basis.InverseTransformPosition(Wall->GetActorLocation());
 const float ExpectedWallY=Basis.InverseTransformPosition(Grid->CellToWorld({0,4})).Y;
 TestTrue(TEXT("Wall is on row index four"),FMath::IsNearlyEqual(WallLocal.Y,ExpectedWallY,.1f));
 const auto TickWithTerminalDiagnostic=[&]()
 {
  TArray<FVector> PreTick;for(auto* Player:Players)PreTick.Add(Player->GetActorLocation());
  const bool bWasHeld=Battle->HasPresentationHold();Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
  if(bWasHeld&&!Battle->HasPresentationHold())
  {
   AddInfo(FString::Printf(TEXT("FormationEnd fallbackCount=%d"),Rules->GetBarrierPushbackFallbackCount()));
   for(int32 I=0;I<Players.Num();++I)
    AddInfo(FString::Printf(TEXT("FormationEnd actor=%s start=%s preLast=%s final=%s lastStep=%.2f"),*GetNameSafe(Players[I]),
     *Before[I].Location.ToCompactString(),*PreTick[I].ToCompactString(),*Players[I]->GetActorLocation().ToCompactString(),
     FVector::Dist2D(PreTick[I],Players[I]->GetActorLocation())));
  }
 };
 int32 MovingIndex=INDEX_NONE;
 for(int32 Sample=0;Sample<4;++Sample)
 {
  FPlatformProcess::SleepNoStats(.04f);TickWithTerminalDiagnostic();
  for(int32 I=0;I<Players.Num();++I)
  {
   if(FVector::DistSquared2D(Before[I].Location,Players[I]->GetActorLocation())>1.f)MovingIndex=I;
   for(int32 J=0;J<I;++J)
    TestTrue(TEXT("Pushback samples keep physical bases separated"),FVector::Dist2D(Players[I]->GetActorLocation(),Players[J]->GetActorLocation())+.1f>=
     UEGCombatComponent::BlockingRadius(Players[I])+UEGCombatComponent::BlockingRadius(Players[J]));
  }
 }
 TestTrue(TEXT("At least one unit visibly moves during the wall intro"),MovingIndex!=INDEX_NONE);
 auto* MovingUnit=Players[MovingIndex!=INDEX_NONE?MovingIndex:0];const FVector Moving=MovingUnit->GetActorLocation();
 if(TestTrue(TEXT("Independent pause during pushback"),Mode->SetPause(PC)))
 {
  FPlatformProcess::SleepNoStats(.12f);Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
  TestTrue(TEXT("Independent pause freezes pushback"),MovingUnit->GetActorLocation().Equals(Moving,.1f));
  PC->SetPause(false);
 }
 for(int32 Sample=0;Sample<6;++Sample)
 {
  FPlatformProcess::SleepNoStats(.04f);TickWithTerminalDiagnostic();
  for(int32 I=0;I<Players.Num();++I)for(int32 J=0;J<I;++J)
   TestTrue(TEXT("Resumed pushback keeps physical bases separated"),FVector::Dist2D(Players[I]->GetActorLocation(),Players[J]->GetActorLocation())+.1f>=
    UEGCombatComponent::BlockingRadius(Players[I])+UEGCombatComponent::BlockingRadius(Players[J]));
 }
 const double IntroDeadline=FPlatformTime::Seconds()+4.;
 while(Battle->HasPresentationHold()&&FPlatformTime::Seconds()<IntroDeadline)
 {
  FPlatformProcess::SleepNoStats(.04f);TickWithTerminalDiagnostic();
  for(int32 I=0;I<Players.Num();++I)for(int32 J=0;J<I;++J)
   TestTrue(TEXT("Pushback remains separated until the transition ends"),FVector::Dist2D(Players[I]->GetActorLocation(),Players[J]->GetActorLocation())+.1f>=
    UEGCombatComponent::BlockingRadius(Players[I])+UEGCombatComponent::BlockingRadius(Players[J]));
 }
 TestFalse(TEXT("Pushback completes before challenge starts"),Battle->HasPresentationHold());
 TestEqual(TEXT("Normal formation needs no final teleport fallback"),Rules->GetBarrierPushbackFallbackCount(),0);
 TestTrue(TEXT("Full wall challenge remains"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
 const float HalfX=Grid->Columns*Grid->CellWidth*.5f,Bottom=-Grid->Rows*Grid->CellDepth*.5f;
 const float PlayerTop=Bottom+Grid->PlayerRows*Grid->CellDepth;
 int32 BottomRow=0,UpperRows=0;
 for(int32 I=0;I<Players.Num();++I)
 {
  auto* Player=Players[I];const FVector Local=Basis.InverseTransformPosition(Player->GetActorLocation());
  const float Radius=UEGCombatComponent::BlockingRadius(Player);
  TestTrue(TEXT("Whole blocking radius stays inside player deployment footprint"),
   Local.X-Radius>=-HalfX-.1f&&Local.X+Radius<=HalfX+.1f&&Local.Y-Radius>=Bottom-.1f&&Local.Y+Radius<=PlayerTop+.1f);
  TestTrue(TEXT("Whole blocking radius clears wall thickness"),Local.Y+Radius+Box->GetScaledBoxExtent().Y<=WallLocal.Y+.1f);
  FIntPoint Cell;TestTrue(TEXT("Final position belongs to a real board cell"),Grid->TryWorldToCell(Player->GetActorLocation(),Cell));
  if(Cell.Y==0)++BottomRow;else if(Cell.Y>0&&Cell.Y<Grid->PlayerRows)++UpperRows;
  TestTrue(TEXT("Health, mana and shield survive relocation"),FMath::IsNearlyEqual(Player->GetAttributes()->GetHealth(),Before[I].Health,.01f)&&
   FMath::IsNearlyEqual(Player->GetAttributes()->GetMana(),Before[I].Mana,.01f)&&FMath::IsNearlyEqual(Player->GetAttributes()->GetShield(),Before[I].Shield,.01f));
  for(int32 J=0;J<I;++J)
   TestTrue(TEXT("Distinct live actors do not overlap"),FVector::Dist2D(Player->GetActorLocation(),Players[J]->GetActorLocation())+.1f>=Radius+UEGCombatComponent::BlockingRadius(Players[J]));
 }
 TestTrue(TEXT("Bottom row is filled before overflow uses upper rows"),BottomRow>=6&&UpperRows>0);
 const FVector Finished=Hero->GetActorLocation();Battle->Stop();
 Rules->TickComponent(1.f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Battle end leaves no late pushback"),Hero->GetActorLocation().Equals(Finished,.1f));
 TestFalse(TEXT("Battle end clears wall"),Rules->IsWallActive());
 return true;
}

// Replays the eight actual board-space positions logged at the stage-seven
// transitions in MainNaturalPushbackStrict04 and MainNaturalBossReadability07.
// Controlled geometry only: not a replay of AI, timing, status effects or levels.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionWallNaturalGeometryTest,
 "ElyGambit.Corruption.WallPushbackNaturalGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionWallNaturalGeometryTest::RunTest(const FString&)
{
 for(int32 Case=0;Case<2;++Case)
 {
 AddInfo(Case==0?TEXT("Geometry fixture: MainNaturalPushbackStrict04"):TEXT("Geometry fixture: MainNaturalBossReadability07"));
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("Standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Grid->CellWidth=499.96054f;Grid->CellDepth=500.67874f;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Fixture mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
 auto* Battle=Mode->BattleRules.Get();Battle->CompleteOpeningForTests();
 // Remove only the sandbox's temporary pawns; this fixture supplies the real
 // identities and actual logged geometry explicitly below.
 for(auto* Unit:Battle->Units())if(IsValid(Unit))Unit->Destroy();
 const TCHAR* Paths[]={
  TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV.DA_VillV"),
  TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"),
  TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Human.DA_Human"),
  TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Sakura.DA_Sakura"),
  TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Griseo.DA_Griseo"),
  TEXT("/Game/Blueprints/Characters/Griseo/Summons/Knight/Data/DA_Knight.DA_Knight"),
  TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption")};
 UEGHeroDefinition* Defs[UE_ARRAY_COUNT(Paths)]={};
 for(int32 I=0;I<UE_ARRAY_COUNT(Paths);++I)
  if(!TestNotNull(TEXT("Formal unit definition"),Defs[I]=LoadObject<UEGHeroDefinition>(nullptr,Paths[I])))return false;
 auto* VillRules=Cast<UEGVillVSkillDefinition>(Defs[0]->SkillDefinition);
 if(!TestNotNull(TEXT("Formal Vill-V device configuration"),VillRules))return false;
 auto* GatlingDef=VillRules->GatlingDefinition.LoadSynchronous();auto* CannonDef=VillRules->CannonDefinition.LoadSynchronous();
 if(!TestNotNull(TEXT("Gatling definition"),GatlingDef)||!TestNotNull(TEXT("Cannon definition"),CannonDef))return false;
 auto Spawn=[&](UEGHeroDefinition* Definition,EEGFaction Faction)
 {
  auto* Result=Mode->SpawnBattleSummon(Definition,1,Grid->CellToWorld({3,3})+FVector(0,0,88),Faction,false);
  if(Result){Result->GetHeroSkill()->Configure(Battle,1,false);Battle->EnterUnit(Result);Result->GetCombat()->SetComponentTickEnabled(false);}
  return Result;
 };
 auto* Vill=Spawn(Defs[0],EEGFaction::Player);auto* Elysia=Spawn(Defs[1],EEGFaction::Player);
 auto* Human=Spawn(Defs[2],EEGFaction::Player);auto* Sakura=Spawn(Defs[3],EEGFaction::Player);
 auto* Griseo=Spawn(Defs[4],EEGFaction::Player);auto* Boss=Spawn(Defs[6],EEGFaction::Enemy);
 if(!TestNotNull(TEXT("Vill-V"),Vill)||!TestNotNull(TEXT("Elysia"),Elysia)||!TestNotNull(TEXT("Human"),Human)||
    !TestNotNull(TEXT("Sakura"),Sakura)||!TestNotNull(TEXT("Griseo"),Griseo)||!TestNotNull(TEXT("Formal corruption boss"),Boss))return false;
 auto* Gatling=Mode->SpawnBattleEmplacement(GatlingDef,Vill,0,Grid->CellToWorld({0,2})+FVector(0,0,88));
 auto* Cannon=Mode->SpawnBattleEmplacement(CannonDef,Vill,1,Grid->CellToWorld({1,2})+FVector(0,0,88));
 // Griseo's real BeginCombat may already have created her once-per-round
 // knight. SpawnKnight returns that live owned summon or creates it through
 // the same production transaction; SpawnSummon would reject a duplicate.
 auto* Knight=Battle->SpawnKnight(Griseo,1.f,1.f);
 if(!TestNotNull(TEXT("Real gatling"),Gatling)||!TestNotNull(TEXT("Real cannon"),Cannon)||!TestNotNull(TEXT("Real knight summon"),Knight))return false;
 if(!TestTrue(TEXT("Knight is the formal owned summon"),Knight->GetDefinition()==Defs[5]&&Knight->GetHeroSkill()->bSummon&&
   Knight->GetHeroSkill()->SummonerId==Griseo->GetInstanceId()))return false;
 Gatling->GetCombat()->SetComponentTickEnabled(false);Cannon->GetCombat()->SetComponentTickEnabled(false);Knight->GetCombat()->SetComponentTickEnabled(false);
 TArray<AEGChessUnit*> Players={Gatling,Cannon,Vill,Elysia,Human,Sakura,Knight,Griseo};
 const FVector Starts04[]={FVector(-499.96,-1502.04,87.53),FVector(-999.92,-1001.36,87.65),FVector(-999.92,-1001.36,87.65),
  FVector(928.12,-409.13,88),FVector(-1452.51,214.96,88),FVector(54.62,594.85,88),FVector(493.89,842.71,88),FVector(-475.48,958.49,88)};
 const FVector Starts07[]={FVector(-499.96,-1502.04,87.53),FVector(-999.92,-1001.36,87.65),FVector(-999.92,-1001.36,87.65),
  FVector(927.68,-405.90,88),FVector(-1452.48,217.26,88),FVector(54.18,598.08,88),FVector(494.02,848.49,88),FVector(-475.43,962.56,88)};
 const FVector* Starts=Case==0?Starts04:Starts07;
 const FTransform Basis=Grid->GetActorTransform();
 for(int32 I=0;I<Players.Num();++I)Players[I]->SetActorLocation(Basis.TransformPosition(Starts[I]),false,nullptr,ETeleportType::TeleportPhysics);
 TArray<float> PreviousDistances;PreviousDistances.Init(0.f,Players.Num()*Players.Num());
 TArray<FVector> PreviousPositions;for(auto* Player:Players)PreviousPositions.Add(Player->GetActorLocation());
 for(int32 I=0;I<Players.Num();++I)for(int32 J=0;J<I;++J)
 {
  const float Distance=FVector::Dist2D(Players[I]->GetActorLocation(),Players[J]->GetActorLocation());
  PreviousDistances[I*Players.Num()+J]=Distance;
  const float Required=UEGCombatComponent::BlockingRadius(Players[I])+UEGCombatComponent::BlockingRadius(Players[J]);
  if(I==2&&J==1)TestTrue(TEXT("Recorded Vill-V pilot and cannon begin coincident"),Distance<.1f);
  else TestTrue(TEXT("Other recorded physical bases begin separate"),Distance+.1f>=Required);
 }
 Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Battle,1000.f,100.f,50);
 if(!TestNotNull(TEXT("Boss rules"),Rules)||!TestTrue(TEXT("Damage begins actual wall transition"),FEGCombatExecution::Damage(Elysia,Boss,100000.f)))return false;
 if(!TestTrue(TEXT("Formal source animation owns transition"),Rules->IsBarrierIntroPresentationActive()))return false;
 const double Deadline=FPlatformTime::Seconds()+5.;
 while(Battle->HasPresentationHold()&&FPlatformTime::Seconds()<Deadline)
 {
  FPlatformProcess::SleepNoStats(.04f);Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
  TArray<FVector> CurrentPositions;for(auto* Player:Players)CurrentPositions.Add(Player->GetActorLocation());
  for(int32 I=0;I<Players.Num();++I)for(int32 J=0;J<I;++J)
  {
   const float Distance=FVector::Dist2D(CurrentPositions[I],CurrentPositions[J]);
   const float Required=UEGCombatComponent::BlockingRadius(Players[I])+UEGCombatComponent::BlockingRadius(Players[J]);
   const float Previous=PreviousDistances[I*Players.Num()+J];
   TestTrue(TEXT("Recorded initial overlap only separates and no new overlap appears"),
    Previous+.1f<Required?Distance+.1f>=Previous:Distance+.1f>=Required);
   // Relative-motion sweep detects crossings between samples even if both
   // endpoints are separate. An existing overlap must not deepen either.
   const FVector2D RelativeStart(PreviousPositions[I].X-PreviousPositions[J].X,PreviousPositions[I].Y-PreviousPositions[J].Y);
   const FVector2D RelativeEnd(CurrentPositions[I].X-CurrentPositions[J].X,CurrentPositions[I].Y-CurrentPositions[J].Y);
   const FVector2D Delta=RelativeEnd-RelativeStart;
   const double Fraction=Delta.SizeSquared()>UE_DOUBLE_SMALL_NUMBER?FMath::Clamp(-FVector2D::DotProduct(RelativeStart,Delta)/Delta.SizeSquared(),0.,1.):0.;
   const double MinimumDistance=(RelativeStart+Delta*Fraction).Size();
   TestTrue(TEXT("Swept bases do not cross or deepen an initial overlap"),
    Previous+.1f<Required?MinimumDistance+.1>=Previous:MinimumDistance+.1>=Required);
   PreviousDistances[I*Players.Num()+J]=Distance;
  }
  PreviousPositions=MoveTemp(CurrentPositions);
 }
 TestFalse(TEXT("Source transition ends"),Battle->HasPresentationHold());
 TestEqual(TEXT("Recorded natural formation has no final teleport fallback"),Rules->GetBarrierPushbackFallbackCount(),0);
 const float HalfX=Grid->Columns*Grid->CellWidth*.5f,Bottom=-Grid->Rows*Grid->CellDepth*.5f;
 const float PlayerTop=Bottom+Grid->PlayerRows*Grid->CellDepth;
 for(int32 I=0;I<Players.Num();++I)
 {
  const FVector Local=Basis.InverseTransformPosition(Players[I]->GetActorLocation());const float Radius=UEGCombatComponent::BlockingRadius(Players[I]);
  TestTrue(TEXT("Natural-geometry destination stays in player rows"),Local.X-Radius>=-HalfX-.1f&&Local.X+Radius<=HalfX+.1f&&
   Local.Y-Radius>=Bottom-.1f&&Local.Y+Radius<=PlayerTop+.1f);
  for(int32 J=0;J<I;++J)TestTrue(TEXT("Natural-geometry final bases are separate"),
   FVector::Dist2D(Players[I]->GetActorLocation(),Players[J]->GetActorLocation())+.1f>=Radius+UEGCombatComponent::BlockingRadius(Players[J]));
 }
 Battle->Stop();
 }
 return true;
}
#endif
