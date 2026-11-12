#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGMobiusLinkVisual.generated.h"
class AEGChessUnit;
class UEGBattleContextComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UEGSourceEffectDefinition;
class UNiagaraComponent;
class UNiagaraSystem;

/** Cosmetic lightning. Damage is committed exclusively by the skill's Hit notify. */
UCLASS()
class ELYGAMBIT_API AEGMobiusLinkVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGMobiusLinkVisual();
 static AEGMobiusLinkVisual* Spawn(AEGChessUnit* Source,const TArray<AEGChessUnit*>& Targets,float Duration,bool bConnection,const FVector* SourceOrigin=nullptr);
 void Refresh(float Duration);
 // Cosmetic receipt only: this actor never schedules or applies damage.
 static AEGMobiusLinkVisual* SpawnPulse(AEGChessUnit* Caster,AEGChessUnit* From,AEGChessUnit* To);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Caster;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGChessUnit>> Members;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Segments;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Marks;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> Material;
 UPROPERTY(Transient) TObjectPtr<UEGSourceEffectDefinition> NativeSource;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> NativeMaterials;
 UPROPERTY(Transient) TObjectPtr<UNiagaraSystem> NiagaraSource;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> NiagaraSegments;
 float NativeWidth=18.f;
 FVector NativeOrigin=FVector::ZeroVector;
 bool bFixedNativeOrigin=false;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> PulseOrigin;
 bool bPulse=false;
 FGuid Round;
 bool bLink=false;
 float Age=0,End=0;
};
