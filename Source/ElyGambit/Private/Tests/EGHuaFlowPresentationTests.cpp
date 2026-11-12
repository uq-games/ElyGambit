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
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHitFeedbackComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "EngineUtils.h"

namespace
{
struct FHuaIncomingFixture
{
 TStrongObjectPtr<UGameInstance> GI;
 UWorld* World=nullptr;
 AEGDemoGameMode* Mode=nullptr;
 AEGCardManager* Cards=nullptr;
 FHuaIncomingFixture(FAutomationTestBase& Test):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();World=GI->GetWorld();
  World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;World->SetGameMode(URL);Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  Mode->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(Base)Mode->RunDefinition=DuplicateObject<UEGRunDefinition>(Base,Mode);
  if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Mode->RunDefinition.Get(),Test))return;
  auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
  auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  Cards=World->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
  Scene->CombatBoard=World->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  World->InitializeActorsForPlay(URL);World->BeginPlay();
 }
 ~FHuaIncomingFixture()
 {
  if(!World)return;
  World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();
  World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 }
 UEGCardDefinition* CopyCard(const TCHAR* Path,UEGSourcePreviewDefinition* HitAssembly)
 {
  auto* Source=LoadObject<UEGCardDefinition>(nullptr,Path);
  if(!Source||!Source->HeroDefinition||!Source->HeroDefinition->PresentationDefinition)return nullptr;
  auto* Card=DuplicateObject<UEGCardDefinition>(Source,Mode);
  Card->HeroDefinition=DuplicateObject<UEGHeroDefinition>(Source->HeroDefinition,Card);
  auto* P=DuplicateObject<UEGHeroPresentationDefinition>(Card->HeroDefinition->PresentationDefinition,Card->HeroDefinition);
  Card->HeroDefinition->PresentationDefinition=P;P->HitAssembly=HitAssembly;
  return Card;
 }
 FGuid Deploy(UEGCardDefinition* Card,int32 X)
 {
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{X,2}))return FGuid();
  return Mode->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHuaIncomingPresentation,
 "ElyGambit.HuaFlow.IncomingDamageAndPresentation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHuaIncomingPresentation::RunTest(const FString&)
{
 FHuaIncomingFixture F(*this);
 if(!TestNotNull(TEXT("Run configuration"),F.Mode->RunDefinition.Get())||!TestNotNull(TEXT("Fixture cards"),F.Cards))return false;
 auto* HitAssembly=LoadObject<UEGSourcePreviewDefinition>(nullptr,
  TEXT("/Game/Blueprints/Characters/FuHua/Data/Completion20261028/Assemblies/DA_Hua_Run"));
 auto* Profile=LoadObject<UEGHitFeedbackProfile>(nullptr,
  TEXT("/Game/Blueprints/Characters/Enemies/Melee/Data/Effects/DA_HitFeedback_Monster_Melee"));
 if(!TestNotNull(TEXT("Real candidate hit assembly"),HitAssembly)||
    !TestTrue(TEXT("Candidate has an observable motion"),!HitAssembly->Motions.IsEmpty()&&HitAssembly->Motions[0].Duration>0)||
    !TestNotNull(TEXT("Real contact feedback profile"),Profile)||
    !TestTrue(TEXT("Contact profile has layers"),!Profile->Layers.IsEmpty()))return false;
 auto* HuaCard=F.CopyCard(TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"),HitAssembly);
 auto* SakuraCard=F.CopyCard(TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"),HitAssembly);
 auto* PardoCard=F.CopyCard(TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"),HitAssembly);
 const FGuid HuaId=F.Deploy(HuaCard,2),PardoId=F.Deploy(PardoCard,1),SakuraId=F.Deploy(SakuraCard,3);
 if(!TestTrue(TEXT("Deploy controlled heroes"),HuaId.IsValid()&&PardoId.IsValid()&&SakuraId.IsValid())||
    !TestTrue(TEXT("Enter production combat"),F.Mode->EnterCombat()))return false;
 AEGChessUnit* Hua=nullptr;AEGChessUnit* Sakura=nullptr;AEGChessUnit* Pardo=nullptr;AEGChessUnit* Enemy=nullptr;
 for(auto* Unit:F.Mode->GetCombatUnits())
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  auto* ASC=Unit->GetAbilitySystemComponent();
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
  if(Unit->GetInstanceId()==HuaId)Hua=Unit;
  else if(Unit->GetInstanceId()==SakuraId)Sakura=Unit;
  else if(Unit->GetInstanceId()==PardoId)Pardo=Unit;
  else if(!Enemy&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Enemy=Unit;
 }
 if(!TestNotNull(TEXT("Hua combat incarnation"),Hua)||!TestNotNull(TEXT("Sakura combat incarnation"),Sakura)||!TestNotNull(TEXT("Other hero incarnation"),Pardo)||
    !TestNotNull(TEXT("Production enemy"),Enemy))return false;
 Hua->GetHitFeedback()->Profile=Profile;Sakura->GetHitFeedback()->Profile=Profile;Enemy->GetHitFeedback()->Profile=Profile;Pardo->GetHitFeedback()->Profile=Profile;
 Hua->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 const float InitialHealth=Hua->GetAttributes()->GetHealth();
 const float ExpectedMana=FMath::Min(Hua->GetAttributes()->GetMaxMana(),Hua->GetDefinition()->GetManaOnDamage());
 if(!TestTrue(TEXT("Configured mana-on-damage can be observed"),ExpectedMana>0))return false;
 TestFalse(TEXT("Hua is not acting before received damage"),Hua->GetHeroSkill()->IsActionRunning());
 const int32 HuaEffects=Hua->GetHitFeedback()->PlayedCount;
 TestTrue(TEXT("Real enemy damage accepted by Hua"),FEGCombatExecution::Damage(Enemy,Hua,25));
 TestTrue(TEXT("Received damage still reduces Hua health"),Hua->GetAttributes()->GetHealth()<InitialHealth);
 TestTrue(TEXT("Received damage still restores configured mana"),FMath::IsNearlyEqual(Hua->GetAttributes()->GetMana(),ExpectedMana,.01f));
 TestEqual(TEXT("Hua does not emit target-owned contact FX"),Hua->GetHitFeedback()->PlayedCount,HuaEffects);
 TestEqual(TEXT("Hua creates no active incoming bursts"),Hua->GetHitFeedback()->GetActiveBurstCount(),0);
 Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 const float SakuraHealth=Sakura->GetAttributes()->GetHealth();
 const float SakuraMana=FMath::Min(Sakura->GetAttributes()->GetMaxMana(),Sakura->GetDefinition()->GetManaOnDamage());
 const int32 SakuraEffects=Sakura->GetHitFeedback()->PlayedCount;
 TestTrue(TEXT("Sakura mana-on-damage can be observed"),SakuraMana>0);
 TestFalse(TEXT("Sakura is not acting before received damage"),Sakura->GetHeroSkill()->IsActionRunning());
 const float EnemyHealthBefore=Enemy->GetAttributes()->GetHealth();
 TestTrue(TEXT("The first real enemy hit is accepted by Sakura"),FEGCombatExecution::Damage(Enemy,Sakura,25));
 TestTrue(TEXT("Sakura does not counter the attacker"),FMath::IsNearlyEqual(Enemy->GetAttributes()->GetHealth(),EnemyHealthBefore,.01f));
 TestTrue(TEXT("Received damage still reduces Sakura health"),Sakura->GetAttributes()->GetHealth()<SakuraHealth);
 TestTrue(TEXT("Received damage still restores Sakura mana"),FMath::IsNearlyEqual(Sakura->GetAttributes()->GetMana(),SakuraMana,.01f));
 TestEqual(TEXT("Sakura does not emit target-owned contact FX"),Sakura->GetHitFeedback()->PlayedCount,SakuraEffects);
 TestEqual(TEXT("Sakura creates no active incoming bursts"),Sakura->GetHitFeedback()->GetActiveBurstCount(),0);
 const int32 EnemyEffects=Enemy->GetHitFeedback()->PlayedCount;
 TestTrue(TEXT("Hua can damage the ordinary enemy"),FEGCombatExecution::Damage(Hua,Enemy,25));
 TestEqual(TEXT("Ordinary enemy still emits its contact effect"),Enemy->GetHitFeedback()->PlayedCount,EnemyEffects+1);
 TestTrue(TEXT("Other hero is idle before received damage"),!Pardo->GetHeroSkill()->IsActionRunning());
 TestTrue(TEXT("Real enemy damage accepted by the other hero"),FEGCombatExecution::Damage(Enemy,Pardo,25));
 ++GFrameCounter;F.World->Tick(LEVELTICK_All,.04f);
 bool bHuaHitRig=false,bSakuraHitRig=false,bPardoHitRig=false;
 for(TActorIterator<AEGCombatVisual> It(F.World);It;++It)
  if(!It->IsActorBeingDestroyed()&&It->bDriveBodyPose&&It->BodyState==4&&It->Definition==HitAssembly)
  {bHuaHitRig|=It->GetOwner()==Hua;bSakuraHitRig|=It->GetOwner()==Sakura;bPardoHitRig|=It->GetOwner()==Pardo;}
 TestFalse(TEXT("Hua ignores an explicitly configured received-hit assembly"),bHuaHitRig);
 TestFalse(TEXT("Sakura ignores an explicitly configured received-hit assembly"),bSakuraHitRig);
 TestTrue(TEXT("Other hero still instantiates the same received-hit assembly"),bPardoHitRig);
 return true;
}
#endif
