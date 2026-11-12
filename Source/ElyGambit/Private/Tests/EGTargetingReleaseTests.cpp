#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGPieceTestGameMode.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Placement/EGDeploymentBoard.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FTargetingReleaseWorld
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr;
 AEGPieceTestGameMode* Mode=nullptr;
 AEGChessUnit* Attacker=nullptr;
 AEGChessUnit* First=nullptr;
 AEGChessUnit* Second=nullptr;

 FTargetingReleaseWorld():GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;Grid->CellWidth=500;Grid->CellDepth=500;
  World->InitializeActorsForPlay(URL);World->BeginPlay();Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
  if(!Mode||!Mode->EnterCombat())return;
  Mode->BattleRules->CompleteOpeningForTests();
  for(const auto& Entry:Mode->GetBattleUnits())if(auto* U=Entry.Get())
  {
   U->GetCombat()->SetComponentTickEnabled(false);
   auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
   if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)Attacker=U;
   else if(U->GetDefinition()&&U->GetDefinition()->MonsterAttack&&U->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Melee)First=U;
   else Second=U;
  }
  if(!Ready())return;
  Attacker->SetActorLocation(FVector(0,0,88));First->SetActorLocation(FVector(600,0,88));Second->SetActorLocation(FVector(1000,0,88));
  auto* ASC=Attacker->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),0.f);Attacker->GetCombat()->ResetCombat();
 }
 ~FTargetingReleaseWorld()
 {if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
 bool Ready() const {return Attacker&&First&&Second;}
 void Decide() const {Attacker->GetCombat()->TickComponent(.001f,LEVELTICK_All,nullptr);}
 void Tick(float Seconds) const {for(int32 I=0;I<FMath::CeilToInt(Seconds/.01f);++I){++GFrameCounter;World->Tick(LEVELTICK_All,.01f);}}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGTargetingValidityAndTaunt,
 "ElyGambit.Combat.Targeting.ValidityAndTaunt",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGTargetingValidityAndTaunt::RunTest(const FString&)
{
 FTargetingReleaseWorld E;if(!TestTrue(TEXT("Two valid opponents"),E.Ready()))return false;
 auto* C=E.Attacker->GetCombat();auto* Skill=E.Attacker->GetHeroSkill();
 E.Decide();TestEqual(TEXT("Initial target is nearest valid enemy"),C->GetTarget(),E.First);
 E.Second->SetActorLocation(FVector(350,0,88));E.Decide();
 TestEqual(TEXT("Currently valid target is retained"),C->GetTarget(),E.First);
 Skill->SetTaunt(E.Second,.2f);E.Decide();
 TestTrue(TEXT("Valid taunt is active"),Skill->IsTauntActive());
 TestEqual(TEXT("Taunt selects its source"),C->GetTarget(),E.Second);
 E.Tick(.25f);TestFalse(TEXT("Taunt timer expires"),Skill->IsTauntActive());
 TestFalse(TEXT("Expired taunt releases forced source"),Skill->ForcedTarget.IsValid());
 E.Second->GetHeroSkill()->bInFlight=true;E.Decide();
 TestEqual(TEXT("Untargetable enemy is replaced by valid candidate"),C->GetTarget(),E.First);
 E.Second->GetHeroSkill()->bInFlight=false;E.First->Die();E.Decide();
 TestEqual(TEXT("Dead target is replaced by surviving enemy"),C->GetTarget(),E.Second);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGTargetingCommittedAttackSnapshot,
 "ElyGambit.Combat.Targeting.CommittedAttackSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGTargetingCommittedAttackSnapshot::RunTest(const FString&)
{
 FTargetingReleaseWorld E;if(!TestTrue(TEXT("Two valid opponents"),E.Ready()))return false;
 auto* C=E.Attacker->GetCombat();auto* Skill=E.Attacker->GetHeroSkill();
 const float Range=C->GetEffectiveAttackRange(E.First);
 E.First->SetActorLocation(FVector(Range-3.f,0,88));E.Second->SetActorLocation(FVector(1000,0,88));
 E.Decide();if(!TestEqual(TEXT("First target selected"),C->GetTarget(),E.First))return false;
 if(!TestTrue(TEXT("First target is within attack range"),C->IsInAttackRange(E.First)))return false;
 E.Attacker->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),1.f);
 if(!TestTrue(TEXT("Begin committed basic attack"),Skill->BeginAction(false)))return false;
 const FGuid Action=Skill->ActionId;const float FirstHealth=E.First->GetAttributes()->GetHealth();const float SecondHealth=E.Second->GetAttributes()->GetHealth();
 E.Second->SetActorLocation(FVector(-(C->GetEffectiveAttackRange(E.Second)-3.f),0,88));
 Skill->SetTaunt(E.Second,1.f);E.Decide();
 TestTrue(TEXT("Taunt does not replace a running action"),Skill->IsBusy()&&Skill->ActionId==Action);
 Skill->AnimationEvent(TEXT("Hit"));
 TestTrue(TEXT("Original valid target receives committed hit"),E.First->GetAttributes()->GetHealth()<FirstHealth);
 TestEqual(TEXT("New taunter does not receive prior hit"),E.Second->GetAttributes()->GetHealth(),SecondHealth);
 Skill->CancelAction();E.Decide();TestEqual(TEXT("Active taunt selects source after cancellation"),C->GetTarget(),E.Second);
 return true;
}
#endif
