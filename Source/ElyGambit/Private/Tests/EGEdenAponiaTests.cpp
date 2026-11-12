#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGSourceEffect.h"
#include "Animation/AnimMontage.h"
namespace
{
struct FSupportWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGChessUnit *Eden=nullptr,*Aponia=nullptr,*Pardo=nullptr,*Enemy=nullptr;
 FSupportWorld(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return;
  GM->RunDefinition=DuplicateObject<UEGRunDefinition>(Base,GM);
  if(!EGProductionTestAssets::ConfigureBehaviorEnemies(GM->RunDefinition.Get(),Test))return;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden"),TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia"),TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo")};
  for(int32 I=0;I<3;++I)if(auto* Card=LoadObject<UEGCardDefinition>(nullptr,Paths[I])){Cards->GrantCard(Card);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{I+1,2});}
  GM->EnterCombat();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);Mana(U,0);
   if(U->GetDefinition()->HeroKind==EEGHeroKind::Eden)Eden=U;
   else if(U->GetDefinition()->HeroKind==EEGHeroKind::Aponia)Aponia=U;
   else if(U->GetDefinition()->HeroKind==EEGHeroKind::Pardofelis)Pardo=U;
   else if(!Enemy&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemy=U;
  }
  if(Enemy&&Aponia)Enemy->SetActorLocation(Aponia->GetActorLocation()+FVector(600,0,0));
 }
 ~FSupportWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 bool Ready()const{return Eden&&Aponia&&Pardo&&Enemy;}
 static void Mana(AEGChessUnit* U,float Value){U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Value);}
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);}}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEdenNotes,"ElyGambit.EdenAponia.EdenNotesAndEligibleHeroes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEdenNotes::RunTest(const FString&)
{
 FSupportWorld E(*this);if(!TestTrue(TEXT("Three support heroes deploy as ordinary population"),E.Ready()))return false;
 auto* B=E.GM->BattleRules.Get();auto* S=Cast<UEGEdenSkillBehavior>(E.Eden->GetHeroSkill()->GetBehavior());if(!TestNotNull(TEXT("Eden policy"),S))return false;
 FSupportWorld::Mana(E.Aponia,100);FSupportWorld::Mana(E.Pardo,0);
 const FGuid Id=FGuid::NewGuid();B->BasicHit(E.Eden,E.Enemy,Id);B->BasicHit(E.Eden,E.Enemy,Id);
 TestEqual(TEXT("Multiple bullets share one note"),S->Notes,1);
 B->BasicHit(E.Eden,E.Enemy,FGuid::NewGuid());TestEqual(TEXT("Two attacks do not pay"),E.Pardo->GetAttributes()->GetMana(),0.f);
 B->BasicHit(E.Eden,E.Enemy,FGuid::NewGuid());TestEqual(TEXT("Third hit restores ten to eligible ally"),E.Pardo->GetAttributes()->GetMana(),10.f);TestEqual(TEXT("One note paid"),S->NotesPaid,1);
 FSupportWorld::Mana(E.Aponia,100);FSupportWorld::Mana(E.Pardo,100);FSupportWorld::Mana(E.Eden,0);
 for(int32 I=0;I<5;++I)B->BasicHit(E.Eden,E.Enemy,FGuid::NewGuid());
 TestEqual(TEXT("No eligible recipient banks one full note"),S->Notes,3);TestEqual(TEXT("Banked note not paid to singer"),S->NotesPaid,1);
 FSupportWorld::Mana(E.Pardo,20);E.Tick(.05f);TestEqual(TEXT("Banked note pays when mana is spent"),E.Pardo->GetAttributes()->GetMana(),30.f);TestEqual(TEXT("Bank empties once"),S->Notes,0);E.Tick(.1f);TestEqual(TEXT("No repeat payment without hits"),E.Pardo->GetAttributes()->GetMana(),30.f);
 E.GM->BattleRules->Stop();TestEqual(TEXT("Round cleanup clears notes"),S->Notes,0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEnemyEdenCastTargetable,"ElyGambit.EdenAponia.EnemyEdenCastTargetable",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEnemyEdenCastTargetable::RunTest(const FString&)
{
 FSupportWorld E(*this);if(!TestTrue(TEXT("Production support fixture"),E.Ready()))return false;
 auto* Battle=E.GM->BattleRules.Get();Battle->CompleteOpeningForTests();
 auto* Copy=E.W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestNotNull(TEXT("Enemy Eden copy"),Copy)||!TestTrue(TEXT("Initialize enemy Eden"),Copy->InitializeUnit(E.Eden->GetDefinition(),FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 Copy->GetCombat()->SetComponentTickEnabled(false);
 Copy->SetActorLocation(E.Pardo->GetActorLocation()+FVector(300,0,0));
 Copy->BeginCombat_Implementation();Copy->GetHeroSkill()->Configure(Battle,1,false);
 Copy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 TArray<TWeakObjectPtr<AEGChessUnit>> Opponents{E.Pardo};Copy->GetCombat()->SetCombatants(Opponents);
 TArray<TWeakObjectPtr<AEGChessUnit>> OnlyEnemy{Copy};E.Pardo->GetCombat()->SetCombatants(OnlyEnemy);
 FSupportWorld::Mana(Copy,Copy->GetAttributes()->GetMaxMana());
 auto* Skill=Copy->GetHeroSkill();
 if(!TestTrue(TEXT("Enemy Eden starts the ordinary ultimate"),Skill->BeginAction(true)))return false;
 TestTrue(TEXT("The last enemy remains a valid target"),E.Pardo->GetCombat()->FindNearestEnemy()==Copy);
 TestFalse(TEXT("Enemy cast does not protect the caster"),Skill->IsActionProtected());
 const float BeforeHealth=Copy->GetAttributes()->GetHealth();
 TestTrue(TEXT("Opposing damage is accepted during cast"),FEGCombatExecution::Damage(E.Pardo,Copy,10.f));
 TestTrue(TEXT("Enemy health decreases during cast"),Copy->GetAttributes()->GetHealth()<BeforeHealth);
 const double BeforeTime=E.W->GetTimeSeconds();E.Tick(.1f);
 TestTrue(TEXT("World and combat continue during cast"),!E.W->IsPaused()&&Battle->IsCombatRunning()&&E.W->GetTimeSeconds()>BeforeTime);
 TestTrue(TEXT("Enemy cast remains active after being hit"),Skill->IsCasting());
 Skill->CancelAction();Copy->Destroy();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEdenSong,"ElyGambit.EdenAponia.PeriodicSongRefreshDeathAndStop",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEdenSong::RunTest(const FString&)
{
 FSupportWorld E(*this);if(!E.Ready())return false;auto* B=E.GM->BattleRules.Get();
 B->ApplyEdenSong(E.Eden,3,6);TestEqual(TEXT("Song has no instant application tick"),E.Pardo->GetAttributes()->GetMana(),0.f);E.Tick(.9f);TestEqual(TEXT("Song waits a full second"),E.Pardo->GetAttributes()->GetMana(),0.f);E.Tick(.15f);TestEqual(TEXT("One periodic GAS tick"),E.Pardo->GetAttributes()->GetMana(),3.f);
 B->ApplyEdenSong(E.Eden,3,6);E.Tick(1.05f);TestEqual(TEXT("Refreshing does not stack the rate"),E.Pardo->GetAttributes()->GetMana(),6.f);
 TestEqual(TEXT("Singer is excluded"),E.Eden->GetAttributes()->GetMana(),0.f);
 TestTrue(TEXT("Singer can die after applying song"),FEGCombatExecution::Damage(E.Enemy,E.Eden,20000));TestFalse(TEXT("Singer died"),IEGCombatant::Execute_IsAlive(E.Eden));
 E.Tick(5.1f);TestEqual(TEXT("All six refreshed ticks survive singer death"),E.Pardo->GetAttributes()->GetMana(),21.f);E.Tick(1.2f);TestEqual(TEXT("Expired song stops"),E.Pardo->GetAttributes()->GetMana(),21.f);
 B->ApplyEdenSong(E.Aponia,3,6);E.Tick(1.05f);const float Before=E.Pardo->GetAttributes()->GetMana();B->Stop();E.Tick(2.f);TestEqual(TEXT("Round stop removes active periodic effects"),E.Pardo->GetAttributes()->GetMana(),Before);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAponiaJudgment,"ElyGambit.EdenAponia.JudgmentFilteringCooldownAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAponiaJudgment::RunTest(const FString&)
{
 FSupportWorld E(*this);if(!E.Ready())return false;auto* B=E.GM->BattleRules.Get();auto* S=E.Aponia->GetHeroSkill();auto* Policy=Cast<UEGAponiaSkillBehavior>(S->GetBehavior());if(!Policy)return false;
 S->ApplyPersistentLevel(11);
 FGuid Basic=FGuid::NewGuid();B->BasicHit(E.Pardo,E.Enemy,Basic);B->BasicHit(E.Pardo,E.Enemy,Basic);TestEqual(TEXT("One allied attack adds two mana"),E.Aponia->GetAttributes()->GetMana(),2.f);
 const float Before=E.Aponia->GetAttributes()->GetMana();B->BasicHit(E.Enemy,E.Pardo,FGuid::NewGuid());TestEqual(TEXT("Enemy attack cannot supply mana"),E.Aponia->GetAttributes()->GetMana(),Before);
 auto* CanD=E.Pardo->GetDefinition()->SummonDefinition.Get();auto* Can=B->SpawnSummon(CanD,E.Pardo->GetInstanceId(),1,E.Pardo->GetActorLocation()+FVector(0,500,0));
 if(TestNotNull(TEXT("Skill-support summon exists"),Can)){Can->GetCombat()->SetComponentTickEnabled(false);B->BasicHit(Can,E.Enemy,FGuid::NewGuid());TestEqual(TEXT("Summon attacks excluded"),E.Aponia->GetAttributes()->GetMana(),Before);}
 FSupportWorld::Mana(E.Aponia,100);if(!TestTrue(TEXT("Natural cast transaction begins"),S->BeginAction(true)))return false;
 TestFalse(TEXT("Caster is untargetable"),S->IsTargetable());const float HP=E.Aponia->GetAttributes()->GetHealth();FEGCombatExecution::Damage(E.Enemy,E.Aponia,200);TestEqual(TEXT("GAS damage respects cast immunity"),E.Aponia->GetAttributes()->GetHealth(),HP);
 B->BasicHit(E.Pardo,E.Enemy,FGuid::NewGuid());TestEqual(TEXT("Cast startup does not trigger lightning or mana"),E.Aponia->GetAttributes()->GetMana(),0.f);TestEqual(TEXT("No lightning before Judge node"),Policy->LightningCount,0);
 S->AnimationEvent(TEXT("Judge"));const float TargetHP=E.Enemy->GetAttributes()->GetHealth();const int32 Attacks=B->GetAttackCount();
 Basic=FGuid::NewGuid();B->BasicHit(E.Pardo,E.Enemy,Basic);B->BasicHit(E.Pardo,E.Enemy,Basic);B->BasicHit(E.Eden,E.Enemy,FGuid::NewGuid());
 TestEqual(TEXT("Shared interval permits one lightning"),Policy->LightningCount,1);TestEqual(TEXT("Lightning is not another basic attack"),B->GetAttackCount(),Attacks+2);TestTrue(TEXT("Lightning uses original GAS damage"),FMath::IsNearlyEqual(TargetHP-E.Enemy->GetAttributes()->GetHealth(),E.Aponia->GetAttributes()->GetAttackPower()*.6f,.01f));
 E.Tick(.55f);B->BasicHit(E.Pardo,E.Enemy,FGuid::NewGuid());TestEqual(TEXT("Next allied hit after interval can trigger"),Policy->LightningCount,2);
 S->CancelAction();TestTrue(TEXT("Cancellation restores targetability"),S->IsTargetable());TestFalse(TEXT("Cancellation closes judgment"),Policy->bJudging);const float CancelHP=E.Enemy->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Late release after cancel cannot damage"),E.Enemy->GetAttributes()->GetHealth(),CancelHP);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAponiaSolo,"ElyGambit.EdenAponia.SoloVerdictAndSourceBindings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAponiaSolo::RunTest(const FString&)
{
 FSupportWorld E(*this);if(!E.Ready())return false;auto* B=E.GM->BattleRules.Get();
 FEGCombatExecution::Damage(E.Enemy,E.Eden,20000);FEGCombatExecution::Damage(E.Enemy,E.Pardo,20000);FSupportWorld::Mana(E.Aponia,100);auto* S=E.Aponia->GetHeroSkill();
 if(!TestTrue(TEXT("Solo Aponia can cast"),S->BeginAction(true)))return false;
 const float HP=E.Enemy->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Judge"));S->AnimationEvent(TEXT("Release"));const float After=E.Enemy->GetAttributes()->GetHealth();TestTrue(TEXT("Final verdict needs no ally attacks"),After<HP);S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Verdict exactly once per cast"),E.Enemy->GetAttributes()->GetHealth(),After);S->CancelAction();
 auto* P=E.Aponia->GetDefinition()->PresentationDefinition.Get();
 if(!TestNotNull(TEXT("Aponia presentation definition"),P))return false;
 TestTrue(TEXT("Original locomotion, hit and death bound"),!P->WalkAssembly.IsNull()&&!P->RunAssembly.IsNull()&&!P->DeathAssembly.IsNull()&&!P->HitAssembly.IsNull());
 // PV/source-only adaptation restored the native hand rings; only the giant
 // butterfly wings remain removed. Do not enforce the superseded ring removal.
 for(const auto& A:P->AttackAssemblies)
 {
  TStrongObjectPtr<UEGSourcePreviewDefinition> Assembly(A.LoadSynchronous());
  if(!TestNotNull(FString(TEXT("Attack assembly: "))+A.ToSoftObjectPath().ToString(),Assembly.Get()))return false;
  for(const auto& M:Assembly->Motions)
  {
   int32 Rings=0;for(const auto& Part:M.Parts)if(Part.PartId.ToString().StartsWith(TEXT("Signature_BladeRing")))++Rings;
   TestEqual(TEXT("Both original hand rings remain in each attack"),Rings,2);
  }
 }
 auto Paths=E.GM->RunDefinition->GatherRuntimeResources();auto* D=Cast<UEGAponiaSkillDefinition>(E.Aponia->GetDefinition()->SkillDefinition);
 TestTrue(TEXT("Lightning effect preloaded"),D&&Paths.Contains(D->LightningEffect.ToSoftObjectPath()));TestTrue(TEXT("Chinese voices and event SFX enabled"),P->bUseEventAudio&&P->AudioEvents.Num()>=8);
 FString Error;TestTrue(TEXT("Complete run definition graph valid"),E.GM->RunDefinition->ValidateDefinitionGraph(Error,true));AddInfo(Error);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEdenSupportRework,"ElyGambit.SupportRework.EdenHealSongAndEncore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEdenSupportRework::RunTest(const FString&)
{
 FSupportWorld E(*this);if(!TestTrue(TEXT("Fixture ready"),E.Ready()))return false;E.GM->BattleRules->CompleteOpeningForTests();
 E.Eden->SetActorLocation(FVector(0,0,100));E.Pardo->SetActorLocation(FVector(1000,0,100));E.Aponia->SetActorLocation(FVector(1100,0,100));E.Enemy->SetActorLocation(FVector(500,0,100));
 auto* S=E.Eden->GetHeroSkill();auto* ASC=E.Eden->GetAbilitySystemComponent();
 for(int32 Level:{1,6,11})
 {
  S->ApplyPersistentLevel(Level);ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);
  for(auto* U:{E.Eden,E.Pardo,E.Aponia}){U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),500);FSupportWorld::Mana(U,0);}
  FSupportWorld::Mana(E.Eden,E.Eden->GetAttributes()->GetMaxMana());const float EnemyHP=E.Enemy->GetAttributes()->GetHealth();
  if(!TestTrue(TEXT("Eden ultimate begins"),S->BeginAction(true)))return false;
  S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Self healing"),E.Eden->GetAttributes()->GetHealth(),700.f);
  TestEqual(TEXT("Nearby ally healing including boundary"),E.Pardo->GetAttributes()->GetHealth(),700.f);
  TestEqual(TEXT("Outside ally excluded from healing"),E.Aponia->GetAttributes()->GetHealth(),500.f);
  TestEqual(TEXT("Ultimate no longer damages enemy"),E.Enemy->GetAttributes()->GetHealth(),EnemyHP);
  S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Duplicate release does not double heal"),E.Eden->GetAttributes()->GetHealth(),700.f);
  S->AnimationEvent(TEXT("Finish"));E.Tick(1.05f);
  TestEqual(TEXT("Global song preserved at correct tier"),E.Aponia->GetAttributes()->GetMana(),Level>=6?5.f:3.f);
  FSupportWorld::Mana(E.Pardo,0);FSupportWorld::Mana(E.Aponia,0);
  auto* Policy=Cast<UEGEdenSkillBehavior>(S->GetBehavior());
  for(int32 N=0;N<3;++N)Policy->OwnerBasicHit(E.Enemy,FGuid::NewGuid());
  const float Sum=E.Pardo->GetAttributes()->GetMana()+E.Aponia->GetAttributes()->GetMana();
  TestEqual(TEXT("Level 11 note pays two distinct allies"),Sum,Level>=11?20.f:10.f);
  E.Tick(6.1f);
 }
 E.GM->BattleRules->Stop();return true;
}
#endif
