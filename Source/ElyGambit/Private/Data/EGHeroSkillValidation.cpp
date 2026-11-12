#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Data/EGGriseoAdultSkillDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"

bool UEGElysiaSkillDefinition::HasValidSkillRules() const
{
 return FMath::IsFinite(NormalAttackMultiplier)&&NormalAttackMultiplier>=0&&ChargedAttackInterval>=1
  &&FMath::IsFinite(ChargedMultiplier)&&ChargedMultiplier>0
  &&FMath::IsFinite(EmpoweredSeconds)&&EmpoweredSeconds>0&&FMath::IsFinite(EmpoweredRadius)&&EmpoweredRadius>=0
  &&FMath::IsFinite(DomeRicochetMultiplier)&&DomeRicochetMultiplier>=0
  &&FMath::IsFinite(ArrowRainSeconds)&&ArrowRainSeconds>0.f&&ArrowRainPulses>=1&&ArrowRainPulses<=64
  &&FMath::IsFinite(ArrowRainRadiusCells)&&ArrowRainRadiusCells>0.f&&FMath::IsFinite(ArrowRainTotalMultiplier)&&ArrowRainTotalMultiplier>=0.f
  &&FMath::IsFinite(ArrowFlightSeconds)&&ArrowFlightSeconds>0;
}
bool UEGElysiaHerrscherSkillDefinition::HasValidSkillRules() const
{
 const auto Valid=[](float V){return FMath::IsFinite(V)&&V>=0.f;};
 return Valid(InitialMana)&&Valid(MaxMana)&&MaxMana>0.f&&InitialMana<=MaxMana&&Valid(ManaPerFinishedBowAttack)
  &&BowAttackInterval>=1&&OriginAttackInterval>=1&&Valid(ArrowLaneWidthCells)&&ArrowLaneWidthCells>0.f
  &&Valid(ArrowSpeed)&&ArrowSpeed>0.f&&Valid(ArrowRadius)&&Valid(ArrowLifetime)&&ArrowLifetime>0.f&&FMath::IsFinite(ArrowFallbackHeightOffset)
  &&Valid(OriginWaveWidthCells)&&OriginWaveWidthCells>0.f&&ScepterSplashRadiusCells>=0
  &&Valid(ShieldHealthRatio)&&ShieldHealthRatio<=1.f&&FMath::IsFinite(OriginSeconds)&&OriginSeconds>0.f
  &&FMath::IsFinite(OriginActionPlaybackRate)&&OriginActionPlaybackRate>0.f
  &&Valid(BowAttack)&&Valid(PiercingArrow)&&Valid(ScepterMain)&&Valid(ScepterSplash)&&Valid(AdvancedScepterMain)&&Valid(AdvancedScepterSplash)
  &&Valid(OriginWave)&&Valid(FinisherMain)&&Valid(FinisherOthers);
}
bool UEGEdenSkillDefinition::HasValidSkillRules() const
{return FMath::IsFinite(HealingMultiplier)&&HealingMultiplier>=0&&FMath::IsFinite(HealingRadiusCells)&&HealingRadiusCells>0&&FMath::IsFinite(AdvancedSongManaPerSecond)&&AdvancedSongManaPerSecond>=0&&AttacksPerNote>=1&&SongSeconds>=1&&FMath::IsFinite(NoteMana)&&NoteMana>=0&&FMath::IsFinite(SongManaPerSecond)&&SongManaPerSecond>=0&&FMath::IsFinite(UltimateRadius)&&UltimateRadius>0;}
bool UEGAponiaSkillDefinition::HasValidSkillRules() const
{return FMath::IsFinite(PenaltyMultiplier)&&PenaltyMultiplier>=0&&FMath::IsFinite(AdvancedPenaltyMultiplier)&&AdvancedPenaltyMultiplier>=0&&FMath::IsFinite(JudgmentSeconds)&&JudgmentSeconds>0&&FMath::IsFinite(UltimateRadius)&&UltimateRadius>0&&FMath::IsFinite(AllyAttackMana)&&AllyAttackMana>=0&&FMath::IsFinite(LightningMultiplier)&&LightningMultiplier>=0&&FMath::IsFinite(LightningInterval)&&LightningInterval>=.05f&&DisciplineLimit>=1&&DisciplineLimit<=6&&FMath::IsFinite(PenaltyDamageShare)&&PenaltyDamageShare>=0.f&&PenaltyDamageShare<=1.f&&FMath::IsFinite(PenaltyInterval)&&PenaltyInterval>=.05f;}
bool UEGGriseoSkillDefinition::HasValidSkillRules() const
{
 const auto Positive=[](float V){return FMath::IsFinite(V)&&V>0;};
 const auto Ratio=[](float V){return FMath::IsFinite(V)&&V>=0&&V<=1;};
 return Positive(SupportSeconds)&&Positive(SupportRadiusCells)&&Ratio(ShieldRatio)&&Ratio(ShieldRatioAdvanced)&&Ratio(AttackBonus)
  &&Positive(KnightHealthRatio)&&FMath::IsFinite(KnightAttackRatio)&&KnightAttackRatio>=0;
}

bool UEGGriseoAdultSkillDefinition::HasValidSkillRules() const
{
 return FMath::IsFinite(AdvancedTeamSpeedBonus)&&AdvancedTeamSpeedBonus>=0&&FMath::IsFinite(MeteorStunSeconds)&&MeteorStunSeconds>0&&FMath::IsFinite(UltimateRadius)&&UltimateRadius>0&&FMath::IsFinite(TeamSpeedBonus)&&TeamSpeedBonus>=0
  &&FMath::IsFinite(TeamSpeedSeconds)&&TeamSpeedSeconds>0&&!MeteorImpact.IsNull()
  &&FMath::IsFinite(PassiveSpeedBonus)&&PassiveSpeedBonus>=0
  &&FMath::IsFinite(KnightHealthRatio)&&KnightHealthRatio>0&&FMath::IsFinite(KnightAttackRatio)&&KnightAttackRatio>=0;
}

bool UEGSakuraSkillDefinition::HasValidSkillRules() const
{
 return FMath::IsFinite(CounterMultiplier)&&CounterMultiplier>=0.f&&FMath::IsFinite(KillMana)&&KillMana>=0.f
  &&FMath::IsFinite(ExecuteHealthFraction)&&ExecuteHealthFraction>0.f&&ExecuteHealthFraction<=1.f
  &&FMath::IsFinite(EvadeCooldownSeconds)&&EvadeCooldownSeconds>0.f
  &&FMath::IsFinite(DelayedSlashSeconds)&&DelayedSlashSeconds>0.f
  &&FMath::IsFinite(FrontalEvadeDot)&&FrontalEvadeDot>=-1.f&&FrontalEvadeDot<=1.f;
}
