#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Editor.h"
#include "Engine/World.h"
#include "UnrealClient.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"

namespace
{
struct FBalancePolicy
{
 int32 Index=0;
 FName Route=TEXT("Unity");
 int32 Seed=-1;
 FString Directory;
 bool bControlled=false,bCaptureFrames=false,bRequireContinuousPushback=false;
 FString Arm=TEXT("control");
 const TCHAR* Name() const
 {
  static const TCHAR* Names[]={TEXT("ultimate"),TEXT("basic"),TEXT("survival"),TEXT("form_bond")};
  return Names[FMath::Clamp(Index,0,3)];
 }
 TArray<FName> Priority() const
 {
  switch(Index)
  {
   case 0:return {TEXT("Deliverance"),TEXT("Gold"),TEXT("Helix"),TEXT("Flawless")};
   case 1:return {TEXT("Discipline"),TEXT("Wisdom"),TEXT("Daybreak"),TEXT("LoveBeginning")};
   case 2:return {TEXT("Infinity"),TEXT("Decimation"),TEXT("Setsuna"),TEXT("DuskStreet")};
   default:return {TEXT("Stars"),TEXT("Vicissitude"),TEXT("DuskStreet"),TEXT("Kindred"),TEXT("LastBanquet")};
  }
 }
 int32 HeroRank(EEGHeroKind Kind) const
 {
  TArray<EEGHeroKind> Preferred;
  switch(Index)
  {
   case 0:Preferred={EEGHeroKind::Eden,EEGHeroKind::Aponia,EEGHeroKind::VillV};break;
   case 1:Preferred={EEGHeroKind::Sakura,EEGHeroKind::Pardofelis,EEGHeroKind::VillV};break;
   case 2:Preferred={EEGHeroKind::Griseo,EEGHeroKind::Hua,EEGHeroKind::Pardofelis};break;
   default:Preferred={EEGHeroKind::Griseo,EEGHeroKind::Hua,EEGHeroKind::Aponia,EEGHeroKind::Sakura,EEGHeroKind::Pardofelis};break;
  }
  const int32 Found=Preferred.IndexOfByKey(Kind);
  return Found==INDEX_NONE?100:Found;
 }
};

class FBalanceNaturalRun final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FBalancePolicy Policy;
 FString RunId,Path,Rows,LogPath,SetupHash,StigmataPath,StigmataRows;
 double Deadline=0.,OpeningStarted=0.,OpeningSeconds=0.;
 double NextCapture=0.,PreviousWall=0.;int32 CaptureIndex=0;
 int32 StartedStage=0,RecordedStage=0,LastSurvivors=0;
 float LastBattleSeconds=0.f,LastMana=0.f;
 int32 LastTriggers=0;
 float LastBonusDamage=0.f,LastStigmaMana=0.f,LastStigmaShield=0.f;
 TArray<double> FrameMs,WallMs;
 TArray<FEGStigmataContribution> LastContributions;
 bool bObservedRunning=false,bWritten=false;
 bool bObservedBossWall=false,bReportedPushbackFallback=false;
 FString EncounterPath,EncounterRows;
 float NextEncounterTrace=0.f;
 TMap<FGuid,TSet<FGuid>> EnemyUltimateActions;

