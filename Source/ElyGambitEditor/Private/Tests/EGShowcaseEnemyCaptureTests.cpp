#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/WidgetComponent.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Interaction/EGCombatant.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGCombatTextLayer.h"

namespace
{
class FShowcaseEnemyStartPIE final : public IAutomationLatentCommand
{
public:
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1280;Settings->NewWindowHeight=720;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).Title(FText::FromString(TEXT("ElyGambit Showcase")))
   .ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1280,720)).AutoCenter(EAutoCenter::None)
   .CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(!GUnrealEd->CheckForPlayerStart())FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

/** Demonstration fixture: formal action entry and montage notifies, not a natural round. */
class FShowcaseEnemyCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 int32 Variant=0,Step=0,SceneAction=0;
 double Deadline=0,BeginAt=0,PhaseAt=0,NextActionRetry=0,CameraLastAt=0;
 FVector CameraFocus=FVector::ZeroVector;
 TWeakObjectPtr<AEGDemoGameMode> ClockMode;
 bool bRestoreClockTick=false;
 TWeakObjectPtr<AEGChessUnit> Hero,Enemy,Ally;
 TWeakObjectPtr<UEGCorruptionCombatComponent> BossRules;
 TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<AActor> OldView;
 TWeakObjectPtr<APlayerController> PC;
 const TCHAR* ClipName() const
 {static const TCHAR* Names[]={TEXT("Chest"),TEXT("Eye"),TEXT("Spore"),TEXT("Flower"),TEXT("Boss")};return Names[Variant];}
 void Signal(const TCHAR* State,UWorld* World)
 {
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("Showcase20261111/signals");
  IFileManager::Get().MakeDirectory(*Directory,true);
  const FString Content=FString::Printf(TEXT("{\"clip\":\"%s\",\"state\":\"%s\",\"utc\":\"%s\",\"world_seconds\":%.4f,\"wall_seconds\":%.6f}"),
   ClipName(),State,*FDateTime::UtcNow().ToIso8601(),World?World->GetTimeSeconds():0.f,FPlatformTime::Seconds());
  if(!FFileHelper::SaveStringToFile(Content,*(Directory/TEXT("enemy.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
   Test->AddError(TEXT("Could not write enemy capture signal"));
 }
 bool Fail(UWorld* World,const FString& Reason)
 {Signal(TEXT("error"),World);Test->AddError(Reason);Cleanup();return true;}
 void Cleanup()
 {
  if(PC.IsValid()&&OldView.IsValid())PC->SetViewTarget(OldView.Get());
  if(Camera.IsValid())Camera->Destroy();Camera.Reset();
  if(ClockMode.IsValid()&&bRestoreClockTick)ClockMode->SetActorTickEnabled(true);
  ClockMode.Reset();bRestoreClockTick=false;
 }
 AEGChessUnit* Spawn(AEGDemoGameMode* Mode,UEGHeroDefinition* Def,FVector Point)
 {
  auto* U=Mode->SpawnBattleSummon(Def,1,Point,EEGFaction::Enemy);
  if(!U)return nullptr;
  U->GetCombat()->SetComponentTickEnabled(false);
  U->GetHeroSkill()->Configure(Mode->BattleRules,1,false);
  Mode->BattleRules->EnterUnit(U);return U;
 }
 void SetHealth(AEGChessUnit* Unit,float Maximum,float Current)
 {
  if(!Unit)return;auto* ASC=Unit->GetAbilitySystemComponent();
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),Maximum);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Current);
 }
 void UpdateBossCamera()
 {
  if(Variant!=4||!Camera.IsValid()||!Hero.IsValid()||!Enemy.IsValid())return;
  const double Now=FPlatformTime::Seconds();
  const float Delta=CameraLastAt>0?FMath::Clamp(float(Now-CameraLastAt),0.f,.05f):0.f;
  CameraLastAt=Now;
  const FVector HeroPoint=Hero->GetActorLocation(),BossPoint=Enemy->GetActorLocation();
  const FVector DesiredFocus=(HeroPoint+BossPoint)*.5f+FVector(0,0,105.f);
  // Begin widening shortly before the authored wall starts. Hold the wider
  // shot through its dissolve; afterwards the actual actor separation controls
  // the distance, so the shot naturally settles back in after their regroup.
  const double Elapsed=BeginAt>0?Now-BeginAt:0.;
  const bool bWallWindow=(Elapsed>=13.&&Elapsed<22.)||
   (BossRules.IsValid()&&BossRules->GetBossPhase()==EEGCorruptionBossPhase::Barrier);
  const float Separation=FVector::Dist2D(HeroPoint,BossPoint);
  float Distance=FMath::Clamp(1000.f+Separation*1.2f,1450.f,3200.f);
  if(bWallWindow)Distance=FMath::Max(Distance,3000.f);
  CameraFocus=FMath::VInterpTo(CameraFocus,DesiredFocus,Delta,1.35f);
  const FVector Axis=FVector(1000.f,-1050.f,0.f).GetSafeNormal();
  const FVector DesiredEye=CameraFocus+Axis*Distance+FVector(0,0,Distance*(850.f/1450.f));
  const FVector CurrentEye=Camera->GetActorLocation();
  const float CurrentDistance=FVector::Dist2D(CurrentEye,CameraFocus);
  const float Speed=Distance>CurrentDistance?1.8f:.8f;
  const FVector Eye=FMath::VInterpTo(CurrentEye,DesiredEye,Delta,Speed);
  Camera->SetActorLocationAndRotation(Eye,(CameraFocus-Eye).Rotation());
 }
 bool Act(AEGChessUnit* Unit,bool Ultimate)
 {
  if(!IsValid(Unit)||Unit->GetHeroSkill()->IsBusy())return false;
  if(Ultimate)Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Unit->GetAttributes()->GetMaxMana());
  const bool Ok=Unit->GetHeroSkill()->BeginAction(Ultimate);
  Test->AddInfo(FString::Printf(TEXT("%s requested %s: %s, montage=%s"),ClipName(),Ultimate?TEXT("ultimate"):TEXT("basic"),Ok?TEXT("started"):TEXT("not started"),*GetNameSafe(Unit->GetHeroSkill()->GetActiveMontage())));
  return Ok;
 }
