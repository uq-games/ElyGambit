#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Types/EGChessTypes.h"
#include "EGHitFeedbackComponent.generated.h"
class AEGChessUnit;
class UMaterialInterface;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHitFeedbackProfile : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<TSoftObjectPtr<UMaterialInterface>> Layers;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Tint=FLinearColor(1.f,.8f,.35f,1);
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.01")) float SizeInBodyHeights=.25f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.01")) float Duration=.26f;
 /** Layers become alternative complete animations: slash first, blunt second. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bAnimatedAtlas=false;
};

/** Target-owned cosmetic listener. Gameplay damage is already committed by GAS. */
UCLASS(ClassGroup=(Presentation),meta=(BlueprintSpawnableComponent))
class ELYGAMBIT_API UEGHitFeedbackComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGHitFeedbackComponent();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hit Feedback") TSoftObjectPtr<UEGHitFeedbackProfile> Profile;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Hit Feedback") int32 PlayedCount=0;
 UFUNCTION(BlueprintCallable,Category="Hit Feedback") void ClearFeedback();
 int32 GetActiveBurstCount() const;
 int32 GetVisiblePartCount() const;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Hit Feedback") FVector LastImpactPoint=FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Hit Feedback") int32 LastVariant=0;
private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Unit;
 UPROPERTY(Transient) TObjectPtr<UEGHitFeedbackProfile> Data;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
 TArray<float> Ages;
 TArray<FVector> Points;
 TArray<FVector> Directions;
 TArray<int32> Variants;
 TArray<float> ImpactScales,ImpactDurations;
 TArray<FLinearColor> ImpactTints;
 int32 Cursor=0;
 UFUNCTION() void OnResolved(const FEGCombatResolution& Receipt);
 UFUNCTION() void OnState(EEGUnitState State);
 bool BuildPool();
};
