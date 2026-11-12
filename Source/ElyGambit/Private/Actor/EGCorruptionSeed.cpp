#include "Actor/EGCorruptionSeed.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatGeometry.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Game/EGChessGameMode.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AEGCorruptionSeed::AEGCorruptionSeed()
{
 PrimaryActorTick.bCanEverTick=true;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ring(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 Core=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Core"));Core->SetupAttachment(GetRootComponent());
 Core->SetStaticMesh(Sphere.Object);Core->SetRelativeLocation(FVector(0,0,25));Core->SetRelativeScale3D(FVector(.45f,.45f,.65f));
 Warning=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Warning"));Warning->SetupAttachment(GetRootComponent());
 Warning->SetStaticMesh(Plane.Object);Warning->SetMaterial(0,Ring.Object);Warning->SetRelativeLocation(FVector(0,0,4));
 for(auto* Part:{Core.Get(),Warning.Get()}){Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCastShadow(false);}
 SetActorEnableCollision(false);
}
AEGCorruptionSeed* AEGCorruptionSeed::Spawn(UEGBattleContextComponent* Context,AEGChessUnit* Owner,FVector Location,float RawDamage,float Delay,float RadiusCells,int32 Limit)
{
 if(!IsValid(Context)||!Context->IsBattleActive()||!IsValid(Owner)||!Owner->HasAuthority()||!FMath::IsFinite(RawDamage)||RawDamage<=0.f||!FMath::IsFinite(Delay)||Delay<=0.f||!FMath::IsFinite(RadiusCells)||RadiusCells<=0.f||Limit<=0)return nullptr;
 auto* Mode=Cast<AEGChessGameMode>(Context->GetOwner());auto* Board=Mode?Mode->GetBoard():nullptr;if(!Board)return nullptr;
 int32 Count=0;for(TActorIterator<AEGCorruptionSeed> It(Context->GetWorld());It;++It)if(It->BelongsTo(Context)&&!It->IsActorBeingDestroyed())++Count;
 if(Count>=Limit)return nullptr;
 const float Spacing=FVector::Dist2D(Board->CellToWorld(FIntPoint(0,0)),Board->CellToWorld(FIntPoint(0,1)));
 if(!FMath::IsFinite(Spacing)||Spacing<=0.f)return nullptr;
 Location.Z-=Board->UnitCenterHeight;
 auto* Seed=Context->GetWorld()->SpawnActor<AEGCorruptionSeed>(Location,FRotator::ZeroRotator);if(!Seed)return nullptr;
 Seed->Battle=Context;Seed->Source=Owner;Seed->RoundId=Context->GetRoundId();Seed->AttackId=FGuid::NewGuid();
 Seed->Damage=RawDamage;Seed->Remaining=Delay;Seed->Radius=RadiusCells*Spacing;
 Seed->Warning->SetRelativeScale3D(FVector(Seed->Radius/50.f,Seed->Radius/50.f,1));
 if(auto* Material=Seed->Warning->CreateDynamicMaterialInstance(0))Material->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.8f,.08f,1.f,1.f));
 if(auto* Corruption=Owner->FindComponentByClass<UEGCorruptionCombatComponent>())
  Corruption->NotifySpreadStarted(Seed,Seed->GetActorLocation()+FVector(0,0,88),Seed->Radius,Delay,Seed->AttackId);
 return Seed;
}
bool AEGCorruptionSeed::BelongsTo(const UEGBattleContextComponent* Context) const{return Battle.Get()==Context;}
void AEGCorruptionSeed::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=RoundId||!Source.IsValid()){Destroy();return;}
 if(!IEGCombatant::Execute_IsAlive(Source.Get())){Destroy();return;}
 if(!Battle->IsCombatRunning())return;
 Remaining-=DeltaSeconds;const float Pulse=1.f+.2f*FMath::Sin(GetWorld()->GetTimeSeconds()*15.f);
 Core->SetRelativeScale3D(FVector(.45f*Pulse,.45f*Pulse,.65f*Pulse));
 if(Remaining>0.f)return;
 auto* Corruption=Source->FindComponentByClass<UEGCorruptionCombatComponent>();
 const bool bBossCast=Corruption&&Corruption->IsBoss();
 const FGuid CastAction=bBossCast?Corruption->GetBossCastActionId():FGuid();
 if(bBossCast&&!Corruption->IsCurrentSpread(this,RoundId,CastAction)){Destroy();return;}
 const FVector HitPoint=GetActorLocation()+FVector(0,0,88);const FGuid HitRound=RoundId;
 TWeakObjectPtr<AEGCorruptionSeed> WeakSeed(this);
 TWeakObjectPtr<UEGBattleContextComponent> WeakBattle=Battle;
 TWeakObjectPtr<AEGChessUnit> WeakSource=Source;
 if(bBossCast)
 {
  const auto IsSpreadCurrent=[WeakSeed,WeakBattle,WeakSource,HitRound,CastAction]()
  {
   const auto* Seed=WeakSeed.Get();const auto* Context=WeakBattle.Get();const auto* Owner=WeakSource.Get();
   if(!Seed||Seed->IsActorBeingDestroyed()||!Context||!Context->IsBattleActive()||!Context->IsCombatRunning()||
      Context->GetRoundId()!=HitRound||!Owner||!IEGCombatant::Execute_IsAlive(Owner))return false;
   const auto* Rules=Owner->FindComponentByClass<UEGCorruptionCombatComponent>();
   return Rules&&Rules->IsCurrentSpread(Seed,HitRound,CastAction);
  };
  const auto Victims=Battle->Units();TSet<AEGChessUnit*> Visited;
  for(auto* Victim:Victims)
  {
   if(!IsSpreadCurrent())break;
   if(!Source->GetCombat()->IsValidEnemy(Victim)||Visited.Contains(Victim)||
      EGCombatGeometry::PointDistanceSquared(HitPoint,Victim)>FMath::Square(Radius))continue;
   Visited.Add(Victim);Battle->Damage(Source.Get(),Victim,Damage,AttackId);
  }
  if(IsSpreadCurrent())Battle->Visual(Source.Get(),HitPoint-FVector(0,0,85),Radius,true);
 }
 else Battle->Area(Source.Get(),HitPoint,Radius,Damage,false,false,AttackId);
 // Damage callbacks can synchronously end the round, kill the boss or destroy
 // this seed. Publish a resolved event only for the same surviving transaction.
 if(!WeakSeed.IsValid()||IsActorBeingDestroyed())return;
 if(WeakBattle.IsValid()&&WeakBattle->IsBattleActive()&&WeakBattle->GetRoundId()==HitRound&&
    WeakSource.IsValid()&&IEGCombatant::Execute_IsAlive(WeakSource.Get()))
  if(auto* Current=WeakSource->FindComponentByClass<UEGCorruptionCombatComponent>())
   if(!bBossCast||Current->IsCurrentSpread(this,HitRound,CastAction))Current->NotifySpreadResolved(this,HitPoint,Radius);
 Destroy();
}
void AEGCorruptionSeed::EndPlay(const EEndPlayReason::Type Reason)
{
 if(Source.IsValid())if(auto* Corruption=Source->FindComponentByClass<UEGCorruptionCombatComponent>())
  Corruption->NotifySpreadEnded(this);
 Super::EndPlay(Reason);
}
