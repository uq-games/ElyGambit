#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Game/EGPieceTestGameMode.h"
#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Placement/EGDeploymentBoard.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPieceTestLifecycle,"ElyGambit.PieceTest.IsolatedResetLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPieceTestLifecycle::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();auto* Roster=Instance->GetSubsystem<UEGHeroRoster>();
 auto* Pardo=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"));
 const FGuid PreviousId=Roster->AddHero(Pardo);Roster->AddLevels(PreviousId,3);
 Roster->SetMergeLevelOverride(7);
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 TestEqual(TEXT("Only Pardo selectable"),Mode->AvailablePieces.Num(),1);
 TestEqual(TEXT("Exact actor count"),Mode->GetBattleUnits().Num(),3);
 TestEqual(TEXT("Only one roster hero"),Roster->GetHeroes().Num(),1);
 TestFalse(TEXT("Invalid selector does not mutate"),Mode->SelectPiece(Mode->AvailablePieces.Num()));
 for(int32 I=0;I<10;++I)
 {
  TestTrue(TEXT("Start real battle"),Mode->EnterCombat());const FGuid Round=Mode->BattleRules->GetRoundId();
  const TWeakObjectPtr<AEGChessUnit> OldUnit=Mode->GetBattleUnits().Last();
  for(int32 Frame=0;Frame<100&&Mode->GetMatch()->GetPhase()==EEGMatchPhase::Combat;++Frame){++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}
  TestTrue(TEXT("Reset during/after combat"),Mode->ResetTest());
  TestFalse(TEXT("Old incarnation destroyed"),OldUnit.IsValid());
  TestFalse(TEXT("Round invalidated"),Mode->BattleRules->GetRoundId().IsValid());
  TestEqual(TEXT("One hero and two enemies after reset"),Mode->GetBattleUnits().Num(),3);
  TestEqual(TEXT("Hero reset to level one"),Roster->GetHeroes()[0].Level,1);
  TestTrue(TEXT("Reselect Pardo safely"),Mode->SelectPiece(0));
  TestTrue(TEXT("Fresh round can start"),Mode->EnterCombat());
  TestNotEqual(TEXT("Round ID never reused"),Mode->BattleRules->GetRoundId(),Round);
  TestTrue(TEXT("Reset before next iteration"),Mode->ResetTest());
 }
 for(int32 Index=0;Index<Mode->AvailablePieces.Num();++Index)
 {
  TestTrue(TEXT("Switch maintained piece"),Mode->SelectPiece(Index));
  TestEqual(TEXT("Selected definition is authoritative"),Roster->GetHeroes()[0].Definition.Get(),Mode->AvailablePieces[Index].Get());
  TestTrue(TEXT("Selected piece starts real battle"),Mode->EnterCombat());
  TestTrue(TEXT("Reset clears summons and effects"),Mode->ResetTest());
  TestEqual(TEXT("No leaked summons after switching"),Mode->GetBattleUnits().Num(),3);
 }
 World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);
 FEGHeroInstance Restored;TestTrue(TEXT("Previous roster restored on exit"),Roster->GetHero(PreviousId,Restored));
 TestEqual(TEXT("Previous levels preserved"),Restored.Level,4);
 TestEqual(TEXT("Merge rule restored"),Roster->GetMergeLevelOverride(),7);
 Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);return true;
}
#endif
