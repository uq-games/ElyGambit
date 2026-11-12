#pragma once
#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "EGCardDragOperation.generated.h"

class AEGBoardPlayerController;
class FDragDropOperation;
class UEGMainHUD;
class UEGOverlayWidgetController;

/** Slate lifecycle adapter only; the controller owns every transaction. */
UCLASS()
class ELYGAMBIT_API UEGCardDragOperation : public UDragDropOperation
{
    GENERATED_BODY()
public:
    UPROPERTY(Transient) TWeakObjectPtr<AEGBoardPlayerController> Controller;
    UPROPERTY(Transient,BlueprintReadOnly,Category="UI|Drag") FGuid InstanceId;
    UPROPERTY(Transient) TWeakObjectPtr<UEGMainHUD> ReorderHUD;
    bool bReorder=false;
    int32 OrderRevision=0;
    void ArmHandGesture(UEGMainHUD* HUD,UEGOverlayWidgetController* InController,FVector2D InPressPosition,int32 Revision);
    void UpdatePendingHandGesture(FVector2D ScreenPosition);
    /** Terminal operations must never submit to a newer controller drag. */
    bool HasFinished() const { return bFinished; }
    void MarkFinished();
    /** Slate still reads its operation after OnDragged returns. Dispose later. */
    static void CancelSlateDragDeferred(const TSharedPtr<FDragDropOperation>& ExpectedOperation);
    virtual void Dragged_Implementation(const FPointerEvent& Event) override;
    virtual void Drop_Implementation(const FPointerEvent& Event) override;
    virtual void DragCancelled_Implementation(const FPointerEvent& Event) override;
private:
    void BeginOwnedCardDrag(FVector2D ScreenPosition);
    UPROPERTY(Transient) TWeakObjectPtr<UEGOverlayWidgetController> HandController;
    FVector2D HandPressPosition=FVector2D::ZeroVector;
    bool bPendingHandGesture=false;
    bool bFinished=false;
};
