#include "Presentation/EGBoardModelSizing.h"
#include "Actor/EGBoard.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BoneContainer.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Game/EGChessGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Misc/MemStack.h"

namespace
{
struct FInstanceSizing
{
 TWeakObjectPtr<const UEGHeroDefinition> Definition;
 float Scale=1.f;
};
// Actor-scoped weak keys do not retain a world or a unit. A configured instance
// keeps its measured scale across passive/action assemblies; configuration
// entry points explicitly Forget before re-resolving.
TMap<TWeakObjectPtr<const UObject>,FInstanceSizing> InstanceSizing;

bool UsesVillVSourceScale(const UEGHeroDefinition* Definition)
{
 return Definition->HeroKind==EEGHeroKind::VillV
  ||Definition->HeroId==TEXT("VillV_Gatling")||Definition->HeroId==TEXT("VillV_Cannon");
}
// The source meshes include accessories and large T-pose arms. Measure the
// configured idle pose's central body bones instead of imported mesh bounds.
bool IdleBodySize(const UEGHeroDefinition* Definition,float& Width,float& Height)
{
 const auto& IdleAsset=Definition->GetIdleAnimation();
 UAnimSequence* Idle=IdleAsset.Get();
 if(!Idle&&!IdleAsset.IsNull())Idle=IdleAsset.LoadSynchronous();
 FTransform SourceBodyTransform=FTransform::Identity;
 if(Definition->PresentationDefinition)
 {
  const auto* P=Definition->PresentationDefinition.Get();
  const TSoftObjectPtr<UEGSourcePreviewDefinition>* Assembly=!P->PreparationIdleAssembly.IsNull()
   ? &P->PreparationIdleAssembly : P->IdleAssemblies.IsEmpty()?nullptr:&P->IdleAssemblies[0];
  if(Assembly&&!Assembly->IsNull())
  {
   UEGSourcePreviewDefinition* Source=Assembly->Get();
   if(!Source)Source=Assembly->LoadSynchronous();
   if(Source&&!Source->Motions.IsEmpty())
    for(const auto& Part:Source->Motions[0].Parts)
     if(Part.PartId==TEXT("Body"))
     {
      SourceBodyTransform=Part.Transform;
      if(Part.Animation)Idle=Part.Animation;
      break;
     }
  }
 }
 if(!Idle||!Idle->GetSkeleton())return false;
 struct FBodySample
 {
  FTransform BodyTransform;
  float Width=0.f;
  float Height=0.f;
  bool bValid=false;
 };
 // Device poses can be sampled every frame. Cache only derived values with a
 // weak asset key; the cache never owns animations or changes their data.
 static TMap<TWeakObjectPtr<UAnimSequence>,FBodySample> Samples;
 const TWeakObjectPtr<UAnimSequence> Key(Idle);
 const FTransform Orientation=SourceBodyTransform*FTransform(Definition->GetPresentationRotation(),FVector::ZeroVector,Definition->GetMeshScale());
 if(const FBodySample* Sample=Samples.Find(Key))
  if(Sample->BodyTransform.Equals(Orientation))
  {Width=Sample->Width;Height=Sample->Height;return Sample->bValid;}
 if(Samples.Num()>128)
  for(auto It=Samples.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
 const FReferenceSkeleton& Ref=Idle->GetSkeleton()->GetReferenceSkeleton();
 TArray<FTransform> ComponentPose;ComponentPose.SetNum(Ref.GetNum());
 FBox BodyBox(ForceInit);
 int32 Count=0;
 for(int32 Index=0;Index<Ref.GetNum();++Index)
 {
  // GetBoneTransform leaves unanimated compressed tracks untouched.
  FTransform Local=Ref.GetRefBonePose()[Index];
  Idle->GetBoneTransform(Local,FSkeletonPoseBoneIndex(Index),FAnimExtractContext(0.f,false),false);
  const int32 Parent=Ref.GetParentIndex(Index);
  ComponentPose[Index]=Parent==INDEX_NONE?Local:Local*ComponentPose[Parent];
  const FString Name=Ref.GetBoneName(Index).ToString().ToLower();
  const bool bBody=Name.Contains(TEXT("head"))||Name.Contains(TEXT("neck"))||Name.Contains(TEXT("spine"))
   ||Name.Contains(TEXT("chest"))||Name.Contains(TEXT("pelvis"))||Name.Contains(TEXT("hip"))
   ||Name.Contains(TEXT("clavicle"))||Name.Contains(TEXT("shoulder"))||Name.Contains(TEXT("upperarm"))
   ||Name.Contains(TEXT("thigh"))||Name.Contains(TEXT("calf"))||Name.Contains(TEXT("foot"));
  const bool bExtra=Name.Contains(TEXT("hair"))||Name.Contains(TEXT("weapon"))||Name.Contains(TEXT("cloth"))
   ||Name.Contains(TEXT("twist"))||Name.Contains(TEXT("helper"))||Name.Contains(TEXT("ik_"));
  if(bBody&&!bExtra)
  {
   const FVector Position=Orientation.TransformPosition(ComponentPose[Index].GetTranslation());
   if(!Position.ContainsNaN()){BodyBox+=Position;++Count;}
  }
 }
 if(Count<4||!BodyBox.IsValid)
 {
  Samples.Add(Key,FBodySample{Orientation,0.f,0.f,false});
  return false;
 }
 const FVector Size=BodyBox.GetSize();
 // Bones lie inside the skin. Leave a margin without allowing T-pose props to dominate.
 Width=FMath::Max(Size.X,Size.Y)*1.25f;
 Height=Size.Z*1.15f;
 const bool bValid=FMath::IsFinite(Width)&&FMath::IsFinite(Height)&&Width>1.f&&Height>1.f;
 Samples.Add(Key,FBodySample{Orientation,Width,Height,bValid});
 return bValid;
}

// Sample the actual preparation body mesh with the same retargeting path as
// the runtime anim graph. The old all-body box also includes displaced arms
// and helpers, which are not a reliable proxy for head-to-foot body height.
bool PreparationBodyLandmarks(const UEGHeroDefinition* Definition,float& Width,float& Height)
{
 if(!Definition)return false;
 USkeletalMesh* Mesh=Definition->GetMesh().LoadSynchronous();
 UAnimSequence* Idle=Definition->GetIdleAnimation().LoadSynchronous();
 FTransform BodyTransform=FTransform(Definition->GetPresentationRotation(),FVector::ZeroVector,Definition->GetMeshScale());
 if(const auto* P=Definition->PresentationDefinition.Get())
 {
  const TSoftObjectPtr<UEGSourcePreviewDefinition>* Assembly=!P->PreparationIdleAssembly.IsNull()
   ? &P->PreparationIdleAssembly : P->IdleAssemblies.IsEmpty()?nullptr:&P->IdleAssemblies[0];
  auto* Source=Assembly&&!Assembly->IsNull()?Assembly->LoadSynchronous():nullptr;
  if(Source&&!Source->Motions.IsEmpty())
   for(const auto& Part:Source->Motions[0].Parts)
    if(Part.PartId==TEXT("Body"))
    {
     if(!Part.ParentPartId.IsNone())return false;
     if(Part.Mesh)Mesh=Part.Mesh;
     if(Part.Animation)Idle=Part.Animation;
     // Preparation assemblies are spawned with zero actor rotation, unlike
     // the ordinary non-assembly body which uses PresentationRotation.
     BodyTransform=Part.Transform*FTransform(FRotator::ZeroRotator,FVector::ZeroVector,Definition->GetMeshScale());
     break;
    }
 }
 if(!Mesh||!Idle||!Mesh->GetSkeleton()||!Idle->GetSkeleton()||Idle->IsValidAdditive()||BodyTransform.ContainsNaN())return false;
 const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
 if(Ref.GetNum()<4||Ref.GetNum()>MAX_uint16)return false;
 TArray<FBoneIndexType> Bones;Bones.Reserve(Ref.GetNum());
 int32 Head=INDEX_NONE;TArray<int32> Feet;TArray<int32> Torso;
 for(int32 Index=0;Index<Ref.GetNum();++Index)
 {
  Bones.Add(static_cast<FBoneIndexType>(Index));
  const FString Name=Ref.GetBoneName(Index).ToString();
  if(Name.EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))Head=Index;
  if(Name.EndsWith(TEXT("Foot"),ESearchCase::IgnoreCase)&&!Name.Contains(TEXT("IK")))Feet.Add(Index);
  const bool bTorso=Name.EndsWith(TEXT("Clavicle"),ESearchCase::IgnoreCase)||Name.EndsWith(TEXT("Shoulder"),ESearchCase::IgnoreCase)
   ||Name.EndsWith(TEXT("UpperArm"),ESearchCase::IgnoreCase)||Name.EndsWith(TEXT("Thigh"),ESearchCase::IgnoreCase);
  if(bTorso&&!Name.Contains(TEXT("IK"))&&!Name.Contains(TEXT("Twist")))Torso.Add(Index);
 }
 if(Head==INDEX_NONE||Feet.IsEmpty()||Torso.Num()<2)return false;
 FMemMark Mark(FMemStack::Get());
 FBoneContainer Container;
 Container.InitializeTo(Bones,UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),*Mesh);
 float TotalHeight=0.f,MaxWidth=0.f;
 constexpr int32 SampleCount=4;
 for(int32 SampleIndex=0;SampleIndex<SampleCount;++SampleIndex)
 {
  FCompactPose Pose;Pose.SetBoneContainer(&Container);Pose.ResetToRefPose();
  FBlendedCurve Curve;Curve.InitFrom(Container);UE::Anim::FStackAttributeContainer Attributes;
  FAnimationPoseData PoseData(Pose,Curve,Attributes);
  const float Time=FMath::Max(0.f,Idle->GetPlayLength())*float(SampleIndex)/float(SampleCount);
  Idle->GetAnimationPose(PoseData,FAnimExtractContext(Time,false));
  TArray<FTransform> ComponentPose;ComponentPose.SetNum(Ref.GetNum());
  for(int32 Index=0;Index<Ref.GetNum();++Index)
  {
   const FCompactPoseBoneIndex Compact=Container.MakeCompactPoseIndex(FMeshPoseBoneIndex(Index));
   if(Compact.GetInt()==INDEX_NONE)return false;
   const int32 Parent=Ref.GetParentIndex(Index);
   ComponentPose[Index]=Parent==INDEX_NONE?Pose[Compact]:Pose[Compact]*ComponentPose[Parent];
  }
  const auto Position=[&](int32 Index){return BodyTransform.TransformPosition(ComponentPose[Index].GetTranslation());};
  const FVector HeadPosition=Position(Head);
  float LowestFoot=TNumericLimits<float>::Max();FBox TorsoBox(ForceInit);
  for(int32 Index:Feet){const FVector P=Position(Index);if(P.ContainsNaN())return false;LowestFoot=FMath::Min(LowestFoot,float(P.Z));}
  for(int32 Index:Torso){const FVector P=Position(Index);if(P.ContainsNaN())return false;TorsoBox+=P;}
  const float BodyHeight=float(HeadPosition.Z)-LowestFoot;
  if(HeadPosition.ContainsNaN()||!FMath::IsFinite(BodyHeight)||BodyHeight<=1.f||!TorsoBox.IsValid)return false;
  const FVector Extent=TorsoBox.GetSize();
  TotalHeight+=BodyHeight;MaxWidth=FMath::Max(MaxWidth,float(FMath::Max(Extent.X,Extent.Y)));
 }
 Width=MaxWidth*1.25f;Height=TotalHeight/float(SampleCount);
 return FMath::IsFinite(Width)&&FMath::IsFinite(Height)&&Width>1.f&&Height>1.f;
}
}

