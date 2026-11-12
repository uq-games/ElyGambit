#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGCorruptionPhantomVisual.generated.h"

class AEGChessUnit;
class UEGBattleContextComponent;
class UEGHeroDefinition;
class USkeletalMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class AEGCombatVisual;
class UStaticMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UEGCorruptionCombatComponent;
class UAudioComponent;
class USoundBase;
enum class EEGCorruptionBossCast : uint8;
enum class EEGCorruptionBossCastEvent : uint8;
enum class EEGCorruptionMimicCue : uint8 { Area, SelfShield, AllTargets };

/** Ephemeral visual for one already-authorized imitation; never executes combat. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGCorruptionPhantomVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGCorruptionPhantomVisual();
 static AEGCorruptionPhantomVisual* Spawn(AEGChessUnit* Boss, const UEGHeroDefinition* BaseDefinition,
     FVector Location, FRotator Facing, float WindupDuration, FGuid ActionId);
 void NotifyCast(FVector TargetLocation, float Radius, EEGCorruptionMimicCue Cue, bool bShieldApplied);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void SpawnPhantomAccent(float Scale,FVector RelativeLocation=FVector(0.f,0.f,-35.f),UNiagaraSystem* System=nullptr);
 void HandleBossCast(EEGCorruptionBossCast Cast,EEGCorruptionBossCastEvent Event,FGuid Action,float Duration);
 UPROPERTY(Transient) TObjectPtr<USkeletalMeshComponent> Body;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> PhantomMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> PhantomMID;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> Assembly;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CastBeam;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CastBeamMID;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> CastAreaRing;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> CastAreaRingMID;
 UPROPERTY() TObjectPtr<UNiagaraSystem> PhantomAccentSystem;
 UPROPERTY() TObjectPtr<UNiagaraSystem> PhantomBurstSystem;
 UPROPERTY() TObjectPtr<USoundBase> MimicImpactSound;
 UPROPERTY() TObjectPtr<USoundBase> MimicShieldSound;
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> PhantomAccents;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Source;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 TWeakObjectPtr<UEGCorruptionCombatComponent> Corruption;
 TWeakObjectPtr<UAudioComponent> CastAudio;
 FDelegateHandle CastDelegate;
 FGuid Round;
 FGuid CastAction;
 float Age=0.f, MotionDuration=0.f, CastAge=-1.f, CastAreaRadius=0.f;
 bool bDissolveAccentStarted=false;
};
