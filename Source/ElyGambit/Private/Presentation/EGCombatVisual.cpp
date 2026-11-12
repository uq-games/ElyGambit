#include "Presentation/EGCombatVisual.h"

#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Placement/EGPlacementVisuals.h"
#include "Data/EGHeroDefinition.h"
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

namespace
{
// Only known character, equipment and device parts are resized. Source effects
// may also use skeletal meshes (for example the human Herrscher's manta).
bool IsBoardModelPart(const FEGSourcePreviewPart& Part)
{
 const FString Id=Part.PartId.ToString();
 return Id.Equals(TEXT("Body"),ESearchCase::IgnoreCase)
  || Id.StartsWith(TEXT("Body_LV"),ESearchCase::IgnoreCase)
  || Id.StartsWith(TEXT("Hair_LV"),ESearchCase::IgnoreCase)
  || Id.Equals(TEXT("Bow"),ESearchCase::IgnoreCase)
  || Id.Equals(TEXT("IceBow"),ESearchCase::IgnoreCase)
  || Id.Equals(TEXT("Mace"),ESearchCase::IgnoreCase)
  || Id.Equals(TEXT("Gatling"),ESearchCase::IgnoreCase)
  || Id.Equals(TEXT("Cannon"),ESearchCase::IgnoreCase)
  || Id.StartsWith(TEXT("Signature_"),ESearchCase::IgnoreCase);
}
// Imported modular meshes may own distinct USkeleton assets while retaining
// the exact same reference hierarchy. LeaderPose maps that hierarchy by name;
// never infer compatibility merely from the asset name or bone count.
bool HasSameReferenceHierarchy(const USkeletalMesh* A, const USkeletalMesh* B)
{
	if (!A || !B) return false;
	const FReferenceSkeleton& Left = A->GetRefSkeleton();
	const FReferenceSkeleton& Right = B->GetRefSkeleton();
	if (Left.GetNum() != Right.GetNum()) return false;
	for (int32 Index = 0; Index < Left.GetNum(); ++Index)
	{
		if (Left.GetBoneName(Index) != Right.GetBoneName(Index)
			|| Left.GetParentIndex(Index) != Right.GetParentIndex(Index)) return false;
		const FTransform& ARef = Left.GetRefBonePose()[Index];
		const FTransform& BRef = Right.GetRefBonePose()[Index];
		constexpr float RefTolerance = .001f;
		if (!ARef.GetTranslation().Equals(BRef.GetTranslation(), RefTolerance)
			|| !ARef.GetRotation().Equals(BRef.GetRotation(), RefTolerance)
			|| !ARef.GetScale3D().Equals(BRef.GetScale3D(), RefTolerance)) return false;
	}
	return true;
}
}

AEGCombatVisual::AEGCombatVisual()
{
	PrimaryActorTick.bCanEverTick = true;
	bAllowTickBeforeBeginPlay = true;
	bReplicates = false;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
}

void AEGCombatVisual::SetBoardModelScale(float Scale,float GroundOffset)
{
 BoardModelScale=FMath::IsFinite(Scale)&&Scale>0.f?FMath::Min(Scale,1.5f):1.f;
 BoardGroundOffset=FMath::IsFinite(GroundOffset)?GroundOffset*(BoardModelScale-1.f):0.f;
}

const FEGSourcePreviewMotion* AEGCombatVisual::CurrentMotion() const
{
	return Definition && Definition->Motions.IsValidIndex(MotionIndex) ? &Definition->Motions[MotionIndex] : nullptr;
}

void AEGCombatVisual::ClearPreview()
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
	AttachmentParents.Reset();
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