float FEGBoardModelSizing::Fit(float CellWidth,float CellDepth,float BodyWidth,float BodyHeight,float HeightCells,float AuthoredScale)
{
 if(!FMath::IsFinite(AuthoredScale)||AuthoredScale<=0.f)AuthoredScale=1.f;
 float Result=FMath::Min(1.f,AuthoredScale);
 if(FMath::IsFinite(CellWidth)&&FMath::IsFinite(CellDepth)&&CellWidth>0.f&&CellDepth>0.f)
 {
  const float Cell=FMath::Min(CellWidth,CellDepth);
  if(FMath::IsFinite(BodyWidth)&&BodyWidth>1.f)Result=FMath::Min(Result,.8f*Cell/BodyWidth);
  if(FMath::IsFinite(BodyHeight)&&BodyHeight>1.f&&FMath::IsFinite(HeightCells)&&HeightCells>0.f)
   Result=FMath::Min(Result,HeightCells*Cell/BodyHeight);
 }
 return FMath::Clamp(Result,.001f,1.f);
}

float FEGBoardModelSizing::Resolve(const UEGHeroDefinition* Definition,const UObject* WorldContext)
{
 if(!Definition)return 1.f;
 // These three rigs form one source-authored seated composition. Independently
 // fitting device skeletons as humanoid bodies changes their relative sizes.
 if(UsesVillVSourceScale(Definition))return 1.f;
 if(WorldContext)
  if(const FInstanceSizing* Existing=InstanceSizing.Find(TWeakObjectPtr<const UObject>(WorldContext)))
   if(Existing->Definition.Get()==Definition)return Existing->Scale;
 const float Authored=Definition->GetBoardModelScale();
 const UWorld* World=WorldContext?WorldContext->GetWorld():nullptr;
 const auto* Mode=World?World->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Board=Mode?Mode->GetBoard():nullptr;
 const auto* Grid=Board?Board->LayoutSource.Get():nullptr;
 if(!Grid)return Fit(0.f,0.f,0.f,0.f,1.f,Authored);
 const FVector GridScale=Grid->GetActorScale3D().GetAbs();
 if(!FMath::IsFinite(Grid->CellWidth)||!FMath::IsFinite(Grid->CellDepth)
  ||Grid->CellWidth*GridScale.X<=0.f||Grid->CellDepth*GridScale.Y<=0.f)
  return Fit(0.f,0.f,0.f,0.f,1.f,Authored);
 float Width=0.f,Height=0.f;
 const bool bMeasuredBody=IdleBodySize(Definition,Width,Height);
 if(!bMeasuredBody)
 {
  // Configured presentation dimensions are already world-space; never use the T-pose bounds.
  Width=Definition->GetPresentationPickRadius()*2.f;
  Height=Definition->GetPresentationHeight();
 }
 float HeightCells=1.35f;
 if(Definition->HeroKind==EEGHeroKind::Can||Definition->HeroKind==EEGHeroKind::Knight)HeightCells=1.1f;
 else if(Definition->HeroId==TEXT("ElysiaCorruption"))HeightCells=1.65f;
 float Scale=Fit(Grid->CellWidth*GridScale.X,Grid->CellDepth*GridScale.Y,Width,Height,HeightCells,Authored);
 // Both Elysia forms depict the same adult. Fit their actual idle body to a
 // slightly shorter silhouette than Eden, excluding hair, bows and ribbons.
 // Do not normalize other heroes or derive height from decorative mesh bounds.
 if(Definition->HeroKind==EEGHeroKind::Elysia||Definition->HeroKind==EEGHeroKind::ElysiaHerrscher)
 {
  static TWeakObjectPtr<UEGHeroDefinition> AdultReference;
  if(!AdultReference.IsValid())AdultReference=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Eden.DA_Eden"));
  float ReferenceWidth=0.f,ReferenceHeight=0.f,ReferenceTorso=0.f,ReferenceBodyHeight=0.f;
  float ElysiaTorso=0.f,ElysiaBodyHeight=0.f;
  if(AdultReference.IsValid()&&IdleBodySize(AdultReference.Get(),ReferenceWidth,ReferenceHeight)
   &&PreparationBodyLandmarks(AdultReference.Get(),ReferenceTorso,ReferenceBodyHeight)
   &&PreparationBodyLandmarks(Definition,ElysiaTorso,ElysiaBodyHeight))
  {
   const float ReferenceScale=Fit(Grid->CellWidth*GridScale.X,Grid->CellDepth*GridScale.Y,
    ReferenceWidth,ReferenceHeight,1.35f,AdultReference->GetBoardModelScale());
   const float AdultHeight=ReferenceBodyHeight*ReferenceScale*.94f;
   Scale=Fit(Grid->CellWidth*GridScale.X,Grid->CellDepth*GridScale.Y,ElysiaTorso,ElysiaBodyHeight,
    AdultHeight/FMath::Min(Grid->CellWidth*GridScale.X,Grid->CellDepth*GridScale.Y),Authored);
  }
 }
 // Collapsed Hua's rig has extra displaced helper/gauntlet bones, so the
 // generic all-bone box can make her body much smaller than Hua after the
 // transformation. Compare the actual idle head and feet via the runtime
 // retargeting path, then keep the same torso-width limit as other units.
 // This changes only the board model multiplier, not the source mesh or FX.
 else if(Definition->HeroKind==EEGHeroKind::CollapsedHua&&Definition->HeroId==TEXT("CollapsedHua"))
 {
  static TWeakObjectPtr<UEGHeroDefinition> HuaReference;
  if(!HuaReference.IsValid())HuaReference=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua.DA_Hua"));
  float ReferenceWidth=0.f,ReferenceHeight=0.f,ReferenceTorso=0.f,ReferenceBodyHeight=0.f;
  float FormTorso=0.f,FormBodyHeight=0.f;
  if(HuaReference.IsValid()&&IdleBodySize(HuaReference.Get(),ReferenceWidth,ReferenceHeight)
   &&PreparationBodyLandmarks(HuaReference.Get(),ReferenceTorso,ReferenceBodyHeight)
   &&PreparationBodyLandmarks(Definition,FormTorso,FormBodyHeight))
  {
   const float CellWidth=Grid->CellWidth*GridScale.X,CellDepth=Grid->CellDepth*GridScale.Y;
   const float ReferenceScale=Fit(CellWidth,CellDepth,ReferenceWidth,ReferenceHeight,1.35f,HuaReference->GetBoardModelScale());
   const float Cell=FMath::Min(CellWidth,CellDepth);
   const float HeightScale=ReferenceBodyHeight*ReferenceScale/FormBodyHeight;
   const float WidthScale=.8f*Cell/FormTorso;
   Scale=FMath::Clamp(FMath::Min(HeightScale,WidthScale)*FMath::Clamp(Authored,.01f,1.f),.001f,2.f);
  }
 }
 if(WorldContext)
 {
  if(InstanceSizing.Num()>128)
   for(auto It=InstanceSizing.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Value().Definition.IsValid())It.RemoveCurrent();
  InstanceSizing.Add(TWeakObjectPtr<const UObject>(WorldContext),FInstanceSizing{TWeakObjectPtr<const UEGHeroDefinition>(Definition),Scale});
 }
 return Scale;
}

