#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/ComboBoxString.h"
#include "GameFramework/PlayerController.h"
#include "EGPieceTestPanel.generated.h"

class UComboBoxString;
class UTextBlock;
class UButton;

UCLASS()
class ELYGAMBIT_API UEGPieceTestSelector : public UComboBoxString
{
 GENERATED_BODY()
public:
 UEGPieceTestSelector();
};

UCLASS()
class ELYGAMBIT_API UEGPieceTestPanel : public UUserWidget
{
 GENERATED_BODY()
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
private:
 UFUNCTION() void PieceChanged(FString Item,ESelectInfo::Type SelectionType);
 UFUNCTION() void StartBattle();
 UFUNCTION() void ResetScene();
 UPROPERTY(Transient) TObjectPtr<UComboBoxString> Selector;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Status;
 UPROPERTY(Transient) TObjectPtr<UButton> StartButton;
};

UCLASS()
class ELYGAMBIT_API AEGPieceTestPlayerController : public APlayerController
{
 GENERATED_BODY()
protected:
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(Transient) TObjectPtr<UEGPieceTestPanel> Panel;
};
