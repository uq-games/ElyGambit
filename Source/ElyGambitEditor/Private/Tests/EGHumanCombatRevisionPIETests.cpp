#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimInstance.h"
#include "SkeletalRenderPublic.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"

namespace
{
struct FHumanRevisionCapture
{
 TWeakObjectPtr<UGameViewportClient> Client; FDelegateHandle Handle;
 FString Path; bool bRequested=false;
 ~FHumanRevisionCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FHumanRevisionStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1920,1080))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

class FHumanRevisionScenario final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Human,Pilot,Enemy;
 TWeakObjectPtr<AEGVillVEmplacement> Device;
 TWeakObjectPtr<AEGCombatVisual> Manta;
 TSharedPtr<FHumanRevisionCapture> Capture;
 TArray<TSharedPtr<FHumanRevisionCapture>> OriginCaptures;
 TWeakObjectPtr<ACameraActor> OriginCamera;
 TWeakObjectPtr<AActor> PreviousViewTarget;
 FVector OriginCameraSide=FVector::ZeroVector;
 FVector Start=FVector::ZeroVector,DevicePoint=FVector::ZeroVector,EnemyPoint=FVector::ZeroVector,MantaStart=FVector::ZeroVector;
 bool bStarted=false,bPositioned=false,bChaseObserved=false,bInjected=false,bMantaStart=false,bLanding=false;
 bool bRangePositioned=false,bRangeChecked=false;float RangeEnemyHealth=0.f;
 FGuid OriginAction;int32 OriginIndex=INDEX_NONE;FName PelvisBone,SpineBone;
 FVector PelvisStart=FVector::ZeroVector,SpineStart=FVector::ZeroVector;
 float PelvisPeak=0.f,SpinePeak=0.f,StartMontageTime=0.f,LastMontageTime=0.f;
 FVector LastPelvis=FVector::ZeroVector,LastSpine=FVector::ZeroVector;
 int32 OriginSamples=0;
 TSet<int32> CompletedOriginActions;
 TSet<int32> CapturedOriginContacts;
 float PreviousOriginTargetHealth=-1.f;
 bool bCapturedSecondSwingPose=false;
 double InspectionOpenedAt=0.;bool bInspectionCaptured=false,bInspectionClosed=false;
 TWeakObjectPtr<UEGHeroInfoWidget> Inspection;
 double Deadline=0.,StartTime=0.,ChaseStart=0.; int32 InitialCommit=0;
 bool RequestOriginCapture(UWorld* World,const FString& Label)
 {
  auto* View=World?World->GetGameViewport():nullptr;
  if(!View||!View->Viewport||!View->GetGameViewportWidget().IsValid())return false;
  auto State=MakeShared<FHumanRevisionCapture>();State->Client=View;
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("FinalPolish20261111/Presentation/PIE");
  IFileManager::Get().MakeDirectory(*Directory,true);
  State->Path=FPaths::ConvertRelativePathToFull(Directory/(Label+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
  const TWeakPtr<FHumanRevisionCapture> Weak=State;const TWeakObjectPtr<UWorld> WeakWorld=World;
  FAutomationTestBase* Current=Test;
  State->Handle=View->OnDrawn().AddLambda([Weak,WeakWorld,Current]()
  {
   auto Item=Weak.Pin();if(!Item.IsValid()||Item->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   Item->bRequested=true;
   Current->AddInfo(FString::Printf(TEXT("Origin PIE viewport drawn at world %.3f, screenshot=%s"),WeakWorld.IsValid()?WeakWorld->GetTimeSeconds():-1.,*Item->Path));
   FScreenshotRequest::RequestScreenshot(Item->Path,true,false,false,FIntRect(),true);
  });
  OriginCaptures.Add(State);return true;
 }
 void UpdateOriginCamera(UWorld* World,bool bOrigin)
 {
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  if(!PC)return;
  if(!bOrigin)
  {
   if(OriginCamera.IsValid())
   {
    if(PreviousViewTarget.IsValid())PC->SetViewTarget(PreviousViewTarget.Get());
    OriginCamera->Destroy();OriginCamera.Reset();PreviousViewTarget.Reset();
   }
   return;
  }
  if(!Human.IsValid()||!Enemy.IsValid())return;
  const FVector Mid=(Human->GetActorLocation()+Enemy->GetActorLocation())*.5f+FVector(0,0,140.f);
  if(!OriginCamera.IsValid())
  {
   PreviousViewTarget=PC->GetViewTarget();
   const FVector Axis=(Enemy->GetActorLocation()-Human->GetActorLocation()).GetSafeNormal2D();
   OriginCameraSide=FVector(-Axis.Y,Axis.X,0.f);
   FVector OldLocation;FRotator OldRotation;PC->GetPlayerViewPoint(OldLocation,OldRotation);
   if(FVector::DotProduct(OriginCameraSide,OldLocation-Mid)<0.f)OriginCameraSide*=-1.f;
   if(OriginCameraSide.IsNearlyZero())OriginCameraSide=FVector(0,-1,0);
   OriginCamera=World->SpawnActor<ACameraActor>();
   if(!OriginCamera.IsValid())return;
   OriginCamera->GetCameraComponent()->SetFieldOfView(56.f);PC->SetViewTarget(OriginCamera.Get());
   Test->AddInfo(TEXT("Test-only close side camera used for origin contacts; normal view restored before finisher."));
  }
  const FVector Position=Mid+OriginCameraSide*1800.f+FVector(0,0,1050.f);
  OriginCamera->SetActorLocationAndRotation(Position,(Mid-Position).Rotation());
 }
 void ReportChase(const TCHAR* Label)
 {
  if(!Human.IsValid()||!Enemy.IsValid()||!Device.IsValid())return;
  const auto* Combat=Human->GetCombat();const auto* Skill=Human->GetHeroSkill();const auto* Stats=Human->GetAttributes();
  const AEGChessUnit* Blocker=nullptr;const TCHAR* Reason=nullptr;
  const FVector Probe=DevicePoint+(EnemyPoint-Start).GetSafeNormal2D()*500.f;
  const bool bDirect=Combat->IsBoardPathClear(Human->GetActorLocation(),Probe,Enemy.Get(),nullptr,&Blocker,&Reason);
  TArray<FVector> Route;FVector Chosen;const bool bRoute=Combat->PlanBoardRoute({Probe},Route,Chosen,Enemy.Get());
  const auto* Movement=Human->GetCharacterMovement();
  Test->AddInfo(FString::Printf(TEXT("%s chase state: location=%s enemy=%s device=%s speed=%.2f attackSpeed=%.2f range=%.2f tick=%d canAct=%d busy=%d action=%d flight=%d externalBlock=%d landingPending=%d state=%d paused=%d movementMode=%d target=%s nearest=%s expectedValid=%d expectedInRange=%d mutualIgnore=%d/%d direct=%d reason=%s blocker=%s route=%d points=%d chosen=%s probe=%s"),
   Label,*Human->GetActorLocation().ToString(),*Enemy->GetActorLocation().ToString(),*Device->GetActorLocation().ToString(),Stats->GetMoveSpeed(),Stats->GetAttackSpeed(),Combat->GetEffectiveAttackRange(Enemy.Get()),
   Combat->IsComponentTickEnabled()?1:0,Combat->CanAct()?1:0,Skill->IsBusy()?1:0,Skill->IsActionRunning()?1:0,Skill->bInFlight?1:0,
   Combat->HasExternalActionBlock()?1:0,Combat->IsBoardLandingPending()?1:0,int32(Human->GetCombatState_Implementation()),Human->GetWorld()->IsPaused()?1:0,
   Movement?int32(Movement->MovementMode):-1,*GetNameSafe(Combat->GetTarget()),*GetNameSafe(Combat->FindNearestEnemy()),
   Combat->IsValidEnemy(Enemy.Get())?1:0,Combat->IsInAttackRange(Enemy.Get())?1:0,
   Human->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Device.Get())?1:0,Device->GetCapsuleComponent()->GetMoveIgnoreActors().Contains(Human.Get())?1:0,
   bDirect?1:0,Reason?Reason:TEXT("None"),*GetNameSafe(Blocker),bRoute?1:0,Route.Num(),*Chosen.ToString(),*Probe.ToString()));
 }
public:
 explicit FHumanRevisionScenario(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Human/Vill-V controlled PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Cards=Mode->GetCards();FGuid HumanCard,VillCard;
   for(const auto& Card:Cards->GetHand())if(Card.Definition&&Card.Definition->HeroDefinition)
   {
    if(Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher)HumanCard=Card.InstanceId;
    if(Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::VillV)VillCard=Card.InstanceId;
   }
   const auto Grant=[&](const TCHAR* Path,FGuid& Id)
   {
    if(Id.IsValid())return true;auto* Card=LoadObject<UEGCardDefinition>(nullptr,Path);
    if(!Card||!Cards->GrantCard(Card))return false;Id=Cards->GetHand().Last().InstanceId;return true;
   };
   if(!Test->TestTrue(TEXT("Human card available"),Grant(TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"),HumanCard))||
      !Test->TestTrue(TEXT("Vill-V card available"),Grant(TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV.DA_Card_VillV"),VillCard))||
      !Test->TestTrue(TEXT("Human deployed"),Cards->TryDeployHandCard(HumanCard,{3,0}))||
      !Test->TestTrue(TEXT("Vill-V deployed"),Cards->TryDeployHandCard(VillCard,{0,0}))||
      !Test->TestTrue(TEXT("Formal combat starts"),Mode->EnterCombat()))return true;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetDefinition())
   {
    auto* ASC=Unit->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
    if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=Unit;
    if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetDefinition()->HeroKind==EEGHeroKind::VillV)Pilot=Unit;
    if(Unit->GetFaction_Implementation()==EEGFaction::Enemy){Unit->GetCombat()->SetComponentTickEnabled(false);if(!Enemy.IsValid())Enemy=Unit;}
   }
   if(!Test->TestTrue(TEXT("Human, Vill-V and enemy spawned"),Human.IsValid()&&Pilot.IsValid()&&Enemy.IsValid()))return true;
   Test->AddInfo(TEXT("Controlled PIE: programmatic deployment, elevated health, enemy and Vill-V mechanism AI disabled, actors repositioned, human range set to 900 cm and mana later injected; human movement, montage, finisher and damage run on their production paths. Not natural play, real input, visual acceptance or a balance test."));
   bStarted=true;return false;
  }
  if(!Human.IsValid()||!Pilot.IsValid()||!Enemy.IsValid()){Test->AddError(TEXT("Controlled units disappeared"));return true;}
  if(!Mode->BattleRules->IsCombatRunning())return false;
  auto* Board=Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
  if(!Test->TestNotNull(TEXT("Production board"),Board))return true;
  if(!bRangeChecked)
  {
   if(!bRangePositioned)
   {
    Pilot->GetCombat()->SetComponentTickEnabled(false);
    for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit!=Human.Get()&&Unit!=Enemy.Get())
     Unit->SetActorLocation(FVector(10000,10000,88),false,nullptr,ETeleportType::TeleportPhysics);
    Start=Board->CellToWorld({3,0})+FVector(0,0,88);
    Human->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
    Enemy->SetActorLocation(Board->CellToWorld({3,4})+FVector(0,0,88),false,nullptr,ETeleportType::TeleportPhysics);
    Human->GetCombat()->ResetCombat();
    Test->TestEqual(TEXT("Formal human attack range is five cells without override"),Human->GetAttributes()->GetAttackRange(),2500.f);
    RangeEnemyHealth=Enemy->GetAttributes()->GetHealth();StartTime=World->GetTimeSeconds();bRangePositioned=true;
    return false;
   }
   if(Enemy->GetAttributes()->GetHealth()<RangeEnemyHealth)
   {
    Test->TestTrue(TEXT("Rear row arrow hits four cells away without chasing"),FVector::Dist2D(Start,Human->GetActorLocation())<50.f);
    FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HumanRework20261110/PIE/RearRowFormalRange.png")),true,false);
    bRangeChecked=true;return false;
   }
   if(World->GetTimeSeconds()-StartTime>8.){Test->AddError(TEXT("Formal rear row shot did not hit within eight seconds"));return true;}
   return false;
  }
  if(!bPositioned)
  {
   auto* Mechanism=Pilot->FindComponentByClass<UEGVillVMechanismComponent>();
   auto* Emplacement=Mechanism?Mechanism->GetDevice(0):nullptr;
   if(!IsValid(Emplacement))return false;
   Device=Emplacement;Pilot->GetCombat()->SetComponentTickEnabled(false);Mechanism->SetComponentTickEnabled(false);
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit!=Human.Get()&&Unit!=Enemy.Get()&&Unit!=Device.Get())
    Unit->SetActorLocation(FVector(10000,10000,88),false,nullptr,ETeleportType::TeleportPhysics);
   Start=Board->CellToWorld({3,0})+FVector(0,0,88);DevicePoint=Board->CellToWorld({3,1})+FVector(0,0,88);
   EnemyPoint=Board->CellToWorld({3,6})+FVector(0,0,88);
   Human->SetActorLocation(Start,false,nullptr,ETeleportType::TeleportPhysics);
   Device->SetActorLocation(DevicePoint,false,nullptr,ETeleportType::TeleportPhysics);
   Enemy->SetActorLocation(EnemyPoint,false,nullptr,ETeleportType::TeleportPhysics);
   Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),900.f);
   Human->GetCombat()->ResetCombat();Human->RefreshBoardCollision();Device->RefreshBoardCollision();
   InitialCommit=Human->GetHeroSkill()->CommitCount;StartTime=World->GetTimeSeconds();ChaseStart=StartTime;bPositioned=true;
   Test->AddInfo(FString::Printf(TEXT("Chase fixture human=%s device=%s enemy=%s attackRange=%.1f"),*Start.ToString(),*DevicePoint.ToString(),*EnemyPoint.ToString(),Human->GetCombat()->GetEffectiveAttackRange(Enemy.Get())));
   ReportChase(TEXT("Initial"));
   return false;
  }
  if(!bChaseObserved)
  {
   const FVector Travel=EnemyPoint-Start;const FVector Direction=Travel.GetSafeNormal2D();
   const float Forward=FVector::DotProduct(Human->GetActorLocation()-Start,Direction);
   const float DeviceForward=FVector::DotProduct(DevicePoint-Start,Direction);
   if(Forward>DeviceForward+50.f&&Human->GetHeroSkill()->CommitCount>InitialCommit)
   {
    FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HumanRework20261110/PIE/Bow.png")),true,false);
    bChaseObserved=true;Test->AddInfo(FString::Printf(TEXT("Human passed the friendly device and committed an attack after %.2fs; travel=%.1f cm"),World->GetTimeSeconds()-ChaseStart,Forward));
   }
   else if(World->GetTimeSeconds()-ChaseStart>15.)
   {ReportChase(TEXT("Timeout"));Test->AddError(FString::Printf(TEXT("Human did not pass device and attack: forward=%.1f device=%.1f commits=%d"),Forward,DeviceForward,Human->GetHeroSkill()->CommitCount-InitialCommit));return true;}
   return false;
  }
  if(!bInjected)
  {
   Human->GetHeroSkill()->Configure(Mode->BattleRules,11,false);
   Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Human->GetAttributes()->GetMaxMana());
   PreviousOriginTargetHealth=Enemy->GetAttributes()->GetHealth();
   bInjected=true;Test->AddInfo(TEXT("Injected full mana after chase to observe the actual entry and natural finisher."));return false;
  }
  auto* Policy=Cast<UEGElysiaHerrscherSkillBehavior>(Human->GetHeroSkill()->GetBehavior());
  auto* Presentation=Human->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
  if(!Test->TestNotNull(TEXT("Human behavior"),Policy)||!Test->TestNotNull(TEXT("Human presentation"),Presentation))return true;
  const auto Phase=Policy->GetPresentationPhase();
  UpdateOriginCamera(World,Phase==EEGElysiaHerrscherPresentationPhase::Origin);
  auto* Skill=Human->GetHeroSkill();auto* Mesh=Human->GetVisiblePresentationMesh();
  const auto* Definition=Human->GetDefinition();const auto* P=Definition?Definition->PresentationDefinition.Get():nullptr;
  const float CurrentTargetHealth=Enemy->GetAttributes()->GetHealth();
  const bool bTargetDamaged=PreviousOriginTargetHealth>=0.f&&CurrentTargetHealth<PreviousOriginTargetHealth;
  PreviousOriginTargetHealth=CurrentTargetHealth;
  if(OriginAction.IsValid()&&Mesh&&(!Skill->IsActionRunning()||Skill->ActionId!=OriginAction))
  {
   // Measure actual world centimetres relative to the actor position. This
   // removes gameplay translation without dividing out the displayed scale.
   const FVector Pelvis=Mesh->GetBoneLocation(PelvisBone)-Human->GetActorLocation();
   const FVector Spine=Mesh->GetBoneLocation(SpineBone)-Human->GetActorLocation();
   const float Limit=.5f*FMath::Min(Board->CellWidth*Board->GetActorTransform().GetScaledAxis(EAxis::X).Size(),
                                  Board->CellDepth*Board->GetActorTransform().GetScaledAxis(EAxis::Y).Size());
   const float EndPelvis=FVector::Dist2D(Pelvis,PelvisStart),EndSpine=FVector::Dist2D(Spine,SpineStart);
   Test->AddInfo(FString::Printf(TEXT("Origin stage %d action=%s mesh=%s pelvis=%s spine=%s meshWorldScale=%s actorScale=%s basis=world-bone-minus-actor-location XY limit=%.2f samples=%d montageTime=%.3f..%.3f startPelvis=%s startSpine=%s lastActivePelvis=%s lastActiveSpine=%s endPelvis=%s endSpine=%s peakPelvis=%.2f peakSpine=%.2f returnPelvis=%.2f returnSpine=%.2f"),
    OriginIndex,*OriginAction.ToString(),*GetNameSafe(Mesh),*PelvisBone.ToString(),*SpineBone.ToString(),*Mesh->GetComponentScale().ToString(),*Human->GetActorScale3D().ToString(),
    Limit,OriginSamples,StartMontageTime,LastMontageTime,*PelvisStart.ToString(),*SpineStart.ToString(),*LastPelvis.ToString(),*LastSpine.ToString(),
    *Pelvis.ToString(),*Spine.ToString(),PelvisPeak,SpinePeak,EndPelvis,EndSpine));
   Test->TestTrue(FString::Printf(TEXT("Origin stage %d peak pelvis %.2f spine %.2f within half cell %.2f"),OriginIndex,PelvisPeak,SpinePeak,Limit),
    OriginSamples>1&&PelvisPeak<=Limit&&SpinePeak<=Limit);
   Test->TestTrue(FString::Printf(TEXT("Origin stage %d returns pelvis %.2f spine %.2f within half cell %.2f"),OriginIndex,EndPelvis,EndSpine,Limit),
    EndPelvis<=Limit&&EndSpine<=Limit);
   CompletedOriginActions.Add(OriginIndex);OriginAction.Invalidate();OriginIndex=INDEX_NONE;
  }
  if(Phase==EEGElysiaHerrscherPresentationPhase::Origin&&Skill->IsActionRunning()&&!Skill->IsCasting()&&Mesh&&P)
  {
   int32 Index=INDEX_NONE;
   for(int32 I=3;I<=5&&P->AttackSequence.IsValidIndex(I);++I)
    if(P->AttackSequence[I].Montage.Get()==Skill->GetActiveMontage()){Index=I;break;}
   auto* Anim=Mesh->GetAnimInstance();auto* Montage=Skill->GetActiveMontage();
   const float MontageTime=Anim&&Montage?Anim->Montage_GetPosition(Montage):0.f;
   if(Index==4&&!bCapturedSecondSwingPose&&MontageTime>=2.85f&&!FScreenshotRequest::IsScreenshotRequested())
   {
    Test->AddInfo(FString::Printf(TEXT("Second scepter action comparison pose requested at montage %.3f"),MontageTime));
    bCapturedSecondSwingPose=RequestOriginCapture(World,TEXT("OriginAttack2Pose285"));
   }
   if(bTargetDamaged&&(Index==3||Index==4)&&!CapturedOriginContacts.Contains(Index)&&!FScreenshotRequest::IsScreenshotRequested())
   {
    int32 VisibleMaces=0;
    const bool bMacePointExists=Mesh->DoesSocketExist(TEXT("MacePoint"));
    Test->AddInfo(FString::Printf(TEXT("Origin contact %d source MacePoint exists=%d world=%s rightHand=%s"),Index,bMacePointExists?1:0,
     *Mesh->GetSocketLocation(TEXT("MacePoint")).ToString(),*Mesh->GetSocketLocation(TEXT("Bip001-R-Hand")).ToString()));
    for(TActorIterator<AEGCombatVisual> It(World);It;++It)
    {
     if(It->GetOwner()!=Human.Get()||It->IsActorBeingDestroyed())continue;
     TArray<USkeletalMeshComponent*> Parts;It->GetComponents(Parts);
     for(const auto* Part:Parts)
     {
      if(!IsValid(Part)||!Part->GetSkeletalMeshAsset()||!Part->GetSkeletalMeshAsset()->GetName().Contains(TEXT("SK_C2_Mace_CM")))continue;
      Test->AddInfo(FString::Printf(TEXT("Origin contact %d montage=%.3f mace=%s visible=%d parent=%s socket=%s world=%s scale=%s bounds=%s target=%s"),
       Index,MontageTime,*Part->GetName(),Part->IsVisible()?1:0,*GetNameSafe(Part->GetAttachParent()),*Part->GetAttachSocketName().ToString(),
       *Part->GetComponentLocation().ToString(),*Part->GetComponentScale().ToString(),*Part->Bounds.BoxExtent.ToString(),*Enemy->GetActorLocation().ToString()));
      if(Part->IsVisible()&&Part->GetAttachParent()==Mesh&&Part->GetAttachSocketName()==TEXT("Bip001-R-Hand"))++VisibleMaces;
     }
    }
    Test->TestEqual(FString::Printf(TEXT("Origin stage %d has one visible mace attached to the live right hand"),Index),VisibleMaces,1);
    Test->AddInfo(TEXT("Actual health loss contact observed; next game-viewport draw requested, separate visual inspection required."));
    if(RequestOriginCapture(World,FString::Printf(TEXT("OriginContact%d"),Index-2)))CapturedOriginContacts.Add(Index);
   }
   // BeginAction can run after the mesh tick. Wait for an advancing montage
   // before taking a baseline so it cannot be the preceding action's pose.
   if(Index!=INDEX_NONE&&!OriginAction.IsValid()&&MontageTime>.01f)
   {
    PelvisBone=TEXT("Bip001-Pelvis");SpineBone=TEXT("Bip001-Spine2");
    if(!Test->TestTrue(TEXT("Origin body exposes exact skeleton pelvis and Spine2"),
      Mesh->GetBoneIndex(PelvisBone)!=INDEX_NONE&&Mesh->GetBoneIndex(SpineBone)!=INDEX_NONE))return true;
    OriginAction=Skill->ActionId;OriginIndex=Index;
    PelvisStart=Mesh->GetBoneLocation(PelvisBone)-Human->GetActorLocation();
    SpineStart=Mesh->GetBoneLocation(SpineBone)-Human->GetActorLocation();
    LastPelvis=PelvisStart;LastSpine=SpineStart;PelvisPeak=SpinePeak=0.f;OriginSamples=0;
    StartMontageTime=LastMontageTime=MontageTime;
   }
   if(OriginAction.IsValid()&&Skill->ActionId==OriginAction)
   {
    LastPelvis=Mesh->GetBoneLocation(PelvisBone)-Human->GetActorLocation();
    LastSpine=Mesh->GetBoneLocation(SpineBone)-Human->GetActorLocation();
    PelvisPeak=FMath::Max(PelvisPeak,float(FVector::Dist2D(PelvisStart,LastPelvis)));
    SpinePeak=FMath::Max(SpinePeak,float(FVector::Dist2D(SpineStart,LastSpine)));
    LastMontageTime=MontageTime;++OriginSamples;
   }
  }
  if(Phase==EEGElysiaHerrscherPresentationPhase::Origin&&InspectionOpenedAt==0.)
  {
   auto* PC=World->GetFirstPlayerController();auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
   auto* Main=HUD?HUD->GetMainWidget():nullptr;
   if(Main)
   {
    Main->InspectUnit(Human.Get());
    const auto* Property=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));
    Inspection=Property?Cast<UEGHeroInfoWidget>(Property->GetObjectPropertyValue_InContainer(Main)):nullptr;
    if(Inspection.IsValid())if(auto* Button=Cast<UButton>(Inspection->GetWidgetFromName(TEXT("Button_BasicInfo"))))Button->OnClicked.Broadcast();
    InspectionOpenedAt=World->GetTimeSeconds();
   }
  }
  if(InspectionOpenedAt>0.&&!bInspectionCaptured&&World->GetTimeSeconds()-InspectionOpenedAt>.4)
  {
   Test->TestTrue(TEXT("Origin inspection skill stays open through data refresh"),Inspection.IsValid()&&Inspection->HasSkillDetailsOpen());
   if(Inspection.IsValid())if(auto* Title=Cast<UTextBlock>(Inspection->GetWidgetFromName(TEXT("ActionDetailsTitle"))))
    Test->TestEqual(TEXT("Origin keeps original skill name"),Title->GetText().ToString(),FString(TEXT("落花刹那，光翳明灭")));
   FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HumanRework20261110/PIE/OriginInspection.png")),true,false);bInspectionCaptured=true;
  }
  if(bInspectionCaptured&&!bInspectionClosed&&World->GetTimeSeconds()-InspectionOpenedAt>1.)
  {
   if(Inspection.IsValid())if(auto* Button=Cast<UButton>(Inspection->GetWidgetFromName(TEXT("Button_CloseInfo"))))Button->OnClicked.Broadcast();
   bInspectionClosed=true;
  }
  if(!Manta.IsValid()&&Presentation->BoardFinisherAssembly.IsValid())
   for(TActorIterator<AEGCombatVisual> It(World);It;++It)
    if(!It->IsActorBeingDestroyed()&&It->GetOwner()==Human.Get()&&It->Definition==Presentation->BoardFinisherAssembly.Get()){Manta=*It;break;}
  if(!bMantaStart&&Manta.IsValid()&&Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin)
  {MantaStart=Manta->GetActorLocation();bMantaStart=true;Test->AddInfo(FString::Printf(TEXT("Manta start %s"),*MantaStart.ToString()));}
  if(!bLanding&&Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased)
  {
   Test->TestTrue(TEXT("Captured actual successful contacts from both scepter attack clips"),CapturedOriginContacts.Contains(3)&&CapturedOriginContacts.Contains(4));
   Test->TestTrue(TEXT("Captured second scepter action comparison pose"),bCapturedSecondSwingPose);
   Test->TestTrue(TEXT("Both ordinary scepter attacks completed; no retired wave"),CompletedOriginActions.Contains(3)&&CompletedOriginActions.Contains(4)&&!CompletedOriginActions.Contains(5));
   if(!Test->TestTrue(TEXT("Manta spawn was observed before landing"),bMantaStart&&Manta.IsValid()))return true;
   auto* View=World->GetGameViewport();
   if(!Test->TestTrue(TEXT("PIE game viewport exists"),View&&View->Viewport&&View->GetGameViewportWidget().IsValid()))return true;
   Capture=MakeShared<FHumanRevisionCapture>();Capture->Client=View;
   Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HumanRework20261110/PIE/MantaLanding_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
   const TWeakPtr<FHumanRevisionCapture> Weak=Capture;const TWeakObjectPtr<AEGCombatVisual> WeakManta=Manta;
   const FVector Initial=MantaStart;const FVector Player=Board->GetActorTransform().TransformPosition(FVector(0,(.5f*Board->PlayerRows-.5f*Board->Rows)*Board->CellDepth,0));
   const FVector Opponent=Board->GetActorTransform().TransformPosition(FVector(0,(.5f*(Board->PlayerRows+Board->NeutralRows+Board->Rows)-.5f*Board->Rows)*Board->CellDepth,0));
   FAutomationTestBase* Current=Test;
   Capture->Handle=View->OnDrawn().AddLambda([Weak,WeakManta,Initial,Player,Opponent,Current]()
   {
    auto State=Weak.Pin();if(!State.IsValid()||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
    if(WeakManta.IsValid())
    {
     const FVector Expected=Initial+(Opponent-Player);const FVector Actual=WeakManta->GetActorLocation();
     Current->TestTrue(TEXT("Manta reaches opposing half at the finisher contact frame"),FVector::Dist2D(Expected,Actual)<100.f);
     Current->AddInfo(FString::Printf(TEXT("Manta landing expected=%s actual=%s screenshot=%s"),*Expected.ToString(),*Actual.ToString(),*State->Path));
     TArray<USkeletalMeshComponent*> Parts;WeakManta->GetComponents(Parts);int32 VisibleParts=0;
     float Lowest=TNumericLimits<float>::Max();
     for(const auto* Part:Parts)if(IsValid(Part)&&Part->IsVisible()&&Part->GetSkeletalMeshAsset())
     {
      ++VisibleParts;const float Bottom=Part->Bounds.Origin.Z-Part->Bounds.BoxExtent.Z;Lowest=FMath::Min(Lowest,Bottom);
      Current->AddInfo(FString::Printf(TEXT("Manta visible mesh %s bounds origin=%s extent=%s bottom=%.1f ground=%.1f"),
       *Part->GetName(),*Part->Bounds.Origin.ToString(),*Part->Bounds.BoxExtent.ToString(),Bottom,Opponent.Z));
      // CPU skinning is a slow, editor-only diagnostic, sampled once on the
      // actual landing draw. Component Bounds can be conservative and do not
      // establish where the visible, deformed geometry really lands.
      TArray<FFinalSkinVertex> Vertices;Part->GetCPUSkinnedVertices(Vertices,0);
      FBox WorldVertices(ForceInit);const FTransform ComponentToWorld=Part->GetComponentTransform();
      for(const auto& Vertex:Vertices)WorldVertices+=ComponentToWorld.TransformPosition(FVector(Vertex.Position));
      const FName RootBone=Part->GetNumBones()>0?Part->GetBoneName(0):NAME_None;
      const FVector RootWorld=RootBone.IsNone()?FVector::ZeroVector:Part->GetBoneLocation(RootBone);
      Current->AddInfo(FString::Printf(TEXT("Manta CPU-skinned mesh=%s asset=%s lod=0 count=%d worldMin=%s worldMax=%s worldCenter=%s rootBone0=%s rootWorld=%s boardGroundZ=%.2f; includes all LOD vertices, not visibility/section-clipped geometry"),
       *Part->GetName(),*GetNameSafe(Part->GetSkeletalMeshAsset()),Vertices.Num(),*WorldVertices.Min.ToString(),*WorldVertices.Max.ToString(),
       *WorldVertices.GetCenter().ToString(),*RootBone.ToString(),*RootWorld.ToString(),Opponent.Z));
      Current->TestTrue(TEXT("Manta landing CPU-skinned vertex sample is available"),Vertices.Num()>0&&WorldVertices.IsValid);
     }
     Current->TestTrue(TEXT("Visible manta mesh bounds reach the board at contact"),VisibleParts>0&&Lowest<=Opponent.Z+250.f&&Lowest>=Opponent.Z-250.f);
    }
    else Current->AddError(TEXT("Manta vanished before the landing frame was drawn"));
    State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
   });
   bLanding=true;return false;
  }
  if(bLanding&&Capture.IsValid()&&Capture->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
  {
   for(const auto& OriginCapture:OriginCaptures)
   {
    if(!OriginCapture.IsValid()||!OriginCapture->bRequested)return false;
    TArray<uint8> Image;if(!FFileHelper::LoadFileToArray(Image,*OriginCapture->Path)||Image.Num()<36)return false;
    const int32 ImageEnd=Image.Num()-12;
    if(Image[ImageEnd+4]!='I'||Image[ImageEnd+5]!='E'||Image[ImageEnd+6]!='N'||Image[ImageEnd+7]!='D')return false;
    const auto ReadBE=[&Image](int32 Offset){return (uint32(Image[Offset])<<24)|(uint32(Image[Offset+1])<<16)|(uint32(Image[Offset+2])<<8)|uint32(Image[Offset+3]);};
    Test->TestEqual(FString::Printf(TEXT("Origin PIE image %s is 1080p"),*OriginCapture->Path),
     FIntPoint(int32(ReadBE(16)),int32(ReadBE(20))),FIntPoint(1920,1080));
   }
   TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
   const int32 End=Bytes.Num()-12;if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
   const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
   Test->TestEqual(TEXT("Manta landing image is 1080p"),FIntPoint(int32(BE(16)),int32(BE(20))),FIntPoint(1920,1080));
   Test->AddInfo(TEXT("Captured image requires separate visual inspection; automated geometry is not visual acceptance."));return true;
  }
  if(World->GetTimeSeconds()-StartTime>45.){Test->AddError(TEXT("Finisher landing was not observed"));return true;}
  return false;
 }
};

