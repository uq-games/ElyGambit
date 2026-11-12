#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGSettlementPresentation.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettlementDeadPoseLifecycle,
 "ElyGambit.Settlement.DeadPoseStopAndRevive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettlementDeadPoseLifecycle::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* World=GI->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL Url;World->SetGameMode(Url);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 World->InitializeActorsForPlay(Url);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("test game mode"),Mode)||!TestTrue(TEXT("battle starts"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 for(auto* Unit:Mode->BattleRules->Units())if(Unit&&Unit->GetCombat())Unit->GetCombat()->SetComponentTickEnabled(false);
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"));
 if(!TestNotNull(TEXT("formal human card"),Card)||!TestNotNull(TEXT("formal human definition"),Card->HeroDefinition.Get()))return false;
 auto Spawn=[&](EEGFaction Faction,FIntPoint Cell)->AEGChessUnit*
 {
  auto* Unit=Mode->SpawnBattleSummon(Card->HeroDefinition,1,Grid->CellToWorld(Cell)+FVector(0,0,88),Faction);
  if(Unit){Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->Configure(Mode->BattleRules,1,false);Mode->BattleRules->EnterUnit(Unit);}
  return Unit;
 };
 AEGChessUnit* Player=Spawn(EEGFaction::Player,{0,1});AEGChessUnit* Enemy=Spawn(EEGFaction::Enemy,{6,5});
 AEGChessUnit* Reviver=Spawn(EEGFaction::Player,{2,1});
 if(!TestNotNull(TEXT("player human"),Player)||!TestNotNull(TEXT("enemy human"),Enemy)||!TestNotNull(TEXT("revival fixture"),Reviver))return false;
 const auto Advance=[&](float Seconds){while(Seconds>0.f){const float Dt=FMath::Min(.025f,Seconds);++GFrameCounter;World->Tick(LEVELTICK_All,Dt);Seconds-=Dt;}};
 for(auto* Unit:{Player,Enemy})
 {
  Unit->GetVisiblePresentationMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),0.f);
 }
 Advance(.1f);
 TArray<UEGCombatAnimInstance*> Animations;
 for(auto* Unit:{Player,Enemy})
 {
  auto* Anim=Cast<UEGCombatAnimInstance>(Unit->GetVisiblePresentationMesh()->GetAnimInstance());
  if(!TestNotNull(TEXT("formal combat animation instance"),Anim))return false;
  if(!TestTrue(TEXT("death pose entered"),Anim->bDead)||!TestNotNull(TEXT("death clip"),Anim->DeathClip.Get()))return false;
  // Stand in for a source assembly whose death pose has already advanced.
  // Releasing it must transfer that sampled clock instead of restarting it.
  const float Sample=FMath::Min(.3f,Anim->DeathClip->GetPlayLength()*.5f);
  if(!TestTrue(TEXT("death clip supports a nonzero sample"),Sample>.01f))return false;
  Anim->SetSourcePose(Anim->DeathClip.Get(),Sample,3);Animations.Add(Anim);
 }
 for(int32 Index=0;Index<2;++Index)
 {
  auto* Unit=Index==0?Player:Enemy;auto* Anim=Animations[Index];const float Before=Anim->DeathTime;
  Unit->GetHeroSkill()->EndBattle();Unit->EndCombat_Implementation();Unit->GetHeroSkill()->EndBattle();
  TestTrue(TEXT("repeated teardown keeps dead state"),Anim->bDead&&!Unit->IsAlive_Implementation());
  TestTrue(TEXT("released death sample is not rewound"),Anim->DeathClip&&Anim->DeathTime>=Before-.001f);
 }
 auto* Settlement=World->SpawnActor<AEGSettlementPresentation>();
 if(!TestNotNull(TEXT("settlement presentation"),Settlement))return false;
 TArray<AEGChessUnit*> Units{Player,Enemy,Reviver};Settlement->StartPresentation(Units,nullptr,FVector::ZeroVector,FVector::ZeroVector,true);
 Advance(.1f);
 for(int32 Index=0;Index<2;++Index)
 {
  auto* Anim=Animations[Index];auto* Unit=Index==0?Player:Enemy;
  TestTrue(TEXT("settlement cannot revive or stand a corpse"),!Unit->IsAlive_Implementation()&&Anim->bDead&&Anim->DeathClip);
  TestTrue(TEXT("settlement cannot restart corpse from zero"),Anim->DeathTime>.01f);
 }
 Settlement->Destroy();
 // Revival begins before lethal damage, as in the real recoverable transaction.
 auto* ReviveSkill=Reviver->GetHeroSkill();
 if(TestTrue(TEXT("revive transaction begins"),ReviveSkill->BeginRevive(.2f,.5f,0.f)))
 {
  Reviver->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),0.f);
  Advance(.5f);
  TestTrue(TEXT("real revive restores life and ends recovery"),Reviver->IsAlive_Implementation()&&!ReviveSkill->IsRecoverable());
  if(auto* Anim=Cast<UEGCombatAnimInstance>(Reviver->GetVisiblePresentationMesh()->GetAnimInstance()))
   TestFalse(TEXT("revived actor is not locked in death state"),Anim->bDead);
  else AddError(TEXT("Revived unit lost its combat animation instance"));
 }
 Mode->BattleRules->Stop();return true;
}
#endif
