#include "Presentation/EGOpeningPresentation.h"

#include "Actor/EGArtPiece.h"
#include "Actor/EGBoard.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Character/EGChessUnit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/EGDemoGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGTeleportVisual.h"
#include "UI/Widget/EGOpeningWidget.h"

namespace
{
const FName OpeningApproachProgressCurve(TEXT("OpeningApproachProgress"));
// Conservative camera-space sphere test, with padding for hair, cloth and shadows.
// Never move a still-visible actor simply because a timeline threshold elapsed.
bool OpeningActorOutsideCamera(const AEGArtPiece* Piece,const ACameraActor* Camera)
{
 if(!IsValid(Piece)||!IsValid(Piece->Body)||!IsValid(Camera))return false;
 const UCameraComponent* Lens=Camera->GetCameraComponent();
 if(!IsValid(Lens))return false;
 const FVector Local=Camera->GetActorTransform().InverseTransformPosition(Piece->Body->Bounds.Origin);
 const double Radius=Piece->Body->Bounds.SphereRadius+80.;
 if(Local.X+Radius<0.)return true;
 const double Horizontal=FMath::Tan(FMath::DegreesToRadians(Lens->FieldOfView*.5));
 const double Vertical=Horizontal/FMath::Max(1.,static_cast<double>(Lens->AspectRatio));
 // The signed distance to a sloping frustum plane is divided by its normal
 // length; adding merely Radius underestimates a sphere near screen edges.
 const double HorizontalRadius=Radius*FMath::Sqrt(1.+Horizontal*Horizontal);
 const double VerticalRadius=Radius*FMath::Sqrt(1.+Vertical*Vertical);
 return FMath::Abs(Local.Y)>FMath::Max(0.,Local.X)*Horizontal+HorizontalRadius ||
        FMath::Abs(Local.Z)>FMath::Max(0.,Local.X)*Vertical+VerticalRadius;
}
const FName OpeningActorHeightCurve(TEXT("OpeningActorHeightCm"));
// Actor offsets, in centimetres, relative to the reference stair's axes and
// reference scale. Missing axes are zero; values are relative to the first key.
const FName OpeningActorOffsetCurves[]={TEXT("OpeningActorOffsetXcm"),TEXT("OpeningActorOffsetYcm"),TEXT("OpeningActorOffsetZcm")};

bool SampleOpeningActorOffset(const UAnimSequence* Animation, float Time, FVector& OutOffset, bool& bHasCurve)
{
 OutOffset=FVector::ZeroVector;
 bHasCurve=false;
 if(!IsValid(Animation)||!FMath::IsFinite(Time))return false;
 for(int32 Axis=0;Axis<UE_ARRAY_COUNT(OpeningActorOffsetCurves);++Axis)
 {
  if(!Animation->HasCurveData(OpeningActorOffsetCurves[Axis],false))continue;
  bHasCurve=true;
  const float Value=Animation->EvaluateCurveData(OpeningActorOffsetCurves[Axis],FAnimExtractContext(Time),false);
  if(!FMath::IsFinite(Value))return false;
  OutOffset[Axis]=Value;
 }
 return true;
}

// Orbit the board's vertical axis from the title side to the opposite gameplay
// side. One angular clock drives the entire arc; the descent overlaps its tail
// so there are no stops at artificial waypoints.
void OpeningOrbitCamera(const FVector& Start,const FVector& Target,const FVector& Center,
                        const FQuat& StartRotation,const FQuat& TargetRotation,float Alpha,
                        FVector& OutLocation,FQuat& OutRotation)
{
 constexpr float OrbitEnd=.77f;
 const auto Ease=[](float T){return FMath::SmoothStep(0.f,1.f,FMath::Clamp(T,0.f,1.f));};
 const auto Window=[&](float A,float B,float T){return Ease((T-A)/(B-A));};
 const FVector2D From(Start.X-Center.X,Start.Y-Center.Y);
 const FVector2D To(Target.X-Center.X,Target.Y-Center.Y);
 const float StartRadius=FMath::Max(1.f,static_cast<float>(From.Size()));
 const float TargetRadius=FMath::Max(1.f,static_cast<float>(To.Size()));
 const float StartAngle=FMath::Atan2(From.Y,From.X);
 const float TargetAngle=FMath::Atan2(To.Y,To.X);
 float AngleDelta=FMath::FindDeltaAngleRadians(StartAngle,TargetAngle);
 // This is the clear side of the authored opening staircase. The start and
 // gameplay viewpoints are on opposite sides, so this is roughly half a turn.
 if(AngleDelta>0.f)AngleDelta-=2.f*PI;
 const float Progress=Ease(Alpha/OrbitEnd);
 const float Angle=StartAngle+AngleDelta*Progress;
 // Begin tangentially, keeping the start-side coordinate constant until we are
 // past the staircase, then blend smoothly to the outer orbital radius.
 const float TangentRadius=StartRadius/FMath::Max(.4f,FMath::Cos(AngleDelta*Progress));
 const float OuterRadius=FMath::Max(StartRadius,TargetRadius)+200.f;
 float Radius=FMath::Lerp(TangentRadius,OuterRadius,Window(.12f,.30f,Progress));
 Radius=FMath::Lerp(Radius,TargetRadius,Window(.72f,1.f,Progress));
 constexpr float DescentHeight=700.f;
 const float HighZ=static_cast<float>(Target.Z)+DescentHeight;
 const float Landing=Window(.76f,1.f,Alpha);
 // Ascend above the entrance foliage early, without a step in velocity; keep
 // climbing gently throughout the orbit before the final descent.
 const float EarlyHeight=Ease(Alpha/.38f);
 const float HeightProgress=FMath::Lerp(Progress,EarlyHeight,.85f);
 const float Height=FMath::Lerp(static_cast<float>(Start.Z),HighZ,HeightProgress)-DescentHeight*Landing;
 OutLocation=FVector(Center.X+Radius*FMath::Cos(Angle),Center.Y+Radius*FMath::Sin(Angle),Height);
 // Preserve the title's first frame exactly, then ease onto the board-center
 // look direction. The last blend reaches the actual fixed gameplay camera.
 const FQuat Look=(Center-OutLocation).Rotation().Quaternion();
 OutRotation=FQuat::Slerp(StartRotation,Look,Window(0.f,.12f,Alpha));
 OutRotation=FQuat::Slerp(OutRotation,TargetRotation,Landing);
}
// The startup cover uses the board controller's resolved scene, so validate the
// same camera rather than accepting an unrelated settings actor elsewhere.
ACameraActor* GetOpeningBoardCamera(UWorld* World)
{
 const auto* PC=World?Cast<AEGBoardPlayerController>(World->GetFirstPlayerController()):nullptr;
 const auto* Scene=IsValid(PC)?PC->GetSceneSettings():nullptr;
 ACameraActor* Camera=IsValid(Scene)?Scene->FixedCamera.Get():nullptr;
 return IsValid(Camera)&&IsValid(Camera->GetCameraComponent())?Camera:nullptr;
}
}