class FGriseoLowCanvasScenario final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;TWeakObjectPtr<AEGChessUnit> Griseo;TSharedPtr<FHumanRevisionCapture> Capture;
 bool bStarted=false,bMana=false,bRequested=false;double Deadline=0.,BattleStart=0.;
public:
 explicit FGriseoLowCanvasScenario(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+120.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Controlled Griseo field test timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->BattleRules)return false;
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Cards=Mode->GetCards();FGuid Id;
   for(const auto& Entry:Cards->GetHand())if(Entry.Definition&&Entry.Definition->HeroDefinition&&Entry.Definition->HeroDefinition->HeroKind==EEGHeroKind::Griseo)Id=Entry.InstanceId;
   if(!Id.IsValid())
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo.DA_Card_Griseo"));
    if(!Test->TestNotNull(TEXT("formal Griseo card"),Card)||!Test->TestTrue(TEXT("grant Griseo fixture card"),Cards->GrantCard(Card)))return true;
    Id=Cards->GetHand().Last().InstanceId;
   }
   if(!Test->TestTrue(TEXT("deploy Griseo"),Cards->TryDeployHandCard(Id,{3,1}))||!Test->TestTrue(TEXT("start formal combat"),Mode->EnterCombat()))return true;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetDefinition())
   {
    auto* ASC=Unit->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
    if(Unit->GetFaction_Implementation()==EEGFaction::Enemy)Unit->GetCombat()->SetComponentTickEnabled(false);
    else if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Griseo)Griseo=Unit;
   }
   if(!Test->TestTrue(TEXT("Griseo combat incarnation exists"),Griseo.IsValid()))return true;
   Test->AddInfo(TEXT("Controlled Griseo PIE: programmatic card grant/deploy, ten million health, enemy AI disabled, full mana injected once; Release and field playback advance naturally. Screenshot does not prove natural gameplay or visual acceptance."));
   bStarted=true;return false;
  }
  if(!Griseo.IsValid()){Test->AddError(TEXT("Griseo disappeared"));return true;}
  if(!Mode->BattleRules->IsCombatRunning())return false;
  if(!bMana){BattleStart=World->GetTimeSeconds();Griseo->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Griseo->GetAttributes()->GetMaxMana());bMana=true;return false;}
  const auto* Data=Cast<UEGGriseoSkillDefinition>(Griseo->GetDefinition()->SkillDefinition);
  if(!Test->TestNotNull(TEXT("formal Griseo skill"),Data))return true;
  auto* Definition=Data->UltimateField.Get();
  if(Definition&&!Test->TestTrue(TEXT("formal field binds the revised LowCanvas asset"),Definition->GetPathName().Contains(TEXT("FX_PaintedBarrier_LowCanvas"))))return true;
  AEGSourceEffect* Field=nullptr;
  for(TActorIterator<AEGSourceEffect> It(World);It;++It)
   if(!It->IsActorBeingDestroyed()&&It->GetOwner()==Griseo.Get()&&It->GetSourceDefinition()==Definition&&Definition){Field=*It;break;}
  if(Field&&!bRequested&&Field->GetElapsedTime()>=2.4f&&Field->GetElapsedTime()<=3.f)
  {
   auto* Board=Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
   if(!Test->TestNotNull(TEXT("Griseo field board"),Board))return true;
   const FVector Up=Board->GetActorUpVector(),Ground=Board->GetActorLocation();
   TArray<UStaticMeshComponent*> Parts;Field->GetComponents(Parts);int32 Active=0;float MaxAxis=0.f,MaxHeight=-FLT_MAX;
   const FVector Center=Field->GetActorLocation();
   for(const auto* Part:Parts)if(IsValid(Part)&&Part->IsVisible()&&Part->GetStaticMesh())
   {
    ++Active;const FVector Offset=Part->Bounds.Origin-Center,Extent=Part->Bounds.BoxExtent;
    const float Axis=FMath::Max(FMath::Abs(Offset.X)+Extent.X,FMath::Abs(Offset.Y)+Extent.Y);
    const float Top=FVector::DotProduct(Part->Bounds.Origin-Ground,Up)+FVector::DotProduct(Extent,Up.GetAbs());
    MaxAxis=FMath::Max(MaxAxis,Axis);MaxHeight=FMath::Max(MaxHeight,Top);
    Test->AddInfo(FString::Printf(TEXT("LowCanvas component %s origin=%s extent=%s maxWorldAxisBound=%.1f topAboveBoard=%.1f"),
     *Part->GetName(),*Part->Bounds.Origin.ToString(),*Extent.ToString(),Axis,Top));
   }
   Test->TestTrue(TEXT("LowCanvas has visible field parts"),Active>0);
   Test->TestTrue(TEXT("LowCanvas AABB axis remains close to field extent"),MaxAxis<=Data->FieldRadius+200.f);
   Test->TestTrue(TEXT("LowCanvas reaches above the board without becoming a tall wall"),MaxHeight>0.f&&MaxHeight<=250.f);
   auto* View=World->GetGameViewport();if(!Test->TestTrue(TEXT("Griseo game viewport exists"),View&&View->Viewport&&View->GetGameViewportWidget().IsValid()))return true;
   Capture=MakeShared<FHumanRevisionCapture>();Capture->Client=View;
   Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("HumanCombatRevision20261107/PIE/GriseoField_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);const TWeakPtr<FHumanRevisionCapture> Weak=Capture;
   Capture->Handle=View->OnDrawn().AddLambda([Weak](){auto State=Weak.Pin();if(!State.IsValid()||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
    State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);});
   bRequested=true;Test->AddInfo(FString::Printf(TEXT("LowCanvas age %.3f max AABB axis %.1f top above board %.1f screenshot %s; AABB is not a radial geometry measurement"),Field->GetElapsedTime(),MaxAxis,MaxHeight,*Capture->Path));return false;
  }
  if(bRequested&&Capture.IsValid()&&Capture->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
  {
   TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
   const int32 End=Bytes.Num()-12;if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
   Test->AddInfo(TEXT("Griseo image requires separate visual inspection, including whether any white obstruction belongs to body rather than field."));return true;
  }
  if(World->GetTimeSeconds()-BattleStart>25.){Test->AddError(TEXT("Naturally released Griseo LowCanvas field was not observed"));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanCombatRevisionPIE,
 "ElyGambit.Integration.HumanCombatRevision1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanCombatRevisionPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Test requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FHumanRevisionStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FHumanRevisionScenario(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGriseoLowCanvasPIE,
 "ElyGambit.Integration.GriseoLowCanvas1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGriseoLowCanvasPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Test requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FHumanRevisionStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FGriseoLowCanvasScenario(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
