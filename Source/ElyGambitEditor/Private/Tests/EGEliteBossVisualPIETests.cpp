#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UnrealClient.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "GameFramework/PlayerController.h"
#include "Actor/EGBoard.h"
#include "Actor/EGCorruptionSeed.h"
#include "Placement/EGDeploymentBoard.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Presentation/EGCorruptionBarrierVisual.h"
#include "Presentation/EGCorruptionBossVisual.h"
#include "Presentation/EGCorruptionPhantomVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"

namespace
{
class FEliteBossStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1920,1080))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};
struct FEliteBossCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle DrawHandle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FEliteBossCapture(){if(Client.IsValid()&&DrawHandle.IsValid())Client->OnDrawn().Remove(DrawHandle);}
};
// Uses the formal board and hero assets, but deliberately injects a Boss,
// accelerates configured intervals and applies damage. It is not a natural run.
class FEliteBossVisualRun final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 const bool bTimeoutBranch;
 int32 Step=0;
 double Deadline=0., CaptureDeadline=0., WaitUntil=0., EffectDeadline=0.;
 double IntroObservedAt=0.;
 float IntroObservedElapsed=0.f;
 int32 IntroObservationCount=0;
 bool bPushCaptureTaken=false,bObservedRevealShake=false,bObservedShakeOffset=false,bCameraBaselineValid=false;
 FVector CameraBaselineLocation=FVector::ZeroVector;
 FRotator CameraBaselineRotation=FRotator::ZeroRotator;
 TWeakObjectPtr<AActor> IntroCameraTarget;
 float CapturedSourceElapsed=0.f,CapturedSourceRemaining=0.f;
 TWeakObjectPtr<AEGChessUnit> Boss,Attacker;
 TWeakObjectPtr<UEGCorruptionCombatComponent> Rules;
 TArray<TWeakObjectPtr<AEGChessUnit>> FixtureEnemies;
 TWeakObjectPtr<AEGCorruptionSeed> ObservedSeed;
 TWeakObjectPtr<AEGCorruptionPhantomVisual> ObservedPhantom;
 TArray<FTransform> PhantomWindupPose;
 TArray<FTransform> BossWalkPose;
 FVector BossWalkStart=FVector::ZeroVector;
 float OriginalBossRange=0.f;
 bool bSampledWalk=false;
 TWeakObjectPtr<APlayerController> CameraController;
 TWeakObjectPtr<AActor> PreviousViewTarget;
 TWeakObjectPtr<ACameraActor> CloseCamera;
 FString CapturePath;
 TSharedPtr<FEliteBossCapture> Capture;
 FGuid Hold;
 FDelegateHandle FailurePause;

 void ReleaseFrozenCapture(AEGDemoGameMode* Mode)
 {
  if(Mode&&FailurePause.IsValid())Mode->ReleasePauseRequest(FailurePause);
  FailurePause.Reset();ReleaseCaptureHold(Mode);
 }
 bool BeginFrozenCapture(AEGDemoGameMode* Mode,const TCHAR* Label)
 {
  if(!BeginCapture(Mode,Label))return false;
  auto* PC=Mode->GetWorld()->GetFirstPlayerController();
  const FCanUnpause Independent=FCanUnpause::CreateLambda([](){return true;});
  if(!PC||!Mode->SetPause(PC,Independent))
  {
   Test->AddError(TEXT("Could not independently freeze the visual for capture"));
   Capture.Reset();ReleaseCaptureHold(Mode);return false;
  }
  FailurePause=Independent.GetHandle();
  if(!Mode->GetWorld()->IsPaused()||Mode->BattleRules->CanAdvancePresentationVisuals())
  {
   Test->AddError(TEXT("Independent capture pause did not freeze presentation"));
   Capture.Reset();ReleaseFrozenCapture(Mode);return false;
  }
  return true;
 }

 void ReleaseCaptureHold(AEGDemoGameMode* Mode)
 {
  if(Mode&&Mode->BattleRules&&Hold.IsValid())Mode->BattleRules->SetPresentationHold(Hold,false);
  Hold.Invalidate();
 }
 bool BeginCapture(AEGDemoGameMode* Mode,const TCHAR* Label)
 {
  if(FScreenshotRequest::IsScreenshotRequested()||(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0)return false;
  auto* Client=Mode&&Mode->GetWorld()?Mode->GetWorld()->GetGameViewport():nullptr;
  if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return false;
  CapturePath=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/
   (FString(Label)+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(CapturePath),true);
  if(!Hold.IsValid()){Hold=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(Hold,true);}
  Capture=MakeShared<FEliteBossCapture>();Capture->Client=Client;Capture->Path=CapturePath;
  const TWeakPtr<FEliteBossCapture> Weak=Capture;
  Capture->DrawHandle=Client->OnDrawn().AddLambda([Weak]()
  {
   auto State=Weak.Pin();if(!State.IsValid())return;
   ++State->Draws;
   if(State->Draws<2||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  });
  CaptureDeadline=FPlatformTime::Seconds()+12.;return true;
 }
 // 0 pending, 1 captured, -1 failed.
 int32 PollCapture(AEGDemoGameMode* Mode,bool bRelease=true)
 {
  TArray<uint8> Bytes;
  if(Capture.IsValid()&&Capture->bRequested&&Capture->Draws>=4&&!FScreenshotRequest::IsScreenshotRequested()&&
     FFileHelper::LoadFileToArray(Bytes,*CapturePath)&&Bytes.Num()>=36&&
     Bytes[Bytes.Num()-8]=='I'&&Bytes[Bytes.Num()-7]=='E'&&Bytes[Bytes.Num()-6]=='N'&&Bytes[Bytes.Num()-5]=='D')
  {
   const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
   Test->TestEqual(TEXT("Captured PIE viewport dimensions"),FIntPoint(int32(BE(16)),int32(BE(20))),FIntPoint(1920,1080));
   Test->AddInfo(TEXT("Controlled Boss viewport: ")+CapturePath);Capture.Reset();if(bRelease)ReleaseCaptureHold(Mode);return 1;
  }
  if(FPlatformTime::Seconds()>CaptureDeadline)
  {
   Test->AddError(TEXT("Game viewport screenshot did not complete: ")+CapturePath);Capture.Reset();ReleaseCaptureHold(Mode);return -1;
  }
  return 0;
 }
 void RestoreCamera()
 {
  if(CameraController.IsValid()&&PreviousViewTarget.IsValid())
  {
   CameraController->SetViewTarget(PreviousViewTarget.Get());
   if(CameraController->PlayerCameraManager)CameraController->PlayerCameraManager->UpdateCamera(0.f);
  }
  if(CloseCamera.IsValid())CloseCamera->Destroy();
  CameraController.Reset();PreviousViewTarget.Reset();CloseCamera.Reset();
 }
 bool SetCloseCamera(UWorld* World,AActor* Subject,const FVector& Facing)
 {
  if(!World||!IsValid(Subject))return false;
  auto* PC=World->GetFirstPlayerController();if(!PC||!PC->GetViewTarget())return false;
  FVector Forward=Facing.GetSafeNormal2D();if(Forward.IsNearlyZero())Forward=FVector(1,0,0);
  const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
  USkeletalMeshComponent* Mesh=nullptr;
  if(auto* Unit=Cast<AEGChessUnit>(Subject))Mesh=Unit->GetVisiblePresentationMesh();
  else Mesh=FindObject<USkeletalMeshComponent>(Subject,TEXT("PhantomBody"));
  FVector Point=Subject->GetActorLocation()+FVector(0,0,100);
  FVector Extent(100,100,200);
  if(IsValid(Mesh)&&Mesh->Bounds.SphereRadius>1.f){Point=Mesh->Bounds.Origin;Extent=Mesh->Bounds.BoxExtent;}
  // Fit the actual skeletal bounds with room for the bow and HUD. UE camera
  // FOV is horizontal; use the narrower 16:9 vertical angle for height.
  const float TanH=FMath::Tan(FMath::DegreesToRadians(22.5f));
  const float TanV=TanH/(1920.f/1080.f);
  const float Distance=FMath::Max(500.f,FMath::Max(Extent.Z/TanV,Extent.Size2D()/TanH)*2.1f);
  const FVector Eye=Point+Forward*Distance+Right*(Distance*.16f)+FVector(0,0,Distance*.12f);
  auto* Camera=World->SpawnActor<ACameraActor>(Eye,(Point-Eye).Rotation());if(!Camera)return false;
  Camera->GetCameraComponent()->SetFieldOfView(45.f);
  CameraController=PC;PreviousViewTarget=PC->GetViewTarget();CloseCamera=Camera;PC->SetViewTarget(Camera);
  // The test deliberately freezes world simulation during paired captures;
  // refresh the player camera explicitly so the new view is not one frame old.
  if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(0.f);
  Test->AddInfo(FString::Printf(TEXT("Temporary close camera bounds=(%.1f,%.1f,%.1f) distance=%.1f"),Extent.X,Extent.Y,Extent.Z,Distance));
  return true;
 }
 bool MarkOnce()
 {
  const FGuid Action=FGuid::NewGuid();Rules->RememberBasicAttack(Action);
  return FEGCombatExecution::Damage(Boss.Get(),Attacker.Get(),30.f,0.f,1.f,FGameplayEffectContextHandle(),Action);
 }
 bool CompareSourcePose(USkeletalMeshComponent* Actual,UAnimSequence* Clip,float Time,const TCHAR* Label)
 {
  if(!Actual||!Clip||!Boss.IsValid()||!Actual->GetSkeletalMeshAsset())return false;
  auto* Reference=NewObject<USkeletalMeshComponent>(Boss.Get(),NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
  if(!Test->TestNotNull(TEXT("Independent source-pose reference component"),Reference))return false;
  Reference->SetCollisionEnabled(ECollisionEnabled::NoCollision);Reference->SetVisibility(false);Reference->SetHiddenInGame(true);
  Reference->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
  Reference->SetSkeletalMeshAsset(Actual->GetSkeletalMeshAsset());Reference->RegisterComponent();
  Reference->SetComponentTickEnabled(false);Reference->PlayAnimation(Clip,false);
  auto* Node=Reference->GetSingleNodeInstance();
  if(!Test->TestNotNull(TEXT("Independent reference single-node instance"),Node)){Reference->DestroyComponent();return false;}
  Node->SetPlaying(false);
  const FName Bones[]={TEXT("Bip001-R-UpperArm"),TEXT("Bip001-R-Forearm"),TEXT("Bip001-R-Hand")};
  auto SampleReference=[&](float Position)
  {
   Reference->SetPosition(Position,false);Reference->TickAnimation(0.f,false);Reference->RefreshBoneTransforms();
  };
  auto LocalRotation=[](USkeletalMeshComponent* Mesh,FName Bone)
  {
   const FName Parent=Mesh->GetParentBone(Bone);
   const FTransform BoneTransform=Mesh->GetSocketTransform(Bone,RTS_Component);
   return Parent.IsNone()?BoneTransform.GetRotation():BoneTransform.GetRelativeTransform(Mesh->GetSocketTransform(Parent,RTS_Component)).GetRotation();
  };
  for(const FName Bone:Bones)
   if(!Test->TestTrue(TEXT("Right-arm diagnostic bone exists on both meshes"),Actual->GetBoneIndex(Bone)!=INDEX_NONE&&Reference->GetBoneIndex(Bone)!=INDEX_NONE))
   {Reference->DestroyComponent();return false;}
  SampleReference(0.f);TArray<FQuat> First;
  for(const FName Bone:Bones)First.Add(LocalRotation(Reference,Bone));
  SampleReference(Time);
  float LargestMotion=0.f,LargestError=0.f;
  for(int32 Index=0;Index<UE_ARRAY_COUNT(Bones);++Index)
  {
   const FQuat Expected=LocalRotation(Reference,Bones[Index]),Observed=LocalRotation(Actual,Bones[Index]);
   const float Motion=static_cast<float>(FMath::RadiansToDegrees(First[Index].AngularDistance(Expected)));
   const float Error=static_cast<float>(FMath::RadiansToDegrees(Observed.AngularDistance(Expected)));
   LargestMotion=FMath::Max(LargestMotion,Motion);LargestError=FMath::Max(LargestError,Error);
   Test->AddInfo(FString::Printf(TEXT("%s bone=%s t=%.3f referenceMovementDeg=%.2f actualVsReferenceDeg=%.2f"),Label,*Bones[Index].ToString(),Time,Motion,Error));
  }
  Reference->DestroyComponent();
  const bool bSensitive=Test->TestTrue(TEXT("Reference source action visibly changes the right arm"),LargestMotion>12.f);
  const bool bMatched=Test->TestTrue(TEXT("Actual AnimGraph right-arm pose matches independent source sample"),LargestError<15.f);
  return bSensitive&&bMatched;
 }
 bool IsWallVisible(UWorld* World) const
 {
  for(TActorIterator<AEGCorruptionBarrierVisual> It(World);It;++It)
  {
   if(It->GetOwner()!=Boss.Get()||It->IsActorBeingDestroyed())continue;
   TInlineComponentArray<UStaticMeshComponent*> Parts;It->GetComponents(Parts);
   for(const auto* Part:Parts)if(IsValid(Part)&&Part->IsVisible()&&!Part->bHiddenInGame)return true;
  }
  return false;
 }
 void ReportWallSurface(UWorld* World)
 {
  AEGCorruptionBarrierVisual* Wall=nullptr;
  for(TActorIterator<AEGCorruptionBarrierVisual> It(World);It;++It)
   if(It->GetOwner()==Boss.Get()&&!It->IsActorBeingDestroyed()){Wall=*It;break;}
  if(!Test->TestNotNull(TEXT("Active wall visual at capture"),Wall))return;
  auto* Surface=FindObject<UStaticMeshComponent>(Wall,TEXT("WallSurface"));
  if(!Test->TestNotNull(TEXT("Active wall surface at capture"),Surface))return;
  auto* MID=Cast<UMaterialInstanceDynamic>(Surface->GetMaterial(0));
  if(!Test->TestNotNull(TEXT("Wall surface material instance"),MID))return;
  const FVector Location=Surface->GetComponentLocation(),Origin=Surface->Bounds.Origin,Extent=Surface->Bounds.BoxExtent;
  const FRotator Rotation=Surface->GetComponentRotation();const FVector Scale=Surface->GetComponentScale();
  TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Ids;MID->GetAllScalarParameterInfo(Infos,Ids);
  const auto Scalar=[MID,&Infos](FName Name)
  {
   return Infos.ContainsByPredicate([Name](const FMaterialParameterInfo& Info){return Info.Name==Name;})
    ?FString::Printf(TEXT("%.4f"),MID->K2_GetScalarParameterValue(Name)):FString(TEXT("MISSING"));
  };
  const FString Opacity=Scalar(TEXT("DomainOpacity")),Emissive=Scalar(TEXT("DomainEmissive")),Time=Scalar(TEXT("VisualTime"));
  const FString Rows=Scalar(TEXT("GridRows")),Columns=Scalar(TEXT("GridColumns")),Sharpness=Scalar(TEXT("GridSharpness"));
  Test->AddInfo(FString::Printf(TEXT("WallSurface sample: visual=%s parent=%s mid=%s visible=%d hiddenInGame=%d sourceTag=%d location=(%.2f,%.2f,%.2f) rotation=(pitch %.2f,yaw %.2f,roll %.2f) scale=(%.3f,%.3f,%.3f) boundsOrigin=(%.2f,%.2f,%.2f) boundsExtent=(%.2f,%.2f,%.2f) DomainOpacity=%s DomainEmissive=%s VisualTime=%s GridRows=%s GridColumns=%s GridSharpness=%s"),
   *GetPathNameSafe(Wall),*GetPathNameSafe(MID->Parent.Get()),*GetPathNameSafe(MID),Surface->IsVisible()?1:0,Surface->bHiddenInGame?1:0,
   Surface->ComponentHasTag(TEXT("EGSourceStyleCorruptionWall"))?1:0,Location.X,Location.Y,Location.Z,Rotation.Pitch,Rotation.Yaw,Rotation.Roll,
   Scale.X,Scale.Y,Scale.Z,Origin.X,Origin.Y,Origin.Z,Extent.X,Extent.Y,Extent.Z,*Opacity,*Emissive,*Time,*Rows,*Columns,*Sharpness));
  Test->TestTrue(TEXT("Initialized wall surface is visible"),Surface->IsVisible()&&!Surface->bHiddenInGame);
  Test->TestTrue(TEXT("Wall uses source-style material"),Surface->ComponentHasTag(TEXT("EGSourceStyleCorruptionWall")));
  Test->TestTrue(TEXT("Wall has ticked beyond zero reveal"),Time!=TEXT("MISSING")&&MID->K2_GetScalarParameterValue(TEXT("VisualTime"))>.05f);
  Test->TestTrue(TEXT("Wall reveal opacity is positive"),Opacity!=TEXT("MISSING")&&MID->K2_GetScalarParameterValue(TEXT("DomainOpacity"))>0.f);
  auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
  if(Test->TestNotNull(TEXT("Formal board geometry for wall and player framing"),Layout))
  {
   const FTransform Basis=Layout->GetActorTransform();const FVector LocalWall=Basis.InverseTransformPosition(Wall->GetActorLocation());
   const float Row4=Basis.InverseTransformPosition(Layout->CellToWorld({0,4})).Y;
   Test->TestTrue(TEXT("Visible wall is centered on row index four"),FMath::IsNearlyEqual(LocalWall.Y,Row4,.5f));
   Test->AddInfo(FString::Printf(TEXT("Formal board wall local=%s row4Y=%.2f playerRows=%d"),*LocalWall.ToCompactString(),Row4,Layout->PlayerRows));
   for(auto* Player:Mode->BattleRules->Units())if(IsValid(Player)&&IEGCombatant::Execute_GetFaction(Player)==EEGFaction::Player&&IEGCombatant::Execute_IsAlive(Player))
   {
    const FVector Local=Basis.InverseTransformPosition(Player->GetActorLocation());const float Radius=UEGCombatComponent::BlockingRadius(Player);
    const float RX=Radius/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::X).Size());
    const float RY=Radius/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size());
    const float HalfX=Layout->Columns*Layout->CellWidth*.5f,Bottom=-Layout->Rows*Layout->CellDepth*.5f;
    auto* Body=Player->GetVisiblePresentationMesh();
    Test->AddInfo(FString::Printf(TEXT("Formal player landing actor=%s local=%s radius=%.2f bodyOrigin=%s bodyExtent=%s"),
     *GetPathNameSafe(Player),*Local.ToCompactString(),Radius,Body?*Body->Bounds.Origin.ToCompactString():TEXT("none"),Body?*Body->Bounds.BoxExtent.ToCompactString():TEXT("none")));
    Test->TestTrue(TEXT("Player blocking base remains inside player board area"),Local.X-RX>=-HalfX-.1f&&Local.X+RX<=HalfX+.1f&&
     Local.Y-RY>=Bottom-.1f&&Local.Y+RY<=Bottom+Layout->PlayerRows*Layout->CellDepth+.1f);
   }
  }
 }
 bool GetFailureSweepProgress(UWorld* World,float& Progress) const
 {
  const auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
  if(!Layout)return false;
  const FTransform Basis=Layout->GetActorTransform();
  const float Start=Basis.InverseTransformPosition(Layout->CellToWorld({0,4})).Y;
  const float End=-.5f*Layout->Rows*Layout->CellDepth;
  if(Start<=End)return false;
  for(TActorIterator<AEGCorruptionBossVisual> It(World);It;++It)
  {
   if(It->GetOwner()!=Boss.Get()||It->IsActorBeingDestroyed())continue;
   TInlineComponentArray<UNiagaraComponent*> Particles;It->GetComponents(Particles);
   for(const auto* Particle:Particles)if(IsValid(Particle)&&Particle->GetAsset()&&Particle->GetAsset()->GetName()==TEXT("NS_CorruptionBrokenWavefront"))
   {Progress=(Start-Basis.InverseTransformPosition(It->GetActorLocation()).Y)/(Start-End);return true;}
  }
  return false;
 }
 void ReportParticleSample(UWorld* World,const TCHAR* Label)
 {
  auto Report=[this,Label](AActor* Actor)
  {
   if(!IsValid(Actor)||Actor->IsActorBeingDestroyed())return;
   TInlineComponentArray<UNiagaraComponent*> Components;Actor->GetComponents(Components);
   for(const auto* Component:Components)if(IsValid(Component))
    Test->AddInfo(FString::Printf(TEXT("BossParticleSample label=%s actor=%s asset=%s active=%d paused=%d controller=%d position=%s particleCount=unavailable"),
     Label,*GetPathNameSafe(Actor),*GetPathNameSafe(Component->GetAsset()),Component->IsActive()?1:0,Component->IsPaused()?1:0,
     Component->GetSystemInstanceController().IsValid()?1:0,*Component->GetComponentLocation().ToCompactString()));
  };
  for(TActorIterator<AEGCorruptionBossVisual> It(World);It;++It)if(It->GetOwner()==Boss.Get())Report(*It);
  if(ObservedPhantom.IsValid())Report(ObservedPhantom.Get());
 }
