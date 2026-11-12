#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "Tests/AutomationEditorCommon.h"
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
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Actor/EGBoard.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"

namespace
{
class FSourceWallStartPIE final : public IAutomationLatentCommand
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
struct FSourceWallCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FSourceWallCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FSourceWallPreview final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 int32 Step=0,SampleIndex=0;
 double Deadline=0.,CaptureDeadline=0.;
 TWeakObjectPtr<AEGChessUnit> Boss;
 TWeakObjectPtr<USkeletalMeshComponent> Body,Bow;
 TWeakObjectPtr<AEGCombatVisual> Rig;
 TWeakObjectPtr<APlayerController> Controller;
 TWeakObjectPtr<AActor> PreviousView;
 TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<UEGBattleContextComponent> Battle;
 TUniquePtr<TStrongObjectPtr<UEGSourcePreviewDefinition>> PrivateAssembly;
 TWeakObjectPtr<UAnimSequence> Candidate;
 TWeakObjectPtr<UClass> PreviousAnimClass;
 EAnimationMode::Type PreviousMode=EAnimationMode::AnimationBlueprint;
 bool bPreviousPauseAnims=false;
 bool bBodyBorrowed=false;
 FGuid Hold;
 TSharedPtr<FSourceWallCapture> Capture;
 const float Samples[4]={0.f,.78f,1.55f,2.333f};

