#include "UI/Widget/EGWidgets.h"
#include "Components/Border.h"
#include "Components/Image.h"

// Kept for source/Blueprint compatibility. Resource preparation no longer has a modal UI.
void UEGMainHUD::UpdateResourceLoading()
{
 if(ResourceLoadingPanel)ResourceLoadingPanel->SetVisibility(ESlateVisibility::Collapsed);
 if(ResourceLoadingScrim)ResourceLoadingScrim->SetVisibility(ESlateVisibility::Collapsed);
}
