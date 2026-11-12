#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"

namespace
{
struct FSakuraColdIaiWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* W=nullptr;
 AEGDemoGameMode* GM=nullptr;
 AEGCardManager* Cards=nullptr;
 AEGChessUnit* Sakura=nullptr;
 TArray<AEGChessUnit*> Enemies;

 explicit FSakuraColdIaiWorld(FAutomationTestBase& Test,bool bRoundShop=true):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();
  W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();
  if(!GM)return;
  GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);if(!Base)return;
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);
  if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return;
  Run->bRoundShopRules=bRoundShop;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
  Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  W->InitializeActorsForPlay(URL);W->BeginPlay();
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"));
  if(!Card||!Card->HeroDefinition||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2}))return;
  const FGuid Id=GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
  if(!Id.IsValid()||Run->Stages.IsEmpty()||Run->Stages[0].Enemies.IsEmpty())return;
  auto* Board=GM->GetBoard();auto* EnemyDefinition=Run->Stages[0].Enemies[0].Definition.Get();
  if(!Board||!EnemyDefinition)return;
  auto EnemyCount=[this](){int32 Count=0;for(auto* U:GM->GetCombatUnits())if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)++Count;return Count;};
  for(int32 Y=0;Y<Board->Dimensions.Y&&EnemyCount()<4;++Y)
   for(int32 X=0;X<Board->Dimensions.X&&EnemyCount()<4;++X)
    if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))GM->SpawnEnemy(EnemyDefinition,1,{X,Y});
  if(!GM->EnterCombat())return;
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())
  {
   if(U->GetInstanceId()==Id)Sakura=U;
   if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemies.Add(U);
   U->GetCombat()->SetComponentTickEnabled(false);
   auto* ASC=U->GetAbilitySystemComponent();
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  }
 }
 ~FSakuraColdIaiWorld()
 {
  if(!W)return;
  W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 }
 bool Ready() const {return Sakura&&Enemies.Num()>=4&&Sakura->GetHeroSkill()&&Sakura->GetHeroSkill()->Battle.IsValid();}
 void ResetEnemies()
 {
  for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Sakura->GetActorLocation()+FVector(10000.f+I*300.f,0,0));
 }
 bool Cast()
 {
  auto* Skill=Sakura->GetHeroSkill();Skill->CancelAction();
  Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Sakura->GetAttributes()->GetMaxMana());
  return Skill->BeginAction(true);
 }
 int32 Count(const UEGSourceEffectDefinition* Definition) const
 {
  int32 N=0;for(TActorIterator<AEGSourceEffect> It(W);It;++It)
   if(It->GetOwner()==Sakura&&It->GetSourceDefinition()==Definition&&!It->IsActorBeingDestroyed())++N;
  return N;
 }
 AEGSourceEffect* Find(const UEGSourceEffectDefinition* Definition) const
 {
  for(TActorIterator<AEGSourceEffect> It(W);It;++It)
   if(It->GetOwner()==Sakura&&It->GetSourceDefinition()==Definition&&!It->IsActorBeingDestroyed())return *It;
  return nullptr;
 }
};

