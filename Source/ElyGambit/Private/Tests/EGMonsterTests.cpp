#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGPieceTestGameMode.h"
#include "Game/EGMatchManager.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Presentation/EGSkillProjectile.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimSequence.h"
#include "Presentation/EGChessAnimInstance.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Actor/EGCorruptionMine.h"
#include "Data/EGEnvironmentTrapDefinition.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Character/EGVillVEmplacement.h"

namespace
{
struct FMonsterWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGPieceTestGameMode* GM;
 AEGChessUnit *Hero=nullptr,*Melee=nullptr,*Ranged=nullptr;
 FMonsterWorld(int32 HeroIndex=0):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;
  W->InitializeActorsForPlay(URL);W->BeginPlay();GM=W->GetAuthGameMode<AEGPieceTestGameMode>();
  if(HeroIndex>0)GM->SelectPiece(HeroIndex);
  GM->EnterCombat();
  for(const auto& Entry:GM->GetBattleUnits())if(auto* U=Entry.Get())
  {
   U->GetCombat()->SetComponentTickEnabled(false);
   auto* A=U->GetAbilitySystemComponent();A->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);
   A->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);A->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
   if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)Hero=U;
   else if(U->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Melee)Melee=U;else Ranged=U;
  }
  Hero->SetActorLocation(FVector(0,0,88));Melee->SetActorLocation(FVector(-130,0,88));Ranged->SetActorLocation(FVector(400,0,88));
 }
 ~FMonsterWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.01f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.01f);}}
 bool Start(AEGChessUnit* U){U->GetHeroSkill()->CancelAction();U->GetCombat()->ResetCombat();U->GetCombat()->TickComponent(.001f,LEVELTICK_All,nullptr);return U->GetHeroSkill()->IsBusy();}
 int32 Projectiles(){int32 N=0;for(TActorIterator<AEGSkillProjectile> It(W);It;++It)if(!It->IsActorBeingDestroyed())++N;return N;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterTransactions,"ElyGambit.Monsters.AttackTransactionsAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterTransactions::RunTest(const FString&)
{
 FMonsterWorld F;if(!TestNotNull(TEXT("melee"),F.Melee)||!TestNotNull(TEXT("ranged"),F.Ranged)||!TestNotNull(TEXT("hero"),F.Hero))return false;
 FString Error;TestTrue(TEXT("Melee authored notifies/config valid"),F.Melee->GetDefinition()->ValidateDefinition(Error,true));
 TestTrue(TEXT("Ranged authored notifies/config valid"),F.Ranged->GetDefinition()->ValidateDefinition(Error,true));
 TestTrue(TEXT("Begin melee"),F.Start(F.Melee));const float Before=F.Hero->GetAttributes()->GetHealth();
 TestEqual(TEXT("No damage at animation start"),F.Hero->GetAttributes()->GetHealth(),Before);
 F.Melee->GetHeroSkill()->AnimationEvent(TEXT("Hit"));const float After=F.Hero->GetAttributes()->GetHealth();
 TestTrue(TEXT("Contact damages"),After<Before);F.Melee->GetHeroSkill()->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Duplicate notify does not damage twice"),F.Hero->GetAttributes()->GetHealth(),After);
 F.Melee->GetHeroSkill()->CancelAction();F.Melee->GetHeroSkill()->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Cancelled notify inert"),F.Hero->GetAttributes()->GetHealth(),After);
 TestTrue(TEXT("Start next melee"),F.Start(F.Melee));F.Hero->SetActorLocation(FVector(0,800,88));
 F.Melee->GetHeroSkill()->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Moved target outside reach not damaged"),F.Hero->GetAttributes()->GetHealth(),After);
 F.Melee->GetHeroSkill()->CancelAction();F.Hero->SetActorLocation(FVector(0,0,88));
 TestTrue(TEXT("Begin ranged"),F.Start(F.Ranged));F.Ranged->GetHeroSkill()->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Launch does not damage"),F.Hero->GetAttributes()->GetHealth(),After);TestEqual(TEXT("One actual projectile"),F.Projectiles(),1);
 F.Ranged->GetHeroSkill()->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Duplicate launch rejected"),F.Projectiles(),1);
 F.Tick(.1f);TestEqual(TEXT("Flight time respected"),F.Hero->GetAttributes()->GetHealth(),After);
 F.Tick(1.f);TestTrue(TEXT("Arrival damages"),F.Hero->GetAttributes()->GetHealth()<After);TestEqual(TEXT("Projectile consumed"),F.Projectiles(),0);
 TestTrue(TEXT("Next ranged shot"),F.Start(F.Ranged));F.Ranged->GetHeroSkill()->AnimationEvent(TEXT("Hit"));
 const float PreDeath=F.Hero->GetAttributes()->GetHealth();F.Ranged->Die();F.Tick(1.f);
 TestEqual(TEXT("Dead source cannot leave delayed damage"),F.Hero->GetAttributes()->GetHealth(),PreDeath);TestEqual(TEXT("Dead source projectile gone"),F.Projectiles(),0);
 TestTrue(TEXT("Reset"),F.GM->ResetTest());TestEqual(TEXT("Reset clears projectiles"),F.Projectiles(),0);
 TestFalse(TEXT("Round invalidated"),F.GM->BattleRules->GetRoundId().IsValid());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterSpacing,"ElyGambit.Monsters.SweptSeparationAndRealNotifies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterSpacing::RunTest(const FString&)
{
 FMonsterWorld F;
 // Keep the existing controlled pursuit inside a real board. The default
 // six-column fixture ends at +/-400 cm after the body-radius inset.
 F.GM->GetBoard()->LayoutSource->Columns=14;
 F.Melee->SetActorLocation(FVector(-900,0,88));F.Ranged->SetActorLocation(FVector(-450,0,88));
 TestTrue(TEXT("Melee pursuit starts at a legal board location"),F.Melee->GetCombat()->IsBoardLocationFree(F.Melee->GetActorLocation()));
 TestTrue(TEXT("Stationary blocker starts at a legal board location"),F.Ranged->GetCombat()->IsBoardLocationFree(F.Ranged->GetActorLocation()));
 F.Melee->GetCombat()->SetComponentTickEnabled(true);
 float Minimum=TNumericLimits<float>::Max();
 for(int32 I=0;I<800;++I)
 {
  F.Tick(.01f);Minimum=FMath::Min(Minimum,FVector::Dist2D(F.Melee->GetActorLocation(),F.Ranged->GetActorLocation()));
 }
 TestTrue(TEXT("Monster walks around stationary occupied footprint"),Minimum>=
  UEGCombatComponent::BlockingRadius(F.Melee)+UEGCombatComponent::BlockingRadius(F.Ranged)-1.f);
 TestTrue(TEXT("Melee reaches attack distance without passing through blocker"),F.Melee->GetCombat()->IsInAttackRange(F.Hero));
 F.Melee->GetCombat()->SetComponentTickEnabled(false);F.Melee->GetHeroSkill()->CancelAction();
 auto* ASC=F.Melee->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),3.f);
 TestTrue(TEXT("Fast attack begins"),F.Start(F.Melee));const float Before=F.Hero->GetAttributes()->GetHealth();
 F.Tick(.05f);TestEqual(TEXT("Actual animation front swing has no damage"),F.Hero->GetAttributes()->GetHealth(),Before);
 F.Tick(.2f);TestTrue(TEXT("Actual notify deals damage at high speed"),F.Hero->GetAttributes()->GetHealth()<Before);
 const float Once=F.Hero->GetAttributes()->GetHealth();F.Tick(.3f);
 TestEqual(TEXT("Single animation cannot deal repeated contact damage"),F.Hero->GetAttributes()->GetHealth(),Once);
 F.Ranged->SetActorLocation(FVector(500,0,88));TestTrue(TEXT("Projectile action begins"),F.Start(F.Ranged));
 F.Ranged->GetHeroSkill()->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Shot exists before target death"),F.Projectiles(),1);
 F.Hero->Die();F.Tick(.1f);TestEqual(TEXT("Dead target dissipates shot"),F.Projectiles(),0);
 TestTrue(TEXT("Reset creates fresh incarnation"),F.GM->ResetTest());TestTrue(TEXT("New round starts"),F.GM->EnterCombat());
 TestEqual(TEXT("No projectile crosses round boundary"),F.Projectiles(),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterHeroReach,"ElyGambit.Monsters.MeleeHeroReachesLargeMonster",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterHeroReach::RunTest(const FString&)
{
 FMonsterWorld F(2);F.GM->GetBoard()->LayoutSource->Columns=14;
 F.Melee->SetActorLocation(FVector(-600,0,88));F.Ranged->SetActorLocation(FVector(1000,0,88));
 TestTrue(TEXT("Target starts at a legal board location"),F.Melee->GetCombat()->IsBoardLocationFree(F.Melee->GetActorLocation()));
 const float Before=F.Melee->GetAttributes()->GetHealth();
 F.Hero->GetCombat()->SetComponentTickEnabled(true);F.Tick(7.f);
 TestTrue(TEXT("Short-range hero reaches a large physical target"),F.Hero->GetCombat()->IsInAttackRange(F.Melee));
 TestTrue(TEXT("Real hero notify damages large monster"),F.Melee->GetAttributes()->GetHealth()<Before);
 TestTrue(TEXT("Hero body stays outside monster footprint"),FVector::Dist2D(F.Hero->GetActorLocation(),F.Melee->GetActorLocation())>=
  UEGCombatComponent::BlockingRadius(F.Hero)+UEGCombatComponent::BlockingRadius(F.Melee)-1.f);
 // Check the reverse approach too: stopping and hit validation must agree
 // after body radii shrink, rather than forcing either side into the gap.
 F.Hero->GetCombat()->SetComponentTickEnabled(false);F.Hero->GetHeroSkill()->CancelAction();
 F.Melee->SetActorLocation(FVector(-700,0,88));F.Hero->SetActorLocation(FVector(0,0,88));
 F.Ranged->SetActorLocation(FVector(2000,2000,88));F.Melee->GetCombat()->SetComponentTickEnabled(true);
 const float Health=F.Hero->GetAttributes()->GetHealth();F.Tick(7.f);
 TestTrue(TEXT("Monster can hit across the visible body clearance"),F.Hero->GetAttributes()->GetHealth()<Health);
 TestTrue(TEXT("Monster approach stays outside the combined blocking radii"),FVector::Dist2D(F.Hero->GetActorLocation(),F.Melee->GetActorLocation())>=
  UEGCombatComponent::BlockingRadius(F.Hero)+UEGCombatComponent::BlockingRadius(F.Melee)-1.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterGridRange,"ElyGambit.Monsters.ThreeRowProjectileRange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterGridRange::RunTest(const FString&)
{
 FMonsterWorld F;auto* Board=F.GM->GetBoard();auto* Grid=Board->LayoutSource.Get();
 if(!TestNotNull(TEXT("Active layout"),Grid))return false;
 TestEqual(TEXT("Ranged profile uses three rows"),F.Ranged->GetDefinition()->MonsterAttack->AttackRangeCells,3.f);
 // Match the formal board, then exercise a rotated, scaled test layout too.
 for(float Scale:{1.f,1.25f})
 {
  Grid->CellDepth=500.678741f;Grid->SetActorScale3D(FVector(Scale));Grid->SetActorRotation(FRotator(0,27.f,0));
  F.Ranged->SetActorLocation(Board->CellToWorld({2,5}));F.Hero->SetActorLocation(Board->CellToWorld({2,2}));
  TestTrue(TEXT("Sixth row reaches third row"),F.Ranged->GetCombat()->IsInAttackRange(F.Hero));
  TestTrue(TEXT("Reach follows the active layout transform"),FMath::IsNearlyEqual(F.Ranged->GetCombat()->GetEffectiveAttackRange(F.Hero),3*Grid->CellDepth*Scale,.01f));
  TestTrue(TEXT("Shot can start across three rows"),F.Start(F.Ranged));
  F.Hero->SetActorLocation(Board->CellToWorld({2,1}));
  TestFalse(TEXT("Sixth row cannot reach second row"),F.Ranged->GetCombat()->IsInAttackRange(F.Hero));
  F.Ranged->GetHeroSkill()->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Leaving range during windup suppresses launch"),F.Projectiles(),0);
  F.Ranged->GetHeroSkill()->CancelAction();
  F.Hero->SetActorLocation(Board->CellToWorld({2,2}));F.Ranged->GetCombat()->SetComponentTickEnabled(true);
  const FVector Position=F.Ranged->GetActorLocation();F.Tick(.25f);
  TestTrue(TEXT("Ranged monster holds its row when target is already in reach"),F.Ranged->GetActorLocation().Equals(Position,.01f));
  F.Ranged->GetCombat()->SetComponentTickEnabled(false);F.Ranged->GetHeroSkill()->CancelAction();
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterDeath,"ElyGambit.Monsters.FallThenDissolveAndReset",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterDeath::RunTest(const FString&)
{
 for(bool Ranged:{false,true})
 {
  FMonsterWorld F;auto* Unit=Cast<AEGDemoCombatUnit>(Ranged?F.Ranged:F.Melee);auto* Mesh=Unit->GetVisiblePresentationMesh();
  const auto* Profile=Unit->GetDefinition()->MonsterAttack.Get();const float Fall=Unit->GetDefinition()->GetDeathAnimation().LoadSynchronous()->GetPlayLength();
  auto* LiveMaterial=Mesh->GetMaterial(0);auto* OtherMesh=(Ranged?F.Melee:F.Ranged)->GetVisiblePresentationMesh();auto* OtherMaterial=OtherMesh->GetMaterial(0);
  Mesh->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Unit->Die();F.Tick(.1f);
  TestFalse(TEXT("Dead monster cannot start another attack"),Unit->GetHeroSkill()->BeginAction(false));
  TestFalse(TEXT("Death hides vitals immediately"),Unit->Vitals->IsVisible());
  auto* Animation=Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance());
  if(!TestNotNull(TEXT("Real death-state animation blueprint"),Animation))return false;
  TestTrue(TEXT("Full death state plays before dissolve"),Animation->bDead&&Animation->GetActiveAction()==EGPresentationAction::Death);
  F.Tick(Fall-.2f);TestTrue(TEXT("Fall keeps the live surface material"),Mesh->GetMaterial(0)==LiveMaterial);
  TestTrue(TEXT("Fallen body remains visible"),Mesh->IsVisible());
  F.Tick(.3f);auto* Dynamic=Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
  if(!TestNotNull(TEXT("Dissolve starts after fall and hold"),Dynamic))return false;
  const float First=Dynamic->K2_GetScalarParameterValue(TEXT("Dissolve"));F.Tick(.3f);
  TestTrue(TEXT("Dissolve parameter advances monotonically"),Dynamic->K2_GetScalarParameterValue(TEXT("Dissolve"))>First);
  TestTrue(TEXT("One dynamic instance per monster, owned by its actor"),Mesh->GetMaterial(0)==Dynamic&&Dynamic->GetOuter()==Unit);
  TestTrue(TEXT("Other living monster's material is untouched"),OtherMesh->GetMaterial(0)==OtherMaterial&&OtherMesh->IsVisible());
  Unit->Die();F.Tick(Profile->DissolveSeconds);
  TestFalse(TEXT("Dissolve completes and hides the body"),Mesh->IsVisible());
  TestEqual(TEXT("Dissolve reaches the fully removed endpoint"),Dynamic->K2_GetScalarParameterValue(TEXT("Dissolve")),1.f);
  TWeakObjectPtr<AEGDemoCombatUnit> Old=Unit;
  TestTrue(TEXT("Reset after dissolve"),F.GM->ResetTest());
  TestTrue(TEXT("Old incarnation destroyed by existing reset"),!Old.IsValid()||Old->IsActorBeingDestroyed());
  for(const auto& Entry:F.GM->GetBattleUnits())if(auto* Fresh=Entry.Get())if(Fresh->GetDefinition()->MonsterAttack)
  {TestTrue(TEXT("Fresh monster visible"),Fresh->GetVisiblePresentationMesh()->IsVisible());TestFalse(TEXT("Fresh monster has no previous death MID"),Fresh->GetVisiblePresentationMesh()->GetMaterial(0)->IsA<UMaterialInstanceDynamic>());}
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterSkills,"ElyGambit.Monsters.Skills.ShieldsAndEnhancedShot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterSkills::RunTest(const FString&)
{
 FMonsterWorld F;auto* S=F.Melee->GetHeroSkill();auto* A=F.Melee->GetAbilitySystemComponent();
 S->SetShield(40);S->GrantSourceShield(TEXT("Other"),60,20);S->GrantSourceShield(TEXT("MonsterShield"),100,5);
 TestEqual(TEXT("Independent shield total"),S->Shield,200.f);
 S->GrantSourceShield(TEXT("MonsterShield"),100,5);TestEqual(TEXT("Refresh never stacks"),S->Shield,200.f);
 S->AbsorbShield(30);TestEqual(TEXT("Soonest shield consumed first"),S->GetSourceShield(TEXT("MonsterShield")),70.f);
 F.Tick(5.1f);TestEqual(TEXT("Expiry preserves other sources"),S->Shield,100.f);
 A->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);F.Melee->GetCombat()->SetCombatants(TArray<TWeakObjectPtr<AEGChessUnit>>{});
 TestTrue(TEXT("Targetless shield starts through GAS"),F.Melee->RequestUltimate());TestEqual(TEXT("Successful cast consumes mana"),F.Melee->GetAttributes()->GetMana(),0.f);
 S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Shield is max health ratio"),S->GetSourceShield(TEXT("MonsterShield")),2500.f);S->CancelAction();
 auto* R=F.Ranged;auto* RS=R->GetHeroSkill();R->GetCombat()->ResetCombat();R->GetCombat()->TickComponent(.001f,LEVELTICK_All,nullptr);RS->CancelAction();
 R->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 TestTrue(TEXT("Enhanced shot starts through GAS"),R->RequestUltimate());const float HP=F.Hero->GetAttributes()->GetHealth();
 RS->AnimationEvent(TEXT("Release"));TestEqual(TEXT("No damage before projectile impact"),F.Hero->GetAttributes()->GetHealth(),HP);
 F.Tick(1);
 TestTrue(TEXT("Enhanced ratio applied at impact"),FMath::IsNearlyEqual(HP-F.Hero->GetAttributes()->GetHealth(),R->GetAttributes()->GetAttackPower()*1.8f,.01f));
 RS->CancelAction();R->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 TestTrue(TEXT("Next ranged cast"),R->RequestUltimate());F.Hero->SetActorLocation(FVector(10000,0,88));RS->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("Target leaves before release refunds mana"),R->GetAttributes()->GetMana(),100.f);RS->CancelAction();
 TestFalse(TEXT("No valid target does not activate"),R->RequestUltimate());
 F.GM->BattleRules->Stop();TestEqual(TEXT("End clears shields"),S->Shield,0.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterHealer,"ElyGambit.Monsters.Skills.HealingAndSnapshotBuff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterHealer::RunTest(const FString&)
{
 FMonsterWorld F;auto* D=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Enemies/Support/Data/DA_Monster_Support"));
 if(!TestNotNull(TEXT("Authored mushroom definition"),D))return false;
 auto* H=F.GM->SpawnBattleSummon(D,1,FVector(150,180,88),EEGFaction::Enemy);if(!TestNotNull(TEXT("Mushroom"),H))return false;
 H->GetHeroSkill()->Configure(F.GM->BattleRules,1,false);F.GM->BattleRules->EnterUnit(H);H->GetCombat()->SetComponentTickEnabled(false);
 auto* C=H->GetCombat();auto* S=H->GetHeroSkill();
 TestNull(TEXT("Full health allies produce no target"),C->FindHealingTarget());
 F.Melee->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),3000);
 F.Ranged->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000);
 TestTrue(TEXT("Lowest fraction wins"),C->FindHealingTarget()==F.Melee);
 F.Ranged->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),3000);
 TestTrue(TEXT("Equal fraction picks nearest"),C->FindHealingTarget()==F.Ranged);
 F.Ranged->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000);
 const float Before=F.Melee->GetAttributes()->GetHealth();const float Mana=H->GetAttributes()->GetMana();const int32 Hits=F.GM->BattleRules->GetAttackCount();
 TestTrue(TEXT("Healer basic action"),H->RequestBasicAttack());S->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Eight percent target max health"),F.Melee->GetAttributes()->GetHealth()-Before,800.f);
 TestEqual(TEXT("Effective healing grants configured mana"),H->GetAttributes()->GetMana(),Mana+D->GetManaOnHit());
 TestEqual(TEXT("Healing does not emit attack hit"),F.GM->BattleRules->GetAttackCount(),Hits);
 S->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Duplicate notify grants no extra mana"),H->GetAttributes()->GetMana(),Mana+D->GetManaOnHit());S->CancelAction();
 const float Speed=H->GetAttributes()->GetAttackSpeed();H->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 TestTrue(TEXT("Healer team skill"),H->RequestUltimate());S->AnimationEvent(TEXT("Release"));S->CancelAction();
 TestTrue(TEXT("Self attack speed multiplied"),FMath::IsNearlyEqual(H->GetAttributes()->GetAttackSpeed(),Speed*1.25f));
 F.GM->BattleRules->ApplyTeamSpeed(H,.25f,5,true);TestTrue(TEXT("Same buff replaces instead of stacks"),FMath::IsNearlyEqual(H->GetAttributes()->GetAttackSpeed(),Speed*1.25f));
 auto* Late=F.GM->SpawnBattleSummon(D,1,FVector(900,500,88),EEGFaction::Enemy);Late->GetHeroSkill()->Configure(F.GM->BattleRules,1,false);F.GM->BattleRules->EnterUnit(Late);Late->GetCombat()->SetComponentTickEnabled(false);
 TestEqual(TEXT("Later entrant excluded from snapshot"),Late->GetAttributes()->GetAttackSpeed(),Speed);
 F.Tick(5.2f);TestEqual(TEXT("Buff expiration restores speed"),H->GetAttributes()->GetAttackSpeed(),Speed);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterMine,"ElyGambit.Monsters.Skills.EnvironmentMine",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterMine::RunTest(const FString&)
{
 FMonsterWorld F;auto* B=F.GM->BattleRules.Get();auto* Data=NewObject<UEGEnvironmentTrapDefinition>();Data->Damage=30;
 auto* Mine=F.W->SpawnActor<AEGCorruptionMine>(FVector(0,0,0),FRotator::ZeroRotator);Mine->Configure(Data,500);
 const float HP=F.Hero->GetAttributes()->GetHealth();F.Tick(.1f);TestFalse(TEXT("Preparation cannot trigger"),Mine->bTriggered);
 Mine->Arm(B);F.Tick(.01f);TestTrue(TEXT("Player entry triggers"),Mine->bTriggered);
 const float Remaining=Mine->Remaining;F.Tick(.1f);
 TestTrue(TEXT("Armed mine countdown advances normally"),Mine->Remaining<Remaining);
 F.Tick(.7f);TestEqual(TEXT("Mine deals one damage"),HP-F.Hero->GetAttributes()->GetHealth(),30.f);
 TestTrue(TEXT("Receipt identifies environment"),F.Hero->LastCombatResolution.bEnvironmentDamage);TestEqual(TEXT("Mine awards no basic hits"),B->GetAttackCount(),0);
 F.Tick(.8f);TestEqual(TEXT("No repeated explosion"),HP-F.Hero->GetAttributes()->GetHealth(),30.f);
 auto* BubbleMine=F.W->SpawnActor<AEGCorruptionMine>();BubbleMine->Configure(Data,500);BubbleMine->Arm(B);F.Hero->GetHeroSkill()->GrantPrismaticBubble(5,.2f,2);F.Tick(.8f);
 TestEqual(TEXT("Bubble blocks environment explosion"),HP-F.Hero->GetAttributes()->GetHealth(),30.f);
 auto* Last=F.W->SpawnActor<AEGCorruptionMine>();Last->Configure(Data,500);Last->Arm(B);F.Tick(.01f);B->Stop();F.Tick(.8f);
 TestTrue(TEXT("Round end invalidates armed trap"),!IsValid(Last)||Last->IsActorBeingDestroyed());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterMineUnits,"ElyGambit.Monsters.Skills.MineSummonAndEmplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterMineUnits::RunTest(const FString&)
{
 FMonsterWorld F;auto* B=F.GM->BattleRules.Get();
 auto* CanDef=F.Hero->GetDefinition()->SummonDefinition.Get();
 if(!TestNotNull(TEXT("Can definition"),CanDef))return false;
 auto* Can=B->SpawnSummon(CanDef,F.Hero->GetInstanceId(),1,FVector(80,0,88));
 if(!TestNotNull(TEXT("Can spawned through battle"),Can))return false;
 Can->GetCombat()->SetComponentTickEnabled(false);Can->SetActorLocation(FVector(80,0,88));
 auto* VillDef=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV"));
 auto* VillRules=VillDef?Cast<UEGVillVSkillDefinition>(VillDef->SkillDefinition):nullptr;
 if(!TestNotNull(TEXT("VillV device rules"),VillRules))return false;
 auto* Pilot=F.GM->SpawnBattleSummon(VillDef,1,FVector(2000,0,88),EEGFaction::Player);
 if(!TestNotNull(TEXT("VillV pilot"),Pilot))return false;
 Pilot->GetHeroSkill()->Configure(B,1,false);B->EnterUnit(Pilot);Pilot->GetCombat()->SetComponentTickEnabled(false);
 auto* Device=F.GM->SpawnBattleEmplacement(VillRules->GatlingDefinition.LoadSynchronous(),Pilot,0,FVector(120,0,88));
 if(!TestNotNull(TEXT("Independent emplacement"),Device))return false;
 Device->SetActorLocation(FVector(120,0,88));
 for(auto* U:{Can,static_cast<AEGChessUnit*>(Device)}){U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),1000);U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000);U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);}
 auto* Data=NewObject<UEGEnvironmentTrapDefinition>();Data->Damage=30;
 auto* Mine=F.W->SpawnActor<AEGCorruptionMine>();Mine->Configure(Data,500);Mine->Arm(B);F.Tick(.8f);
 TestEqual(TEXT("Summon hit once"),Can->GetAttributes()->GetHealth(),970.f);
 TestEqual(TEXT("Fixed device hit once"),Device->GetAttributes()->GetHealth(),970.f);
 TestEqual(TEXT("No basic attack credited to environment"),B->GetAttackCount(),0);return true;
}
#endif
