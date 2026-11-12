#include "Presentation/EGKillBeamVisual.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGMatchManager.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

FSoftObjectPath AEGKillBeamVisual::GetPreloadPath()
{
 return FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_KillSkywardV3.NS_KillSkywardV3"));
}

AEGKillBeamVisual::AEGKillBeamVisual()
{
 bReplicates=false;
 PrimaryActorTick.bCanEverTick=true;
 SetActorEnableCollision(false);
 Beam=CreateDefaultSubobject<UNiagaraComponent>(TEXT("KillBeam"));
 SetRootComponent(Beam);
 Beam->SetAutoActivate(false);
 Beam->SetAutoDestroy(false);
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Asset(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_KillSkywardV3"));
 if(Asset.Succeeded())Beam->SetAsset(Asset.Object);
}

void AEGKillBeamVisual::BeginPlay()
{
 Super::BeginPlay();
 if(GetNetMode()==NM_DedicatedServer || !Beam->GetAsset()){Destroy();return;}
 if(auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>())Match=Mode->GetMatch();
 Beam->Activate(true);
 SetLifeSpan(.8f); // Sharp flash, expanding ring and short upward beam tail.
}

void AEGKillBeamVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 // Preserve the last kill through settlement, but remove it on same-world retry/preparation.
 if(Match.IsValid() && Match->GetPhase()==EEGMatchPhase::Preparation)Destroy();
}
