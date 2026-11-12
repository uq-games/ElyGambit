#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
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
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Presentation/EGElysiaCombatVisual.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"

namespace
{
struct FElysiaPolishWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr;
 AEGDemoGameMode* GM=nullptr;
 AEGChessUnit* Hero=nullptr;
 AEGChessUnit* Enemy=nullptr;
 explicit FElysiaPolishWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);GM=World->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  GM->RunDefinition=EGProductionTestAssets::ResolveRun(Test);
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=World->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=World->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  World->InitializeActorsForPlay(URL);World->BeginPlay();
  UEGCardDefinition* Card=GM->RunDefinition?GM->RunDefinition->StartingCard.Get():nullptr;
  if(!Card&&GM->RunDefinition)for(const auto& Candidate:GM->RunDefinition->CardPool)
   if(Candidate&&Candidate->HeroDefinition&&Candidate->HeroDefinition->HeroKind==EEGHeroKind::Elysia){Card=Candidate.Get();break;}
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2})||!GM->EnterCombat())return;
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* Unit:GM->GetCombatUnits())
  {
   Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetCharacterMovement()->DisableMovement();
   if(Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;
   else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Enemy=Unit;
  }
 }
 ~FElysiaPolishWorld(){if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
 void Advance(float Seconds)
 {
  while(Seconds>0.f){const float Delta=FMath::Min(Seconds,.02f);++GFrameCounter;World->Tick(LEVELTICK_All,Delta);Seconds-=Delta;}
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaDomeExitLifecycle,
 "ElyGambit.Elysia.Polish.DomeExitClearsOnBehaviorEnd",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaDomeExitLifecycle::RunTest(const FString&)
{
 FElysiaPolishWorld E(*this);
 if(!TestNotNull(TEXT("Ordinary Elysia fixture"),E.Hero)||!TestNotNull(TEXT("Enemy fixture"),E.Enemy))return false;
 auto* Skill=E.Hero->GetHeroSkill();
 auto* Behavior=Cast<UEGElysiaSkillBehavior>(Skill->GetBehavior());
 auto* Data=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/ToonProjectiles2/Effects/NS_Hit_1.NS_Hit_1"));
 if(!TestNotNull(TEXT("Behavior"),Behavior)||!TestNotNull(TEXT("Skill data"),Data)||!TestNotNull(TEXT("Existing finite Niagara fixture"),System))return false;
 // The configured enemy Corruption version retains its old dome semantics;
 // use the legacy policy explicitly instead of changing the player contract.
 TGuardValue<bool> LegacyDome(Data->bArrowRainUltimate,false);
 TGuardValue<bool> FullPresentation(Data->bBasicOnlyPresentation,false);
 TGuardValue<float> FieldDuration(Data->EmpoweredSeconds,.2f);
 TGuardValue<float> ExitDuration(Data->DomeExitSeconds,5.f);
 FEGElysiaNiagaraCue ExitCue;ExitCue.System=System;
 TGuardValue<FEGElysiaNiagaraCue> ScopedExitCue(Data->DomeExitCue,ExitCue);
 FEGSkillActionContext Cast;
 Cast.RoundId=E.GM->BattleRules->GetRoundId();Cast.ActionId=FGuid::NewGuid();Cast.Target=E.Enemy;Cast.bUltimate=true;
 Behavior->ActionStarted(Cast);Behavior->ActionEvent(TEXT("Release"),Cast);Behavior->ActionEnded(true);
 AEGElysiaCombatVisual* Dome=nullptr;
 for(TActorIterator<AEGElysiaCombatVisual> It(E.World);It;++It)if(It->VisualKind==2&&It->GetOwner()==E.Hero)Dome=*It;
 if(!TestNotNull(TEXT("Ultimate creates its field"),Dome))return false;
 E.Advance(.3f);
 TWeakObjectPtr<AEGElysiaCombatVisual> Exit;
 for(TActorIterator<AEGElysiaCombatVisual> It(E.World);It;++It)if(It->VisualKind==6&&It->GetOwner()==E.Hero)Exit=*It;
 if(!TestTrue(TEXT("Natural expiry creates a finite exit cue"),Exit.IsValid()))return false;
 TestTrue(TEXT("Battle is still active when the old behavior ends"),E.GM->BattleRules->IsBattleActive());
 Behavior->EndBattle();
 TestTrue(TEXT("EndBattle removes the exit without waiting for its duration"),!Exit.IsValid()||Exit->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRainTailLifecycle,
 "ElyGambit.Elysia.Polish.RainTailClearsOnBehaviorEnd",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRainTailLifecycle::RunTest(const FString&)
{
 FElysiaPolishWorld E(*this);
 if(!TestNotNull(TEXT("Ordinary Elysia fixture"),E.Hero)||!TestNotNull(TEXT("Enemy fixture"),E.Enemy))return false;
 auto* Skill=E.Hero->GetHeroSkill();
 auto* Behavior=Cast<UEGElysiaSkillBehavior>(Skill->GetBehavior());
 auto* Data=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/ToonProjectiles2/Effects/NS_Hit_1.NS_Hit_1"));
 if(!TestNotNull(TEXT("Behavior"),Behavior)||!TestNotNull(TEXT("Skill data"),Data)||!TestNotNull(TEXT("Niagara fixture"),System))return false;
 TGuardValue<bool> FullPresentation(Data->bBasicOnlyPresentation,false);
 TGuardValue<bool> RainPolicy(Data->bArrowRainUltimate,true);
 TGuardValue<float> RainSeconds(Data->ArrowRainSeconds,.2f);
 TGuardValue<int32> Pulses(Data->ArrowRainPulses,2);
 FEGElysiaNiagaraCue Cue;Cue.System=System;
 TGuardValue<FEGElysiaNiagaraCue> RainCue(Data->RainCue,Cue);
 TGuardValue<FEGElysiaNiagaraCue> ImpactCue(Data->RainImpactCue,Cue);
 for(auto* Unit:E.GM->GetCombatUnits())if(Unit&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)
  if(auto* EnemyASC=Unit->GetAbilitySystemComponent())
  {EnemyASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);EnemyASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);}
 auto* ASC=E.Hero->GetAbilitySystemComponent();
 if(!TestNotNull(TEXT("Hero ability system"),ASC))return false;
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),E.Hero->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("Real ultimate begins"),Skill->BeginAction(true)))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
 TWeakObjectPtr<AEGElysiaCombatVisual> Rain;
 for(TActorIterator<AEGElysiaCombatVisual> It(E.World);It;++It)if(It->VisualKind==2&&It->GetOwner()==E.Hero)Rain=*It;
 if(!TestTrue(TEXT("Release creates the rain visual"),Rain.IsValid()))return false;
 E.Advance(.25f);
 TestFalse(TEXT("Damage rain ends naturally"),Behavior->IsArrowRainActive());
 TestTrue(TEXT("Last pulse keeps a short visual tail"),Rain.IsValid()&&!Rain->IsActorBeingDestroyed());
 TestTrue(TEXT("Battle remains active before forced cleanup"),E.GM->BattleRules->IsBattleActive());
 Behavior->EndBattle();
 TestTrue(TEXT("EndBattle removes the tail immediately"),!Rain.IsValid()||Rain->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaChargedArrowCue,
 "ElyGambit.Elysia.Polish.ChargedArrowUsesConfiguredCue",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaChargedArrowCue::RunTest(const FString&)
{
 FElysiaPolishWorld E(*this);
 if(!TestNotNull(TEXT("Ordinary Elysia fixture"),E.Hero)||!TestNotNull(TEXT("Enemy fixture"),E.Enemy))return false;
 auto* Data=Cast<UEGElysiaSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* NormalSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/ToonProjectiles2/Effects/NS_Projectile_1.NS_Projectile_1"));
 auto* ChargedSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/ToonProjectiles2/Effects/NS_Projectile_2.NS_Projectile_2"));
 if(!TestNotNull(TEXT("Skill data"),Data)||!TestNotNull(TEXT("Normal fixture cue"),NormalSystem)||!TestNotNull(TEXT("Charged fixture cue"),ChargedSystem))return false;
 FEGElysiaNiagaraCue Normal;Normal.System=NormalSystem;
 FEGElysiaNiagaraCue Charged;Charged.System=ChargedSystem;
 TGuardValue<FEGElysiaNiagaraCue> ScopedNormal(Data->ArrowCue,Normal);
 TGuardValue<FEGElysiaNiagaraCue> ScopedCharged(Data->ChargedArrowCue,Charged);
 const FVector Start=E.Hero->GetActionOrigin(),End=E.Enemy->GetActorLocation();
 auto* NormalVisual=AEGElysiaCombatVisual::Spawn(E.Hero,E.Enemy,Start,End,.5f,0,false);
 auto* ChargedVisual=AEGElysiaCombatVisual::Spawn(E.Hero,E.Enemy,Start,End,.5f,0,true);
 if(!TestNotNull(TEXT("Normal arrow visual"),NormalVisual)||!TestNotNull(TEXT("Charged arrow visual"),ChargedVisual))return false;
 const auto* NormalComponent=NormalVisual->FindComponentByClass<UNiagaraComponent>();
 const auto* ChargedComponent=ChargedVisual->FindComponentByClass<UNiagaraComponent>();
 TestTrue(TEXT("Normal attack uses normal cue"),NormalComponent&&NormalComponent->GetAsset()==NormalSystem);
 TestTrue(TEXT("Charged attack uses distinct configured cue"),ChargedComponent&&ChargedComponent->GetAsset()==ChargedSystem);
 Data->ChargedArrowCue=FEGElysiaNiagaraCue();
 auto* Fallback=AEGElysiaCombatVisual::Spawn(E.Hero,E.Enemy,Start,End,.5f,0,true);
 if(!TestNotNull(TEXT("Charged fallback visual"),Fallback))return false;
 const auto* FallbackComponent=Fallback->FindComponentByClass<UNiagaraComponent>();
 TestTrue(TEXT("Unconfigured charged cue falls back to normal"),FallbackComponent&&FallbackComponent->GetAsset()==NormalSystem);
 return true;
}
#endif
