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
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Skills/EGGriseoSkillBehaviors.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
namespace
{
struct FGriseoWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGCardManager* Cards;AEGChessUnit *Griseo=nullptr,*Pardo=nullptr,*Enemy=nullptr,*Knight=nullptr;
 float OpeningOwnerHealth=0,OpeningOwnerAttack=0,OpeningKnightHealth=0,OpeningKnightAttack=0;
 FGriseoWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return;
  GM->RunDefinition=DuplicateObject<UEGRunDefinition>(Base,GM);
  if(!EGProductionTestAssets::ConfigureBehaviorEnemies(GM->RunDefinition.Get(),Test))return;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo"),TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo")};
  for(int32 I=0;I<2;++I)if(auto* C=LoadObject<UEGCardDefinition>(nullptr,Paths[I])){Cards->GrantCard(C);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{I+1,2});}
  GM->EnterCombat();
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())if(U&&U->GetDefinition())
  {
   if(U->GetDefinition()->HeroKind==EEGHeroKind::Griseo){OpeningOwnerHealth=U->GetAttributes()->GetMaxHealth();OpeningOwnerAttack=U->GetAttributes()->GetAttackPower();}
   else if(U->GetDefinition()->HeroKind==EEGHeroKind::Knight){Knight=U;OpeningKnightHealth=U->GetAttributes()->GetMaxHealth();OpeningKnightAttack=U->GetAttributes()->GetAttackPower();}
  }
  for(auto* U:GM->GetCombatUnits())
  {
   if(U->GetDefinition()->HeroKind==EEGHeroKind::Griseo)Griseo=U;
   else if(U->GetDefinition()->HeroKind==EEGHeroKind::Pardofelis)Pardo=U;
   else if(!Enemy&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemy=U;
   U->GetCombat()->SetComponentTickEnabled(false);auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),20000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),20000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
  }
 }
 ~FGriseoWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);for(auto* U:GM->GetCombatUnits())U->GetCombat()->SetComponentTickEnabled(false);}}
 UEGGriseoSkillBehavior* Policy()const{return Griseo?Cast<UEGGriseoSkillBehavior>(Griseo->GetHeroSkill()->GetBehavior()):nullptr;}
 bool Ready()const{return Griseo&&Pardo&&Enemy&&Knight&&Policy();}
 int32 Population()const{int32 N=0;for(const auto& H:GM->GetRoster()->GetHeroes())if(H.bDeployed)++N;return N;}
 int32 Knights()const{int32 N=0;for(auto* U:GM->GetCombatUnits())if(U&&U->GetDefinition()->HeroKind==EEGHeroKind::Knight&&IEGCombatant::Execute_IsAlive(U))++N;return N;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoBubble,"ElyGambit.Griseo.FrontlineBubbleAndDamageOrder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoBubble::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;
 auto* S=E.Griseo->GetHeroSkill();E.Tick(.1f);
 TestEqual(TEXT("One independently spawned knight"),E.Knights(),1);TestFalse(TEXT("Reworked Griseo has no automatic bubble"),S->HasPrismaticBubble());S->GrantPrismaticBubble(10,.3,3);
 const float HP=E.Griseo->GetAttributes()->GetHealth();S->SetShield(40);
 FEGCombatExecution::Damage(E.Enemy,E.Griseo,50000);
 TestEqual(TEXT("Bubble blocks lethal contact"),E.Griseo->GetAttributes()->GetHealth(),HP);
 TestEqual(TEXT("Bubble precedes ordinary shield"),S->Shield,40.f);
 TestEqual(TEXT("Blocked contact receipt"),E.Griseo->LastCombatResolution.BubbleAbsorbed,50000.f);
 TestEqual(TEXT("Blocked victim receives no mana"),E.Griseo->GetAttributes()->GetMana(),0.f);
 TestFalse(TEXT("One bubble is consumed"),S->HasPrismaticBubble());
 FEGCombatExecution::Damage(E.Enemy,E.Griseo,100);
 TestEqual(TEXT("Same-frame second contact is reduced then shielded"),E.Griseo->GetAttributes()->GetHealth(),HP-30);
 TestTrue(TEXT("Reduction receipt"),FMath::IsNearlyEqual(E.Griseo->LastCombatResolution.ProtectionPrevented,30.f,.001f));
 TestEqual(TEXT("Shield receipt"),E.Griseo->LastCombatResolution.ShieldAbsorbed,40.f);
 E.Tick(3.1f);TestEqual(TEXT("Break protection expires"),S->GetProtectionReduction(),0.f);
 S->GrantPrismaticBubble(1,.3,3);FEGCombatExecution::Damage(E.Enemy,E.Griseo,0);FEGCombatExecution::Heal(E.Griseo,10);
 TestTrue(TEXT("Zero and healing preserve bubble"),S->HasPrismaticBubble());E.Tick(1.1f);
 TestFalse(TEXT("Bubble naturally expires"),S->HasPrismaticBubble());TestEqual(TEXT("Expiry grants no reduction"),S->GetProtectionReduction(),0.f);
 S->GrantPrismaticBubble(10,.3,3);FEGCombatExecution::Damage(E.Enemy,E.Griseo,100,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid(),true);
 TestEqual(TEXT("Linked damage also consumes bubble"),E.Griseo->LastCombatResolution.BubbleAbsorbed,100.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoCadence,"ElyGambit.Griseo.FiveHitsDedupAndRefresh",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoCadence::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;auto* S=E.Griseo->GetHeroSkill();S->ClearProtection();
 E.Enemy->SetActorLocation(E.Griseo->GetActorLocation()+FVector(400,0,0));
 for(int32 I=0;I<5;++I)
 {
  if(!TestTrue(TEXT("Begin ordinary melee action"),S->BeginAction(false)))return false;
  const float Before=E.Enemy->GetAttributes()->GetHealth();const float Mana=E.Griseo->GetAttributes()->GetMana();
  S->AnimationEvent(TEXT("Hit"));const float After=E.Enemy->GetAttributes()->GetHealth();
  TestTrue(TEXT("Direct melee hit"),After<Before);S->AnimationEvent(TEXT("Hit"));
  TestEqual(TEXT("Repeated notification never damages twice"),E.Enemy->GetAttributes()->GetHealth(),After);
  TestEqual(TEXT("One mana payment"),E.Griseo->GetAttributes()->GetMana(),Mana+15);
  TestEqual(TEXT("One passive count per committed hit"),E.Policy()->BubbleHits,0);
  TestEqual(TEXT("No retired automatic bubble"),S->HasPrismaticBubble(),false);S->AnimationEvent(TEXT("Finish"));
 }
 E.Tick(8);S->GrantPrismaticBubble(10,.3,3);E.Tick(3);TestTrue(TEXT("Refresh renews one charge"),S->HasPrismaticBubble());
 FEGCombatExecution::Damage(E.Enemy,E.Griseo,100);TestFalse(TEXT("Refresh did not stack charges"),S->HasPrismaticBubble());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoField,"ElyGambit.Griseo.FixedFieldBoundaryFactionsAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoField::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;
 auto* S=E.Griseo->GetHeroSkill();auto* ASC=E.Griseo->GetAbilitySystemComponent();
 E.Griseo->SetActorLocation(FVector(0,0,100));E.Knight->SetActorLocation(FVector(1500,0,100));E.Pardo->SetActorLocation(FVector(2500,0,100));E.Enemy->SetActorLocation(FVector(1600,0,100));
 for(int32 Level:{1,6,11})
 {
  S->ApplyPersistentLevel(Level);ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),1000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Griseo->GetAttributes()->GetMaxMana());
  const float Attack=E.Griseo->GetAttributes()->GetAttackPower();
  if(!TestTrue(TEXT("Support ultimate begins"),S->BeginAction(true)))return false;
  S->AnimationEvent(TEXT("Release"));const float Expected=Level>=6?300.f:200.f;
  TestEqual(TEXT("Knight receives tier shield"),E.Knight->GetHeroSkill()->Shield,Expected);
  TestEqual(TEXT("Ally at two-cell boundary receives shield"),E.Pardo->GetHeroSkill()->Shield,Expected);
  TestEqual(TEXT("Enemy excluded from shields"),E.Enemy->GetHeroSkill()->Shield,0.f);
  TestEqual(TEXT("Remote caster not in knight shield"),S->Shield,0.f);
  TestTrue(TEXT("Only level 11 grants caster attack"),FMath::IsNearlyEqual(E.Griseo->GetAttributes()->GetAttackPower(),Attack*(Level==11?1.2f:1.f),.01f));
  S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Duplicate release never stacks shield"),E.Knight->GetHeroSkill()->Shield,Expected);
  S->AnimationEvent(TEXT("Finish"));E.Pardo->SetActorLocation(FVector(5000,0,100));
  TestEqual(TEXT("Leaving range preserves snapshot shield"),E.Pardo->GetHeroSkill()->Shield,Expected);
  E.Tick(6.1f);TestEqual(TEXT("Shield expires"),E.Pardo->GetHeroSkill()->Shield,0.f);
  TestTrue(TEXT("Attack bonus expires"),FMath::IsNearlyEqual(E.Griseo->GetAttributes()->GetAttackPower(),Attack,.01f));
  E.Pardo->SetActorLocation(FVector(2500,0,100));
 }
 E.Knight->Die();const float Before=E.Griseo->GetAttributes()->GetAttackPower();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Griseo->GetAttributes()->GetMaxMana());
 if(!S->BeginAction(true))return false;S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 TestTrue(TEXT("Dead knight does not suppress level 11 attack branch"),E.Griseo->GetAttributes()->GetAttackPower()>Before);
 E.GM->BattleRules->Stop();TestTrue(TEXT("Round stop removes support attack"),FMath::IsNearlyEqual(E.Griseo->GetAttributes()->GetAttackPower(),Before,.01f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoExpiry,"ElyGambit.Griseo.FieldExpiryAndReconfigure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoExpiry::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;
 auto* S=E.Griseo->GetHeroSkill();
 S->GrantSourceShield(TEXT("TestSupport"),100,6,E.Griseo,true);S->AbsorbShield(30);
 S->GrantSourceShield(TEXT("TestSupport"),50,6,E.Griseo,true);TestEqual(TEXT("Weaker recast preserves remaining shield"),S->Shield,70.f);
 E.Griseo->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetShieldMultiplierAttribute(),2);
 S->GrantSourceShield(TEXT("TestSupport"),50,6,E.Griseo,true);TestEqual(TEXT("Shield multiplier applied once before max"),S->Shield,100.f);
 S->EndBattle();TestEqual(TEXT("Round cleanup clears shield"),S->Shield,0.f);
 S->Configure(E.GM->BattleRules,1,false);TestFalse(TEXT("Reconfigure never restores retired bubble"),S->HasPrismaticBubble());
 TestEqual(TEXT("Reconfigure keeps one knight"),E.Knights(),1);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoOpeningKnight,"ElyGambit.Griseo.OpeningKnightLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoOpeningKnight::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture and opening Knight"),E.Ready()))return false;
 TestEqual(TEXT("Exactly one opening Knight"),E.Knights(),1);
 TestTrue(TEXT("Knight starts at owner's max health"),FMath::IsNearlyEqual(E.OpeningKnightHealth,E.OpeningOwnerHealth,.01f));
 TestTrue(TEXT("Knight starts at one quarter of owner's attack"),FMath::IsNearlyEqual(E.OpeningKnightAttack,E.OpeningOwnerAttack*.25f,.01f));
 TestEqual(TEXT("Summon does not consume deployed roster population"),E.Population(),2);
 TestEqual(TEXT("Summoner identity"),E.Knight->GetHeroSkill()->SummonerId,E.Griseo->GetInstanceId());
 const FVector Owner=E.Griseo->GetActorLocation(),Offset=E.Knight->GetActorLocation()-Owner;
 AEGChessUnit* Nearest=nullptr;double Distance=DBL_MAX;
 for(auto* U:E.GM->GetCombatUnits())if(U&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy&&IEGCombatant::Execute_IsAlive(U))
 {const double D=FVector::DistSquared2D(Owner,U->GetActorLocation());if(D<Distance){Distance=D;Nearest=U;}}
 if(TestNotNull(TEXT("Opening enemy"),Nearest))TestTrue(TEXT("Knight starts towards the enemy"),FVector::DotProduct(Offset.GetSafeNormal2D(),(Nearest->GetActorLocation()-Owner).GetSafeNormal2D())>0.f);
 const float Clearance=UEGCombatComponent::BlockingRadius(E.Griseo)+UEGCombatComponent::BlockingRadius(E.Knight);
 TestTrue(TEXT("Knight starts clear of owner"),FVector::Dist2D(Owner,E.Knight->GetActorLocation())>=Clearance);
 for(auto* U:E.GM->GetCombatUnits())if(U&&U!=E.Knight&&U->GetDefinition()&&IEGCombatant::Execute_IsAlive(U))
  TestTrue(TEXT("Knight starts clear of other living units"),FVector::Dist2D(U->GetActorLocation(),E.Knight->GetActorLocation())>=UEGCombatComponent::BlockingRadius(U)+UEGCombatComponent::BlockingRadius(E.Knight));
 E.Griseo->GetHeroSkill()->Configure(E.GM->BattleRules,1,false);
 TestEqual(TEXT("Reconfigure cannot duplicate Knight"),E.Knights(),1);
 E.Knight->Die();E.Griseo->GetHeroSkill()->Configure(E.GM->BattleRules,1,false);
 TestEqual(TEXT("Dead Knight is not resummoned this round"),E.Knights(),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoKnightHit,"ElyGambit.Griseo.KnightInjectedHitAndOwnerDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoKnightHit::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture and opening Knight"),E.Ready()))return false;
 E.Enemy->SetActorLocation(E.Knight->GetActorLocation()+FVector(100,0,0));
 auto* S=E.Knight->GetHeroSkill();
 if(!TestTrue(TEXT("Knight basic action starts"),S->BeginAction(false)))return false;
 const float Before=E.Enemy->GetAttributes()->GetHealth(),Mana=E.Knight->GetAttributes()->GetMana();
 const int32 Attacks=E.GM->BattleRules->GetAttackCount(),BubbleHits=E.Policy()->BubbleHits;
 // Inject the animation notify twice; this validates dispatch, not natural montage playback.
 S->AnimationEvent(TEXT("Hit"));const float After=E.Enemy->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Hit"));
 TestTrue(TEXT("Knight's injected hit damages enemy"),After<Before);
 TestEqual(TEXT("Repeated hit notify is deduplicated"),E.Enemy->GetAttributes()->GetHealth(),After);
 TestEqual(TEXT("Knight hit does not count as a hero attack"),E.GM->BattleRules->GetAttackCount(),Attacks);
 TestEqual(TEXT("Knight hit does not grant mana"),E.Knight->GetAttributes()->GetMana(),Mana);
 TestEqual(TEXT("Knight hit does not count toward owner's bubble"),E.Policy()->BubbleHits,BubbleHits);
 S->AnimationEvent(TEXT("Finish"));E.Griseo->Die();
 TestTrue(TEXT("Knight survives owner death"),IsValid(E.Knight)&&IEGCombatant::Execute_IsAlive(E.Knight));
 E.GM->BattleRules->Stop();
 TestEqual(TEXT("Stop empties summon registry"),E.GM->BattleRules->SummonCount(),0);
 TestFalse(TEXT("Stop destroys Knight"),IsValid(E.Knight));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoKnightNatural,"ElyGambit.Griseo.KnightNaturalChaseAndMontage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoKnightNatural::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture and opening Knight"),E.Ready()))return false;
 // Controlled health/position fixture; no animation events or damage injected.
 // Isolate the target while leaving the Knight's movement, ability, AnimGraph
 // and notify pipeline to advance from the world's normal ticks.
 int32 Offboard=0;
 for(auto* U:E.GM->GetCombatUnits())if(U!=E.Knight&&U!=E.Enemy)U->SetActorLocation(FVector(-5000.f,++Offboard*800.f,88.f));
 E.Knight->SetActorLocation(FVector(-600,0,88));E.Enemy->SetActorLocation(FVector(800,0,88));
 E.Knight->GetCombat()->SetComponentTickEnabled(true);
 const float Before=E.Enemy->GetAttributes()->GetHealth();const FVector Start=E.Knight->GetActorLocation();
 bool bHit=false;
 for(int32 I=0;I<600&&!bHit;++I)
 {++GFrameCounter;E.W->Tick(LEVELTICK_All,.025f);bHit=E.Enemy->GetAttributes()->GetHealth()<Before;}
 TestTrue(TEXT("AI chases into melee range"),FVector::Dist2D(Start,E.Knight->GetActorLocation())>100.f);
 TestTrue(TEXT("Real montage notification commits damage"),bHit);
 if(bHit)TestTrue(TEXT("First natural stroke commits exactly one attack"),FMath::IsNearlyEqual(Before-E.Enemy->GetAttributes()->GetHealth(),E.Knight->GetAttributes()->GetAttackPower(),.1f));
 TestEqual(TEXT("Natural summon attack does not count as hero basic hit"),E.GM->BattleRules->GetAttackCount(),0);
 const float KnightHP=E.Knight->GetAttributes()->GetHealth();
 E.Enemy->GetCombat()->SetComponentTickEnabled(true);
 for(int32 I=0;I<400&&E.Knight->GetAttributes()->GetHealth()>=KnightHP;++I){++GFrameCounter;E.W->Tick(LEVELTICK_All,.025f);}
 TestTrue(TEXT("Enemy selects and damages independent Knight"),E.Enemy->GetCombat()->GetTarget()==E.Knight&&E.Knight->GetAttributes()->GetHealth()<KnightHP);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoKnightNoSpace,"ElyGambit.Griseo.KnightBlockedSpawnRollsBack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoKnightNoSpace::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;
 auto* Blocker=E.W->SpawnActor<AActor>();auto* Capsule=NewObject<UCapsuleComponent>(Blocker);
 Blocker->SetRootComponent(Capsule);Capsule->InitCapsuleSize(10000.f,10000.f);
 Capsule->SetCollisionObjectType(ECC_Pawn);Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Capsule->SetCollisionResponseToAllChannels(ECR_Block);
 Capsule->RegisterComponent();Blocker->SetActorLocation(FVector(0,0,88));
 auto* D=E.Knight->GetDefinition();auto* B=E.GM->BattleRules.Get();const auto Id=E.Pardo->GetInstanceId();
 TestNull(TEXT("Noncombat Pawn blocker prevents overlapping spawn"),B->SpawnSummon(D,Id,1,E.Pardo->GetActorLocation()));
 TestEqual(TEXT("Failed placement leaves only original Knight"),E.Knights(),1);
 Blocker->Destroy();
 TestNotNull(TEXT("Failed placement does not spend owner's summon"),B->SpawnSummon(D,Id,1,E.Pardo->GetActorLocation()));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoFormBackend,"ElyGambit.Griseo.FormBackendPreservesRoster",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoFormBackend::RunTest(const FString&)
{
 FGriseoWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;
 auto* R=E.GM->GetRoster();const FGuid Id=E.Griseo->GetInstanceId();auto* Base=E.Griseo->GetDefinition();
 if(!TestNotNull(TEXT("Base form formally references adult form"),Base->EnhancedDefinition.Get()))return false;
 // Controlled persisted growth state. This exercises the backend transition,
 // not an unimplemented stigmata reward/selection UI.
 auto State=R->CaptureRunState();for(auto& H:State.Heroes)if(H.InstanceId==Id){H.Level=7;H.bAwakened=true;}
 R->RestoreRunState(State);FEGHeroInstance Before;R->GetHero(Id,Before);
 TestTrue(TEXT("Resolve battle"),E.GM->ResolveCombat(true));
 TestTrue(TEXT("Existing backend unlocks linked form"),R->UnlockForm(Base));
 FEGHeroInstance After;R->GetHero(Id,After);
 TestEqual(TEXT("Same identity"),After.InstanceId,Before.InstanceId);TestEqual(TEXT("Same level"),After.Level,Before.Level);
 TestEqual(TEXT("Same awakening"),After.bAwakened,Before.bAwakened);TestEqual(TEXT("Same board cell"),After.DeploymentCell,Before.DeploymentCell);
 TestEqual(TEXT("Same deployed population"),E.Population(),2);
 TestTrue(TEXT("Old round Knight destroyed"),!IsValid(E.Knight));
 if(!TestTrue(TEXT("Next stage rebuilds incarnations"),E.GM->NextStage()))return false;
 AEGChessUnit* Adult=nullptr;for(auto* U:E.GM->GetCombatUnits())if(U->GetInstanceId()==Id)Adult=U;
 if(!TestNotNull(TEXT("Enhanced incarnation"),Adult))return false;
 TestEqual(TEXT("Adult definition resolved"),Adult->GetDefinition()->HeroKind,EEGHeroKind::GriseoAdult);
 TestTrue(TEXT("Next combat begins"),E.GM->EnterCombat());E.GM->BattleRules->CompleteOpeningForTests();TestEqual(TEXT("Only one Knight after form change"),E.Knights(),1);
 TestEqual(TEXT("Population unchanged after new combat"),E.Population(),2);
 return true;
}
#endif
