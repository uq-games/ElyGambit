#include "Actor/EGCorruptionMine.h"
#include "Data/EGEnvironmentTrapDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Presentation/EGPresentationBurst.h"
#include "UObject/ConstructorHelpers.h"
AEGCorruptionMine::AEGCorruptionMine()
{
 PrimaryActorTick.bCanEverTick=true;SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ring(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));Body->SetupAttachment(GetRootComponent());Body->SetStaticMesh(Sphere.Object);Body->SetRelativeLocation(FVector(0,0,18));Body->SetRelativeScale3D(FVector(.7,.7,.3));
 TriggerRing=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TriggerRange"));DangerRing=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ExplosionRange"));
 for(auto* C:{TriggerRing.Get(),DangerRing.Get()}){C->SetupAttachment(GetRootComponent());C->SetStaticMesh(Plane.Object);C->SetMaterial(0,Ring.Object);C->SetRelativeLocation(FVector(0,0,C==TriggerRing?6:4));}
 for(auto* C:{Body.Get(),TriggerRing.Get(),DangerRing.Get()}){C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetCastShadow(false);}
 SetActorEnableCollision(false);
}
void AEGCorruptionMine::Configure(UEGEnvironmentTrapDefinition* Data,float Spacing)
{
 Definition=Data;if(!Data||!Data->IsValidConfiguration()||!FMath::IsFinite(Spacing)||Spacing<=0){Destroy();return;}
 TriggerRadius=Data->TriggerRadiusCells*Spacing;ExplosionRadius=Data->ExplosionRadiusCells*Spacing;
 TriggerRing->SetRelativeScale3D(FVector(TriggerRadius/50,TriggerRadius/50,1));DangerRing->SetRelativeScale3D(FVector(ExplosionRadius/50,ExplosionRadius/50,1));
 if(Data->Material)Body->SetMaterial(0,Data->Material);
 if(auto* M=TriggerRing->CreateDynamicMaterialInstance(0))M->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1,.12,.02,1));
 if(auto* M=DangerRing->CreateDynamicMaterialInstance(0))M->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.7,.05,1,1));
}
bool AEGCorruptionMine::IsEligible(AEGChessUnit* U) const
{return IsValid(U)&&IEGCombatant::Execute_IsAlive(U)&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player&&IEGCombatant::Execute_GetCombatState(U)==EEGUnitState::Fighting&&U->GetHeroSkill()->IsTargetable();}
void AEGCorruptionMine::Arm(UEGBattleContextComponent* Context)
{
 if(bArmed||!HasAuthority()||!Definition||!Context||!Context->IsBattleActive())return;
 Battle=Context;Round=Context->GetRoundId();ExplosionId=FGuid::NewGuid();bArmed=true;Remaining=Definition->TriggerDelay;
 for(auto* U:Context->Units())if(IsEligible(U))Previous.Add(U,U->GetActorLocation());
}
void AEGCorruptionMine::Tick(float Delta)
{
 Super::Tick(Delta);if(!HasAuthority()||!bArmed||bExploded)return;
 if(!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round){Destroy();return;}
 const FVector Center=GetActorLocation();
 if(!bTriggered)
 {
  for(auto* U:Battle->Units())if(IsEligible(U))
  {
   FVector Point=U->GetActorLocation();Point.Z=Center.Z;FVector Before=Previous.FindRef(U);if(!Previous.Contains(U))Before=Point;Before.Z=Center.Z;
   if(FMath::PointDistToSegmentSquared(Center,Before,Point)<=FMath::Square(TriggerRadius))bTriggered=true;
   Previous.Add(U,Point);
  }
  if(!bTriggered)return;
  UE_LOG(LogTemp,Display,TEXT("EG_MINE_TRIGGER %s Round=%s"),*GetActorLocation().ToString(),*Round.ToString());
  return;
 }
 Remaining=FMath::Max(0.f,Remaining-Delta);
 const float Pulse=1.f+.25f*FMath::Sin(GetWorld()->GetTimeSeconds()*22);Body->SetRelativeScale3D(FVector(.7*Pulse,.7*Pulse,.3*Pulse));
 if(Remaining>0)return;
 bExploded=true;
 // Snapshot before damage: a death callback may end the battle or modify the roster.
 const auto Targets=Battle->Units();
 for(auto* U:Targets)if(Battle.IsValid()&&Battle->IsBattleActive()&&Battle->GetRoundId()==Round&&IsEligible(U)&&FVector::DistSquared2D(Center,U->GetActorLocation())<=FMath::Square(ExplosionRadius))
  if(FEGCombatExecution::EnvironmentDamage(U,Definition->Damage,ExplosionId))++DamageCount;
 UE_LOG(LogTemp,Display,TEXT("EG_MINE_EXPLODE Hits=%d Round=%s"),DamageCount,*Round.ToString());Destroy();
}
