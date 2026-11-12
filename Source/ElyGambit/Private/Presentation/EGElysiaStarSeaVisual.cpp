#include "Presentation/EGElysiaStarSeaVisual.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "MaterialShared.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGBoardScene.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

AEGElysiaStarSeaVisual::AEGElysiaStarSeaVisual()
{
	PrimaryActorTick.bCanEverTick = true;
	// The entry action can reveal the domain while combat world time is paused.
	PrimaryActorTick.bTickEvenWhenPaused = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	SetActorEnableCollision(false);

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	DomainEffect = CreateDefaultSubobject<UNiagaraComponent>(TEXT("DomainEffect"));
	DomainEffect->SetupAttachment(SceneRoot);
	DomainEffect->SetAutoActivate(false);
	DomainEffect->SetForceSolo(true);
	DomainEffect->PrimaryComponentTick.bTickEvenWhenPaused = true;
	DomainSurface = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DomainSurface"));
	BorderMeshes.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderWest")));
	BorderMeshes.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderEast")));
	BorderMeshes.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderSouth")));
	BorderMeshes.Add(CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BorderNorth")));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
	DomainSurface->SetupAttachment(SceneRoot);
	DomainSurface->SetStaticMesh(Plane.Object);
	for (UStaticMeshComponent* Border : BorderMeshes)
	{
		Border->SetupAttachment(SceneRoot);
		Border->SetStaticMesh(Cube.Object);
	}

	TArray<UStaticMeshComponent*> Meshes;
	Meshes.Add(DomainSurface);
	for (UStaticMeshComponent* Border : BorderMeshes)
	{
		Meshes.Add(Border);
	}
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->SetVisibility(false);
		Mesh->SetTranslucentSortPriority(-1);
	}
}

