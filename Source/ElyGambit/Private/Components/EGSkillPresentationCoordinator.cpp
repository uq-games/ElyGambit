#include "Components/EGSkillPresentationCoordinator.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"

int32 UEGSkillPresentationCoordinator::SelectAttackPresentationIndex(int32 BasicIndex) const
{
 auto* Owner=Skill.Get();
 const auto* Unit=Owner?Owner->Unit.Get():nullptr;
 const auto* Definition=Unit?Unit->GetDefinition():nullptr;
 if(Definition&&Definition->HeroKind==EEGHeroKind::ElysiaHerrscher&&BasicIndex>=0&&BasicIndex<3)
  return bHumanBowChargeStarted?1:0;
 return BasicIndex;
}

void UEGSkillPresentationCoordinator::FinishActionPresentation(bool bCompleted)
{
 if(!bCompleted)bHumanBowChargeStarted=false;
}

void UEGSkillPresentationCoordinator::StartActionAssembly()
{
 auto* Owner=Skill.Get();if(!Owner)return;
 const auto Unit=Owner->Unit;auto* Behavior=Owner->Behavior.Get();
 auto* ActiveMontage=Owner->ActiveMontage.Get();auto* StageAssembly=Owner->StageAssembly.Get();
 const int32 ActiveBasicAttackIndex=Owner->ActiveVisualAttackIndex,ActiveUltimateIndex=Owner->ActiveUltimateIndex;
 const bool bUltimate=Owner->bUltimate;

 auto* U=Unit.Get();const auto* P=U&&U->GetDefinition()?U->GetDefinition()->PresentationDefinition.Get():nullptr;
 if(bUltimate)bHumanBowChargeStarted=false;
 else if(U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher
  &&Owner->ActiveBasicAttackIndex>=0&&Owner->ActiveBasicAttackIndex<3)bHumanBowChargeStarted=true;
 ClearIdleAssembly();
 if(Behavior&&Behavior->OverridesBodyPresentation()){if(U)U->GetVisiblePresentationMesh()->SetVisibility(false);return;}
 if(!P||!ActiveMontage)return;
 UEGSourcePreviewDefinition* Assembly=bUltimate?P->SkillAssembly.LoadSynchronous():P->AttackAssemblies.IsValidIndex(ActiveBasicAttackIndex)?P->AttackAssemblies[ActiveBasicAttackIndex].LoadSynchronous():nullptr;
 if(bUltimate&&P->UltimateAssemblies.IsValidIndex(ActiveUltimateIndex))Assembly=P->UltimateAssemblies[ActiveUltimateIndex].LoadSynchronous();
 if(StageAssembly)Assembly=StageAssembly;
 if(!Assembly||Assembly->Motions.IsEmpty())return;
 ActionAssembly=Owner->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,U,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!ActionAssembly)return;
 ActionAssembly->Definition=Assembly;ActionAssembly->MotionIndex=0;ActionAssembly->bAutoPlay=false;ActionAssembly->bLoop=false;
 ActionAssembly->FinishSpawning(FTransform::Identity);ActionAssembly->SetActorTickEnabled(false);
 ActionAssembly->AttachToComponent(U->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
 ActionAssembly->SetActorRelativeTransform(FTransform(P->PresentationRotation,FVector(0,0,-88+P->PresentationGroundOffset),P->MeshScale));
 ActionAssembly->SetBoardModelScale(FEGBoardModelSizing::Resolve(U->GetDefinition(),U),P->PresentationGroundOffset);
 AssemblyHiddenMesh=U->GetVisiblePresentationMesh();bAssemblyMeshWasVisible=AssemblyHiddenMesh.IsValid()&&AssemblyHiddenMesh->IsVisible();
 if(AssemblyHiddenMesh.IsValid())ActionAssembly->BindBody(AssemblyHiddenMesh.Get(),false);
 ActionAssembly->SetSourceTime(0);
}
void UEGSkillPresentationCoordinator::ClearActionAssembly()
{
 auto* Owner=Skill.Get();if(!Owner)return;
 const auto Unit=Owner->Unit;auto* Behavior=Owner->Behavior.Get();

 if(ActionAssembly){ActionAssembly->Destroy();ActionAssembly=nullptr;}
 if(AssemblyHiddenMesh.IsValid())AssemblyHiddenMesh->SetVisibility(bAssemblyMeshWasVisible);
 AssemblyHiddenMesh.Reset();
 if(Behavior&&Behavior->OverridesBodyPresentation()&&Unit.IsValid())Unit->GetVisiblePresentationMesh()->SetVisibility(false);
}
void UEGSkillPresentationCoordinator::ClearIdleAssembly()
{
 auto* Owner=Skill.Get();if(!Owner)return;
 const auto Unit=Owner->Unit;

 if(bOwnsPassiveTickPrerequisite&&PassiveTickBody.IsValid())
  PassiveTickBody->RemoveTickPrerequisiteComponent(Owner);
 PassiveTickBody.Reset();bOwnsPassiveTickPrerequisite=false;
 if(IdleAssembly){IdleAssembly->Destroy();IdleAssembly=nullptr;
  if(Unit.IsValid())Unit->GetVisiblePresentationMesh()->SetVisibility(true);}
 IdleAssemblyIndex=INDEX_NONE;
}
void UEGSkillPresentationCoordinator::ResetTickOrdering()
{
 bHumanBowChargeStarted=false;
 ClearBarrierTransition();
 ClearIdleAssembly();
 if(auto* Owner=Skill.Get();Owner&&bOwnsMovementTickPrerequisite&&MovementTickCombat.IsValid())
  Owner->RemoveTickPrerequisiteComponent(MovementTickCombat.Get());
 MovementTickCombat.Reset();bOwnsMovementTickPrerequisite=false;PassiveLocomotion.Reset();
}
void UEGSkillPresentationCoordinator::PresentIdleHit()
{
 auto* Owner=Skill.Get();if(!Owner)return;
 if(BarrierTransitionAssembly)return;
 const auto Unit=Owner->Unit;auto* Behavior=Owner->Behavior.Get();const bool bActing=Owner->bActing;

 auto* U=Unit.Get();const auto* P=U&&U->GetDefinition()?U->GetDefinition()->PresentationDefinition.Get():nullptr;
 if(bHumanBowChargeStarted){PassiveHitRemaining=0;return;}
 if(U&&U->GetDefinition()&&(U->GetDefinition()->HeroKind==EEGHeroKind::Hua||U->GetDefinition()->HeroKind==EEGHeroKind::Sakura))
 {PassiveHitRemaining=0;return;}
 if(!P||bActing||(Behavior&&Behavior->OverridesBodyPresentation())||!IEGCombatant::Execute_IsAlive(U))return;
 if(auto* D=P->HitAssembly.LoadSynchronous();D&&!D->Motions.IsEmpty()){PassiveHitRemaining=D->Motions[0].Duration;IdleAssemblyIndex=INDEX_NONE;}
}
void UEGSkillPresentationCoordinator::RefreshIdleAssembly(float DeltaTime)
{
 auto* Owner=Skill.Get();if(!Owner)return;
 // The encounter transition owns this body until its round/token is released.
 if(BarrierTransitionAssembly)return;
 const auto Unit=Owner->Unit;auto* Behavior=Owner->Behavior.Get();
 const bool bActing=Owner->bActing;const auto Battle=Owner->Battle;

 auto* U=Unit.Get();
 if(!U||!Battle.IsValid()||!Battle->IsBattleActive()||!IEGCombatant::Execute_IsAlive(U))bHumanBowChargeStarted=false;
 const bool bHua=U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::Hua;
 if(bHua&&Battle.IsValid()&&Battle->IsBattleActive()&&!MovementTickCombat.IsValid())
 {
  if(auto* Combat=U->GetCombat())
  {
   MovementTickCombat=Combat;
   const auto HasMovementEdge=[Owner,Combat](){return Owner->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate([Combat](const FTickPrerequisite& P)
    {return P.PrerequisiteObject.Get()==Combat&&P.PrerequisiteTickFunction==&Combat->PrimaryComponentTick;});};
   if(!HasMovementEdge()){Owner->AddTickPrerequisiteComponent(Combat);bOwnsMovementTickPrerequisite=HasMovementEdge();}
  }
 }
 // The seated assembly owns Vill-V's passive pose. A second standing-idle rig
 // must never write the same mesh transform or reset its animation instance.
 if(U)if(const auto* Devices=U->FindComponentByClass<UEGVillVMechanismComponent>();Devices&&Devices->IsOperating())
 {PassiveHitRemaining=0;ClearIdleAssembly();PassiveLocomotion.Reset();return;}
 if(U&&Behavior&&Behavior->OverridesBodyPresentation()){PassiveHitRemaining=0;ClearIdleAssembly();PassiveLocomotion.Reset();U->GetVisiblePresentationMesh()->SetVisibility(false);return;}
 if(!U||U->IsAuxiliaryEmplacement()||bActing||!Battle.IsValid()||!Battle->IsBattleActive())
 {PassiveHitRemaining=0;ClearIdleAssembly();PassiveLocomotion.Reset();return;}
 const auto* P=U->GetDefinition()?U->GetDefinition()->PresentationDefinition.Get():nullptr;
 if(!P){PassiveLocomotion.Reset();return;}
 const float Speed=PassiveLocomotion.Sample(U->GetActorLocation(),DeltaTime,P->RunAnimationMinSpeed);
 const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Behavior);
 if(Human&&bHumanBowChargeStarted&&!Human->IsOriginActive())
 {
  // Hold the charged pose only while this unit can continue firing in place.
  // A lost target can be replaced without restarting the pose if another is
  // already in range; chasing or losing all in-range targets ends the chain.
  const auto* Combat=U->GetCombat();
  auto* Target=Combat?Combat->GetTarget():nullptr;
  if(Combat&&!Combat->IsInAttackRange(Target))Target=Combat->FindNearestEnemy();
  if(Speed>1.f||!Combat||!Combat->IsInAttackRange(Target))bHumanBowChargeStarted=false;
 }
 int32 Index=Behavior?Behavior->IdlePresentationIndex():0;bool Loop=true;float Rate=1;
 UEGSourcePreviewDefinition* D=P->IdleAssemblies.IsValidIndex(Index)?P->IdleAssemblies[Index].LoadSynchronous():nullptr;
 // Hold the end of the charged shot between attacks. The first frame of the
 // repeat assembly is that terminal pose, so no gameplay timer is introduced.
 if(Human&&bHumanBowChargeStarted&&!Human->IsOriginActive()&&P->AttackAssemblies.IsValidIndex(1))
 {
  Index=5;Loop=false;Rate=0.f;D=P->AttackAssemblies[1].LoadSynchronous();
 }
 if(!IEGCombatant::Execute_IsAlive(U)||Owner->IsRecoverable())
 {PassiveLocomotion.Reset();Index=3;Loop=false;UAnimSequence* Clip=nullptr;Owner->ResolveDeathPresentation(Clip,D);}
 else if(PassiveHitRemaining>0){Index=4;Loop=false;D=P->HitAssembly.LoadSynchronous();PassiveHitRemaining=FMath::Max(0.f,PassiveHitRemaining-DeltaTime);}
 else if(Speed>1.f)
 {
  const bool Running=PassiveLocomotion.IsRunning();Index=Running?2:1;
  D=(Running?P->RunAssembly:P->WalkAssembly).LoadSynchronous();
  const float Reference=Running?P->RunReferenceSpeed:P->WalkReferenceSpeed;
  if(Reference>1)Rate=FMath::Clamp(Speed/Reference,.35f,1.8f);
 }
 if(!D||D->Motions.IsEmpty()){ClearIdleAssembly();return;}
 if(!IdleAssembly||IdleAssemblyIndex!=Index||IdleAssembly->Definition!=D)
 {
  ClearIdleAssembly();
  IdleAssembly=Owner->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,U,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!IdleAssembly)return;
  IdleAssembly->Definition=D;IdleAssembly->bAutoPlay=false;
  IdleAssembly->FinishSpawning(FTransform::Identity);IdleAssembly->SetActorTickEnabled(false);
  IdleAssembly->AttachToComponent(U->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
  IdleAssembly->SetActorRelativeTransform(FTransform(P->PresentationRotation,FVector(0,0,-88+P->PresentationGroundOffset),P->MeshScale));
  IdleAssembly->SetBoardModelScale(FEGBoardModelSizing::Resolve(U->GetDefinition(),U),P->PresentationGroundOffset);
  IdleAssemblyIndex=Index;PassiveClock=0;
  auto* Body=U->GetVisiblePresentationMesh();
  if(Body&&bHua&&(Index==1||Index==2))
  {
   PassiveTickBody=Body;
   const bool Existing=Body->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate([Owner](const FTickPrerequisite& P)
    {return P.PrerequisiteObject.Get()==Owner&&P.PrerequisiteTickFunction==&Owner->PrimaryComponentTick;});
   if(!Existing)
   {
    Body->AddTickPrerequisiteComponent(Owner);
    bOwnsPassiveTickPrerequisite=Body->PrimaryComponentTick.GetPrerequisites().ContainsByPredicate([Owner](const FTickPrerequisite& P)
     {return P.PrerequisiteObject.Get()==Owner&&P.PrerequisiteTickFunction==&Owner->PrimaryComponentTick;});
   }
  }
  IdleAssembly->BindBody(Body,true,Index);
 }
 else PassiveClock+=DeltaTime*Rate;
 const float Length=FMath::Max(.01f,D->Motions[0].Duration);
 IdleAssembly->SetSourceTime(Loop?FMath::Fmod(PassiveClock,Length):FMath::Min(PassiveClock,Length));
}

