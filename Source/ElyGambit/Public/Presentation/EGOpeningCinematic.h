#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "EGOpeningCinematic.generated.h"

class ACameraActor;
class ALevelSequenceActor;
class APlayerController;
class ASkeletalMeshActor;
class SWidget;
class UAnimSequence;
class ULevelSequencePlayer;
class UFont;

/** Self-contained presentation controller for the separate opening map. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGOpeningCinematic : public AActor
{
    GENERATED_BODY()

public:
    AEGOpeningCinematic();

    /** Static low-angle composition shown before the player starts. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Opening")
    TObjectPtr<ACameraActor> OpeningCamera;

    /** Place a sequence actor in the map and disable its autoplay. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Opening")
    TObjectPtr<ALevelSequenceActor> SequenceActor;

    /** Match this camera to the last shot of the sequence. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Opening")
    TObjectPtr<ACameraActor> FinalCamera;

    /** Seated performer whose idle loop meets the first animation section at four seconds. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Opening|Animation")
    TObjectPtr<ASkeletalMeshActor> CorruptionActor;

    /** Loop that continues after the performer reaches the enemy station. */
    UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="Opening|Animation")
    TObjectPtr<UAnimSequence> EndIdle;

    /** Optional project CJK font for the start button. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Opening|UI")
    TObjectPtr<UFont> ButtonFont;

    UFUNCTION(BlueprintCallable, Category="Opening")
    void StartOpening();

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    UFUNCTION()
    void SequenceFinished();

    void SynchronizeSeatedIdle(ULevelSequencePlayer* Player);
    void StartEndIdle();
    void RemoveStartInterface();

    TSharedPtr<SWidget> StartInterface;
    TWeakObjectPtr<APlayerController> LocalController;
    TWeakObjectPtr<AActor> PreviousViewTarget;
    FTimerHandle EndIdleTimer;
    float PreviousIdlePlayRate = 1.f;
    bool bHasPreviousIdlePlayRate = false;
    bool bStarted = false;
    bool bPreviousCursorVisible = false;
    bool bPreviousAutoManageCamera = true;
};

/** Empty game mode for the opening map. The board game is not started here. */
UCLASS(Blueprintable)
class ELYGAMBIT_API AEGOpeningGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AEGOpeningGameMode();
};
