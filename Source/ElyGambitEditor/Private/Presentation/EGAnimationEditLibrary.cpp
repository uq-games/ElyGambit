#include "Presentation/EGAnimationEditLibrary.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#endif

namespace
{
	const TCHAR* DocumentHeader = TEXT("v1");

#if WITH_EDITOR
	bool SplitKeys(const FString& Line, TArray<FString>& OutTokens)
	{
		FString Payload = Line.RightChop(1).TrimStartAndEnd();
		Payload.ParseIntoArray(OutTokens, TEXT(" "), true);
		return OutTokens.Num() == 3;
	}

	bool ParseVector(const FString& Token, FVector& Out)
	{
		TArray<FString> Parts;
		Token.ParseIntoArray(Parts, TEXT(","), false);
		if (Parts.Num() != 3) return false;
		Out = FVector(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]));
		return true;
	}

	bool ParseQuat(const FString& Token, FQuat& Out)
	{
		TArray<FString> Parts;
		Token.ParseIntoArray(Parts, TEXT(","), false);
		if (Parts.Num() != 4) return false;
		Out = FQuat(FCString::Atod(*Parts[0]), FCString::Atod(*Parts[1]), FCString::Atod(*Parts[2]),
			FCString::Atod(*Parts[3]));
		Out.Normalize();
		return true;
	}
#endif
}

FString UEGAnimationEditLibrary::ExportTrackDocument(UAnimSequence* Animation)
{
#if WITH_EDITOR
	const auto* Model = Animation ? Animation->GetDataModel() : nullptr;
	if (!Model) return FString();
	const FFrameRate Rate = Model->GetFrameRate();
	TStringBuilder<1 << 12> Builder;
	Builder.Appendf(TEXT("%s\n"), DocumentHeader);
	Builder.Appendf(TEXT("fps %d %d\n"), Rate.Numerator, Rate.Denominator);
	Builder.Appendf(TEXT("frames %d\n"), Model->GetNumberOfFrames());
	Builder.Appendf(TEXT("length %.8f\n"), Animation->GetPlayLength());
	TArray<FName> Names;
	Model->GetBoneTrackNames(Names);
	for (const FName Name : Names)
	{
		TArray<FTransform> Keys;
		Model->GetBoneTrackTransforms(Name, Keys);
		Builder.Appendf(TEXT("bone %s\n"), *Name.ToString());
		for (const FTransform& Key : Keys)
		{
			const FVector P = Key.GetTranslation();
			const FQuat R = Key.GetRotation().GetNormalized();
			const FVector S = Key.GetScale3D();
			Builder.Appendf(TEXT("k %.6f,%.6f,%.6f %.7f,%.7f,%.7f,%.7f %.6f,%.6f,%.6f\n"),
				P.X, P.Y, P.Z, R.X, R.Y, R.Z, R.W, S.X, S.Y, S.Z);
		}
	}
	return Builder.ToString();
#else
	return FString();
#endif
}