bool AEGElysiaStarSeaVisual::InitializeFromBoard(AEGDeploymentBoard* Board)
{
	if (!GetWorld() || !IsValid(Board) || Board->IsActorBeingDestroyed() || Board->GetWorld() != GetWorld() ||
		GetNetMode() == NM_DedicatedServer ||
		Board->Columns < 1 || Board->Rows < 1 || !FMath::IsFinite(Board->CellWidth) ||
		!FMath::IsFinite(Board->CellDepth) || Board->CellWidth <= 0.f || Board->CellDepth <= 0.f ||
		Board->GetActorTransform().ContainsNaN() || (!DomainSystem && !DomainMaterial && !BorderMaterial))
	{
		return false;
	}

	const float Width = Board->Columns * Board->CellWidth;
	const float Depth = Board->Rows * Board->CellDepth;
	if (DomainSystem && (DomainSystemTransform.ContainsNaN() || !FMath::IsFinite(DomainReferenceSize.X) ||
		!FMath::IsFinite(DomainReferenceSize.Y) || DomainReferenceSize.X <= 0.f || DomainReferenceSize.Y <= 0.f))
	{
		return false;
	}
	const FVector BoardScale = Board->GetActorScale3D();
	if (!FMath::IsFinite(Width) || !FMath::IsFinite(Depth) ||
		!FMath::IsFinite(FloorOffset) || !FMath::IsFinite(BorderHeight) || !FMath::IsFinite(BorderThickness) ||
		FMath::IsNearlyZero(BoardScale.X) || FMath::IsNearlyZero(BoardScale.Y) || FMath::IsNearlyZero(BoardScale.Z))
	{
		return false;
	}

	const auto IsFadeMaterial = [](UMaterialInterface* Material)
	{
		if (!Material)
		{
			return false;
		}
		float DefaultOpacity = 0.f;
		const bool bCompatible = IsTranslucentBlendMode(*Material) &&
			Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DomainOpacity")), DefaultOpacity);
		if (!bCompatible)
		{
			UE_LOG(LogTemp, Warning, TEXT("Star sea material %s must be translucent and expose DomainOpacity."), *GetNameSafe(Material));
		}
		return bCompatible;
	};
	const bool bHasDomainMaterial = IsFadeMaterial(DomainMaterial);
	const bool bHasBorderMaterial = IsFadeMaterial(BorderMaterial);
	if (!DomainSystem && !bHasDomainMaterial && !bHasBorderMaterial)
	{
		return false;
	}

	if (AEGDeploymentBoard* PreviousBoard = SourceBoard.Get())
	{
		PreviousBoard->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleBoardDestroyed);
		PreviousBoard->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleBoardEndPlay);
	}
	SourceBoard = Board;
	Board->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleBoardDestroyed);
	Board->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleBoardEndPlay);

	// The layout's transform defines both grid axes and its origin. Attaching to
	// the board keeps the domain aligned if the board itself moves, not the caster.
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorTransform(Board->GetActorTransform());
	AttachToActor(Board, FAttachmentTransformRules::KeepWorldTransform);

	const float Height = FMath::Max(0.f, BorderHeight);
	const float Thickness = FMath::Max(0.f, BorderThickness);
	const float WallZ = FloorOffset + Height * 0.5f;
	DomainSurface->SetRelativeLocation(FVector(0.f, 0.f, FloorOffset));
	DomainSurface->SetRelativeScale3D(FVector(Width / 100.f, Depth / 100.f, 1.f));
	BorderMeshes[0]->SetRelativeLocation(FVector(-Width * 0.5f, 0.f, WallZ));
	BorderMeshes[1]->SetRelativeLocation(FVector(Width * 0.5f, 0.f, WallZ));
	BorderMeshes[2]->SetRelativeLocation(FVector(0.f, -Depth * 0.5f, WallZ));
	BorderMeshes[3]->SetRelativeLocation(FVector(0.f, Depth * 0.5f, WallZ));
	BorderMeshes[0]->SetRelativeScale3D(FVector(Thickness / 100.f, Depth / 100.f, Height / 100.f));
	BorderMeshes[1]->SetRelativeScale3D(FVector(Thickness / 100.f, Depth / 100.f, Height / 100.f));
	BorderMeshes[2]->SetRelativeScale3D(FVector(Width / 100.f, Thickness / 100.f, Height / 100.f));
	BorderMeshes[3]->SetRelativeScale3D(FVector(Width / 100.f, Thickness / 100.f, Height / 100.f));

	DomainMID = bHasDomainMaterial ? UMaterialInstanceDynamic::Create(DomainMaterial, this) : nullptr;
	BorderMID = bHasBorderMaterial ? UMaterialInstanceDynamic::Create(BorderMaterial, this) : nullptr;
	if (!DomainSystem && !DomainMID && !BorderMID)
	{
		return false;
	}
	if (DomainMID)
	{
		DomainMID->SetVectorParameterValue(TEXT("DomainTint"), DomainTint);
		DomainMID->SetScalarParameterValue(TEXT("DomainEmissive"), DomainEmissive);
		DomainMID->SetScalarParameterValue(TEXT("GridColumns"), float(Board->Columns));
		DomainMID->SetScalarParameterValue(TEXT("GridRows"), float(Board->Rows));
		DomainSurface->SetMaterial(0, DomainMID);
	}
	DomainSurface->SetVisibility(DomainMID != nullptr);
	// Overlay the scene's explicitly wired decorative ground, without changing
	// its shared material. Each sea owns and releases its own geometry/MID.
	if (IsValid(OuterBoardSurface)) OuterBoardSurface->DestroyComponent();
	OuterBoardSurface=nullptr;OuterBoardMID=nullptr;
	if (IsFadeMaterial(OuterBoardMaterial))
	{
		for (TActorIterator<AEGBoardSceneSettings> It(GetWorld()); It; ++It)
		{
			if (It->Board!=Board || !IsValid(It->FramingGround)) continue;
			auto* Ground=It->FramingGround->FindComponentByClass<UStaticMeshComponent>();
			if (!IsValid(Ground) || !Ground->GetStaticMesh() || Ground->GetComponentTransform().ContainsNaN()) continue;
			OuterBoardMID=UMaterialInstanceDynamic::Create(OuterBoardMaterial,this);
			OuterBoardSurface=NewObject<UStaticMeshComponent>(this,TEXT("OuterBoardSurface"),RF_Transient);
			AddInstanceComponent(OuterBoardSurface);OuterBoardSurface->SetupAttachment(SceneRoot);
			OuterBoardSurface->SetMobility(EComponentMobility::Movable);
			OuterBoardSurface->SetStaticMesh(Ground->GetStaticMesh());
			OuterBoardSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			OuterBoardSurface->SetGenerateOverlapEvents(false);OuterBoardSurface->SetCanEverAffectNavigation(false);
			OuterBoardSurface->SetCastShadow(false);OuterBoardSurface->SetTranslucentSortPriority(-2);
			FTransform Overlay=Ground->GetComponentTransform();
			Overlay.AddToTranslation(Board->GetActorTransform().GetUnitAxis(EAxis::Z)*.5f);
			OuterBoardSurface->SetWorldTransform(Overlay);
			for(int32 Slot=0;Slot<Ground->GetNumMaterials();++Slot)OuterBoardSurface->SetMaterial(Slot,OuterBoardMID);
			OuterBoardMID->SetVectorParameterValue(TEXT("DomainTint"),DomainTint);
			OuterBoardMID->SetScalarParameterValue(TEXT("DomainEmissive"),DomainEmissive);
			OuterBoardMID->SetVectorParameterValue(TEXT("WorldOrigin"),FLinearColor(Board->GetActorLocation()));
			OuterBoardMID->SetVectorParameterValue(TEXT("WorldAxisX"),FLinearColor(Board->GetActorTransform().GetUnitAxis(EAxis::X)));
			OuterBoardMID->SetVectorParameterValue(TEXT("WorldAxisY"),FLinearColor(Board->GetActorTransform().GetUnitAxis(EAxis::Y)));
			OuterBoardMID->SetScalarParameterValue(TEXT("WorldWidth"),Width*FMath::Abs(BoardScale.X));
			OuterBoardMID->SetScalarParameterValue(TEXT("WorldDepth"),Depth*FMath::Abs(BoardScale.Y));
			OuterBoardSurface->RegisterComponent();
			break;
		}
	}
	if (BorderMID)
	{
		BorderMID->SetVectorParameterValue(TEXT("DomainTint"), BorderTint);
		BorderMID->SetScalarParameterValue(TEXT("DomainEmissive"), BorderEmissive);
	}
	for (UStaticMeshComponent* Border : BorderMeshes)
	{
		if (BorderMID)
		{
			Border->SetMaterial(0, BorderMID);
		}
		Border->SetVisibility(!DomainSystem && BorderMID != nullptr && Height > 0.f && Thickness > 0.f);
	}
	if (DomainSystem)
	{
		FTransform EffectTransform = DomainSystemTransform;
		EffectTransform.SetLocation(EffectTransform.GetLocation() + FVector(0.f, 0.f, FloorOffset));
		EffectTransform.SetScale3D(EffectTransform.GetScale3D() * FVector(Width / DomainReferenceSize.X, Depth / DomainReferenceSize.Y, 1.f));
		DomainEffect->SetRelativeTransform(EffectTransform);
		DomainEffect->SetAsset(DomainSystem);
		DomainEffect->Activate(true);
	}

	bInitialized = true;
	bDismissing = false;
	CurrentOpacity = 0.f;
	ApplyOpacity();
	SetActorTickEnabled(false);
	return true;
}

