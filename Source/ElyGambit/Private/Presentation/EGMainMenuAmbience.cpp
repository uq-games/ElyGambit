#include "Presentation/EGMainMenuAmbience.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AEGMainMenuAmbience::AEGMainMenuAmbience()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 1.f / 24.f;
    bReplicates = false;

    Petals = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Petals"));
    SetRootComponent(Petals);
    Petals->SetMobility(EComponentMobility::Movable);
    Petals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Petals->SetGenerateOverlapEvents(false);
    Petals->SetCanEverAffectNavigation(false);
    Petals->SetCastShadow(false);
}

void AEGMainMenuAmbience::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RebuildPetals();
}

void AEGMainMenuAmbience::BeginPlay()
{
    Super::BeginPlay();
    ElapsedSeconds = 0.f;
    RebuildPetals();
}

void AEGMainMenuAmbience::RebuildPetals()
{
    if (!Petals) return;
    Petals->ClearInstances();
    States.Reset();
    FrameTransforms.Reset();
    Petals->SetStaticMesh(PetalMesh);
    Petals->SetMaterial(0, PetalMaterial);

    const int32 SafeCount = FMath::Clamp(ParticleCount, 0, 64);
    SetActorTickInterval(1.f / FMath::Clamp(UpdateRateHz, 10.f, 30.f));
    SetActorTickEnabled(PetalMesh && SafeCount > 0);
    if (!PetalMesh || SafeCount == 0) return;

    FRandomStream Random(Seed);
    States.Reserve(SafeCount);
    FrameTransforms.Reserve(SafeCount);
    const float ExtentX = FMath::Abs(BoxExtent.X);
    const float ExtentY = FMath::Abs(BoxExtent.Y);
    for (int32 Index = 0; Index < SafeCount; ++Index)
    {
        FPetalState& State = States.AddDefaulted_GetRef();
        State.Centre = FVector2D(Random.FRandRange(-ExtentX, ExtentX), Random.FRandRange(-ExtentY, ExtentY));
        State.Phase = Random.FRand();
        State.Size = Random.FRandRange(0.7f, 1.3f);
        State.Yaw = Random.FRandRange(0.f, 360.f);
        State.Flutter = Random.FRandRange(0.f, 2.f * PI);
        FrameTransforms.Add(MakePetalTransform(State, ElapsedSeconds));
    }
    Petals->AddInstances(FrameTransforms, false, false, false);
}

void AEGMainMenuAmbience::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const float SafeCycle = FMath::Max(2.f, CycleSeconds);
    ElapsedSeconds = FMath::Fmod(ElapsedSeconds + FMath::Max(0.f, DeltaSeconds), SafeCycle);
    RefreshTransforms();
}

FTransform AEGMainMenuAmbience::MakePetalTransform(const FPetalState& State, float TimeSeconds) const
{
    const float SafeCycle = FMath::Max(2.f, CycleSeconds);
    const float Phase = FMath::Frac(TimeSeconds / SafeCycle + State.Phase);
    const float Angle = 2.f * PI * Phase;
    const float ExtentZ = FMath::Abs(BoxExtent.Z);
    FVector Breeze(WindDirection.X, WindDirection.Y, 0.f);
    Breeze = Breeze.GetSafeNormal();
    const FVector CrossBreeze(-Breeze.Y, Breeze.X, 0.f);
    const float Amplitude = FMath::Max(0.f, WindAmplitude);
    const FVector Drift = Amplitude * (Breeze * FMath::Sin(Angle + State.Flutter)
        + CrossBreeze * (0.3f * FMath::Sin(2.f * Angle + State.Flutter)));
    const FVector Position(State.Centre.X + Drift.X, State.Centre.Y + Drift.Y,
        ExtentZ * (1.f - 2.f * Phase));

    // The petal is invisible when it wraps from the bottom back to the top.
    const float Enter = FMath::SmoothStep(0.f, 0.10f, Phase);
    const float Leave = FMath::SmoothStep(0.f, 0.10f, 1.f - Phase);
    const float Scale = FMath::Max(0.0001f, PetalSizeCm * 0.01f * State.Size * Enter * Leave);
    const FRotator Rotation(20.f * FMath::Sin(Angle + State.Flutter),
        State.Yaw + 360.f * Phase, 25.f * FMath::Cos(Angle + State.Flutter));
    return FTransform(Rotation, Position, FVector(Scale));
}

void AEGMainMenuAmbience::RefreshTransforms()
{
    if (!Petals || States.IsEmpty() || States.Num() != Petals->GetInstanceCount()) return;
    for (int32 Index = 0; Index < States.Num(); ++Index)
    {
        FrameTransforms[Index] = MakePetalTransform(States[Index], ElapsedSeconds);
    }
    Petals->BatchUpdateInstancesTransforms(0, FrameTransforms, false, true, true);
}
