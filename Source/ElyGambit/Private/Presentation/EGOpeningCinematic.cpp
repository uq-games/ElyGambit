#include "Presentation/EGOpeningCinematic.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/Skeleton.h"
#include "Camera/CameraActor.h"
#include "Components/InputComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/SkeletalMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

AEGOpeningCinematic::AEGOpeningCinematic()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("OpeningRoot")));
}

void AEGOpeningCinematic::BeginPlay()
{
    Super::BeginPlay();

    APlayerController* Controller = UGameplayStatics::GetPlayerController(this, 0);
    if (!Controller || !Controller->IsLocalController() || !OpeningCamera)
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening requires a local controller and an opening camera."));
        return;
    }

    LocalController = Controller;
    PreviousViewTarget = Controller->GetViewTarget();
    bPreviousCursorVisible = Controller->bShowMouseCursor;
    bPreviousAutoManageCamera = Controller->bAutoManageActiveCameraTarget;
    Controller->bAutoManageActiveCameraTarget = false;
    Controller->SetViewTarget(OpeningCamera);
    Controller->bShowMouseCursor = true;
    Controller->SetInputMode(FInputModeGameAndUI().SetHideCursorDuringCapture(false));

    EnableInput(Controller);
    if (InputComponent)
    {
        InputComponent->BindKey(EKeys::Enter, IE_Pressed, this, &ThisClass::StartOpening);
        InputComponent->BindKey(EKeys::SpaceBar, IE_Pressed, this, &ThisClass::StartOpening);
    }

    FSlateFontInfo Font = ButtonFont ? FSlateFontInfo(ButtonFont, 24) : FCoreStyle::GetDefaultFontStyle("Regular", 24);
    const TWeakObjectPtr<AEGOpeningCinematic> WeakThis(this);
    StartInterface = SNew(SOverlay)
        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Bottom)
        .Padding(FMargin(100.f, 0.f, 0.f, 90.f))
        [
            SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
            .BorderBackgroundColor(FLinearColor(.045f, .025f, .075f, .70f))
            .Padding(FMargin(1.f))
            [
                SNew(SBox)
                .WidthOverride(250.f)
                .HeightOverride(64.f)
                [
                    SNew(SButton)
                    .ButtonColorAndOpacity(FLinearColor(.47f, .24f, .43f, .80f))
                    .HAlign(HAlign_Center)
                    .VAlign(VAlign_Center)
                    .OnClicked_Lambda([WeakThis]()
                    {
                        if (AEGOpeningCinematic* Opening = WeakThis.Get()) Opening->StartOpening();
                        return FReply::Handled();
                    })
                    [
                        SNew(STextBlock)
                        .Text(FText::FromString(TEXT("开始游戏")))
                        .Font(Font)
                        .ColorAndOpacity(FLinearColor(.98f, .93f, .97f))
                    ]
                ]
            ]
        ];

    if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
        Viewport->AddViewportWidgetContent(StartInterface.ToSharedRef(), 10);
}

void AEGOpeningCinematic::StartOpening()
{
    if (bStarted) return;
    ULevelSequencePlayer* Player = IsValid(SequenceActor) ? SequenceActor->GetSequencePlayer() : nullptr;
    if (!Player || !SequenceActor->GetSequence() || !IsValid(FinalCamera))
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening requires a sequence actor with a sequence and a final camera."));
        return;
    }
    if (Player->IsPlaying() || Player->IsPaused())
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening sequence is already active; disable autoplay and start it with the opening button."));
        return;
    }

    SynchronizeSeatedIdle(Player);
    bStarted = true;
    RemoveStartInterface();
    if (APlayerController* Controller = LocalController.Get())
    {
        Controller->bShowMouseCursor = false;
        Controller->SetInputMode(FInputModeGameOnly());
    }
    Player->SetCompletionModeOverride(EMovieSceneCompletionModeOverride::ForceKeepState);
    Player->OnFinished.AddUniqueDynamic(this, &ThisClass::SequenceFinished);
    Player->Play();
}

