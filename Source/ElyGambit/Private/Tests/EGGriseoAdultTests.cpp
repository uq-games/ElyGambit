#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGGriseoAdultSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Skills/EGGriseoAdultSkillBehavior.h"
#include "Presentation/EGSourceEffect.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"

namespace
{
struct FAdultGriseoWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Enemy=nullptr;AEGChessUnit* Knight=nullptr;AEGChessUnit* Pardo=nullptr;
 float OpeningHeroMaxHealth=0,OpeningHeroAttack=0,OpeningKnightMaxHealth=0,OpeningKnightHealth=0,OpeningKnightAttack=0;
 FAdultGriseoWorld(FAutomationTestBase& Test,bool bWithPardo=false):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  GM->RunDefinition=EGProductionTestAssets::ResolveRun(Test);
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult"));
  if(Card&&Cards->GrantCard(Card)&&!Cards->GetHand().IsEmpty())Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2});
  if(bWithPardo)if(auto* PardoCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo")))
   if(Cards->GrantCard(PardoCard)&&!Cards->GetHand().IsEmpty())Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{1,2});
  GM->EnterCombat();
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())
  {
   if(!IsValid(U)||!U->GetDefinition())continue;
   U->GetCombat()->SetComponentTickEnabled(false);
   if(U->GetDefinition()->HeroKind==EEGHeroKind::GriseoAdult)
   {OpeningHeroMaxHealth=U->GetAttributes()->GetMaxHealth();OpeningHeroAttack=U->GetAttributes()->GetAttackPower();}
   if(U->GetDefinition()->HeroKind==EEGHeroKind::Knight)
   {OpeningKnightMaxHealth=U->GetAttributes()->GetMaxHealth();OpeningKnightHealth=U->GetAttributes()->GetHealth();OpeningKnightAttack=U->GetAttributes()->GetAttackPower();}
   auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
   if(U->GetDefinition()->HeroKind==EEGHeroKind::GriseoAdult)Hero=U;
   else if(U->GetDefinition()->HeroKind==EEGHeroKind::Knight)Knight=U;
   else if(U->GetDefinition()->HeroKind==EEGHeroKind::Pardofelis)Pardo=U;
   else if(!Enemy&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemy=U;
  }
  if(Hero&&Enemy)Enemy->SetActorLocation(Hero->GetActorLocation()+FVector(150,0,0));
 }
 ~FAdultGriseoWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 UEGGriseoAdultSkillBehavior* Policy() const{return Hero?Cast<UEGGriseoAdultSkillBehavior>(Hero->GetHeroSkill()->GetBehavior()):nullptr;}
 bool Ready()const{return Hero&&Enemy&&Knight&&Policy();}
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.05f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}}
 FEGSkillActionContext Context(bool Ultimate=false)const
 {FEGSkillActionContext C;C.RoundId=GM->BattleRules->GetRoundId();C.ActionId=FGuid::NewGuid();C.Target=Enemy;C.Attack=100;C.Multiplier=3.f;C.bUltimate=Ultimate;return C;}
 int32 LivingKnights()const{int32 N=0;for(auto* U:GM->GetCombatUnits())if(IsValid(U)&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::Knight&&IEGCombatant::Execute_IsAlive(U))++N;return N;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoKnight,"ElyGambit.GriseoAdult.KnightOncePerRound",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoKnight::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this);if(!TestTrue(TEXT("Adult fixture and opening Knight"),E.Ready()))return false;
 TestEqual(TEXT("One independent Knight"),E.LivingKnights(),1);
 TestEqual(TEXT("Knight owns summoner identity"),E.Knight->GetHeroSkill()->SummonerId,E.Hero->GetInstanceId());
 TestTrue(TEXT("Knight max health snapshots 100% of owner"),FMath::IsNearlyEqual(E.OpeningKnightMaxHealth,E.OpeningHeroMaxHealth*1.f,.01f));
 TestTrue(TEXT("Knight starts at full health"),FMath::IsNearlyEqual(E.OpeningKnightHealth,E.OpeningKnightMaxHealth,.01f));
 TestTrue(TEXT("Knight attack snapshots 25% of owner"),FMath::IsNearlyEqual(E.OpeningKnightAttack,E.OpeningHeroAttack*.25f,.01f));
 E.Hero->GetHeroSkill()->Configure(E.GM->BattleRules,1,false);
 TestEqual(TEXT("Same-round reconfigure does not create a second Knight"),E.LivingKnights(),1);
 E.Knight->Die();E.Hero->GetHeroSkill()->Configure(E.GM->BattleRules,1,false);
 TestEqual(TEXT("Dead Knight is not resummoned"),E.LivingKnights(),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoAura,"ElyGambit.GriseoAdult.KnightAuraLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoAura::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this);if(!TestTrue(TEXT("Adult fixture and opening Knight"),E.Ready()))return false;
 const auto* D=Cast<UEGGriseoAdultSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Adult definition"),D))return false;
 const float HeroBase=E.Hero->GetDefinition()->GetBaseStats().AttackSpeed;
 const float KnightBase=E.Knight->GetDefinition()->GetBaseStats().AttackSpeed;
 TestEqual(TEXT("Default aura bonus"),D->PassiveSpeedBonus,.10f);
 TestTrue(TEXT("Living Knight buffs allied hero"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),HeroBase*1.10f,.01f));
 TestTrue(TEXT("Knight itself is excluded"),FMath::IsNearlyEqual(E.Knight->GetAttributes()->GetAttackSpeed(),KnightBase,.01f));
 E.GM->BattleRules->RegisterAdultKnightAura(E.Knight,D->PassiveSpeedBonus);
 TestTrue(TEXT("Same source cannot stack"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),HeroBase*1.10f,.01f));
 E.GM->BattleRules->ApplyTeamSpeed(E.Hero,D->TeamSpeedBonus,D->TeamSpeedSeconds);
 TestTrue(TEXT("Ultimate and aura both apply"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),HeroBase*1.30f,.01f));
 E.Knight->Die();
 TestTrue(TEXT("Knight death removes aura but keeps ultimate"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),HeroBase*1.20f,.01f));
 E.GM->BattleRules->Stop();
 TestTrue(TEXT("Battle stop removes remaining bonus"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),HeroBase,.01f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoAuraSources,"ElyGambit.GriseoAdult.AuraSourcesAndOwnerDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoAuraSources::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this,true);if(!TestTrue(TEXT("Adult and allied Pardo fixture"),E.Ready()&&E.Pardo))return false;
 const float Base=E.Pardo->GetAbilitySystemComponent()->GetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute());
 TestTrue(TEXT("Other allied hero receives aura"),FMath::IsNearlyEqual(E.Pardo->GetAttributes()->GetAttackSpeed(),Base*1.10f,.01f));
 auto* Other=E.GM->BattleRules->SpawnSummon(E.Hero->GetDefinition()->SummonDefinition.Get(),E.Pardo->GetInstanceId(),1,E.Pardo->GetActorLocation()+FVector(300,0,0));
 if(!TestNotNull(TEXT("Second independent Knight source"),Other))return false;
 E.GM->BattleRules->RegisterAdultKnightAura(Other,.20f);
 TestTrue(TEXT("Different aura sources use strongest instead of stacking"),FMath::IsNearlyEqual(E.Pardo->GetAttributes()->GetAttackSpeed(),Base*1.20f,.01f));
 Other->Die();
 TestTrue(TEXT("Removing strongest source restores remaining aura"),FMath::IsNearlyEqual(E.Pardo->GetAttributes()->GetAttackSpeed(),Base*1.10f,.01f));
 E.Hero->Die();
 TestTrue(TEXT("Owner death leaves Knight alive"),IEGCombatant::Execute_IsAlive(E.Knight));
 TestTrue(TEXT("Owner death leaves Knight aura on allies"),FMath::IsNearlyEqual(E.Pardo->GetAttributes()->GetAttackSpeed(),Base*1.10f,.01f));
 E.Knight->Die();
 TestTrue(TEXT("Last Knight death clears aura"),FMath::IsNearlyEqual(E.Pardo->GetAttributes()->GetAttackSpeed(),Base,.01f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoKnightOwner,"ElyGambit.GriseoAdult.KnightSurvivesOwner",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoKnightOwner::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this);if(!TestTrue(TEXT("Adult fixture and opening Knight"),E.Ready()))return false;
 E.Hero->Die();TestTrue(TEXT("Independent Knight remains alive when owner dies"),IsValid(E.Knight)&&IEGCombatant::Execute_IsAlive(E.Knight));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoActions,"ElyGambit.GriseoAdult.FourMontagesAndProtection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoActions::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this);if(!TestTrue(TEXT("Adult fixture"),E.Ready()))return false;
 auto* Skill=E.Hero->GetHeroSkill();const auto* P=E.Hero->GetDefinition()->PresentationDefinition.Get();
 if(!TestNotNull(TEXT("Four action presentation"),P)||!TestEqual(TEXT("Four montage entries"),P->AttackSequence.Num(),4))return false;
 for(int32 I=0;I<20;++I)
 {
  if(!TestTrue(TEXT("Begin melee through action transaction"),Skill->BeginAction(false)))return false;
  TestTrue(TEXT("Expected montage in four-action cycle"),Skill->GetActiveMontage()==P->AttackSequence[I%4].Montage.LoadSynchronous());
  const float Before=E.Enemy->GetAttributes()->GetHealth();Skill->AnimationEvent(TEXT("Hit"));const float After=E.Enemy->GetAttributes()->GetHealth();
  TestTrue(TEXT("Melee hit committed"),After<Before);Skill->AnimationEvent(TEXT("Hit"));
  TestEqual(TEXT("Duplicate hit ignored"),E.Enemy->GetAttributes()->GetHealth(),After);Skill->AnimationEvent(TEXT("Finish"));
 }
 TestEqual(TEXT("Twenty attacks do not create extra Knights"),E.LivingKnights(),1);
 auto* ASC=E.Hero->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Hero->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("Begin ultimate through action transaction"),Skill->BeginAction(true)))return false;
 TestTrue(TEXT("Casting protected"),Skill->IsActionProtected());Skill->AnimationEvent(TEXT("Finish"));
 TestFalse(TEXT("Finish clears protection"),Skill->IsActionProtected());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoTarget,"ElyGambit.GriseoAdult.UltimateTargetEligibilityAndLockedPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoTarget::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this);if(!TestTrue(TEXT("Adult fixture"),E.Ready()))return false;
 auto* Skill=E.Hero->GetHeroSkill();auto* ASC=E.Hero->GetAbilitySystemComponent();
 for(auto* U:E.GM->BattleRules->Units())if(IsValid(U)&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)U->GetHeroSkill()->SetActionProtected(true);
 const float Full=E.Hero->GetAttributes()->GetMaxMana();ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Full);
 TestFalse(TEXT("No targetable enemy rejects cast"),Skill->BeginAction(true));
 TestEqual(TEXT("Rejected cast keeps full mana"),E.Hero->GetAttributes()->GetMana(),Full);
 E.Enemy->GetHeroSkill()->SetActionProtected(false);const FVector Locked=E.Enemy->GetActorLocation();
 if(!TestTrue(TEXT("Valid target permits ultimate"),Skill->BeginAction(true)))return false;
 const FVector Ground=Locked-FVector(0,0,88);E.Enemy->SetActorLocation(Locked+FVector(300,0,0));
 TestTrue(TEXT("Presentation ground remains at start point"),Skill->GetPresentationTargetGround().Equals(Ground,.1f));
 Skill->CancelAction();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAdultGriseoSkill,"ElyGambit.GriseoAdult.LockedMeteorAndTeamSpeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAdultGriseoSkill::RunTest(const FString&)
{
 FAdultGriseoWorld E(*this);if(!TestTrue(TEXT("Adult fixture"),E.Ready()))return false;
 auto* S=E.Policy();const auto* D=Cast<UEGGriseoAdultSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Skill definition"),D))return false;
 auto* Meteor=D->MeteorImpact.LoadSynchronous();
 if(TestNotNull(TEXT("Meteor source effect loads"),Meteor))
 {
  TestTrue(TEXT("Meteor uses locked ground target"),Meteor->bTargetGround);
  TestTrue(TEXT("Meteor contains source tracks"),!Meteor->Tracks.IsEmpty());
  for(const auto& Track:Meteor->Tracks)
  {
   TestNotNull(TEXT("Meteor track mesh loads"),Track.Mesh.LoadSynchronous());
   TestNotNull(TEXT("Meteor track material loads"),Track.Material.LoadSynchronous());
  }
 }
 TestEqual(TEXT("Radius"),D->UltimateRadius,1000.f);TestEqual(TEXT("Team speed"),D->TeamSpeedBonus,.2f);TestEqual(TEXT("Duration"),D->TeamSpeedSeconds,6.f);
 TestTrue(TEXT("Tier multipliers"),D->SkillMultipliers.Equals(FVector(3,3,3),.01));
 auto C=E.Context();S->ActionStarted(C);const float BasicBefore=E.Enemy->GetAttributes()->GetHealth();
 S->ActionEvent(TEXT("Hit"),C);S->ActionEvent(TEXT("Hit"),C);
 TestTrue(TEXT("Basic hit submitted once"),FMath::IsNearlyEqual(BasicBefore-E.Enemy->GetAttributes()->GetHealth(),100.f,.1f));S->ActionEnded(true);
 const FVector Locked=E.Enemy->GetActorLocation();C=E.Context(true);S->ActionStarted(C);
 TestTrue(TEXT("Ultimate action protects caster"),E.Hero->GetHeroSkill()->IsActionProtected());
 E.Enemy->SetActorLocation(Locked+FVector(100,0,0));const float Before=E.Enemy->GetAttributes()->GetHealth();
 const float BaseSpeed=E.Knight->GetAttributes()->GetAttackSpeed();S->ActionEvent(TEXT("Release"),C);S->ActionEvent(TEXT("Release"),C);
 TestTrue(TEXT("Meteor hits once inside locked area"),FMath::IsNearlyEqual(Before-E.Enemy->GetAttributes()->GetHealth(),300.f*E.Hero->GetUltimateBonus(),.1f));
 TestTrue(TEXT("Existing Knight excluded from hero-only team speed"),FMath::IsNearlyEqual(E.Knight->GetAttributes()->GetAttackSpeed(),BaseSpeed,.01f));
 TestTrue(TEXT("Team speed active"),E.GM->BattleRules->GetTeamBuffSecondsFor(EEGFaction::Player)>5.9f);
 S->ActionEnded(true);E.Hero->GetHeroSkill()->SetActionProtected(false);
 E.Tick(3.f);
 E.Enemy->SetActorLocation(Locked);C=E.Context(true);S->ActionStarted(C);E.Enemy->SetActorLocation(Locked+FVector(1001,0,0));
 const float Outside=E.Enemy->GetAttributes()->GetHealth();S->ActionEvent(TEXT("Release"),C);
 TestEqual(TEXT("Target outside fixed cast point is not hit"),E.Enemy->GetAttributes()->GetHealth(),Outside);
 TestTrue(TEXT("Repeated ultimate still excludes Knight"),FMath::IsNearlyEqual(E.Knight->GetAttributes()->GetAttackSpeed(),BaseSpeed,.01f));
 TestTrue(TEXT("Hero speed refreshes without stacking"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),E.Hero->GetDefinition()->GetBaseStats().AttackSpeed*1.30f,.01f));
 TestTrue(TEXT("Repeated ultimate refreshes duration"),E.GM->BattleRules->GetTeamBuffSecondsFor(EEGFaction::Player)>5.9f);
 const auto* Pardo=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"));
 auto* Late=Pardo?E.GM->BattleRules->SpawnSummon(Pardo->SummonDefinition.Get(),E.Hero->GetInstanceId(),1,E.Hero->GetActorLocation()+FVector(300,0,0)):nullptr;
 float LateBase=0.f;
 if(TestNotNull(TEXT("Late allied Can summon"),Late))
 {
  Late->GetCombat()->SetComponentTickEnabled(false);
  LateBase=Late->GetAbilitySystemComponent()->GetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute());
  TestTrue(TEXT("Late summon receives neither hero-only ultimate speed nor aura"),FMath::IsNearlyEqual(Late->GetAttributes()->GetAttackSpeed(),LateBase,.01f));
 }
 E.Tick(6.2f);
 TestTrue(TEXT("Ultimate expires and Knight aura remains"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetAttackSpeed(),E.Hero->GetDefinition()->GetBaseStats().AttackSpeed*1.10f,.01f));
 if(IsValid(Late))TestTrue(TEXT("Late summon's ultimate speed expires"),FMath::IsNearlyEqual(Late->GetAttributes()->GetAttackSpeed(),LateBase,.01f));
 E.GM->BattleRules->Stop();TestEqual(TEXT("Stop removes speed"),E.Hero->GetAttributes()->GetAttackSpeed(),E.Hero->GetDefinition()->GetBaseStats().AttackSpeed);
 return true;
}
#endif
