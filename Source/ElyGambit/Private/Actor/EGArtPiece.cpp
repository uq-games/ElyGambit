#include "Actor/EGArtPiece.h"
#include "Presentation/EGHumanEntranceComponent.h"
#include "Presentation/EGCombatVisual.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGChessAnimInstance.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGWeaponVisuals.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Placement/EGPlacementVisuals.h"

#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Components/SceneComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "TimerManager.h"

AEGArtPiece::AEGArtPiece()
{
	PrimaryActorTick.bCanEverTick = false;
	ArtRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArtRoot"));
	SetRootComponent(ArtRoot);
	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(ArtRoot);
	Body->SetCollisionProfileName(TEXT("NoCollision"));
	Body->SetCastShadow(true);
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Outline = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Outline"));
	Outline->SetupAttachment(ArtRoot);
	Outline->SetCollisionProfileName(TEXT("NoCollision"));
	Outline->SetCastShadow(false);
	Outline->bUseAttachParentBound = true;
#if WITH_EDITOR
	Body->SetUpdateAnimationInEditor(true);
#endif
}

void AEGArtPiece::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	EnsureAppearance();
}

void AEGArtPiece::BeginPlay()
{
	Super::BeginPlay();
 if(IsA<AEGDeployedPiece>()||IsA<AEGPlacementPreview>())RefreshAppearance();
 else EnsureAppearance();
 SetBattlePresentation(bBattlePresentation);
}

void AEGArtPiece::PostRegisterAllComponents()
{
	Super::PostRegisterAllComponents();
	// Dynamic materials and single-node playback are transient; restore on editor map load too.
	if (!IsTemplate())
	{
		if (!FacePoseHandle.IsValid())
		{
			FacePoseHandle = Body->RegisterOnBoneTransformsFinalizedDelegate(
				FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this,
																			  &AEGArtPiece::UpdateFaceDirections));
		}
		EnsureAppearance();
	}
}

void AEGArtPiece::PostUnregisterAllComponents()
{
 bAppearanceApplied=false;
	if (Body && FacePoseHandle.IsValid())
	{
		Body->UnregisterOnBoneTransformsFinalizedDelegate(FacePoseHandle);
		FacePoseHandle.Reset();
	}
	Super::PostUnregisterAllComponents();
}

void AEGArtPiece::UpdateFaceDirections()
{
	FQuat PoseDelta = FQuat::Identity;
	const TArray<FTransform>& Pose = Body->GetComponentSpaceTransforms();
	if (Pose.IsValidIndex(FaceBoneIndex))
	{
		PoseDelta = Pose[FaceBoneIndex].GetRotation() * FaceReferenceInverse;
	}
	const FTransform& WorldTransform = Body->GetComponentTransform();
	// Imported FBXs face component +Y; +X is the source face field's right axis.
	const FVector Forward =
		WorldTransform.TransformVectorNoScale(PoseDelta.RotateVector(FVector::RightVector)).GetSafeNormal();
	const FVector Right =
		WorldTransform.TransformVectorNoScale(PoseDelta.RotateVector(FVector::ForwardVector)).GetSafeNormal();
	// Indices 0..7 are reserved for face axes by the visual actor's material contract.
	Body->SetCustomPrimitiveDataVector3(0, Forward);
	Body->SetCustomPrimitiveDataVector3(4, Right);
}

