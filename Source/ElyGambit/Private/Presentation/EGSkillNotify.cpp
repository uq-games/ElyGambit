#include "Presentation/EGSkillNotify.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Animation/ActiveMontageInstanceScope.h"

bool UEGSkillNotify::DispatchEvent(FName InEvent, USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation)
{
	if (!Mesh || !Mesh->GetWorld() || !Mesh->GetWorld()->IsGameWorld()) return false;
	AActor* Owner = Mesh->GetOwner();
	auto* Unit = Cast<AEGChessUnit>(Owner);
	if (!Unit && Owner) Unit = Cast<AEGChessUnit>(Owner->GetAttachParentActor());
	if (!Unit || !Unit->GetHeroSkill()) return false;
	// Guard against late dispatches from a clip that is no longer the committed action.
 if (Unit->GetHeroSkill()->GetActivePresentationAsset() != Animation) return false;
	Unit->GetHeroSkill()->AnimationEvent(InEvent);
	return true;
}

void UEGSkillNotify::Notify(USkeletalMeshComponent* Mesh, UAnimSequenceBase* Animation,
							const FAnimNotifyEventReference& Reference)
{
	Super::Notify(Mesh, Animation, Reference);
 if(auto* Instance=Mesh?Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance()):nullptr)
 {
  AActor* Owner=Mesh->GetOwner();auto* Unit=Cast<AEGChessUnit>(Owner);
  if(!Unit&&Owner)Unit=Cast<AEGChessUnit>(Owner->GetAttachParentActor());
  const auto* Context=Reference.GetContextData<UE::Anim::FAnimNotifyMontageInstanceContext>();
  if(!Unit||!Context||!Instance->AcceptsNotify(Animation,Context->MontageInstanceID,Unit->GetHeroSkill()->ActionId))return;
 }
	DispatchEvent(Event, Mesh, Animation);
}
