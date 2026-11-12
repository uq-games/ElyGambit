#include "Presentation/EGSourceCharacterPreview.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceDistortionPass.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Curves/CurveFloat.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EGNativeRendererLibrary.h"

bool FEGSourcePreviewPart::EvaluateVisibility(const UCurveFloat* Curve, float Time) const
{
	if (!Curve) return true;
	const float Value = Curve->GetFloatValue(Time);
	return bNonzeroSourceVisibility ? Value != 0.f : Value > .5f;
}

void FEGSourcePreviewPart::ApplyRenderBindings(USkeletalMeshComponent* Component,UObject* LifetimeOwner,float SampleTime) const
{
	if (!IsValid(Component)) return;
	UObject* BindingOwner=LifetimeOwner ? LifetimeOwner : Component->GetOwner();
	if (!NativePassDefinition) UEGNativeRendererLibrary::ReleaseSourceDefinition(Component,BindingOwner);
	if (!bUseSourceRenderBindings && !NativePassDefinition) return;
	if (!NativePassDefinition)
	{
		Component->SetRenderInMainPass(!bSourceDepthOnly);
		Component->SetRenderInDepthPass(true);
	}
	Component->SetTranslucentSortPriority(SourceTranslucencySortPriority);
	for (const auto& Override : MaterialOverrides)
	{
		if (Override.Key < 0 || Override.Key >= Component->GetNumMaterials() || !IsValid(Override.Value)) continue;
		auto* Current = Cast<UMaterialInstanceDynamic>(Component->GetMaterial(Override.Key));
		if (!Current || Current->Parent != Override.Value)
			Component->CreateDynamicMaterialInstance(Override.Key, Override.Value);
	}
	if (NativePassDefinition)
	{
		if (auto* Native=UEGNativeRendererLibrary::ApplySourceDefinition(Component,NativePassDefinition,BindingOwner))
			for (const auto& Curve:MaterialCurves)
				if (Curve.Value) Native->SetNativeScalarParameter(Curve.Key,Curve.Value->GetFloatValue(SampleTime));
	}
}

AEGSourceCharacterPreview::AEGSourceCharacterPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	bAllowTickBeforeBeginPlay = true;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
}

const FEGSourcePreviewMotion* AEGSourceCharacterPreview::CurrentMotion() const
{
	return Definition && Definition->Motions.IsValidIndex(MotionIndex) ? &Definition->Motions[MotionIndex] : nullptr;
}

void AEGSourceCharacterPreview::ClearPreview()
{
	for (const auto& Part:PreviewParts) if (IsValid(Part)) UEGNativeRendererLibrary::ReleaseSourceDefinition(Part.Get(),this);
	// PIE may duplicate instance components without the transient bookkeeping
	// array. Recover only components explicitly owned by this viewer.
	TArray<USkeletalMeshComponent*> OwnedParts;
	GetComponents(OwnedParts);
	for (USkeletalMeshComponent* Part : OwnedParts)
	{
		if (IsValid(Part) && (PreviewParts.Contains(Part) || Part->ComponentHasTag(TEXT("EGSourcePreviewOwned"))))
		{
			RemoveInstanceComponent(Part);
			Part->DestroyComponent();
		}
	}
	PreviewParts.Reset();
	PreviewAttachmentParents.Reset();
	PreviewMaterials.Reset();
	TArray<UStaticMeshComponent*> OwnedEffects;
	GetComponents(OwnedEffects);
	for (UStaticMeshComponent* Effect : OwnedEffects)
	{
		if (IsValid(Effect) && Effect->ComponentHasTag(TEXT("EGSourcePreviewOwnedFX")))
		{
			UEGNativeRendererLibrary::ReleaseSourceDefinition(Effect,this);
			RemoveInstanceComponent(Effect);
			Effect->DestroyComponent();
		}
	}
	PreviewEffects.Reset();
	PreviewEffectMaterials.Reset();
	EffectTrackSources.Reset();
	EffectBirthAnchors.Reset();
	BuiltDefinition = nullptr;
	BuiltMotion = INDEX_NONE;
}

