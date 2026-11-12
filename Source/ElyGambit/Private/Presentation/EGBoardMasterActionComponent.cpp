#include "Presentation/EGBoardMasterActionComponent.h"
#include "Actor/EGArtPiece.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGCombatAnimInstance.h"

namespace
{
const TCHAR* EnemyIdle=TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/Settlement20261109/A_Corruption_Idle.A_Corruption_Idle");
const TCHAR* EnemyCast=TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/SourceWall20261108/A_Corruption_SummonWall_BoardCandidate.A_Corruption_SummonWall_BoardCandidate");
const TCHAR* FriendlyHit=TEXT("/Game/Assets/Characters/Elysia/Combat20261101/Animations/Settlement20261109/A_Elysia_Hit.A_Elysia_Hit");
const TCHAR* EnemyHit=TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/Settlement20261109/A_Corruption_Hit.A_Corruption_Hit");
}
UEGBoardMasterActionComponent::UEGBoardMasterActionComponent()
{EnemyIdleSource=FSoftObjectPath(EnemyIdle);EnemyAttackSource=FSoftObjectPath(EnemyCast);FriendlyHitSource=FSoftObjectPath(FriendlyHit);EnemyHitSource=FSoftObjectPath(EnemyHit);}
TArray<FSoftObjectPath> UEGBoardMasterActionComponent::GetPreloadPaths()
{
 const auto* D=GetDefault<UEGBoardMasterActionComponent>();
 return {D->EnemyIdleSource.ToSoftObjectPath(),D->EnemyAttackSource.ToSoftObjectPath(),D->FriendlyHitSource.ToSoftObjectPath(),D->EnemyHitSource.ToSoftObjectPath()};
}

USkeletalMeshComponent* UEGBoardMasterActionComponent::GetBody() const
{const auto* Master=Cast<AEGArtPiece>(GetOwner());return Master?Master->Body.Get():nullptr;}