void AEGElysiaStarSeaVisual::BeginReveal()
{
	if (!bInitialized)
	{
		return;
	}
	bDismissing = false;
	StartFade(1.f, RevealSeconds);
}

void AEGElysiaStarSeaVisual::BeginDismiss()
{
	if (bDismissing)
	{
		return;
	}
	bDismissing = true;
	if (DomainSystem && IsValid(DomainEffect)) DomainEffect->Deactivate();
	if (!bInitialized)
	{
		Destroy();
		return;
	}
	StartFade(0.f, DismissSeconds);
}

void AEGElysiaStarSeaVisual::StartFade(float TargetOpacity, float Duration)
{
	FadeStartOpacity = CurrentOpacity;
	FadeTargetOpacity = TargetOpacity;
	FadeElapsed = 0.f;
	FadeDuration = FMath::IsFinite(Duration) ? FMath::Max(0.f, Duration) : 0.f;
	if (FadeDuration <= KINDA_SMALL_NUMBER)
	{
		CurrentOpacity = FadeTargetOpacity;
		ApplyOpacity();
		SetActorTickEnabled(false);
		if (bDismissing)
		{
			Destroy();
		}
		return;
	}
	SetActorTickEnabled(true);
}

void AEGElysiaStarSeaVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	FadeElapsed += FMath::Max(0.f, DeltaSeconds);
	const float Progress = FMath::Clamp(FadeElapsed / FadeDuration, 0.f, 1.f);
	CurrentOpacity = FMath::Lerp(FadeStartOpacity, FadeTargetOpacity, FMath::SmoothStep(0.f, 1.f, Progress));
	ApplyOpacity();
	if (Progress >= 1.f)
	{
		SetActorTickEnabled(false);
		if (bDismissing)
		{
			Destroy();
		}
	}
}

void AEGElysiaStarSeaVisual::ApplyOpacity()
{
	if (OuterBoardMID)
	{
		OuterBoardMID->SetScalarParameterValue(TEXT("DomainOpacity"),CurrentOpacity*FMath::Clamp(DomainOpacity,0.f,1.f));
	}
	if (DomainMID)
	{
		DomainMID->SetScalarParameterValue(TEXT("DomainOpacity"), CurrentOpacity * FMath::Clamp(DomainOpacity, 0.f, 1.f));
	}
	if (BorderMID)
	{
		BorderMID->SetScalarParameterValue(TEXT("DomainOpacity"), CurrentOpacity * FMath::Clamp(BorderOpacity, 0.f, 1.f));
	}
}

void AEGElysiaStarSeaVisual::HandleBoardDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == SourceBoard.Get(true))
	{
		Destroy();
	}
}

void AEGElysiaStarSeaVisual::HandleBoardEndPlay(AActor* BoardActor, EEndPlayReason::Type Reason)
{
	if (BoardActor == SourceBoard.Get(true))
	{
		Destroy();
	}
}

void AEGElysiaStarSeaVisual::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetActorTickEnabled(false);
	if (AEGDeploymentBoard* Board = SourceBoard.Get())
	{
		Board->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleBoardDestroyed);
		Board->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleBoardEndPlay);
	}
	SourceBoard.Reset();
	Super::EndPlay(EndPlayReason);
}
