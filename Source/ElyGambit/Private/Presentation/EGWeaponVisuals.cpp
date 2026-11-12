#include "Presentation/EGWeaponVisuals.h"
#include "Data/EGHeroDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/StaticMesh.h"
void EGApplyWeaponVisuals(AActor* Owner,USkeletalMeshComponent* Body,const UEGHeroDefinition* D,float BoardModelScale)
{
 if(!Owner||!Body||!D)return;
 const auto Old=Owner->GetComponentsByTag(USceneComponent::StaticClass(),TEXT("EGWeapon"));
 for(auto* C:Old)C->DestroyComponent();
 for(const auto& W:D->GetWeapons())
 {
  if(!Body->DoesSocketExist(W.Bone))continue;
  USceneComponent* C=nullptr;
  if(auto* Mesh=W.DeformableMesh.LoadSynchronous())
  {
   if(!W.AnimationClass.IsNull())
   {
    auto* Part=NewObject<USkeletalMeshComponent>(Owner);Part->SetSkeletalMeshAsset(Mesh);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
    Part->SetAnimInstanceClass(W.AnimationClass.LoadSynchronous());
    // Body (including secondary motion) evaluates first. Bow never writes to Body.
    Part->AddTickPrerequisiteComponent(Body);C=Part;
   }
   else {auto* Part=NewObject<UPoseableMeshComponent>(Owner);Part->SetSkinnedAssetAndUpdate(Mesh);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);C=Part;}
  }
  else if(auto* Rigid=W.Mesh.LoadSynchronous())
  {auto* Part=NewObject<UStaticMeshComponent>(Owner);Part->SetStaticMesh(Rigid);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);C=Part;}
  if(!C)continue;
  C->ComponentTags.Add(TEXT("EGWeapon"));Owner->AddInstanceComponent(C);C->RegisterComponent();C->AttachToComponent(Body,FAttachmentTransformRules::SnapToTargetNotIncludingScale,W.Bone);
  FTransform Offset=W.Offset;const FVector BoneScale=Body->GetSocketTransform(W.Bone,RTS_World).GetScale3D();
  if(!W.bUseAssetBinding){Offset.SetTranslation(Offset.GetTranslation()*BoneScale.Reciprocal()*BoardModelScale);Offset.SetScale3D(Offset.GetScale3D()*BoneScale.Reciprocal()*BoardModelScale);}
  C->SetRelativeTransform(Offset);
  if(auto* ArrowMesh=W.NockedArrow.LoadSynchronous())
  {
   auto* Arrow=NewObject<UStaticMeshComponent>(Owner);Arrow->SetStaticMesh(ArrowMesh);
   Arrow->ComponentTags.Add(TEXT("EGWeapon"));Arrow->ComponentTags.Add(TEXT("EGNockedArrow"));
   Arrow->SetCollisionEnabled(ECollisionEnabled::NoCollision);Arrow->SetVisibility(false);
   Owner->AddInstanceComponent(Arrow);Arrow->RegisterComponent();Arrow->AttachToComponent(C,FAttachmentTransformRules::SnapToTargetNotIncludingScale,TEXT("ArrowNockSocket"));
   Arrow->SetRelativeRotation(FRotator(-90,0,0));Arrow->SetRelativeScale3D(FVector(.5f));
  }
 }
 for(const auto& B:D->GetHiddenPresentationBones())if(Body->GetBoneIndex(B)!=INDEX_NONE)Body->HideBoneByName(B,EPhysBodyOp::PBO_None);
}
