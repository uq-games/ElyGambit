#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Game/EGPieceTestGameMode.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Placement/EGDeploymentBoard.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterProductionMotionBindings,
 "ElyGambit.Monsters.Release.ProductionMotionBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterProductionMotionBindings::RunTest(const FString&)
{
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);
 if(!Seed)return false;
 TStrongObjectPtr<UEGRunDefinition> Run(EGSevenStage::Build(GetTransientPackage(),Seed));
 if(!TestNotNull(TEXT("Formal seven-stage run"),Run.Get())||!Run->Stages.IsValidIndex(3))return false;
 TSet<EEGMonsterUltimate> Seen;
 for(const auto& Entry:Run->Stages[3].OrdinaryMonsterDefinitions)
 {
  const UEGHeroDefinition* D=Entry.Get();
  if(!TestNotNull(TEXT("Production monster definition"),D)||!TestNotNull(TEXT("Monster profile"),D->MonsterAttack.Get()))continue;
  const FString Name=D->HeroId.ToString();Seen.Add(D->MonsterAttack->Ultimate);
  USkeletalMesh* Mesh=D->GetMesh().LoadSynchronous();
  if(!TestNotNull(*(Name+TEXT(" mesh")),Mesh))continue;
  UClass* AnimClass=D->GetAnimationClass().LoadSynchronous();
  TestTrue(*(Name+TEXT(" uses supported animation graph")),AnimClass&&AnimClass->IsChildOf(UEGCombatAnimInstance::StaticClass()));
  const auto CheckSequence=[&](const TCHAR* Label,UAnimSequence* Sequence)
  {
   if(!TestNotNull(*(Name+TEXT(" ")+Label),Sequence))return;
   TestTrue(*(Name+TEXT(" compatible sequence skeleton")),Sequence->GetSkeleton()==Mesh->GetSkeleton());
   TestTrue(*(Name+TEXT(" nonempty sequence")),Sequence->GetPlayLength()>0.f);
  };
  CheckSequence(TEXT("idle"),D->GetIdleAnimation().LoadSynchronous());
  CheckSequence(TEXT("walk"),D->GetWalkAnimation().LoadSynchronous());
  CheckSequence(TEXT("attack"),D->GetAttackAnimation().LoadSynchronous());
  CheckSequence(TEXT("skill"),D->GetSkillAnimation().LoadSynchronous());
  CheckSequence(TEXT("hit"),D->GetHitAnimation().LoadSynchronous());
  CheckSequence(TEXT("death"),D->GetDeathAnimation().LoadSynchronous());
  for(const auto Action:{EGPresentationAction::Attack,EGPresentationAction::Skill})
  {
   UAnimMontage* Montage=D->GetPresentationMontage(Action);
   if(TestNotNull(*(Name+TEXT(" action montage")),Montage))
    TestTrue(*(Name+TEXT(" compatible action montage skeleton")),Montage->GetSkeleton()==Mesh->GetSkeleton());
  }
 }
 for(const auto Kind:{EEGMonsterUltimate::Shield,EEGMonsterUltimate::EnhancedShot,EEGMonsterUltimate::TeamSpeed,EEGMonsterUltimate::CrystalSeed})
  TestTrue(TEXT("All four campaign monster roles present"),Seen.Contains(Kind));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterDamagePresentation,
 "ElyGambit.Monsters.Release.DamagePresentationLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterDamagePresentation::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;W->SetGameMode(URL);auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;
 W->InitializeActorsForPlay(URL);W->BeginPlay();auto* GM=W->GetAuthGameMode<AEGPieceTestGameMode>();
 const auto Tick=[W](float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.01f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.01f);}};
 if(TestNotNull(TEXT("Test game mode"),GM)&&TestTrue(TEXT("Start battle"),GM->EnterCombat()))
 {
  AEGChessUnit* Hero=nullptr;AEGDemoCombatUnit* Enemy=nullptr;
  for(const auto& Entry:GM->GetBattleUnits())if(auto* U=Entry.Get())
  {
   U->GetCombat()->SetComponentTickEnabled(false);
   if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)Hero=U;
   else if(U->GetDefinition()&&U->GetDefinition()->MonsterAttack&&U->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Melee)Enemy=Cast<AEGDemoCombatUnit>(U);
  }
  if(TestNotNull(TEXT("Player target"),Hero)&&TestNotNull(TEXT("Melee monster"),Enemy))
  {
   Hero->SetActorLocation(FVector(0,0,88));Enemy->SetActorLocation(FVector(-130,0,88));
   auto* Mesh=Enemy->GetVisiblePresentationMesh();Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
   auto* Anim=Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance());
   if(TestNotNull(TEXT("Production animation instance"),Anim))
   {
    Enemy->NotifyDamageReceived(0.f);TestFalse(TEXT("Zero damage does not trigger reaction"),Anim->bHit);
    Enemy->NotifyDamageReceived(1.f);TestTrue(TEXT("Positive damage triggers hit pose"),Anim->bHit);
    Tick(.1f);const float Age=Anim->HitTime;Enemy->NotifyDamageReceived(1.f);Tick(.05f);
    TestTrue(TEXT("Repeated damage does not restart reaction"),Anim->HitTime>Age);
    Tick(Enemy->GetDefinition()->GetHitAnimation().LoadSynchronous()->GetPlayLength()+.1f);
    TestFalse(TEXT("Hit returns to locomotion"),Anim->bHit);
    Enemy->GetCombat()->ResetCombat();Enemy->GetCombat()->TickComponent(.001f,LEVELTICK_All,nullptr);
    auto* Skill=Enemy->GetHeroSkill();
    if(TestTrue(TEXT("Real attack begins"),Skill->IsBusy()))
    {
     const FGuid Action=Skill->ActionId;auto* Montage=Skill->GetActiveMontage();
     Enemy->NotifyDamageReceived(1.f);
     TestFalse(TEXT("Hit does not interrupt attack"),Anim->bHit);
     TestTrue(TEXT("Attack action and montage are preserved"),Skill->ActionId==Action&&Skill->GetActiveMontage()==Montage&&Skill->IsBusy());
    }
    Enemy->Die();Tick(.05f);Enemy->NotifyDamageReceived(1.f);
    TestTrue(TEXT("Death pose remains terminal after damage"),Anim->bDead&&!Anim->bHit);
   }
  }
 }
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 return true;
}
#endif
