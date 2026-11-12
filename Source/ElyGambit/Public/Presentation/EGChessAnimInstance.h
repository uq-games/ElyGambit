#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "EGChessAnimInstance.generated.h"

class UAnimSequence;

/** Presentation clips shared by every animated unit. Filled from the hero data asset. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGPresentationClips
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Idle = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Walk = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Run = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Attack = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Skill = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Hit = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") TObjectPtr<UAnimSequence> Death = nullptr;
	/** Board speed in cm/s that matches the walk clip' natural stride. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") float WalkReferenceSpeed = 160.f;
	/** Board speed in cm/s that matches the run clip' natural stride. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") float RunReferenceSpeed = 380.f;
	/** Speed above which the run clip starts blending in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Presentation") float RunMinimumSpeed = 250.f;

	bool HasLocomotion() const { return Idle || Walk || Run; }
};

UENUM(BlueprintType)
enum class EGPresentationAction : uint8
{
	None,
	Attack,
	Skill,
	Hit,
	Death
};

/**
 * Crossfading presentation animation instance.
 *
 * Clip playback and layer weights are decided on the game thread in NativeUpdateAnimation and handed
 * to the proxy, which owns no state of its own. The proxy evaluates the selected sequences directly
 * through UAnimSequence::GetAnimationPose and blends them, so a unit needs no authored anim graph and
 * every transition (idle <-> walk <-> run, locomotion <-> action, action -> death) is continuous.
 */
struct ELYGAMBIT_API FEGChessAnimInstanceProxy : public FAnimInstanceProxy
{
	FEGChessAnimInstanceProxy() {}
	FEGChessAnimInstanceProxy(UAnimInstance* InInstance) : FAnimInstanceProxy(InInstance) {}

	// Game thread state mirrored by FEGChessAnimInstance.
	TObjectPtr<UAnimSequence> IdleClip = nullptr;
	TObjectPtr<UAnimSequence> WalkClip = nullptr;
	TObjectPtr<UAnimSequence> RunClip = nullptr;
	TObjectPtr<UAnimSequence> ActionClip = nullptr;
	float IdleTime = 0.f;
	float WalkTime = 0.f;
	float RunTime = 0.f;
	float ActionTime = 0.f;
	float IdleWeight = 1.f;
	float WalkWeight = 0.f;
	float RunWeight = 0.f;
	float ActionWeight = 0.f;
	bool bActionLooping = false;

	virtual void AddReferencedObjects(UAnimInstance* InAnimInstance, FReferenceCollector& Collector) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	void EvaluateLayer(UAnimSequence* Sequence, float Time, bool bLooping, FPoseContext& OutLayer) const;
	float LayerLength(const UAnimSequence* Sequence) const;
};

UCLASS(Blueprintable, BlueprintType)
class ELYGAMBIT_API UEGChessAnimInstance : public UAnimInstance
{
	GENERATED_BODY()
public:
	UEGChessAnimInstance();
	void SetDeathClip(UAnimSequence* Clip) {Clips.Death=Clip;}

	virtual void NativeInitializeAnimation() override;
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	/** Copy clip references and tuning from the owning unit' hero definition. */
	UFUNCTION(BlueprintCallable, Category="Presentation") void ApplyClips(const FEGPresentationClips& InClips);
	UFUNCTION(BlueprintCallable, Category="Presentation") void SetLocomotionSpeed(float InSpeed);
	UFUNCTION(BlueprintCallable, Category="Presentation") void PlayAction(EGPresentationAction InAction, float PlayRate = 1.f);
	UFUNCTION(BlueprintCallable, Category="Presentation") void StopAction();
	UFUNCTION(BlueprintCallable, Category="Presentation") void SetActionRate(float Rate);
	UFUNCTION(BlueprintPure, Category="Presentation") bool IsActionActive() const;
	UFUNCTION(BlueprintPure, Category="Presentation") EGPresentationAction GetActiveAction() const { return ActiveAction; }
	/** 0..1 progress of the running action, 1 when it finished or nothing runs. */
	UFUNCTION(BlueprintPure, Category="Presentation") float GetActionProgress() const;
	UFUNCTION(BlueprintPure, Category="Presentation") float GetLocomotionSpeed() const { return LocomotionSpeed; }
	/** Highest weighted locomotion clip; tests and HUD use it to read the intended gait. */
	UFUNCTION(BlueprintPure, Category="Presentation") UAnimSequence* GetDominantLocomotionClip() const;
	/** Advances clip clocks and blend weights; NativeUpdateAnimation calls this every anim update. */
	UFUNCTION(BlueprintCallable, Category="Presentation") void TickPresentation(float DeltaSeconds);
	UFUNCTION(BlueprintPure, Category="Presentation") UAnimSequence* GetActiveActionClip() const;

protected:
	UPROPERTY(Transient, BlueprintReadOnly, Category="Presentation") FEGPresentationClips Clips;
	UPROPERTY(Transient) EGPresentationAction ActiveAction = EGPresentationAction::None;
	UPROPERTY(Transient) float LocomotionSpeed = 0.f;
	UPROPERTY(Transient) bool bPoseFrozen = false;

	void RefreshProxy();

	// Game thread clock, advanced in NativeUpdateAnimation.
	float ClockIdle = 0.f;
	float ClockWalk = 0.f;
	float ClockRun = 0.f;
	float ClockAction = 0.f;
	float ActionBlend = 0.f;
	float ActionRate = 1.f;
	float IdleBlend = 1.f;
	float WalkBlend = 0.f;
	float RunBlend = 0.f;
	float BlendSeconds = 0.14f;
	bool bInitializedClips = false;
	/** Action clock of the previous update, used to fire the clip' gameplay notifies exactly once. */
	float PreviousClockAction = 0.f;
};
