#include "Components/EGUltimateRingComponent.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGSkillNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"

namespace
{
/** Inverse of UAnimSegment::ConvertTrackPosToAnimPos: montage time at a source clip time. */
float MontagePositionForSourceTime(const UAnimMontage* Montage,float SourceTime)
{
 if(!Montage||Montage->SlotAnimTracks.IsEmpty())return -1.f;
 for(const auto& Segment:Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
 {
  const float Span=Segment.AnimEndTime-Segment.AnimStartTime;
  if(Span<=KINDA_SMALL_NUMBER)continue;
  if(SourceTime>=Segment.AnimStartTime-KINDA_SMALL_NUMBER&&SourceTime<=Segment.AnimEndTime+KINDA_SMALL_NUMBER)
   return Segment.StartPos+(SourceTime-Segment.AnimStartTime)/Span*Segment.GetLength();
 }
 return -1.f;
}
float SmoothStep01(float T)
{
 T=FMath::Clamp(T,0.f,1.f);return T*T*(3.f-2.f*T);
}
}

UEGUltimateRingComponent::UEGUltimateRingComponent()
{
 PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;
 SetCollisionEnabled(ECollisionEnabled::NoCollision);SetGenerateOverlapEvents(false);SetCastShadow(false);
 SetVisibility(false);SetAbsolute(true,true,true);
}
void UEGUltimateRingComponent::RestoreWeapon()
{
 if(HiddenOnMesh.IsValid())for(FName Bone:HiddenByUs)HiddenOnMesh->UnHideBoneByName(Bone);
 HiddenByUs.Reset();HiddenOnMesh.Reset();
}
FVector UEGUltimateRingComponent::ResolveOverheadCenter(FVector ImpactRoot,float TargetHeight) const
{
 const auto* U=Cast<AEGChessUnit>(GetOwner());
 const float Height=U&&U->GetDefinition()?FMath::Max(160.f,U->GetDefinition()->GetPresentationHeight()):160.f;
 return ImpactRoot-FVector(0,0,88)+FVector(0,0,FMath::Max(0.f,TargetHeight)+Height*TargetClearanceInBodyHeights);
}
void UEGUltimateRingComponent::BeginDescent(AEGSkillProjectile* Flight)
{
 if(!bTargetOverhead||!IsValid(Flight))return;
 if(ActiveDescent.IsValid())RemoveTickPrerequisiteActor(ActiveDescent.Get());
 ActiveDescent=Flight;AddTickPrerequisiteActor(Flight);
 const auto* Target=Flight->Target.Get();
 OverheadCenter=ResolveOverheadCenter(Flight->Destination,Target&&Target->GetDefinition()?Target->GetDefinition()->GetPresentationHeight():0.f);
 Refresh();
}
void UEGUltimateRingComponent::Refresh()
{
 auto* U=Cast<AEGChessUnit>(GetOwner());
 auto* Flight=ActiveDescent.Get();
 const bool Flying=Flight&&!Flight->IsActorBeingDestroyed()&&Flight->Battle.IsValid()&&Flight->Battle->IsBattleActive()&&Flight->Battle->GetRoundId()==Flight->Round;
 const bool Casting=U&&U->GetHeroSkill()->IsCasting();
 const bool Active=U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::Pardofelis
  &&IEGCombatant::Execute_IsAlive(U)&&(Casting||(bTargetOverhead&&Flying))&&GetStaticMesh()
  &&GetWorld()&&GetWorld()->GetNetMode()!=NM_DedicatedServer;
 SetComponentTickEnabled(Active);
 if(!Active){SetVisibility(false);RestoreWeapon();if(Flight)RemoveTickPrerequisiteActor(Flight);ActiveDescent.Reset();return;}
 auto* Mesh=U->GetVisiblePresentationMesh();auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
 auto* Montage=U->GetHeroSkill()->GetActiveMontage();
 if(!Mesh||(!Flying&&(!Instance||!Montage))||SourceEndTime<=SourceStartTime){SetVisibility(false);RestoreWeapon();return;}
 AddTickPrerequisiteComponent(Mesh);
 const float Position=Montage&&Instance?Instance->Montage_GetPosition(Montage):0.f;
 if(Casting&&!Flying)
 {
  OverheadCenter=U->GetHeroSkill()->GetPresentationTargetGround(1.f)+FVector(0,0,FMath::Max(160.f,U->GetDefinition()->GetPresentationHeight())*TargetClearanceInBodyHeights);
 }
 float SourceTime=0.f;
 if(Montage&&!Montage->SlotAnimTracks.IsEmpty())for(const auto& Segment:Montage->SlotAnimTracks[0].AnimTrack.AnimSegments)
  if(Position>=Segment.StartPos&&Position<=Segment.StartPos+Segment.GetLength()+KINDA_SMALL_NUMBER)
  {SourceTime=Segment.ConvertTrackPosToAnimPos(Position);break;}
 // The source trajectory only covers the throw window; the montage owns the wind-up.
 const float ThrowPosition=MontagePositionForSourceTime(Montage,SourceStartTime);
 const bool bAnticipate=bAnticipateBeforeThrow&&ThrowPosition>KINDA_SMALL_NUMBER;
 const float ShowPosition=bAnticipate?FMath::Max(0.f,ThrowPosition-AnticipationLeadSeconds):ThrowPosition;
 if(!Flying&&Position+KINDA_SMALL_NUMBER<ShowPosition){SetVisibility(false);RestoreWeapon();return;}
 const bool bThrown=SourceTime+KINDA_SMALL_NUMBER>=SourceStartTime;
 // After the throw the ring is gone: shrink it away instead of hovering statically.
 float ReleaseScale=1.f;
 if(!bTargetOverhead&&bFadeAfterSourceEnd&&SourceTime>SourceEndTime)
 {
  ReleaseScale=1.f-FMath::Clamp((SourceTime-SourceEndTime)/FMath::Max(.02f,ReleaseFadeSeconds),0.f,1.f);
  if(ReleaseScale<=KINDA_SMALL_NUMBER){SetVisibility(false);return;}
 }
 SetVisibility(true);
 if(!Casting)RestoreWeapon();
 if(Casting&&!HiddenOnMesh.IsValid())
 {
  HiddenOnMesh=U->GetVisiblePresentationMesh();
  if(HiddenOnMesh.IsValid())for(const FName Bone:{FName(TEXT("BladeRing_P1_S_Root")),FName(TEXT("BladeRing_S"))})
   if(HiddenOnMesh->GetBoneIndex(Bone)!=INDEX_NONE&&!HiddenOnMesh->IsBoneHiddenByName(Bone))
   {HiddenOnMesh->HideBoneByName(Bone,EPhysBodyOp::PBO_None);HiddenByUs.Add(Bone);}
 }
 const auto SourceBounds=GetStaticMesh()->GetBounds();const FVector Extent=SourceBounds.BoxExtent;
 const float Height=FMath::Max(160.f,U->GetDefinition()->GetPresentationHeight());
 const float BaseScale=Height*DiameterInBodyHeights/FMath::Max(1.f,Extent.GetMax()*2.f);
 // Airborne ring is almost horizontal, clearing the head instead of encircling the torso.
 const FVector SourceNormal=Extent.X<=Extent.Y&&Extent.X<=Extent.Z?FVector::ForwardVector:
  Extent.Y<=Extent.Z?FVector::RightVector:FVector::UpVector;
 const FVector Normal=(U->GetActorForwardVector()*.12f+FVector::UpVector).GetSafeNormal();
 float ScaleFactor=ReleaseScale;
 float AngleDegrees=0.f;
 FVector NormalizedCenter(0,.35f,FMath::Max(1.85f,CenterHeightInBodyHeights));
 if(!bThrown)
 {
  // Conjure: ring materialises below its launch point, scales up and spins up
  // while the source clip still holds the ordinary ring in hand.
  const float AntT=SmoothStep01((Position-ShowPosition)/FMath::Max(.02f,ThrowPosition-ShowPosition));
  ScaleFactor*=FMath::Lerp(FMath::Clamp(AnticipationScale,.1f,1.f),1.f,AntT);
  AngleDegrees=AnticipationSpinDegPerSec*FMath::Max(0.f,Position-ShowPosition);
  const float TargetZ=AirborneCenters.IsEmpty()?NormalizedCenter.Z:AirborneCenters[0].Z;
  NormalizedCenter=FVector(0.f,.35f,FMath::Lerp(AnticipationHeightInBodyHeights,TargetZ,AntT));
 }
 else
 {
  AngleDegrees=(bAnticipate?AnticipationSpinDegPerSec*FMath::Max(0.f,ThrowPosition-ShowPosition):0.f)
   +FlightSpinDegPerSec*FMath::Max(0.f,Position-ThrowPosition);
  if(!AirborneCenters.IsEmpty())
  {
   const float Frame=FMath::Clamp((SourceTime-SourceStartTime)/(SourceEndTime-SourceStartTime),0.f,1.f)*(AirborneCenters.Num()-1);
   const int32 Low=FMath::FloorToInt(Frame),High=FMath::Min(Low+1,AirborneCenters.Num()-1);
   NormalizedCenter=FMath::Lerp(AirborneCenters[Low],AirborneCenters[High],Frame-Low);
  }
  const float FlightT=FMath::Clamp((Position-ThrowPosition)/FMath::Max(.02f,(Montage?Montage->GetPlayLength():1.f)-ThrowPosition),0.f,1.f);
  ScaleFactor*=1.f+.05f*FMath::Sin(FlightT*PI);
 }
 const FQuat Rotation=FQuat(Normal,FMath::DegreesToRadians(AngleDegrees))*FQuat::FindBetweenNormals(SourceNormal,Normal);
 FVector Center=U->GetActorLocation()-FVector(0,0,88)+Mesh->GetComponentQuat().RotateVector(NormalizedCenter*Height);
 if(bTargetOverhead)
 {
  float Arrival=ThrowPosition+.3f;
  if(Montage)for(const auto& N:Montage->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify);Notify&&Notify->Event==TEXT("Release")){Arrival=N.GetTime();break;}
  if(Flying)
  {
   Center=OverheadCenter;
   AngleDegrees=FlightSpinDegPerSec*Flight->Elapsed;
   // Keep the portal while Can emerges, then collapse it after the body passes through.
   ScaleFactor=1.f-SmoothStep01((Flight->Elapsed/FMath::Max(.01f,Flight->Duration)-.15f)/.5f);
  }
  else if(bThrown)
  {
   const FVector First=AirborneCenters.IsEmpty()?FVector(0,.35f,1.85f):AirborneCenters[0];
   const FVector Launch=U->GetActorLocation()-FVector(0,0,88)+Mesh->GetComponentQuat().RotateVector(First*Height);
   const float T=FMath::Clamp((Position-ThrowPosition)/FMath::Max(.02f,Arrival-ThrowPosition),0.f,1.f);
   Center=FMath::Lerp(Launch,OverheadCenter,SmoothStep01(T))+FVector(0,0,TravelArcInBodyHeights*Height*FMath::Sin(PI*T));
  }
  if(ScaleFactor<=KINDA_SMALL_NUMBER){SetVisibility(false);return;}
 }
 const FQuat FinalRotation=bTargetOverhead?FQuat(Normal,FMath::DegreesToRadians(AngleDegrees))*FQuat::FindBetweenNormals(SourceNormal,Normal):Rotation;
 SetWorldTransform(FTransform(FinalRotation,Center-FinalRotation.RotateVector(SourceBounds.Origin*BaseScale*ScaleFactor),FVector(BaseScale*ScaleFactor)));
}
void UEGUltimateRingComponent::TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)
{Super::TickComponent(Delta,Tick,Function);Refresh();}
void UEGUltimateRingComponent::EndPlay(const EEndPlayReason::Type Reason)
{RestoreWeapon();ActiveDescent.Reset();Super::EndPlay(Reason);}
