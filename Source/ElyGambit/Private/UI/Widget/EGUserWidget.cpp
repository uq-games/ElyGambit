#include "UI/Widget/EGUserWidget.h"
#include "UI/WidgetController/EGWidgetController.h"

void UEGUserWidget::SetWidgetController(UEGWidgetController* InController)
{
    if (WidgetController == InController) return;
    UnbindWidgetControllerCallbacks();
    WidgetController = InController;
    BindWidgetControllerCallbacks();
    WidgetControllerSet();
}
void UEGUserWidget::NativeConstruct()
{
    Super::NativeConstruct();
    // The same widget may be removed and added again without being re-created.
    BindWidgetControllerCallbacks();
}
void UEGUserWidget::NativeDestruct()
{
    UnbindWidgetControllerCallbacks();
    Super::NativeDestruct();
}
