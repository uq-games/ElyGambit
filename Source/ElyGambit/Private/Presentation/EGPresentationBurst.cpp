#include "Presentation/EGPresentationBurst.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AEGPresentationBurst::AEGPresentationBurst()
{
 PrimaryActorTick.bCanEverTick=true;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMaterial(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 Ring=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));
 Ring->SetupAttachment(GetRootComponent());Ring->SetStaticMesh(Plane.Object);
 Ring->SetMaterial(0,RingMaterial.Object);Ring->SetRelativeLocation(FVector(0,0,3));
 Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ring->SetCastShadow(false);
 for(int32 I=0;I<8;++I)
 {
  auto* Spark=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Spark%d"),I));
  Spark->SetupAttachment(GetRootComponent());Spark->SetStaticMesh(Sphere.Object);
  Spark->SetCollisionEnabled(ECollisionEnabled::NoCollision);Spark->SetCastShadow(false);
  Spark->SetRelativeScale3D(FVector(.025f,.025f,.12f));Sparks.Add(Spark);
 }
 SetActorEnableCollision(false);
}
AEGPresentationBurst* AEGPresentationBurst::Spawn(UWorld* World,FVector Position,UMaterialInterface* Material,float Size)
{
 if(!World||World->GetNetMode()==NM_DedicatedServer)return nullptr;
 auto* Burst=World->SpawnActor<AEGPresentationBurst>(Position,FRotator::ZeroRotator);
 if(!Burst)return nullptr;
 Burst->Radius=Size;Burst->SetLifeSpan(.65f);
 FLinearColor Color;if(Material&&Material->GetVectorParameterValue(FMaterialParameterInfo(TEXT("Color")),Color))if(auto* MID=Burst->Ring->CreateDynamicMaterialInstance(0))MID->SetVectorParameterValue(TEXT("Tint"),Color);
 for(const auto& Spark:Burst->Sparks)if(Material)Spark->SetMaterial(0,Material);
 Burst->Tick(0.f);return Burst;
}
AEGPresentationBurst* AEGPresentationBurst::SpawnContact(UWorld* World,FVector Position,UMaterialInterface* Material,FGuid Round)
{
 auto* Burst=Spawn(World,Position,Material,.55f);
 if(Burst){Burst->bContact=true;Burst->RoundId=Round;Burst->Ring->SetVisibility(false);Burst->SetLifeSpan(.18f);}
 return Burst;
}
void AEGPresentationBurst::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);Age+=DeltaSeconds;
 const float T=FMath::Clamp(Age/(bContact?.18f:.65f),0.f,1.f);
 const float Width=FixedAreaRadius>0?FixedAreaRadius/50.f:(.3f+T*1.8f)*Radius;
 Ring->SetRelativeScale3D(FVector(Width,Width,1));
 Ring->SetVisibility(!bContact&&T<.8f);
 for(int32 I=0;I<Sparks.Num();++I)
 {
  const float Angle=I*UE_TWO_PI/Sparks.Num();
  Sparks[I]->SetRelativeLocation(FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*(15+T*65)*Radius+FVector(0,0,15+FMath::Sin(T*UE_PI)*65*Radius));
  Sparks[I]->SetRelativeScale3D(FVector(.035f,.035f,.13f)*(1-T)*Radius);
 }
}

namespace
{
void ApplyRingMaterial(UStaticMeshComponent* Component,UMaterialInterface* Material,const FLinearColor& Tint)
{
 if(!Component)return;
 if(Material)Component->SetMaterial(0,Material);
 if(auto* MID=Component->CreateDynamicMaterialInstance(0))
 {
  // Placement/skill materials expose the tint under a few common names; setting
  // all of them is harmless for materials that ignore the parameter.
  MID->SetVectorParameterValue(TEXT("Color"),Tint);
  MID->SetVectorParameterValue(TEXT("Tint"),Tint);
  MID->SetVectorParameterValue(TEXT("BaseColor"),Tint);
 }
}
}

