#include "Presentation/EGAnimationRetargetLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#endif

bool UEGAnimationRetargetLibrary::CopyUnitScaleRetarget(UAnimSequence* Source, UAnimSequence* Destination, USkeletalMesh* TargetMesh)
{
#if WITH_EDITOR
	if (!Source || !Destination || !TargetMesh || Source == Destination || !Source->GetSkeleton()
		|| Destination->GetSkeleton() != TargetMesh->GetSkeleton() || !Source->GetDataModel()) return false;
	const auto* Model = Source->GetDataModel();
	// These locomotion clips are plain bone animation. Refuse to silently discard other content.
	if (!Source->Notifies.IsEmpty() || Model->GetNumberOfFloatCurves() || Model->GetNumberOfTransformCurves())
	{
		UE_LOG(LogTemp, Warning, TEXT("Retarget scale copy rejected non-bone content: %d notifies, %d float curves, %d transform curves"),
			Source->Notifies.Num(), Model->GetNumberOfFloatCurves(), Model->GetNumberOfTransformCurves());
		return false;
	}
	const auto* SourceMesh = Source->GetPreviewMesh();
	if (!SourceMesh) return false;
	const auto& From = SourceMesh->GetRefSkeleton();
	const auto& To = TargetMesh->GetRefSkeleton();
	if (From.GetNum() != To.GetNum()) return false;
	TArray<FTransform> FromGlobal, ToGlobal;
	for (int32 I = 0; I < To.GetNum(); ++I)
	{
		if (From.GetBoneName(I) != To.GetBoneName(I) || From.GetParentIndex(I) != To.GetParentIndex(I)) return false;
		const int32 Parent = To.GetParentIndex(I);
		FromGlobal.Add(Parent < 0 ? From.GetRefBonePose()[I] : From.GetRefBonePose()[I] * FromGlobal[Parent]);
		ToGlobal.Add(Parent < 0 ? To.GetRefBonePose()[I] : To.GetRefBonePose()[I] * ToGlobal[Parent]);
		if (!FromGlobal[I].GetTranslation().Equals(ToGlobal[I].GetTranslation(), .01)
			|| !FromGlobal[I].GetRotation().Equals(ToGlobal[I].GetRotation(), .0001)
			|| !FromGlobal[I].GetScale3D().Equals(FVector::OneVector, .001))
		{
			UE_LOG(LogTemp, Warning, TEXT("Retarget reference mismatch on %s: source %s, target %s"),
				*To.GetBoneName(I).ToString(), *FromGlobal[I].ToString(), *ToGlobal[I].ToString());
			return false;
		}
	}
	struct FConvertedTrack { FName Name; TArray<FVector> Positions, Scales; TArray<FQuat> Rotations; };
	TArray<FConvertedTrack> Tracks;
	TArray<FName> Names; Model->GetBoneTrackNames(Names);
	for (const FName Name : Names)
	{
		const int32 Index = To.FindBoneIndex(Name);
		if (Index == INDEX_NONE) return false;
		const int32 Parent = To.GetParentIndex(Index);
		const FVector ParentScale = Parent < 0 ? FVector::OneVector : ToGlobal[Parent].GetScale3D();
		if (ParentScale.GetAbsMin() < SMALL_NUMBER) return false;
		TArray<FTransform> Keys; Model->GetBoneTrackTransforms(Name, Keys);
		auto& Track = Tracks.AddDefaulted_GetRef(); Track.Name = Name;
		for (const auto& Key : Keys)
		{
			if (Key.ContainsNaN()) return false;
			Track.Positions.Add(Key.GetTranslation() / ParentScale);
			Track.Rotations.Add(Key.GetRotation());
			Track.Scales.Add(Key.GetScale3D() * To.GetRefBonePose()[Index].GetScale3D());
		}
	}
	auto& Controller = Destination->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Restore imported reference scale after IK retarget")), false);
	Controller.RemoveAllBoneTracks(false);
	Controller.SetFrameRate(Model->GetFrameRate(), false);
	Controller.SetNumberOfFrames(FFrameNumber(Model->GetNumberOfFrames()), false);
	bool bSuccess = true;
	for (const auto& Track : Tracks)
		bSuccess &= Controller.AddBoneCurve(Track.Name, false)
			&& Controller.SetBoneTrackKeys(Track.Name, Track.Positions, Track.Rotations, Track.Scales, false);
	Controller.NotifyPopulated(); Controller.CloseBracket(false);
	Destination->SetPreviewMesh(TargetMesh);
	Destination->bEnableRootMotion = false;
	Destination->bForceRootLock = true;
	Destination->MarkPackageDirty();
	return bSuccess;
#else
	return false;
#endif
}