void AEGOpeningCinematic::SynchronizeSeatedIdle(ULevelSequencePlayer* Player)
{
    // The first skeletal animation section starts four sequence seconds after Play().
    constexpr float AnimationStartSeconds = 4.f;
    USkeletalMeshComponent* Mesh = IsValid(CorruptionActor) ? CorruptionActor->GetSkeletalMeshComponent() : nullptr;
    UAnimSingleNodeInstance* Idle = IsValid(Mesh) ? Mesh->GetSingleNodeInstance() : nullptr;
    UAnimSequenceBase* IdleAsset = Idle ? Cast<UAnimSequenceBase>(Idle->GetCurrentAsset()) : nullptr;
    if (!Idle || !IdleAsset)
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening cannot synchronize the seated idle: performer or single-node animation is missing."));
        return;
    }

    const float OriginalRate = Idle->GetPlayRate();
    if (FMath::IsFinite(OriginalRate))
    {
        PreviousIdlePlayRate = OriginalRate;
        bHasPreviousIdlePlayRate = true;
    }

    const float Length = Idle->GetLength();
    const float Position = Idle->GetCurrentTime();
    const float SequenceRate = Player ? Player->GetPlayRate() : 0.f;
    const float AssetRate = IdleAsset->RateScale;
    const float ComponentRate = Mesh->GlobalAnimRateScale;
    const double SequenceDuration = Player ? Player->GetDuration().AsSeconds() : 0.0;
    const double SequenceElapsed = Player ? Player->GetCurrentTime().AsSeconds() - Player->GetStartTime().AsSeconds() : 0.0;
    if (!Idle->IsPlaying() || !Idle->IsLooping() || Idle->IsReverse() ||
        !FMath::IsFinite(OriginalRate) || OriginalRate <= 0.f ||
        !FMath::IsFinite(Length) || Length <= KINDA_SMALL_NUMBER ||
        !FMath::IsFinite(Position) || !FMath::IsFinite(SequenceRate) || SequenceRate <= KINDA_SMALL_NUMBER ||
        !FMath::IsFinite(AssetRate) || AssetRate <= KINDA_SMALL_NUMBER ||
        !FMath::IsFinite(ComponentRate) || ComponentRate <= KINDA_SMALL_NUMBER ||
        !FMath::IsFinite(SequenceDuration) || SequenceDuration < AnimationStartSeconds ||
        !FMath::IsFinite(SequenceElapsed) || SequenceElapsed < -KINDA_SMALL_NUMBER || SequenceElapsed >= AnimationStartSeconds)
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening cannot synchronize the seated idle: a playing, forward loop and a valid four-second lead are required."));
        return;
    }

    const float Phase = FMath::Fmod(FMath::Fmod(Position, Length) + Length, Length);
    const float LeadTime = (AnimationStartSeconds - static_cast<float>(SequenceElapsed)) / SequenceRate;
    const float OriginalAdvance = LeadTime * OriginalRate * AssetRate * ComponentRate;
    if (!FMath::IsFinite(LeadTime) || LeadTime <= KINDA_SMALL_NUMBER ||
        !FMath::IsFinite(OriginalAdvance) || (Phase + OriginalAdvance) / Length > 1000000.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening cannot synchronize the seated idle: playback timing is outside the supported range."));
        return;
    }
    const int32 TargetCycle = FMath::Max(1, FMath::RoundToInt((Phase + OriginalAdvance) / Length));
    const float TargetAdvance = TargetCycle * Length - Phase;
    const float AlignedRate = TargetAdvance / (LeadTime * AssetRate * ComponentRate);
    if (!FMath::IsFinite(AlignedRate) || AlignedRate <= 0.f)
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening cannot synchronize the seated idle: computed playback rate is invalid."));
        return;
    }

    Idle->SetPlayRate(AlignedRate);
}

