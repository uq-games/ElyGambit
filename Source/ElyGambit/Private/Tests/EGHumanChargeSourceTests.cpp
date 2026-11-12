#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGHumanArrowProjectile.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "Presentation/EGElysiaHumanArrowVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Engine/StaticMesh.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Presentation/EGSkillNotify.h"

namespace
{
// Formal hero and presentation with a controlled, floorless battle. This is
// deliberately not a natural match or a visual comparison against the source.
struct FHumanChargeNotifyWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGChessUnit* Human=nullptr;
 TArray<AEGChessUnit*> Enemies;

 explicit FHumanChargeNotifyWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();
  if(!Test.TestNotNull(TEXT("Standalone world"),World))return;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Test.TestNotNull(TEXT("Game mode"),Mode))return;
  TSoftObjectPtr<UEGCardDefinition> FormalHumanCard;
  Mode->UnitClass=AEGDemoCombatUnit::StaticClass();Mode->RunDefinition=EGProductionTestAssets::ResolveRun(Test,&FormalHumanCard);
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  auto* Cards=World->SpawnActor<AEGCardManager>();
  if(!Scene||!Grid||!Cards||!Mode->RunDefinition)return;
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500.f;
  Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=World->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  UEGCardDefinition* Card=FormalHumanCard.LoadSynchronous();
  if(!Test.TestNotNull(TEXT("Formal HumanFusionCard"),Card)||!Card->HeroDefinition||Card->HeroDefinition->HeroKind!=EEGHeroKind::ElysiaHerrscher)return;
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,1})||!Mode->EnterCombat())return;
  Mode->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:Mode->GetCombatUnits())
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetCharacterMovement()->DisableMovement();
   if(Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=Unit;
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
 ~FHumanChargeNotifyWorld()
 {if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
 bool Ready()const{return Human&&Enemies.Num()>=2&&Human->GetHeroSkill();}
 void Step(float Delta=.01f){++GFrameCounter;World->Tick(LEVELTICK_All,Delta);}
};

UEGElysiaHerrscherPresentationComponent* ChargePresentation(FHumanChargeNotifyWorld& Fixture)
{
 auto* Component=Fixture.Human->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
 if(!Component)
 {
  // Some controlled runs use the generic combat actor. Reproduce its real
  // registered component on that actor rather than calling impact directly.
  Component=NewObject<UEGElysiaHerrscherPresentationComponent>(Fixture.Human);
  Fixture.Human->AddInstanceComponent(Component);Component->RegisterComponent();
 }
 for(int32 Frame=0;Frame<30;++Frame)Fixture.Step();
 return Component;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanChargeNaturalNotifications,
 "ElyGambit.ElysiaHerrscher.ChargeNaturalNotifications",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanChargeNaturalNotifications::RunTest(const FString&)
{
 FHumanChargeNotifyWorld E(*this);if(!TestTrue(TEXT("Controlled human fixture"),E.Ready()))return false;
 auto* Skill=E.Human->GetHeroSkill();
 auto* Target=E.Human->GetCombat()->FindNearestEnemy();
 if(!TestNotNull(TEXT("Target"),Target))return false;
 const float InitialHealth=Target->GetAttributes()->GetHealth();

 // Notifications must come from the actually selected montage; no Hit or
 // Finish injection and no fabricated claim about an original release frame.
 for(int32 Shot=0;Shot<2;++Shot)
 {
  const float BeforeMana=E.Human->GetAttributes()->GetMana();
  const float ExpectedMana=FMath::Min(E.Human->GetAttributes()->GetMaxMana(),BeforeMana+E.Human->GetAttributes()->GetManaOnHit());
  if(!TestTrue(TEXT("Bow attack begins"),Skill->BeginAction(false)))return false;
  const FGuid Action=Skill->ActionId;
  if(!TestTrue(TEXT("Attack has a current action identity"),Action.IsValid()))return false;
  if(!TestNotNull(TEXT("Formal attack montage is loaded"),Skill->GetActiveMontage()))return false;
  TSet<TWeakObjectPtr<AEGHumanArrowProjectile>> Arrows;
  for(int32 Frame=0;Frame<800&&Skill->IsActionRunning();++Frame)
  {
   E.Step();
   for(TActorIterator<AEGHumanArrowProjectile> It(E.World);It;++It)
    if(It->Source.Get()==E.Human&&It->AttackId==Action)Arrows.Add(*It);
  }
  TestFalse(TEXT("Authored Finish completes the action"),Skill->IsActionRunning());
  TestEqual(TEXT("One natural action emits one arrow"),Arrows.Num(),1);
  TestTrue(TEXT("Successful shot keeps the charged presentation"),Skill->IsHumanBowChargePresentationActive());
  TestTrue(TEXT("Natural Finish grants exactly the configured mana once"),
   FMath::IsNearlyEqual(E.Human->GetAttributes()->GetMana(),ExpectedMana,.01f));
  if(HasAnyErrors())return false;
 }
 for(int32 Frame=0;Frame<250;++Frame)E.Step();
 TestTrue(TEXT("Naturally released arrows can reach the target"),Target->GetAttributes()->GetHealth()<InitialHealth);

 const float BeforeCancel=E.Human->GetAttributes()->GetMana();
 if(!TestTrue(TEXT("Third bow action begins"),Skill->BeginAction(false)))return false;
 const FGuid CancelledAction=Skill->ActionId;
 Skill->CancelAction();
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
 for(int32 Frame=0;Frame<20;++Frame)E.Step();
 TestEqual(TEXT("Cancelled action grants no completion mana"),E.Human->GetAttributes()->GetMana(),BeforeCancel);
 int32 CancelledArrows=0;
 for(TActorIterator<AEGHumanArrowProjectile> It(E.World);It;++It)
  if(It->Source.Get()==E.Human&&It->AttackId==CancelledAction)++CancelledArrows;
 TestEqual(TEXT("Late events cannot fire an arrow for the cancelled action"),CancelledArrows,0);
 TestFalse(TEXT("Cancellation clears the held-charge presentation"),Skill->IsHumanBowChargePresentationActive());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanChargeNativeAimTiming,
 "ElyGambit.ElysiaHerrscher.ChargeNativeAimAndCadence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanChargeNativeAimTiming::RunTest(const FString&)
{
 FHumanChargeNotifyWorld E(*this);if(!TestTrue(TEXT("Human charge fixture ready"),E.Ready()))return false;
 auto* Skill=E.Human->GetHeroSkill();auto* ASC=E.Human->GetAbilitySystemComponent();
 for(float Speed:{1.f,1.5f,2.f})
 {
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Speed);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
  if(!TestTrue(TEXT("Charge action starts at configured attack speed"),Skill->BeginAction(false)))return false;
  const FGuid Action=Skill->ActionId;auto* Montage=Skill->GetActiveMontage();
  if(!TestNotNull(TEXT("Selected authored montage"),Montage))return false;
  float Hit=-1.f,Finish=-1.f;
  for(const auto& Event:Montage->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(Event.Notify))
  {if(Notify->Event==TEXT("Hit")&&Hit<0.f)Hit=Event.GetTime();if(Notify->Event==TEXT("Finish")&&Finish<0.f)Finish=Event.GetTime();}
  if(!TestTrue(TEXT("Montage exposes ordered Hit/Finish and positive rate"),Hit>0.f&&Finish>Hit&&Montage->RateScale>0.f))return false;
  const float OldEffectiveRate=FMath::Max(.1f,Speed*Finish)*Montage->RateScale;
  const double ExpectedHit=Hit/OldEffectiveRate,ExpectedFinish=Finish/OldEffectiveRate;
  const double Start=E.World->GetTimeSeconds();double ObservedHit=-1.;bool bSawNativeAim=false;
  TSet<TWeakObjectPtr<AEGHumanArrowProjectile>> Arrows;
  const float FrameStep=Speed==1.5f?1.f/30.f:.005f;
  for(int32 Frame=0;Frame<1200&&Skill->IsActionRunning();++Frame)
  {
   E.Step(FrameStep);
   auto* Mesh=E.Human->GetVisiblePresentationMesh();auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
   if(Anim&&Skill->GetActiveMontage()==Montage)
   {
    const float Position=Anim->Montage_GetPosition(Montage);
    if(Position<Hit-.01f&&FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage)*Montage->RateScale,Speed,.01f))bSawNativeAim=true;
   }
   for(TActorIterator<AEGHumanArrowProjectile> It(E.World);It;++It)
    if(It->Source.Get()==E.Human&&It->AttackId==Action)
    {if(!Arrows.Contains(*It)&&ObservedHit<0.)ObservedHit=E.World->GetTimeSeconds()-Start;Arrows.Add(*It);}
  }
  const double ObservedFinish=E.World->GetTimeSeconds()-Start;
  TestTrue(TEXT("Aim before release uses source speed times attack speed"),bSawNativeAim);
  TestEqual(TEXT("Exactly one arrow released by natural notify"),Arrows.Num(),1);
  const double Tolerance=FMath::Max(.035,double(FrameStep)*2.2);
  TestTrue(FString::Printf(TEXT("Hit budget at %.1fx: actual %.3f expected %.3f"),Speed,ObservedHit,ExpectedHit),ObservedHit>=0.&&FMath::Abs(ObservedHit-ExpectedHit)<=Tolerance);
  TestFalse(TEXT("Authored Finish completed without watchdog"),Skill->IsActionRunning());
  TestTrue(FString::Printf(TEXT("Finish budget at %.1fx: actual %.3f expected %.3f"),Speed,ObservedFinish,ExpectedFinish),FMath::Abs(ObservedFinish-ExpectedFinish)<=Tolerance);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanChargeInterruptedStance,
 "ElyGambit.ElysiaHerrscher.ChargeInterruptedStance",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanChargeInterruptedStance::RunTest(const FString&)
{
 FHumanChargeNotifyWorld E(*this);if(!TestTrue(TEXT("Controlled human fixture"),E.Ready()))return false;
 auto* Skill=E.Human->GetHeroSkill();
 auto* Original=E.Human->GetCombat()->FindNearestEnemy();
 if(!TestNotNull(TEXT("Original target"),Original))return false;
 auto* Replacement=E.Enemies[0]==Original?E.Enemies[1]:E.Enemies[0];
 const FVector InRange=Original->GetActorLocation();
 const FVector Away=InRange+FVector(50000.f,0.f,0.f);
 const auto* Presentation=E.Human->GetDefinition()->PresentationDefinition.Get();
 if(!TestNotNull(TEXT("Formal presentation definition"),Presentation)||
    !TestTrue(TEXT("First and repeat presentations exist"),Presentation->AttackSequence.Num()>=2))return false;
 auto* First=Presentation->AttackSequence[0].Montage.LoadSynchronous();
 auto* Repeat=Presentation->AttackSequence[1].Montage.LoadSynchronous();
 if(!TestNotNull(TEXT("First montage"),First)||!TestNotNull(TEXT("Repeat montage"),Repeat))return false;

 // Manual events here set up stance/target transitions only. Natural authored
 // notification timing is covered independently by ChargeNaturalNotifications.
 if(!TestTrue(TEXT("First shot begins"),Skill->BeginAction(false)))return false;
 TestTrue(TEXT("Initial action chooses the first presentation"),Skill->GetActiveMontage()==First);
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
 for(int32 Frame=0;Frame<20;++Frame)E.Step();
 TestTrue(TEXT("Standing still keeps charged stance"),Skill->IsHumanBowChargePresentationActive());

 Original->SetActorLocation(Away,false,nullptr,ETeleportType::TeleportPhysics);
 Replacement->SetActorLocation(InRange,false,nullptr,ETeleportType::TeleportPhysics);
 E.Human->GetCombat()->ResetCombat();
 for(int32 Frame=0;Frame<20;++Frame)E.Step();
 TestTrue(TEXT("Another reachable target preserves charged stance"),Skill->IsHumanBowChargePresentationActive());
 if(!TestTrue(TEXT("Next shot starts against replacement"),Skill->BeginAction(false)))return false;
 TestTrue(TEXT("Replacement target uses repeat presentation"),Skill->GetActiveMontage()==Repeat);
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
 for(int32 Frame=0;Frame<20;++Frame)E.Step();

 for(auto* Enemy:E.Enemies)Enemy->SetActorLocation(Away,false,nullptr,ETeleportType::TeleportPhysics);
 E.Human->GetCombat()->ResetCombat();
 for(int32 Frame=0;Frame<20;++Frame)E.Step();
 TestFalse(TEXT("No reachable target clears charged stance"),Skill->IsHumanBowChargePresentationActive());

 Replacement->SetActorLocation(InRange,false,nullptr,ETeleportType::TeleportPhysics);
 if(!TestTrue(TEXT("Shot starts once a target returns"),Skill->BeginAction(false)))return false;
 TestTrue(TEXT("After losing targets the next shot restarts"),Skill->GetActiveMontage()==First);
 Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
 for(int32 Frame=0;Frame<20;++Frame)E.Step();
 TestTrue(TEXT("Completed restart holds the stance"),Skill->IsHumanBowChargePresentationActive());
 E.Human->SetActorLocation(E.Human->GetActorLocation()+FVector(30.f,0.f,0.f),false,nullptr,ETeleportType::TeleportPhysics);
 E.Step();
 TestFalse(TEXT("Actual movement clears charged stance"),Skill->IsHumanBowChargePresentationActive());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanChargeStanceBattleEnd,
 "ElyGambit.ElysiaHerrscher.ChargeStanceBattleEnd",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanChargeStanceBattleEnd::RunTest(const FString&)
{
 for(bool bKillHuman:{false,true})
 {
  FHumanChargeNotifyWorld E(*this);if(!TestTrue(TEXT("Controlled human fixture"),E.Ready()))return false;
  auto* Skill=E.Human->GetHeroSkill();
  if(!TestTrue(TEXT("Bow attack begins"),Skill->BeginAction(false)))return false;
  Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
  for(int32 Frame=0;Frame<20;++Frame)E.Step();
  if(!TestTrue(TEXT("Completed attack holds charged stance"),Skill->IsHumanBowChargePresentationActive()))return false;
  TWeakObjectPtr<UEGHeroSkillComponent> WeakSkill(Skill);
  if(bKillHuman)E.Human->Die();else E.Mode->BattleRules->Stop();
  for(int32 Frame=0;Frame<20;++Frame)E.Step();
  TestTrue(TEXT("Death or round stop clears held charge"),!WeakSkill.IsValid()||!WeakSkill->IsHumanBowChargePresentationActive());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanNiagaraArrowImpactLifetime,
 "ElyGambit.ElysiaHerrscher.NiagaraArrowImpactLifetime",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanNiagaraArrowImpactLifetime::RunTest(const FString&)
{
 // This looping system is a controlled fixture so both contacts remain alive
 // long enough to check independent world-space anchors and round cleanup.
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Elysia/NiagaraRevision20261107/NS_C2_StarSea_StarTrack.NS_C2_StarSea_StarTrack"));
 if(!TestNotNull(TEXT("Looping Niagara fixture"),System))return false;
 for(bool bKillHuman:{false,true})
 {
  FHumanChargeNotifyWorld E(*this);if(!TestTrue(TEXT("Controlled human fixture"),E.Ready()))return false;
  auto* Visual=ChargePresentation(E);
  auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
  auto* Target=E.Human->GetCombat()->FindNearestEnemy();
  if(!TestNotNull(TEXT("Human presentation component"),Visual)||!TestNotNull(TEXT("Human combat rules"),Rules)||
     !TestNotNull(TEXT("Target"),Target))return false;
  auto* Behind=E.Enemies[0]==Target?E.Enemies[1]:E.Enemies[0];
  const FVector Forward=(Target->GetActorLocation()-E.Human->GetActorLocation()).GetSafeNormal2D();
  Behind->SetActorLocation(Target->GetActorLocation()+Forward*500.f,false,nullptr,ETeleportType::TeleportPhysics);
  for(auto* Enemy:E.Enemies)if(Enemy!=Target&&Enemy!=Behind)
   Enemy->SetActorLocation(Target->GetActorLocation()+FVector(0.f,5000.f,0.f),false,nullptr,ETeleportType::TeleportPhysics);

  TGuardValue<TObjectPtr<UNiagaraSystem>> ImpactSystem(Visual->ArrowImpactSystem,System);
  TSet<TWeakObjectPtr<UNiagaraComponent>> Existing;
  TArray<UNiagaraComponent*> Components;E.Human->GetComponents(Components);
  for(auto* Component:Components)Existing.Add(Component);
  // The projectile is the production C++ visual subclass; a Blueprint override
  // and its asset binding still need their own formal-asset verification.
  TGuardValue<TSubclassOf<AEGHumanArrowProjectile>> ArrowClass(Rules->ArrowProjectileClass,AEGElysiaHumanArrowVisual::StaticClass());
  auto* Skill=E.Human->GetHeroSkill();
  // Advance the release-owned fifth-arrow counter; discard only these fixture projectiles.
  for(int32 I=0;I<4;++I)
  {
   if(!TestTrue(TEXT("Prior bow action starts"),Skill->BeginAction(false)))return false;
   Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
   for(TActorIterator<AEGHumanArrowProjectile> It(E.World);It;++It)if(It->Source.Get()==E.Human)It->Destroy();
  }
  if(!TestTrue(TEXT("Piercing action starts"),Skill->BeginAction(false)))return false;
  const FGuid Action=Skill->ActionId;
  Skill->AnimationEvent(TEXT("Hit"));
  AEGElysiaHumanArrowVisual* Projectile=nullptr;
  for(TActorIterator<AEGElysiaHumanArrowVisual> It(E.World);It;++It)
   if(It->Source.Get()==E.Human&&It->AttackId==Action){Projectile=*It;break;}
  if(!TestNotNull(TEXT("Real piercing projectile"),Projectile))return false;
  // No legacy SourceFX object is required: contacts drive Niagara directly.
  Projectile->SourceImpact=nullptr;
  Skill->AnimationEvent(TEXT("Finish"));
  TArray<TWeakObjectPtr<UNiagaraComponent>> Effects;
  for(int32 Frame=0;Frame<300&&Effects.Num()<2;++Frame)
  {
   E.Step();Effects.Reset();Components.Reset();E.Human->GetComponents(Components);
   for(auto* Component:Components)
    if(!Existing.Contains(Component)&&Component->GetAsset()==System)Effects.Add(Component);
  }
  if(!TestEqual(TEXT("Two actual piercing contacts create independent Niagara effects"),Effects.Num(),2))return false;
  FVector A=Effects[0]->GetComponentLocation(),B=Effects[1]->GetComponentLocation();
  if(FVector::DistSquared(A,Target->GetActorLocation())>FVector::DistSquared(B,Target->GetActorLocation()))
  {Effects.Swap(0,1);Swap(A,B);}
  TestTrue(TEXT("First effect is at the first contact"),FVector::Dist(A,Target->GetActorLocation())<350.f);
  TestTrue(TEXT("Second effect is at the second contact"),FVector::Dist(B,Behind->GetActorLocation())<350.f);
  TestTrue(TEXT("The two impact points are distinct"),FVector::Dist(A,B)>100.f);
  E.Human->SetActorLocation(E.Human->GetActorLocation()+FVector(800.f,500.f,0.f),false,nullptr,ETeleportType::TeleportPhysics);
  for(int32 Frame=0;Frame<10;++Frame)E.Step();
  TestTrue(TEXT("Both impact effects stay at their contact points when the source moves"),Effects[0].IsValid()&&Effects[1].IsValid()&&
   Effects[0]->GetComponentLocation().Equals(A,.1f)&&Effects[1]->GetComponentLocation().Equals(B,.1f));
  if(bKillHuman)E.Human->Die();else E.Mode->BattleRules->Stop();
  for(int32 Frame=0;Frame<20;++Frame)E.Step();
  TestTrue(TEXT("Death or battle stop clears the first impact effect"),!Effects[0].IsValid()||!Effects[0]->IsRegistered());
  TestTrue(TEXT("Death or battle stop clears the second impact effect"),!Effects[1].IsValid()||!Effects[1]->IsRegistered());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanFallbackArrowImpactLocation,
 "ElyGambit.ElysiaHerrscher.FallbackArrowImpactLocation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanFallbackArrowImpactLocation::RunTest(const FString&)
{
 FHumanChargeNotifyWorld E(*this);if(!TestTrue(TEXT("Controlled human fixture"),E.Ready()))return false;
 auto* Visual=ChargePresentation(E);
 auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/ToonProjectiles2/Effects/NS_Hit_1.NS_Hit_1"));
 auto* Target=E.Human->GetCombat()->FindNearestEnemy();
 if(!TestNotNull(TEXT("Human presentation component"),Visual)||!TestNotNull(TEXT("Human combat rules"),Rules)||
    !TestNotNull(TEXT("Fallback Niagara fixture"),System)||!TestNotNull(TEXT("Target"),Target))return false;
 TGuardValue<TSubclassOf<AEGHumanArrowProjectile>> ArrowClass(Rules->ArrowProjectileClass,AEGElysiaHumanArrowVisual::StaticClass());
 TGuardValue<TObjectPtr<UNiagaraSystem>> Fallback(Visual->ArrowImpactSystem,System);
 TSet<TWeakObjectPtr<UNiagaraComponent>> Existing;
 TArray<UNiagaraComponent*> Components;E.Human->GetComponents(Components);
 for(auto* Component:Components)Existing.Add(Component);
 auto* Skill=E.Human->GetHeroSkill();
 if(!TestTrue(TEXT("Bow action starts"),Skill->BeginAction(false)))return false;
 const FGuid Action=Skill->ActionId;
 Skill->AnimationEvent(TEXT("Hit"));
 AEGElysiaHumanArrowVisual* Projectile=nullptr;
 for(TActorIterator<AEGElysiaHumanArrowVisual> It(E.World);It;++It)
  if(It->Source.Get()==E.Human&&It->AttackId==Action){Projectile=*It;break;}
 if(!TestNotNull(TEXT("Real fallback projectile"),Projectile))return false;
 Projectile->SourceImpact=nullptr;
 Skill->AnimationEvent(TEXT("Finish"));
 TWeakObjectPtr<UNiagaraComponent> Impact;
 for(int32 Frame=0;Frame<300&&!Impact.IsValid();++Frame)
 {
  E.Step();Components.Reset();E.Human->GetComponents(Components);
  for(auto* Component:Components)
   if(!Existing.Contains(Component)&&Component->GetAsset()==System){Impact=Component;break;}
 }
 if(!TestTrue(TEXT("A real contact spawns the configured fallback"),Impact.IsValid()))return false;
 const FVector Contact=Impact->GetComponentLocation();
 TestTrue(TEXT("Fallback appears at the contacted target"),FVector::Dist(Contact,Target->GetActorLocation())<350.f);
 E.Human->SetActorLocation(E.Human->GetActorLocation()+FVector(800.f,500.f,0.f),false,nullptr,ETeleportType::TeleportPhysics);
 TestTrue(TEXT("Fallback remains at the contact when the source moves"),Impact.IsValid()&&Impact->GetComponentLocation().Equals(Contact,.1f));
 return true;
}

#endif