 bool Write()
 {
  IFileManager::Get().MakeDirectory(*Policy.Directory,true);
  bWritten=FFileHelper::SaveStringToFile(Rows,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  bWritten=FFileHelper::SaveStringToFile(StigmataRows,*StigmataPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&bWritten;
  bWritten=FFileHelper::SaveStringToFile(EncounterRows,*EncounterPath,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)&&bWritten;
  if(!bWritten)Test->AddError(FString::Printf(TEXT("Cannot write balance result: %s"),*Path));
  return bWritten;
 }
 void Record(AEGDemoGameMode* Mode,const TCHAR* Result)
 {
  if(!Mode||StartedStage<=RecordedStage)return;
  const auto Stats=Mode->GetCombatStatistics();
  float Damage=0,Healing=0,Shield=0;
  for(const auto& Stat:Stats){Damage+=Stat.Damage;Healing+=Stat.Healing;Shield+=Stat.Shield;}
  double AppP95=0.,WallP95=0.;
  if(!FrameMs.IsEmpty()){FrameMs.Sort();AppP95=FrameMs[FMath::Clamp(FMath::CeilToInt(FrameMs.Num()*.95f)-1,0,FrameMs.Num()-1)];}
  if(!WallMs.IsEmpty()){WallMs.Sort();WallP95=WallMs[FMath::Clamp(FMath::CeilToInt(WallMs.Num()*.95f)-1,0,WallMs.Num()-1)];}
  if(Mode->BattleRules&&Mode->BattleRules->GetStigmata())LastContributions=Mode->BattleRules->GetStigmata()->GetContributions();
  for(const auto& C:LastContributions)
   StigmataRows+=FString::Printf(TEXT("%s,%s,%s,%d,%d,%s,%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%d\n"),
    *RunId,Policy.Name(),Policy.bControlled?*Policy.Arm:*Policy.Route.ToString(),Policy.Seed,StartedStage,
    Policy.bControlled?TEXT("controlled"):TEXT("natural"),*C.Id.ToString(),C.TriggerCount,C.AdditionalDamage,
    C.HealingRestored,C.ManaRestored,C.ShieldAbsorbed,C.DamagePrevented,C.GoldEarned);
  FString CsvLog=LogPath;CsvLog.ReplaceInline(TEXT("\""),TEXT("\"\""));
  const FString WallValue=Policy.bCaptureFrames||WallMs.IsEmpty()?FString():FString::Printf(TEXT("%.3f"),WallP95);
  const FString AppValue=Policy.bCaptureFrames||FrameMs.IsEmpty()?FString():FString::Printf(TEXT("%.3f"),AppP95);
  const int32 Casts=Mode->BattleRules?Mode->BattleRules->GetSuccessfulUltimateCount():0;
  const float FirstUltimate=Mode->BattleRules?Mode->BattleRules->GetFirstUltimateBattleSeconds():-1.f;
  const FString FirstValue=FirstUltimate>=0.f?FString::Printf(TEXT("%.3f"),FirstUltimate):FString();
  Rows+=FString::Printf(TEXT("%s,%s,%s,%d,%d,%s,%s,%s,%.3f,%.3f,%d,\"%s\",%d,%.3f,%.3f,%.3f,%.3f,%d,%d,%s,%s,%s\n"),
   *RunId,Policy.Name(),Policy.bControlled?*Policy.Arm:*Policy.Route.ToString(),Policy.Seed,StartedStage,*SetupHash,
   Policy.bControlled?TEXT("controlled"):TEXT("natural"),Result,
   OpeningSeconds,LastBattleSeconds,Mode->GetMatch()->GetCorruption(),*CsvLog,LastTriggers,Damage,Healing,Shield,LastStigmaMana,Casts,LastSurvivors,*WallValue,*AppValue,*FirstValue);
  Test->AddInfo(FString::Printf(TEXT("Balance %s stage=%d result=%s opening=%.2f battle=%.2f survivors=%d damage=%.1f stigmata-extra=%.1f mana=%.1f shield-absorbed=%.1f final-mana=%.1f frames=%d"),
   *RunId,StartedStage,Result,OpeningSeconds,LastBattleSeconds,LastSurvivors,Damage,LastBonusDamage,LastStigmaMana,LastStigmaShield,LastMana,FrameMs.Num()));
  RecordedStage=StartedStage;Write();
 }
 bool Prepare(AEGDemoGameMode* Mode)
 {
  auto* Cards=Mode->GetCards();auto* Roster=Mode->GetRoster();
  const int32 Goal=Cards->GetRules().DeploymentLimit;
  for(int32 Attempts=0;Attempts<100;++Attempts)
  {
   bool bProgress=false;
   for(const auto& Card:Cards->GetHand())
   {
    if(!Card.Definition||!Card.Definition->HeroDefinition)continue;
    const FGuid HeroId=Roster->FindHeroByDefinition(Card.Definition->HeroDefinition);
    FEGHeroInstance Hero;
    if(HeroId.IsValid()&&Roster->GetHero(HeroId,Hero)&&Hero.bDeployed&&Cards->TryMergeIntoHero(Card.InstanceId,HeroId))
    {bProgress=true;break;}
   }
   if(bProgress)continue;
   if(Cards->GetPieces().Num()<Goal)
   {
    const auto Hand=Cards->GetHand();
    // A fusion now produces a card, so deploy it before filling the final
    // formation with ordinary units; otherwise the route is never exercised.
    const bool bPrioritizeHuman=Policy.Route==TEXT("OriginFlower")&&Mode->GetMatch()->GetStageNumber()==7;
    for(int32 Rank=0;Rank<=101&&!bProgress;++Rank)for(const auto& Card:Hand)
    {
     if(!Card.Definition||!Card.Definition->HeroDefinition)continue;
     const auto Kind=Card.Definition->HeroDefinition->HeroKind;
     const int32 CardRank=bPrioritizeHuman?(Kind==EEGHeroKind::ElysiaHerrscher?0:Policy.HeroRank(Kind)+1):Policy.HeroRank(Kind);
     if(CardRank!=Rank||Roster->FindHeroByDefinition(Card.Definition->HeroDefinition).IsValid())continue;
     for(int32 Y=0;Y<3&&!bProgress;++Y)for(int32 X=0;X<7&&!bProgress;++X)
      if(Cards->EvaluateHandPlacement(Card.InstanceId,{X,Y}).IsAllowed())bProgress=Cards->TryDeployHandCard(Card.InstanceId,{X,Y});
    }
    if(bProgress)continue;
   }
   const bool bNeedPopulation=Cards->GetPieces().Num()<Goal;
   if(Cards->GetGold()>=Cards->GetRules().PurchasePrice)
   {
    const auto Shop=Cards->GetShop();
    for(int32 Rank=0;Rank<=100&&!bProgress;++Rank)for(int32 Slot=0;Slot<Shop.Num()&&!bProgress;++Slot)
    {
     auto* Def=Shop[Slot];if(!Def||!Def->HeroDefinition||Policy.HeroRank(Def->HeroDefinition->HeroKind)!=Rank)continue;
     const FGuid HeroId=Roster->FindHeroByDefinition(Def->HeroDefinition);
     FEGHeroInstance Hero;const bool bDeployed=HeroId.IsValid()&&Roster->GetHero(HeroId,Hero)&&Hero.bDeployed;
     const bool bInHand=Cards->GetHand().ContainsByPredicate([&](const FEGCardInstance& C){return C.Definition==Def;});
     if((bNeedPopulation&&(HeroId.IsValid()||bInHand))||(!bNeedPopulation&&!bDeployed))continue;
     bProgress=Cards->BuyShopSlotExpected(Slot,Cards->GetShopSlotRevision(Slot));
    }
   }
   if(bProgress)continue;
   const int32 Cost=Cards->GetRefreshCost();
   if(Cards->GetGold()<Cost+Cards->GetRules().PurchasePrice||!Cards->RefreshShop())break;
  }
  Test->AddInfo(FString::Printf(TEXT("Balance preparation stage=%d gold=%lld deployed=%d hand=%d"),
   Mode->GetMatch()->GetStageNumber(),Cards->GetGold(),Cards->GetPieces().Num(),Cards->GetHand().Num()));
  for(const auto& Hero:Roster->GetHeroes())if(Hero.bDeployed&&Hero.Definition)
   Test->AddInfo(FString::Printf(TEXT("Balance deployed kind=%d level=%d paid=%lld"),static_cast<int32>(Hero.Definition->HeroKind),Hero.Level,Hero.PaidCost));
  TArray<FString> Formation;
  for(const auto& Hero:Roster->GetHeroes())if(Hero.Definition)
   Formation.Add(FString::Printf(TEXT("%s:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%d:%lld"),*Hero.Definition->GetPathName(),Hero.Level,
    static_cast<int32>(Hero.Form),Hero.SameNameCards,Hero.bAwakened?1:0,Hero.bDeployed?1:0,
    Hero.DeploymentCell.X,Hero.DeploymentCell.Y,Hero.bVillVLayout?1:0,Hero.GatlingCell.X,Hero.GatlingCell.Y,Hero.CannonCell.X,Hero.CannonCell.Y,Hero.PaidCost));
  Formation.Sort();
  FString Snapshot=FString::Printf(TEXT("seed=%d;stage=%d;corruption=%d;"),Policy.Seed,Mode->GetMatch()->GetStageNumber(),Mode->GetMatch()->GetCorruption());
  for(const auto& Entry:Formation)Snapshot+=Entry+TEXT(";");
  if(Mode->RunDefinition&&Mode->RunDefinition->Stages.IsValidIndex(Mode->GetMatch()->GetStageNumber()-1))
   for(const auto& Enemy:Mode->RunDefinition->Stages[Mode->GetMatch()->GetStageNumber()-1].Enemies)
    Snapshot+=FString::Printf(TEXT("enemy=%s:%d:%d:%d:%d;"),Enemy.Definition?*Enemy.Definition->GetPathName():TEXT("missing"),Enemy.Level,Enemy.Cell.X,Enemy.Cell.Y,Enemy.bCorruptedCopy?1:0);
  SetupHash=FMD5::HashAnsiString(*Snapshot);
  Test->AddInfo(FString::Printf(TEXT("Balance setup hash=%s snapshot=%s"),*SetupHash,*Snapshot));
  if(Policy.bControlled)
  {
   TArray<FName> Controlled;
   const auto Priority=Policy.Priority();
   if(Policy.Arm==TEXT("single")||Policy.Arm==TEXT("three"))Controlled.Add(Priority[0]);
   if(Policy.Arm==TEXT("three")){Controlled.Add(Priority[1]);Controlled.Add(Priority[2]);}
   if(!Mode->GetMatch()->SetStigmataForBalanceTests(Controlled))return false;
   Test->AddInfo(FString::Printf(TEXT("Controlled A/B injected stigmata only: arm=%s count=%d"),*Policy.Arm,Controlled.Num()));
  }
  return Mode->EnterCombat();
 }
 void Snapshot(AEGDemoGameMode* Mode)
 {
  LastBattleSeconds=Mode->GetBattleSeconds();LastSurvivors=0;LastMana=0.f;
  for(auto* Unit:Mode->GetCombatUnits())
   if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->IsAlive_Implementation()&&
      !Unit->IsAuxiliaryEmplacement()&&Unit->GetHeroSkill()&&!Unit->GetHeroSkill()->bSummon)
   {++LastSurvivors;if(Unit->GetAttributes())LastMana+=Unit->GetAttributes()->GetMana();}
  LastTriggers=0;LastBonusDamage=LastStigmaMana=LastStigmaShield=0.f;
  if(Mode->BattleRules&&Mode->BattleRules->GetStigmata())for(const auto& C:Mode->BattleRules->GetStigmata()->GetContributions())
  {LastTriggers+=C.TriggerCount;LastBonusDamage+=C.AdditionalDamage;LastStigmaMana+=C.ManaRestored;LastStigmaShield+=C.ShieldAbsorbed;}
  const double Now=FPlatformTime::Seconds();
  if(PreviousWall>0.)WallMs.Add((Now-PreviousWall)*1000.);
  PreviousWall=Now;FrameMs.Add(FApp::GetDeltaTime()*1000.);
  const int32 Stage=Mode->GetMatch()->GetStageNumber();
  if(Stage==3||Stage==6||Stage==7)
  {
   for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
    if(auto* Skill=Unit->GetHeroSkill();Skill&&Skill->IsCasting()&&Skill->ActionId.IsValid())EnemyUltimateActions.FindOrAdd(Unit->GetInstanceId()).Add(Skill->ActionId);
   if(LastBattleSeconds>=NextEncounterTrace)
   {
    NextEncounterTrace=LastBattleSeconds+.25f;
    for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Enemy&&Unit->GetDefinition()&&Unit->GetAttributes())
    {
     const auto* Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
     const auto* Casts=EnemyUltimateActions.Find(Unit->GetInstanceId());
     EncounterRows+=FString::Printf(TEXT("%d,%.3f,%s,%d,%d,%.3f,%.3f,%d,%.3f,%.3f,%d,%d\n"),
      Stage,LastBattleSeconds,*Unit->GetInstanceId().ToString(),int32(Unit->GetDefinition()->HeroKind),Unit->GetHeroSkill()->GetLevel(),
      Unit->GetAttributes()->GetHealth(),Unit->GetAttributes()->GetMaxHealth(),Boss&&Boss->IsBoss()?int32(Boss->GetBossPhase()):-1,
      Boss?Boss->GetBarrierHealth():0.f,Boss?Boss->GetBarrierRemainingSeconds():0.f,Casts?Casts->Num():0,LastSurvivors);
    }
   }
  }
 }
public:
 FBalanceNaturalRun(FAutomationTestBase* InTest,const FBalancePolicy& InPolicy):Test(InTest),Policy(InPolicy)
 {
  RunId=FString::Printf(TEXT("p%d-s%d-%s"),Policy.Index,Policy.Seed,Policy.bControlled?*Policy.Arm:*Policy.Route.ToString());
  Path=Policy.Directory/(RunId+TEXT(".csv"));
  StigmataPath=Policy.Directory/(RunId+TEXT("-stigmata.csv"));
  EncounterPath=Policy.Directory/(RunId+TEXT("-encounters.csv"));
  EncounterRows=TEXT("stage,battle_s,unit_id,kind,level,health,max_health,boss_phase,barrier_health,barrier_remaining,observed_ultimate_actions,player_survivors\n");
  if(!FParse::Value(FCommandLine::Get(),TEXT("abslog="),LogPath))LogPath=Path;
  Rows=TEXT("run_id,scenario,arm,seed,stage,setup_hash,mode,result,opening_s,battle_s,corruption,evidence,triggers,damage,healing,shield,mana,casts,survivors,frame_p95_ms,app_p95_ms,first_ult_s\n");
  StigmataRows=TEXT("run_id,scenario,arm,seed,stage,mode,id,triggers,attributed_damage,healing_restored,mana_restored,shield_absorbed,damage_prevented,gold_earned\n");
 }
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+900.;
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(FPlatformTime::Seconds()>Deadline)
  {if(Mode)Record(Mode,TEXT("censored"));if(!bWritten)Write();Test->AddError(TEXT("Natural balance run exceeded wall-time limit"));return true;}
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards()||!Mode->GetRoster()||!Mode->AreResourcesReady())return false;
  auto* Match=Mode->GetMatch();
  if(Mode->GetBalanceSeed()!=Policy.Seed)
  {Test->AddError(TEXT("Balance seed did not reach the campaign"));Write();return true;}
  const int32 Stage=Match->GetStageNumber();const auto Phase=Match->GetPhase();
  if(Phase==EEGMatchPhase::Preparation)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(StartedStage==Stage)return false;
   if(!Prepare(Mode)){Test->AddError(TEXT("Natural preparation failed to enter combat"));Write();return true;}
   StartedStage=Stage;OpeningStarted=FPlatformTime::Seconds();OpeningSeconds=0.;bObservedRunning=false;
   LastBattleSeconds=LastMana=LastBonusDamage=LastStigmaMana=LastStigmaShield=0.f;LastTriggers=LastSurvivors=0;FrameMs.Reset();WallMs.Reset();LastContributions.Reset();PreviousWall=0.;
   NextEncounterTrace=0.f;EnemyUltimateActions.Reset();
   return false;
  }
  if(Phase==EEGMatchPhase::Combat)
  {
   // Optional acceptance gate: presentation holds pause combat but the wall
   // transition still ticks, so observe it outside IsCombatRunning().
   if(Policy.bRequireContinuousPushback&&Stage==7)
    for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit))
     if(const auto* Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&Boss->IsBoss())
     {
      bObservedBossWall|=Boss->GetBossPhase()==EEGCorruptionBossPhase::Barrier;
      const int32 Fallbacks=Boss->GetBarrierPushbackFallbackCount();
      if(Fallbacks>0&&!bReportedPushbackFallback)
      {
       bReportedPushbackFallback=true;
       Test->AddError(FString::Printf(TEXT("Natural Boss wall used %d final-position pushback fallback(s)"),Fallbacks));
      }
     }
   if(Mode->BattleRules&&Mode->BattleRules->IsCombatRunning())
   {
    if(!bObservedRunning){OpeningSeconds=FPlatformTime::Seconds()-OpeningStarted;bObservedRunning=true;}
    Snapshot(Mode);
    const double Now=FPlatformTime::Seconds();
    if(Policy.bCaptureFrames&&Stage==7&&FApp::CanEverRender()&&Now>=NextCapture&&!FScreenshotRequest::IsScreenshotRequested())
    {
     const FString FrameDir=Policy.Directory/TEXT("FinalFrames");IFileManager::Get().MakeDirectory(*FrameDir,true);
     const FString Frame=FrameDir/FString::Printf(TEXT("frame_%05d.png"),CaptureIndex++);
     FScreenshotRequest::RequestScreenshot(Frame,true,false,false,FIntRect(),true);NextCapture=Now+.5;
    }
   }
   return false;
  }
  if(StartedStage>RecordedStage)
  {
   if(!bObservedRunning&&OpeningStarted>0.)OpeningSeconds=FPlatformTime::Seconds()-OpeningStarted;
   Record(Mode,Mode->WasVictory()?TEXT("win"):TEXT("loss"));
   if(Policy.bControlled)return true;
  }
  if(Phase==EEGMatchPhase::Reward)
  {
   const int32 Revision=Mode->GetStigmataRevision();
   if(Mode->IsChoosingEndgameRoute())
   {
    if(!Mode->ChooseEndgameRoute(Policy.Route,Revision))
    {Test->AddError(FString::Printf(TEXT("Requested final route %s was rejected"),*Policy.Route.ToString()));Write();return true;}
    Test->AddInfo(FString::Printf(TEXT("Balance final route %s"),*Policy.Route.ToString()));return false;
   }
   const auto Offer=Mode->GetStigmataOffer();
   FString OfferText;for(const FName Id:Offer){if(!OfferText.IsEmpty())OfferText+=TEXT("|");OfferText+=Id.ToString();}
   TArray<FName> Ordered;
   for(const FName Id:Policy.Priority())if(Offer.Contains(Id))Ordered.Add(Id);
   for(const FName Id:Offer)if(!Ordered.Contains(Id))Ordered.Add(Id);
   for(const FName Id:Ordered)if(Mode->ChooseStigmata(Id,Revision))
   {Test->AddInfo(FString::Printf(TEXT("Balance offer=%s chosen=%s"),*OfferText,*Id.ToString()));return false;}
   Test->AddError(FString::Printf(TEXT("No offered stigmata could be chosen: %s"),*OfferText));Write();return true;
  }
  if(Phase==EEGMatchPhase::Finished)
  {
   if(Policy.bRequireContinuousPushback)
   {
    if(!bObservedBossWall)Test->AddError(TEXT("Natural Boss wall was not observed; continuous pushback cannot be verified"));
    else if(!bReportedPushbackFallback)Test->AddInfo(TEXT("Natural Boss wall observed with no final-position pushback fallback"));
   }
   Test->AddInfo(FString::Printf(TEXT("Natural balance finished: stage=%d won=%d (programmatic input; no injected gold, cards, population, health, mana or outcomes). CSV=%s"),
    Stage,Match->DidWinRun()?1:0,*Path));
   if(!bWritten)Write();return true;
  }
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBalanceNaturalSingle,"ElyGambit.BalanceRework.Natural.Single",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBalanceNaturalSingle::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Natural balance test requires an idle editor"));return false;}
 FBalancePolicy Policy;FString Route;
 if(!FParse::Value(FCommandLine::Get(),TEXT("EGBalanceSeed="),Policy.Seed)||Policy.Seed<0||
    !FParse::Value(FCommandLine::Get(),TEXT("EGBalancePolicy="),Policy.Index)||Policy.Index<0||Policy.Index>3||
    !FParse::Value(FCommandLine::Get(),TEXT("EGBalanceRoute="),Route))
 {AddError(TEXT("Specify EGBalanceSeed >= 0, EGBalancePolicy 0..3 and EGBalanceRoute OriginFlower/Story/Unity"));return false;}
 Policy.Route=FName(*Route);
 if(Policy.Route!=TEXT("OriginFlower")&&Policy.Route!=TEXT("Story")&&Policy.Route!=TEXT("Unity"))
 {AddError(TEXT("Unknown balance route"));return false;}
 if(!FParse::Value(FCommandLine::Get(),TEXT("EGBalanceDir="),Policy.Directory))Policy.Directory=FPaths::ProjectSavedDir()/TEXT("BalanceRework20261105/Natural");
 Policy.bControlled=FParse::Param(FCommandLine::Get(),TEXT("EGBalanceControlled"));
 Policy.bCaptureFrames=FParse::Param(FCommandLine::Get(),TEXT("EGBalanceCaptureFrames"));
 Policy.bRequireContinuousPushback=FParse::Param(FCommandLine::Get(),TEXT("EGBalanceRequireContinuousPushback"));
 if(Policy.bControlled&&(!FParse::Value(FCommandLine::Get(),TEXT("EGBalanceArm="),Policy.Arm)||
    (Policy.Arm!=TEXT("control")&&Policy.Arm!=TEXT("single")&&Policy.Arm!=TEXT("three"))))
 {AddError(TEXT("Controlled runs require EGBalanceArm=control/single/three"));return false;}
 if(Policy.bCaptureFrames&&!FApp::CanEverRender()){AddError(TEXT("Final-frame capture requires graphics, not NullRHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FBalanceNaturalRun(this,Policy));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
