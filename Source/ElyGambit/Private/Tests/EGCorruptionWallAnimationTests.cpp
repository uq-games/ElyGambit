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
#include "Actor/EGBoard.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCorruptionBarrierVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "AbilitySystem/EGCombatExecution.h"

// Controlled authoritative battle lifecycle. It intentionally uses the real
// formal Boss and authored private wall assembly, not a fabricated montage.
// It is not a screenshot, natural-battle, sound or visual acceptance test.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionWallAnimationTest,
 "ElyGambit.Corruption.WallAnimationLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionWallAnimationTest::RunTest(const FString&)
{
 auto* FormalBoss=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
 auto* WallAssembly=LoadObject<UEGSourcePreviewDefinition>(nullptr,TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/SourceWall20261108/DA_Corruption_SummonWall_Assembly.DA_Corruption_SummonWall_Assembly"));
 if(!TestNotNull(TEXT("Formal Boss definition"),FormalBoss)||!TestNotNull(TEXT("Required private wall assembly must exist"),WallAssembly)||
    !TestTrue(TEXT("Private wall assembly contains the expected motion"),!WallAssembly->Motions.IsEmpty()))return false;
 TestTrue(TEXT("Authored motion duration is 2.333 seconds"),FMath::IsNearlyEqual(WallAssembly->Motions[0].Duration,2.333333f,.01f));
 for(int32 Case=0;Case<4;++Case)
 {
  TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
  UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("Controlled standalone world"),World))return false;
  ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
  World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
  Grid->CellWidth=499.96054f;Grid->CellDepth=500.67874f;
  World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!TestNotNull(TEXT("Test game mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
  Mode->BattleRules->CompleteOpeningForTests();
  auto* PC=World->SpawnActor<APlayerController>();if(!TestNotNull(TEXT("Pause controller"),PC))return false;
  if(!PC->PlayerState)PC->SetPlayerState(World->SpawnActor<APlayerState>());
  if(!TestNotNull(TEXT("Pause player state"),PC->PlayerState.Get()))return false;
  AEGChessUnit* Hero=nullptr;
  for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);
   if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;
  }
  if(!TestNotNull(TEXT("Player unit"),Hero)||!TestNotNull(TEXT("Board"),Mode->GetBoard()))return false;
  auto* Boss=Mode->SpawnBattleSummon(FormalBoss,6,Mode->GetBoard()->CellToWorld({3,5}),EEGFaction::Enemy);
  if(!TestNotNull(TEXT("Formal Boss incarnation"),Boss))return false;
  Boss->GetCombat()->SetComponentTickEnabled(false);Boss->GetHeroSkill()->Configure(Mode->BattleRules,6,false);
  Mode->BattleRules->EnterUnit(Boss);
  auto* Body=Boss->GetVisiblePresentationMesh();
  if(!TestNotNull(TEXT("Boss body"),Body))return false;
  UClass* OriginalClass=Body->GetAnimClass();const auto OriginalMode=Body->GetAnimationMode();
  auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Mode->BattleRules,1000.f,100.f,50);
  if(!TestNotNull(TEXT("Boss authority"),Rules))return false;
  FDelegateHandle ExistingLease;
  if(Case==3)
  {
   const auto OtherOwner=FEGPresentationReleaseLease::CreateLambda([](AEGChessUnit*){});
   if(!TestTrue(TEXT("Another presentation owns the Boss body"),Boss->TryAcquirePresentationReleaseLease(OtherOwner,ExistingLease)))return false;
  }
  if(Case==1)
  {
   // An unconfigured soft reference checks the production fallback without
   // modifying assets; normal cases still require the real authored asset.
   Rules->SetBarrierIntroAssemblyForTests(TSoftObjectPtr<UEGSourcePreviewDefinition>());
  }
  if(!TestTrue(TEXT("Actual damage enters the wall phase"),FEGCombatExecution::Damage(Hero,Boss,100000.f)))return false;
  if(!TestEqual(TEXT("Wall phase"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier)))return false;
  if(!TestTrue(TEXT("Wall intro holds combat"),Mode->BattleRules->HasPresentationHold()&&World->IsPaused()))return false;
  TestTrue(TEXT("Wall starts with full eight-second challenge"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
  const bool bFallback=Case==1||Case==3;
  const float Expected=bFallback?.7f:2.333333f;
  TestTrue(TEXT("Intro duration matches selected valid/fallback asset"),FMath::IsNearlyEqual(Rules->GetBarrierIntroDurationSeconds(),Expected,.02f));
  TestEqual(TEXT("Only available compatible asset owns the intro presentation"),Rules->IsBarrierIntroPresentationActive(),!bFallback);
  auto CountWallRigs=[&]()
  {
   int32 Count=0;for(TActorIterator<AEGCombatVisual> It(World);It;++It)
    if(It->GetOwner()==Boss&&It->Definition==WallAssembly&&!It->IsActorBeingDestroyed())++Count;
   return Count;
  };
  AEGCorruptionBarrierVisual* WallVisual=nullptr;
  for(TActorIterator<AEGCorruptionBarrierVisual> It(World);It;++It)if(It->GetOwner()==Boss&&!It->IsActorBeingDestroyed()){WallVisual=*It;break;}
  if(!TestNotNull(TEXT("Wall presentation actor"),WallVisual))return false;
  auto* Surface=FindObject<UStaticMeshComponent>(WallVisual,TEXT("WallSurface"));
  if(!TestNotNull(TEXT("Wall surface"),Surface))return false;
  if(!bFallback)
  {
   TestEqual(TEXT("Exactly one source-wall rig owns the Boss"),CountWallRigs(),1);
   bool bBowAttached=false;
   for(TActorIterator<AEGCombatVisual> It(World);It;++It)if(It->GetOwner()==Boss&&It->Definition==WallAssembly&&!It->IsActorBeingDestroyed())
   {
    TInlineComponentArray<USkeletalMeshComponent*> Parts;It->GetComponents(Parts);
    for(const auto* Part:Parts)if(Part&&Part!=Body&&Part->GetAttachParent()==Body&&Part->GetAttachSocketName()==TEXT("Bip001-L-Hand")&&Part->IsVisible())bBowAttached=true;
   }
   TestTrue(TEXT("Source wall rig displays its bow on the actual left-hand bone"),bBowAttached);
   TestFalse(TEXT("Wall surface initially waits for action reveal"),Surface->IsVisible());
  }
  else TestEqual(TEXT("Fallback creates no source wall rig"),CountWallRigs(),0);
  if(Case==3)
  {
   FDelegateHandle ProbeHandle;const auto Probe=FEGPresentationReleaseLease::CreateLambda([](AEGChessUnit*){});
   TestFalse(TEXT("Fallback never takes another owner's body lease"),Boss->TryAcquirePresentationReleaseLease(Probe,ProbeHandle));
  }
  if(Case==0)
  {
   FPlatformProcess::SleepNoStats(.12f);Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);WallVisual->Tick(0.f);
   const float Initial=Rules->GetBarrierIntroRemainingSeconds();
   TestTrue(TEXT("Intro progresses while it owns the only hold"),Initial<Expected-.06f&&Initial>1.f);
   const FGuid OtherHold=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(OtherHold,true);
   FPlatformProcess::SleepNoStats(1.15f);Rules->TickComponent(3.f,LEVELTICK_PauseTick,nullptr);WallVisual->Tick(0.f);
   const float AfterOther=Rules->GetBarrierIntroRemainingSeconds();
   TestTrue(TEXT("A second presentation hold freezes intro"),FMath::IsNearlyEqual(AfterOther,Initial,.04f));
   TestFalse(TEXT("Overlapping hold also freezes wall reveal"),Surface->IsVisible());
   Rules->CompleteBarrierIntroForTests();
   TestTrue(TEXT("Fast-forward cannot bypass another presentation owner"),Rules->IsBarrierIntroPresentationActive()&&FMath::IsNearlyEqual(Rules->GetBarrierIntroRemainingSeconds(),AfterOther,.04f));
   TestTrue(TEXT("Second hold never consumes the challenge"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
   Mode->BattleRules->SetPresentationHold(OtherHold,false);
   if(!TestTrue(TEXT("External pause overlaps wall intro"),Mode->SetPause(PC)))return false;
   FPlatformProcess::SleepNoStats(.30f);Rules->TickComponent(3.f,LEVELTICK_PauseTick,nullptr);
   TestTrue(TEXT("Independent pause freezes intro"),FMath::IsNearlyEqual(Rules->GetBarrierIntroRemainingSeconds(),AfterOther,.04f));
   PC->SetPause(false);
   FPlatformProcess::SleepNoStats(.12f);Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
   TestTrue(TEXT("Intro resumes after unrelated pauses release"),Rules->GetBarrierIntroRemainingSeconds()<AfterOther-.06f);
  }
  if(Case==2)
  {
   FPlatformProcess::SleepNoStats(.12f);Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
   // NotifyPresentationWillRelease invokes observers before death finishes.
   // The borrowed body must be returned before an observer can replace it.
   bool bObserverCalled=false,bRigReleasedBeforeObserver=false;
   const FDelegateHandle Observer=Boss->OnPresentationWillRelease.AddLambda([&](AEGChessUnit*)
   {bObserverCalled=true;bRigReleasedBeforeObserver=CountWallRigs()==0;});
   Boss->Die();Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
   const FVector PlayerAtDeath=Hero->GetActorLocation();
   FPlatformProcess::SleepNoStats(.45f);Rules->TickComponent(1.f,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Boss death cancels all remaining friendly displacement"),Hero->GetActorLocation().Equals(PlayerAtDeath,.1f));
   Boss->OnPresentationWillRelease.Remove(Observer);
   TestTrue(TEXT("Death notifies presentation observers"),bObserverCalled);
   TestTrue(TEXT("Borrowed body is released before death observers"),bRigReleasedBeforeObserver);
   TestEqual(TEXT("Boss death destroys its source-wall rig"),CountWallRigs(),0);
   TestEqual(TEXT("Boss death finishes its authority state"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Finished));
   TestFalse(TEXT("Boss death stops wall introduction"),Rules->IsBarrierIntroPresentationActive());
   TestFalse(TEXT("Boss death removes wall hold"),Mode->BattleRules->HasPresentationHold());
   TestFalse(TEXT("Boss death releases its world pause"),World->IsPaused());
   TestFalse(TEXT("Boss death removes wall obstacle"),Rules->IsWallActive());
   Mode->BattleRules->Stop();continue;
  }
  Rules->CompleteBarrierIntroForTests();
  TestFalse(TEXT("Finished intro releases presentation"),Rules->IsBarrierIntroPresentationActive());
  TestEqual(TEXT("Finished intro removes source-wall rig"),CountWallRigs(),0);
  TestFalse(TEXT("Finished intro releases its hold"),Mode->BattleRules->HasPresentationHold());
  TestTrue(TEXT("Challenge still starts with full eight seconds"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),8.f,.01f));
  TestTrue(TEXT("Original body animation mode is restored"),Body->GetAnimationMode()==OriginalMode&&Body->GetAnimClass()==OriginalClass);
  if(Case==0)
  {
   // The intro token has been cleared. Advancing only the cosmetic wall now
   // must reveal it; this does not advance the challenge countdown.
   WallVisual->Tick(1.2f);
   TestTrue(TEXT("Wall reveal advances after intro token clears"),Surface->IsVisible());
  }
  Rules->TickComponent(1.25f,LEVELTICK_All,nullptr);
  TestTrue(TEXT("Only effective battle time reduces wall countdown"),FMath::IsNearlyEqual(Rules->GetBarrierRemainingSeconds(),6.75f,.02f));
  if(ExistingLease.IsValid())Boss->ReleasePresentationLease(ExistingLease);
  Mode->BattleRules->Stop();
  TestFalse(TEXT("Battle stop removes wall obstacle"),Rules->IsWallActive());
  TestFalse(TEXT("Battle stop clears all intro holds"),Mode->BattleRules->HasPresentationHold());
  TestFalse(TEXT("Stopped intro has no active presentation"),Rules->IsBarrierIntroPresentationActive());
  if(Case==0)
  {
   Mode->BattleRules->Start({});Mode->BattleRules->CompleteOpeningForTests();
   Hero->GetHeroSkill()->Configure(Mode->BattleRules,Hero->GetHeroSkill()->GetLevel(),false);Mode->BattleRules->EnterUnit(Hero);
   Boss->GetHeroSkill()->Configure(Mode->BattleRules,6,false);Mode->BattleRules->EnterUnit(Boss);
   Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Mode->BattleRules,1000.f,100.f,50);
   if(!TestNotNull(TEXT("Boss reattached for a new round"),Rules)||!TestTrue(TEXT("New round can start another wall"),FEGCombatExecution::Damage(Hero,Boss,100000.f)))return false;
   TestTrue(TEXT("New round starts fresh authored presentation"),Rules->IsBarrierIntroPresentationActive()&&FMath::IsNearlyEqual(Rules->GetBarrierIntroDurationSeconds(),2.333333f,.02f));
   FPlatformProcess::SleepNoStats(.12f);Rules->TickComponent(0.f,LEVELTICK_PauseTick,nullptr);
   Mode->BattleRules->Stop();
   const FVector PlayerAtStop=Hero->GetActorLocation();
   FPlatformProcess::SleepNoStats(.45f);Rules->TickComponent(1.f,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Interrupted new round cannot continue an old pushback"),Hero->GetActorLocation().Equals(PlayerAtStop,.1f));
   TestFalse(TEXT("Restart cleanup releases the new hold"),Mode->BattleRules->HasPresentationHold());
   TestEqual(TEXT("Restart cleanup destroys its source-wall rig"),CountWallRigs(),0);
  }
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionWallSoundClockTest,
 "ElyGambit.Corruption.WallSoundClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionWallSoundClockTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();if(!TestNotNull(TEXT("Controlled standalone world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;
 World->InitializeActorsForPlay(URL);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("Test game mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
 auto* Battle=Mode->BattleRules.Get();Battle->CompleteOpeningForTests();
 auto* PC=World->SpawnActor<APlayerController>();if(!TestNotNull(TEXT("Pause controller"),PC))return false;
 if(!PC->PlayerState)PC->SetPlayerState(World->SpawnActor<APlayerState>());
 if(!TestNotNull(TEXT("Pause player state"),PC->PlayerState.Get()))return false;
 const FGuid Round=Battle->GetRoundId(),SoundToken=FGuid::NewGuid(),OtherToken=FGuid::NewGuid();
 Battle->SetPresentationHold(SoundToken,true);
 TestTrue(TEXT("Sound token is owned"),Battle->HasPresentationHold(SoundToken));
 TestTrue(TEXT("Sound advances when its hold is exclusive"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 Battle->SetPresentationHold(OtherToken,true);
 TestFalse(TEXT("Another presentation hold freezes sound"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 Battle->SetPresentationHold(OtherToken,false);
 TestTrue(TEXT("Sound resumes when its hold is exclusive again"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 if(!TestTrue(TEXT("Independent pause can overlap sound"),Mode->SetPause(PC)))return false;
 TestFalse(TEXT("Independent pause freezes sound"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 PC->SetPause(false);
 TestTrue(TEXT("Sound resumes after independent pause"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 Battle->SetPresentationHold(OtherToken,true);Battle->SetPresentationHold(SoundToken,false);
 TestFalse(TEXT("Released sound token cannot bypass another hold"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 Battle->SetPresentationHold(OtherToken,false);
 TestTrue(TEXT("Released sound token can finish in active combat"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 if(!TestTrue(TEXT("Independent pause can hold active combat"),PC->SetPause(true)))return false;
 TestFalse(TEXT("External-only pause freezes sound"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 PC->SetPause(false);Battle->Stop();
 TestFalse(TEXT("Stopped battle invalidates sound"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 Battle->Start({});Battle->CompleteOpeningForTests();
 TestFalse(TEXT("Old round cannot resume stale sound"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Round,SoundToken));
 TestTrue(TEXT("New active round accepts an unscoped sound"),UEGGameAudioComponent::CanAdvanceBossEffect(Battle,Battle->GetRoundId(),FGuid()));
 Battle->Stop();
 AddInfo(TEXT("Device-independent audio clock and round ownership only; no voice playback or listening was tested."));
 return true;
}
#endif
