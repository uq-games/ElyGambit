#include "Presentation/EGBoardShotVisual.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/StaticMesh.h"
#include "Presentation/EGPresentationBurst.h"
AEGBoardShotVisual::AEGBoardShotVisual()
{
 PrimaryActorTick.bCanEverTick=true;Beam=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Tracer"));SetRootComponent(Beam);
 Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);Beam->SetGenerateOverlapEvents(false);Beam->SetCanEverAffectNavigation(false);Beam->SetCastShadow(false);
}
void AEGBoardShotVisual::Spawn(AEGChessUnit* Source,AEGChessUnit* Target,bool bCannon)
{
 if(!Source||!Target||Source->GetNetMode()==NM_DedicatedServer)return;
 auto* V=Source->GetWorld()->SpawnActor<AEGBoardShotVisual>();if(!V)return;
 V->SetOwner(Source);V->Battle=Source->GetHeroSkill()->Battle;V->Round=V->Battle.IsValid()?V->Battle->GetRoundId():FGuid();
 FVector Start=Source->GetActorLocation()+Source->GetActorForwardVector()*180+FVector(0,0,160);
 Source->GetHeroSkill()->GetAssemblyBoneLocation(bCannon?TEXT("Cannon"):TEXT("Gatling"),bCannon?TEXT("CannonFire"):TEXT("FirePoint1"),Start);
 const FVector End=Target->GetActorLocation()+FVector(0,0,Target->GetDefinition()->GetPresentationHeight()*.4f-88);
 const FVector Delta=End-Start;
 V->Beam->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder")));
 auto* Material=Source->GetDefinition()->GetAttackMaterial().LoadSynchronous();if(Material)V->Beam->SetMaterial(0,Material);
 V->SetActorLocation((Start+End)*.5f);V->SetActorRotation(FQuat::FindBetweenNormals(FVector::UpVector,Delta.GetSafeNormal()));
 V->SetActorScale3D(FVector(bCannon?.13f:.055f,bCannon?.13f:.055f,Delta.Size()/100));
 AEGPresentationBurst::SpawnContact(Source->GetWorld(),Start,Material,V->Round);
 AEGPresentationBurst::SpawnContact(Source->GetWorld(),End,Material,V->Round);
}
void AEGBoardShotVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);Age+=DeltaSeconds;
 if(Age>.09f||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round)Destroy();
}
