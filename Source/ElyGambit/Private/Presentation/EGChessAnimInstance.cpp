#include "Presentation/EGChessAnimInstance.h"

#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Character/EGChessUnit.h"
#include "GameFramework/Pawn.h"
#include "Presentation/EGSkillNotify.h"

namespace
{
	float SafeLength(const UAnimSequence* Sequence)
	{
		return Sequence ? FMath::Max(Sequence->GetPlayLength(), 0.001f) : 0.f;
	}

	void WrapTime(float& Time, float InLength)
	{
		if (InLength > KINDA_SMALL_NUMBER)
		{
			Time = FMath::Fmod(Time, InLength);
			if (Time < 0.f) Time += InLength;
		}
	}
}

void FEGChessAnimInstanceProxy::AddReferencedObjects(UAnimInstance* InAnimInstance, FReferenceCollector& Collector)
{
	FAnimInstanceProxy::AddReferencedObjects(InAnimInstance, Collector);
	Collector.AddReferencedObject(IdleClip, InAnimInstance);
	Collector.AddReferencedObject(WalkClip, InAnimInstance);
	Collector.AddReferencedObject(RunClip, InAnimInstance);
	Collector.AddReferencedObject(ActionClip, InAnimInstance);
}

float FEGChessAnimInstanceProxy::LayerLength(const UAnimSequence* Sequence) const
{
	return SafeLength(Sequence);
}

void FEGChessAnimInstanceProxy::EvaluateLayer(UAnimSequence* Sequence, float Time, bool bLooping,
	FPoseContext& OutLayer) const
{
	OutLayer.Pose.ResetToRefPose();
	OutLayer.Curve.InitFrom(GetRequiredBones());
	if (!Sequence) return;
	const float Length = SafeLength(Sequence);
	const double SampleTime = bLooping ? FMath::Fmod(static_cast<double>(Time), static_cast<double>(Length)) : FMath::Clamp<double>(Time, 0.0, Length);
	FAnimationPoseData PoseData(OutLayer.Pose, OutLayer.Curve, OutLayer.CustomAttributes);
	const FAnimExtractContext Extraction(SampleTime, false, FDeltaTimeRecord(), bLooping);
	Sequence->GetAnimationPose(PoseData, Extraction);
}

bool FEGChessAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	const float ActionBlendWeight = FMath::Clamp(ActionWeight, 0.f, 1.f);
	const float LocomotionWeight = 1.f - ActionBlendWeight;
	float IdleW = FMath::Max(0.f, IdleWeight) * LocomotionWeight;
	float WalkW = FMath::Max(0.f, WalkWeight) * LocomotionWeight;
	float RunW = FMath::Max(0.f, RunWeight) * LocomotionWeight;
	float Total = IdleW + WalkW + RunW + ActionBlendWeight;

	Output.Pose.ResetToRefPose();
	Output.Curve.InitFrom(GetRequiredBones());
	if (Total <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	FPoseContext Layer(this);
	FAnimationPoseData OutputData(Output.Pose, Output.Curve, Output.CustomAttributes);
	FAnimationPoseData LayerData(Layer.Pose, Layer.Curve, Layer.CustomAttributes);
	bool bHasAccumulated = false;
	float AccumulatedWeight = 0.f;

	auto Accumulate = [&](UAnimSequence* Sequence, float Time, bool bLooping, float Weight)
	{
		if (!Sequence || Weight <= KINDA_SMALL_NUMBER) return;
		if (!bHasAccumulated)
		{
			// First contributing layer seeds the output pose directly.
			EvaluateLayer(Sequence, Time, bLooping, Output);
			bHasAccumulated = true;
			AccumulatedWeight = Weight;
			return;
		}
		EvaluateLayer(Sequence, Time, bLooping, Layer);
		FAnimationRuntime::BlendTwoPosesTogetherInPlace(OutputData, LayerData, AccumulatedWeight / (AccumulatedWeight + Weight));
		AccumulatedWeight += Weight;
	};

	// Locomotion first so an action always blends on top of the moving pose.
	Accumulate(IdleClip, IdleTime, true, IdleW);
	Accumulate(WalkClip, WalkTime, true, WalkW);
	Accumulate(RunClip, RunTime, true, RunW);
	Accumulate(ActionClip, ActionTime, bActionLooping, ActionBlendWeight);
	return true;
}

