#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Components/EGRunResourceComponent.h"
#include "Presentation/EGOpeningPresentation.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGBoardScene.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

namespace
{
struct FOpeningFlowWorld
{
 TStrongObjectPtr<UGameInstance> Instance;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGCardManager* Cards=nullptr;
 bool Initialize(FAutomationTestBase& Test,bool bOpening)
 {
  auto* Seed=EGProductionTestAssets::ResolveRun(Test);
  if(!Seed||!GEngine)return false;
  Instance.Reset(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();World=Instance->GetWorld();
  if(!Test.TestNotNull(TEXT("Opening fixture world"),World))return false;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;if(bOpening)URL.AddOption(TEXT("EGOpening=1"));World->SetGameMode(URL);
  Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Test.TestNotNull(TEXT("Opening fixture game mode"),Mode))return false;
  Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Cards=World->SpawnActor<AEGCardManager>();auto* Board=World->SpawnActor<AEGBoard>();
  if(!Scene||!Grid||!Cards||!Board||!Mode->RunDefinition)return false;
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  return Test.TestNotNull(TEXT("Opening fixture match"),Mode->GetMatch());
 }
 bool InitializeMissingScene(FAutomationTestBase& Test,bool bOpening)
 {
  if(!GEngine)return false;
  Instance.Reset(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();World=Instance->GetWorld();
  if(!Test.TestNotNull(TEXT("Invalid opening fixture world"),World))return false;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;if(bOpening)URL.AddOption(TEXT("EGOpening=1"));World->SetGameMode(URL);
  Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Test.TestNotNull(TEXT("Invalid opening fixture game mode"),Mode))return false;
  // No scene or run: exercise the existing configuration guard without
  // altering any production map or asset.
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  return Test.TestNotNull(TEXT("Invalid opening fixture match"),Mode->GetMatch());
 }
 void Tick(float Seconds)
 {
  while(Seconds>0.f){const float Step=FMath::Min(.05f,Seconds);++GFrameCounter;World->Tick(LEVELTICK_All,Step);Seconds-=Step;}
 }
 bool TickUntilSettlementEnds()
 {
  if(!Mode||!Mode->GetMatch()||Mode->GetMatch()->GetPhase()!=EEGMatchPhase::Settlement)return false;
  // Stop on the exact phase change: a subsequent world tick automatically
  // advances Resolution for round-shop runs.
  for(int32 Frame=0;Frame<200&&Mode->GetMatch()->GetPhase()==EEGMatchPhase::Settlement;++Frame)Tick(.05f);
  return Mode->GetMatch()->GetPhase()!=EEGMatchPhase::Settlement;
 }
 ~FOpeningFlowWorld()
 {
  if(!World||!GEngine)return;
  World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
  Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGOpeningFlowTransactionTest,"ElyGambit.Opening.TransactionGate",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGOpeningFlowTransactionTest::RunTest(const FString&)
{
 AddExpectedError(TEXT("Basic demo configuration:"),EAutomationExpectedErrorFlags::Contains,2);
 {
  FOpeningFlowWorld F;
  if(!F.InitializeMissingScene(*this,true))return false;
  TestTrue(TEXT("Formal entry publishes a configuration failure"),F.Mode->IsOpeningStartupFailure());
  TestFalse(TEXT("Formal entry exposes a recovery message"),F.Mode->GetOpeningPreloadError().IsEmpty());
  TestTrue(TEXT("Formal entry blocks gameplay on configuration failure"),F.Mode->IsPresentationInputBlocked());
  TestFalse(TEXT("Invalid formal entry never starts the run"),F.Mode->GetMatch()->HasStarted());
  TestFalse(TEXT("Invalid formal entry cannot claim resources are ready"),F.Mode->AreResourcesReady());
  TestFalse(TEXT("Invalid formal entry rejects start requests"),F.Mode->RequestOpeningStart());
  if(!F.World->GetFirstPlayerController())
   TestTrue(TEXT("Error overlay waits for a local player"),F.Mode->bOpeningErrorWidgetPending);
 }
 {
  FOpeningFlowWorld F;
  if(!F.InitializeMissingScene(*this,false))return false;
  TestFalse(TEXT("Direct board entry keeps its existing failure behavior"),F.Mode->IsOpeningStartupFailure());
  TestFalse(TEXT("Direct board entry does not install the opening error overlay"),F.Mode->bOpeningErrorWidgetPending);
  TestFalse(TEXT("Invalid direct entry never starts the run"),F.Mode->GetMatch()->HasStarted());
 }
 {
  FOpeningFlowWorld F;if(!F.Initialize(*this,true))return false;
  TestTrue(TEXT("Formal opening waits before gameplay"),F.Mode->IsPresentationInputBlocked());
  TestFalse(TEXT("Run has not started before masters arrive"),F.Mode->GetMatch()->HasStarted());
  TestTrue(TEXT("No opening hand before masters arrive"),F.Cards->GetHand().IsEmpty());
  TestTrue(TEXT("No stage enemy before masters arrive"),F.Mode->GetCombatUnits().IsEmpty());
  F.Mode->CompleteOpeningPresentation(FGuid::NewGuid());
  TestFalse(TEXT("A foreign opening token cannot start the run"),F.Mode->GetMatch()->HasStarted());
  TestFalse(TEXT("Direct gameplay start cannot bypass opening"),F.Mode->StartGame());
  if(!TestNotNull(TEXT("Formal opening presentation"),F.Mode->OpeningPresentation.Get()))return false;
  const FGuid Token=F.Mode->GetOpeningToken();
  F.Mode->OpeningPresentation->bDepartureCompleted=true;
  F.Mode->CompleteOpeningPresentation(Token);
  TestFalse(TEXT("Completed presentation cannot start without the player's request"),F.Mode->GetMatch()->HasStarted());
  F.Mode->OpeningPresentation->bDepartureCompleted=false;
  // Simulate a constructed title scene in this headless fixture; the real
  // request still passes through the production GameMode and component.
  F.Mode->OpeningPresentation->State=EEGOpeningPresentationState::AwaitingStart;
  F.Tick(.25f);
  TestFalse(TEXT("Run resources do not preload before the player's request"),F.Mode->bOpeningPreloadStarted);
  TestFalse(TEXT("Empty startup resource state is not readiness"),F.Mode->IsOpeningPreloadReady());
  TestTrue(TEXT("Before click no run resources are held"),F.Mode->Resources->GetHeldResourcePaths().IsEmpty());
  if(!TestTrue(TEXT("Start click is accepted before resource readiness"),F.Mode->RequestOpeningStart()))return false;
  TestFalse(TEXT("Repeated click is rejected"),F.Mode->RequestOpeningStart());
  TestTrue(TEXT("Accepted click records a load request"),F.Mode->bOpeningLoadRequested);
  TestFalse(TEXT("No card is dealt while waiting for resources"),F.Mode->GetMatch()->HasStarted());
  F.Tick(.25f);
  TestTrue(TEXT("Accepted click starts actual resource requests"),F.Mode->bOpeningPreloadStarted);
  TestFalse(TEXT("The run remains blocked while resources load"),F.Mode->GetMatch()->HasStarted());
  // Exercise the commit contract independently from disk and rendering.
  F.Mode->Resources->SetRunHeroes({},F.Mode->RunDefinition);
  F.Mode->CompleteOpeningPresentation(Token);
  TestFalse(TEXT("A correct token cannot bypass the unfinished entrance"),F.Mode->GetMatch()->HasStarted());
  F.Mode->OpeningPresentation->bDepartureCompleted=true; // Controlled callback fixture, no rendering asserted.
  F.Mode->CompleteOpeningPresentation(Token);
  if(!TestTrue(TEXT("Matching completed opening starts the run"),F.Mode->GetMatch()->HasStarted()))return false;
  const int32 OpeningCards=F.Cards->GetHand().Num();const int32 OpeningEnemies=F.Mode->GetCombatUnits().Num();
  TestEqual(TEXT("Opening grants the configured two-card hand"),OpeningCards,2);
  TestTrue(TEXT("Opening creates stage enemies"),OpeningEnemies>0);
  TestTrue(TEXT("Enemy entrance keeps input blocked"),F.Mode->IsPresentationInputBlocked());
  F.Mode->CompleteOpeningPresentation(Token);
  TestEqual(TEXT("Repeated opening callback cannot deal twice"),F.Cards->GetHand().Num(),OpeningCards);
  TestEqual(TEXT("Repeated opening callback cannot spawn enemies twice"),F.Mode->GetCombatUnits().Num(),OpeningEnemies);
 }
 {
  FOpeningFlowWorld F;if(!F.Initialize(*this,false))return false;
  auto* Mode=F.Mode;auto* Match=Mode->GetMatch();auto* Cards=F.Cards;
  if(!TestTrue(TEXT("Direct board entry retains immediate startup"),Match->HasStarted()&&Mode->AreStageResourcesReady()))return false;
  Mode->OpeningPresentation=NewObject<UEGOpeningPresentation>(Mode);
  const FGuid First=FGuid::NewGuid();
  Mode->bStageEntrancePending=true;Mode->EntranceToken=First;Mode->bResourcesReady=false;
  Mode->OpeningPresentation->StageToken=First;Mode->OpeningPresentation->bStageEntranceCompleted=false;
  Cards->SetPreparationEnabled(false);
  const float Before=Mode->GetPhaseClock().RemainingRealSeconds;
  const int64 Gold=Cards->GetGold();const int32 HandCount=Cards->GetHand().Num();
  TestFalse(TEXT("Entrance blocks purchases at the transaction boundary"),Cards->BuyShopSlot(0));
  TestFalse(TEXT("Entrance blocks placement"),Cards->CanEditPlacement());
  TestFalse(TEXT("Entrance blocks combat"),Mode->EnterCombat());
  F.Tick(2.f);
  TestTrue(TEXT("Entrance does not consume preparation time"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,Before,.01f));
  TestEqual(TEXT("Blocked purchase leaves gold unchanged"),Cards->GetGold(),Gold);
  TestEqual(TEXT("Blocked purchase leaves hand unchanged"),Cards->GetHand().Num(),HandCount);
  Mode->CompleteStageEntrance(FGuid::NewGuid());
  TestTrue(TEXT("Foreign entrance token cannot release gate"),Mode->IsPresentationInputBlocked());
  Mode->CompleteStageEntrance(First);
  TestTrue(TEXT("Matching token alone cannot skip unfinished arrival"),Mode->IsPresentationInputBlocked());
  Mode->OpeningPresentation->bStageEntranceCompleted=true;
  Mode->CompleteStageEntrance(First);
  TestFalse(TEXT("Matching entrance token releases gate"),Mode->IsPresentationInputBlocked());
  TestTrue(TEXT("Trades resume after arrival"),Cards->CanTrade());
  const FGuid Second=FGuid::NewGuid();
  Mode->bStageEntrancePending=true;Mode->EntranceToken=Second;Mode->bResourcesReady=false;
  Mode->OpeningPresentation->StageToken=Second;Mode->OpeningPresentation->bStageEntranceCompleted=true;
  Cards->SetPreparationEnabled(false);
  Mode->CompleteStageEntrance(First);
  TestTrue(TEXT("Previously consumed token cannot release a later entrance"),Mode->IsPresentationInputBlocked());
  Mode->CompleteStageEntrance(Second);
  TestFalse(TEXT("Later entrance accepts only its own token"),Mode->IsPresentationInputBlocked());
  if(!TestTrue(TEXT("Fixture enters combat"),Match->EnterCombat())||
     !TestTrue(TEXT("Fixture resolves combat"),Match->ResolveCombat(true))||
     !TestTrue(TEXT("Fixture finishes settlement"),Match->FinishSettlement())||
     !TestTrue(TEXT("Fixture opens reward"),Match->EnterReward()))return false;
  Mode->OpeningPresentation=NewObject<UEGOpeningPresentation>(Mode);
  Mode->bStageTransitionPending=true;Mode->TransitionFromStage=Match->GetStageNumber();
  Mode->TransitionToken=FGuid::NewGuid();
  const int32 RewardStage=Match->GetStageNumber();
  Mode->CompleteStageTransition(Mode->TransitionToken);
  TestEqual(TEXT("Transition cannot bypass an unresolved reward"),Match->GetPhase(),EEGMatchPhase::Reward);
  TestEqual(TEXT("Reward guard preserves stage"),Match->GetStageNumber(),RewardStage);
  TestTrue(TEXT("Rejected reward transition remains gated"),Mode->bStageTransitionPending);
 }
 {
  FOpeningFlowWorld F;if(!F.Initialize(*this,false))return false;
  auto* Mode=F.Mode;auto* Match=Mode->GetMatch();auto* Cards=F.Cards;
  Mode->OpeningPresentation=NewObject<UEGOpeningPresentation>(Mode);
  Mode->bPresentationFlowActive=true;
  const int64 FirstGold=Cards->GetGold();
  const int32 FirstReward=Mode->RunDefinition->Stages[0].CompletionGold;
  if(!TestTrue(TEXT("First stage enters combat"),Match->EnterCombat())||
     !TestTrue(TEXT("First stage resolves"),Match->ResolveCombat(true)))return false;
  TestFalse(TEXT("Cannot advance during settlement"),Mode->NextStage());
  if(!TestTrue(TEXT("First settlement completes"),F.TickUntilSettlementEnds()))return false;
  if(!TestEqual(TEXT("First settlement reaches resolution"),Match->GetPhase(),EEGMatchPhase::Resolution))return false;
  TestEqual(TEXT("Settlement pays first stage exactly once"),Cards->GetGold(),FirstGold+FirstReward);
  if(!TestTrue(TEXT("Next stage requests transition"),Mode->NextStage()))return false;
  const FGuid FirstTransition=Mode->TransitionToken;
  TestEqual(TEXT("Transition has not yet advanced stage"),Match->GetStageNumber(),1);
  TestFalse(TEXT("Duplicate transition request rejected"),Mode->NextStage());
  Mode->CompleteStageTransition(FGuid::NewGuid());
  TestEqual(TEXT("Foreign transition token cannot advance"),Match->GetStageNumber(),1);
  Mode->CompleteStageTransition(FirstTransition);
  TestEqual(TEXT("Matching token cannot skip unfinished transition"),Match->GetStageNumber(),1);
  if(!TestTrue(TEXT("Second stage has an enemy fixture"),!Mode->RunDefinition->Stages[1].Enemies.IsEmpty()))return false;
  auto* SavedEnemy=Mode->RunDefinition->Stages[1].Enemies[0].Definition.Get();
  Mode->RunDefinition->Stages[1].Enemies[0].Definition=nullptr;
  Mode->OpeningPresentation->bStageTransitionCompleted=true;
  Mode->CompleteStageTransition(FirstTransition);
  TestEqual(TEXT("Matching transition advances once"),Match->GetStageNumber(),2);
  TestTrue(TEXT("Failed enemy spawn exposes an entrance retry"),Mode->IsStageEntranceRetryNeeded());
  const int64 BeforeRetryGold=Cards->GetGold();const int32 BeforeRetryHand=Cards->GetHand().Num();
  TestFalse(TEXT("Invalid enemy fixture remains retryable"),Mode->RetryStageEntrance());
  TestEqual(TEXT("Failed retry does not advance stage"),Match->GetStageNumber(),2);
  TestEqual(TEXT("Failed retry does not duplicate rewards"),Cards->GetGold(),BeforeRetryGold);
  TestEqual(TEXT("Failed retry does not deal again"),Cards->GetHand().Num(),BeforeRetryHand);
  Mode->RunDefinition->Stages[1].Enemies[0].Definition=SavedEnemy;
  if(!TestTrue(TEXT("Restored enemy can be retried"),Mode->RetryStageEntrance()))return false;
  TestFalse(TEXT("Successful retry clears entrance error"),Mode->IsStageEntranceRetryNeeded());
  TestTrue(TEXT("Successful retry created enemies"),Mode->GetCombatUnits().Num()>0);
  TestEqual(TEXT("Successful retry does not duplicate rewards"),Cards->GetGold(),BeforeRetryGold);
  TestEqual(TEXT("Successful retry does not deal again"),Cards->GetHand().Num(),BeforeRetryHand);
  TestTrue(TEXT("Arrival holds preparation"),Mode->IsPresentationInputBlocked());
  TestFalse(TEXT("Arrival blocks trading"),Cards->CanTrade());
  Mode->CompleteStageTransition(FirstTransition);
  TestEqual(TEXT("Duplicate transition callback cannot advance twice"),Match->GetStageNumber(),2);
  const FGuid FirstEntrance=Mode->EntranceToken;
  Mode->CompleteStageEntrance(FirstEntrance);
  TestTrue(TEXT("Matching arrival token cannot skip animation"),Mode->IsPresentationInputBlocked());
  Mode->OpeningPresentation->bStageEntranceCompleted=true;
  Mode->CompleteStageEntrance(FirstEntrance);
  TestTrue(TEXT("Arrival restores trade once"),Cards->CanTrade());
  TestFalse(TEXT("Arrival releases the gate"),Mode->IsPresentationInputBlocked());
  const int64 SecondGold=Cards->GetGold();
  const int32 SecondReward=Mode->RunDefinition->Stages[1].CompletionGold;
  if(!TestTrue(TEXT("Second stage enters combat"),Match->EnterCombat())||
     !TestTrue(TEXT("Second stage resolves"),Match->ResolveCombat(true)))return false;
  if(!TestTrue(TEXT("Second settlement completes"),F.TickUntilSettlementEnds()))return false;
  if(!TestEqual(TEXT("Second settlement enters reward"),Match->GetPhase(),EEGMatchPhase::Reward))return false;
  TestEqual(TEXT("Second completion paid before choice"),Cards->GetGold(),SecondGold+SecondReward);
  TestFalse(TEXT("Transition unavailable before reward choice"),Mode->NextStage());
  bool bChosen=false;const int32 Revision=Mode->GetStigmataRevision();
  for(const FName Id:Mode->GetStigmataOffer())if(Mode->ChooseStigmata(Id,Revision)){bChosen=true;break;}
  if(!TestTrue(TEXT("A real offered reward can be chosen"),bChosen))return false;
  if(!TestTrue(TEXT("Transition available after choice"),Mode->NextStage()))return false;
  const FGuid SecondTransition=Mode->TransitionToken;
  Mode->CompleteStageTransition(FirstTransition);
  TestEqual(TEXT("Old stage token cannot advance a later transition"),Match->GetStageNumber(),2);
  Mode->OpeningPresentation->bStageTransitionCompleted=true;
  Mode->CompleteStageTransition(SecondTransition);
  TestEqual(TEXT("Current stage token advances to third stage"),Match->GetStageNumber(),3);
  const FGuid SecondEntrance=Mode->EntranceToken;
  Mode->CompleteStageEntrance(FirstEntrance);
  TestTrue(TEXT("Old arrival cannot unlock current stage"),Mode->IsPresentationInputBlocked());
  Mode->OpeningPresentation->bStageEntranceCompleted=true;
  Mode->CompleteStageEntrance(SecondEntrance);
  TestFalse(TEXT("Current arrival unlocks current stage"),Mode->IsPresentationInputBlocked());
 }
 return true;
}
#endif