UEGBoardMasterActionComponent* UEGBoardMasterActionComponent::Install(AEGArtPiece* Master,bool bFriendly)
{
 if(!IsValid(Master))return nullptr;
 if(auto* Existing=Master->FindComponentByClass<UEGBoardMasterActionComponent>())return Existing;
 auto* Component=NewObject<UEGBoardMasterActionComponent>(Master);
 Master->AddInstanceComponent(Component);Component->RegisterComponent();
 if(!Component->Initialize(bFriendly)){Component->DestroyComponent();return nullptr;}
 return Component;
}
bool UEGBoardMasterActionComponent::Initialize(bool bFriendly)
{
 auto* Master=Cast<AEGArtPiece>(GetOwner());
 if(!Master||!Master->HeroVisualDefinition||!Master->Body ||
    (Master->HeroVisualDefinition->bUseSplitConfiguration&&!Master->HeroVisualDefinition->PresentationDefinition))return false;
 // One-time setup before the master is revealed, never on entry to settlement.
 Master->SetBattlePresentation(true);
 PrivateDefinition=DuplicateObject<UEGHeroDefinition>(Master->HeroVisualDefinition,this);
 if(PrivateDefinition->PresentationDefinition)
  PrivateDefinition->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(PrivateDefinition->PresentationDefinition,PrivateDefinition);
 if(!bFriendly)
 {
  auto* Idle=EnemyIdleSource.LoadSynchronous();
  if(!Idle)return false;
  if(PrivateDefinition->bUseSplitConfiguration)PrivateDefinition->PresentationDefinition->IdleAnimation=Idle;
  else PrivateDefinition->IdleAnimation=Idle;
  for(auto* Weapon:Master->GetComponentsByTag(USceneComponent::StaticClass(),TEXT("EGWeapon")))
   if(auto* Visual=Cast<USceneComponent>(Weapon))Visual->SetVisibility(false,true);
 }
 AttackClip=bFriendly?PrivateDefinition->GetAttackAnimation().LoadSynchronous():EnemyAttackSource.LoadSynchronous();
 if(bFriendly&&PrivateDefinition->PresentationDefinition&&!PrivateDefinition->PresentationDefinition->AttackSequence.IsEmpty())
  AttackClip=PrivateDefinition->PresentationDefinition->AttackSequence[0].Animation.LoadSynchronous();
 HitClip=(bFriendly?FriendlyHitSource:EnemyHitSource).LoadSynchronous();
 ReleaseSeconds=bFriendly?.72f:.9f;
 if(!AttackClip||!HitClip||!Master->Body->GetSkeletalMeshAsset()||
  AttackClip->GetSkeleton()!=Master->Body->GetSkeletalMeshAsset()->GetSkeleton()||HitClip->GetSkeleton()!=AttackClip->GetSkeleton())return false;
 return FEGAnimationPlayback::Configure(Master->Body,PrivateDefinition)&&Cast<UEGCombatAnimInstance>(Master->Body->GetAnimInstance());
}
bool UEGBoardMasterActionComponent::Play(AActor* Requester,UAnimSequence* Clip,bool bForAttack,bool bForPresentation)
{
 auto* Body=GetBody();auto* Instance=Body?Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()):nullptr;
 if(!IsValid(Requester)||!Instance||!Clip||(ActiveMontage&&ActionRequester.IsValid()))return false;
 // Private transient montage leaves source clips and gameplay montages untouched.
 auto* Montage=UAnimMontage::CreateSlotAnimationAsDynamicMontage(Clip,TEXT("FullBody"),bForAttack?.18f:.10f,.25f,1.f,1,-1.f);
 if(!Montage)return false;
 if(bForAttack)
 {
  auto* Notify=NewObject<UEGBoardMasterReleaseNotify>(Montage);
  FAnimNotifyEvent Event;Event.Notify=Notify;Event.NotifyName=TEXT("MasterRelease");
  Event.Link(Montage,FMath::Clamp(ReleaseSeconds,.01f,Clip->GetPlayLength()-.01f));
  Event.TriggerWeightThreshold=.01f;Montage->Notifies.Add(Event);
 }
 ActiveMontage=Montage;ActionRequester=Requester;bAttack=bForAttack;bReleased=false;bPresentation=bForPresentation;
 if(Instance->Montage_Play(Montage)<=0){ActiveMontage=nullptr;ActionRequester.Reset();bAttack=false;bPresentation=false;return false;}
 FOnMontageEnded End;End.BindUObject(this,&ThisClass::Finished);Instance->Montage_SetEndDelegate(End,Montage);
 return true;
}
bool UEGBoardMasterActionComponent::Attack(AActor* Requester){return Play(Requester,AttackClip,true);}
bool UEGBoardMasterActionComponent::React(AActor* Requester){return Play(Requester,HitClip,false);}
bool UEGBoardMasterActionComponent::PlayPresentation(AActor* Requester,UAnimSequence* Clip)
{
 auto* Body=GetBody();auto* Mesh=Body?Body->GetSkeletalMeshAsset():nullptr;
 auto* Instance=Body?Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()):nullptr;
 // Never steal another requester's montage or run source gameplay notifies.
 if(!IsValid(Requester)||!IsValid(Clip)||!Mesh||!Instance||!Clip->GetSkeleton()||
    !Clip->GetSkeleton()->IsCompatibleMesh(Mesh)||Clip->GetPlayLength()<=KINDA_SMALL_NUMBER||
    Clip->bEnableRootMotion||!Clip->Notifies.IsEmpty()||IsBusy()||Instance->IsAnyMontagePlaying())return false;
 return Play(Requester,Clip,false,true);
}
void UEGBoardMasterActionComponent::DispatchRelease(const UEGBoardMasterReleaseNotify* Notify)
{
 if(!bAttack||bReleased||!ActiveMontage||!ActionRequester.IsValid()||!Notify||Notify->GetOuter()!=ActiveMontage)return;
 bReleased=true;OnRelease.Broadcast();
}
void UEGBoardMasterActionComponent::Finished(UAnimMontage* Montage,bool bInterrupted)
{
 if(Montage!=ActiveMontage)return;
 const bool bNotify=bPresentation;TWeakObjectPtr<AActor> Requester=ActionRequester;
 ActiveMontage=nullptr;ActionRequester.Reset();bAttack=false;bPresentation=false;
 if(bNotify&&Requester.IsValid())OnPresentationFinished.Broadcast(Requester.Get(),bInterrupted);
}
void UEGBoardMasterActionComponent::Cancel(AActor* Requester)
{
 if(ActionRequester.Get()!=Requester)return;
 auto* Body=GetBody();auto* Instance=Body?Body->GetAnimInstance():nullptr;
 const bool bCancelPresentation=bPresentation;
 auto* Montage=ActiveMontage.Get();ActionRequester.Reset();bAttack=false;bReleased=true;bPresentation=false;
 // Presentation nodes already fade through black; release their slot before
 // the next node starts. Existing attack/react requests retain their blend.
 if(Instance&&Montage)Instance->Montage_Stop(bCancelPresentation?0.f:.2f,Montage);
 // Keep the montage referenced until its blend-out callback completes.
}
void UEGBoardMasterActionComponent::EndPlay(const EEndPlayReason::Type Reason)
{OnRelease.Clear();OnPresentationFinished.Clear();Cancel(ActionRequester.Get());Super::EndPlay(Reason);}
void UEGBoardMasterReleaseNotify::Notify(USkeletalMeshComponent* Mesh,UAnimSequenceBase*,const FAnimNotifyEventReference&)
{
 if(Mesh&&Mesh->GetOwner())if(auto* Action=Mesh->GetOwner()->FindComponentByClass<UEGBoardMasterActionComponent>())Action->DispatchRelease(this);
}
