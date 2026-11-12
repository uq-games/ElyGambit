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
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"

namespace
{
// Controlled fixture using the formal player/Corruption definitions. The
// encounter is copied and its enemies positioned deterministically; it is not
// a claim that the natural production encounter has been played.
struct FThreeUnitsGeometryWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGCardManager* Cards=nullptr;
 AEGDeploymentBoard* Grid=nullptr;
 AEGChessUnit* Human=nullptr;
 AEGChessUnit* PlayerElysia=nullptr;
 AEGChessUnit* Melee=nullptr;
 AEGChessUnit* Corruption=nullptr;

 explicit FThreeUnitsGeometryWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();
  if(!Test.TestNotNull(TEXT("Standalone world"),World))return;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Test.TestNotNull(TEXT("Game mode"),Mode))return;
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base||Base->Stages.Num()<2){Test.AddError(TEXT("Formal two-stage run is unavailable"));return;}
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,Mode);
  UEGHeroDefinition* EnemyElysia=nullptr;
  for(const auto& Entry:Base->Stages[1].Enemies)
   if(Entry.Definition&&Entry.Definition->HeroId==FName(TEXT("ElysiaCorruption")))EnemyElysia=Entry.Definition;
  auto* MeleeDefinition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Enemies/Melee/Data/DA_Monster_Melee"));
  if(!Test.TestNotNull(TEXT("Configured stage-two Corruption hero"),EnemyElysia)||
     !Test.TestNotNull(TEXT("Melee monster fixture"),MeleeDefinition))return;
  FEGEnemySpawn A;A.Definition=MeleeDefinition;A.Level=1;A.Cell={0,4};
  FEGEnemySpawn B;B.Definition=EnemyElysia;B.Level=1;B.Cell={6,4};
  Run->Stages[0].Enemies={A,B};Run->Stages[0].Traps.Reset();
  Mode->RunDefinition=Run;Mode->UnitClass=AEGDemoCombatUnit::StaticClass();

  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();Grid=World->SpawnActor<AEGDeploymentBoard>();
  Cards=World->SpawnActor<AEGCardManager>();
  if(!Scene||!Grid||!Cards){Test.AddError(TEXT("Cannot construct board fixture"));return;}
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500.f;
  Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=World->SpawnActor<AEGBoard>();
  Scene->CombatBoard->Dimensions={7,7};World->InitializeActorsForPlay(URL);World->BeginPlay();
  FGuid ElysiaCard,HumanCard;
  for(const auto& Card:Cards->GetHand())if(Card.Definition&&Card.Definition->HeroDefinition)
  {
   const FName Id=Card.Definition->HeroDefinition->HeroId;
   if(Id==FName(TEXT("Elysia")))ElysiaCard=Card.InstanceId;
   if(Id==FName(TEXT("ElysiaHerrscherOfHuman")))HumanCard=Card.InstanceId;
  }
  if(!Test.TestTrue(TEXT("Both formal opening cards exist"),ElysiaCard.IsValid()&&HumanCard.IsValid())||
     !Test.TestTrue(TEXT("Deploy ordinary Elysia"),Cards->TryDeployHandCard(ElysiaCard,{0,0}))||
     !Test.TestTrue(TEXT("Deploy single-cell human"),Cards->TryDeployHandCard(HumanCard,{3,1}))||
     !Test.TestTrue(TEXT("Enter controlled combat"),Mode->EnterCombat()))return;
  Mode->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetDefinition())
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);
   const auto Faction=IEGCombatant::Execute_GetFaction(Unit);
   if(Faction==EEGFaction::Player&&Unit->GetDefinition()->HeroId==FName(TEXT("ElysiaHerrscherOfHuman")))Human=Unit;
   else if(Faction==EEGFaction::Player&&Unit->GetDefinition()->HeroId==FName(TEXT("Elysia")))PlayerElysia=Unit;
   else if(Faction==EEGFaction::Enemy&&Unit->GetDefinition()->HeroId==FName(TEXT("ElysiaCorruption")))Corruption=Unit;
   else if(Faction==EEGFaction::Enemy&&Unit->GetDefinition()->MonsterAttack&&
           Unit->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Melee)Melee=Unit;
   auto* ASC=Unit->GetAbilitySystemComponent();
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   Unit->GetHeroSkill()->AbsorbShield(Unit->GetHeroSkill()->Shield);
  }
 }

 ~FThreeUnitsGeometryWorld()
 {
  if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
 }
 bool Ready()const{return World&&Mode&&Grid&&Human&&PlayerElysia&&Melee&&Corruption;}
 void Advance(float Seconds)
 {
  while(Seconds>0.f){const float Step=FMath::Min(Seconds,.02f);++GFrameCounter;World->Tick(LEVELTICK_All,Step);Seconds-=Step;}
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGThreeUnitsAreaAndMeleeEdge,
 "ElyGambit.ThreeUnits.Geometry.AreaAndMeleeAtFootprintEdge",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGThreeUnitsAreaAndMeleeEdge::RunTest(const FString&)
{
 FThreeUnitsGeometryWorld E(*this);if(!TestTrue(TEXT("Controlled three-unit fixture ready"),E.Ready()))return false;
 const FVector Center=E.Human->GetActorLocation();
 E.PlayerElysia->SetActorLocation(Center+FVector(-5000.f,0,0));
 E.Corruption->SetActorLocation(Center+FVector(-4000.f,0,0));
 const float Radius=100.f;const FVector AreaCenter=Center+FVector(Radius*.5f,0,0);
 const float Before=E.Human->GetAttributes()->GetHealth();
 E.Mode->BattleRules->Area(E.Melee,AreaCenter,Radius,100.f,false,false,FGuid::NewGuid());
 TestTrue(TEXT("Circular area damages a human whose center is inside"),
  FVector::Dist2D(Center,AreaCenter)<Radius&&FMath::IsNearlyEqual(Before-E.Human->GetAttributes()->GetHealth(),100.f,.1f));
 const auto Found=E.Melee->GetCombat()->FindEnemiesInRange(AreaCenter,Radius);
 TestEqual(TEXT("Radius lookup returns the single-cell actor once"),Found.FilterByPredicate([&](const AEGChessUnit* U){return U==E.Human;}).Num(),1);
 const float After=E.Human->GetAttributes()->GetHealth();
 E.Mode->BattleRules->Area(E.Melee,Center+FVector(Radius+1.f,0,0),Radius,100.f,false,false,FGuid::NewGuid());
 TestEqual(TEXT("Circle beyond the unit center does not hit"),E.Human->GetAttributes()->GetHealth(),After);

 auto* EnemyASC=E.Melee->GetAbilitySystemComponent();
 EnemyASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 const float Reach=E.Melee->GetCombat()->GetEffectiveAttackRange(E.Human);
 E.Melee->SetActorLocation(Center+FVector(Reach-5.f,0,0));
 E.Melee->GetCombat()->ResetCombat();
 if(!TestTrue(TEXT("Melee can reach the single-cell human"),E.Melee->GetCombat()->IsInAttackRange(E.Human)))return false;
 auto* Skill=E.Melee->GetHeroSkill();
 if(!TestTrue(TEXT("Melee action begins against human"),Skill->BeginAction(false)))return false;
 const float BeforeHit=E.Human->GetAttributes()->GetHealth();
 Skill->AnimationEvent(TEXT("Hit"));const float AfterHit=E.Human->GetAttributes()->GetHealth();
 TestTrue(TEXT("Actual monster Hit event damages the human"),FMath::IsNearlyEqual(BeforeHit-AfterHit,100.f,.1f));
 Skill->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Repeated Hit notify does not damage twice"),E.Human->GetAttributes()->GetHealth(),AfterHit);

 // Exercise movement plus a natural montage notify as well. The monster must
 // stop outside the unit's blocking radius while reaching its target.
 Skill->CancelAction();E.Melee->GetCombat()->ResetCombat();
 E.Melee->SetActorLocation(Center+FVector(Reach+350.f,0,0));
 E.Melee->GetCombat()->SetComponentTickEnabled(true);
 const float BeforePursuit=E.Human->GetAttributes()->GetHealth();bool bEnteredFootprint=false;
 const float Clearance=UEGCombatComponent::BlockingRadius(E.Melee)+UEGCombatComponent::BlockingRadius(E.Human);
 for(int32 Frame=0;Frame<500&&E.Human->GetAttributes()->GetHealth()>=BeforePursuit;++Frame)
 {
  E.Advance(.02f);
  bEnteredFootprint|=FVector::Dist2D(E.Melee->GetActorLocation(),Center)<Clearance-1.f;
 }
 TestFalse(TEXT("Chasing enemy never overlaps the human blocker"),bEnteredFootprint);
 TestTrue(TEXT("Natural melee montage reaches and damages the human"),E.Human->GetAttributes()->GetHealth()<BeforePursuit);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGThreeUnitsEnemyDomeEdge,
 "ElyGambit.ThreeUnits.Geometry.CorruptionDomeHitsFootprintEdge",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGThreeUnitsEnemyDomeEdge::RunTest(const FString&)
{
 FThreeUnitsGeometryWorld E(*this);if(!TestTrue(TEXT("Controlled three-unit fixture ready"),E.Ready()))return false;
 auto* Rules=Cast<UEGElysiaSkillDefinition>(E.Corruption->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Corruption uses ordinary Elysia rules"),Rules))return false;
 TestFalse(TEXT("Corruption retains its legacy dome rule"),Rules->bArrowRainUltimate);
 const FVector Center=E.Human->GetActorLocation();
 const float Radius=Rules->EmpoweredRadius>0.f?Rules->EmpoweredRadius:2.f*E.Grid->CellWidth;
 const FVector FieldCenter=Center+FVector(Radius*.7f,0,0);
 E.PlayerElysia->SetActorLocation(FieldCenter);
 E.Corruption->SetActorLocation(FieldCenter+FVector(Radius*.1f,0,0));
 E.Melee->SetActorLocation(Center+FVector(-4000.f,0,0));
 auto* EnemyASC=E.Corruption->GetAbilitySystemComponent();
 EnemyASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 EnemyASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 E.Corruption->GetCombat()->ResetCombat();
 auto* Skill=E.Corruption->GetHeroSkill();
 if(!TestTrue(TEXT("Enemy Elysia casts with ordinary player at the field center"),Skill->BeginAction(true)))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));

 // The human is the nearest target and its center is inside the dome.
 E.Corruption->SetActorLocation(Center+FVector(Radius*.1f,0,0));
 E.Corruption->GetCombat()->ResetCombat();
 if(!TestTrue(TEXT("Human center is inside the dome"),FVector::Dist2D(Center,FieldCenter)<Radius)||
    !TestTrue(TEXT("Human is the nearest valid enemy"),E.Corruption->GetCombat()->FindNearestEnemy()==E.Human)||
    !TestTrue(TEXT("Empowered shot starts"),Skill->BeginAction(false)))return false;
 const float HumanBefore=E.Human->GetAttributes()->GetHealth(),OtherBefore=E.PlayerElysia->GetAttributes()->GetHealth();
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
 E.Advance(Rules->ArrowFlightSeconds+.05f);
 TestTrue(TEXT("Empowered primary damages the human once"),FMath::IsNearlyEqual(HumanBefore-E.Human->GetAttributes()->GetHealth(),180.f,.1f));
 const float ManaAfterPrimary=E.Corruption->GetAttributes()->GetMana();
 E.Advance(Rules->ArrowFlightSeconds+.05f);
 TestTrue(TEXT("Dome edge contact selects the other enemy for one 60 percent bounce"),
  FMath::IsNearlyEqual(OtherBefore-E.PlayerElysia->GetAttributes()->GetHealth(),60.f,.1f));
 TestEqual(TEXT("Ricochet does not add another mana reward"),E.Corruption->GetAttributes()->GetMana(),ManaAfterPrimary);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGThreeUnitsElysiaFactionIsolation,
 "ElyGambit.ThreeUnits.Factions.IndependentDomesAndNoFriendlyFire",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGThreeUnitsElysiaFactionIsolation::RunTest(const FString&)
{
 FThreeUnitsGeometryWorld E(*this);if(!TestTrue(TEXT("Controlled three-unit fixture ready"),E.Ready()))return false;
 auto* PlayerSkill=E.PlayerElysia->GetHeroSkill();auto* EnemySkill=E.Corruption->GetHeroSkill();
 auto* PlayerPolicy=Cast<UEGElysiaSkillBehavior>(PlayerSkill->GetBehavior());
 auto* EnemyPolicy=Cast<UEGElysiaSkillBehavior>(EnemySkill->GetBehavior());
 auto* Rules=Cast<UEGElysiaSkillDefinition>(E.Corruption->GetDefinition()->SkillDefinition);
 auto* PlayerRules=Cast<UEGElysiaSkillDefinition>(E.PlayerElysia->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Player Elysia policy"),PlayerPolicy)||!TestNotNull(TEXT("Enemy Elysia policy"),EnemyPolicy)||
    !TestNotNull(TEXT("Enemy skill rules"),Rules)||!TestNotNull(TEXT("Player skill rules"),PlayerRules))return false;
 TestTrue(TEXT("Player uses arrow rain"),PlayerRules->bArrowRainUltimate);
 TestFalse(TEXT("Corruption keeps the independent legacy dome"),Rules->bArrowRainUltimate);
 TestTrue(TEXT("Each Elysia owns an independent policy"),PlayerPolicy!=EnemyPolicy);
 const FVector Origin=E.Grid->CellToWorld({3,3})+FVector(0,0,88);
 E.PlayerElysia->SetActorLocation(Origin);E.Corruption->SetActorLocation(Origin+FVector(500,0,0));
 const float Radius=Rules->EmpoweredRadius>0.f?Rules->EmpoweredRadius:2.f*E.Grid->CellWidth;
 E.Melee->SetActorLocation(Origin+FVector(500,100,0));
 E.Human->SetActorLocation(Origin+FVector(-FMath::Min(200.f,Radius*.5f),0,0));
 E.PlayerElysia->GetCombat()->ResetCombat();E.Corruption->GetCombat()->ResetCombat();
 E.Corruption->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 E.Corruption->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("Enemy dome begins"),EnemySkill->BeginAction(true)))return false;
 EnemySkill->AnimationEvent(TEXT("Release"));EnemySkill->AnimationEvent(TEXT("Finish"));
 TestTrue(TEXT("Enemy dome is active"),EnemyPolicy->DomeRemaining>0.f);
 TestEqual(TEXT("Enemy dome does not activate player dome"),PlayerPolicy->DomeRemaining,0.f);
 TestTrue(TEXT("Player remains able to cast its own dome"),PlayerPolicy->AllowsAction(true));
 const float AllyBefore=E.Melee->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Player attacks an opposing unit"),PlayerSkill->BeginAction(false)))return false;
 PlayerSkill->AnimationEvent(TEXT("Hit"));PlayerSkill->AnimationEvent(TEXT("Finish"));
 const int32 PlayerCursor=PlayerPolicy->NormalCursor;
 const float FriendlyBefore=E.Melee->GetAttributes()->GetHealth();
 const float PlayerBefore=E.PlayerElysia->GetAttributes()->GetHealth(),HumanBefore=E.Human->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Enemy selects the ordinary player as primary"),E.Corruption->GetCombat()->FindNearestEnemy()==E.PlayerElysia))return false;
 if(!TestTrue(TEXT("Enemy attacks an opposing unit"),EnemySkill->BeginAction(false)))return false;
 EnemySkill->AnimationEvent(TEXT("Hit"));EnemySkill->AnimationEvent(TEXT("Finish"));
 E.Advance(Rules->ArrowFlightSeconds*2.f+.1f);
 TestTrue(TEXT("Enemy empowered primary hits player exactly once"),FMath::IsNearlyEqual(PlayerBefore-E.PlayerElysia->GetAttributes()->GetHealth(),180.f,.1f));
 TestTrue(TEXT("Enemy dome bounces once to the other player"),FMath::IsNearlyEqual(HumanBefore-E.Human->GetAttributes()->GetHealth(),60.f,.1f));
 TestEqual(TEXT("Enemy attack and bounce do not damage its melee ally"),E.Melee->GetAttributes()->GetHealth(),FriendlyBefore);
 TestEqual(TEXT("Enemy attack does not advance player attack count"),PlayerPolicy->NormalCursor,PlayerCursor);
 TestTrue(TEXT("Player attack actually reached an enemy"),E.Melee->GetAttributes()->GetHealth()<AllyBefore||E.Corruption->GetAttributes()->GetHealth()<100000.f);
 TestEqual(TEXT("Player remains outside a dome"),PlayerPolicy->DomeRemaining,0.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGThreeUnitsDeathResourceGraph,
 "ElyGambit.ThreeUnits.Resources.HumanDeathVariantsAndFallback",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGThreeUnitsDeathResourceGraph::RunTest(const FString&)
{
 auto* Production=EGProductionTestAssets::ResolveRun(*this);if(!Production)return false;
 UEGHeroDefinition* Human=nullptr;
 for(const auto& Grant:Production->ExplicitStartingCards)
  if(Grant.Card&&Grant.Card->HeroDefinition&&Grant.Card->HeroDefinition->HeroId==TEXT("ElysiaHerrscherOfHuman"))Human=Grant.Card->HeroDefinition;
 if(!TestNotNull(TEXT("Production opening human definition"),Human)||
    !TestNotNull(TEXT("Production human presentation"),Human->PresentationDefinition.Get()))return false;
 const auto* Authored=Human->PresentationDefinition.Get();
 if(!TestTrue(TEXT("Two authored death form pairs are configured"),Authored->DeathVariants.Num()>=2))return false;

 // The fixture holds only the real death configuration. Removing the pairs
 // below detects accidental success from unrelated heroes/attack assets.
 TStrongObjectPtr<UEGRunDefinition> Run(NewObject<UEGRunDefinition>());
 auto* Card=NewObject<UEGCardDefinition>(Run.Get());auto* Hero=NewObject<UEGHeroDefinition>(Run.Get());
 auto* Presentation=NewObject<UEGHeroPresentationDefinition>(Run.Get());
 Hero->bUseSplitConfiguration=true;Hero->PresentationDefinition=Presentation;Card->HeroDefinition=Hero;Run->StartingCard=Card;
 Presentation->DeathVariants=Authored->DeathVariants;
 Presentation->DeathAnimation=Authored->DeathAnimation;Presentation->DeathAssembly=Authored->DeathAssembly;
 const FSoftObjectPath OldAnimation=Presentation->DeathAnimation.ToSoftObjectPath(),OldAssembly=Presentation->DeathAssembly.ToSoftObjectPath();
 if(!TestTrue(TEXT("Legacy death animation is configured"),OldAnimation.IsValid())||
    !TestTrue(TEXT("Legacy death assembly is configured"),OldAssembly.IsValid()))return false;
 const TArray<FSoftObjectPath> WithVariants=Run->GatherRuntimeResources();
 TestTrue(TEXT("Legacy death animation remains preloaded"),WithVariants.Contains(OldAnimation));
 TestTrue(TEXT("Legacy death assembly remains preloaded"),WithVariants.Contains(OldAssembly));
 TArray<FSoftObjectPath> Assemblies;
 bool bCheckedChild=false;
 for(int32 Index=0;Index<2;++Index)
 {
  const auto& Variant=Presentation->DeathVariants[Index];
  const FSoftObjectPath Animation=Variant.Animation.ToSoftObjectPath(),AssemblyPath=Variant.Assembly.ToSoftObjectPath();
  if(!TestTrue(TEXT("Variant animation path is configured"),Animation.IsValid())||
     !TestTrue(TEXT("Variant assembly path is configured"),AssemblyPath.IsValid()))return false;
  TestTrue(TEXT("Variant animation is preloaded"),WithVariants.Contains(Animation));
  TestTrue(TEXT("Variant assembly is preloaded"),WithVariants.Contains(AssemblyPath));
  Assemblies.Add(AssemblyPath);
  auto* Assembly=Variant.Assembly.LoadSynchronous();
  if(!TestNotNull(TEXT("Configured death assembly loads"),Assembly))return false;
  for(const auto& Motion:Assembly->Motions)for(const auto& Part:Motion.Parts)if(Part.Mesh)
  {
   TestTrue(TEXT("Assembly mesh dependency is preloaded"),WithVariants.Contains(FSoftObjectPath(Part.Mesh.Get())));
   bCheckedChild=true;break;
  }
 }
 TestTrue(TEXT("At least one actual assembly mesh was checked"),bCheckedChild);
 Presentation->DeathVariants.Reset();
 const TArray<FSoftObjectPath> WithoutVariants=Run->GatherRuntimeResources();
 TestTrue(TEXT("Legacy animation remains after removing variants"),WithoutVariants.Contains(OldAnimation));
 TestTrue(TEXT("Legacy assembly remains after removing variants"),WithoutVariants.Contains(OldAssembly));
 bool bUnique=false;
 for(const auto& Path:Assemblies)if(Path!=OldAssembly)
 {bUnique=true;TestFalse(TEXT("Variant-only assembly disappears from isolated graph"),WithoutVariants.Contains(Path));}
 TestTrue(TEXT("At least one variant assembly differs from legacy fallback"),bUnique);
 return true;
}

#endif
