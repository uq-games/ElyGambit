#include "Presentation/EGTeleportVisual.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

FSoftObjectPath AEGTeleportVisual::GetPreloadPath()
{
 return FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_StageArrivalV3.NS_StageArrivalV3"));
}

AEGTeleportVisual::AEGTeleportVisual()
{
 PrimaryActorTick.bCanEverTick=false;
 bReplicates=false;
 SetActorEnableCollision(false);
 Arrival=CreateDefaultSubobject<UNiagaraComponent>(TEXT("Arrival"));
 SetRootComponent(Arrival);
 Arrival->SetAutoActivate(false);
 Arrival->SetAutoDestroy(false);
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Asset(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_StageArrivalV3"));
 if(Asset.Succeeded())Arrival->SetAsset(Asset.Object);
 Floor=CreateDefaultSubobject<UNiagaraComponent>(TEXT("PortalFloor"));
 Floor->SetupAttachment(Arrival);
 Floor->SetAutoActivate(false);
 Floor->SetAutoDestroy(false);
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> FloorAsset(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_ArrivalFloorV3"));
 if(FloorAsset.Succeeded())Floor->SetAsset(FloorAsset.Object);
}

void AEGTeleportVisual::Initialize(bool bEnemy, float Radius, float Duration)
{
 if(GetNetMode()==NM_DedicatedServer || !Arrival->GetAsset()){Destroy();return;}
 // Existing PoisonFX aperture plus ARPGVFX column, owned by one short-lived actor.
 const float Scale=FMath::Clamp(Radius,30.f,200.f)/80.f;
 Arrival->SetRelativeScale3D(FVector(Scale));
 // Arrival is the root: a relative location here would teleport it to world origin.
 Arrival->Activate(true);
 Floor->SetRelativeLocation(FVector(0,0,15));
 Floor->SetVariableLinearColor(TEXT("User.Color"),FLinearColor(.25f,2.f,6.f,1.f));
 Floor->Activate(true);
 SetLifeSpan(FMath::Max(.2f,Duration));
}
