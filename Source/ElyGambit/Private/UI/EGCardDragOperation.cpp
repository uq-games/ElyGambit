#include "UI/EGCardDragOperation.h"
#include "Placement/EGBoardScene.h"
#include "InputCoreTypes.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/Widget.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/EGGameAudioComponent.h"

void UEGCardDragOperation::ArmHandGesture(UEGMainHUD* HUD,UEGOverlayWidgetController* InController,FVector2D InPressPosition,int32 Revision)
{
    ReorderHUD=HUD;HandController=InController;HandPressPosition=InPressPosition;OrderRevision=Revision;bPendingHandGesture=true;
    if(DefaultDragVisual)DefaultDragVisual->SetRenderOpacity(0.f);
}
void UEGCardDragOperation::UpdatePendingHandGesture(FVector2D ScreenPosition)
{
    if(!bPendingHandGesture||bFinished)return;
    auto* HUD=ReorderHUD.Get();auto* Overlay=HandController.Get();
    if(!HUD||!Overlay||!HUD->CanEditHand()||Overlay->GetHandOrderRevision()!=OrderRevision)
    {if(HUD)HUD->CancelHandReorder();else MarkFinished();return;}
    const auto& G=HUD->GetCachedGeometry();
    const FVector2D Delta=G.AbsoluteToLocal(ScreenPosition)-G.AbsoluteToLocal(HandPressPosition);
    const bool bHorizontal=FMath::Abs(Delta.X)>=12.f&&FMath::Abs(Delta.X)>=1.5f*FMath::Abs(Delta.Y);
    const bool bUp=Delta.Y<=-24.f&&HUD->IsAboveHandForDeployment(ScreenPosition);
    if(bUp||HUD->IsOutsideHandReorderArea(ScreenPosition))
    {
        BeginOwnedCardDrag(ScreenPosition);
    }
    else if(bHorizontal)
    {
        int32 Revision=INDEX_NONE;
        if(!HUD->BeginHandReorder(InstanceId,Revision)||Revision!=OrderRevision){HUD->CancelHandReorder();return;}
        bPendingHandGesture=false;bReorder=true;
        UEGGameAudioComponent::PlayUI(HUD,EEGGameAudioCue::Drag);
        if(DefaultDragVisual)DefaultDragVisual->SetRenderOpacity(1.f);
        HUD->UpdateHandReorder(ScreenPosition);
    }
}

void UEGCardDragOperation::BeginOwnedCardDrag(FVector2D ScreenPosition)
{
    auto* HUD=ReorderHUD.Get();auto* Overlay=HandController.Get();
    if(!HUD||!Overlay||!HUD->CanEditHand()||Overlay->GetHandOrderRevision()!=OrderRevision)
    {if(HUD)HUD->CancelHandReorder();else MarkFinished();return;}
    // Keep the same Slate operation alive; no reorder transaction is submitted.
    HUD->UnregisterHandReorder(this);bPendingHandGesture=false;bReorder=false;
    if(!Overlay->BeginHandCardDrag(InstanceId,this)||!Overlay->AttachHandCardDrag(this,ScreenPosition))
    {
        Overlay->CancelActiveDrag();MarkFinished();
        if(FSlateApplication::IsInitialized()&&UWidgetBlueprintLibrary::GetDragDroppingContent()==this)
            CancelSlateDragDeferred(FSlateApplication::Get().GetDragDroppingContent());
    }
}

void UEGCardDragOperation::MarkFinished()
{
    bFinished=true;
    Controller.Reset();
    HandController.Reset();bPendingHandGesture=false;
    if(DefaultDragVisual)DefaultDragVisual->SetRenderOpacity(0.f);
}
void UEGCardDragOperation::CancelSlateDragDeferred(const TSharedPtr<FDragDropOperation>& ExpectedOperation)
{
    if(!ExpectedOperation.IsValid())return;
    const TWeakPtr<FDragDropOperation> Expected=ExpectedOperation;
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Expected](float)
    {
        if(FSlateApplication::IsInitialized())
        {
            const auto Operation=Expected.Pin();
            auto& App=FSlateApplication::Get();
            // Another drag may have started before this tick. Never cancel it.
            if(Operation.IsValid()&&App.GetDragDroppingContent()==Operation)App.CancelDragDrop();
        }
        return false;
    }));
}

void UEGCardDragOperation::Dragged_Implementation(const FPointerEvent& Event)
{
    if(bFinished)return;
    if(bPendingHandGesture){UpdatePendingHandGesture(Event.GetScreenSpacePosition());if(bFinished)return;}
    if(bReorder)
    {
        auto* HUD=ReorderHUD.Get();
        if(HUD&&HUD->IsOutsideHandReorderArea(Event.GetScreenSpacePosition()))
            BeginOwnedCardDrag(Event.GetScreenSpacePosition());
        else if(HUD)HUD->UpdateHandReorder(Event.GetScreenSpacePosition());
        if(bFinished)return;
        if(bReorder){Super::Dragged_Implementation(Event);return;}
    }
    if(Controller.IsValid())Controller->UpdateDragAtScreenPosition(Event.GetScreenSpacePosition());
    if(!bFinished)Super::Dragged_Implementation(Event);
}
void UEGCardDragOperation::Drop_Implementation(const FPointerEvent& Event)
{
    if(bFinished)return;
    if(bPendingHandGesture){auto* HUD=ReorderHUD.Get();if(IsValid(HUD))HUD->UnregisterHandReorder(this);MarkFinished();Super::Drop_Implementation(Event);return;}
    if(bReorder){auto* HUD=ReorderHUD.Get();MarkFinished();if(IsValid(HUD))HUD->FinishHandReorder(InstanceId,OrderRevision,Event.GetScreenSpacePosition(),true);Super::Drop_Implementation(Event);return;}
    auto* PC=Controller.Get();MarkFinished();
    if(IsValid(PC))PC->TryCompleteDragAtScreenPosition(Event.GetScreenSpacePosition());
    Super::Drop_Implementation(Event);
}
void UEGCardDragOperation::DragCancelled_Implementation(const FPointerEvent& Event)
{
    if(bFinished)return;
    if(bPendingHandGesture){auto* HUD=ReorderHUD.Get();if(IsValid(HUD))HUD->UnregisterHandReorder(this);MarkFinished();Super::DragCancelled_Implementation(Event);return;}
    if(bReorder){auto* HUD=ReorderHUD.Get();MarkFinished();if(IsValid(HUD))HUD->FinishHandReorder(InstanceId,OrderRevision,Event.GetScreenSpacePosition(),Event.GetEffectingButton()==EKeys::LeftMouseButton);Super::DragCancelled_Implementation(Event);return;}
    auto* PC=Controller.Get();MarkFinished();
    // A release over the 3D viewport has no UMG drop target. Explicit cancellation
    // clears the controller context first, so this callback cannot commit it again.
    if(IsValid(PC))
    {
        if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
            PC->TryCompleteDragAtScreenPosition(Event.GetScreenSpacePosition());
        else PC->CancelActiveDrag();
    }
    Super::DragCancelled_Implementation(Event);
}