bool UEGSkillPresentationCoordinator::GetAssemblyBoneLocation(FName Part,FName Bone,FVector& Location)
{
 auto* Owner=Skill.Get();if(!Owner)return false;
 const auto Unit=Owner->Unit;auto* ActiveMontage=Owner->ActiveMontage.Get();

 // Notifies may arrive before the component tick; sample the exact action clock first.
 if(ActionAssembly&&ActiveMontage&&Unit.IsValid())if(auto* I=Unit->GetVisiblePresentationMesh()->GetAnimInstance())ActionAssembly->SetSourceTime(I->Montage_GetPosition(ActiveMontage));
 if(Unit.IsValid())if(auto* Devices=Unit->FindComponentByClass<UEGVillVMechanismComponent>())
 {if(ActiveMontage)if(auto* I=Unit->GetVisiblePresentationMesh()->GetAnimInstance())Devices->SampleAction(I->Montage_GetPosition(ActiveMontage));if(Devices->Muzzle(Location))return true;}
 return ActionAssembly&&ActionAssembly->GetVisiblePartBoneLocation(Part,Bone,Location);
}

void UEGSkillPresentationCoordinator::TickAction(float Position,bool bHide)
{
 if(ActionAssembly){ActionAssembly->SetSourceTime(Position);ActionAssembly->SetActorHiddenInGame(bHide);}
}

