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
#include "Data/EGElysiaSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Sound/SoundBase.h"

namespace
{
struct FElysiaImpactAudioFixture
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr; AEGDemoGameMode* Mode=nullptr; AEGChessUnit* Hero=nullptr;
 TArray<AEGChessUnit*> Enemies;
 explicit FElysiaImpactAudioFixture(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL Url;World->SetGameMode(Url);Mode=World->GetAuthGameMode<AEGDemoGameMode>();Mode->UnitClass=AEGDemoCombatUnit::StaticClass();
  Mode->RunDefinition=EGProductionTestAssets::ResolveRun(Test);
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=World->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=World->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};World->InitializeActorsForPlay(Url);World->BeginPlay();
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia.DA_Card_Elysia"));
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2})||!Mode->EnterCombat())return;
  Mode->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:Mode->GetCombatUnits())
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);
   auto* ASC=Unit->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia)Hero=Unit;
   else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Enemies.Add(Unit);
  }
  if(Hero)for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Hero->GetActorLocation()+FVector(500.f+I*180.f,0.f,0.f));
 }
 ~FElysiaImpactAudioFixture(){if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
 void Advance(float Seconds){while(Seconds>0.f){const float Step=FMath::Min(Seconds,.025f);++GFrameCounter;World->Tick(LEVELTICK_All,Step);Seconds-=Step;}}
 bool LoadSounds()
 {
  const auto* P=Hero&&Hero->GetDefinition()?Hero->GetDefinition()->GetCombatAudioPresentation():nullptr;
  if(!P||!P->bUseEventAudio)return false;
  bool Hit=false,Rain=false;
  for(const auto& Event:P->AudioEvents)for(const auto& Sound:Event.Sounds)
  {
   if(Event.Node==TEXT("Hit")&&!Event.bUltimate)Hit|=Sound.LoadSynchronous()!=nullptr;
   if(Event.Node==TEXT("RainHit")&&Event.bUltimate)Rain|=Sound.LoadSynchronous()!=nullptr;
  }
  return Hit&&Rain;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaCommittedAudioIdentity,
 "ElyGambit.Elysia.CombatAudio.CommittedImpactIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaCommittedAudioIdentity::RunTest(const FString&)
{
 FElysiaImpactAudioFixture E(*this);
 if(!TestNotNull(TEXT("Elysia"),E.Hero)||!TestTrue(TEXT("production impact sounds are resident"),E.LoadSounds()))return false;
 auto* Audio=E.Hero->FindComponentByClass<UEGCombatAudioComponent>();
 if(!TestNotNull(TEXT("combat audio component"),Audio))return false;
 const FGuid Round=E.Mode->BattleRules->GetRoundId(),Shot=FGuid::NewGuid(),Later=FGuid::NewGuid();
 Audio->BeginAction(Shot,false,0);Audio->EndAction(true);Audio->BeginAction(Later,false,1);
 const int32 Start=Audio->EventCount;
 Audio->CommittedImpact(TEXT("Hit"),Round,Shot,false,0,0,FVector::ZeroVector);
 TestEqual(TEXT("previous action's committed projectile can sound during a later action"),Audio->EventCount,Start+1);
 Audio->CommittedImpact(TEXT("Hit"),Round,Shot,false,0,0,FVector::ZeroVector);
 Audio->CommittedImpact(TEXT("Hit"),FGuid::NewGuid(),Shot,false,0,1,FVector::ZeroVector);
 Audio->CommittedImpact(TEXT("Hit"),Round,Shot,false,0,-1,FVector::ZeroVector);
 TestEqual(TEXT("duplicate, foreign round and invalid pulse are rejected"),Audio->EventCount,Start+1);
 const FGuid Rain=FGuid::NewGuid();
 Audio->CommittedImpact(TEXT("RainHit"),Round,Rain,true,0,1,FVector::ZeroVector);
 Audio->CommittedImpact(TEXT("RainHit"),Round,Rain,true,0,1,FVector::ZeroVector);
 Audio->CommittedImpact(TEXT("RainHit"),Round,Rain,true,0,2,FVector::ZeroVector);
 TestEqual(TEXT("each rain wave emits once"),Audio->EventCount,Start+3);
 Audio->EndAction(false);
 Audio->CommittedImpact(TEXT("RainHit"),Round,Rain,true,0,3,FVector::ZeroVector);
 TestEqual(TEXT("later basic cancellation does not discard committed rain"),Audio->EventCount,Start+4);
 E.Mode->BattleRules->Stop();
 Audio->CommittedImpact(TEXT("RainHit"),Round,Rain,true,0,4,FVector::ZeroVector);
 TestEqual(TEXT("round stop rejects delayed impacts"),Audio->EventCount,Start+4);
 TestEqual(TEXT("round stop clears component voices"),Audio->GetActiveVoiceCount(),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRainAudioProduction,
 "ElyGambit.Elysia.CombatAudio.RainDamageAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRainAudioProduction::RunTest(const FString&)
{
 FElysiaImpactAudioFixture E(*this);
 if(!TestNotNull(TEXT("Elysia"),E.Hero)||!TestTrue(TEXT("two enemies"),E.Enemies.Num()>=2)||!TestTrue(TEXT("impact sounds resident"),E.LoadSounds()))return false;
 auto* Skill=E.Hero->GetHeroSkill();auto* Policy=Cast<UEGElysiaSkillBehavior>(Skill->GetBehavior());
 auto* Rules=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* Audio=E.Hero->FindComponentByClass<UEGCombatAudioComponent>();
 if(!TestNotNull(TEXT("rain behavior"),Policy)||!TestNotNull(TEXT("rain rules"),Rules)||!TestNotNull(TEXT("audio"),Audio))return false;
 TGuardValue<bool> RainMode(Rules->bArrowRainUltimate,true);
 TGuardValue<float> Duration(Rules->ArrowRainSeconds,4.f);TGuardValue<int32> Pulses(Rules->ArrowRainPulses,8);
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Hero->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("cancelled rain starts"),Skill->BeginAction(true)))return false;
 Skill->CancelAction();const int32 Cancelled=Audio->EventCount;
 Skill->AnimationEvent(TEXT("Release"));E.Advance(.1f);
 TestEqual(TEXT("cancelled pre-release rain cannot make impact sounds"),Audio->EventCount,Cancelled);
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Hero->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("rain action begins"),Skill->BeginAction(true)))return false;
 const int32 Before=Audio->EventCount;
 const float FirstHP=E.Enemies[0]->GetAttributes()->GetHealth(),SecondHP=E.Enemies[1]->GetAttributes()->GetHealth();
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
 E.Advance(.55f);
 TestEqual(TEXT("first wave emitted"),Policy->GetArrowRainPulseCount(),1);
 TestTrue(TEXT("both nearby enemies actually took wave damage"),E.Enemies[0]->GetAttributes()->GetHealth()<FirstHP&&E.Enemies[1]->GetAttributes()->GetHealth()<SecondHP);
 TestEqual(TEXT("one hit sound for a wave hitting multiple enemies"),Audio->EventCount,Before+1);
 TestTrue(TEXT("rain hit node is selected"),Audio->LastNode==TEXT("RainHit"));
 E.Mode->BattleRules->Stop();const int32 Stopped=Audio->EventCount;E.Advance(1.f);
 TestEqual(TEXT("stopped rain cannot emit another impact"),Audio->EventCount,Stopped);
 TestEqual(TEXT("round stop leaves no active voice"),Audio->GetActiveVoiceCount(),0);
 return true;
}
#endif
