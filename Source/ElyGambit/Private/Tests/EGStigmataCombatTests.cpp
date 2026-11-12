#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGPieceTestGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Data/EGStigmataBalance.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Placement/EGDeploymentBoard.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "HAL/PlatformProcess.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataCombatTest,"ElyGambit.Stigmata.Combat",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataCombatTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("game mode"),Mode))return false;
 if(!TestTrue(TEXT("enter combat"),Mode->EnterCombat()))return false;
 auto* Battle=Mode->BattleRules.Get();auto* Rules=Battle->GetStigmata();
 AEGChessUnit* Hero=nullptr;TArray<AEGChessUnit*> Enemies;
 for(auto* Unit:Battle->Units())
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  auto* ASC=Unit->GetAbilitySystemComponent();
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;else Enemies.Add(Unit);
 }
 if(TestNotNull(TEXT("round component"),Rules)&&TestNotNull(TEXT("player"),Hero)&&TestTrue(TEXT("two enemies"),Enemies.Num()>=2))
 {
  auto* Target=Enemies[0];auto* Other=Enemies[1];auto* HeroASC=Hero->GetAbilitySystemComponent();
  const auto& Balance=UEGStigmataBalanceSettings::Get();
  TestTrue(TEXT("balance contract: discipline 20 percent, gold refund 35 percent, infinity 20 percent"),
   FMath::IsNearlyEqual(Balance.DisciplineStep,.2f)&&FMath::IsNearlyEqual(Balance.GoldRefund,.35f)&&FMath::IsNearlyEqual(Balance.InfinityBonus,.2f));
  TestTrue(TEXT("balance contract: helix 120 percent and three charges, wisdom six attacks and 240 percent"),
   Balance.HelixCharges==3&&FMath::IsNearlyEqual(Balance.HelixDamage,1.2f)&&Balance.WisdomAttacks==6&&FMath::IsNearlyEqual(Balance.WisdomDamage,2.4f));
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
  Rules->Selected.Add(TEXT("Discipline"));
  for(int32 I=0;I<5;++I)
  {
   const FGuid Id=FGuid::NewGuid();Rules->RegisterAction(Hero,Id,Target,false);
   const float Before=Target->GetAttributes()->GetHealth();
   FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),Id);
   const float Expected=100.f*(1.f+.2f*FMath::Min(I,3));
   TestTrue(TEXT("successive primary hits get capped discipline bonus"),FMath::IsNearlyEqual(Before-Target->GetAttributes()->GetHealth(),Expected,.05f));
   Battle->BasicHit(Hero,Target,Id);Battle->BasicHit(Hero,Target,Id);
  }
  const FGuid Switched=FGuid::NewGuid();Rules->RegisterAction(Hero,Switched,Other,false);
  const float BeforeSwitch=Other->GetAttributes()->GetHealth();
  FEGCombatExecution::Damage(Hero,Other,100.f,0,1,FGameplayEffectContextHandle(),Switched);
  TestTrue(TEXT("new primary target resets discipline"),FMath::IsNearlyEqual(BeforeSwitch-Other->GetAttributes()->GetHealth(),100.f,.05f));

  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Decimation"));
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  const float BeforeMissing=Target->GetAttributes()->GetHealth();
  FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
  TestTrue(TEXT("half health gives capped 60 percent direct bonus"),FMath::IsNearlyEqual(BeforeMissing-Target->GetAttributes()->GetHealth(),160.f,.05f));
  TestTrue(TEXT("decimation heals ten percent of effective damage"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetHealth(),5016.f,.05f));
  // Count only the settled increment after defense and shield, never a raw estimate.
  Rules->ClearContributions();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),Balance.DefenseConstant);
  Target->GetHeroSkill()->GrantSourceShield(TEXT("TestDamageStatistics"),50.f,30.f);
  FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
  const auto* Decimation=Rules->Contributions.Find(TEXT("Decimation"));
  TestTrue(TEXT("bonus records actual defended damage including shield"),Decimation&&FMath::IsNearlyEqual(Decimation->AdditionalDamage,30.f,.05f));
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  Target->GetHeroSkill()->GrantPrismaticBubble(10.f,0.f,1.f,nullptr,nullptr);
  FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
  Decimation=Rules->Contributions.Find(TEXT("Decimation"));
  TestTrue(TEXT("blocked hit records no damage"),Decimation&&FMath::IsNearlyEqual(Decimation->AdditionalDamage,30.f,.05f));
  Target->GetHeroSkill()->ClearProtection();
  Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Decimation"));Rules->ClearContributions();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  Target->GetHeroSkill()->GrantSourceShield(TEXT("TestDirectStatistics"),50.f,30.f);
  FEGCombatExecution::StigmataDamage(Hero,Target,100.f,FGuid::NewGuid(),false,TEXT("Helix"));
  const auto* Direct=Rules->Contributions.Find(TEXT("Helix"));
  Decimation=Rules->Contributions.Find(TEXT("Decimation"));
  TestTrue(TEXT("direct signet and its bonus split actual damage without overlap"),Direct&&Decimation&&
   FMath::IsNearlyEqual(Direct->AdditionalDamage,100.f,.05f)&&FMath::IsNearlyEqual(Decimation->AdditionalDamage,60.f,.05f));
  Rules->ClearContributions();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  if(TestNull(TEXT("target has no corruption component before copy test"),Target->FindComponentByClass<UEGCorruptionCombatComponent>()))
  {
   auto* CopyRules=UEGCorruptionCombatComponent::AttachCopy(Target,Battle,EEGCorruptionRole::Ranged);
   if(TestNotNull(TEXT("attach corruption copy"),CopyRules))
   {
    FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
    Decimation=Rules->Contributions.Find(TEXT("Decimation"));
    TestTrue(TEXT("stable corruption copy records real bonus"),Decimation&&FMath::IsNearlyEqual(Decimation->AdditionalDamage,60.f,.05f));
    CopyRules->EndCombat();CopyRules->DestroyComponent();
   }
  }
  Rules->ClearContributions();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
  if(TestNull(TEXT("other target has no corruption component before boss test"),Other->FindComponentByClass<UEGCorruptionCombatComponent>()))
  {
   const float PreviousAttack=Other->GetAttributes()->GetAttackPower();
   auto* BossRules=UEGCorruptionCombatComponent::AttachBoss(Other,Battle,10000.f,100.f,50);
   if(TestNotNull(TEXT("attach controlled boss"),BossRules))
   {
    FEGCombatExecution::Damage(Hero,Other,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
    Decimation=Rules->Contributions.Find(TEXT("Decimation"));
    TestTrue(TEXT("stable boss phase records real bonus"),Decimation&&FMath::IsNearlyEqual(Decimation->AdditionalDamage,60.f,.05f));
    Rules->ClearContributions();
    HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
    Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100.f);
    FEGCombatExecution::Damage(Hero,Other,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
    Decimation=Rules->Contributions.Find(TEXT("Decimation"));
    TestTrue(TEXT("first-pool overkill starts wall without guessed damage attribution"),
     BossRules->GetBossPhase()==EEGCorruptionBossPhase::Barrier&&(!Decimation||FMath::IsNearlyZero(Decimation->AdditionalDamage,.05f)));
    TestTrue(TEXT("first-pool overkill does not damage new wall"),FMath::IsNearlyEqual(BossRules->GetBarrierHealth(),BossRules->GetBarrierMaxHealth(),.05f));
    // Once the cinematic has completed, Daybreak's 50% additional wall loss
    // must be attributed exactly once as actual shield damage.
    BossRules->CompleteBarrierIntroForTests();
    Rules->Selected.Reset();Rules->Selected.Add(TEXT("Daybreak"));Rules->ClearContributions();
    const float BeforeWall=BossRules->GetBarrierHealth();
    FEGCombatExecution::Damage(Hero,Other,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
    const auto* WallDaybreak=Rules->Contributions.Find(TEXT("Daybreak"));
    TestTrue(TEXT("daybreak increases real wall damage by half"),FMath::IsNearlyEqual(BeforeWall-BossRules->GetBarrierHealth(),150.f,.05f));
    TestTrue(TEXT("daybreak attributes only its extra wall damage"),WallDaybreak&&FMath::IsNearlyEqual(WallDaybreak->AdditionalDamage,50.f,.05f));
    BossRules->EndCombat();BossRules->DestroyComponent();
    Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),PreviousAttack);
    Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   }
  }
  Rules->ClearContributions();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);

  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Gold"));Rules->Selected.Add(TEXT("Deliverance"));
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
  const int32 BeforeCancelledCast=Battle->GetSuccessfulUltimateCount();
  if(TestTrue(TEXT("real ultimate begins"),Hero->GetHeroSkill()->BeginAction(true)))
  {
   Hero->GetHeroSkill()->CancelAction();
   TestEqual(TEXT("cancel before successful skill gives no refund"),Hero->GetAttributes()->GetMana(),0.f);
   TestFalse(TEXT("cancelled skill gives no deliverance window"),Rules->DeliveranceEnds.Contains(Hero->GetInstanceId()));
   TestEqual(TEXT("cancelled pre-commit ultimate is not counted"),Battle->GetSuccessfulUltimateCount(),BeforeCancelledCast);
  }
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
  const FGuid CastId=FGuid::NewGuid();Rules->RegisterAction(Hero,CastId,Target,true);
  TestEqual(TEXT("cancelled cast grants no mana"),Hero->GetAttributes()->GetMana(),0.f);
  Rules->CommitUltimate(Hero,CastId,100.f);Rules->CommitUltimate(Hero,CastId,100.f);
  TestTrue(TEXT("successful cast refunds 35 percent once"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetMana(),35.f,.05f));
  const float BeforeUltimate=Target->GetAttributes()->GetHealth();
  FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),CastId);
  TestTrue(TEXT("committed cast damage receives 50 percent deliverance"),FMath::IsNearlyEqual(BeforeUltimate-Target->GetAttributes()->GetHealth(),150.f,.05f));

  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Helix"));
  const FGuid HelixCast=FGuid::NewGuid();Rules->RegisterAction(Hero,HelixCast,Target,true);
  Rules->CommitUltimate(Hero,HelixCast,100.f);
  const FGuid Hit=FGuid::NewGuid();const float BeforeHelix=Target->GetAttributes()->GetHealth();
  Battle->BasicHit(Hero,Target,Hit);Battle->BasicHit(Hero,Target,Hit);
  TestTrue(TEXT("helix fires once on a deduplicated basic action"),FMath::IsNearlyEqual(BeforeHelix-Target->GetAttributes()->GetHealth(),120.f,.05f));
  Battle->BasicHit(Hero,Target,FGuid::NewGuid());Battle->BasicHit(Hero,Target,FGuid::NewGuid());
  const float AfterThree=Target->GetAttributes()->GetHealth();
  TestTrue(TEXT("helix fires three charges"),FMath::IsNearlyEqual(BeforeHelix-AfterThree,360.f,.05f));
  Battle->BasicHit(Hero,Target,FGuid::NewGuid());
  TestTrue(TEXT("helix cannot fire a fourth charge"),FMath::IsNearlyEqual(Target->GetAttributes()->GetHealth(),AfterThree,.05f));

  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Wisdom"));Rules->WisdomCount=0;
  FGuid Last;
  for(int32 I=0;I<5;++I){Last=FGuid::NewGuid();Battle->BasicHit(Hero,Target,Last);}
  Battle->BasicHit(Hero,Target,Last);
  TestEqual(TEXT("duplicate hit does not increase wisdom"),Rules->WisdomCount,5);
  const float BeforeWisdom=Target->GetAttributes()->GetHealth();
  Battle->BasicHit(Hero,Target,FGuid::NewGuid());
  TestEqual(TEXT("sixth hit resets counter"),Rules->WisdomCount,0);
  TestTrue(TEXT("wisdom deals 240 percent average attack damage"),FMath::IsNearlyEqual(BeforeWisdom-Target->GetAttributes()->GetHealth(),240.f,.05f));
  FEGCombatExecution::StigmataDamage(Hero,Target,10.f,FGuid::NewGuid());
  TestEqual(TEXT("additional damage cannot count as a basic hit"),Rules->WisdomCount,0);

  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Setsuna"));
  const float BeforeEvade=Hero->GetAttributes()->GetHealth();
  const float SpeedBeforeEvade=Hero->GetAttributes()->GetAttackSpeed();
  TestFalse(TEXT("first direct enemy attack is evaded"),FEGCombatExecution::Damage(Target,Hero,100.f));
  TestEqual(TEXT("evade avoids damage"),Hero->GetAttributes()->GetHealth(),BeforeEvade);
  TestTrue(TEXT("next direct attack is not evaded during cooldown"),FEGCombatExecution::Damage(Target,Hero,100.f));
  TestTrue(TEXT("evade grants 40 percent attack speed"),FMath::IsNearlyEqual(Hero->GetAttributes()->GetAttackSpeed(),SpeedBeforeEvade*1.4f,.01f));

  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Daybreak"));Rules->ClearContributions();
  Target->GetHeroSkill()->GrantSourceShield(TEXT("TestDaybreak"),100.f,30.f);
  const float BeforeShield=Target->GetAttributes()->GetHealth();
  FEGCombatExecution::Damage(Hero,Target,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
  TestTrue(TEXT("daybreak increases shield damage without amplifying health spillover"),
   FMath::IsNearlyZero(Target->GetHeroSkill()->GetSourceShield(TEXT("TestDaybreak")),.01f)&&
   FMath::IsNearlyEqual(BeforeShield-Target->GetAttributes()->GetHealth(),100.f-100.f/1.5f,.05f));
  const auto* Daybreak=Rules->Contributions.Find(TEXT("Daybreak"));
  TestTrue(TEXT("daybreak records only its actual extra shield loss"),Daybreak&&FMath::IsNearlyEqual(Daybreak->AdditionalDamage,100.f/3.f,.05f));
  const float BeforeBleed=Target->GetAttributes()->GetHealth();
  Battle->BasicHit(Hero,Target,FGuid::NewGuid());
  for(int32 I=0;I<62;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}
  AddInfo(FString::Printf(TEXT("daybreak actual health damage %.3f"),BeforeBleed-Target->GetAttributes()->GetHealth()));
  TestTrue(TEXT("daybreak deals three periodic ticks"),FMath::IsNearlyEqual(BeforeBleed-Target->GetAttributes()->GetHealth(),150.f,.5f));

  auto SpawnHero=[&](const TCHAR* Path)->AEGChessUnit*
  {
   auto* Card=LoadObject<UEGCardDefinition>(nullptr,Path);
   if(!Card||!Card->HeroDefinition)return nullptr;
   auto* Unit=Mode->SpawnBattleSummon(Card->HeroDefinition,1,Grid->CellToWorld({0,1})+FVector(0,0,88),EEGFaction::Player);
   if(Unit)
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);
    Unit->GetHeroSkill()->Configure(Battle,1,false);
    Rules->Deployed.Add(Unit->GetInstanceId());
    // This controlled test actor represents a deployed hero for both the
    // stigmata rules and battle-owned successful-cast statistics.
    Battle->DeployedIds.Add(Unit->GetInstanceId());
    Battle->EnterUnit(Unit);
   }
   return Unit;
  };
  auto* Sakura=SpawnHero(TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"));
  auto* Aponia=SpawnHero(TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia"));
  auto* Eden=SpawnHero(TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden"));
  auto* Elysia=SpawnHero(TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"));
  auto* Human=SpawnHero(TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human"));
  auto* Mobius=SpawnHero(TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius"));
  auto* Vill=SpawnHero(TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
  if(TestNotNull(TEXT("Sakura"),Sakura)&&TestNotNull(TEXT("Aponia"),Aponia)&&TestNotNull(TEXT("Eden"),Eden)&&
     TestNotNull(TEXT("ordinary Elysia"),Elysia)&&TestNotNull(TEXT("human Elysia"),Human)&&
     TestNotNull(TEXT("Mobius"),Mobius)&&TestNotNull(TEXT("Vill-V"),Vill))
  {
   Rules->Selected.Reset();Rules->Selected.Add(TEXT("DuskStreet"));Rules->Selected.Add(TEXT("LoveBeginning"));
   const float ElysiaSpeed=Elysia->GetAttributes()->GetAttackSpeed(),HumanSpeed=Human->GetAttributes()->GetAttackSpeed();
   Rules->ApplyOpening();
   TestTrue(TEXT("opening grants twilight shield to each member"),
    FMath::IsNearlyEqual(Hero->GetHeroSkill()->GetSourceShield(TEXT("StigmataDuskStreet")),Hero->GetAttributes()->GetMaxHealth()*.35f,.05f)&&
    FMath::IsNearlyEqual(Sakura->GetHeroSkill()->GetSourceShield(TEXT("StigmataDuskStreet")),Sakura->GetAttributes()->GetMaxHealth()*.35f,.05f)&&
    FMath::IsNearlyEqual(Aponia->GetHeroSkill()->GetSourceShield(TEXT("StigmataDuskStreet")),Aponia->GetAttributes()->GetMaxHealth()*.35f,.05f));
   TestTrue(TEXT("love gives ordinary Elysia team plus personal speed"),FMath::IsNearlyEqual(Elysia->GetAttributes()->GetAttackSpeed(),ElysiaSpeed*1.35f,.01f));
   TestTrue(TEXT("love gives human Elysia team plus personal speed"),FMath::IsNearlyEqual(Human->GetAttributes()->GetAttackSpeed(),HumanSpeed*1.35f,.01f));

   Rules->Selected.Reset();Rules->Selected.Add(TEXT("LastBanquet"));
   Aponia->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Eden->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   const FGuid BanquetCast=FGuid::NewGuid();Rules->RegisterAction(Elysia,BanquetCast,Target,true);Rules->CommitUltimate(Elysia,BanquetCast,100.f);
   const float AponiaMana=Aponia->GetAttributes()->GetMana(),EdenMana=Eden->GetAttributes()->GetMana();
   TestTrue(TEXT("banquet restores 30 percent to both partners"),
    FMath::IsNearlyEqual(AponiaMana,Aponia->GetAttributes()->GetMaxMana()*.3f,.05f)&&FMath::IsNearlyEqual(EdenMana,Eden->GetAttributes()->GetMaxMana()*.3f,.05f));
   const FGuid Repeat=FGuid::NewGuid();Rules->RegisterAction(Elysia,Repeat,Target,true);Rules->CommitUltimate(Elysia,Repeat,100.f);
   TestTrue(TEXT("banquet cannot repeat during cooldown"),FMath::IsNearlyEqual(Aponia->GetAttributes()->GetMana(),AponiaMana,.05f)&&FMath::IsNearlyEqual(Eden->GetAttributes()->GetMana(),EdenMana,.05f));

   Rules->Selected.Reset();Rules->Selected.Add(TEXT("Infinity"));
   Rules->BuffedUnits.Remove(Hero);
   const float HeroMax=Hero->GetAttributes()->GetMaxHealth(),HeroAttack=Hero->GetAttributes()->GetAttackPower();
   Rules->ApplyUnitBonuses(Hero);Rules->ApplyUnitBonuses(Hero);
   TestTrue(TEXT("infinity gives deployed hero health and attack once"),
    FMath::IsNearlyEqual(Hero->GetAttributes()->GetMaxHealth(),HeroMax*1.2f,.05f)&&FMath::IsNearlyEqual(Hero->GetAttributes()->GetAttackPower(),HeroAttack*1.2f,.05f));
   if(auto* Definition=Hero->GetDefinition()->SummonDefinition.Get())
   {
    auto* Can=Mode->SpawnBattleSummon(Definition,1,Hero->GetActorLocation(),EEGFaction::Player);
    if(TestNotNull(TEXT("combat summon"),Can))
    {
     Can->GetHeroSkill()->bSummon=true;
     const float Max=Can->GetAttributes()->GetMaxHealth(),Attack=Can->GetAttributes()->GetAttackPower();
     Rules->ApplySummon(Can);Rules->ApplySummon(Can);
     TestTrue(TEXT("infinity gives independent summon health and attack exactly once"),FMath::IsNearlyEqual(Can->GetAttributes()->GetMaxHealth(),Max*1.2f,.05f)&&FMath::IsNearlyEqual(Can->GetAttributes()->GetAttackPower(),Attack*1.2f,.05f));
    }
   }
   else AddError(TEXT("Pardo summon definition missing"));

   auto* GriseoCard=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo"));
   auto* Griseo=GriseoCard&&GriseoCard->HeroDefinition?Mode->SpawnBattleSummon(GriseoCard->HeroDefinition,1,Grid->CellToWorld({0,1})+FVector(0,0,88),EEGFaction::Player):nullptr;
   if(TestNotNull(TEXT("knight owner"),Griseo))
   {
    // Production applies the deployed owner's modifiers before configuring
    // Griseo; Configure synchronously creates the knight with authored ratios.
    Griseo->GetCombat()->SetComponentTickEnabled(false);
    Rules->Deployed.Add(Griseo->GetInstanceId());
    Rules->ApplyUnitBonuses(Griseo);
    const float OwnerHealth=Griseo->GetAttributes()->GetMaxHealth(),OwnerAttack=Griseo->GetAttributes()->GetAttackPower();
    auto* Definition=Cast<UEGGriseoSkillDefinition>(Griseo->GetDefinition()->SkillDefinition);
    Griseo->GetHeroSkill()->Configure(Battle,1,false);Battle->EnterUnit(Griseo);
    auto* Knight=Definition?Battle->SpawnKnight(Griseo,Definition->KnightHealthRatio,Definition->KnightAttackRatio):nullptr;
    if(TestNotNull(TEXT("knight authored rules"),Definition)&&TestNotNull(TEXT("derived knight"),Knight))
    {
     Knight->GetCombat()->SetComponentTickEnabled(false);
     Rules->ApplySummon(Knight);
     AddInfo(FString::Printf(TEXT("knight derived health owner=%.3f ratio=%.3f actual=%.3f; attack owner=%.3f ratio=%.3f actual=%.3f"),
      OwnerHealth,Definition->KnightHealthRatio,Knight->GetAttributes()->GetMaxHealth(),OwnerAttack,Definition->KnightAttackRatio,Knight->GetAttributes()->GetAttackPower()));
     TestTrue(TEXT("derived knight inherits buffed owner health without another 1.2"),FMath::IsNearlyEqual(Knight->GetAttributes()->GetMaxHealth(),OwnerHealth*Definition->KnightHealthRatio,.05f));
     TestTrue(TEXT("derived knight inherits buffed owner attack without another 1.2"),FMath::IsNearlyEqual(Knight->GetAttributes()->GetAttackPower(),OwnerAttack*Definition->KnightAttackRatio,.05f));
    }
   }

   Rules->Selected.Reset();Rules->Selected.Add(TEXT("Kindred"));
   auto* Link=Cast<UEGMobiusSkillBehavior>(Mobius->GetHeroSkill()->GetBehavior());
   if(TestNotNull(TEXT("Mobius behavior"),Link))
   {
    Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
    Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),5000.f);
    Link->OwnerBasicHit(Target,FGuid::NewGuid());
    Vill->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
    const FGuid Uncommitted=FGuid::NewGuid();Rules->RegisterAction(Vill,Uncommitted,Target,true);
    const float BeforeUncommitted=Target->GetAttributes()->GetHealth();
    FEGCombatExecution::Damage(Vill,Target,10.f,0,1,FGameplayEffectContextHandle(),Uncommitted);
    TestTrue(TEXT("uncommitted Vill-V action cannot trigger kindred"),FMath::IsNearlyEqual(BeforeUncommitted-Target->GetAttributes()->GetHealth(),10.f,.1f));
    const FGuid JointCast=FGuid::NewGuid();Rules->RegisterAction(Vill,JointCast,Target,true);Rules->CommitUltimate(Vill,JointCast,100.f);
    const float BeforeJoint=Target->GetAttributes()->GetHealth();
    FEGCombatExecution::Damage(Vill,Target,10.f,0,1,FGameplayEffectContextHandle(),JointCast);
    const float FirstJoint=BeforeJoint-Target->GetAttributes()->GetHealth();
    FEGCombatExecution::Damage(Vill,Target,10.f,0,1,FGameplayEffectContextHandle(),JointCast);
    const float SecondJoint=BeforeJoint-Target->GetAttributes()->GetHealth()-FirstJoint;
    TestTrue(TEXT("kindred adds 480 percent attack to lowest linked target"),FMath::IsNearlyEqual(FirstJoint,490.f,.1f));
    TestTrue(TEXT("kindred cannot recurse or repeat within the same cast"),FMath::IsNearlyEqual(SecondJoint,10.f,.1f));
    // The Kindred fixture deliberately created Mobius' shared-damage link.
    // End it before measuring the later rain pulses, otherwise each enemy's
    // area hit can add linked damage to the other until that link expires.
    TestTrue(TEXT("Kindred fixture left the target linked"),Link->IsLinkedTarget(Target));
    Link->EndBattle();
    TestFalse(TEXT("Kindred link is cleared before subsequent scenarios"),Link->IsLinkedTarget(Target));
   }

   Rules->Selected.Reset();Rules->Selected.Add(TEXT("Infinity"));Rules->bUnity=true;
   Rules->BuffedUnits.Remove(Eden);
   const float EdenMax=Eden->GetAttributes()->GetMaxHealth(),EdenAttack=Eden->GetAttributes()->GetAttackPower();
   Eden->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDamageMultiplierAttribute(),1.3f);
   Rules->ApplyUnitBonuses(Eden);Rules->ApplyUnitBonuses(Eden);
   TestTrue(TEXT("infinity and unity health factors multiply exactly once"),FMath::IsNearlyEqual(Eden->GetAttributes()->GetMaxHealth(),EdenMax*1.5f,.05f));
   TestTrue(TEXT("infinity attack applies exactly once with unity"),FMath::IsNearlyEqual(Eden->GetAttributes()->GetAttackPower(),EdenAttack*1.2f,.05f));
   const float BeforeUnity=Target->GetAttributes()->GetHealth();
   FEGCombatExecution::Damage(Eden,Target,100.f);
   TestTrue(TEXT("unity multiplies the existing outgoing damage multiplier"),FMath::IsNearlyEqual(BeforeUnity-Target->GetAttributes()->GetHealth(),175.5f,.05f));
   const float BeforeUnityLinked=Target->GetAttributes()->GetHealth();
   FEGCombatExecution::Damage(Eden,Target,100.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),true);
   TestTrue(TEXT("already resolved linked damage is not multiplied by unity again"),FMath::IsNearlyEqual(BeforeUnityLinked-Target->GetAttributes()->GetHealth(),100.f,.05f));
   Rules->bUnity=false;

   Rules->Selected.Reset();Rules->Selected.Add(TEXT("Flawless"));
   auto* ElysiaRules=Cast<UEGElysiaSkillDefinition>(Elysia->GetDefinition()->SkillDefinition);
   auto* ElysiaBehavior=Cast<UEGElysiaSkillBehavior>(Elysia->GetHeroSkill()->GetBehavior());
   if(!TestNotNull(TEXT("Elysia rules"),ElysiaRules)||!TestNotNull(TEXT("Elysia behavior"),ElysiaBehavior))return false;
   const int32 BeforeElysiaCast=Battle->GetSuccessfulUltimateCount();
   TGuardValue<bool> ElysiaRainMode(ElysiaRules->bArrowRainUltimate,true);
   TGuardValue<float> ElysiaRainSeconds(ElysiaRules->ArrowRainSeconds,4.f);
   TGuardValue<int32> ElysiaRainWaves(ElysiaRules->ArrowRainPulses,8);
   TGuardValue<float> FlawlessSeconds(Rules->FlawlessSeconds,3.f);
   Elysia->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
   Elysia->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Elysia->GetAttributes()->GetMaxMana());
   if(TestTrue(TEXT("Elysia can cast"),Elysia->GetHeroSkill()->BeginAction(true)))
   {
    auto* RainTarget=Elysia->GetCombat()->FindNearestEnemy();
    if(TestNotNull(TEXT("Rain target"),RainTarget))
    {
     RainTarget->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
     RainTarget->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
     RainTarget->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
     RainTarget->GetHeroSkill()->ClearProtection();RainTarget->GetHeroSkill()->AbsorbShield(RainTarget->GetHeroSkill()->Shield);
    }
    Elysia->GetHeroSkill()->AnimationEvent(TEXT("Release"));Elysia->GetHeroSkill()->AnimationEvent(TEXT("Finish"));
    TestTrue(TEXT("flawless extends the 4 second rain to 7 seconds"),FMath::IsNearlyEqual(ElysiaBehavior->GetArrowRainRemaining(),7.f,.05f));
    TestEqual(TEXT("successful ultimate counts without a mana-triggering stigmata"),Battle->GetSuccessfulUltimateCount(),BeforeElysiaCast+1);
    TestTrue(TEXT("first successful ultimate has an effective battle timestamp"),Battle->GetFirstUltimateBattleSeconds()>=0.f);
    float PreviousHealth=RainTarget?RainTarget->GetAttributes()->GetHealth():0.f,FirstPulseDamage=-1.f;
    int32 PreviousPulses=0;
    for(int32 I=0;I<142;++I)
    {
     ++GFrameCounter;World->Tick(LEVELTICK_All,.05f);
     const int32 Count=ElysiaBehavior->GetArrowRainPulseCount();
     if(Count!=PreviousPulses&&RainTarget)
     {
      const float CurrentHealth=RainTarget->GetAttributes()->GetHealth(),Delta=PreviousHealth-CurrentHealth;
      TestEqual(TEXT("Each sampled update advances by one wave"),Count,PreviousPulses+1);
      if(FirstPulseDamage<0.f)FirstPulseDamage=Delta;
      else TestTrue(FString::Printf(TEXT("Flawless wave %d damage %.3f matches first wave %.3f"),Count,Delta,FirstPulseDamage),FMath::IsNearlyEqual(Delta,FirstPulseDamage,.1f));
      PreviousHealth=CurrentHealth;PreviousPulses=Count;
     }
    }
    TestTrue(TEXT("Every rain wave actually damages its target"),FirstPulseDamage>0.f);
    TestEqual(TEXT("Seven second rain delivers fourteen waves"),ElysiaBehavior->GetArrowRainPulseCount(),14);
    TestFalse(TEXT("Extended rain ends after its final wave"),ElysiaBehavior->IsArrowRainActive());
    // An extension that does not land exactly on the half-second grid retains
    // its true duration; only whole due waves are emitted.
    Rules->FlawlessSeconds=1.2f;
    Elysia->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Elysia->GetAttributes()->GetMaxMana());
    if(TestTrue(TEXT("Second rain can start after expiry"),Elysia->GetHeroSkill()->BeginAction(true)))
    {
     Elysia->GetHeroSkill()->AnimationEvent(TEXT("Release"));Elysia->GetHeroSkill()->AnimationEvent(TEXT("Finish"));
     TestTrue(TEXT("Non-grid bonus preserves 5.2 second duration"),FMath::IsNearlyEqual(ElysiaBehavior->GetArrowRainRemaining(),5.2f,.05f));
     for(int32 I=0;I<105;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}
     TestEqual(TEXT("5.2 second rain has ten whole half-second waves"),ElysiaBehavior->GetArrowRainPulseCount(),10);
     TestFalse(TEXT("Non-grid rain completes"),ElysiaBehavior->IsArrowRainActive());
    }
   }
   Rules->Selected.Add(TEXT("Gold"));
   Rules->BuffedUnits.Remove(Human);Rules->ApplyUnitBonuses(Human);
   TestTrue(TEXT("flawless adds 25 percent skill multiplier"),FMath::IsNearlyEqual(Human->GetAttributes()->GetSkillMultiplier(),1.25f,.01f));
   auto* HumanRules=Cast<UEGElysiaHerrscherSkillDefinition>(Human->GetDefinition()->SkillDefinition);
   auto* HumanSkill=Human->GetHeroSkill();HumanSkill->Configure(Battle,11,false);
   const int32 BeforeHumanCast=Battle->GetSuccessfulUltimateCount();
   Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
   Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Human->GetAttributes()->GetMaxMana());
   Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   if(TestNotNull(TEXT("human rules"),HumanRules)&&TestTrue(TEXT("human can cast"),HumanSkill->BeginAction(true)))
   {
    HumanSkill->AnimationEvent(TEXT("Release"));
    HumanSkill->AnimationEvent(TEXT("Release"));
    TestEqual(TEXT("human entry success counts once despite duplicate release"),Battle->GetSuccessfulUltimateCount(),BeforeHumanCast+1);
    TestFalse(TEXT("human gold refund is discarded during entry"),Rules->DeferredMana.Contains(Human->GetInstanceId()));
    TestTrue(TEXT("human mana remains empty during entry"),FMath::IsNearlyZero(Human->GetAttributes()->GetMana(),.01f));
    HumanSkill->AnimationEvent(TEXT("Finish"));
    auto* HumanBehavior=Cast<UEGElysiaHerrscherSkillBehavior>(HumanSkill->GetBehavior());
    const auto InitialState=HumanBehavior?HumanBehavior->GetPassiveState():FEGElysiaPassiveState();
    int32 FirstCastingFrame=INDEX_NONE;
    // Origin form duration comes from the production skill definition (12 s
    // currently), not the former fixed 8.2 s fixture. Stop at the first
    // casting frame so the finisher cannot complete before its hit assertions.
    const int32 FinisherWaitTicks=FMath::CeilToInt((FMath::Max(0.f,InitialState.RemainingSeconds)+2.f)/.05f);
    for(int32 I=0;I<FinisherWaitTicks&&!HumanSkill->IsCasting();++I)
    {
     ++GFrameCounter;World->Tick(LEVELTICK_All,.05f);
     if(FirstCastingFrame==INDEX_NONE&&HumanSkill->IsCasting())FirstCastingFrame=I;
    }
    const auto FinalState=HumanBehavior?HumanBehavior->GetPassiveState():FEGElysiaPassiveState();
    AddInfo(FString::Printf(TEXT("human finisher diagnostic originSeconds=%.3f initialPhase=%d initialRemaining=%.3f finalPhase=%d finalRemaining=%.3f firstCastingFrame=%d casting=%d targetValid=%d otherValid=%d battleRunning=%d world=%.3f"),
     HumanRules->OriginSeconds,int32(InitialState.Phase),InitialState.RemainingSeconds,int32(FinalState.Phase),FinalState.RemainingSeconds,
     FirstCastingFrame,HumanSkill->IsCasting()?1:0,Human->GetCombat()->IsValidEnemy(Target)?1:0,Human->GetCombat()->IsValidEnemy(Other)?1:0,
     Battle->IsCombatRunning()?1:0,World->GetTimeSeconds()));
    if(TestTrue(TEXT("human finisher starts"),HumanSkill->IsCasting()))
    {
     const float BeforeMain=Target->GetAttributes()->GetHealth(),BeforeOther=Other->GetAttributes()->GetHealth();
     HumanSkill->AnimationEvent(TEXT("Hit"));HumanSkill->AnimationEvent(TEXT("Hit"));
     const float DeltaMain=BeforeMain-Target->GetAttributes()->GetHealth(),DeltaOther=BeforeOther-Other->GetAttributes()->GetHealth();
     const float Main=100.f*HumanRules->FinisherMain*1.5f*1.25f,Secondary=100.f*HumanRules->FinisherOthers*1.5f*1.25f;
     TestTrue(TEXT("flawless scales both human finisher damage values once"),
      (FMath::IsNearlyEqual(DeltaMain,Main,.5f)&&FMath::IsNearlyEqual(DeltaOther,Secondary,.5f))||
      (FMath::IsNearlyEqual(DeltaOther,Main,.5f)&&FMath::IsNearlyEqual(DeltaMain,Secondary,.5f)));
     HumanSkill->AnimationEvent(TEXT("Finish"));
     TestEqual(TEXT("human finisher continuation is not a second cast"),Battle->GetSuccessfulUltimateCount(),BeforeHumanCast+1);
     const float Refunded=Human->GetAttributes()->GetMana();
     TestTrue(TEXT("human never receives deferred refund after returning to bow"),FMath::IsNearlyZero(Refunded,.05f));
     Rules->DeliverDeferredMana(Human);
     TestTrue(TEXT("deferred refund cannot be paid twice"),FMath::IsNearlyEqual(Human->GetAttributes()->GetMana(),Refunded,.05f));

     Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Human->GetAttributes()->GetMaxMana());
     if(TestTrue(TEXT("human enters another ultimate for controlled revival test"),HumanSkill->BeginAction(true)))
     {
      HumanSkill->AnimationEvent(TEXT("Release"));HumanSkill->AnimationEvent(TEXT("Finish"));
      TestFalse(TEXT("second human ultimate also rejects deferred refund"),Rules->DeferredMana.Contains(Human->GetInstanceId()));
      if(TestTrue(TEXT("controlled same-actor revival begins"),HumanSkill->BeginRevive(.1f,.5f,0.f)))
      {
       TestFalse(TEXT("revival discards old deferred refund immediately"),Rules->DeferredMana.Contains(Human->GetInstanceId()));
       for(int32 I=0;I<5;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.05f);}
       Rules->DeliverDeferredMana(Human);
       TestTrue(TEXT("revival cannot pay the prior action refund later"),FMath::IsNearlyZero(Human->GetAttributes()->GetMana(),.05f));
      }
     }
    }
   }
  }

  // A shield-breaking bonus must not count health beyond the remaining life.
  Rules->Selected.Reset();Rules->Selected.Add(TEXT("Daybreak"));Rules->ClearContributions();
  HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetDamageMultiplierAttribute(),1.f);
  Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
  Other->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10.f);
  Other->GetHeroSkill()->GrantSourceShield(TEXT("DaybreakLowHealth"),100.f,30.f);
  FEGCombatExecution::Damage(Hero,Other,100.f,0,1,FGameplayEffectContextHandle(),FGuid::NewGuid());
  const auto* LowHealthDaybreak=Rules->Contributions.Find(TEXT("Daybreak"));
  TestTrue(TEXT("daybreak extra shield damage is capped by remaining health"),LowHealthDaybreak&&FMath::IsNearlyEqual(LowHealthDaybreak->AdditionalDamage,10.f,.05f));

  Battle->Stop();
  TestFalse(TEXT("round state is invalid after stop"),Rules->Current());
  TestTrue(TEXT("round stops and clears pending bleeds"),Rules->Bleeds.IsEmpty());
  TestTrue(TEXT("round stops and clears queued feedback"),Rules->PendingTriggers.IsEmpty());
  TestTrue(TEXT("round stops and clears action identities"),Rules->Actions.IsEmpty());
 }
 World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return true;
}
#endif
