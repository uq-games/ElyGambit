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
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHuaConfirmedHitAudio,
 "ElyGambit.HuaSakuraPresentation.HuaConfirmedHitAudio",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHuaConfirmedHitAudio::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);
 auto* Source=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"));
 auto* Sound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Assets/Characters/FuHua/Audio/UltimateLayers/AVATAR_FUKA_NL_ATTACK_B_ULTRA_READY_V01"));
 if(!TestNotNull(TEXT("Run fixture"),Base)||!TestNotNull(TEXT("Hua card"),Source)||!TestNotNull(TEXT("Resident test sound"),Sound))
 {GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return false;}
 GM->RunDefinition=DuplicateObject<UEGRunDefinition>(Base,GM);
 if(!EGProductionTestAssets::ConfigureBehaviorEnemies(GM->RunDefinition.Get(),*this)){GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return false;}
 auto* Card=DuplicateObject<UEGCardDefinition>(Source,GM);
 Card->HeroDefinition=DuplicateObject<UEGHeroDefinition>(Source->HeroDefinition,Card);
 auto* AudioConfig=NewObject<UEGHeroPresentationDefinition>(Card->HeroDefinition);
 AudioConfig->bUseEventAudio=true;FEGCombatAudioEvent Hit;Hit.Node=TEXT("Hit");Hit.Sounds.Add(Sound);AudioConfig->AudioEvents.Add(Hit);
 Card->HeroDefinition->CombatAudioOverride=AudioConfig;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
 Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 bool Ready=Cards->GrantCard(Card)&&Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2});
 const FGuid Id=Ready?GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition):FGuid();
 Ready=Id.IsValid()&&GM->EnterCombat();AEGChessUnit* Hua=nullptr;AEGChessUnit* Enemy=nullptr;
 if(Ready)for(auto* Unit:GM->GetCombatUnits())
 {
  Unit->GetCombat()->SetComponentTickEnabled(false);
  auto* ASC=Unit->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
  if(Unit->GetInstanceId()==Id)Hua=Unit;
  else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy){if(!Enemy)Enemy=Unit;else Unit->SetActorLocation(FVector(10000,10000,88));}
 }
 if(TestNotNull(TEXT("Hua in combat"),Hua)&&TestNotNull(TEXT("Enemy in combat"),Enemy))
 {
  auto* Skill=Hua->GetHeroSkill();auto* Audio=Hua->FindComponentByClass<UEGCombatAudioComponent>();
  if(TestNotNull(TEXT("Combat audio component"),Audio))
  {
   Enemy->SetActorLocation(Hua->GetActorLocation()+FVector(120,0,0));
   if(TestTrue(TEXT("Start first basic attack"),Skill->BeginAction(false)))
   {
    const float HP=Enemy->GetAttributes()->GetHealth();const int32 Before=Audio->EventCount;
    Skill->AnimationEvent(TEXT("Hit"));
    TestTrue(TEXT("Confirmed hit deals the existing damage"),Enemy->GetAttributes()->GetHealth()<HP);
    TestEqual(TEXT("Confirmed hit emits one configured sound event"),Audio->EventCount,Before+1);
    const float After=Enemy->GetAttributes()->GetHealth();Skill->AnimationEvent(TEXT("Hit"));
    TestEqual(TEXT("Duplicate notify cannot damage twice"),Enemy->GetAttributes()->GetHealth(),After);
    TestEqual(TEXT("Duplicate notify cannot emit twice"),Audio->EventCount,Before+1);
    Skill->CancelAction();Skill->AnimationEvent(TEXT("Hit"));
    TestEqual(TEXT("Late notify after cancellation is silent"),Audio->EventCount,Before+1);
    for(TActorIterator<AEGChessNiagaraBurst> It(W);It;++It)if(It->GetOwner()==Hua&&!It->IsActorBeingDestroyed())It->Tick(.01f);
   }
   Enemy->SetActorLocation(Hua->GetActorLocation()+FVector(120,0,0));
   if(TestTrue(TEXT("Start second basic attack"),Skill->BeginAction(false)))
   {
    const int32 Before=Audio->EventCount;const float HP=Enemy->GetAttributes()->GetHealth();
    TArray<TWeakObjectPtr<AEGChessNiagaraBurst>> BeforeFX;
    for(TActorIterator<AEGChessNiagaraBurst> It(W);It;++It)if(It->GetOwner()==Hua&&!It->IsActorBeingDestroyed()&&It->GetRootComponent()->GetAttachParent())BeforeFX.Add(*It);
    TestEqual(TEXT("Attack start does not create a swing"),BeforeFX.Num(),0);
    Enemy->SetActorLocation(Hua->GetActorLocation()+FVector(10000,0,0));Skill->AnimationEvent(TEXT("Hit"));
    TestEqual(TEXT("Out-of-range swing cannot deal damage"),Enemy->GetAttributes()->GetHealth(),HP);
    TestEqual(TEXT("Out-of-range swing has no hit sound"),Audio->EventCount,Before);
    TArray<TWeakObjectPtr<AEGChessNiagaraBurst>> Swing;
    for(TActorIterator<AEGChessNiagaraBurst> It(W);It;++It)if(It->GetOwner()==Hua&&!It->IsActorBeingDestroyed()&&It->GetRootComponent()->GetAttachParent())Swing.Add(*It);
    TestEqual(TEXT("Out-of-range Hit creates one swing"),Swing.Num(),1);
    Skill->AnimationEvent(TEXT("Hit"));int32 Count=0;
    for(TActorIterator<AEGChessNiagaraBurst> It(W);It;++It)if(It->GetOwner()==Hua&&!It->IsActorBeingDestroyed()&&It->GetRootComponent()->GetAttachParent())++Count;
    TestEqual(TEXT("Repeated Hit does not create another swing"),Count,1);
    Skill->CancelAction();
    for(auto FX:Swing)if(FX.IsValid())FX->Tick(.01f);
    for(auto FX:Swing)TestTrue(TEXT("Cancellation removes action-bound swing"),!FX.IsValid()||FX->IsActorBeingDestroyed());
   }
   // Controlled displacement exercises the production passive-assembly clock.
   // This is not a substitute for natural AI movement or visual review.
   for(int32 I=0;I<15;++I)
   {
    Hua->SetActorLocation(Hua->GetActorLocation()+FVector(16,0,0));
    ++GFrameCounter;W->Tick(LEVELTICK_All,.04f);
   }
   AEGCombatVisual* RunRig=nullptr;
   for(TActorIterator<AEGCombatVisual> It(W);It;++It)
    if(It->GetOwner()==Hua&&!It->IsActorBeingDestroyed()&&It->bDriveBodyPose&&It->BodyState==2){RunRig=*It;break;}
   if(TestNotNull(TEXT("Controlled movement selects run assembly"),RunRig))
   {
    auto* Body=Hua->GetVisiblePresentationMesh();auto* Anim=Body?Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()):nullptr;
    auto HasSkillPrerequisite=[Body,Skill](){return Body&&Body->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate([Skill](const FTickPrerequisite& P)
     {return P.PrerequisiteObject.Get()==Skill&&P.PrerequisiteTickFunction==&Skill->PrimaryComponentTick;});};
    auto* Combat=Hua->GetCombat();
    auto HasMovementPrerequisite=[Skill,Combat](){return Skill->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate([Combat](const FTickPrerequisite& P)
     {return P.PrerequisiteObject.Get()==Combat&&P.PrerequisiteTickFunction==&Combat->PrimaryComponentTick;});};
    TestTrue(TEXT("Hua run body evaluates after skill pose clock"),HasSkillPrerequisite());
    TestTrue(TEXT("Skill pose clock evaluates after combat movement"),HasMovementPrerequisite());
    TestTrue(TEXT("Displayed body stays visible during run"),Body&&Body->IsVisible());
    TestTrue(TEXT("Body runs the configured run clip"),Anim&&Anim->bRun&&Anim->RunClip==Hua->GetDefinition()->PresentationDefinition->RunAnimation.Get());
    const float Previous=RunRig->SourceTime;
    Hua->SetActorLocation(Hua->GetActorLocation()+FVector(16,0,0));++GFrameCounter;W->Tick(LEVELTICK_All,.04f);
    TestTrue(TEXT("Run assembly clock advances or loops"),RunRig->SourceTime!=Previous);
    TArray<USkeletalMeshComponent*> Parts;RunRig->GetComponents(Parts);int32 Followers=0;
    for(auto* Part:Parts)if(Part&&Part->GetSkeletalMeshAsset()&&Part->GetSkeletalMeshAsset()->GetName().Contains(TEXT("Signature_Weapon")))
    {
     ++Followers;TestTrue(TEXT("Run gauntlet is visible"),Part->IsVisible());
     TestTrue(TEXT("Run gauntlet follows displayed body"),Part->LeaderPoseComponent.Get()==Body);
    }
    TestEqual(TEXT("Both run gauntlets exist"),Followers,2);
    Enemy->SetActorLocation(Hua->GetActorLocation()+FVector(120,0,0));
    if(TestTrue(TEXT("Attack starts after run"),Skill->BeginAction(false)))
    {
     TestFalse(TEXT("Attack releases run tick prerequisite"),HasSkillPrerequisite());
     TestTrue(TEXT("Attack keeps movement prerequisite"),HasMovementPrerequisite());
     Skill->CancelAction();
    }
    // An existing dependency belongs to its original installer, not this rig.
    Body->AddTickPrerequisiteComponent(Skill);
    for(int32 I=0;I<4;++I){Hua->SetActorLocation(Hua->GetActorLocation()+FVector(16,0,0));++GFrameCounter;W->Tick(LEVELTICK_All,.04f);}
    Enemy->SetActorLocation(Hua->GetActorLocation()+FVector(120,0,0));
    if(TestTrue(TEXT("Attack starts after externally ordered run"),Skill->BeginAction(false)))
    {
     TestTrue(TEXT("Existing external prerequisite survives rig cleanup"),HasSkillPrerequisite());
     Skill->CancelAction();
    }
    Body->RemoveTickPrerequisiteComponent(Skill);
    for(int32 I=0;I<4;++I){Hua->SetActorLocation(Hua->GetActorLocation()+FVector(16,0,0));++GFrameCounter;W->Tick(LEVELTICK_All,.04f);}
    TestTrue(TEXT("Rebuilt run reinstalls tick prerequisite"),HasSkillPrerequisite());
    Skill->EndBattle();
    TestFalse(TEXT("EndBattle removes run tick prerequisite"),HasSkillPrerequisite());
    TestFalse(TEXT("EndBattle removes owned movement prerequisite"),HasMovementPrerequisite());
    Skill->AddTickPrerequisiteComponent(Combat);
    for(int32 I=0;I<4;++I){Hua->SetActorLocation(Hua->GetActorLocation()+FVector(16,0,0));++GFrameCounter;W->Tick(LEVELTICK_All,.04f);}
    Skill->EndBattle();
    TestTrue(TEXT("Preexisting movement prerequisite survives EndBattle"),HasMovementPrerequisite());
    Skill->RemoveTickPrerequisiteComponent(Combat);
    if(Skill->Battle.IsValid())Skill->Battle->Stop();
    TestFalse(TEXT("Battle stop leaves no run tick prerequisite"),HasSkillPrerequisite());
    TestFalse(TEXT("Battle stop leaves no owned movement prerequisite"),HasMovementPrerequisite());
   }
  }
 }
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 return true;
}
#endif
