#pragma once
#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Presentation/EGChessAnimInstance.h"
#include "EGCombatAnimInstance.generated.h"
class UEGHeroDefinition;
class UAnimMontage;
class UEGHeroSkillComponent;

/** Normal AnimGraph evaluation, standard Montage/Notify pipeline. No custom evaluation proxy. */
UCLASS(Blueprintable,BlueprintType)
class ELYGAMBIT_API UEGCombatAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 virtual void NativeInitializeAnimation() override;
 virtual void NativeUpdateAnimation(float DeltaSeconds) override;
 virtual void NativeUninitializeAnimation() override;
 void Configure(const UEGHeroDefinition* InDefinition);
 void SetDeathClip(UAnimSequence* Clip) {ResolvedClips.Death=Clip;DeathClip=Clip;}
 UFUNCTION(BlueprintCallable,Category="Presentation") void SetSpeed(float Value);
 UFUNCTION(BlueprintCallable,Category="Presentation") bool PlayAction(EGPresentationAction Action,float Rate=1.f);
 UFUNCTION(BlueprintCallable,Category="Presentation") void StopAction();
 UFUNCTION(BlueprintCallable,Category="Presentation") void SetActionRate(float Rate);
 UFUNCTION(BlueprintPure,Category="Presentation") bool IsActionActive() const;
 bool AcceptsNotify(const UAnimSequenceBase* Asset,int32 InstanceId,const FGuid& Id);
 EGPresentationAction GetActiveAction() const {return ActiveAction;}
 UAnimMontage* GetActionMontage() const {return ActiveMontage;}
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") float Speed=0;
 /** AnimGraph gait rate only. Never changes the montage or gameplay clock. */
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") float LocomotionRate=1.f;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") bool bIdle=true;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") bool bWalk=false;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") bool bRun=false;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") bool bDead=false;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") bool bHit=false;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") bool bReviving=false;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") int32 FormIndex=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") int32 DevicePhase=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") TObjectPtr<UAnimSequence> PassiveClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") float PassiveTime=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") TObjectPtr<UAnimSequence> IdleClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") TObjectPtr<UAnimSequence> WalkClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") TObjectPtr<UAnimSequence> RunClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") TObjectPtr<UAnimSequence> HitClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") TObjectPtr<UAnimSequence> DeathClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") float IdleTime=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") float WalkTime=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") float RunTime=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") float HitTime=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation|States") float DeathTime=0;
 /** A seated/device pose uses an absolute source clock without replacing the action montage. */
 UFUNCTION(BlueprintCallable,Category="Presentation") void SetSourcePose(UAnimSequence* Clip,float Time,int32 State);
 void ResetSourcePose();
private:
 UPROPERTY(Transient) TObjectPtr<const UEGHeroDefinition> Definition;
 UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveMontage;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> ActionOwner;
 EGPresentationAction ActiveAction=EGPresentationAction::None;
 int32 ActiveInstanceId=INDEX_NONE;
 FGuid ActionToken;
 float WalkReferenceSpeed=0.f,RunReferenceSpeed=0.f;
 FEGPresentationClips ResolvedClips;
 float PassiveAge=0;
 int32 SourceState=0;
 bool bExternalPose=false;
 void SetStateSample(UAnimSequence* Clip,float Time,int32 State);
 void OnActionBlendingOut(UAnimMontage* Montage,bool bInterrupted,int32 PlaybackId,FGuid Token);
};

/** Cosmetic follower only. Sequence Evaluator teleports, so no notifies or root motion. */
UCLASS(Blueprintable,BlueprintType)
class ELYGAMBIT_API UEGCombatPartAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") TObjectPtr<UAnimSequence> SourceClip;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Presentation") float SourceTime=0;
 virtual void NativeInitializeAnimation() override;
 void SetSourcePose(UAnimSequence* Clip,float Time);
};

/** Bow reads the already evaluated body pose/curve; it never drives the body back. */
UCLASS(Blueprintable,BlueprintType)
class ELYGAMBIT_API UEGBowAnimInstance : public UAnimInstance
{
 GENERATED_BODY()
public:
 virtual void NativeUpdateAnimation(float DeltaSeconds) override;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Bow") float BowDraw=0;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Bow") FVector StringOffset=FVector::ZeroVector;
 UPROPERTY(Transient,BlueprintReadOnly,Category="Bow") FVector LimbOffset=FVector::ZeroVector;
};