 void Cleanup()
 {
  Capture.Reset();
  if(Controller.IsValid()&&PreviousView.IsValid())
  {
   Controller->SetViewTarget(PreviousView.Get());
   if(Controller->PlayerCameraManager)Controller->PlayerCameraManager->UpdateCamera(0.f);
  }
  if(Camera.IsValid())Camera->Destroy();Camera.Reset();
  if(Rig.IsValid()){Rig->ReleaseBoundBody();Rig->Destroy();}Rig.Reset();
  if(bBodyBorrowed&&Body.IsValid())
  {
   Body->bPauseAnims=bPreviousPauseAnims;
   if(PreviousMode==EAnimationMode::AnimationBlueprint&&PreviousAnimClass.IsValid())Body->SetAnimInstanceClass(PreviousAnimClass.Get());
   else Body->SetAnimationMode(PreviousMode);
  }
  if(Battle.IsValid()&&Hold.IsValid())Battle->SetPresentationHold(Hold,false);
  Hold.Invalidate();PrivateAssembly.Reset();bBodyBorrowed=false;
 }
 bool SetupCamera(UWorld* World)
 {
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  if(!PC||!PC->GetViewTarget()||!Boss.IsValid()||!Body.IsValid())return false;
  FVector Forward=Boss->GetActorForwardVector().GetSafeNormal2D();if(Forward.IsNearlyZero())Forward=FVector(1,0,0);
  const FVector Right=FVector::CrossProduct(FVector::UpVector,Forward);
  const FVector Point=Body->Bounds.Origin,Extent=Body->Bounds.BoxExtent;
  const float TanH=FMath::Tan(FMath::DegreesToRadians(22.5f)),TanV=TanH/(1920.f/1080.f);
  const float Distance=FMath::Max(500.f,FMath::Max(Extent.Z/TanV,Extent.Size2D()/TanH)*2.1f);
  const FVector Eye=Point+Forward*Distance+Right*(Distance*.16f)+FVector(0,0,Distance*.12f);
  auto* View=World->SpawnActor<ACameraActor>(Eye,(Point-Eye).Rotation());if(!View)return false;
  View->GetCameraComponent()->SetFieldOfView(45.f);Controller=PC;PreviousView=PC->GetViewTarget();Camera=View;
  PC->SetViewTarget(View);if(PC->PlayerCameraManager)PC->PlayerCameraManager->UpdateCamera(0.f);
  Test->AddInfo(FString::Printf(TEXT("Source wall preview camera bounds=(%.2f,%.2f,%.2f) distance=%.2f"),Extent.X,Extent.Y,Extent.Z,Distance));
  return true;
 }
 bool BeginCapture(UWorld* World,int32 Index)
 {
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid()||FScreenshotRequest::IsScreenshotRequested()||
     (GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0)return false;
  Capture=MakeShared<FSourceWallCapture>();Capture->Client=Client;
  Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/
   (FString::Printf(TEXT("CorruptionSourceWall_%02d_%0.3fs_"),Index,Samples[Index])+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
  const TWeakPtr<FSourceWallCapture> Weak=Capture;
  Capture->Handle=Client->OnDrawn().AddLambda([Weak]()
  {
   auto State=Weak.Pin();if(!State.IsValid())return;++State->Draws;
   if(State->Draws<2||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  });
  CaptureDeadline=FPlatformTime::Seconds()+12.;return true;
 }
 int32 PollCapture()
 {
  TArray<uint8> Bytes;
  if(Capture.IsValid()&&Capture->bRequested&&Capture->Draws>=4&&!FScreenshotRequest::IsScreenshotRequested()&&
     FFileHelper::LoadFileToArray(Bytes,*Capture->Path)&&Bytes.Num()>=36&&
     Bytes[Bytes.Num()-8]=='I'&&Bytes[Bytes.Num()-7]=='E'&&Bytes[Bytes.Num()-6]=='N'&&Bytes[Bytes.Num()-5]=='D')
  {
   const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
   Test->TestEqual(TEXT("Source wall PIE capture size"),FIntPoint(int32(BE(16)),int32(BE(20))),FIntPoint(1920,1080));
   Test->AddInfo(TEXT("Controlled source wall pose: ")+Capture->Path);Capture.Reset();return 1;
  }
  if(FPlatformTime::Seconds()>CaptureDeadline){Test->AddError(TEXT("Source wall game viewport screenshot timed out"));Capture.Reset();return -1;}
  return 0;
 }
 bool SetSample(float Time)
 {
  auto* Mesh=Body.Get();auto* Assembly=Rig.Get();auto* Clip=Candidate.Get();
  if(!Mesh||!Assembly||!Clip||!Mesh->GetSingleNodeInstance())return false;
  Mesh->SetPosition(FMath::Clamp(Time,0.f,Clip->GetPlayLength()),false);
  if(!Test->TestTrue(TEXT("Candidate single-node time is exact"),FMath::IsNearlyEqual(Mesh->GetSingleNodeInstance()->GetCurrentTime(),Time,.002f)))return false;
  Mesh->TickAnimation(0.f,false);Mesh->RefreshBoneTransforms();
  Assembly->SetSourceTime(Time);
  // Source rig uses the exact Body -> Bip001-L-Hand attachment from the
  // existing formal attack assembly; no synthetic or newly positioned bow.
  if(!Bow.IsValid()||!Bow->IsVisible()||Bow->GetAttachParent()!=Mesh||Bow->GetAttachSocketName()!=TEXT("Bip001-L-Hand"))return false;
  Test->AddInfo(FString::Printf(TEXT("Candidate pose t=%.3f body=%s bow=%s socket=%s socketDistance=%.2f"),Time,
   *GetPathNameSafe(Mesh->GetSkeletalMeshAsset()),*GetPathNameSafe(Bow->GetSkeletalMeshAsset()),*Bow->GetAttachSocketName().ToString(),
   FVector::Distance(Bow->GetComponentLocation(),Mesh->GetSocketLocation(TEXT("Bip001-L-Hand")))));
  return true;
 }
public:
 explicit FSourceWallPreview(FAutomationTestBase* InTest):Test(InTest){}
 virtual ~FSourceWallPreview() override {Cleanup();}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(FString::Printf(TEXT("Source wall preview timed out at step %d"),Step));Cleanup();return true;}
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Hand=Mode->GetCards()->GetHand();if(Hand.IsEmpty())return false;
   if(!Test->TestTrue(TEXT("Deploy opening hero"),Mode->GetCards()->TryDeployHandCard(Hand[0].InstanceId,{3,1}))||!Test->TestTrue(TEXT("Enter test battle"),Mode->EnterCombat()))return true;
   Step=1;return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   TArray<TWeakObjectPtr<AEGChessUnit>> Enemies;
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->CancelAction();
    if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Enemies.Add(Unit);
   }
   auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
   auto* Clip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/SourceWall20261108/A_Corruption_SummonWall_BoardCandidate.A_Corruption_SummonWall_BoardCandidate"));
   if(!Test->TestNotNull(TEXT("Formal Boss definition"),Definition)||!Test->TestNotNull(TEXT("Private source wall animation"),Clip)||!Test->TestNotNull(TEXT("Formal board"),Mode->GetBoard()))return true;
   Boss=Mode->SpawnBattleSummon(Definition,6,Mode->GetBoard()->CellToWorld({3,5}),EEGFaction::Enemy);
   if(!Test->TestNotNull(TEXT("Formal Boss body"),Boss.Get()))return true;
   Boss->GetCombat()->SetComponentTickEnabled(false);Boss->GetHeroSkill()->SetComponentTickEnabled(false);
   for(const auto& Enemy:Enemies)if(Enemy.IsValid())Enemy->Destroy();
   auto* Mesh=Boss->GetVisiblePresentationMesh();Body=Mesh;Candidate=Clip;
   const auto* Presentation=Definition->PresentationDefinition.Get();
   if(!Test->TestNotNull(TEXT("Formal Boss mesh"),Mesh)||!Test->TestNotNull(TEXT("Formal Boss presentation"),Presentation)||
      !Test->TestNotNull(TEXT("Boss skeletal asset"),Mesh->GetSkeletalMeshAsset())||
      !Test->TestTrue(TEXT("Candidate skeleton matches formal body"),Clip->GetSkeleton()==Mesh->GetSkeletalMeshAsset()->GetSkeleton())||
      !Test->TestTrue(TEXT("Candidate contains all requested times"),Clip->GetPlayLength()>=2.333f))return true;
   auto* Source=Presentation->AttackAssemblies.IsValidIndex(0)?Presentation->AttackAssemblies[0].LoadSynchronous():nullptr;
   if(!Test->TestNotNull(TEXT("Existing formal attack assembly"),Source)||!Test->TestTrue(TEXT("Formal assembly has a motion"),!Source->Motions.IsEmpty()))return true;
   auto* Copy=DuplicateObject<UEGSourcePreviewDefinition>(Source,GetTransientPackage());
   if(!Test->TestNotNull(TEXT("Transient candidate assembly"),Copy))return true;
   PrivateAssembly=MakeUnique<TStrongObjectPtr<UEGSourcePreviewDefinition>>(Copy);
   auto& Motion=Copy->Motions[0];Motion.Duration=Clip->GetPlayLength();Motion.Effects.Reset();
   FEGSourcePreviewPart* BodyPart=nullptr;FEGSourcePreviewPart* BowPart=nullptr;
   for(auto& Part:Motion.Parts){if(Part.PartId==TEXT("Body"))BodyPart=&Part;else if(Part.PartId==TEXT("Bow"))BowPart=&Part;}
   if(!Test->TestNotNull(TEXT("Existing assembly body"),BodyPart)||!Test->TestNotNull(TEXT("Existing assembly bow"),BowPart))return true;
   if(!Test->TestTrue(TEXT("Existing bow attaches to left hand"),BowPart->ParentPartId==TEXT("Body")&&BowPart->AttachBone==TEXT("Bip001-L-Hand")&&!BowPart->Animation&&BowPart->Mesh&&Mesh->DoesSocketExist(BowPart->AttachBone)))return true;
   BodyPart->Mesh=Mesh->GetSkeletalMeshAsset();BodyPart->Animation=Clip;BodyPart->StartTime=0.f;BodyPart->Duration=Motion.Duration;BodyPart->bLoop=false;
   BowPart->StartTime=0.f;BowPart->Duration=Motion.Duration;BowPart->bLoop=false;BowPart->bVisible=true;BowPart->Visibility=nullptr;
   auto* Visual=World->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,Boss.Get(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if(!Test->TestNotNull(TEXT("Transient source rig"),Visual))return true;
   Rig=Visual;Visual->Definition=Copy;Visual->bAutoPlay=false;Visual->bLoop=false;Visual->FinishSpawning(FTransform::Identity);Visual->SetActorTickEnabled(false);
   Visual->AttachToComponent(Boss->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
   Visual->SetActorRelativeTransform(FTransform(Presentation->PresentationRotation,FVector(0,0,-88.f+Presentation->PresentationGroundOffset),Presentation->MeshScale));
   Visual->SetBoardModelScale(FEGBoardModelSizing::Resolve(Definition,Boss.Get()),Presentation->PresentationGroundOffset);
   PreviousMode=Mesh->GetAnimationMode();PreviousAnimClass=Mesh->GetAnimClass();bPreviousPauseAnims=Mesh->bPauseAnims;
   Visual->BindBody(Mesh,false);bBodyBorrowed=true;Mesh->PlayAnimation(Clip,false);
   if(!Test->TestNotNull(TEXT("Single-node candidate animation instance"),Mesh->GetSingleNodeInstance()))return true;
   Mesh->GetSingleNodeInstance()->SetPlaying(false);Mesh->bPauseAnims=true;
   TArray<USkeletalMeshComponent*> Parts;Visual->GetComponents<USkeletalMeshComponent>(Parts);
   for(auto* Part:Parts)if(Part&&Part->GetSkeletalMeshAsset()==BowPart->Mesh)Bow=Part;
   if(!Test->TestNotNull(TEXT("Existing bow component was built"),Bow.Get()))return true;
   Battle=Mode->BattleRules;Hold=FGuid::NewGuid();Battle->SetPresentationHold(Hold,true);
   if(!Test->TestTrue(TEXT("Sample first candidate pose and existing bow"),SetSample(Samples[0]))||!Test->TestTrue(TEXT("Frame close camera"),SetupCamera(World)))return true;
   Test->AddInfo(TEXT("Transient preview only: formal attack assembly duplicated in memory, body animation replaced by candidate, authored hand-bow binding retained. No production binding or asset save."));
   Step=2;return false;
  }
  if(Step==2)
  {
   if(!Test->TestTrue(TEXT("Sample candidate body and attached bow"),SetSample(Samples[SampleIndex])))return true;
   if(!BeginCapture(World,SampleIndex))return false;Step=3;return false;
  }
  if(Step==3)
  {
   const int32 Result=PollCapture();if(Result==0)return false;if(Result<0)return true;
   if(++SampleIndex<4){Step=2;return false;}
   Cleanup();return true;
  }
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionSourceWallPreviewPIE,"ElyGambit.Integration.CorruptionSourceWallPreviewPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionSourceWallPreviewPIE::RunTest(const FString&)
{
 if(!FApp::CanEverRender()||!GEditor||GEditor->PlayWorld){AddError(TEXT("Source wall preview requires an idle graphical editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FSourceWallStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FSourceWallPreview(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