UEGChessAnimInstance::UEGChessAnimInstance()
{
	bUseMultiThreadedAnimationUpdate = true;
}

FAnimInstanceProxy* UEGChessAnimInstance::CreateAnimInstanceProxy()
{
	return new FEGChessAnimInstanceProxy(this);
}

void UEGChessAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	if (const AEGChessUnit* Unit = Cast<AEGChessUnit>(TryGetPawnOwner()))
	{
		FEGPresentationClips Source;
		if (Unit->GetPresentationClips(Source))
		{
			ApplyClips(Source);
		}
	}
}

void UEGChessAnimInstance::ApplyClips(const FEGPresentationClips& InClips)
{
	Clips = InClips;
	bInitializedClips = true;
	RefreshProxy();
}

void UEGChessAnimInstance::SetLocomotionSpeed(float InSpeed)
{
	LocomotionSpeed = FMath::Max(0.f, InSpeed);
}

void UEGChessAnimInstance::PlayAction(EGPresentationAction InAction, float PlayRate)
{
	if (InAction == EGPresentationAction::None)
	{
		StopAction();
		return;
	}
	ActiveAction = InAction;
	ClockAction = 0.f;
	PreviousClockAction = 0.f;
	ActionRate = FMath::Max(0.05f, PlayRate);
}

void UEGChessAnimInstance::SetActionRate(float Rate)
{
 if(FMath::IsFinite(Rate)&&Rate>0.f)ActionRate=FMath::Max(.05f,Rate);
}

void UEGChessAnimInstance::StopAction()
{
 // Round teardown can cancel presentation repeatedly after death. Preserve the
 // legacy graph's current death clock; a real revive changes state first.
 auto* Unit=Cast<AEGChessUnit>(GetOwningActor());
 if(!Unit&&GetOwningActor())Unit=Cast<AEGChessUnit>(GetOwningActor()->GetAttachParentActor());
 if(ActiveAction==EGPresentationAction::Death&&Unit&&!Unit->IsAlive_Implementation())return;
	ActiveAction = EGPresentationAction::None;
	ClockAction = 0.f;
	PreviousClockAction = 0.f;
}

bool UEGChessAnimInstance::IsActionActive() const
{
	return ActiveAction != EGPresentationAction::None;
}

UAnimSequence* UEGChessAnimInstance::GetActiveActionClip() const
{
	switch (ActiveAction)
	{
	case EGPresentationAction::Attack: return Clips.Attack;
	case EGPresentationAction::Skill: return Clips.Skill;
	case EGPresentationAction::Hit: return Clips.Hit;
	case EGPresentationAction::Death: return Clips.Death;
	default: return nullptr;
	}
}

UAnimSequence* UEGChessAnimInstance::GetDominantLocomotionClip() const
{
	if (RunBlend >= IdleBlend && RunBlend >= WalkBlend && Clips.Run) return Clips.Run;
	if (WalkBlend >= IdleBlend && Clips.Walk) return Clips.Walk;
	return Clips.Idle ? Clips.Idle.Get() : (Clips.Walk ? Clips.Walk.Get() : Clips.Run.Get());
}

float UEGChessAnimInstance::GetActionProgress() const
{
	const UAnimSequence* Sequence = nullptr;
	switch (ActiveAction)
	{
	case EGPresentationAction::Attack: Sequence = Clips.Attack; break;
	case EGPresentationAction::Skill: Sequence = Clips.Skill; break;
	case EGPresentationAction::Hit: Sequence = Clips.Hit; break;
	case EGPresentationAction::Death: Sequence = Clips.Death; break;
	default: break;
	}
	const float Length = SafeLength(Sequence);
	if (Length <= KINDA_SMALL_NUMBER) return 1.f;
	return FMath::Clamp(ClockAction / Length, 0.f, 1.f);
}

void UEGChessAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	TickPresentation(DeltaSeconds);
}

void UEGChessAnimInstance::TickPresentation(float DeltaSeconds)
{
	if (!bInitializedClips)
	{
		if (const AEGChessUnit* Unit = Cast<AEGChessUnit>(TryGetPawnOwner()))
		{
			FEGPresentationClips Source;
			if (Unit->GetPresentationClips(Source))
			{
				ApplyClips(Source);
			}
		}
	}

	UAnimSequence* ActionSequence = nullptr;
	switch (ActiveAction)
	{
	case EGPresentationAction::Attack: ActionSequence = Clips.Attack; break;
	case EGPresentationAction::Skill: ActionSequence = Clips.Skill; break;
	case EGPresentationAction::Hit: ActionSequence = Clips.Hit; break;
	case EGPresentationAction::Death: ActionSequence = Clips.Death; break;
	default: break;
	}
	if (ActiveAction != EGPresentationAction::None && !ActionSequence)
	{
		ActiveAction = EGPresentationAction::None;
	}

	const float Delta = FMath::Min(FMath::Max(DeltaSeconds, 0.f), 0.2f);
	ClockIdle += Delta;
	WrapTime(ClockIdle, SafeLength(Clips.Idle));

	// Locomotion clocks advance at the rate that matches the board speed so strides stay believable.
	const float WalkRate = Clips.WalkReferenceSpeed > 1.f
		? FMath::Clamp(LocomotionSpeed / Clips.WalkReferenceSpeed, 0.45f, 1.9f) : 1.f;
	const float RunRate = Clips.RunReferenceSpeed > 1.f
		? FMath::Clamp(LocomotionSpeed / Clips.RunReferenceSpeed, 0.6f, 1.8f) : 1.f;
	ClockWalk += Delta * WalkRate;
	WrapTime(ClockWalk, SafeLength(Clips.Walk));
	ClockRun += Delta * RunRate;
	WrapTime(ClockRun, SafeLength(Clips.Run));

	// Locomotion blend: idle below a crawl, walk in the middle, run above the threshold.
	const float WalkBand = FMath::Max(Clips.WalkReferenceSpeed * 0.55f, 20.f);
	const float RunBand = FMath::Max(Clips.RunMinimumSpeed, WalkBand + 20.f);
	float TargetIdle = 0.f, TargetWalk = 0.f, TargetRun = 0.f;
	if (LocomotionSpeed <= 5.f)
	{
		TargetIdle = 1.f;
	}
	else if (LocomotionSpeed < WalkBand)
	{
		const float T = FMath::Clamp((LocomotionSpeed - 5.f) / FMath::Max(WalkBand - 5.f, 1.f), 0.f, 1.f);
		TargetIdle = 1.f - T;
		TargetWalk = T;
	}
	else if (LocomotionSpeed < RunBand)
	{
		TargetWalk = 1.f;
	}
	else
	{
		const float T = FMath::Clamp((LocomotionSpeed - RunBand) / 120.f, 0.f, 1.f);
		TargetWalk = 1.f - T;
		TargetRun = T;
	}
	if (!Clips.Walk) { TargetIdle += TargetWalk; TargetWalk = 0.f; }
	if (!Clips.Run) { TargetWalk += TargetRun; TargetRun = 0.f; }
	if (!Clips.Idle) { TargetWalk += TargetIdle; TargetIdle = 0.f; }

	const float BlendStep = FMath::Max(Delta, 0.0001f) / BlendSeconds;
	auto Approach = [BlendStep](float Current, float Target)
	{
		return FMath::Clamp(Current + FMath::Clamp(Target - Current, -BlendStep, BlendStep), 0.f, 1.f);
	};
	IdleBlend = Approach(IdleBlend, TargetIdle);
	WalkBlend = Approach(WalkBlend, TargetWalk);
	RunBlend = Approach(RunBlend, TargetRun);
	const float LocoTotal = IdleBlend + WalkBlend + RunBlend;
	if (LocoTotal > KINDA_SMALL_NUMBER && FMath::Abs(LocoTotal - 1.f) > 0.01f)
	{
		IdleBlend /= LocoTotal;
		WalkBlend /= LocoTotal;
		RunBlend /= LocoTotal;
	}

	// Action layer.
	const bool bDeath = ActiveAction == EGPresentationAction::Death;
	if (ActionSequence && ActiveAction != EGPresentationAction::None)
	{
		ClockAction += Delta * ActionRate;
		const float Length = SafeLength(ActionSequence);
		if (bDeath)
		{
			ClockAction = FMath::Min(ClockAction, Length);
		}
		else if (ClockAction >= Length)
		{
			ClockAction = Length;
		}
		// No anim graph means no notify pipeline: dispatch the clip' gameplay events ourselves so
		// the skill component still commits damage on the authored frames.
		USkeletalMeshComponent* Mesh = GetOwningComponent();
		for (const FAnimNotifyEvent& NotifyEvent : ActionSequence->Notifies)
		{
			const float NotifyTime = NotifyEvent.GetTime();
			if (NotifyTime > PreviousClockAction && NotifyTime <= ClockAction && NotifyEvent.Notify)
			{
				if (const UEGSkillNotify* SkillNotify = Cast<UEGSkillNotify>(NotifyEvent.Notify))
				{
					UEGSkillNotify::DispatchEvent(SkillNotify->Event, Mesh, ActionSequence);
				}
			}
		}
		if (!bDeath && ClockAction >= Length)
		{
			ActiveAction = EGPresentationAction::None;
			ActionSequence = nullptr;
		}
	}
	PreviousClockAction = ClockAction;
	const float ActionTarget = ActionSequence && ActiveAction != EGPresentationAction::None ? 1.f : 0.f;
	// Death never blends back out: the corpse keeps the final pose.
	if (bPoseFrozen)
	{
		ActionBlend = ActionTarget;
	}
	else
	{
		ActionBlend = FMath::Clamp(ActionBlend + FMath::Clamp(ActionTarget - ActionBlend, -BlendStep, BlendStep), 0.f, 1.f);
	}
	if (bDeath && ActionBlend >= 0.999f && ClockAction >= SafeLength(ActionSequence) - KINDA_SMALL_NUMBER)
	{
		bPoseFrozen = true;
	}
	if (!bDeath)
	{
		bPoseFrozen = false;
	}

	RefreshProxy();
}

void UEGChessAnimInstance::RefreshProxy()
{
	FEGChessAnimInstanceProxy& Proxy = GetProxyOnGameThread<FEGChessAnimInstanceProxy>();
	Proxy.IdleClip = Clips.Idle;
	Proxy.WalkClip = Clips.Walk;
	Proxy.RunClip = Clips.Run;
	Proxy.IdleTime = ClockIdle;
	Proxy.WalkTime = ClockWalk;
	Proxy.RunTime = ClockRun;
	Proxy.IdleWeight = IdleBlend;
	Proxy.WalkWeight = WalkBlend;
	Proxy.RunWeight = RunBlend;
	Proxy.ActionTime = ClockAction;
	Proxy.ActionWeight = ActionBlend;
	Proxy.bActionLooping = false;
	switch (ActiveAction)
	{
	case EGPresentationAction::Attack: Proxy.ActionClip = Clips.Attack; break;
	case EGPresentationAction::Skill: Proxy.ActionClip = Clips.Skill; break;
	case EGPresentationAction::Hit: Proxy.ActionClip = Clips.Hit; break;
	case EGPresentationAction::Death: Proxy.ActionClip = Clips.Death; break;
	default: Proxy.ActionClip = nullptr; break;
	}
}