public:
 FEliteBossVisualRun(FAutomationTestBase* InTest,bool bInTimeout):Test(InTest),bTimeoutBranch(bInTimeout){}
 virtual ~FEliteBossVisualRun() override {RestoreCamera();}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(FPlatformTime::Seconds()>Deadline)
  {
   ReleaseFrozenCapture(Mode);RestoreCamera();
   Test->AddError(FString::Printf(TEXT("Controlled Boss visual test timed out, step %d"),Step));return true;
  }
  if(!World||!Mode||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Hand=Mode->GetCards()->GetHand();if(Hand.IsEmpty())return false;
   if(!Test->TestTrue(TEXT("Deploy opening hero"),Mode->GetCards()->TryDeployHandCard(Hand[0].InstanceId,{3,1}))||
      !Test->TestTrue(TEXT("Enter controlled stage-one battle"),Mode->EnterCombat()))return true;
   Step=1;return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->CancelAction();
    if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Attacker=Unit;
    else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)FixtureEnemies.Add(Unit);
   }
   auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
   if(!Test->TestNotNull(TEXT("Formal corruption definition"),Definition)||!Test->TestNotNull(TEXT("Controlled player attacker"),Attacker.Get())||!Test->TestNotNull(TEXT("Formal board"),Mode->GetBoard()))return true;
   Boss=Mode->SpawnBattleSummon(Definition,6,Mode->GetBoard()->CellToWorld({3,5}),EEGFaction::Enemy);
   if(!Test->TestNotNull(TEXT("Formal Boss model"),Boss.Get()))return true;
   // SpawnBattleSummon only initializes the actor. Give this injected enemy
   // the same battle configuration and resolution subscription as a stage
   // encounter before attaching its Boss-specific behavior.
   Boss->GetHeroSkill()->Configure(Mode->BattleRules,6,false);
   Mode->BattleRules->EnterUnit(Boss.Get());
   // Keep the sole observation target alive while several presentation
   // windows play; this attack value is not a balance measurement.
   Rules=UEGCorruptionCombatComponent::AttachBoss(Boss.Get(),Mode->BattleRules,10000.f,20.f,50);
   if(!Test->TestNotNull(TEXT("Boss rules"),Rules.Get()))return true;
   // The injected Boss is already registered and alive. Remove only the
   // stage-one fixture enemies so their huge nearby meshes cannot occlude it;
   // this is a transient test world, not a production spawn or combat change.
   for(const auto& Enemy:FixtureEnemies)if(Enemy.IsValid()&&Enemy.Get()!=Boss.Get())Enemy->Destroy();
   FixtureEnemies.Reset();
   Rules->BarrierDurationSeconds=bTimeoutBranch?4.f:8.f;
   Rules->SpreadIntervalSeconds=3.f;Rules->MimicIntervalSeconds=2.5f;
   OriginalBossRange=Boss->GetAbilitySystemComponent()->GetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute());
   Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),100.f);
   BossWalkStart=Boss->GetActorLocation();
   Step=2;return false;
  }
  if(Step==20)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   for(TActorIterator<AEGCorruptionBarrierVisual> It(World);It;++It)
    Test->TestTrue(TEXT("Wall visual cleaned at battle stop"),It->IsActorBeingDestroyed());
   for(TActorIterator<AEGCorruptionPhantomVisual> It(World);It;++It)
    Test->TestTrue(TEXT("Phantom cleaned at battle stop"),It->IsActorBeingDestroyed());
   for(TActorIterator<AEGCorruptionBossVisual> It(World);It;++It)
    Test->TestTrue(TEXT("Boss visual and attached events cleaned at battle stop"),It->IsActorBeingDestroyed());
   Test->AddInfo(TEXT("Controlled stage-one scene: injected Boss/damage/intervals and one forced clock value; screenshots are not natural-battle or visual acceptance evidence."));
   return true;
  }
  if(!Rules.IsValid()||!Boss.IsValid()||!Attacker.IsValid()){ReleaseCaptureHold(Mode);RestoreCamera();Test->AddError(TEXT("Controlled battle actors unexpectedly destroyed"));return true;}
  if(Step==2){if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_FirstLife"):TEXT("BossBreak_FirstLife")))return false;Step=3;return false;}
  if(Step==3)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   WaitUntil=FPlatformTime::Seconds()+5.;Step=45;return false;
  }
  if(Step==45)
  {
   auto* Body=Boss->GetVisiblePresentationMesh();
   if(!bSampledWalk)
   {
    if(FVector::Dist2D(BossWalkStart,Boss->GetActorLocation())<60.f)
    {if(FPlatformTime::Seconds()>WaitUntil){Test->AddError(TEXT("Boss never began its real chase"));return true;}return false;}
    BossWalkPose=Body->GetBoneSpaceTransforms();bSampledWalk=true;WaitUntil=FPlatformTime::Seconds()+.25;return false;
   }
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   float LegMotion=0.f;const auto& Pose=Body->GetBoneSpaceTransforms();
   for(int32 Bone=1;Bone<FMath::Min(Pose.Num(),BossWalkPose.Num());++Bone)
   {
    const FString Name=Body->GetBoneName(Bone).ToString();
    if(Name.Contains(TEXT("Thigh"))||Name.Contains(TEXT("Calf"))||Name.Contains(TEXT("Foot")))
     LegMotion=FMath::Max(LegMotion,float(Pose[Bone].GetRotation().AngularDistance(BossWalkPose[Bone].GetRotation())));
   }
   Test->AddInfo(FString::Printf(TEXT("Boss real chase leg motion %.2f degrees, displacement %.1f cm"),FMath::RadiansToDegrees(LegMotion),FVector::Dist2D(BossWalkStart,Boss->GetActorLocation())));
   if(!Test->TestTrue(TEXT("Boss legs animate during real movement"),LegMotion>.05f))return true;
   if(!BeginCapture(Mode,TEXT("Boss_FirstLifeWalk")))return false;Step=46;return false;
  }
  if(Step==46)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   Boss->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),OriginalBossRange);
   WaitUntil=FPlatformTime::Seconds()+15.;Step=22;return false;
  }
  if(Step==22)
  {
   if(!Boss->GetHeroSkill()->IsActionRunning())
   {
    if(FPlatformTime::Seconds()>WaitUntil){Test->AddError(TEXT("Boss did not begin a normal first-life action within 15 seconds"));return true;}
    return false;
   }
   if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_FirstAction"):TEXT("BossBreak_FirstAction")))return false;Step=23;return false;
  }
  if(Step==23)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   Boss->GetCombat()->SetComponentTickEnabled(false);Boss->GetHeroSkill()->CancelAction();
   // Only the resource is injected: the skill, montage and their events run
   // through the normal first-life action path.
   Boss->RestoreMana(100000.f);
   if(!Test->TestTrue(TEXT("Start first-life arrow rain action"),Boss->GetHeroSkill()->BeginAction(true)))return true;
   WaitUntil=FPlatformTime::Seconds()+12.;Step=24;return false;
  }
  if(Step==24)
  {
   const auto* Elysia=Cast<UEGElysiaSkillBehavior>(Boss->GetHeroSkill()->GetBehavior());
   if(!Elysia||!Elysia->IsArrowRainActive())
   {
    if(FPlatformTime::Seconds()>WaitUntil){Test->AddError(TEXT("First-life arrow rain never reached its real animation event"));return true;}
    return false;
   }
   if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_ArrowRain"):TEXT("BossBreak_ArrowRain")))return false;Step=25;return false;
  }
  if(Step==25)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   auto* PC=World->GetFirstPlayerController();auto* Manager=PC?PC->PlayerCameraManager.Get():nullptr;
   if(!Test->TestNotNull(TEXT("Formal board local camera manager"),Manager)||!Test->TestNotNull(TEXT("Formal board view target"),PC->GetViewTarget()))return true;
   Manager->UpdateCamera(0.f);CameraBaselineLocation=Manager->GetCameraLocation();CameraBaselineRotation=Manager->GetCameraRotation();
   IntroCameraTarget=PC->GetViewTarget();bCameraBaselineValid=true;
   Boss->GetHeroSkill()->CancelAction();
   if(!Test->TestTrue(TEXT("First life exhausted by combat damage"),FEGCombatExecution::Damage(Attacker.Get(),Boss.Get(),1000000.f)))return true;
   if(!Test->TestTrue(TEXT("Authoritative wall intro owns the source pose"),Rules->IsBarrierIntroPresentationActive()))return true;
   IntroObservedAt=FPlatformTime::Seconds();
   IntroObservedElapsed=Rules->GetBarrierIntroDurationSeconds()-Rules->GetBarrierIntroRemainingSeconds();
   IntroObservationCount=0;
   UE_LOG(LogTemp,Display,TEXT("BossIntroObserve start branch=%s duration=%.6f remaining=%.6f elapsed=%.6f phase=%d round=%s boss=%s"),
    bTimeoutBranch?TEXT("timeout"):TEXT("break"),Rules->GetBarrierIntroDurationSeconds(),Rules->GetBarrierIntroRemainingSeconds(),IntroObservedElapsed,
    int32(Rules->GetBossPhase()),*Mode->BattleRules->GetRoundId().ToString(),*GetPathNameSafe(Boss.Get()));
   Step=39;return false;
  }
  if(Step==39)
  {
   const double ObservationNow=FPlatformTime::Seconds();
   const float Elapsed=Rules->GetBarrierIntroDurationSeconds()-Rules->GetBarrierIntroRemainingSeconds();
   const bool bIntroActive=Rules->IsBarrierIntroPresentationActive();
   UE_LOG(LogTemp,Display,TEXT("BossIntroObserve sample=%d elapsed=%.6f deltaElapsed=%.6f deltaReal=%.6f phase=%d active=%d duration=%.6f remaining=%.6f"),
    ++IntroObservationCount,Elapsed,Elapsed-IntroObservedElapsed,ObservationNow-IntroObservedAt,int32(Rules->GetBossPhase()),bIntroActive?1:0,
    Rules->GetBarrierIntroDurationSeconds(),Rules->GetBarrierIntroRemainingSeconds());
   IntroObservedAt=ObservationNow;IntroObservedElapsed=Elapsed;
   if(!bIntroActive)
   {
    auto* Body=Boss->GetVisiblePresentationMesh();
    UE_LOG(LogTemp,Display,TEXT("BossIntroObserve lost round=%s battleActive=%d boss=%s bossAlive=%d body=%s anim=%s phase=%d"),
     *Mode->BattleRules->GetRoundId().ToString(),Mode->BattleRules->IsBattleActive()?1:0,*GetPathNameSafe(Boss.Get()),
     IEGCombatant::Execute_IsAlive(Boss.Get())?1:0,*GetPathNameSafe(Body),*GetPathNameSafe(Body?Body->GetAnimInstance():nullptr),int32(Rules->GetBossPhase()));
    for(TActorIterator<AEGCombatVisual> It(World);It;++It)
     if(It->GetOwner()==Boss.Get())UE_LOG(LogTemp,Display,TEXT("BossIntroObserve ownedRig=%s destroying=%d definition=%s body=%s"),
      *GetPathNameSafe(*It),It->IsActorBeingDestroyed()?1:0,*GetPathNameSafe(It->Definition.Get()),*GetPathNameSafe(It->BodyMesh.Get()));
    Test->AddError(TEXT("Source wall intro ended before the mid-action capture"));return true;
   }
   if(!bPushCaptureTaken&&Elapsed>=.10f)
   {
    if(Elapsed>.35f){Test->AddError(TEXT("Pushback mid-action window missed before screenshot"));return true;}
    if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_PushbackMid"):TEXT("BossBreak_PushbackMid")))return false;
    bPushCaptureTaken=true;Step=43;return false;
   }
   AEGCorruptionBarrierVisual* IntroWall=nullptr;
   for(TActorIterator<AEGCorruptionBarrierVisual> It(World);It;++It)if(It->GetOwner()==Boss.Get()&&!It->IsActorBeingDestroyed()){IntroWall=*It;break;}
   auto* PC=World->GetFirstPlayerController();auto* Manager=PC?PC->PlayerCameraManager.Get():nullptr;
   if(IntroWall&&IntroWall->HasRevealShakeForTests()&&Manager&&PC->GetViewTarget()==IntroCameraTarget.Get())
   {
    bObservedRevealShake=true;
    bObservedShakeOffset|=FVector::Dist(Manager->GetCameraLocation(),CameraBaselineLocation)>.001f||
     !Manager->GetCameraRotation().Equals(CameraBaselineRotation,.0001f);
   }
   if(Elapsed<1.45f)return false;
   if(!Test->TestTrue(TEXT("Real wall reveal started its own camera shake"),bObservedRevealShake)||
      !Test->TestTrue(TEXT("Reveal shake produced a real camera offset"),bObservedShakeOffset)||
      !Test->TestTrue(TEXT("Reveal shake stopped after its short duration"),IntroWall&&!IntroWall->HasRevealShakeForTests())||
      !Test->TestTrue(TEXT("Camera returns to the pre-shake view"),bCameraBaselineValid&&Manager&&PC->GetViewTarget()==IntroCameraTarget.Get()&&
       Manager->GetCameraLocation().Equals(CameraBaselineLocation,.05f)&&Manager->GetCameraRotation().Equals(CameraBaselineRotation,.005f)))return true;
   auto* Body=Boss->GetVisiblePresentationMesh();
   if(!Test->TestNotNull(TEXT("Source transition body"),Body)||
      !Test->TestTrue(TEXT("Source transition preserves animation blueprint mode"),Body->GetAnimationMode()==EAnimationMode::AnimationBlueprint)||
      !Test->TestNotNull(TEXT("Source transition uses real combat AnimGraph"),Cast<UEGCombatAnimInstance>(Body->GetAnimInstance())))return true;
   const auto* Anim=Cast<UEGCombatAnimInstance>(Body->GetAnimInstance());
   Test->TestTrue(TEXT("Real AnimGraph consumes the authored source clip"),Anim->PassiveClip&&Anim->PassiveClip->GetName()==TEXT("A_Corruption_SummonWall_BoardCandidate"));
   Test->TestTrue(TEXT("Real AnimGraph source time tracks the encounter"),FMath::IsNearlyEqual(Anim->PassiveTime,Elapsed,.1f));
   bool bSourceRig=false,bAttachedBow=false;
   for(TActorIterator<AEGCombatVisual> It(World);It;++It)
   {
    if(It->GetOwner()!=Boss.Get()||It->IsActorBeingDestroyed()||!It->Definition||
       It->Definition->GetName()!=TEXT("DA_Corruption_SummonWall_Assembly"))continue;
    bSourceRig=It->BodyMesh==Body;
    TInlineComponentArray<USkeletalMeshComponent*> Parts;It->GetComponents(Parts);
    for(const auto* Part:Parts)if(Part&&Part!=Body&&Part->GetAttachParent()==Body&&Part->GetAttachSocketName()==TEXT("Bip001-L-Hand")&&Part->IsVisible())bAttachedBow=true;
   }
   Test->TestTrue(TEXT("Authoritative source rig is bound to the actual body"),bSourceRig);
   Test->TestTrue(TEXT("Authoritative source rig displays the attached bow"),bAttachedBow);
   CapturedSourceElapsed=Elapsed;CapturedSourceRemaining=Rules->GetBarrierIntroRemainingSeconds();
   CompareSourcePose(Body,Anim->PassiveClip.Get(),Elapsed,TEXT("BeforeCapture"));
   // Freeze the real source pose before waiting on shader/viewport readiness;
   // no test fast-forward is used, and the encounter resumes after capture.
   if(!Hold.IsValid()){Hold=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(Hold,true);}
   if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_WallSourceAction"):TEXT("BossBreak_WallSourceAction")))return false;
   Test->AddInfo(FString::Printf(TEXT("Real AnimGraph source transition sample elapsed=%.3f duration=%.3f (no test fast-forward or single-node playback)"),Elapsed,Rules->GetBarrierIntroDurationSeconds()));
   Step=40;return false;
  }
  if(Step==43)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   Step=39;return false;
  }
  if(Step==40)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;if(Result<0)return true;
   if(!Test->TestTrue(TEXT("Source transition stays active while capture holds it"),Rules->IsBarrierIntroPresentationActive())||
      !Test->TestTrue(TEXT("Temporary source transition close-up camera"),SetCloseCamera(World,Boss.Get(),Boss->GetActorForwardVector())))
   {ReleaseCaptureHold(Mode);return true;}
   Step=41;return false;
  }
  if(Step==41)
  {
   if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_WallSourceClose"):TEXT("BossBreak_WallSourceClose")))return false;Step=42;return false;
  }
  if(Step==42)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;
   if(Result>0)
   {
    auto* Body=Boss->GetVisiblePresentationMesh();auto* Anim=Body?Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()):nullptr;
    Test->TestTrue(TEXT("Source capture still owns its extra pause hold"),Hold.IsValid()&&Mode->BattleRules->HasPresentationHold(Hold));
    Test->TestTrue(TEXT("Intro clock remains frozen until close capture finishes"),FMath::IsNearlyEqual(Rules->GetBarrierIntroRemainingSeconds(),CapturedSourceRemaining,.01f));
    if(Test->TestNotNull(TEXT("AnimGraph still exists at close capture"),Anim)&&Test->TestNotNull(TEXT("Source clip remains bound at close capture"),Anim->PassiveClip.Get()))
     CompareSourcePose(Body,Anim->PassiveClip.Get(),CapturedSourceElapsed,TEXT("AfterCapture"));
   }
   ReleaseCaptureHold(Mode);RestoreCamera();if(Result<0)return true;
   Step=4;return false;
  }
  if(Step==4)
  {
   if(Mode->BattleRules->HasPresentationHold())return false;
   if(!Test->TestEqual(TEXT("Wall challenge started"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier)))return true;
   if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_Wall"):TEXT("BossBreak_Wall")))return false;
   ReportWallSurface(World);Step=5;return false;
  }
  if(Step==5)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   if(bTimeoutBranch){Step=8;return false;}
   if(!Test->TestTrue(TEXT("Partial hit damages wall"),FEGCombatExecution::Damage(Attacker.Get(),Boss.Get(),Rules->GetBarrierMaxHealth()*.55f)))return true;
   Step=6;return false;
  }
  if(Step==6){if(!BeginCapture(Mode,TEXT("BossBreak_WallDamaged")))return false;Step=7;return false;}
  if(Step==7)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   if(!Test->TestTrue(TEXT("Remaining wall destroyed by combat damage"),FEGCombatExecution::Damage(Attacker.Get(),Boss.Get(),1000000.f)))return true;
   WaitUntil=FPlatformTime::Seconds()+.10;
   Step=8;return false;
  }
  if(Step==8)
  {
   if(Rules->GetBossPhase()!=EEGCorruptionBossPhase::Second)return false;
   Test->TestEqual(TEXT("Barrier outcome agrees with selected branch"),Rules->WasBarrierBroken(),!bTimeoutBranch);
   if(bTimeoutBranch){EffectDeadline=FPlatformTime::Seconds()+1.5;Step=34;return false;}
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   if(!BeginFrozenCapture(Mode,TEXT("BossBreak_SecondTransition")))return false;ReportParticleSample(World,TEXT("PhaseTransition"));Step=9;return false;
  }
  if(Step==34)
  {
   float Progress=0.f;
   if(!GetFailureSweepProgress(World,Progress))
   {
    if(FPlatformTime::Seconds()>EffectDeadline){Test->AddError(TEXT("Timeout impact did not produce the private failure sweep"));return true;}
    return false;
   }
   if(Progress<.25f)return false;
   if(!Test->TestTrue(TEXT("First wave capture is within the early sweep"),Progress<=.45f))return true;
   if(!BeginFrozenCapture(Mode,TEXT("BossTimeout_Impact")))return false;Step=35;return false;
  }
  if(Step==9)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;ReleaseFrozenCapture(Mode);if(Result<0)return true;
   WaitUntil=FPlatformTime::Seconds()+4.;Step=33;return false;
  }
  if(Step==35)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;ReleaseFrozenCapture(Mode);if(Result<0)return true;
   Step=36;return false;
  }
  if(Step==36)
  {
   float Progress=0.f;
   if(!GetFailureSweepProgress(World,Progress)){Test->AddError(TEXT("Failure sweep ended before its middle capture"));return true;}
   if(Progress<.60f)return false;
   if(!Test->TestTrue(TEXT("Second wave capture is within the middle sweep"),Progress<=.82f))return true;
   if(!BeginFrozenCapture(Mode,TEXT("BossTimeout_ImpactMid")))return false;Step=37;return false;
  }
  if(Step==37)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;ReleaseFrozenCapture(Mode);if(Result<0)return true;
   WaitUntil=FPlatformTime::Seconds()+4.;Step=33;return false;
  }
  if(Step==33)
  {
   if(IsWallVisible(World))
   {
    if(FPlatformTime::Seconds()>WaitUntil){Test->AddError(TEXT("Wall visuals did not disappear before the Boss close-up"));return true;}
    return false;
   }
   if(!Test->TestTrue(TEXT("Temporary Boss close-up camera"),SetCloseCamera(World,Boss.Get(),Boss->GetActorForwardVector())))return true;
   Step=28;return false;
  }
  if(Step==28)
  {
   if(!BeginCapture(Mode,bTimeoutBranch?TEXT("BossTimeout_SecondClose"):TEXT("BossBreak_SecondClose")))return false;Step=29;return false;
  }
  if(Step==29)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;RestoreCamera();if(Result<0)return true;
   if(auto* Audio=UEGGameAudioComponent::FindFor(Boss.Get()))Audio->SkipStoryDialogue();
   if(bTimeoutBranch){Mode->BattleRules->Stop();Step=20;WaitUntil=FPlatformTime::Seconds()+.5;return false;}
   if(!Test->TestTrue(TEXT("Apply first real Boss basic hit"),MarkOnce()))return true;
   Step=10;return false;
  }
  if(Step==10){if(!BeginCapture(Mode,TEXT("BossSecond_Mark")))return false;Step=11;return false;}
  if(Step==11)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   if(!Test->TestTrue(TEXT("Apply second mark hit"),MarkOnce())||!Test->TestTrue(TEXT("Apply threshold hit"),MarkOnce()))return true;
   WaitUntil=FPlatformTime::Seconds()+.10;
   Step=12;return false;
  }
  if(Step==12){if(FPlatformTime::Seconds()<WaitUntil)return false;if(!BeginFrozenCapture(Mode,TEXT("BossSecond_MarkBurst")))return false;ReportParticleSample(World,TEXT("MarkBurst"));Step=13;return false;}
  if(Step==13)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;ReleaseFrozenCapture(Mode);if(Result<0)return true;
   Step=14;return false;
  }
  if(Step==14)
  {
   for(TActorIterator<AEGCorruptionSeed> It(World);It;++It)if(It->BelongsTo(Mode->BattleRules)){ObservedSeed=*It;break;}
   if(!ObservedSeed.IsValid())return false;
   if(!BeginCapture(Mode,TEXT("BossSecond_SpreadWarning")))return false;Step=15;return false;
  }
  if(Step==15)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   Step=16;return false;
  }
  if(Step==16)
  {
   if(ObservedSeed.IsValid()&&!ObservedSeed->IsActorBeingDestroyed())return false;
   WaitUntil=FPlatformTime::Seconds()+.10;Step=32;return false;
  }
  if(Step==32)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   if(!BeginFrozenCapture(Mode,TEXT("BossSecond_SpreadImpact")))return false;ReportParticleSample(World,TEXT("SpreadImpact"));Step=17;return false;
  }
  if(Step==17)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;ReleaseFrozenCapture(Mode);if(Result<0)return true;
   Step=18;return false;
  }
  if(Step==18)
  {
   if(!ObservedPhantom.IsValid())
   {
    for(TActorIterator<AEGCorruptionPhantomVisual> It(World);It;++It)if(It->GetOwner()==Boss.Get()){ObservedPhantom=*It;WaitUntil=FPlatformTime::Seconds()+.2;break;}
   }
   if(!ObservedPhantom.IsValid()||FPlatformTime::Seconds()<WaitUntil)return false;
   if(auto* Body=FindObject<USkeletalMeshComponent>(ObservedPhantom.Get(),TEXT("PhantomBody")))PhantomWindupPose=Body->GetBoneSpaceTransforms();
   if(!BeginCapture(Mode,TEXT("BossSecond_NaturalPhantom")))return false;Step=19;return false;
  }
  if(Step==19)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   WaitUntil=FPlatformTime::Seconds()+3.;Step=26;return false;
  }
  if(Step==26)
  {
   const auto* Beam=ObservedPhantom.IsValid()?FindObject<UStaticMeshComponent>(ObservedPhantom.Get(),TEXT("PhantomCastBeam")):nullptr;
   if(!Beam||!Beam->IsVisible())
   {
    if(FPlatformTime::Seconds()>WaitUntil){Test->AddError(TEXT("Natural phantom did not present its cast"));return true;}
    return false;
   }
   WaitUntil=FPlatformTime::Seconds()+.10;Step=44;return false;
  }
  if(Step==44)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   const auto* Beam=ObservedPhantom.IsValid()?FindObject<UStaticMeshComponent>(ObservedPhantom.Get(),TEXT("PhantomCastBeam")):nullptr;
   if(!Test->TestTrue(TEXT("Phantom remains in the real cast window at impact sample"),Beam&&Beam->IsVisible()))return true;
   auto* PhantomBody=FindObject<USkeletalMeshComponent>(ObservedPhantom.Get(),TEXT("PhantomBody"));
   float MaximumPoseChange=0.f;
   if(PhantomBody)
   {
    const auto& Pose=PhantomBody->GetBoneSpaceTransforms();
    for(int32 Bone=1;Bone<FMath::Min(Pose.Num(),PhantomWindupPose.Num());++Bone)
     MaximumPoseChange=FMath::Max(MaximumPoseChange,float(Pose[Bone].GetRotation().AngularDistance(PhantomWindupPose[Bone].GetRotation())));
   }
   Test->AddInfo(FString::Printf(TEXT("Phantom actual local-bone motion %.2f degrees"),FMath::RadiansToDegrees(MaximumPoseChange)));
   if(!Test->TestTrue(TEXT("Visible phantom body actually animates between windup and hit"),PhantomBody&&PhantomBody->IsVisible()&&MaximumPoseChange>.05f))return true;
   if(!BeginFrozenCapture(Mode,TEXT("BossSecond_PhantomCast")))return false;ReportParticleSample(World,TEXT("PhantomCast"));Step=27;return false;
  }
  if(Step==27)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;if(Result<0){ReleaseFrozenCapture(Mode);return true;}
   const FVector PhantomFacing=ObservedPhantom.IsValid()?(ObservedPhantom->GetActorLocation()-Boss->GetActorLocation()):FVector::ZeroVector;
   if(!Test->TestTrue(TEXT("Temporary phantom cast close-up camera"),SetCloseCamera(World,ObservedPhantom.Get(),PhantomFacing)))
   {ReleaseFrozenCapture(Mode);return true;}
   Step=30;return false;
  }
  if(Step==30)
  {
   if(!BeginCapture(Mode,TEXT("BossSecond_PhantomCastClose")))return false;Step=31;return false;
  }
  if(Step==31)
  {
   const int32 Result=PollCapture(Mode,false);if(Result==0)return false;ReleaseFrozenCapture(Mode);RestoreCamera();if(Result<0)return true;
   Rules->SetEffectiveBattleSeconds(60.f);Test->TestTrue(TEXT("Enrage reached by controlled clock"),Rules->IsEnraged());
   if(!BeginCapture(Mode,TEXT("BossSecond_Enrage")))return false;Step=21;return false;
  }
  if(Step==21)
  {
   const int32 Result=PollCapture(Mode);if(Result==0)return false;if(Result<0)return true;
   Mode->BattleRules->Stop();Step=20;WaitUntil=FPlatformTime::Seconds()+.5;return false;
  }
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteBossVisualPIE,"ElyGambit.Integration.EliteBossVisualPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteBossVisualPIE::RunTest(const FString&)
{
 if(!FApp::CanEverRender()||!GEditor||GEditor->PlayWorld){AddError(TEXT("Boss visual check requires an idle graphical editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEliteBossStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FEliteBossVisualRun(this,false));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEliteBossTimeoutVisualPIE,"ElyGambit.Integration.EliteBossTimeoutVisualPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEliteBossTimeoutVisualPIE::RunTest(const FString&)
{
 if(!FApp::CanEverRender()||!GEditor||GEditor->PlayWorld){AddError(TEXT("Boss timeout visual check requires an idle graphical editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEliteBossStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FEliteBossVisualRun(this,true));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
