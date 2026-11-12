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
#include "Data/EGElysiaSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGSkillNotify.h"
#include "Presentation/EGElysiaCombatVisual.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGSourceEffect.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
namespace
{
struct FElysiaWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGChessUnit* Hero=nullptr;TArray<AEGChessUnit*> Enemies;
 TWeakObjectPtr<UEGElysiaSkillDefinition> ScopedRules;bool PreviousRainMode=false;
 FElysiaWorld(FAutomationTestBase& Test,bool bRainMode=false):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  GM->RunDefinition=EGProductionTestAssets::ResolveRun(Test);
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  UEGCardDefinition* Card=GM->RunDefinition?GM->RunDefinition->StartingCard.Get():nullptr;
  if((!Card||!Card->HeroDefinition||Card->HeroDefinition->HeroKind!=EEGHeroKind::Elysia)&&GM->RunDefinition)
  {Card=nullptr;for(const auto& Candidate:GM->RunDefinition->CardPool)if(Candidate&&Candidate->HeroDefinition&&Candidate->HeroDefinition->HeroKind==EEGHeroKind::Elysia){Card=Candidate.Get();break;}}
  if(!Card)Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia.DA_Card_Elysia"));
  if(Card&&Card->HeroDefinition)if(auto* Rules=Cast<UEGElysiaSkillDefinition>(Card->HeroDefinition->SkillDefinition))
  {ScopedRules=Rules;PreviousRainMode=Rules->bArrowRainUltimate;Rules->bArrowRainUltimate=bRainMode;}
  if(Card){Cards->GrantCard(Card);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2});}
  GM->EnterCombat();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
   if(U->GetDefinition()->HeroKind==EEGHeroKind::Elysia)Hero=U;else if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemies.Add(U);
  }
  if(Hero)for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Hero->GetActorLocation()+FVector(500+I*180,0,0));
 }
 ~FElysiaWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);if(ScopedRules.IsValid())ScopedRules->bArrowRainUltimate=PreviousRainMode;}
 UEGElysiaSkillBehavior* Policy()const{return Hero?Cast<UEGElysiaSkillBehavior>(Hero->GetHeroSkill()->GetBehavior()):nullptr;}
 bool Ready()const{return Hero&&Policy()&&!Enemies.IsEmpty();}
 void Advance(float Seconds){while(Seconds>0.f){const float Step=FMath::Min(Seconds,.05f);++GFrameCounter;W->Tick(LEVELTICK_All,Step);Seconds-=Step;}}
 FEGSkillActionContext Context(int32 Index=0,bool Ultimate=false)const
 {FEGSkillActionContext C;C.RoundId=GM->BattleRules->GetRoundId();C.ActionId=FGuid::NewGuid();C.Target=Enemies[0];C.Attack=100;C.BasicAttackIndex=Index;C.bUltimate=Ultimate;return C;}
};
// Set up the field through the existing behavior events. Clear cast protection
// as FinishAction normally does when a real montage completes.
void StartRicochetField(FElysiaWorld& E)
{
 auto* S=E.Policy();auto Cast=E.Context(0,true);Cast.Multiplier=1.5f;
 S->ActionStarted(Cast);S->ActionEvent(TEXT("Release"),Cast);S->ActionEnded(true);
 E.Hero->GetHeroSkill()->SetActionProtected(false);
}
bool FireControlledChargedArrow(FElysiaWorld& E)
{
 auto* S=E.Policy();const auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!S||!D)return false;
 auto Shot=E.Context(4);S->ActionStarted(Shot);S->ActionEvent(TEXT("Hit"),Shot);S->ActionEnded(true);
 E.Advance(2.f*D->ArrowFlightSeconds+.12f);return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaPassiveSnapshot,"ElyGambit.Elysia.PassiveSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaPassiveSnapshot::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Production Elysia fixture"),E.Ready()))return false;
 E.GM->BattleRules->CompleteOpeningForTests();
 auto* S=E.Policy();auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Skill rules"),D))return false;
 TGuardValue<int32> Interval(D->ChargedAttackInterval,3);
 TGuardValue<float> Duration(D->EmpoweredSeconds,1.f);
 TGuardValue<float> Radius(D->EmpoweredRadius,0.f);
 const auto* Board=E.GM->GetBoard()?E.GM->GetBoard()->LayoutSource.Get():nullptr;
 if(!TestNotNull(TEXT("Combat board layout"),Board))return false;
 const float ExpectedRadius=2.f*FMath::Max(Board->CellWidth*Board->GetActorScale3D().X,Board->CellDepth*Board->GetActorScale3D().Y);
 TestTrue(TEXT("Field radius exposes the same two-cell fallback as combat"),FMath::IsNearlyEqual(S->GetEmpoweredRadius(),ExpectedRadius));
 auto State=S->GetPassiveState();
 TestTrue(TEXT("Initial passive is ordinary"),State.Phase==EEGElysiaPassivePhase::Normal);
 TestEqual(TEXT("Configured cycle is reported"),State.Threshold,3);
 TestEqual(TEXT("No attacks completed yet"),State.Progress,0);
 TestTrue(TEXT("Snapshot has current round"),State.RoundId==E.GM->BattleRules->GetRoundId());
 auto Shot=E.Context();S->ActionStarted(Shot);
 TestTrue(TEXT("Snapshot action identifies the active attack"),S->GetPassiveState().ActionId==Shot.ActionId);
 S->ActionEnded(false);TestEqual(TEXT("Cancellation does not advance passive"),S->GetPassiveState().Progress,0);
 for(int32 Index=0;Index<2;++Index){Shot=E.Context(Index);S->ActionStarted(Shot);S->ActionEnded(true);}
 State=S->GetPassiveState();TestEqual(TEXT("Two completed attacks are counted"),State.Progress,2);
 TestTrue(TEXT("Next attack is charged"),State.bNextAttackEnhanced);
 StartRicochetField(E);State=S->GetPassiveState();
 TestTrue(TEXT("Completed ultimate opens the empowered state"),State.Phase==EEGElysiaPassivePhase::Empowered&&State.bNextAttackEnhanced);
 TestTrue(TEXT("Duration comes from the actual combat clock"),State.RemainingSeconds>.9f&&State.RemainingSeconds<=1.f);
 E.Advance(.3f);State=S->GetPassiveState();
 TestTrue(TEXT("Remaining time decreases"),State.RemainingSeconds>0.f&&State.RemainingSeconds<.8f);
 E.Advance(.8f);State=S->GetPassiveState();
 TestTrue(TEXT("Natural expiry returns to normal"),State.Phase==EEGElysiaPassivePhase::Normal);
 TestEqual(TEXT("Expiry resets normal cycle"),State.Progress,0);
 S->EndBattle();State=S->GetPassiveState();
 TestTrue(TEXT("Stopped behavior has no live passive"),State.Phase==EEGElysiaPassivePhase::Inactive&&!State.RoundId.IsValid()&&!State.ActionId.IsValid());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaBoardMotion,"ElyGambit.Elysia.BoardMotionProgressAndCancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaBoardMotion::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Formal board-motion card deploys"),E.Ready()))return false;
 auto* Skill=E.Hero->GetHeroSkill();auto* S=E.Policy();S->NormalCursor=1;
 if(!TestTrue(TEXT("Back-jump begins through existing action transaction"),Skill->BeginAction(false)))return false;
 const FVector Start=E.Hero->GetActorLocation();
 Skill->AnimationEvent(TEXT("Hit"));const int32 Hits=S->GetPendingHitCount();
 Skill->AnimationEvent(TEXT("BoardBackstep"));
 S->TickAction(.2f);
 const FVector Moved=E.Hero->GetActorLocation();
 TestTrue(TEXT("Actual unit retreats a short distance away from target"),Moved.X<Start.X-1.f&&FVector::Dist2D(Start,Moved)<=150.1f);
 TestEqual(TEXT("Movement adds no pending damage"),S->GetPendingHitCount(),Hits);
 TestFalse(TEXT("Backstep grants no immunity"),Skill->IsActionProtected());
 Skill->CancelAction();S->TickAction(2.f);
 TestTrue(TEXT("Interrupted movement cannot continue or snap to endpoint"),E.Hero->GetActorLocation().Equals(Moved,.1f));
 TestEqual(TEXT("Interrupted primary flight is cleared"),S->GetPendingHitCount(),0);
 S->NormalCursor=0;
 if(TestTrue(TEXT("Ordinary attack begins"),Skill->BeginAction(false)))
 {
  Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("BoardBackstep"));S->TickAction(1.f);
  TestTrue(TEXT("Other attack stages cannot move even with an incorrect backstep event"),E.Hero->GetActorLocation().Equals(Moved,.1f));Skill->CancelAction();
 }
 S->EndBattle();S->TickAction(3.f);
 TestTrue(TEXT("Round cleanup cannot resume an old retreat"),E.Hero->GetActorLocation().Equals(Moved,.1f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaCycle,"ElyGambit.Elysia.FiveShotCycleAndDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaCycle::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Production Elysia card deploys"),E.Ready()))return false;
 auto* S=E.Policy();auto* T=E.Enemies[0];auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Elysia skill"),D))return false;
 TestEqual(TEXT("Configured charge multiplier"),D->ChargedMultiplier,1.8f);
 for(int32 I=0;I<5;++I)
 {
  TestEqual(TEXT("Expected sequential attack index"),S->PresentationIndex(false,0),I);
  auto C=E.Context(I);const float Before=T->GetAttributes()->GetHealth();S->ActionStarted(C);
  S->ActionEvent(TEXT("Hit"),C);S->ActionEvent(TEXT("Hit"),C);
  TestEqual(TEXT("One primary arrow is queued despite duplicate notify"),S->GetPendingHitCount(),1);
  const double TimeBefore=E.W->GetTimeSeconds();E.Advance(D->ArrowFlightSeconds+.01f);
  TestTrue(TEXT("World clock advances past arrow flight"),E.W->GetTimeSeconds()-TimeBefore>=D->ArrowFlightSeconds);
  TestEqual(TEXT("Primary arrow is resolved after simulated frames"),S->GetPendingHitCount(),0);
  TestTrue(TEXT("One normal or charged hit has the configured damage"),FMath::IsNearlyEqual(Before-T->GetAttributes()->GetHealth(),I==4?180.f:100.f,.1f));
  S->ActionEnded(true);
 }
 TestEqual(TEXT("Cycle wraps to first attack"),S->PresentationIndex(false,0),0);
 TestEqual(TEXT("Duplicate notifies did not produce extra primaries"),S->PrimaryHitCount,5);
 TestEqual(TEXT("Exactly one charged hit"),S->ChargedHitCount,1);
 TestEqual(TEXT("No legacy seed stacks are created"),S->GetSeedStacks(T),0);
 if(D->bBasicOnlyPresentation)
 {
  int32 Visuals=0;
  for(TActorIterator<AEGElysiaCombatVisual> I(E.W);I;++I)if(I->GetOwner()==E.Hero)++Visuals;
  TestEqual(TEXT("Basic-only attacks do not spawn retired Elysia visuals"),Visuals,0);
 }
 auto Interrupted=E.Context();S->ActionStarted(Interrupted);S->ActionEnded(false);
 TestEqual(TEXT("Interrupted attack does not advance"),S->PresentationIndex(false,0),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaConfiguredCycle,"ElyGambit.Elysia.ConfiguredAttackCycleAndDescription",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaConfiguredCycle::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Production Elysia fixture"),E.Ready()))return false;
 auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);if(!TestNotNull(TEXT("Skill rules"),D))return false;
 TGuardValue<int32> Interval(D->ChargedAttackInterval,3);
 TGuardValue<float> Normal(D->NormalAttackMultiplier,.75f);
 auto* S=E.Policy();auto* T=E.Enemies[0];S->NormalCursor=0;
 for(int32 I=0;I<3;++I)
 {
  const int32 Index=S->PresentationIndex(false,0);
  TestEqual(TEXT("Configured cycle chooses ordinary or final charged presentation"),Index,I==2?4:I);
  auto C=E.Context(Index);const float Before=T->GetAttributes()->GetHealth();
  S->ActionStarted(C);S->ActionEvent(TEXT("Hit"),C);E.Advance(D->ArrowFlightSeconds+.01f);S->ActionEnded(true);
  TestTrue(TEXT("Configured ordinary multiplier and charged replacement drive damage"),
   FMath::IsNearlyEqual(Before-T->GetAttributes()->GetHealth(),I==2?180.f:75.f,.1f));
 }
 TestEqual(TEXT("Configured interval wraps"),S->PresentationIndex(false,0),0);
 const FString Text=E.Hero->GetDefinition()->GetPlayerFacingSkillEffect().ToString();
 TestTrue(TEXT("Description displays the live normal multiplier"),Text.Contains(TEXT("75%")));
 TestTrue(TEXT("Description displays the live charged interval"),Text.Contains(TEXT("每第3次普攻")));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaAuthoredAttackRate,"ElyGambit.Elysia.AuthoredAttackRateAndHandoff",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaAuthoredAttackRate::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Elysia ready"),E.Ready()))return false;
 auto* Skill=E.Hero->GetHeroSkill();auto* S=E.Policy();auto* ASC=E.Hero->GetAbilitySystemComponent();
 const auto* P=E.Hero->GetDefinition()->PresentationDefinition.Get();
 auto* Mesh=E.Hero->GetVisiblePresentationMesh();auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
 if(!TestNotNull(TEXT("Presentation definition"),P)||!TestNotNull(TEXT("Combat anim instance"),Instance)||!TestEqual(TEXT("Five authored attacks"),P->AttackSequence.Num(),5))return false;
 const auto CheckAction=[&](int32 ExpectedIndex,float Speed)
 {
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Speed);
  if(!TestTrue(TEXT("Attack starts"),Skill->BeginAction(false)))return false;
  auto* Montage=Skill->GetActiveMontage();
  TestTrue(TEXT("Expected authored attack selected"),Montage==P->AttackSequence[ExpectedIndex].Montage.LoadSynchronous());
  if(TestNotNull(TEXT("Active attack montage"),Montage))
  {
   auto* LiveMesh=E.Hero->GetVisiblePresentationMesh();auto* LiveInstance=LiveMesh?LiveMesh->GetAnimInstance():nullptr;
   if(TestNotNull(TEXT("Action anim instance"),LiveInstance))
    TestTrue(TEXT("Montage rate equals current GAS attack speed"),FMath::IsNearlyEqual(LiveInstance->Montage_GetPlayRate(Montage),Speed,.001f));
   TestTrue(TEXT("Authored montage has positive duration"),Montage->GetPlayLength()>0.f);
  }
  Skill->AnimationEvent(TEXT("Finish"));
  TestFalse(TEXT("Finish releases the action"),Skill->IsBusy());
  return true;
 };
 S->NormalCursor=0;
 if(!CheckAction(0,1.f)||!CheckAction(1,1.5f))return false;
 TestEqual(TEXT("Finished ordinary actions advance once"),S->NormalCursor,2);
 S->NormalCursor=4;
 if(!CheckAction(4,1.5f))return false;
 TestEqual(TEXT("Charged action wraps normal cycle"),S->NormalCursor,0);
 auto Ultimate=E.Context(0,true);Ultimate.Multiplier=1.5f;
 S->ActionStarted(Ultimate);S->ActionEvent(TEXT("Release"),Ultimate);S->ActionEnded(true);
 TestEqual(TEXT("First empowered action is charged"),S->PresentationIndex(false,0),4);
 if(!CheckAction(4,1.f)||!CheckAction(4,1.5f))return false;
 TestEqual(TEXT("Every empowered attack is charged"),S->PresentationIndex(false,0),4);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaUltimate,"ElyGambit.Elysia.DomeWithoutOpeningExplosion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaUltimate::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Production Elysia card deploys"),E.Ready()))return false;
 auto* S=E.Policy();auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Elysia skill"),D))return false;
 TestTrue(TEXT("Dome radius is configured or uses two-cell fallback"),D->EmpoweredRadius>=0.f);
 auto* T=E.Enemies[0];const FVector Locked=T->GetActorLocation();
 auto C=E.Context(0,true);C.Multiplier=2.5f;S->ActionStarted(C);
 T->SetActorLocation(Locked+FVector(100,0,0));const float Before=T->GetAttributes()->GetHealth();
 S->ActionEvent(TEXT("Release"),C);S->ActionEvent(TEXT("Release"),C);
 TestEqual(TEXT("Dome release causes no opening explosion"),T->GetAttributes()->GetHealth(),Before);
 S->ActionEnded(true);E.Advance(6.01f);
 T->SetActorLocation(Locked);C=E.Context(0,true);C.Multiplier=2.5f;S->ActionStarted(C);
 T->SetActorLocation(Locked+FVector(600,0,0));const float OutsideBefore=T->GetAttributes()->GetHealth();
 S->ActionEvent(TEXT("Release"),C);
 TestEqual(TEXT("Moving target outside locked area is not hit"),T->GetAttributes()->GetHealth(),OutsideBefore);
 S->ActionEnded(true);TestEqual(TEXT("Empowered sequence starts charged"),S->PresentationIndex(false,0),4);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaSeedAndEmpowerment,"ElyGambit.Elysia.ChargedDamageAndEmpowerment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaSeedAndEmpowerment::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Elysia fixture"),E.Ready()))return false;
 auto* S=E.Policy();auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);auto* T=E.Enemies[0];
 if(!TestNotNull(TEXT("Skill data"),D))return false;
 auto C=E.Context(4);float Before=T->GetAttributes()->GetHealth();S->ActionStarted(C);S->ActionEvent(TEXT("Hit"),C);
 TestEqual(TEXT("Charged primary is queued"),S->GetPendingHitCount(),1);
 E.Advance(D->ArrowFlightSeconds+.01f);
 TestEqual(TEXT("Charged primary resolves"),S->GetPendingHitCount(),0);S->ActionEnded(true);
 TestEqual(TEXT("Charged hit does not plant legacy seeds"),S->GetSeedStacks(T),0);
 TestTrue(TEXT("Charged hit deals 180 percent"),FMath::IsNearlyEqual(Before-T->GetAttributes()->GetHealth(),180.f,.1f));
 auto Ultimate=E.Context(0,true);Ultimate.Multiplier=1.5f;S->ActionStarted(Ultimate);S->ActionEvent(TEXT("Release"),Ultimate);
 TestEqual(TEXT("No empowerment while casting"),S->DomeRemaining,0.f);S->ActionEnded(true);
 TestTrue(TEXT("Full six seconds after completion"),FMath::IsNearlyEqual(S->DomeRemaining,6.f));
 TestFalse(TEXT("Cannot recast during empowerment"),S->AllowsAction(true));
 TestEqual(TEXT("First empowered attack charged"),S->PresentationIndex(false,0),4);
 C=E.Context(4);S->ActionStarted(C);S->ActionEvent(TEXT("Hit"),C);S->ActionEnded(true);
 TestEqual(TEXT("Next empowered attack remains charged"),S->PresentationIndex(false,0),4);
 E.Advance(6.01f);
 TestEqual(TEXT("Normal sequence resets after expiry"),S->PresentationIndex(false,0),0);
 TestTrue(TEXT("Ultimate allowed after expiry"),S->AllowsAction(true));
 TestEqual(TEXT("Flying charged shot retains type after expiry"),S->ChargedHitCount,2);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRicochetLethal,"ElyGambit.Elysia.RicochetAfterLethalPrimary",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRicochetLethal::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("At least two targets"),E.Ready()&&E.Enemies.Num()>=2))return false;
 auto* S=E.Policy();auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* First=E.Enemies[0];auto* Second=E.Enemies[1];
 Second->SetActorLocation(First->GetActorLocation()+FVector(100,0,0));
 auto Cast=E.Context(0,true);Cast.Multiplier=1.5f;S->ActionStarted(Cast);S->ActionEvent(TEXT("Release"),Cast);S->ActionEnded(true);
 First->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1.f);
 const float Before=Second->GetAttributes()->GetHealth();
 auto Shot=E.Context(4);Shot.Tier=0;S->ActionStarted(Shot);S->ActionEvent(TEXT("Hit"),Shot);S->ActionEvent(TEXT("Hit"),Shot);
 TestEqual(TEXT("Lethal primary is queued once"),S->GetPendingHitCount(),1);
 E.Advance(D->ArrowFlightSeconds+.01f);
 TestEqual(TEXT("Lethal primary queues one ricochet"),S->GetPendingHitCount(),1);
 S->ActionEnded(true);E.Advance(D->ArrowFlightSeconds+.01f);
 TestEqual(TEXT("Ricochet resolves after its flight"),S->GetPendingHitCount(),0);
 TestFalse(TEXT("Primary target died"),IEGCombatant::Execute_IsAlive(First));
 TestTrue(TEXT("One 60 percent ricochet after death"),FMath::IsNearlyEqual(Before-Second->GetAttributes()->GetHealth(),60.f,.1f));
 TestEqual(TEXT("Ricochet does not plant seeds"),S->GetSeedStacks(Second),0);
 TestEqual(TEXT("Only one extra hit"),S->RicochetHitCount,1);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRicochetTiers,"ElyGambit.Elysia.RicochetNearestAndConstantDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRicochetTiers::RunTest(const FString&)
{
 for(int32 Level:{1,6,11})
 {
  FElysiaWorld E(*this);if(!TestTrue(TEXT("Three-target fixture"),E.Ready()&&E.Enemies.Num()>=3))return false;
  auto* Main=E.Enemies[0];auto* Near=E.Enemies[1];auto* Far=E.Enemies[2];
  const FVector Center=Main->GetActorLocation();Near->SetActorLocation(Center+FVector(120,0,0));Far->SetActorLocation(Center+FVector(300,0,0));
  StartRicochetField(E);
  auto* Skill=E.Hero->GetHeroSkill();auto* ASC=E.Hero->GetAbilitySystemComponent();
  Skill->ApplyPersistentLevel(Level);ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
  E.Hero->GetCombat()->ResetCombat();
  if(!TestTrue(TEXT("Primary is the nearest attack target"),E.Hero->GetCombat()->FindNearestEnemy()==Main))return false;
  const float MainBefore=Main->GetAttributes()->GetHealth(),NearBefore=Near->GetAttributes()->GetHealth(),FarBefore=Far->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Levelled empowered attack starts"),Skill->BeginAction(false)))return false;
  Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));
  const auto* D=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);if(!TestNotNull(TEXT("Skill data"),D))return false;
  E.Advance(2.f*D->ArrowFlightSeconds+.12f);
  const float Expected=60.f;
  TestTrue(TEXT("Primary receives one 180 percent charged hit"),FMath::IsNearlyEqual(MainBefore-Main->GetAttributes()->GetHealth(),180.f,.1f));
  TestTrue(FString::Printf(TEXT("Level %d ricochet damages nearest enemy"),Level),FMath::IsNearlyEqual(NearBefore-Near->GetAttributes()->GetHealth(),Expected,.1f));
  TestEqual(TEXT("Farther eligible enemy receives no ricochet"),Far->GetAttributes()->GetHealth(),FarBefore);
  TestTrue(TEXT("One base attack mana reward"),FMath::IsNearlyEqual(ASC->GetNumericAttribute(UEGAttributeSet::GetManaAttribute()),14.f,.1f));
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRicochetSingle,"ElyGambit.Elysia.RicochetSingleLivingEnemy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRicochetSingle::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Three-target fixture"),E.Ready()&&E.Enemies.Num()>=3))return false;
 auto* Main=E.Enemies[0];StartRicochetField(E);
 for(int32 I=1;I<E.Enemies.Num();++I)
 {
  E.GM->BattleRules->Damage(E.Hero,E.Enemies[I],200000.f,FGuid::NewGuid());
  TestFalse(TEXT("Other enemy is dead"),IEGCombatant::Execute_IsAlive(E.Enemies[I]));
 }
 const float Before=Main->GetAttributes()->GetHealth();if(!TestTrue(TEXT("Charged shot fixture"),FireControlledChargedArrow(E)))return false;
 TestTrue(TEXT("Only the 180 percent primary is applied"),FMath::IsNearlyEqual(Before-Main->GetAttributes()->GetHealth(),180.f,.1f));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRicochetEligibility,"ElyGambit.Elysia.RicochetCircleAndTargetEligibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRicochetEligibility::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Three-target fixture"),E.Ready()&&E.Enemies.Num()>=3))return false;
 auto* Main=E.Enemies[0];auto* Near=E.Enemies[1];auto* Far=E.Enemies[2];const FVector Center=Main->GetActorLocation();
 StartRicochetField(E);
 // A closer untargetable enemy must not intercept the shot intended for a valid one.
 Near->SetActorLocation(Center+FVector(100,0,0));Far->SetActorLocation(Center+FVector(300,0,0));Near->GetHeroSkill()->SetActionProtected(true);
 float NearBefore=Near->GetAttributes()->GetHealth(),FarBefore=Far->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Charged shot fixture"),FireControlledChargedArrow(E)))return false;
 TestEqual(TEXT("Untargetable closer enemy takes no damage"),Near->GetAttributes()->GetHealth(),NearBefore);
 TestTrue(TEXT("Next closest targetable enemy is hit"),FMath::IsNearlyEqual(FarBefore-Far->GetAttributes()->GetHealth(),60.f,.1f));
 // Put the primary at the circle edge; an outside enemy is closer than any eligible target.
 Main->SetActorLocation(Center-FVector(490,0,0));Near->SetActorLocation(Center-FVector(510,0,0));Near->GetHeroSkill()->SetActionProtected(false);Far->GetHeroSkill()->SetActionProtected(true);
 NearBefore=Near->GetAttributes()->GetHealth();FarBefore=Far->GetAttributes()->GetHealth();const float MainBefore=Main->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Charged shot fixture"),FireControlledChargedArrow(E)))return false;
 TestTrue(TEXT("Inside primary still receives charged damage"),Main->GetAttributes()->GetHealth()<MainBefore);
 TestEqual(TEXT("Closer outside enemy receives no ricochet"),Near->GetAttributes()->GetHealth(),NearBefore);
 TestEqual(TEXT("Untargetable inside enemy receives no ricochet"),Far->GetAttributes()->GetHealth(),FarBefore);
 // When the primary itself is outside, no bounce is allowed even to an inside enemy.
 Main->SetActorLocation(Center-FVector(510,0,0));Near->SetActorLocation(Center-FVector(400,0,0));
 NearBefore=Near->GetAttributes()->GetHealth();const float OutsideBefore=Main->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Charged shot fixture"),FireControlledChargedArrow(E)))return false;
 TestTrue(TEXT("Outside primary still receives charged damage"),Main->GetAttributes()->GetHealth()<OutsideBefore);
 TestEqual(TEXT("Outside primary cannot ricochet inside"),Near->GetAttributes()->GetHealth(),NearBefore);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaLifetimes,"ElyGambit.Elysia.CancelAndRoundStopClearPendingDamage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaLifetimes::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Elysia ready"),E.Ready()))return false;auto* S=E.Policy();auto C=E.Context();S->ActionStarted(C);S->ActionEvent(TEXT("Hit"),C);TestEqual(TEXT("Arrow waits for flight"),S->GetPendingHitCount(),1);S->ActionEnded(false);E.Advance(1);TestEqual(TEXT("Cancelled arrow cannot hit"),S->PrimaryHitCount,0);
 C=E.Context();C.RoundId=FGuid::NewGuid();S->ActionEvent(TEXT("Hit"),C);TestEqual(TEXT("Old generation rejected"),S->GetPendingHitCount(),0);
 C=E.Context();S->ActionStarted(C);S->ActionEvent(TEXT("Hit"),C);E.GM->BattleRules->Stop();E.Advance(1);TestEqual(TEXT("Stopped round drops pending hit"),S->GetPendingHitCount(),0);TestEqual(TEXT("No late damage"),S->PrimaryHitCount,0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaTarget,"ElyGambit.Elysia.UltimateTargetEligibilityAndLockedPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaTarget::RunTest(const FString&)
{
 FElysiaWorld E(*this);if(!TestTrue(TEXT("Elysia fixture"),E.Ready()))return false;
 auto* Skill=E.Hero->GetHeroSkill();auto* ASC=E.Hero->GetAbilitySystemComponent();
 for(auto* U:E.GM->BattleRules->Units())if(IsValid(U)&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)U->GetHeroSkill()->SetActionProtected(true);
 const float Full=E.Hero->GetAttributes()->GetMaxMana();ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Full);
 TestFalse(TEXT("No targetable enemy rejects cast"),Skill->BeginAction(true));
 TestEqual(TEXT("Rejected cast keeps full mana"),E.Hero->GetAttributes()->GetMana(),Full);
 auto* Target=E.Enemies[0];Target->GetHeroSkill()->SetActionProtected(false);const FVector Locked=Target->GetActorLocation();
 const auto* Definition=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 const float Before=Target->GetAttributes()->GetHealth();
 if(Definition&&E.Hero->GetDefinition()->GetSkillAnimation().IsNull())
 {
  const int32 Actions=Skill->ActionCount,Commits=Skill->CommitCount;
  TestFalse(TEXT("Unauthored ultimate is rejected"),Skill->BeginAction(true));
  TestFalse(TEXT("Rejected ultimate does not leave a busy action"),Skill->IsBusy());
  TestEqual(TEXT("Rejected ultimate keeps mana"),E.Hero->GetAttributes()->GetMana(),Full);
  TestEqual(TEXT("Rejected ultimate does not start an action"),Skill->ActionCount,Actions);
  TestEqual(TEXT("Rejected ultimate does not commit an event"),Skill->CommitCount,Commits);
  Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));E.Advance(.2f);
  TestEqual(TEXT("Rejected ultimate and stale events cause no damage"),Target->GetAttributes()->GetHealth(),Before);
  TestEqual(TEXT("Rejected ultimate does not empower"),E.Policy()->DomeRemaining,0.f);
  int32 Visuals=0;
  for(TActorIterator<AEGElysiaCombatVisual> I(E.W);I;++I)if(I->GetOwner()==E.Hero)++Visuals;
  TestEqual(TEXT("Rejected ultimate spawns no Elysia visuals"),Visuals,0);
 }
 else
 {
  if(!TestTrue(TEXT("Valid target permits ultimate"),Skill->BeginAction(true)))return false;
  const FVector Ground=Locked-FVector(0,0,88);Target->SetActorLocation(Locked+FVector(300,0,0));
  TestTrue(TEXT("Presentation ground remains at start point"),Skill->GetPresentationTargetGround().Equals(Ground,.1f));
  Skill->CancelAction();
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaResources,"ElyGambit.Elysia.ProductionConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaResources::RunTest(const FString&)
{
 TStrongObjectPtr<UEGRunDefinition> Run(EGProductionTestAssets::ResolveRun(*this));
 if(!TestNotNull(TEXT("Production run"),Run.Get()))return false;
 UEGHeroDefinition* Hero=Run->StartingCard?Run->StartingCard->HeroDefinition.Get():nullptr;
 if(!Hero)for(const auto& Card:Run->CardPool)if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==EEGHeroKind::Elysia){Hero=Card->HeroDefinition;break;}
 if(!TestNotNull(TEXT("Production Elysia"),Hero))return false;
 const auto* D=Cast<UEGElysiaSkillDefinition>(Hero->SkillDefinition);
 if(!TestNotNull(TEXT("Dedicated skill data"),D)||!TestNotNull(TEXT("Presentation"),Hero->PresentationDefinition.Get()))return false;
 FString Error;TestTrue(TEXT("Formal graph remains valid"),Run->ValidateDefinitionGraph(Error,true));AddInfo(Error);
 TestEqual(TEXT("Exactly five attack presentations"),Hero->PresentationDefinition->AttackSequence.Num(),5);
 TestTrue(TEXT("Runtime AnimGraph retained"),Hero->PresentationDefinition->bUseAnimationBlueprintVisuals);
 TestEqual(TEXT("Dome ricochet is sixty percent at every level"),D->DomeRicochetMultiplier,.6f);
 TestEqual(TEXT("Charged multiplier configured"),D->ChargedMultiplier,1.8f);
 TestTrue(TEXT("Dome radius is configured or uses two-cell fallback"),D->EmpoweredRadius>=0.f);
 for(int32 I=0;I<FMath::Min(5,Hero->PresentationDefinition->AttackSequence.Num());++I)
 {
  const auto* M=Hero->PresentationDefinition->AttackSequence[I].Montage.LoadSynchronous();bool Hit=false,Finish=false;
  float HitTime=-1.f,BackstepTime=-1.f,FinishTime=-1.f;
  if(M)for(const auto& N:M->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify))
  {if(Notify->Event==TEXT("Hit")){Hit=true;HitTime=N.GetTime();}if(Notify->Event==TEXT("BoardBackstep"))BackstepTime=N.GetTime();if(Notify->Event==TEXT("Finish")){Finish=N.GetTime()<M->GetPlayLength()-.12f;FinishTime=N.GetTime();}}
  TestTrue(TEXT("Attack montage contains Hit and timely Finish"),Hit&&Finish);
  if(I==1)TestTrue(TEXT("Second attack's backstep follows Hit and precedes Finish"),HitTime>=0.f&&BackstepTime>HitTime&&FinishTime>BackstepTime);
 }
 // BeginAction uses ultimate sequence entry zero when present, otherwise the
 // main skill montage. Inspect the same production asset used by the action.
 const UAnimMontage* Ultimate=Hero->PresentationDefinition->UltimateSequence.IsEmpty()
  ?Hero->GetPresentationMontage(EGPresentationAction::Skill)
  :Hero->PresentationDefinition->UltimateSequence[0].Montage.LoadSynchronous();
 if(D->bBasicOnlyPresentation&&Hero->GetSkillAnimation().IsNull())
 {
  const auto& Assemblies=Hero->PresentationDefinition->AttackAssemblies;
  TestTrue(TEXT("Basic-only presentation has no assembly or five new attack assemblies"),Assemblies.IsEmpty()||Assemblies.Num()==5);
  const auto IsNewElysiaResource=[](const UObject* Asset)
  {
   return Asset&&Asset->GetPathName().StartsWith(TEXT("/Game/"))&&
    Asset->GetPathName().Contains(TEXT("/Characters/Elysia/"))&&Asset->GetPathName().Contains(TEXT("Reimport20261101"));
  };
  for(int32 I=0;I<Assemblies.Num();++I)
  {
   auto* Assembly=Assemblies[I].LoadSynchronous();
   if(!TestNotNull(TEXT("New attack body and bow assembly loads"),Assembly))continue;
   TestTrue(TEXT("Attack assembly belongs to this reimport"),IsNewElysiaResource(Assembly));
   TestTrue(TEXT("Attack assembly has a motion"),!Assembly->Motions.IsEmpty());
   for(const auto& Motion:Assembly->Motions)
   {
    TestTrue(TEXT("New body and bow animation has no effects"),Motion.Effects.IsEmpty());
    bool bBody=false,bBow=false;
    for(const auto& Part:Motion.Parts)
    {
     TestTrue(TEXT("Part mesh belongs to this reimport"),IsNewElysiaResource(Part.Mesh.Get()));
     TestTrue(TEXT("Part animation belongs to this reimport"),IsNewElysiaResource(Part.Animation.Get()));
     TestNull(TEXT("Part has no old native rendering pass"),Part.NativePassDefinition.Get());
     if(Part.PartId==TEXT("Body"))
     {
      bBody=true;
      TestTrue(TEXT("Body part uses the production mesh"),Part.Mesh.Get()==Hero->GetMesh().LoadSynchronous());
      if(Hero->PresentationDefinition->AttackSequence.IsValidIndex(I))
       TestTrue(TEXT("Body part follows the selected attack"),Part.Animation.Get()==Hero->PresentationDefinition->AttackSequence[I].Animation.LoadSynchronous());
     }
     if(Part.PartId.ToString().StartsWith(TEXT("Bow")))bBow=true;
    }
    TestTrue(TEXT("New attack motion contains both body and bow"),bBody&&bBow);
   }
  }
  TestTrue(TEXT("Basic-only presentation has no charge/burst assemblies"),D->ChargeAssembly.IsNull()&&D->BurstAssembly.IsNull());
 }
 if(Ultimate)
 {
  float ReleaseTime=-1.f,FinishTime=-1.f;
  for(const auto& N:Ultimate->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify))
  {
   if(Notify->Event==TEXT("Release")&&ReleaseTime<0.f)ReleaseTime=N.GetTime();
   if(Notify->Event==TEXT("Finish")&&FinishTime<0.f)FinishTime=N.GetTime();
  }
  TestTrue(TEXT("Ultimate montage has Release notify"),ReleaseTime>=0.f);
  TestTrue(TEXT("Ultimate montage has Finish notify"),FinishTime>=0.f);
  TestTrue(TEXT("Ultimate Release precedes Finish"),ReleaseTime>=0.f&&FinishTime>ReleaseTime);
  TestTrue(TEXT("Ultimate Finish precedes natural blend-out"),FinishTime>=0.f&&FinishTime<Ultimate->GetPlayLength()-.12f);
 }
 else TestTrue(TEXT("Only a still-unconfigured basic-only hero may omit its ultimate"),D->bBasicOnlyPresentation&&Hero->GetSkillAnimation().IsNull());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaArrowRain,
 "ElyGambit.Elysia.ArrowRain.LockedAreaPulsesAndConcurrentBasic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaArrowRain::RunTest(const FString&)
{
 FElysiaWorld E(*this,true);
 if(!TestTrue(TEXT("Arrow-rain fixture has two enemies"),E.Ready()&&E.Enemies.Num()>=2))return false;
 E.GM->BattleRules->CompleteOpeningForTests();
 auto* Skill=E.Hero->GetHeroSkill();auto* Policy=E.Policy();auto* Rules=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* First=E.Enemies[0];auto* Second=E.Enemies[1];
 if(!TestNotNull(TEXT("Rules"),Rules))return false;
 TGuardValue<float> Duration(Rules->ArrowRainSeconds,4.f);TGuardValue<int32> Count(Rules->ArrowRainPulses,8);
 TGuardValue<float> RadiusCells(Rules->ArrowRainRadiusCells,1.5f);
 // Deliberately choose a target capsule whose bottom is not the board plane.
 // The lock and visual must still land on the real board, not its capsule.
 First->GetCapsuleComponent()->SetCapsuleHalfHeight(150.f);
 const FVector Original=First->GetActorLocation();Second->SetActorLocation(Original+FVector(1600,0,0));
 for(int32 I=2;I<E.Enemies.Num();++I)E.Enemies[I]->SetActorLocation(Original+FVector(2000+I*100,0,0));
 E.Hero->GetCombat()->ResetCombat();
 auto* ASC=E.Hero->GetAbilitySystemComponent();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Hero->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("Valid ultimate starts"),Skill->BeginAction(true)))return false;
 const FGuid Cast=Skill->ActionId;
 const auto* Board=E.GM->GetBoard()?E.GM->GetBoard()->LayoutSource.Get():nullptr;
 if(!TestNotNull(TEXT("Actual board layout"),Board))return false;
 const FTransform BoardBasis=Board->GetActorTransform();FVector LocalGround=BoardBasis.InverseTransformPosition(Original);LocalGround.Z=0.f;
 const FVector Ground=BoardBasis.TransformPosition(LocalGround);
 TestTrue(TEXT("Fixture capsule bottom differs from board plane"),
  !Ground.Equals(Original-FVector(0,0,First->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()),.1f));
 // Movement after the cast begins never changes the snapshotted center.
 First->SetActorLocation(Original+FVector(50,0,0));
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Release"));
 if(!TestTrue(TEXT("One Release activates rain"),Policy->IsArrowRainActive()))return false;
 TestTrue(TEXT("Locked center projects the initial target onto the actual board plane"),Policy->GetArrowRainCenter().Equals(Ground,.1f));
 TestTrue(TEXT("Three-cell diameter uses live board world scale"),FMath::IsNearlyEqual(Policy->GetArrowRainRadius(),750.f,.1f));
 Skill->AnimationEvent(TEXT("Finish"));
 TestFalse(TEXT("Ultimate completion releases the action immediately"),Skill->IsActionRunning());
 TestTrue(TEXT("Rain remains active independently of the action"),Policy->IsArrowRainActive());
 const float FirstBefore=First->GetAttributes()->GetHealth(),SecondBefore=Second->GetAttributes()->GetHealth();
 E.Advance(.49f);TestEqual(TEXT("No damage before first half second"),First->GetAttributes()->GetHealth(),FirstBefore);
 E.Advance(.03f);
 const float OnePulse=FirstBefore-First->GetAttributes()->GetHealth();
 TestTrue(TEXT("First pulse deals damage"),OnePulse>0.f);
 TestEqual(TEXT("Outside target avoids the first pulse"),Second->GetAttributes()->GetHealth(),SecondBefore);
 TestEqual(TEXT("Only one pulse after half a second"),Policy->GetArrowRainPulseCount(),1);
 // Once the cast is completed, a new ordinary action and its cancellation
 // have a distinct action ID and must not cancel the already released rain.
 if(TestTrue(TEXT("Basic attack can start while rain continues"),Skill->BeginAction(false)))
 {
  TestTrue(TEXT("Basic action has its own identity"),Skill->ActionId.IsValid()&&Skill->ActionId!=Cast);
  Skill->CancelAction();TestTrue(TEXT("Cancelling basic leaves rain running"),Policy->IsArrowRainActive());
 }
 First->SetActorLocation(Original+FVector(1600,0,0));Second->SetActorLocation(Original+FVector(100,0,0));
 E.Advance(3.55f);
 TestFalse(TEXT("Rain ends after four seconds"),Policy->IsArrowRainActive());
 TestEqual(TEXT("Exactly eight pulses, with no duplicate Release"),Policy->GetArrowRainPulseCount(),8);
 TestTrue(TEXT("Enemy leaving receives no later pulses"),FMath::IsNearlyEqual(FirstBefore-First->GetAttributes()->GetHealth(),OnePulse,.1f));
 TestTrue(TEXT("Enemy entering receives remaining seven equal pulses"),FMath::IsNearlyEqual(SecondBefore-Second->GetAttributes()->GetHealth(),OnePulse*7.f,.2f));
 TestEqual(TEXT("Arrow rain does not count as a basic hit"),Policy->PrimaryHitCount,0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaArrowRainCancel,
 "ElyGambit.Elysia.ArrowRain.CancelRefundAndRoundStop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaArrowRainCancel::RunTest(const FString&)
{
 FElysiaWorld E(*this,true);if(!TestTrue(TEXT("Arrow-rain fixture ready"),E.Ready()))return false;
 E.GM->BattleRules->CompleteOpeningForTests();auto* Skill=E.Hero->GetHeroSkill();auto* Policy=E.Policy();
 auto* ASC=E.Hero->GetAbilitySystemComponent();const float Full=E.Hero->GetAttributes()->GetMaxMana();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Full);E.Hero->GetCombat()->ResetCombat();
 if(!TestTrue(TEXT("Ultimate starts before cancellation"),Skill->BeginAction(true)))return false;
 Skill->CancelAction();
 TestFalse(TEXT("Cancelled before Release never starts rain"),Policy->IsArrowRainActive());
 TestTrue(TEXT("Cancelled uncommitted cast refunds mana"),FMath::IsNearlyEqual(E.Hero->GetAttributes()->GetMana(),Full,.01f));
 if(!TestTrue(TEXT("Ultimate can be retried"),Skill->BeginAction(true)))return false;
 Skill->AnimationEvent(TEXT("Release"));
 if(!TestTrue(TEXT("Release starts rain"),Policy->IsArrowRainActive()))return false;
 Skill->CancelAction();E.Advance(.6f);
 TestFalse(TEXT("Interrupted unrecovered cast cancels its rain"),Policy->IsArrowRainActive());
 TestEqual(TEXT("Interrupted cast produced no late pulses"),Policy->GetArrowRainPulseCount(),0);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Full);
 if(!TestTrue(TEXT("Third cast begins"),Skill->BeginAction(true)))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
 E.Advance(.51f);TestEqual(TEXT("One pulse before stop"),Policy->GetArrowRainPulseCount(),1);
 E.GM->BattleRules->Stop();E.Advance(1.f);
 TestFalse(TEXT("Round stop clears released rain"),Policy->IsArrowRainActive());
 TestEqual(TEXT("Round teardown resets pulse bookkeeping"),Policy->GetArrowRainPulseCount(),0);
 return true;
}
#endif
