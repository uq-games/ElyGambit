#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGElysiaEntryMovieWidget.generated.h"

class UImage;
class UMediaTexture;

/** Non-interactive local full-screen movie layer; the owning component controls its lifetime. */
UCLASS()
class ELYGAMBIT_API UEGElysiaEntryMovieWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual void NativeOnInitialized() override;
 virtual void NativeDestruct() override;
 void SetMovieTexture(UMediaTexture* Texture);
 /** Remains transparent until the owner observes a decoded-frame candidate. */
 void SetMovieVisible(bool bVisible);
private:
 UPROPERTY(Transient) TObjectPtr<UImage> MovieImage;
};