bool UEGSkillPresentationCoordinator::BeginBarrierTransition(UEGSourcePreviewDefinition* Definition,FGuid Round,FGuid Token,float& OutDuration)
{
 OutDuration=0.f;
 auto* Owner=Skill.Get();auto* U=Owner?Owner->Unit.Get():nullptr;
 auto* Battle=Owner?Owner->Battle.Get():nullptr;
 auto* Body=U?U->GetVisiblePresentationMesh():nullptr;
 const auto* P=U&&U->GetDefinition()?U->GetDefinition()->PresentationDefinition.Get():nullptr;
 if(!U||!U->HasAuthority()||!IEGCombatant::Execute_IsAlive(U)||!Battle||!Battle->IsBattleActive()||
    Battle->GetRoundId()!=Round||!Round.IsValid()||!Token.IsValid()||!Body||!P||Owner->bActing||
    BarrierTransitionAssembly||!Cast<UEGCombatAnimInstance>(Body->GetAnimInstance())||
    !Definition||Definition->Motions.IsEmpty()||!Body->GetSkeletalMeshAsset())return false;
 const auto& Motion=Definition->Motions[0];
 if(!FMath::IsFinite(Motion.Duration)||Motion.Duration<.1f||Motion.Duration>15.f||!Motion.Effects.IsEmpty())return false;
 const FEGSourcePreviewPart* BodyPart=nullptr;const FEGSourcePreviewPart* BowPart=nullptr;
 int32 BodyIndex=INDEX_NONE,BowIndex=INDEX_NONE;
 for(int32 I=0;I<Motion.Parts.Num();++I)
 {
  const auto& Part=Motion.Parts[I];
  if(Part.PartId==TEXT("Body")){if(BodyPart)return false;BodyPart=&Part;BodyIndex=I;}
  if(Part.PartId==TEXT("Bow")){if(BowPart)return false;BowPart=&Part;BowIndex=I;}
 }
 if(!BodyPart||!BodyPart->Mesh||!BodyPart->Animation||!BodyPart->bVisible||BodyPart->bLoop||
    !FMath::IsNearlyZero(BodyPart->StartTime)||BodyPart->Duration+KINDA_SMALL_NUMBER<Motion.Duration||
    BodyPart->Mesh->GetSkeleton()!=Body->GetSkeletalMeshAsset()->GetSkeleton()||
    BodyPart->Animation->GetSkeleton()!=Body->GetSkeletalMeshAsset()->GetSkeleton()||
    BodyPart->Animation->GetPlayLength()+KINDA_SMALL_NUMBER<Motion.Duration||
    !BowPart||BowIndex<=BodyIndex||!BowPart->Mesh||BowPart->Animation||!BowPart->bVisible||
    BowPart->ParentPartId!=TEXT("Body")||BowPart->AttachBone.IsNone()||!Body->DoesSocketExist(BowPart->AttachBone)||
    !FMath::IsNearlyZero(BowPart->StartTime)||BowPart->Duration+KINDA_SMALL_NUMBER<Motion.Duration)return false;

 const auto Lease=FEGPresentationReleaseLease::CreateUObject(this,&ThisClass::OnBarrierPresentationWillRelease);
 FDelegateHandle LeaseHandle;
 if(!U->TryAcquirePresentationReleaseLease(Lease,LeaseHandle))return false;
 BarrierTransitionUnit=U;BarrierTransitionLease=LeaseHandle;
 ClearIdleAssembly();
 // The old action has already been cancelled and its token invalidated.
 // Remove its residual blend before the paused source evaluator takes over.
 if(auto* Instance=Body->GetAnimInstance())Instance->Montage_Stop(0.f);
 if(!IsValid(Owner)||!IsValid(U)||U->IsActorBeingDestroyed()||U->IsPendingKillPending()||
    !IsValid(Body)||U->GetVisiblePresentationMesh()!=Body||!IEGCombatant::Execute_IsAlive(U)||
    !IsValid(Battle)||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round||
    BarrierTransitionUnit.Get()!=U||BarrierTransitionLease!=LeaseHandle)
 {ClearBarrierTransition();return false;}
 auto* Rig=Owner->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,U,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!Rig){ClearBarrierTransition();return false;}
 Rig->Definition=Definition;Rig->MotionIndex=0;Rig->bAutoPlay=false;Rig->bLoop=false;
 Rig->FinishSpawning(FTransform::Identity);
 if(!IsValid(Rig)||!IsValid(U)||U->IsActorBeingDestroyed()||U->IsPendingKillPending()||!IsValid(Body)||
    U->GetVisiblePresentationMesh()!=Body||BarrierTransitionUnit.Get()!=U||BarrierTransitionLease!=LeaseHandle)
 {if(IsValid(Rig))Rig->Destroy();ClearBarrierTransition();return false;}
 Rig->SetActorTickEnabled(false);
 Rig->AttachToComponent(U->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
 Rig->SetActorRelativeTransform(FTransform(P->PresentationRotation,FVector(0,0,-88+P->PresentationGroundOffset),P->MeshScale));
 Rig->SetBoardModelScale(FEGBoardModelSizing::Resolve(U->GetDefinition(),U),P->PresentationGroundOffset);
 BarrierTransitionAssembly=Rig;BarrierTransitionBody=Body;BarrierTransitionRound=Round;BarrierTransitionToken=Token;
 BarrierTransitionLastPosition=0.f;
 Rig->BindBody(Body,true);
 if(!SampleBarrierTransition(Round,Token,0.f)){ClearBarrierTransition();return false;}
 TArray<USkeletalMeshComponent*> Parts;Rig->GetComponents<USkeletalMeshComponent>(Parts);
 const bool bBowBound=Parts.ContainsByPredicate([Body,BowPart](const USkeletalMeshComponent* Part)
  {return IsValid(Part)&&Part->IsVisible()&&Part->GetSkeletalMeshAsset()==BowPart->Mesh&&
   Part->GetAttachParent()==Body&&Part->GetAttachSocketName()==BowPart->AttachBone;});
 if(Rig->BodyMesh!=Body||!bBowBound){ClearBarrierTransition();return false;}
 OutDuration=Motion.Duration;return true;
}