void FEGBoardModelSizing::Forget(const UObject* WorldContext)
{
 if(WorldContext)InstanceSizing.Remove(TWeakObjectPtr<const UObject>(WorldContext));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCollapsedHuaBoardHeight,
 "ElyGambit.BoardModel.CollapsedHuaHeight",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCollapsedHuaBoardHeight::RunTest(const FString&)
{
 auto* Hua=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua.DA_Hua"));
 auto* Form=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_CollapsedHua.DA_CollapsedHua"));
 if(!TestNotNull(TEXT("Production Hua"),Hua)||!TestNotNull(TEXT("Production collapsed Hua"),Form))return false;
 TestEqual(TEXT("Production upgraded form identity"),Form->HeroId,FName(TEXT("CollapsedHua")));
 float HuaTorso=0.f,HuaHeight=0.f,FormTorso=0.f,FormHeight=0.f;
 if(!TestTrue(TEXT("Hua idle body landmarks available"),PreparationBodyLandmarks(Hua,HuaTorso,HuaHeight))
  ||!TestTrue(TEXT("Collapsed Hua idle body landmarks available"),PreparationBodyLandmarks(Form,FormTorso,FormHeight)))return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* World=GI->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGChessGameMode>();if(!TestNotNull(TEXT("Fixture game mode"),Mode))return false;
 Mode->bAutoStart=false;World->InitializeActorsForPlay(URL);World->BeginPlay();
 if(!TestNotNull(TEXT("Fixture board"),Mode->GetBoard()))return false;
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->CellWidth=500.f;Grid->CellDepth=500.f;Mode->GetBoard()->UseLayout(Grid);
 auto* HuaContext=World->SpawnActor<AActor>();auto* FormContext=World->SpawnActor<AActor>();
 const float HuaScale=FEGBoardModelSizing::Resolve(Hua,HuaContext);
 const float FormScale=FEGBoardModelSizing::Resolve(Form,FormContext);
 const float Ratio=FormHeight*FormScale/(HuaHeight*HuaScale);
 AddInfo(FString::Printf(TEXT("Hua body %.2f x %.3f, collapsed body %.2f x %.3f, ratio %.3f, torso %.2f"),HuaHeight,HuaScale,FormHeight,FormScale,Ratio,FormTorso*FormScale));
 TestTrue(TEXT("Upgraded body remains close to normal Hua's height"),Ratio>=.90f&&Ratio<=1.05f);
 TestTrue(TEXT("Upgraded torso fits the cell"),FormTorso*FormScale<=400.1f);
 FEGBoardModelSizing::Forget(HuaContext);FEGBoardModelSizing::Forget(FormContext);
 return true;
}
#endif
