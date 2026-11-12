#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "EGSupportEffects.generated.h"
UCLASS()
class ELYGAMBIT_API UEGPaintAttackEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: UEGPaintAttackEffect();
};
UCLASS()
class ELYGAMBIT_API UEGHuaArmorEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: UEGHuaArmorEffect();
};
UCLASS()
class ELYGAMBIT_API UEGMeteorStunEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: UEGMeteorStunEffect();
};
UCLASS()
class ELYGAMBIT_API UEGCompanionSpeedEffect : public UGameplayEffect
{
 GENERATED_BODY()
public: UEGCompanionSpeedEffect();
};