UEGSourceEffectDefinition* MakeSakuraProbe(UStaticMesh* Mesh,UMaterialInterface* Material,bool Ground=false)
{
 auto* Effect=NewObject<UEGSourceEffectDefinition>();Effect->bTargetGround=Ground;
 FEGSourceEffectTrack Track;Track.Mesh=Mesh;Track.Material=Material;Track.Duration=10.f;
 Track.Samples.Add(FEGSourceEffectSample());Effect->Tracks.Add(Track);return Effect;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraColdIaiBasic,"ElyGambit.SakuraColdIai.BasicHitAndSequence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraColdIaiBasic::RunTest(const FString&)
{
 FSakuraColdIaiWorld E(*this);if(!TestTrue(TEXT("Formal Sakura and four enemies enter combat"),E.Ready()))return false;
 auto* D=Cast<UEGSakuraSkillDefinition>(E.Sakura->GetDefinition()->SkillDefinition);
 auto* Presentation=E.Sakura->GetDefinition()->PresentationDefinition.Get();
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 auto* Legacy=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Sakura/CombatPresentation20261030/NS_Sakura_Strike_Private.NS_Sakura_Strike_Private"));
 if(!TestTrue(TEXT("Skill, five attack presentations, and probe resources exist"),D&&Presentation&&Presentation->AttackSequence.Num()==5&&Mesh&&Material&&Legacy))return false;
 TStrongObjectPtr<UEGSourceEffectDefinition> Hit(MakeSakuraProbe(Mesh,Material));
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardHit(D->BasicHitPresentation,Hit.Get());
 TGuardValue<TMap<FName,FEGChessFXCue>> GuardCues(D->ChessFX,D->ChessFX);
 FEGChessFXCue LegacyCue;LegacyCue.System=Legacy;LegacyCue.Duration=2.f;D->ChessFX.Add(TEXT("Strike"),LegacyCue);
 const auto LegacyCount=[&](){int32 N=0;for(TActorIterator<AEGChessNiagaraBurst> It(E.W);It;++It)
  if(It->GetOwner()==E.Sakura&&!It->IsActorBeingDestroyed())if(const auto* FX=It->FindComponentByClass<UNiagaraComponent>();FX&&FX->GetAsset()==Legacy)++N;return N;};
 E.ResetEnemies();auto* Target=E.Enemies[0];Target->SetActorLocation(E.Sakura->GetActorLocation()+FVector(80,0,0));
 auto* Skill=E.Sakura->GetHeroSkill();auto* Anim=E.Sakura->GetVisiblePresentationMesh()->GetAnimInstance();
 if(!TestNotNull(TEXT("Combat animation instance"),Anim))return false;
 float FirstRate=0.f;
 for(int32 I=0;I<6;++I)
 {
  const float Speed=I==5?2.f:1.f;
  E.Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Speed);
  if(!TestTrue(TEXT("Attack starts"),Skill->BeginAction(false)))return false;
  TestEqual(TEXT("The selected montage follows the five-stage sequence"),Skill->GetActiveMontage(),Presentation->AttackSequence[I%5].Montage.LoadSynchronous());
  const float Rate=Anim->Montage_GetPlayRate(Skill->GetActiveMontage());
  if(I==0)FirstRate=Rate;
  if(I==5)TestTrue(TEXT("The same stroke plays twice as fast with double attack speed"),FMath::IsNearlyEqual(Rate,FirstRate*2.f,.01f));
  TestEqual(TEXT("No contact accent appears before a hit"),E.Count(Hit.Get()),0);
  const float Before=Target->GetAttributes()->GetHealth();
  const int32 LegacyBefore=LegacyCount();
  Skill->AnimationEvent(TEXT("Hit"));
  TestTrue(TEXT("Confirmed basic hit damages its target"),Target->GetAttributes()->GetHealth()<Before);
  TestEqual(TEXT("Confirmed basic hit creates one accent"),E.Count(Hit.Get()),1);
  TestEqual(TEXT("Configured impact does not duplicate its legacy Niagara"),LegacyCount(),LegacyBefore);
  Skill->AnimationEvent(TEXT("Hit"));
  TestEqual(TEXT("Repeated hit notification does not duplicate the accent"),E.Count(Hit.Get()),1);
  Skill->CancelAction();
  TestEqual(TEXT("Cancelled attack clears its contact accent"),E.Count(Hit.Get()),0);
 }
 if(!TestTrue(TEXT("Attack starts before target becomes invulnerable"),Skill->BeginAction(false)))return false;
 Target->GetHeroSkill()->SetActionProtected(true);
 Skill->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Rejected hit has no contact accent"),E.Count(Hit.Get()),0);
 Skill->CancelAction();Target->GetHeroSkill()->SetActionProtected(false);
 if(!TestTrue(TEXT("Attack starts for death cleanup"),Skill->BeginAction(false)))return false;
 Skill->AnimationEvent(TEXT("Hit"));
 if(!TestEqual(TEXT("Hit accent exists before caster death"),E.Count(Hit.Get()),1))return false;
 E.Sakura->Die();
 TestEqual(TEXT("Caster death clears its contact accent"),E.Count(Hit.Get()),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraColdIaiUltimate,"ElyGambit.SakuraColdIai.UltimateReceiptsAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraColdIaiUltimate::RunTest(const FString&)
{
 FSakuraColdIaiWorld E(*this);if(!TestTrue(TEXT("Formal Sakura and four enemies enter combat"),E.Ready()))return false;
 auto* D=Cast<UEGSakuraSkillDefinition>(E.Sakura->GetDefinition()->SkillDefinition);
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 if(!TestTrue(TEXT("Skill and probe resources exist"),D&&D->HasValidSkillRules()&&Mesh&&Material))return false;
 TStrongObjectPtr<UEGSourceEffectDefinition> Fan(MakeSakuraProbe(Mesh,Material,true));
 TStrongObjectPtr<UEGSourceEffectDefinition> Release(MakeSakuraProbe(Mesh,Material));
 TStrongObjectPtr<UEGSourceEffectDefinition> Normal(MakeSakuraProbe(Mesh,Material));
 TStrongObjectPtr<UEGSourceEffectDefinition> Execute(MakeSakuraProbe(Mesh,Material));
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardFan(D->AreaPresentation,Fan.Get());
 TGuardValue<float> GuardReferenceRadius(D->AreaPresentationReferenceRadius,100.f);
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardRelease(D->SlashReleasePresentation,Release.Get());
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardNormal(D->NormalImpactPresentation,Normal.Get());
 // Explicitly isolate the fallback hook; the formal receiver has its own presentation tests.
 TGuardValue<TSoftObjectPtr<UEGSakuraReworkPresentationConfig>> GuardReceiver(D->ReworkPresentation,{});
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardExecute(D->ExecutionImpactPresentation,Execute.Get());
 E.ResetEnemies();E.Sakura->SetActorRotation(FRotator::ZeroRotator);
 const FVector Origin=E.Sakura->GetActorLocation();
 auto* Ordinary=E.Enemies[0];auto* Executed=E.Enemies[1];auto* Protected=E.Enemies[2];auto* Outside=E.Enemies[3];
 Ordinary->SetActorLocation(Origin+FVector(100,0,0));Executed->SetActorLocation(Origin+FVector(150,0,0));
 Protected->SetActorLocation(Origin+FVector(200,0,0));Outside->SetActorLocation(Origin+FVector(D->UltimateRadius+50.f,0,0));
 Executed->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f*D->ExecuteHealthFraction);
 Protected->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 Protected->GetHeroSkill()->SetActionProtected(true);
 auto* Skill=E.Sakura->GetHeroSkill();
 if(!TestTrue(TEXT("Ultimate starts"),E.Cast()))return false;
 Skill->AnimationEvent(TEXT("FanStart"));Skill->AnimationEvent(TEXT("FanStart"));
 if(!TestEqual(TEXT("FanStart creates exactly one sector"),E.Count(Fan.Get()),1))return false;
 auto* Area=E.Find(Fan.Get());
 TestTrue(TEXT("Sector uses the locked origin and radius"),Area&&Area->GetActorLocation().Equals(Origin-FVector(0,0,85),.1f)&&Area->GetActorScale3D().Equals(FVector(D->UltimateRadius/100.f),.01f));
 TestTrue(TEXT("Sector uses locked facing"),Area&&Area->GetActorForwardVector().Equals(FVector::ForwardVector,.001f));
 TestEqual(TEXT("FanStart has not yet created an impact"),E.Count(Normal.Get())+E.Count(Execute.Get())+E.Count(Release.Get()),0);
 E.Sakura->SetActorLocation(Origin+FVector(1000,1000,0));E.Sakura->SetActorRotation(FRotator(0,180,0));
 TestTrue(TEXT("Moving the caster does not move the sector"),Area&&Area->GetActorLocation().Equals(Origin-FVector(0,0,85),.1f));
 Skill->AnimationEvent(TEXT("Slash"));
 TestEqual(TEXT("Marked ordinary target is not damaged before the delayed slash"),Ordinary->GetAttributes()->GetHealth(),10000.f);
 TestTrue(TEXT("Marked execution target survives before the delayed slash"),IEGCombatant::Execute_IsAlive(Executed));
 Skill->GetBehavior()->TickBattle(.4f);
 TestTrue(TEXT("Ordinary target receives damage"),Ordinary->GetAttributes()->GetHealth()<10000.f);
 TestTrue(TEXT("Threshold target receives confirmed execution"),Executed->LastCombatResolution.bExecution&&Executed->LastCombatResolution.HealthDelta<0.f);
 TestEqual(TEXT("Protected target receives no damage"),Protected->GetAttributes()->GetHealth(),1000.f);
 TestEqual(TEXT("Outside target receives no damage"),Outside->GetAttributes()->GetHealth(),10000.f);
 TestEqual(TEXT("One release flash occurs on Slash"),E.Count(Release.Get()),1);
 TestEqual(TEXT("Only ordinary successful hit receives normal accent"),E.Count(Normal.Get()),1);
 TestEqual(TEXT("Only actual execution receives execution accent"),E.Count(Execute.Get()),1);
 auto* ReleaseActor=E.Find(Release.Get());
 TestTrue(TEXT("Release remains at the cast origin and facing"),ReleaseActor&&ReleaseActor->GetActorLocation().Equals(Origin-FVector(0,0,88),.1f)&&ReleaseActor->GetActorForwardVector().Equals(FVector::ForwardVector,.001f));
 Skill->AnimationEvent(TEXT("Slash"));
 TestEqual(TEXT("Repeated Slash does not duplicate presentation"),E.Count(Release.Get())+E.Count(Normal.Get())+E.Count(Execute.Get()),3);
 Skill->CancelAction();
 TestEqual(TEXT("Cancellation clears the sector and all action accents"),E.Count(Fan.Get())+E.Count(Release.Get())+E.Count(Normal.Get())+E.Count(Execute.Get()),0);
 E.Sakura->SetActorLocation(Origin);E.Sakura->SetActorRotation(FRotator::ZeroRotator);
 if(!TestTrue(TEXT("Next ultimate starts"),E.Cast()))return false;
 Skill->AnimationEvent(TEXT("FanStart"));Skill->AnimationEvent(TEXT("Slash"));
 if(!TestTrue(TEXT("Next cast produces action presentation"),E.Count(Fan.Get())>0&&E.Count(Release.Get())>0))return false;
 E.GM->BattleRules->Stop();
 TestEqual(TEXT("Battle stop clears this cast's effects"),E.Count(Fan.Get())+E.Count(Release.Get())+E.Count(Normal.Get())+E.Count(Execute.Get()),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraColdIaiFinalExecution,"ElyGambit.SakuraColdIai.FinalExecutionTail",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraColdIaiFinalExecution::RunTest(const FString&)
{
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 if(!TestTrue(TEXT("Tail probe resources exist"),Mesh&&Material))return false;
 for(int32 Scenario=0;Scenario<3;++Scenario)
 {
  // The non-shop route retains the winning combat incarnation after resolution.
  FSakuraColdIaiWorld E(*this,false);if(!TestTrue(TEXT("Final-target combat fixture"),E.Ready()))return false;
  auto* D=Cast<UEGSakuraSkillDefinition>(E.Sakura->GetDefinition()->SkillDefinition);
  if(!TestNotNull(TEXT("Sakura skill"),D))return false;
  TStrongObjectPtr<UEGSourceEffectDefinition> Execute(MakeSakuraProbe(Mesh,Material));
  Execute->Tracks[0].Duration=.3f;
  TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardExecute(D->ExecutionImpactPresentation,Execute.Get());
  E.ResetEnemies();E.Sakura->SetActorRotation(FRotator::ZeroRotator);
  for(int32 I=1;I<E.Enemies.Num();++I)E.Enemies[I]->Die();
  auto* Target=E.Enemies[0];Target->SetActorLocation(E.Sakura->GetActorLocation()+FVector(100,0,0));
  Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
  if(!TestTrue(TEXT("Cast against final target"),E.Cast()))return false;
  E.Sakura->GetHeroSkill()->AnimationEvent(TEXT("Slash"));
  E.Sakura->GetHeroSkill()->GetBehavior()->TickBattle(.4f);
  if(!TestTrue(TEXT("Execution is confirmed and produces a tail"),Target->LastCombatResolution.bExecution&&E.Count(Execute.Get())==1))return false;
  TWeakObjectPtr<AEGSourceEffect> Tail=E.Find(Execute.Get());
  auto* Battle=E.Sakura->GetHeroSkill()->Battle.Get();
  if(!TestTrue(TEXT("Victory resolves while effect is active"),E.GM->ResolveCombat(true))||
     !TestTrue(TEXT("Victory stops the battle"),Battle&&!Battle->IsBattleActive())||
     !TestTrue(TEXT("Victory retains the execution tail"),Tail.IsValid()&&!Tail->IsActorBeingDestroyed()))return false;
  if(Scenario==0)
  {
   const float Rate=Tail->GetPlaybackRate();
   if(!TestTrue(TEXT("Tail playback rate is finite and positive"),FMath::IsFinite(Rate)&&Rate>=1.f))return false;
   Tail->Tick(.1f/Rate);
   TestTrue(TEXT("Tail remains for its short authored interval"),Tail.IsValid()&&!Tail->IsActorBeingDestroyed());
   if(Tail.IsValid())Tail->Tick(.25f/Rate);
   TestTrue(TEXT("Tail ends at its authored duration"),!Tail.IsValid()||Tail->IsActorBeingDestroyed());
  }
  else if(Scenario==1)
  {
   E.Sakura->Die();if(Tail.IsValid())Tail->Tick(.01f);
   TestTrue(TEXT("Caster death clears the retained tail"),!Tail.IsValid()||Tail->IsActorBeingDestroyed());
  }
  else
  {
   Battle->Start(TArray<FEGHeroInstance>());if(Tail.IsValid())Tail->Tick(.01f);
   TestTrue(TEXT("A new round clears the previous tail"),!Tail.IsValid()||Tail->IsActorBeingDestroyed());
  }
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraOffenseRework,"ElyGambit.OffenseRework.SakuraTiersAndElite",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraOffenseRework::RunTest(const FString&)
{
 for(int32 Level:{1,6,10,11})
 {
  FSakuraColdIaiWorld E(*this);if(!TestTrue(TEXT("Sakura fixture"),E.Ready()))return false;E.ResetEnemies();
  auto* U=E.Sakura;auto* S=U->GetHeroSkill();S->ApplyPersistentLevel(Level);U->SetActorRotation(FRotator::ZeroRotator);
  auto* T=E.Enemies[0];T->SetActorLocation(U->GetActorLocation()+FVector(150,0,0));
  auto* D=const_cast<UEGHeroDefinition*>(T->GetDefinition());
  const float Attack=U->GetAttributes()->GetAttackPower();
  {TGuardValue<bool> Elite(D->bEliteEnemy,true);T->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),2000.f);
   if(!TestTrue(TEXT("Elite test cast"),E.Cast()))return false;S->AnimationEvent(TEXT("Slash"));S->GetBehavior()->TickBattle(.4f);
   TestFalse(TEXT("Elite does not receive execution"),T->LastCombatResolution.bExecution);
   TestTrue(TEXT("Elite receives ordinary tier damage"),FMath::IsNearlyEqual(2000.f-T->GetAttributes()->GetHealth(),Attack*(Level>=6?4.5f:3.f),.05f));}
  S->CancelAction();T->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100.f);
  auto* T2=E.Enemies[1];T2->SetActorLocation(U->GetActorLocation()+FVector(200,0,0));T2->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100.f);
  if(!E.Cast())return false;S->AnimationEvent(TEXT("Slash"));S->GetBehavior()->TickBattle(.4f);
  TestTrue(TEXT("Two ordinary enemies are executed"),T->LastCombatResolution.bExecution&&T2->LastCombatResolution.bExecution);
  TestEqual(TEXT("Kill refund once and only at eleven"),U->GetAttributes()->GetMana(),Level>=11?30.f:0.f);
  S->AnimationEvent(TEXT("Slash"));S->GetBehavior()->TickBattle(.4f);TestEqual(TEXT("Repeated notify cannot refund twice"),U->GetAttributes()->GetMana(),Level>=11?30.f:0.f);
 }
 return true;
}

#endif
