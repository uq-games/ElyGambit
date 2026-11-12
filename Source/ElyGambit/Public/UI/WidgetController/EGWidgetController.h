#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EGWidgetController.generated.h"

class APlayerController;
class AEGCardManager;
class UEGMatchManager;

/** Inject existing dependencies; UI never creates another gameplay data source. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGWidgetControllerParams
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI") TObjectPtr<APlayerController> PlayerController;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI") TObjectPtr<AEGCardManager> HandSource;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="UI") TObjectPtr<UEGMatchManager> MatchSource;
};

/** HUD-owned presentation bridge. Explicit unbinding also supports PIE/rebinding. */
UCLASS(Abstract, Blueprintable)
class ELYGAMBIT_API UEGWidgetController : public UObject
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="UI") void SetWidgetControllerParams(const FEGWidgetControllerParams& Params);
    UFUNCTION(BlueprintCallable, Category="UI") virtual void BindCallbacksToDependencies();
    UFUNCTION(BlueprintCallable, Category="UI") virtual void UnbindCallbacksFromDependencies();
    UFUNCTION(BlueprintCallable, Category="UI") virtual void BroadcastInitialValues();
    UFUNCTION(BlueprintCallable, Category="UI") virtual void Shutdown();
protected:
    virtual void BeginDestroy() override;
    UPROPERTY(Transient) TWeakObjectPtr<APlayerController> PlayerController;
    UPROPERTY(Transient) TWeakObjectPtr<AEGCardManager> HandSource;
    UPROPERTY(Transient) TWeakObjectPtr<UEGMatchManager> MatchSource;
    bool bDependenciesBound = false;
};
