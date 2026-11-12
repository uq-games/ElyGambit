#include "Data/EGHeroGrowthSettings.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGStigmataBalance.h"
#include "UObject/UObjectGlobals.h"

UEGHeroGrowthSettings::UEGHeroGrowthSettings()
{
 auto Add=[this](EEGHeroKind Kind,float HM,float HL,float AM,float AL,float DM,float DL)
 {
  FEGHeroGrowthProfile& P=Profiles.AddDefaulted_GetRef();
  P.Kind=Kind;P.HealthMiddle=HM;P.HealthLate=HL;P.AttackMiddle=AM;P.AttackLate=AL;P.DefenseMiddle=DM;P.DefenseLate=DL;
 };
 // The opening preserves each asset's identity; later intervals tune staying power,
 // damage and effective defense independently instead of also scaling attack speed/mana.
 Add(EEGHeroKind::Elysia,        1.05f, .90f, 1.05f, .90f, 1.f, .80f);
 Add(EEGHeroKind::Hua,           1.15f,1.00f, 1.05f, .95f,1.1f, .95f);
 Add(EEGHeroKind::CollapsedHua,  1.15f,1.00f, 1.10f, .95f,1.1f, .95f);
 Add(EEGHeroKind::Pardofelis,    1.10f, .95f, 1.05f, .90f,1.0f, .85f);
 Add(EEGHeroKind::Eden,          1.15f,1.00f, 1.00f, .85f,1.1f, .95f);
 Add(EEGHeroKind::Sakura,        1.10f, .95f, 1.10f, .95f,1.0f, .85f);
 Add(EEGHeroKind::VillV,         1.10f, .95f, 1.05f, .90f,1.0f, .85f);
 Add(EEGHeroKind::Griseo,        1.15f,1.00f, 1.00f, .85f,1.1f, .95f);
 Add(EEGHeroKind::GriseoAdult,   1.15f,1.00f, 1.05f, .90f,1.1f, .95f);
 Add(EEGHeroKind::Mobius,        1.10f, .95f, 1.05f, .90f,1.0f, .85f);
 Add(EEGHeroKind::Aponia,        1.15f,1.00f, 1.00f, .85f,1.1f, .95f);
 Add(EEGHeroKind::ElysiaHerrscher,1.00f, .75f, 1.00f, .75f,1.0f, .75f);
 // Pardo is the only playable base with neither defense nor defense growth;
 // her separate 3-level merge/tier cadence and Can remain unchanged.
 for(FEGHeroGrowthProfile& P:Profiles)if(P.Kind==EEGHeroKind::Pardofelis){P.BaseDefenseBonus=5.f;P.DefenseGrowthBonus=.5f;break;}
}

const UEGHeroGrowthSettings& UEGHeroGrowthSettings::Get(){return *GetDefault<UEGHeroGrowthSettings>();}
const FEGHeroGrowthProfile* UEGHeroGrowthSettings::FindProfile(EEGHeroKind Kind) const
{
 // An ini override can append one identity without restating the complete roster.
 for(int32 Index=Profiles.Num()-1;Index>=0;--Index)if(Profiles[Index].Kind==Kind)return &Profiles[Index];
 return nullptr;
}
float UEGHeroGrowthSettings::WeightedLevels(int32 Level,float Middle,float Late)
{
 const int64 Steps=FMath::Max<int64>(0,int64(Level)-1);
 const float Early=float(FMath::Min<int64>(Steps,5));
 const float Mid=float(FMath::Min<int64>(FMath::Max<int64>(0,Steps-5),5));
 const float Tail=float(FMath::Max<int64>(0,Steps-10));
 return Early+Mid*(FMath::IsFinite(Middle)?FMath::Max(0.f,Middle):1.f)+Tail*(FMath::IsFinite(Late)?FMath::Max(0.f,Late):1.f);
}
int32 UEGHeroGrowthSettings::ResolveEliteLevel(int32 StageNumber,int32 SourceLevel,int32 Corruption) const
{
 if(StageNumber!=3&&StageNumber!=6)return FMath::Max(1,SourceLevel);
 const auto& Old=UEGStigmataBalanceSettings::Get();
 const int32 CorruptionBonus=FMath::Max(0,Corruption<40?Old.CopyLevelBonusLow:Corruption<=70?Old.CopyLevelBonusMid:Old.CopyLevelBonusHigh);
 const int32 Minimum=FMath::Max(1,StageNumber==3?Stage3MinimumLevel:Stage6MinimumLevel);
 const int32 Extra=FMath::Max(0,StageNumber==3?Stage3ExtraLevel:Stage6ExtraLevel);
 const int64 Increased=int64(FMath::Max(1,SourceLevel))+Extra+CorruptionBonus;
 return int32(FMath::Min<int64>(MAX_int32,FMath::Max<int64>(Minimum,Increased)));
}
UEGHeroDefinition* UEGHeroGrowthSettings::MakeEliteDefinition(UObject* Outer,const UEGHeroDefinition* Source,int32 StageNumber) const
{
 if(!IsValid(Outer)||!IsValid(Source)||(StageNumber!=3&&StageNumber!=6))return nullptr;
 auto* Copy=DuplicateObject<UEGHeroDefinition>(Source,Outer);
 if(!Copy)return nullptr;
 Copy->bEliteEnemy=true;
 if(Source->bUseSplitConfiguration)
 {
  if(!Source->StatsDefinition)return nullptr;
  Copy->StatsDefinition=DuplicateObject<UEGHeroStatsDefinition>(Source->StatsDefinition,Copy);
  if(!Copy->StatsDefinition)return nullptr;
 }
 FEGStatBlock& Base=Copy->bUseSplitConfiguration?Copy->StatsDefinition->BaseStats:Copy->BaseStats;
 FEGStatBlock& Growth=Copy->bUseSplitConfiguration?Copy->StatsDefinition->GrowthPerLevel:Copy->GrowthPerLevel;
 const float RawHealth=StageNumber==3?Stage3HealthMultiplier:Stage6HealthMultiplier;
 const float RawAttack=StageNumber==3?Stage3AttackMultiplier:Stage6AttackMultiplier;
 const float Health=FMath::IsFinite(RawHealth)?FMath::Max(.01f,RawHealth):1.f;
 const float Attack=FMath::IsFinite(RawAttack)?FMath::Max(.01f,RawAttack):1.f;
 Base.MaxHealth*=Health;Growth.MaxHealth*=Health;
 Base.AttackPower*=Attack;Growth.AttackPower*=Attack;
 return Copy;
}
