#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/EGHeroGrowthSettings.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGStigmataBalance.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "EGProductionTestAssets.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHeroGrowthSegmentsTest,"ElyGambit.HeroGrowth.SegmentsAndIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHeroGrowthSegmentsTest::RunTest(const FString&)
{
 const auto& Rules=UEGHeroGrowthSettings::Get();
 const EEGHeroKind Playable[]={EEGHeroKind::Elysia,EEGHeroKind::Hua,EEGHeroKind::CollapsedHua,
  EEGHeroKind::Pardofelis,EEGHeroKind::Eden,EEGHeroKind::Sakura,EEGHeroKind::VillV,
  EEGHeroKind::Griseo,EEGHeroKind::GriseoAdult,EEGHeroKind::Mobius,EEGHeroKind::Aponia,EEGHeroKind::ElysiaHerrscher};
 for(EEGHeroKind Kind:Playable)TestNotNull(TEXT("Every playable kind has a growth profile"),Rules.FindProfile(Kind));
 TestTrue(TEXT("Negative level has no growth"),FMath::IsNearlyZero(Rules.WeightedLevels(-4,1.2f,.75f)));
 TestTrue(TEXT("First five growth increments unmodified"),FMath::IsNearlyEqual(Rules.WeightedLevels(6,1.2f,.75f),5.f));
 TestTrue(TEXT("Middle interval reaches its own weighted endpoint"),FMath::IsNearlyEqual(Rules.WeightedLevels(11,1.2f,.75f),11.f));
 TestTrue(TEXT("High levels continue growing beyond 11"),FMath::IsNearlyEqual(Rules.WeightedLevels(21,1.2f,.75f),18.5f));
 TStrongObjectPtr<UEGHeroDefinition> Hero(NewObject<UEGHeroDefinition>());
 Hero->HeroKind=EEGHeroKind::Elysia;
 Hero->BaseStats.MaxHealth=100.f;Hero->BaseStats.AttackPower=10.f;Hero->BaseStats.Defense=5.f;Hero->BaseStats.MaxMana=100.f;Hero->BaseStats.AttackSpeed=1.f;
 Hero->GrowthPerLevel.MaxHealth=10.f;Hero->GrowthPerLevel.AttackPower=2.f;Hero->GrowthPerLevel.Defense=1.f;Hero->GrowthPerLevel.MaxMana=3.f;Hero->GrowthPerLevel.AttackSpeed=.01f;
 const FEGHeroGrowthProfile* Profile=Rules.FindProfile(Hero->HeroKind);
 if(!TestNotNull(TEXT("Elysia profile"),Profile))return false;
 const FEGStatBlock At6=Hero->EvaluateStats(6),At21=Hero->EvaluateStats(21);
 TestTrue(TEXT("Opening levels keep authored health growth"),FMath::IsNearlyEqual(At6.MaxHealth,150.f));
 TestTrue(TEXT("Health uses health curve"),FMath::IsNearlyEqual(At21.MaxHealth,100.f+10.f*Rules.WeightedLevels(21,Profile->HealthMiddle,Profile->HealthLate)));
 TestTrue(TEXT("Attack uses attack curve"),FMath::IsNearlyEqual(At21.AttackPower,10.f+2.f*Rules.WeightedLevels(21,Profile->AttackMiddle,Profile->AttackLate)));
 TestTrue(TEXT("Defense uses defense curve"),FMath::IsNearlyEqual(At21.Defense,5.f+Rules.WeightedLevels(21,Profile->DefenseMiddle,Profile->DefenseLate)));
 TestTrue(TEXT("Mana is not multiplied by health or attack curve"),FMath::IsNearlyEqual(At21.MaxMana,160.f));
 TestTrue(TEXT("Speed is not multiplied by health or attack curve"),FMath::IsNearlyEqual(At21.AttackSpeed,1.2f));
 Hero->HeroKind=EEGHeroKind::Generic;
 TestTrue(TEXT("Unlisted NPC retains linear growth"),FMath::IsNearlyEqual(Hero->EvaluateStats(21).MaxHealth,300.f));
 Hero->HeroKind=EEGHeroKind::Pardofelis;
 Hero->BaseStats.Defense=0.f;Hero->GrowthPerLevel.Defense=0.f;
 const FEGHeroGrowthProfile* Pardo=Rules.FindProfile(Hero->HeroKind);
 if(TestNotNull(TEXT("Pardo profile"),Pardo))
 {
  TestTrue(TEXT("Pardo defense correction starts at level one"),FMath::IsNearlyEqual(Hero->EvaluateStats(1).Defense,FMath::Max(0.f,Pardo->BaseDefenseBonus)));
  TestTrue(TEXT("Pardo defense correction grows with level"),Hero->EvaluateStats(11).Defense>Hero->EvaluateStats(1).Defense);
 }
 Hero->HeroKind=EEGHeroKind::ElysiaHerrscher;
 Hero->GrowthPerLevel=UEGHeroStatsDefinition::ZeroGrowth();
 TestTrue(TEXT("Zero-authored Human still grows at high levels"),Hero->EvaluateStats(81).MaxHealth>Hero->EvaluateStats(21).MaxHealth);
 TestTrue(TEXT("Zero-authored Human still gains attack at high levels"),Hero->EvaluateStats(81).AttackPower>Hero->EvaluateStats(21).AttackPower);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteGrowthCopyTest,"ElyGambit.HeroGrowth.ElitePrivateCopy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteGrowthCopyTest::RunTest(const FString&)
{
 const auto& Rules=UEGHeroGrowthSettings::Get();const auto& Old=UEGStigmataBalanceSettings::Get();
 const int32 Expected3=FMath::Max(FMath::Max(1,Rules.Stage3MinimumLevel),1+FMath::Max(0,Rules.Stage3ExtraLevel)+FMath::Max(0,Old.CopyLevelBonusMid));
 TestEqual(TEXT("Stage 3 level floor, bonus and mid corruption"),Rules.ResolveEliteLevel(3,1,50),Expected3);
 TestTrue(TEXT("Stage 6 does not lower an already above-cap source"),Rules.ResolveEliteLevel(6,25,90)>=25);
 TestEqual(TEXT("Level saturation cannot overflow"),Rules.ResolveEliteLevel(6,MAX_int32,90),MAX_int32);
 TestEqual(TEXT("Non elite stages keep valid source level"),Rules.ResolveEliteLevel(4,19,50),19);
 TStrongObjectPtr<UEGHeroDefinition> Source(NewObject<UEGHeroDefinition>());
 Source->HeroKind=EEGHeroKind::Sakura;Source->bUseSplitConfiguration=true;
 Source->StatsDefinition=NewObject<UEGHeroStatsDefinition>(Source.Get());
 Source->StatsDefinition->BaseStats.MaxHealth=100.f;Source->StatsDefinition->BaseStats.AttackPower=20.f;
 Source->StatsDefinition->GrowthPerLevel.MaxHealth=10.f;Source->StatsDefinition->GrowthPerLevel.AttackPower=2.f;
 TStrongObjectPtr<UEGHeroDefinition> Copy(Rules.MakeEliteDefinition(GetTransientPackage(),Source.Get(),6));
 if(!TestNotNull(TEXT("Private elite definition"),Copy.Get()))return false;
 if(!TestNotNull(TEXT("Private elite stats"),Copy->StatsDefinition.Get()))return false;
 TestTrue(TEXT("Hero copy is distinct"),Copy.Get()!=Source.Get());
 TestTrue(TEXT("Stats copy is distinct"),Copy->StatsDefinition!=Source->StatsDefinition);
 TestTrue(TEXT("Source base remains untouched"),FMath::IsNearlyEqual(Source->StatsDefinition->BaseStats.MaxHealth,100.f));
 TestTrue(TEXT("Source growth remains untouched"),FMath::IsNearlyEqual(Source->StatsDefinition->GrowthPerLevel.AttackPower,2.f));
 const float HP=FMath::IsFinite(Rules.Stage6HealthMultiplier)?FMath::Max(.01f,Rules.Stage6HealthMultiplier):1.f;
 const float Attack=FMath::IsFinite(Rules.Stage6AttackMultiplier)?FMath::Max(.01f,Rules.Stage6AttackMultiplier):1.f;
 TestTrue(TEXT("Health multiplier applies at high levels"),FMath::IsNearlyEqual(Copy->EvaluateStats(21).MaxHealth,Source->EvaluateStats(21).MaxHealth*HP,.01f));
 TestTrue(TEXT("Attack multiplier applies at high levels"),FMath::IsNearlyEqual(Copy->EvaluateStats(21).AttackPower,Source->EvaluateStats(21).AttackPower*Attack,.01f));
 TestEqual(TEXT("Non elite stages cannot accidentally create a buffed copy"),Rules.MakeEliteDefinition(GetTransientPackage(),Source.Get(),4),static_cast<UEGHeroDefinition*>(nullptr));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHeroGrowthProductionTableTest,"ElyGambit.HeroGrowth.ProductionTable",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHeroGrowthProductionTableTest::RunTest(const FString&)
{
 UEGRunDefinition* Seed=EGProductionTestAssets::ResolveRun(*this);
 if(!Seed)return false;
 TStrongObjectPtr<UEGRunDefinition> Run(EGSevenStage::Build(GetTransientPackage(),Seed));
 if(!TestNotNull(TEXT("Production seven-stage run"),Run.Get()))return false;
 if(TestTrue(TEXT("Production first stage exists"),Run->Stages.IsValidIndex(0)))
 {
  const UEGHeroDefinition* Chest=nullptr;
  for(const auto& Monster:Run->Stages[0].OrdinaryMonsterDefinitions)
   if(Monster&&Monster->MonsterAttack&&Monster->MonsterAttack->Ultimate==EEGMonsterUltimate::Shield){Chest=Monster.Get();break;}
  if(TestNotNull(TEXT("First-stage chest"),Chest))
  {
   const auto& Health=UEGStigmataBalanceSettings::Get().OrdinaryStageHealthMultipliers;
   const float Scale=Health.IsValidIndex(0)?FMath::Max(.01f,Health[0]):1.f;
   TestTrue(TEXT("Monster HP is frozen to formal pre-rebalance baseline"),FMath::IsNearlyEqual(Chest->GetBaseStats().MaxHealth,950.f*1.4f*Scale,.01f));
   TestTrue(TEXT("Monster HP growth is frozen to formal pre-rebalance baseline"),FMath::IsNearlyEqual(Chest->GetGrowthPerLevel().MaxHealth,65.f*1.4f*Scale,.01f));
   TestTrue(TEXT("Monster attack is frozen to formal pre-rebalance baseline"),FMath::IsNearlyEqual(Chest->GetBaseStats().AttackPower,100.f*.75f,.01f));
  }
 }
 TSet<const UEGHeroDefinition*> Reported;
 auto Report=[&](const UEGHeroDefinition* Hero,const TCHAR* Note)
 {
  if(!Hero||Reported.Contains(Hero))return;
  Reported.Add(Hero);
  const auto& Growth=Hero->GetGrowthPerLevel();
  AddInfo(FString::Printf(TEXT("%s %s Kind=%d authored_growth=(HP %.3f, ATK %.3f, DEF %.3f, Mana %.3f, Speed %.3f) SkillTierStep=%d"),
   Note,*Hero->GetPathName(),int32(Hero->HeroKind),Growth.MaxHealth,Growth.AttackPower,Growth.Defense,Growth.MaxMana,Growth.AttackSpeed,Hero->GetSkillTierStep()));
  for(const int32 Level:{1,6,11,21,51})
  {
   const auto Stats=Hero->EvaluateStats(Level);
   AddInfo(FString::Printf(TEXT("  L%d HP=%.3f ATK=%.3f DEF=%.3f Mana=%.3f Speed=%.3f"),
    Level,Stats.MaxHealth,Stats.AttackPower,Stats.Defense,Stats.MaxMana,Stats.AttackSpeed));
   TestTrue(TEXT("Production health and attack are finite"),FMath::IsFinite(Stats.MaxHealth)&&FMath::IsFinite(Stats.AttackPower));
  }
 };
 for(const auto& Card:Run->CardPool)
 {
  if(!Card||!Card->HeroDefinition)continue;
  const UEGHeroDefinition* Hero=Card->HeroDefinition.Get();
  Report(Hero,TEXT("Player"));
  Report(Hero->EnhancedDefinition,TEXT("Enhanced"));
  Report(Hero->SummonDefinition,TEXT("Summon-authored (runtime may inherit owner attributes)"));
 }
 // Human source can have zero growth; the production fusion transaction copies
 // the configurable fallback into a private runtime definition in that case.
 const auto* Human=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Human.DA_Human"));
 if(TestNotNull(TEXT("Human definition"),Human)&&Run->StartingCard&&Run->StartingCard->HeroDefinition)
 {
  TStrongObjectPtr<UEGHeroDefinition> Effective(DuplicateObject<UEGHeroDefinition>(Human,GetTransientPackage()));
  if(!TestNotNull(TEXT("Private runtime Human fixture"),Effective.Get()))return false;
  if(Human->bUseSplitConfiguration)
  {
   Effective->StatsDefinition=DuplicateObject<UEGHeroStatsDefinition>(Human->StatsDefinition,Effective.Get());
   if(!TestNotNull(TEXT("Private runtime Human stats"),Effective->StatsDefinition.Get()))return false;
  }
  FEGStatBlock& Growth=Effective->bUseSplitConfiguration?Effective->StatsDefinition->GrowthPerLevel:Effective->GrowthPerLevel;
  if(FMath::IsNearlyZero(Growth.MaxHealth)&&FMath::IsNearlyZero(Growth.MaxMana)&&FMath::IsNearlyZero(Growth.AttackPower)&&FMath::IsNearlyZero(Growth.AttackSpeed)&&FMath::IsNearlyZero(Growth.Defense))
  {
   const auto& Fallback=UEGHeroGrowthSettings::Get();
   Growth.MaxHealth=Fallback.HumanFallbackHealthGrowth;Growth.AttackPower=Fallback.HumanFallbackAttackGrowth;Growth.Defense=Fallback.HumanFallbackDefenseGrowth;
  }
  Report(Effective.Get(),TEXT("Human runtime fusion growth"));
 }
 return true;
}
#endif