UEGOpeningPresentation::UEGOpeningPresentation()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.bStartWithTickEnabled=true;
 ElysiaDefinitionAsset=FSoftObjectPath(TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"));
 ElysiaIdleAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/Elysia/Combat20261101/Animations/A_C1_Standby_BoardInPlace.A_C1_Standby_BoardInPlace"));
 ElysiaWalkAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/Elysia/Opening20261108/Animations/A_C1_Walk_InPlace.A_C1_Walk_InPlace"));
 CorruptionSitAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaCorruption/Opening20261108/LocalMocap20261109/ReferencePose/A_Corruption_Local_SitIdleLoop.A_Corruption_Local_SitIdleLoop"));
 CorruptionRiseAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaCorruption/Opening20261108/Animations/A_Corruption_UncrossStand_NativeBase.A_Corruption_UncrossStand_NativeBase"));
 CorruptionWalkAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaCorruption/Opening20261108/LocalMocap20261109/ReferencePose/A_Corruption_Local_WalkLoop.A_Corruption_Local_WalkLoop"));
 CorruptionWalkLoopAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaCorruption/Opening20261108/LocalMocap20261109/ReferencePose/A_Corruption_Local_WalkLoop.A_Corruption_Local_WalkLoop"));
 CorruptionStandAsset=FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/Settlement20261109/A_Corruption_Idle.A_Corruption_Idle"));
 StairAsset=FSoftObjectPath(TEXT("/Game/Assets/Environments/HI3ElysiaDuelArena/Meshes/SM_Stage_ElysionC_Euro_Area_Stair_LOD0.SM_Stage_ElysionC_Euro_Area_Stair_LOD0"));
}

UEGOpeningPresentation* UEGOpeningPresentation::Install(AEGDemoGameMode* Mode)
{
 if(!IsValid(Mode) || !Mode->GetWorld() || Mode->GetNetMode()==NM_DedicatedServer)return nullptr;
 if(auto* Existing=Mode->FindComponentByClass<UEGOpeningPresentation>())return Existing;
 auto* Component=NewObject<UEGOpeningPresentation>(Mode);
 Mode->AddInstanceComponent(Component);
 Component->OpeningToken=Mode->GetOpeningToken();
 Component->RegisterComponent();
 return Component;
}

bool UEGOpeningPresentation::IsLoading() const
{return State==EEGOpeningPresentationState::Initializing || State==EEGOpeningPresentationState::LoadingPreparing || State==EEGOpeningPresentationState::Failed;}
bool UEGOpeningPresentation::IsLoadingSceneVisible() const
{return State==EEGOpeningPresentationState::AwaitingStart || State==EEGOpeningPresentationState::LoadingPreparing;}
bool UEGOpeningPresentation::IsAwaitingStart() const {return State==EEGOpeningPresentationState::AwaitingStart;}
bool UEGOpeningPresentation::IsPreparingAfterStart() const {return State==EEGOpeningPresentationState::LoadingPreparing||State==EEGOpeningPresentationState::Departure;}
bool UEGOpeningPresentation::IsDeparturePlaying() const {return State==EEGOpeningPresentationState::Departure;}
bool UEGOpeningPresentation::IsWaitingForRunResources() const
{
 const float Duration=FMath::IsFinite(CameraRiseSeconds)?FMath::Clamp(CameraRiseSeconds,.1f,30.f):15.f;
 return State==EEGOpeningPresentationState::Departure&&Elapsed>=Duration;
}
bool UEGOpeningPresentation::IsOpeningActive() const
{return State==EEGOpeningPresentationState::Initializing || State==EEGOpeningPresentationState::AwaitingStart || State==EEGOpeningPresentationState::LoadingPreparing || State==EEGOpeningPresentationState::Departure || State==EEGOpeningPresentationState::Failed;}
FString UEGOpeningPresentation::GetLoadingError() const
{
 if(!Error.IsEmpty())return Error;
 const auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
 return Mode?Mode->GetOpeningPreloadError():FString();
}

void UEGOpeningPresentation::PlayClip(AEGArtPiece* Actor,UAnimSequence* Clip,bool bLoop)
{
 if(!IsValid(Actor)||!IsValid(Actor->Body)||!IsValid(Clip)||!Actor->Body->GetSkeletalMeshAsset() ||
    !Clip->GetSkeleton() || !Clip->GetSkeleton()->IsCompatibleMesh(Actor->Body->GetSkeletalMeshAsset()))return;
 Actor->Body->PlayAnimation(Clip,bLoop);
 Actor->Body->SetPlayRate(1.f);
}

