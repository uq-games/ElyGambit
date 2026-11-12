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
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGHitFeedbackComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Character/EGVillVEmplacement.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "Skills/EGSakuraVillVSkillBehaviors.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGMobiusLinkVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGVillVCombatFX.h"
#include "Presentation/EGVillVShotVisual.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGSourceEffect.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "EngineUtils.h"
#include "Tickable.h"

namespace
{
struct FFlameWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGCardManager* Cards=nullptr;AEGDeploymentBoard* Grid;
 FFlameWorld(FAutomationTestBase& Test,bool bRoundShop=true):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return;
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return;Run->bRoundShopRules=bRoundShop;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
  Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  W->InitializeActorsForPlay(URL);W->BeginPlay();
 }
 ~FFlameWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 FGuid Deploy(const TCHAR* Folder,const TCHAR* Key,int32 X)
 {
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,*FString::Printf(TEXT("/Game/Blueprints/Characters/%s/Data/DA_Card_%s"),Folder,Key));
  if(!Card||!Cards->GrantCard(Card))return FGuid();
  if(!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{X,2}))return FGuid();
  return GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
 }
 AEGChessUnit* Find(FGuid Id){for(auto* U:GM->GetCombatUnits())if(U->GetInstanceId()==Id)return U;return nullptr;}
 TArray<AEGChessUnit*> Enemies(){TArray<AEGChessUnit*> R;for(auto* U:GM->GetCombatUnits())if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)R.Add(U);return R;}
 void Freeze()
 {
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* ASC=U->GetAbilitySystemComponent();
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
  }
 }
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.05f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}}
 bool Cast(AEGChessUnit* U){U->GetHeroSkill()->CancelAction();U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),U->GetAttributes()->GetMaxMana());return U->GetHeroSkill()->BeginAction(true);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHuaRevival,"ElyGambit.FlameChasers7x7.HuaRevival",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHuaRevival::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("FuHua"),TEXT("Hua"),2);
 if(!TestTrue(TEXT("Deploy and enter 7x7 combat"),Id.IsValid()&&E.GM->EnterCombat()))return false;
 E.Freeze();auto* U=E.Find(Id);auto* Enemy=E.Enemies()[0];const float Attack=U->GetAttributes()->GetAttackPower();
 TestTrue(TEXT("First lethal damage accepted"),FEGCombatExecution::Damage(Enemy,U,20000));
 TestTrue(TEXT("Lethal enters recoverable state"),U->GetHeroSkill()->IsRecoverable());
 TestFalse(TEXT("Revival cannot be targeted"),U->GetHeroSkill()->IsTargetable());
 TestFalse(TEXT("Already launched damage cannot hurt revival"),FEGCombatExecution::Damage(Enemy,U,100));
 E.Tick(.4f);TestEqual(TEXT("Last hero reviving does not lose"),E.GM->GetMatch()->GetPhase(),EEGMatchPhase::Combat);
 E.Tick(1.7f);TestEqual(TEXT("Revives at fifty percent after two seconds"),U->GetAttributes()->GetHealth(),5000.f);
 TestEqual(TEXT("Normal Hua gains no attack"),U->GetAttributes()->GetAttackPower(),Attack);
 FEGCombatExecution::Damage(Enemy,U,20000);
 TestFalse(TEXT("Second lethal cannot revive"),U->GetHeroSkill()->IsRecoverable());
 TestFalse(TEXT("Second lethal is death"),IEGCombatant::Execute_IsAlive(U));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHuaLeap,"ElyGambit.FlameChasers7x7.HuaLeap",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHuaLeap::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("FuHua"),TEXT("Hua"),6);
 if(!TestTrue(TEXT("Enter combat"),Id.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* Enemy=E.Enemies()[0];Enemy->SetActorLocation(E.Grid->CellToWorld({6,4})+FVector(0,0,88));
 if(!TestTrue(TEXT("Cast original leap"),E.Cast(U)))return false;
 U->GetHeroSkill()->AnimationEvent(TEXT("Takeoff"));
 TestFalse(TEXT("Airborne is untargetable"),U->GetHeroSkill()->IsTargetable());
 TestFalse(TEXT("Airborne ignores old projectile damage"),FEGCombatExecution::Damage(Enemy,U,100));
 auto* Behavior=U->GetHeroSkill()->GetBehavior();
 const auto* Leap=Cast<UEGHuaSkillDefinition>(U->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Hua leap configuration exists"),Leap))return false;
 const float Begin=Leap->TakeoffTime,End=Leap->LandingTime,Duration=End-Begin;
 if(!TestTrue(TEXT("Leap interval is positive"),Duration>0.f))return false;
 Behavior->TickAction(Begin);const FVector Start=U->GetActorLocation();
 Behavior->TickAction(Begin+Duration*.05f);const FVector Early=U->GetActorLocation();
 Behavior->TickAction(Begin+Duration*.25f);const FVector Quarter=U->GetActorLocation();
 Behavior->TickAction(Begin+Duration*.5f);const FVector Middle=U->GetActorLocation();
 Behavior->TickAction(Begin+Duration*.75f);const FVector ThreeQuarter=U->GetActorLocation();
 Behavior->TickAction(Begin+Duration*.95f);const FVector Late=U->GetActorLocation();
 Behavior->TickAction(End);const FVector Destination=U->GetActorLocation();
 const double Distance=FVector::Dist2D(Start,Destination);
 if(TestTrue(TEXT("Leap fixture has horizontal travel"),Distance>1.f))
 {
  TestTrue(TEXT("Takeoff accelerates from rest"),FVector::Dist2D(Start,Early)<Distance*.05);
  TestTrue(TEXT("Quarter travel eases in"),FVector::Dist2D(Start,Quarter)<Distance*.25&&FVector::Dist2D(Start,Quarter)>FVector::Dist2D(Start,Early));
  TestTrue(TEXT("Middle remains halfway to locked destination"),Middle.Equals(FMath::Lerp(Start,Destination,.5f),.1f));
  TestTrue(TEXT("Three-quarter travel eases out"),FVector::Dist2D(Start,ThreeQuarter)>Distance*.75&&FVector::Dist2D(Start,ThreeQuarter)<FVector::Dist2D(Start,Late));
  TestTrue(TEXT("Landing decelerates to rest"),FVector::Dist2D(Late,Destination)<Distance*.05);
  TestTrue(TEXT("Board actor remains at ground height throughout"),FMath::IsNearlyEqual(Start.Z,Early.Z,.01f)&&FMath::IsNearlyEqual(Start.Z,Quarter.Z,.01f)&&FMath::IsNearlyEqual(Start.Z,Middle.Z,.01f)&&FMath::IsNearlyEqual(Start.Z,ThreeQuarter.Z,.01f)&&FMath::IsNearlyEqual(Start.Z,Late.Z,.01f)&&FMath::IsNearlyEqual(Start.Z,Destination.Z,.01f));
 }
 Behavior->TickAction(1.29f);const FVector Locked=U->GetActorLocation();
 Enemy->Die();U->GetHeroSkill()->AnimationEvent(TEXT("Release"));
 TestTrue(TEXT("Landing stays locked after target dies"),U->GetActorLocation().Equals(Locked,20.f));
 const FVector Local=E.Grid->GetActorTransform().InverseTransformPosition(U->GetActorLocation());
 TestTrue(TEXT("Landing remains inside seven columns/rows"),FMath::Abs(Local.X)<1750&&FMath::Abs(Local.Y)<1750);
 TestTrue(TEXT("Landing ends protection"),U->GetHeroSkill()->IsTargetable());
 const auto Position=U->GetActorLocation();U->GetHeroSkill()->AnimationEvent(TEXT("Release"));
 TestTrue(TEXT("Duplicate landing has no movement"),Position.Equals(U->GetActorLocation()));
 U->GetHeroSkill()->CancelAction();TestTrue(TEXT("Cancel releases protection"),U->GetHeroSkill()->IsTargetable());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCollapsedHua,"ElyGambit.FlameChasers7x7.CollapsedBurnAndRevival",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCollapsedHua::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("FuHua/FuSheng"),TEXT("CollapsedHua"),3);
 if(!TestTrue(TEXT("Enter combat"),Id.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* Enemy=E.Enemies()[0];const float Attack=U->GetAttributes()->GetAttackPower();
 if(!TestTrue(TEXT("Cast collapsed ultimate"),E.Cast(U)))return false;
 U->GetHeroSkill()->AnimationEvent(TEXT("Burn"));TestTrue(TEXT("Burn consumes current life without death"),FMath::IsNearlyEqual(U->GetAttributes()->GetHealth(),7000.f,.1f));
 TestFalse(TEXT("Burn does not enter revival"),U->GetHeroSkill()->IsRecoverable());
 TestFalse(TEXT("Casting is immune"),FEGCombatExecution::Damage(Enemy,U,20000));
 U->GetHeroSkill()->AnimationEvent(TEXT("Release"));U->GetHeroSkill()->AnimationEvent(TEXT("Finish"));
 TestTrue(TEXT("Ultimate does not reduce health already above the recovery floor"),FMath::IsNearlyEqual(U->GetAttributes()->GetHealth(),7000.f,.1f));
 TestEqual(TEXT("Ultimate recovery gives no attack buff"),U->GetAttributes()->GetAttackPower(),Attack);
 FEGCombatExecution::Damage(Enemy,U,20000);TestTrue(TEXT("Passive remains unused after burn"),U->GetHeroSkill()->IsRecoverable());
 E.Tick(2.1f);TestEqual(TEXT("Passive recovers fifty percent"),U->GetAttributes()->GetHealth(),5000.f);
 TestTrue(TEXT("Revival adds attack only"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackPower(),Attack*1.25f,.01f));
 U->GetHeroSkill()->ApplyPersistentLevel(6);
 TestTrue(TEXT("GE survives level base recalculation"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackPower(),U->GetDefinition()->EvaluateStats(6).AttackPower*1.25f,.01f));
 U->GetHeroSkill()->Battle->Stop();TestFalse(TEXT("Stop clears recoverability"),U->GetHeroSkill()->IsRecoverable());
 TestTrue(TEXT("Stop removes attack GE"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackPower(),U->GetDefinition()->EvaluateStats(6).AttackPower,.01f));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusLinks,"ElyGambit.FlameChasers7x7.MobiusLinks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusLinks::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Mobius"),TEXT("Mobius"),2);const auto AllyId=E.Deploy(TEXT("Pardofelis"),TEXT("Pardo"),3);
 auto* Third=E.GM->SpawnEnemy(E.GM->RunDefinition->Stages[0].Enemies[0].Definition,1,{6,6});
 if(!TestTrue(TEXT("Three enemy combat"),Id.IsValid()&&AllyId.IsValid()&&Third&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* Ally=E.Find(AllyId);auto Enemies=E.Enemies();auto* A=Enemies[0];auto* B=Enemies[1];auto* C=Enemies[2];
 A->SetActorLocation({0,0,88});B->SetActorLocation({300,0,88});C->SetActorLocation({1300,0,88});
 auto* Rules=U->GetHeroSkill()->Battle.Get();auto* Behavior=U->GetHeroSkill()->GetBehavior();
 Rules->BasicHit(U,A,FGuid::NewGuid());
 B->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),500);
 FEGCombatExecution::Damage(Ally,A,100);
 TestEqual(TEXT("Allied damage propagates actual loss without second defense"),B->GetAttributes()->GetHealth(),9980.f);
 TestEqual(TEXT("Outsider untouched"),C->GetAttributes()->GetHealth(),10000.f);
 A->GetHeroSkill()->SetShield(80);FEGCombatExecution::Damage(Ally,A,100);
 TestEqual(TEXT("Absorbed shield excluded from link base"),B->GetAttributes()->GetHealth(),9976.f);
 FEGCombatExecution::Damage(U,A,100,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid(),true);
 TestEqual(TEXT("Propagated damage never propagates"),B->GetAttributes()->GetHealth(),9976.f);
 Rules->BasicHit(U,C,FGuid::NewGuid());float Before=A->GetAttributes()->GetHealth();
 FEGCombatExecution::Damage(Ally,C,100);TestEqual(TEXT("Outsider basic replaces pair"),A->GetAttributes()->GetHealth(),Before);
 C->SetActorLocation({600,0,88});B->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
 if(!TestTrue(TEXT("Cast giant snake"),E.Cast(U)))return false;
 TestFalse(TEXT("Snake transformation untargetable"),U->GetHeroSkill()->IsTargetable());U->GetHeroSkill()->AnimationEvent(TEXT("Release"));U->GetHeroSkill()->AnimationEvent(TEXT("Finish"));
 TestTrue(TEXT("Link lifetime does not extend immunity"),U->GetHeroSkill()->IsTargetable());
 Before=C->GetAttributes()->GetHealth();Rules->BasicHit(U,A,FGuid::NewGuid());FEGCombatExecution::Damage(Ally,A,100);
 TestTrue(TEXT("Basic cannot shrink ultimate group"),FMath::IsNearlyEqual(C->GetAttributes()->GetHealth(),Before-20,.01f));
 const float BeforeExecutionA=A->GetAttributes()->GetHealth(),BeforeExecutionB=B->GetAttributes()->GetHealth();
 C->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 TestTrue(TEXT("Execution accepted on linked member"),FEGCombatExecution::Execute(Ally,C,FGuid::NewGuid()));
 TestTrue(TEXT("Execution receipt remains explicitly marked"),C->LastCombatResolution.bExecution);
 TestEqual(TEXT("Execution never propagates to first linked member"),A->GetAttributes()->GetHealth(),BeforeExecutionA);
 TestEqual(TEXT("Execution never propagates to second linked member"),B->GetAttributes()->GetHealth(),BeforeExecutionB);
 E.Tick(6.2f);Before=B->GetAttributes()->GetHealth();FEGCombatExecution::Damage(Ally,A,100);
 TestEqual(TEXT("Link expires"),B->GetAttributes()->GetHealth(),Before);
 Rules->BasicHit(U,A,FGuid::NewGuid());Before=B->GetAttributes()->GetHealth();FEGCombatExecution::StigmataDamage(Ally,A,100,FGuid::NewGuid());
 TestTrue(TEXT("Allied signet life loss also propagates exactly once"),FMath::IsNearlyEqual(Before-B->GetAttributes()->GetHealth(),20.f,.01f));
 Rules->BasicHit(U,A,FGuid::NewGuid());U->Die();Before=B->GetAttributes()->GetHealth();FEGCombatExecution::Damage(Ally,A,100);
 TestEqual(TEXT("Owner death clears link"),B->GetAttributes()->GetHealth(),Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFlameLifecycle,"ElyGambit.FlameChasers7x7.SummonsGroupsAndStop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFlameLifecycle::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Mobius"),TEXT("Mobius"),2);const auto HuaId=E.Deploy(TEXT("FuHua"),TEXT("Hua"),3);
 if(!TestTrue(TEXT("Enter combat"),Id.IsValid()&&HuaId.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* Hua=E.Find(HuaId);auto* Rules=U->GetHeroSkill()->Battle.Get();auto Enemies=E.Enemies();auto* A=Enemies[0];auto* B=Enemies[1];
 auto* CanDefinition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Summons/Can/Data/DA_Can"));
 auto* Can=Rules->SpawnSummon(CanDefinition,HuaId,1,E.Grid->CellToWorld({0,1})+FVector(0,0,88));
 if(!TestNotNull(TEXT("Existing independent summon"),Can))return false;Can->GetCombat()->SetComponentTickEnabled(false);
 // A second independently owned behavior exercises the global nonrecursive receipt guard.
 auto* Second=Rules->SpawnSummon(U->GetDefinition(),Id,1,E.Grid->CellToWorld({6,1})+FVector(0,0,88));
 if(!TestNotNull(TEXT("Second connection owner"),Second))return false;Second->GetCombat()->SetComponentTickEnabled(false);
 Rules->BasicHit(U,A,FGuid::NewGuid());Rules->BasicHit(Second,A,FGuid::NewGuid());
 FEGCombatExecution::Damage(Can,A,100);
 TestEqual(TEXT("Two owners each transmit once from allied summon"),B->GetAttributes()->GetHealth(),9960.f);
 TestEqual(TEXT("No link bounce returns to initial victim"),A->GetAttributes()->GetHealth(),9900.f);
 A->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10);
 FEGCombatExecution::Damage(Can,A,1000);
 TestEqual(TEXT("Only actual ten health loss propagates, excluding overkill"),B->GetAttributes()->GetHealth(),9956.f);
 FEGCombatExecution::Damage(B,Hua,20000);TestTrue(TEXT("Pending revival"),Hua->GetHeroSkill()->IsRecoverable());
 Rules->Stop();E.Tick(1.3f);
 TestFalse(TEXT("Stopping prevents queued revival"),IEGCombatant::Execute_IsAlive(Hua));
 TestFalse(TEXT("Stop drops recovery state"),Hua->GetHeroSkill()->IsRecoverable());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusLockedCast,"ElyGambit.FlameChasers7x7.LockedCastAndAvailability",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusLockedCast::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;bool Hua=false,Mobius=false;
 for(const auto& Card:E.GM->RunDefinition->CardPool)
 {
  TestTrue(TEXT("Collapsed Hua cannot appear in shop or opening pool"),Card->HeroDefinition->HeroKind!=EEGHeroKind::CollapsedHua);
  Hua|=Card->HeroDefinition->HeroKind==EEGHeroKind::Hua;Mobius|=Card->HeroDefinition->HeroKind==EEGHeroKind::Mobius;
 }
 TestTrue(TEXT("Moonblade Hua and Mobius remain available"),Hua&&Mobius);
 const auto Id=E.Deploy(TEXT("Mobius"),TEXT("Mobius"),2);
 if(!TestTrue(TEXT("Enter combat"),Id.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto Enemies=E.Enemies();auto* A=Enemies[0];auto* B=Enemies[1];
 U->SetActorLocation({300,-500,88});A->SetActorLocation({300,200,88});B->SetActorLocation({400,200,88});
 const FVector Locked=A->GetActorLocation();
 if(!TestTrue(TEXT("Begin ground-locked ultimate"),E.Cast(U)))return false;
 int32 Warnings=0;
 const auto* Data=U->GetDefinition()->SkillDefinition.Get();const auto* Cue=Data?Data->ChessFX.Find(TEXT("Telegraph")):nullptr;
 for(TActorIterator<AEGChessNiagaraBurst> It(E.W);It;++It)if(It->GetOwner()==U)
 {
  auto* Component=It->FindComponentByClass<UNiagaraComponent>();
  if(Cue&&Component&&Component->GetAsset()==Cue->System)
  {++Warnings;TestTrue(TEXT("Warning uses the confirmed target ground point"),It->GetActorLocation().Equals(Locked-FVector(0,0,88),.1f));}
 }
 TestEqual(TEXT("One clear warning"),Warnings,1);
 B->SetActorLocation(Locked+FVector(150,0,0));A->SetActorLocation({1600,1600,88});
 const float BeforeA=A->GetAttributes()->GetHealth(),BeforeB=B->GetAttributes()->GetHealth();
 U->GetHeroSkill()->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("Moving target escapes the locked strike"),A->GetAttributes()->GetHealth(),BeforeA);
 TestTrue(TEXT("Enemy still inside warning receives damage"),B->GetAttributes()->GetHealth()<BeforeB);
 U->GetHeroSkill()->CancelAction();
 for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)TestTrue(TEXT("Cancel removes both caster and target assemblies"),It->IsActorBeingDestroyed());
 TestTrue(TEXT("Cancel clears casting protection"),U->GetHeroSkill()->IsTargetable());
 U->GetHeroSkill()->Battle->BasicHit(U,B,FGuid::NewGuid());
 U->GetHeroSkill()->Battle->Stop();
 for(TActorIterator<AEGMobiusLinkVisual> It(E.W);It;++It)TestTrue(TEXT("Stop removes persistent link presentation"),It->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraExecution,"ElyGambit.FlameChasers7x7.SakuraExecution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraExecution::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Sakura"),TEXT("Sakura"),2);
 if(!Id.IsValid()||E.GM->RunDefinition->Stages.IsEmpty()||E.GM->RunDefinition->Stages[0].Enemies.IsEmpty())return false;
 auto* Board=E.GM->GetBoard();auto* EnemyDefinition=E.GM->RunDefinition->Stages[0].Enemies[0].Definition.Get();
 if(!Board||!EnemyDefinition)return false;
 for(int32 Y=0;Y<Board->Dimensions.Y&&E.Enemies().Num()<6;++Y)
  for(int32 X=0;X<Board->Dimensions.X&&E.Enemies().Num()<6;++X)
   if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))E.GM->SpawnEnemy(EnemyDefinition,1,{X,Y});
 // A genuine enemy Sakura must now accept execution without the removed evade.
 auto* SakuraCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"));
 AEGChessUnit* EnemySakura=nullptr;
 if(SakuraCard&&SakuraCard->HeroDefinition)
  for(int32 Y=0;Y<Board->Dimensions.Y&&!EnemySakura;++Y)
   for(int32 X=0;X<Board->Dimensions.X&&!EnemySakura;++X)
    if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))EnemySakura=E.GM->SpawnEnemy(SakuraCard->HeroDefinition,1,{X,Y});
 if(!TestTrue(TEXT("Sakura and seven enemies enter combat"),E.Enemies().Num()>=7&&EnemySakura&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto Enemies=E.Enemies();auto* Below=Enemies[0];auto* Equal=Enemies[1];auto* Above=Enemies[2];auto* Protected=Enemies[3];auto* Outside=Enemies[4];auto* Crossing=Enemies[5];
 auto* S=U->GetHeroSkill();auto* B=S->Battle.Get();
 const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Formal Sakura definition has valid 20-percent threshold"),D&&D->HasValidSkillRules()&&FMath::IsNearlyEqual(D->ExecuteHealthFraction,.2f)))return false;
 auto* ExecuteProbe=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Sakura/CombatPresentation20261030/NS_Sakura_Strike_Private.NS_Sakura_Strike_Private"));
 if(!TestNotNull(TEXT("Existing private Niagara system for an observable execution accent"),ExecuteProbe))return false;
 auto* MutableDefinition=const_cast<UEGSakuraSkillDefinition*>(D);
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardExecution(MutableDefinition->ExecutionImpactPresentation,TSoftObjectPtr<UEGSourceEffectDefinition>());
 TGuardValue<TMap<FName,FEGChessFXCue>> GuardCues(MutableDefinition->ChessFX,MutableDefinition->ChessFX);
 FEGChessFXCue ExecuteCue;ExecuteCue.System=ExecuteProbe;ExecuteCue.Duration=2.f;
 // Even accidental action/bone settings on a cue must not cut off or relocate the impact.
 ExecuteCue.bActionBound=true;ExecuteCue.bActionClock=true;ExecuteCue.bAttachToOwner=true;
 MutableDefinition->ChessFX.Add(TEXT("Execute"),ExecuteCue);
 auto Bursts=[&](){TArray<AEGChessNiagaraBurst*> Result;for(TActorIterator<AEGChessNiagaraBurst> It(E.W);It;++It)
  if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())if(const auto* FX=It->FindComponentByClass<UNiagaraComponent>();FX&&FX->GetAsset()==ExecuteProbe)Result.Add(*It);return Result;};
 U->SetActorRotation(FRotator::ZeroRotator);
 Below->SetActorLocation(U->GetActorLocation()+FVector(50,0,0));
 Equal->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));
 Above->SetActorLocation(U->GetActorLocation()+FVector(150,0,0));
 Protected->SetActorLocation(U->GetActorLocation()+FVector(200,0,0));
 Crossing->SetActorLocation(U->GetActorLocation()+FVector(250,0,0));
 EnemySakura->SetActorLocation(U->GetActorLocation()+FVector(275,0,0));
 Outside->SetActorLocation(U->GetActorLocation()+FVector(D->UltimateRadius+250,0,0));
 const float BelowBeforeDirectHit=Below->GetAttributes()->GetHealth();
 TestTrue(TEXT("Sakura accepts the first direct hit"),FEGCombatExecution::Damage(Below,U,100));
 TestTrue(TEXT("First direct hit reduces Sakura health"),U->GetAttributes()->GetHealth()<10000.f);
 TestTrue(TEXT("Sakura does not counter the close attacker"),FMath::IsNearlyEqual(Below->GetAttributes()->GetHealth(),BelowBeforeDirectHit,.01f));
 auto Health=[](AEGChessUnit* Target,float Value){Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Value);};
 Health(Below,1999.f);Health(Equal,2000.f);Health(Above,2001.f);Health(Protected,1000.f);Health(Outside,1000.f);Health(Crossing,2001.f);
 Health(EnemySakura,1000.f);
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),60.f);
 Below->GetHeroSkill()->SetShield(50000.f);Below->GetHeroSkill()->GrantPrismaticBubble(10.f,.3f,3.f);
 Below->GetHeroSkill()->ApplyProtection(FGuid::NewGuid(),Below,.5f,10.f);
 Below->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100000.f);
 Equal->GetHeroSkill()->SetShield(50000.f);Equal->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100000.f);
 Above->GetHeroSkill()->SetShield(50000.f);Protected->GetHeroSkill()->SetActionProtected(true);
 auto* P=E.W->SpawnActor<AEGSkillProjectile>();P->Battle=B;P->Round=B->GetRoundId();P->Source=Above;P->Target=U;P->Kind=EEGProjectileKind::BasicAttack;P->Start=Above->GetActorLocation();P->Destination=U->GetActorLocation();P->Duration=10;P->RawDamage=1;P->SetActorLocation(P->Start);
 if(!TestTrue(TEXT("Sakura ultimate starts"),E.Cast(U)))return false;
 TestTrue(TEXT("Casting still grants Sakura protection"),S->IsActionProtected());
 auto* Policy=Cast<UEGSakuraSkillBehavior>(S->GetBehavior());if(!TestNotNull(TEXT("Sakura owns its behavior"),Policy))return false;
 FEGSkillActionContext Replay;Replay.RoundId=B->GetRoundId();Replay.ActionId=FGuid::NewGuid();Replay.bUltimate=true;Replay.Attack=60.f;Replay.Multiplier=2.5f;
 Policy->ActionEvent(TEXT("Slash"),Replay);
 TestEqual(TEXT("A stale action cannot submit a slash"),Above->GetHeroSkill()->Shield,50000.f);
 const FVector ProjectileStart=P->GetActorLocation();E.Tick(.05f);
 TestFalse(TEXT("Enemy projectile advances during Sakura cast"),P->GetActorLocation().Equals(ProjectileStart,.01f));
 S->AnimationEvent(TEXT("Slash"));
 TestTrue(TEXT("Marked targets remain alive before the delayed execution"),IEGCombatant::Execute_IsAlive(Below)&&IEGCombatant::Execute_IsAlive(Equal));
 S->GetBehavior()->TickBattle(.4f);
 TestEqual(TEXT("Only three actually executed targets create confirmed execution accents"),Bursts().Num(),3);
 TestFalse(TEXT("Enemy Sakura does not evade the execution"),IEGCombatant::Execute_IsAlive(EnemySakura));
 TestTrue(TEXT("Enemy Sakura records an execution receipt"),EnemySakura->LastCombatResolution.bExecution);
 for(const auto* Burst:Bursts())TestTrue(TEXT("Execution accents stay at confirmed target points"),
  Burst->GetActorLocation().Equals(Below->GetActorLocation(),.1f)||Burst->GetActorLocation().Equals(Equal->GetActorLocation(),.1f)||Burst->GetActorLocation().Equals(EnemySakura->GetActorLocation(),.1f));
 TestFalse(TEXT("Below 20 percent is executed through protection"),IEGCombatant::Execute_IsAlive(Below));
 TestFalse(TEXT("Exactly 20 percent is executed through shield and defense"),IEGCombatant::Execute_IsAlive(Equal));
 TestTrue(TEXT("Below-threshold receipt is execution"),Below->LastCombatResolution.bExecution);
 TestEqual(TEXT("Execution does not count shield as absorbed"),Below->LastCombatResolution.ShieldAbsorbed,0.f);
 TestEqual(TEXT("Execution does not consume bubble as a hit"),Below->LastCombatResolution.BubbleAbsorbed,0.f);
 TestEqual(TEXT("Execution does not count protection as prevented"),Below->LastCombatResolution.ProtectionPrevented,0.f);
 TestEqual(TEXT("Above threshold receives ordinary shielded damage"),Above->GetAttributes()->GetHealth(),2001.f);
 TestTrue(TEXT("Ordinary slash consumes ordinary shield"),Above->GetHeroSkill()->Shield<50000.f);
 TestFalse(TEXT("Above-threshold hit is not execution"),Above->LastCombatResolution.bExecution);
 TestTrue(TEXT("Ordinary slash can cross threshold without executing"),IEGCombatant::Execute_IsAlive(Crossing)&&Crossing->GetAttributes()->GetHealth()<2000.f);
 TestFalse(TEXT("Threshold crossed by this hit does not retroactively execute"),Crossing->LastCombatResolution.bExecution);
 TestEqual(TEXT("Action-protected enemy is untouched"),Protected->GetAttributes()->GetHealth(),1000.f);
 TestEqual(TEXT("Enemy outside range is untouched"),Outside->GetAttributes()->GetHealth(),1000.f);
 const float ShieldAfterSlash=Above->GetHeroSkill()->Shield;
 Replay.ActionId=S->ActionId;Policy->ActionEvent(TEXT("Slash"),Replay);
 S->AnimationEvent(TEXT("Slash"));S->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("Behavior replay, repeated Slash and legacy Release never damage twice"),Above->GetHeroSkill()->Shield,ShieldAfterSlash);
 TestEqual(TEXT("Replayed notifies do not duplicate execution accents"),Bursts().Num(),3);
 S->CancelAction();TestFalse(TEXT("Cancellation removes casting protection"),S->IsActionProtected());
 TestEqual(TEXT("Confirmed target accents survive the immediate action end"),Bursts().Num(),3);
 if(!TestTrue(TEXT("Next cast starts"),E.Cast(U)))return false;
 S->CancelAction();S->AnimationEvent(TEXT("Slash"));
 TestEqual(TEXT("Slash after cancellation is ignored"),Above->GetHeroSkill()->Shield,ShieldAfterSlash);
 TestEqual(TEXT("Cancel before Slash cannot create execution accents"),Bursts().Num(),3);
 FEGChessFXCue OrdinaryCue=ExecuteCue;OrdinaryCue.bActionBound=false;OrdinaryCue.bActionClock=false;OrdinaryCue.bAttachToOwner=false;
 TWeakObjectPtr<AEGChessNiagaraBurst> Ordinary=AEGChessNiagaraBurst::SpawnCue(S,B,OrdinaryCue,U->GetActorLocation());
 TestTrue(TEXT("Ordinary comparison burst starts"),Ordinary.IsValid());
 B->Stop();TestFalse(TEXT("Battle stop clears action"),S->IsActionRunning());
 E.Tick(.05f);
 TestTrue(TEXT("The default burst still ends when its round stops"),!Ordinary.IsValid()||Ordinary->IsActorBeingDestroyed());
 TestEqual(TEXT("Confirmed execution accents survive the immediate round transition"),Bursts().Num(),3);
 E.Tick(.55f);TestEqual(TEXT("Confirmed execution accents expire within their short cap"),Bursts().Num(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraExecutionAccentDeath,"ElyGambit.FlameChasers7x7.SakuraExecutionAccentDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraExecutionAccentDeath::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Sakura"),TEXT("Sakura"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;
 E.Freeze();auto* U=E.Find(Id);auto Targets=E.Enemies();
 if(!TestNotNull(TEXT("Sakura caster"),U)||!TestTrue(TEXT("Enemy fixture"),!Targets.IsEmpty()))return false;
 auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Sakura/CombatPresentation20261030/NS_Sakura_Strike_Private.NS_Sakura_Strike_Private"));
 if(!TestNotNull(TEXT("Sakura skill"),D)||!TestNotNull(TEXT("Execution accent probe"),System))return false;
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardExecution(D->ExecutionImpactPresentation,TSoftObjectPtr<UEGSourceEffectDefinition>());
 TGuardValue<TMap<FName,FEGChessFXCue>> GuardCues(D->ChessFX,D->ChessFX);
 FEGChessFXCue Cue;Cue.System=System;Cue.Duration=2.f;D->ChessFX.Add(TEXT("Execute"),Cue);
 U->SetActorRotation(FRotator::ZeroRotator);
 for(auto* Target:Targets)Target->SetActorLocation(U->GetActorLocation()+FVector(2000,0,0));
 Targets[0]->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));
 Targets[0]->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 if(!TestTrue(TEXT("Begin Sakura ultimate"),E.Cast(U)))return false;
 U->GetHeroSkill()->AnimationEvent(TEXT("Slash"));
 U->GetHeroSkill()->GetBehavior()->TickBattle(.4f);
 TWeakObjectPtr<AEGChessNiagaraBurst> Burst;
 for(TActorIterator<AEGChessNiagaraBurst> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())Burst=*It;
 if(!TestTrue(TEXT("Confirmed execution creates an accent"),Burst.IsValid()))return false;
 U->Die();E.Tick(.05f);
 TestTrue(TEXT("Caster death cleans the execution accent"),!Burst.IsValid()||Burst->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraExecutionFinalAccent,"ElyGambit.FlameChasers7x7.SakuraExecutionFinalAccent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraExecutionFinalAccent::RunTest(const FString&)
{
 // Non-round-shop fixture keeps the winning unit alive during Resolution.
 FFlameWorld E(*this,false);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Sakura"),TEXT("Sakura"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;
 E.Freeze();auto* U=E.Find(Id);auto Targets=E.Enemies();
 if(!TestNotNull(TEXT("Sakura caster"),U)||!TestTrue(TEXT("At least one enemy"),!Targets.IsEmpty()))return false;
 auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Sakura/CombatPresentation20261030/NS_Sakura_Strike_Private.NS_Sakura_Strike_Private"));
 if(!TestNotNull(TEXT("Sakura skill"),D)||!TestNotNull(TEXT("Execution accent probe"),System))return false;
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardExecution(D->ExecutionImpactPresentation,TSoftObjectPtr<UEGSourceEffectDefinition>());
 TGuardValue<TMap<FName,FEGChessFXCue>> GuardCues(D->ChessFX,D->ChessFX);
 FEGChessFXCue Cue;Cue.System=System;Cue.Duration=2.f;D->ChessFX.Add(TEXT("Execute"),Cue);
 U->SetActorRotation(FRotator::ZeroRotator);
 for(int32 I=1;I<Targets.Num();++I)Targets[I]->Die();
 Targets[0]->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));
 Targets[0]->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 if(!TestTrue(TEXT("Begin last-target execution"),E.Cast(U)))return false;
 auto* Battle=U->GetHeroSkill()->Battle.Get();U->GetHeroSkill()->AnimationEvent(TEXT("Slash"));
 U->GetHeroSkill()->GetBehavior()->TickBattle(.4f);
 TestFalse(TEXT("Final target is executed"),IEGCombatant::Execute_IsAlive(Targets[0]));
 TestTrue(TEXT("Battle resolution is deferred from the damage callback"),Battle&&Battle->IsBattleActive());
 TWeakObjectPtr<AEGChessNiagaraBurst> Burst;
 for(TActorIterator<AEGChessNiagaraBurst> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())Burst=*It;
 if(!TestTrue(TEXT("Final execution creates an accent before victory"),Burst.IsValid()))return false;
 TestTrue(TEXT("Victory resolves immediately when requested"),E.GM->ResolveCombat(true));
 TestFalse(TEXT("Victory stops the battle without delay"),Battle->IsBattleActive());
 E.Tick(.05f);TestTrue(TEXT("Execution accent remains visible briefly after victory"),Burst.IsValid()&&!Burst->IsActorBeingDestroyed());
 E.Tick(.55f);TestTrue(TEXT("Execution accent expires after victory"),!Burst.IsValid()||Burst->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraSector,"ElyGambit.FlameChasers7x7.SakuraSector",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraSector::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Sakura"),TEXT("Sakura"),2);
 if(!Id.IsValid()||E.GM->RunDefinition->Stages.IsEmpty()||E.GM->RunDefinition->Stages[0].Enemies.IsEmpty())return false;
 auto* Board=E.GM->GetBoard();auto* EnemyDefinition=E.GM->RunDefinition->Stages[0].Enemies[0].Definition.Get();
 if(!Board||!EnemyDefinition)return false;
 for(int32 Y=0;Y<Board->Dimensions.Y&&E.Enemies().Num()<7;++Y)
  for(int32 X=0;X<Board->Dimensions.X&&E.Enemies().Num()<7;++X)
   if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))E.GM->SpawnEnemy(EnemyDefinition,1,{X,Y});
 if(!TestTrue(TEXT("Sector fixture has seven enemies"),E.Enemies().Num()>=7&&E.GM->EnterCombat()))return false;
 E.Freeze();auto* U=E.Find(Id);auto* Skill=U->GetHeroSkill();const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Formal Sakura radius is three metres"),D&&FMath::IsNearlyEqual(D->UltimateRadius,300.f)))return false;
 auto Enemies=E.Enemies();const FVector Origin=U->GetActorLocation();U->SetActorRotation(FRotator::ZeroRotator);
 auto Point=[](double Radius,double Degrees){const double A=FMath::DegreesToRadians(Degrees);return FVector(Radius*FMath::Cos(A),Radius*FMath::Sin(A),0);};
 const FVector Locations[]={Point(100,0),Point(299,29.9),Point(250,-30),Point(200,30.1),Point(100,180),Point(301,0),Point(300,0)};
 for(int32 I=0;I<7;++I)Enemies[I]->SetActorLocation(Origin+Locations[I]);
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 if(!TestTrue(TEXT("Sector cast starts"),E.Cast(U)))return false;
 // Simulate the authored montage's early FanStart event; Slash remains separate.
 const int32 CommitsBeforeFan=Skill->CommitCount;
 Skill->AnimationEvent(TEXT("FanStart"));
 TestEqual(TEXT("Sector visual is not a gameplay commit"),Skill->CommitCount,CommitsBeforeFan);
 AEGSourceEffect* Area=nullptr;
 for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(It->GetOwner()==U&&It->GetSourceDefinition()==D->AreaPresentation.Get())Area=*It;
 if(!TestNotNull(TEXT("Sector visual starts on FanStart"),Area))return false;
 TestTrue(TEXT("Visual starts at the marked sector origin"),Area->GetActorLocation().Equals(Origin-FVector(0,0,85),.1f));
 TestTrue(TEXT("Visual faces the casting direction"),Area->GetActorForwardVector().Equals(FVector::ForwardVector,.001f));
 TestEqual(TEXT("Starting the visual does not submit damage"),Enemies[0]->GetAttributes()->GetHealth(),10000.f);
 U->SetActorLocation(Origin+FVector(1000,1000,0));U->SetActorRotation(FRotator(0,180,0));
 TestTrue(TEXT("Visual remains at the locked position"),Area->GetActorLocation().Equals(Origin-FVector(0,0,85),.1f));
 Skill->AnimationEvent(TEXT("Slash"));
 TestEqual(TEXT("Marking a target does not apply slash damage early"),Enemies[0]->GetAttributes()->GetHealth(),10000.f);
 Skill->GetBehavior()->TickBattle(.4f);
 auto Damaged=[&](int32 I){return Enemies[I]->GetAttributes()->GetHealth()<10000.f;};
 TestTrue(TEXT("Front target is hit"),Damaged(0));
 TestTrue(TEXT("Inside positive angular edge is hit"),Damaged(1));
 TestTrue(TEXT("Negative 30 degree edge is inclusive"),Damaged(2));
 TestFalse(TEXT("Beyond positive angular edge is excluded"),Damaged(3));
 TestFalse(TEXT("Behind the caster is excluded"),Damaged(4));
 TestFalse(TEXT("Beyond 300 cm is excluded"),Damaged(5));
 TestTrue(TEXT("Exactly 300 cm is inclusive"),Damaged(6));
 TWeakObjectPtr<AEGSourceEffect> WeakArea=Area;Skill->CancelAction();
 TestTrue(TEXT("Cancelling the action removes the sector visual"),!WeakArea.IsValid()||WeakArea->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraThresholdValidation,"ElyGambit.FlameChasers7x7.SakuraThresholdValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraThresholdValidation::RunTest(const FString&)
{
 auto* D=NewObject<UEGSakuraSkillDefinition>();
 TestTrue(TEXT("Default 20-percent execution rule is valid"),D->HasValidSkillRules());
 D->ExecuteHealthFraction=0.f;TestFalse(TEXT("Zero threshold is invalid"),D->HasValidSkillRules());
 D->ExecuteHealthFraction=1.01f;TestFalse(TEXT("Threshold above one is invalid"),D->HasValidSkillRules());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraExecutionRevival,"ElyGambit.FlameChasers7x7.SakuraExecutionRespectsRevival",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraExecutionRevival::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto SakuraId=E.Deploy(TEXT("Sakura"),TEXT("Sakura"),2);
 auto* HuaCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"));
 if(!SakuraId.IsValid()||!TestNotNull(TEXT("Hua revival fixture card"),HuaCard)||!HuaCard->HeroDefinition)return false;
 auto* Board=E.GM->GetBoard();AEGChessUnit* Hua=nullptr;
 for(int32 Y=0;Y<Board->Dimensions.Y&&!Hua;++Y)
  for(int32 X=0;X<Board->Dimensions.X&&!Hua;++X)
   if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))Hua=E.GM->SpawnEnemy(HuaCard->HeroDefinition,1,{X,Y});
 if(!TestTrue(TEXT("Enemy Hua enters real combat"),Hua&&E.GM->EnterCombat()))return false;
 E.Freeze();auto* Sakura=E.Find(SakuraId);auto* Skill=Sakura->GetHeroSkill();
 for(auto* Enemy:E.Enemies())Enemy->SetActorLocation(Sakura->GetActorLocation()+FVector(15000,0,0));
 Sakura->SetActorRotation(FRotator::ZeroRotator);
 Hua->SetActorLocation(Sakura->GetActorLocation()+FVector(200,0,0));
 Hua->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 Hua->GetHeroSkill()->SetShield(50000.f);Hua->GetHeroSkill()->GrantPrismaticBubble(10.f,.3f,3.f);
 if(!TestTrue(TEXT("Sakura can cast against recoverable target"),E.Cast(Sakura)))return false;
 Skill->AnimationEvent(TEXT("Slash"));
 Skill->GetBehavior()->TickBattle(.4f);
 TestTrue(TEXT("Execution triggers Hua's existing revival path"),Hua->GetHeroSkill()->IsRecoverable());
 TestTrue(TEXT("Lethal hit uses execution receipt"),Hua->LastCombatResolution.bExecution);
 TestFalse(TEXT("Pending revival is not reported as permanent death"),Hua->LastCombatResolution.bFatal);
 TestEqual(TEXT("Execution bypasses shield absorption"),Hua->LastCombatResolution.ShieldAbsorbed,0.f);
 TestEqual(TEXT("Execution bypasses bubble absorption"),Hua->LastCombatResolution.BubbleAbsorbed,0.f);
 E.Tick(1.2f);
 TestTrue(TEXT("Revived Hua becomes alive and targetable"),IEGCombatant::Execute_IsAlive(Hua)&&Hua->GetHeroSkill()->IsTargetable());
 TestEqual(TEXT("Hua revives with existing forty-percent health"),Hua->GetAttributes()->GetHealth(),4000.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraExecutionNoEvade,"ElyGambit.FlameChasers7x7.SakuraExecutionNoEvade",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraExecutionNoEvade::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto AttackerId=E.Deploy(TEXT("Pardofelis"),TEXT("Pardo"),2);
 auto* SakuraCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"));
 if(!AttackerId.IsValid()||!TestNotNull(TEXT("Sakura fixture card"),SakuraCard)||!SakuraCard->HeroDefinition)return false;
 auto* Board=E.GM->GetBoard();if(!TestNotNull(TEXT("Fixture combat board"),Board))return false;
 AEGChessUnit* Target=nullptr;
 for(int32 Y=0;Y<Board->Dimensions.Y&&!Target;++Y)
  for(int32 X=0;X<Board->Dimensions.X&&!Target;++X)
   if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))Target=E.GM->SpawnEnemy(SakuraCard->HeroDefinition,1,{X,Y});
 if(!TestTrue(TEXT("Enemy Sakura enters combat"),Target&&E.GM->EnterCombat()))return false;
 E.Freeze();auto* Attacker=E.Find(AttackerId);if(!TestNotNull(TEXT("Allied attacker"),Attacker))return false;
 if(!TestNotNull(TEXT("Enemy owns Sakura's combat behavior"),Cast<UEGSakuraSkillBehavior>(Target->GetHeroSkill()->GetBehavior())))return false;
 Target->SetActorLocation(Attacker->GetActorLocation()+FVector(50,0,0));
 TestTrue(TEXT("The attacker is close enough for the removed counter"),Target->GetCombat()->IsInAttackRange(Attacker));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 const float AttackerHealth=Attacker->GetAttributes()->GetHealth();
 TestTrue(TEXT("First direct attack against Sakura is accepted"),FEGCombatExecution::Damage(Attacker,Target,25.f));
 TestTrue(TEXT("First direct attack reduces Sakura health"),Target->GetAttributes()->GetHealth()<1000.f);
 TestTrue(TEXT("Sakura does not counter a nearby attacker"),FMath::IsNearlyEqual(Attacker->GetAttributes()->GetHealth(),AttackerHealth,.01f));
 Target->GetHeroSkill()->SetActionProtected(true);
 TestFalse(TEXT("Ultimate protection still blocks direct damage"),FEGCombatExecution::Damage(Attacker,Target,25.f));
 TestFalse(TEXT("Ultimate protection still blocks execution"),FEGCombatExecution::Execute(Attacker,Target,FGuid::NewGuid()));
 Target->GetHeroSkill()->SetActionProtected(false);
 Target->GetHeroSkill()->SetShield(50000.f);
 TestTrue(TEXT("First execution against Sakura is accepted"),FEGCombatExecution::Execute(Attacker,Target,FGuid::NewGuid()));
 TestFalse(TEXT("Execution remains lethal through shield"),IEGCombatant::Execute_IsAlive(Target));
 TestTrue(TEXT("Accepted hit is an execution receipt"),Target->LastCombatResolution.bExecution);
 TestEqual(TEXT("Execution receipt reports no shield absorption"),Target->LastCombatResolution.ShieldAbsorbed,0.f);
 TestTrue(TEXT("Sakura never counterattacks the source"),FMath::IsNearlyEqual(Attacker->GetAttributes()->GetHealth(),AttackerHealth,.01f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraExecutionLinkedOrder,"ElyGambit.FlameChasers7x7.SakuraExecutionLinkedTargetOrder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraExecutionLinkedOrder::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto SakuraId=E.Deploy(TEXT("Sakura"),TEXT("Sakura"),2);const auto MobiusId=E.Deploy(TEXT("Mobius"),TEXT("Mobius"),3);
 if(!TestTrue(TEXT("Both heroes enter combat"),SakuraId.IsValid()&&MobiusId.IsValid()&&E.GM->EnterCombat()))return false;
 E.Freeze();auto* Sakura=E.Find(SakuraId);auto* Mobius=E.Find(MobiusId);
 if(!TestNotNull(TEXT("Sakura combat unit"),Sakura)||!TestNotNull(TEXT("Mobius combat unit"),Mobius))return false;
 auto* Skill=Sakura->GetHeroSkill();auto* Rules=Skill->Battle.Get();
 if(!TestNotNull(TEXT("Active battle"),Rules))return false;
 const auto Snapshot=Rules->Units();TArray<AEGChessUnit*> Enemies;
 for(auto* Unit:Snapshot)if(Sakura->GetCombat()->IsValidEnemy(Unit))Enemies.Add(Unit);
 if(!TestTrue(TEXT("Actual battle snapshot contains at least two enemies"),Enemies.Num()>=2))return false;
 auto* First=Enemies[0];auto* Second=Enemies[1];
 TestTrue(TEXT("Normal-damage target precedes execution candidate in actual snapshot"),Snapshot.IndexOfByKey(First)<Snapshot.IndexOfByKey(Second));
 for(auto* Enemy:Enemies)Enemy->SetActorLocation(Sakura->GetActorLocation()+FVector(15000,0,0));
 First->SetActorLocation(Sakura->GetActorLocation()+FVector(100,0,0));
 Second->SetActorLocation(Sakura->GetActorLocation()+FVector(200,0,0));
 Sakura->SetActorRotation(FRotator::ZeroRotator);
 auto* SakuraDefinition=Cast<UEGSakuraSkillDefinition>(Sakura->GetDefinition()->SkillDefinition);
 auto* MobiusDefinition=Cast<UEGMobiusSkillDefinition>(Mobius->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Formal skill rules support this fixture"),SakuraDefinition&&SakuraDefinition->HasValidSkillRules()&&MobiusDefinition&&MobiusDefinition->LinkDamageRatio>0.f&&SakuraDefinition->UltimateRadius>200.f))return false;
 Rules->BasicHit(Mobius,First,FGuid::NewGuid());
 // Verify that the selected pair is actually linked before setting the boundary fixture.
 Second->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
 if(!TestTrue(TEXT("Ordinary allied damage is accepted"),FEGCombatExecution::Damage(Sakura,First,100.f)))return false;
 const float Ratio=FMath::Clamp(MobiusDefinition->LinkDamageRatio,0.f,1.f);
 TestTrue(TEXT("Ordinary allied damage propagates to the second target"),FMath::IsNearlyEqual(Second->GetAttributes()->GetHealth(),5000.f-100.f*Ratio,.01f));
 TestTrue(TEXT("Propagation records a linked receipt"),Second->LastCombatResolution.bLinkedDamage);
 First->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),8000.f);
 const float Threshold=Second->GetAttributes()->GetMaxHealth()*SakuraDefinition->ExecuteHealthFraction;
 const float SecondBefore=Threshold+.25f;
 Second->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),SecondBefore);
 Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 const int32 Tier=FMath::Clamp((Skill->GetLevel()-1)/FMath::Max(1,Sakura->GetDefinition()->GetSkillTierStep()),0,2);
 const float OrdinaryHit=Sakura->GetAttributes()->GetAttackPower()*Sakura->GetDefinition()->GetSkillMultipliers()[Tier]*Sakura->GetUltimateBonus();
 if(!TestTrue(TEXT("First hit crosses the second target's threshold without killing the first"),FMath::IsFinite(OrdinaryHit)&&OrdinaryHit>0.f&&OrdinaryHit<8000.f&&OrdinaryHit*Ratio>.25f))return false;
 if(!TestTrue(TEXT("Sakura casts the area slash"),E.Cast(Sakura)))return false;
 const auto CastSnapshot=Rules->Units();
 if(!TestTrue(TEXT("The actual slash snapshot retains the fixture's target order"),CastSnapshot.IndexOfByKey(First)!=INDEX_NONE&&CastSnapshot.IndexOfByKey(Second)!=INDEX_NONE&&CastSnapshot.IndexOfByKey(First)<CastSnapshot.IndexOfByKey(Second)))return false;
 Skill->AnimationEvent(TEXT("Slash"));
 Skill->GetBehavior()->TickBattle(.4f);
 TestTrue(TEXT("First target received ordinary area damage"),FMath::IsNearlyEqual(First->GetAttributes()->GetHealth(),8000.f-OrdinaryHit,.01f));
 TestFalse(TEXT("First target's receipt is not execution"),First->LastCombatResolution.bExecution);
 TestFalse(TEXT("Linked damage moved the later target under threshold before its hit"),IEGCombatant::Execute_IsAlive(Second));
 TestTrue(TEXT("Later target is executed using its current health"),Second->LastCombatResolution.bExecution);
 TestTrue(TEXT("Execution uses health remaining after linked damage"),FMath::IsNearlyEqual(Second->LastCombatResolution.RawAmount,SecondBefore-OrdinaryHit*Ratio,.05f));
 TestTrue(TEXT("Execution does not propagate back to the first target"),FMath::IsNearlyEqual(First->GetAttributes()->GetHealth(),8000.f-OrdinaryHit,.01f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVSwitch,"ElyGambit.FlameChasers7x7.VillVSwitch",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVSwitch::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Vill-V enters combat"),Id.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* B=S->Battle.Get();auto* V=Cast<UEGVillVSkillBehavior>(S->GetBehavior());auto* Enemy=E.Enemies()[0];
 // Keep unrelated fixture enemies out of the controlled target positions.
 for(auto* T:E.Enemies())T->SetActorLocation(FVector(15000,15000,88));
 auto* Mechanism=U->FindComponentByClass<UEGVillVMechanismComponent>();
 if(!TestNotNull(TEXT("Pilot owns device coordinator"),Mechanism))return false;
 auto WaitOperating=[&](int32 Mode){for(int32 N=0;N<240;++N){E.Tick(.05f);if(Mechanism->IsOperating()&&Mechanism->Mode()==Mode&&!S->IsActionRunning())return true;}return false;};
 TestFalse(TEXT("Pilot is never an additional damage target"),S->IsTargetable());
 if(!TestTrue(TEXT("Natural generation walk and Enter notify dock at gatling"),WaitOperating(0)))return false;
 TestTrue(TEXT("Starts in gatling"),V&&!V->IsCannon());
 // Pick an on-board target point in range of both seats and outside their
 // one-cell bases. No range or damage attribute is modified by this fixture.
 FVector SafeTarget=FVector::ZeroVector;bool bFoundSafeTarget=false;
 const float EnemyRadius=UEGCombatComponent::BlockingRadius(Enemy);
 const float Range=U->GetCombat()->GetEffectiveAttackRange(Enemy);
 for(int32 Y=0;Y<E.Grid->Rows&&!bFoundSafeTarget;++Y)for(int32 X=0;X<E.Grid->Columns&&!bFoundSafeTarget;++X)
 for(int32 DY=-1;DY<=1&&!bFoundSafeTarget;++DY)for(int32 DX=-1;DX<=1&&!bFoundSafeTarget;++DX)
 {
  FVector Point=E.Grid->CellToWorld({X,Y})+E.Grid->GetActorTransform().TransformVector(
   FVector(DX*E.Grid->CellWidth*.25f,DY*E.Grid->CellDepth*.25f,0));Point.Z=Enemy->GetActorLocation().Z;
  if(!Enemy->GetCombat()->IsBoardLocationFree(Point))continue;
  bool bClearBoth=true;
  for(int32 Index=0;Index<2;++Index)
  {
   auto* Device=Mechanism->GetDevice(Index);if(!Device){bClearBoth=false;break;}
   const float Reach=UEGCombatComponent::BlockingRadius(Device);
   const float Distance=FVector::Dist2D(Point,Device->GetActorLocation());
   if(Distance<=Reach+EnemyRadius+10.f||Distance>Range-.1f){bClearBoth=false;break;}
  }
  if(bClearBoth){SafeTarget=Point;bFoundSafeTarget=true;}
 }
 if(!TestTrue(TEXT("On-board enemy point fits both weapon ranges and single-cell bases"),bFoundSafeTarget))return false;
 Enemy->SetActorLocation(SafeTarget,false);
 if(!TestTrue(TEXT("Safe target is in the original gatling range"),U->GetCombat()->IsInAttackRange(Enemy)))return false;
 E.Tick(.8f);const int32 Count=B->GetAttackCount();const float HP=Enemy->GetAttributes()->GetHealth();
 TestTrue(TEXT("Gatling starts"),S->BeginAction(false));
 E.Tick(1.4f);
 TestTrue(TEXT("Three pellets total one attack"),FMath::IsNearlyEqual(HP-Enemy->GetAttributes()->GetHealth(),U->GetAttributes()->GetAttackPower(),.02f));
 TestEqual(TEXT("Burst counts once"),B->GetAttackCount(),Count+1);
 const float H=Enemy->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Fire1"));
 TestTrue(TEXT("Switch ultimate starts"),E.Cast(U));
 TestFalse(TEXT("Leave does not immediately switch weapon"),V->IsCannon());
 if(!TestTrue(TEXT("Natural leave jump enter fire completes"),WaitOperating(1)))return false;
 TestTrue(TEXT("Switch commits cannon at Dock"),V->IsCannon());
 TestTrue(TEXT("Enhanced cannon damages target"),Enemy->GetAttributes()->GetHealth()<H-U->GetAttributes()->GetAttackPower()*2);
 TestFalse(TEXT("Enhancement consumed on hit"),V->HasEnhancedShot());
 for(auto* T:E.Enemies())T->SetActorLocation(U->GetActorLocation()+FVector(15000,0,0));
 TestTrue(TEXT("Return ultimate starts without target"),E.Cast(U));
 if(!TestTrue(TEXT("Return jumps and docks at gatling"),WaitOperating(0)))return false;
 TestTrue(TEXT("No target retains enhancement"),V->HasEnhancedShot());
 Enemy->SetActorLocation(SafeTarget,false);E.Tick(.8f);const float H2=Enemy->GetAttributes()->GetHealth();
 TestTrue(TEXT("Restored target remains inside the original gatling range"),U->GetCombat()->IsInAttackRange(Enemy));
 TestTrue(TEXT("Next gatling starts"),S->BeginAction(false));E.Tick(1.4f);
 TestTrue(TEXT("Enhancement covers every pellet"),FMath::IsNearlyEqual(H2-Enemy->GetAttributes()->GetHealth(),U->GetAttributes()->GetAttackPower()*U->GetDefinition()->GetSkillMultipliers().X,.03f));
 E.Tick(.1f);
 int32 Idle=0;for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)if(It->GetOwner()==U)++Idle;
 TestEqual(TEXT("Exactly one seated idle assembly"),Idle,1);
 B->Stop();Idle=0;for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)if(It->GetOwner()==U)++Idle;
 TestEqual(TEXT("Battle end destroys pilot assembly"),Idle,0);
 TestNull(TEXT("Battle end clears device actor reference"),Mechanism->GetDevice(0));return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVPreview,"ElyGambit.FlameChasers7x7.VillVPreviewLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVPreview::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
 if(!TestNotNull(TEXT("Vill-V card loads"),Card))return false;
 auto* Preview=E.W->SpawnActor<AEGPlacementPreview>(Card->PreviewClass?Card->PreviewClass.Get():AEGPlacementPreview::StaticClass());
 if(!TestNotNull(TEXT("Production preview class or native fallback spawns"),Preview))return false;
 Preview->ApplyHeroDefinition(Card->HeroDefinition);
 TWeakObjectPtr<AEGCombatVisual> Device;
 for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)if(It->GetOwner()==Preview)Device=*It;
 TestFalse(TEXT("Pilot preview contains no duplicate device assembly"),Device.IsValid());
 Preview->SetActorHiddenInGame(false);Preview->HidePreview();Preview->Destroy();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVLayout,"ElyGambit.FlameChasers7x7.VillVLayoutTransactions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVLayout::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Deploy hero"),Id.IsValid()))return false;
 TestEqual(TEXT("Three separately placed parts"),E.Cards->GetDeploymentParts().Num(),3);
 TestEqual(TEXT("One population"),E.Cards->GetPieces().Num(),1);TestEqual(TEXT("One roster entry"),E.GM->GetRoster()->GetHeroes().Num(),1);
 const auto Parts=E.Cards->GetDeploymentParts();
 AEGDeployedPiece* Gun=nullptr;AEGDeployedPiece* Cannon=nullptr;
 for(auto* P:Parts){if(P->PartRole==EEGDeploymentPart::Gatling)Gun=P;if(P->PartRole==EEGDeploymentPart::Cannon)Cannon=P;}
 if(!TestNotNull(TEXT("Gatling exists"),Gun)||!TestNotNull(TEXT("Cannon exists"),Cannon))return false;
 TestTrue(TEXT("Person drags independently"),E.Cards->TryMovePlacedPiece(E.Cards->GetPieces()[0],{6,0}));
 TestTrue(TEXT("Gatling drags independently"),E.Cards->TryMovePlacedPiece(Gun,{0,0}));
 TestTrue(TEXT("Cannon drags independently"),E.Cards->TryMovePlacedPiece(Cannon,{4,0}));
 TestFalse(TEXT("Parts may not overlap"),E.Cards->TryMovePlacedPiece(Gun,Cannon->Cell));
 TestFalse(TEXT("Parts may not enter neutral row"),E.Cards->TryMovePlacedPiece(Gun,{0,3}));
 FEGHeroInstance H;E.GM->GetRoster()->GetHero(Id,H);
 TestEqual(TEXT("Person layout saved"),H.DeploymentCell,FIntPoint(6,0));TestEqual(TEXT("Gun layout saved"),H.GatlingCell,FIntPoint(0,0));TestEqual(TEXT("Cannon layout saved"),H.CannonCell,FIntPoint(4,0));
 auto* Card=Gun->CardDefinition.Get();E.Cards->GrantCard(Card);
 TestTrue(TEXT("Merge over device affects owning hero"),E.Cards->TryDeployHandCard(E.Cards->GetHand().Last().InstanceId,Gun->Cell));
 E.GM->GetRoster()->GetHero(Id,H);TestEqual(TEXT("Merge increments owning hero"),H.SameNameCards,2);TestEqual(TEXT("Merge does not duplicate parts"),E.Cards->GetDeploymentParts().Num(),3);
 const auto Saved=E.GM->GetRoster()->GetHeroes();E.GM->GetRoster()->RestoreSnapshot(Saved);E.Cards->RefreshHeroPresentations();
 TestEqual(TEXT("Refresh preserves three existing parts"),E.Cards->GetDeploymentParts().Num(),3);
 const int64 Gold=E.Cards->GetGold(),Value=E.Cards->GetSaleValue(Cannon->InstanceId);
 TestTrue(TEXT("Sale via cannon succeeds"),E.Cards->TrySellPlacedPiece(Cannon));TestEqual(TEXT("One sale reward"),E.Cards->GetGold(),Gold+Value);
 TestEqual(TEXT("Sale clears all three parts"),E.Cards->GetDeploymentParts().Num(),0);TestEqual(TEXT("Sale clears roster once"),E.GM->GetRoster()->GetHeroes().Num(),0);
 for(const auto Cell:{FIntPoint(6,0),FIntPoint(0,0),FIntPoint(4,0)})TestTrue(TEXT("Sale releases all occupied cells"),E.Grid->IsCellAvailable(Cell));
 TArray<AActor*> Blockers;for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<7;++X)if(!(Y==2&&(X==5||X==6)))
 {auto* B=E.W->SpawnActor<AEGDeployedPiece>();Blockers.Add(B);TestTrue(TEXT("Capacity fixture occupies real placement cell"),E.Grid->TryOccupyCell({X,Y},B));}
 E.Cards->GrantCard(Card);const int32 Hand=E.Cards->GetHand().Num();
 TestFalse(TEXT("Two free cells cannot deploy group"),E.Cards->TryDeployHandCard(E.Cards->GetHand().Last().InstanceId,{5,2}));
 TestEqual(TEXT("Insufficient space retains card"),E.Cards->GetHand().Num(),Hand);TestEqual(TEXT("Failure leaves roster empty"),E.GM->GetRoster()->GetHeroes().Num(),0);
 for(int32 Y=0;Y<3;++Y)for(int32 X=0;X<7;++X)if(auto* Occupant=E.Grid->GetOccupant({X,Y})){E.Grid->ReleaseCell({X,Y},Occupant);Occupant->Destroy();}
 const auto Handle=E.W->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([](AActor* A){if(auto* P=Cast<AEGDeployedPiece>(A);P&&P->PartRole!=EEGDeploymentPart::Hero)P->Destroy();}));
 TestFalse(TEXT("Device spawn rejection rolls back whole group"),E.Cards->TryDeployHandCard(E.Cards->GetHand().Last().InstanceId,{2,2}));
 E.W->RemoveOnActorSpawnedHandler(Handle);TestEqual(TEXT("Rejected group retains hand"),E.Cards->GetHand().Num(),Hand);TestEqual(TEXT("Rejected group releases every cell"),E.Grid->GetOccupiedCount(),0);TestEqual(TEXT("Rejected group removes uncommitted hero"),E.GM->GetRoster()->GetHeroes().Num(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVHealth,"ElyGambit.FlameChasers7x7.VillVIndependentHealth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVHealth::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Combat starts"),Id.IsValid()&&E.GM->EnterCombat()))return false;
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();
 if(!TestNotNull(TEXT("Coordinator"),M))return false;
 auto* G=M->GetDevice(0);auto* C=M->GetDevice(1);if(!TestNotNull(TEXT("Gun"),G)||!TestNotNull(TEXT("Cannon"),C))return false;
 TestEqual(TEXT("Gun owns half hero max HP"),G->GetAttributes()->GetMaxHealth(),U->GetAttributes()->GetMaxHealth()*.5f);
 TestEqual(TEXT("Cannon owns half hero max HP"),C->GetAttributes()->GetMaxHealth(),U->GetAttributes()->GetMaxHealth()*.5f);
 E.Freeze();auto* Enemy=E.Enemies()[0];
 TestTrue(TEXT("Empty gun targetable"),Enemy->GetCombat()->IsValidEnemy(G));TestTrue(TEXT("Empty cannon targetable"),Enemy->GetCombat()->IsValidEnemy(C));TestFalse(TEXT("Pilot untargetable"),Enemy->GetCombat()->IsValidEnemy(U));
 const auto GP=G->GetActorLocation(),CP=C->GetActorLocation();const float HP=C->GetAttributes()->GetHealth();
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 TestTrue(TEXT("Damage gun accepted"),FEGCombatExecution::Damage(Enemy,G,50));TestEqual(TEXT("Gun damage does not alter cannon HP"),C->GetAttributes()->GetHealth(),HP);
 const float Mana=U->GetAttributes()->GetMana();FEGCombatExecution::Damage(Enemy,C,50);TestEqual(TEXT("Shared damage mana cooldown"),U->GetAttributes()->GetMana(),Mana);
 TestEqual(TEXT("One shared damage mana grant"),Mana,U->GetDefinition()->GetManaOnDamage());
 const auto Rows=E.GM->GetCombatStatistics();
 TestTrue(TEXT("Both devices report taken damage under pilot"),Rows.ContainsByPredicate([Id](const FEGHeroCombatStatistics& R){return R.HeroId==Id&&FMath::IsNearlyEqual(R.Taken,100.f);}));
 TestEqual(TEXT("Auxiliary devices do not count as summons"),S->Battle->SummonCount(),0);
 for(int32 I=0;I<160&&!M->IsOperating();++I)E.Tick(.05f);E.Tick(.1f);
 if(!TestTrue(TEXT("Natural docking reaches occupied gun"),M->IsOperating()))return false;
 FEGCombatExecution::Damage(Enemy,G,20000);TestTrue(TEXT("One destroyed device keeps pilot alive"),IEGCombatant::Execute_IsAlive(U));
 TestFalse(TEXT("Destruction does not grant enhancement"),M->HasEnhancedShot());
 for(int32 I=0;I<240&&!(M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning());++I)E.Tick(.05f);
 TestTrue(TEXT("Destroyed occupied device transfers to cannon"),M->IsOperating()&&M->Mode()==1);
 TestTrue(TEXT("Devices never chase"),G->GetActorLocation().Equals(GP)&&C->GetActorLocation().Equals(CP));
 for(auto* EnemyUnit:E.Enemies())EnemyUnit->SetActorLocation(U->GetActorLocation()+FVector(15000,0,0));
 TestTrue(TEXT("Single-device ultimate starts"),E.Cast(U));E.Tick(1.5f);TestTrue(TEXT("Single-device ultimate remains at cannon"),M->Mode()==1&&M->IsOperating());TestTrue(TEXT("No target retains solo enhancement"),M->HasEnhancedShot());
 FEGCombatExecution::Damage(Enemy,C,20000);TestFalse(TEXT("Both destroyed retires pilot"),IEGCombatant::Execute_IsAlive(U));TestFalse(TEXT("Death cancels action"),S->IsActionRunning());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVTransferLifetime,"ElyGambit.FlameChasers7x7.VillVTransferAndRounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVTransferLifetime::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Enter first round"),Id.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M)return false;
 auto Wait=[&](int32 Mode){for(int32 I=0;I<300;++I){E.Tick(.05f);if(M->IsOperating()&&M->Mode()==Mode&&!S->IsActionRunning())return true;}return false;};
 if(!TestTrue(TEXT("Natural initial docking"),Wait(0)))return false;
 for(auto* T:E.Enemies())T->SetActorLocation({15000,15000,88});
 TestTrue(TEXT("Paid switch begins"),E.Cast(U));
 for(int32 I=0;I<50&&M->Phase!=EEGVillVPhase::Jumping;++I)E.Tick(.05f);
 TestEqual(TEXT("Leave reaches original jump state"),M->Phase,EEGVillVPhase::Jumping);
 auto* Enemy=E.Enemies()[0];FEGCombatExecution::Damage(Enemy,M->GetDevice(1),20000);
 if(!TestTrue(TEXT("Travel destination destroyed reroutes to survivor"),Wait(0)))return false;
 TestTrue(TEXT("Paid unused enhancement retained"),M->HasEnhancedShot());
 TWeakObjectPtr<AEGVillVEmplacement> OldGun=M->GetDevice(0),OldCannon=M->GetDevice(1);
 FEGHeroInstance Before;E.GM->GetRoster()->GetHero(Id,Before);
 TestTrue(TEXT("First round settles"),E.GM->ResolveCombat(true));E.Tick(.25f);
 if(E.GM->GetMatch()->GetPhase()==EEGMatchPhase::Resolution)E.GM->AdvanceDemo();
 TestEqual(TEXT("Second preparation begins"),E.GM->GetMatch()->GetPhase(),EEGMatchPhase::Preparation);
 TestFalse(TEXT("Settlement destroys old live device"),OldGun.IsValid());TestFalse(TEXT("Settlement destroys old dead device"),OldCannon.IsValid());
 FEGHeroInstance After;E.GM->GetRoster()->GetHero(Id,After);
 TestTrue(TEXT("Saved layout survives round"),Before.DeploymentCell==After.DeploymentCell&&Before.GatlingCell==After.GatlingCell&&Before.CannonCell==After.CannonCell);
 TestEqual(TEXT("Preparation restores exactly three parts"),E.Cards->GetDeploymentParts().Num(),3);
 if(!TestTrue(TEXT("Second round begins"),E.GM->EnterCombat()))return false;
 U=E.Find(Id);M=U->FindComponentByClass<UEGVillVMechanismComponent>();
 if(!TestNotNull(TEXT("Second round coordinator"),M))return false;
 TestTrue(TEXT("Destroyed cannon regenerates with full half HP"),M->GetDevice(1)&&IEGCombatant::Execute_IsAlive(M->GetDevice(1))&&FMath::IsNearlyEqual(M->GetDevice(1)->GetAttributes()->GetHealth(),U->GetAttributes()->GetMaxHealth()*.5f));
 TestTrue(TEXT("New round resets enhancement"),!M->HasEnhancedShot());
 TestTrue(TEXT("Second round settles"),E.GM->ResolveCombat(true));E.Tick(.25f);
 if(E.GM->GetMatch()->GetPhase()==EEGMatchPhase::Resolution)E.GM->AdvanceDemo();
 TestTrue(TEXT("New game starts"),E.GM->StartGame());TestEqual(TEXT("New game removes group"),E.Cards->GetDeploymentParts().Num(),0);TestEqual(TEXT("New game clears roster layout"),E.GM->GetRoster()->GetHeroes().Num(),0);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVAreaDamage,"ElyGambit.FlameChasers7x7.VillVAreaAndPendingEnhancement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVAreaDamage::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Combat"),Id.IsValid()&&E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M)return false;
 for(int32 I=0;I<200&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Initially docked"),M->IsOperating()&&!S->IsActionRunning()))return false;
 auto* Enemy=E.Enemies()[0];auto* G=M->GetDevice(0);auto* C=M->GetDevice(1);
 const auto Point=(G->GetActorLocation()+C->GetActorLocation())*.5f;
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 S->Battle->Area(Enemy,Point,800,100,false,false,FGuid::NewGuid());
 TestEqual(TEXT("One area hits gun health"),G->GetAttributes()->GetHealth(),9900.f);TestEqual(TEXT("Same area hits empty cannon separately"),C->GetAttributes()->GetHealth(),9900.f);
 TestEqual(TEXT("Area still grants one shared damage mana tick"),U->GetAttributes()->GetMana(),U->GetDefinition()->GetManaOnDamage());
 for(auto* T:E.Enemies())T->SetActorLocation({15000,15000,88});
 TestTrue(TEXT("Ultimate with no target"),E.Cast(U));
 for(int32 I=0;I<250&&!(M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning());++I)E.Tick(.05f);
 TestTrue(TEXT("Paid cannon enhancement pending"),M->IsOperating()&&M->Mode()==1&&M->HasEnhancedShot());
 FEGCombatExecution::Damage(Enemy,C,20000);
 TestTrue(TEXT("Free transfer keeps previous enhancement"),M->HasEnhancedShot());
 for(int32 I=0;I<250&&!(M->IsOperating()&&M->Mode()==0&&!S->IsActionRunning());++I)E.Tick(.05f);
 TestTrue(TEXT("Free transfer reaches gun without consuming enhancement"),M->IsOperating()&&M->Mode()==0&&M->HasEnhancedShot());
 S->Battle->Area(Enemy,Point,800,20000,false,false,FGuid::NewGuid());TestFalse(TEXT("Final area retires pilot"),IEGCombatant::Execute_IsAlive(U));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVRetry,"ElyGambit.FlameChasers7x7.VillVSimultaneousDeathAndRetry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVRetry::RunTest(const FString&)
{
 FFlameWorld E(*this,false);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Legacy retry-compatible deployment"),Id.IsValid()))return false;
 FEGHeroInstance Before;E.GM->GetRoster()->GetHero(Id,Before);
 if(!TestTrue(TEXT("Combat"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Find(Id);auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M)return false;
 auto* G=M->GetDevice(0);auto* C=M->GetDevice(1);const FVector Point=(G->GetActorLocation()+C->GetActorLocation())*.5f;
 TWeakObjectPtr<AEGVillVEmplacement> OldG=G,OldC=C;
 U->GetHeroSkill()->Battle->Area(E.Enemies()[0],Point,800,20000,false,false,FGuid::NewGuid());
 TestFalse(TEXT("Single area destroys both and retires pilot"),IEGCombatant::Execute_IsAlive(U));
 E.Tick(.25f);TestEqual(TEXT("Defeat enters retry phase"),E.GM->GetMatch()->GetPhase(),EEGMatchPhase::Finished);
 TestTrue(TEXT("Retry restores stage snapshot"),E.GM->AdvanceDemo());
 FEGHeroInstance After;E.GM->GetRoster()->GetHero(Id,After);
 TestTrue(TEXT("Retry restores three source positions"),Before.DeploymentCell==After.DeploymentCell&&Before.GatlingCell==After.GatlingCell&&Before.CannonCell==After.CannonCell&&After.bVillVLayout);
 TestEqual(TEXT("Retry restores exactly three preparation parts"),E.Cards->GetDeploymentParts().Num(),3);
 TestFalse(TEXT("Old gun invalid"),OldG.IsValid());TestFalse(TEXT("Old cannon invalid"),OldC.IsValid());
 if(!TestTrue(TEXT("Retried battle begins"),E.GM->EnterCombat()))return false;
 U=E.Find(Id);M=U->FindComponentByClass<UEGVillVMechanismComponent>();
 TestTrue(TEXT("Both devices regenerate for retry"),M&&M->GetDevice(0)&&M->GetDevice(1)&&IEGCombatant::Execute_IsAlive(M->GetDevice(0))&&IEGCombatant::Execute_IsAlive(M->GetDevice(1)));
 E.Tick(.2f);TestTrue(TEXT("Previous generation cannot retire new pilot"),IEGCombatant::Execute_IsAlive(U));return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVArmoredBurst,"ElyGambit.VillVCombat.ArmoredBurstAndRetarget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVArmoredBurst::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M)return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Opening dock complete"),M->IsOperating()&&!S->IsActionRunning()))return false;
 auto Enemies=E.Enemies();if(!TestTrue(TEXT("Two target fixture"),Enemies.Num()>1))return false;
 for(auto* T:Enemies)T->SetActorLocation({15000,15000,88});auto* A=Enemies[0];auto* B=Enemies[1];
 A->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(.8f);
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),60);
 A->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),30);
 const float Before=A->GetAttributes()->GetHealth();const int32 Count=S->Battle->GetAttackCount();
 if(!TestTrue(TEXT("Aimed armored burst begins"),S->BeginAction(false)))return false;E.Tick(1.5f);
 TestTrue(TEXT("Three pellets each use proportional mitigation with one third of defense"),FMath::IsNearlyEqual(Before-A->GetAttributes()->GetHealth(),60.f*100.f/110.f,.02f));
 TestEqual(TEXT("One attack reward for all bullets"),S->Battle->GetAttackCount(),Count+1);
 A->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
 A->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5);
 A->SetActorLocation(U->GetActorLocation()+FVector(300,0,0));B->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(.3f);
 const float BHealth=B->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Burst with dying first target"),S->BeginAction(false)))return false;E.Tick(1.5f);
 TestFalse(TEXT("First pellet kills first target"),IEGCombatant::Execute_IsAlive(A));
 TestTrue(TEXT("Remaining two pellets reacquire live target"),FMath::IsNearlyEqual(BHealth-B->GetAttributes()->GetHealth(),40.f,.02f));
 TestEqual(TEXT("Retargeted volley still one basic attack"),S->Battle->GetAttackCount(),Count+2);
 B->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(.2f);
 if(!S->BeginAction(false))return false;const float Whiff=B->GetAttributes()->GetHealth();B->SetActorLocation({15000,15000,88});E.Tick(1.5f);
 TestEqual(TEXT("No damage fabricated outside stationary turret range"),B->GetAttributes()->GetHealth(),Whiff);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVAimAndJump,"ElyGambit.VillVCombat.AimJumpAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVAimAndJump::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);const auto AllyId=E.Deploy(TEXT("Pardofelis"),TEXT("Pardo"),6);if(!AllyId.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(Id);auto* Ally=E.Find(AllyId);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M||!TestNotNull(TEXT("Friendly obstruction fixture"),Ally))return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!M->IsOperating()||S->IsActionRunning())return false;
 TestEqual(TEXT("Ground approach only at opening"),M->GroundTravelCount,1);
 auto* Enemy=E.Enemies()[0];for(auto* T:E.Enemies())T->SetActorLocation({15000,15000,88});
 auto* Gun=M->GetDevice(0);auto* Cannon=M->GetDevice(1);if(!TestNotNull(TEXT("Occupied gatling"),Gun)||!TestNotNull(TEXT("Idle cannon"),Cannon))return false;
 const FVector OriginalAlly=Ally->GetActorLocation(),OriginalCannon=Cannon->GetActorLocation();
 Ally->SetActorLocation(FVector(15000,15000,88),false);Cannon->SetActorLocation(FVector(16000,16000,88),false);
 Gun->SetActorRotation(FRotator::ZeroRotator);U->SetActorRotation(FRotator::ZeroRotator);Enemy->SetActorLocation(U->GetActorLocation()+FVector(0,600,0));
 // The nearby enemy is outside the one-cell bases but intersects the long
 // visible barrel sweep. Aiming must still advance at the configured rate.
 const float EnemyRadius=UEGCombatComponent::BlockingRadius(Enemy);
 const float NearDistance=UEGCombatComponent::BlockingRadius(Gun)+EnemyRadius+25.f;
 Enemy->SetActorLocation(Gun->GetActorLocation()+FVector(0,NearDistance,0),false);
 if(!TestTrue(TEXT("Near target remains in the original attack range"),U->GetCombat()->IsInAttackRange(Enemy)))return false;
 E.Tick(.05f);
 TestTrue(TEXT("Gun turns toward a nearby target even when its model crosses the barrel"),Gun->GetActorRotation().Yaw>0.f);
 E.Tick(.8f);
 TestTrue(TEXT("Nearby target becomes aimed"),M->IsAimReady());
 const FVector SideOffset=FVector(1,1,0).GetSafeNormal()*NearDistance;
 for(AEGChessUnit* Obstacle:{Ally,static_cast<AEGChessUnit*>(Cannon)})
 {
  Gun->SetActorRotation(FRotator::ZeroRotator);U->SetActorRotation(FRotator::ZeroRotator);
  Obstacle->SetActorLocation(Gun->GetActorLocation()+SideOffset,false);
  E.Tick(.8f);
  TestTrue(Obstacle==Ally?TEXT("Gatling aims past friendly unit"):TEXT("Gatling aims past its own cannon"),M->IsAimReady());
  Obstacle->SetActorLocation(FVector(15000,15000,88),false);
 }
 Gun->SetActorRotation(FRotator::ZeroRotator);U->SetActorRotation(FRotator::ZeroRotator);
 TestFalse(TEXT("Gun does not fire sideways before aiming"),S->BeginAction(false));E.Tick(.05f);
 const float Yaw=M->GetDevice(0)->GetActorRotation().Yaw;
 TestTrue(TEXT("Barrel turns smoothly at configured angular speed"),Yaw>0&&Yaw<=12.01f);
 TestTrue(TEXT("Pilot stays attached to gun facing"),FMath::IsNearlyEqual(Yaw,U->GetActorRotation().Yaw,.01f));E.Tick(.8f);
 TestTrue(TEXT("Aimed target becomes eligible"),M->IsAimReady());
 Ally->SetActorLocation(OriginalAlly,false);Cannon->SetActorLocation(OriginalCannon,false);Enemy->SetActorLocation(FVector(15000,15000,88),false);
 if(!E.Cast(U))return false;
 bool SawJump=false;
 for(int32 I=0;I<160;++I){E.Tick(.05f);SawJump|=M->Phase==EEGVillVPhase::Jumping;if(M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning())break;}
 TestTrue(TEXT("Switch uses source jump phase"),SawJump&&M->Mode()==1);
 TestEqual(TEXT("No ground walk inserted after Leave"),M->GroundTravelCount,1);
 TestTrue(TEXT("Jump counter records original transfer"),M->JumpTransferCount>=1);
 const FVector OriginalGun=Gun->GetActorLocation();
 Ally->SetActorLocation(FVector(15000,15000,88),false);Gun->SetActorLocation(FVector(16000,16000,88),false);
 Enemy->SetActorLocation(Cannon->GetActorLocation()+FVector(0,NearDistance,0),false);
 for(AEGChessUnit* Obstacle:{Ally,static_cast<AEGChessUnit*>(Gun)})
 {
  Cannon->SetActorRotation(FRotator::ZeroRotator);U->SetActorRotation(FRotator::ZeroRotator);
  Obstacle->SetActorLocation(Cannon->GetActorLocation()+SideOffset,false);
  E.Tick(.8f);
  TestTrue(Obstacle==Ally?TEXT("Cannon aims past friendly unit"):TEXT("Cannon aims past its own gatling"),M->IsAimReady());
  Obstacle->SetActorLocation(FVector(15000,15000,88),false);
 }
 Gun->SetActorLocation(OriginalGun,false);Ally->SetActorLocation(OriginalAlly,false);Enemy->SetActorLocation(FVector(15000,15000,88),false);
 if(!E.Cast(U))return false;S->CancelAction();
 for(int32 I=0;I<180&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 TestTrue(TEXT("Cancelled switch settles back into current weapon without reentrant teardown"),M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning());
 TestEqual(TEXT("Cancelled return still never starts ground walking"),M->GroundTravelCount,1);
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVVisiblePose,"ElyGambit.VillVCombat.VisiblePoseAndZeroDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVVisiblePose::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M)return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Opening reaches a completed seat"),M->IsOperating()&&!S->IsActionRunning()))return false;
 E.Tick(.2f);int32 BodyDrivers=0;
 for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++BodyDrivers;
 TestEqual(TEXT("Exactly one passive rig owns seated body"),BodyDrivers,1);
 auto* T=E.Enemies()[0];for(auto* Other:E.Enemies())Other->SetActorLocation({15000,15000,88});
 for(const FVector Direction:{FVector(0,600,0),FVector(-600,0,0),FVector(0,-600,0),FVector(600,0,0)})
 {T->SetActorLocation(U->GetActorLocation()+Direction);E.Tick(1.f);TestTrue(TEXT("Stationary device aims in each quadrant"),M->IsAimReady(T));}
 T->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100000);
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 const float HP=T->GetAttributes()->GetHealth();const int32 Count=S->Battle->GetAttackCount();
 if(!TestTrue(TEXT("Zero-damage burst starts"),S->BeginAction(false)))return false;E.Tick(.2f);
 auto* Anim=Cast<UEGCombatAnimInstance>(M->GetDevice(0)->GetVisiblePresentationMesh()->GetAnimInstance());
 TestTrue(TEXT("Gun plays fire clip even without HP loss"),Anim&&Anim->PassiveClip&&Anim->PassiveClip->GetName()==TEXT("A_VillV_GatlingFire"));
 TestTrue(TEXT("Visible gun animation advances"),Anim&&Anim->PassiveTime>0);
 auto* PilotAnim=Cast<UEGCombatAnimInstance>(U->GetVisiblePresentationMesh()->GetAnimInstance());
 TestTrue(TEXT("Gatling montage blends over firing body start"),PilotAnim&&PilotAnim->PassiveClip==S->GetActiveAnimation()&&FMath::IsNearlyZero(PilotAnim->PassiveTime));
 E.Tick(1.5f);TestTrue(TEXT("Very high armor retains finite positive damage"),HP-T->GetAttributes()->GetHealth()>0.f&&HP-T->GetAttributes()->GetHealth()<1.f);
 TestEqual(TEXT("Armored volley preserves one accepted-hit reward"),S->Battle->GetAttackCount(),Count+1);
 TestEqual(TEXT("Three shots preserve one basic-hit mana award"),U->GetAttributes()->GetMana(),U->GetDefinition()->GetManaOnHit());
 TestFalse(TEXT("Action completes without watchdog loop"),S->IsActionRunning());
 const auto* GunBody=U->GetDefinition()->PresentationDefinition->AttackSequence[0].Animation.Get();
 TestTrue(TEXT("Firing body persists over the decision gap"),PilotAnim&&GunBody&&PilotAnim->PassiveClip==GunBody);
 const FVector PreviousTarget=T->GetActorLocation();
 T->SetActorLocation(FVector(15000,15000,88));E.Tick(.2f);
 TestTrue(TEXT("Target loss restores occupied seated pose"),PilotAnim&&PilotAnim->PassiveClip&&PilotAnim->PassiveClip->GetName()==TEXT("A_VillV_GatlingSeated"));
 T->SetActorLocation(PreviousTarget);E.Tick(.3f);
 const FVector BeforeLeave=U->GetActorLocation();
 if(!TestTrue(TEXT("Continuous departure begins"),E.Cast(U)))return false;
 E.Tick(.2f);
 TestTrue(TEXT("Pilot travels during original leave, not only at Depart"),FVector::Dist2D(BeforeLeave,U->GetActorLocation())>20.f);
 bool CheckedDeparture=false;
 for(int32 I=0;I<150&&(!M->IsOperating()||S->IsActionRunning());++I)
 {
  const auto BeforePhase=M->Phase;const FVector BeforePosition=U->GetActorLocation();E.Tick(.04f);
  if(BeforePhase==EEGVillVPhase::Leaving&&M->Phase==EEGVillVPhase::Jumping)
  {CheckedDeparture=true;TestTrue(TEXT("Depart never rewinds pilot to occupied device"),FVector::Dist2D(BeforePosition,U->GetActorLocation())<100.f);}
 }
 TestTrue(TEXT("Departure boundary was observed"),CheckedDeparture);
 TestTrue(TEXT("Continuous departure docks at the other device"),M->IsOperating()&&M->Mode()==1);
 for(int32 Cycle=0;Cycle<3;++Cycle)
 {
  if(!TestTrue(TEXT("Cannon repeat cycle starts without another entry"),S->BeginAction(false)))return false;
  E.Tick(.2f);
  TestTrue(TEXT("Cannon montage blends over firing body start"),PilotAnim&&PilotAnim->PassiveClip==S->GetActiveAnimation()&&FMath::IsNearlyZero(PilotAnim->PassiveTime));
  TestTrue(TEXT("Cannon repeat uses attack, never an enter montage"),S->GetActiveAnimation()&&S->GetActiveAnimation()->GetName().Contains(TEXT("CannonAttack")));
  E.Tick(2.f);TestTrue(TEXT("Repeated cannon stays operating"),M->IsOperating()&&M->Mode()==1);
 }
 if(!TestTrue(TEXT("Low-frame-rate paid switch begins"),E.Cast(U)))return false;
 for(int32 I=0;I<40&&(!M->IsOperating()||M->Mode()!=0||S->IsActionRunning());++I)
 {++GFrameCounter;E.W->Tick(LEVELTICK_All,.24f);}
 TestTrue(TEXT("Cross-notify frames reach gun without a stuck traversal"),M->IsOperating()&&M->Mode()==0&&!S->IsActionRunning());
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVHeldPoseDeviceDeath,"ElyGambit.VillVCombat.HeldPoseDeviceDeathTransfer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVHeldPoseDeviceDeath::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U?U->GetHeroSkill():nullptr;
 auto* M=U?U->FindComponentByClass<UEGVillVMechanismComponent>():nullptr;
 if(!S||!M||E.Enemies().IsEmpty())return false;
 auto* Enemy=E.Enemies()[0];
 for(auto* Other:E.Enemies())Other->SetActorLocation({15000,15000,88});
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Gun occupied"),M->IsOperating()&&M->Mode()==0&&!S->IsActionRunning()))return false;
 Enemy->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 if(!TestTrue(TEXT("Gun fires"),S->BeginAction(false)))return false;
 E.Tick(1.5f);
 auto* Anim=Cast<UEGCombatAnimInstance>(U->GetVisiblePresentationMesh()->GetAnimInstance());
 const auto* Gun=U->GetDefinition()->PresentationDefinition->AttackSequence[0].Animation.Get();
 if(!TestTrue(TEXT("Completed gun pose held before destruction"),!S->IsActionRunning()&&Anim&&Gun&&Anim->PassiveClip==Gun))return false;
 if(!TestTrue(TEXT("Damage destroys occupied gun"),FEGCombatExecution::Damage(Enemy,M->GetDevice(0),20000)))return false;
 for(int32 I=0;I<300&&(!M->IsOperating()||M->Mode()!=1||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Pilot reaches surviving cannon"),M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning()))return false;
 E.Tick(.1f);
 TestTrue(TEXT("Cannon seat never inherits destroyed gun's held body"),Anim&&Anim->PassiveClip&&Anim->PassiveClip!=Gun);
 TestTrue(TEXT("Cannon idle pose restored"),Anim&&Anim->PassiveClip&&Anim->PassiveClip->GetName()==TEXT("A_VillV_CannonSeated"));
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVGASSpeedCycles,"ElyGambit.VillVCombat.GASSpeedAcrossCycles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVGASSpeedCycles::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U?U->GetHeroSkill():nullptr;
 auto* M=U?U->FindComponentByClass<UEGVillVMechanismComponent>():nullptr;
 if(!S||!M||E.Enemies().IsEmpty())return false;
 for(auto* T:E.Enemies())T->SetActorLocation({15000,15000,88});
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Gun operating before speed tests"),M->IsOperating()&&M->Mode()==0&&!S->IsActionRunning()))return false;
 auto* Target=E.Enemies()[0];Target->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 auto* ASC=U->GetAbilitySystemComponent();auto* Anim=U->GetVisiblePresentationMesh()->GetAnimInstance();
 if(!ASC||!Anim)return false;
 auto Speed=[&](float Value){ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Value);};
 Speed(1.f);
 for(int32 Cycle=0;Cycle<3;++Cycle)
 {
  const float StartSpeed=Cycle==0?1.f:Cycle==1?.5f:2.f;
  Speed(StartSpeed);const int32 Commits=S->CommitCount,Rewards=S->Battle->GetAttackCount();
  const float Health=Target->GetAttributes()->GetHealth(),Power=U->GetAttributes()->GetAttackPower();
  if(!TestTrue(TEXT("GAS-controlled gun attack begins"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();const auto* Clip=S->GetActiveAnimation();
  if(!Montage||!Clip)return false;
  const float Rate=Anim->Montage_GetPlayRate(Montage);
  TestTrue(TEXT("Start rate uses current GAS speed"),FMath::IsNearlyEqual(Rate,StartSpeed,.001f));
  E.Tick(.05f);Speed(2.f);
  TestTrue(TEXT("Mid-action speed change leaves current gun rate unchanged"),FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage),Rate,.001f));
  if(Cycle==0){Speed(.5f);TestTrue(TEXT("Second mid-action speed change leaves gun rate unchanged"),FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage),Rate,.001f));}
  float Elapsed=.05f;
  for(int32 I=0;I<120&&S->IsActionRunning();++I){E.Tick(.05f);Elapsed+=.05f;}
  if(!TestFalse(TEXT("Gun action completes after speed changes"),S->IsActionRunning()))return false;
  TestTrue(TEXT("Gun finishes near its animation endpoint, before watchdog"),Elapsed<=Montage->GetPlayLength()/Rate+.08f);
  TestEqual(TEXT("Three distinct firing notifies committed once"),S->CommitCount,Commits+3);
  TestEqual(TEXT("One reward per three-shot burst"),S->Battle->GetAttackCount(),Rewards+1);
  TestTrue(TEXT("One normal attack worth of damage"),FMath::IsNearlyEqual(Health-Target->GetAttributes()->GetHealth(),Power,.05f));
 }
 Speed(.5f);if(!TestTrue(TEXT("Start gun action for cancellation"),S->BeginAction(false)))return false;
 E.Tick(.05f);Speed(2.f);S->CancelAction();E.Tick(.15f);
 TestFalse(TEXT("Cancelled gun action stops"),S->IsActionRunning());
 auto* PilotAnim=Cast<UEGCombatAnimInstance>(Anim);
 TestTrue(TEXT("Cancelled gun restores seated body"),PilotAnim&&PilotAnim->PassiveClip&&PilotAnim->PassiveClip->GetName()==TEXT("A_VillV_GatlingSeated"));
 if(!TestTrue(TEXT("Switch to cannon"),E.Cast(U)))return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||M->Mode()!=1||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Cannon operating"),M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning()))return false;
 Target->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 for(float Value:{1.f,.5f,2.f})
 {
  Speed(Value);
  if(!TestTrue(TEXT("Cannon attack begins"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();if(!Montage)return false;
  TestTrue(TEXT("Cannon uses attack speed as source playback rate"),FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage),Value,.001f));
  float Elapsed=0.f;
  for(int32 I=0;I<160&&S->IsActionRunning();++I){E.Tick(.05f);Elapsed+=.05f;}
  if(!TestFalse(TEXT("Cannon attack finishes"),S->IsActionRunning()))return false;
  TestTrue(TEXT("Cannon finishes near its animation endpoint, before watchdog"),Elapsed<=Montage->GetPlayLength()/Value+.08f);
 }
 E.GM->BattleRules->Stop();E.Tick(.1f);
 int32 Remaining=0;for(TActorIterator<AEGVillVCombatFX> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++Remaining;
 TestEqual(TEXT("Round stop clears gun cosmetic effects"),Remaining,0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVPreparationEntrance,"ElyGambit.VillVCombat.PreparationEntranceLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVPreparationEntrance::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!TestTrue(TEXT("Deploy VillV"),Id.IsValid()))return false;
 // Deployment may occur before cosmetic assets finish their background request.
 FlushAsyncLoading();
 // Streamable completion delegates run on the engine tick, not UWorld::Tick alone.
 FTickableGameObject::TickObjects(nullptr,LEVELTICK_All,false,.05f);
 FTickableGameObject::TickObjects(nullptr,LEVELTICK_All,false,.05f);
 auto* S=Cast<UEGVillVSkillDefinition>(E.Find(Id)->GetDefinition()->SkillDefinition);
 auto Rig=[&](AActor* Owner)->AEGCombatVisual*{for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)if(It->GetOwner()==Owner&&!It->IsActorBeingDestroyed())return *It;return nullptr;};
 AEGDeployedPiece* Gun=nullptr;
 for(auto* Part:E.Cards->GetDeploymentParts())if(Part->PartRole!=EEGDeploymentPart::Hero)
 {
  const int32 Index=Part->PartRole==EEGDeploymentPart::Gatling?0:1;if(Index==0)Gun=Part;
  auto* V=Rig(Part);if(!TestNotNull(TEXT("Preparation device rig exists"),V))return false;
  TestTrue(TEXT("Native generation starts in preparation"),V->Definition==S->DeviceSpawns[Index].Get()&&!V->bLoop);
 }
 if(!Gun)return false;E.Tick(.25f);auto* Initial=Rig(Gun);const float Clock=Initial->SourceTime;
 E.Cards->SetPreparationEnabled(true);E.Cards->RefreshHeroPresentations();
 TestTrue(TEXT("Repeated resource/roster refresh does not replay entrance"),Rig(Gun)==Initial&&Rig(Gun)->SourceTime==Clock);
 E.Tick(1.5f);TestTrue(TEXT("Entrance completes into native looping idle"),Rig(Gun)&&Rig(Gun)->Definition==S->DeviceIdles[0].Get()&&Rig(Gun)->bLoop);
 if(!TestTrue(TEXT("Start combat"),E.GM->EnterCombat()))return false;E.Freeze();auto* M=E.Find(Id)->FindComponentByClass<UEGVillVMechanismComponent>();
 TestNull(TEXT("Combat clears preparation rig"),Rig(Gun));
 for(int32 I=0;I<2;++I)TestTrue(TEXT("Combat starts expanded, never generation"),Rig(M->GetDevice(I))&&Rig(M->GetDevice(I))->Definition==S->DeviceIdles[I].Get());
 if(!TestTrue(TEXT("Resolve round"),E.GM->ResolveCombat(true)))return false;
 TestTrue(TEXT("Resolution restores idle without early generation"),Rig(Gun)&&Rig(Gun)->Definition==S->DeviceIdles[0].Get());
 if(!TestTrue(TEXT("Next preparation"),E.GM->NextStage()))return false;
 TestTrue(TEXT("Next preparation replays entrance once"),Rig(Gun)&&Rig(Gun)->Definition==S->DeviceSpawns[0].Get());
 if(!TestTrue(TEXT("Immediate start can interrupt entrance"),E.GM->EnterCombat()))return false;E.Freeze();E.Tick(1.6f);
 TestNull(TEXT("Cancelled entrance timer never recreates hidden preparation rig"),Rig(Gun));
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVSeatedFXLifecycle,"ElyGambit.VillVCombat.SeatedFXLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVSeatedFXLifecycle::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U?U->GetHeroSkill():nullptr;
 auto* M=U?U->FindComponentByClass<UEGVillVMechanismComponent>():nullptr;
 if(!S||!M||E.Enemies().IsEmpty())return false;
 auto* Def=Cast<UEGVillVSkillDefinition>(U->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("VillV skill definition"),Def))return false;
 TStrongObjectPtr<UEGVillVCombatFXDefinition> OriginalConfig(Def->SourceCombatFX.Get());
 TStrongObjectPtr<UEGVillVCombatFXDefinition> V2Config(Def->SourceCombatFX.Get());
 if(!V2Config.IsValid()||V2Config->ShotPresetsV2.Num()<4)V2Config.Reset(LoadObject<UEGVillVCombatFXDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/CombatReturn20261102/DA_VillV_CombatFX_V2.DA_VillV_CombatFX_V2")));
 if(!TestNotNull(TEXT("V2 seated effects configuration is available"),V2Config.Get()))return false;
 if(!TestTrue(TEXT("All seated V2 weapon presets are present"),V2Config->ShotPresetsV2.Num()>=4))return false;
 // Change only the in-memory fixture and restore the original asset reference on all exits.
 TGuardValue<TObjectPtr<UEGVillVCombatFXDefinition>> ConfigGuard(Def->SourceCombatFX,TObjectPtr<UEGVillVCombatFXDefinition>());
 AEGChessUnit* Target=nullptr;
 bool bExpectV2=false,bSawGunV2=false,bSawCannonV2=false;
 auto Check=[&]()
 {
  int32 Effects=0,Shots=0;bool bVisibleShot=false;
  for(TActorIterator<AEGVillVCombatFX> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++Effects;
  for(TActorIterator<AEGVillVShotVisual> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())
  {
   ++Shots;TArray<UStaticMeshComponent*> Parts;It->GetComponents(Parts);
   for(const auto* Part:Parts)if(IsValid(Part)&&Part->IsVisible()&&Part->GetStaticMesh()&&
    (Part->ComponentHasTag(TEXT("VillVShotMuzzle"))||Part->ComponentHasTag(TEXT("VillVShotRelease"))||
     Part->ComponentHasTag(TEXT("VillVShotProjectile"))||Part->ComponentHasTag(TEXT("VillVShotImpact"))))bVisibleShot=true;
  }
  for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++Effects;
  for(TActorIterator<AEGChessNiagaraBurst> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++Effects;
  for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)
  {
   const auto* Owner=It->GetOwner();
   if(Owner!=U&&Owner!=M->GetDevice(0)&&Owner!=M->GetDevice(1))continue;
   TArray<UStaticMeshComponent*> Parts;It->GetComponents(Parts);
   for(const auto* Part:Parts)if(Part->ComponentHasTag(TEXT("EGSourcePreviewOwnedFX")))++Effects;
  }
  TestEqual(TEXT("Retired VillV action, device, and transition effects remain absent"),Effects,0);
  if(bExpectV2)
  {
   TestTrue(TEXT("V2 owned shot actors stay within the hard cap"),Shots<=24);
   if(bVisibleShot&&M->Mode()==0)bSawGunV2=true;
   if(bVisibleShot&&M->Mode()==1)bSawCannonV2=true;
  }
  else TestEqual(TEXT("Without a V2 configuration no new shot actor appears"),Shots,0);
  if(Target&&Target->GetHitFeedback())TestEqual(TEXT("VillV damage creates no generic target hit flash"),Target->GetHitFeedback()->GetActiveBurstCount(),0);
 };
 for(auto* Enemy:E.Enemies())Enemy->SetActorLocation({15000,15000,88});
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I){E.Tick(.05f);Check();}
 if(!TestTrue(TEXT("Pilot reaches gun"),M->IsOperating()&&M->Mode()==0&&!S->IsActionRunning()))return false;
 Target=E.Enemies()[0];Target->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 const float BeforeUnconfiguredGun=Target->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Gun starts"),S->BeginAction(false)))return false;
 for(int32 I=0;I<100&&S->IsActionRunning();++I){E.Tick(.05f);Check();}
 TestTrue(TEXT("Unconfigured gun still damages the target"),Target->GetAttributes()->GetHealth()<BeforeUnconfiguredGun);
 Def->SourceCombatFX=V2Config.Get();bExpectV2=true;
 Target->SetActorLocation(M->GetDevice(1)->GetActorLocation()+M->GetDevice(1)->GetActorForwardVector()*600.f);
 const float BeforeEnhancedCannon=Target->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Switch begins"),E.Cast(U)))return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||M->Mode()!=1||S->IsActionRunning());++I){E.Tick(.05f);Check();}
 if(!TestTrue(TEXT("Pilot reaches cannon"),M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning()))return false;
 TestTrue(TEXT("Enhanced cannon actually damages the target"),Target->GetAttributes()->GetHealth()<BeforeEnhancedCannon);
 TestTrue(TEXT("Configured cannon creates a visible owned V2 shot component"),bSawCannonV2);
 auto* PilotAnim=Cast<UEGCombatAnimInstance>(U->GetVisiblePresentationMesh()->GetAnimInstance());
 TestTrue(TEXT("Enhanced cannon holds the attack pose after firing"),PilotAnim&&PilotAnim->PassiveClip==U->GetDefinition()->PresentationDefinition->AttackSequence[1].Animation.Get());
 Target->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 if(!TestTrue(TEXT("Cannon starts"),S->BeginAction(false)))return false;
 for(int32 I=0;I<100&&S->IsActionRunning();++I){E.Tick(.05f);Check();}
 Target->SetActorLocation(M->GetDevice(0)->GetActorLocation()+M->GetDevice(0)->GetActorForwardVector()*600.f);
 const float BeforeEnhancedGun=Target->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Switch back to gun begins"),E.Cast(U)))return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||M->Mode()!=0||S->IsActionRunning());++I){E.Tick(.05f);Check();}
 if(!TestTrue(TEXT("Pilot returns to gun"),M->IsOperating()&&M->Mode()==0&&!S->IsActionRunning()))return false;
 TestTrue(TEXT("Enhanced gun actually damages the target"),Target->GetAttributes()->GetHealth()<BeforeEnhancedGun);
 TestTrue(TEXT("Enhanced gun holds its attack pose"),PilotAnim&&PilotAnim->PassiveClip==U->GetDefinition()->PresentationDefinition->AttackSequence[0].Animation.Get());
 TestTrue(TEXT("Configured gun creates a visible owned V2 shot component"),bSawGunV2);
 // Stop must destroy a genuinely live V2 shot, not merely report an empty list.
 Target->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 if(!TestTrue(TEXT("Gun starts for live-shot stop check"),S->BeginAction(false)))return false;
 int32 LiveShots=0;
 for(int32 I=0;I<100&&S->IsActionRunning()&&LiveShots==0;++I)
 {
  E.Tick(.05f);Check();
  for(TActorIterator<AEGVillVShotVisual> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++LiveShots;
 }
 if(!TestTrue(TEXT("A real V2 shot exists before stop"),LiveShots>0))return false;
 M->Stop();
 int32 RemainingShots=0;for(TActorIterator<AEGVillVShotVisual> It(E.W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())++RemainingShots;
 TestEqual(TEXT("Stop destroys owned V2 shot actors"),RemainingShots,0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVGunLoopAudio,"ElyGambit.VillVCombat.GunLoopAudioLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVGunLoopAudio::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U?U->GetHeroSkill():nullptr;
 auto* M=U?U->FindComponentByClass<UEGVillVMechanismComponent>():nullptr;
 auto* Audio=U?U->FindComponentByClass<UEGCombatAudioComponent>():nullptr;
 if(!S||!M||!Audio||E.Enemies().IsEmpty())return false;
 const auto* P=U->GetDefinition()->GetCombatAudioPresentation();if(!P)return false;
 for(const auto& Entry:P->AudioEvents)for(const auto& Sound:Entry.Sounds)Sound.LoadSynchronous();
 for(auto* Enemy:E.Enemies())Enemy->SetActorLocation({15000,15000,88});
 for(int32 I=0;I<250&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Gun is operating"),M->IsOperating()&&!S->IsActionRunning()))return false;
 auto* Target=E.Enemies()[0];Target->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));E.Tick(1.f);
 if(!TestTrue(TEXT("First attack begins"),S->BeginAction(false)))return false;
 E.Tick(.05f);TestTrue(TEXT("Complete original gun start is used"),Audio->LastSoundPath.Contains(TEXT("S_VillV_GunStart_Full")));
 E.Tick(.15f);auto* Loop=Audio->GetVillVGunLoop();
 if(!TestTrue(TEXT("Original full gun loop is playing"),IsValid(Loop)&&Loop->IsPlaying()))return false;
 TestTrue(TEXT("Full loop asset is used"),Audio->LastSoundPath.Contains(TEXT("S_VillV_GunLoop_Full")));
 for(int32 I=0;I<100&&S->IsActionRunning();++I)E.Tick(.05f);
 if(!TestFalse(TEXT("First action completes"),S->IsActionRunning()))return false;
 TestTrue(TEXT("Loop continues across the action boundary"),Audio->GetVillVGunLoop()==Loop&&Loop->IsPlaying());
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),.5f);
 if(!TestTrue(TEXT("Second attack begins"),S->BeginAction(false)))return false;
 E.Tick(.2f);TestTrue(TEXT("Second attack reuses the same loop"),Audio->GetVillVGunLoop()==Loop&&Loop->IsPlaying());
 TestTrue(TEXT("Loop tempo follows the current animation"),FMath::IsNearlyEqual(Loop->PitchMultiplier,.5f,.01f));
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),0.f);
 for(int32 I=0;I<120&&S->IsActionRunning();++I)E.Tick(.05f);
 E.Tick(.1f);TestNull(TEXT("Zero attack speed releases the loop after the current cycle"),Audio->GetVillVGunLoop());
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1.f);
 if(!TestTrue(TEXT("Gun restarts after speed recovers"),S->BeginAction(false)))return false;
 E.Tick(.2f);if(!TestNotNull(TEXT("Loop starts again"),Audio->GetVillVGunLoop()))return false;
 Target->SetActorLocation({15000,15000,88});E.Tick(.1f);
 TestNull(TEXT("Losing the target releases the loop"),Audio->GetVillVGunLoop());
 TestTrue(TEXT("Complete original gun stop is used"),Audio->LastSoundPath.Contains(TEXT("S_VillV_GunEnd_Full")));
 Target->SetActorLocation(M->GetDevice(1)->GetActorLocation()+M->GetDevice(1)->GetActorForwardVector()*600.f);
 if(!TestTrue(TEXT("Switch to cannon starts"),E.Cast(U)))return false;
 for(int32 I=0;I<250&&(!M->IsOperating()||M->Mode()!=1||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Cannon is operating"),M->IsOperating()&&M->Mode()==1&&!S->IsActionRunning()))return false;
 auto* Charge=Audio->GetVillVCannonCharge();
 if(!TestTrue(TEXT("Complete cannon charge remains playing"),IsValid(Charge)&&Charge->IsPlaying()))return false;
 if(!TestTrue(TEXT("Cannon attack begins"),S->BeginAction(false)))return false;
 E.Tick(.1f);TestTrue(TEXT("Repeated cannon actions reuse the active charge"),Audio->GetVillVCannonCharge()==Charge&&Charge->IsPlaying());
 S->CancelAction();TestNull(TEXT("Cancelling cannon clears the charge voice"),Audio->GetVillVCannonCharge());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGChessFXLifetime,"ElyGambit.ChessFX.LifetimeRoundAndIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGChessFXLifetime::RunTest(const FString&)
{
 FFlameWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("FuHua"),TEXT("Hua"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;
 E.Freeze();auto* U=E.Find(Id);auto* S=U->GetHeroSkill();S->CancelAction();
 auto* Enemy=E.Enemies()[0];const float HP=Enemy->GetAttributes()->GetHealth();
 const auto* D=U->GetDefinition()->SkillDefinition.Get();
 const auto* Authored=D?D->ChessFX.Find(TEXT("Strike")):nullptr;
 if(!TestNotNull(TEXT("Formal Hua has the imported strike cue"),Authored))return false;
 FEGChessFXCue Cue=*Authored;Cue.bAttachToOwner=false;Cue.bActionBound=false;Cue.Duration=.2f;
 TWeakObjectPtr<AEGChessNiagaraBurst> FX=AEGChessNiagaraBurst::SpawnCue(S,S->Battle.Get(),Cue,U->GetActorLocation());
 if(!TestTrue(TEXT("Resident cosmetic cue spawns"),FX.IsValid()))return false;
 TestFalse(TEXT("Cue does not replicate"),FX->GetIsReplicated());
 E.Tick(.25f);
 TestFalse(TEXT("Finite effect expires on its ordinary clock"),FX.IsValid());
 TestEqual(TEXT("Cosmetics never change target health"),Enemy->GetAttributes()->GetHealth(),HP);
 Cue.Duration=5.f;FX=AEGChessNiagaraBurst::SpawnCue(S,S->Battle.Get(),Cue,U->GetActorLocation());
 if(!FX.IsValid())return false;
 TestTrue(TEXT("Round settles"),E.GM->ResolveCombat(true));if(FX.IsValid())FX->Tick(.01f);
 TestFalse(TEXT("Settlement invalidates old round effects"),FX.IsValid());
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusOffenseRework,"ElyGambit.OffenseRework.MobiusElevenWindow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusOffenseRework::RunTest(const FString&)
{
 for(int32 Level:{1,6,10,11})
 {
  FFlameWorld E(*this);const auto Id=E.Deploy(TEXT("Mobius"),TEXT("Mobius"),2);if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();
  auto* U=E.Find(Id);auto* S=U->GetHeroSkill();S->ApplyPersistentLevel(Level);auto* B=Cast<UEGMobiusSkillBehavior>(S->GetBehavior());
  auto* T=E.Enemies()[0];T->SetActorLocation(U->GetActorLocation()+FVector(600,0,0));
  if(!TestTrue(TEXT("Mobius ultimate starts"),E.Cast(U)))return false;S->AnimationEvent(TEXT("Release"));
  TestEqual(TEXT("Enhancement unlocks at eleven"),B->EmpoweredActive,Level>=11);
  if(Level>=11)
  {
   TestEqual(TEXT("Six seconds begins at release"),B->EmpoweredRemaining,6.f);
   B->TickBattle(1.f);S->AnimationEvent(TEXT("Finish"));TestEqual(TEXT("Finish does not restart window"),B->EmpoweredRemaining,5.f);
   B->TickBattle(5.f);TestFalse(TEXT("Window expires"),B->EmpoweredActive);
  }
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVOffenseRework,"ElyGambit.OffenseRework.VillVAutomaticFire",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVOffenseRework::RunTest(const FString&)
{
 FFlameWorld E(*this);const auto Id=E.Deploy(TEXT("VillV"),TEXT("VillV"),2);if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Find(Id);auto* S=U->GetHeroSkill();auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();if(!M)return false;
 for(int32 I=0;I<240&&(!M->IsOperating()||S->IsActionRunning());++I)E.Tick(.05f);
 if(!TestTrue(TEXT("Pilot operating"),M->IsOperating()))return false;
 for(auto* T:E.Enemies())T->SetActorLocation(FVector(15000,15000,88));
 auto* T=E.Enemies()[0];auto* Device=M->GetDevice(1-M->Mode());T->SetActorLocation(Device->GetActorLocation()+FVector(800,0,0));Device->SetActorRotation(FRotator::ZeroRotator);
 S->ApplyPersistentLevel(10);M->BeginAutoFire(FGuid::NewGuid());TestEqual(TEXT("Level ten cannot automate"),M->GetAutoFireRemaining(),0.f);
 auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>();if(!TestNotNull(TEXT("Auto fire audio component"),Audio))return false;
 const auto* AudioConfig=U->GetDefinition()->GetCombatAudioPresentation();if(!AudioConfig)return false;
 for(const auto& Entry:AudioConfig->AudioEvents)for(const auto& Sound:Entry.Sounds)Sound.LoadSynchronous();
 const int32 AudioBefore=Audio->EventCount;
 S->ApplyPersistentLevel(11);const FGuid Action=FGuid::NewGuid();M->BeginAutoFire(Action);
 const float Before=T->GetAttributes()->GetHealth(),Mana=U->GetAttributes()->GetMana();const int32 Attacks=E.GM->BattleRules->GetAttackCount();
 E.Tick(2.f);TestTrue(TEXT("Unoccupied original gun fires"),T->GetAttributes()->GetHealth()<Before);
 TestTrue(TEXT("Unoccupied device emits original shot sound"),Audio->EventCount>AudioBefore);
 TestTrue(TEXT("Unoccupied device uses finite mechanical cue"),Audio->LastNode==TEXT("AutoCannonShot")||Audio->LastNode==TEXT("AutoGunShot"));
 TestEqual(TEXT("Automatic gun generates no owner mana"),U->GetAttributes()->GetMana(),Mana);
 TestEqual(TEXT("Automatic gun is not another hero basic attack"),E.GM->BattleRules->GetAttackCount(),Attacks);
 const float Remaining=M->GetAutoFireRemaining();M->BeginAutoFire(Action);TestEqual(TEXT("Same release cannot refresh window"),M->GetAutoFireRemaining(),Remaining);
 E.Tick(4.1f);const float End=T->GetAttributes()->GetHealth();E.Tick(2.f);TestEqual(TEXT("No attacks after six seconds"),T->GetAttributes()->GetHealth(),End);
 M->BeginAutoFire(FGuid::NewGuid());Device->Die();E.Tick(.1f);TestEqual(TEXT("Destroyed device stops automatic fire"),M->GetAutoFireRemaining(),0.f);
 E.GM->BattleRules->Stop();TestEqual(TEXT("Round stop clears automatic state"),M->GetAutoFireRemaining(),0.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVAutomaticAttackRecovery,
 "ElyGambit.VillVCombat.AutomaticAttackRecovery",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVAutomaticAttackRecovery::RunTest(const FString&)
{
 // Controlled durability and target positions keep the fight observable; the
 // pilot's AI, mana, normal action selection and animation notifications are
 // not injected or frozen. This is a behavior check, not a balance match.
 for(int32 Scenario=0;Scenario<3;++Scenario)
 {
  const int32 Row=Scenario==1?0:2;const bool bCrowded=Scenario==0;
  FFlameWorld E(*this);
  if(!TestNotNull(TEXT("Fixture cards"),E.Cards)||!TestNotNull(TEXT("Fixture mode"),E.GM))return false;
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
  if(!TestNotNull(TEXT("Formal VillV card"),Card)||!E.Cards->GrantCard(Card)||
     !TestTrue(TEXT("Deploy at selected front/back row"),E.Cards->TryDeployHandCard(E.Cards->GetHand().Last().InstanceId,{3,Row})))return false;
  const FGuid Id=E.GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
  FEGHeroInstance Layout;
  if(!TestTrue(TEXT("VillV deployment layout exists"),E.GM->GetRoster()->GetHero(Id,Layout)&&Layout.bVillVLayout))return false;
  AddInfo(FString::Printf(TEXT("VillV scenario=%d pilot=(%d,%d) gun=(%d,%d) cannon=(%d,%d)"),Scenario,
   Layout.DeploymentCell.X,Layout.DeploymentCell.Y,Layout.GatlingCell.X,Layout.GatlingCell.Y,Layout.CannonCell.X,Layout.CannonCell.Y));
  if(bCrowded)
  {
   // Explicit population is a fixture only; keep three stationary allies near
   // the entrance while leaving the pilot's movement and combat AI enabled.
   E.Cards->SetPopulationFloor(7);
   const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"),
    TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden"),TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura")};
   for(const TCHAR* Path:Paths)
   {
    auto* AllyCard=LoadObject<UEGCardDefinition>(nullptr,Path);
    if(!TestNotNull(TEXT("Crowded formation ally"),AllyCard)||!E.Cards->GrantCard(AllyCard))return false;
    const FGuid CardId=E.Cards->GetHand().Last().InstanceId;
    TArray<FIntPoint> Cells;
    for(int32 Y=0;Y<E.Grid->PlayerRows;++Y)for(int32 X=0;X<E.Grid->Columns;++X)
     if(E.Cards->EvaluateHandPlacement(CardId,{X,Y}).IsAllowed())Cells.Add({X,Y});
    Cells.Sort([&](FIntPoint A,FIntPoint B)
    {return (A-Layout.GatlingCell).SizeSquared()<(B-Layout.GatlingCell).SizeSquared();});
    if(!TestTrue(TEXT("A nearby ally can be legally deployed"),!Cells.IsEmpty())||
       !TestTrue(TEXT("Deploy crowded ally"),E.Cards->TryDeployHandCard(CardId,Cells[0])))return false;
    AddInfo(FString::Printf(TEXT("VillV crowded ally=%s cell=(%d,%d)"),Path,Cells[0].X,Cells[0].Y));
   }
  }
  E.GM->RunDefinition->BattleTimeLimit=120.f;
  if(!TestTrue(TEXT("Enter combat with real pilot AI"),Id.IsValid()&&E.GM->EnterCombat()))return false;
  auto* U=E.Find(Id);auto Enemies=E.Enemies();
  if(!TestNotNull(TEXT("Pilot"),U)||!TestTrue(TEXT("Two target fixture"),Enemies.Num()>=2))return false;
  auto* M=U->FindComponentByClass<UEGVillVMechanismComponent>();
  if(!TestNotNull(TEXT("Device coordinator"),M))return false;
  if(bCrowded)for(auto* Other:E.GM->GetCombatUnits())
   if(Other&&Other!=U&&!Other->IsAuxiliaryEmplacement()&&IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player)
    Other->GetCombat()->SetComponentTickEnabled(false);
  for(auto* Enemy:Enemies)
  {
   Enemy->GetCombat()->SetComponentTickEnabled(false);
   Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
   Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
   Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   Enemy->SetActorLocation(FVector(15000,15000,88),false);
  }
  auto DumpEntry=[&](int32 Frame)
  {
   auto* Gun=M->GetDevice(0);auto* Cannon=M->GetDevice(1);
   const auto* Rules=Cast<UEGVillVSkillDefinition>(U->GetDefinition()->SkillDefinition);
   const FVector Offset=Rules&&Rules->EntryApproachOffsets.IsValidIndex(0)?Rules->EntryApproachOffsets[0]:FVector(-220,0,0);
   const FVector Approx=Gun?Gun->GetActorLocation()+Gun->GetActorRotation().RotateVector(Offset):FVector::ZeroVector;
   AddInfo(FString::Printf(TEXT("Entry scenario=%d elapsed=%.2f phase=%d mode=%d pilot=%s gun=%s cannon=%s approximate_goal=%s travel_speed=%.1f runtime_speed=%.1f"),
    Scenario,Frame*.05f,int32(M->Phase),M->Mode(),*U->GetActorLocation().ToString(),
    Gun?*Gun->GetActorLocation().ToString():TEXT("missing"),Cannon?*Cannon->GetActorLocation().ToString():TEXT("missing"),
    *Approx.ToString(),Rules?Rules->TravelSpeed:-1.f,U->GetAttributes()->GetMoveSpeed()));
   for(auto* Other:E.GM->GetCombatUnits())if(IsValid(Other)&&Other!=U&&IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player)
    AddInfo(FString::Printf(TEXT("Entry nearby=%s auxiliary=%d location=%s distance=%.1f"),*GetNameSafe(Other),Other->IsAuxiliaryEmplacement()?1:0,
     *Other->GetActorLocation().ToString(),FVector::Dist2D(Other->GetActorLocation(),U->GetActorLocation())));
  };
  EEGVillVPhase LastPhase=M->Phase;int32 EntryFrames=0;
  for(;EntryFrames<600&&(!M->IsOperating()||M->Mode()!=0);++EntryFrames)
  {
   E.Tick(.05f);
   if(M->Phase!=LastPhase||EntryFrames%100==0){DumpEntry(EntryFrames+1);LastPhase=M->Phase;}
  }
  DumpEntry(EntryFrames);
  if(!M->IsOperating()&&M->GetDevice(0))
  {
   // Diagnose the existing 16 candidate points without changing the actor or
   // invoking private path logic. A free but unreachable nearest point is
   // distinguished from every possible point being blocked.
   auto* Gun=M->GetDevice(0);const auto* Rules=Cast<UEGVillVSkillDefinition>(U->GetDefinition()->SkillDefinition);
   const FVector Offset=Rules&&Rules->EntryApproachOffsets.IsValidIndex(0)?Rules->EntryApproachOffsets[0]:FVector(-220,0,0);
   const FVector Start=U->GetActorLocation();const float Preferred=(Start-Gun->GetActorLocation()).Rotation().Yaw-Offset.Rotation().Yaw;
   float Nearest=MAX_flt;int32 NearestIndex=INDEX_NONE;bool bNearestReachable=false,bAnyReachable=false;
   for(int32 Index=0;Index<16;++Index)
   {
    FVector Point=Gun->GetActorLocation()+FRotator(0,Preferred+Index*22.5f,0).RotateVector(Offset);Point.Z=Start.Z;
    FIntPoint Cell;const FVector Local=E.Grid->GetActorTransform().InverseTransformPosition(Point);
    if(!E.Grid->TryWorldToCell(Point,Cell)||FMath::Abs(Local.X)>E.Grid->Columns*E.Grid->CellWidth*.5f-60||
       FMath::Abs(Local.Y)>E.Grid->Rows*E.Grid->CellDepth*.5f-60||!U->GetCombat()->IsBoardLocationFree(Point,Gun))continue;
    TArray<FVector> Route;FVector Destination;const bool bReachable=U->GetCombat()->PlanBoardRoute({Point},Route,Destination,Gun);
    const float Distance=FVector::DistSquared2D(Point,Start);bAnyReachable|=bReachable;
    if(Distance<Nearest){Nearest=Distance;NearestIndex=Index;bNearestReachable=bReachable;}
    AddInfo(FString::Printf(TEXT("Entry candidate=%d point=%s distance=%.1f reachable=%d"),Index,*Point.ToString(),FMath::Sqrt(Distance),bReachable?1:0));
   }
   AddInfo(FString::Printf(TEXT("Entry nearest=%d nearest_reachable=%d any_reachable=%d"),NearestIndex,bNearestReachable?1:0,bAnyReachable?1:0));
  }
  if(!TestTrue(*FString::Printf(TEXT("Row %d reaches gatling naturally"),Row),M->IsOperating()&&M->Mode()==0))return false;
  auto* A=Enemies[0];auto* B=Enemies[1];
  TArray<FVector> Positions;
  const float TargetRadius=UEGCombatComponent::BlockingRadius(A);
  const float Range=U->GetCombat()->GetEffectiveAttackRange(A);
  for(int32 Y=0;Y<E.Grid->Rows;++Y)for(int32 X=0;X<E.Grid->Columns;++X)
  for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
  {
   FVector P=E.Grid->CellToWorld({X,Y})+E.Grid->GetActorTransform().TransformVector(
    FVector(DX*E.Grid->CellWidth*.25f,DY*E.Grid->CellDepth*.25f,0));P.Z=A->GetActorLocation().Z;
   if(!A->GetCombat()->IsBoardLocationFree(P))continue;
   bool bValid=true;
   for(int32 Index=0;Index<2;++Index)
   {
    auto* Device=M->GetDevice(Index);if(!Device){bValid=false;break;}
    const float Distance=FVector::Dist2D(P,Device->GetActorLocation());
    if(Distance<=UEGCombatComponent::BlockingRadius(Device)+TargetRadius+10.f||Distance>Range-.1f){bValid=false;break;}
   }
   if(bValid)Positions.Add(P);
  }
  if(!TestTrue(TEXT("Both seats have an unobstructed target point"),!Positions.IsEmpty()))return false;
  const FVector First=Positions[0];FVector Second=First;float LowestDot=1.f;
  const FVector Origin=M->GetDevice(0)->GetActorLocation();
  const FVector Direction=(First-Origin).GetSafeNormal2D();
  for(const FVector& P:Positions)
  {
   const float Dot=FVector::DotProduct(Direction,(P-Origin).GetSafeNormal2D());
   if(Dot<LowestDot){LowestDot=Dot;Second=P;}
  }
  A->SetActorLocation(First,false);
  auto Damage=[&]()
  {
   float Value=0.f;for(const auto& Stat:E.GM->GetCombatStatistics())if(Stat.HeroId==Id)Value+=Stat.Damage;
   return Value;
  };
  auto Diagnostic=[&]()
  {
   const auto* Target=U->GetCombat()->GetTarget();const auto* Fire=M->FiringTarget();
   AddInfo(FString::Printf(TEXT("VillV row=%d phase=%d mode=%d target=%s fire=%s aim=%d action=%d attacks=%d damage=%.2f hpA=%.2f hpB=%.2f device0=%d device1=%d"),
    Row,int32(M->Phase),M->Mode(),*GetNameSafe(Target),*GetNameSafe(Fire),M->IsAimReady()?1:0,
    U->GetHeroSkill()->IsActionRunning()?1:0,E.GM->BattleRules->GetAttackCount(),Damage(),
    A->GetAttributes()->GetHealth(),B->GetAttributes()->GetHealth(),
    IsValid(M->GetDevice(0))&&IEGCombatant::Execute_IsAlive(M->GetDevice(0))?1:0,
    IsValid(M->GetDevice(1))&&IEGCombatant::Execute_IsAlive(M->GetDevice(1))?1:0));
  };
  for(int32 Frame=0;Frame<500&&E.GM->BattleRules->GetAttackCount()==0;++Frame)E.Tick(.05f);
  Diagnostic();
  if(!TestTrue(TEXT("Automatic gatling basic attack deals attributed damage"),
      E.GM->BattleRules->GetAttackCount()>0&&Damage()>0.f&&A->GetAttributes()->GetHealth()<100000.f))return false;
  // Let the regular AI land the lethal hit; no death or firing event is injected.
  A->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1.f);
  for(int32 Frame=0;Frame<300&&IEGCombatant::Execute_IsAlive(A);++Frame)E.Tick(.05f);
  if(!TestFalse(TEXT("Current target dies from an automatic hit"),IEGCombatant::Execute_IsAlive(A))){Diagnostic();return false;}
  B->SetActorLocation(Second,false);
  const float BeforeRetarget=B->GetAttributes()->GetHealth();
  for(int32 Frame=0;Frame<500&&B->GetAttributes()->GetHealth()>=BeforeRetarget;++Frame)E.Tick(.05f);
  Diagnostic();
  if(!TestTrue(TEXT("Pilot automatically reacquires a new target and damages it"),B->GetAttributes()->GetHealth()<BeforeRetarget))return false;
  // A successful basic-hit receipt increments this counter once per volley.
  bool bSwitched=false,bCannonBasic=false;int32 PreviousAttacks=E.GM->BattleRules->GetAttackCount();
  TSet<FGuid> LoggedCloseAllies;bool bDepartureOverlapped=false;
  for(int32 Frame=0;Frame<1600&&!bCannonBasic&&E.GM->BattleRules->IsBattleActive();++Frame)
  {
   E.Tick(.05f);
   if(bCrowded&&Frame<=160)for(auto* Other:E.GM->GetCombatUnits())
    if(Other&&Other!=U&&!Other->IsAuxiliaryEmplacement()&&IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player)
    {
     const float Minimum=UEGCombatComponent::BlockingRadius(U)+UEGCombatComponent::BlockingRadius(Other);
     const float Distance=FVector::Dist2D(U->GetActorLocation(),Other->GetActorLocation());
     if(M->Phase==EEGVillVPhase::Leaving&&Distance<Minimum-1.f)bDepartureOverlapped=true;
     if(Distance<Minimum-1.f&&!LoggedCloseAllies.Contains(Other->GetInstanceId()))
     {
      LoggedCloseAllies.Add(Other->GetInstanceId());
      const auto* Mesh=U->GetVisiblePresentationMesh();const auto* Cannon=M->GetDevice(1);
      AddInfo(FString::Printf(TEXT("First close ally frame=%d elapsed=%.2f phase=%d mode=%d pilot=%s visible_mesh=%s ally=%s ally_pos=%s cannon=%s distance2d=%.2f required2d=%.2f"),
       Frame,Frame*.05f,int32(M->Phase),M->Mode(),*U->GetActorLocation().ToString(),
       Mesh?*Mesh->GetComponentLocation().ToString():TEXT("missing"),*GetNameSafe(Other),*Other->GetActorLocation().ToString(),
       Cannon?*Cannon->GetActorLocation().ToString():TEXT("missing"),Distance,Minimum));
     }
    }
   if(bCrowded&&Frame==160&&!bSwitched)
   {
    // The three fixture allies were deliberately made stationary. Release
    // that synthetic obstruction after observing the blocked traversal.
    for(auto* Other:E.GM->GetCombatUnits())
     if(Other&&Other!=U&&!Other->IsAuxiliaryEmplacement()&&IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player)
     {
      const float Min=UEGCombatComponent::BlockingRadius(U)+UEGCombatComponent::BlockingRadius(Other);
      const float Distance=FVector::Dist2D(U->GetActorLocation(),Other->GetActorLocation());
      if(Distance<Min-1.f)AddInfo(FString::Printf(TEXT("Blocked transfer release check phase=%d mode=%d pilot=%s ally=%s ally_pos=%s distance2d=%.2f required2d=%.2f"),
       int32(M->Phase),M->Mode(),*U->GetActorLocation().ToString(),*GetNameSafe(Other),*Other->GetActorLocation().ToString(),Distance,Min));
      if(M->Phase!=EEGVillVPhase::Jumping)
       TestTrue(TEXT("Grounded blocked traversal does not force pilot through a stationary ally"),Distance>=Min-1.f);
      Other->SetActorLocation(FVector(15000,16000,88),false);
     }
    AddInfo(TEXT("Released static ally obstruction after observing blocked transfer"));
   }
   bSwitched|=M->IsOperating()&&M->Mode()==1;
   const int32 CurrentAttacks=E.GM->BattleRules->GetAttackCount();
   if(bSwitched&&M->Mode()==1&&CurrentAttacks>PreviousAttacks)bCannonBasic=true;
   PreviousAttacks=CurrentAttacks;
  }
  Diagnostic();
  if(bCrowded)TestFalse(TEXT("Grounded departure never overlaps a stationary ally"),bDepartureOverlapped);
  TestTrue(TEXT("Full mana naturally switches to cannon"),bSwitched);
  TestTrue(TEXT("Cannon automatically lands another basic after switch"),bCannonBasic);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVFriendlyDeviceOverlap,
 "ElyGambit.VillVCombat.FriendlyDeviceOverlapRecovery",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVFriendlyDeviceOverlap::RunTest(const FString&)
{
 // Model an already-overlapping board as a controlled setup. Thereafter the
 // released ally and pilot use their own normal combat ticks and movement.
 FFlameWorld E(*this);
 if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;
 const FGuid VillId=E.Deploy(TEXT("VillV"),TEXT("VillV"),3);
 const FGuid AllyAId=E.Deploy(TEXT("Eden"),TEXT("Eden"),0);
 const FGuid AllyBId=E.Deploy(TEXT("Pardofelis"),TEXT("Pardo"),6);
 if(!TestTrue(TEXT("Pilot and two allies deployed"),VillId.IsValid()&&AllyAId.IsValid()&&AllyBId.IsValid()))return false;
 E.GM->RunDefinition->BattleTimeLimit=120.f;
 if(!TestTrue(TEXT("Combat starts"),E.GM->EnterCombat()))return false;
 auto* Pilot=E.Find(VillId);auto* AllyA=E.Find(AllyAId);auto* AllyB=E.Find(AllyBId);
 if(!TestNotNull(TEXT("Pilot"),Pilot)||!TestNotNull(TEXT("First ally"),AllyA)||!TestNotNull(TEXT("Second ally"),AllyB))return false;
 auto* M=Pilot->FindComponentByClass<UEGVillVMechanismComponent>();
 if(!TestNotNull(TEXT("Device coordinator"),M))return false;
 for(auto* Enemy:E.Enemies())
 {
  Enemy->GetCombat()->SetComponentTickEnabled(false);
  Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
  Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
 }
 AllyA->GetCombat()->SetComponentTickEnabled(false);AllyB->GetCombat()->SetComponentTickEnabled(false);
 for(int32 Frame=0;Frame<200&&(!M->GetDevice(0)||!M->GetDevice(1));++Frame)E.Tick(.05f);
 auto* Gun=M->GetDevice(0);auto* Cannon=M->GetDevice(1);
 if(!TestNotNull(TEXT("Gun spawned before entry"),Gun)||!TestNotNull(TEXT("Cannon spawned before entry"),Cannon)||
    !TestFalse(TEXT("Pilot has not yet entered an occupied seat"),M->IsOperating()))return false;
 TestFalse(TEXT("Friendly unit cannot select the gun's occupied location"),
  AllyA->GetCombat()->IsBoardLocationFree(Gun->GetActorLocation(),Pilot));
 // Keep both seats blocked for a finite interval. The driver must not
 // teleport onto either friendly occupant to claim an operating seat.
 AllyA->SetActorLocation(Gun->GetActorLocation(),false);
 AllyB->SetActorLocation(Cannon->GetActorLocation(),false);
 const float MinA=UEGCombatComponent::BlockingRadius(Pilot)+UEGCombatComponent::BlockingRadius(AllyA);
 const float MinB=UEGCombatComponent::BlockingRadius(Pilot)+UEGCombatComponent::BlockingRadius(AllyB);
 bool bPilotCrossed=false;
 for(int32 Frame=0;Frame<100;++Frame)
 {
  E.Tick(.05f);
  bPilotCrossed|=FVector::Dist2D(Pilot->GetActorLocation(),AllyA->GetActorLocation())<MinA-2.f||
   FVector::Dist2D(Pilot->GetActorLocation(),AllyB->GetActorLocation())<MinB-2.f;
 }
 AddInfo(FString::Printf(TEXT("Both occupied: phase=%d pilot=%s allyA=%s allyB=%s gun=%s cannon=%s"),
  int32(M->Phase),*Pilot->GetActorLocation().ToString(),*AllyA->GetActorLocation().ToString(),
  *AllyB->GetActorLocation().ToString(),*Gun->GetActorLocation().ToString(),*Cannon->GetActorLocation().ToString()));
 TestFalse(TEXT("Both blocked seats cannot be forcefully occupied"),M->IsOperating());
 TestFalse(TEXT("Pilot does not pass through a stationary ally"),bPilotCrossed);
 // Release one ally's real AI. It must leave its existing overlap by normal
 // movement at a speed bounded by its own movement attribute.
 AllyA->GetCombat()->SetComponentTickEnabled(true);
 const float Required=UEGCombatComponent::BlockingRadius(AllyA)+UEGCombatComponent::BlockingRadius(Gun);
 FVector Previous=AllyA->GetActorLocation();bool bOverspeed=false,bSeparated=false,bDocked=false;
 for(int32 Frame=0;Frame<800&&(!bSeparated||!bDocked);++Frame)
 {
  E.Tick(.05f);const FVector Current=AllyA->GetActorLocation();
  const float Step=FVector::Dist2D(Previous,Current);
  bOverspeed|=Step>FMath::Max(0.f,AllyA->GetAttributes()->GetMoveSpeed())*.05f+2.f;
  Previous=Current;
  bSeparated|=FVector::Dist2D(Current,Gun->GetActorLocation())>=Required-1.f;
  bDocked|=M->IsOperating()&&M->Mode()==0;
 }
 AddInfo(FString::Printf(TEXT("After ally release: phase=%d mode=%d allyA=%s gun=%s allyB=%s separated=%d docked=%d overspeed=%d"),
  int32(M->Phase),M->Mode(),*AllyA->GetActorLocation().ToString(),*Gun->GetActorLocation().ToString(),
  *AllyB->GetActorLocation().ToString(),bSeparated?1:0,bDocked?1:0,bOverspeed?1:0));
 TestTrue(TEXT("Existing overlapping ally walks out under its own AI"),bSeparated);
 TestFalse(TEXT("Overlap recovery never teleports the ally"),bOverspeed);
 TestTrue(TEXT("Pilot enters the released gun seat"),bDocked);
 auto VillDamage=[&]()
 {float Total=0.f;for(const auto& Stat:E.GM->GetCombatStatistics())if(Stat.HeroId==VillId)Total+=Stat.Damage;return Total;};
 for(int32 Frame=0;Frame<500&&VillDamage()<=0.f&&E.GM->BattleRules->IsBattleActive();++Frame)E.Tick(.05f);
 TestTrue(TEXT("Pilot attacks after safe overlap recovery"),VillDamage()>0.f);
 return true;
}

#endif
