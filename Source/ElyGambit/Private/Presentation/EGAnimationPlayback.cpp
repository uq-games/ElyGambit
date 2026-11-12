#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Data/EGHeroDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"

bool FEGAnimationPlayback::Configure(USkeletalMeshComponent* M,const UEGHeroDefinition* D)
{
 if(!M||!D||!M->GetSkeletalMeshAsset())return false;
 UClass* Class=D->GetAnimationClass().IsNull()?UEGChessAnimInstance::StaticClass():D->GetAnimationClass().LoadSynchronous();
 if(!Class)return false;
 if(M->GetAnimationMode()!=EAnimationMode::AnimationBlueprint||!M->GetAnimInstance()||M->GetAnimInstance()->GetClass()!=Class)
 {M->SetAnimationMode(EAnimationMode::AnimationBlueprint);M->SetAnimInstanceClass(Class);M->InitAnim(true);}
 if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance())){I->Configure(D);return true;}
 if(auto* I=Cast<UEGChessAnimInstance>(M->GetAnimInstance())){I->ApplyClips(D->BuildPresentationClips());I->SetLocomotionSpeed(0);return true;}
 return false;
}
bool FEGAnimationPlayback::Play(USkeletalMeshComponent* M,EGPresentationAction A,float R)
{
 if(!M)return false;
 if(A==EGPresentationAction::Death)
 {
  auto* Unit=Cast<AEGChessUnit>(M->GetOwner());
  if(!Unit&&M->GetOwner())Unit=Cast<AEGChessUnit>(M->GetOwner()->GetAttachParentActor());
  if(Unit&&Unit->GetHeroSkill()&&Unit->GetHeroSkill()->HasDeathPresentationSnapshot())
  {
   UAnimSequence* Clip=nullptr;UEGSourcePreviewDefinition* Assembly=nullptr;
   Unit->GetHeroSkill()->ResolveDeathPresentation(Clip,Assembly);
   if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance()))I->SetDeathClip(Clip);
   else if(auto* Legacy=Cast<UEGChessAnimInstance>(M->GetAnimInstance()))Legacy->SetDeathClip(Clip);
  }
 }
 if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance()))return I->PlayAction(A,R);
 if(auto* I=Cast<UEGChessAnimInstance>(M->GetAnimInstance())){I->PlayAction(A,R);return I->GetActiveAction()==A&&I->GetActiveActionClip()!=nullptr;}
 return false;
}
void FEGAnimationPlayback::Stop(USkeletalMeshComponent* M)
{
 if(!M)return;
 if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance()))I->StopAction();
 else if(auto* Legacy=Cast<UEGChessAnimInstance>(M->GetAnimInstance()))Legacy->StopAction();
}
void FEGAnimationPlayback::Speed(USkeletalMeshComponent* M,float V)
{
 if(!M)return;
 if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance()))I->SetSpeed(V);
 else if(auto* Legacy=Cast<UEGChessAnimInstance>(M->GetAnimInstance()))Legacy->SetLocomotionSpeed(V);
}
void FEGAnimationPlayback::Rate(USkeletalMeshComponent* M,float V)
{
 if(!M||!FMath::IsFinite(V)||V<=0)return;
 if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance()))I->SetActionRate(V);
 else if(auto* Legacy=Cast<UEGChessAnimInstance>(M->GetAnimInstance()))Legacy->SetActionRate(V);
 else if(M->GetAnimationMode()==EAnimationMode::AnimationSingleNode)M->SetPlayRate(V);
}
bool FEGAnimationPlayback::IsActive(USkeletalMeshComponent* M)
{
 if(!M)return false;
 if(auto* I=Cast<UEGCombatAnimInstance>(M->GetAnimInstance()))return I->IsActionActive();
 if(auto* I=Cast<UEGChessAnimInstance>(M->GetAnimInstance()))return I->IsActionActive();
 return false;
}
bool FEGAnimationPlayback::IsSupported(USkeletalMeshComponent* M)
{return M&&(Cast<UEGCombatAnimInstance>(M->GetAnimInstance())||Cast<UEGChessAnimInstance>(M->GetAnimInstance()));}