void AEGOpeningCinematic::SequenceFinished()
{
    if (IsValid(SequenceActor))
        if (ULevelSequencePlayer* Player = SequenceActor->GetSequencePlayer())
            Player->OnFinished.RemoveDynamic(this, &ThisClass::SequenceFinished);
    if (APlayerController* Controller = LocalController.Get())
        if (IsValid(FinalCamera)) Controller->SetViewTarget(FinalCamera);

    // A natural finish may be raised while Sequencer is still flushing its last evaluation.
    if (UWorld* World = GetWorld())
        EndIdleTimer = World->GetTimerManager().SetTimerForNextTick(this, &ThisClass::StartEndIdle);
}

void AEGOpeningCinematic::StartEndIdle()
{
    USkeletalMeshComponent* Mesh = IsValid(CorruptionActor) ? CorruptionActor->GetSkeletalMeshComponent() : nullptr;
    USkeletalMesh* SkeletalMesh = IsValid(Mesh) ? Mesh->GetSkeletalMeshAsset() : nullptr;
    if (!IsValid(Mesh) || !IsValid(SkeletalMesh) || !IsValid(EndIdle) ||
        !IsValid(EndIdle->GetSkeleton()) || !EndIdle->GetSkeleton()->IsCompatibleMesh(SkeletalMesh))
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening cannot start the final idle: performer, mesh or compatible animation is missing."));
        return;
    }

    Mesh->PlayAnimation(EndIdle, true);
    UAnimSingleNodeInstance* EndInstance = Mesh->GetSingleNodeInstance();
    if (!EndInstance || EndInstance->GetCurrentAsset() != EndIdle || !EndInstance->IsPlaying())
    {
        UE_LOG(LogTemp, Warning, TEXT("Opening final idle did not start on the performer."));
        return;
    }
    if (bHasPreviousIdlePlayRate)
        Mesh->SetPlayRate(PreviousIdlePlayRate);
}

void AEGOpeningCinematic::RemoveStartInterface()
{
    if (StartInterface.IsValid())
    {
        if (UWorld* World = GetWorld())
            if (UGameViewportClient* Viewport = World->GetGameViewport())
                Viewport->RemoveViewportWidgetContent(StartInterface.ToSharedRef());
        StartInterface.Reset();
    }
}

void AEGOpeningCinematic::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    RemoveStartInterface();
    if (UWorld* World = GetWorld())
        World->GetTimerManager().ClearTimer(EndIdleTimer);
    if (IsValid(SequenceActor))
    {
        if (ULevelSequencePlayer* Player = SequenceActor->GetSequencePlayer())
        {
            Player->OnFinished.RemoveDynamic(this, &ThisClass::SequenceFinished);
            if (bStarted && (Player->IsPlaying() || Player->IsPaused())) Player->Stop();
        }
    }
    if (bHasPreviousIdlePlayRate && IsValid(CorruptionActor))
        if (USkeletalMeshComponent* Mesh = CorruptionActor->GetSkeletalMeshComponent())
            if (Mesh->GetSingleNodeInstance()) Mesh->SetPlayRate(PreviousIdlePlayRate);
    if (APlayerController* Controller = LocalController.Get())
    {
        DisableInput(Controller);
        Controller->bShowMouseCursor = bPreviousCursorVisible;
        Controller->bAutoManageActiveCameraTarget = bPreviousAutoManageCamera;
        Controller->SetInputMode(FInputModeGameOnly());
        if (IsValid(PreviousViewTarget.Get()) && GetWorld() && !GetWorld()->bIsTearingDown)
            Controller->SetViewTarget(PreviousViewTarget.Get());
    }
    Super::EndPlay(EndPlayReason);
}

AEGOpeningGameMode::AEGOpeningGameMode()
{
    DefaultPawnClass = nullptr;
    PlayerControllerClass = APlayerController::StaticClass();
}
