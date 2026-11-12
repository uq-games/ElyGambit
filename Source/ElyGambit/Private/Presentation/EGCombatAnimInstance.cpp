#include "Presentation/EGCombatAnimInstance.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"

void UEGCombatAnimInstance::NativeInitializeAnimation()
{
 Super::NativeInitializeAnimation();
 RootMotionMode=ERootMotionMode::IgnoreRootMotion;
#if WITH_EDITOR
 // Persona has no chess owner. Resolve its already-loaded definition so the
 // graph can show its native idle without requiring a running match.
 if(GetWorld()&&GetWorld()->WorldType==EWorldType::EditorPreview)
  for(TObjectIterator<UEGHeroDefinition> It;It;++It)
  {
   if(!IsValid(*It)||It->IsTemplate()||It->HasAnyFlags(RF_NeedLoad))continue;
   if(It->bUseSplitConfiguration&&(!It->PresentationDefinition||!It->StatsDefinition))continue;
   if(It->GetAnimationClass().ToSoftObjectPath()==FSoftObjectPath(GetClass())){Configure(*It);break;}
  }
#endif
}
void UEGCombatAnimInstance::NativeUninitializeAnimation()
{
 StopAction();
 Definition=nullptr;Super::NativeUninitializeAnimation();
}
void UEGCombatAnimInstance::Configure(const UEGHeroDefinition* D)
{
 StopAction();Definition=D;WalkReferenceSpeed=RunReferenceSpeed=0;
 if(D){WalkReferenceSpeed=D->GetWalkReferenceSpeed();RunReferenceSpeed=D->GetRunReferenceSpeed();}
 ResolvedClips=D?D->BuildPresentationClips():FEGPresentationClips();
 IdleClip=ResolvedClips.Idle;WalkClip=ResolvedClips.Walk;RunClip=ResolvedClips.Run;HitClip=ResolvedClips.Hit;DeathClip=ResolvedClips.Death;
 IdleTime=WalkTime=RunTime=HitTime=DeathTime=0;
 ResetSourcePose();SetSpeed(0);
}
void UEGCombatAnimInstance::SetSpeed(float Value)
{
 Speed=FMath::IsFinite(Value)?FMath::Max(0.f,Value):0.f;
 if(bDead||bHit)return;
 bIdle=Speed<1.f;bRun=!bIdle&&Speed>=(Definition?Definition->GetRunAnimationMinSpeed():250.f);bWalk=!bIdle&&!bRun;
 const float Reference=bRun?RunReferenceSpeed:WalkReferenceSpeed;
 LocomotionRate=!bIdle&&Reference>1.f?FMath::Clamp(Speed/Reference,.35f,1.8f):1.f;
}
void UEGCombatAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
 Super::NativeUpdateAnimation(DeltaSeconds);
 AActor* Owner=GetOwningActor();auto* Unit=Cast<AEGChessUnit>(Owner);
 if(!Unit&&Owner)Unit=Cast<AEGChessUnit>(Owner->GetAttachParentActor());
 if(Unit)
 {
  bReviving=Unit->GetHeroSkill()&&Unit->GetHeroSkill()->IsRecoverable();
  FormIndex=Unit->GetDefinition()?static_cast<int32>(Unit->GetDefinition()->HeroKind):0;
  if(const auto* Device=Unit->FindComponentByClass<UEGVillVMechanismComponent>())DevicePhase=static_cast<int32>(Device->Phase);
 }
 if(!bExternalPose)
 {
  const int32 NewState=bDead?3:bHit?4:bRun?2:bWalk?1:0;
  if(NewState!=SourceState){PassiveAge=0;SourceState=NewState;}
  PassiveAge+=FMath::Max(0.f,DeltaSeconds)*(NewState==1||NewState==2?LocomotionRate:1.f);
  PassiveClip=NewState==3?ResolvedClips.Death.Get():NewState==4?ResolvedClips.Hit.Get():NewState==2?ResolvedClips.Run.Get():NewState==1?ResolvedClips.Walk.Get():ResolvedClips.Idle.Get();
  if(!PassiveClip)PassiveClip=ResolvedClips.Idle;
  const float Length=PassiveClip?PassiveClip->GetPlayLength():0.f;
  PassiveTime=Length>SMALL_NUMBER?(NewState==3||NewState==4?FMath::Min(PassiveAge,Length):FMath::Fmod(PassiveAge,Length)):0.f;
  SetStateSample(PassiveClip,PassiveTime,NewState);
  if(NewState==4&&PassiveAge>=Length){bHit=false;PassiveAge=0;SetSpeed(Speed);}
 }
}
void UEGCombatAnimInstance::SetSourcePose(UAnimSequence* Clip,float Time,int32 State)
{
 bExternalPose=true;PassiveClip=Clip;PassiveTime=FMath::Max(0.f,Time);SourceState=State;
 SetStateSample(Clip,PassiveTime,State);
 bDead=State==3;bHit=State==4;bIdle=State==0;bWalk=State==1;bRun=State==2;
}
void UEGCombatAnimInstance::SetStateSample(UAnimSequence* Clip,float Time,int32 State)
{
 switch(State)
 {
 case 1:WalkClip=Clip;WalkTime=Time;break;
 case 2:RunClip=Clip;RunTime=Time;break;
 case 3:DeathClip=Clip;DeathTime=Time;break;
 case 4:HitClip=Clip;HitTime=Time;break;
 default:IdleClip=Clip;IdleTime=Time;break;
 }
}
void UEGCombatAnimInstance::ResetSourcePose()
{
 auto* Unit=Cast<AEGChessUnit>(GetOwningActor());
 if(!Unit&&GetOwningActor())Unit=Cast<AEGChessUnit>(GetOwningActor()->GetAttachParentActor());
 if(bDead&&Unit&&!Unit->IsAlive_Implementation())
 {
  // Stopping combat releases a source assembly, but must not reset a corpse.
  // Transfer its sampled death clock to the normal graph before releasing it.
  if(bExternalPose)
  {
   PassiveAge=PassiveTime;
   if(PassiveClip)ResolvedClips.Death=PassiveClip;
  }
  bExternalPose=false;SourceState=3;bHit=bReviving=false;
  bIdle=bWalk=bRun=false;
  return;
 }
 bExternalPose=false;PassiveAge=PassiveTime=0;SourceState=0;bDead=bHit=bReviving=false;PassiveClip=ResolvedClips.Idle;SetSpeed(0);
 SetStateSample(PassiveClip,0,0);
}
void UEGCombatPartAnimInstance::NativeInitializeAnimation()
{Super::NativeInitializeAnimation();RootMotionMode=ERootMotionMode::IgnoreRootMotion;}
void UEGCombatPartAnimInstance::SetSourcePose(UAnimSequence* Clip,float Time)
{SourceClip=Clip;SourceTime=FMath::Max(0.f,Time);}

