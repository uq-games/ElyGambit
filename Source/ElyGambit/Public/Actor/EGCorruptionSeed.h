#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGCorruptionSeed.generated.h"
class AEGChessUnit;
class UEGBattleContextComponent;
class UStaticMeshComponent;
UCLASS()
class ELYGAMBIT_API AEGCorruptionSeed : public AActor
{
 GENERATED_BODY()
public:
 AEGCorruptionSeed();
 static AEGCorruptionSeed* Spawn(UEGBattleContextComponent* Battle, AEGChessUnit* Source, FVector Location, float Damage, float Delay=2.f, float RadiusCells=1.f, int32 Limit=6);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 bool BelongsTo(const UEGBattleContextComponent* Context) const;
private:
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Core;
 UPROPERTY() TObjectPtr<UStaticMeshComponent> Warning;
 UPROPERTY() TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY() TWeakObjectPtr<AEGChessUnit> Source;
 FGuid RoundId,AttackId;
 float Damage=0.f,Radius=0.f,Remaining=0.f;
};
