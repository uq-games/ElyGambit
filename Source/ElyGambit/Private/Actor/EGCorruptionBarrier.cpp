#include "Actor/EGCorruptionBarrier.h"
#include "Components/BoxComponent.h"

AEGCorruptionBarrier::AEGCorruptionBarrier()
{
 PrimaryActorTick.bCanEverTick=false;bReplicates=false;
 Wall=CreateDefaultSubobject<UBoxComponent>(TEXT("Wall"));SetRootComponent(Wall);
 Wall->SetCollisionObjectType(ECC_WorldDynamic);
 Wall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Wall->SetCollisionResponseToAllChannels(ECR_Ignore);
 Wall->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);
 Wall->SetGenerateOverlapEvents(false);Wall->SetCanEverAffectNavigation(false);
}
bool AEGCorruptionBarrier::InitializeWall(FVector GroundCenter,FVector WidthDirection,float Width,float Height)
{
 WidthDirection.Z=0.f;
 if(GroundCenter.ContainsNaN()||WidthDirection.ContainsNaN()||!WidthDirection.Normalize()||
    !FMath::IsFinite(Width)||!FMath::IsFinite(Height)||Width<=0.f||Height<=0.f)return false;
 SetActorLocationAndRotation(GroundCenter+FVector(0,0,Height*.5f),WidthDirection.Rotation());
 Wall->SetBoxExtent(FVector(Width*.5f,HalfThickness,Height*.5f));
 Wall->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 return true;
}
void AEGCorruptionBarrier::SetBlocking(bool bBlocking)
{
 Wall->SetCollisionEnabled(bBlocking?ECollisionEnabled::QueryAndPhysics:ECollisionEnabled::NoCollision);
}
