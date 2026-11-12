#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Actor/EGArtPiece.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "EGBoardScene.generated.h"

class ACameraActor;
class AEGDeploymentBoard;
class AEGCardManager;
class AEGBoard;
class UEGMainHUD;
class AEGDeployedPiece;
class AEGPlacementPreview;
class UEGCardDefinition;

UENUM(BlueprintType)
enum class EEGDragSource : uint8 { None, HandCard, PlacedPiece, Shop };
class UEGHandCard;
class UEGCardDragOperation;
class IInputProcessor;
class AEGHUD;
class AEGChessUnit;
class UEGGameAudioComponent;
class UEGPauseMenuWidget;

UENUM(BlueprintType)
enum class EEGPlacementDragState : uint8
{
    Idle, DraggingHandCard2D, PreviewValid, PreviewInvalid, DraggingPlacedPiece,
    HoveringSellZone, Committing, Cancelling,
    /** Shop-offer drag over the animated compact shop: releasing returns the offer. */
    HoveringShopReturn
};

/** Explicit level wiring. A controller requires exactly one settings actor. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGBoardSceneSettings : public AActor
{
    GENERATED_BODY()
public:
    AEGBoardSceneSettings();
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Placement|Scene")
    TObjectPtr<ACameraActor> FixedCamera;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Placement|Scene")
    TObjectPtr<AEGDeploymentBoard> Board;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Placement|Scene")
    TObjectPtr<AEGCardManager> Manager;
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Placement|Scene") TObjectPtr<AEGBoard> CombatBoard;
    /** Opt-in per scene; resize framing changes only the local cosmetic camera. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement|Camera") bool bFrameActiveBoard=false;
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement|Camera",meta=(ClampMin="1")) float FramingUnitHeight=530.f;
    /** Optional ground mesh: keep its visible extent separate from the interaction grid. */
    UPROPERTY(EditInstanceOnly,BlueprintReadOnly,Category="Placement|Camera") TObjectPtr<AActor> FramingGround;
    /** Fit the actual playable grid; decorative scenery is allowed outside the view. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Placement|Camera") bool bFramePlayableGrid=false;
    UFUNCTION(BlueprintCallable,Category="Placement|Camera") bool FrameCameraForViewport(FVector2D Viewport);
};

UCLASS(Blueprintable)
class ELYGAMBIT_API AEGBoardPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    AEGBoardPlayerController();
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void PlayerTick(float DeltaSeconds) override;
    /** Readiness handshake; either PC or HUD may enter BeginPlay first. */
    bool InitializeUI(AEGHUD* HUD);
    UFUNCTION(BlueprintPure, Category="Placement")
    AEGBoardSceneSettings* GetSceneSettings() const { return SceneSettings; }
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="UI") TSubclassOf<UEGMainHUD> MainHUDClass;
    UFUNCTION(BlueprintPure,Category="UI") UEGMainHUD* GetMainHUD() const;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Placement|Drag") TSubclassOf<AEGPlacementPreview> PreviewClass;
    UFUNCTION(BlueprintCallable,Category="Placement|Drag") bool BeginHandCardDrag(FGuid Id);
    bool BeginHandCardDragForOperation(FGuid Id,UEGCardDragOperation* ExpectedOperation);
    UFUNCTION(BlueprintCallable,Category="Placement|Drag") bool BeginShopCardDrag(int32 Slot,int32 Revision);
    UFUNCTION(BlueprintCallable,Category="Placement|Drag") bool BeginPlacedPieceDrag(AEGDeployedPiece* Piece);
    UFUNCTION(BlueprintCallable,Category="Placement|Drag") void UpdateDragAtScreenPosition(FVector2D AbsolutePosition);
    void UpdateInspectionAtScreenPosition(FVector2D AbsolutePosition);
    void UpdateHoverInformationAtScreenPosition(FVector2D AbsolutePosition);
    /** Accepts one release; all run resources are prepared before input opens. */
    UFUNCTION(BlueprintCallable,Category="Placement|Drag") bool TryCompleteDragAtScreenPosition(FVector2D AbsolutePosition);
    UFUNCTION(BlueprintCallable,Category="Placement|Drag") void CancelActiveDrag();
    UFUNCTION(BlueprintPure,Category="Placement|Drag") EEGPlacementDragState GetDragState() const {return DragState;}
    UFUNCTION(BlueprintPure,Category="Placement|Drag") bool IsDraggingPlacedPiece() const {return DragState!=EEGPlacementDragState::Idle&&DragSource==EEGDragSource::PlacedPiece;}
    UFUNCTION(BlueprintPure,Category="Placement|Drag") EEGDragSource GetDragSource() const {return DragSource;}
    UFUNCTION(BlueprintPure,Category="Placement|Drag") AEGPlacementPreview* GetPlacementPreview() const {return Preview;}
    UFUNCTION(BlueprintPure,Category="Placement|Drag") bool HasPendingShopDeployment() const {return false;}
    bool TryBeginWorldDragAtScreen(FVector2D Position);
    bool IsInsideViewport(FVector2D Position) const;
    bool IsDragSourceValid() const;
    bool OpenPauseMenu();
    bool IsPauseMenuOpen() const;
    void HandlePauseEscape();
    void AttachCardOperation(UEGCardDragOperation* Operation);
protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Audio")
    TObjectPtr<UEGGameAudioComponent> GameAudio;
    UPROPERTY(Transient)
    TObjectPtr<AEGBoardSceneSettings> SceneSettings;
    UPROPERTY(Transient) TWeakObjectPtr<AEGHUD> UIHost;
private:
    UPROPERTY(Transient) EEGPlacementDragState DragState=EEGPlacementDragState::Idle;
    UPROPERTY(Transient) FGuid DraggedCardId;
    UPROPERTY(Transient) EEGDragSource DragSource=EEGDragSource::None;
    UPROPERTY(Transient) TObjectPtr<UEGCardDefinition> DraggedShopDefinition;
    int32 DraggedShopSlot=INDEX_NONE,DraggedShopRevision=INDEX_NONE;
    UPROPERTY(Transient) TWeakObjectPtr<AEGDeployedPiece> DraggedPiece;
    UPROPERTY(Transient) TObjectPtr<AEGPlacementPreview> Preview;
    UPROPERTY(Transient) TObjectPtr<UEGCardDragOperation> CardOperation;
    UPROPERTY(Transient) TObjectPtr<UEGHandCard> WorldDragVisual;
    UPROPERTY(Transient) TObjectPtr<UEGPauseMenuWidget> PauseMenu;
    TSharedPtr<IInputProcessor> InputRouter;
    FDelegateHandle ActivationHandle;
    FVector2D DragOrigin=FVector2D::ZeroVector;
    // Cosmetic source suppression only; occupancy and manager ownership stay intact.
    bool bSourceVisualSuppressed=false;
    bool bSourceWasHidden=false;
    bool bSourceVitalsWereVisible=false;
    bool bWorldInformationVisible=false;
    bool bAudioLegalPreview=false;
    bool bAudioSellTarget=false;
    FIntPoint FramedViewport=FIntPoint::ZeroValue;
    void UpdateBoardFraming();
    void FindInspectableAtScreen(FVector2D Position,AEGChessUnit*& Unit,AEGDeployedPiece*& Piece);
    bool ScreenRay(FVector2D Position,FVector& Origin,FVector& Direction) const;
    bool BoardCellAt(FVector2D Position,FIntPoint& Cell) const;
    void ClearDragVisuals();
    void FinishDrag();
    bool RequestShopDeployment(int32 Slot,int32 Revision,FIntPoint Cell);
    void CancelPendingShopDeployment();
    void ApplicationActivationChanged(bool bActive);
};

/** Isolated prototype entry: deliberately does not start the combat/GAS mode. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGBoardGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEGBoardGameMode();
};
