#include "UI/WidgetController/EGWidgetController.h"
#include "GameFramework/PlayerController.h"
#include "Game/EGMatchManager.h"
#include "Game/EGCardManager.h"

void UEGWidgetController::SetWidgetControllerParams(const FEGWidgetControllerParams& Params)
{
    UnbindCallbacksFromDependencies();
    PlayerController = Params.PlayerController;
    HandSource = Params.HandSource;
    MatchSource = Params.MatchSource;
    if (PlayerController.IsValid() && HandSource.IsValid() && PlayerController->GetWorld() != HandSource->GetWorld())
    {
        UE_LOG(LogTemp, Warning, TEXT("UI rejected a hand source from another world."));
        HandSource.Reset();
    }
}
void UEGWidgetController::BindCallbacksToDependencies() { bDependenciesBound = true; }
void UEGWidgetController::UnbindCallbacksFromDependencies() { bDependenciesBound = false; }
void UEGWidgetController::BroadcastInitialValues() {}
void UEGWidgetController::Shutdown()
{
    UnbindCallbacksFromDependencies();
    PlayerController.Reset(); HandSource.Reset(); MatchSource.Reset();
}
void UEGWidgetController::BeginDestroy()
{
    Shutdown();
    Super::BeginDestroy();
}
