#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "EGSourceEffect.generated.h"
class UStaticMesh;
class UMaterialInterface;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UEGHeroSkillComponent;
class USkeletalMeshComponent;
class UCurveFloat;
class UEGNativeMeshPassDefinition;

/** Deterministic samples of a supported source emitter; no gameplay or collision data. */
USTRUCT(BlueprintType)
struct FEGSourceEffectSample
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FTransform Transform;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor Color=FLinearColor::White;
 /** UV tile transform from the source texture-sheet animation module. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor TextureUV=FLinearColor(1,1,0,0);
 /** Native particle streams, evaluated by the isolated source preview only. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor CustomData1=FLinearColor(0,0,0,0);
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FLinearColor CustomData2=FLinearColor(0,0,0,0);
};
USTRUCT(BlueprintType)
struct FEGSourceEffectTrack
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> Material;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float StartTime=0;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float Duration=.3f;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bCameraFacing=false;
 /** Original Unity particle render alignment: 0=view, 3=facing. INDEX_NONE
     keeps compatibility with previously imported previews. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 SourceBillboardAlignment=INDEX_NONE;
 /** Freeze the preview attachment pose at particle birth for source world simulation. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bWorldSimulation=false;
 /** Recovered source depth-only pass. Shares its source mesh and animation;
     supplies occlusion for a following translucent body/outline pass. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bSourceDepthOnly=false;
 /** Original signed distortion-buffer pass. Its encoded data is never visible
     scene color; a per-world source composite owns the accumulation target. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bSourceDistortion=false;
 /** Optional original fixed-function passes. Existing converted effects keep this empty. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UEGNativeMeshPassDefinition> NativePassDefinition;
 /** Source clip scalar/channel curves, named for the live native shader parameter. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TMap<FName,TObjectPtr<UCurveFloat>> MaterialCurves;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TObjectPtr<UCurveFloat> VisibilityCurve;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGSourceEffectSample> Samples;
 bool IsSourceVisible(float Time) const;
 bool ApplyNativeRendering(UStaticMeshComponent* Component,UObject* LifetimeOwner,float Time) const;
};

/** Convert the sampled source rotation back into its view-plane axes before
    camera alignment. The source quad's native rotation is never discarded. */
ELYGAMBIT_API FQuat EGSourceBillboardRotation(const FQuat& SampleRotation,const FVector& FacingNormal,const FVector& CameraRight);
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGSourceEffectDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FString SourceDescription;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FString ConversionLimitations;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) bool bTargetGround=false;
 /** Source-space anchor mapped to the animated weapon socket, without scaling effect geometry twice. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FName AnchorBone;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector SourceAnchor=FVector::ZeroVector;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) float TargetHeightFraction=0.f;
 /** World-space multiplier on source centimetres; independent of the unit mesh scale. */
 UPROPERTY(EditAnywhere,BlueprintReadOnly,meta=(ClampMin="0.01")) float PresentationScale=1;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TSoftObjectPtr<UMaterialInterface> SourceDistortionComposite;
 UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGSourceEffectTrack> Tracks;
};

/** Nonreplicated source visuals: action-owned trails and round-bound summon impacts. */
UCLASS()
class ELYGAMBIT_API AEGSourceEffect : public AActor
{
 GENERATED_BODY()
public:
 AEGSourceEffect();
 static AEGSourceEffect* Spawn(UEGHeroSkillComponent* Skill,USkeletalMeshComponent* Mesh,UEGSourceEffectDefinition* Definition,float Rate,bool bRoundBound=false);
 /** Fixed world-space, action-bound area effect. ScaleMultiplier is presentation only. */
 static AEGSourceEffect* SpawnAt(UEGHeroSkillComponent* Skill,USkeletalMeshComponent* Mesh,UEGSourceEffectDefinition* Definition,float Rate,const FVector& Point,const FRotator& Rotation,float ScaleMultiplier);
 virtual void Tick(float DeltaSeconds) override;
 void SetPlaybackRate(float Rate);
 float GetPlaybackRate() const {return PlayRate;}
 UFUNCTION(BlueprintPure,Category="Presentation|Source") float GetElapsedTime() const {return Age;}
 /** Read-only identity for runtime inspection without exposing mutable playback state. */
 UFUNCTION(BlueprintPure,Category="Presentation|Source") UEGSourceEffectDefinition* GetSourceDefinition() const {return Data;}
 /** Count of tracks suppressed because their distortion compositor could not be registered. */
 int32 GetRejectedDistortionTrackCount() const {return RejectedDistortionTracks.Num();}
private:
 static AEGSourceEffect* SpawnInternal(UEGHeroSkillComponent* Skill,USkeletalMeshComponent* Mesh,UEGSourceEffectDefinition* Definition,float Rate,bool bRoundBound,const FTransform* FixedTransform);
 UPROPERTY(Transient) TObjectPtr<UEGSourceEffectDefinition> Data;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> ActionOwner;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> AnchorMesh;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Parts;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Materials;
 // Distortion is encoded data, not visible scene color. Never draw a rejected
 // track in the main pass or silently reactivate it on a later tick.
 TSet<int32> RejectedDistortionTracks;
 FGuid ActionToken;
 FGuid BattleRound;
 bool bRoundBound=false;
 float Age=0,PlayRate=1,Duration=0;
};

/** Stateless notify: each playback is validated before a separate cosmetic actor is created. */
UCLASS()
class ELYGAMBIT_API UEGSourceEffectNotify : public UAnimNotify
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TSoftObjectPtr<UEGSourceEffectDefinition> Effect;
 virtual void Notify(USkeletalMeshComponent* Mesh,UAnimSequenceBase* Animation,const FAnimNotifyEventReference& Reference) override;
};
