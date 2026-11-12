#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGCorruptionBossVisual.generated.h"

class AEGChessUnit;
class UEGBattleContextComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USkeletalMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UEGCorruptionCombatComponent;
class UEGCorruptionBossBehavior;
class UEGSourcePreviewDefinition;
class USoundBase;
class UAudioComponent;
enum class EEGCorruptionBossCast : uint8;
enum class EEGCorruptionBossCastEvent : uint8;
enum class EEGCorruptionFirstPulseEvent : uint8;

/** A local consumer of combat events. No collision, timers or damage authority. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGCorruptionBossVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGCorruptionBossVisual();
 static AEGCorruptionBossVisual* SpawnForBoss(AEGChessUnit* Boss);
 void SetSecondPhase(bool bEnabled);
 void SetEnraged(bool bEnabled);
 void ShowMark(AEGChessUnit* Target, int32 Stacks, int32 Threshold);
 void ShowMarkBurst(FVector Location, float Radius);
 void ShowSpreadWarning(FVector Location, float Radius, float Duration);
 void ClearSpreadWarning();
 void ShowSpreadBurst(FVector Location, float Radius);
 void ShowBarrierFailure(FVector WallCenter, float WallHalfWidth);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
 enum class EVisualKind : uint8 { Controller, PhaseTransition, EnrageAccent, Mark, MarkBurst, SpreadWarning, SpreadBurst, PulseBurst, BarrierFailure };
 AEGCorruptionBossVisual* SpawnEffect(EVisualKind Kind, FVector Location, float Radius, float Duration, AEGChessUnit* Target = nullptr);
 void AttachAccent(UNiagaraSystem* System,float Scale,float Height=45.f);
 void AttachCrystalFragments(int32 Count);
 bool IsCurrent() const;
 void SetTint(FLinearColor Color);
 void RefreshBodyOverlay();
 void RestoreBodyOverlay();
 void HandleBossCast(EEGCorruptionBossCast Cast,EEGCorruptionBossCastEvent Event,FGuid Action,float CastDuration);
 void HandleFirstPulse(EEGCorruptionFirstPulseEvent Event,FGuid EventRound,FGuid Action,FVector Point,float InRadius,float InDuration);
 void ReleaseCastPresentation();
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Ring;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> InnerRing;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Shards;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> SweepFragments;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> ShardMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DimShardMaterial;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> SweepParticles;
 UPROPERTY() TObjectPtr<UMaterialInterface> BodyOverlayBase;
 UPROPERTY() TObjectPtr<UNiagaraSystem> SpreadImpactSystem;
 UPROPERTY() TObjectPtr<UNiagaraSystem> AreaBurstSystem;
 UPROPERTY() TObjectPtr<UNiagaraSystem> SweepWaveSystem;
 UPROPERTY() TObjectPtr<UStaticMesh> SweepFragmentMesh;
 UPROPERTY() TObjectPtr<UMaterialInterface> CrystalMaterial;
 UPROPERTY() TObjectPtr<UEGSourcePreviewDefinition> CastAssembly;
 UPROPERTY() TObjectPtr<USoundBase> PulseWarningSound;
 UPROPERTY() TObjectPtr<USoundBase> PulseImpactSound;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyOverlay;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PreviousBodyOverlay;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> OverlayMesh;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Source;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> FollowTarget;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 TWeakObjectPtr<UEGCorruptionCombatComponent> Corruption;
 TWeakObjectPtr<UEGCorruptionBossBehavior> FirstPulseBehavior;
 TWeakObjectPtr<UAudioComponent> FirstPulseAudio;
 FDelegateHandle CastDelegate;
 FDelegateHandle FirstPulseDelegate;
 FGuid CastAction;
 FGuid FirstPulseAction;
 float CastSourceDuration=0.f;
 TWeakObjectPtr<AEGCorruptionBossVisual> ParentVisual;
 TWeakObjectPtr<AEGCorruptionBossVisual> SpreadWarning;
 TWeakObjectPtr<AEGCorruptionBossVisual> FirstPulseWarning;
 TMap<TWeakObjectPtr<AEGChessUnit>,TWeakObjectPtr<AEGCorruptionBossVisual>> Marks;
 TArray<TWeakObjectPtr<AEGCorruptionBossVisual>> Children;
 FGuid Round;
 FVector SweepOrigin=FVector::ZeroVector;
 FVector SweepEnd=FVector::ZeroVector;
 EVisualKind Kind=EVisualKind::Controller;
 double LastRealSeconds=0.0;
 float Age=0.f, Duration=0.f, Radius=100.f, MarkFraction=0.f;
 int32 MarkCount=0, MarkThreshold=1;
 bool bSecondPhase=false, bEnraged=false, bOverlayLoadAttempted=false, bSweepFinishing=false;
 bool bOverlayRelinquished=false;
};
