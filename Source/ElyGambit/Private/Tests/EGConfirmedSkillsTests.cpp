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
#include "Presentation/EGBoardModelSizing.h"
#include "Presentation/EGSkillProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "UI/EGUnitInformation.h"
#include "Sound/SoundBase.h"

namespace
{
struct FConfirmedWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGCardManager* Cards=nullptr;
 TArray<TStrongObjectPtr<UObject>> ResidentAudio;
 FConfirmedWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
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
 ~FConfirmedWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 UEGCardDefinition* Card(const TCHAR* Key){return LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));}
 FGuid Deploy(const TCHAR* Key,int32 X)
 {
  auto* D=Card(Key);Cards->GrantCard(D);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{X,1});return GM->GetRoster()->FindHeroByDefinition(D->HeroDefinition);
 }
 AEGChessUnit* Unit(FGuid Id){for(auto* U:GM->GetCombatUnits())if(U->GetInstanceId()==Id)return U;return nullptr;}
 AEGChessUnit* Enemy(){for(auto* U:GM->GetCombatUnits())if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)return U;return nullptr;}
 void Freeze()
 {
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* A=U->GetAbilitySystemComponent();
   A->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);A->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);
   A->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
  }
 }
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.05f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}}
 bool Cast(AEGChessUnit* U){U->GetHeroSkill()->CancelAction();U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),U->GetAttributes()->GetMaxMana());return U->GetHeroSkill()->BeginAction(true);}
 bool PrepareAudio(AEGChessUnit* U)
 {
  const auto* P=U&&U->GetDefinition()?U->GetDefinition()->GetCombatAudioPresentation():nullptr;
  if(!P||!P->bUseEventAudio||P->AudioEvents.IsEmpty())return false;
  // Audio event/dedup tests require a ready fixture. Production playback remains
  // nonblocking and deliberately skips a sound whose async request is pending.
  for(const auto& Entry:P->AudioEvents)for(const auto& Ref:Entry.Sounds)
  {auto* Sound=Ref.LoadSynchronous();if(!Sound)return false;ResidentAudio.Emplace(Sound);}
  return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGContinuousBoardChase,"ElyGambit.ConfirmedSkills.ContinuousBoardChase",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGContinuousBoardChase::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* C=U->GetCombat();auto* Target=E.Enemy();const FVector Start=U->GetActorLocation();
 auto* Layout=E.GM->GetBoard()->LayoutSource.Get();
 if(!TestNotNull(TEXT("Active board layout"),Layout))return false;
 // The target is intentionally far away to measure a full second without
 // reaching attack range. Keep that original distance inside a legal board.
 Layout->Columns=40;
 TestEqual(TEXT("Movement ticks every render frame"),C->GetComponentTickInterval(),0.f);
 TestTrue(TEXT("Board movement is configured"),U->GetDefinition()->GetBoardMovement());
 for(auto* Other:E.GM->GetCombatUnits())if(Other!=U)Other->SetActorLocation(Start+FVector(5000,5000,0));
 Target->SetActorLocation(Start+FVector(5000,0,0));
 FIntPoint Cell;
 if(!TestTrue(TEXT("Chaser and target start inside the expanded board"),Layout->TryWorldToCell(Start,Cell)&&Layout->TryWorldToCell(Target->GetActorLocation(),Cell)))return false;
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 const float Speed=U->GetDefinition()->GetMoveSpeed();
 auto Move=[&](int32 Frames)
 {
  U->SetActorLocation(Start);C->ResetCombat();
  for(int32 I=0;I<Frames;++I)
  {
   const FVector Before=U->GetActorLocation();C->TickComponent(1.f/Frames,LEVELTICK_All,nullptr);
   TestTrue(TEXT("Every frame advances at configured speed"),FMath::IsNearlyEqual(FVector::Dist2D(Before,U->GetActorLocation()),Speed/Frames,.01f));
  }
  return U->GetActorLocation();
 };
 const FVector At30=Move(30),At120=Move(120);
 TestTrue(TEXT("Travel distance is independent of frame rate"),At30.Equals(At120,.05f));
 TestTrue(TEXT("One second retains configured move speed"),FMath::IsNearlyEqual(FVector::Dist2D(Start,At120),Speed,.05f));
 if(!TestTrue(TEXT("Cast before testing busy movement"),E.Cast(U)))return false;
 const FVector Busy=U->GetActorLocation();C->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 TestTrue(TEXT("Casting never chases"),Busy.Equals(U->GetActorLocation()));
 U->GetHeroSkill()->CancelAction();C->ResetCombat();
 Target->SetActorLocation(U->GetActorLocation()+FVector(UEGCombatComponent::BlockingRadius(U)+UEGCombatComponent::BlockingRadius(Target)+6.f,0,0));C->TickComponent(1.f/60,LEVELTICK_All,nullptr);
 TestTrue(TEXT("In-range attack never advances"),Busy.Equals(U->GetActorLocation()));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardRunTempo,"ElyGambit.ConfirmedSkills.BoardRunClockAndAttackIndependence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardRunTempo::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start running battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* C=U->GetCombat();auto* ASC=U->GetAbilitySystemComponent();auto* Target=E.Enemy();
 const FVector Start=U->GetActorLocation();
 auto* Layout=E.GM->GetBoard()->LayoutSource.Get();
 if(!TestNotNull(TEXT("Active board layout"),Layout))return false;
 // The 8000 cm target is a test isolation device, not an off-board target.
 Layout->Columns=40;
 for(auto* Other:E.GM->GetCombatUnits())if(Other!=U)Other->SetActorLocation(Start+FVector(8000,8000,0));
 Target->SetActorLocation(Start+FVector(8000,0,0));
 FIntPoint Cell;
 if(!TestTrue(TEXT("Runner and target start inside the expanded board"),Layout->TryWorldToCell(Start,Cell)&&Layout->TryWorldToCell(Target->GetActorLocation(),Cell)))return false;
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),0);
 for(int32 I=0;I<40;++I){C->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);U->Tick(1.f/60.f);}
 TestTrue(TEXT("Normal board run covers one 500 cm cell in two thirds of a second"),FMath::IsNearlyEqual(FVector::Dist2D(Start,U->GetActorLocation()),500.f,.1f));
 auto* Anim=Cast<UEGCombatAnimInstance>(U->GetVisiblePresentationMesh()->GetAnimInstance());
 if(!TestNotNull(TEXT("Formal combat locomotion instance"),Anim))return false;
 Anim->SetSpeed(750);
 TestTrue(TEXT("Full board speed selects running"),Anim->bRun);
 TestTrue(TEXT("Scaled normal-run stride follows the current authored reference"),FMath::IsNearlyEqual(Anim->LocomotionRate,750.f/U->GetDefinition()->GetRunReferenceSpeed(),.0001f));
 Target->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));C->TickComponent(.001f,LEVELTICK_All,nullptr);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1);
 if(!TestTrue(TEXT("Begin stroke after moving"),U->GetHeroSkill()->BeginAction(false)))return false;
 Anim->SetSpeed(750);
 TestEqual(TEXT("Gait rate never multiplies the active attack montage"),Anim->Montage_GetPlayRate(U->GetHeroSkill()->GetActiveMontage()),1.f);
 U->GetHeroSkill()->CancelAction();
 const auto* Can=U->GetDefinition()->SummonDefinition.Get();
 TestEqual(TEXT("Legacy summon resolves explicit stride reference"),Can->BuildPresentationClips().RunReferenceSpeed,1000.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNearestMeleeTarget,"ElyGambit.ConfirmedSkills.NearestMeleeTargetAndAttackSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNearestMeleeTarget::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start targeting battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* C=U->GetCombat();auto* Skill=U->GetHeroSkill();auto* First=E.Enemy();AEGChessUnit* Second=nullptr;
 const FVector Origin=U->GetActorLocation();
 for(auto* Other:E.GM->GetCombatUnits())if(Other!=U)
 {
  Other->SetActorLocation(Origin+FVector(8000,8000,0));
  if(Other!=First&&C->IsValidEnemy(Other))Second=Other;
 }
 if(!TestNotNull(TEXT("Two enemy candidates"),Second))return false;
 auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),0);
 auto Decide=[&](){C->TickComponent(.001f,LEVELTICK_All,nullptr);};
 First->SetActorLocation(Origin+FVector(3000,0,0));Second->SetActorLocation(Origin+FVector(4000,0,0));
 Decide();TestEqual(TEXT("Initially pursues nearest enemy"),C->GetTarget(),First);
 Second->SetActorLocation(Origin+FVector(1500,0,0));
 Decide();TestEqual(TEXT("Next movement frame switches to newly nearest enemy"),C->GetTarget(),Second);
 Skill->SetTaunt(First,1.f);Decide();TestEqual(TEXT("Active taunt overrides nearest enemy"),C->GetTarget(),First);
 E.Tick(1.05f);Decide();TestEqual(TEXT("Expired taunt resumes nearest selection"),C->GetTarget(),Second);
 Skill->SetTaunt(First,1.f);First->GetHeroSkill()->bInFlight=true;Decide();
 TestEqual(TEXT("Untargetable taunter cannot keep a forced lock"),C->GetTarget(),Second);First->GetHeroSkill()->bInFlight=false;
 Skill->ForcedTarget.Reset();Skill->TauntEnd=0;
 First->SetActorLocation(U->GetActorLocation()+FVector(C->GetEffectiveAttackRange(First)-2.f,0,0));Second->SetActorLocation(Origin+FVector(1500,0,0));Decide();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1);
 if(!TestTrue(TEXT("Begin swing at first enemy"),Skill->BeginAction(false)))return false;
 Second->SetActorLocation(U->GetActorLocation()-FVector(C->GetEffectiveAttackRange(Second)-6.f,0,0));
 const float FirstHealth=First->GetAttributes()->GetHealth(),SecondHealth=Second->GetAttributes()->GetHealth();
 Decide();TestEqual(TEXT("An active swing retains its target snapshot"),C->GetTarget(),First);
 Skill->AnimationEvent(TEXT("Hit"));TestTrue(TEXT("Committed swing damages original target"),First->GetAttributes()->GetHealth()<FirstHealth);
 TestEqual(TEXT("Closer arrival does not steal committed damage"),Second->GetAttributes()->GetHealth(),SecondHealth);
 Skill->CancelAction();ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),0);
 Decide();TestEqual(TEXT("Once action ends nearest target is available immediately"),C->GetTarget(),Second);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGConfirmedCoin,"ElyGambit.ConfirmedSkills.PardoCoinAndRoundIncome",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGConfirmedCoin::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!TestTrue(TEXT("Start"),E.GM->EnterCombat()))return false;
 E.Freeze();auto* U=E.Unit(Id);auto* B=E.GM->BattleRules.Get();if(!TestNotNull(TEXT("Pardo"),U))return false;
 const auto Gold=E.Cards->GetGold();FGuid First=FGuid::NewGuid();B->BasicHit(U,E.Enemy(),First);B->BasicHit(U,E.Enemy(),First);
 for(int32 I=0;I<3;++I)B->BasicHit(U,E.Enemy(),FGuid::NewGuid());
 TestEqual(TEXT("Four unique attacks no coin"),E.Cards->GetGold(),Gold);
 B->BasicHit(U,E.Enemy(),FGuid::NewGuid());TestEqual(TEXT("Fifth grants exactly one"),E.Cards->GetGold(),Gold+1);
 for(int32 I=0;I<20;++I)B->BasicHit(U,E.Enemy(),FGuid::NewGuid());
 TestEqual(TEXT("No repeat reward"),E.Cards->GetGold(),Gold+1);
 TestTrue(TEXT("No independent card skill in UI"),!U->GetDefinition()->GetSkillDetails(1,false).ToString().Contains(TEXT("卡牌：")));
 E.GM->ResolveCombat(false);TestTrue(TEXT("Advance after first-round defeat"),E.GM->AdvanceDemo());
 TestEqual(TEXT("Formal two-stage run advances instead of retrying"),E.GM->GetMatch()->GetStageNumber(),2);
 TestEqual(TEXT("One skill coin carries forward with configured round income"),E.Cards->GetGold(),Gold+1+E.GM->RunDefinition->RoundIncome);
 const int64 NextGold=E.Cards->GetGold();
 if(!TestTrue(TEXT("Second round begins"),E.GM->EnterCombat()))return false;
 E.Freeze();U=E.Unit(Id);B=E.GM->BattleRules.Get();
 for(int32 I=0;I<5;++I)B->BasicHit(U,E.Enemy(),FGuid::NewGuid());
 TestEqual(TEXT("New battle receives its own single coin allowance"),E.Cards->GetGold(),NextGold+1);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGConfirmedCan,"ElyGambit.ConfirmedSkills.CanASCAndLivingOwner",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGConfirmedCan::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Unit(Id);auto* B=E.GM->BattleRules.Get();
 if(!TestTrue(TEXT("First throw"),E.Cast(U)))return false;U->GetHeroSkill()->AnimationEvent(TEXT("Release"));
 auto* Cat=B->FindCan(Id);if(!TestNotNull(TEXT("Independent cat"),Cat))return false;
 TestFalse(TEXT("Summon information has no independent card skill"),Cat->GetDefinition()->GetSkillDetails(1,false).ToString().Contains(TEXT("卡牌：")));
 TestTrue(TEXT("ASC not shared"),Cat->GetAbilitySystemComponent()!=U->GetAbilitySystemComponent());
 Cat->GetCombat()->SetComponentTickEnabled(false);const float Base=Cat->GetDefinition()->GetBaseStats().AttackSpeed;
 TestTrue(TEXT("Cat no longer receives owner-only bonus"),FMath::IsNearlyEqual(Cat->GetAttributes()->GetAttackSpeed(),Base));
 TestEqual(TEXT("Pardo receives no old cat aura"),U->GetAttributes()->GetAttackSpeed(),U->GetDefinition()->GetBaseStats().AttackSpeed);
 U->GetHeroSkill()->ApplyPersistentLevel(10);auto* PardoPolicy=Cast<UEGPardoSkillBehavior>(U->GetHeroSkill()->GetBehavior());PardoPolicy->TickBattle(0);
 const float OwnerSpeed=U->GetAttributes()->GetAttackSpeed();
 U->GetHeroSkill()->ApplyPersistentLevel(11);PardoPolicy->TickBattle(0);
 TestTrue(TEXT("Level 11 living cat buffs Pardo"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackSpeed(),OwnerSpeed*1.3f,.001f));
 PardoPolicy->TickBattle(0);TestTrue(TEXT("Repeated tick does not stack speed"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackSpeed(),OwnerSpeed*1.3f,.001f));
 U->GetHeroSkill()->ApplyPersistentLevel(10);PardoPolicy->TickBattle(0);TestTrue(TEXT("Tier loss removes bonus"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackSpeed(),OwnerSpeed,.001f));

 const FVector CatScale=Cat->GetVisiblePresentationMesh()->GetComponentScale();
 const FVector CatSourceScale=Cat->GetDefinition()->GetMeshScale();
 const float CatBoardScale=FEGBoardModelSizing::Resolve(Cat->GetDefinition(),Cat);
 TestTrue(TEXT("Can retains its larger authored source scale"),CatSourceScale.X>U->GetDefinition()->GetMeshScale().X*3.f);
 TestTrue(TEXT("Can board fit has a positive scale and never enlarges the source"),CatBoardScale>0.f&&CatBoardScale<=1.f);
 TestTrue(TEXT("Can presentation uses the authored scale and board fit"),CatScale.Equals(CatSourceScale*CatBoardScale,.0001f));
 bool SawImpact=false;
 for(int32 Frame=0;Frame<30;++Frame)
 {
  E.Tick(.05f);
  TestTrue(TEXT("Can scale remains fixed through flight and landing"),Cat->GetVisiblePresentationMesh()->GetComponentScale().Equals(CatScale,.0001f));
  for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(IsValid(*It)&&It->GetOwner()==U)SawImpact=true;
 }
 TestTrue(TEXT("Actual landing creates the source explosion"),SawImpact);
 TestFalse(TEXT("Can landed"),Cat->GetHeroSkill()->bInFlight);
 const auto CatId=Cat->GetInstanceId();const auto Gold=E.Cards->GetGold();
 for(int32 I=0;I<6;++I)B->BasicHit(Cat,E.Enemy(),FGuid::NewGuid());TestEqual(TEXT("Cat cannot earn Pardo coins"),E.Cards->GetGold(),Gold);
 TestTrue(TEXT("Pounce command"),E.Cast(U));U->GetHeroSkill()->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Same cat"),B->FindCan(Id)->GetInstanceId(),CatId);
 TestTrue(TEXT("Second cast retains source scale"),Cat->GetVisiblePresentationMesh()->GetComponentScale().Equals(CatScale,.0001f));
 U->GetHeroSkill()->ApplyPersistentLevel(11);PardoPolicy->TickBattle(0);
 TestTrue(TEXT("Live companion restores level 11 bonus"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackSpeed(),OwnerSpeed*1.3f,.001f));
 Cat->Die();PardoPolicy->TickBattle(0);
 TestTrue(TEXT("Companion death removes owner speed bonus"),FMath::IsNearlyEqual(U->GetAttributes()->GetAttackSpeed(),OwnerSpeed,.001f));
 B->Damage(E.Enemy(),U,1000000);TestTrue(TEXT("Cat aura removed with owner death"),FMath::IsNearlyEqual(Cat->GetAttributes()->GetAttackSpeed(),Base));
 E.GM->ResolveCombat(false);TestNull(TEXT("No cat after stop"),B->FindCan(Id));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoSourcePresentation,"ElyGambit.ConfirmedSkills.PardoSourcePresentationLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoSourcePresentation::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();auto* Target=E.Enemy();
 Target->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));
 const auto* Presentation=U->GetDefinition()->PresentationDefinition.Get();
 if(!TestNotNull(TEXT("Pardo presentation"),Presentation)||!TestEqual(TEXT("Three source attacks"),Presentation->AttackSequence.Num(),3))return false;
 auto* Body=U->GetVisiblePresentationMesh();
 TestTrue(TEXT("Unused large-ring skeleton root is hidden"),Body->IsBoneHiddenByName(TEXT("BladeRing_P1_L_Root")));
 TestTrue(TEXT("Unused large-ring renderer root is hidden"),Body->IsBoneHiddenByName(TEXT("BladeRing_L")));
 TestFalse(TEXT("Animated small ring remains visible"),Body->IsBoneHiddenByName(TEXT("BladeRing_P1_S_Root")));
 TestTrue(TEXT("Combined body does not attach duplicate weapon meshes"),Presentation->Weapons.IsEmpty());
 auto* Effect=LoadObject<UEGSourceEffectDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/Effects/DA_Pardo_Attack01"));
 if(!TestNotNull(TEXT("Source effect definition"),Effect))return false;
 const auto Count=[&E](){int32 N=0;for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(IsValid(*It))++N;return N;};
 const float Health=Target->GetAttributes()->GetHealth();const int64 Gold=E.Cards->GetGold();
 for(int32 I=0;I<6;++I)
 {
  // Exercise real montage playback at both baseline and boosted attack speed.
  const float Speed=I==5?2.f:1.f;
  U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Speed);
  if(!TestTrue(TEXT("Source attack begins"),Skill->BeginAction(false)))return false;
  TestEqual(TEXT("Selected sequence owns both playback and notify token"),Skill->GetActiveMontage(),Presentation->AttackSequence[I%3].Montage.LoadSynchronous());
  TestEqual(TEXT("Source attack seconds scale only with attack speed"),U->GetVisiblePresentationMesh()->GetAnimInstance()->Montage_GetPlayRate(Skill->GetActiveMontage()),Speed);
  const int32 Commits=Skill->CommitCount;
  Skill->PresentSourceEffect(Effect,U->GetVisiblePresentationMesh());Skill->PresentSourceEffect(Effect,U->GetVisiblePresentationMesh());
  TestEqual(TEXT("Duplicate cosmetic notify creates one effect"),Count(),1);
  for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)
  {
   TestTrue(TEXT("Converted source cm do not inherit the board-fit mesh scale"),It->GetActorScale3D().Equals(FVector(Effect->PresentationScale),.0001f));
   const FVector MeshScale=U->GetVisiblePresentationMesh()->GetRelativeScale3D();
   U->GetVisiblePresentationMesh()->SetRelativeScale3D(MeshScale*1.5f);
   TestTrue(TEXT("Attached effect size is independent of later model scaling"),It->GetActorScale3D().Equals(FVector(Effect->PresentationScale),.0001f));
   U->GetVisiblePresentationMesh()->SetRelativeScale3D(MeshScale);
  }
  TestEqual(TEXT("Cosmetic notify does not commit a hit"),Skill->CommitCount,Commits);
  TestEqual(TEXT("Cosmetic notify does not damage"),Target->GetAttributes()->GetHealth(),Health);
  TestEqual(TEXT("Cosmetic notify does not award coins"),E.Cards->GetGold(),Gold);
  Skill->CancelAction();TestEqual(TEXT("Action cancellation destroys its effect immediately"),Count(),0);
  Skill->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Cancelled action cannot submit a late hit"),Target->GetAttributes()->GetHealth(),Health);
 }
 if(!TestTrue(TEXT("Attack before battle stop"),Skill->BeginAction(false)))return false;
 Skill->PresentSourceEffect(Effect,U->GetVisiblePresentationMesh());TestEqual(TEXT("Live effect before stop"),Count(),1);
 E.GM->BattleRules->Stop();TestEqual(TEXT("Battle stop destroys source effects"),Count(),0);
 Skill->PresentSourceEffect(Effect,U->GetVisiblePresentationMesh());TestEqual(TEXT("Stopped battle cannot create an effect"),Count(),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoSourceTiming,"ElyGambit.ConfirmedSkills.PardoSourceTiming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoSourceTiming::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();auto* Target=E.Enemy();
 Target->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));
 U->GetVisiblePresentationMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 // Durations from the retained three 60 Hz native-pose files; contact times are the
 // existing UE weapon-motion adaptation, not recovered source damage scripts.
 const float Durations[]={1.66666675f,2.08333349f,2.81666684f};
 const float Contacts[]={.18333333f,.15f,.5f};
 const float Windows[]={1.35f,1.45f,1.7f};
 const float PhaseEnds[]={.6f,.9f,1.5f};
 const float StrokeRates[]={1.f,1.f,1.2f};
 for(const float Rate:{1.f,2.f})for(int32 I=0;I<3;++I)
 {
  U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Rate);
  if(!TestTrue(TEXT("Begin full recovery playback"),Skill->BeginAction(false)))return false;
  TestTrue(TEXT("Imported duration matches source data"),FMath::IsNearlyEqual(Skill->GetActiveAnimation()->GetPlayLength(),Durations[I],.00001f));
  TestTrue(TEXT("Full attack montage uses its natural cycle"),FMath::IsNearlyEqual(Skill->GetActiveMontage()->GetPlayLength(),Windows[I],.00001f));
  const auto& Segments=Skill->GetActiveMontage()->SlotAnimTracks[0].AnimTrack.AnimSegments;
  TestEqual(TEXT("Separate stroke and recovery segments"),Segments.Num(),2);
  TestTrue(TEXT("Includes the complete recovery tail"),FMath::IsNearlyEqual(Segments.Last().AnimEndTime,Durations[I],.00001f));
  TestTrue(TEXT("Stroke stays near source speed"),FMath::IsNearlyEqual(Segments[0].AnimPlayRate,StrokeRates[I],.00001f));
  TestTrue(TEXT("Recovery begins at exactly the last stroke pose"),FMath::IsNearlyEqual(Segments[0].AnimEndTime,PhaseEnds[I],.00001f)&&FMath::IsNearlyEqual(Segments[1].AnimStartTime,PhaseEnds[I],.00001f));
  TestTrue(TEXT("Runtime phases are contiguous"),FMath::IsNearlyEqual(Segments[1].StartPos,PhaseEnds[I]/StrokeRates[I],.00001f));
  const int32 Before=Skill->CommitCount;
  const float Lead=FMath::Max(.0f,FMath::FloorToFloat((Contacts[I]/StrokeRates[I]/Rate-.025f)/.05f)*.05f);
  if(Lead>0)E.Tick(Lead);
  TestEqual(TEXT("No gameplay hit before the scaled montage contact"),Skill->CommitCount,Before);
  E.Tick(.15f);
  TestEqual(TEXT("Actual animation notify submits one hit"),Skill->CommitCount,Before+1);
  E.Tick(Windows[I]/Rate+.15f);
  TestFalse(TEXT("Natural playback releases the action and watchdog"),Skill->IsBusy());
  TestEqual(TEXT("Only authored contacts submit damage events"),Skill->CommitCount,Before+(I==2?2:1));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoCadence,"ElyGambit.ConfirmedSkills.PardoNaturalCadenceAndAttackSpeed",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoCadence::RunTest(const FString&)
{
 for(float Rate:{1.f,2.f,3.f})
 {
  FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
  if(!TestTrue(TEXT("Start cadence battle"),E.GM->EnterCombat()))return false;E.Freeze();
  auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();auto* Target=E.Enemy();
  Target->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));
  auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxManaAttribute(),0);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Rate);
  U->GetVisiblePresentationMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  U->GetCombat()->ResetCombat();U->GetCombat()->SetComponentTickEnabled(true);
  const float Ready[]={1.35f,1.45f,1.7f};
  TArray<double> Starts;int32 Previous=Skill->ActionCount;
  for(int32 I=0;I<FMath::CeilToInt(9.6f/Rate/.01f);++I)
  {
   ++GFrameCounter;E.W->Tick(LEVELTICK_All,.01f);
   if(Skill->ActionCount!=Previous)
   {
    TestEqual(TEXT("One action is started at a time"),Skill->ActionCount,Previous+1);Previous=Skill->ActionCount;
    Starts.Add(E.W->GetTimeSeconds());
    const auto& Sequence=U->GetDefinition()->PresentationDefinition->AttackSequence;
    TestTrue(TEXT("All three combo forms remain in order"),Skill->GetActiveAnimation()==Sequence[(Starts.Num()-1)%3].Animation.LoadSynchronous());
   }
  }
  FString Timings;for(double Time:Starts)Timings+=FString::Printf(TEXT(" %.3f"),Time);
  AddInfo(FString::Printf(TEXT("Cadence rate %.2f; final speed %.2f; starts:%s"),Rate,U->GetAttributes()->GetAttackSpeed(),*Timings));
  TestTrue(TEXT("At least six complete attack transitions"),Starts.Num()>=6);
  for(int32 I=1;I<Starts.Num();++I)
   TestTrue(TEXT("Authored natural cycles scale once with attack speed"),FMath::Abs(Starts[I]-Starts[I-1]-Ready[(I-1)%3]/Rate)<.035);
  int32 CompleteContacts=0;for(int32 I=0;I<Starts.Num()-1;++I)CompleteContacts+=I%3==2?2:1;
  TestTrue(TEXT("Each completed cycle submits its authored contacts"),Skill->CommitCount>=CompleteContacts);
  TestTrue(TEXT("Only current action may have pending contacts"),Skill->CommitCount<=CompleteContacts+((Starts.Num()-1)%3==2?2:1));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoPhasedEffects,"ElyGambit.ConfirmedSkills.PardoThirdStrokePhasedEffects",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoPhasedEffects::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start phase battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();
 E.Enemy()->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));
 U->GetVisiblePresentationMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1);
 // Advance the real combo cursor, then exercise the third montage's notifies.
 for(int32 I=0;I<2;++I){if(!Skill->BeginAction(false))return false;Skill->CancelAction();}
 auto HasPhase=[&](const TCHAR* Role)
 {
  auto* D=LoadObject<UEGSourceEffectDefinition>(nullptr,*FString::Printf(TEXT("/Game/Blueprints/Characters/Pardofelis/Data/Effects/DA_Pardo_%s"),Role));
  if(!D||D->Tracks.IsEmpty())return false;
  for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&It->GetOwner()==U)
  {
   TArray<UStaticMeshComponent*> Parts;It->GetComponents(Parts);
   if(Parts.Num()==D->Tracks.Num()&&Parts[0]->GetStaticMesh()==D->Tracks[0].Mesh.LoadSynchronous())return true;
  }
  return false;
 };
 if(!TestTrue(TEXT("Start third stroke"),Skill->BeginAction(false)))return false;
 const int32 Before=Skill->CommitCount;
 E.Tick(.2f);
 TestTrue(TEXT("Early rotation emits only its source swirl group"),HasPhase(TEXT("Attack03_Swirl")));
 TestFalse(TEXT("Slash group does not start with swirl"),HasPhase(TEXT("Attack03_Trail1")));
 TestFalse(TEXT("Late group does not start with swirl"),HasPhase(TEXT("Attack03_Trail2")));
 E.Tick(.3f);
 TestTrue(TEXT("Slash group follows the early rotation"),HasPhase(TEXT("Attack03_Trail1")));
 TestFalse(TEXT("Late trail waits for the later weapon phase"),HasPhase(TEXT("Attack03_Trail2")));
 TestEqual(TEXT("First swing has one gameplay hit"),Skill->CommitCount,Before+1);
 E.Tick(.65f);TestTrue(TEXT("Late weapon phase emits its separate trail"),HasPhase(TEXT("Attack03_Trail2")));
 Skill->CancelAction();
 TestFalse(TEXT("Cancellation clears the late phase too"),HasPhase(TEXT("Attack03_Trail2")));
 E.Tick(.2f);TestEqual(TEXT("Cancellation before second contact suppresses it"),Skill->CommitCount,Before+1);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoLiveSpeed,"ElyGambit.ConfirmedSkills.PardoLiveAttackSpeedClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoLiveSpeed::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;auto Advance=[&](float Seconds){for(int32 I=0;I<FMath::RoundToInt(Seconds/.01f);++I){++GFrameCounter;E.W->Tick(LEVELTICK_All,.01f);}};const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start live-speed battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();auto* ASC=U->GetAbilitySystemComponent();
 E.Enemy()->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));
 U->GetVisiblePresentationMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1);
 if(!TestTrue(TEXT("Start first source stroke"),Skill->BeginAction(false)))return false;
 const int32 Before=Skill->CommitCount;Advance(.02f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),2);
 TestEqual(TEXT("Live attribute changes active montage rate"),U->GetVisiblePresentationMesh()->GetAnimInstance()->Montage_GetPlayRate(Skill->GetActiveMontage()),2.f);
 Advance(.02f);TestEqual(TEXT("No premature hit on speed change"),Skill->CommitCount,Before);
 Advance(.07f);TestEqual(TEXT("Scaled timeline delivers its single hit"),Skill->CommitCount,Before+1);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),.5f);
 for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(IsValid(*It)&&It->GetOwner()==U)
  TestEqual(TEXT("Existing action effects use the same live clock"),It->GetPlaybackRate(),.5f);
 Advance(.5f);TestTrue(TEXT("Slowdown extends recovery watchdog instead of cancelling early"),Skill->IsBusy());
 Advance(2.5f);TestFalse(TEXT("Slowed stroke finishes naturally"),Skill->IsBusy());
 TestEqual(TEXT("Speed changes cannot duplicate the hit"),Skill->CommitCount,Before+1);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoShortThrow,"ElyGambit.ConfirmedSkills.PardoLongerThrowAndIndependentImpact",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoShortThrow::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);
 if(!TestTrue(TEXT("Start longer throw battle"),E.GM->EnterCombat()))return false;E.Freeze();
 auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();auto* Target=E.Enemy();
 Target->SetActorLocation(U->GetActorLocation()+FVector(80,0,0));
 U->GetVisiblePresentationMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 if(!TestTrue(TEXT("Natural ultimate playback"),E.Cast(U)))return false;
 const FGuid Token=Skill->ActionId,Round=E.GM->BattleRules->GetRoundId();const float Health=Target->GetAttributes()->GetHealth();
 E.Tick(1.1f);TestNull(TEXT("No cat before the delayed throw notify"),E.GM->BattleRules->FindCan(Id));
 E.Tick(.15f);auto* Cat=E.GM->BattleRules->FindCan(Id);
 if(!TestNotNull(TEXT("Throw notify creates cat"),Cat))return false;
 Cat->GetCombat()->SetComponentTickEnabled(false);
 TestTrue(TEXT("Cat still in flight"),Cat->GetHeroSkill()->bInFlight);
 TestEqual(TEXT("No impact damage before landing"),Target->GetAttributes()->GetHealth(),Health);
 E.Tick(.2f);TestTrue(TEXT("Longer throw still owns the summoner action"),Skill->IsBusy());
 E.Tick(.2f);TestTrue(TEXT("Cat remains visibly descending before landing"),Cat->GetHeroSkill()->bInFlight);
 E.Tick(.25f);TestTrue(TEXT("Slow ultimate keeps its recovery before two seconds"),Skill->IsBusy());
 E.Tick(.55f);TestFalse(TEXT("Summoner finishes at two seconds without the posing tail"),Skill->IsBusy());TestFalse(TEXT("Cat completes the source-derived descent"),Cat->GetHeroSkill()->bInFlight);
 TestTrue(TEXT("Actual landing deals damage"),Target->GetAttributes()->GetHealth()<Health);
 auto Impacts=[&](){TArray<AEGSourceEffect*> Found;for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)
  if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&It->GetOwner()==U&&!It->GetAttachParentActor())Found.Add(*It);return Found;};
 auto Landed=Impacts();if(!TestEqual(TEXT("One landing burst"),Landed.Num(),1))return false;
 TWeakObjectPtr<AEGSourceEffect> Burst=Landed[0];
 Skill->PresentSummonImpact(Token,Round,Target->GetActorLocation()-FVector(0,0,88));
 Skill->PresentSummonImpact(FGuid::NewGuid(),FGuid::NewGuid(),Target->GetActorLocation());
 TestEqual(TEXT("Duplicate and stale-round landing callbacks rejected"),Impacts().Num(),1);
 if(!TestTrue(TEXT("Basic can begin during the landed burst"),Skill->BeginAction(false)))return false;
 E.Tick(.25f);Skill->CancelAction();
 TestTrue(TEXT("Next action and its cancellation do not truncate the landed burst"),Burst.IsValid()&&!Burst->IsActorBeingDestroyed());
 E.GM->BattleRules->Stop();E.Tick(.1f);TestTrue(TEXT("Settlement preserves the already triggered landing burst"),Burst.IsValid()&&!Burst->IsActorBeingDestroyed());
 E.Tick(2.f);TestTrue(TEXT("Settlement burst expires without leaking actors"),!Burst.IsValid()||Burst->IsActorBeingDestroyed());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoEffectAnchors,"ElyGambit.ConfirmedSkills.PardoWeaponAnchorsAndSourceVfxClock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoEffectAnchors::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* Skill=U->GetHeroSkill();auto* Mesh=U->GetVisiblePresentationMesh();
 E.Enemy()->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));
 for(const TCHAR* Role:{TEXT("Attack01_Trail"),TEXT("Attack02_Trail"),TEXT("Attack03_Swirl"),TEXT("Attack03_Trail1"),TEXT("Attack03_Trail2")})
 {
  if(!TestTrue(TEXT("Start source combo action"),Skill->BeginAction(false)))return false;
  auto* Definition=LoadObject<UEGSourceEffectDefinition>(nullptr,*FString::Printf(TEXT("/Game/Blueprints/Characters/Pardofelis/Data/Effects/DA_Pardo_%s"),Role));
  Skill->PresentSourceEffect(Definition,Mesh);
  for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed())
  {
   TestEqual(TEXT("Source particles are not compressed again with the full clip"),It->GetPlaybackRate(),1.f);
   for(float Yaw:{0.f,73.f,180.f})
   {
    U->SetActorRotation(FRotator(0,Yaw,0));It->Tick(0.f);
    const FVector Mapped=It->GetActorLocation()+Mesh->GetComponentQuat().RotateVector(Definition->SourceAnchor*Definition->PresentationScale);
    TestTrue(TEXT("Source weapon anchor matches animated socket at every facing"),Mapped.Equals(Mesh->GetSocketLocation(Definition->AnchorBone),.01f));
   }
  }
  Skill->CancelAction();
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCanDescent,"ElyGambit.ConfirmedSkills.CanSourceDescentAndFixedLanding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCanDescent::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* B=E.GM->BattleRules.Get();auto* Target=E.Enemy();
 Target->SetActorLocation(U->GetActorLocation()+FVector(180,0,0));
 AEGChessUnit* First=nullptr;FVector Scale;UAbilitySystemComponent* CatASC=nullptr;
 for(int32 Cast=0;Cast<2;++Cast)
 {
  if(!TestTrue(TEXT("Cast source descent"),E.Cast(U)))return false;U->GetHeroSkill()->AnimationEvent(TEXT("Release"));
  auto* Cat=B->FindCan(Id);if(!TestNotNull(TEXT("One independent Can"),Cat))return false;Cat->GetCombat()->SetComponentTickEnabled(false);
  if(!First){First=Cat;Scale=Cat->GetVisiblePresentationMesh()->GetComponentScale();CatASC=Cat->GetAbilitySystemComponent();}
  TestTrue(TEXT("First drop and repeat use the same actor, ASC and model scale"),Cat==First&&CatASC==Cat->GetAbilitySystemComponent()&&Scale.Equals(Cat->GetVisiblePresentationMesh()->GetComponentScale(),.0001f));
  AEGSkillProjectile* Flight=nullptr;for(TActorIterator<AEGSkillProjectile> It(E.W);It;++It)if(It->Cargo.Get()==Cat&&!It->IsActorBeingDestroyed())Flight=*It;
  if(!TestNotNull(TEXT("Source motion projectile"),Flight))return false;
  TestNotNull(TEXT("Decoded descent profile"),Flight->SummonMotion.Get());TestTrue(TEXT("Visible source glow during descent"),Flight->DescentEffect.IsValid());
  const FVector Landing=Flight->Destination;const float High=Cat->GetActorLocation().Z;
  TestTrue(TEXT("Drop begins clearly above the fixed landing point"),High-Landing.Z>400.f);
  Target->SetActorLocation(Target->GetActorLocation()+FVector(30,0,0));const float Health=Target->GetAttributes()->GetHealth();
  E.Tick(.5f);const float Middle=Cat->GetActorLocation().Z;
  TestTrue(TEXT("Cat has an observable descending phase"),Middle<High-5.f&&Middle>Landing.Z+50.f&&Cat->GetHeroSkill()->bInFlight);
  TestEqual(TEXT("Descent visuals cannot deal early damage"),Target->GetAttributes()->GetHealth(),Health);
  E.Tick(.7f);TestFalse(TEXT("Cat becomes targetable only after landing"),Cat->GetHeroSkill()->bInFlight);
  TestTrue(TEXT("Cat settles outside the moving target capsule"),FVector::Dist2D(Cat->GetActorLocation(),Target->GetActorLocation())>=UEGCombatComponent::BlockingRadius(Cat)+UEGCombatComponent::BlockingRadius(Target));
  TestTrue(TEXT("Landing deals its area damage"),Target->GetAttributes()->GetHealth()<Health);
  TestTrue(TEXT("Landing restores normal body material"),Cat->GetVisiblePresentationMesh()->GetMaterial(0)==Cat->GetVisiblePresentationMesh()->GetSkeletalMeshAsset()->GetMaterials()[0].MaterialInterface);
  bool SawVisibleImpact=false;
  for(TActorIterator<AEGSourceEffect> It(E.W);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&It->GetOwner()==U)
  {
   TestTrue(TEXT("Landing effect shares the actual impact ground point"),It->GetActorLocation().Equals(Landing-FVector(0,0,88),.01f));
   TArray<UStaticMeshComponent*> Parts;It->GetComponents(Parts);for(auto* Part:Parts)SawVisibleImpact|=Part->IsVisible();
  }
  TestTrue(TEXT("Landing burst contains visible geometry"),SawVisibleImpact);
  E.Tick(2.f);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoSplitContacts,"ElyGambit.ConfirmedSkills.ThirdAttackTwoDamageContacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoSplitContacts::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();auto* S=U->GetHeroSkill();auto* B=E.GM->BattleRules.Get();auto* ASC=U->GetAbilitySystemComponent();
 T->SetActorLocation(U->GetActorLocation()+FVector(U->GetCombat()->GetEffectiveAttackRange(T)-6.f,0,0));
 for(int32 I=0;I<2;++I){if(!S->BeginAction(false))return false;S->CancelAction();}
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 const int32 Attacks=B->GetAttackCount();const float Before=T->GetAttributes()->GetHealth(),Attack=U->GetAttributes()->GetAttackPower();
 if(!TestTrue(TEXT("Begin split third attack"),S->BeginAction(false)))return false;
 const FGuid Token=S->ActionId;const int32 Effects=T->GetHitFeedback()->PlayedCount;
 S->AnimationEvent(TEXT("Hit"));TestTrue(TEXT("First contact deals half raw attack"),FMath::IsNearlyEqual(Before-T->GetAttributes()->GetHealth(),Attack*.5f,.01f));
 const float Mana=U->GetAttributes()->GetMana(),First=T->GetAttributes()->GetHealth();
 S->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Duplicate first notify is inert"),T->GetAttributes()->GetHealth(),First);
 S->AnimationEvent(TEXT("HitSecond"));TestTrue(TEXT("Second contact deals remaining half"),FMath::IsNearlyEqual(Before-T->GetAttributes()->GetHealth(),Attack,.01f));
 TestEqual(TEXT("Both contacts share one action identity"),S->ActionId,Token);
 TestEqual(TEXT("One basic-attack counter increment"),B->GetAttackCount(),Attacks+1);
 TestEqual(TEXT("Second contact does not restore mana twice"),U->GetAttributes()->GetMana(),Mana);
 TestEqual(TEXT("Two actual receipts produce two target effects"),T->GetHitFeedback()->PlayedCount,Effects+2);
 const float Second=T->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("HitSecond"));
 TestEqual(TEXT("Duplicate second notify is inert"),T->GetAttributes()->GetHealth(),Second);S->CancelAction();
 for(int32 I=0;I<2;++I){if(!S->BeginAction(false))return false;S->CancelAction();}
 if(!S->BeginAction(false))return false;S->AnimationEvent(TEXT("Hit"));S->CancelAction();const float Cancelled=T->GetAttributes()->GetHealth();
 S->AnimationEvent(TEXT("HitSecond"));TestEqual(TEXT("Cancelled second contact cannot damage"),T->GetAttributes()->GetHealth(),Cancelled);
 for(int32 I=0;I<2;++I){if(!S->BeginAction(false))return false;S->CancelAction();}
 if(!S->BeginAction(false))return false;S->AnimationEvent(TEXT("Hit"));const float Far=T->GetAttributes()->GetHealth();
 T->SetActorLocation(T->GetActorLocation()+FVector(5000,0,0));S->AnimationEvent(TEXT("HitSecond"));
 TestEqual(TEXT("Second contact independently checks range"),T->GetAttributes()->GetHealth(),Far);S->CancelAction();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardCapsules,"ElyGambit.ConfirmedSkills.AllFactionCapsuleBlocking",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardCapsules::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();auto* B=E.GM->BattleRules.Get();
 int32 I=0;for(auto* Other:E.GM->GetCombatUnits())Other->SetActorLocation(FVector(10000+(I++)*1500,10000,88));
 U->SetActorLocation(FVector(0,0,88));T->SetActorLocation(FVector(1000,0,88));
 TestEqual(TEXT("Root native capsule blocks pawns"),U->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn),ECR_Block);
 FHitResult Hit;U->SetActorLocation(FVector(2000,0,88),true,&Hit);
 TestTrue(TEXT("Large swept step cannot tunnel through monster"),Hit.bBlockingHit);
 TestTrue(TEXT("Capsule separation covers visible bodies and clearance"),FVector::Dist2D(U->GetActorLocation(),T->GetActorLocation())>=UEGCombatComponent::BlockingRadius(U)+UEGCombatComponent::BlockingRadius(T)-.1f);
 U->SetActorLocation(T->GetActorLocation());TestTrue(TEXT("Existing overlap is repaired"),U->GetCombat()->ResolveBoardOverlap());
 auto* Cat=B->SpawnSummon(U->GetDefinition()->SummonDefinition,Id,1,U->GetActorLocation(),false);
 if(!TestNotNull(TEXT("Friendly independent Can"),Cat))return false;Cat->GetCombat()->SetComponentTickEnabled(false);
 TestTrue(TEXT("Allies also separate"),FVector::Dist2D(U->GetActorLocation(),Cat->GetActorLocation())>=UEGCombatComponent::BlockingRadius(U)+UEGCombatComponent::BlockingRadius(Cat));
 TestTrue(TEXT("Can also separates from enemy"),FVector::Dist2D(T->GetActorLocation(),Cat->GetActorLocation())>=UEGCombatComponent::BlockingRadius(T)+UEGCombatComponent::BlockingRadius(Cat));
 Cat->GetHeroSkill()->bInFlight=true;Cat->RefreshBoardCollision();TestEqual(TEXT("Flying Can does not block"),Cat->GetCapsuleComponent()->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
 Cat->GetHeroSkill()->bInFlight=false;Cat->RefreshBoardCollision();TestEqual(TEXT("Landed Can blocks again"),Cat->GetCapsuleComponent()->GetCollisionEnabled(),ECollisionEnabled::QueryOnly);
 auto* Layout=E.GM->GetBoard()->LayoutSource.Get();Layout->CellWidth=500;Layout->CellDepth=500;Layout->Columns=6;
 // Place the two allies outside their combined 500 cm blocking radius before
 // starting the concurrent-movement assertion. SetActorLocation is a fixture
 // teleport and intentionally does not perform production spawn validation.
 U->SetActorLocation(FVector(-900,300,88));Cat->SetActorLocation(FVector(-900,-300,88));T->SetActorLocation(FVector(900,0,88));
 for(const auto Pair:{TPair<AEGChessUnit*,AEGChessUnit*>(U,Cat),TPair<AEGChessUnit*,AEGChessUnit*>(U,T),TPair<AEGChessUnit*,AEGChessUnit*>(Cat,T)})
  TestTrue(TEXT("Concurrent movement starts with separate footprints"),FVector::Dist2D(Pair.Key->GetActorLocation(),Pair.Value->GetActorLocation())>=UEGCombatComponent::BlockingRadius(Pair.Key)+UEGCombatComponent::BlockingRadius(Pair.Value));
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxManaAttribute(),0);
 Cat->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);Cat->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);
 bool Separated=true;for(int32 Frame=0;Frame<240;++Frame)
 {
  for(auto* Moving:{U,Cat,T})Moving->GetCombat()->TickComponent(1.f/30.f,LEVELTICK_All,nullptr);
  ++GFrameCounter;E.W->Tick(LEVELTICK_All,1.f/30.f);
  for(const auto Pair:{TPair<AEGChessUnit*,AEGChessUnit*>(U,Cat),TPair<AEGChessUnit*,AEGChessUnit*>(U,T),TPair<AEGChessUnit*,AEGChessUnit*>(Cat,T)})
   Separated&=FVector::Dist2D(Pair.Key->GetActorLocation(),Pair.Value->GetActorLocation())>=UEGCombatComponent::BlockingRadius(Pair.Key)+UEGCombatComponent::BlockingRadius(Pair.Value)-.1f;
 }
 TestTrue(TEXT("Two allies and enemy stay separated during concurrent movement and attacks"),Separated);
 T->Die();TestFalse(TEXT("Dead monster releases collision immediately"),T->GetActorEnableCollision());
 Cat->SetActorLocation(FVector(10000,10000,88));
 U->SetActorLocation(T->GetActorLocation());TestFalse(TEXT("Dying body cannot block a swept move"),U->GetCombat()->ResolveBoardOverlap());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGTargetFeedback,"ElyGambit.ConfirmedSkills.TargetOwnedHitFeedbackAndUltimateRing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGTargetFeedback::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();auto* B=E.GM->BattleRules.Get();auto* Feedback=T->GetHitFeedback();
 TestFalse(TEXT("Runtime target is configured by its own Blueprint"),T->GetDefinition()->CombatActorClass.IsNull());
 const auto SameAction=FGuid::NewGuid();const auto Initial=T->GetVisiblePresentationMesh()->GetOverlayMaterial();
 B->Damage(U,T,10,SameAction);B->Damage(U,T,10,SameAction);
 TestEqual(TEXT("Same action can show two real hits"),Feedback->PlayedCount,2);
 TestEqual(TEXT("Damage never changes body overlay"),T->GetVisiblePresentationMesh()->GetOverlayMaterial(),Initial);
 FEGCombatExecution::Heal(T,10);TestEqual(TEXT("Healing has no hit burst"),Feedback->PlayedCount,2);
 T->GetHeroSkill()->SetShield(100);const float Health=T->GetAttributes()->GetHealth();B->Damage(U,T,10);
 TestEqual(TEXT("Shield-only hit preserves health"),T->GetAttributes()->GetHealth(),Health);TestEqual(TEXT("Shield-only hit still plays feedback"),Feedback->PlayedCount,3);
 for(int32 I=0;I<10;++I)B->Damage(U,T,1);TestTrue(TEXT("Burst pool stays bounded"),Feedback->GetActiveBurstCount()<=4);
 E.Tick(.4f);TestEqual(TEXT("Bursts expire and stop ticking"),Feedback->GetActiveBurstCount(),0);TestFalse(TEXT("No idle feedback tick"),Feedback->IsComponentTickEnabled());
 auto* Ring=U->FindComponentByClass<UEGUltimateRingComponent>();if(!TestNotNull(TEXT("Pardo has dedicated ultimate ring component"),Ring))return false;
 TestFalse(TEXT("Ultimate ring hidden in normal stance"),Ring->IsVisible());
 if(!TestTrue(TEXT("Begin ultimate ring"),E.Cast(U)))return false;
 // Anticipation: the large ring is conjured during the wind-up while the hand ring is stowed.
 auto* Montage=U->GetHeroSkill()->GetActiveMontage();auto* Anim=U->GetVisiblePresentationMesh()->GetAnimInstance();
 auto* Body=U->GetVisiblePresentationMesh();
 auto ApplySourceTime=[&](float SourceTime)
 {
  for(const auto& Segment:Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
   if(SourceTime>=Segment.AnimStartTime&&SourceTime<=Segment.AnimEndTime)
    Anim->Montage_SetPosition(Montage,Segment.StartPos+(SourceTime-Segment.AnimStartTime)/Segment.AnimPlayRate);
 };
 const float Ground=U->GetActorLocation().Z-88.f,Height=U->GetDefinition()->GetPresentationHeight();
 Anim->Montage_SetPosition(Montage,0.f);Ring->Refresh();
 TestTrue(TEXT("Wind-up conjures the large airborne ring"),Ring->IsVisible()&&Ring->GetStaticMesh()!=nullptr);
 TestTrue(TEXT("Conjured ring still clears the head"),Ring->Bounds.GetBox().Min.Z>Ground+Height*1.05f);
 TestTrue(TEXT("Wind-up hides the ordinary hand weapon"),Body->IsBoneHiddenByName(TEXT("BladeRing_P1_S_Root")));
 const float AnticipationScale=Ring->GetComponentScale().X;
 ApplySourceTime(.3f);Ring->Refresh();
 const float ThrowScale=Ring->GetComponentScale().X;
 TestTrue(TEXT("Ring scale ramps up from conjure to throw"),AnticipationScale<ThrowScale*.98f&&ThrowScale>0.f);
 TestEqual(TEXT("Airborne path retains twenty-two original source samples"),Ring->AirborneCenters.Num(),22);
 float LastHeight=0.f,FirstHeight=0.f;
 for(float SourceTime:{.3f,.4f,.5f,.64f})
 {
  ApplySourceTime(SourceTime);
  Ring->Refresh();
  TestTrue(TEXT("Source throw phase shows the airborne ring"),Ring->IsVisible()&&Ring->GetStaticMesh()!=nullptr);
  TestTrue(TEXT("Entire ring clears the head instead of surrounding waist"),Ring->Bounds.GetBox().Min.Z>Ground+Height*1.05f);
  if(FirstHeight==0.f)FirstHeight=Ring->GetComponentLocation().Z;
  if(!Ring->bTargetOverhead)TestTrue(TEXT("Legacy ring follows upward source movement"),Ring->GetComponentLocation().Z>=LastHeight);LastHeight=Ring->GetComponentLocation().Z;
 }
 TestTrue(TEXT("Ring actually rises instead of staying at a constant offset"),LastHeight-FirstHeight>Height*.45f);
 TestTrue(TEXT("Small ring hidden only during dedicated ultimate"),Body->IsBoneHiddenByName(TEXT("BladeRing_P1_S_Root")));
 // The montage's last frame still maps inside the source throw window; the ring is
 // hidden when the action ends (CancelAction/Finish) rather than during playback.
 TestTrue(TEXT("Anticipation and release fade are authored on the ring"),Ring->bAnticipateBeforeThrow&&Ring->bFadeAfterSourceEnd);
 Anim->Montage_SetPosition(Montage,Montage->GetPlayLength()-.01f);Ring->Refresh();
 TestTrue(TEXT("Late throw phase keeps the ring above the head"),!Ring->IsVisible()||Ring->Bounds.GetBox().Min.Z>Ground+Height*1.05f);
 U->GetHeroSkill()->CancelAction();TestFalse(TEXT("Cancel hides ultimate ring synchronously"),Ring->IsVisible());
 TestFalse(TEXT("Cancel restores normal attack ring"),Body->IsBoneHiddenByName(TEXT("BladeRing_P1_S_Root")));
 T->GetHeroSkill()->SetShield(0);B->Damage(U,T,1000000);TestTrue(TEXT("Fatal hit still has a short target-owned burst"),Feedback->GetActiveBurstCount()>0);
 B->Stop();TestEqual(TEXT("Round stop clears pooled hit effects"),Feedback->GetActiveBurstCount(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHitFlipbook,"ElyGambit.ConfirmedSkills.AnimatedHitFlipbookDirectionAndVariants",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHitFlipbook::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();auto* B=E.GM->BattleRules.Get();auto* Feedback=T->GetHitFeedback();
 auto* Profile=Feedback->Profile.LoadSynchronous();if(!TestNotNull(TEXT("Target owns authored profile"),Profile))return false;
 TestTrue(TEXT("Complete animated atlas replaces static layer stack"),Profile->bAnimatedAtlas&&Profile->Layers.Num()==2);
 T->SetActorLocation(FVector(0,0,88));U->SetActorLocation(FVector(-600,0,88));B->Damage(U,T,10);
 const FVector Left=Feedback->LastImpactPoint;
 TestEqual(TEXT("Pardo selects slash animation"),Feedback->LastVariant,0);
 TestEqual(TEXT("One receipt plays one complete effect"),Feedback->GetVisiblePartCount(),1);
 U->SetActorLocation(FVector(600,0,88));B->Damage(U,T,10);
 TestTrue(TEXT("Opposite incoming directions move contact to opposite body surfaces"),FVector::Dist(Left,Feedback->LastImpactPoint)>UEGCombatComponent::BodyRadius(T));
 E.Tick(.1f);TArray<UStaticMeshComponent*> Parts;T->GetComponents(Parts);bool Advanced=false;
 for(auto* Part:Parts)if(Part->IsVisible())if(auto* MID=Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0)))
 {const float Progress=MID->K2_GetScalarParameterValue(TEXT("FrameProgress"));Advanced|=Progress>0&&Progress<1;TestEqual(TEXT("Pooled part belongs to receiver"),Part->GetOwner(),static_cast<AActor*>(T));}
 TestTrue(TEXT("Flipbook frame clock advances"),Advanced);
 auto* Cat=B->SpawnSummon(U->GetDefinition()->SummonDefinition,Id,1,FVector(-600,-600,88),false);
 if(!TestNotNull(TEXT("Independent Can source"),Cat))return false;Cat->GetCombat()->SetComponentTickEnabled(false);B->Damage(Cat,T,10);
 TestEqual(TEXT("Can selects distinct blunt animation"),Feedback->LastVariant,1);
 for(int32 I=0;I<12;++I)B->Damage(U,T,1);
 TestEqual(TEXT("At most four complete animations visible"),Feedback->GetVisiblePartCount(),4);
 E.Tick(.4f);TestEqual(TEXT("Atlas visuals clear after their one-shot lifetime"),Feedback->GetVisiblePartCount(),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAttackTrailRelease,"ElyGambit.ConfirmedSkills.AttackTrailSoftRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAttackTrailRelease::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();auto* S=U->GetHeroSkill();T->SetActorLocation(U->GetActorLocation()+FVector(150,0,0));
 if(!S->BeginAction(false))return false;
 auto* D=LoadObject<UEGSourceEffectDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/Effects/DA_Pardo_Attack01_Trail"));
 auto* FX=AEGSourceEffect::Spawn(S,U->GetVisiblePresentationMesh(),D,1.f);
 if(!TestNotNull(TEXT("Anchored native trail"),FX))return false;
 const int32 Before=S->CommitCount;TArray<UStaticMeshComponent*> Parts;FX->GetComponents(Parts);
 if(!TestTrue(TEXT("Trail geometry exists"),Parts.Num()>0))return false;
 auto* MID=Cast<UMaterialInstanceDynamic>(Parts[0]->GetMaterial(0));if(!MID)return false;
 TestTrue(TEXT("Strike begins with full source intensity"),MID->K2_GetVectorParameterValue(TEXT("SourceColor")).A>.9f);
 FX->Tick(.24f);TestTrue(TEXT("Nearly opaque source tail releases smoothly before it ends"),MID->K2_GetVectorParameterValue(TEXT("SourceColor")).A<.1f);
 TestTrue(TEXT("Original source samples remain unchanged"),D->Tracks[0].Samples.Last().Color.A>.9f);
 TestEqual(TEXT("Presentation release does not submit damage"),S->CommitCount,Before);FX->Destroy();S->CancelAction();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoAudioContacts,"ElyGambit.PardoAudio.ContactsAndWhiff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoAudioContacts::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();auto* S=U->GetHeroSkill();auto* A=U->FindComponentByClass<UEGCombatAudioComponent>();
 if(!TestNotNull(TEXT("Owned audio component"),A))return false;
 if(!TestTrue(TEXT("Combat audio fixture is resident before testing event deduplication"),E.PrepareAudio(U)))return false;
 TestTrue(TEXT("Formal sound table enabled"),U->GetDefinition()->PresentationDefinition->bUseEventAudio);
 for(int32 I=0;I<3;++I)
 {
  T->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));if(!S->BeginAction(false))return false;
  const int32 Before=A->EventCount,Commits=S->CommitCount,Attacks=E.GM->BattleRules->GetAttackCount();const float HP=T->GetAttributes()->GetHealth();
  S->AnimationEvent(TEXT("Audio_Swing"));S->AnimationEvent(TEXT("Audio_Swing"));
  TestEqual(TEXT("Duplicate swing plays once"),A->EventCount,Before+1);
  TestEqual(TEXT("Audio cannot commit damage"),S->CommitCount,Commits);TestEqual(TEXT("Audio preserves target health"),T->GetAttributes()->GetHealth(),HP);
  S->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Actual contact adds one sound"),A->EventCount,Before+2);
  S->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Duplicate contact has no second sound"),A->EventCount,Before+2);
  if(I==2){S->AnimationEvent(TEXT("HitSecond"));TestEqual(TEXT("Third stroke separate second hit"),A->EventCount,Before+3);}
  TestEqual(TEXT("Audio and split hits still count one attack"),E.GM->BattleRules->GetAttackCount(),Attacks+1);
  S->CancelAction();const int32 Cancelled=A->EventCount;S->AnimationEvent(TEXT("Audio_Swing"));TestEqual(TEXT("Cancelled action cannot sound"),A->EventCount,Cancelled);
 }
 T->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));if(!S->BeginAction(false))return false;
 T->SetActorLocation(U->GetActorLocation()+FVector(5000,0,0));const int32 Before=A->EventCount;S->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Whiff has no hit sound"),A->EventCount,Before);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoAudioLanding,"ElyGambit.PardoAudio.LandingTokensAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoAudioLanding::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Unit(Id);auto* T=E.Enemy();auto* S=U->GetHeroSkill();auto* A=U->FindComponentByClass<UEGCombatAudioComponent>();
 if(!TestTrue(TEXT("Landing audio fixture is resident"),E.PrepareAudio(U)))return false;
 T->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));if(!E.Cast(U))return false;
 const FGuid Cast=S->ActionId,Round=E.GM->BattleRules->GetRoundId();const int32 Before=A->EventCount;
 S->AnimationEvent(TEXT("Audio_Start"));S->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("Start and real successful launch each play once"),A->EventCount,Before+2);
 S->AnimationEvent(TEXT("Finish"));if(!S->BeginAction(false))return false;
 A->Land(Cast,FGuid::NewGuid(),T->GetActorLocation());TestEqual(TEXT("Stale round rejected without consuming launch"),A->EventCount,Before+2);
 A->Land(Cast,Round,T->GetActorLocation());TestEqual(TEXT("Landing survives finished cast and a newer action"),A->EventCount,Before+3);
 A->Land(Cast,Round,T->GetActorLocation());TestEqual(TEXT("Duplicate landing rejected"),A->EventCount,Before+3);
 E.GM->BattleRules->Stop();A->Land(FGuid::NewGuid(),Round,T->GetActorLocation());TestEqual(TEXT("End battle clears voices"),A->GetActiveVoiceCount(),0);
 TestEqual(TEXT("Stopped round cannot emit"),A->EventCount,Before+3);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoVoiceAndSilentCan,"ElyGambit.PardoAudio.UltimateVoiceAndSilentCan",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoVoiceAndSilentCan::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();
 auto* U=E.Unit(Id);auto* T=E.Enemy();T->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));auto* S=U->GetHeroSkill();auto* A=U->FindComponentByClass<UEGCombatAudioComponent>();
 if(!TestTrue(TEXT("Voice audio fixture is resident"),E.PrepareAudio(U)))return false;
 FString Previous;
 for(int32 I=0;I<4;++I)
 {
  if(!E.Cast(U))return false;const int32 Before=A->EventCount,Commits=S->CommitCount;
  S->AnimationEvent(TEXT("Audio_Voice"));TestEqual(TEXT("Exactly one full voice variant"),A->EventCount,Before+1);
  TestTrue(TEXT("Voice references native CN assets"),A->LastSoundPath.Contains(TEXT("Ultimate_CN")));
  TestTrue(TEXT("Native random container avoids immediate repeat"),Previous.IsEmpty()||Previous!=A->LastSoundPath);Previous=A->LastSoundPath;
  S->AnimationEvent(TEXT("Audio_Voice"));TestEqual(TEXT("Voice notify is deduplicated"),A->EventCount,Before+1);TestEqual(TEXT("Voice has no gameplay commits"),S->CommitCount,Commits);S->CancelAction();
 }
 if(!E.Cast(U))return false;S->AnimationEvent(TEXT("Release"));auto* Cat=E.GM->BattleRules->FindCan(Id);if(!Cat)return false;
 Cat->GetCombat()->SetComponentTickEnabled(false);E.Tick(1.2f);S->CancelAction();
 Cat->SetActorLocation(U->GetActorLocation()+FVector(300,0,0));T->SetActorLocation(Cat->GetActorLocation()+FVector(100,0,0));
 auto* CA=Cat->FindComponentByClass<UEGCombatAudioComponent>();const int32 Before=CA->EventCount;
 TestTrue(TEXT("Can still attacks normally"),Cat->GetHeroSkill()->BeginAction(false));Cat->GetHeroSkill()->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Can basic hit emits no sound"),CA->EventCount,Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPardoOverheadPortal,"ElyGambit.ConfirmedSkills.PardoOverheadPortalAndDescent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPardoOverheadPortal::RunTest(const FString&)
{
 FConfirmedWorld E(*this);if(!TestNotNull(TEXT("Fixture cards"),E.Cards))return false;const auto Id=E.Deploy(TEXT("Pardo"),1);if(!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Unit(Id);auto* T=E.Enemy();
 T->SetActorLocation(U->GetActorLocation()+FVector(650,0,0));if(!E.Cast(U))return false;
 E.Tick(1.25f);auto* Cat=E.GM->BattleRules->FindCan(Id);if(!TestNotNull(TEXT("Natural release creates Can"),Cat))return false;Cat->GetCombat()->SetComponentTickEnabled(false);
 AEGSkillProjectile* P=nullptr;for(TActorIterator<AEGSkillProjectile> It(E.W);It;++It)if(It->Cargo==Cat&&!It->IsActorBeingDestroyed())P=*It;
 if(!TestNotNull(TEXT("Real descent actor"),P))return false;
 auto* Ring=U->FindComponentByClass<UEGUltimateRingComponent>();Ring->Refresh();const FVector Portal=Ring->GetOverheadCenter();
 TestTrue(TEXT("Portal is directly above the locked impact point"),FVector::DistSquared2D(Portal,P->Destination)<1.f);
 TestTrue(TEXT("Portal clears victim head by two Pardo body heights"),Portal.Z>=P->Destination.Z-88+T->GetDefinition()->GetPresentationHeight()+U->GetDefinition()->GetPresentationHeight()*1.99f);
 TestTrue(TEXT("Cat emerges from the portal"),FMath::Abs(Cat->GetActorLocation().Z-88-Portal.Z)<60.f);
 TestTrue(TEXT("Flight preserves the live cat material without glow replacement"),P->SavedCargoMaterial==nullptr);
 const float Start=Cat->GetActorLocation().Z,HP=T->GetAttributes()->GetHealth();E.Tick(.5f);
 TestTrue(TEXT("Substantial visible travel before impact"),Cat->GetActorLocation().Z<Start-100&&Cat->GetActorLocation().Z>P->Destination.Z+100);
 TestEqual(TEXT("Visual descent never applies early damage"),T->GetAttributes()->GetHealth(),HP);
 const FVector Locked=Ring->GetOverheadCenter();T->SetActorLocation(T->GetActorLocation()+FVector(100,0,0));Ring->Refresh();TestTrue(TEXT("Portal does not chase victim after release"),Locked.Equals(Ring->GetOverheadCenter(),.01f));
 E.Tick(.7f);TestFalse(TEXT("Flight completes without changing landing time"),Cat->GetHeroSkill()->bInFlight);return true;
}
#endif
