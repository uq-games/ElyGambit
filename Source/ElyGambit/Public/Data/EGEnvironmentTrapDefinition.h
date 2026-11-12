#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EGEnvironmentTrapDefinition.generated.h"
class UMaterialInterface;
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGEnvironmentTrapDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,meta=(ClampMin="0")) float Damage=30;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,meta=(ClampMin="0.01")) float TriggerRadiusCells=.30f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,meta=(ClampMin="0.01")) float ExplosionRadiusCells=.45f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,meta=(ClampMin="0")) float TriggerDelay=.6f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly) TObjectPtr<UMaterialInterface> Material;
 bool IsValidConfiguration() const {return FMath::IsFinite(Damage)&&Damage>=0&&FMath::IsFinite(TriggerRadiusCells)&&TriggerRadiusCells>0&&FMath::IsFinite(ExplosionRadiusCells)&&ExplosionRadiusCells>=TriggerRadiusCells&&FMath::IsFinite(TriggerDelay)&&TriggerDelay>=0;}
};
