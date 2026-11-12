#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGElysiaStarSeaVisual.generated.h"

class AEGDeploymentBoard;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;

/** A local, board-aligned presentation of the Human Herrscher's star sea. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGElysiaStarSeaVisual : public AActor
{
	GENERATED_BODY()

public:
	AEGElysiaStarSeaVisual();

	/** Creates per-instance materials and places the domain over the supplied board. */
	UFUNCTION(BlueprintCallable, Category="Elysia|Star Sea")
	bool InitializeFromBoard(AEGDeploymentBoard* Board);

	/** Fades in from the current visual opacity. */
	UFUNCTION(BlueprintCallable, Category="Elysia|Star Sea")
	void BeginReveal();

	/** Fades out from the current opacity and destroys this visual when finished. */
	UFUNCTION(BlueprintCallable, Category="Elysia|Star Sea")
	void BeginDismiss();

	virtual void Tick(float DeltaSeconds) override;

	/** Board-space Niagara replacement. Reference dimensions describe the authored system extent in cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Niagara")
	TObjectPtr<UNiagaraSystem> DomainSystem;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Niagara")
	FVector2D DomainReferenceSize = FVector2D(1000.f, 1000.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Niagara")
	FTransform DomainSystemTransform = FTransform::Identity;

	/** Both materials must use DomainOpacity, DomainTint and DomainEmissive parameters. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials")
	TObjectPtr<UMaterialInterface> DomainMaterial;

	/** World-projected sea for the actual decorative ground mesh outside the playable grid. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials")
	TObjectPtr<UMaterialInterface> OuterBoardMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials")
	TObjectPtr<UMaterialInterface> BorderMaterial;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials")
	FLinearColor DomainTint = FLinearColor(0.18f, 0.20f, 0.65f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials")
	FLinearColor BorderTint = FLinearColor(0.65f, 0.25f, 1.f, 1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials", meta=(ClampMin="0", ClampMax="1"))
	float DomainOpacity = 0.7f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials", meta=(ClampMin="0", ClampMax="1"))
	float BorderOpacity = 0.55f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials", meta=(ClampMin="0"))
	float DomainEmissive = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Materials", meta=(ClampMin="0"))
	float BorderEmissive = 1.5f;

	/** Local board-space height prevents z-fighting without covering units. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Geometry")
	float FloorOffset = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Geometry", meta=(ClampMin="0"))
	float BorderHeight = 140.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Geometry", meta=(ClampMin="0"))
	float BorderThickness = 4.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Transition", meta=(ClampMin="0"))
	float RevealSeconds = 0.4f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Elysia|Star Sea|Transition", meta=(ClampMin="0"))
	float DismissSeconds = 0.3f;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY(VisibleAnywhere, Category="Elysia|Star Sea")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category="Elysia|Star Sea|Niagara")
	TObjectPtr<UNiagaraComponent> DomainEffect;

	UPROPERTY(VisibleAnywhere, Category="Elysia|Star Sea")
	TObjectPtr<UStaticMeshComponent> DomainSurface;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> OuterBoardSurface;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> OuterBoardMID;

	UPROPERTY(VisibleAnywhere, Category="Elysia|Star Sea")
	TArray<TObjectPtr<UStaticMeshComponent>> BorderMeshes;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DomainMID;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BorderMID;

	TWeakObjectPtr<AEGDeploymentBoard> SourceBoard;
	float CurrentOpacity = 0.f;
	float FadeStartOpacity = 0.f;
	float FadeTargetOpacity = 0.f;
	float FadeElapsed = 0.f;
	float FadeDuration = 0.f;
	bool bInitialized = false;
	bool bDismissing = false;

	void StartFade(float TargetOpacity, float Duration);
	void ApplyOpacity();

	UFUNCTION()
	void HandleBoardDestroyed(AActor* DestroyedActor);

	UFUNCTION()
	void HandleBoardEndPlay(AActor* BoardActor, EEndPlayReason::Type Reason);
};