void AEGSourceCharacterPreview::RebuildPreview()
{
	ClearPreview();
	if (IsTemplate() || !GetWorld()) return;
	BuiltDefinition = Definition;
	BuiltMotion = MotionIndex;
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	if (!Motion) return;
	for (const FEGSourcePreviewPart& Source : Motion->Parts)
	{
		USkeletalMeshComponent* Part = nullptr;
		if (Source.Mesh && (!Source.Animation || Source.Animation->GetSkeleton() == Source.Mesh->GetSkeleton()))
		{
			Part = NewObject<USkeletalMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
			Part->ComponentTags.Add(TEXT("EGSourcePreviewOwned"));
			Part->ComponentTags.Add(FName(*FString::Printf(TEXT("EGSourcePartIndex_%d"), PreviewParts.Num())));
			AddInstanceComponent(Part);
			Part->SetupAttachment(GetRootComponent());
			Part->SetRelativeTransform(Source.Transform);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetGenerateOverlapEvents(false);
			Part->SetCanEverAffectNavigation(false);
			Part->SetSkeletalMesh(Source.Mesh);
			Part->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Part->PrimaryComponentTick.bStartWithTickEnabled = false;
			Part->RegisterComponent();
			// Preserve the asset across component/animation-instance reinitialization.
			// SetAnimation alone updates the transient instance, not AnimationData.
			Part->OverrideAnimationData(Source.Animation, false, false, 0.f, 1.f);
			Part->SetAnimation(Source.Animation);
			Part->Stop();
			Part->SetComponentTickEnabled(false);
			for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
			{
				if (UMaterialInstanceDynamic* Material = Part->CreateDynamicMaterialInstance(Slot))
				{
					PreviewMaterials.Add(Material);
				}
			}
		}
		PreviewParts.Add(Part);
	}
	// Resolve after creating every part. Restrict parents to earlier entries so
	// evaluation order is deterministic and cycles cannot be constructed.
	PreviewAttachmentParents.Init(INDEX_NONE, Motion->Parts.Num());
	for (int32 Index = 0; Index < Motion->Parts.Num(); ++Index)
	{
		const FEGSourcePreviewPart& Source = Motion->Parts[Index];
		if (Source.ParentPartId.IsNone() && Source.AttachBone.IsNone()) continue;
		int32 ParentIndex = INDEX_NONE;
		int32 Matches = 0;
		for (int32 Other = 0; Other < Motion->Parts.Num(); ++Other)
			if (!Source.ParentPartId.IsNone() && Motion->Parts[Other].PartId == Source.ParentPartId)
			{ ParentIndex = Other; ++Matches; }
		USkeletalMeshComponent* Child = PreviewParts.IsValidIndex(Index) ? PreviewParts[Index].Get() : nullptr;
		USkeletalMeshComponent* Parent = PreviewParts.IsValidIndex(ParentIndex) ? PreviewParts[ParentIndex].Get() : nullptr;
		const bool bValid = Matches == 1 && ParentIndex < Index && IsValid(Child) && IsValid(Parent)
			&& PreviewAttachmentParents[ParentIndex] != -2 && !Source.Animation
			&& (Source.AttachBone.IsNone() || Parent->DoesSocketExist(Source.AttachBone));
		if (!bValid || !Child->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform, Source.AttachBone))
		{
			PreviewAttachmentParents[Index] = -2;
			if (IsValid(Child)) Child->SetVisibility(false);
			UE_LOG(LogTemp, Warning, TEXT("Invalid source preview attachment for part %s"), *Source.PartId.ToString());
			continue;
		}
		// Imported skeletons can carry 100x bone scale. The attachment's mesh
		// already has its own import scale, so never inherit the socket scale.
		Child->SetAbsolute(false, false, true);
		PreviewAttachmentParents[Index] = ParentIndex;
	}
	for (int32 Effect = 0; Effect < Motion->Effects.Num(); ++Effect)
	{
		if (const auto* Data = Motion->Effects[Effect].Effect.Get())
		{
			for (int32 Track = 0; Track < Data->Tracks.Num(); ++Track)
			{
				EffectTrackSources.Emplace(Effect, Track);
				PreviewEffects.Add(nullptr);
				PreviewEffectMaterials.Add(nullptr);
			}
		}
	}
}