public:
 FShowcaseEnemyCapture(FAutomationTestBase* In,int32 InVariant):Test(In),Variant(InVariant){}
 ~FShowcaseEnemyCapture(){Cleanup();}
 bool Update() override
 {
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Controller=World?World->GetFirstPlayerController():nullptr;
  if(!Deadline)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline)return Fail(World,FString::Printf(TEXT("%s capture timeout step %d action %d"),ClipName(),Step,SceneAction));
  if(!World||!Mode||!Mode->BattleRules||!Mode->RunDefinition||!Mode->GetBoard()||!Controller)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady()||!Mode->RunDefinition->Stages.IsValidIndex(3))return false;
   const auto Hand=Mode->GetCards()->GetHand();if(Hand.IsEmpty())return false;
   if(!Mode->GetCards()->TryDeployHandCard(Hand[0].InstanceId,{3,1})||!Mode->EnterCombat())return Fail(World,TEXT("Could not enter formal combat"));
   Step=1;return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   for(auto* U:Mode->BattleRules->Units())if(IsValid(U))
   {
    U->GetCombat()->SetComponentTickEnabled(false);U->GetHeroSkill()->CancelAction();
    if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)Hero=U;else U->Destroy();
   }
   if(!Hero.IsValid())return Fail(World,TEXT("Missing friendly target"));
   const auto& Defs=Mode->RunDefinition->Stages[3].OrdinaryMonsterDefinitions;
   if(Defs.Num()!=4)return Fail(World,TEXT("Missing four formal monster definitions"));
   UEGHeroDefinition* Definition=nullptr;
   if(Variant<4)
   {
    const EEGMonsterUltimate Skills[]={EEGMonsterUltimate::Shield,EEGMonsterUltimate::EnhancedShot,EEGMonsterUltimate::TeamSpeed,EEGMonsterUltimate::CrystalSeed};
    for(const auto& D:Defs)if(D&&D->MonsterAttack&&D->MonsterAttack->Ultimate==Skills[Variant]){Definition=D;break;}
   }
   else Definition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
   if(!Definition)return Fail(World,TEXT("Formal requested definition missing"));
   const FVector Origin=Mode->GetBoard()->CellToWorld({3,3});
   Hero->SetActorLocation(Origin+FVector(0,-200,0),false,nullptr,ETeleportType::TeleportPhysics);
   SetHealth(Hero.Get(),1000000.f,1000000.f);
   // A summon fixture is ineligible for the boss's copying candidate list.
   if(Variant==4)Hero->GetHeroSkill()->bSummon=true;
   Enemy=Spawn(Mode,Definition,Origin+FVector(0,100,0));
   if(!Enemy.IsValid())return Fail(World,TEXT("Cannot spawn formal enemy"));
   SetHealth(Enemy.Get(),100000.f,100000.f);
   if(Variant==2)
   {
    UEGHeroDefinition* ChestDef=nullptr;
    for(const auto& D:Defs)if(D&&D->MonsterAttack&&D->MonsterAttack->Ultimate==EEGMonsterUltimate::Shield){ChestDef=D;break;}
    Ally=Spawn(Mode,ChestDef,Origin+FVector(220,100,0));
    if(!Ally.IsValid())return Fail(World,TEXT("Support ally fixture unavailable"));
    SetHealth(Ally.Get(),100000.f,50000.f);
   }
   if(Variant==4)
   {
    BossRules=UEGCorruptionCombatComponent::AttachBoss(Enemy.Get(),Mode->BattleRules,100000.f,20.f,50);
    if(!BossRules.IsValid())return Fail(World,TEXT("Cannot attach boss rules"));
    BossRules->FirstSpreadDelaySeconds=2.f;BossRules->SpreadIntervalSeconds=1000.f;BossRules->MimicIntervalSeconds=1000.f;
   }
   if(auto* HUD=Cast<AEGHUD>(Controller->GetHUD()))
   {
    if(auto* W=HUD->GetMainWidget())W->RemoveFromParent();
    if(auto* W=HUD->GetRunPanel())W->RemoveFromParent();
    if(auto* W=HUD->GetCombatTextLayer())W->RemoveFromParent();
   }
   PC=Controller;OldView=Controller->GetViewTarget();
   const FVector Focus=Origin+FVector(0,0,105),Eye=Focus+FVector(1000,-1050,850);
   Camera=World->SpawnActor<ACameraActor>(Eye,(Focus-Eye).Rotation());
   if(!Camera.IsValid())return Fail(World,TEXT("Cannot create capture camera"));
   Camera->GetCameraComponent()->SetFieldOfView(55.f);Controller->SetViewTarget(Camera.Get());
   CameraFocus=Focus;CameraLastAt=FPlatformTime::Seconds();
   // Stage one ends at 25 + 15 real seconds, shorter than the Boss reel.
   // Freeze only the game-mode actor clock after combat has begun. Battle,
   // skill, animation and boss actor components continue ticking normally.
   ClockMode=Mode;bRestoreClockTick=Mode->IsActorTickEnabled();Mode->SetActorTickEnabled(false);
   Enemy->GetCombat()->SetComponentTickEnabled(true);PhaseAt=FPlatformTime::Seconds();Step=2;return false;
  }
  if(!Enemy.IsValid()||!Hero.IsValid()||!Mode->BattleRules->IsBattleActive())
   return Fail(World,FString::Printf(TEXT("Capture fixture ended unexpectedly: enemy=%d hero=%d active=%d action=%d"),
    Enemy.IsValid()?1:0,Hero.IsValid()?1:0,Mode->BattleRules->IsBattleActive()?1:0,SceneAction));
  // UI refresh may re-show widgets after a phase or attribute update. These
  // presentation-only changes belong to this temporary PIE world; no asset
  // and no authoritative gameplay state is altered.
  if(auto* HUD=Cast<AEGHUD>(Controller->GetHUD()))
  {
   if(auto* W=HUD->GetMainWidget();W&&W->IsInViewport())W->RemoveFromParent();
   if(auto* W=HUD->GetRunPanel();W&&W->IsInViewport())W->RemoveFromParent();
   if(auto* W=HUD->GetCombatTextLayer();W&&W->IsInViewport())W->RemoveFromParent();
  }
  for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
   if(auto* Vitals=Unit->FindComponentByClass<UWidgetComponent>())
   {Vitals->SetVisibility(false,true);Vitals->SetHiddenInGame(true,true);}
  UpdateBossCamera();
  if(Step==2)
  {
   // Let real AI select a valid combat target; all subsequent action requests
   // are explicit and the component is frozen to avoid unplanned cuts.
   if(!Enemy->GetCombat()->GetTarget()&&FPlatformTime::Seconds()-PhaseAt<4.)return false;
   Enemy->GetCombat()->SetComponentTickEnabled(false);
   if(!Enemy->GetCombat()->GetTarget())return Fail(World,TEXT("Enemy did not acquire a target"));
   if(Enemy->GetHeroSkill()->IsBusy()){Enemy->GetHeroSkill()->CancelAction();}
   Signal(TEXT("start"),World);BeginAt=FPlatformTime::Seconds();Step=3;return false;
  }
  if(Step==3)
  {
   const double T=FPlatformTime::Seconds()-BeginAt;
   if(SceneAction==0&&T>=2.)
   {
    if(!Act(Enemy.Get(),false))return Fail(World,TEXT("Basic action could not start"));
    ++SceneAction;
   }
   if(SceneAction==1&&T>=8.&&!Enemy->GetHeroSkill()->IsBusy())
   {
    if(!Act(Enemy.Get(),true))return Fail(World,TEXT("Ultimate action could not start"));
    ++SceneAction;
   }
   if(SceneAction==2&&T>=15.&&!Enemy->GetHeroSkill()->IsBusy())
   {
    if(Variant==4)
    {
     // Show the actual barrier transition and its authored introduction.
     if(!FEGCombatExecution::Damage(Hero.Get(),Enemy.Get(),1000000.f))return Fail(World,TEXT("Cannot start boss barrier"));
    }
    else if(!Act(Enemy.Get(),false))return Fail(World,TEXT("Second basic action could not start"));
    ++SceneAction;
   }
   if(Variant==4&&SceneAction==3&&T>=20.)
   {
    if(!BossRules.IsValid()||BossRules->GetBossPhase()!=EEGCorruptionBossPhase::Barrier)return Fail(World,TEXT("Boss did not enter barrier"));
    if(!FEGCombatExecution::Damage(Hero.Get(),Enemy.Get(),1000000.f))return Fail(World,TEXT("Cannot break boss barrier"));
    ++SceneAction;
   }
   if(Variant==4&&SceneAction==4&&T>=31.)
   {
    if(!BossRules.IsValid()||BossRules->GetBossPhase()!=EEGCorruptionBossPhase::Second)return Fail(World,TEXT("Boss did not enter second life"));
    // The real barrier pushes players to the far side of the board. Once it
    // is gone, reset only this test fixture to a legal looking firing range;
    // otherwise the frozen combat AI cannot walk back to the boss.
    Hero->SetActorLocation(Enemy->GetActorLocation()+FVector(0,-520,0),false,nullptr,ETeleportType::TeleportPhysics);
    if(!Enemy->GetCombat()->IsInAttackRange(Hero.Get()))
     return Fail(World,FString::Printf(TEXT("Boss target remains out of range: distance=%.1f, effective=%.1f"),
      FVector::Dist2D(Hero->GetActorLocation(),Enemy->GetActorLocation()),Enemy->GetCombat()->GetEffectiveAttackRange(Hero.Get())));
    BossRules->SetEffectiveBattleSeconds(60.f);++SceneAction;
   }
   if(Variant==4&&SceneAction>=5&&SceneAction<8&&T>=34.+(SceneAction-5)*4.&&
      FPlatformTime::Seconds()>=NextActionRetry&&!Enemy->GetHeroSkill()->IsBusy())
   {
    if(Act(Enemy.Get(),false))++SceneAction;
    else
    {
     NextActionRetry=FPlatformTime::Seconds()+.5;
     Test->AddInfo(FString::Printf(TEXT("Boss retry: running=%d target=%s targetable=%d inRange=%d actionBlocked=%d dist=%.1f"),
      Mode->BattleRules->IsCombatRunning()?1:0,*GetNameSafe(Enemy->GetCombat()->GetTarget()),
      Hero->GetHeroSkill()->IsTargetable()?1:0,Enemy->GetCombat()->IsInAttackRange(Hero.Get())?1:0,
      Enemy->GetCombat()->HasExternalActionBlock()?1:0,FVector::Dist2D(Hero->GetActorLocation(),Enemy->GetActorLocation())));
    }
   }
   const double EndAt=Variant==4?52.:23.;
   if(T>=EndAt)
   {
    if((Variant==4&&SceneAction<8)||(Variant!=4&&SceneAction<3))return Fail(World,TEXT("Capture actions did not complete"));
    Signal(TEXT("end"),World);PhaseAt=FPlatformTime::Seconds();Step=4;
   }
   return false;
  }
  if(Step==4)
  {
   if(FPlatformTime::Seconds()-PhaseAt<2.)return false;
   Test->AddInfo(TEXT("Showcase fixture controls positions, health, combat decisions and boss phase damage; gameplay remains montage-notify driven. No copied Flame-Chaser phantom is eligible."));
   Cleanup();return true;
  }
  return true;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowcaseEnemyChest,"ElyGambit.ShowcaseEnemy.Chest",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowcaseEnemyEye,"ElyGambit.ShowcaseEnemy.Eye",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowcaseEnemySpore,"ElyGambit.ShowcaseEnemy.Spore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowcaseEnemyFlower,"ElyGambit.ShowcaseEnemy.Flower",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShowcaseEnemyBoss,"ElyGambit.ShowcaseEnemy.Boss",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
#define SHOWCASE_ENEMY_TEST(ClassName,Index) bool ClassName::RunTest(const FString&) { \
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender()){AddError(TEXT("Showcase requires idle graphical editor"));return false;} \
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7"))); \
 ADD_LATENT_AUTOMATION_COMMAND(FShowcaseEnemyStartPIE()); \
 ADD_LATENT_AUTOMATION_COMMAND(FShowcaseEnemyCapture(this,Index)); \
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true; }
SHOWCASE_ENEMY_TEST(FShowcaseEnemyChest,0)
SHOWCASE_ENEMY_TEST(FShowcaseEnemyEye,1)
SHOWCASE_ENEMY_TEST(FShowcaseEnemySpore,2)
SHOWCASE_ENEMY_TEST(FShowcaseEnemyFlower,3)
SHOWCASE_ENEMY_TEST(FShowcaseEnemyBoss,4)
#undef SHOWCASE_ENEMY_TEST
#endif

