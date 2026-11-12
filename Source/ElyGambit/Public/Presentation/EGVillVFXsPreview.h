#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "Presentation/EGSourceEffect.h"
#include "EGVillVFXsPreview.generated.h"
class UNiagaraSystem;
class UNiagaraComponent;
class UEGSourceEffectDefinition;
class UEGSourcePreviewDefinition;
class AEGSourceCharacterPreview;

USTRUCT(BlueprintType)
struct FEGVillVFXsRendererSettings
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadOnly) int32 RenderMode=0;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float LengthScale=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) float VelocityScale=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadOnly) FVector Pivot=FVector::ZeroVector;
};
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGVillVFXsSampleDefinition : public UEGSourceEffectDefinition
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadOnly) TArray<FEGVillVFXsRendererSettings> SourceRenderers;
};

UENUM(BlueprintType)
enum class EEGVillVFXsAnchor : uint8 { Muzzle, Projectile, Target };

/** Isolated source-effect playback. No combat ability, hit or replication state. */
USTRUCT(BlueprintType)
struct FEGVillVFXsLayer
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UNiagaraSystem> System;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UEGSourceEffectDefinition> Samples;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) EEGVillVFXsAnchor Anchor=EEGVillVFXsAnchor::Muzzle;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float StartTime=0.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float FlightDuration=.3f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float EffectScale=1.f;
    /** Native Niagara simulation, independent from sampled source-frame playback. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite) bool bNativeSimulation=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,meta=(ClampMin="0.01")) float NativeDuration=1.f;
};
USTRUCT(BlueprintType)
struct FEGVillVFXsPreset
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FText Label;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UEGSourcePreviewDefinition> Rig;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName MuzzlePartId;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FName MuzzleBone;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) float Duration=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FEGVillVFXsLayer> Layers;
};
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGVillVFXsPreviewDefinition : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<FEGVillVFXsPreset> Presets;
    UPROPERTY(EditAnywhere,BlueprintReadWrite) FString SourceAndLimitations;
};
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGVillVFXsPreview : public AActor
{
    GENERATED_BODY()
public:
    AEGVillVFXsPreview();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs") TObjectPtr<UEGVillVFXsPreviewDefinition> Definition;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs",meta=(ClampMin="0",ClampMax="3")) int32 PresetIndex=0;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs") bool bLoop=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs") bool bAutoPlay=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs") FVector TargetOffset=FVector(650,0,100);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs",meta=(ClampMin="0.01")) float BoardEffectScale=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="VillV FXs") bool bUseSourceScale=false;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="VillV FXs") float PreviewTime=0.f;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Transient,Category="VillV FXs") bool bPlaying=false;
    UFUNCTION(BlueprintCallable,CallInEditor,Category="VillV FXs") void Play();
    UFUNCTION(BlueprintCallable,CallInEditor,Category="VillV FXs") void PlayOnce();
    UFUNCTION(BlueprintCallable,CallInEditor,Category="VillV FXs") void Stop();
    UFUNCTION(BlueprintCallable,CallInEditor,Category="VillV FXs") void NextPreset();
    UFUNCTION(BlueprintCallable,Category="VillV FXs") void Seek(float Time);
    virtual void Tick(float DeltaSeconds) override;
    virtual bool ShouldTickIfViewportsOnly() const override {return true;}
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void Destroyed() override;
private:
    UPROPERTY(Transient) TObjectPtr<AEGSourceCharacterPreview> RigActor;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> EffectComponents;
    TMap<int32,FTransform> BirthAnchors;
    int32 BuiltPreset=INDEX_NONE;
    UPROPERTY(Transient) TObjectPtr<UEGVillVFXsPreviewDefinition> BuiltDefinition;
    void ResetPlayback();
    void Evaluate();
    void ToggleLoop(){bLoop=!bLoop;}
};