bool UEGAnimationEditLibrary::ImportTrackDocument(UAnimSequence* Animation, const FString& Document)
{
#if WITH_EDITOR
	if (!Animation || Document.IsEmpty()) return false;
	TArray<FString> Lines;
	Document.ParseIntoArrayLines(Lines, false);
	if (Lines.Num() < 4 || !Lines[0].StartsWith(DocumentHeader)) return false;

	int32 RateNumerator = 30;
	int32 RateDenominator = 1;
	int32 FrameCount = 0;
	struct FTrack { FName Name; TArray<FVector> Positions; TArray<FQuat> Rotations; TArray<FVector> Scales; };
	TArray<FTrack> Tracks;
	for (int32 Index = 1; Index < Lines.Num(); ++Index)
	{
		const FString& Line = Lines[Index];
		if (Line.StartsWith(TEXT("fps ")))
		{
			FString Payload = Line.RightChop(4).TrimStartAndEnd();
			TArray<FString> Parts;
			Payload.ParseIntoArray(Parts, TEXT(" "), true);
			if (Parts.Num() == 2)
			{
				RateNumerator = FCString::Atoi(*Parts[0]);
				RateDenominator = FCString::Atoi(*Parts[1]);
			}
		}
		else if (Line.StartsWith(TEXT("frames ")))
		{
			FrameCount = FCString::Atoi(*Line.RightChop(7).TrimStartAndEnd());
		}
		else if (Line.StartsWith(TEXT("bone ")))
		{
			Tracks.AddDefaulted_GetRef().Name = FName(*Line.RightChop(5).TrimStartAndEnd());
		}
		else if (Line.StartsWith(TEXT("k ")) && Tracks.Num())
		{
			TArray<FString> Tokens;
			if (!SplitKeys(Line, Tokens)) return false;
			FVector Position, Scale;
			FQuat Rotation;
			if (!ParseVector(Tokens[0], Position) || !ParseQuat(Tokens[1], Rotation) || !ParseVector(Tokens[2], Scale))
			{
				return false;
			}
			auto& Track = Tracks.Last();
			Track.Positions.Add(Position);
			Track.Rotations.Add(Rotation);
			Track.Scales.Add(Scale);
		}
	}
	if (Tracks.IsEmpty() || FrameCount < 1) return false;
	for (const FTrack& Track : Tracks)
	{
		if (Track.Positions.Num() != Track.Rotations.Num() || Track.Positions.Num() != Track.Scales.Num()
			|| Track.Positions.Num() != FrameCount + 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("ImportTrackDocument frame count mismatch on %s: %d vs %d keys"),
				*Track.Name.ToString(), Track.Positions.Num(), FrameCount + 1);
			return false;
		}
	}

	// Every track is rewritten, so the destination skeleton must know all of them.
	if (const USkeleton* Skeleton = Animation->GetSkeleton())
	{
		for (const FTrack& Track : Tracks)
		{
			if (Skeleton->GetReferenceSkeleton().FindBoneIndex(Track.Name) == INDEX_NONE)
			{
				UE_LOG(LogTemp, Warning, TEXT("ImportTrackDocument unknown bone %s"), *Track.Name.ToString());
				return false;
			}
		}
	}

	auto& Controller = Animation->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Replace authored bone keys")), false);
	Controller.RemoveAllBoneTracks(false);
	Controller.SetFrameRate(FFrameRate(RateNumerator, FMath::Max(1, RateDenominator)), false);
	Controller.SetNumberOfFrames(FFrameNumber(FrameCount), false);
	bool bSuccess = true;
	for (const FTrack& Track : Tracks)
	{
		bSuccess &= Controller.AddBoneCurve(Track.Name, false)
			&& Controller.SetBoneTrackKeys(Track.Name, Track.Positions, Track.Rotations, Track.Scales, false);
	}
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);
	Animation->MarkPackageDirty();
	return bSuccess;
#else
	return false;
#endif
}

bool UEGAnimationEditLibrary::LockPlanarRootTranslation(UAnimSequence* Animation)
{
#if WITH_EDITOR
	if (!Animation || !Animation->GetSkeleton() || !Animation->GetDataModel()) return false;
	const auto& Ref = Animation->GetSkeleton()->GetReferenceSkeleton();
	if (!Ref.GetNum()) return false;
	const FName Root = Ref.GetBoneName(0);
	TArray<FTransform> Keys;
	Animation->GetDataModel()->GetBoneTrackTransforms(Root, Keys);
	if (Keys.IsEmpty()) return false;
	TArray<FVector> Positions, Scales; TArray<FQuat> Rotations;
	for (const auto& Key : Keys)
	{
		Positions.Add(FVector(0, 0, Key.GetTranslation().Z));
		Rotations.Add(Key.GetRotation()); Scales.Add(Key.GetScale3D());
	}
	auto& Controller = Animation->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Lock common carrier root XY")), false);
	const bool Result = Controller.SetBoneTrackKeys(Root, Positions, Rotations, Scales, false);
	Controller.CloseBracket(false); Animation->MarkPackageDirty(); return Result;
#else
	return false;
#endif
}

bool UEGAnimationEditLibrary::IsPlainBoneAnimation(UAnimSequence* Animation)
{
#if WITH_EDITOR
	const auto* Model = Animation ? Animation->GetDataModel() : nullptr;
	if (!Model) return false;
	return Animation->Notifies.IsEmpty() && Model->GetNumberOfFloatCurves() == 0
		&& Model->GetNumberOfTransformCurves() == 0;
#else
	return false;
#endif
}

FString UEGAnimationEditLibrary::DescribeTiming(UAnimSequence* Animation)
{
#if WITH_EDITOR
	const auto* Model = Animation ? Animation->GetDataModel() : nullptr;
	if (!Model) return TEXT("");
	const FFrameRate Rate = Model->GetFrameRate();
	return FString::Printf(TEXT("%d frames @ %d/%d, %.4fs"), Model->GetNumberOfFrames(), Rate.Numerator,
		Rate.Denominator, Animation->GetPlayLength());
#else
	return FString();
#endif
}

bool UEGAnimationEditLibrary::RebindSkeleton(UAnimSequence* Animation, USkeletalMesh* Mesh)
{
#if WITH_EDITOR
	if (!Animation || !Mesh || !Mesh->GetSkeleton()) return false;
	Animation->SetSkeleton(Mesh->GetSkeleton());
	Animation->SetPreviewMesh(Mesh);
	Animation->MarkPackageDirty();
	return true;
#else
	return false;
#endif
}
