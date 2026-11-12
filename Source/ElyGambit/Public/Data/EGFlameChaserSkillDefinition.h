#pragma once
#include "Data/EGHeroConfig.h"
#include "EGFlameChaserSkillDefinition.generated.h"
class UEGSourcePreviewDefinition;
class UEGSourceEffectDefinition;
class UMaterialInterface;
class UNiagaraSystem;

UENUM(BlueprintType)
enum class EEGMobiusSourceAnchor : uint8
{
 TargetBody,
 TargetGround,
 CasterHand,
 /** Target-facing surface, using its existing presentation radius. */
 TargetFront
};

/** An original effect on the attack montage clock. Placement never transports its root. */
USTRUCT(BlueprintType)
struct FEGMobiusBasicSourceCue
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UEGSourcePreviewDefinition> Assembly;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) EEGMobiusSourceAnchor Anchor=EEGMobiusSourceAnchor::TargetBody;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0")) float StartTime=0.f;
 /** Zero plays the complete source effect; a positive value is its contact cutoff. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0")) float Duration=0.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.01")) float PlayRate=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.01")) float Scale=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName Bone=TEXT("Bip001-R-Hand");
 /** Confirmed contact cosmetics may only appear after the existing gameplay Hit succeeds. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bRequiresHit=false;
};

USTRUCT(BlueprintType)
struct FEGMobiusBasicSourceStage
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGMobiusBasicSourceCue> Cues;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHuaSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hua|Presentation") TSoftObjectPtr<UNiagaraSystem> ChessStrikeFX;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hua|Presentation") TSoftObjectPtr<UNiagaraSystem> ChessLandingFX;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Revival",meta=(ClampMin="0.01")) float ReviveDelay=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Revival",meta=(ClampMin="0.01",ClampMax="1")) float ReviveHealthRatio=.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hua") float ArmorReduction=.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Hua") float ArmorSeconds=6.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Leap",meta=(ClampMin="0")) float TakeoffTime=.02f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Leap",meta=(ClampMin="0.01")) float LandingTime=1.3f;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGCollapsedHuaSkillDefinition : public UEGHuaSkillDefinition
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Revival",meta=(ClampMin="0")) float ReviveAttackBonus=.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Burn",meta=(ClampMin="0",ClampMax="1")) float BurnCurrentHealthRatio=.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Burn",meta=(ClampMin="0",ClampMax="1")) float RecoveryHealthRatio=.3f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Burn") float BurnShieldRatio=.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Burn") float BurnShieldSeconds=6.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Burn",meta=(ClampMin="0")) float BurnDamageBonus=2.f;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGMobiusSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 /** Board-scale Niagara presentation. Gameplay values and source animations are independent. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") bool bUseNiagaraPresentation=false;
 /** Optional source cues for ordinary basic attacks; Niagara remains the fallback. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") bool bUseSourceBasicPresentation=false;
 /** Optional source mesh links/discharges while Niagara remains enabled elsewhere. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") bool bUseSourceLinks=false;
 /** Opt in to source atmosphere and impact alongside Niagara during the ultimate only. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Ultimate") bool bUseSourceUltimateOverlay=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraBasicStrike;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraBasicImpact;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraStormImpact;
 /** Optional ultimate-only storm. Unset or unavailable falls back to NiagaraStormImpact. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraUltimateStorm;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraCast;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraRecovery;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation") TSoftObjectPtr<UNiagaraSystem> NiagaraLink;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation",meta=(ClampMin="1")) float NiagaraLinkWidth=25.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Niagara Presentation",meta=(ClampMin="1")) float NiagaraDischargeWidth=42.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TArray<FEGMobiusBasicSourceStage> BasicSourceStages;
 /** Historical serialized references, retained for compatibility; no travelling carrier uses these. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TArray<TSoftObjectPtr<UEGSourcePreviewDefinition>> BasicProjectileAssemblies;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TSoftObjectPtr<UEGSourcePreviewDefinition> BasicImpactAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TArray<float> BasicLaunchTimes;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack",meta=(ClampMin="0.01")) float BasicVisualScale=.8f;
 /** Private source sampling windows, mapped to the existing launch-to-Hit clock. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TArray<FVector2D> BasicSourceWindows;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TArray<float> BasicProjectileScales;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack") TArray<FName> BasicLaunchBones;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack",meta=(ClampMin="0.01")) float ImpactVisualScale=.65f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Attack",meta=(ClampMin="0.01")) float ImpactPlaybackRate=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent") TSoftObjectPtr<UEGSourcePreviewDefinition> SerpentBody;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent") TSoftObjectPtr<UEGSourcePreviewDefinition> SerpentRecovery;
 /** Source BackToAvatar effect, separate from the original human recovery body. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Ultimate") TSoftObjectPtr<UEGSourcePreviewDefinition> UltimateRecoveryEffect;
 /** Authored recovery-part start on the existing source montage clock. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Ultimate",meta=(ClampMin="0")) float UltimateRecoveryStartTime=0.f;

 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent") TSoftObjectPtr<UEGSourcePreviewDefinition> StormImpact;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent") TSoftObjectPtr<UMaterialInterface> StormBeamMaterial;
 /** Human-form enhancement; legacy Serpent fields below remain asset-compatible only. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Empowered",meta=(ClampMin="0.1")) float EmpoweredDuration=12.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Empowered",meta=(ClampMin="0")) float EmpoweredRadius=500.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Empowered",meta=(ClampMin="0")) float EmpoweredMultiplier=1.f;
 /** Uniform source-FX scale for human empowered hits only; never changes damage or giant proportions. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Empowered",meta=(ClampMin="0.001",ClampMax="1")) float EmpoweredStormVisualScale=1.f;

 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent",meta=(ClampMin="0.1")) float SerpentDuration=12.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent",meta=(ClampMin="0.1")) float SerpentRecoverySeconds=.7f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent",meta=(ClampMin="0")) float StormRadius=500.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent",meta=(ClampMin="0")) float StormMultiplier=1.f;
 /** Local entry/idle/fire poses from the original five-second SKL02 snake clip. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent") float SerpentIdleTime=4.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Serpent") float SerpentFireTime=4.7f;
 /** Self-anchored source energy layers. Storm impacts use the confirmed enemy point. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Source Ultimate") TSoftObjectPtr<UEGSourcePreviewDefinition> UltimateSourceAssembly;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Link",meta=(ClampMin="0.01")) float LinkDuration=6.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Link",meta=(ClampMin="0",ClampMax="1")) float LinkDamageRatio=.2f;
 /** Source electricity track stretched between linked units; unset retains legacy asset compatibility. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Link|Source") TSoftObjectPtr<UEGSourceEffectDefinition> LinkSourceEffect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Link|Source") TSoftObjectPtr<UEGSourceEffectDefinition> DischargeSourceEffect;
 /** Cosmetic ribbon width in board centimetres. Does not affect link range or damage. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Link|Source",meta=(ClampMin="1")) float LinkSourceWidth=18.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Link|Source",meta=(ClampMin="1")) float DischargeSourceWidth=36.f;
};