void AEGSourceCharacterPreview::EvaluatePreview()
{
	if (BuiltDefinition != Definition || BuiltMotion != MotionIndex) RebuildPreview();
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	if (!Motion) return;
	for (int32 Index = 0; Index < Motion->Parts.Num(); ++Index)
	{
		USkeletalMeshComponent* Part = PreviewParts.IsValidIndex(Index) ? PreviewParts[Index] : nullptr;
		if (!IsValid(Part)) continue;
		const int32 ParentIndex = PreviewAttachmentParents.IsValidIndex(Index) ? PreviewAttachmentParents[Index] : INDEX_NONE;
		if (ParentIndex == -2) { Part->SetVisibility(false); continue; }
		if (Part->IsComponentTickEnabled()) Part->SetComponentTickEnabled(false);
		const FEGSourcePreviewPart& Source = Motion->Parts[Index];
		USkeletalMeshComponent* Parent = PreviewParts.IsValidIndex(ParentIndex) ? PreviewParts[ParentIndex].Get() : nullptr;
		if (ParentIndex >= 0 && !IsValid(Parent)) { Part->SetVisibility(false); continue; }
		const float LocalTime = PreviewTime - Source.StartTime;
		const float ClipLength = Source.Animation ? Source.Animation->GetPlayLength() : Motion->Duration;
		const float Window = Source.Duration > 0.f ? Source.Duration : ClipLength;
		const float Sample = Source.bLoop && ClipLength > SMALL_NUMBER
			? FMath::Fmod(FMath::Max(0.f, LocalTime), ClipLength) : FMath::Clamp(LocalTime, 0.f, ClipLength);
		const bool bAtMotionEnd = FMath::IsNearlyEqual(PreviewTime, Motion->Duration)
			&& FMath::IsNearlyEqual(Source.StartTime + Window, Motion->Duration);
		const bool bInside = LocalTime >= 0.f && (LocalTime < Window || bAtMotionEnd);
		Source.ApplyRenderBindings(Part,this,Sample);
		Part->SetVisibility(Source.bVisible && bInside && Source.EvaluateVisibility(Source.Visibility, LocalTime)
			&& (ParentIndex == INDEX_NONE || Parent->IsVisible()));
		if (Parent)
		{
			const FTransform Socket = Source.AttachBone.IsNone() ? Parent->GetComponentTransform()
				: Parent->GetSocketTransform(Source.AttachBone, RTS_World);
			const FVector ActorScale = GetActorScale3D();
			const FVector Position = Socket.TransformPositionNoScale(Source.Transform.GetTranslation() * ActorScale);
			const FQuat Rotation = Socket.GetRotation() * Source.Transform.GetRotation();
			Part->SetWorldTransform(FTransform(Rotation, Position, ActorScale * Source.Transform.GetScale3D()));
		}
		if (Source.Animation)
		{
			// This viewer owns these components and advances them with PreviewTime.
			// Registration or instance reinitialization can discard the transient
			// single-node asset, so repair only a missing or mismatched binding.
			if (Part->GetAnimationMode() != EAnimationMode::AnimationSingleNode)
			{
				Part->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			}
			if (!Part->GetSingleNodeInstance()) Part->InitAnim(false);
			UAnimSingleNodeInstance* SingleNode = Part->GetSingleNodeInstance();
			if (SingleNode && SingleNode->GetAnimationAsset() != Source.Animation)
			{
				Part->SetAnimation(Source.Animation);
			}
			if (SingleNode && SingleNode->IsPlaying()) Part->Stop();
			// Seeking is absolute and never fires gameplay notifies or extracts root motion onto the Actor.
			Part->SetPosition(Sample, false);
			Part->TickAnimation(0.f, false);
			Part->RefreshBoneTransforms();
			Part->UpdateChildTransforms(EUpdateTransformFlags::OnlyUpdateIfUsingSocket);
			Part->UpdateComponentToWorld();
			Part->UpdateBounds();
			Part->MarkRenderTransformDirty();
		}
		for (int32 Slot = 0; Slot < Part->GetNumMaterials(); ++Slot)
		{
			if (const auto* Curve = Source.SectionVisibility.Find(Slot); Curve && *Curve)
			{
				const bool bShow = Source.EvaluateVisibility(*Curve, Sample);
				for (int32 LOD = 0; LOD < Source.Mesh->GetLODNum(); ++LOD)
				{
					if (Part->IsMaterialSectionShown(Slot, LOD) != bShow)
						Part->ShowMaterialSection(Slot, INDEX_NONE, bShow, LOD);
				}
			}
			if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(Part->GetMaterial(Slot)))
			{
				for (const auto& Curve : Source.MaterialCurves)
				{
					if (Curve.Value) Material->SetScalarParameterValue(Curve.Key, Curve.Value->GetFloatValue(Sample));
				}
			}
		}
	}
	EvaluateEffects(*Motion);
}

