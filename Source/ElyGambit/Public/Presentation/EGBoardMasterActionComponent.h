#pragma once
#include "Components/ActorComponent.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "EGBoardMasterActionComponent.generated.h"
class AEGArtPiece;
class UAnimMontage;
class UAnimSequence;
class UEGHeroDefinition;

DECLARE_MULTICAST_DELEGATE_TwoParams(FEGMasterPresentationFinished, AActor* /*Requester*/, bool /*bInterrupted*/);

/** Animation-local cue. No damage, game-mode access or mutable shared notify state. */
UCLASS()
class ELYGAMBIT_API UEGBoardMasterReleaseNotify : public UAnimNotify
{
 GENERATED_BODY()
public:
 virtual void Notify(USkeletalMeshComponent* Mesh,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& Reference) override;
};

/** Lives on the persistent master actor; its AnimGraph owns all pose blending. */
UCLASS(ClassGroup=(Presentation))
class ELYGAMBIT_API UEGBoardMasterActionComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 static UEGBoardMasterActionComponent* Install(AEGArtPiece* Master,bool bFriendly);
 UEGBoardMasterActionComponent();
 static TArray<FSoftObjectPath> GetPreloadPaths();
 bool Attack(AActor* Requester);
 bool React(AActor* Requester);
 /** Plays only a compatible, in-place, notify-free presentation clip. */
 bool PlayPresentation(AActor* Requester,UAnimSequence* Clip);
 void Cancel(AActor* Requester);
 void DispatchRelease(const UEGBoardMasterReleaseNotify* Notify);
 bool IsAttacking() const {return bAttack;}
 bool IsBusy() const {return ActiveMontage && ActionRequester.IsValid();}
 UAnimMontage* GetPlayingMontage() const {return ActiveMontage;}
 USkeletalMeshComponent* GetBody() const;
 FSimpleMulticastDelegate OnRelease;
 FEGMasterPresentationFinished OnPresentationFinished;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(EditDefaultsOnly,Category="Master|Animation") TSoftObjectPtr<UAnimSequence> EnemyIdleSource;
 UPROPERTY(EditDefaultsOnly,Category="Master|Animation") TSoftObjectPtr<UAnimSequence> EnemyAttackSource;
 UPROPERTY(EditDefaultsOnly,Category="Master|Animation") TSoftObjectPtr<UAnimSequence> FriendlyHitSource;
 UPROPERTY(EditDefaultsOnly,Category="Master|Animation") TSoftObjectPtr<UAnimSequence> EnemyHitSource;
 bool Initialize(bool bFriendly);
 bool Play(AActor* Requester,UAnimSequence* Clip,bool bForAttack,bool bForPresentation=false);
 void Finished(UAnimMontage* Montage,bool bInterrupted);
 UPROPERTY(Transient) TObjectPtr<UEGHeroDefinition> PrivateDefinition;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackClip;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> HitClip;
 UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveMontage;
 UPROPERTY(Transient) TWeakObjectPtr<AActor> ActionRequester;
 float ReleaseSeconds=.72f;
 bool bAttack=false,bReleased=false,bPresentation=false;
};
