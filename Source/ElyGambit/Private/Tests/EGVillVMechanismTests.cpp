#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Character/EGDemoCombatUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/ProgressBar.h"
#include "Components/CanvasPanelSlot.h"
#include "Layout/Geometry.h"
#include "Widgets/SWidget.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "UI/Widget/EGDemoPanel.h"

namespace
{
struct FVillVVitalsWorld
{
 TStrongObjectPtr<UGameInstance> Instance;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGCardManager* Cards=nullptr;
 TMap<TWeakObjectPtr<UEGUnitVitals>,TSharedPtr<SWidget>> SlateViews;

 bool Initialize(FAutomationTestBase& Test)
 {
  if(!GEngine)return false;
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return false;
  Instance.Reset(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();World=Instance->GetWorld();
  if(!Test.TestNotNull(TEXT("Vill-V vitals fixture world"),World))return false;
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Test.TestNotNull(TEXT("Vill-V vitals game mode"),Mode))return false;
  Mode->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,Mode);
  if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return false;
  Run->bRoundShopRules=true;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;Mode->RunDefinition=Run;
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Cards=World->SpawnActor<AEGCardManager>();auto* Board=World->SpawnActor<AEGBoard>();
  if(!Scene||!Grid||!Cards||!Board){Test.AddError(TEXT("Vill-V vitals board spawn failed"));return false;}
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
  Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=Board;Board->Dimensions={7,7};
  World->InitializeActorsForPlay(URL);World->BeginPlay();return true;
 }
 void TickViews()
 {
  // This standalone world has no game viewport. Drive each actual Slate wrapper
  // explicitly so widget NativeTick observes mechanism transitions.
  const auto Geometry=FGeometry::MakeRoot(FVector2D(128,72),FSlateLayoutTransform());
  TSet<TWeakObjectPtr<UEGUnitVitals>> LiveViews;
  for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit))
   if(auto* Component=Unit->FindComponentByClass<UWidgetComponent>())
    if(auto* View=Cast<UEGUnitVitals>(Component->GetUserWidgetObject()))
    {
     LiveViews.Add(View);
     auto& Slate=SlateViews.FindOrAdd(View);
     // A real screen layer holds the wrapper throughout display. A temporary
     // TakeWidget result would destruct it and unbind delegates every frame.
     if(!Slate.IsValid())Slate=View->TakeWidget();
     Slate->Tick(Geometry,FPlatformTime::Seconds(),.05f);
    }
  for(auto It=SlateViews.CreateIterator();It;++It)
   if(!It.Key().IsValid()||!LiveViews.Contains(It.Key()))It.RemoveCurrent();
 }
 void Tick()
 {++GFrameCounter;World->Tick(LEVELTICK_All,.05f);TickViews();}
 ~FVillVVitalsWorld()
 {
  if(!World||!GEngine)return;
  SlateViews.Reset();
  World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVVitalsLifecycle,"ElyGambit.Presentation.UnitVitals.VillVMechanismLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVVitalsLifecycle::RunTest(const FString&)
{
 FVillVVitalsWorld Fixture;if(!Fixture.Initialize(*this))return false;
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
 if(!TestNotNull(TEXT("Formal Vill-V card"),Card)||!TestTrue(TEXT("Grant Vill-V fixture"),Fixture.Cards->GrantCard(Card)))return false;
 if(!TestTrue(TEXT("Deploy Vill-V and two-device formation"),Fixture.Cards->TryDeployHandCard(Fixture.Cards->GetHand().Last().InstanceId,{2,2})))return false;
 const FGuid Id=Fixture.Mode->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
 if(!TestTrue(TEXT("Deployed Vill-V has persistent identity"),Id.IsValid())||!TestTrue(TEXT("Enter real combat path"),Fixture.Mode->EnterCombat()))return false;
 Fixture.Mode->BattleRules->CompleteOpeningForTests();
 Fixture.TickViews();
 AEGChessUnit* Pilot=nullptr;
 for(auto* Unit:Fixture.Mode->GetCombatUnits())
 {
  if(Unit->GetInstanceId()==Id)Pilot=Unit;
  if(Unit->GetCombat())Unit->GetCombat()->SetComponentTickEnabled(false);
 }
 if(!TestNotNull(TEXT("Combat pilot exists"),Pilot))return false;
 auto* Mechanism=Pilot->FindComponentByClass<UEGVillVMechanismComponent>();
 auto* WidgetComponent=Pilot->FindComponentByClass<UWidgetComponent>();
 auto* View=WidgetComponent?Cast<UEGUnitVitals>(WidgetComponent->GetUserWidgetObject()):nullptr;
 if(!TestNotNull(TEXT("Pilot mechanism exists"),Mechanism)||!TestNotNull(TEXT("Pilot owns combat vitals"),View)||
    !TestNotNull(TEXT("Pilot health bar exists"),View->GetHealthBar())||!TestNotNull(TEXT("Pilot mana bar exists"),View->GetManaBar()))return false;
 TestTrue(TEXT("Pilot health bar is visible before boarding"),View->GetHealthBar()->IsVisible());
 TestTrue(TEXT("Pilot mana bar is visible before boarding"),View->GetManaBar()->IsVisible());
 auto* Badge=View->GetWidgetFromName(TEXT("LevelBadge"));
 auto* BadgeSlot=Badge?Cast<UCanvasPanelSlot>(Badge->Slot):nullptr;
 auto* HealthSlot=Cast<UCanvasPanelSlot>(View->GetHealthBar()->Slot);
 if(TestNotNull(TEXT("Single pilot level badge has canvas slot"),BadgeSlot)&&TestNotNull(TEXT("Pilot health bar has canvas slot"),HealthSlot))
  TestTrue(TEXT("Restored pilot level badge sits left of health and mana"),BadgeSlot->GetPosition().X<HealthSlot->GetPosition().X);
 auto* ASC=Pilot->GetAbilitySystemComponent();
 const float MaxMana=Pilot->GetAttributes()->GetMaxMana();
 if(!TestTrue(TEXT("Vill-V has mana"),MaxMana>0.f))return false;
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),MaxMana*.4f);
 TestTrue(TEXT("Pilot mana bar follows pilot ASC"),FMath::IsNearlyEqual(View->GetManaBar()->GetPercent(),.4f,.01f));
 auto WaitOperating=[&](int32 Device)
 {
  bool bKeptBothBars=true;
  for(int32 Frame=0;Frame<240;++Frame)
  {
   Fixture.Tick();
   bKeptBothBars&=View->GetHealthBar()->IsVisible()&&View->GetManaBar()->IsVisible();
   if(Mechanism->IsOperating()&&Mechanism->Mode()==Device&&!Pilot->GetHeroSkill()->IsActionRunning())
   {TestTrue(TEXT("Pilot keeps both bars throughout travel and boarding"),bKeptBothBars);return true;}
  }
  TestTrue(TEXT("Pilot keeps both bars throughout travel and boarding"),bKeptBothBars);return false;
 };
 if(!TestTrue(TEXT("Pilot boards first mechanism"),WaitOperating(0)))return false;
 TestEqual(TEXT("Pilot vitals stay bound while operating"),View->DisplayMode,EEGVitalsMode::Combat);
 auto* Gun=Mechanism->GetDevice(0);auto* Cannon=Mechanism->GetDevice(1);
 if(!TestNotNull(TEXT("First mechanism exists"),Gun)||!TestNotNull(TEXT("Second mechanism exists"),Cannon))return false;
 auto DeviceVitalsVisible=[](AEGVillVEmplacement* Device)
 {
  auto* Component=Device?Device->FindComponentByClass<UWidgetComponent>():nullptr;
  auto* DeviceView=Component?Cast<UEGUnitVitals>(Component->GetUserWidgetObject()):nullptr;
  const auto* CombatGroup=DeviceView?DeviceView->GetWidgetFromName(TEXT("CombatGroup")):nullptr;
  return Component&&Component->IsVisible()&&DeviceView&&DeviceView->IsVisible()&&CombatGroup&&CombatGroup->IsVisible();
 };
 TestFalse(TEXT("Occupied mechanism hides its own vitals"),DeviceVitalsVisible(Gun));
 TestTrue(TEXT("Unoccupied mechanism retains its own vitals"),DeviceVitalsVisible(Cannon));
 const float PilotMax=Pilot->GetAttributes()->GetMaxHealth();
 const float GunMax=Gun->GetAttributes()->GetMaxHealth();
 const float CannonMax=Cannon->GetAttributes()->GetMaxHealth();
 if(!TestTrue(TEXT("Pilot and device health pools are positive"),PilotMax>0.f&&GunMax>0.f&&CannonMax>0.f))return false;
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),PilotMax*.8f);
 Gun->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),GunMax*.25f);
 Cannon->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),CannonMax*.7f);
 TestTrue(TEXT("Mounted pilot health shows occupied mechanism ASC immediately"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.25f,.01f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),MaxMana*.4f);
 TestTrue(TEXT("Mounted mana continues following pilot ASC"),FMath::IsNearlyEqual(View->GetManaBar()->GetPercent(),.4f,.01f));
 Pilot->GetHeroSkill()->CancelAction();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),MaxMana);
 if(!TestTrue(TEXT("Controlled switch starts"),Pilot->GetHeroSkill()->BeginAction(true)))return false;
 bool bSawTravel=false,bTravelSourceCorrect=true,bTravelDevicesVisible=true,bBoardedSecond=false;
 for(int32 Frame=0;Frame<240;++Frame)
 {
  Fixture.Tick();
  if(!Mechanism->IsOperating())
  {
   bSawTravel=true;
   bTravelSourceCorrect&=FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.8f,.01f);
   bTravelDevicesVisible&=DeviceVitalsVisible(Gun)&&DeviceVitalsVisible(Cannon);
  }
  if(Mechanism->IsOperating()&&Mechanism->Mode()==1&&!Pilot->GetHeroSkill()->IsActionRunning())
  {bBoardedSecond=true;break;}
 }
 TestTrue(TEXT("Switch exposes a leaving or travel phase"),bSawTravel);
 TestTrue(TEXT("Travel shows pilot ASC health"),bTravelSourceCorrect);
 TestTrue(TEXT("Both unoccupied mechanisms show their vitals during travel"),bTravelDevicesVisible);
 if(!TestTrue(TEXT("Pilot changes mechanisms and boards again"),bBoardedSecond))return false;
 TestEqual(TEXT("Switch keeps the same pilot widget"),Cast<UEGUnitVitals>(WidgetComponent->GetUserWidgetObject()),View);
 TestTrue(TEXT("Vacated first mechanism restores its own vitals"),DeviceVitalsVisible(Gun));
 TestFalse(TEXT("Occupied second mechanism hides its own vitals"),DeviceVitalsVisible(Cannon));
 TestTrue(TEXT("Switch selects the second mechanism health source"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.7f,.01f));
 Cannon->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),CannonMax*.35f);
 TestTrue(TEXT("Second mechanism health updates pilot bar immediately"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.35f,.01f));
 TWeakObjectPtr<AEGVillVEmplacement> WeakGun=Gun,WeakCannon=Cannon;
 Fixture.Mode->BattleRules->Stop();
 Fixture.TickViews();
 TestTrue(TEXT("Stopped battle destroys first mechanism"),!WeakGun.IsValid()||WeakGun->IsActorBeingDestroyed());
 TestTrue(TEXT("Stopped battle destroys second mechanism"),!WeakCannon.IsValid()||WeakCannon->IsActorBeingDestroyed());
 TestTrue(TEXT("Stop restores pilot health source"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.8f,.01f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),PilotMax*.6f);
 TestTrue(TEXT("Pilot health continues updating after device cleanup"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.6f,.01f));
 Pilot->Die();
 TestEqual(TEXT("Pilot death clears combat vitals"),View->DisplayMode,EEGVitalsMode::Unbound);
 return true;
}
#endif