bool UEGCombatAnimInstance::PlayAction(EGPresentationAction Action,float Rate)
{
 if(!Definition||!FMath::IsFinite(Rate)||Rate<=0||ActiveAction==EGPresentationAction::Death)return false;
 if(Action==EGPresentationAction::Death){StopAction();bExternalPose=false;bDead=true;bHit=bIdle=bWalk=bRun=false;PassiveAge=0;ActiveAction=Action;return ResolvedClips.Death!=nullptr;}
 if(Action==EGPresentationAction::Hit){if(IsActionActive()||bHit||!ResolvedClips.Hit)return false;bExternalPose=false;bHit=true;bIdle=bWalk=bRun=false;PassiveAge=0;return true;}
 bExternalPose=false;bDead=bHit=false;
 AActor* Owner=GetOwningActor();auto* Unit=Cast<AEGChessUnit>(Owner);
 if(!Unit&&Owner)Unit=Cast<AEGChessUnit>(Owner->GetAttachParentActor());
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;
 UAnimMontage* Montage=Skill&&Skill->GetActiveMontage()&&(Action==EGPresentationAction::Attack||Action==EGPresentationAction::Skill)
  ?Skill->GetActiveMontage():Definition->GetPresentationMontage(Action);
 if(!Montage)return false;
 StopAction();
 ActiveAction=Action;ActiveMontage=Montage;
 if(Montage_Play(Montage,Rate)<=0){StopAction();return false;}
 const auto* Instance=GetActiveInstanceForMontage(Montage);
 ActiveInstanceId=Instance?Instance->GetInstanceID():INDEX_NONE;
 ActionOwner=Unit?Unit->GetHeroSkill():nullptr;
 ActionToken=ActionOwner.IsValid()?ActionOwner->ActionId:FGuid();
 FOnMontageBlendingOutStarted BlendingOut;
 BlendingOut.BindUObject(this,&ThisClass::OnActionBlendingOut,ActiveInstanceId,ActionToken);
 Montage_SetBlendingOutDelegate(BlendingOut,Montage);
 return true;
}
void UEGCombatAnimInstance::StopAction()
{
 const float Blend=Definition&&Definition->HeroKind==EEGHeroKind::Pardofelis&&ActiveMontage?
  FMath::Clamp(.08f/FMath::Max(.1f,Montage_GetPlayRate(ActiveMontage)),.01f,.15f):.1f;
 // Invalidate before stopping: queued/blending-out notifies cannot acquire a new action token.
 ActiveInstanceId=INDEX_NONE;ActionToken.Invalidate();ActionOwner.Reset();
 ActiveAction=EGPresentationAction::None;ActiveMontage=nullptr;ResetSourcePose();
 Montage_Stop(Blend);
}
void UEGCombatAnimInstance::SetActionRate(float Rate)
{if(ActiveMontage&&FMath::IsFinite(Rate)&&Rate>0)Montage_SetPlayRate(ActiveMontage,Rate);}
bool UEGCombatAnimInstance::IsActionActive() const
{return bDead||(ActiveMontage&&Montage_IsPlaying(ActiveMontage));}
bool UEGCombatAnimInstance::AcceptsNotify(const UAnimSequenceBase* Asset,int32 InstanceId,const FGuid& Id)
{
 // UE dispatches queued notifies before blend-out/end delegates, then removes ended instances.
 // A long frame can cross release AND natural blend-out; the active-montage map is already empty.
 // Resolve the exact playback ID instead. Explicit cancellation invalidates our token before Stop.
 const auto* Instance=ActiveMontage?GetMontageInstanceForID(ActiveInstanceId):nullptr;
 return ActionToken.IsValid()&&ActionToken==Id&&ActionOwner.IsValid()&&Instance
  &&ActiveInstanceId==InstanceId&&Instance->GetInstanceID()==InstanceId&&Asset==ActiveMontage
  &&(ActiveAction==EGPresentationAction::Attack||ActiveAction==EGPresentationAction::Skill);
}
void UEGCombatAnimInstance::OnActionBlendingOut(UAnimMontage* Montage,bool bInterrupted,int32 PlaybackId,FGuid Token)
{
 // An old queued delegate must not cancel a newer playback of the same Montage asset.
 if(Montage!=ActiveMontage||PlaybackId!=ActiveInstanceId||Token!=ActionToken)return;
 // Vill-V's Finish notify is intentionally inside the montage's blend-out.
 // Keep this playback token alive so the notify can complete the action at
 // the authored endpoint instead of waiting for the watchdog.
 if(!bInterrupted&&Definition&&Definition->HeroKind==EEGHeroKind::VillV)return;
 if(ActionToken.IsValid()&&ActionOwner.IsValid()&&ActionOwner->ActionId==ActionToken)
 {
  if(bInterrupted)ActionOwner->CancelAction();
  // Complete the cycle at its natural endpoint if a Finish notify was missed.
  // Complete this exact action; queued hit notifies were dispatched first.
  else if(Definition&&Definition->HeroKind==EEGHeroKind::Pardofelis)ActionOwner->AnimationEvent(TEXT("Finish"));
 }
 ActiveInstanceId=INDEX_NONE;ActionToken.Invalidate();ActionOwner.Reset();
 if(ActiveAction!=EGPresentationAction::Death){ActiveMontage=nullptr;ActiveAction=EGPresentationAction::None;}
}
void UEGBowAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
 Super::NativeUpdateAnimation(DeltaSeconds);BowDraw=0;StringOffset=LimbOffset=FVector::ZeroVector;
 auto* Bow=GetSkelMeshComponent();auto* Body=Bow?Cast<USkeletalMeshComponent>(Bow->GetAttachParent()):nullptr;
 auto* BodyAnim=Body?Body->GetAnimInstance():nullptr;
 if(Bow)
 {
  TArray<USceneComponent*> Parts;Bow->GetChildrenComponents(false,Parts);
  for(auto* Part:Parts)if(Part->ComponentHasTag(TEXT("EGNockedArrow")))
   Part->SetVisibility(BodyAnim&&BodyAnim->GetCurveValue(TEXT("ArrowVisible"))>.5f);
 }
 if(!BodyAnim)return;
 BowDraw=FMath::Clamp(BodyAnim->GetCurveValue(TEXT("BowDraw")),0.f,1.f);
 LimbOffset=FVector(-8.f*BowDraw,0,0);
 if(BowDraw<=0)return;
 // This is an acyclic body -> bow read after Body's tick prerequisite, including post-process.
 const FVector Hand=Body->GetSocketLocation(TEXT("StringHandSocket"));
 const FVector LocalHand=Bow->GetComponentTransform().InverseTransformPosition(Hand);
 const FVector Rest=Bow->GetSocketTransform(TEXT("ArrowNockRestSocket"),RTS_Component).GetLocation();
 StringOffset=(LocalHand-Rest)*BowDraw;
}
