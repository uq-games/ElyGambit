#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGPieceTestGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Placement/EGDeploymentBoard.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGOpeningQueueTest,"ElyGambit.Battle.OpeningQueue",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGOpeningQueueTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();auto* World=Instance->GetWorld();
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 auto* Battle=Mode?Mode->BattleRules.Get():nullptr;AEGChessUnit* Player=nullptr;TArray<AEGChessUnit*> Enemies;
 if(Battle)for(const auto& Entry:Mode->GetBattleUnits())if(auto* Unit=Entry.Get())
  {if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Player=Unit;else Enemies.Add(Unit);}
 if(TestNotNull(TEXT("player"),Player)&&TestTrue(TEXT("two enemies"),Enemies.Num()==2))
 {
  Enemies.Sort([](const AEGChessUnit& A,const AEGChessUnit& B){return A.GetActorLocation().X<B.GetActorLocation().X;});
  Battle->Start(Mode->GetRoster()->GetHeroes());const FGuid Round=Battle->GetRoundId();TArray<int32> Seen;
  Battle->EnqueueOpening(Enemies[1],[&](){Seen.Add(5);});
  Battle->EnqueueTeamOpening([&](){Seen.Add(3);});
  Battle->EnqueueOpening(Enemies[0],[&](){Seen.Add(4);});
  Battle->EnqueueOpening(Player,[&](){Seen.Add(2);});
  Battle->EnqueueOpening(Player,[&](){Seen.Add(1);},true);
  Battle->BeginOpening();
  TestTrue(TEXT("player effects merge and run first"),Seen==TArray<int32>({1,2}));
  TestFalse(TEXT("opening is not running"),Battle->IsCombatRunning());
  const float Before=Enemies[0]->GetAttributes()->GetHealth();
  TestFalse(TEXT("direct damage is rejected in opening"),FEGCombatExecution::Damage(Player,Enemies[0],100,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid()));
  TestEqual(TEXT("opening preserves health"),Enemies[0]->GetAttributes()->GetHealth(),Before);
  Battle->AdvanceOpeningForTests(.5f);
  TestEqual(TEXT("item waits its full real second"),Seen.Num(),2);
  Battle->AdvanceOpeningForTests(2.2f);
  TestTrue(TEXT("long frame starts only the next visible item"),Seen==TArray<int32>({1,2,3}));
  Battle->AdvanceOpeningForTests(0.f);
  TestEqual(TEXT("long frame does not consume the new item's display time"),Seen.Num(),3);
  auto Next=[&](){Battle->AdvanceOpeningForTests(1.1f);};
  Next();Next();Next();
  TestTrue(TEXT("team precedes enemies left to right"),Seen==TArray<int32>({1,2,3,4,5}));
  TestTrue(TEXT("queue enters running after final second"),Battle->IsCombatRunning());
  TestEqual(TEXT("round identity survives opening"),Battle->GetRoundId(),Round);
  Battle->Stop();Battle->Start(Mode->GetRoster()->GetHeroes());int32 Cancelled=0;
  Battle->EnqueueTeamOpening([&](){++Cancelled;});Battle->EnqueueTeamOpening([&](){++Cancelled;});Battle->BeginOpening();
  Battle->Stop();Battle->AdvanceOpening();TestEqual(TEXT("stop cancels remaining work"),Cancelled,1);
  TestFalse(TEXT("stopped round is not running"),Battle->IsCombatRunning());
  Battle->Start(Mode->GetRoster()->GetHeroes());Battle->BeginOpening();
  TestTrue(TEXT("empty opening starts immediately"),Battle->IsCombatRunning());
  TestTrue(TEXT("restart owns a new round"),Battle->GetRoundId()!=Round);
 }
 World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return true;
}
#endif
