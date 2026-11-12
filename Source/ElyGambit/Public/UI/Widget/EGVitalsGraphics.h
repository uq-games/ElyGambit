#pragma once
#include "CoreMinimal.h"
#include "Components/ProgressBar.h"
#include "Components/Widget.h"
#include "EGVitalsGraphics.generated.h"

/**
 * Shared overhead-vitals tokens, authored for the Wangzhe Wanxiang Qi (WXQ)
 * visual language: ink-navy capsules, champagne hairline rims, cyan mana,
 * glossy specular bands and medal-style level badges.
 *
 * Logical pixels. The screen-space widget layer applies the project's DPI once.
 */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGUnitVitalsStyle
{
 GENERATED_BODY()
 // ---- Layout (logical px) ----
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") float RingDiameter=36.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D HealthSize=FVector2D(80,10);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") FVector2D ManaSize=FVector2D(64,5);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") float LevelBadgeSize=18.f;
 /** Badge floats left of the health bar and overlaps its cap by this many px. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") float BadgeOverlap=4.f;
 /** Toggle: false = medallion attached left of the bars, true = medallion centered above them. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") bool bBadgeAboveBars=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") float HeadGap=8.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") float BarGap=2.f;
 /** 0 = continuous health fill, N>0 = subtle separator ticks. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") int32 HealthSegments=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") int32 ManaSegments=3;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") int32 PreparationFontSize=14;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Layout") int32 CombatFontSize=14;

 // ---- Camp colours (sampled from the WXQ reference) ----
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color") FLinearColor Ally=FLinearColor::FromSRGBColor(FColor(153,211,122));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color") FLinearColor Enemy=FLinearColor::FromSRGBColor(FColor(179,58,53));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color") FLinearColor Mana=FLinearColor::FromSRGBColor(FColor(72,183,239));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color") FLinearColor AllyHighlight=FLinearColor::FromSRGBColor(FColor(201,238,169));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color") FLinearColor EnemyHighlight=FLinearColor::FromSRGBColor(FColor(232,123,110));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color") FLinearColor ManaHighlight=FLinearColor::FromSRGBColor(FColor(154,226,255));

 // ---- WXQ chrome: capsule frame / track / shield ----
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|WXQ") FLinearColor FrameBlack=FLinearColor::FromSRGBColor(FColor(4,6,10));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|WXQ") FLinearColor Rim=FLinearColor::FromSRGBColor(FColor(226,213,180));
 /** Cool-cyan hairline reserved for the mana capsule (WXQ mage blue). */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|WXQ") FLinearColor ManaRim=FLinearColor(0.635f,0.878f,1.f,.45f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|WXQ") FLinearColor TrackTop=FLinearColor::FromSRGBColor(FColor(35,43,57));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|WXQ") FLinearColor TrackBottom=FLinearColor::FromSRGBColor(FColor(10,14,21));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|WXQ") FLinearColor Shield=FLinearColor::FromSRGBColor(FColor(216,244,255));

 // ---- Level medal ----
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeRim=FLinearColor::FromSRGBColor(FColor(214,193,150));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeRimBright=FLinearColor::FromSRGBColor(FColor(246,234,208));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeRimShadow=FLinearColor::FromSRGBColor(FColor(140,116,72));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeFace=FLinearColor::FromSRGBColor(FColor(22,32,47));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeFaceGloss=FLinearColor::FromSRGBColor(FColor(53,70,95));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeAwakenedRim=FLinearColor::FromSRGBColor(FColor(235,203,132));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeAwakenedFace=FLinearColor::FromSRGBColor(FColor(58,43,16));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeNumber=FLinearColor::FromSRGBColor(FColor(255,246,228));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Medal") FLinearColor BadgeAwakenedNumber=FLinearColor::FromSRGBColor(FColor(255,231,173));

 // ---- Awakening ring ----
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Ring") FLinearColor RingRim=FLinearColor::FromSRGBColor(FColor(201,176,126));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Ring") FLinearColor RingDisk=FLinearColor::FromSRGBColor(FColor(10,15,23));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Ring") FLinearColor RingTrack=FLinearColor::FromSRGBColor(FColor(26,36,51));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Ring") FLinearColor RingProgress=FLinearColor::FromSRGBColor(FColor(111,216,240));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Color|Ring") FLinearColor RingAwakened=FLinearColor::FromSRGBColor(FColor(242,200,120));
};