bool UEGSkillPresentationCoordinator::IsBarrierTransitionActive(FGuid Round,FGuid Token) const
{
 const auto* Owner=Skill.Get();const auto* U=Owner?Owner->Unit.Get():nullptr;const auto* Battle=Owner?Owner->Battle.Get():nullptr;
 return IsValid(BarrierTransitionAssembly)&&BarrierTransitionBody.IsValid()&&U&&IEGCombatant::Execute_IsAlive(U)&&
  Battle&&Battle->IsBattleActive()&&Round.IsValid()&&Token.IsValid()&&Battle->GetRoundId()==Round&&
  BarrierTransitionRound==Round&&BarrierTransitionToken==Token;
}

bool UEGSkillPresentationCoordinator::SampleBarrierTransition(FGuid Round,FGuid Token,float Position)
{
 if(!FMath::IsFinite(Position)||!IsBarrierTransitionActive(Round,Token))return false;
 auto* Body=BarrierTransitionBody.Get();auto* Rig=BarrierTransitionAssembly.Get();
 if(!Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()))return false;
 // The sequence evaluator seeks to absolute source time, but the real body
 // graph also contains state transitions. A zero-only update freezes their
 // blend at its old pose indefinitely. Advance those weights using only this
 // encounter-owned clock; a hitch cannot inject an unbounded animation step.
 // The FullBody montage was stopped before this assembly acquired the body.
 Rig->SetSourceTime(Position);
 const float GraphDelta=FMath::Clamp(Position-BarrierTransitionLastPosition,0.f,.1f);
 BarrierTransitionLastPosition=FMath::Max(BarrierTransitionLastPosition,Position);
 const bool bWasPaused=Body->bPauseAnims;Body->bPauseAnims=false;
 const bool bWasSuppressingNotifies=Body->bSuppressNotifyEventDispatch;Body->bSuppressNotifyEventDispatch=true;
 Body->TickAnimation(GraphDelta,false);Body->RefreshBoneTransforms();
 Body->bPauseAnims=bWasPaused;
 Body->bSuppressNotifyEventDispatch=bWasSuppressingNotifies;
 Rig->SetSourceTime(Position);Body->UpdateBounds();Body->MarkRenderTransformDirty();Body->MarkRenderDynamicDataDirty();
 return true;
}

void UEGSkillPresentationCoordinator::EndBarrierTransition(FGuid Round,FGuid Token)
{
 if(BarrierTransitionRound==Round&&BarrierTransitionToken==Token&&Token.IsValid())ClearBarrierTransition();
}

void UEGSkillPresentationCoordinator::ClearBarrierTransition()
{
 // Relinquish identity before destruction, which can invoke UObject callbacks.
 auto* Unit=BarrierTransitionUnit.Get();const FDelegateHandle Lease=BarrierTransitionLease;
 BarrierTransitionUnit.Reset();BarrierTransitionLease.Reset();
 auto* Rig=BarrierTransitionAssembly.Get();BarrierTransitionAssembly=nullptr;BarrierTransitionBody.Reset();
 BarrierTransitionRound.Invalidate();BarrierTransitionToken.Invalidate();
 BarrierTransitionLastPosition=0.f;
 if(IsValid(Unit))Unit->ReleasePresentationLease(Lease);
 if(IsValid(Rig))Rig->Destroy();
}

void UEGSkillPresentationCoordinator::OnBarrierPresentationWillRelease(AEGChessUnit* Unit)
{
 if(Unit&&BarrierTransitionUnit.Get()==Unit)ClearBarrierTransition();
}
