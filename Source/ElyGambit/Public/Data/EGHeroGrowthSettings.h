#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/EGChessTypes.h"
#include "EGHeroGrowthSettings.generated.h"

class UEGHeroDefinition;

/** Growth increments gained at levels 7-11 and 12+; levels 1-6 retain authored growth. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGHeroGrowthProfile
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,Category="Growth") EEGHeroKind Kind=EEGHeroKind::Generic;
 UPROPERTY(EditAnywhere,Category="Growth",meta=(ClampMin="0")) float HealthMiddle=1.f;
 UPROPERTY(EditAnywhere,Category="Growth",meta=(ClampMin="0")) float HealthLate=1.f;
 UPROPERTY(EditAnywhere,Category="Growth",meta=(ClampMin="0")) float AttackMiddle=1.f;
 UPROPERTY(EditAnywhere,Category="Growth",meta=(ClampMin="0")) float AttackLate=1.f;
 UPROPERTY(EditAnywhere,Category="Growth",meta=(ClampMin="0")) float DefenseMiddle=1.f;
 UPROPERTY(EditAnywhere,Category="Growth",meta=(ClampMin="0")) float DefenseLate=1.f;
 /** Optional flat correction when the authored asset has no meaningful defense progression. */
 UPROPERTY(EditAnywhere,Category="Growth") float BaseDefenseBonus=0.f;
 UPROPERTY(EditAnywhere,Category="Growth") float DefenseGrowthBonus=0.f;
};

/** Runtime tuning; authored hero assets remain the base/growth authority. */
UCLASS(Config=Game,DefaultConfig)
class ELYGAMBIT_API UEGHeroGrowthSettings : public UObject
{
 GENERATED_BODY()
public:
 UEGHeroGrowthSettings();
 static const UEGHeroGrowthSettings& Get();
 const FEGHeroGrowthProfile* FindProfile(EEGHeroKind Kind) const;
 static float WeightedLevels(int32 Level,float Middle,float Late);
 int32 ResolveEliteLevel(int32 StageNumber,int32 SourceLevel,int32 Corruption) const;
 UEGHeroDefinition* MakeEliteDefinition(UObject* Outer,const UEGHeroDefinition* Source,int32 StageNumber) const;

 /** Entries cover every playable base and enhanced identity; unlisted NPCs retain authored linear growth. */
 UPROPERTY(Config,EditAnywhere,Category="Growth") TArray<FEGHeroGrowthProfile> Profiles;
 /** Human source DA currently has no authored growth; the fusion transaction uses these formal Elysia growth values. */
 UPROPERTY(Config,EditAnywhere,Category="Growth|Human",meta=(ClampMin="0")) float HumanFallbackHealthGrowth=65.f;
 UPROPERTY(Config,EditAnywhere,Category="Growth|Human",meta=(ClampMin="0")) float HumanFallbackAttackGrowth=10.f;
 UPROPERTY(Config,EditAnywhere,Category="Growth|Human",meta=(ClampMin="0")) float HumanFallbackDefenseGrowth=1.f;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage3",meta=(ClampMin="1")) int32 Stage3MinimumLevel=3;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage3",meta=(ClampMin="0")) int32 Stage3ExtraLevel=1;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage3",meta=(ClampMin="0.01")) float Stage3HealthMultiplier=2.f;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage3",meta=(ClampMin="0.01")) float Stage3AttackMultiplier=1.15f;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage6",meta=(ClampMin="1")) int32 Stage6MinimumLevel=6;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage6",meta=(ClampMin="0")) int32 Stage6ExtraLevel=2;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage6",meta=(ClampMin="0.01")) float Stage6HealthMultiplier=2.3f;
 UPROPERTY(Config,EditAnywhere,Category="Elite|Stage6",meta=(ClampMin="0.01")) float Stage6AttackMultiplier=1.25f;
};