/**
 * A normal UProgressBar API whose Slate paint is a WXQ ink-gold capsule:
 * black outer frame, champagne hairline, navy track, glossy camp-coloured fill,
 * optional segment separators and a shield overlay with a bright leading edge.
 */
UCLASS(meta=(DisplayName="WXQ Vitals Capsule"))
class ELYGAMBIT_API UEGVitalsBar : public UProgressBar
{
 GENERATED_BODY()
public:
 /** Legacy-compatible frame thickness. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") float BorderWidth=2.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") FLinearColor TrackColor=FLinearColor::FromSRGBColor(FColor(10,14,21));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") FLinearColor TrackTopColor=FLinearColor::FromSRGBColor(FColor(35,43,57));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") FLinearColor FrameColor=FLinearColor::FromSRGBColor(FColor(4,6,10));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") FLinearColor RimColor=FLinearColor::FromSRGBColor(FColor(200,178,140));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") FLinearColor HighlightColor=FLinearColor::FromSRGBColor(FColor(180,227,154));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") FLinearColor ShieldColor=FLinearColor::FromSRGBColor(FColor(216,244,255));
 /** Emphasise low-health frames with a warm danger glow. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") bool bDangerGlow=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Vitals") int32 Segments=0;
 void SetHighlightColor(FLinearColor Value);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Vitals") float ShieldFraction=0.f;
 UFUNCTION(BlueprintCallable,Category="Vitals") void SetShieldFraction(float Value);
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
};

/** Circular WXQ level medallion: beveled champagne ring, ink face, gloss arcs. */
UCLASS(meta=(DisplayName="WXQ Level Medal"))
class ELYGAMBIT_API UEGLevelMedal : public UWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor Rim=FLinearColor::FromSRGBColor(FColor(201,177,131));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor RimBright=FLinearColor::FromSRGBColor(FColor(246,234,208));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor RimShadow=FLinearColor::FromSRGBColor(FColor(138,114,69));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor Face=FLinearColor::FromSRGBColor(FColor(24,34,49));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor FaceGloss=FLinearColor::FromSRGBColor(FColor(52,70,95));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor InnerLine=FLinearColor::FromSRGBColor(FColor(201,177,131));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor AwakenedRim=FLinearColor::FromSRGBColor(FColor(232,200,126));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor AwakenedFace=FLinearColor::FromSRGBColor(FColor(199,169,107));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Medal") FLinearColor AwakenedGlow=FLinearColor::FromSRGBColor(FColor(255,226,150));
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Medal") bool bAwakened=false;
 UFUNCTION(BlueprintCallable,Category="Medal") void SetAwakened(bool bValue);
 virtual void SynchronizeProperties() override;
 virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
private:
 TSharedPtr<SWidget> MedalWidget;
};

/** Each clockwise segment represents an additional matching card, excluding the initial hero card. */
UCLASS(meta=(DisplayName="Awakening Progress Ring"))
class ELYGAMBIT_API UEGAwakeningRing : public UWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ring") float StrokeWidth=3.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ring") FLinearColor FrameColor=FLinearColor::FromSRGBColor(FColor(201,176,126));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ring") FLinearColor DiskColor=FLinearColor::FromSRGBColor(FColor(10,15,23));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ring") FLinearColor TrackColor=FLinearColor::FromSRGBColor(FColor(26,36,51));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ring") FLinearColor ProgressColor=FLinearColor::FromSRGBColor(FColor(111,216,240));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Ring") FLinearColor AwakenedColor=FLinearColor::FromSRGBColor(FColor(242,200,120));
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Ring") float Progress=0.f;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Ring") int32 Segments=2;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="Ring") bool bAwakened=false;
 /** Cards and Required are roster totals including the first owned card. */
 UFUNCTION(BlueprintCallable,Category="Ring") void SetProgress(int32 Cards,int32 Required,bool Awakened);
 virtual void SynchronizeProperties() override;
 virtual void ReleaseSlateResources(bool bReleaseChildren) override;
protected:
 virtual TSharedRef<SWidget> RebuildWidget() override;
private:
 TSharedPtr<SWidget> RingWidget;
};
