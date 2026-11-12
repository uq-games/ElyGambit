#pragma once
#include "CoreMinimal.h"
#include "Actor/EGArtPiece.h"
#include "Types/EGChessTypes.h"
#include "EGPlacementVisuals.generated.h"

class AEGDeploymentBoard;
class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UEGCardDefinition;
class UWidgetComponent;

/** A deployed card incarnation, intentionally unrelated to the commander class. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGDeployedPiece : public AEGArtPiece
{
    GENERATED_BODY()
public:
    AEGDeployedPiece();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    void AssignPlacement(AEGDeploymentBoard* InBoard,FIntPoint InCell,FGuid InId);
    void RefreshPreparationVitals();
    void SetPreparationVitalsVisible(bool bVisible);
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="UI") TObjectPtr<UWidgetComponent> PreparationVitals;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") TObjectPtr<UBoxComponent> PickQuery;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Placement") FGuid InstanceId;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Placement") FGuid RosterId;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Placement") EEGDeploymentPart PartRole=EEGDeploymentPart::Hero;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Placement") TObjectPtr<UEGCardDefinition> CardDefinition;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Placement") EEGFaction Faction=EEGFaction::Player;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Placement") FIntPoint Cell=FIntPoint(INDEX_NONE,INDEX_NONE);
    UFUNCTION(BlueprintPure,Category="Placement") AEGDeploymentBoard* GetBoard() const;
private:
    UPROPERTY(Transient) TWeakObjectPtr<AEGDeploymentBoard> Board;
};

/** Cosmetic visual actor; run mode keeps one initialized instance for each hero. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGPlacementPreview : public AEGArtPiece
{
    GENERATED_BODY()
public:
    AEGPlacementPreview();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Placement") TObjectPtr<UMaterialInterface> RingMaterial;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Placement") TObjectPtr<UStaticMeshComponent> Ring;
    UFUNCTION(BlueprintCallable,Category="Placement") void ShowAt(FVector WorldPosition,bool bLegal);
    UFUNCTION(BlueprintCallable,Category="Placement") void HidePreview();
private:
    void SetPresentationActive(bool bActive);
    bool bPresentationActive=true;
    TArray<TWeakObjectPtr<UActorComponent>> SuspendedComponents;
    TArray<TWeakObjectPtr<AActor>> SuspendedActors;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RingMID;
};
