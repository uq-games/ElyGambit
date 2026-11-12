#pragma once
#include "CoreMinimal.h"
#include "Data/EGHeroConfig.h"
#include "EGElysiaHerrscherSkillDefinition.generated.h"

class AEGHumanArrowProjectile;

/** Independent Human Herrscher rules. None of these values use the C1 growth asset. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGElysiaHerrscherSkillDefinition : public UEGHeroSkillDefinition
{
 GENERATED_BODY()
public:
 virtual bool HasValidSkillRules() const override;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float InitialMana=20.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float MaxMana=100.f;
 /** Legacy serialized name; awarded once on successful arrow release, never on Finish. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(DisplayName="Mana Per Released Arrow")) float ManaPerFinishedBowAttack=10.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float ShieldHealthRatio=.2f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float OriginSeconds=8.f;
 /** Origin attacks retain their authored timing. Attack speed scales this playback rate. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(ClampMin="0.1")) float OriginActionPlaybackRate=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(ClampMin="1")) int32 BowAttackInterval=5;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(ClampMin="1")) int32 OriginAttackInterval=3;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(ClampMin="0.01")) float ArrowLaneWidthCells=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Arrow") TSubclassOf<AEGHumanArrowProjectile> ArrowProjectileClass;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Arrow") FName ArrowLaunchBone=TEXT("Bone_C2_IceBow");
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Arrow") float ArrowFallbackHeightOffset=0.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Arrow",meta=(ClampMin="1")) float ArrowSpeed=2200.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Arrow",meta=(ClampMin="0")) float ArrowRadius=25.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Arrow",meta=(ClampMin="0.01")) float ArrowLifetime=3.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(ClampMin="0.01")) float OriginWaveWidthCells=2.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules",meta=(ClampMin="0")) int32 ScepterSplashRadiusCells=1;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float BowAttack=1.6f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float PiercingArrow=3.f;
 /** Total damage across the three staff contacts. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float ScepterMain=4.5f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float ScepterSplash=2.25f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float AdvancedScepterMain=6.75f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float AdvancedScepterSplash=3.375f;
 float GetScepterMain(int32 Level) const {return Level>=6?AdvancedScepterMain:ScepterMain;}
 float GetScepterSplash(int32 Level) const {return Level>=6?AdvancedScepterSplash:ScepterSplash;}
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float OriginWave=2.4f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float FinisherMain=7.f;
 /** Retained for old asset compatibility. FinisherMain now applies to every enemy. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Herrscher|Rules") float FinisherOthers=7.f;
};