void AEGSourceCharacterPreview::EvaluateEffects(const FEGSourcePreviewMotion& Motion)
{
	for (int32 Index = 0; Index < EffectTrackSources.Num(); ++Index)
	{
		const FIntPoint SourceIndex = EffectTrackSources[Index];
		if (!Motion.Effects.IsValidIndex(SourceIndex.X)) continue;
		const auto& Binding = Motion.Effects[SourceIndex.X];
		const auto* Data = Binding.Effect.Get();
		if (!Data || !Data->Tracks.IsValidIndex(SourceIndex.Y)) continue;
		const auto& Track = Data->Tracks[SourceIndex.Y];
		const float LocalTime = PreviewTime - Binding.StartTime;
		const float Age = LocalTime - Track.StartTime;
		bool bVisible = FMath::IsFinite(LocalTime) && Age >= 0.f && Age < Track.Duration
			&& (Binding.Duration <= 0.f || LocalTime < Binding.Duration)
			&& Track.Duration > SMALL_NUMBER && !Track.Samples.IsEmpty()
			&& FMath::IsFinite(Data->PresentationScale) && Data->PresentationScale > 0.f;
		FTransform Anchor = GetActorTransform();
		if (!Binding.AnchorPartId.IsNone())
		{
			bool bFound = false;
			for (int32 PartIndex = 0; PartIndex < Motion.Parts.Num(); ++PartIndex)
			{
				USkeletalMeshComponent* Part = PreviewParts.IsValidIndex(PartIndex) ? PreviewParts[PartIndex] : nullptr;
				if (Motion.Parts[PartIndex].PartId != Binding.AnchorPartId || !IsValid(Part)) continue;
				const auto& SourcePart = Motion.Parts[PartIndex];
				const float BirthTime = Binding.StartTime + Track.StartTime - SourcePart.StartTime;
				const float ClipLength = SourcePart.Animation ? SourcePart.Animation->GetPlayLength() : Motion.Duration;
				const float Window = SourcePart.Duration > 0.f ? SourcePart.Duration : ClipLength;
				if (Track.bWorldSimulation)
				{
					if (BirthTime < 0.f || BirthTime >= Window || !SourcePart.bVisible
						|| !SourcePart.EvaluateVisibility(SourcePart.Visibility, BirthTime)) continue;
				}
				else if (!Part->IsVisible()) continue;
				if (Track.bWorldSimulation)
				{
					if (const FTransform* Cached = EffectBirthAnchors.Find(Index))
					{
						Anchor = *Cached * GetActorTransform();
						bFound = true;
						break;
					}
				}
				Anchor = Part->GetComponentTransform();
				if (!Binding.AnchorBone.IsNone())
				{
					if (!Part->DoesSocketExist(Binding.AnchorBone)) continue;
					const float RestoreTime = Part->GetPosition();
					if (Track.bWorldSimulation && SourcePart.Animation)
					{
						const float SampleTime = SourcePart.bLoop && ClipLength > SMALL_NUMBER
							? FMath::Fmod(BirthTime, ClipLength) : FMath::Clamp(BirthTime, 0.f, ClipLength);
						Part->SetPosition(SampleTime, false);
						Part->TickAnimation(0.f, false);
						Part->RefreshBoneTransforms();
					}
					const FTransform Bone = Part->GetSocketTransform(Binding.AnchorBone);
					// Samples are already in centimetres. A source bone's 100x
					// metre conversion must not enlarge the effect a second time.
					Anchor.SetLocation(Bone.GetLocation());
					Anchor.SetRotation(Bone.GetRotation());
					if (Track.bWorldSimulation && SourcePart.Animation)
					{
						Part->SetPosition(RestoreTime, false);
						Part->TickAnimation(0.f, false);
						Part->RefreshBoneTransforms();
					}
				}
				if (Track.bWorldSimulation) EffectBirthAnchors.Add(Index, Anchor.GetRelativeTransform(GetActorTransform()));
				bFound = true;
				break;
			}
			bVisible &= bFound;
		}
		UStaticMeshComponent* Part = PreviewEffects[Index];
		if (!bVisible)
		{
			if (IsValid(Part)) Part->SetVisibility(false);
			continue;
		}
		if (!IsValid(Part))
		{
			auto* Mesh = Track.Mesh.LoadSynchronous();
			auto* Material = Track.Material.LoadSynchronous();
			if (!Mesh || !Material) continue;
			Part = NewObject<UStaticMeshComponent>(this, NAME_None, RF_Transient | RF_DuplicateTransient | RF_TextExportTransient);
			Part->ComponentTags.Add(TEXT("EGSourcePreviewOwnedFX"));
			AddInstanceComponent(Part);
			Part->SetupAttachment(GetRootComponent());
			Part->SetStaticMesh(Mesh);
			Part->SetRenderInMainPass(!Track.bSourceDepthOnly);
			Part->SetRenderInDepthPass(true);
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Part->SetGenerateOverlapEvents(false);
			Part->SetCanEverAffectNavigation(false);
			Part->SetCastShadow(false);
			Part->SetMaterial(0, Material);
			Part->RegisterComponent();
			PreviewEffects[Index] = Part;
			PreviewEffectMaterials[Index] = Part->CreateDynamicMaterialInstance(0);
			if(Track.bSourceDistortion)
			{
				Part->SetVisibleInSceneCaptureOnly(true);
				AEGSourceDistortionPass::RegisterSource(Part,Data->SourceDistortionComposite.LoadSynchronous());
			}
		}
		if (!Track.IsSourceVisible(Age) || !Track.ApplyNativeRendering(Part,this,Age)) { Part->SetVisibility(false); continue; }
		const float Frame = FMath::Clamp(Age / Track.Duration, 0.f, 1.f) * (Track.Samples.Num() - 1);
		const int32 Low = FMath::FloorToInt(Frame), High = FMath::Min(Low + 1, Track.Samples.Num() - 1);
		const float Alpha = Frame - Low;
		FTransform Sample;
		Sample.Blend(Track.Samples[Low].Transform, Track.Samples[High].Transform, Alpha);
		Anchor.SetScale3D(Anchor.GetScale3D() * Data->PresentationScale);
		const FTransform Result = Sample * Binding.Transform * Anchor;
		if (Result.ContainsNaN()) { Part->SetVisibility(false); continue; }
		Part->SetWorldTransform(Result);
		Part->SetVisibility(true);
		if (Track.bCameraFacing)
		{
			if (auto* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0))
				Part->SetWorldRotation(Track.SourceBillboardAlignment==INDEX_NONE
					? FRotationMatrix::MakeFromZ(Camera->GetCameraLocation()-Part->GetComponentLocation()).ToQuat()
					: EGSourceBillboardRotation(Sample.GetRotation(),Track.SourceBillboardAlignment==0
						? -Camera->GetCameraRotation().Vector():Camera->GetCameraLocation()-Part->GetComponentLocation(),Camera->GetCameraRotation().Quaternion().GetRightVector()));
		}
		if (UMaterialInstanceDynamic* Material = PreviewEffectMaterials[Index])
		{
			Material->SetVectorParameterValue(TEXT("SourceColor"), FMath::Lerp(Track.Samples[Low].Color, Track.Samples[High].Color, Alpha));
			Material->SetVectorParameterValue(TEXT("SourceParticleUV"), Track.Samples[Low].TextureUV);
			Material->SetVectorParameterValue(TEXT("SourceCustom1"), FMath::Lerp(Track.Samples[Low].CustomData1, Track.Samples[High].CustomData1, Alpha));
			Material->SetVectorParameterValue(TEXT("SourceCustom2"), FMath::Lerp(Track.Samples[Low].CustomData2, Track.Samples[High].CustomData2, Alpha));
			Material->SetScalarParameterValue(TEXT("SourceAge"), Age);
			Material->SetScalarParameterValue(TEXT("SourceTimeline"), LocalTime);
		}
	}
}

