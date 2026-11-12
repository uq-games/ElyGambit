#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalRenderPublic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGDemoGameMode.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGWidgets.h"

namespace
{
class FMonsterReleaseStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1600;Settings->NewWindowHeight=900;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1600,900))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

/** A controlled graph pose sample, not a natural combat or visual-acceptance test. */
class FMonsterReleaseGraphical final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGDemoCombatUnit> Unit;
 TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<AActor> OriginalView;
 FString Directory,CurrentPath;
 double Deadline=0.,SampleAfter=0.;
 int32 MonsterIndex=0,PoseIndex=0,State=0;
 float GroundZ=0.f;
 const TCHAR* PoseName(int32 I) const
 {static const TCHAR* Names[]={TEXT("idle"),TEXT("walk"),TEXT("attack"),TEXT("skill"),TEXT("hit"),TEXT("death"),TEXT("death_end")};return Names[I];}
 void Cleanup(APlayerController* PC)
 {
  if(PC&&OriginalView.IsValid())PC->SetViewTarget(OriginalView.Get());
  if(Unit.IsValid())Unit->Destroy();Unit.Reset();
  if(Camera.IsValid())Camera->Destroy();Camera.Reset();
 }
 bool Fail(APlayerController* PC,const FString& Message)
 {Test->AddError(Message);Cleanup(PC);return true;}