uint32 AEGArtPiece::AppearanceKey() const
{
 uint32 Key=HashCombine(GetTypeHash(HeroVisualDefinition),GetTypeHash(CharacterMesh));
 Key=HashCombine(Key,GetTypeHash(IdleAnimation));Key=HashCombine(Key,GetTypeHash(OutlineMaterial));
 Key=HashCombine(Key,GetTypeHash(DirectionToKeyLight));
 for(FName Bone:HiddenPresentationBones)Key=HashCombine(Key,GetTypeHash(Bone));
 return Key;
}
void AEGArtPiece::EnsureAppearance()
{
 if(!bAppearanceApplied||AppliedAppearanceKey!=AppearanceKey())RefreshAppearance();
}
void AEGArtPiece::RefreshAppearance()
{
 FEGBoardModelSizing::Forget(this);
 StopPreparationEntranceAudio();
 CancelHumanEntrance();
 ++AppearanceRevision;
 // A preparation assembly temporarily owns Body and restores its captured mesh,
 // materials and transform in EndPlay. Release that owner BEFORE installing the
 // next hero; otherwise destroying it later restores the previous drag's model.
 if(PreparationAssembly)
 {
  PreparationAssembly->Destroy();PreparationAssembly=nullptr;
  Body->SetVisibility(true);
 }
 if(!bHasMasterModelScale)
  BoardModelScale=HeroVisualDefinition&&(IsA<AEGDeployedPiece>()||IsA<AEGPlacementPreview>())?FEGBoardModelSizing::Resolve(HeroVisualDefinition,this):1.f;
 if(HeroVisualDefinition)
 {CharacterMesh=HeroVisualDefinition->GetMesh().LoadSynchronous();IdleAnimation=HeroVisualDefinition->GetIdleAnimation().LoadSynchronous();HiddenPresentationBones=HeroVisualDefinition->GetHiddenPresentationBones();Body->SetRelativeScale3D(HeroVisualDefinition->GetMeshScale()*BoardModelScale);Outline->SetRelativeScale3D(HeroVisualDefinition->GetMeshScale()*BoardModelScale);
  // Source rigs are not authored on a common origin; drop each mesh so its rest feet touch Z=0.
  const FVector GroundLocation(0.f,0.f,HeroVisualDefinition->GetPresentationGroundOffset()*BoardModelScale);
  Body->SetRelativeLocation(GroundLocation);Outline->SetRelativeLocation(GroundLocation);}
	const bool bUpdatePresentationVisibility =
		!AppliedHiddenPresentationBones.IsEmpty() || !HiddenPresentationBones.IsEmpty();
	// Undo only visibility owned by this appearance preset before changing meshes.
	for (const FName BoneName : AppliedHiddenPresentationBones)
	{
		Body->UnHideBoneByName(BoneName);
		Outline->UnHideBoneByName(BoneName);
	}
 // Component overrides outlive mesh swaps, including slots unused by the next
 // hero. Rebuild only this actor's materials instead of retaining the last hero.
 Body->EmptyOverrideMaterials();Outline->EmptyOverrideMaterials();
	Body->SetSkeletalMeshAsset(CharacterMesh);
	Outline->SetSkeletalMeshAsset(CharacterMesh);
	Outline->SetLeaderPoseComponent(Body, true, false);
	FaceBoneIndex = INDEX_NONE;
	FaceReferenceInverse = FQuat::Identity;
	if (CharacterMesh)
	{
		const FReferenceSkeleton& Ref = CharacterMesh->GetRefSkeleton();
		for (int32 Index = 0; Index < Ref.GetNum(); ++Index)
		{
			if (Ref.GetBoneName(Index).ToString().EndsWith(TEXT("Head"), ESearchCase::IgnoreCase))
			{
				FaceBoneIndex = Index;
				FQuat Reference = FQuat::Identity;
				for (int32 Parent = Index; Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
				{
					Reference = Ref.GetRefBonePose()[Parent].GetRotation() * Reference;
				}
				FaceReferenceInverse = Reference.Inverse();
				break;
			}
		}
	}
	const bool bConfigured = HeroVisualDefinition && CharacterMesh
        && !FParse::Param(FCommandLine::Get(), TEXT("NoCrossfade"))
        && FEGAnimationPlayback::Configure(Body, HeroVisualDefinition);
    if (!bConfigured)
    {
        Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
        if (IdleAnimation && CharacterMesh && IdleAnimation->GetSkeleton() == CharacterMesh->GetSkeleton())
            Body->PlayAnimation(IdleAnimation, true);
        else Body->SetAnimation(nullptr);
    }
	for (int32 Index = 0; Index < Body->GetNumMaterials(); ++Index)
	{
		UMaterialInterface* ParentMaterial = CharacterMesh && CharacterMesh->GetMaterials().IsValidIndex(Index)
												 ? CharacterMesh->GetMaterials()[Index].MaterialInterface.Get()
												 : nullptr;
		if (UMaterialInstanceDynamic* Material =
				ParentMaterial ? Body->CreateDynamicMaterialInstance(Index, ParentMaterial) : nullptr)
		{
			Material->SetVectorParameterValue(TEXT("DirectionToKeyLight"),
											  FLinearColor(DirectionToKeyLight.GetSafeNormal()));
		}
		if (OutlineMaterial && ParentMaterial)
		{
			if (UMaterialInstanceDynamic* Border = Outline->CreateDynamicMaterialInstance(Index, OutlineMaterial))
			{
				// Preserve each section's cutout and default visibility in the outline pass.
				Border->CopyMaterialUniformParameters(ParentMaterial);
			}
		}
	}
	Outline->SetVisibility(CharacterMesh != nullptr && OutlineMaterial != nullptr);
	// Skip invisible source props and zero-width outline sections before submitting draws.
	// Geometry and source material slots stay in the asset for reversible presentation changes.
	if (CharacterMesh)
	{
		if (const FSkeletalMeshRenderData* RenderData = CharacterMesh->GetResourceForRendering())
		{
			for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
			{
				const auto& Sections = RenderData->LODRenderData[LODIndex].RenderSections;
				for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
				{
					const int32 MaterialIndex = Sections[SectionIndex].MaterialIndex;
					UMaterialInterface* Material = Body->GetMaterial(MaterialIndex);
					float Opacity = 1.0f;
					float Width = 0.0f;
					if (Material)
					{
						Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Opacity")), Opacity);
						Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineScale")), Width);
					}
					const bool bVisible = Material && Opacity > KINDA_SMALL_NUMBER;
					Body->ShowMaterialSection(MaterialIndex, SectionIndex, bVisible, LODIndex);
					Outline->ShowMaterialSection(MaterialIndex, SectionIndex, bVisible && Width > KINDA_SMALL_NUMBER,
												 LODIndex);
				}
			}
		}
	}
	for (const FName BoneName : HiddenPresentationBones)
	{
		if (Body->GetBoneIndex(BoneName) != INDEX_NONE)
		{
			Body->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
			Outline->HideBoneByName(BoneName, EPhysBodyOp::PBO_None);
		}
	}
	AppliedHiddenPresentationBones = HiddenPresentationBones;
	if (bUpdatePresentationVisibility && Body->IsRegistered())
	{
		// Publish visibility even for a reference-pose preview with editor ticking off.
		Body->RefreshBoneTransforms();
		Outline->MarkRenderDynamicDataDirty();
	}
	UpdateFaceDirections();

 if(HeroVisualDefinition)EGApplyWeaponVisuals(this,Body,HeroVisualDefinition,BoardModelScale);
 if(HasActorBegunPlay())SetBattlePresentation(bBattlePresentation);
 AppliedAppearanceKey=AppearanceKey();bAppearanceApplied=true;
}

void AEGArtPiece::ApplyHeroDefinition(UEGHeroDefinition* D)
{
 if(!D)return;HeroVisualDefinition=D;EnsureAppearance();
}

void AEGArtPiece::SetMasterModelScale(float Scale)
{
 if(!IsValid(HeroVisualDefinition)||!Body||!Outline)return;
 const float NewScale=FMath::IsFinite(Scale)&&Scale>0.f?FMath::Min(Scale,1.5f):1.f;
 bHasMasterModelScale=true;
 if(FMath::IsNearlyEqual(BoardModelScale,NewScale))return;
 BoardModelScale=NewScale;
 const float GroundOffset=HeroVisualDefinition->GetPresentationGroundOffset();
 Outline->SetRelativeScale3D(HeroVisualDefinition->GetMeshScale()*NewScale);
 Outline->SetRelativeLocation(FVector(0.f,0.f,GroundOffset*NewScale));
 if(IsValid(PreparationAssembly))
 {
  PreparationAssembly->SetBoardModelScale(NewScale,GroundOffset);
  PreparationAssembly->SetSourceTime(PreparationAssembly->SourceTime);
 }
 else
 {
  Body->SetRelativeScale3D(HeroVisualDefinition->GetMeshScale()*NewScale);
  Body->SetRelativeLocation(FVector(0.f,0.f,GroundOffset*NewScale));
 }
}

void AEGArtPiece::SetBattlePresentation(bool bBattle)
{
 if(bBattle)StopPreparationEntranceAudio();
 if(bBattle)CancelHumanEntrance();
 const auto* P=HeroVisualDefinition?HeroVisualDefinition->PresentationDefinition.Get():nullptr;
 auto* Desired=!bBattle&&P?(!P->PreparationIdleAssembly.IsNull()?P->PreparationIdleAssembly.Get():!P->IdleAssemblies.IsEmpty()?P->IdleAssemblies[0].Get():nullptr):nullptr;
 if(bBattlePresentation==bBattle&&((bBattle&&!PreparationAssembly)||
    (!bBattle&&PreparationAssembly&&PreparationAssembly->Definition==Desired&&PreparationAssembly->bLoop)))return;
 if(GetWorld())GetWorldTimerManager().ClearTimer(PreparationEntranceTimer);
 bBattlePresentation=bBattle;
 if(PreparationAssembly)
 {
  PreparationAssembly->Destroy();PreparationAssembly=nullptr;Body->SetVisibility(true);
  // ShowPreparationAssembly hides the outline while its source rig owns the
  // body. Restore the normal appearance before falling back to the base mesh
  // or entering combat; another preparation rig will hide it again below.
  Outline->SetVisibility(CharacterMesh != nullptr && OutlineMaterial != nullptr);
 }
 if(HeroVisualDefinition&&GetWorld()&&GetWorld()->IsGameWorld())FEGAnimationPlayback::Configure(Body,HeroVisualDefinition);
 if(bBattle||!GetWorld()||GetWorld()->bIsTearingDown||!GetWorld()->IsGameWorld()||!P)return;
 auto* D=!P->PreparationIdleAssembly.IsNull()?P->PreparationIdleAssembly.LoadSynchronous():!P->IdleAssemblies.IsEmpty()?P->IdleAssemblies[0].LoadSynchronous():nullptr;
 if(!D||D->Motions.IsEmpty())return;
 ShowPreparationAssembly(D,true);
}
void AEGArtPiece::ShowPreparationAssembly(UEGSourcePreviewDefinition* D,bool bLoop)
{
 const auto* P=HeroVisualDefinition?HeroVisualDefinition->PresentationDefinition.Get():nullptr;
 if(!P||!D||D->Motions.IsEmpty()||!GetWorld()||GetWorld()->bIsTearingDown)return;
 if(PreparationAssembly){PreparationAssembly->Destroy();PreparationAssembly=nullptr;}
 PreparationAssembly=GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,this,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!PreparationAssembly)
 {
  // A failed replacement must not leave the base body without its outline.
  Outline->SetVisibility(CharacterMesh != nullptr && OutlineMaterial != nullptr);
  return;
 }
 PreparationAssembly->Definition=D;PreparationAssembly->bAutoPlay=true;PreparationAssembly->bLoop=bLoop;
 PreparationAssembly->FinishSpawning(FTransform::Identity);
 PreparationAssembly->AttachToComponent(ArtRoot,FAttachmentTransformRules::KeepRelativeTransform);
 PreparationAssembly->SetActorRelativeTransform(FTransform(FRotator::ZeroRotator,FVector(0,0,P->PresentationGroundOffset),P->MeshScale));
 PreparationAssembly->SetBoardModelScale(BoardModelScale,P->PresentationGroundOffset);
 PreparationAssembly->BindBody(Body,true);PreparationAssembly->SetSourceTime(0);Outline->SetVisibility(false);
 PreparationAssembly->SetActorHiddenInGame(IsHidden());
}
void AEGArtPiece::PlayPreparationEntrance(UEGSourcePreviewDefinition* Entrance,USoundBase* Voice,bool bDeferVoice)
{
 if(bBattlePresentation||!Entrance||Entrance->Motions.IsEmpty()||!GetWorld()||GetWorld()->bIsTearingDown||!GetWorld()->IsGameWorld())return;
 StopPreparationEntranceAudio();
 CancelHumanEntrance();
#if WITH_DEV_AUTOMATION_TESTS
 PreparationVoicePlayCallsForTests=0;
#endif
 GetWorldTimerManager().ClearTimer(PreparationEntranceTimer);
 ShowPreparationAssembly(Entrance,false);
 if(!PreparationAssembly)return;
 if(Voice)
 {
  PreparationEntranceAudio=NewObject<UAudioComponent>(this);
  if(PreparationEntranceAudio)
  {
   AddInstanceComponent(PreparationEntranceAudio);
   PreparationEntranceAudio->bAutoActivate=false;
   PreparationEntranceAudio->bAutoDestroy=false;
   PreparationEntranceAudio->bAllowSpatialization=false;
   PreparationEntranceAudio->SetSound(Voice);
   PreparationEntranceAudio->RegisterComponent();
   if(bDeferVoice)bPreparationEntranceVoicePending=true;
   else
   {
#if WITH_DEV_AUTOMATION_TESTS
    ++PreparationVoicePlayCallsForTests;
#endif
    PreparationEntranceAudio->Play();
   }
  }
 }
 GetWorldTimerManager().SetTimer(PreparationEntranceTimer,this,&ThisClass::FinishPreparationEntrance,FMath::Max(.01f,Entrance->Motions[0].Duration),false);
}
void AEGArtPiece::StartDeferredPreparationEntranceVoice()
{
 if(!bPreparationEntranceVoicePending)return;
 bPreparationEntranceVoicePending=false;
 if(!IsValid(PreparationEntranceAudio)||bBattlePresentation||IsActorBeingDestroyed()||IsHidden()||!GetWorld()||GetWorld()->bIsTearingDown)return;
#if WITH_DEV_AUTOMATION_TESTS
 ++PreparationVoicePlayCallsForTests;
#endif
 PreparationEntranceAudio->Play();
}
void AEGArtPiece::DiscardDeferredPreparationEntranceVoice()
{
 bPreparationEntranceVoicePending=false;
}
void AEGArtPiece::StopPreparationEntranceAudio()
{
 bPreparationEntranceVoicePending=false;
 if(PreparationEntranceAudio)
 {
  PreparationEntranceAudio->Stop();
  PreparationEntranceAudio->DestroyComponent();
  PreparationEntranceAudio=nullptr;
 }
}
void AEGArtPiece::FinishPreparationEntrance()
{
 if(!bBattlePresentation&&!IsActorBeingDestroyed())SetBattlePresentation(false);
}
void AEGArtPiece::SetActorHiddenInGame(bool bNewHidden)
{
 if(bNewHidden)
 {
  if(bHumanEntranceSuppressed||FindComponentByClass<UEGHumanEntranceComponent>())StopPreparationEntranceAudio();
  CancelHumanEntrance();
 }
 Super::SetActorHiddenInGame(bNewHidden);
 if(PreparationAssembly)PreparationAssembly->SetActorHiddenInGame(bNewHidden);
}
void AEGArtPiece::EndPlay(const EEndPlayReason::Type Reason)
{
 StopPreparationEntranceAudio();
 CancelHumanEntrance();
 if(GetWorld())GetWorldTimerManager().ClearTimer(PreparationEntranceTimer);
 if(PreparationAssembly){PreparationAssembly->Destroy();PreparationAssembly=nullptr;}
 Super::EndPlay(Reason);
}

