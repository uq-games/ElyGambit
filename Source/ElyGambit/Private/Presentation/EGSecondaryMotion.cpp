#include "Presentation/EGSecondaryMotion.h"
#include "AnimNode_KawaiiPhysics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#if WITH_EDITOR
#include "SkeletalMeshAttributes.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#endif

void UEGSecondaryMotionAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	bWasEnabled = bSecondaryMotionEnabled;
	PreviousStrengths = FVector(BreastStrength, GluteStrength, ClothingStrength);
	LastUpdateTime = -1.;
}

void UEGSecondaryMotionAnimInstance::ResetSecondaryMotion()
{
	ResetDynamics(ETeleportType::ResetPhysics);
}

void UEGSecondaryMotionAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	// NativeUpdate runs before parallel graph evaluation; never mutate nodes from an actor tick.
	const double Now = FPlatformTime::Seconds();
	const FVector Strengths(BreastStrength, GluteStrength, ClothingStrength);
	const bool bGroupToggled = (PreviousStrengths.X <= 0.) != (Strengths.X <= 0.)
		|| (PreviousStrengths.Y <= 0.) != (Strengths.Y <= 0.) || (PreviousStrengths.Z <= 0.) != (Strengths.Z <= 0.);
	if (bWasEnabled != bSecondaryMotionEnabled || bGroupToggled || DeltaSeconds > .1f || (LastUpdateTime >= 0. && Now - LastUpdateTime > .25))
		ResetSecondaryMotion();
	PreviousStrengths = Strengths;
	bWasEnabled = bSecondaryMotionEnabled;
	LastUpdateTime = Now;
	for (TFieldIterator<FStructProperty> It(GetClass()); It; ++It)
	{
		if (It->Struct != FAnimNode_KawaiiPhysics::StaticStruct()) continue;
		auto* Node = It->ContainerPtrToValuePtr<FAnimNode_KawaiiPhysics>(this);
		const FString Root = Node->RootBone.BoneName.ToString();
		const float Strength = Root.Contains(TEXT("Breast")) ? BreastStrength : Root.Contains(TEXT("EG_Glute")) ? GluteStrength : ClothingStrength;
		Node->Alpha = bSecondaryMotionEnabled && FMath::IsFinite(Strength) ? FMath::Clamp(Strength, 0.f, 1.f) : 0.f;
	}
}

bool UEGSecondaryMotionLibrary::SetSecondaryMotion(USkeletalMeshComponent* Mesh, bool bEnabled, float Breast, float Glute, float Clothing)
{
	auto* Instance = Mesh ? Cast<UEGSecondaryMotionAnimInstance>(Mesh->GetPostProcessInstance()) : nullptr;
	if (!Instance || !FMath::IsFinite(Breast) || !FMath::IsFinite(Glute) || !FMath::IsFinite(Clothing)) return false;
	Instance->bSecondaryMotionEnabled = bEnabled;
	Instance->BreastStrength = FMath::Clamp(Breast, 0.f, 1.f);
	Instance->GluteStrength = FMath::Clamp(Glute, 0.f, 1.f);
	Instance->ClothingStrength = FMath::Clamp(Clothing, 0.f, 1.f);
	return true;
}

void UEGSecondaryMotionLibrary::ResetSecondaryMotion(USkeletalMeshComponent* Mesh)
{
	if (auto* Instance = Mesh ? Cast<UEGSecondaryMotionAnimInstance>(Mesh->GetPostProcessInstance()) : nullptr)
		Instance->ResetSecondaryMotion();
}

