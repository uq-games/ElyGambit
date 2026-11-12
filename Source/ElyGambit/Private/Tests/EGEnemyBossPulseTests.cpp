#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Skills/EGCorruptionBossBehavior.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEnemyBossPulseTest,"ElyGambit.Corruption.FirstPulseLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEnemyBossPulseTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();if(!TestNotNull(TEXT("world"),W))return false;
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;W->SetGameMode(URL);auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=7;Grid->Rows=7;Grid->CellWidth=Grid->CellDepth=500.f;
 W->InitializeActorsForPlay(URL);W->BeginPlay();auto* Mode=W->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("mode"),Mode)||!TestTrue(TEXT("combat starts"),Mode->EnterCombat()))return false;
 auto* Battle=Mode->BattleRules.Get();Battle->CompleteOpeningForTests();
 AEGChessUnit* Hero=nullptr;
 for(auto* Unit:Battle->Units())if(IsValid(Unit))
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->CancelAction();
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;
 }
 auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption"));
 if(!TestNotNull(TEXT("formal Boss definition"),Definition)||!TestNotNull(TEXT("player"),Hero))return false;
 Hero->SetActorLocation(FVector(0,0,88));
 auto* Boss=Mode->SpawnBattleSummon(Definition,1,FVector(300,0,88),EEGFaction::Enemy);
 if(!TestNotNull(TEXT("Boss actor"),Boss))return false;
 Boss->GetCombat()->SetComponentTickEnabled(false);Boss->GetHeroSkill()->CancelAction();
 Boss->GetHeroSkill()->Configure(Battle,1,false);Battle->EnterUnit(Boss);
 auto* Rules=UEGCorruptionCombatComponent::AttachBoss(Boss,Battle,10000.f,100.f,50);
 auto* Skill=Boss->GetHeroSkill();auto* Behavior=Cast<UEGCorruptionBossBehavior>(Skill->GetBehavior());
 if(!TestNotNull(TEXT("Boss component"),Rules)||!TestNotNull(TEXT("Boss policy"),Behavior))return false;
 auto* HeroASC=Hero->GetAbilitySystemComponent();HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
 HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);HeroASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 int32 Started=0,Resolved=0,Cancelled=0;FGuid LastStarted;
 Behavior->OnFirstPulse.AddLambda([&](EEGCorruptionFirstPulseEvent Event,FGuid Round,FGuid Action,FVector,float,float)
 {
  if(Round!=Battle->GetRoundId()||!Action.IsValid())return;
  if(Event==EEGCorruptionFirstPulseEvent::Started){++Started;LastStarted=Action;}
  if(Event==EEGCorruptionFirstPulseEvent::Resolved)++Resolved;
  if(Event==EEGCorruptionFirstPulseEvent::Cancelled)++Cancelled;
 });
 const auto Begin=[&](){Boss->GetCombat()->ResetCombat();Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Boss->GetAttributes()->GetMaxMana());return Skill->BeginAction(true);};
 TestEqual(TEXT("No charged fifth arrow for Boss"),Behavior->PresentationIndex(false,4),0);
 const float Initial=Hero->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Begin pulse"),Begin()))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Release"));Skill->AnimationEvent(TEXT("Finish"));
 TestEqual(TEXT("One telegraph for duplicated release"),Started,1);
 TestEqual(TEXT("Warning deals no damage"),Hero->GetAttributes()->GetHealth(),Initial);
 Behavior->TickBattle(.5f);TestEqual(TEXT("Half warning deals no damage"),Hero->GetAttributes()->GetHealth(),Initial);
 Behavior->TickBattle(.51f);
 const float Damage=100.f*Rules->FirstPulseDamageMultiplier*Boss->GetAttributes()->GetSkillMultiplier();
 TestTrue(TEXT("Pulse uses its own multiplier exactly once"),FMath::IsNearlyEqual(Initial-Hero->GetAttributes()->GetHealth(),Damage,.1f));
 TestEqual(TEXT("One resolved cue"),Resolved,1);Behavior->TickBattle(2.f);
 TestEqual(TEXT("No repeated pulse"),Resolved,1);
 const float After=Hero->GetAttributes()->GetHealth();
 if(!TestTrue(TEXT("Begin cancellable pulse"),Begin()))return false;
 Skill->AnimationEvent(TEXT("Release"));TestTrue(TEXT("New action token"),Skill->ActionId==LastStarted);
 Skill->CancelAction();Behavior->TickBattle(2.f);
 TestEqual(TEXT("Cancelled cue exactly once"),Cancelled,1);
 TestEqual(TEXT("Cancelled pulse cannot damage"),Hero->GetAttributes()->GetHealth(),After);
 if(!TestTrue(TEXT("Begin before phase change"),Begin()))return false;
 Skill->AnimationEvent(TEXT("Release"));
 FEGCombatExecution::Damage(Hero,Boss,1000000.f);
 TestEqual(TEXT("First life enters barrier"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier));
 TestEqual(TEXT("Phase transition cancels old warning"),Cancelled,2);
 return true;
}
#endif