void AEGCombatVisual::RebuildPreview()
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
		const bool bFollowsBody = BodyMesh && Source.Animation && BodyMesh->GetSkeletalMeshAsset()
			&& Source.Animation->GetSkeleton() == BodyMesh->GetSkeletalMeshAsset()->GetSkeleton()
			&& HasSameReferenceHierarchy(Source.Mesh, BodyMesh->GetSkeletalMeshAsset())
			&& Motion->Parts.ContainsByPredicate([&](const FEGSourcePreviewPart& BodySource)
			{
				return BodySource.PartId == TEXT("Body") && BodySource.Animation == Source.Animation
					&& FMath::IsNearlyEqual(BodySource.StartTime, Source.StartTime) && BodySource.bLoop == Source.bLoop;
			});
		if (Source.Mesh && (!Source.Animation || Source.Animation->GetSkeleton() == Source.Mesh->GetSkeleton() || bFollowsBody))
		{
			if(BodyMesh&&(Source.PartId==TEXT("Body")||Motion->Parts.Num()==1)&&BodyMesh->GetSkeletalMeshAsset()&&Source.Mesh->GetSkeleton()==BodyMesh->GetSkeletalMeshAsset()->GetSkeleton())
            {
             PreviewParts.Add(BodyMesh);continue;
            }
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
			Part->RegisterComponent();
			if(Source.AnimationBlueprintClass)Part->SetAnimInstanceClass(Source.AnimationBlueprintClass);
			else if(Source.Animation && !bFollowsBody)ensureMsgf(false,TEXT("Missing runtime AnimBP for %s"),*Source.Mesh->GetPathName());
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
	// Resolve static weapon attachments only after all components exist. Parents
	// must precede children so evaluation is deterministic and cycles fail closed.
	AttachmentParents.Init(INDEX_NONE, Motion->Parts.Num());
	for (int32 Index = 0; Index < Motion->Parts.Num(); ++Index)
	{
		const FEGSourcePreviewPart& Source = Motion->Parts[Index];
		if (Source.ParentPartId.IsNone() && Source.AttachBone.IsNone()) continue;
		int32 ParentIndex = INDEX_NONE, Matches = 0;
		for (int32 Other = 0; Other < Motion->Parts.Num(); ++Other)
			if (!Source.ParentPartId.IsNone() && Motion->Parts[Other].PartId == Source.ParentPartId)
			{ ParentIndex = Other; ++Matches; }
		USkeletalMeshComponent* Child = PreviewParts.IsValidIndex(Index) ? PreviewParts[Index].Get() : nullptr;
		USkeletalMeshComponent* Parent = PreviewParts.IsValidIndex(ParentIndex) ? PreviewParts[ParentIndex].Get() : nullptr;
		const bool bValid = Matches == 1 && ParentIndex < Index && IsValid(Child) && IsValid(Parent)
			&& Child != BodyMesh && AttachmentParents[ParentIndex] != -2 && !Source.Animation
			&& (Source.AttachBone.IsNone() || Parent->DoesSocketExist(Source.AttachBone));
		if (!bValid || !Child->AttachToComponent(Parent, FAttachmentTransformRules::KeepRelativeTransform, Source.AttachBone))
		{
			AttachmentParents[Index] = -2;
			if (IsValid(Child) && Child != BodyMesh) Child->SetVisibility(false);
			UE_LOG(LogTemp, Warning, TEXT("Invalid combat presentation attachment for part %s"), *Source.PartId.ToString());
			continue;
		}
		Child->SetAbsolute(false, false, true);
		AttachmentParents[Index] = ParentIndex;
	}
	const auto* Unit = Cast<AEGChessUnit>(GetOwner());
	const auto* Piece = Cast<AEGArtPiece>(GetOwner());
	const auto* Deployment = Cast<AEGDeployedPiece>(Piece);
	const auto* PieceDefinition = Piece ? Piece->HeroVisualDefinition.Get() : nullptr;
	const bool bVillVRig = Cast<AEGVillVEmplacement>(GetOwner())
		|| (Unit && Unit->GetDefinition() && Unit->GetDefinition()->HeroKind == EEGHeroKind::VillV)
		|| (PieceDefinition && (PieceDefinition->HeroKind == EEGHeroKind::VillV
			|| PieceDefinition->HeroId == TEXT("VillV_Gatling") || PieceDefinition->HeroId == TEXT("VillV_Cannon")))
		|| (Deployment && Deployment->PartRole != EEGDeploymentPart::Hero);
	for (int32 Effect = 0; !bVillVRig && Effect < Motion->Effects.Num(); ++Effect)
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

void AEGCombatVisual::EvaluatePreview()
{
	if (BuiltDefinition != Definition || BuiltMotion != MotionIndex) RebuildPreview();
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	if (!Motion) return;
 if(BodyMesh)BodyMesh->SetVisibility(false);
	for (int32 Index = 0; Index < Motion->Parts.Num(); ++Index)
	{
		USkeletalMeshComponent* Part = PreviewParts.IsValidIndex(Index) ? PreviewParts[Index] : nullptr;
		if (!IsValid(Part)) continue;
		const FEGSourcePreviewPart& Source = Motion->Parts[Index];
		const float PartBoardScale=(Part==BodyMesh||IsBoardModelPart(Source))?BoardModelScale:1.f;
		const int32 ParentIndex = AttachmentParents.IsValidIndex(Index) ? AttachmentParents[Index] : INDEX_NONE;
		if (ParentIndex == -2) { if (Part != BodyMesh) Part->SetVisibility(false); continue; }
		USkeletalMeshComponent* Parent = PreviewParts.IsValidIndex(ParentIndex) ? PreviewParts[ParentIndex].Get() : nullptr;
		if (ParentIndex >= 0 && !IsValid(Parent)) { Part->SetVisibility(false); continue; }
		const float LocalTime = SourceTime - Source.StartTime;
		const float ClipLength = Source.Animation ? Source.Animation->GetPlayLength() : Motion->Duration;
		const float Window = Source.Duration > 0.f ? Source.Duration : ClipLength;
		const float Sample = Source.bLoop && ClipLength > SMALL_NUMBER
			? FMath::Fmod(FMath::Max(0.f, LocalTime), ClipLength) : FMath::Clamp(LocalTime, 0.f, ClipLength);
		const bool bAtMotionEnd = FMath::IsNearlyEqual(SourceTime, Motion->Duration)
			&& FMath::IsNearlyEqual(Source.StartTime + Window, Motion->Duration);
		const bool bInside = LocalTime >= 0.f && (LocalTime < Window || bAtMotionEnd);
        if(Part==BodyMesh)
        {
         if(!bInside)continue;
         if(Part->GetSkeletalMeshAsset()!=Source.Mesh)
         {
          Part->SetSkeletalMesh(Source.Mesh,false);
          // Component overrides survive mesh changes. Use the assembly's materials
          // while it owns the body; EndPlay restores the saved idle overrides.
          Part->EmptyOverrideMaterials();
          for(int32 Slot=0;Slot<Source.Mesh->GetMaterials().Num();++Slot)
           if(auto* Material=Part->CreateDynamicMaterialInstance(Slot))
           {
            // Appearance installs the board's light direction on its MIDs.
            // A source mesh swap must keep that scene input while retaining
            // the replacement mesh's own textures and material parameters.
            FLinearColor Direction;
            if(OriginalMaterials.IsValidIndex(Slot)&&OriginalMaterials[Slot]&&
               OriginalMaterials[Slot]->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DirectionToKeyLight")),Direction))
             Material->SetVectorParameterValue(TEXT("DirectionToKeyLight"),Direction);
           }
         }
         FTransform ModelTransform=Source.Transform;
         ModelTransform.SetTranslation(ModelTransform.GetTranslation()*PartBoardScale);
         ModelTransform.SetScale3D(ModelTransform.GetScale3D()*PartBoardScale);
         FTransform BodyTransform=ModelTransform*GetActorTransform();
         if(PartBoardScale!=1.f)BodyTransform.AddToTranslation(FVector(0,0,BoardGroundOffset));
         if(const auto* Unit=Cast<AEGChessUnit>(GetOwner());Unit&&IEGCombatant::Execute_IsAlive(Unit))
          if(const auto* P=Unit->GetDefinition()?Unit->GetDefinition()->PresentationDefinition.Get():nullptr;P&&(P->HoverHeight>0||P->HoverAmplitude>0))
           BodyTransform.AddToTranslation(FVector(0,0,P->HoverHeight+P->HoverAmplitude*FMath::Sin(GetWorld()->GetTimeSeconds()*2.f*PI*P->HoverFrequency)));
         Part->SetWorldTransform(BodyTransform);
        }
		Source.ApplyRenderBindings(Part,this,Sample);
		Part->SetVisibility(Source.bVisible && bInside && Source.EvaluateVisibility(Source.Visibility, LocalTime)
			&& (!Parent || Parent->IsVisible()));
        if(Part!=BodyMesh&&!Parent)
        {
         FTransform ModelTransform=Source.Transform;
         ModelTransform.SetTranslation(ModelTransform.GetTranslation()*PartBoardScale);
         ModelTransform.SetScale3D(ModelTransform.GetScale3D()*PartBoardScale);
         FTransform ModelWorld=ModelTransform*GetActorTransform();
         if(PartBoardScale!=1.f)ModelWorld.AddToTranslation(FVector(0,0,BoardGroundOffset));
         Part->SetWorldTransform(ModelWorld);
        }
		if (Parent)
		{
			const FTransform Socket = Source.AttachBone.IsNone() ? Parent->GetComponentTransform()
				: Parent->GetSocketTransform(Source.AttachBone, RTS_World);
			const FVector VisualScale = GetActorScale3D()*PartBoardScale;
			const FVector Position = Socket.TransformPositionNoScale(Source.Transform.GetTranslation() * VisualScale);
			const FQuat Rotation = Socket.GetRotation() * Source.Transform.GetRotation();
			Part->SetWorldTransform(FTransform(Rotation, Position, VisualScale * Source.Transform.GetScale3D()));
		}
		if (Source.Animation)
		{
            if(Part==BodyMesh)
            {
             if(bDriveBodyPose)if(auto* I=Cast<UEGCombatAnimInstance>(Part->GetAnimInstance()))I->SetSourcePose(Source.Animation,Sample,BodyState);
            }
            else
            {
             bool bFollowBody=false;
             if(BodyMesh)for(int32 Other=0;Other<Motion->Parts.Num();++Other)
             {
              const auto& BodySource=Motion->Parts[Other];
              if(PreviewParts.IsValidIndex(Other)&&PreviewParts[Other]==BodyMesh&&BodySource.Animation==Source.Animation
               &&FMath::IsNearlyEqual(BodySource.StartTime,Source.StartTime)&&BodySource.bLoop==Source.bLoop)
               {bFollowBody=true;break;}
             }
             auto* Leader=bFollowBody?BodyMesh.Get():nullptr;
             if(Part->LeaderPoseComponent.Get()!=Leader)Part->SetLeaderPoseComponent(Leader,true);
             if(!bFollowBody)SamplePart(Part,Source.Animation,Sample);
            }
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
 if(BodyMesh&&bDriveBodyPose&&BodyState==3)
  if(const auto* Unit=Cast<AEGChessUnit>(GetOwner()))
   if(const auto* P=Unit->GetDefinition()?Unit->GetDefinition()->PresentationDefinition.Get():nullptr;P&&P->DeathVisualHideDelay>=0.f&&SourceTime>=P->DeathVisualHideDelay)
    BodyMesh->SetVisibility(false);
	EvaluateEffects(*Motion);
}

void AEGCombatVisual::EvaluateEffects(const FEGSourcePreviewMotion& Motion)
{
	for (int32 Index = 0; Index < EffectTrackSources.Num(); ++Index)
	{
		const FIntPoint SourceIndex = EffectTrackSources[Index];
		if (!Motion.Effects.IsValidIndex(SourceIndex.X)) continue;
		const auto& Binding = Motion.Effects[SourceIndex.X];
		const auto* Data = Binding.Effect.Get();
		if (!Data || !Data->Tracks.IsValidIndex(SourceIndex.Y)) continue;
		const auto& Track = Data->Tracks[SourceIndex.Y];
		const float LocalTime = SourceTime - Binding.StartTime;
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
                    FTransform Bone=Part->GetSocketTransform(Binding.AnchorBone);
                    if(Track.bWorldSimulation&&SourcePart.Animation)
                    {
                     const float SampleTime=SourcePart.bLoop&&ClipLength>SMALL_NUMBER?FMath::Fmod(BirthTime,ClipLength):FMath::Clamp(BirthTime,0.f,ClipLength);
                     const auto& Ref=SourcePart.Animation->GetSkeleton()->GetReferenceSkeleton();
                     int32 BoneIndex=Ref.FindBoneIndex(Binding.AnchorBone);FTransform Component=FTransform::Identity;
                     while(BoneIndex!=INDEX_NONE)
                     {
                      FTransform Local;SourcePart.Animation->GetBoneTransform(Local,FSkeletonPoseBoneIndex(BoneIndex),FAnimExtractContext(SampleTime,false),false);
                      Component=Component*Local;BoneIndex=Ref.GetParentIndex(BoneIndex);
                     }
                     Bone=Component*Part->GetComponentTransform();
                    }
                    Anchor.SetLocation(Bone.GetLocation());Anchor.SetRotation(Bone.GetRotation());
				}
                // The socket follows the resized body, but the source effect
                // keeps its previous world-space size and timeline.
                if(Part==BodyMesh||IsBoardModelPart(SourcePart))Anchor.SetScale3D(Anchor.GetScale3D()/BoardModelScale);
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

void AEGCombatVisual::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildPreview();
	EvaluatePreview();
}

void AEGCombatVisual::BeginPlay()
{
	Super::BeginPlay();
	RebuildPreview();
	if (bAutoPlay) Play(); else EvaluatePreview();
}

void AEGCombatVisual::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
 ReleaseBoundBody();
	ClearPreview();
	bPlaying = false;
	Super::EndPlay(EndPlayReason);
}

void AEGCombatVisual::ReleaseBoundBody(bool bRestore)
{
 USkeletalMeshComponent* Released=BodyMesh.Get();
 if(!Released)return;
 // Relinquish ownership before touching the body: destruction callbacks may
 // synchronously reenter this visual or assign that body to another owner.
 BodyMesh=nullptr;
 bPlaying=false;
 for(auto& Part:PreviewParts)if(Part==Released)Part=nullptr;
 if(IsValid(Released))
 {
  UEGNativeRendererLibrary::ReleaseSourceDefinition(Released,this);
  if(bRestore)
  {
  if(OriginalMesh&&Released->GetSkeletalMeshAsset()!=OriginalMesh)Released->SetSkeletalMesh(OriginalMesh,false);
  for(int32 Slot=0;Slot<OriginalMaterials.Num();++Slot)Released->SetMaterial(Slot,OriginalMaterials[Slot]);
  if(OriginalMesh)for(int32 LOD=0;LOD<OriginalMesh->GetLODNum();++LOD)for(int32 Slot=0;Slot<OriginalSectionSlotCount;++Slot)
  {
   const int32 Index=LOD*OriginalSectionSlotCount+Slot;
   if(OriginalSectionVisibility.IsValidIndex(Index))Released->ShowMaterialSection(Slot,INDEX_NONE,OriginalSectionVisibility[Index]!=0,LOD);
  }
  Released->SetRelativeTransform(OriginalBodyTransform);Released->SetVisibility(bOriginalVisibility);
  Released->SetRenderInMainPass(bOriginalRenderInMainPass);Released->SetRenderInDepthPass(bOriginalRenderInDepthPass);
  Released->SetTranslucentSortPriority(OriginalTranslucencySortPriority);
  for(const FName Bone:OriginalHiddenBones)Released->HideBoneByName(Bone,EPhysBodyOp::PBO_None);
  if(bDriveBodyPose)if(auto* I=Cast<UEGCombatAnimInstance>(Released->GetAnimInstance()))I->ResetSourcePose();
  }
 }
 OriginalMesh=nullptr;OriginalMaterials.Reset();OriginalHiddenBones.Reset();OriginalSectionVisibility.Reset();OriginalSectionSlotCount=0;
}

void AEGCombatVisual::Play()
{
	EvaluatePreview();
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	bPlaying = Motion && Motion->Duration > SMALL_NUMBER;
}

void AEGCombatVisual::Pause() { bPlaying = false; }
void AEGCombatVisual::Stop() { bPlaying = false; SetSourceTime(0.f); }
void AEGCombatVisual::SetSourceTime(float Time)
{
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	SourceTime = FMath::Clamp(FMath::IsFinite(Time) ? Time : 0.f, 0.f, Motion ? Motion->Duration : 0.f);
	EvaluatePreview();
}

void AEGCombatVisual::NextMotion()
{
	if (!Definition || Definition->Motions.IsEmpty()) return;
	MotionIndex = (FMath::Max(0, MotionIndex) + 1) % Definition->Motions.Num();
	SetSourceTime(0.f);
}

void AEGCombatVisual::PreviousMotion()
{
	if (!Definition || Definition->Motions.IsEmpty()) return;
	MotionIndex = (FMath::Clamp(MotionIndex, 0, Definition->Motions.Num() - 1) + Definition->Motions.Num() - 1) % Definition->Motions.Num();
	SetSourceTime(0.f);
}

void AEGCombatVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FEGSourcePreviewMotion* Motion = CurrentMotion();
	if (bPlaying && Motion && Motion->Duration > SMALL_NUMBER && FMath::IsFinite(DeltaSeconds))
	{
		const float Advance = FMath::Max(0.f, DeltaSeconds) * FMath::Clamp(FMath::IsFinite(PlayRate) ? PlayRate : 1.f, .01f, 4.f);
		SourceTime += Advance;
		if (SourceTime >= Motion->Duration)
		{
			if (bLoop) SourceTime = FMath::Fmod(SourceTime, Motion->Duration);
			else { SourceTime = Motion->Duration; bPlaying = false; }
		}
	}
	EvaluatePreview();
}

bool AEGCombatVisual::GetVisiblePartBoneLocation(FName PartId,FName Bone,FVector& Location) const
{
 const auto* Motion=CurrentMotion();if(!Motion)return false;
 for(int32 I=0;I<Motion->Parts.Num();++I)if(Motion->Parts[I].PartId==PartId&&PreviewParts.IsValidIndex(I))
 {
  const auto* P=PreviewParts[I].Get();if(P&&P->IsVisible()&&P->DoesSocketExist(Bone)){Location=P->GetSocketLocation(Bone);return true;}
 }
 return false;
}

void AEGCombatVisual::BindBody(USkeletalMeshComponent* Mesh,bool bSourcePose,int32 State)
{
 if(BodyMesh==Mesh)return;
 ReleaseBoundBody();
 BodyMesh=Mesh;bDriveBodyPose=bSourcePose;BodyState=State;
 if(!Mesh)return;
 OriginalMesh=Mesh->GetSkeletalMeshAsset();OriginalBodyTransform=Mesh->GetRelativeTransform();bOriginalVisibility=Mesh->IsVisible();
 bOriginalRenderInMainPass=Mesh->bRenderInMainPass;bOriginalRenderInDepthPass=Mesh->bRenderInDepthPass;OriginalTranslucencySortPriority=Mesh->TranslucencySortPriority;
 for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)OriginalMaterials.Add(Mesh->GetMaterial(Slot));
 OriginalSectionSlotCount=OriginalMesh?OriginalMesh->GetMaterials().Num():0;
 if(OriginalMesh)for(int32 LOD=0;LOD<OriginalMesh->GetLODNum();++LOD)for(int32 Slot=0;Slot<OriginalSectionSlotCount;++Slot)
  OriginalSectionVisibility.Add(Mesh->IsMaterialSectionShown(Slot,LOD)?1:0);
 if(OriginalMesh)for(const auto& Bone:OriginalMesh->GetRefSkeleton().GetRefBoneInfo())
  if(Mesh->IsBoneHiddenByName(Bone.Name)){OriginalHiddenBones.Add(Bone.Name);Mesh->UnHideBoneByName(Bone.Name);}
 for(int32 Slot=0;Slot<Mesh->GetNumMaterials();++Slot)Mesh->CreateDynamicMaterialInstance(Slot);
 // Only reuse a compatible main mesh. The remaining parts own independent, cosmetic graphs.
 RebuildPreview();EvaluatePreview();
}
void AEGCombatVisual::SamplePart(USkeletalMeshComponent* Part,UAnimSequence* Clip,float Time)
{
 if(auto* I=Cast<UEGCombatPartAnimInstance>(Part->GetAnimInstance()))
 {
  I->SetSourcePose(Clip,Time);Part->TickAnimation(0.f,false);Part->RefreshBoneTransforms();
 }
}