void UEGSecondaryMotionLibrary::TickPreview(USkeletalMeshComponent* Mesh, float DeltaSeconds)
{
#if WITH_EDITOR
	if (!Mesh || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;
	Mesh->TickAnimation(FMath::Min(DeltaSeconds, .15f), false);
	Mesh->RefreshBoneTransforms(); Mesh->UpdateBounds(); Mesh->MarkRenderTransformDirty(); Mesh->MarkRenderDynamicDataDirty();
#endif
}

FString UEGSecondaryMotionLibrary::AuditMesh(USkeletalMesh* Mesh, bool bIncludeVertices)
{
#if WITH_EDITOR
	if (!Mesh) return TEXT("{}");
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("mesh"), Mesh->GetPathName());
	Result->SetStringField(TEXT("skeleton"), GetPathNameSafe(Mesh->GetSkeleton()));
	Result->SetStringField(TEXT("physics_asset"), GetPathNameSafe(Mesh->GetPhysicsAsset()));
	Result->SetStringField(TEXT("post_process"), GetPathNameSafe(Mesh->GetPostProcessAnimBlueprint()));
	Result->SetNumberField(TEXT("clothing_assets"), Mesh->GetMeshClothingAssets().Num());
	Result->SetNumberField(TEXT("morph_targets"), Mesh->GetMorphTargets().Num());
	Result->SetNumberField(TEXT("lod_count"), Mesh->GetLODNum());
	const auto& Ref = Mesh->GetRefSkeleton();
	TArray<FTransform> Component;
	TArray<TSharedPtr<FJsonValue>> Bones, Vertices;
	TArray<int32> Counts; Counts.Init(0, Ref.GetNum());
	const auto Vec = [](const FVector& V) { return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X), MakeShared<FJsonValueNumber>(V.Y), MakeShared<FJsonValueNumber>(V.Z)}; };
	if (const FMeshDescription* MD = Mesh->GetMeshDescription(0))
	{
		FSkeletalMeshConstAttributes Attr(*MD);
		const auto Positions = Attr.GetVertexPositions();
		const auto Weights = Attr.GetVertexSkinWeights();
		for (const FVertexID V : MD->Vertices().GetElementIDs())
		{
			TArray<TSharedPtr<FJsonValue>> Influence;
			for (const auto Weight : Weights.Get(V))
			{
				if (Weight.GetWeight() <= 0.f) continue;
				if (Counts.IsValidIndex(Weight.GetBoneIndex())) ++Counts[Weight.GetBoneIndex()];
				if (bIncludeVertices) Influence.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Weight.GetBoneIndex()), MakeShared<FJsonValueNumber>(Weight.GetWeight())}));
			}
			if (bIncludeVertices)
			{
				auto Entry = MakeShared<FJsonObject>(); Entry->SetNumberField(TEXT("id"), V.GetValue());
				Entry->SetArrayField(TEXT("position"), Vec(FVector(Positions[V])));
				Entry->SetArrayField(TEXT("weights"), Influence); Vertices.Add(MakeShared<FJsonValueObject>(Entry));
			}
		}
		Result->SetNumberField(TEXT("vertices"), MD->Vertices().Num());
	}
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		const int32 Parent = Ref.GetParentIndex(I);
		Component.Add(Parent == INDEX_NONE ? Ref.GetRefBonePose()[I] : Ref.GetRefBonePose()[I] * Component[Parent]);
		auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Ref.GetBoneName(I).ToString());
		Entry->SetNumberField(TEXT("parent"), Parent); Entry->SetNumberField(TEXT("weighted_vertices"), Counts[I]);
		Entry->SetArrayField(TEXT("position"), Vec(Component[I].GetTranslation())); Bones.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("bones"), Bones);
	if (bIncludeVertices) Result->SetArrayField(TEXT("vertex_data"), Vertices);
	FString Json; auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Result, Writer); return Json;
#else
	return TEXT("{\"error\":\"editor_only\"}");
#endif
}

