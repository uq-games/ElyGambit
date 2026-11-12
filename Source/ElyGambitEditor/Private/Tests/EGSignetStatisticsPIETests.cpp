#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGRunDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "GameFramework/PlayerController.h"

namespace
{
class FSignetStatisticsStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1600;Settings->NewWindowHeight=900;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1600,900))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

UEGBoardButton* FindSignetMetric(UWidget* Widget)
{
 if(auto* Button=Cast<UEGBoardButton>(Widget);Button&&Button->Action==23&&Button->Index==3)return Button;
 if(auto* Panel=Cast<UPanelWidget>(Widget))
  for(int32 I=0;I<Panel->GetChildrenCount();++I)
   if(auto* Found=FindSignetMetric(Panel->GetChildAt(I)))return Found;
 return nullptr;
}

int32 CountSignetMetrics(UWidget* Widget)
{
 if(!Widget)return 0;
 const auto* Button=Cast<UEGBoardButton>(Widget);
 int32 Count=Button&&Button->Action==23&&Button->Index==3?1:0;
 if(const auto* Panel=Cast<UPanelWidget>(Widget))
  for(int32 I=0;I<Panel->GetChildrenCount();++I)Count+=CountSignetMetrics(Panel->GetChildAt(I));
 return Count;
}

void DescribeStatisticsTree(UWidget* Widget,FString& Out,int32 Depth=0)
{
 if(!Widget)return;
 Out+=FString::Printf(TEXT("%s%s (%s)"),*FString::ChrN(Depth,TEXT(' ')),*Widget->GetName(),*Widget->GetClass()->GetName());
 if(const auto* Button=Cast<UEGBoardButton>(Widget))Out+=FString::Printf(TEXT(" action=%d index=%d"),Button->Action,Button->Index);
 Out+=TEXT("\n");
 if(const auto* Panel=Cast<UPanelWidget>(Widget))
  for(int32 I=0;I<Panel->GetChildrenCount();++I)DescribeStatisticsTree(Panel->GetChildAt(I),Out,Depth+1);
}

class FSignetStatisticsPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Player,Target;
 double Deadline=0.,Until=0.;
 int32 Step=0;
 bool bReconstructed=false;
 float FirstHealth=0.f;
