#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGCorruptionMine.generated.h"
class UEGBattleContextComponent;
class UEGEnvironmentTrapDefinition;
class AEGChessUnit;
class UStaticMeshComponent;
UCLASS()
class ELYGAMBIT_API AEGCorruptionMine : public AActor
{
 GENERATED_BODY()
public:
 AEGCorruptionMine();
 void Configure(UEGEnvironmentTrapDefinition* Data,float CellSpacing);
 void Arm(UEGBattleContextComponent* Context);
 virtual void Tick(float Delta) override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) bool bTriggered=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) bool bExploded=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) float Remaining=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 DamageCount=0;
private:
 UPROPERTY() TObjectPtr<UEGEnvironmentTrapDefinition> Definition;
 UPROPERTY() TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Body;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> TriggerRing;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> DangerRing;
 TMap<TWeakObjectPtr<AEGChessUnit>,FVector> Previous;
 FGuid Round,ExplosionId;
 float TriggerRadius=0,ExplosionRadius=0;
 bool bArmed=false;
 bool IsEligible(AEGChessUnit* Unit) const;
};
