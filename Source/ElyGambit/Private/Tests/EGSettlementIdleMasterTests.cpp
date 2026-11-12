#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "EGSettlementTestClock.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Actor/EGArtPiece.h"
#include "Presentation/EGBoardMasterActionComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGChessAnimInstance.h"
#include "Presentation/EGSettlementPresentation.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
UAnimSequence* SettlementMasterAttack(const UEGHeroDefinition* Hero)
{
 if(!Hero)return nullptr;
 if(Hero->PresentationDefinition&&!Hero->PresentationDefinition->AttackSequence.IsEmpty())
  if(auto* Clip=Hero->PresentationDefinition->AttackSequence[0].Animation.LoadSynchronous())return Clip;
 return Hero->GetAttackAnimation().LoadSynchronous();
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettlementIdleMaster,
 "ElyGambit.Presentation.SettlementIdleMaster",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettlementIdleMaster::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* Player=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"));
 auto* Enemy=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
 auto* PlayerAttack=SettlementMasterAttack(Player);
 auto* EnemyAttack=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/SourceWall20261108/A_Corruption_SummonWall_BoardCandidate.A_Corruption_SummonWall_BoardCandidate"));
 if(!TestNotNull(TEXT("Default independent player master"),Player)||!TestNotNull(TEXT("Enemy master"),Enemy)||
    !TestNotNull(TEXT("Player master attack"),PlayerAttack)||!TestNotNull(TEXT("Enemy master attack"),EnemyAttack))return false;
 const TCHAR* HeroPaths[]={TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"),
  TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Human.DA_Human")};
 for(const TCHAR* Path:HeroPaths)
 {
  auto* Formal=LoadObject<UEGHeroDefinition>(nullptr,Path);
  if(!TestNotNull(TEXT("Formal piece definition"),Formal)||!TestNotNull(TEXT("Piece presentation"),Formal->PresentationDefinition.Get()))return false;
  auto* Hero=DuplicateObject<UEGHeroDefinition>(Formal,W);
  Hero->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(Formal->PresentationDefinition.Get(),Hero);
  auto* P=Hero->PresentationDefinition.Get();
  UAnimSequence* PieceClip=Hero->GetIdleAnimation().LoadSynchronous();
  if(!TestNotNull(TEXT("Fixture piece idle"),PieceClip))return false;
  P->SettlementIdleAnimation=PieceClip;P->SettlementIdleAssembly.Reset();
  if(Hero->HeroKind==EEGHeroKind::ElysiaHerrscher)
  {
   P->SettlementIdleVariants.SetNum(1);
   P->SettlementIdleVariants[0].Animation=PieceClip;
   P->SettlementIdleVariants[0].Assembly.Reset();
  }
  for(const bool bVictory:{true,false})
  {
   auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
   if(!TestTrue(TEXT("Initialize living piece"),Unit&&Unit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
   auto* Body=Unit->GetVisiblePresentationMesh();auto* Anim=Cast<UEGCombatAnimInstance>(Body->GetAnimInstance());
   if(!TestNotNull(TEXT("Piece combat anim instance"),Anim))return false;
   auto* EnemyPiece=W->SpawnActor<AEGDemoCombatUnit>();
   if(!TestTrue(TEXT("Initialize living enemy piece"),EnemyPiece&&EnemyPiece->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
   auto* EnemyAnim=Cast<UEGCombatAnimInstance>(EnemyPiece->GetVisiblePresentationMesh()->GetAnimInstance());
   if(!TestNotNull(TEXT("Enemy piece anim instance"),EnemyAnim))return false;
   const FVector PieceLocation=Unit->GetActorLocation();
   auto* Settlement=W->SpawnActor<AEGSettlementPresentation>();int32 Impacts=0,Finishes=0;
   Settlement->OnImpact.AddLambda([&](){++Impacts;});Settlement->OnFinished.AddLambda([&](){++Finishes;});
   Settlement->StartPresentation({Unit,EnemyPiece},Unit,FVector::ZeroVector,FVector(500,0,0),bVictory);
   auto* FriendlyActor=Settlement->GetMasterActor(true);
   auto* Friendly=FriendlyActor?FriendlyActor->Body.Get():nullptr;
   auto* EnemyActor=Settlement->GetMasterActor(false);
   auto* Opponent=EnemyActor?EnemyActor->Body.Get():nullptr;
   if(!TestNotNull(TEXT("Independent friendly master"),Friendly)||!TestNotNull(TEXT("Independent opponent master"),Opponent))return false;
   TestTrue(TEXT("Friendly master is visible even with an Elysia piece"),Friendly->IsVisible());
   TestTrue(TEXT("Master always uses original Elysia mesh"),Friendly->GetSkeletalMeshAsset()==Player->GetMesh().LoadSynchronous());
   TestTrue(TEXT("Piece still uses its own result pose"),Anim->PassiveClip==PieceClip);
   TestTrue(TEXT("The piece never moves to master station"),Unit->GetActorLocation().Equals(PieceLocation,.01f));
   auto* Attacker=bVictory?Friendly:Opponent;auto* Defender=bVictory?Opponent:Friendly;
   auto* AttackClip=bVictory?PlayerAttack:EnemyAttack;
   auto* Action=Attacker->GetOwner()->FindComponentByClass<UEGBoardMasterActionComponent>();
   auto* Montage=Action?Action->GetPlayingMontage():nullptr;
   TestTrue(TEXT("Winning persistent master starts a blended montage"),Montage&&Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()==AttackClip&&Montage->BlendIn.GetBlendTime()>0&&Montage->BlendOut.GetBlendTime()>0);
   TestTrue(TEXT("Master keeps an animation blueprint"),Cast<UEGCombatAnimInstance>(Attacker->GetAnimInstance())!=nullptr);
   auto* InitialInstance=Attacker->GetAnimInstance();
   AdvanceSettlementFixture(Settlement,70);
   TestEqual(TEXT("No impact before release notify"),Impacts,0);
   AdvanceSettlementFixture(Settlement,100);
   TestEqual(TEXT("Notify launches one settlement impact"),Impacts,1);
   auto* HitAction=Defender->GetOwner()->FindComponentByClass<UEGBoardMasterActionComponent>();
   auto* HitMontage=HitAction?HitAction->GetPlayingMontage():nullptr;
   TestTrue(TEXT("Defender plays the source hit montage"),HitMontage&&HitMontage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()->GetName()==(bVictory?TEXT("A_Corruption_Hit"):TEXT("A_Elysia_Hit")));
   TestTrue(TEXT("Attack never replaces the animation instance"),Attacker->GetAnimInstance()==InitialInstance);
   TestTrue(TEXT("Settlement reuses the persistent master actors"),Settlement->GetMasterActor(true)==FriendlyActor&&Settlement->GetMasterActor(false)==EnemyActor);
   TestTrue(TEXT("Independent piece retains result pose"),Anim->PassiveClip==PieceClip);
   const float ExpectedSample=bVictory?FMath::Fmod(Settlement->GetElapsedSeconds(),PieceClip->GetPlayLength()):0.f;
   TestTrue(TEXT("Winners animate their idle while losers stay still"),FMath::IsNearlyEqual(Anim->PassiveTime,ExpectedSample,.01f));
   const float EnemySample=bVictory?0.f:FMath::Fmod(Settlement->GetElapsedSeconds(),PieceClip->GetPlayLength());
   TestTrue(TEXT("Enemy victory and defeat use the same idle/freeze rule"),EnemyAnim->PassiveClip==PieceClip&&FMath::IsNearlyEqual(EnemyAnim->PassiveTime,EnemySample,.01f));
   Settlement->Tick(FMath::Max(0.f,5.99f-Settlement->GetElapsedSeconds()));
   TestEqual(TEXT("No early finish"),Finishes,0);Settlement->Tick(.02f);Settlement->Tick(.2f);
   TestEqual(TEXT("Finish fires once"),Finishes,1);TestEqual(TEXT("Impact does not repeat"),Impacts,1);
   Settlement->Destroy();
   FDelegateHandle Handle;
   const auto Probe=FEGPresentationReleaseLease::CreateLambda([](AEGChessUnit*){});
   TestTrue(TEXT("Settlement returns piece pose lease"),Unit->TryAcquirePresentationReleaseLease(Probe,Handle));
   Unit->ReleasePresentationLease(Handle);Unit->Destroy();EnemyPiece->Destroy();
  }
 }
 // A dead enemy is never offered an idle lease or changed by settlement.
 auto* Dead=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize dead-piece fixture"),Dead&&Dead->InitializeUnit(Enemy,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 Dead->Die();Dead->Tick(.05f);
 auto* DeadAnim=Cast<UEGCombatAnimInstance>(Dead->GetVisiblePresentationMesh()->GetAnimInstance());
 if(!TestNotNull(TEXT("Dead enemy animation"),DeadAnim))return false;
 const bool WasDead=DeadAnim->bDead;
 const auto* DeadClip=DeadAnim->PassiveClip.Get();
 auto* DeathSettlement=W->SpawnActor<AEGSettlementPresentation>();
 DeathSettlement->StartPresentation({Dead},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 DeathSettlement->Tick(.5f);
 TestFalse(TEXT("Dead enemy remains dead"),Dead->IsAlive_Implementation());
 TestEqual(TEXT("Settlement never changes dead enemy animation state"),DeadAnim->bDead,WasDead);
 TestTrue(TEXT("Settlement never replaces dead enemy pose"),DeadAnim->PassiveClip.Get()==DeadClip);
 DeathSettlement->Destroy();Dead->Destroy();
 // Legacy/native-single-node actors keep their graph and restore their prior
 // animation state after the losing pose lease is relinquished.
 for(const bool bSingle:{false,true})
 {
  auto* Piece=W->SpawnActor<AEGDemoCombatUnit>();
  if(!TestTrue(TEXT("Initialize alternate animation fixture"),Piece&&Piece->InitializeUnit(Player,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
  auto* Body=Piece->GetVisiblePresentationMesh();auto* Idle=Player->GetIdleAnimation().LoadSynchronous();
  if(!TestNotNull(TEXT("Alternate fixture idle"),Idle))return false;
  if(bSingle){Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);Body->PlayAnimation(Idle,true);}
  else
  {
   Body->SetAnimationMode(EAnimationMode::AnimationBlueprint);Body->SetAnimInstanceClass(UEGChessAnimInstance::StaticClass());
   auto* Legacy=Cast<UEGChessAnimInstance>(Body->GetAnimInstance());
   if(!TestNotNull(TEXT("Legacy fixture anim instance"),Legacy))return false;
   Legacy->ApplyClips(Player->BuildPresentationClips());Legacy->SetLocomotionSpeed(0.f);
  }
  auto* OriginalInstance=Body->GetAnimInstance();
  if(!TestNotNull(TEXT("Alternate graph exists"),OriginalInstance))return false;
  const bool OriginalPause=Body->bPauseAnims;
  auto* Result=W->SpawnActor<AEGSettlementPresentation>();
  Result->StartPresentation({Piece},nullptr,FVector::ZeroVector,FVector(500,0,0),false);
  TestTrue(TEXT("Losing alternate graph freezes"),Body->bPauseAnims);
  TestTrue(TEXT("Settlement preserves animation instance"),Body->GetAnimInstance()==OriginalInstance);
  Result->Tick(1.f);Result->Destroy();
  TestEqual(TEXT("Cleanup restores original pause flag"),static_cast<bool>(Body->bPauseAnims),OriginalPause);
  TestTrue(TEXT("Cleanup keeps original animation instance"),Body->GetAnimInstance()==OriginalInstance);
  if(bSingle)TestTrue(TEXT("Single-node playback restored"),Body->GetSingleNodeInstance()&&Body->GetSingleNodeInstance()->IsPlaying());
  Piece->Destroy();
 }
 // Interrupt before release, then replay on exactly the same actor/graph.
 auto* Interrupted=W->SpawnActor<AEGSettlementPresentation>();
 int32 CanceledImpacts=0;Interrupted->OnImpact.AddLambda([&](){++CanceledImpacts;});
 Interrupted->StartPresentation({},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 auto* Persistent=Interrupted->GetMasterActor(true);
 auto* Action=Persistent?Persistent->FindComponentByClass<UEGBoardMasterActionComponent>():nullptr;
 if(!TestNotNull(TEXT("Interrupt fixture action"),Action))return false;
 auto* Graph=Persistent->Body->GetAnimInstance();
 auto* CanceledMontage=Action->GetPlayingMontage();
 AdvanceSettlementFixture(Interrupted,20);
 Interrupted->Destroy();
 TestFalse(TEXT("Cancellation releases action ownership during blend out"),Action->IsBusy());
 auto* Again=W->SpawnActor<AEGSettlementPresentation>();int32 AgainImpacts=0;
 Again->OnImpact.AddLambda([&](){++AgainImpacts;});
 Again->StartPresentation({},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 TestTrue(TEXT("Immediate replay retains master and graph"),Again->GetMasterActor(true)==Persistent&&Persistent->Body->GetAnimInstance()==Graph);
 if(CanceledMontage)for(const auto& Event:CanceledMontage->Notifies)
  if(auto* Notify=Cast<UEGBoardMasterReleaseNotify>(Event.Notify))Action->DispatchRelease(Notify);
 TestEqual(TEXT("Stale notify cannot launch replay"),AgainImpacts,0);
 AdvanceSettlementFixture(Again,170);
 TestEqual(TEXT("Replayed montage releases exactly once"),AgainImpacts,1);
 TestEqual(TEXT("Canceled request never impacts"),CanceledImpacts,0);
 Again->Destroy();
 AddInfo(TEXT("Controlled persistent-master fixtures: both outcomes, notify, cancellation/replay and piece leases. No natural play, audio or visual acceptance."));
 return !HasAnyErrors();
}
#endif
