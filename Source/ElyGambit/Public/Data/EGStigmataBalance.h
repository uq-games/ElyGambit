#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EGStigmataBalance.generated.h"

/** Shared balance source for gameplay, candidate previews and card descriptions. */
UCLASS(Config=Game, DefaultConfig)
class ELYGAMBIT_API UEGStigmataBalanceSettings : public UObject
{
 GENERATED_BODY()
public:
 static const UEGStigmataBalanceSettings& Get();
 UPROPERTY(Config,EditDefaultsOnly,Category="Combat",meta=(ClampMin="0.01")) float DefenseConstant=100.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Combat",meta=(ClampMin="0",ClampMax="1")) float InitialHeroMana=.2f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float LoveTeamSpeed=.15f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float LoveSpeed=.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float FlawlessSkillBonus=.25f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float FlawlessSeconds=3.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float FlawlessFinisherMultiplier=1.5f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DeliveranceBonus=.50f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DeliveranceDuration=8.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DisciplineStep=.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float GoldOpeningMana=.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float GoldRefund=.35f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") int32 HelixCharges=3;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float HelixDamage=1.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float HelixRadiusCells=1.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DecimationStep=.05f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DecimationMissingHealthScale=1.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DecimationMaxBonus=.60f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DecimationLifesteal=.10f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") int32 WisdomAttacks=6;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float WisdomDamage=2.40f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float WisdomRadiusCells=1.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float SetsunaSpeed=.40f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float SetsunaSpeedDuration=4.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float SetsunaCooldown=6.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DaybreakDamage=.50f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DaybreakDuration=3.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DaybreakShieldBonus=.50f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float InfinityBonus=.20f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float FormSkillBonus=.25f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float DuskShield=.35f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") int32 DuskGold=1;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float KindredDamage=4.80f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float BanquetMana=.30f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Stigmata") float BanquetCooldown=8.f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Endgame") float UnityDamageBonus=.35f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Endgame") float UnityHealthBonus=.25f;
 UPROPERTY(Config,EditDefaultsOnly,Category="Endgame") int32 StoryMinimumLevel=6;
 UPROPERTY(Config,EditDefaultsOnly,Category="Economy") int32 StartingGold=6;
 UPROPERTY(Config,EditDefaultsOnly,Category="Economy") int32 BaseFreeRefreshesPerPreparation=1;
 UPROPERTY(Config,EditDefaultsOnly,Category="Economy") int32 ReverieExtraFreeRefreshes=1;
 UPROPERTY(Config,EditDefaultsOnly,Category="Economy") int32 ReverieGold=3;
 UPROPERTY(Config,EditDefaultsOnly,Category="Economy") int32 SaleRefundNumerator=2;
 UPROPERTY(Config,EditDefaultsOnly,Category="Economy") int32 SaleRefundDenominator=3;
 UPROPERTY(Config,EditDefaultsOnly,Category="Encounter") int32 CopyLevelBonusLow=0;
 UPROPERTY(Config,EditDefaultsOnly,Category="Encounter") int32 CopyLevelBonusMid=1;
 UPROPERTY(Config,EditDefaultsOnly,Category="Encounter") int32 CopyLevelBonusHigh=2;
 UPROPERTY(Config,EditDefaultsOnly,Category="Encounter") int32 CopyLevelCap=11;
 UPROPERTY(Config,EditDefaultsOnly,Category="Encounter") TArray<float> OrdinaryStageHealthMultipliers={.85f,.90f,1.f,1.f,1.25f,1.10f,1.f};
};
