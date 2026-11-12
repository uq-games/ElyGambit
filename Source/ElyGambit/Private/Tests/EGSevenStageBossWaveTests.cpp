#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "HAL/PlatformProcess.h"
#include "Tests/EGProductionTestAssets.h"
#include "UObject/StrongObjectPtr.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Actor/EGCorruptionBarrier.h"
#include "Presentation/EGCorruptionBarrierVisual.h"
#include "Presentation/EGCorruptionPhantomVisual.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageBossWaveRetryTest,
 "ElyGambit.SevenStage.BossNoReinforcementAndCleanup",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSevenStageBossWaveRetryTest::RunTest(const FString&)
{
 // Settlement/reward outcomes in stages 1-6 are controlled to reach the real
 // stage-seven preparation, spawning and combat chain. This is not balance data.
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);if(!Seed||!GEngine)return false;
 for(int32 Case=0;Case<2;++Case)
 {
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
  Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
  if(!TestNotNull(TEXT("Fixture world"),World))return false;
  ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
  auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Fixture mode"),Mode))return false;
  Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);if(!TestNotNull(TEXT("Production seven-stage run"),Mode->RunDefinition.Get()))return false;
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  auto* Cards=World->SpawnActor<AEGCardManager>();auto* Board=World->SpawnActor<AEGBoard>();
  if(!TestNotNull(TEXT("Scene"),Scene)||!TestNotNull(TEXT("Grid"),Grid)||!TestNotNull(TEXT("Cards"),Cards)||!TestNotNull(TEXT("Board"),Board))return false;
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  auto WaitReady=[&](){for(int32 I=0;I<300&&!Mode->AreStageResourcesReady();++I){++GFrameCounter;World->Tick(LEVELTICK_All,.02f);}return Mode->AreStageResourcesReady();};
  if(!TestTrue(TEXT("Starting stage ready"),WaitReady()))return false;
  const auto Hand=Cards->GetHand();
  if(!TestTrue(TEXT("Opening hand has a card"),!Hand.IsEmpty())||!TestTrue(TEXT("Deploy player"),Cards->TryDeployHandCard(Hand[0].InstanceId,{3,1})))return false;
  auto* Match=Mode->GetMatch();
  for(int32 Stage=1;Stage<=6;++Stage)
  {
   if(!TestEqual(TEXT("Controlled stage index"),Match->GetStageNumber(),Stage)||
      !TestTrue(TEXT("Controlled prior stage enters"),Match->EnterCombat())||
      !TestTrue(TEXT("Controlled prior victory"),Mode->ResolveCombat(true))||
      !TestTrue(TEXT("Controlled settlement completion"),Match->FinishSettlement())||
      !TestTrue(TEXT("Production stage transition"),Mode->NextStage())||
      !TestTrue(TEXT("Next stage ready"),WaitReady()))return false;
  }
  if(!TestEqual(TEXT("Real final stage"),Match->GetStageNumber(),7)||!TestTrue(TEXT("Real boss combat starts"),Mode->EnterCombat()))return false;
  Mode->BattleRules->CompleteOpeningForTests();
  AEGChessUnit* Boss=nullptr;AEGChessUnit* Player=nullptr;
  for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit))
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->SetComponentTickEnabled(false);
   Unit->GetCharacterMovement()->DisableMovement();
   if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Player=Unit;
   if(auto* Rules=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();Rules&&Rules->IsBoss())Boss=Unit;
  }
  if(!TestNotNull(TEXT("Actual final-stage boss attached"),Boss)||!TestNotNull(TEXT("Player"),Player))return false;
  auto* Rules=Boss->FindComponentByClass<UEGCorruptionCombatComponent>();
  const int32 InitialUnits=Mode->GetCombatUnits().Num();const int32 InitialSummons=Mode->BattleRules->SummonCount();
  TestTrue(TEXT("Lethal first-pool damage accepted"),FEGCombatExecution::Damage(Player,Boss,1.e8f));
  TestEqual(TEXT("First pool leads to wall"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier));
  TestEqual(TEXT("Wall adds no reinforcement units"),Mode->GetCombatUnits().Num(),InitialUnits);
  TestEqual(TEXT("Wall adds no reinforcement summons"),Mode->BattleRules->SummonCount(),InitialSummons);
  if(Case==0)
  {
   Rules->CompleteBarrierIntroForTests();
   TestTrue(TEXT("Wall break hit accepted"),FEGCombatExecution::Damage(Player,Boss,1.e8f));
   TestEqual(TEXT("Wall break enters second phase"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Second));
   TestEqual(TEXT("Second phase adds no reinforcement units"),Mode->GetCombatUnits().Num(),InitialUnits);
   TestEqual(TEXT("Second phase adds no reinforcement summons"),Mode->BattleRules->SummonCount(),InitialSummons);
  }
  TestTrue(TEXT("Ending actual combat succeeds"),Mode->ResolveCombat(false));
  TestFalse(TEXT("Finished battle has no active wall"),Rules->IsWallActive());
  TestFalse(TEXT("Finished battle releases all presentation holds"),Mode->BattleRules->HasPresentationHold());
  for(int32 I=0;I<10;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.1f);}
  TestEqual(TEXT("No delayed reinforcement after cleanup"),Mode->BattleRules->SummonCount(),0);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageEncounterRecoveryTest,
 "ElyGambit.SevenStage.EncounterRecoveryTransaction",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSevenStageEncounterRecoveryTest::RunTest(const FString&)
{
 // Controlled earlier results lead to production stage seven. No editor, mouse
 // input, natural strategy or balance claim is made by this fixture.
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);if(!Seed||!GEngine)return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("Recovery fixture world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Recovery fixture mode"),Mode))return false;
 Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);if(!TestNotNull(TEXT("Production seven-stage run"),Mode->RunDefinition.Get()))return false;
 auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 auto* Cards=World->SpawnActor<AEGCardManager>();auto* Board=World->SpawnActor<AEGBoard>();
 if(!TestNotNull(TEXT("Scene"),Scene)||!TestNotNull(TEXT("Grid"),Grid)||!TestNotNull(TEXT("Cards"),Cards)||!TestNotNull(TEXT("Board"),Board))return false;
 Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto Tick=[&](){++GFrameCounter;World->Tick(LEVELTICK_All,.02f);};
 auto WaitReady=[&](){for(int32 I=0;I<300&&!Mode->AreStageResourcesReady();++I)Tick();return Mode->AreStageResourcesReady();};
 if(!TestTrue(TEXT("Starting stage ready"),WaitReady()))return false;
 const auto StartingHand=Cards->GetHand();
 if(!TestTrue(TEXT("Opening card exists"),!StartingHand.IsEmpty())||
    !TestTrue(TEXT("Opening player deployed"),Cards->TryDeployHandCard(StartingHand[0].InstanceId,{3,1})))return false;
 auto* Match=Mode->GetMatch();
 for(int32 Stage=1;Stage<=6;++Stage)
  if(!TestEqual(TEXT("Controlled prior stage"),Match->GetStageNumber(),Stage)||
     !TestTrue(TEXT("Enter prior stage"),Match->EnterCombat())||!TestTrue(TEXT("Resolve controlled prior victory"),Mode->ResolveCombat(true))||
     !TestTrue(TEXT("Complete prior settlement"),Match->FinishSettlement())||!TestTrue(TEXT("Advance stage via game mode"),Mode->NextStage())||
     !TestTrue(TEXT("Next stage is ready"),WaitReady()))return false;
 if(!TestEqual(TEXT("Reached boss preparation"),Match->GetStageNumber(),7))return false;
 UEGCardDefinition* OverflowDefinition=nullptr;
 for(const auto& Card:Mode->RunDefinition->CardPool)if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==EEGHeroKind::Sakura){OverflowDefinition=Card;break;}
 if(!TestNotNull(TEXT("Ordinary overflow fixture card"),OverflowDefinition)||!TestTrue(TEXT("Create controlled overflow"),Cards->GrantCard(OverflowDefinition,12)))return false;
 const TArray<FGuid> HandBefore=Cards->GetOrderedHandIds();
 const int64 GoldBefore=Cards->GetGold();const int32 CorruptionBefore=Match->GetCorruption();
 const int32 PiecesBefore=Cards->GetPieces().Num();
 if(!TestTrue(TEXT("Fixture includes actual hand overflow"),!Cards->GetOverflowHandIds().IsEmpty())||
    !TestTrue(TEXT("Start real boss combat"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();const FGuid OldRound=Mode->BattleRules->GetRoundId();
 if(!TestTrue(TEXT("Started battle has valid round"),OldRound.IsValid()))return false;
 AEGChessUnit* Boss=nullptr;AEGChessUnit* Player=nullptr;
 for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit))
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->SetComponentTickEnabled(false);
  Unit->GetCharacterMovement()->DisableMovement();
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Player=Unit;
  if(auto* Rules=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();Rules&&Rules->IsBoss())Boss=Unit;
 }
 if(!TestNotNull(TEXT("Real boss"),Boss)||!TestNotNull(TEXT("Deployed player"),Player))return false;
 Mode->RequestEncounterRecovery(FGuid::NewGuid());Tick();
 TestEqual(TEXT("Unknown round cannot recover current battle"),Match->GetPhase(),EEGMatchPhase::Combat);
 TestEqual(TEXT("Unknown round leaves battle identity intact"),Mode->BattleRules->GetRoundId(),OldRound);
 auto* Rules=Boss->FindComponentByClass<UEGCorruptionCombatComponent>();
 if(!TestTrue(TEXT("Trigger real wall"),FEGCombatExecution::Damage(Player,Boss,1.e8f))||
    !TestEqual(TEXT("Wall stage active"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier)))return false;
 TestNull(TEXT("Wall phase cannot create an unrelated mimic visual"),
  AEGCorruptionPhantomVisual::Spawn(Boss,Player->GetDefinition(),Boss->GetActorLocation(),Boss->GetActorRotation(),3.f,FGuid::NewGuid()));
 Mode->RequestEncounterRecovery(OldRound);Mode->RequestEncounterRecovery(OldRound);
 for(int32 I=0;I<20&&Match->GetPhase()==EEGMatchPhase::Combat;++I)Tick();
 if(!TestEqual(TEXT("Current round recovers preparation"),Match->GetPhase(),EEGMatchPhase::Preparation))return false;
 TestEqual(TEXT("Recovery keeps stage"),Match->GetStageNumber(),7);
 TestEqual(TEXT("Recovery does not change corruption"),Match->GetCorruption(),CorruptionBefore);
 TestEqual(TEXT("Recovery does not credit, debit or retain overflow proceeds"),Cards->GetGold(),GoldBefore);
 TestTrue(TEXT("Recovery restores exact ordered hand including overflow"),Cards->GetOrderedHandIds()==HandBefore);
 TestEqual(TEXT("Recovery restores deployed population"),Cards->GetPieces().Num(),PiecesBefore);
 TestFalse(TEXT("Recovery stops old battle"),Mode->BattleRules->IsBattleActive());
 TestFalse(TEXT("Recovery clears all old holds"),Mode->BattleRules->HasPresentationHold());
 // Tick once more for ephemeral visuals to observe the invalidated round.
 Tick();
 for(TActorIterator<AEGCorruptionBarrier> It(World);It;++It)TestTrue(TEXT("No live collision wall"),It->IsActorBeingDestroyed());
 for(TActorIterator<AEGCorruptionBarrierVisual> It(World);It;++It)TestTrue(TEXT("No live wall presentation"),It->IsActorBeingDestroyed());
 for(TActorIterator<AEGCorruptionPhantomVisual> It(World);It;++It)TestTrue(TEXT("No live phantom"),It->IsActorBeingDestroyed());
 if(!TestTrue(TEXT("Recovered stage can start again"),WaitReady()&&Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();const FGuid NewRound=Mode->BattleRules->GetRoundId();
 TestTrue(TEXT("Retry uses a new round identity"),NewRound.IsValid()&&NewRound!=OldRound);
 Mode->RequestEncounterRecovery(OldRound);Tick();
 TestEqual(TEXT("Delayed old-round request cannot cancel retry"),Match->GetPhase(),EEGMatchPhase::Combat);
 TestEqual(TEXT("Retry round identity remains current"),Mode->BattleRules->GetRoundId(),NewRound);
 return true;
}
#endif