bool UEGOpeningPresentation::InitializeScene(bool bSynchronous)
{
 auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
 UWorld* World=GetWorld();
 if(!Mode||!World||!Mode->GetBoard()||!Mode->RunDefinition)
 {FailOpening(TEXT("开场棋盘配置不完整，请重新进入游戏。"));return false;}
 if(!GetOpeningBoardCamera(World))
 {FailOpening(TEXT("开场无法找到棋盘镜头，请检查场景配置后重试。"));return false;}
 FVector Friendly,Enemy;
 if(!UEGBoardMasterPresentation::GetMasterAnchors(Mode->GetBoard(),Friendly,Enemy))
 {FailOpening(TEXT("开场无法定位棋手位置，请检查棋盘配置。"));return false;}
 if(!SceneLoad.IsValid())
 {
  TArray<FSoftObjectPath> Paths={ElysiaDefinitionAsset.ToSoftObjectPath(),ElysiaIdleAsset.ToSoftObjectPath(),CorruptionWalkAsset.ToSoftObjectPath(),CorruptionWalkLoopAsset.ToSoftObjectPath(),CorruptionStandAsset.ToSoftObjectPath()};
  SceneLoad=bSynchronous?UAssetManager::GetStreamableManager().RequestSyncLoad(Paths):
                         UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
  if(!SceneLoad.IsValid()){Error=TEXT("无法请求开场资源。" );State=EEGOpeningPresentationState::Failed;}
  if(!bSynchronous||!SceneLoad.IsValid())return false;
 }
 if(bSynchronous&&SceneLoad.IsValid()&&!SceneLoad->HasLoadCompleted())SceneLoad->WaitUntilComplete();
 if(!SceneLoad->HasLoadCompleted())return false;
 auto* ElysiaDefinition=ElysiaDefinitionAsset.Get();
 auto* EnemyDefinition=Mode->RunDefinition->Stages.Num()==7&&!Mode->RunDefinition->Stages[6].Enemies.IsEmpty()
   ?Mode->RunDefinition->Stages[6].Enemies[0].Definition.Get():nullptr;
 ElysiaIdle=ElysiaIdleAsset.Get();
 CorruptionWalk=CorruptionWalkAsset.Get();CorruptionWalkLoop=CorruptionWalkLoopAsset.Get();CorruptionStand=CorruptionStandAsset.Get();
 if(!ElysiaDefinition||!EnemyDefinition||!ElysiaIdle||!CorruptionWalk||!CorruptionWalkLoop||!CorruptionStand)
 {
  Error=TEXT("开场角色动作资源不可用，请检查资源并重新启动。");
  State=EEGOpeningPresentationState::Failed;
  return false;
 }
 if((ElysiaDefinition->bUseSplitConfiguration&&!ElysiaDefinition->PresentationDefinition)||
    (EnemyDefinition->bUseSplitConfiguration&&!EnemyDefinition->PresentationDefinition))
 {FailOpening(TEXT("开场角色表现配置不完整，请检查资源。"));return false;}
 if(!PuppetLoad.IsValid())
 {
  TSet<FSoftObjectPath> UniquePaths;
  auto AddPath=[&](const FSoftObjectPath& Path){if(Path.IsValid())UniquePaths.Add(Path);};
  for(const UEGHeroDefinition* Hero:{ElysiaDefinition,EnemyDefinition})
  {
   AddPath(Hero->GetMesh().ToSoftObjectPath());AddPath(Hero->GetIdleAnimation().ToSoftObjectPath());
   AddPath(Hero->GetAnimationClass().ToSoftObjectPath());
   for(const auto& Weapon:Hero->GetWeapons())
   {
    AddPath(Weapon.Mesh.ToSoftObjectPath());AddPath(Weapon.DeformableMesh.ToSoftObjectPath());
    AddPath(Weapon.AnimationClass.ToSoftObjectPath());AddPath(Weapon.NockedArrow.ToSoftObjectPath());
   }
  }
  if(UniquePaths.IsEmpty()){FailOpening(TEXT("开场角色网格资源未配置。"));return false;}
  TArray<FSoftObjectPath> Paths=UniquePaths.Array();
  PuppetLoad=bSynchronous?UAssetManager::GetStreamableManager().RequestSyncLoad(MoveTemp(Paths)):
                          UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths));
  if(!PuppetLoad.IsValid())FailOpening(TEXT("无法请求开场角色资源。"));
  if(!bSynchronous||!PuppetLoad.IsValid())return false;
 }
 if(bSynchronous&&PuppetLoad.IsValid()&&!PuppetLoad->HasLoadCompleted())PuppetLoad->WaitUntilComplete();
 if(!PuppetLoad->HasLoadCompleted())return false;
 // Reuse the unique authored environment, never spawn a replacement stair.
 UStaticMeshComponent* OriginalStair=nullptr;
 for(TActorIterator<AStaticMeshActor> It(World);It;++It)
 {
  UStaticMeshComponent* Mesh=It->GetStaticMeshComponent();
  if(!IsValid(Mesh)||!IsValid(Mesh->GetStaticMesh())||
     FSoftObjectPath(Mesh->GetStaticMesh())!=StairAsset.ToSoftObjectPath())continue;
  if(OriginalStair){FailOpening(TEXT("开场找到多处原楼梯，请检查场景配置。"));return false;}
  OriginalStair=Mesh;
 }
 if(!OriginalStair){FailOpening(TEXT("开场找不到原楼梯，请检查场景配置。"));return false;}
 const FTransform StairTransform=OriginalStair->GetComponentTransform();
 const FVector StairWorldScale=StairTransform.GetScale3D();
 if(StairTransform.ContainsNaN()||FMath::Abs(StairWorldScale.X)<=KINDA_SMALL_NUMBER||
    FMath::Abs(StairWorldScale.Y)<=KINDA_SMALL_NUMBER||FMath::Abs(StairWorldScale.Z)<=KINDA_SMALL_NUMBER)
 {FailOpening(TEXT("开场楼梯变换无效，请检查场景配置。"));return false;}
 const FTransform ReferenceStair(FQuat::Identity,FVector::ZeroVector,FVector(1.3125));
 auto Rebase=[&](const FVector& Reference){return StairTransform.TransformPosition(ReferenceStair.InverseTransformPosition(Reference));};
 const float Height=Mode->GetBoard()->UnitCenterHeight;
 FriendlyGround=Friendly-FVector(0,0,Height);EnemyGround=Enemy-FVector(0,0,Height);
 SeatGround=Rebase(SeatReferenceLocation);
 SeatRootLocation=SeatGround;
 RiseHorizontalOffset=StairTransform.TransformVector(ReferenceStair.InverseTransformVector(FVector(0,-10,0)));
 RiseOffsetReferenceFrame=StairTransform;
 ApproachEnd=Rebase(ElysiaReferenceLocation);
 const FVector Compose=Rebase(CameraReferenceLocation);
 const FVector Focus=Rebase(CameraReferenceFocus);
 CameraInwardLocation=Rebase(CameraInwardReferenceLocation);
 CameraInwardRotation=StairTransform.GetRotation()*CameraInwardReferenceRotation.Quaternion();
 const float ElysiaSourceZ=FMath::Abs(ElysiaDefinition->GetMeshScale().Z);
 const float SourceCorruptionZ=FMath::Abs(EnemyDefinition->GetMeshScale().Z);
 if(!FMath::IsFinite(OpeningBodyWorldScale)||OpeningBodyWorldScale<=KINDA_SMALL_NUMBER||
    !FMath::IsFinite(OpeningCameraFOV)||!FMath::IsFinite(ElysiaIdlePlayRate)||ElysiaIdlePlayRate<=0.f||!FMath::IsFinite(CorruptionIdlePlayRate)||CorruptionIdlePlayRate<=0.f||!FMath::IsFinite(CorruptionStandYawOffset)||Compose.ContainsNaN()||Focus.ContainsNaN()||CameraInwardLocation.ContainsNaN()||
    CameraInwardReferenceRotation.ContainsNaN()||CameraInwardRotation.ContainsNaN()||
    !FMath::IsFinite(CameraOverheadFraction)||!FMath::IsFinite(CameraOverheadLift)||!FMath::IsFinite(CameraOverheadPitch)||
    SeatGround.ContainsNaN()||ApproachEnd.ContainsNaN()||!FMath::IsFinite(ElysiaSourceZ)||
    !FMath::IsFinite(SourceCorruptionZ)||ElysiaSourceZ<=KINDA_SMALL_NUMBER||SourceCorruptionZ<=KINDA_SMALL_NUMBER||
    OpeningBodyWorldScale/ElysiaSourceZ>1.5f||OpeningBodyWorldScale/SourceCorruptionZ>1.5f)
 {FailOpening(TEXT("开场场景或角色尺寸参数无效，请检查配置。"));return false;}
 auto SpawnPiece=[&](UEGHeroDefinition* Definition,const FVector& Position,const FVector& Facing)
 {
  FVector Direction=Facing-Position;Direction.Z=0.f;
  const FRotator Rotation=Direction.Rotation()+Definition->GetPresentationRotation();
  auto* Piece=World->SpawnActorDeferred<AEGArtPiece>(AEGArtPiece::StaticClass(),FTransform(Rotation,Position),Mode,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!Piece)return static_cast<AEGArtPiece*>(nullptr);
  Piece->HeroVisualDefinition=Definition;Piece->SetActorEnableCollision(false);
  Piece->FinishSpawning(FTransform(Rotation,Position));
  Piece->SetBattlePresentation(true);
  const float SourceZ=FMath::Abs(Definition->GetMeshScale().Z);
  Piece->SetMasterModelScale(SourceZ>KINDA_SMALL_NUMBER?OpeningBodyWorldScale/SourceZ:1.f);
  // These opening clips were calibrated on the runtime mesh's own origin.
  Piece->Body->SetRelativeLocation(FVector::ZeroVector);
  if(Piece->Outline)Piece->Outline->SetRelativeLocation(FVector::ZeroVector);
  for(UActorComponent* Component:Piece->GetComponentsByTag(USceneComponent::StaticClass(),TEXT("EGWeapon")))
   if(auto* Weapon=Cast<USceneComponent>(Component))Weapon->SetVisibility(false,true);
  return Piece;
 };
 Elysia=SpawnPiece(ElysiaDefinition,ApproachEnd,SeatGround);
 Corruption=SpawnPiece(EnemyDefinition,SeatGround,ApproachEnd);
 Camera=World->SpawnActor<ACameraActor>();
 if(!Elysia||!Corruption||!Camera||!IsValid(Elysia->Body)||!IsValid(Corruption->Body))
 {FailOpening(TEXT("开场场景创建失败，请重新启动。"));return false;}
 Corruption->AddActorWorldRotation(FRotator(0.f,CorruptionStandYawOffset,0.f));
 // Read the actual single-node time only after the skeletal component advances.
 AddTickPrerequisiteComponent(Corruption->Body);
 CorruptionOpeningModelScale=SourceCorruptionZ>KINDA_SMALL_NUMBER?OpeningBodyWorldScale/SourceCorruptionZ:1.f;
 CorruptionMasterModelScale=UEGBoardMasterPresentation::GetDisplayScale(EnemyDefinition,Corruption);
 Camera->GetCameraComponent()->SetFieldOfView(FMath::Clamp(OpeningCameraFOV,5.f,170.f));
 auto Compatible=[](AEGArtPiece* Piece,UAnimSequence* Clip)
 {return IsValid(Piece)&&IsValid(Piece->Body)&&Piece->Body->GetSkeletalMeshAsset()&&IsValid(Clip)&&Clip->GetSkeleton()&&Clip->GetSkeleton()->IsCompatibleMesh(Piece->Body->GetSkeletalMeshAsset());};
 if(!Compatible(Elysia,ElysiaIdle)||!Compatible(Corruption,CorruptionWalk)||!Compatible(Corruption,CorruptionWalkLoop)||!Compatible(Corruption,CorruptionStand))
 {
  Error=TEXT("开场动作与角色骨架不匹配，请检查资源。");State=EEGOpeningPresentationState::Failed;
  return false;
 }
 if(ElysiaIdle->GetPlayLength()<=KINDA_SMALL_NUMBER||CorruptionStand->GetPlayLength()<=KINDA_SMALL_NUMBER)
 {Error=TEXT("开场动作时长无效，请检查资源。");State=EEGOpeningPresentationState::Failed;return false;}
 const float CorruptionWalkSpeed=FMath::Clamp(static_cast<float>(95.4*Corruption->Body->GetComponentScale().GetAbsMax()),40.f,450.f);
 WalkDuration=FMath::Max(2.f,static_cast<float>(FVector::Dist2D(SeatGround,EnemyGround)/CorruptionWalkSpeed));
 PlayClip(Elysia,ElysiaIdle,true);
 Elysia->Body->SetPlayRate(ElysiaIdlePlayRate);
 PlayClip(Corruption,CorruptionStand,true);
 Corruption->Body->SetPlayRate(CorruptionIdlePlayRate);
 auto* Lens=Camera->GetCameraComponent();Lens->bConstrainAspectRatio=true;Lens->AspectRatio=16.f/9.f;
 Lens->PostProcessSettings.bOverride_ColorSaturation=true;Lens->PostProcessSettings.ColorSaturation=FVector4(.90,.90,.94,1.);
 Lens->PostProcessSettings.bOverride_VignetteIntensity=true;Lens->PostProcessSettings.VignetteIntensity=.28f;
 Lens->PostProcessSettings.bOverride_BloomIntensity=true;Lens->PostProcessSettings.BloomIntensity=.18f;
 State=bStartWasRequested?EEGOpeningPresentationState::LoadingPreparing:EEGOpeningPresentationState::AwaitingStart;
 Elapsed=0.f;

 MoveCamera(Compose,Focus);
 return true;
}

