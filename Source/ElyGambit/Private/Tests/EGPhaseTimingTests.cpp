#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/ActorComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Character/EGChessUnit.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGBoardScene.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Presentation/EGSettlementPresentation.h"

namespace
{
/** A standalone production-rule fixture. World ticks inject elapsed real seconds. */
struct FPhaseFixture
{
 TStrongObjectPtr<UGameInstance> Instance;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGCardManager* Cards=nullptr;

 bool Initialize(FAutomationTestBase& Test)
 {
  auto* Seed=EGProductionTestAssets::ResolveRun(Test);
  if(!Seed||!GEngine)return false;
  Instance.Reset(NewObject<UGameInstance>(GEngine));
  Instance->InitializeStandalone();
  World=Instance->GetWorld();
  if(!Test.TestNotNull(TEXT("Phase fixture world"),World))return false;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);
  Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Test.TestNotNull(TEXT("Phase fixture game mode"),Mode))return false;
  Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
  if(!Test.TestNotNull(TEXT("Phase fixture campaign"),Mode->RunDefinition.Get()))return false;
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Cards=World->SpawnActor<AEGCardManager>();
  auto* Board=World->SpawnActor<AEGBoard>();
  if(!Scene||!Grid||!Cards||!Board){Test.AddError(TEXT("Phase fixture actors failed to spawn"));return false;}
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  if(!Test.TestNotNull(TEXT("Phase fixture match"),Mode->GetMatch()))return false;
  for(int32 Frame=0;Frame<300&&!Mode->AreStageResourcesReady();++Frame)TickWorld(.02f);
  return Test.TestTrue(TEXT("Phase fixture stage resources are ready"),Mode->AreStageResourcesReady());
 }

 void TickWorld(float Delta)
 {
  ++GFrameCounter;
  World->Tick(LEVELTICK_All,Delta);
 }

 void TickClock(float RealSeconds)
 {
  while(RealSeconds>0.f)
  {
   const float Step=FMath::Min(.05f,RealSeconds);
   TickWorld(Step);
   RealSeconds-=Step;
  }
 }

 void TickBossClock(float RealSeconds,FAutomationTestBase& Test)
 {
  const auto PreviousPhase=Mode->GetMatch()->GetPhase();
  bool bReported=false;
  while(RealSeconds>0.f)
  {
   const float Step=FMath::Min(.05f,RealSeconds);
   TickWorld(Step);
   RealSeconds-=Step;
   if(!bReported&&Mode->GetMatch()->GetPhase()!=PreviousPhase)
   {
    bReported=true;
    Test.AddInfo(FString::Printf(TEXT("Boss clock phase changed: phase=%d victory=%d battle=%.3f corruption=%d units=%d"),
     static_cast<int32>(Mode->GetMatch()->GetPhase()),Mode->WasVictory()?1:0,Mode->GetBattleSeconds(),
     Mode->GetMatch()->GetCorruption(),Mode->GetCombatUnits().Num()));
    for(const AEGChessUnit* Unit:Mode->GetCombatUnits())if(IsValid(Unit))
     Test.AddInfo(FString::Printf(TEXT("Boss clock unit=%s faction=%d alive=%d hp=%.2f"),*Unit->GetName(),
      static_cast<int32>(Unit->GetFaction_Implementation()),Unit->IsAlive_Implementation()?1:0,
      Unit->GetAttributes()?Unit->GetAttributes()->GetHealth():-1.f));
   }
  }
 }

 void FreezeCombatUnits()
 {
  for(AEGChessUnit* Unit:Mode->GetCombatUnits())
  {
   if(!IsValid(Unit))continue;
   Unit->SetActorTickEnabled(false);
   TInlineComponentArray<UActorComponent*> Components;
   Unit->GetComponents(Components);
   for(UActorComponent* Component:Components)if(Component)Component->SetComponentTickEnabled(false);
  }
 }

 bool DeployOpeningCard(FAutomationTestBase& Test)
 {
  const auto Hand=Cards->GetHand();
  return Test.TestTrue(TEXT("Opening card exists"),!Hand.IsEmpty())&&
   Test.TestTrue(TEXT("Opening card deploys"),Cards->TryDeployHandCard(Hand[0].InstanceId,FIntPoint(3,1)));
 }

 ~FPhaseFixture()
 {
  if(!World||!GEngine)return;
  World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
  Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPhasePreparationTest,"ElyGambit.Phase.Preparation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPhasePreparationTest::RunTest(const FString&)
{
 {
  FPhaseFixture Fixture;if(!Fixture.Initialize(*this))return false;
  auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();
  TestEqual(TEXT("Ready stage starts in preparation"),Match->GetPhase(),EEGMatchPhase::Preparation);
  const auto Start=Mode->GetPhaseClock();
  TestTrue(TEXT("Ready preparation has a countdown"),Start.bHasCountdown);
  TestTrue(TEXT("Preparation starts near 45 real seconds"),Start.RemainingRealSeconds>44.f&&Start.RemainingRealSeconds<=45.f);
  Fixture.TickClock(10.f);
  TestTrue(TEXT("Preparation consumes ten real seconds"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,Start.RemainingRealSeconds-10.f,.15f));
  if(!Fixture.DeployOpeningCard(*this))return false;
  if(!TestTrue(TEXT("Player can start before preparation expires"),Mode->EnterCombat()))return false;
  TestEqual(TEXT("Early start enters combat"),Match->GetPhase(),EEGMatchPhase::Combat);
  TestTrue(TEXT("Early combat starts near 25 real seconds"),Mode->GetPhaseClock().RemainingRealSeconds>24.9f);
 }
 {
  FPhaseFixture Fixture;if(!Fixture.Initialize(*this))return false;
  if(!Fixture.DeployOpeningCard(*this))return false;
  Fixture.TickClock(44.f);
  TestEqual(TEXT("Preparation has not auto-started early"),Fixture.Mode->GetMatch()->GetPhase(),EEGMatchPhase::Preparation);
  Fixture.TickClock(1.1f);
  TestEqual(TEXT("Deployed formation auto-starts at 45 seconds"),Fixture.Mode->GetMatch()->GetPhase(),EEGMatchPhase::Combat);
 }
 {
  FPhaseFixture Fixture;if(!Fixture.Initialize(*this))return false;
  Fixture.TickClock(44.f);
  TestEqual(TEXT("Empty preparation waits for deadline"),Fixture.Mode->GetMatch()->GetPhase(),EEGMatchPhase::Preparation);
  Fixture.TickClock(1.1f);
  TestEqual(TEXT("Empty formation auto-resolves as loss"),Fixture.Mode->GetMatch()->GetPhase(),EEGMatchPhase::Settlement);
  TestFalse(TEXT("Empty formation did not win"),Fixture.Mode->WasVictory());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPhaseOvertimeTest,"ElyGambit.Phase.Overtime",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPhaseOvertimeTest::RunTest(const FString&)
{
 FPhaseFixture Fixture;if(!Fixture.Initialize(*this)||!Fixture.DeployOpeningCard(*this))return false;
 auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();
 if(!TestTrue(TEXT("Combat begins"),Mode->EnterCombat()))return false;
 if(!TestTrue(TEXT("No-opening fixture is running"),Mode->BattleRules&&Mode->BattleRules->IsCombatRunning()))return false;
 Fixture.FreezeCombatUnits();
 Fixture.TickClock(24.5f);
 TestFalse(TEXT("Normal phase remains before 25 real seconds"),Mode->GetPhaseClock().bOvertime);
 TestTrue(TEXT("Normal countdown remains half a second"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,.5f,.15f));
 Fixture.TickClock(1.f);
 const auto Overtime=Mode->GetPhaseClock();
 TestTrue(TEXT("Cross-frame transition enters overtime"),Overtime.bOvertime);
 TestTrue(TEXT("Cross-frame overtime retains excess real time"),FMath::IsNearlyEqual(Overtime.RemainingRealSeconds,14.5f,.15f));
 TestTrue(TEXT("Overtime speed is 1.5x"),FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(Fixture.World),1.5f,.01f));
 Fixture.TickClock(14.4f);
 TestEqual(TEXT("Overtime is active before 15 seconds"),Match->GetPhase(),EEGMatchPhase::Combat);
 Fixture.TickClock(.2f);
 TestEqual(TEXT("Overtime expires into settlement"),Match->GetPhase(),EEGMatchPhase::Settlement);
 TestFalse(TEXT("Overtime expiry with enemies alive is defeat"),Mode->WasVictory());
 TestTrue(TEXT("Settlement restores original speed"),FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(Fixture.World),1.f,.01f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPhaseSettlementRealTimeTest,"ElyGambit.Phase.SettlementRealTime",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPhaseSettlementRealTimeTest::RunTest(const FString&)
{
 for(const bool bVictory:{true,false})for(const float Dilation:{.5f,2.f})
 {
  FPhaseFixture Fixture;if(!Fixture.Initialize(*this)||!Fixture.DeployOpeningCard(*this))return false;
  auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();
  const int32 CorruptionBefore=Match->GetCorruption();
  if(!TestTrue(TEXT("Settlement fixture enters combat"),Mode->EnterCombat())||
     !TestTrue(TEXT("Settlement fixture resolves combat"),Mode->ResolveCombat(bVictory)))return false;
  if(!TestEqual(TEXT("Resolved combat starts settlement"),Match->GetPhase(),EEGMatchPhase::Settlement))return false;
  AEGSettlementPresentation* Presentation=nullptr;
  for(TActorIterator<AEGSettlementPresentation> It(Fixture.World);It;++It)
   if(It->GetOwner()==Mode){Presentation=*It;break;}
  if(!TestNotNull(TEXT("Settlement presentation exists before interruption"),Presentation))return false;
  if(bVictory)Presentation->OnFinished.Broadcast();
  else Presentation->Destroy();
  TestEqual(TEXT("Early visual completion or missing actor cannot advance settlement"),Match->GetPhase(),EEGMatchPhase::Settlement);
  UGameplayStatics::SetGlobalTimeDilation(Fixture.World,Dilation);
  const auto Initial=Mode->GetPhaseClock();
  TestTrue(TEXT("Settlement HUD starts with six real seconds"),Initial.bHasCountdown&&FMath::IsNearlyEqual(Initial.RemainingRealSeconds,6.f,.1f));
  Fixture.TickClock(5.75f);
  TestEqual(TEXT("Settlement remains before six real seconds"),Match->GetPhase(),EEGMatchPhase::Settlement);
  const auto NearEnd=Mode->GetPhaseClock();
  TestTrue(TEXT("HUD follows real time despite time dilation"),NearEnd.bHasCountdown&&FMath::IsNearlyEqual(NearEnd.RemainingRealSeconds,.25f,.15f));
  Fixture.TickClock(.4f);
  TestTrue(TEXT("Settlement advances after six real seconds"),Match->GetPhase()!=EEGMatchPhase::Settlement);
  TestEqual(TEXT("Settlement applies the stage impact exactly once"),Match->GetCorruption(),FMath::Clamp(CorruptionBefore+(bVictory?-5:5),0,200));
  const auto PhaseAfter=Match->GetPhase();const int32 StageAfter=Match->GetStageNumber();const int32 CorruptionAfter=Match->GetCorruption();
  Fixture.TickClock(2.f);
  TestEqual(TEXT("Settlement cannot advance a second time"),Match->GetPhase(),PhaseAfter);
  TestEqual(TEXT("Settlement cannot increment the stage twice"),Match->GetStageNumber(),StageAfter);
  TestEqual(TEXT("Settlement impact is applied at most once"),Match->GetCorruption(),CorruptionAfter);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPhaseSettlementRestartTest,"ElyGambit.Phase.SettlementRestart",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPhaseSettlementRestartTest::RunTest(const FString&)
{
 {
  FPhaseFixture Fixture;if(!Fixture.Initialize(*this)||!Fixture.DeployOpeningCard(*this))return false;
  auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();
  if(!TestTrue(TEXT("Terminal fixture enters combat"),Mode->EnterCombat())||
     !TestTrue(TEXT("Terminal fixture enters settlement"),Mode->ResolveCombat(false)))return false;
  Fixture.TickClock(1.f);Match->AddCorruption(200);
  if(!TestEqual(TEXT("Corruption ends run during settlement"),Match->GetPhase(),EEGMatchPhase::Finished))return false;
  Fixture.TickClock(6.5f);
  TestEqual(TEXT("Expired settlement timer cannot alter finished run"),Match->GetPhase(),EEGMatchPhase::Finished);
 }
 {
 FPhaseFixture Fixture;if(!Fixture.Initialize(*this)||!Fixture.DeployOpeningCard(*this))return false;
 auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();
 if(!TestTrue(TEXT("Restart fixture enters combat"),Mode->EnterCombat())||
    !TestTrue(TEXT("Restart fixture enters settlement"),Mode->ResolveCombat(false)))return false;
 Fixture.TickClock(1.f);
 Match->AddCorruption(200);
 if(!TestEqual(TEXT("Restart fixture reaches terminal state"),Match->GetPhase(),EEGMatchPhase::Finished))return false;
 if(!TestTrue(TEXT("Finished run can restart"),Mode->StartGame()))return false;
 Fixture.TickClock(6.5f);
 TestEqual(TEXT("Old settlement timer cannot alter restarted preparation"),Match->GetPhase(),EEGMatchPhase::Preparation);
 TestEqual(TEXT("Restart begins at first stage"),Match->GetStageNumber(),1);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPhaseBossClockTest,"ElyGambit.Phase.BossClock",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPhaseBossClockTest::RunTest(const FString&)
{
 FPhaseFixture Fixture;if(!Fixture.Initialize(*this)||!Fixture.DeployOpeningCard(*this))return false;
 auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();
 FGuid PreviousRound;
 for(int32 Stage=1;Stage<=6;++Stage)
 {
  if(!TestEqual(TEXT("Controlled stage"),Match->GetStageNumber(),Stage)||
     !TestTrue(TEXT("Controlled stage begins"),Mode->EnterCombat()))return false;
  PreviousRound=Mode->BattleRules->GetRoundId();
  if(!TestTrue(TEXT("Controlled victory"),Mode->ResolveCombat(true)))return false;
  for(int32 Frame=0;Frame<300&&Match->GetStageNumber()==Stage;++Frame)
  {
   Fixture.TickWorld(.1f);
   if(Match->GetPhase()==EEGMatchPhase::Reward)
   {
    if(Mode->IsChoosingEndgameRoute())
    {
     if(!TestTrue(TEXT("Controlled final route commits"),Mode->ChooseEndgameRoute(TEXT("Unity"),Mode->GetStigmataRevision())))return false;
     continue;
    }
    const auto Offer=Mode->GetStigmataOffer();
    if(!TestEqual(TEXT("Controlled reward has three options"),Offer.Num(),3))return false;
    const FName Choice=Stage==6?Offer.Last():Offer[0];
    if(!TestTrue(TEXT("Controlled reward commits"),Mode->ChooseStigmata(Choice,Mode->GetStigmataRevision())))return false;
   }
  }
  if(!TestEqual(TEXT("Automatic advance reaches next stage"),Match->GetStageNumber(),Stage+1))return false;
  for(int32 Frame=0;Frame<300&&!Mode->AreStageResourcesReady();++Frame)Fixture.TickWorld(.02f);
  if(!TestTrue(TEXT("Next stage resources ready"),Mode->AreStageResourcesReady()))return false;
 }
 // Controlled injected card supplies a real opening item without relying on a
 // particular random stigmata offer. This fixture checks timing, not balance.
 auto* OpeningCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo"));
 if(!TestNotNull(TEXT("Opening fixture card"),OpeningCard)||!TestTrue(TEXT("Grant opening fixture card"),Fixture.Cards->GrantCard(OpeningCard)))return false;
 const FGuid OpeningCardId=Fixture.Cards->GetHand().Last().InstanceId;
 if(!TestTrue(TEXT("Deploy opening fixture card"),Fixture.Cards->TryDeployHandCard(OpeningCardId,FIntPoint(1,1))))return false;
 if(!TestTrue(TEXT("Boss combat begins"),Mode->EnterCombat()))return false;
 if(!TestFalse(TEXT("Summoner opening delays combat running"),Mode->BattleRules->IsCombatRunning()))return false;
 Fixture.FreezeCombatUnits();
 Mode->BattleRules->SetComponentTickEnabled(false);
 TestFalse(TEXT("Boss no longer uses corruption countdown"),Mode->GetPhaseClock().bHasCorruptionCountdown);
 Fixture.TickClock(2.f);
 TestTrue(TEXT("Opening preserves full 60-second enrage clock"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,60.f,.15f));
 TestEqual(TEXT("Opening does not advance effective battle seconds"),Mode->GetBattleSeconds(),0.f);
 for(int32 Item=0;Item<16&&!Mode->BattleRules->IsCombatRunning();++Item)
  Mode->BattleRules->AdvanceOpeningForTests(1.01f);
 if(!TestTrue(TEXT("Opening eventually enters running"),Mode->BattleRules->IsCombatRunning()))return false;
 TestTrue(TEXT("Boss enrage clock starts full after opening"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,60.f,.15f));
 // Running attaches the boss combat component after the first freeze. Freeze
 // that newly-created component as well so this fixture measures the clock.
 Fixture.FreezeCombatUnits();
 UEGCorruptionCombatComponent* BossRules=nullptr;
 for(AEGChessUnit* Unit:Mode->GetCombatUnits())if(IsValid(Unit))
  if(auto* Corruption=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();Corruption&&Corruption->IsBoss())
  {
   BossRules=Corruption;
   TestFalse(TEXT("Controlled boss combat component is not ticking"),Corruption->IsComponentTickEnabled());
  }
 if(!TestNotNull(TEXT("Controlled boss combat component exists"),BossRules))return false;
 Mode->BattleRules->SetComponentTickEnabled(true);
 Mode->BattleRules->OnCombatRunning.Broadcast(PreviousRound);
 Fixture.TickClock(25.1f);
 const auto BeforeEnrage=Mode->GetPhaseClock();
 TestTrue(TEXT("Boss remains at normal speed after 25 seconds"),!BeforeEnrage.bOvertime&&BeforeEnrage.bHasCountdown&&FMath::IsNearlyEqual(BeforeEnrage.Speed,1.f));
 TestFalse(TEXT("Old corruption countdown is not active"),BeforeEnrage.bHasCorruptionCountdown);
 TestTrue(TEXT("Stale running event does not freeze the current battle"),FMath::IsNearlyEqual(Mode->GetBattleSeconds(),25.15f,.2f));
 const int32 InitialCorruption=Match->GetCorruption();
 Fixture.TickBossClock(FMath::Max(0.f,59.8f-Mode->GetBattleSeconds()),*this);
 TestEqual(TEXT("Boss remains in combat just before enrage"),Match->GetPhase(),EEGMatchPhase::Combat);
 TestFalse(TEXT("Boss has not enraged early"),BossRules->IsEnraged());
 Fixture.TickClock(.3f);
 TestTrue(TEXT("Boss enrages after sixty effective seconds"),BossRules->IsEnraged());
 TestTrue(TEXT("Boss uses damage multiplier instead of game speed"),BossRules->GetOutgoingDamageMultiplier()>=1.1f&&FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(Fixture.World),1.f,.01f));
 TestTrue(TEXT("HUD marks enrage without a hard countdown"),Mode->GetPhaseClock().bOvertime&&!Mode->GetPhaseClock().bHasCountdown);
 Fixture.TickClock(5.1f);
 TestTrue(TEXT("Boss gains another ten percent after five seconds"),BossRules->GetOutgoingDamageMultiplier()>=1.2f);
 TestEqual(TEXT("Long fight does not accumulate old corruption ticks"),Match->GetCorruption(),InitialCorruption);
 TestTrue(TEXT("Boss combat can be interrupted"),Mode->ResolveCombat(false));
 TestEqual(TEXT("Final stage ends directly without settlement"),Match->GetPhase(),EEGMatchPhase::Finished);
 Fixture.TickClock(6.5f);
 TestEqual(TEXT("Final stage remains finished after six seconds"),Match->GetPhase(),EEGMatchPhase::Finished);
 TestTrue(TEXT("Boss finish restores normal speed"),FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(Fixture.World),1.f,.01f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBalanceReverieEconomyTest,"ElyGambit.BalanceRework.Economy.ReverieReceiptAndRefresh",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBalanceReverieEconomyTest::RunTest(const FString&)
{
 int32 ExternalSeed=0;
 if(FParse::Value(FCommandLine::Get(),TEXT("EGBalanceSeed="),ExternalSeed))
 {AddError(TEXT("Reverie seed search requires no EGBalanceSeed command-line override"));return false;}
 FPhaseFixture Fixture;if(!Fixture.Initialize(*this))return false;
 auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();auto* Cards=Fixture.Cards;
 auto Wait=[&](TFunctionRef<bool()> Ready)
 {
  for(int32 Frame=0;Frame<600&&!Ready();++Frame)Fixture.TickWorld(.1f);
  return Ready();
 };
 bool bFound=false;
 for(int32 Seed=0;Seed<80&&!bFound;++Seed)
 {
  Match->AddCorruption(200);
  Mode->RunDefinition->EncounterSeed=Seed;
  if(!TestTrue(TEXT("Start deterministic candidate search run"),Mode->StartGame())||
     !TestTrue(TEXT("Candidate search resources ready"),Wait([&](){return Mode->AreStageResourcesReady();}))||
     !TestEqual(TEXT("Candidate search seed is active"),Mode->GetBalanceSeed(),Seed)||
     !Fixture.DeployOpeningCard(*this))return false;
  for(int32 Stage=1;Stage<=2;++Stage)
  {
   if(!TestTrue(TEXT("Controlled search stage starts"),Mode->EnterCombat())||
      !TestTrue(TEXT("Controlled search stage resolves"),Mode->ResolveCombat(true)))return false;
   if(Stage==1)
   {
    if(!TestTrue(TEXT("Search advances to stage two"),Wait([&](){return Match->GetStageNumber()==2&&Mode->AreStageResourcesReady();})))return false;
   }
   else if(!TestTrue(TEXT("Search reaches stage-two ordinary reward"),Wait([&](){return Match->GetPhase()==EEGMatchPhase::Reward&&!Mode->IsChoosingEndgameRoute();})))return false;
  }
  const auto Offer=Mode->GetStigmataOffer();
  if(!Offer.Contains(TEXT("Reverie")))continue;
  bFound=true;AddInfo(FString::Printf(TEXT("Reverie appeared through normal offer generation at seed %d"),Seed));
  const int64 Before=Cards->GetGold();const int32 Revision=Mode->GetStigmataRevision();
  TestFalse(TEXT("Stale Reverie request cannot grant gold"),Mode->ChooseStigmata(TEXT("Reverie"),Revision-1));
  if(!TestTrue(TEXT("Offered Reverie is claimed through production transaction"),Mode->ChooseStigmata(TEXT("Reverie"),Revision)))return false;
  TestEqual(TEXT("Reverie grants three once"),Cards->GetGold(),Before+3);
  TestEqual(TEXT("Reverie income ledger records three"),Match->GetStigmataGoldEarned(TEXT("Reverie")),int64(3));
  TestFalse(TEXT("Duplicate Reverie request is rejected"),Mode->ChooseStigmata(TEXT("Reverie"),Revision));
  TestEqual(TEXT("Duplicate request does not pay twice"),Cards->GetGold(),Before+3);
  if(!TestTrue(TEXT("After reward preparation reaches stage three"),Wait([&](){return Match->GetStageNumber()==3&&Mode->AreStageResourcesReady();})))return false;
  const int64 Gold=Cards->GetGold();
  TestEqual(TEXT("First stage-three refresh is free"),Cards->GetRefreshCost(),0);
  TestTrue(TEXT("Base free refresh commits"),Cards->RefreshShop());
  TestEqual(TEXT("Base free refresh is not credited to Reverie"),Match->GetStigmataRefreshSavings(TEXT("Reverie")),int64(0));
  TestEqual(TEXT("Reverie adds a second free refresh"),Cards->GetRefreshCost(),0);
  TestTrue(TEXT("Reverie free refresh commits"),Cards->RefreshShop());
  TestEqual(TEXT("Two free refreshes spend no gold"),Cards->GetGold(),Gold);
  TestEqual(TEXT("Reverie savings ledger records its refresh"),Match->GetStigmataRefreshSavings(TEXT("Reverie")),int64(1));
  TestEqual(TEXT("Third refresh is paid"),Cards->GetRefreshCost(),1);
  TestTrue(TEXT("Paid refresh commits"),Cards->RefreshShop());
  TestEqual(TEXT("Paid refresh spends one"),Cards->GetGold(),Gold-1);
  TestEqual(TEXT("Paid refresh does not increase savings"),Match->GetStigmataRefreshSavings(TEXT("Reverie")),int64(1));
  if(!TestTrue(TEXT("Controlled third stage starts"),Mode->EnterCombat())||!TestTrue(TEXT("Controlled third stage resolves"),Mode->ResolveCombat(true))||
     !TestTrue(TEXT("Fourth stage preparation is ready"),Wait([&](){return Match->GetStageNumber()==4&&Mode->AreStageResourcesReady();})))return false;
  TestEqual(TEXT("Free refresh replenishes next preparation"),Cards->GetRefreshCost(),0);
  TestTrue(TEXT("Fourth stage base refresh commits"),Cards->RefreshShop());
  TestTrue(TEXT("Fourth stage Reverie refresh commits"),Cards->RefreshShop());
  TestEqual(TEXT("Savings accumulate across stages"),Match->GetStigmataRefreshSavings(TEXT("Reverie")),int64(2));
  Match->AddCorruption(200);
  if(!TestTrue(TEXT("Restart after economy test"),Mode->StartGame()))return false;
  TestEqual(TEXT("Restart clears Reverie income ledger"),Match->GetStigmataGoldEarned(TEXT("Reverie")),int64(0));
  TestEqual(TEXT("Restart clears Reverie savings ledger"),Match->GetStigmataRefreshSavings(TEXT("Reverie")),int64(0));
  TestEqual(TEXT("Restart restores base free refresh"),Cards->GetRefreshCost(),0);
 }
 return TestTrue(TEXT("At least one real stage-two offer contains Reverie"),bFound);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBalanceDuskEconomyTest,"ElyGambit.BalanceRework.Economy.DuskReceiptAndSettlement",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBalanceDuskEconomyTest::RunTest(const FString&)
{
 int32 ExternalSeed=0;
 if(FParse::Value(FCommandLine::Get(),TEXT("EGBalanceSeed="),ExternalSeed))
 {AddError(TEXT("Dusk seed search requires no EGBalanceSeed command-line override"));return false;}
 FPhaseFixture Fixture;if(!Fixture.Initialize(*this))return false;
 auto* Mode=Fixture.Mode;auto* Match=Mode->GetMatch();auto* Cards=Fixture.Cards;
 auto Wait=[&](TFunctionRef<bool()> Ready)
 {
  for(int32 Frame=0;Frame<600&&!Ready();++Frame)Fixture.TickWorld(.1f);
  return Ready();
 };
 const auto Wanted=[](EEGHeroKind Kind)
 {return Kind==EEGHeroKind::Pardofelis||Kind==EEGHeroKind::Sakura||Kind==EEGHeroKind::Aponia;};
 auto Prepare=[&]()
 {
  for(int32 Attempts=0;Attempts<60;++Attempts)
  {
   bool bChanged=false;
   for(const auto& Card:Cards->GetHand())
   {
    if(!Card.Definition||!Card.Definition->HeroDefinition||!Wanted(Card.Definition->HeroDefinition->HeroKind)||
       Mode->GetRoster()->FindHeroByDefinition(Card.Definition->HeroDefinition).IsValid())continue;
    for(int32 Y=0;Y<3&&!bChanged;++Y)for(int32 X=0;X<7&&!bChanged;++X)
     if(Cards->EvaluateHandPlacement(Card.InstanceId,{X,Y}).IsAllowed())bChanged=Cards->TryDeployHandCard(Card.InstanceId,{X,Y});
    if(bChanged)break;
   }
   if(bChanged)continue;
   if(Cards->GetPieces().IsEmpty())
   {
    const auto Hand=Cards->GetHand();
    if(!Hand.IsEmpty())bChanged=Cards->TryDeployHandCard(Hand[0].InstanceId,{3,1});
    if(bChanged)continue;
   }
   TSet<EEGHeroKind> Held;
   for(const auto& Hero:Mode->GetRoster()->GetHeroes())if(Hero.Definition&&Wanted(Hero.Definition->HeroKind))Held.Add(Hero.Definition->HeroKind);
   for(const auto& Card:Cards->GetHand())if(Card.Definition&&Card.Definition->HeroDefinition&&Wanted(Card.Definition->HeroDefinition->HeroKind))Held.Add(Card.Definition->HeroDefinition->HeroKind);
   if(Held.Num()>=3)break;
   if(Cards->GetGold()>=Cards->GetRules().PurchasePrice)
   {
    const auto Offers=Cards->GetShop();
    for(int32 Index=0;Index<Offers.Num();++Index)
    {
     const auto* Card=Offers[Index];
     if(!Card||!Card->HeroDefinition||!Wanted(Card->HeroDefinition->HeroKind)||
        Mode->GetRoster()->FindHeroByDefinition(Card->HeroDefinition).IsValid()||
        Cards->GetHand().ContainsByPredicate([&](const FEGCardInstance& H){return H.Definition==Card;}))continue;
     if(Cards->BuyShopSlotExpected(Index,Cards->GetShopSlotRevision(Index))){bChanged=true;break;}
    }
   }
   if(bChanged)continue;
   if(Cards->GetGold()<Cards->GetRules().PurchasePrice+Cards->GetRefreshCost()||!Cards->RefreshShop())break;
  }
 };
 bool bFound=false;
 for(int32 Seed=0;Seed<120&&!bFound;++Seed)
 {
  Match->AddCorruption(200);Mode->RunDefinition->EncounterSeed=Seed;
  if(!TestTrue(TEXT("Start deterministic Dusk search run"),Mode->StartGame())||
     !TestTrue(TEXT("Dusk search resources ready"),Wait([&](){return Mode->AreStageResourcesReady();}))||
     !TestEqual(TEXT("Dusk search seed is active"),Mode->GetBalanceSeed(),Seed))return false;
  bool bDusk=false;
  for(int32 Stage=1;Stage<=5&&!bFound;++Stage)
  {
   if(!Wait([&](){return Match->GetStageNumber()==Stage&&Mode->AreStageResourcesReady();}))return false;
   Prepare();
   TSet<EEGHeroKind> OnBoard;
   for(const auto& Hero:Mode->GetRoster()->GetHeroes())if(Hero.bDeployed&&Hero.Definition)OnBoard.Add(Hero.Definition->HeroKind);
   const bool bEligible=bDusk&&OnBoard.Contains(EEGHeroKind::Pardofelis)&&OnBoard.Contains(EEGHeroKind::Sakura)&&OnBoard.Contains(EEGHeroKind::Aponia);
   const int64 Before=Cards->GetGold();
   if(Cards->GetPieces().IsEmpty()||!Mode->EnterCombat()||!Mode->ResolveCombat(true))break;
   if(Stage==2||Stage==4)
   {
    if(!Wait([&](){return Match->GetPhase()==EEGMatchPhase::Reward;}))break;
   }
   else if(!Wait([&](){return Match->GetStageNumber()==Stage+1&&Mode->AreStageResourcesReady();}))break;
   if(bEligible)
   {
    bFound=true;
    AddInfo(FString::Printf(TEXT("Dusk earned from normal candidate at seed %d and triggered on stage %d"),Seed,Stage));
    TestEqual(TEXT("Dusk settlement pays one additional gold"),Cards->GetGold(),Before+Mode->RunDefinition->Stages[Stage-1].CompletionGold+1);
    TestEqual(TEXT("Dusk run ledger records settlement income once"),Match->GetStigmataGoldEarned(TEXT("DuskStreet")),int64(1));
    Fixture.TickClock(.5f);
    TestEqual(TEXT("Dusk settlement income does not repeat"),Match->GetStigmataGoldEarned(TEXT("DuskStreet")),int64(1));
    Match->AddCorruption(200);
    if(!TestTrue(TEXT("Restart after Dusk settlement"),Mode->StartGame()))return false;
    TestEqual(TEXT("Restart clears Dusk run ledger"),Match->GetStigmataGoldEarned(TEXT("DuskStreet")),int64(0));
    break;
   }
   if(Stage==2||Stage==4)
   {
    const auto Offer=Mode->GetStigmataOffer();if(Offer.IsEmpty())break;
    const FName Choice=Offer.Contains(TEXT("DuskStreet"))?FName(TEXT("DuskStreet")):Offer[0];
    const int32 Revision=Mode->GetStigmataRevision();
    if(!Mode->ChooseStigmata(Choice,Revision))break;
    if(Choice==TEXT("DuskStreet"))
    {
     bDusk=true;
     TestFalse(TEXT("Dusk claim cannot be replayed"),Mode->ChooseStigmata(Choice,Revision));
     TestEqual(TEXT("Dusk does not pay before an eligible settlement"),Match->GetStigmataGoldEarned(TEXT("DuskStreet")),int64(0));
    }
   }
  }
 }
 return TestTrue(TEXT("Natural economy and a real offer can exercise Dusk settlement"),bFound);
}

#endif
