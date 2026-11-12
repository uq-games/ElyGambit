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
#include "Game/EGMatchManager.h"
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
#include "Components/EGStigmataBattleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Skills/EGSakuraVillVSkillBehaviors.h"
#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGFiveHeroPresentation.h"
#include "Presentation/EGSourceEffect.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include <limits>

namespace
{
// Real production card, run and behavior, with AI frozen only so the test
// controls the exact event ordering and health values.
struct FFiveHeroSakuraWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGChessUnit* Sakura=nullptr;
 TArray<AEGChessUnit*> Enemies;

 explicit FFiveHeroSakuraWorld(FAutomationTestBase& Test,bool bSetsuna=false):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();if(!World)return;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);Mode=World->GetAuthGameMode<AEGDemoGameMode>();if(!Mode)return;
  Mode->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);if(!Base)return;
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,Mode);
  if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return;
  Run->bRoundShopRules=true;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;Mode->RunDefinition=Run;
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=World->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=World->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"));
  if(!Card||!Card->HeroDefinition||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2}))return;
  if(bSetsuna&&(!Mode->GetMatch()||!Mode->GetMatch()->SetStigmataForBalanceTests({TEXT("Setsuna")})))return;
  const FGuid Id=Mode->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
  if(!Id.IsValid()||!Mode->EnterCombat())return;
  Mode->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:Mode->GetCombatUnits())
  {
   if(Unit->GetInstanceId()==Id)Sakura=Unit;
   if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Enemies.Add(Unit);
   Unit->GetCombat()->SetComponentTickEnabled(false);
   auto* ASC=Unit->GetAbilitySystemComponent();
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  }
  if(Sakura)
  {
   Sakura->SetActorRotation(FRotator::ZeroRotator);
   for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Sakura->GetActorLocation()+FVector(15000.f+300.f*I,0,0));
  }
 }
 ~FFiveHeroSakuraWorld()
 {
  if(!World)return;
  World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
 bool Ready() const {return Sakura&&!Enemies.IsEmpty()&&Sakura->GetHeroSkill()&&Sakura->GetHeroSkill()->Battle.IsValid();}
 void Tick(float Seconds)
 {
  for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;World->Tick(LEVELTICK_All,.025f);}
 }
 bool Cast()
 {
  auto* Skill=Sakura->GetHeroSkill();Skill->CancelAction();
  Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Sakura->GetAttributes()->GetMaxMana());
  return Skill->BeginAction(true);
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraDelayedExecution,
 "ElyGambit.FiveHeroRework.Sakura.DelayedExecutionAndSnapshot",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraDelayedExecution::RunTest(const FString&)
{
 FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Formal Sakura enters an active battle"),E.Ready()))return false;
 auto* Target=E.Enemies[0];auto* Def=Cast<UEGSakuraSkillDefinition>(E.Sakura->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Skill has a valid execution rule"),Def&&Def->HasValidSkillRules()))return false;
 Target->SetActorLocation(E.Sakura->GetActorLocation()+FVector(100,0,0));
 const float Threshold=Target->GetAttributes()->GetMaxHealth()*Def->ExecuteHealthFraction;
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Threshold+10.f);
 if(!TestTrue(TEXT("Sakura can begin the area ultimate"),E.Cast()))return false;
 auto* Skill=E.Sakura->GetHeroSkill();Skill->AnimationEvent(TEXT("Slash"));
 Skill->AnimationEvent(TEXT("Slash"));
 E.Tick(.25f);
 TestTrue(TEXT("No damage or execution before the delayed release"),IEGCombatant::Execute_IsAlive(Target)&&FMath::IsNearlyEqual(Target->GetAttributes()->GetHealth(),Threshold+10.f,.01f));
 // Target selection is a snapshot, while execution health is re-read at settlement.
 Target->SetActorLocation(E.Sakura->GetActorLocation()+FVector(15000,0,0));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Threshold-1.f);
 E.Tick(.15f);
 TestFalse(TEXT("Previously marked target executes even after leaving the sector"),IEGCombatant::Execute_IsAlive(Target));
 TestTrue(TEXT("Execution uses health at delayed settlement"),Target->LastCombatResolution.bExecution);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraDelayedCancellation,
 "ElyGambit.FiveHeroRework.Sakura.DelayedSlashCancellation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraDelayedCancellation::RunTest(const FString&)
{
 for(int32 Scenario=0;Scenario<3;++Scenario)
 {
  FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Sakura cancellation fixture"),E.Ready()))return false;
  auto* Target=E.Enemies[0];Target->SetActorLocation(E.Sakura->GetActorLocation()+FVector(100,0,0));
  if(!TestTrue(TEXT("Ultimate starts"),E.Cast()))return false;
  auto* Skill=E.Sakura->GetHeroSkill();Skill->AnimationEvent(TEXT("Slash"));
  const float Before=Target->GetAttributes()->GetHealth();
  if(Scenario==0)Skill->CancelAction();
  else if(Scenario==1)E.Sakura->Die();
  else E.Mode->BattleRules->Stop();
  E.Tick(.5f);
  TestEqual(TEXT("Cancelled, dead or stopped caster cannot apply a pending slash"),Target->GetAttributes()->GetHealth(),Before);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraEvade,
 "ElyGambit.FiveHeroRework.Sakura.EvadeAttackIdentityAndCooldown",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraEvade::RunTest(const FString&)
{
 FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Sakura evade fixture"),E.Ready()))return false;
 auto* Enemy=E.Enemies[0];auto* S=E.Sakura;Enemy->SetActorLocation(S->GetActorLocation()+FVector(150,0,0));
 const float Health=S->GetAttributes()->GetHealth();
 const float Speed=S->GetAttributes()->GetAttackSpeed();
 TestTrue(TEXT("A frontal skill hit is not mistaken for an ordinary attack"),FEGCombatExecution::Damage(Enemy,S,10.f));
 TestTrue(TEXT("The skill actually reduces health"),S->GetAttributes()->GetHealth()<Health);
 const float Before=S->GetAttributes()->GetHealth();const FGuid Volley=FGuid::NewGuid();
 const auto Basic=[&](FGuid Id){return FEGCombatExecution::Damage(Enemy,S,10.f,0.f,1.f,FGameplayEffectContextHandle(),Id,false,1.f,true);};
 TestFalse(TEXT("First frontal basic is dodged"),Basic(Volley));
 TestEqual(TEXT("Intrinsic dodge without a signet does not grant signet speed"),S->GetAttributes()->GetAttackSpeed(),Speed);
 TestFalse(TEXT("Later pellets of the same action cannot hit"),Basic(Volley));
 TestEqual(TEXT("The whole dodged action causes no damage"),S->GetAttributes()->GetHealth(),Before);
 TestTrue(TEXT("A different basic damages during cooldown"),Basic(FGuid::NewGuid()));
 E.Tick(8.1f);
 Enemy->SetActorLocation(S->GetActorLocation()+FVector(-150,0,0));
 TestTrue(TEXT("A rear basic is not dodged"),Basic(FGuid::NewGuid()));
 Enemy->SetActorLocation(S->GetActorLocation()+FVector(150,0,0));
 // Generic monster GA passes an empty action ID; an empty ID must not turn a
 // valid incoming ordinary attack into a skill or a permanently dodged action.
 TestFalse(TEXT("A frontal basic with no action ID is still dodged"),Basic(FGuid()));
 TestTrue(TEXT("A second action without an ID is not indefinitely dodged"),Basic(FGuid()));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraProjectileEvade,
 "ElyGambit.FiveHeroRework.Sakura.ProjectileAttackIdentity",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraProjectileEvade::RunTest(const FString&)
{
 for(int32 Scenario=0;Scenario<2;++Scenario)
 {
  FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Projectile evade fixture"),E.Ready()))return false;
  auto* Enemy=E.Enemies[0];auto* S=E.Sakura;Enemy->SetActorLocation(S->GetActorLocation()+FVector(150,0,0));
  if(Scenario==1&&!TestNotNull(TEXT("Configured monster projectile profile"),Enemy->GetDefinition()->MonsterAttack.Get()))return false;
  auto* Shot=E.World->SpawnActor<AEGSkillProjectile>();if(!TestNotNull(TEXT("Basic projectile exists"),Shot))return false;
  Shot->Battle=E.Mode->BattleRules;Shot->Round=E.Mode->BattleRules->GetRoundId();Shot->Source=Enemy;Shot->Target=S;
  Shot->AttackId=FGuid::NewGuid();Shot->Kind=Scenario==0?EEGProjectileKind::BasicAttack:EEGProjectileKind::MonsterBasic;Shot->RawDamage=100.f;
  Shot->Start=Enemy->GetActorLocation();Shot->Destination=S->GetActorLocation();Shot->Duration=.1f;Shot->SetActorLocation(Shot->Start);
  const float Before=S->GetAttributes()->GetHealth();TWeakObjectPtr<AEGSkillProjectile> WeakShot=Shot;E.Tick(2.f);
  TestEqual(TEXT("Real basic projectile is dodged on impact"),S->GetAttributes()->GetHealth(),Before);
  TestTrue(TEXT("Projectile resolves and is destroyed"),!WeakShot.IsValid()||WeakShot->IsActorBeingDestroyed());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraCueHold,
 "ElyGambit.FiveHeroRework.Sakura.CueClockDuringPresentationHold",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraCueHold::RunTest(const FString&)
{
 FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Sakura visual hold fixture"),E.Ready()))return false;
 auto* S=E.Sakura;auto* Target=E.Enemies[0];auto* Skill=S->GetHeroSkill();auto* Battle=E.Mode->BattleRules.Get();
 auto* D=Cast<UEGSakuraSkillDefinition>(S->GetDefinition()->SkillDefinition);
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 auto* Niagara=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Sakura/CombatPresentation20261030/NS_Sakura_Strike_Private.NS_Sakura_Strike_Private"));
 if(!TestTrue(TEXT("Cue resources available"),D&&Mesh&&Material&&Niagara))return false;
 TStrongObjectPtr<UEGSourceEffectDefinition> Source(NewObject<UEGSourceEffectDefinition>());
 FEGSourceEffectTrack Track;Track.Mesh=Mesh;Track.Material=Material;Track.Duration=10.f;Track.Samples.Add(FEGSourceEffectSample());Source->Tracks.Add(Track);
 TStrongObjectPtr<UEGSakuraReworkPresentationConfig> Config(NewObject<UEGSakuraReworkPresentationConfig>());
 Config->FanMark.SourceEffect=Source.Get();Config->FanMark.LifetimeSeconds=1.f;
 Config->DelayedMark.NiagaraSystem=Niagara;Config->DelayedMark.LifetimeSeconds=1.f;
 TGuardValue<TSoftObjectPtr<UEGSakuraReworkPresentationConfig>> Guard(D->ReworkPresentation,Config.Get());
 const FGuid Action=FGuid::NewGuid(),Round=Battle->GetRoundId();
 AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraFanMark,S,Target,Target->GetActorLocation(),Round,Action);
 AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraDelayedMark,S,Target,Target->GetActorLocation(),Round,Action);
 AEGFiveHeroPresentation* Visual=nullptr;AEGSourceEffect* SourceActor=nullptr;
 for(TActorIterator<AEGFiveHeroPresentation> It(E.World);It;++It)if(It->GetOwner()==S&&!It->IsActorBeingDestroyed())Visual=*It;
 for(TActorIterator<AEGSourceEffect> It(E.World);It;++It)if(It->GetOwner()==S&&It->GetSourceDefinition()==Source.Get()&&!It->IsActorBeingDestroyed())SourceActor=*It;
 if(!TestTrue(TEXT("Both real marker backends started"),Visual&&SourceActor&&Visual->GetActiveMarkCount()==2))return false;
 TInlineComponentArray<UNiagaraComponent*> Components(Visual);UNiagaraComponent* NiagaraComponent=nullptr;
 for(auto* C:Components)if(C&&C->GetAsset()==Niagara)NiagaraComponent=C;
 if(!TestNotNull(TEXT("Niagara mark component exists"),NiagaraComponent))return false;
 E.Tick(.1f);const float Before=SourceActor->GetElapsedTime();
 const FGuid Hold=FGuid::NewGuid();Battle->SetPresentationHold(Hold,true);
 if(!TestFalse(TEXT("Fixture exercises hold without a paused world"),E.World->IsPaused())){Battle->SetPresentationHold(Hold,false);return false;}
 E.Tick(.6f);
 TestTrue(TEXT("Source effect playback clock freezes during hold"),FMath::IsNearlyEqual(SourceActor->GetElapsedTime(),Before,.001f));
 TestTrue(TEXT("Niagara simulation pauses during hold"),NiagaraComponent->IsPaused());
 TestEqual(TEXT("Both marks survive their unpaused hold interval"),Visual->GetActiveMarkCount(),2);
 Battle->SetPresentationHold(Hold,false);E.Tick(.1f);
 TestTrue(TEXT("Source clock resumes when the hold ends"),SourceActor->GetElapsedTime()>Before+.05f);
 TestFalse(TEXT("Niagara resumes when the hold ends"),NiagaraComponent->IsPaused());
 Skill->CancelAction();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraDodgePresentation,
 "ElyGambit.FiveHeroRework.Sakura.DodgePresentationRestore",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraDodgePresentation::RunTest(const FString&)
{
 for(int32 Scenario=0;Scenario<5;++Scenario)
 {
  FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Dodge visual fixture"),E.Ready()))return false;
  auto* S=E.Sakura;auto* Target=E.Enemies[0];auto* Battle=E.Mode->BattleRules.Get();
  auto* Body=S->GetVisiblePresentationMesh();auto* Rules=Cast<UEGSakuraSkillDefinition>(S->GetDefinition()->SkillDefinition);
  if(!TestTrue(TEXT("Formal body and skill exist"),Body&&Body->GetSkeletalMeshAsset()&&Rules))return false;
  TStrongObjectPtr<UEGSakuraReworkPresentationConfig> Config(NewObject<UEGSakuraReworkPresentationConfig>());
  Config->DodgeDuration=.2f;Config->DodgeDistance=24.f;
  TGuardValue<TSoftObjectPtr<UEGSakuraReworkPresentationConfig>> Guard(Rules->ReworkPresentation,Config.Get());
  const FTransform ActorBefore=S->GetActorTransform(),BodyBefore=Body->GetRelativeTransform();
  Target->SetActorLocation(S->GetActorLocation()+FVector(100,0,0));
  AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraEvade,S,Target,S->GetActorLocation(),Battle->GetRoundId(),FGuid::NewGuid());
  AEGFiveHeroPresentation* Visual=nullptr;
  for(TActorIterator<AEGFiveHeroPresentation> It(E.World);It;++It)if(It->GetOwner()==S&&!It->IsActorBeingDestroyed())Visual=*It;
  if(!TestNotNull(TEXT("Dodge presentation actor"),Visual))return false;
  TWeakObjectPtr<AEGFiveHeroPresentation> WeakVisual=Visual;
  Visual->Tick(.05f);
  TestFalse(TEXT("Body moves visibly during the dodge"),Body->GetRelativeTransform().Equals(BodyBefore,.01f));
  TestTrue(TEXT("Dodge never moves the board actor"),S->GetActorTransform().Equals(ActorBefore,.01f));
  if(Scenario==0)
  {
   Visual->Tick(.2f);
   TestTrue(TEXT("Dodge restores body transform after expiry"),Body->GetRelativeTransform().Equals(BodyBefore,.01f));
  }
  else if(Scenario==1)
  {
   FTransform RigUpdate=BodyBefore;RigUpdate.AddToTranslation(FVector(0,0,7));Body->SetRelativeTransform(RigUpdate);
   Visual->Tick(.05f);Visual->Tick(.2f);
   TestTrue(TEXT("External rig transform is preserved after the offset ends"),Body->GetRelativeTransform().Equals(RigUpdate,.01f));
  }
  else if(Scenario==2)
  {
   auto* OldMesh=Body->GetSkeletalMeshAsset();Body->SetSkeletalMeshAsset(nullptr);Visual->Tick(.05f);
   TestTrue(TEXT("Replacing the body mesh restores the prior offset"),Body->GetRelativeTransform().Equals(BodyBefore,.01f));
   Body->SetSkeletalMeshAsset(OldMesh);
  }
  else
  {
   if(Scenario==3)Battle->Stop();else S->Die();
   E.Tick(.05f);
   TestTrue(TEXT("Stopped battle or caster death destroys the dodge visual"),!WeakVisual.IsValid()||WeakVisual->IsActorBeingDestroyed());
  }
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraSignetEvade,
 "ElyGambit.FiveHeroRework.Sakura.SignetEvadeCooldownAndAccounting",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraSignetEvade::RunTest(const FString&)
{
 for(int32 Scenario=0;Scenario<2;++Scenario)
 {
  FFiveHeroSakuraWorld E(*this,true);if(!TestTrue(TEXT("Sakura and Setsuna enter combat"),E.Ready()))return false;
  auto* S=E.Sakura;auto* Enemy=E.Enemies[0];auto* Rules=E.Mode->BattleRules->GetStigmata();
  auto* Definition=Cast<UEGSakuraSkillDefinition>(S->GetDefinition()->SkillDefinition);
  if(!TestTrue(TEXT("Signet selected and Sakura rules available"),Rules&&Rules->Has(TEXT("Setsuna"))&&Definition))return false;
  TGuardValue<float> GuardCooldown(Definition->EvadeCooldownSeconds,Scenario==0?2.f:8.f);
  Rules->SetsunaCooldown=Scenario==0?8.f:1.f;
  Enemy->SetActorLocation(S->GetActorLocation()+FVector(150,0,0));
  const float Speed=S->GetAttributes()->GetAttackSpeed(),Health=S->GetAttributes()->GetHealth();
  const float EnemyMana=Enemy->GetAttributes()->GetMana(),EnemyHealth=Enemy->GetAttributes()->GetHealth();
  const auto Basic=[&](FGuid Id){return FEGCombatExecution::Damage(Enemy,S,100.f,0.f,1.f,FGameplayEffectContextHandle(),Id,false,1.f,true);};
  const auto Count=[&]()
  {
   for(const auto& C:Rules->GetContributions())if(C.Id==TEXT("Setsuna"))return C;
   return FEGStigmataContribution();
  };
  const FGuid Volley=FGuid::NewGuid();
  TestFalse(TEXT("First basic is avoided once by intrinsic dodge and signet"),Basic(Volley));
  TestFalse(TEXT("Repeated pellet remains avoided"),Basic(Volley));
  const auto First=Count();
  TestEqual(TEXT("One action triggers the signet once"),First.TriggerCount,1);
  TestTrue(TEXT("Prevented amount is counted once"),FMath::IsNearlyEqual(First.DamagePrevented,100.f,.05f));
  TestTrue(TEXT("Ready signet gives its speed benefit"),FMath::IsNearlyEqual(S->GetAttributes()->GetAttackSpeed(),Speed*(1.f+Rules->SetsunaSpeed),.01f));
  E.Tick(Scenario==0?2.2f:1.2f);
  TestFalse(TEXT("The ready independent evade avoids a second attack"),Basic(FGuid::NewGuid()));
  const auto Second=Count();
  TestEqual(TEXT("Only ready signet triggers a second time"),Second.TriggerCount,Scenario==0?1:2);
  TestTrue(TEXT("Avoided amount is attributed only when the signet triggers"),FMath::IsNearlyEqual(Second.DamagePrevented,Scenario==0?100.f:200.f,.05f));
  TestEqual(TEXT("Avoided damage leaves Sakura health unchanged"),S->GetAttributes()->GetHealth(),Health);
  TestEqual(TEXT("No attacker mana is awarded by a missed ordinary attack"),Enemy->GetAttributes()->GetMana(),EnemyMana);
  TestEqual(TEXT("No counter damage is fabricated"),Enemy->GetAttributes()->GetHealth(),EnemyHealth);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraConfiguredRules,
 "ElyGambit.FiveHeroRework.Sakura.ConfiguredRules",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraConfiguredRules::RunTest(const FString&)
{
 auto* D=NewObject<UEGSakuraSkillDefinition>();
 TestTrue(TEXT("Default skill settings are valid"),D->HasValidSkillRules());
 D->EvadeCooldownSeconds=-1.f;TestFalse(TEXT("Negative evade cooldown is invalid"),D->HasValidSkillRules());D->EvadeCooldownSeconds=8.f;
 D->DelayedSlashSeconds=-1.f;TestFalse(TEXT("Negative slash delay is invalid"),D->HasValidSkillRules());D->DelayedSlashSeconds=.35f;
 D->FrontalEvadeDot=1.1f;TestFalse(TEXT("Dot product above one is invalid"),D->HasValidSkillRules());D->FrontalEvadeDot=-1.1f;
 TestFalse(TEXT("Dot product below minus one is invalid"),D->HasValidSkillRules());D->FrontalEvadeDot=.5f;
 D->DelayedSlashSeconds=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Non-finite slash delay is invalid"),D->HasValidSkillRules());
 FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Configurable rule battle fixture"),E.Ready()))return false;
 auto* S=E.Sakura;auto* Enemy=E.Enemies[0];auto* Rules=Cast<UEGSakuraSkillDefinition>(S->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Sakura skill rules"),Rules))return false;
 TGuardValue<float> GuardFacing(Rules->FrontalEvadeDot,.8f);
 TGuardValue<float> GuardDelay(Rules->DelayedSlashSeconds,.6f);
 Enemy->SetActorLocation(S->GetActorLocation()+FVector(100,100,0));
 TestTrue(TEXT("A 45-degree hit lies outside the configured cone"),FEGCombatExecution::Damage(Enemy,S,10.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),false,1.f,true));
 Enemy->SetActorLocation(S->GetActorLocation()+FVector(100,0,0));
 TestFalse(TEXT("A frontal hit remains inside the configured cone"),FEGCombatExecution::Damage(Enemy,S,10.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),false,1.f,true));
 auto* Skill=S->GetHeroSkill();const float Before=Enemy->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Configured delayed basic starts"),Skill->BeginAction(false)))return false;
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));E.Tick(.45f);
 TestEqual(TEXT("Configured .6-second delay has not completed after .45 seconds"),Enemy->GetAttributes()->GetHealth(),Before);
 E.Tick(.2f);TestTrue(TEXT("Configured delay eventually settles the hit"),Enemy->GetAttributes()->GetHealth()<Before);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraGenericAbility,
 "ElyGambit.FiveHeroRework.Sakura.GenericMonsterBasicAbility",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraGenericAbility::RunTest(const FString&)
{
 FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Generic monster basic fixture"),E.Ready()))return false;
 auto* S=E.Sakura;auto* Monster=E.World->SpawnActor<AEGChessUnit>();
 if(!TestNotNull(TEXT("Generic monster spawned"),Monster))return false;
 auto* Definition=NewObject<UEGHeroDefinition>(Monster);
 if(!TestTrue(TEXT("Generic definition uses the direct basic GA path"),Definition->HeroKind==EEGHeroKind::Generic&&!Definition->MonsterAttack))return false;
 if(!TestTrue(TEXT("Generic monster initialized"),Monster->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 Monster->GetHeroSkill()->Configure(E.Mode->BattleRules.Get(),1,false);
 Monster->SetRuntimeState(EEGUnitState::Fighting);
 Monster->SetActorLocation(S->GetActorLocation()+FVector(100,0,0));
 Monster->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 Monster->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1.f);
 TArray<TWeakObjectPtr<AEGChessUnit>> Targets;Targets.Add(S);
 Monster->GetCombat()->SetCombatants(Targets);
 const float Before=S->GetAttributes()->GetHealth();
 auto* SakuraBehavior=Cast<UEGSakuraSkillBehavior>(S->GetHeroSkill()->GetBehavior());
 if(!TestTrue(TEXT("Sakura starts without a stored dodge"),SakuraBehavior&&!SakuraBehavior->bDelayedAttackReady))return false;
 Monster->GetCombat()->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("The AI selected Sakura as its target"),Monster->GetCombat()->GetTarget()==S);
 TestTrue(TEXT("The Generic ability's ordinary hit triggers the dodge"),SakuraBehavior->bDelayedAttackReady);
 TestEqual(TEXT("The Generic ordinary hit does not apply damage"),S->GetAttributes()->GetHealth(),Before);
 Monster->Destroy();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHeroSakuraDelayedBasic,
 "ElyGambit.FiveHeroRework.Sakura.DelayedBasicFinishAndCancellation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHeroSakuraDelayedBasic::RunTest(const FString&)
{
 for(int32 Scenario=0;Scenario<2;++Scenario)
 {
  FFiveHeroSakuraWorld E(*this);if(!TestTrue(TEXT("Delayed basic fixture"),E.Ready()))return false;
  auto* Enemy=E.Enemies[0];auto* S=E.Sakura;auto* Skill=S->GetHeroSkill();
  Enemy->SetActorLocation(S->GetActorLocation()+FVector(100,0,0));
  if(!TestFalse(TEXT("Frontal basic arms Sakura's next strike"),FEGCombatExecution::Damage(Enemy,S,10.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),false,1.f,true)))return false;
  const float Before=Enemy->GetAttributes()->GetHealth();const int32 Count=E.Mode->BattleRules->GetAttackCount();
  if(!TestTrue(TEXT("Sakura begins her ordinary attack"),Skill->BeginAction(false)))return false;
  Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Hit"));
  TestEqual(TEXT("Empowered attack causes no early damage"),Enemy->GetAttributes()->GetHealth(),Before);
  if(Scenario==0)Skill->AnimationEvent(TEXT("Finish"));else Skill->CancelAction();
  E.Tick(.4f);
  if(Scenario==0)
  {
   TestTrue(TEXT("Completed counter deals 150 percent after action end"),FMath::IsNearlyEqual(Before-Enemy->GetAttributes()->GetHealth(),S->GetAttributes()->GetAttackPower()*1.5f,.05f));
   TestEqual(TEXT("Delayed damage rewards at most one basic hit"),E.Mode->BattleRules->GetAttackCount(),Count+1);
  }
  else
  {
   TestEqual(TEXT("Cancelled attack deals no late damage"),Enemy->GetAttributes()->GetHealth(),Before);
   TestEqual(TEXT("Cancelled attack creates no basic hit"),E.Mode->BattleRules->GetAttackCount(),Count);
  }
 }
 return true;
}
#endif
