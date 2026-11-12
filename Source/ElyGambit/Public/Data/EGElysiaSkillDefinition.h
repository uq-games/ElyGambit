#pragma once
#include "CoreMinimal.h"
#include "Data/EGHeroConfig.h"
#include "EGElysiaSkillDefinition.generated.h"
class UEGSourcePreviewDefinition;
class UMaterialInterface;
class UNiagaraSystem;

/** Optional authored Niagara cue. Transform is relative to the cue actor. */
USTRUCT(BlueprintType)
struct FEGElysiaNiagaraCue
{
 GENERATED_BODY()
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TSoftObjectPtr<UNiagaraSystem> System;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) FTransform Transform=FTransform::Identity;
};

/** Pink Elf's board adaptation. Original clips remain immutable in source folders. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGElysiaSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 virtual bool HasValidSkillRules() const override;
 /** Temporary presentation contract: only the body and basic attacks are authored.
  * Unauthored skills remain unavailable until their animations are supplied. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Presentation") bool bBasicOnlyPresentation=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules") int32 SeedStackLimit=4;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules",meta=(ClampMin="0")) float NormalAttackMultiplier=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules",meta=(ClampMin="1")) int32 ChargedAttackInterval=5;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules") float SeedBonusPerStack=.2f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules") float EmpoweredSeconds=6.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules") float EmpoweredRadius=500.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules") FVector RicochetTierMultipliers=FVector(.6f,.8f,1.f);
 /** One non-recursive bounce from an empowered arrow, as a fraction of attack. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Rules",meta=(ClampMin="0")) float DomeRicochetMultiplier=.6f;
 /** Arrow rain replaces the former empowerment. Eight 0.5s pulses by default. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Arrow Rain") bool bArrowRainUltimate=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Arrow Rain",meta=(ClampMin="0.1")) float ArrowRainSeconds=4.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Arrow Rain",meta=(ClampMin="1",ClampMax="64")) int32 ArrowRainPulses=8;
 /** Radius in board cells, resolved against the actual board once at release. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Arrow Rain",meta=(ClampMin="0.1")) float ArrowRainRadiusCells=1.5f;
 /** Total damage is captured attack × tier multiplier × this factor. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Arrow Rain",meta=(ClampMin="0")) float ArrowRainTotalMultiplier=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) int32 MaximumSeeds=4;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float SeedBurstPerStack=.15f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float ChargedMultiplier=1.8f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float DomeChargedBonus=1.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float DomeDuration=12.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float DomeRadius=1700.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float ArrowFlightSeconds=.16f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float RicochetMultiplier=.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) float RicochetInterval=.12f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) int32 RicochetWaves=2;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) int32 MaximumRicochetTargets=6;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TSoftObjectPtr<UEGSourcePreviewDefinition> ArrowAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TSoftObjectPtr<UEGSourcePreviewDefinition> ImpactAssembly;
 /** Source effect assemblies for the charged windup and one-shot ultimate impact. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> ChargeAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UEGSourcePreviewDefinition> BurstAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation",meta=(ClampMin="0.01")) float BurstPresentationScale=1.f;
 /** Original C1 Seed1..Seed4. No generated crystal geometry or replacement material. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> SeedAssemblies;
 /** Optional: unsupported native screen shaders stay absent rather than receive a substitute. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TSoftObjectPtr<UEGSourcePreviewDefinition> DomeAssembly;
 /** Original sky disappearance clip, mapped to the end of the existing dome timer. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TSoftObjectPtr<UEGSourcePreviewDefinition> DomeDissolveAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) bool bHoldDomeLastSourceFrame=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,meta=(ClampMin="0.01")) float DomePresentationScale=3.f;
 /** Optional board-readable additive accents, shared by field, attacks and seeds. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") TSoftObjectPtr<UMaterialInterface> BoardFXMaterial;

 /** Empty cues retain the existing SourceFX or field fallback. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue ArrowCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue ChargedArrowCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue RicochetCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue ImpactCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue ChargeCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue DomeEntryCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue DomeLoopCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue DomeExitCue;
 /** Petals remain owned by the same authoritative domain presentation actor. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue DomePetalCue;
 /** Optional one-shot petals at the existing natural domain-expiry presentation. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue DomeExitPetalCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue RainCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue RainPetalCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue RainImpactCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara") FEGElysiaNiagaraCue RainAreaCue;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara",meta=(ClampMin="1")) float RainReferenceRadius=750.f;
 /** Authored radius of domain cues, in world centimetres before cue transform scaling. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara",meta=(ClampMin="1")) float DomeCueReferenceRadius=500.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Niagara",meta=(ClampMin="0.01")) float DomeExitSeconds=1.5f;
 /** Optional private translucent materials for a readable field outline and shell. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Dome") TSoftObjectPtr<UMaterialInterface> DomeBoundaryMaterial;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Dome") TSoftObjectPtr<UMaterialInterface> DomeShellMaterial;
 /** Height above the board plane, in world centimetres. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Elysia|Dome",meta=(ClampMin="1")) float DomeFloorOffset=15.f;
};