AEGDescentRing::AEGDescentRing()
{
 PrimaryActorTick.bCanEverTick=true;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultRing(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 auto Make=[this](const TCHAR* Name)
 {
  auto* Component=CreateDefaultSubobject<UStaticMeshComponent>(Name);
  Component->SetupAttachment(GetRootComponent());
  Component->SetStaticMesh(Plane.Object);
  if(DefaultRing.Object)Component->SetMaterial(0,DefaultRing.Object);
  Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Component->SetCastShadow(false);
  Component->SetRelativeLocation(FVector(0,0,2));
  return Component;
 };
 Outer=Make(TEXT("Outer"));
 Inner=Make(TEXT("Inner"));
 SetActorEnableCollision(false);
}
AEGDescentRing* AEGDescentRing::SpawnTelegraph(UWorld* World,FVector Position,UMaterialInterface* Material,float InRadius,float InDuration)
{
 if(!World||World->GetNetMode()==NM_DedicatedServer||InRadius<=1.f||InDuration<=0.f)return nullptr;
 auto* Ring=World->SpawnActor<AEGDescentRing>(Position,FRotator::ZeroRotator);
 if(!Ring)return nullptr;
 Ring->bShockwave=false;Ring->Radius=InRadius;Ring->Duration=InDuration;
 Ring->SetLifeSpan(InDuration+.25f);
 ApplyRingMaterial(Ring->Outer,Material,FLinearColor(1.f,.78f,.34f,1.f));
 ApplyRingMaterial(Ring->Inner,Material,FLinearColor(1.f,.92f,.66f,1.f));
 Ring->Outer->SetRelativeScale3D(FVector(InRadius/50.f,InRadius/50.f,1));
 Ring->Inner->SetRelativeScale3D(FVector(InRadius/50.f*.05f,InRadius/50.f*.05f,1));
 return Ring;
}
AEGDescentRing* AEGDescentRing::SpawnShockwave(UWorld* World,FVector Position,UMaterialInterface* Material,float InRadius,float InDuration)
{
 if(!World||World->GetNetMode()==NM_DedicatedServer||InRadius<=1.f||InDuration<=0.f)return nullptr;
 auto* Ring=World->SpawnActor<AEGDescentRing>(Position,FRotator::ZeroRotator);
 if(!Ring)return nullptr;
 Ring->bShockwave=true;Ring->Radius=InRadius;Ring->Duration=InDuration;
 Ring->SetLifeSpan(InDuration+.1f);
 ApplyRingMaterial(Ring->Outer,Material,FLinearColor(1.f,.86f,.55f,1.f));
 Ring->Inner->SetVisibility(false);
 Ring->Outer->SetRelativeScale3D(FVector(InRadius/50.f*.2f,InRadius/50.f*.2f,1));
 return Ring;
}
void AEGDescentRing::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 Age+=DeltaSeconds;
 const float T=FMath::Clamp(Age/FMath::Max(.01f,Duration),0.f,1.f);
 if(bShockwave)
 {
  // Fast ease-out expansion, then a brief hold before the lifespan ends.
  const float Ease=1.f-FMath::Pow(1.f-T,2.2f);
  const float Width=Radius/50.f*FMath::Lerp(.2f,1.12f,Ease);
  Outer->SetRelativeScale3D(FVector(Width,Width,1));
  Outer->SetVisibility(T<.96f);
  return;
 }
 const float Base=Radius/50.f;
 const float Pulse=1.f+.035f*FMath::Sin(Age*20.f);
 Outer->SetRelativeScale3D(FVector(Base*Pulse,Base*Pulse,1));
 const float Grow=FMath::Clamp(T*1.04f,0.f,1.f);
 Inner->SetRelativeScale3D(FVector(FMath::Max(.03f,Base*Grow),FMath::Max(.03f,Base*Grow),1));
 Inner->SetVisibility(Grow<.985f);
 Outer->SetVisibility(true);
}

void AEGDescentRing::SetTint(const FLinearColor& Color)
{
 for(auto* Component:{Outer.Get(),Inner.Get()})
  if(auto* MID=Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0)))
  {MID->SetVectorParameterValue(TEXT("Tint"),Color);MID->SetVectorParameterValue(TEXT("Color"),Color);}
}
