#pragma once
#include "CoreMinimal.h"
#include "Components/ContentWidget.h"
#include "EGGradientSurface.generated.h"

/**
 * Draws directly in Slate; child artwork is never rasterized into a smaller target.
 *
 * Two presentation channels are composed exactly once here:
 *  - Reveal is the legacy opening value and no longer clips or skews the shape.
 *  - Compact narrows the painted rectangle symmetrically toward the top centre
 *    and lifts its lower edge while the shop collapses to its drag state.
 */
UCLASS()
class ELYGAMBIT_API UEGGradientSurface : public UContentWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") FLinearColor TopColor=FLinearColor(0,0,0,.82f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") FLinearColor BottomColor=FLinearColor::Transparent;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") float CornerRadius=0.f;
 /** Fade left to right instead of top to bottom. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") bool bHorizontalGradient=false;
 /** Keep the initial colour solid over this fraction before fading. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance",meta=(ClampMin="0",ClampMax="0.95")) float SolidFraction=0.f;
 /** Optional eased fade after the solid portion; false preserves existing gradients. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") bool bSmoothFade=false;
 /** Legacy authoring flag; the shape is never skewed or squeezed any more. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Appearance") bool bTrapezoidReveal=false;
 void SetReveal(float Value);
 void SetCompact(float Value);
 /** Compact target rectangle in the owning design space; Y always starts at 0. */
 void SetCompactGeometry(FVector2D Size);
 void SetGradient(FLinearColor Top,FLinearColor Bottom);
 void SetSolidFraction(float Value);
 virtual void SynchronizeProperties() override;
 virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
 virtual void OnSlotAdded(UPanelSlot* InSlot) override;
 virtual void OnSlotRemoved(UPanelSlot* InSlot) override;
private:
 TSharedPtr<class SEGGradientSurface> Surface;
 float Reveal=1.f;
 FVector2D CompactSize=FVector2D::ZeroVector;
};
