#pragma once
#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "EGUltimateRingComponent.generated.h"
class USkeletalMeshComponent;
class AEGSkillProjectile;

/** Board-readable ultimate weapon. Never drives gameplay or the ordinary attack weapon. */
UCLASS(ClassGroup=(Presentation),meta=(BlueprintSpawnableComponent))
class ELYGAMBIT_API UEGUltimateRingComponent : public UStaticMeshComponent
{
 GENERATED_BODY()
public:
 UEGUltimateRingComponent();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function) override;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring",meta=(ClampMin="0.1")) float DiameterInBodyHeights=1.25f;
 /** Compatibility fallback; normal playback uses the authored source trajectory. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring",meta=(AdvancedDisplay)) float CenterHeightInBodyHeights=1.9f;
 /** Midpoints of the real thrown weapon, board-fitted in the presentation mesh basis. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Ultimate Ring") TArray<FVector> AirborneCenters;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Ultimate Ring") float SourceStartTime=.3f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Ultimate Ring") float SourceEndTime=.65f;
 /** Private board adaptation: throw to the victim's overhead portal before Can emerges. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Target") bool bTargetOverhead=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Target",meta=(ClampMin="0")) float TargetClearanceInBodyHeights=2.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Target",meta=(ClampMin="0")) float TravelArcInBodyHeights=.75f;
 FVector ResolveOverheadCenter(FVector ImpactRoot,float TargetHeight) const;
 void BeginDescent(AEGSkillProjectile* Flight);
 UFUNCTION(BlueprintPure,Category="Ultimate Ring") FVector GetOverheadCenter() const {return OverheadCenter;}

 // ---- WXQ-style anticipation / release polish ------------------------------
 /** Materialise the big ring above Pardo before the source throw window instead of popping at the throw. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Anticipation") bool bAnticipateBeforeThrow=true;
 /** Montage seconds of conjure/charge time before the source throw frame. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Anticipation",meta=(ClampMin="0")) float AnticipationLeadSeconds=.7f;
 /** Ring scale at the start of the conjure, easing to 1 at the throw frame. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Anticipation",meta=(ClampMin="0.1",ClampMax="1")) float AnticipationScale=.55f;
 /** Normalized body-height center the ring is conjured at; it rises to the first source sample. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Anticipation") float AnticipationHeightInBodyHeights=1.35f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Spin") float AnticipationSpinDegPerSec=520.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Spin") float FlightSpinDegPerSec=200.f;
 /** Shrink the ring away after the source throw window so it never hovers statically. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Release") bool bFadeAfterSourceEnd=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ultimate Ring|Release",meta=(ClampMin="0.02")) float ReleaseFadeSeconds=.12f;
 UFUNCTION(BlueprintCallable,Category="Ultimate Ring") void Refresh();
private:
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> HiddenOnMesh;
 TArray<FName> HiddenByUs;
 UPROPERTY(Transient) TWeakObjectPtr<AEGSkillProjectile> ActiveDescent;
 FVector OverheadCenter=FVector::ZeroVector;
 void RestoreWeapon();
};
