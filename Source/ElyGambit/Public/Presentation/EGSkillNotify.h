#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "EGSkillNotify.generated.h"
class USkeletalMeshComponent;
class UAnimSequenceBase;
UCLASS()
class ELYGAMBIT_API UEGSkillNotify : public UAnimNotify
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Skill") FName Event=TEXT("Hit");
 virtual void Notify(USkeletalMeshComponent* Mesh,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& Reference) override;
 /** Shared dispatch so custom anim instances (which evaluate clips without the notify pipeline) commit the same events. */
 static bool DispatchEvent(FName InEvent,USkeletalMeshComponent* Mesh,UAnimSequenceBase* Animation);
};