bool UEGSecondaryMotionLibrary::AssignDerivedSkeleton(UObject* Asset, USkeleton* Skeleton)
{
#if WITH_EDITOR
	USkeleton* Previous = nullptr;
	if (auto* Mesh = Cast<USkeletalMesh>(Asset)) Previous = Mesh->GetSkeleton();
	if (auto* Animation = Cast<UAnimationAsset>(Asset)) Previous = Animation->GetSkeleton();
	if (!Previous || !Skeleton) return false;
	const auto& OldRef = Previous->GetReferenceSkeleton();
	const auto& NewRef = Skeleton->GetReferenceSkeleton();
	for (int32 I = 0; I < OldRef.GetNum(); ++I)
	{
		const int32 J = NewRef.FindBoneIndex(OldRef.GetBoneName(I));
		if (J == INDEX_NONE || !OldRef.GetRefBonePose()[I].Equals(NewRef.GetRefBonePose()[J], .001f)) return false;
		const int32 P = OldRef.GetParentIndex(I), Q = NewRef.GetParentIndex(J);
		if ((P == INDEX_NONE) != (Q == INDEX_NONE) || (P != INDEX_NONE && OldRef.GetBoneName(P) != NewRef.GetBoneName(Q))) return false;
	}
	if (auto* Mesh = Cast<USkeletalMesh>(Asset)) Mesh->SetSkeleton(Skeleton);
	if (auto* Animation = Cast<UAnimationAsset>(Asset)) Animation->SetSkeleton(Skeleton);
	Asset->MarkPackageDirty(); return true;
#else
	return false;
#endif
}

bool UEGSecondaryMotionLibrary::CopyBoneAnimation(UAnimSequence* Source, UAnimSequence* Destination)
{
#if WITH_EDITOR
	if (!Source || !Destination || Source == Destination || !Source->GetDataModel()) return false;
	const auto* Model = Source->GetDataModel();
	if (!Source->Notifies.IsEmpty() || Model->GetNumberOfFloatCurves() || Model->GetNumberOfTransformCurves()) return false;
	// Same original hierarchy and rest poses are mandatory; no retargeting is performed here.
	const auto& From = Source->GetSkeleton()->GetReferenceSkeleton();
	const auto& To = Destination->GetSkeleton()->GetReferenceSkeleton();
	for (int32 I = 0; I < From.GetNum(); ++I)
	{
		const int32 J = To.FindBoneIndex(From.GetBoneName(I));
		if (J == INDEX_NONE || !From.GetRefBonePose()[I].Equals(To.GetRefBonePose()[J], .001f)) return false;
	}
	auto& Controller = Destination->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Copy original prototype bone keys")), false);
	Controller.RemoveAllBoneTracks(false);
	Controller.SetFrameRate(Model->GetFrameRate(), false);
	Controller.SetNumberOfFrames(FFrameNumber(Model->GetNumberOfFrames()), false);
	TArray<FName> Names; Model->GetBoneTrackNames(Names); bool bOk = true;
	for (const FName Name : Names)
	{
		TArray<FTransform> Keys; Model->GetBoneTrackTransforms(Name, Keys);
		TArray<FVector> Pos, Scale; TArray<FQuat> Rot;
		for (const auto& K : Keys) { Pos.Add(K.GetTranslation()); Rot.Add(K.GetRotation()); Scale.Add(K.GetScale3D()); }
		bOk &= Controller.AddBoneCurve(Name, false) && Controller.SetBoneTrackKeys(Name, Pos, Rot, Scale, false);
	}
	Controller.NotifyPopulated(); Controller.CloseBracket(false);
	Destination->MarkPackageDirty(); return bOk;
#else
	return false;
#endif
}

