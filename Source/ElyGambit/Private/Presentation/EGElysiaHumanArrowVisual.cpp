#include "Presentation/EGElysiaHumanArrowVisual.h"

#include "Character/EGChessUnit.h"
#include "NiagaraComponent.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"

AEGElysiaHumanArrowVisual::AEGElysiaHumanArrowVisual()
{
 FlightEffect=CreateDefaultSubobject<UNiagaraComponent>(TEXT("FlightEffect"));
 FlightEffect->SetupAttachment(GetRootComponent());
 FlightEffect->SetAutoActivate(true);
 LaunchEffect=CreateDefaultSubobject<UNiagaraComponent>(TEXT("LaunchEffect"));
 LaunchEffect->SetupAttachment(GetRootComponent());
 LaunchEffect->SetAutoActivate(false);
}

void AEGElysiaHumanArrowVisual::BeginPlay()
{
 Super::BeginPlay();
 if(GetNetMode()!=NM_DedicatedServer && LaunchEffect && LaunchEffect->GetAsset())
 {
  const FTransform SpawnTransform=LaunchEffect->GetComponentTransform();
  LaunchEffect->SetAbsolute(true,true,true);
  LaunchEffect->SetWorldTransform(SpawnTransform);
  LaunchEffect->Activate(true);
 }
}

void AEGElysiaHumanArrowVisual::OnArrowImpact_Implementation(AEGChessUnit* HitUnit,FVector ImpactPoint)
{
 if(auto* Unit=Source.Get())
  if(auto* Presentation=Unit->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>())
   Presentation->ShowArrowImpact(ImpactPoint,GetActorRotation(),SourceImpact,bPiercing);
}
