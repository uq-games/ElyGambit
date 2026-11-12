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
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "UI/EGUnitInspectionView.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Skills/EGHumanArrowProjectile.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/Effects/EGEdenManaEffect.h"
#include "AbilitySystem/Effects/EGTeamSpeedEffect.h"
#include "AbilitySystemComponent.h"
#include "EGGameplayTags.h"
#include "GameplayEffect.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"

namespace
{
float OriginContactSpacing(const AEGChessUnit* Human)
{
 const auto* Skill=Human->GetHeroSkill();
 const auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 const auto* Mesh=Human->GetVisiblePresentationMesh();
 const auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
 const float Rate=Anim&&Montage?Anim->Montage_GetPlayRate(Montage)*Montage->RateScale:0.f;
 return Rate>.01f?.12f/Rate:.12f;
}
struct FHumanWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W=nullptr;AEGDemoGameMode* GM=nullptr;
 AEGChessUnit* Human=nullptr;TArray<AEGChessUnit*> Enemies;
 explicit FHumanWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  TSoftObjectPtr<UEGCardDefinition> FormalHumanCard;
  GM->RunDefinition=EGProductionTestAssets::ResolveRun(Test,&FormalHumanCard);
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  // The production reward supplies Human via this binding, not the opening pool.
  UEGCardDefinition* Card=FormalHumanCard.LoadSynchronous();
  if(!Test.TestNotNull(TEXT("Formal HumanFusionCard"),Card)||!Card->HeroDefinition||Card->HeroDefinition->HeroKind!=EEGHeroKind::ElysiaHerrscher)return;
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,1})||!GM->EnterCombat())return;
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);
   // This fixture supplies a query-only board and no physical floor. Keep
   // targets at their assigned cells while the world advances for arrow flight.
   U->GetCharacterMovement()->DisableMovement();
   if(U->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=U;
   else if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)
   {
    Enemies.Add(U);auto* ASC=U->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   }
  }
  if(Human)for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Grid->CellToWorld({2+I,4})+FVector(0,0,88));
 }
 ~FHumanWorld(){if(W){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}}
 bool Ready()const{return Human&&Enemies.Num()>=2&&Cast<UEGElysiaHerrscherSkillBehavior>(Human->GetHeroSkill()->GetBehavior());}
 void Advance(float Time){while(Time>0){const float Step=FMath::Min(Time,.02f);++GFrameCounter;W->Tick(LEVELTICK_All,Step);Time-=Step;}}
 UEGElysiaHerrscherSkillBehavior* Policy()const{return Human?Cast<UEGElysiaHerrscherSkillBehavior>(Human->GetHeroSkill()->GetBehavior()):nullptr;}
 bool EnterOrigin(FAutomationTestBase& Test)
 {
  auto* S=Human->GetHeroSkill();
  for(int32 I=0;I<8;++I)
  {
   if(!Test.TestTrue(TEXT("Bow attack starts"),S->BeginAction(false)))return false;
   S->AnimationEvent(TEXT("Hit"));
   Test.TestEqual(TEXT("Each released arrow immediately restores ten mana"),Human->GetAttributes()->GetMana(),30.f+I*10.f);
   S->AnimationEvent(TEXT("Finish"));
  }
  if(!Test.TestEqual(TEXT("Eight released arrows fill mana"),Human->GetAttributes()->GetMana(),100.f))return false;
  Advance(2.f); // Let the already released arrows land before later origin assertions.
  if(!Test.TestTrue(TEXT("Enter-origin montage starts after eighth arrow"),S->BeginAction(true)))return false;
  S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
  return Test.TestTrue(TEXT("Origin begins at completed transition"),Policy()->IsOriginActive());
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanPassiveSnapshot,"ElyGambit.ElysiaHerrscher.PassiveSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanPassiveSnapshot::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* S=E.Human->GetHeroSkill();auto* P=E.Policy();
 auto* D=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Human skill rules"),D))return false;
 TGuardValue<int32> BowInterval(D->BowAttackInterval,3);
 auto State=P->GetPassiveState();
 TestTrue(TEXT("Initial state is bow"),State.Phase==EEGElysiaPassivePhase::Bow);
 TestEqual(TEXT("Bow threshold follows definition"),State.Threshold,D->BowAttackInterval);
 TestTrue(TEXT("Snapshot belongs to the current round"),State.RoundId==E.GM->BattleRules->GetRoundId());
 for(int32 Index=0;Index<2;++Index)
 {
  if(!TestTrue(TEXT("Bow action starts"),S->BeginAction(false)))return false;
  TestTrue(TEXT("Active action is exposed"),P->GetPassiveState().ActionId==S->ActionId);
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
 }
 State=P->GetPassiveState();TestEqual(TEXT("Two completed bow actions"),State.Progress,2);
 TestTrue(TEXT("Next bow action is piercing"),State.bNextAttackEnhanced);
 if(!TestTrue(TEXT("Third bow action starts"),S->BeginAction(false)))return false;
 S->CancelAction();TestEqual(TEXT("Cancelled attack does not advance"),P->GetPassiveState().Progress,2);
 if(!TestTrue(TEXT("Third bow action can restart"),S->BeginAction(false)))return false;
 S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(.05f);
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("Origin transition starts"),S->BeginAction(true)))return false;
 TestTrue(TEXT("Entry is visible before release"),P->GetPassiveState().Phase==EEGElysiaPassivePhase::Entering);
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 State=P->GetPassiveState();
 TestTrue(TEXT("Finished entry reports origin"),State.Phase==EEGElysiaPassivePhase::Origin);
 TestEqual(TEXT("Origin has no enhanced attack counter"),State.Threshold,0);
 TestTrue(TEXT("Origin reports its actual remaining time"),State.RemainingSeconds>0.f&&State.RemainingSeconds<=D->OriginSeconds);
 E.Advance(.25f);TestTrue(TEXT("Origin countdown advances"),P->GetPassiveState().RemainingSeconds<State.RemainingSeconds);
 P->EndBattle();State=P->GetPassiveState();
 TestTrue(TEXT("Stopped behavior has no live passive"),State.Phase==EEGElysiaPassivePhase::Inactive&&!State.RoundId.IsValid()&&!State.ActionId.IsValid());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanLifecycle,"ElyGambit.ElysiaHerrscher.BowManaShieldAndFinisher",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanLifecycle::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* S=E.Human->GetHeroSkill();auto* ASC=E.Human->GetAbilitySystemComponent();
 const float MaxHealth=E.Human->GetAttributes()->GetMaxHealth();
 S->Configure(E.GM->BattleRules,11,false);
 TestEqual(TEXT("Initial mana"),E.Human->GetAttributes()->GetMana(),20.f);
 TestEqual(TEXT("One initial crystal mirror"),S->GetSourceShield(TEXT("ElysiaHerrscher.CrystalMirror")),MaxHealth*.2f);
 S->SetTaunt(E.Enemies[0],3.f);TestFalse(TEXT("Human is immune to taunt"),S->IsTauntActive());
 E.GM->BattleRules->Damage(E.Enemies[0],E.Human,MaxHealth*.1f+E.Human->GetAttributes()->GetDefense(),FGuid::NewGuid());
 TestTrue(TEXT("Damage consumes the shield"),S->GetSourceShield(TEXT("ElysiaHerrscher.CrystalMirror"))<MaxHealth*.2f);
 if(!E.EnterOrigin(*this))return false;
 TestTrue(TEXT("Entry refills the same shield"),FMath::IsNearlyEqual(S->GetSourceShield(TEXT("ElysiaHerrscher.CrystalMirror")),MaxHealth*.2f,.01f));
 const float Mana=E.Human->GetAttributes()->GetMana();E.Human->RestoreMana(50.f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),80.f);
 TestEqual(TEXT("Origin blocks external and direct positive mana"),E.Human->GetAttributes()->GetMana(),Mana);
 auto Song=ASC->MakeOutgoingSpec(UEGEdenManaEffect::StaticClass(),1.f,ASC->MakeEffectContext());
 if(!TestTrue(TEXT("Periodic ally mana effect can be created"),Song.IsValid()))return false;
 Song.Data->SetSetByCallerMagnitude(EGTags::Mana,9.f);Song.Data->SetDuration(2.f,true);
 ASC->ApplyGameplayEffectSpecToSelf(*Song.Data);E.Advance(1.1f);
 TestEqual(TEXT("Origin blocks periodic ally mana GE"),E.Human->GetAttributes()->GetMana(),Mana);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100.f);
 const float BeforeAttack=E.Human->GetAttributes()->GetAttackPower(),BeforeDefense=E.Human->GetAttributes()->GetDefense();
 TStrongObjectPtr<UGameplayEffect> Debuff(NewObject<UGameplayEffect>());Debuff->DurationPolicy=EGameplayEffectDurationType::Infinite;
 for(const auto& Attribute:{UEGAttributeSet::GetAttackPowerAttribute(),UEGAttributeSet::GetDefenseAttribute()})
 {
  FGameplayModifierInfo Mod;Mod.Attribute=Attribute;Mod.ModifierOp=EGameplayModOp::Multiplicitive;
  Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(.5f));Debuff->Modifiers.Add(Mod);
 }
 FGameplayEffectSpec DebuffSpec(Debuff.Get(),ASC->MakeEffectContext(),1.f);
 const FActiveGameplayEffectHandle DebuffHandle=ASC->ApplyGameplayEffectSpecToSelf(DebuffSpec);
 TestTrue(TEXT("Attack/defense debuff GE applies"),DebuffHandle.IsValid());
 TestTrue(TEXT("Attack debuff remains effective"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetAttackPower(),BeforeAttack*.5f));
 TestTrue(TEXT("Defense debuff remains effective"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetDefense(),BeforeDefense*.5f));
 const float Raw=E.Human->GetAttributes()->GetAttackPower();
 const float HP0=E.Enemies[0]->GetAttributes()->GetHealth(),HP1=E.Enemies[1]->GetAttributes()->GetHealth();
 E.Advance(E.Policy()->GetPassiveState().RemainingSeconds+.1f);
 if(!TestTrue(TEXT("Finisher begins as its own action"),S->IsCasting()))return false;
 const FGuid Token=S->ActionId;TestTrue(TEXT("Finisher action has an identity"),Token.IsValid());
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Release"));
 const float Delta0=HP0-E.Enemies[0]->GetAttributes()->GetHealth(),Delta1=HP1-E.Enemies[1]->GetAttributes()->GetHealth();
 TestTrue(TEXT("One primary and one other enemy receive nonstacked finisher damage"),
  (FMath::IsNearlyEqual(Delta0,Raw*7.f,.1f)&&FMath::IsNearlyEqual(Delta1,Raw*7.f,.1f))||
  (FMath::IsNearlyEqual(Delta1,Raw*7.f,.1f)&&FMath::IsNearlyEqual(Delta0,Raw*7.f,.1f)));
 S->AnimationEvent(TEXT("Finish"));
 TestFalse(TEXT("Finished finisher returns to bow"),E.Policy()->IsOriginActive());
 TestEqual(TEXT("Mana resets after finisher"),E.Human->GetAttributes()->GetMana(),0.f);
 TestEqual(TEXT("Bow cycle resets"),E.Policy()->GetBowAttackCount(),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanControlImmunity,"ElyGambit.ElysiaHerrscher.TaggedControlImmunity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanControlImmunity::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* HumanASC=E.Human->GetAbilitySystemComponent();auto* OrdinaryASC=E.Enemies[0]->GetAbilitySystemComponent();
 const float InitialHumanSpeed=E.Human->GetAttributes()->GetAttackSpeed(),InitialOrdinarySpeed=E.Enemies[0]->GetAttributes()->GetAttackSpeed();
 const FGameplayTag ControlTags[]={EGTags::ControlStun,EGTags::ControlFreeze,EGTags::ControlSilence,EGTags::ControlForcedMovement};
 for(const FGameplayTag& Tag:ControlTags)
 {
  auto Tagged=HumanASC->MakeOutgoingSpec(UEGTeamSpeedEffect::StaticClass(),1.f,HumanASC->MakeEffectContext());
  if(!TestTrue(TEXT("Synthetic control effect constructed"),Tagged.IsValid()))return false;
  Tagged.Data->AddDynamicAssetTag(Tag);Tagged.Data->SetSetByCallerMagnitude(EGTags::TeamAttackSpeed,.5f);
  const auto HumanHandle=HumanASC->ApplyGameplayEffectSpecToSelf(*Tagged.Data);
  const auto OrdinaryHandle=OrdinaryASC->ApplyGameplayEffectSpecToSelf(*Tagged.Data);
  TestFalse(TEXT("Tagged control rejected before applying to human"),HumanHandle.IsValid());
  TestTrue(TEXT("Ordinary enemy still accepts the tagged control"),OrdinaryHandle.IsValid());
  TestTrue(TEXT("Human attack speed stays unchanged"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetAttackSpeed(),InitialHumanSpeed));
  TestTrue(TEXT("Control affected ordinary enemy"),FMath::IsNearlyEqual(E.Enemies[0]->GetAttributes()->GetAttackSpeed(),InitialOrdinarySpeed*.5f));
  if(OrdinaryHandle.IsValid())OrdinaryASC->RemoveActiveGameplayEffect(OrdinaryHandle);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanAttackCycle,"ElyGambit.ElysiaHerrscher.BowPierceAndOriginAttackCycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanAttackCycle::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
 auto* Other=E.Enemies[0]==Main?E.Enemies[1]:E.Enemies[0];
 if(!TestNotNull(TEXT("Attack target"),Main)||!TestNotNull(TEXT("Other target"),Other))return false;
 const float Raw=E.Human->GetAttributes()->GetAttackPower();
 for(int32 I=0;I<8;++I)
 {
  const float BeforeMain=Main->GetAttributes()->GetHealth(),BeforeOther=Other->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Bow action starts"),S->BeginAction(false)))return false;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Hit"));
  TWeakObjectPtr<AEGHumanArrowProjectile> Shot;int32 ShotCount=0;
  for(TActorIterator<AEGHumanArrowProjectile> It(E.W);It;++It)
   if(It->Source.Get()==E.Human&&It->AttackId==S->ActionId){Shot=*It;++ShotCount;}
  TestEqual(TEXT("Duplicate hit notifies launch one human arrow"),ShotCount,1);
  const FVector ShotStart=Shot.IsValid()?Shot->GetActorLocation():FVector::ZeroVector;
  const float ShotDamage=Shot.IsValid()?Shot->RawDamage:0.f;
  const bool bShotPierces=Shot.IsValid()&&Shot->bPiercing;
  const FVector TargetStart=Main->GetActorLocation();
  S->AnimationEvent(TEXT("Finish"));
  TestEqual(TEXT("Launch does not immediately damage the target"),Main->GetAttributes()->GetHealth(),BeforeMain);
  E.Advance(2.f);
  const float ActualDamage=BeforeMain-Main->GetAttributes()->GetHealth();
  if(!FMath::IsNearlyEqual(ActualDamage,Raw*(I==4?3.f:1.6f),.1f))
   AddInfo(FString::Printf(TEXT("Bow shot %d: damage %.3f expected %.3f projectile raw %.3f pierce %d start %s target start %s target end %s projectile end %s"),
    I+1,ActualDamage,Raw*(I==4?3.f:1.6f),ShotDamage,bShotPierces,*ShotStart.ToString(),*TargetStart.ToString(),
    *Main->GetActorLocation().ToString(),Shot.IsValid()?*Shot->GetActorLocation().ToString():TEXT("destroyed")));
  TestTrue(TEXT("Bow main receives exactly 160% or fifth arrow 300%"),FMath::IsNearlyEqual(ActualDamage,Raw*(I==4?3.f:1.6f),.1f));
  TestTrue(TEXT("One-cell arrow does not pierce the neighboring lane"),FMath::IsNearlyEqual(BeforeOther-Other->GetAttributes()->GetHealth(),0.f,.1f));
 }
 if(!TestTrue(TEXT("Origin transition starts after eighth arrow"),S->BeginAction(true)))return false;
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 const auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
 auto* Animation=E.Human->GetVisiblePresentationMesh()->GetAnimInstance();
 if(!TestNotNull(TEXT("Human origin rules"),Rules)||!TestNotNull(TEXT("Human animation instance"),Animation))return false;
 for(int32 I=0;I<3;++I)
 {
  const float BeforeMain=Main->GetAttributes()->GetHealth(),BeforeOther=Other->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Origin action starts"),S->BeginAction(false)))return false;
  if(!TestNotNull(TEXT("Origin montage"),S->GetActiveMontage()))return false;
  TestTrue(TEXT("Origin montage uses authored speed scaled by configured rate and attack speed"),
   FMath::IsNearlyEqual(Animation->Montage_GetPlayRate(S->GetActiveMontage()),E.Human->GetAttributes()->GetAttackSpeed()*Rules->OriginActionPlaybackRate,.001f));
  TestFalse(TEXT("An unfinished origin action cannot start another attack"),S->BeginAction(false));
  const float Spacing=OriginContactSpacing(E.Human);
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(Spacing*2.f+.04f);
  TestTrue(TEXT("Origin main receives all three contacts once"),FMath::IsNearlyEqual(BeforeMain-Main->GetAttributes()->GetHealth(),Raw*4.5f,.1f));
  TestTrue(TEXT("Other target receives total splash once"),FMath::IsNearlyEqual(BeforeOther-Other->GetAttributes()->GetHealth(),Raw*2.25f,.1f));
 }
 TestEqual(TEXT("Origin attacks generate no mana"),E.Human->GetAttributes()->GetMana(),0.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanOriginThreeContacts,"ElyGambit.ElysiaHerrscher.OriginThreeContactsLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanOriginThreeContacts::RunTest(const FString&)
{
 // The fixture has stationary high-health, zero-defense targets and no AI.
 // Duplicate notifies cannot enqueue a second set of contacts.
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Three-contact fixture"),E.Ready())||!E.EnterOrigin(*this))return false;
  auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
  const auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
  if(!TestNotNull(TEXT("Rules"),Rules)||!TestNotNull(TEXT("Target"),Main))return false;
  const float Before=Main->GetAttributes()->GetHealth(),Total=E.Human->GetAttributes()->GetAttackPower()*Rules->ScepterMain;
  if(!TestTrue(TEXT("Origin attack begins"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();auto* Anim=E.Human->GetVisiblePresentationMesh()->GetAnimInstance();
  if(!TestNotNull(TEXT("Origin montage"),Montage)||!TestNotNull(TEXT("Origin anim"),Anim))return false;
  TGuardValue<float> UnitAssetRate(Montage->RateScale,1.f);Anim->Montage_SetPlayRate(Montage,1.f);
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Hit"));
  TestTrue(TEXT("First contact is sixty percent"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*.6f,.1f));
  S->AnimationEvent(TEXT("Finish"));E.Advance(.08f);
  TestTrue(TEXT("No early follow-up"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*.6f,.1f));
  E.Advance(.06f);
  TestTrue(TEXT("Second contact after completion is twenty percent"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*.8f,.1f));
  E.Advance(.12f);
  TestTrue(TEXT("Third contact completes total damage"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total,.1f));
  E.Advance(.3f);
  TestTrue(TEXT("No extra contact from duplicate notify"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total,.1f));
 }
 // Two real montage play rates and asset scales: a completed action retains
 // its pending contacts at the cadence of the committed animation, rather
 // than using an unscaled fixed world-time delay.
 for(int32 Case=0;Case<2;++Case)
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Scaled cadence fixture"),E.Ready())||!E.EnterOrigin(*this))return false;
  auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
  auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
  if(!TestNotNull(TEXT("Scaled cadence target"),Main)||!TestNotNull(TEXT("Scaled cadence rules"),Rules)||
     !TestTrue(TEXT("Scaled cadence action begins"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();auto* Anim=E.Human->GetVisiblePresentationMesh()->GetAnimInstance();
  if(!TestNotNull(TEXT("Scaled cadence montage"),Montage)||!TestNotNull(TEXT("Scaled cadence anim"),Anim))return false;
  TGuardValue<float> AssetRate(Montage->RateScale,Case==0?1.5f:.75f);
  Anim->Montage_SetPlayRate(Montage,Case==0?.5f:2.f);
  const float Interval=Case==0?.16f:.08f;
  const float Before=Main->GetAttributes()->GetHealth(),Total=E.Human->GetAttributes()->GetAttackPower()*Rules->ScepterMain;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(Interval*.55f);
  TestTrue(TEXT("Scaled second contact waits for animation cadence"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*.6f,.1f));
  E.Advance(Interval*.6f);
  TestTrue(TEXT("Scaled second contact arrives once"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*.8f,.1f));
  E.Advance(Interval);
  TestTrue(TEXT("Scaled third contact completes original total"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total,.1f));
 }
 // Interrupt, death and round end each invalidate the two pending contacts.
 for(int32 Reason=0;Reason<3;++Reason)
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Interruption fixture"),E.Ready())||!E.EnterOrigin(*this))return false;
  auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
  if(!TestNotNull(TEXT("Interruption target"),Main)||!TestTrue(TEXT("Origin attack starts"),S->BeginAction(false)))return false;
  S->AnimationEvent(TEXT("Hit"));const float AfterFirst=Main->GetAttributes()->GetHealth();
  if(Reason==0)S->CancelAction();else if(Reason==1)E.Human->Die();else E.GM->BattleRules->Stop();
  E.Advance(.4f);
  TestEqual(TEXT("Interrupted contact never damages later"),Main->GetAttributes()->GetHealth(),AfterFirst);
 }
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Expiry fixture"),E.Ready()))return false;
  auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
  if(!TestNotNull(TEXT("Expiry rules"),Rules))return false;
  TGuardValue<float> ShortOrigin(Rules->OriginSeconds,.2f);
  if(!E.EnterOrigin(*this))return false;
  auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
  if(!TestNotNull(TEXT("Expiry target"),Main)||!TestTrue(TEXT("Origin attack starts before expiry"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();auto* Anim=E.Human->GetVisiblePresentationMesh()->GetAnimInstance();
  if(!TestNotNull(TEXT("Expiry montage"),Montage)||!TestNotNull(TEXT("Expiry anim"),Anim))return false;
  TGuardValue<float> UnitAssetRate(Montage->RateScale,1.f);Anim->Montage_SetPlayRate(Montage,1.f);
  const float Before=Main->GetAttributes()->GetHealth(),Total=E.Human->GetAttributes()->GetAttackPower()*Rules->ScepterMain;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(.26f);
  TestTrue(TEXT("Expiry cancels the third contact"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*.8f,.1f));
 }
 // One long frame spans the deadline. A contact due before that deadline must
 // settle; a contact due after it must not, even though both are now overdue.
 for(int32 Case=0;Case<2;++Case)
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Long-frame fixture"),E.Ready()))return false;
  auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
  if(!TestNotNull(TEXT("Long-frame rules"),Rules))return false;
  TGuardValue<float> ShortOrigin(Rules->OriginSeconds,Case==0?.2f:.3f);
  if(!E.EnterOrigin(*this))return false;
  auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
  if(!TestNotNull(TEXT("Long-frame target"),Main)||!TestTrue(TEXT("Attack starts"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();auto* Anim=E.Human->GetVisiblePresentationMesh()->GetAnimInstance();
  if(!TestNotNull(TEXT("Long-frame montage"),Montage)||!TestNotNull(TEXT("Long-frame anim"),Anim))return false;
  TGuardValue<float> UnitAssetRate(Montage->RateScale,1.f);Anim->Montage_SetPlayRate(Montage,1.f);
  const float Before=Main->GetAttributes()->GetHealth(),Total=E.Human->GetAttributes()->GetAttackPower()*Rules->ScepterMain;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
  const float FrameStart=E.W->GetTimeSeconds();++GFrameCounter;E.W->Tick(LEVELTICK_All,.36f);
  TestTrue(TEXT("A single frame spans the origin deadline"),E.W->GetTimeSeconds()-FrameStart>Rules->OriginSeconds);
  TestTrue(TEXT("Only contacts scheduled before expiry settle"),
   FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*(Case==0?.8f:1.f),.1f));
  E.Advance(.2f);
  TestTrue(TEXT("Expired contacts never arrive on subsequent frames"),
   FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Total*(Case==0?.8f:1.f),.1f));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanConfiguredCycle,"ElyGambit.ElysiaHerrscher.ConfiguredCycleAndDescription",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanConfiguredCycle::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* D=Cast<UEGElysiaHerrscherSkillDefinition>(E.Human->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Human rules"),D))return false;
 TGuardValue<int32> BowInterval(D->BowAttackInterval,2),OriginInterval(D->OriginAttackInterval,2);
 TGuardValue<float> BowMultiplier(D->BowAttack,.75f),ArrowWidth(D->ArrowLaneWidthCells,1.5f),WaveWidth(D->OriginWaveWidthCells,2.5f);
 TGuardValue<float> OriginRate(D->OriginActionPlaybackRate,.6f);
 TGuardValue<int32> SplashRadius(D->ScepterSplashRadiusCells,2);
 auto* S=E.Human->GetHeroSkill();auto* Main=E.Human->GetCombat()->FindNearestEnemy();
 if(!TestNotNull(TEXT("Target"),Main))return false;
 const float Raw=E.Human->GetAttributes()->GetAttackPower();
 for(int32 I=0;I<2;++I)
 {
  const float Before=Main->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Configured bow attack starts"),S->BeginAction(false)))return false;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
  E.Advance(2.f);
  TestTrue(TEXT("Configured bow cycle drives ordinary or charged damage"),
   FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Raw*(I==0?.75f:3.f),.1f));
 }
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("Configured human transition starts"),S->BeginAction(true)))return false;
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 auto* Animation=E.Human->GetVisiblePresentationMesh()->GetAnimInstance();
 if(!TestNotNull(TEXT("Human animation instance"),Animation))return false;
 for(int32 I=0;I<2;++I)
 {
  const float Before=Main->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Configured origin attack starts"),S->BeginAction(false)))return false;
  if(!TestNotNull(TEXT("Configured origin montage"),S->GetActiveMontage()))return false;
  TestTrue(TEXT("Origin playback multiplier is configurable"),
   FMath::IsNearlyEqual(Animation->Montage_GetPlayRate(S->GetActiveMontage()),E.Human->GetAttributes()->GetAttackSpeed()*.6f,.001f));
  const float Spacing=OriginContactSpacing(E.Human);
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(Spacing*2.f+.04f);
  TestTrue(TEXT("Configured origin cycle drives scepter or wave damage"),
   FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),Raw*D->ScepterMain,.1f));
 }
 const FString Text=E.Human->GetDefinition()->GetPlayerFacingSkillEffect().ToString();
 TestTrue(TEXT("Description reflects configurable attack multipliers"),Text.Contains(TEXT("75%")));
 TestTrue(TEXT("Description reflects both configured intervals"),Text.Contains(TEXT("每第2箭")));
 TestTrue(TEXT("Description excludes retired origin wave"),!Text.Contains(TEXT("贯穿波")));
 TestTrue(TEXT("Description reflects splash radius"),Text.Contains(TEXT("周围2格")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanArrowContact,"ElyGambit.ElysiaHerrscher.ArrowContactAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanArrowContact::RunTest(const FString&)
{
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("High flight fixture"),E.Ready()))return false;
  auto* Main=E.Human->GetCombat()->FindNearestEnemy();if(!TestNotNull(TEXT("High flight target"),Main))return false;
  FVector Start=E.Human->GetActorLocation();
  Start.Z=Main->GetActorLocation().Z+FMath::Max(200.f,Main->GetDefinition()->GetPresentationHeight())+200.f;
  const FVector Direction=(Main->GetActorLocation()-Start).GetSafeNormal2D();
  auto* Arrow=E.W->SpawnActor<AEGHumanArrowProjectile>(AEGHumanArrowProjectile::StaticClass(),Start,Direction.Rotation());
  if(!TestNotNull(TEXT("High flight arrow"),Arrow))return false;
  Arrow->Battle=E.GM->BattleRules;Arrow->Round=E.GM->BattleRules->GetRoundId();Arrow->Source=E.Human;
  Arrow->AttackId=FGuid::NewGuid();Arrow->RawDamage=E.Human->GetAttributes()->GetAttackPower();
  Arrow->InitializeArrow(E.Policy(),Direction,2200.f,25.f,25.f,3.f,false,0.f);
  const float Before=Main->GetAttributes()->GetHealth();E.Advance(2.f);
  TestEqual(TEXT("An arrow flying above the body does not hit"),Main->GetAttributes()->GetHealth(),Before);
 }
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
  auto* Main=E.Enemies[0];auto* Behind=E.Enemies[1];
  auto* Stats=E.Human->GetDefinition()->StatsDefinition.Get();
  if(!TestNotNull(TEXT("Human independent stats"),Stats))return false;
  TGuardValue<float> MisconfiguredHitMana(Stats->ManaOnHit,7.f);
  const FVector Direction=(Main->GetActorLocation()-E.Human->GetActorLocation()).GetSafeNormal2D();
  Behind->SetActorLocation(Main->GetActorLocation()+Direction*500.f);
  for(int32 I=2;I<E.Enemies.Num();++I)E.Enemies[I]->SetActorLocation(Main->GetActorLocation()+FVector(0,1500,0));
  auto* S=E.Human->GetHeroSkill();const float Raw=E.Human->GetAttributes()->GetAttackPower();
  for(int32 I=0;I<4;++I)
  {
   if(!TestTrue(TEXT("Earlier bow action starts"),S->BeginAction(false)))return false;
   S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(2.f);
   TestEqual(TEXT("Contact grants no extra mana even with nonzero shared hit setting"),E.Human->GetAttributes()->GetMana(),30.f+10.f*I);
  }
  const float BeforeMain=Main->GetAttributes()->GetHealth(),BeforeBehind=Behind->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Piercing bow action starts"),S->BeginAction(false)))return false;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Hit"));
  TWeakObjectPtr<AEGHumanArrowProjectile> Shot;
  for(TActorIterator<AEGHumanArrowProjectile> It(E.W);It;++It)
   if(It->Source.Get()==E.Human&&It->AttackId==S->ActionId){Shot=*It;break;}
  TestTrue(TEXT("Fifth action launches a piercing arrow"),Shot.IsValid()&&Shot->bPiercing);
  const FVector ShotStart=Shot.IsValid()?Shot->GetActorLocation():FVector::ZeroVector;
  const FVector MainStart=Main->GetActorLocation(),BehindStart=Behind->GetActorLocation();
  const float ShotDamage=Shot.IsValid()?Shot->RawDamage:0.f;
  TestEqual(TEXT("Hit notification only launches"),Main->GetAttributes()->GetHealth(),BeforeMain);
  E.Advance(.1f);TestEqual(TEXT("Arrow in flight has not reached target"),Main->GetAttributes()->GetHealth(),BeforeMain);
  S->AnimationEvent(TEXT("Finish"));
  E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
  if(!TestTrue(TEXT("External mana enables transition with arrow in flight"),S->BeginAction(true)))return false;
  S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
  E.Advance(1.5f);
  if(!FMath::IsNearlyEqual(BeforeMain-Main->GetAttributes()->GetHealth(),Raw*3.f,.1f)||
     !FMath::IsNearlyEqual(BeforeBehind-Behind->GetAttributes()->GetHealth(),Raw*3.f,.1f))
   AddInfo(FString::Printf(TEXT("Piercing shot: main %.3f behind %.3f expected %.3f projectile raw %.3f start %s main %s -> %s behind %s -> %s projectile end %s"),
    BeforeMain-Main->GetAttributes()->GetHealth(),BeforeBehind-Behind->GetAttributes()->GetHealth(),Raw*3.f,ShotDamage,*ShotStart.ToString(),
    *MainStart.ToString(),*Main->GetActorLocation().ToString(),*BehindStart.ToString(),*Behind->GetActorLocation().ToString(),
    Shot.IsValid()?*Shot->GetActorLocation().ToString():TEXT("destroyed")));
  TestTrue(TEXT("Main receives one 300 percent contact hit after form transition"),FMath::IsNearlyEqual(BeforeMain-Main->GetAttributes()->GetHealth(),Raw*3.f,.1f));
  TestTrue(TEXT("Aligned enemy receives one piercing contact"),FMath::IsNearlyEqual(BeforeBehind-Behind->GetAttributes()->GetHealth(),Raw*3.f,.1f));
 }
 for(bool bKillSource:{false,true})
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Cleanup fixture"),E.Ready()))return false;
  auto* Main=E.Human->GetCombat()->FindNearestEnemy();if(!TestNotNull(TEXT("Cleanup target"),Main))return false;
  const float Before=Main->GetAttributes()->GetHealth();auto* S=E.Human->GetHeroSkill();
  if(!TestTrue(TEXT("Arrow launches before cleanup"),S->BeginAction(false)))return false;
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
  if(bKillSource)E.Human->Die();else E.GM->BattleRules->Stop();
  E.Advance(2.f);TestEqual(TEXT("Death or round stop cancels in-flight damage"),Main->GetAttributes()->GetHealth(),Before);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanEndBattle,"ElyGambit.ElysiaHerrscher.RoundStopCancelsPendingFinisher",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanEndBattle::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready())||!E.EnterOrigin(*this))return false;
 const float Before=E.Enemies[0]->GetAttributes()->GetHealth();
 E.GM->BattleRules->Stop();E.Advance(9.f);E.Human->GetHeroSkill()->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("No delayed finisher after round stop"),E.Enemies[0]->GetAttributes()->GetHealth(),Before);
 TestFalse(TEXT("Origin state cleared at round stop"),E.Policy()->IsOriginActive());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanFootprint,"ElyGambit.ElysiaHerrscher.FootprintEdgeAndAreaOnce",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanFootprint::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* Enemy=E.Enemies[0];const float Reach=Enemy->GetCombat()->GetEffectiveAttackRange(E.Human);
 const FVector Center=E.Human->GetActorLocation();
 Enemy->SetActorLocation(Center+FVector(Reach-.5f,0,0));
 TestTrue(TEXT("Enemy can attack from within single-cell reach"),Enemy->GetCombat()->IsInAttackRange(E.Human));
 Enemy->SetActorLocation(Center+FVector(Reach+1.f,0,0));
 TestFalse(TEXT("Enemy beyond single-cell reach cannot attack"),Enemy->GetCombat()->IsInAttackRange(E.Human));
 // Isolate the blocker: the ordinary enemies remain elsewhere in the fixture.
 TArray<TWeakObjectPtr<AEGChessUnit>> Blockers;Blockers.Add(E.Human);Enemy->GetCombat()->SetCombatants(Blockers);
 // Endpoints outside a blocker are not enough: the segment must be clear.
 TestFalse(TEXT("A movement segment cannot tunnel across the human"),Enemy->GetCombat()->IsBoardPathClear(Center+FVector(-1200,0,0),Center+FVector(1200,0,0)));
 TestTrue(TEXT("A movement segment outside clearance stays clear"),Enemy->GetCombat()->IsBoardPathClear(Center+FVector(-1200,1400,0),Center+FVector(1200,1400,0)));
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
 // The current combat rules mitigate damage proportionally rather than by
 // subtracting Defense. Make a single hit's health delta exactly 100 so the
 // original once-per-area assertion remains independent of that formula.
 E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDamageMultiplierAttribute(),1.f);
 TestTrue(TEXT("Area fixture has no defense mitigation"),FMath::IsNearlyZero(E.Human->GetAttributes()->GetDefense()));
 TestTrue(TEXT("Area source has unit damage multiplier"),FMath::IsNearlyEqual(Enemy->GetAttributes()->GetDamageMultiplier(),1.f));
 const float Before=E.Human->GetAttributes()->GetHealth(),Shield=E.Human->GetHeroSkill()->Shield;
 E.GM->BattleRules->Area(Enemy,Center,100.f,Shield+100.f);
 TestTrue(TEXT("One area event damages the human actor once"),FMath::IsNearlyEqual(Before-E.Human->GetAttributes()->GetHealth(),100.f,.1f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanPersistentManaEffects,"ElyGambit.ElysiaHerrscher.PersistentManaEffects",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanPersistentManaEffects::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* ASC=E.Human->GetAbilitySystemComponent();auto* S=E.Human->GetHeroSkill();S->Configure(E.GM->BattleRules,11,false);
 if(!E.EnterOrigin(*this))return false;
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100.f);
 const float Attack=E.Human->GetAttributes()->GetAttackPower();
 TStrongObjectPtr<UGameplayEffect> Mixed(NewObject<UGameplayEffect>());Mixed->DurationPolicy=EGameplayEffectDurationType::Infinite;
 auto AddMod=[](UGameplayEffect* Effect,FGameplayAttribute Attribute,EGameplayModOp::Type Op,float Amount)
 {FGameplayModifierInfo M;M.Attribute=Attribute;M.ModifierOp=Op;M.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(Amount));Effect->Modifiers.Add(M);};
 AddMod(Mixed.Get(),UEGAttributeSet::GetManaAttribute(),EGameplayModOp::AddBase,20.f);
 AddMod(Mixed.Get(),UEGAttributeSet::GetManaAttribute(),EGameplayModOp::AddBase,-5.f);
 AddMod(Mixed.Get(),UEGAttributeSet::GetAttackPowerAttribute(),EGameplayModOp::AddBase,-10.f);
 AddMod(Mixed.Get(),UEGAttributeSet::GetDefenseAttribute(),EGameplayModOp::AddBase,-10.f);
 FGameplayEffectSpec MixedSpec(Mixed.Get(),ASC->MakeEffectContext(),1.f);auto MixedHandle=ASC->ApplyGameplayEffectSpecToSelf(MixedSpec);
 TestTrue(TEXT("Mixed infinite GE remains active"),MixedHandle.IsValid());
 TestEqual(TEXT("Mixed effect mana is blocked"),E.Human->GetAttributes()->GetMana(),0.f);
 TestTrue(TEXT("Mixed effect attack debuff survives"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetAttackPower(),Attack-10.f));
 TestTrue(TEXT("Mixed effect defense debuff survives"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetDefense(),90.f));
 TStrongObjectPtr<UGameplayEffect> Timed(NewObject<UGameplayEffect>());Timed->DurationPolicy=EGameplayEffectDurationType::HasDuration;
 Timed->DurationMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(30.f));
 AddMod(Timed.Get(),UEGAttributeSet::GetManaAttribute(),EGameplayModOp::Override,20.f);
 FGameplayEffectSpec TimedSpec(Timed.Get(),ASC->MakeEffectContext(),1.f);auto TimedHandle=ASC->ApplyGameplayEffectSpecToSelf(TimedSpec);
 TestTrue(TEXT("Timed override GE remains active"),TimedHandle.IsValid());
 TestEqual(TEXT("Timed override is blocked"),E.Human->GetAttributes()->GetMana(),0.f);
 E.Advance(E.Policy()->GetPassiveState().RemainingSeconds+.1f);if(!TestTrue(TEXT("Finisher action starts"),S->IsCasting()))return false;
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 TestFalse(TEXT("Returns to bow"),E.Policy()->IsOriginActive());
 TestEqual(TEXT("No blocked mana is cached on return"),E.Human->GetAttributes()->GetMana(),0.f);
 TestTrue(TEXT("Mixed attack debuff still applies after return"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetAttackPower(),Attack-10.f));
 TestTrue(TEXT("Mixed defense debuff still applies after return"),FMath::IsNearlyEqual(E.Human->GetAttributes()->GetDefense(),90.f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),50.f);
 TestEqual(TEXT("Negative mana modifier in mixed effect remains effective"),E.Human->GetAttributes()->GetMana(),45.f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
 ASC->RemoveActiveGameplayEffect(MixedHandle);ASC->RemoveActiveGameplayEffect(TimedHandle);
 TestEqual(TEXT("Removal does not restore blocked mana"),E.Human->GetAttributes()->GetMana(),0.f);
 TStrongObjectPtr<UGameplayEffect> Fresh(NewObject<UGameplayEffect>());Fresh->DurationPolicy=EGameplayEffectDurationType::Infinite;
 AddMod(Fresh.Get(),UEGAttributeSet::GetManaAttribute(),EGameplayModOp::AddBase,20.f);
 FGameplayEffectSpec FreshSpec(Fresh.Get(),ASC->MakeEffectContext(),1.f);auto FreshHandle=ASC->ApplyGameplayEffectSpecToSelf(FreshSpec);
 TestTrue(TEXT("New post-return effect applies"),FreshHandle.IsValid());
 TestEqual(TEXT("New post-return mana is accepted"),E.Human->GetAttributes()->GetMana(),20.f);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanPreexistingManaEffect,"ElyGambit.ElysiaHerrscher.PreexistingManaEffect",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanPreexistingManaEffect::RunTest(const FString&)
{
 FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
 auto* ASC=E.Human->GetAbilitySystemComponent();auto* S=E.Human->GetHeroSkill();S->Configure(E.GM->BattleRules,11,false);
 TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo M;M.Attribute=UEGAttributeSet::GetManaAttribute();M.ModifierOp=EGameplayModOp::AddBase;
 M.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(20.f));Effect->Modifiers.Add(M);
 FGameplayEffectSpec Spec(Effect.Get(),ASC->MakeEffectContext(),1.f);const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);
 TestTrue(TEXT("Preexisting mana effect applies in bow"),Handle.IsValid());
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
 if(!TestTrue(TEXT("Entry starts with preexisting modifier"),S->BeginAction(true)))return false;
 TestEqual(TEXT("Entry invalidates preexisting positive modifier"),E.Human->GetAttributes()->GetMana(),0.f);
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 E.Advance(E.Policy()->GetPassiveState().RemainingSeconds+.1f);if(!TestTrue(TEXT("Finisher action starts"),S->IsCasting()))return false;
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 TestEqual(TEXT("Preexisting modifier cannot replay on return"),E.Human->GetAttributes()->GetMana(),0.f);
 ASC->RemoveActiveGameplayEffect(Handle);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanEntryRefund,"ElyGambit.ElysiaHerrscher.EntryCommitRefundAndDeath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanEntryRefund::RunTest(const FString&)
{
 auto Prepare=[this](FHumanWorld& E)->bool
 {
  if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
  E.Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
  return TestTrue(TEXT("Entry begins"),E.Human->GetHeroSkill()->BeginAction(true));
 };
 {
  FHumanWorld E(*this);if(!Prepare(E))return false;auto* S=E.Human->GetHeroSkill();
  S->AbsorbShield(E.Human->GetAttributes()->GetMaxHealth()*.1f);const float Shield=S->GetSourceShield(TEXT("ElysiaHerrscher.CrystalMirror"));
  S->CancelAction();
  TestEqual(TEXT("Cancel before Release refunds the uncommitted mana"),E.Human->GetAttributes()->GetMana(),100.f);
  TestEqual(TEXT("Cancel before Release does not refill shield"),S->GetSourceShield(TEXT("ElysiaHerrscher.CrystalMirror")),Shield);
 }
 {
  FHumanWorld E(*this);if(!Prepare(E))return false;auto* S=E.Human->GetHeroSkill();
  S->AbsorbShield(E.Human->GetAttributes()->GetMaxHealth()*.1f);S->AnimationEvent(TEXT("Release"));
  TestTrue(TEXT("Release refills the same shield"),FMath::IsNearlyEqual(S->GetSourceShield(TEXT("ElysiaHerrscher.CrystalMirror")),E.Human->GetAttributes()->GetMaxHealth()*.2f,.01f));
  S->CancelAction();TestEqual(TEXT("Committed entry cancellation does not refund"),E.Human->GetAttributes()->GetMana(),0.f);
 }
 {
  FHumanWorld E(*this);if(!Prepare(E))return false;E.Human->Die();
  TestEqual(TEXT("Direct death never refunds a pending entry"),E.Human->GetAttributes()->GetMana(),0.f);
 }
 {
  FHumanWorld E(*this);if(!Prepare(E))return false;E.GM->BattleRules->Stop();
  TestEqual(TEXT("Round stop never refunds a pending entry"),E.Human->GetAttributes()->GetMana(),0.f);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanDeathPresentation,"ElyGambit.ElysiaHerrscher.DeathPresentationSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanDeathPresentation::RunTest(const FString&)
{
 // Use the real unit/animation binding but private transient pairs, so this
 // verifies selection independently of the authored asset import order.
 auto Check=[this](int32 Stage,int32 Expected)->bool
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Human production fixture"),E.Ready()))return false;
  auto* U=E.Human;auto* Skill=U->GetHeroSkill();auto* Presentation=U->GetDefinition()->PresentationDefinition.Get();
  auto* Mesh=U->GetVisiblePresentationMesh();
  if(!TestNotNull(TEXT("Presentation definition"),Presentation)||!TestNotNull(TEXT("Visible body"),Mesh)||!TestNotNull(TEXT("Body mesh asset"),Mesh->GetSkeletalMeshAsset()))return false;
  auto* Clip0=U->GetDefinition()->GetDeathAnimation().LoadSynchronous();auto* Clip1=U->GetDefinition()->GetIdleAnimation().LoadSynchronous();
  if(!TestNotNull(TEXT("Default death clip"),Clip0)||!TestNotNull(TEXT("Alternate test clip"),Clip1))return false;
  TStrongObjectPtr<UEGSourcePreviewDefinition> Assembly0(NewObject<UEGSourcePreviewDefinition>()),Assembly1(NewObject<UEGSourcePreviewDefinition>());
  auto MakeAssembly=[Mesh](UEGSourcePreviewDefinition* Assembly,UAnimSequence* Clip)
  {
   auto& Motion=Assembly->Motions.AddDefaulted_GetRef();Motion.Duration=1.f;
   auto& Body=Motion.Parts.AddDefaulted_GetRef();Body.PartId=TEXT("Body");Body.Mesh=Mesh->GetSkeletalMeshAsset();Body.Animation=Clip;
  };
  MakeAssembly(Assembly0.Get(),Clip0);MakeAssembly(Assembly1.Get(),Clip1);
  TArray<FEGDeathPresentationVariant> Variants;Variants.SetNum(2);
  Variants[0].Animation=Clip0;Variants[0].Assembly=Assembly0.Get();Variants[1].Animation=Clip1;Variants[1].Assembly=Assembly1.Get();
  TGuardValue<TArray<FEGDeathPresentationVariant>> Restore(Presentation->DeathVariants,Variants);
  TestFalse(TEXT("New incarnation has no death snapshot"),Skill->HasDeathPresentationSnapshot());
  if(Stage>0)
  {
   U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100.f);
   if(!TestTrue(TEXT("Entry action starts"),Skill->BeginAction(true)))return false;
   if(Stage>=2)Skill->AnimationEvent(TEXT("Release"));
   if(Stage>=3)Skill->AnimationEvent(TEXT("Finish"));
  }
  U->Die();TestTrue(TEXT("Death stores a snapshot before cancellation"),Skill->HasDeathPresentationSnapshot());
  TestEqual(TEXT("Correct visible form persists after death cleanup"),Skill->GetDeathPresentationIndex(),Expected);
  UAnimSequence* Selected=nullptr;UEGSourcePreviewDefinition* SelectedAssembly=nullptr;
  Skill->ResolveDeathPresentation(Selected,SelectedAssembly);
  auto* ExpectedClip=Expected?Clip1:Clip0;auto* ExpectedAssembly=Expected?Assembly1.Get():Assembly0.Get();
  TestEqual(TEXT("Selected body and assembly are paired"),SelectedAssembly,ExpectedAssembly);
  TestEqual(TEXT("Selected body animation matches the form"),Selected,ExpectedClip);
  auto* Anim=Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance());
  if(!TestNotNull(TEXT("Body uses the combat AnimGraph"),Anim))return false;
  TestTrue(TEXT("Body starts actual death state"),FEGAnimationPlayback::Play(Mesh,EGPresentationAction::Death));
  TestEqual(TEXT("Body AnimGraph uses the selected death clip"),Anim->DeathClip.Get(),ExpectedClip);
  // A broken pair falls back together, not to a mismatched body/parts mix.
  Presentation->DeathVariants[Expected].Assembly.Reset();Skill->ResolveDeathPresentation(Selected,SelectedAssembly);
  TestEqual(TEXT("Incomplete pair falls back to default animation"),Selected,Clip0);
  TestEqual(TEXT("Incomplete pair falls back to legacy assembly"),SelectedAssembly,Presentation->DeathAssembly.LoadSynchronous());
  return true;
 };
 return Check(0,0)&&Check(1,0)&&Check(2,1)&&Check(3,1);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanReworkContract,"ElyGambit.ElysiaHerrscher.ReworkReleaseAndTiers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanReworkContract::RunTest(const FString&)
{
 for(int32 Level:{1,5,6,10,11})
 {
  FHumanWorld E(*this);if(!TestTrue(TEXT("Production fixture"),E.Ready()))return false;
  auto* S=E.Human->GetHeroSkill();S->Configure(E.GM->BattleRules,Level,false);
  auto* ASC=E.Human->GetAbilitySystemComponent();
  TestEqual(TEXT("Opening mana is twenty"),E.Human->GetAttributes()->GetMana(),20.f);
  if(!TestTrue(TEXT("Unreleased attack starts"),S->BeginAction(false)))return false;
  S->CancelAction();TestEqual(TEXT("Cancel before release gives no mana"),E.Human->GetAttributes()->GetMana(),20.f);
  TestEqual(TEXT("Cancel before release does not count"),E.Policy()->GetBowAttackCount(),0);
  E.Human->NotifyDamageReceived(1.f);TestEqual(TEXT("Being hit gives no intrinsic mana"),E.Human->GetAttributes()->GetMana(),20.f);
  for(int32 I=0;I<8;++I)
  {
   if(!TestTrue(TEXT("Bow arrow starts"),S->BeginAction(false)))return false;
   S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Hit"));
   TestEqual(TEXT("Release pays exactly once"),E.Human->GetAttributes()->GetMana(),30.f+10.f*I);
   TestEqual(TEXT("Released arrows drive fifth shot"),E.Policy()->GetBowAttackCount(),(I+1)%5);
   S->CancelAction();TestEqual(TEXT("Cancel after release keeps earned mana"),E.Human->GetAttributes()->GetMana(),30.f+10.f*I);
  }
  E.Advance(2.f);
  S->GrantSourceShield(TEXT("HumanRework.Ally"),123.f,30.f);
  if(!TestTrue(TEXT("First ultimate starts after eight arrows"),S->BeginAction(true)))return false;
  S->RestoreStigmataMana(50.f);TestEqual(TEXT("Entry blocks direct stigmata recovery"),E.Human->GetAttributes()->GetMana(),0.f);
  S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
  TestTrue(TEXT("Eight seconds start at finished entry"),FMath::IsNearlyEqual(E.Policy()->GetPassiveState().RemainingSeconds,8.f,.01f));
  TestEqual(TEXT("Entry preserves allied shield"),S->GetSourceShield(TEXT("HumanRework.Ally")),123.f);
  E.Human->RestoreMana(50.f);S->RestoreStigmataMana(50.f);
  if(auto* Stigmata=E.GM->BattleRules->GetStigmata())Stigmata->GrantStigmataMana(E.Human,TEXT("Gold"),50.f);
  TestEqual(TEXT("Origin rejects every mana entry point"),E.Human->GetAttributes()->GetMana(),0.f);
  auto* Main=E.Human->GetCombat()->FindNearestEnemy();
  const float HP=Main->GetAttributes()->GetHealth(),Attack=E.Human->GetAttributes()->GetAttackPower();
  if(!TestTrue(TEXT("Scepter starts"),S->BeginAction(false)))return false;
  const float Spacing=OriginContactSpacing(E.Human);
  S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));E.Advance(Spacing*2.f+.04f);
  TestTrue(TEXT("Level six raises total scepter damage"),FMath::IsNearlyEqual(HP-Main->GetAttributes()->GetHealth(),Attack*(Level>=6?6.75f:4.5f),.1f));
  const auto View=FEGUnitInspectionView::Capture(E.Human,nullptr,FGuid());
  TestTrue(TEXT("Inspection follows Origin without a new definition"),View.bHasSkillOverride&&View.SkillBasicOverride.ToString().Contains(Level>=6?TEXT("675%"):TEXT("450%")));
  E.Advance(7.5f);
  if(!TestTrue(TEXT("Final unfinished attack starts"),S->BeginAction(false)))return false;
  const float BeforeExpiry=Main->GetAttributes()->GetHealth();
  E.Advance(.25f);
  TestEqual(TEXT("Expiry cancels an unreleased scepter"),Main->GetAttributes()->GetHealth(),BeforeExpiry);
  if(Level>=11)
  {
   if(!TestTrue(TEXT("Level eleven starts automatic finisher"),S->IsCasting()))return false;
   S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Release"));
   TestTrue(TEXT("Finisher deals exactly seven hundred percent once"),FMath::IsNearlyEqual(BeforeExpiry-Main->GetAttributes()->GetHealth(),Attack*7.f,.1f));
   S->AnimationEvent(TEXT("Finish"));
  }
  else TestFalse(TEXT("Earlier tiers have no finisher action"),S->IsActionRunning());
  TestFalse(TEXT("Origin returns to bow"),E.Policy()->IsOriginActive());
  TestEqual(TEXT("No deferred mana is returned"),E.Human->GetAttributes()->GetMana(),0.f);
  for(int32 I=0;I<10;++I)
  {
   if(!TestTrue(TEXT("Next cycle arrow starts"),S->BeginAction(false)))return false;
   S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
   TestEqual(TEXT("Later activation requires ten arrows"),E.Human->GetAttributes()->GetMana(),10.f*(I+1));
  }
 }
 return true;
}

#endif
