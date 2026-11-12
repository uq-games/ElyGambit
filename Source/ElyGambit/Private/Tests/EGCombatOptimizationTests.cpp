#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHitFeedbackComponent.h"
#include "Components/EGUltimateRingComponent.h"
#include "Components/CapsuleComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGConfirmedSkillBehaviors.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGSourceSummonDefinition.h"
#include "Presentation/EGSkillProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "UI/EGUnitInformation.h"

namespace
{
struct FCombatOptimizationWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGCardManager* Cards=nullptr;
 FCombatOptimizationWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return;
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return;Run->CardPool.Reset();Run->StartingCards=1;
  Run->CardPool.Add(Card(TEXT("Pardo")));
  Run->StartingCard=Card(TEXT("Pardo"));
  Run->CardRules.DeploymentLimit=4;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;Grid->CellWidth=500;Grid->CellDepth=500;
  Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();
  W->InitializeActorsForPlay(URL);W->BeginPlay();
 }
 ~FCombatOptimizationWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 UEGCardDefinition* Card(const TCHAR* Key){return LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));}
 FGuid Deploy(const TCHAR* Key,int32 X)
 {
  auto* D=Card(Key);Cards->GrantCard(D);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{X,1});return GM->GetRoster()->FindHeroByDefinition(D->HeroDefinition);
 }
 AEGChessUnit* Unit(FGuid Id){for(auto* U:GM->GetCombatUnits())if(U->GetInstanceId()==Id)return U;return nullptr;}
 AEGChessUnit* Enemy(){for(auto* U:GM->GetCombatUnits())if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)return U;return nullptr;}
 void Freeze()
 {
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* A=U->GetAbilitySystemComponent();
   A->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);A->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);
   A->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
  }
 }
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.05f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}}
 bool Cast(AEGChessUnit* U){U->GetHeroSkill()->CancelAction();U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),U->GetAttributes()->GetMaxMana());return U->GetHeroSkill()->BeginAction(true);}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGTauntClock,"ElyGambit.CombatOptimization.TauntClockAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGTauntClock::RunTest(const FString&)
{
 FCombatOptimizationWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const FGuid Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start battle"),E.GM->EnterCombat()))return false;
 E.Freeze();auto* U=E.Unit(Id);auto* Enemy=E.Enemy();
 if(!TestNotNull(TEXT("Target unit"),U)||!TestNotNull(TEXT("Taunt source"),Enemy))return false;
 auto* Skill=U->GetHeroSkill();
 Skill->SetTaunt(Enemy,1.f);E.Tick(.2f);
 TestTrue(TEXT("Taunt stays active before its deadline"),Skill->IsTauntActive());
 E.Tick(.9f);TestFalse(TEXT("Taunt expires while battle time advances"),Skill->IsTauntActive());
 Skill->SetTaunt(Enemy,.6f);E.Tick(.3f);
 TestTrue(TEXT("Refreshed taunt starts a new duration"),Skill->IsTauntActive());
 E.Tick(.4f);TestFalse(TEXT("Refreshed taunt expires without extending the old duration"),Skill->IsTauntActive());
 Skill->SetTaunt(Enemy,1.f);Enemy->Destroy();E.Tick(.1f);
 TestFalse(TEXT("Destroyed source cannot retain taunt"),Skill->IsTauntActive());
 TestFalse(TEXT("Destroyed source is cleared"),Skill->ForcedTarget.IsValid());
 Skill->EndBattle();TestFalse(TEXT("End battle clears taunt"),Skill->IsTauntActive());
 return true;
}
#endif