FString UEGSecondaryMotionLibrary::ValidateMotion(USkeletalMesh* Mesh, UAnimationAsset* Animation, int32 FPS)
{
#if WITH_EDITOR
	if (!Mesh || FPS < 1 || (Animation && Animation->GetSkeleton() != Mesh->GetSkeleton())) return TEXT("{\"error\":\"invalid_input\"}");
	const auto Values = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
	AActor* Actor = World->SpawnActor<AActor>();
	auto MakeComponent = [&](const TCHAR* Name)
	{
		auto* C = NewObject<USkeletalMeshComponent>(Actor, Name);
		Actor->AddInstanceComponent(C); C->SetSkeletalMeshAsset(Mesh);
		C->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		C->bEnableUpdateRateOptimizations = false; C->RegisterComponentWithWorld(World);
		C->PlayAnimation(Animation, true); return C;
	};
	auto* Enabled = MakeComponent(TEXT("Enabled")); auto* Disabled = MakeComponent(TEXT("Disabled"));
	auto Result = MakeShared<FJsonObject>();
	const bool Active = SetSecondaryMotion(Enabled, true) && SetSecondaryMotion(Disabled, false);
	Result->SetBoolField(TEXT("post_process_active"), Active); Result->SetNumberField(TEXT("fps"), FPS);
	const auto& Ref = Mesh->GetRefSkeleton();
	TArray<double> Peaks, Settled; Peaks.Init(0., Ref.GetNum()); Settled.Init(0., Ref.GetNum());
	double MainMax = 0., ElapsedMs = 0., OffMs = 0.; bool Finite = true;
	TArray<TSharedPtr<FJsonValue>> Samples;
	for (int32 F = 0; F < FPS * 5; ++F)
	{
		const float T = float(F) / FPS, Dt = 1.f / FPS;
		const float Phase = FMath::Min(T, 1.5f);
		const FVector Location(25.f * FMath::Sin(Phase * 4.f), 0., 2.f * FMath::Sin(Phase * 8.f));
		const FRotator Rotation(0., 18.f * FMath::Sin(Phase * 4.f), 0.);
		for (auto* C : {Enabled, Disabled})
		{
			C->SetWorldLocationAndRotation(Location, Rotation);
			if (F == FPS * 2) { C->SetWorldLocation(FVector(1500, 0, 0), false, nullptr, ETeleportType::TeleportPhysics); ResetSecondaryMotion(C); }
			const double Start = FPlatformTime::Seconds();
			C->TickAnimation(F == FPS * 3 ? .15f : Dt, false); C->RefreshBoneTransforms();
			const double Ms = (FPlatformTime::Seconds() - Start) * 1000.;
			if (C == Enabled) ElapsedMs += Ms; else OffMs += Ms;
		}
		const auto& A = Enabled->GetComponentSpaceTransforms(); const auto& B = Disabled->GetComponentSpaceTransforms();
		if (A.Num() != Ref.GetNum() || B.Num() != Ref.GetNum()) { Finite = false; break; }
		auto Frame = MakeShared<FJsonObject>(); Frame->SetNumberField(TEXT("time"), T);
		for (int32 I = 0; I < Ref.GetNum(); ++I)
		{
			Finite &= !A[I].ContainsNaN() && !B[I].ContainsNaN();
			const double Angle = FMath::RadiansToDegrees(A[I].GetRotation().AngularDistance(B[I].GetRotation()));
			Peaks[I] = FMath::Max(Peaks[I], Angle); if (T > 4.5f) Settled[I] = FMath::Max(Settled[I], Angle);
			const FString Name = Ref.GetBoneName(I).ToString();
			if (Name.StartsWith(TEXT("Bip001"))) MainMax = FMath::Max(MainMax, Angle);
			if (F % FMath::Max(1, FPS / 30) == 0 && (Name.Contains(TEXT("Breast")) || Name.Contains(TEXT("EG_Glute")) || Name.Contains(TEXT("Skirt_B_"))))
				Frame->SetNumberField(Name, Angle);
		}
		if (F % FMath::Max(1, FPS / 30) == 0) Samples.Add(MakeShared<FJsonValueObject>(Frame));
	}
	TArray<TSharedPtr<FJsonValue>> Bones;
	for (int32 I = 0; I < Ref.GetNum(); ++I)
	{
		auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("name"), Ref.GetBoneName(I).ToString());
		Entry->SetNumberField(TEXT("peak_degrees"), Peaks[I]); Entry->SetNumberField(TEXT("settled_degrees"), Settled[I]); Bones.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetBoolField(TEXT("finite"), Finite); Result->SetNumberField(TEXT("main_bone_max_degrees"), MainMax);
	Result->SetNumberField(TEXT("enabled_mean_ms"), ElapsedMs / (FPS * 5)); Result->SetNumberField(TEXT("disabled_mean_ms"), OffMs / (FPS * 5));
	Result->SetArrayField(TEXT("bones"), Bones); Result->SetArrayField(TEXT("samples"), Samples);
	World->DestroyWorld(false);
	FString Json; auto Writer = TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Result, Writer); return Json;
#else
	return TEXT("{\"error\":\"editor_only\"}");
#endif
}
