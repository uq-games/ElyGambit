#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGElysiaOriginKnockupComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/CapsuleComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"

namespace
{
struct FOriginFixture
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W=nullptr;AEGDemoGameMode* GM=nullptr;
 AEGChessUnit* Human=nullptr;AEGChessUnit* Hua=nullptr;TArray<AEGChessUnit*> Enemies;
 explicit FOriginFixture(FAutomationTestBase& Test,bool bAddHua=false):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  TSoftObjectPtr<UEGCardDefinition> FormalHumanCard;
  GM->RunDefinition=EGProductionTestAssets::ResolveRun(Test,&FormalHumanCard);
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  UEGCardDefinition* Card=FormalHumanCard.LoadSynchronous();
  if(!Test.TestNotNull(TEXT("Formal HumanFusionCard"),Card)||!Card->HeroDefinition||Card->HeroDefinition->HeroKind!=EEGHeroKind::ElysiaHerrscher)return;
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,1}))return;
  if(bAddHua)
  {
   auto* HuaCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"));
   if(!HuaCard||!Cards->GrantCard(HuaCard)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1}))return;
  }
  if(!GM->EnterCombat())return;
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:GM->GetCombatUnits())
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);
   Unit->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
   Unit->GetCharacterMovement()->SetComponentTickEnabled(false);
   if(Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=Unit;
   else if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Hua&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hua=Unit;
   else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)
   {
    Enemies.Add(Unit);auto* ASC=Unit->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   }
  }
  if(Human)for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Grid->CellToWorld({2+I,4})+FVector(0,0,88));
 }
 ~FOriginFixture(){if(W){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}}
 bool Ready()const{return Human&&Enemies.Num()>=2&&Cast<UEGElysiaHerrscherSkillBehavior>(Human->GetHeroSkill()->GetBehavior());}
 void Advance(float Seconds){while(Seconds>0.f){const float Delta=FMath::Min(.02f,Seconds);++GFrameCounter;W->Tick(LEVELTICK_All,Delta);Seconds-=Delta;}}
 APlayerController* AddController()
 {
  auto* PC=W->SpawnActor<APlayerController>();if(!PC)return nullptr;
  if(!PC->PlayerState)PC->SetPlayerState(W->SpawnActor<APlayerState>());
  return PC;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginKnockup,"ElyGambit.ElysiaHerrscher.OriginKnockupLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginKnockup::RunTest(const FString&)
{
 FOriginFixture E(*this);if(!TestTrue(TEXT("Production human and targets"),E.Ready()))return false;
 auto* Target=E.Enemies[0];auto* Combat=Target->GetCombat();const FVector Initial=Target->GetActorLocation();const float Ground=Initial.Z;
 const FGuid Round=E.GM->BattleRules->GetRoundId(),Action=FGuid::NewGuid();
 auto* Reaction=UEGElysiaOriginKnockupComponent::Start(E.Human,Target,E.GM->BattleRules,Round,Action,120.f,.6f);
 if(!TestNotNull(TEXT("ordinary target can be lifted"),Reaction))return false;
 TestTrue(TEXT("reaction component ticks"),Reaction->IsRegistered()&&Reaction->IsComponentTickEnabled());
 TestTrue(TEXT("action blocked while lifted"),Combat->HasExternalActionBlock());
 const FGuid OtherBlock=FGuid::NewGuid();Combat->AddExternalActionBlock(OtherBlock);
 E.Advance(.15f);TestTrue(TEXT("ordinary target rises"),Target->GetActorLocation().Z>Ground+10.f);
 TestTrue(TEXT("vertical lift preserves occupied board XY"),FVector::Dist2D(Target->GetActorLocation(),Initial)<.1f);
 E.Advance(.6f);TestTrue(TEXT("landing preserves another owner's block"),Combat->HasExternalActionBlock());
 TestTrue(TEXT("movement remains disabled while the other block is held"),Target->GetCharacterMovement()->MovementMode==MOVE_None);
 Combat->RemoveExternalActionBlock(OtherBlock);E.Advance(.04f);
 TestFalse(TEXT("reaction releases its own block after the other owner"),Combat->HasExternalActionBlock());
 TestTrue(TEXT("target returns to original height"),FMath::IsNearlyEqual(Target->GetActorLocation().Z,Ground,1.f));
 TestTrue(TEXT("movement mode restored"),Target->GetCharacterMovement()->MovementMode==MOVE_Flying);
 Reaction=UEGElysiaOriginKnockupComponent::Start(E.Human,Target,E.GM->BattleRules,Round,FGuid::NewGuid(),120.f,.6f);
 if(!TestNotNull(TEXT("second reaction"),Reaction))return false;
 E.Advance(.15f);E.GM->BattleRules->Stop();E.Advance(.05f);
 TestFalse(TEXT("round stop releases reaction"),Combat->HasExternalActionBlock());
 TestTrue(TEXT("round stop restores height"),FMath::IsNearlyEqual(Target->GetActorLocation().Z,Ground,1.f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginReviveDuringKnockup,"ElyGambit.ElysiaHerrscher.OriginKnockupRevive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginReviveDuringKnockup::RunTest(const FString&)
{
 for(bool bDeferred:{false,true})
 {
  FOriginFixture E(*this);if(!TestTrue(TEXT("Production human and targets"),E.Ready()))return false;
  auto* Target=E.Enemies[0];auto* Combat=Target->GetCombat();auto* Move=Target->GetCharacterMovement();
  Move->SetComponentTickEnabled(true);
  const float Ground=Target->GetActorLocation().Z;const FGuid Round=E.GM->BattleRules->GetRoundId();
  auto* Reaction=UEGElysiaOriginKnockupComponent::Start(E.Human,Target,E.GM->BattleRules,Round,FGuid::NewGuid(),120.f,.6f);
  if(!TestNotNull(TEXT("reaction begins before revive"),Reaction))return false;
  E.Advance(.15f);TestTrue(TEXT("target is airborne"),Target->GetActorLocation().Z>Ground+10.f);
  AActor* Blocker=nullptr;UCapsuleComponent* Capsule=nullptr;
  if(bDeferred)
  {
   Blocker=E.W->SpawnActor<AActor>();Capsule=NewObject<UCapsuleComponent>(Blocker);Blocker->SetRootComponent(Capsule);
   Capsule->InitCapsuleSize(10000.f,10000.f);Capsule->SetCollisionObjectType(ECC_Pawn);
   Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Capsule->SetCollisionResponseToAllChannels(ECR_Block);
   Capsule->RegisterComponent();Blocker->SetActorLocation(FVector(0,0,88));
   Reaction->FinishReaction();
   if(!TestTrue(TEXT("full board defers the knockup landing"),Combat->IsBoardLandingPending()))return false;
  }
  const FGuid Other=FGuid::NewGuid();
  if(!bDeferred)Combat->AddExternalActionBlock(Other);
  if(!TestTrue(TEXT("recoverable transition starts"),Target->GetHeroSkill()->BeginRevive(.3f,.5f,0.f)))return false;
  Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),0.f);
  TestTrue(TEXT("zero-health target remains recoverable"),Target->GetHeroSkill()->IsRecoverable());
  E.Advance(.04f);
  if(!bDeferred)
  {
   TestTrue(TEXT("other owner retains disabled movement"),Move->MovementMode==MOVE_None);
   Combat->RemoveExternalActionBlock(Other);E.Advance(.04f);
  }
  else Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  TestFalse(TEXT("reaction releases pending landing before revive"),Combat->IsBoardLandingPending());
  TestTrue(TEXT("recoverable target regains the pre-knockup mode"),Move->MovementMode==MOVE_Flying);
  TestTrue(TEXT("recoverable target returns to ground height"),FMath::IsNearlyEqual(Target->GetActorLocation().Z,Ground,1.f));
  E.Advance(.4f);
  TestFalse(TEXT("revival completes"),Target->GetHeroSkill()->IsRecoverable());
  TestTrue(TEXT("revived target remains movable"),IEGCombatant::Execute_IsAlive(Target)&&Move->MovementMode==MOVE_Flying);
  TestFalse(TEXT("reaction releases only its own block"),Combat->HasExternalActionBlock());
  if(Blocker)Blocker->Destroy();
 }
 {
  FOriginFixture E(*this);if(!TestTrue(TEXT("Death fixture"),E.Ready()))return false;
  auto* Target=E.Enemies[0];auto* Combat=Target->GetCombat();auto* Move=Target->GetCharacterMovement();
  if(!TestNotNull(TEXT("reaction before death"),UEGElysiaOriginKnockupComponent::Start(E.Human,Target,E.GM->BattleRules,E.GM->BattleRules->GetRoundId(),FGuid::NewGuid(),120.f,.6f)))return false;
  E.Advance(.1f);Target->Die();E.Advance(.04f);
  TestTrue(TEXT("true death does not restore movement"),Move->MovementMode==MOVE_None);
  TestFalse(TEXT("true death releases the reaction block"),Combat->HasExternalActionBlock());
 }
 {
  FOriginFixture E(*this);if(!TestTrue(TEXT("Movement owner fixture"),E.Ready()))return false;
  auto* Target=E.Enemies[0];auto* Move=Target->GetCharacterMovement();
  auto* Reaction=UEGElysiaOriginKnockupComponent::Start(E.Human,Target,E.GM->BattleRules,E.GM->BattleRules->GetRoundId(),FGuid::NewGuid(),120.f,.6f);
  if(!TestNotNull(TEXT("reaction before other movement mode"),Reaction))return false;
  Move->SetMovementMode(MOVE_Walking);Reaction->FinishReaction();
  TestTrue(TEXT("reaction does not overwrite another movement mode"),Move->MovementMode==MOVE_Walking);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginLethalRevive,"ElyGambit.ElysiaHerrscher.OriginKnockupLethalRevive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginLethalRevive::RunTest(const FString&)
{
 FOriginFixture E(*this,true);
 if(!TestTrue(TEXT("Production human, Hua and enemy"),E.Ready()&&E.Hua))return false;
 auto* Target=E.Hua;auto* Source=E.Enemies[0];auto* Move=Target->GetCharacterMovement();
 auto* Rules=Cast<UEGHuaSkillDefinition>(Target->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Hua revival rules"),Rules))return false;
 TGuardValue<float> ReviveDelay(Rules->ReviveDelay,.3f);
 auto* Reaction=UEGElysiaOriginKnockupComponent::Start(Source,Target,E.GM->BattleRules,E.GM->BattleRules->GetRoundId(),FGuid::NewGuid(),120.f,.6f);
 if(!TestNotNull(TEXT("enemy lifts Hua"),Reaction))return false;
 E.Advance(.15f);
 TestTrue(TEXT("lethal combat damage is accepted"),FEGCombatExecution::Damage(Source,Target,Target->GetAttributes()->GetMaxHealth()*100.f));
 TestTrue(TEXT("real Hua behavior enters revive at zero health"),Target->GetHeroSkill()->IsRecoverable()&&Target->GetAttributes()->GetHealth()<=0.f);
 E.Advance(.5f);
 if(!TestTrue(TEXT("Hua actually revives"),IEGCombatant::Execute_IsAlive(Target)&&!Target->GetHeroSkill()->IsRecoverable()))return false;
 TestTrue(TEXT("movement mode is restored after lethal revival"),Move->MovementMode==MOVE_Flying);
 Move->SetComponentTickEnabled(true);Move->bRunPhysicsWithNoController=true;Move->BrakingDecelerationFlying=0.f;
 Move->Velocity=FVector(0,-200.f,0);const FVector Before=Target->GetActorLocation();E.Advance(.1f);
 TestTrue(TEXT("revived Hua physically moves"),FVector::Dist2D(Before,Target->GetActorLocation())>1.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginPause,"ElyGambit.ElysiaHerrscher.OriginEntryPauseOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginPause::RunTest(const FString&)
{
 FOriginFixture E(*this);if(!TestTrue(TEXT("Production human and targets"),E.Ready()))return false;
 auto* PC=E.AddController();if(!TestNotNull(TEXT("local controller with player state"),PC)||!TestNotNull(TEXT("player state"),PC->PlayerState.Get()))return false;
 auto* Skill=E.Human->GetHeroSkill();auto* Battle=E.GM->BattleRules.Get();
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("entry action starts"),Skill->BeginAction(true)))return false;
 const FGuid Action=Skill->ActionId;
 TestFalse(TEXT("pause waits until next world tick"),E.W->IsPaused());
 E.Advance(.01f);
 if(!TestTrue(TEXT("next frame owns world pause"),E.W->IsPaused()&&Battle->IsHumanEntryPauseOwner(E.Human,Action)))return false;
 PC->SetPause(false);TestTrue(TEXT("unrelated unpause cannot release active entry"),E.W->IsPaused());
 TestTrue(TEXT("another system can register its own pause"),E.GM->SetPause(PC));
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
 TestTrue(TEXT("entry finish preserves other system's pause"),E.W->IsPaused());
 TestFalse(TEXT("pause token released"),Battle->HasHumanEntryPause());
 PC->SetPause(false);TestFalse(TEXT("other pause can be released independently"),E.W->IsPaused());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginPauseCancel,"ElyGambit.ElysiaHerrscher.OriginEntryPauseCancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginPauseCancel::RunTest(const FString&)
{
 FOriginFixture E(*this);if(!TestTrue(TEXT("Production human and targets"),E.Ready()))return false;
 auto* PC=E.AddController();if(!TestNotNull(TEXT("controller"),PC)||!TestNotNull(TEXT("player state"),PC->PlayerState.Get()))return false;
 auto* Skill=E.Human->GetHeroSkill();auto* Battle=E.GM->BattleRules.Get();
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("entry starts"),Skill->BeginAction(true)))return false;
 Skill->CancelAction();E.Advance(.01f);
 TestFalse(TEXT("cancel before the next frame never pauses the world"),E.W->IsPaused());
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("entry restarts"),Skill->BeginAction(true)))return false;
 if(!TestTrue(TEXT("another system pauses before acquisition"),PC->SetPause(true)))return false;
 E.Advance(.01f);TestFalse(TEXT("preexisting pause is not acquired"),Battle->HasHumanEntryPause());
 Skill->CancelAction();TestTrue(TEXT("entry cancellation preserves preexisting pause"),E.W->IsPaused());
 PC->SetPause(false);TestFalse(TEXT("original pause owner can unpause"),E.W->IsPaused());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginFinisher,"ElyGambit.ElysiaHerrscher.OriginFinisherAllEnemies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginFinisher::RunTest(const FString&)
{
 FOriginFixture E(*this);if(!TestTrue(TEXT("Production human and targets"),E.Ready()))return false;
 auto* Skill=E.Human->GetHeroSkill();auto* Battle=E.GM->BattleRules.Get();Skill->Configure(Battle,11,false);
 auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("human rules"),Rules))return false;
 TGuardValue<float> ShortOrigin(Rules->OriginSeconds,.2f),MainMultiplier(Rules->FinisherMain,4.f),OtherMultiplier(Rules->FinisherOthers,4.f);
 auto* Main=E.Human->GetCombat()->FindNearestEnemy();auto* Boss=E.Enemies[0]==Main?E.Enemies[1]:E.Enemies[0];
 if(!TestNotNull(TEXT("ordinary main target"),Main)||!TestNotNull(TEXT("boss target"),Boss))return false;
 if(!TestNotNull(TEXT("authoritative boss identity"),UEGCorruptionCombatComponent::AttachBoss(Boss,Battle,100000.f,10.f,0)))return false;
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("entry starts"),Skill->BeginAction(true)))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
 E.Advance(.3f);
 if(!TestTrue(TEXT("finisher starts after origin"),Skill->IsCasting()))return false;
 const float MainHP=Main->GetAttributes()->GetHealth(),BossHP=Boss->GetAttributes()->GetHealth();
 const float Attack=E.Human->GetAttributes()->GetAttackPower()*E.Human->GetAttributes()->GetSkillMultiplier();
 const float MainZ=Main->GetActorLocation().Z,BossZ=Boss->GetActorLocation().Z;
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Hit"));
 TestTrue(TEXT("main takes exactly 400 percent once"),FMath::IsNearlyEqual(MainHP-Main->GetAttributes()->GetHealth(),Attack*4.f,.1f));
 TestTrue(TEXT("boss takes exactly 400 percent once"),FMath::IsNearlyEqual(BossHP-Boss->GetAttributes()->GetHealth(),Attack*4.f,.1f));
 E.Advance(.15f);
 TestTrue(TEXT("finisher does not add unrequested knockup"),FMath::IsNearlyEqual(Main->GetActorLocation().Z,MainZ,.1f));
 TestTrue(TEXT("boss receives damage without displacement"),FMath::IsNearlyEqual(Boss->GetActorLocation().Z,BossZ,.1f));
 Skill->CancelAction();
 TestFalse(TEXT("cancel releases lifted target"),Main->GetCombat()->HasExternalActionBlock());
 return true;
}
#endif