public:
 explicit FSignetStatisticsPIE(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+120.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Signet statistics PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Main=HUD?HUD->GetMainWidget():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards()||!Main)return false;
  // GetMainWidget can be assigned before NativeConstruct builds the dynamic
  // statistics children; do not test an incomplete HUD tree.
  if(Step==0&&(!Mode->CanPrepare()||!Mode->AreStageResourcesReady()||!Main->GetWidgetFromName(TEXT("BoardStatisticsTitle"))))return false;
  auto* Statistics=Cast<UButton>(Main->GetWidgetFromName(TEXT("Button_Statistics")));
  auto* Dock=Main->GetWidgetFromName(TEXT("BoardStatistics"));
  auto* Label=Cast<UTextBlock>(Main->GetWidgetFromName(TEXT("StatisticsLabel_0")));
  auto* Metric=FindSignetMetric(Dock);
  if(!Metric&&Dock)
  {
   FString Tree;DescribeStatisticsTree(Dock,Tree);
   Test->AddInfo(TEXT("Statistics drawer widget tree:\n")+Tree);
  }
  if(!Test->TestNotNull(TEXT("Statistics button exists"),Statistics)||!Test->TestNotNull(TEXT("Statistics drawer exists"),Dock)||
     !Test->TestNotNull(TEXT("Signet damage tab exists"),Metric)||!Test->TestNotNull(TEXT("Statistics row exists"),Label))return true;

  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   Test->TestEqual(TEXT("Exactly one signet damage tab"),CountSignetMetrics(Dock),1);
   Test->TestTrue(TEXT("Signet damage tab has a stable name"),Metric->GetFName()==TEXT("StatisticsMetric_SignetDamage"));
   if(!bReconstructed)
   {
    bReconstructed=true;Main->RemoveFromParent();Main->AddToViewport(10);
    return false;
   }
   auto* Cards=Mode->GetCards();auto* Match=Mode->GetMatch();
   if(!Test->TestTrue(TEXT("Set controlled Helix selection"),Match->SetStigmataForBalanceTests({FName(TEXT("Helix"))}))||
      !Test->TestNotNull(TEXT("Starting card exists"),Mode->RunDefinition?Mode->RunDefinition->StartingCard.Get():nullptr)||
      !Test->TestTrue(TEXT("Grant starting card"),Cards->GrantCard(Mode->RunDefinition->StartingCard)))return true;
   const FGuid CardId=Cards->GetHand().Last().InstanceId;
   if(!Test->TestTrue(TEXT("Deploy controlled hero"),Cards->TryDeployHandCard(CardId,{2,1}))||
      !Test->TestTrue(TEXT("Start real combat"),Mode->EnterCombat()))return true;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit)
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);
    if(Unit->GetFaction_Implementation()==EEGFaction::Player&&!Player.IsValid())Player=Unit;
    if(Unit->GetFaction_Implementation()==EEGFaction::Enemy&&!Target.IsValid()&&!Unit->FindComponentByClass<UEGCorruptionCombatComponent>())Target=Unit;
   }
   if(!Test->TestTrue(TEXT("Both battle sides exist"),Player.IsValid()&&Target.IsValid()))return true;
   auto* ASC=Target->GetAbilitySystemComponent();
   Player->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDamageMultiplierAttribute(),1.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   Target->GetHeroSkill()->ClearProtection();
   Target->GetHeroSkill()->GrantSourceShield(TEXT("StatisticsFixture"),40.f,30.f);
   FirstHealth=Target->GetAttributes()->GetHealth();
   Statistics->OnClicked.Broadcast();Until=FPlatformTime::Seconds()+.3;Step=1;
   Test->AddInfo(TEXT("Controlled PIE uses the formal board, a test-selected Helix and real combat execution; buttons use programmatic clicks, not real mouse input or natural battle."));
   return false;
  }
  if(Step==1)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   if(!Test->TestTrue(TEXT("Statistics drawer opened"),Dock->IsVisible()))return true;
   Metric->OnClicked.Broadcast();
   Test->TestTrue(TEXT("Empty signet damage is shown before a hit"),Label->GetText().ToString().Contains(TEXT("暂无刻印伤害")));
   if(!Test->TestTrue(TEXT("First real signet hit resolves"),FEGCombatExecution::StigmataDamage(Player.Get(),Target.Get(),70.f,FGuid::NewGuid(),false,TEXT("Helix"))))return true;
   Test->TestTrue(TEXT("First hit consumes forty shield and thirty health"),
    FMath::IsNearlyZero(Target->GetHeroSkill()->GetSourceShield(TEXT("StatisticsFixture")),.05f)&&
    FMath::IsNearlyEqual(FirstHealth-Target->GetAttributes()->GetHealth(),30.f,.05f));
   Until=FPlatformTime::Seconds()+.1;Step=2;return false;
  }
  if(Step==2||Step==3)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   auto* Rules=Mode->BattleRules?Mode->BattleRules->GetStigmata():nullptr;
   if(!Test->TestNotNull(TEXT("Authoritative battle contribution exists"),Rules))return true;
   const auto Rows=Rules->GetContributions();
   const auto* Helix=Rows.FindByPredicate([](const FEGStigmataContribution& Row){return Row.Id==TEXT("Helix");});
   const float Expected=Step==2?70.f:100.f;
   Test->TestTrue(TEXT("Contribution equals actual settled shield and health damage"),Helix&&FMath::IsNearlyEqual(Helix->AdditionalDamage,Expected,.05f));
   const FString Display=Label->GetText().ToString();
   Test->TestTrue(TEXT("Open statistics row updates to authoritative amount"),Display.Contains(TEXT("螺旋"))&&Display.Contains(FString::Printf(TEXT("%.0f"),Expected)));
   if(Step==2)
   {
    if(!Test->TestTrue(TEXT("Second real signet hit resolves"),FEGCombatExecution::StigmataDamage(Player.Get(),Target.Get(),30.f,FGuid::NewGuid(),false,TEXT("Helix"))))return true;
    Until=FPlatformTime::Seconds()+.1;Step=3;return false;
   }
   if(!Test->TestTrue(TEXT("Finish first battle"),Mode->ResolveCombat(true)))return true;
   Step=4;return false;
  }
  if(Step==4)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady()||Mode->GetMatch()->GetStageNumber()!=2)return false;
   if(!Test->TestTrue(TEXT("Start next battle"),Mode->EnterCombat()))return true;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit)Unit->GetCombat()->SetComponentTickEnabled(false);
   auto* Rules=Mode->BattleRules?Mode->BattleRules->GetStigmata():nullptr;
   if(!Test->TestNotNull(TEXT("Next battle contribution exists"),Rules))return true;
   const auto Rows=Rules->GetContributions();
   const auto* Helix=Rows.FindByPredicate([](const FEGStigmataContribution& Row){return Row.Id==TEXT("Helix");});
   Test->TestTrue(TEXT("New battle resets contribution"),!Helix||FMath::IsNearlyZero(Helix->AdditionalDamage));
   // The statistics drawer can remain open across the phase transition.
   // Let its visibility settle before deciding whether a click is needed.
   Until=FPlatformTime::Seconds()+.3;Step=5;return false;
  }
  if(FPlatformTime::Seconds()<Until)return false;
  if(Step==5&&!Dock->IsVisible())
  {
   Statistics->OnClicked.Broadcast();Until=FPlatformTime::Seconds()+.3;Step=6;return false;
  }
  if(!Test->TestTrue(TEXT("Statistics drawer is open in next battle"),Dock->IsVisible()))return true;
  // Keep the signet metric selected: reset must reach an already-open drawer
  // without forcing a manual refresh by clicking the metric a second time.
  Test->TestTrue(TEXT("New battle shows no stale signet damage"),Label->GetText().ToString().Contains(TEXT("暂无刻印伤害")));
  return true;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSignetStatisticsGraphical,
 "ElyGambit.Integration.SignetStatisticsGraphical",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSignetStatisticsGraphical::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Signet statistics requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Signet statistics requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FSignetStatisticsStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FSignetStatisticsPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