public:
 explicit FMonsterReleaseGraphical(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.){Deadline=FPlatformTime::Seconds()+180.;Directory=FPaths::ProjectSavedDir()/TEXT("ReleaseAudit20261110/MonsterGraphical")/FGuid::NewGuid().ToString(EGuidFormats::Digits);IFileManager::Get().MakeDirectory(*Directory,true);}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  if(FPlatformTime::Seconds()>Deadline)return Fail(PC,FString::Printf(TEXT("Monster graph capture timed out: monster=%d pose=%d state=%d"),MonsterIndex,PoseIndex,State));
  if(!World||!Mode||!PC||!HUD||!Mode->RunDefinition||!Mode->GetBoard())return false;
  if(!Mode->RunDefinition->Stages.IsValidIndex(3))return Fail(PC,TEXT("Formal run lacks stage 4 definitions"));
  const auto& Definitions=Mode->RunDefinition->Stages[3].OrdinaryMonsterDefinitions;
  if(Definitions.Num()!=4)return Fail(PC,FString::Printf(TEXT("Expected four production monster definitions, got %d"),Definitions.Num()));
  if(State==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   // This isolated presentation fixture must not auto-start the preparation timer.
   Mode->SetActorTickEnabled(false);
   if(auto* Main=HUD->GetMainWidget())Main->SetVisibility(ESlateVisibility::Collapsed);
   if(auto* Panel=HUD->GetRunPanel())Panel->SetVisibility(ESlateVisibility::Collapsed);
   for(auto* Existing:Mode->GetCombatUnits())if(Existing)Existing->SetActorHiddenInGame(true);
   const FVector Position=Mode->GetBoard()->CellToWorld({3,3});GroundZ=Position.Z-Mode->GetBoard()->UnitCenterHeight;
   OriginalView=PC->GetViewTarget();
   const FVector Eye=Position+FVector(900.f,900.f,520.f);
   Camera=World->SpawnActor<ACameraActor>(Eye,(Position+FVector(0,0,100)-Eye).Rotation());
   if(!Camera.IsValid())return Fail(PC,TEXT("Cannot create temporary screenshot camera"));
   PC->SetViewTarget(Camera.Get());State=1;
  }
  if(State==1)
  {
   if(MonsterIndex>=Definitions.Num())
   {
    Test->AddInfo(FString::Printf(TEXT("28 controlled production AnimGraph screenshots: %s"),*Directory));
    Test->AddInfo(TEXT("Fixed midpoint and final-death samples are graph-evaluated; action notifies, gameplay, natural motion and audiovisual acceptance are not tested. CPU-skinned body vertices are geometric evidence only and may include hidden or transparent sections; visual contact still requires screenshot inspection."));
    Cleanup(PC);return true;
   }
   auto* Definition=Definitions[MonsterIndex].Get();
   if(!IsValid(Definition))return Fail(PC,TEXT("Production monster definition is missing"));
   if(!Unit.IsValid())
   {
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Unit=World->SpawnActor<AEGDemoCombatUnit>(Mode->GetBoard()->CellToWorld({3,3}),FRotator::ZeroRotator,Params);
    if(!Unit.IsValid()||!Unit->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1))return Fail(PC,TEXT("Cannot initialize production monster"));
    Unit->GetCharacterMovement()->DisableMovement();Unit->SetActorTickEnabled(false);
    Unit->GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    const auto Bounds=Unit->GetMesh()->Bounds;
    const float Distance=FMath::Clamp(Bounds.BoxExtent.GetMax()*5.f,550.f,1800.f);
    const FVector Focus=Bounds.Origin;
    const FVector Eye=Focus+FVector(.68f,.68f,.32f)*Distance;
    Camera->SetActorLocationAndRotation(Eye,(Focus-Eye).Rotation());
   }
   auto* Mesh=Unit->GetMesh();auto* Anim=Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance());
   if(!Anim||!Mesh->GetSkeletalMeshAsset())return Fail(PC,FString::Printf(TEXT("%s missing formal mesh or combat AnimGraph"),*Definition->HeroId.ToString()));
   FEGAnimationPlayback::Stop(Mesh);
   if(PoseIndex==2||PoseIndex==3)
   {
    const auto Action=PoseIndex==2?EGPresentationAction::Attack:EGPresentationAction::Skill;
    if(!FEGAnimationPlayback::Play(Mesh,Action))return Fail(PC,FString::Printf(TEXT("%s cannot play %s montage"),*Definition->HeroId.ToString(),PoseName(PoseIndex)));
    auto* Montage=Anim->GetActionMontage();
    if(!Montage)return Fail(PC,TEXT("Production action montage missing"));
    Anim->Montage_SetPosition(Montage,Montage->GetPlayLength()*.5f);Anim->Montage_Pause(Montage);
   }
   else
   {
    UAnimSequence* Clip=PoseIndex==0?Definition->GetIdleAnimation().LoadSynchronous():PoseIndex==1?Definition->GetWalkAnimation().LoadSynchronous():
     PoseIndex==4?Definition->GetHitAnimation().LoadSynchronous():Definition->GetDeathAnimation().LoadSynchronous();
    if(!Clip)return Fail(PC,FString::Printf(TEXT("%s missing %s clip"),*Definition->HeroId.ToString(),PoseName(PoseIndex)));
    const int32 GraphState=PoseIndex==1?1:PoseIndex==4?4:PoseIndex>=5?3:0;
    const float SampleTime=PoseIndex==6?FMath::Max(0.f,Clip->GetPlayLength()-1.f/120.f):Clip->GetPlayLength()*.5f;
    Anim->SetSourcePose(Clip,SampleTime,GraphState);
   }
   SampleAfter=FPlatformTime::Seconds()+.25;State=2;return false;
  }
  if(State==2)
  {
   if(FPlatformTime::Seconds()<SampleAfter||FScreenshotRequest::IsScreenshotRequested())return false;
   auto* Definition=Definitions[MonsterIndex].Get();auto* Mesh=Unit->GetMesh();
   const FBoxSphereBounds Bounds=Mesh->Bounds;float LowestBone=MAX_flt;FName LowestName=NAME_None;
   for(int32 I=0;I<Mesh->GetNumBones();++I)
   {
    const FName Name=Mesh->GetBoneName(I);const float Z=Mesh->GetBoneLocation(Name).Z;
    if(FMath::IsFinite(Z)&&Z<LowestBone){LowestBone=Z;LowestName=Name;}
   }
   if(LowestName.IsNone())return Fail(PC,TEXT("No sampled skeleton bones"));
   const float Bottom=Bounds.Origin.Z-Bounds.BoxExtent.Z;
   Test->AddInfo(FString::Printf(TEXT("monster=%s pose=%s mesh=%s graph=%s groundZ=%.3f boundsMinZ=%.3f boundsDelta=%.3f lowestBone=%s boneZ=%.3f boneDelta=%.3f actorRot=%s meshRelativeRot=%s meshWorldRot=%s configuredRot=%s configuredGroundOffset=%.3f"),
    *Definition->HeroId.ToString(),PoseName(PoseIndex),*GetPathNameSafe(Mesh->GetSkeletalMeshAsset()),*GetNameSafe(Mesh->GetAnimInstance()->GetClass()),GroundZ,Bottom,Bottom-GroundZ,
    *LowestName.ToString(),LowestBone,LowestBone-GroundZ,*Unit->GetActorRotation().ToString(),*Mesh->GetRelativeRotation().ToString(),*Mesh->GetComponentRotation().ToString(),
    *Definition->GetPresentationRotation().ToString(),Definition->GetPresentationGroundOffset()));
   if(PoseIndex==6)
   {
    // Bounds and bones can be far from the rendered surface. Sample the actual
    // CPU-skinned body mesh in world space; material visibility still needs a
    // human to inspect the corresponding frame.
    TArray<FFinalSkinVertex> Vertices;Mesh->GetCPUSkinnedVertices(Vertices,0);
    if(Vertices.IsEmpty())return Fail(PC,TEXT("No CPU-skinned death-end vertices for ")+Definition->HeroId.ToString());
    const auto* RenderData=Mesh->GetSkeletalMeshAsset()->GetResourceForRendering();
    if(!RenderData||RenderData->LODRenderData.IsEmpty())return Fail(PC,TEXT("Death-end mesh render data unavailable"));
    TBitArray<> Shown(false,Vertices.Num());
    for(const auto& Section:RenderData->LODRenderData[0].RenderSections)
     if(Mesh->IsMaterialSectionShown(Section.MaterialIndex,0))
      for(uint32 I=0;I<Section.NumVertices&&Section.BaseVertexIndex+I<uint32(Vertices.Num());++I)Shown[Section.BaseVertexIndex+I]=true;
    float MinZ=MAX_flt,MaxZ=-MAX_flt,Closest=MAX_flt;int32 NearGround=0,ShownCount=0;
    for(int32 I=0;I<Vertices.Num();++I)
    {
     if(!Shown[I])continue;
     ++ShownCount;
     const float Z=Mesh->GetComponentTransform().TransformPosition(FVector(Vertices[I].Position)).Z;
     if(!FMath::IsFinite(Z))return Fail(PC,TEXT("Non-finite skinned vertex"));
     MinZ=FMath::Min(MinZ,Z);MaxZ=FMath::Max(MaxZ,Z);
     const float Distance=FMath::Abs(Z-GroundZ);Closest=FMath::Min(Closest,Distance);
     if(Distance<=30.f)++NearGround;
    }
    if(ShownCount==0)return Fail(PC,TEXT("Death-end mesh has no shown section vertices"));
    Test->AddInfo(FString::Printf(TEXT("death-end body geometry monster=%s LOD=0 totalVertices=%d shownSectionVertices=%d minZ=%.3f maxZ=%.3f minDelta=%.3f maxDelta=%.3f closestToGround=%.3f verticesWithin30cm=%d"),
     *Definition->HeroId.ToString(),Vertices.Num(),ShownCount,MinZ,MaxZ,MinZ-GroundZ,MaxZ-GroundZ,Closest,NearGround));
    Test->TestTrue(*(Definition->HeroId.ToString()+TEXT(" death-end body has vertices within 30cm of board")),NearGround>0);
   }
   CurrentPath=FPaths::ConvertRelativePathToFull(Directory/(Definition->HeroId.ToString()+TEXT("_")+PoseName(PoseIndex)+TEXT(".png")));
   FScreenshotRequest::RequestScreenshot(CurrentPath,false,false,false,FIntRect(),true);State=3;return false;
  }
  if(FScreenshotRequest::IsScreenshotRequested()||!IFileManager::Get().FileExists(*CurrentPath))return false;
  TArray<uint8> Bytes;
  if(!FFileHelper::LoadFileToArray(Bytes,*CurrentPath)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;
  if(Bytes[End]!=0||Bytes[End+1]!=0||Bytes[End+2]!=0||Bytes[End+3]!=0||
     Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  Test->AddInfo(TEXT("Screenshot: ")+CurrentPath);
  if(++PoseIndex>=7){PoseIndex=0;++MonsterIndex;if(Unit.IsValid())Unit->Destroy();Unit.Reset();}
  State=1;return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterReleaseGraphicalTest,"ElyGambit.Monsters.Release.GraphicalPoseSamples",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterReleaseGraphicalTest::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Monster graph samples require an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Monster graph samples require a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FMonsterReleaseStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FMonsterReleaseGraphical(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