void AEGSourceCharacterPreview::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPreview();
	EvaluatePreview();
}

void AEGSourceCharacterPreview::BeginPlay()
{
	Super::BeginPlay();
	RebuildPreview();
	if (bAutoPlay) Play(); else EvaluatePreview();
}

void AEGSourceCharacterPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bPlaying = false;
	ClearPreview();
	Super::EndPlay(EndPlayReason);
}

void AEGSourceCharacterPreview::Play()
{
	EvaluatePreview();
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	bPlaying = Motion && Motion->Duration > SMALL_NUMBER;
}

void AEGSourceCharacterPreview::Pause() { bPlaying = false; }
void AEGSourceCharacterPreview::Stop() { bPlaying = false; SetPreviewTime(0.f); }
void AEGSourceCharacterPreview::SetPreviewTime(float Time)
{
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	PreviewTime = FMath::Clamp(FMath::IsFinite(Time) ? Time : 0.f, 0.f, Motion ? Motion->Duration : 0.f);
	EvaluatePreview();
}

void AEGSourceCharacterPreview::NextMotion()
{
	if (!Definition || Definition->Motions.IsEmpty()) return;
	MotionIndex = (FMath::Max(0, MotionIndex) + 1) % Definition->Motions.Num();
	SetPreviewTime(0.f);
}

