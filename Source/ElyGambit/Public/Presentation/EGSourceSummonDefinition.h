#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EGSourceSummonDefinition.generated.h"
class UEGSourceEffectDefinition;
class UMaterialInterface;

/** Sampled source descent, rebased to the fixed impact point and fitted to the board. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGSourceSummonDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FString SourceDescription;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Duration=1.1f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float HeightInSummonerUnits=1.1f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bDropFromTargetPortal=false;
 /** Time warp of the original descent curve; below one reveals vertical travel earlier. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.1",ClampMax="2")) float DescentTimeExponent=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FVector> NormalizedOffsets;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UEGSourceEffectDefinition> DescentEffect;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> DescentMaterial;
 bool IsValidMotion() const
 {return FMath::IsFinite(Duration)&&Duration>0&&FMath::IsFinite(HeightInSummonerUnits)&&HeightInSummonerUnits>0&&FMath::IsFinite(DescentTimeExponent)&&DescentTimeExponent>=.1f&&DescentTimeExponent<=2.f&&NormalizedOffsets.Num()>=2&&NormalizedOffsets.Last().IsNearlyZero()&&!NormalizedOffsets.ContainsByPredicate([](const FVector& P){return P.ContainsNaN();});}
};
