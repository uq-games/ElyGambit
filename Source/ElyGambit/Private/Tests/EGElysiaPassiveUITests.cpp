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
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/EGUnitInformation.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaPassiveUI,"ElyGambit.Presentation.ElysiaPassiveUI.SnapshotAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaPassiveUI::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();GM->RunDefinition=EGProductionTestAssets::ResolveRun(*this);
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 UEGCardDefinition* Card=GM->RunDefinition?GM->RunDefinition->StartingCard.Get():nullptr;
 if(!Card&&GM->RunDefinition)for(const auto& Candidate:GM->RunDefinition->CardPool)if(Candidate&&Candidate->HeroDefinition&&Candidate->HeroDefinition->HeroKind==EEGHeroKind::Elysia){Card=Candidate.Get();break;}
 if(Card){Cards->GrantCard(Card);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2});}
 GM->EnterCombat();GM->BattleRules->CompleteOpeningForTests();
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Enemy=nullptr;
 for(auto* U:GM->GetCombatUnits())
 {
  U->GetCombat()->SetComponentTickEnabled(false);
  if(U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::Elysia&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)Hero=U;
  else if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemy=U;
 }
 auto* View=CreateWidget<UEGUnitVitals>(GI.Get(),UEGUnitVitals::ResolveWidgetClass());TStrongObjectPtr<UEGUnitVitals> Keep(View);
 if(TestNotNull(TEXT("Ordinary Elysia exists"),Hero)&&TestNotNull(TEXT("Enemy fixture exists"),Enemy)&&TestNotNull(TEXT("Vitals widget exists"),View))
 {
  View->TakeWidget();View->SetUnit(Hero);
  auto* Label=Cast<UTextBlock>(View->GetWidgetFromName(TEXT("ElysiaPassiveLabel")));
  auto* Bar=Cast<UProgressBar>(View->GetWidgetFromName(TEXT("ElysiaPassiveBar")));
  auto* Caption=Cast<UTextBlock>(View->GetWidgetFromName(TEXT("StatusLabel")));
  auto* Policy=Cast<UEGElysiaSkillBehavior>(Hero->GetHeroSkill()->GetBehavior());
  auto* Rules=Cast<UEGElysiaSkillDefinition>(Hero->GetDefinition()->SkillDefinition);
  if(TestNotNull(TEXT("Passive label"),Label)&&TestNotNull(TEXT("Passive bar"),Bar)&&TestNotNull(TEXT("Caption"),Caption)&&TestNotNull(TEXT("Behavior"),Policy)&&TestNotNull(TEXT("Rules"),Rules))
  {
   TGuardValue<int32> Interval(Rules->ChargedAttackInterval,3);
   Hero->GetHeroSkill()->OnStatusChanged.Broadcast();
   TestEqual(TEXT("Initial passive uses only numeric progress"),Label->GetText().ToString(),FString(TEXT("0/3")));
   const FLinearColor NormalColor=Label->GetColorAndOpacity().GetSpecifiedColor();
   Hero->GetHeroSkill()->SetTaunt(Enemy,5.f);
   for(int32 I=0;I<2;++I)
   {
    FEGSkillActionContext C;C.RoundId=GM->BattleRules->GetRoundId();C.ActionId=FGuid::NewGuid();C.Target=Enemy;C.Attack=100;
    Policy->ActionStarted(C);Policy->ActionEnded(true);Policy->TickBattle(0.f);
   }
   TestEqual(TEXT("Completed attacks refresh numeric progress"),Label->GetText().ToString(),FString(TEXT("2/3")));
   TestTrue(TEXT("Progress bar reads authoritative completed attacks"),FMath::IsNearlyEqual(Bar->GetPercent(),2.f/3.f));
   TestTrue(TEXT("Ready progress uses a different highlight"),!Label->GetColorAndOpacity().GetSpecifiedColor().Equals(NormalColor));
   TestTrue(TEXT("Ready marker preserves taunt"),Caption->GetText().ToString().Contains(TEXT("嘲讽"))&&Caption->GetText().ToString().Contains(TEXT("◆")));
   TestTrue(TEXT("Details read current passive"),EGUnitInformation::Status(Hero).Contains(TEXT("下一击蓄力")));
   TGuardValue<bool> RainPolicy(Rules->bArrowRainUltimate,true);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   if(TestTrue(TEXT("Arrow rain ultimate can start"),Hero->GetHeroSkill()->BeginAction(true)))
   {
    Hero->GetHeroSkill()->AnimationEvent(TEXT("Release"));Hero->GetHeroSkill()->AnimationEvent(TEXT("Finish"));
    Policy->TickBattle(0.f);
    TestTrue(TEXT("Rain is independent and active"),Policy->IsArrowRainActive());
    TestEqual(TEXT("Rain does not replace normal attack progress with a legacy empowerment counter"),Label->GetText().ToString(),FString(TEXT("2/3")));
    TestTrue(TEXT("Charged readiness remains visible during rain"),Caption->GetText().ToString().Contains(TEXT("◆")));
   }
   Policy->EndBattle();Hero->GetHeroSkill()->OnStatusChanged.Broadcast();
   TestEqual(TEXT("Ending battle hides old passive row"),Label->GetVisibility(),ESlateVisibility::Collapsed);
   TestTrue(TEXT("Ending battle clears transient marker but keeps taunt"),Caption->GetText().ToString().Contains(TEXT("嘲讽"))&&!Caption->GetText().ToString().Contains(TEXT("◆")));
  }
  View->SetUnit(nullptr);
 }
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanPassiveUI,"ElyGambit.Presentation.ElysiaPassiveUI.HumanPhases",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanPassiveUI::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 TSoftObjectPtr<UEGCardDefinition> FormalHumanCard;
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();GM->RunDefinition=EGProductionTestAssets::ResolveRun(*this,&FormalHumanCard);
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 UEGCardDefinition* Card=FormalHumanCard.LoadSynchronous();
 const bool bDeployed=Card&&Cards->GrantCard(Card)&&Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,1})&&GM->EnterCombat();
 if(TestTrue(TEXT("Human card deploys"),bDeployed))
 {
  GM->BattleRules->CompleteOpeningForTests();AEGChessUnit* Human=nullptr;AEGChessUnit* Enemy=nullptr;
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);U->GetCharacterMovement()->DisableMovement();
   if(U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=U;
   else if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemy=U;
  }
  auto* View=CreateWidget<UEGUnitVitals>(GI.Get(),UEGUnitVitals::ResolveWidgetClass());TStrongObjectPtr<UEGUnitVitals> Keep(View);
  if(TestNotNull(TEXT("Human unit"),Human)&&TestNotNull(TEXT("Enemy"),Enemy)&&TestNotNull(TEXT("Vitals widget"),View))
  {
   Enemy->SetActorLocation(Grid->CellToWorld({2,4})+FVector(0,0,88));View->TakeWidget();View->SetUnit(Human);
   auto* Label=Cast<UTextBlock>(View->GetWidgetFromName(TEXT("ElysiaPassiveLabel")));
   auto* Caption=Cast<UTextBlock>(View->GetWidgetFromName(TEXT("StatusLabel")));
   auto* Policy=Cast<UEGElysiaHerrscherSkillBehavior>(Human->GetHeroSkill()->GetBehavior());
   if(TestNotNull(TEXT("Human passive label"),Label)&&TestNotNull(TEXT("Human caption"),Caption)&&TestNotNull(TEXT("Human behavior"),Policy))
   {
    const auto BowState=Policy->GetPassiveState();
    TestEqual(TEXT("Bow progress uses only numbers"),Label->GetText().ToString(),FString::Printf(TEXT("%d/%d"),BowState.Progress,BowState.Threshold));
    const FLinearColor NormalColor=Label->GetColorAndOpacity().GetSpecifiedColor();
    auto* Skill=Human->GetHeroSkill();bool bAllAttacks=true;
    for(int32 I=0;I<8;++I)
    {
     if(!Skill->BeginAction(false)){bAllAttacks=false;break;}
     Skill->AnimationEvent(TEXT("Hit"));Skill->AnimationEvent(TEXT("Finish"));Policy->TickBattle(0.f);
     if(I==3)
     {
      TestTrue(TEXT("Fifth arrow ready marker is shown"),Caption->GetText().ToString().Contains(TEXT("◆")));
      TestTrue(TEXT("Fifth arrow ready uses highlight"),!Label->GetColorAndOpacity().GetSpecifiedColor().Equals(NormalColor));
     }
    }
    if(TestTrue(TEXT("Eight bow actions complete"),bAllAttacks))
    {
     ++GFrameCounter;W->Tick(LEVELTICK_All,.05f);
     if(TestTrue(TEXT("Origin entry begins"),Skill->BeginAction(true)))
     {
      Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
      TestTrue(TEXT("Origin status shows numeric progress and seconds"),!Label->GetText().ToString().Contains(TEXT("/"))&&Label->GetText().ToString().Contains(TEXT("s")));
      TestTrue(TEXT("Origin entry marker is shown"),Caption->GetText().ToString().Contains(TEXT("◆")));
      TestTrue(TEXT("Details include origin timer"),EGUnitInformation::Status(Human).Contains(TEXT("剩余")));
     }
    }
    Policy->EndBattle();Skill->OnStatusChanged.Broadcast();
    TestEqual(TEXT("Origin row is removed after battle"),Label->GetVisibility(),ESlateVisibility::Collapsed);
   }
   View->SetUnit(nullptr);
  }
 }
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 return true;
}
#endif
