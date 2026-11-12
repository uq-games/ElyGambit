#include "Character/EGVillVEmplacement.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"

namespace
{
 bool DeviceAnchorHeight(const UEGSourcePreviewDefinition* Definition,int32 Index,float Time,const FTransform& Presentation,float& Height)
 {
  if(!Definition||Definition->Motions.IsEmpty())return false;
  const FName PartId=Index==0?TEXT("Gatling"):TEXT("Cannon");
  const FName BoneName=Index==0?TEXT("Bip_Weapon_Gatling_Root"):TEXT("Bip_Weapon_Cannon_Root");
  for(const auto& Part:Definition->Motions[0].Parts)
  {
   if(Part.PartId!=PartId||!Part.Animation||!Part.Animation->GetSkeleton())continue;
   const auto& Skeleton=Part.Animation->GetSkeleton()->GetReferenceSkeleton();
   int32 BoneIndex=Skeleton.FindBoneIndex(BoneName);if(BoneIndex==INDEX_NONE)continue;
   const float Length=Part.Animation->GetPlayLength();
   const float LocalTime=FMath::Max(0.f,Time-Part.StartTime);
   const float Sample=Part.bLoop&&Length>SMALL_NUMBER?FMath::Fmod(LocalTime,Length):FMath::Clamp(LocalTime,0.f,Length);
   FTransform Bone=FTransform::Identity;
   while(BoneIndex!=INDEX_NONE)
   {
    FTransform Local;
    Part.Animation->GetBoneTransform(Local,FSkeletonPoseBoneIndex(BoneIndex),FAnimExtractContext(Sample,false),false);
    Bone=Bone*Local;BoneIndex=Skeleton.GetParentIndex(BoneIndex);
   }
   Height=(Bone*Part.Transform*Presentation).GetLocation().Z;
   return FMath::IsFinite(Height);
  }
  return false;
 }
}
void AEGVillVEmplacement::SetPilot(AEGChessUnit* Hero,int32 Index)
{Pilot=Hero;DeviceIndex=Index;bHasGroundAnchor=false;GetCombat()->SetComponentTickEnabled(false);}
void AEGVillVEmplacement::NotifyDamageReceived(float Amount)
{OnDamageReceived.Broadcast(Amount);if(Pilot.IsValid())Pilot->NotifyDamageReceived(Amount);}
void AEGVillVEmplacement::ShowDevice(UEGSourcePreviewDefinition* Motion,float Position)
{
 if(!Motion||!IEGCombatant::Execute_IsAlive(this))return;
 const auto* P=GetDefinition()->PresentationDefinition.Get();if(!P)return;
 if(DeviceVisual&&DeviceVisual->Definition!=Motion)ClearDevice();
 if(!DeviceVisual)
 {
  DeviceVisual=GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!DeviceVisual)return;
  DeviceVisual->Definition=Motion;DeviceVisual->bAutoPlay=false;DeviceVisual->bLoop=false;
  DeviceVisual->FinishSpawning(FTransform::Identity);DeviceVisual->SetActorTickEnabled(false);
  DeviceVisual->AttachToComponent(GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
 }
 // Keep the source animation's joints and recoil, but hold the device base at
 // its opening idle height. Read the clip directly: the AnimBP pose may still
 // be from the previous frame when this function runs. Rebuild from the base
 // transform each time, so compensation cannot accumulate or move collision.
 FTransform Presentation(P->PresentationRotation,FVector(0,0,-88+P->PresentationGroundOffset),P->MeshScale);
 const float ModelScale=FEGBoardModelSizing::Resolve(GetDefinition(),this);
 float Height=0.f;
 if(DeviceAnchorHeight(Motion,DeviceIndex,Position,Presentation,Height))
 {
  if(!bHasGroundAnchor){GroundAnchorHeight=Height;bHasGroundAnchor=true;}
  Presentation.AddToTranslation(FVector(0,0,(GroundAnchorHeight-Height)*ModelScale));
 }
 DeviceVisual->SetActorRelativeTransform(Presentation);
 DeviceVisual->SetBoardModelScale(ModelScale,P->PresentationGroundOffset);
 DeviceVisual->BindBody(GetMesh(),true);DeviceVisual->SetSourceTime(Position);
}
void AEGVillVEmplacement::ClearDevice(){if(DeviceVisual){DeviceVisual->Destroy();DeviceVisual=nullptr;}}
float AEGVillVEmplacement::DistanceToCombatSurface(FVector Point,FVector* OutClosest) const
{
 FVector Center=GetActorLocation();Center.Z=Point.Z;
 FVector Direction=(Point-Center).GetSafeNormal2D();
 if(Direction.IsNearlyZero())Direction=FVector::ForwardVector;
 const float Radius=UEGCombatComponent::BlockingRadius(this);
 if(OutClosest)*OutClosest=Center+Direction*Radius;
 return FMath::Max(0.f,float(FVector::Dist2D(Point,Center))-Radius);
}
bool AEGVillVEmplacement::Muzzle(FVector& Location) const
{return DeviceVisual&&DeviceVisual->GetVisiblePartBoneLocation(DeviceIndex==0?TEXT("Gatling"):TEXT("Cannon"),DeviceIndex==0?TEXT("FirePoint1"):TEXT("CannonFire"),Location);}
bool AEGVillVEmplacement::MuzzleFrame(FTransform& Transform) const
{
 return MuzzleFrame(DeviceIndex==0?FName(TEXT("FirePoint1")):FName(TEXT("CannonFire")),Transform);
}
bool AEGVillVEmplacement::MuzzleFrame(FName Bone,FTransform& Transform) const
{
 if(!IsValid(DeviceVisual)||Bone.IsNone())return false;
 // The source rig may reuse the emplacement's visible body. Its owning Actor
 // is then the device, so it is absent from DeviceVisual's component array.
 const auto* Body=DeviceVisual->BodyMesh.Get();
 if(IsValid(Body)&&Body->IsVisible()&&Body->DoesSocketExist(Bone))
 {Transform=Body->GetSocketTransform(Bone);Transform.SetScale3D(FVector::OneVector);return true;}
 TInlineComponentArray<USkeletalMeshComponent*> Meshes(DeviceVisual);
 for(const auto* PresentedMesh:Meshes)if(IsValid(PresentedMesh)&&PresentedMesh->IsVisible()&&PresentedMesh->DoesSocketExist(Bone))
 {Transform=PresentedMesh->GetSocketTransform(Bone);Transform.SetScale3D(FVector::OneVector);return true;}
 return false;
}
void AEGVillVEmplacement::Tick(float Delta)
{Super::Tick(Delta);if(!IEGCombatant::Execute_IsAlive(this)){ClearDevice();GetMesh()->SetVisibility(false);}}
void AEGVillVEmplacement::EndPlay(const EEndPlayReason::Type Reason){ClearDevice();Pilot.Reset();Super::EndPlay(Reason);}
