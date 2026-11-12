#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGUserWidget.generated.h"

class UEGWidgetController;

/** View base following Aura's controller injection, with explicit native teardown. */
UCLASS(Abstract, Blueprintable)
class ELYGAMBIT_API UEGUserWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="UI") void SetWidgetController(UEGWidgetController* InController);
    UFUNCTION(BlueprintPure, Category="UI") UEGWidgetController* GetWidgetController() const { return WidgetController; }
protected:
    UPROPERTY(Transient, BlueprintReadOnly, Category="UI") TObjectPtr<UEGWidgetController> WidgetController;
    UFUNCTION(BlueprintImplementableEvent, Category="UI") void WidgetControllerSet();
    virtual void BindWidgetControllerCallbacks() {}
    virtual void UnbindWidgetControllerCallbacks() {}
    virtual void NativeConstruct() override;
    virtual void NativeDestruct() override;
};