void AEGArtPiece::CancelHumanEntrance()
{
 if(auto* Effect=FindComponentByClass<UEGHumanEntranceComponent>())Effect->Cancel();
 else if(bHumanEntranceSuppressed)SetHumanEntranceSuppressed(false);
}

void AEGArtPiece::SetHumanEntranceSuppressed(bool bSuppressed)
{
 if(bHumanEntranceSuppressed==bSuppressed)return;
 bHumanEntranceSuppressed=bSuppressed;
 if(bSuppressed)
 {
  TArray<USceneComponent*> Components;
  if(Body){Body->GetChildrenComponents(true,Components);Components.AddUnique(Body);}
  if(Outline)Components.AddUnique(Outline);
  for(USceneComponent* Component:Components)
  {
   if(!IsValid(Component))continue;
   HumanEntranceHiddenComponents.Emplace(Component,Component->bHiddenInGame != 0);
   Component->SetHiddenInGame(true,false);
  }
  if(IsValid(PreparationAssembly))
  {
   HumanEntranceHiddenAssembly=PreparationAssembly;
   bHumanEntranceAssemblyWasHidden=PreparationAssembly->IsHidden();
   bHumanEntranceAssemblyWasPlaying=PreparationAssembly->bPlaying;
   if(bHumanEntranceAssemblyWasPlaying)PreparationAssembly->Pause();
   PreparationAssembly->SetActorHiddenInGame(true);
  }
  if(GetWorld()&&GetWorldTimerManager().IsTimerActive(PreparationEntranceTimer))
  {
   GetWorldTimerManager().PauseTimer(PreparationEntranceTimer);
   bHumanEntranceTimerPaused=true;
  }
  if(IsValid(PreparationEntranceAudio)&&PreparationEntranceAudio->IsPlaying())
  {
   PreparationEntranceAudio->SetPaused(true);
   bHumanEntranceAudioPaused=true;
  }
 }
 else
 {
  for(const auto& Entry:HumanEntranceHiddenComponents)
   if(Entry.Key.IsValid())Entry.Key->SetHiddenInGame(Entry.Value,false);
  HumanEntranceHiddenComponents.Reset();
  if(HumanEntranceHiddenAssembly.IsValid())
  {
   HumanEntranceHiddenAssembly->SetActorHiddenInGame(bHumanEntranceAssemblyWasHidden||IsHidden());
   if(bHumanEntranceAssemblyWasPlaying)HumanEntranceHiddenAssembly->Play();
  }
  HumanEntranceHiddenAssembly.Reset();
  bHumanEntranceAssemblyWasPlaying=false;
  if(bHumanEntranceTimerPaused&&GetWorld())GetWorldTimerManager().UnPauseTimer(PreparationEntranceTimer);
  bHumanEntranceTimerPaused=false;
  if(bHumanEntranceAudioPaused&&IsValid(PreparationEntranceAudio))PreparationEntranceAudio->SetPaused(false);
  bHumanEntranceAudioPaused=false;
 }
}
