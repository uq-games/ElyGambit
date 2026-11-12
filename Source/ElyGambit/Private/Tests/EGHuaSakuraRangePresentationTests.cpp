#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGSourceEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHuaSakuraRangePresentation,
 "ElyGambit.HuaSakuraRange.FixedWorldAreaAndCleanup",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHuaSakuraRangePresentation::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Game mode"),GM))return false;
 GM->UnitClass=AEGDemoCombatUnit::StaticClass();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);
 if(!TestNotNull(TEXT("Run definition"),Base))return false;
 auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,*this))return false;Run->bRoundShopRules=true;Run->StartingCards=1;
 Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
 Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto Deploy=[&](const TCHAR* Folder,const TCHAR* Name,int32 X)
 {
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,*FString::Printf(TEXT("/Game/Blueprints/Characters/%s/Data/DA_Card_%s"),Folder,Name));
  if(!Card||!Cards->GrantCard(Card)||!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{X,2}))return FGuid();
  return GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
 };
 const FGuid HuaId=Deploy(TEXT("FuHua"),TEXT("Hua"),1),SakuraId=Deploy(TEXT("Sakura"),TEXT("Sakura"),3),PardoId=Deploy(TEXT("Pardofelis"),TEXT("Pardo"),5);
 if(!TestTrue(TEXT("Three formal heroes enter combat"),HuaId.IsValid()&&SakuraId.IsValid()&&PardoId.IsValid()&&GM->EnterCombat()))return false;
 AEGChessUnit *Hua=nullptr,*Sakura=nullptr,*Pardo=nullptr;
 for(auto* Unit:GM->GetCombatUnits())
 {
  if(Unit->GetInstanceId()==HuaId)Hua=Unit;
  if(Unit->GetInstanceId()==SakuraId)Sakura=Unit;
  if(Unit->GetInstanceId()==PardoId)Pardo=Unit;
  Unit->GetCombat()->SetComponentTickEnabled(false);
 }
 if(!TestTrue(TEXT("Combat units exist"),Hua&&Sakura&&Pardo))return false;
 auto* Battle=Hua->GetHeroSkill()->Battle.Get();if(!TestNotNull(TEXT("Active battle"),Battle))return false;
 auto* Geometry=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 if(!TestTrue(TEXT("Fixture assets exist"),Geometry&&Material))return false;
 TStrongObjectPtr<UEGSourceEffectDefinition> FX(NewObject<UEGSourceEffectDefinition>());
 FX->bTargetGround=true;FX->PresentationScale=1.5f;
 FEGSourceEffectTrack Track;Track.Mesh=Geometry;Track.Material=Material;Track.Duration=.65f;
 Track.Samples.Add(FEGSourceEffectSample());FX->Tracks.Add(Track);
 auto BurstCount=[&]()
 {
  int32 Count=0;for(TActorIterator<AEGPresentationBurst> It(W);It;++It)if(!It->IsActorBeingDestroyed())++Count;return Count;
 };
 auto DescentRingCount=[&]()
 {
  int32 Count=0;for(TActorIterator<AEGDescentRing> It(W);It;++It)if(!It->IsActorBeingDestroyed())++Count;return Count;
 };
 auto Exercise=[&](AEGChessUnit* Unit,const TCHAR* Label)
 {
  auto* Data=Unit->GetDefinition()->SkillDefinition.Get();auto* Skill=Unit->GetHeroSkill();
  if(!TestTrue(FString::Printf(TEXT("%s has skill config"),Label),Data&&Skill))return false;
  // Guard in-memory fixture overrides; no asset is marked dirty or saved.
  TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardEffect(Data->AreaPresentation,FX.Get());
  TGuardValue<float> GuardRadius(Data->AreaPresentationReferenceRadius,100.f);
  Skill->CancelAction();Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Unit->GetAttributes()->GetMaxMana());
  if(!TestTrue(FString::Printf(TEXT("%s starts a real ultimate"),Label),Skill->BeginAction(true)))return false;
  if(Unit==Hua)
  {
   const int32 Rings=DescentRingCount();Skill->AnimationEvent(TEXT("Takeoff"));
   TestEqual(TEXT("Hua takeoff does not create a fallback circle"),DescentRingCount(),Rings);
  }
  const FVector Point=Unit->GetActorLocation()+FVector(130,-90,-85);const FRotator Facing=Unit->GetActorRotation();
  const int32 Before=BurstCount();Battle->Visual(Unit,Point,200.f,true);
  TWeakObjectPtr<AEGSourceEffect> Found;int32 Matches=0;
  for(TActorIterator<AEGSourceEffect> It(W);It;++It)
   if(It->GetOwner()==Unit&&It->GetSourceDefinition()==FX.Get()&&!It->IsActorBeingDestroyed()){Found=*It;++Matches;}
  if(!TestEqual(FString::Printf(TEXT("%s owns one area effect"),Label),Matches,1))return false;
  TestEqual(FString::Printf(TEXT("%s has no generic circle"),Label),BurstCount(),Before);
  TestTrue(TEXT("Area starts at requested world point"),Found->GetActorLocation().Equals(Point,.01f));
  TestTrue(TEXT("Area scale follows configured radius"),Found->GetActorScale3D().Equals(FVector(3.f),.01f));
  TestTrue(TEXT("Area faces the unit without attachment"),Found->GetActorRotation().Equals(Facing,.01f)&&!Found->GetAttachParentActor());
  Unit->SetActorLocation(Unit->GetActorLocation()+FVector(30,0,0));Found->Tick(.01f);
  TestTrue(TEXT("Area stays at the damage point if the owner moves"),Found->GetActorLocation().Equals(Point,.01f));
  Battle->Visual(Unit,Point,200.f,true);int32 Repeated=0;
  for(TActorIterator<AEGSourceEffect> It(W);It;++It)if(It->GetOwner()==Unit&&It->GetSourceDefinition()==FX.Get()&&!It->IsActorBeingDestroyed())++Repeated;
  TestEqual(TEXT("Repeated visual request does not duplicate this action"),Repeated,1);
  Skill->CancelAction();
  TestTrue(TEXT("Cancellation immediately destroys the action effect"),!Found.IsValid()||Found->IsActorBeingDestroyed());
  return true;
 };
 if(!Exercise(Hua,TEXT("Hua"))||!Exercise(Sakura,TEXT("Sakura")))return false;
 for(auto* Unit:{Hua,Sakura})
 {
  auto* Data=Unit->GetDefinition()->SkillDefinition.Get();
  TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardEffect(Data->AreaPresentation,TSoftObjectPtr<UEGSourceEffectDefinition>());
  const int32 Before=BurstCount();Battle->Visual(Unit,Unit->GetActorLocation(),200.f,true);
  TestEqual(TEXT("Missing dedicated area does not fall back to a generic circle"),BurstCount(),Before);
 }
 const int32 Before=BurstCount();Battle->Visual(Pardo,Pardo->GetActorLocation(),200.f,true);
 TestEqual(TEXT("Other hero keeps existing generic area presentation"),BurstCount(),Before+1);
 auto* HuaData=Hua->GetDefinition()->SkillDefinition.Get();auto* SakuraData=Sakura->GetDefinition()->SkillDefinition.Get();
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardHua(HuaData->AreaPresentation,FX.Get());
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> GuardSakura(SakuraData->AreaPresentation,FX.Get());
 auto StartArea=[&](AEGChessUnit* Unit)->TWeakObjectPtr<AEGSourceEffect>
 {
  auto* Skill=Unit->GetHeroSkill();Skill->CancelAction();
  Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Unit->GetAttributes()->GetMaxMana());
  if(!Skill->BeginAction(true))return nullptr;
  Battle->Visual(Unit,Unit->GetActorLocation()-FVector(0,0,85),200.f,true);
  for(TActorIterator<AEGSourceEffect> It(W);It;++It)
   if(It->GetOwner()==Unit&&It->GetSourceDefinition()==FX.Get()&&!It->IsActorBeingDestroyed())return *It;
  return nullptr;
 };
 TWeakObjectPtr<AEGSourceEffect> DeathEffect=StartArea(Sakura);
 TWeakObjectPtr<AEGSourceEffect> RoundEffect=StartArea(Hua);
 if(!TestTrue(TEXT("Both lifetime fixtures are active"),DeathEffect.IsValid()&&RoundEffect.IsValid()))return false;
 Sakura->Die();
 TestTrue(TEXT("Death immediately clears owned area"),!DeathEffect.IsValid()||DeathEffect->IsActorBeingDestroyed());
 Battle->Stop();
 TestTrue(TEXT("Battle stop immediately clears owned area"),!RoundEffect.IsValid()||RoundEffect->IsActorBeingDestroyed());
 return true;
}
#endif
