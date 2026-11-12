#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/WrapBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Data/EGHeroCardInfoDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Texture2D.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif
#include "Engine/World.h"
#include "UI/EGUnitInspectionView.h"
#include "UI/Widget/EGGradientSurface.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGCombatTextLayer.h"
#include "UI/Widget/EGRichText.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCombatTextWindowContract,
 "ElyGambit.UI.InspectionRedesign.CombatTextWindow", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGCombatTextWindowContract::RunTest(const FString&)
{
 const FGuid Target=FGuid::NewGuid(), Other=FGuid::NewGuid();
 FEGActiveCombatNumber Number;
 Number.TargetId=Target; Number.StartedAt=0.; Number.Amount=.4; Number.Age=.06f; Number.Duration=.85f;
 TestTrue(TEXT("Same target and kind inside the first window merge"), Number.TryAccumulate(Target,false,false,.119,.4f));
 TestTrue(TEXT("Sub-unit hits accumulate before formatting"), FMath::IsNearlyEqual(Number.Amount,.8,1.e-6));
 TestEqual(TEXT("Sub-unit aggregate remains readable"), UEGCombatNumber::FormatAmount(Number.Amount), FString(TEXT("<1")));
 TestTrue(TEXT("Third hit merges"), Number.TryAccumulate(Target,false,false,.1195,.4f));
 TestEqual(TEXT("Formatting follows the total rather than rounded hits"), UEGCombatNumber::FormatAmount(Number.Amount), FString(TEXT("1")));
 TestEqual(TEXT("Start time is never reset"), Number.StartedAt, 0.);
 TestEqual(TEXT("Age is never reset"), Number.Age, .06f);
 TestEqual(TEXT("Duration is never extended"), Number.Duration, .85f);
 const double Total=Number.Amount;
 TestFalse(TEXT("Exact end of fixed window starts a new number"), Number.TryAccumulate(Target,false,false,.12,1.f));
 TestFalse(TEXT("Later hit starts a new number"), Number.TryAccumulate(Target,false,false,.121,1.f));
 TestFalse(TEXT("Earlier time does not merge"), Number.TryAccumulate(Target,false,false,-.001,1.f));
 TestFalse(TEXT("Another target never merges"), Number.TryAccumulate(Other,false,false,.05,1.f));
 TestFalse(TEXT("Healing never merges into damage"), Number.TryAccumulate(Target,true,false,.05,1.f));
 TestFalse(TEXT("Shield absorption never merges into health damage"), Number.TryAccumulate(Target,false,true,.05,1.f));
 TestFalse(TEXT("Zero is ignored"), Number.TryAccumulate(Target,false,false,.05,0.f));
 TestFalse(TEXT("Negative values are ignored"), Number.TryAccumulate(Target,false,false,.05,-1.f));
 TestTrue(TEXT("Rejected hits leave the total unchanged"), FMath::IsNearlyEqual(Number.Amount,Total));
 TestTrue(TEXT("Zero is not formatted as a visible value"), UEGCombatNumber::FormatAmount(0.).IsEmpty());
 TestTrue(TEXT("Negative values are not formatted as a visible value"), UEGCombatNumber::FormatAmount(-1.).IsEmpty());
 TestEqual(TEXT("Normal values round only for display"), UEGCombatNumber::FormatAmount(12.6), FString(TEXT("13")));
 FEGActiveCombatNumber Heal; Heal.TargetId=Target; Heal.bHealing=true; Heal.StartedAt=1.; Heal.Amount=5.;
 TestTrue(TEXT("Same-target healing can merge with healing"), Heal.TryAccumulate(Target,true,false,1.1,3.f));
 TestFalse(TEXT("Shield absorption never merges into healing"), Heal.TryAccumulate(Target,false,true,1.1,3.f));
 FEGActiveCombatNumber Shield; Shield.TargetId=Target; Shield.bShield=true; Shield.StartedAt=1.; Shield.Amount=5.;
 TestTrue(TEXT("Same-target shield absorption can merge"), Shield.TryAccumulate(Target,false,true,1.1,3.f));
 TestFalse(TEXT("Health damage never merges into shield absorption"), Shield.TryAccumulate(Target,false,false,1.1,3.f));

 TArray<FEGActiveCombatNumber> Lanes;
 const double Starts[]={0.,0.,0.,.13,.26,.39};
 const float Durations[]={.85f,.95f,.85f,.95f,.85f,.95f};
 const float Preferred[]={-12.f,-12.f,20.f,-12.f,20.f,-12.f};
 for(int32 Index=0;Index<6;++Index)
 {
  const float Offset=FEGActiveCombatNumber::ChooseStartOffsetY(Lanes,Target,Starts[Index],Durations[Index],Preferred[Index]);
  for(const auto& Existing:Lanes)
  {
   const double First=Starts[Index];
   const double Last=FMath::Min(Existing.StartedAt+Existing.Duration,Starts[Index]+Durations[Index]);
   if(Last<=First)continue;
   const auto Y=[&](double At){return Existing.StartOffsetY-44.*(At-Existing.StartedAt)/Existing.Duration;};
   const auto NewY=[&](double At){return Offset-44.*(At-Starts[Index])/Durations[Index];};
   const double DifferenceAtStart=NewY(First)-Y(First),DifferenceAtEnd=NewY(Last)-Y(Last);
   TestTrue(TEXT("Distinct numbers stay separated for the entire shared lifetime"),
    FMath::Abs(DifferenceAtStart)>=33.98&&FMath::Abs(DifferenceAtEnd)>=33.98&&DifferenceAtStart*DifferenceAtEnd>0.);
  }
  auto& Added=Lanes.AddDefaulted_GetRef();Added.TargetId=Target;Added.StartedAt=Starts[Index];
  Added.Duration=Durations[Index];Added.StartOffsetY=Offset;Added.Amount=1.;
  Added.bHealing=Index==1;Added.bShield=Index==2;
 }
 TestEqual(TEXT("Other targets do not occupy this target's lane"),
  FEGActiveCombatNumber::ChooseStartOffsetY(Lanes,Other,.39,.85f,-12.f),-12.f);
 const float BeforeMerge=Lanes.Last().StartOffsetY;
 TestTrue(TEXT("Last same-target hit merges in its own window"),Lanes.Last().TryAccumulate(Target,false,false,.40,2.f));
 TestEqual(TEXT("Merging does not move an established lane"),Lanes.Last().StartOffsetY,BeforeMerge);

 FVector2D RightStart,LeftStart,Peak,Exit;float StartScale,LeftScale,PeakScale,ExitScale,StartAlpha,LeftAlpha,PeakAlpha,ExitAlpha;
 FEGActiveCombatNumber::SampleEnemyDamage(0.f,1,RightStart,StartScale,StartAlpha);
 FEGActiveCombatNumber::SampleEnemyDamage(0.f,-1,LeftStart,LeftScale,LeftAlpha);
 FEGActiveCombatNumber::SampleEnemyDamage(196.f/416.f,1,Peak,PeakScale,PeakAlpha);
 FEGActiveCombatNumber::SampleEnemyDamage(1.f,1,Exit,ExitScale,ExitAlpha);
 TestTrue(TEXT("Enemy damage starts on opposite sides"),RightStart.X>0.f&&LeftStart.X<0.f&&FMath::IsNearlyEqual(RightStart.X,-LeftStart.X));
 TestTrue(TEXT("Damage rises and enlarges near its apex"),Peak.Y<RightStart.Y&&PeakScale>StartScale&&FMath::IsNearlyEqual(PeakAlpha,1.f));
 TestTrue(TEXT("Damage exits sideways and downward while shrinking and fading"),Exit.X>Peak.X&&Exit.Y>RightStart.Y&&ExitScale<PeakScale&&FMath::IsNearlyZero(ExitAlpha));
 TArray<FEGActiveCombatNumber> EnemyLanes;
 auto& FirstEnemy=EnemyLanes.AddDefaulted_GetRef();
 FirstEnemy.TargetId=Target;FirstEnemy.bEnemyDamage=true;FirstEnemy.Direction=1;FirstEnemy.Duration=.72f;FirstEnemy.StartedAt=0.;FirstEnemy.StartOffsetY=-8.f;
 TestEqual(TEXT("Same-side enemy damage uses a nearby stagger"),FEGActiveCombatNumber::ChooseEnemyStartOffsetY(EnemyLanes,Target,0.,1),-28.f);
 TestEqual(TEXT("Opposite-side enemy damage can share its height"),FEGActiveCombatNumber::ChooseEnemyStartOffsetY(EnemyLanes,Target,0.,-1),-8.f);
 TestEqual(TEXT("Enemy damage does not push healing into distant linear lanes"),FEGActiveCombatNumber::ChooseStartOffsetY(EnemyLanes,Target,0.,.95f,-12.f),-12.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionSkillTextContract,
 "ElyGambit.UI.InspectionRedesign.SkillText", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGInspectionSkillTextContract::RunTest(const FString&)
{
 TStrongObjectPtr<UEGHeroDefinition> Hero(NewObject<UEGHeroDefinition>());
 TStrongObjectPtr<UEGElysiaSkillDefinition> Elysia(NewObject<UEGElysiaSkillDefinition>());
 Hero->HeroKind=EEGHeroKind::Elysia; Hero->SkillDefinition=Elysia.Get();
 Elysia->NormalAttackMultiplier=1.37f; Elysia->ChargedAttackInterval=7; Elysia->ChargedMultiplier=2.41f;
 Elysia->bArrowRainUltimate=true; Elysia->ArrowRainSeconds=3.5f; Elysia->ArrowRainPulses=7;
 const FString Basic=Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Basic).ToString();
 const FString Passive=Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString();
 const FString Ultimate=Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Ultimate).ToString();
 TestTrue(TEXT("Elysia basic uses the current data asset value"), Basic.Contains(TEXT("137%")));
 TestTrue(TEXT("Elysia passive uses the current interval and multiplier"), Passive.Contains(TEXT("7次")) && Passive.Contains(TEXT("241%")));
 TestTrue(TEXT("Elysia ultimate uses current duration and pulse count"), Ultimate.Contains(TEXT("3.5秒")) && Ultimate.Contains(TEXT("0.5秒")));
 Elysia->NormalAttackMultiplier=1.69f;
 TestTrue(TEXT("Changing the authoritative configuration changes displayed basic"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Basic).ToString().Contains(TEXT("169%")));
 Elysia->bArrowRainUltimate=false; Elysia->EmpoweredSeconds=9.5f;
 TestTrue(TEXT("The alternate authored ultimate is shown when configured"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Ultimate).ToString().Contains(TEXT("9.5秒")));
 for(const EEGSkillInfoKind Kind:{EEGSkillInfoKind::Basic,EEGSkillInfoKind::Ultimate,EEGSkillInfoKind::Passive})
  TestTrue(TEXT("Combined Elysia description preserves each current section"), Hero->GetPlayerFacingSkillEffect().ToString().Contains(Hero->GetPlayerFacingSkillSection(Kind).ToString()));

 TStrongObjectPtr<UEGElysiaHerrscherSkillDefinition> Human(NewObject<UEGElysiaHerrscherSkillDefinition>());
 Hero->HeroKind=EEGHeroKind::ElysiaHerrscher; Hero->SkillDefinition=Human.Get();
 Human->BowAttack=1.43f; Human->ShieldHealthRatio=.37f; Human->OriginSeconds=9.5f;
 TestTrue(TEXT("Human basic uses Human's own configuration"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Basic).ToString().Contains(TEXT("143%")));
 TestTrue(TEXT("Human passive uses Human's current shield ratio"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString().Contains(TEXT("37%")));
 TestTrue(TEXT("Human ultimate uses Human's current origin duration"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Ultimate).ToString().Contains(TEXT("9.5秒")));
 Human->ShieldHealthRatio=.41f;
 TestTrue(TEXT("Changing Human shield configuration updates passive"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString().Contains(TEXT("41%")));
 for(const EEGSkillInfoKind Kind:{EEGSkillInfoKind::Basic,EEGSkillInfoKind::Ultimate,EEGSkillInfoKind::Passive})
  TestTrue(TEXT("Combined Human description preserves each current section"), Hero->GetPlayerFacingSkillEffect().ToString().Contains(Hero->GetPlayerFacingSkillSection(Kind).ToString()));
 Hero->HeroKind=EEGHeroKind::Generic; Hero->SkillDefinition=nullptr;
 Hero->BasicDescription=FText::FromString(TEXT("普通攻击说明"));
 Hero->UltimateDescription=FText::GetEmpty();
 Hero->PassiveDescription=FText::FromString(TEXT("被动说明"));
 TestEqual(TEXT("Other heroes retain authored basic text"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Basic).ToString(), FString(TEXT("普通攻击说明")));
 TestTrue(TEXT("Missing ultimate description remains unknown rather than asserting absent skill"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Ultimate).IsEmpty());
 TestEqual(TEXT("Other heroes retain authored passive text"), Hero->GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString(), FString(TEXT("被动说明")));
 return true;
}

namespace
{
bool InspectionHasAncestor(const UWidget* Child,const UWidget* Ancestor)
{
 for(const UWidget* Parent=Child?Child->GetParent():nullptr;Parent;Parent=Parent->GetParent())
  if(Parent==Ancestor)return true;
 return false;
}
void CheckInspectionTree(FAutomationTestBase& Test,UWidgetTree* Tree,const TCHAR* Label)
{
 if(!Test.TestNotNull(FString::Printf(TEXT("%s tree"),Label),Tree))return;
 auto* Surface=Cast<UEGGradientSurface>(Tree->FindWidget(TEXT("UnitInfoSurface")));
 auto* Card=Cast<USizeBox>(Tree->FindWidget(TEXT("InfoCardSize")));
 auto* Scroll=Cast<UScrollBox>(Tree->FindWidget(TEXT("UnitInfoScroll")));
 auto* Content=Tree->FindWidget(TEXT("UnitInfoBody"));
 auto* Actions=Tree->FindWidget(TEXT("SkillActionRow"));
 auto* Details=Tree->FindWidget(TEXT("ActionDetails"));
 auto* Body=Tree->FindWidget(TEXT("ActionDetailsBody"));
 auto* Heading=Tree->FindWidget(TEXT("CharacterName"));
 auto* Sell=Tree->FindWidget(TEXT("Button_SellInfo"));
 if(Test.TestNotNull(FString::Printf(TEXT("%s continuous surface"),Label),Surface))
 {
  Test.TestTrue(TEXT("One horizontal black-to-transparent gradient"), Surface->bHorizontalGradient && FMath::IsNearlyEqual(Surface->TopColor.A,.88f,.001f) && FMath::IsNearlyZero(Surface->BottomColor.A));
  Test.TestEqual(TEXT("Gradient cannot intercept clicks"),Surface->GetVisibility(),ESlateVisibility::HitTestInvisible);
 }
 int32 Gradients=0;Tree->ForEachWidget([&](UWidget* Widget){if(Cast<UEGGradientSurface>(Widget))++Gradients;});
 Test.TestEqual(FString::Printf(TEXT("%s has no second overlapping gradient"),Label),Gradients,1);
 if(Test.TestNotNull(TEXT("Card exists"),Card))
  Test.TestTrue(TEXT("Card retains complete 200x280 aspect"), FMath::IsNearlyEqual(Card->GetWidthOverride(),200.f) && FMath::IsNearlyEqual(Card->GetHeightOverride(),280.f));
 Test.TestNotNull(TEXT("Content scroll exists"),Scroll);
 Test.TestNotNull(TEXT("Character body exists"),Content);
 Test.TestNotNull(TEXT("Skill row exists"),Actions);
 Test.TestNotNull(TEXT("Character heading remains visible"),Heading);
 Test.TestNotNull(TEXT("Fixed sale action exists"),Sell);
 if(Test.TestNotNull(TEXT("Details container exists"),Details))
 {
  Test.TestEqual(TEXT("Details start closed"), Details->GetVisibility(), ESlateVisibility::Collapsed);
  Test.TestTrue(TEXT("Details body is in details container"),InspectionHasAncestor(Body,Details));
  if(auto* Slot=Cast<UCanvasPanelSlot>(Details->Slot))
   Test.TestTrue(TEXT("Details begin at 376+12 and are 420 wide"),FMath::IsNearlyEqual(Slot->GetPosition().X,388.f) && FMath::IsNearlyEqual(Slot->GetSize().X,420.f));
  else Test.AddError(TEXT("Details must use a canvas slot"));
 }
 for(const TCHAR* Name:{TEXT("Button_BasicInfo"),TEXT("Button_UltimateInfo"),TEXT("Button_PassiveInfo")})
 {
  auto* Button=Cast<UButton>(Tree->FindWidget(Name));
  if(Test.TestNotNull(FString::Printf(TEXT("%s skill button %s"),Label,Name),Button))
  {
   Test.TestTrue(TEXT("Skill buttons belong to the action row"),InspectionHasAncestor(Button,Actions));
   Test.TestFalse(TEXT("Skill buttons remain outside the inline-hidden body"),InspectionHasAncestor(Button,Content));
  }
 }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionDetailsLayoutContract,
 "ElyGambit.UI.InspectionRedesign.NativeAndSavedLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGInspectionDetailsLayoutContract::RunTest(const FString&)
{
 TStrongObjectPtr<UWidgetTree> Native(NewObject<UWidgetTree>());
 UEGHeroInfoWidget::BuildPresentation(Native.Get(),EEGHeroInfoPresentation::UnitDetails);
 CheckInspectionTree(*this,Native.Get(),TEXT("Native"));
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();
 UWorld* World=GI->GetWorld();
 ON_SCOPE_EXIT { World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
 auto* SavedClass=LoadClass<UEGHeroInfoWidget>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_UnitPinnedInfo.WBP_UnitPinnedInfo_C"));
 if(!TestNotNull(TEXT("Actual saved pinned widget class"),SavedClass))return false;
 TStrongObjectPtr<UEGHeroInfoWidget> Saved(CreateWidget<UEGHeroInfoWidget>(GI.Get(),SavedClass));
 if(!TestNotNull(TEXT("Actual saved pinned widget instance"),Saved.Get()))return false;
 CheckInspectionTree(*this,Saved->WidgetTree,TEXT("Saved"));
 const TSharedRef<SWidget> KeepSlate=Saved->TakeWidget();
 TStrongObjectPtr<UEGHeroDefinition> Hero(NewObject<UEGHeroDefinition>());
 Hero->HeroKind=EEGHeroKind::Generic;Hero->BasicDescription=FText::FromString(TEXT("普攻测试"));
 Hero->UltimateDescription=FText::FromString(TEXT("大招测试"));Hero->PassiveDescription=FText::FromString(TEXT("被动测试"));
 Saved->Present(Hero.Get(),FText::GetEmpty());
 auto* Basic=Cast<UButton>(Saved->GetWidgetFromName(TEXT("Button_BasicInfo")));
 auto* Ultimate=Cast<UButton>(Saved->GetWidgetFromName(TEXT("Button_UltimateInfo")));
 auto* Scroll=Saved->GetWidgetFromName(TEXT("UnitInfoScroll"));
 auto* Content=Saved->GetWidgetFromName(TEXT("UnitInfoBody"));
 auto* Details=Saved->GetWidgetFromName(TEXT("ActionDetails"));
 if(!TestNotNull(TEXT("Saved basic button is bound"),Basic)||!TestNotNull(TEXT("Saved ultimate button is bound"),Ultimate)||
    !TestNotNull(TEXT("Saved content scroll is bound"),Scroll)||!TestNotNull(TEXT("Saved character body is bound"),Content)||
    !TestNotNull(TEXT("Saved details container is bound"),Details))return false;
 TestFalse(TEXT("No skill open initially"),Saved->HasSkillDetailsOpen());
 Basic->OnClicked.Broadcast();
 TestTrue(TEXT("Clicking basic opens skill details"),Saved->HasSkillDetailsOpen());
 TestEqual(TEXT("Wide layout includes adjacent details"),Saved->GetSkillDetailsWidth(1920.f),808.f);
 Saved->SetSkillLayoutWidth(1024.f);
 TestEqual(TEXT("Narrow layout uses sidebar width"),Saved->GetSkillDetailsWidth(1024.f),376.f);
 TestEqual(TEXT("Narrow layout hides character body while retaining scroll and skill row"),Content->GetVisibility(),ESlateVisibility::Collapsed);
 TestEqual(TEXT("Narrow layout retains the skill scroll"),Scroll->GetVisibility(),ESlateVisibility::Visible);
 TestEqual(TEXT("Basic remains visible in narrow detail layout"),Basic->GetVisibility(),ESlateVisibility::Visible);
 TestEqual(TEXT("Ultimate remains visible in narrow detail layout"),Ultimate->GetVisibility(),ESlateVisibility::Visible);
 Ultimate->OnClicked.Broadcast();
 TestTrue(TEXT("Selecting another skill keeps details open"),Saved->HasSkillDetailsOpen());
 Ultimate->OnClicked.Broadcast();
 TestFalse(TEXT("Selecting the current skill closes details"),Saved->HasSkillDetailsOpen());
 TestEqual(TEXT("Closing restores character body"),Content->GetVisibility(),ESlateVisibility::SelfHitTestInvisible);
 Basic->OnClicked.Broadcast();
 Saved->Present(Hero.Get(),FText::FromString(TEXT("updated health")));
 TestTrue(TEXT("Same-character refresh preserves selected details"),Saved->HasSkillDetailsOpen());
 FEGUnitInspectionView BossSnapshot;
 BossSnapshot.bHasSkillOverride=true;BossSnapshot.SkillBasicOverride=FText::FromString(TEXT("首阶段机制"));
 Saved->PresentAttributes(BossSnapshot);
 auto* Body=Cast<UEGRichTextBlock>(Saved->GetWidgetFromName(TEXT("ActionDetailsBody")));
 if(TestNotNull(TEXT("Skill body supports dynamic role override"),Body))
  TestEqual(TEXT("Role override replaces generic hero text"),Body->GetPlainText().ToString(),FString(TEXT("首阶段机制")));
 BossSnapshot.SkillBasicOverride=FText::FromString(TEXT("第二阶段机制"));
 Saved->PresentAttributes(BossSnapshot);
 if(Body)TestEqual(TEXT("Stage update refreshes currently open skill"),Body->GetPlainText().ToString(),FString(TEXT("第二阶段机制")));
 TestTrue(TEXT("Stage update keeps selected skill open"),Saved->HasSkillDetailsOpen());
 BossSnapshot.bSummonUltimateUnavailable=true;Saved->PresentAttributes(BossSnapshot);
 TestFalse(TEXT("Summon without an independent ultimate disables ultimate button"),Ultimate->GetIsEnabled());
 TestFalse(TEXT("Unavailable ultimate explains its disabled state"),Ultimate->GetToolTipText().IsEmpty());
 BossSnapshot.bSummonUltimateUnavailable=false;Saved->PresentAttributes(BossSnapshot);
 TestTrue(TEXT("An enabled ultimate is restored after role changes"),Ultimate->GetIsEnabled());
 TStrongObjectPtr<UEGMonsterAttackDefinition> Monster(NewObject<UEGMonsterAttackDefinition>());
 Hero->MonsterAttack=Monster.Get();BossSnapshot.Definition=Hero.Get();BossSnapshot.bHasSkillOverride=false;
 Monster->Ultimate=EEGMonsterUltimate::None;Saved->PresentAttributes(BossSnapshot);
 TestFalse(TEXT("Monster explicitly configured without ultimate is disabled"),Ultimate->GetIsEnabled());
 TestFalse(TEXT("Monster without ultimate explains its disabled state"),Ultimate->GetToolTipText().IsEmpty());
 Monster->Ultimate=EEGMonsterUltimate::Shield;Saved->PresentAttributes(BossSnapshot);
 TestTrue(TEXT("Monster with configured ultimate remains enabled"),Ultimate->GetIsEnabled());
 Hero->MonsterAttack=nullptr;
 FEGUnitInspectionView Enemy;Enemy.Definition=Hero.Get();Enemy.bEnemy=true;Enemy.BossArtworkPhase=1;
 const TCHAR* Ids[]={TEXT("arrow"),TEXT("pulse"),TEXT("barrier"),TEXT("mark"),TEXT("spread"),TEXT("mimic"),TEXT("enrage")};
 const TCHAR* IconNames[]={TEXT("T_Boss_Arrow"),TEXT("T_Boss_Pulse"),TEXT("T_Boss_Barrier"),TEXT("T_Boss_Mark"),TEXT("T_Boss_Spread"),TEXT("T_Boss_Mimic"),TEXT("T_Boss_Enrage")};
 for(int32 I=0;I<UE_ARRAY_COUNT(Ids);++I)Enemy.EnemySkills.Add({FName(Ids[I]),FText::FromString(Ids[I]),FText::FromString(FString::Printf(TEXT("敌方技能%d"),I)),FString(TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/"))+IconNames[I]});
 Saved->PresentAttributes(Enemy);
 auto* Row=Cast<UWrapBox>(Saved->GetWidgetFromName(TEXT("SkillActionRow")));
 if(TestNotNull(TEXT("Dynamic enemy skill row"),Row))
 {
  const int32 Children=Row->GetChildrenCount();TestTrue(TEXT("All seven skills are represented"),Children>=10);
  const TCHAR* GroupNames[]={TEXT("EnemySkillGroup_0"),TEXT("EnemySkillGroup_1"),TEXT("EnemySkillGroup_2")};
  const TCHAR* GroupTitles[]={TEXT("第一阶段"),TEXT("第二阶段"),TEXT("全场")};
  for(int32 I=0;I<UE_ARRAY_COUNT(GroupNames);++I)
  {
   auto* Header=Cast<USizeBox>(Saved->GetWidgetFromName(GroupNames[I]));
   auto* Text=Header?Cast<UTextBlock>(Header->GetContent()):nullptr;
   if(TestNotNull(TEXT("Enemy skill group title"),Text))TestEqual(TEXT("Enemy skill group uses a separate heading"),Text->GetText().ToString(),FString(GroupTitles[I]));
  }
  Saved->OpenEnemySkill(6);
  if(Body)TestEqual(TEXT("Seventh skill has its own description"),Body->GetPlainText().ToString(),FString(TEXT("敌方技能6")));
  Enemy.BossArtworkPhase=2;Saved->PresentAttributes(Enemy);
  TestTrue(TEXT("Phase change preserves selected enemy skill"),Saved->HasSkillDetailsOpen());
  if(Body)TestEqual(TEXT("Phase change keeps the selected skill description"),Body->GetPlainText().ToString(),FString(TEXT("敌方技能6")));
  for(int32 I=0;I<20;++I)Saved->PresentAttributes(Enemy);
  TestEqual(TEXT("Repeated refresh does not create more skill buttons"),Row->GetChildrenCount(),Children);
  if(auto* Icon=Cast<UImage>(Saved->GetWidgetFromName(TEXT("EnemyInspectionIcon_6"))))
  {
   TestNotNull(TEXT("Enemy icons use the dedicated atlas"),Icon->GetBrush().GetResourceObject());
   const FBox2f UV=Icon->GetBrush().GetUVRegion();
   TestTrue(TEXT("Seventh skill uses the enrage atlas cell"),UV.bIsValid&&
    FMath::IsNearlyEqual(UV.Min.X,.5f)&&FMath::IsNearlyEqual(UV.Min.Y,.75f)&&
    FMath::IsNearlyEqual(UV.Max.X,.75f)&&FMath::IsNearlyEqual(UV.Max.Y,1.f));
  }
  else AddError(TEXT("Seventh enemy skill icon is missing"));
 }
 Saved->PresentAttributes(FEGUnitInspectionView());
 TestEqual(TEXT("Player skill buttons return after inspecting an enemy"),Basic->GetParent()->GetVisibility(),ESlateVisibility::Visible);
 TStrongObjectPtr<UEGHeroDefinition> Other(NewObject<UEGHeroDefinition>());
 Saved->Present(Other.Get(),FText::GetEmpty());
 TestFalse(TEXT("Changing character clears the previous skill selection"),Saved->HasSkillDetailsOpen());
 TestEqual(TEXT("Changing character closes old details"),Details->GetVisibility(),ESlateVisibility::Collapsed);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionFormalSkillIconBindings,
 "ElyGambit.UI.InspectionRedesign.FormalSkillIconBindings", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGInspectionFormalSkillIconBindings::RunTest(const FString&)
{
 // HeroDefinition paths were read back from the production assets before binding.
 const TCHAR* Paths[]={
  TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Mobius.DA_Mobius"),
  TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV.DA_VillV"),
  TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Human.DA_Human"),
  TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_CollapsedHua.DA_CollapsedHua"),
  TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua.DA_Hua"),
  TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo.DA_Pardo"),
  TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Griseo.DA_Griseo"),
  TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Sakura.DA_Sakura"),
  TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_GriseoAdult.DA_GriseoAdult"),
  TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"),
  TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Eden.DA_Eden"),
  TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Aponia.DA_Aponia"),
  TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia")
 };
 for(const TCHAR* Path:Paths)
 {
  auto* Hero=LoadObject<UEGHeroDefinition>(nullptr,Path);
  if(!TestNotNull(FString::Printf(TEXT("Formal hero %s"),Path),Hero))continue;
  auto* Card=Hero->CardInformation.Get();
  if(!TestNotNull(FString::Printf(TEXT("Formal card info for %s"),Path),Card))continue;
  const TSoftObjectPtr<UTexture2D> Icons[]={Card->BasicIcon,Card->UltimateIcon,Card->PassiveIcon};
  const TCHAR* Kinds[]={TEXT("Basic"),TEXT("Ultimate"),TEXT("Passive")};
  for(int32 Index=0;Index<3;++Index)
  {
   const FString Label=FString::Printf(TEXT("%s %s"),Path,Kinds[Index]);
   if(!TestFalse(Label+TEXT(" has an explicit original texture reference"),Icons[Index].IsNull()))continue;
   auto* Texture=Icons[Index].LoadSynchronous();
   if(!TestNotNull(Label+TEXT(" texture loads"),Texture))continue;
#if WITH_EDITOR
   const int32 BeforeWidth=Texture->GetSizeX(),BeforeHeight=Texture->GetSizeY();
   const bool bWasCompiling=Texture->IsCompiling();
   UTexture* PendingTextures[]={Texture};
   FTextureCompilingManager::Get().FinishCompilation(PendingTextures);
   AddInfo(FString::Printf(TEXT("%s: before %dx%d compiling=%d, completed %dx%d, source %lldx%lld"),*Label,
    BeforeWidth,BeforeHeight,bWasCompiling?1:0,Texture->GetSizeX(),Texture->GetSizeY(),
    static_cast<long long>(Texture->Source.GetSizeX()),static_cast<long long>(Texture->Source.GetSizeY())));
#else
   AddInfo(FString::Printf(TEXT("%s: runtime %dx%d"),*Label,Texture->GetSizeX(),Texture->GetSizeY()));
#endif
   TestTrue(FString::Printf(TEXT("%s texture is square and at least 64 pixels (actual %dx%d)"),*Label,Texture->GetSizeX(),Texture->GetSizeY()),Texture->GetSizeX()>=64&&Texture->GetSizeY()>=64&&Texture->GetSizeX()==Texture->GetSizeY());
   TestTrue(Label+TEXT(" texture belongs to UI skill icon assets"),Texture->GetPathName().StartsWith(TEXT("/Game/Assets/UI/SkillIcons/")));
  }
 }
 TestNotNull(TEXT("Dedicated enemy skill atlas loads"),LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/T_EnemySkillAtlas.T_EnemySkillAtlas")));
 AddInfo(TEXT("This verifies production references and decoded dimensions, not whether the source mapping or licensing is correct; compare the asset provenance record separately."));
 return true;
}
#endif