bool UEGOpeningPresentation::RetryOpeningScene()
{
 if(State!=EEGOpeningPresentationState::Failed||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 for(const auto& Effect:ActiveEffects)if(Effect.IsValid())Effect->Destroy();ActiveEffects.Reset();
 RestoreCamera();ReleasePuppets();
 if(IsValid(Camera))Camera->Destroy();Camera=nullptr;
 if(SceneLoad.IsValid())SceneLoad->CancelHandle();SceneLoad.Reset();
 if(PuppetLoad.IsValid())PuppetLoad->CancelHandle();PuppetLoad.Reset();
 if(DepartureLoad.IsValid())DepartureLoad->CancelHandle();DepartureLoad.Reset();
 Error.Reset();State=EEGOpeningPresentationState::Initializing;Elapsed=0.f;
 bWaitingForSitBoundary=false;
 bDepartureCompleted=false;bStageTransitionCompleted=false;bStageEntranceCompleted=false;
 return true;
}

void UEGOpeningPresentation::FailOpening(const FString& Reason)
{
 Error=Reason.IsEmpty()?TEXT("开局初始化失败，请重新进入游戏。"):Reason;
 bDepartureCompleted=false;bStageTransitionCompleted=false;bStageEntranceCompleted=false;
 State=EEGOpeningPresentationState::Failed;
}

void UEGOpeningPresentation::CancelTransition()
{
 for(const auto& Effect:ActiveEffects)if(Effect.IsValid())Effect->Destroy();ActiveEffects.Reset();
 StageToken.Invalidate();bStageTransitionCompleted=false;bStageEntranceCompleted=false;RestoreHiddenUnits();State=EEGOpeningPresentationState::Completed;bFadedForTransition=false;
 if(auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)
  if(PC->PlayerCameraManager)PC->PlayerCameraManager->StartCameraFade(.82f,0.f,.3f,FLinearColor::Black,false,false);
}

void UEGOpeningPresentation::FailStageEntrance(const FString& Reason)
{
 for(const auto& Effect:ActiveEffects)if(Effect.IsValid())Effect->Destroy();ActiveEffects.Reset();
 RestoreHiddenUnits();bStageEntranceCompleted=false;
 StageEntranceError=Reason.IsEmpty()?TEXT("敌人入场失败，请重试。"):Reason;
 State=EEGOpeningPresentationState::StageEntranceFailed;
 if(bFadedForTransition)if(auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)
  if(PC->PlayerCameraManager)PC->PlayerCameraManager->StartCameraFade(.82f,0.f,.3f,FLinearColor::Black,false,false);
 bFadedForTransition=false;
}

void UEGOpeningPresentation::MoveCamera(const FVector& Location,const FVector& Focus)
{
 if(!Camera)return;
 LastCameraLocation=Location;LastCameraFocus=Focus;
 Camera->SetActorLocationAndRotation(Location,(Focus-Location).Rotation());
 if(auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)
  if(PC->IsLocalController()&&PC->GetViewTarget()!=Camera)PC->SetViewTarget(Camera);
}

bool UEGOpeningPresentation::RequestBegin()
{
 auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
 if(!IsAwaitingStart()||!Mode||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 bStartWasRequested=true;bWaitingForSitBoundary=false;
 State=EEGOpeningPresentationState::LoadingPreparing;Elapsed=0.f;
 return true;
}

void UEGOpeningPresentation::StartDeparture()
{
 if(!IsValid(Camera)||!IsValid(Camera->GetCameraComponent()))
 {FailOpening(TEXT("开场镜头已失效，请重试。"));return;}
 bWaitingForSitBoundary=false;State=EEGOpeningPresentationState::Departure;Elapsed=0.f;
 DepartureCameraLocation=Camera->GetActorLocation();DepartureCameraRotation=Camera->GetActorQuat();CameraLogMilestone=0;
 const auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
 const auto* Board=Mode?Mode->GetBoard():nullptr;
 CameraOrbitCenter=Board&&IsValid(Board->LayoutSource)?Board->LayoutSource->GetActorLocation():FVector::ZeroVector;
 if(const ACameraActor* BoardCamera=GetOpeningBoardCamera(GetWorld()))
 {
  const UCameraComponent* Source=BoardCamera->GetCameraComponent();
  UCameraComponent* Target=Camera->GetCameraComponent();
  Target->bConstrainAspectRatio=Source->bConstrainAspectRatio;
  Target->bOverrideAspectRatioAxisConstraint=Source->bOverrideAspectRatioAxisConstraint;
  Target->AspectRatioAxisConstraint=Source->AspectRatioAxisConstraint;
  Target->AspectRatio=Source->AspectRatio;
 }
 bTeleportSpawned=false;bTeleported=false;bTurnStarted=false;bWalkStarted=false;bCorruptionSettled=false;bCorruptionHandoffSafe=false;ElysiaRevealStart=-1.f;
 // Keep the standing idles until the camera has completely left both actors.
}

bool UEGOpeningPresentation::SpawnArrival(const FVector& Location,bool bEnemy,float Radius)
{
 if(!GetWorld()||GetWorld()->bIsTearingDown)return false;
 ActiveEffects.RemoveAll([](const auto& Effect){return !Effect.IsValid();});
 if(auto* FX=GetWorld()->SpawnActor<AEGTeleportVisual>(Location,FRotator::ZeroRotator))
 {
  FX->SetOwner(GetOwner());FX->Initialize(bEnemy,Radius);
  if(!IsValid(FX)||FX->IsActorBeingDestroyed())return false;
  ActiveEffects.Add(FX);return true;
 }
 return false;
}

void UEGOpeningPresentation::BeginStageTransition(FGuid Token)
{
 if(!Token.IsValid()||!GetWorld()||GetWorld()->bIsTearingDown)return;
 StageToken=Token;bStageTransitionCompleted=false;bStageEntranceCompleted=false;State=EEGOpeningPresentationState::StageTransition;Elapsed=0.f;
 bFadedForTransition=true;
 if(auto* PC=GetWorld()->GetFirstPlayerController())if(PC->PlayerCameraManager)
  PC->PlayerCameraManager->StartCameraFade(0.f,.82f,StageTransitionSeconds,FLinearColor::Black,false,true);
}

void UEGOpeningPresentation::BeginStageEntrance(FGuid Token)
{
 auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
 if(!Token.IsValid()||!Mode||!GetWorld()||GetWorld()->bIsTearingDown)return;
 RestoreHiddenUnits();StageEntranceError.Reset();StageToken=Token;bStageTransitionCompleted=false;bStageEntranceCompleted=false;State=EEGOpeningPresentationState::StageEntrance;Elapsed=0.f;bUnitsRevealed=false;
 StageEffectsStart=-1.f;
 StageEffectsDelay=bFadedForTransition?.5f:.1f;
 for(auto* Unit:Mode->GetCombatUnits())
  if(IsValid(Unit)&&Unit->IsAlive_Implementation()&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
  {
   HiddenUnits.Emplace(Unit,Unit->IsHidden());Unit->SetActorHiddenInGame(true);
  }
 if(bFadedForTransition)if(auto* PC=GetWorld()->GetFirstPlayerController())if(PC->PlayerCameraManager)
  PC->PlayerCameraManager->StartCameraFade(.82f,0.f,.5f,FLinearColor::Black,false,false);
 bFadedForTransition=false;
}

void UEGOpeningPresentation::RestoreHiddenUnits()
{
 for(const auto& Entry:HiddenUnits)if(Entry.Key.IsValid())Entry.Key->SetActorHiddenInGame(Entry.Value);
 HiddenUnits.Reset();
}

void UEGOpeningPresentation::RestoreCamera()
{
 if(!GetWorld()||GetWorld()->bIsTearingDown)return;
 auto* PC=GetWorld()->GetFirstPlayerController();
 if(!PC||PC->GetViewTarget()!=Camera)return;
 for(TActorIterator<AEGBoardSceneSettings> It(GetWorld());It;++It)
  if(IsValid(It->FixedCamera)){PC->SetViewTarget(It->FixedCamera);return;}
}

void UEGOpeningPresentation::ReleasePuppets()
{
 for(AActor* SetPiece:OpeningSetPieces)if(IsValid(SetPiece))SetPiece->Destroy();OpeningSetPieces.Reset();
 CorruptionWalkLoopMontage=nullptr;CorruptionRoutePoints.Reset();CorruptionRouteDistances.Reset();
 if(IsValid(PendingCorruption))PendingCorruption->Destroy();PendingCorruption=nullptr;
 if(IsValid(Elysia))Elysia->Destroy();Elysia=nullptr;
 if(IsValid(Corruption)&&IsValid(Corruption->Body))RemoveTickPrerequisiteComponent(Corruption->Body);
 if(IsValid(Corruption))Corruption->Destroy();Corruption=nullptr;
}

void UEGOpeningPresentation::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
 auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
 if(!Mode||!GetWorld()||GetWorld()->bIsTearingDown)return;
 if(!bWidgetInstalled)
 {
  auto* PC=GetWorld()->GetFirstPlayerController();
  if(PC&&PC->IsLocalController())bWidgetInstalled=IsValid(UEGOpeningWidget::Install(PC));
  else
  {
   ControllerWaitSeconds+=FMath::Max(0.f,DeltaTime);
   if(ControllerWaitSeconds>=10.f&&State==EEGOpeningPresentationState::Initializing)
   {FailOpening(TEXT("开场无法找到本地玩家控制器，请重新进入游戏。"));UE_LOG(LogTemp,Error,TEXT("Opening local player controller was not available after 10 seconds"));}
   return;
  }
 }
 if(State==EEGOpeningPresentationState::Initializing){InitializeScene();return;}
 if((State==EEGOpeningPresentationState::AwaitingStart||State==EEGOpeningPresentationState::LoadingPreparing||
     State==EEGOpeningPresentationState::Departure)&&!GetOpeningBoardCamera(GetWorld()))
 {FailOpening(TEXT("棋盘镜头已失效，请检查场景配置后重试。"));return;}
 Elapsed+=FMath::Max(0.f,DeltaTime);
 if(State==EEGOpeningPresentationState::LoadingPreparing)
 {
  const ACameraActor* BoardCamera=GetOpeningBoardCamera(GetWorld());
  if(!BoardCamera||!IsValid(CorruptionWalk)||!IsValid(CorruptionWalkLoop)||!IsValid(CorruptionStand))
  {FailOpening(TEXT("开场动作或镜头尚未就绪，请重试。"));return;}
  CameraTargetLocation=BoardCamera->GetActorLocation();CameraTargetRotation=BoardCamera->GetActorRotation();CameraTargetFOV=BoardCamera->GetCameraComponent()->FieldOfView;
  StartDeparture();

 }
 else if(State==EEGOpeningPresentationState::Departure)
 {
  if(!IsValid(Elysia)||!IsValid(Corruption)||!IsValid(Corruption->HeroVisualDefinition)||!IsValid(Camera)||!IsValid(Mode->GetBoard()))
  {FailOpening(TEXT("开场角色、棋盘或镜头已失效，请重新进入游戏。"));return;}
  const ACameraActor* BoardCamera=GetOpeningBoardCamera(GetWorld());
  if(!BoardCamera){FailOpening(TEXT("棋盘镜头已失效，请检查场景配置后重试。"));return;}
  CameraTargetLocation=BoardCamera->GetActorLocation();CameraTargetRotation=BoardCamera->GetActorRotation();CameraTargetFOV=BoardCamera->GetCameraComponent()->FieldOfView;
  if(CameraTargetLocation.ContainsNaN()||CameraTargetRotation.ContainsNaN()||!FMath::IsFinite(CameraTargetFOV))
  {FailOpening(TEXT("棋盘镜头参数无效，请检查场景配置后重试。"));return;}
  const float CameraDuration=FMath::IsFinite(CameraRiseSeconds)?FMath::Clamp(CameraRiseSeconds,.1f,30.f):15.f;
  const float T=FMath::Clamp(Elapsed/CameraDuration,0.f,1.f);
  FVector CameraLocation;
  FQuat CameraRotation;
  OpeningOrbitCamera(DepartureCameraLocation,CameraTargetLocation,CameraOrbitCenter,
                    DepartureCameraRotation,CameraTargetRotation.Quaternion(),T,CameraLocation,CameraRotation);
  if(CameraLocation.ContainsNaN()||CameraRotation.ContainsNaN())
  {FailOpening(TEXT("开场镜头路径无效，请检查场景配置后重试。"));return;}
  const float OrientationAlpha=FMath::SmoothStep(0.f,1.f,T);
  const float CameraFOV=FMath::Lerp(FMath::Clamp(OpeningCameraFOV,5.f,170.f),CameraTargetFOV,OrientationAlpha);
  Camera->SetActorLocationAndRotation(CameraLocation,CameraRotation);
  Camera->GetCameraComponent()->SetFieldOfView(CameraFOV);
  static constexpr float Milestones[]={0.f,.10f,.23f,.50f,.77f,1.f};
  while(CameraLogMilestone<UE_ARRAY_COUNT(Milestones)&&T>=Milestones[CameraLogMilestone])
  {
   UE_LOG(LogTemp,Display,TEXT("OpeningCamera phase=%u alpha=%.3f elapsed=%.3f location=%s rotation=%s fov=%.2f target=%s targetRotation=%s targetFov=%.2f orbitCenter=%s"),
          static_cast<uint32>(CameraLogMilestone),T,Elapsed,*CameraLocation.ToString(),*CameraRotation.Rotator().ToString(),CameraFOV,
          *CameraTargetLocation.ToString(),*CameraTargetRotation.ToString(),CameraTargetFOV,*CameraOrbitCenter.ToString());
   ++CameraLogMilestone;
  }
  if(!bTeleported&&Elapsed>=.5f&&OpeningActorOutsideCamera(Elysia,Camera))
  {
   bTeleported=true;
   Elysia->SetActorHiddenInGame(true);
   Elysia->SetActorLocation(FriendlyGround);
   Elysia->SetMasterModelScale(UEGBoardMasterPresentation::GetDisplayScale(Elysia->HeroVisualDefinition,Elysia));
   Elysia->SetActorRotation((EnemyGround-FriendlyGround).Rotation()+Elysia->HeroVisualDefinition->GetPresentationRotation());
   // Reveal only while the destination is also outside the camera.
   UE_LOG(LogTemp,Display,TEXT("OpeningHiddenHandoff Elysia elapsed=%.3f"),Elapsed);
  }
  if(bTeleported&&Elysia->IsHidden())
  {
   if(!bTeleportSpawned&&OpeningActorOutsideCamera(Elysia,Camera))
   {
    Elysia->SetActorHiddenInGame(false);
    UE_LOG(LogTemp,Display,TEXT("OpeningElysiaRevealed elapsed=%.3f mode=offscreen"),Elapsed);
   }
   else if(!bTeleportSpawned)
   {
    // The new orbit looks across the board, so the destination may remain in
    // view throughout the journey. Reveal under the existing arrival effect
    // rather than waiting forever or making the actor pop into an open shot.
    if(!SpawnArrival(FriendlyGround,false,95.f))
    {FailOpening(TEXT("开场角色传送效果无法创建，请重试。"));return;}
    bTeleportSpawned=true;ElysiaRevealStart=Elapsed;
    UE_LOG(LogTemp,Display,TEXT("OpeningElysiaArrivalEffect elapsed=%.3f location=%s"),Elapsed,*FriendlyGround.ToString());
   }
   else if(Elapsed-ElysiaRevealStart>=.46f)
   {
    Elysia->SetActorHiddenInGame(false);
    UE_LOG(LogTemp,Display,TEXT("OpeningElysiaRevealed elapsed=%.3f mode=arrival delay=%.3f"),Elapsed,Elapsed-ElysiaRevealStart);
   }
  }
  if(!bWalkStarted&&!PendingCorruption)
  {
   // Prepare a separate, fixed-size board master at the stair opening. It is
   // hidden before spawning finishes; neither visible actor is ever scaled.
   const auto* Grid=Mode->GetBoard()->LayoutSource.Get();
   if(!IsValid(Grid))
   {FailOpening(TEXT("开场无法确定楼梯口位置，请重试。"));return;}
   const FTransform Basis=Grid->GetActorTransform();
   // The upper landing ends around Y=3425 in the authored 7x7 scene. A point
   // near Y=3300 is still on the flat opening and leaves a true offscreen
   // window for the full-sized actor during the camera's initial climb.
   FVector Entrance=Basis.TransformPosition(FVector(0.f,Grid->Rows*Grid->CellDepth*.5f+1550.f,0.f));
   FVector Control=Basis.TransformPosition(FVector(0.f,Grid->Rows*Grid->CellDepth*.5f+600.f,0.f));
   Entrance.Z=EnemyGround.Z;Control.Z=EnemyGround.Z;
   const FRotator InitialFacing=(Control-Entrance).Rotation()+Corruption->HeroVisualDefinition->GetPresentationRotation();
   auto* Piece=GetWorld()->SpawnActorDeferred<AEGArtPiece>(AEGArtPiece::StaticClass(),FTransform(InitialFacing,Entrance),Mode,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if(!Piece){FailOpening(TEXT("开场棋手无法创建，请重试。"));return;}
   Piece->SetActorHiddenInGame(true);Piece->SetActorEnableCollision(false);
   Piece->HeroVisualDefinition=Corruption->HeroVisualDefinition;
   Piece->FinishSpawning(FTransform(InitialFacing,Entrance));
   Piece->SetBattlePresentation(true);Piece->SetMasterModelScale(CorruptionMasterModelScale);
   if(!IsValid(Piece->Body)||!IsValid(Piece->Outline))
   {Piece->Destroy();FailOpening(TEXT("开场棋手模型不可用，请重试。"));return;}
   for(UActorComponent* Component:Piece->GetComponentsByTag(USceneComponent::StaticClass(),TEXT("EGWeapon")))
    if(auto* Weapon=Cast<USceneComponent>(Component))Weapon->SetVisibility(false,true);
   UEGHeroDefinition* OpeningDefinition=DuplicateObject<UEGHeroDefinition>(Piece->HeroVisualDefinition,Piece);
   if(OpeningDefinition&&OpeningDefinition->PresentationDefinition)
    OpeningDefinition->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(OpeningDefinition->PresentationDefinition,OpeningDefinition);
   if(!OpeningDefinition||(OpeningDefinition->bUseSplitConfiguration&&!OpeningDefinition->PresentationDefinition))
   {Piece->Destroy();FailOpening(TEXT("开场角色动作配置无法创建，请重试。"));return;}
   if(OpeningDefinition->bUseSplitConfiguration)OpeningDefinition->PresentationDefinition->IdleAnimation=CorruptionStand;
   else OpeningDefinition->IdleAnimation=CorruptionStand;
   if(!FEGAnimationPlayback::Configure(Piece->Body,OpeningDefinition)||!Cast<UEGCombatAnimInstance>(Piece->Body->GetAnimInstance()))
   {Piece->Destroy();FailOpening(TEXT("开场角色动作无法衔接，请检查动画配置。"));return;}
   Piece->Body->UpdateComponentToWorld();Piece->Body->UpdateBounds();
   PendingCorruption=Piece;
   UE_LOG(LogTemp,Display,TEXT("OpeningCorruptionPrepared elapsed=%.3f hidden=1 location=%s bounds=%s radius=%.2f sourceBounds=%s sourceRadius=%.2f"),Elapsed,*Entrance.ToString(),*Piece->Body->Bounds.Origin.ToString(),Piece->Body->Bounds.SphereRadius,*Corruption->Body->Bounds.Origin.ToString(),Corruption->Body->Bounds.SphereRadius);
  }
  if(!bWalkStarted&&IsValid(PendingCorruption)&&Elapsed>=.5f&&
     OpeningActorOutsideCamera(Corruption,Camera)&&OpeningActorOutsideCamera(PendingCorruption,Camera))
  {
#if WITH_DEV_AUTOMATION_TESTS
   CorruptionHandoffEvidence.CameraTransform=Camera->GetActorTransform();
   CorruptionHandoffEvidence.SourceCenter=Corruption->Body->Bounds.Origin;
   CorruptionHandoffEvidence.DestinationCenter=PendingCorruption->Body->Bounds.Origin;
   CorruptionHandoffEvidence.SourceRadius=Corruption->Body->Bounds.SphereRadius+80.;
   CorruptionHandoffEvidence.DestinationRadius=PendingCorruption->Body->Bounds.SphereRadius+80.;
   CorruptionHandoffEvidence.FieldOfView=Camera->GetCameraComponent()->FieldOfView;
   CorruptionHandoffEvidence.AspectRatio=Camera->GetCameraComponent()->AspectRatio;
#endif
   // Both the old and destination bodies are fully offscreen. Transfer once
   // and begin walking immediately, without a visible spawn or staged wait.
   const auto* Grid=Mode->GetBoard()->LayoutSource.Get();
   FVector Control=Grid->GetActorTransform().TransformPosition(FVector(0.f,Grid->Rows*Grid->CellDepth*.5f+600.f,0.f));
   Control.Z=EnemyGround.Z;
   const FVector Start=PendingCorruption->GetActorLocation();
   const FVector FinalDirection=(EnemyGround-Control).GetSafeNormal2D();
   const float ToGoal=FVector::Dist2D(Control,EnemyGround);
   const float StopDistance=FMath::Min(static_cast<float>(PlatformApproachOffset.Size2D()),ToGoal*.7f);
   if(FinalDirection.IsNearlyZero()||StopDistance<1.f)
   {FailOpening(TEXT("开场角色行走路线无效，请重试。"));return;}
   RiseEndRootLocation=EnemyGround-FinalDirection*StopDistance;
   RiseEndRootLocation.Z=EnemyGround.Z;
   CorruptionRoutePoints.Reset();CorruptionRouteDistances.Reset();CorruptionRouteLength=0.f;
   constexpr int32 Steps=64;
   for(int32 I=0;I<=Steps;++I)
   {
    const float P=static_cast<float>(I)/Steps;
    const FVector A=FMath::Lerp(Start,Control,static_cast<double>(P));
    const FVector B=FMath::Lerp(Control,RiseEndRootLocation,static_cast<double>(P));
    FVector Point=FMath::Lerp(A,B,static_cast<double>(P));
    if(I==0)Point=Start;else if(I==Steps)Point=RiseEndRootLocation;
    if(I>0)CorruptionRouteLength+=FVector::Dist2D(CorruptionRoutePoints.Last(),Point);
    CorruptionRoutePoints.Add(Point);CorruptionRouteDistances.Add(CorruptionRouteLength);
   }
   if(!FMath::IsFinite(CorruptionRouteLength)||CorruptionRouteLength<1.f)
   {FailOpening(TEXT("开场角色行走路线无效，请重试。"));return;}
   UAnimInstance* Instance=PendingCorruption->Body->GetAnimInstance();
   const float LoopLength=CorruptionWalkLoop->GetPlayLength();
   const float BaseSpeed=FMath::Max(1.f,static_cast<float>(95.4*PendingCorruption->Body->GetComponentScale().GetAbsMax()));
   const float ArcSeconds=CorruptionRouteLength/BaseSpeed;
   if(!Instance||!FMath::IsFinite(LoopLength)||LoopLength<=KINDA_SMALL_NUMBER||!FMath::IsFinite(ArcSeconds)||ArcSeconds<=0.f)
   {FailOpening(TEXT("开场行走循环无效，请检查资源。"));return;}
   const int32 Loops=FMath::Clamp(FMath::CeilToInt(ArcSeconds/LoopLength)+2,1,1000);
   CorruptionWalkLoopMontage=UAnimMontage::CreateSlotAnimationAsDynamicMontage(CorruptionWalkLoop,TEXT("FullBody"),.2f,.2f,1.f,Loops,-1.f);
   if(!CorruptionWalkLoopMontage||Instance->Montage_Play(CorruptionWalkLoopMontage,1.f)<=0.f)
   {FailOpening(TEXT("开场角色行走循环无法播放，请检查动画配置。"));return;}
   AEGArtPiece* Old=Corruption;
   RemoveTickPrerequisiteComponent(Old->Body);
   Corruption=PendingCorruption;PendingCorruption=nullptr;
   AddTickPrerequisiteComponent(Corruption->Body);
   Old->Destroy();Corruption->SetActorHiddenInGame(false);
   CorruptionRouteDistance=0.f;CorruptionWalkStartTime=Elapsed;
   CorruptionTurnFrom=Corruption->GetActorQuat();
   WalkDuration=FMath::Clamp(PlatformWalkSeconds,1.f,6.f);
   WalkStartElapsed=Elapsed+ArcSeconds;
   bWalkStarted=true;bTurnStarted=false;bCorruptionHandoffSafe=true;
   UE_LOG(LogTemp,Display,TEXT("OpeningCorruptionSafeHandoff elapsed=%.3f sourceOffscreen=1 destinationOffscreen=1 start=%s control=%s approach=%s target=%s length=%.2f seconds=%.3f scale=%s"),Elapsed,*Start.ToString(),*Control.ToString(),*RiseEndRootLocation.ToString(),*EnemyGround.ToString(),CorruptionRouteLength,ArcSeconds,*Corruption->Body->GetComponentScale().ToString());
  }
  if(bWalkStarted&&!bCorruptionSettled)
  {
   const float BaseSpeed=FMath::Max(1.f,static_cast<float>(95.4*Corruption->Body->GetComponentScale().GetAbsMax()));
   const float Age=FMath::Max(0.f,Elapsed-CorruptionWalkStartTime);
   const float ArcProgress=FMath::Clamp(BaseSpeed*Age/CorruptionRouteLength,0.f,1.f);
   if(!bTurnStarted)
   {
    CorruptionRouteDistance=CorruptionRouteLength*ArcProgress;
    int32 Index=1;
    while(Index<CorruptionRouteDistances.Num()-1&&CorruptionRouteDistances[Index]<CorruptionRouteDistance)++Index;
    const float Before=CorruptionRouteDistances[Index-1],After=CorruptionRouteDistances[Index];
    const float Along=After>Before?FMath::Clamp((CorruptionRouteDistance-Before)/(After-Before),0.f,1.f):0.f;
    const FVector Start=CorruptionRoutePoints[Index-1],End=CorruptionRoutePoints[Index];
    Corruption->SetActorLocation(FMath::Lerp(Start,End,static_cast<double>(Along)));
    const FVector Forward=End-Start;
    const FQuat TravelRotation=(Forward.Rotation()+Corruption->HeroVisualDefinition->GetPresentationRotation()).Quaternion();
    const float InitialTurn=FMath::SmoothStep(0.f,1.f,FMath::Clamp(Age/.25f,0.f,1.f));
    Corruption->SetActorRotation(FQuat::Slerp(CorruptionTurnFrom,TravelRotation,InitialTurn));
    if(ArcProgress>=1.f)
    {
     UAnimInstance* Instance=Corruption->Body->GetAnimInstance();
     const float ClipLength=CorruptionWalk->GetPlayLength();
     if(!Instance||!FMath::IsFinite(ClipLength)||ClipLength<=KINDA_SMALL_NUMBER)
     {FailOpening(TEXT("开场停步动作无效，请检查资源。"));return;}
     if(CorruptionWalkLoopMontage)Instance->Montage_Stop(.70f,CorruptionWalkLoopMontage);
     UAnimMontage* StopMontage=UAnimMontage::CreateSlotAnimationAsDynamicMontage(CorruptionWalk,TEXT("FullBody"),.70f,.4f,1.f,1,-1.f);
     if(!StopMontage||Instance->Montage_Play(StopMontage,ClipLength/WalkDuration)<=0.f)
     {FailOpening(TEXT("开场停步动作无法播放，请检查动画配置。"));return;}
     // The loop uses the definition's authored presentation yaw; the sliced
     // source stop travels mesh +X. Crossfade that axis change with the same
     // .70-second animation blend rather than instantly rotating the actor.
     WalkStartElapsed=Elapsed;CorruptionTurnFrom=Corruption->GetActorQuat();
     CorruptionStopRotation=(EnemyGround-RiseEndRootLocation).Rotation().Quaternion();
     CorruptionTurnTo=((FriendlyGround-EnemyGround).Rotation()+Corruption->HeroVisualDefinition->GetPresentationRotation()+FRotator(0.f,CorruptionStandYawOffset,0.f)).Quaternion();
     bTurnStarted=true;
     UE_LOG(LogTemp,Display,TEXT("OpeningCorruptionFinalApproach elapsed=%.3f from=%s"),Elapsed,*Corruption->GetActorLocation().ToString());
    }
   }
   if(bTurnStarted)
   {
    const float Progress=FMath::Clamp((Elapsed-WalkStartElapsed)/WalkDuration,0.f,1.f);
    float TravelProgress=Progress;
    if(CorruptionWalk->HasCurveData(OpeningApproachProgressCurve,false))
     TravelProgress=FMath::Clamp(CorruptionWalk->EvaluateCurveData(OpeningApproachProgressCurve,FAnimExtractContext(Progress*CorruptionWalk->GetPlayLength()),false),0.f,1.f);
    Corruption->SetActorLocation(FMath::Lerp(RiseEndRootLocation,EnemyGround,static_cast<double>(TravelProgress)));
    const float StartBlend=FMath::SmoothStep(0.f,1.f,FMath::Clamp((Elapsed-WalkStartElapsed)/.7f,0.f,1.f));
    const FQuat Walking=FQuat::Slerp(CorruptionTurnFrom,CorruptionStopRotation,StartBlend);
    const float FinalStart=FMath::Max(0.f,WalkDuration-.4f);
    const float FinalBlend=FMath::SmoothStep(0.f,1.f,FMath::Clamp((Elapsed-WalkStartElapsed-FinalStart)/FMath::Max(.01f,WalkDuration-FinalStart),0.f,1.f));
    Corruption->SetActorRotation(FQuat::Slerp(Walking,CorruptionTurnTo,FinalBlend));
    if(Progress>=1.f)
    {
     bCorruptionSettled=true;
     UE_LOG(LogTemp,Display,TEXT("OpeningActorArrived elapsed=%.3f location=%s"),Elapsed,*Corruption->GetActorLocation().ToString());
    }
   }
  }
  // The camera may finish first; allow the actor's complete continuous walk
  // and final stop before treating an unfinished entrance as a failure.
  const float HandoffDeadline=bWalkStarted?FMath::Max(CameraDuration+1.f,WalkStartElapsed+WalkDuration+1.f):CameraDuration+1.f;
  if(Elapsed>HandoffDeadline&&(!bTeleported||!bCorruptionSettled||Elysia->IsHidden()||Corruption->IsHidden()))
  {
   UE_LOG(LogTemp,Error,TEXT("OpeningHandoffTimeout elapsed=%.3f walkStarted=%d settled=%d oldOutside=%d pending=%d pendingOutside=%d pendingRadius=%.2f"),
    Elapsed,bWalkStarted?1:0,bCorruptionSettled?1:0,OpeningActorOutsideCamera(Corruption,Camera)?1:0,IsValid(PendingCorruption)?1:0,
    OpeningActorOutsideCamera(PendingCorruption,Camera)?1:0,IsValid(PendingCorruption)&&IsValid(PendingCorruption->Body)?PendingCorruption->Body->Bounds.SphereRadius:0.);
   FailOpening(TEXT("开场镜头未完成角色交接，请重试。"));return;
  }
  if(T>=1.f&&bTeleported&&bCorruptionSettled&&!Elysia->IsHidden()&&!Corruption->IsHidden()&&Mode->IsOpeningPreloadReady())
  {
   RestoreCamera();ReleasePuppets();State=EEGOpeningPresentationState::Completed;bDepartureCompleted=true;
   Mode->CompleteOpeningPresentation(OpeningToken);
  }
 }
 else if(State==EEGOpeningPresentationState::StageTransition&&Elapsed>=StageTransitionSeconds)
 {const FGuid Token=StageToken;State=EEGOpeningPresentationState::Completed;bStageTransitionCompleted=true;Mode->CompleteStageTransition(Token);}
 else if(State==EEGOpeningPresentationState::StageEntrance)
 {
  // Start after fade-in, and measure from the actual spawn frame so a loading
  // hitch cannot consume the whole portal before it is ever rendered.
  if(StageEffectsStart<0.f&&Elapsed>=StageEffectsDelay)
  {
   StageEffectsStart=Elapsed;
   for(const auto& Entry:HiddenUnits)
    if(auto* Unit=Entry.Key.Get();IsValid(Unit)&&Unit->IsAlive_Implementation())
    {
     FVector Foot=Unit->GetActorLocation();
     Foot.Z=Mode->GetBoard()?Mode->GetBoard()->CellToWorld(FIntPoint::ZeroValue).Z-Mode->GetBoard()->UnitCenterHeight+5.f:
      Foot.Z-Unit->GetSimpleCollisionHalfHeight()+5.f;
     SpawnArrival(Foot,true,80.f);
    }
  }
  const float EffectAge=StageEffectsStart<0.f?-1.f:Elapsed-StageEffectsStart;
  if(!bUnitsRevealed&&EffectAge>=.46f){RestoreHiddenUnits();bUnitsRevealed=true;}
  if(Elapsed>=StageEntranceSeconds&&EffectAge>=1.3f)
  {const FGuid Token=StageToken;RestoreHiddenUnits();State=EEGOpeningPresentationState::Completed;bStageEntranceCompleted=true;Mode->CompleteStageEntrance(Token);}
 }
}

void UEGOpeningPresentation::EndPlay(const EEndPlayReason::Type Reason)
{
 RestoreHiddenUnits();RestoreCamera();ReleasePuppets();
 bDepartureCompleted=false;bStageTransitionCompleted=false;bStageEntranceCompleted=false;StageToken.Invalidate();
 for(const auto& Effect:ActiveEffects)if(Effect.IsValid())Effect->Destroy();ActiveEffects.Reset();
 if(SceneLoad.IsValid())SceneLoad->CancelHandle();SceneLoad.Reset();
 if(PuppetLoad.IsValid())PuppetLoad->CancelHandle();PuppetLoad.Reset();
 if(DepartureLoad.IsValid())DepartureLoad->CancelHandle();DepartureLoad.Reset();
 if(IsValid(Camera))Camera->Destroy();Camera=nullptr;
 Super::EndPlay(Reason);
}