void AEGSourceCharacterPreview::PreviousMotion()
{
	if (!Definition || Definition->Motions.IsEmpty()) return;
	MotionIndex = (FMath::Clamp(MotionIndex, 0, Definition->Motions.Num() - 1) + Definition->Motions.Num() - 1) % Definition->Motions.Num();
	SetPreviewTime(0.f);
}

void AEGSourceCharacterPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	if (bPlaying && Motion && Motion->Duration > SMALL_NUMBER && FMath::IsFinite(DeltaSeconds))
	{
		const float Advance = FMath::Max(0.f, DeltaSeconds) * FMath::Clamp(FMath::IsFinite(PlayRate) ? PlayRate : 1.f, .01f, 4.f);
		PreviewTime += Advance;
		if (PreviewTime >= Motion->Duration)
		{
			if (bLoop) PreviewTime = FMath::Fmod(PreviewTime, Motion->Duration);
			else { PreviewTime = Motion->Duration; bPlaying = false; }
		}
	}
	EvaluatePreview();
}

bool AEGSourceCharacterPreview::GetVisiblePartBoneLocation(FName PartId,FName Bone,FVector& Location) const
{
 const auto* Motion=CurrentMotion();if(!Motion)return false;
 for(int32 I=0;I<Motion->Parts.Num();++I)if(Motion->Parts[I].PartId==PartId&&PreviewParts.IsValidIndex(I))
 {
  const auto* P=PreviewParts[I].Get();if(P&&P->IsVisible()&&P->DoesSocketExist(Bone)){Location=P->GetSocketLocation(Bone);return true;}
 }
 return false;
}

bool AEGSourceCharacterPreview::GetVisiblePartBoneTransform(FName PartId,FName Bone,FTransform& Transform) const
{
 const auto* Motion=CurrentMotion();if(!Motion)return false;
 for(int32 I=0;I<Motion->Parts.Num();++I)if(Motion->Parts[I].PartId==PartId&&PreviewParts.IsValidIndex(I))
 {
  const auto* P=PreviewParts[I].Get();if(P&&P->IsVisible()&&P->DoesSocketExist(Bone)){Transform=P->GetSocketTransform(Bone);return true;}
 }
 return false;
}
