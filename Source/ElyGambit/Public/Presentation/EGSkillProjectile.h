#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGSkillProjectile.generated.h"
UENUM()
enum class EEGProjectileKind : uint8 { BasicAttack=0, ThrownSummon=2, AreaImpact=3, MonsterBasic=4 };
class UEGBattleContextComponent;
class AEGChessUnit;
class UStaticMeshComponent;
class UEGSourceSummonDefinition;
class AEGSourceEffect;
class UMaterialInterface;
class AEGDescentRing;
/** Value snapshot used to roll back an in-flight barrier relocation. */
struct FEGThrownSummonFlightState
{
 FVector Start=FVector::ZeroVector,Destination=FVector::ZeroVector,Location=FVector::ZeroVector,CargoLocation=FVector::ZeroVector,RingLocation=FVector::ZeroVector;
 FQuat FlightBasis=FQuat::Identity;
 float Elapsed=0.f,Duration=0.f,FlightHeight=0.f,RebasedCurveStart=0.f;
 bool bRebased=false,bHadRing=false,bHadCargo=false;
};
UCLASS()
class ELYGAMBIT_API AEGSkillProjectile : public AActor
{
 GENERATED_BODY()
public:
 AEGSkillProjectile();
 virtual void Tick(float Delta) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 /** Preserve this flight, then relocate its current point and landing together. */
 FEGThrownSummonFlightState CaptureThrownSummonFlight() const;
 bool RebaseThrownSummon(FVector NewCurrent);
 bool RebaseThrownSummon(FVector NewCurrent,FVector NewLanding);
 void RestoreThrownSummonFlight(const FEGThrownSummonFlightState& State);
 UPROPERTY(Transient) TObjectPtr<UEGSourceSummonDefinition> SummonMotion;
 UPROPERTY(Transient) TWeakObjectPtr<AEGSourceEffect> DescentEffect;
 /** Ground warning ring shown at the fixed landing point while the summon descends. */
 UPROPERTY(Transient) TWeakObjectPtr<AEGDescentRing> DescentRing;
 float DescentRingRadius=0.f;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SavedCargoMaterial;
 FQuat FlightBasis=FQuat::Identity;
 float FlightHeight=0.f;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Source;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Target;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Cargo;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Visual;
 FGuid Round,AttackId;FVector Start,Destination;
 float RawDamage=0,Elapsed=0,Duration=.35f;EEGProjectileKind Kind=EEGProjectileKind::BasicAttack;
 bool bMonsterUltimate=false;
private:
 bool bFlightRebased=false;
 float RebasedCurveStart=0.f;
};

