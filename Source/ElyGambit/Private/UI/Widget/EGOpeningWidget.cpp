#include "UI/Widget/EGOpeningWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/CircularThrobber.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "Components/TextBlock.h"
#include "Game/EGDemoGameMode.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Placement/EGBoardScene.h"
#include "Presentation/EGOpeningPresentation.h"
#include "UI/EGVisualStyle.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGMainMenuWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGDemoPanel.h"

namespace
{
void Place(UCanvasPanel* Root, UWidget* Child, const FVector2D& Offset, const FVector2D& Size,
           const FAnchors& Anchors, const FVector2D& Alignment)
{
    if (UCanvasPanelSlot* Slot = Root->AddChildToCanvas(Child))
    {
        Slot->SetAnchors(Anchors);
        Slot->SetAlignment(Alignment);
        Slot->SetPosition(Offset);
        Slot->SetSize(Size);
    }
}

UTextBlock* MakeLabel(UWidgetTree* Tree, const TCHAR* Name, const FText& Text, int32 Size)
{
    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
    Label->SetText(Text);
    Label->SetFont(FEGVisualStyle::Font(Size));
    Label->SetColorAndOpacity(FSlateColor(FLinearColor(.98f, .91f, .97f, 1.f)));
    Label->SetShadowOffset(FVector2D(1.f, 2.f));
    Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .8f));
    Label->SetJustification(ETextJustify::Center);
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Label;
}

UButton* MakeButton(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* LabelText, int32 FontSize)
{
    UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
    FButtonStyle Style;
    Style.SetNormal(FEGVisualStyle::Rounded(FLinearColor(.045f, .025f, .075f, .55f), 3.f,
                                            FLinearColor(.86f, .56f, .76f, .72f), 1.f));
    Style.SetHovered(FEGVisualStyle::Rounded(FLinearColor(.18f, .06f, .14f, .82f), 3.f,
                                             FLinearColor(1.f, .7f, .86f, 1.f), 1.f));
    Style.SetPressed(FEGVisualStyle::Rounded(FLinearColor(.3f, .1f, .22f, .9f), 3.f));
    Button->SetStyle(Style);
    if (UButtonSlot* Slot = Cast<UButtonSlot>(Button->AddChild(MakeLabel(Tree, *(FString(Name) + TEXT("Label")), FText::FromString(LabelText), FontSize))))
    {
        Slot->SetHorizontalAlignment(HAlign_Center);
        Slot->SetVerticalAlignment(VAlign_Center);
    }
    return Button;
}
}

UEGOpeningWidget* UEGOpeningWidget::Install(APlayerController* Player)
{
    if (!IsValid(Player) || !Player->IsLocalController()) return nullptr;
    AEGDemoGameMode* GameMode = Player->GetWorld() ? Player->GetWorld()->GetAuthGameMode<AEGDemoGameMode>() : nullptr;
    const bool bEndgameRetry = IsValid(GameMode) && GameMode->IsEndgameCheckpointRetryNeeded();
    // An opening failure can arrive while the first overlay is still fading.
    // Reuse that overlay, but ignore any old widget already removed from view.
    TArray<UUserWidget*> Existing;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Player, Existing, StaticClass(), false);
    for (UUserWidget* Candidate : Existing)
        if (Candidate && Candidate->GetOwningPlayer() == Player && Candidate->IsInViewport())
            if (UEGOpeningWidget* Opening = Cast<UEGOpeningWidget>(Candidate))
            {
                if (bEndgameRetry)
                {
                    Opening->bEndgameRecoveryMode = true;
                    Opening->bStartupSceneRevealed = true;
                    if (Opening->StartupCover) Opening->StartupCover->SetVisibility(ESlateVisibility::Collapsed);
                }
                Opening->HideGameHUD();
                return Opening;
            }
    UEGOpeningWidget* Widget = CreateWidget<UEGOpeningWidget>(Player, StaticClass());
    if (!Widget) return nullptr;
    UEGMainMenuSettingsWidget::ApplySavedVolume(Player->GetWorld());
    Widget->AddToViewport(500);
    // A later enemy-entrance retry reuses this UI over an already-live board;
    // its error screen must not recreate the one-time startup cover.
    if (GameMode)
    {
        if (GameMode->IsStageEntranceRetryNeeded() || bEndgameRetry)
        {
            Widget->bEndgameRecoveryMode = bEndgameRetry;
            Widget->bStartupSceneRevealed = true;
            if (Widget->StartupCover) Widget->StartupCover->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    // Hide both HUD roots immediately, before an engine movie reveals the world.
    // Tick repeats this in case either root is constructed later.
    Widget->HideGameHUD();
    Player->bShowMouseCursor = true;
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    Player->SetInputMode(Mode);
    return Widget;
}

void UEGOpeningWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    BuildLayout();
}

void UEGOpeningWidget::BuildLayout()
{
    if (!WidgetTree || WidgetTree->RootWidget) return;
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("OpeningRoot"));
    WidgetTree->RootWidget = Root;
    // Consume pointer input outside the buttons as well; do not let the board HUD receive it.
    Root->SetVisibility(ESlateVisibility::Visible);

    // Draw first, behind the title and recovery controls. The first frame is
    // opaque while the opening camera and its scene are still being prepared.
    StartupCover = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OpeningStartupCover"));
    StartupCover->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.025f, .012f, .055f, 1.f), 0.f));
    StartupCover->SetVisibility(ESlateVisibility::HitTestInvisible);
    UCanvasPanelSlot* CoverSlot = Root->AddChildToCanvas(StartupCover);
    CoverSlot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    CoverSlot->SetOffsets(FMargin(0.f));
    CoverSlot->SetZOrder(-1);

    UTextBlock* Brand = MakeLabel(WidgetTree, TEXT("OpeningBrand"), FText::FromString(TEXT("ELYGAMBIT")), 54);
    FSlateFontInfo BrandFont = FEGVisualStyle::SerifFont(54);
    BrandFont.LetterSpacing = 100;
    Brand->SetFont(BrandFont);
    Brand->SetJustification(ETextJustify::Left);
    Place(Root, Brand, FVector2D(96.f, 86.f), FVector2D(620.f, 78.f), FAnchors(0.f, 0.f), FVector2D::ZeroVector);
    UTextBlock* Subtitle = MakeLabel(WidgetTree, TEXT("OpeningSubtitle"), FText::FromString(TEXT("往 世 之 弈")), 20);
    Subtitle->SetColorAndOpacity(FSlateColor(FLinearColor(.91f, .69f, .84f, 1.f)));
    Subtitle->SetJustification(ETextJustify::Left);
    Place(Root, Subtitle, FVector2D(102.f, 166.f), FVector2D(350.f, 32.f), FAnchors(0.f, 0.f), FVector2D::ZeroVector);
    UImage* BrandLine = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("OpeningBrandLine"));
    BrandLine->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.92f, .64f, .83f, .72f), 0.f));
    BrandLine->SetVisibility(ESlateVisibility::HitTestInvisible);
    Place(Root, BrandLine, FVector2D(102.f, 216.f), FVector2D(200.f, 1.f), FAnchors(0.f, 0.f), FVector2D::ZeroVector);
    MenuDecoration={Brand,Subtitle,BrandLine};

    StatusText = MakeLabel(WidgetTree, TEXT("LoadingStatus"), FText::FromString(TEXT("正在准备")), 22);
    Place(Root, StatusText, FVector2D(-72.f, -62.f), FVector2D(470.f, 60.f), FAnchors(1.f, 1.f), FVector2D(1.f, 1.f));
    StatusLine = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("LoadingLine"));
    StatusLine->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.92f, .62f, .82f, .78f), 0.f));
    StatusLine->SetVisibility(ESlateVisibility::HitTestInvisible);
    Place(Root, StatusLine, FVector2D(-72.f, -60.f), FVector2D(200.f, 1.5f), FAnchors(1.f, 1.f), FVector2D(1.f, 1.f));
    LoadingDetail=MakeLabel(WidgetTree,TEXT("LoadingDetail"),FText::GetEmpty(),16);
    Place(Root,LoadingDetail,FVector2D(-72.f,-25.f),FVector2D(600.f,32.f),FAnchors(1.f,1.f),FVector2D(1.f,1.f));
    LoadingSpinner=WidgetTree->ConstructWidget<UCircularThrobber>(UCircularThrobber::StaticClass(),TEXT("LoadingSpinner"));
    Place(Root,LoadingSpinner,FVector2D(-565.f,-90.f),FVector2D(28.f,28.f),FAnchors(1.f,1.f),FVector2D(1.f,1.f));
    StartButton = MakeButton(WidgetTree, TEXT("OpeningStart"), TEXT("开始游戏"), 26);
    Place(Root, StartButton, FVector2D(96.f, -224.f), FVector2D(310.f, 64.f), FAnchors(0.f, 1.f), FVector2D(0.f, 1.f));
    RetryButton = MakeButton(WidgetTree, TEXT("OpeningRetry"), TEXT("重试加载"), 22);
    Place(Root, RetryButton, FVector2D(-72.f, -132.f), FVector2D(210.f, 54.f), FAnchors(1.f, 1.f), FVector2D(1.f, 1.f));
    SettingsButton = MakeButton(WidgetTree, TEXT("OpeningSettings"), TEXT("设置"), 20);
    Place(Root, SettingsButton, FVector2D(96.f, -150.f), FVector2D(310.f, 56.f), FAnchors(0.f, 1.f), FVector2D(0.f, 1.f));
    ReturnMenuButton = MakeButton(WidgetTree, TEXT("OpeningReturnMenu"), TEXT("返回主菜单"), 20);
    Place(Root, ReturnMenuButton, FVector2D(96.f, -224.f), FVector2D(310.f, 64.f), FAnchors(0.f, 1.f), FVector2D(0.f, 1.f));
    QuitButton = MakeButton(WidgetTree, TEXT("OpeningQuit"), TEXT("退出"), 20);
    Place(Root, QuitButton, FVector2D(96.f, -84.f), FVector2D(310.f, 56.f), FAnchors(0.f, 1.f), FVector2D(0.f, 1.f));
    StartButton->OnClicked.AddDynamic(this, &ThisClass::StartClicked);
    RetryButton->OnClicked.AddDynamic(this, &ThisClass::RetryClicked);
    SettingsButton->OnClicked.AddDynamic(this, &ThisClass::SettingsClicked);
    ReturnMenuButton->OnClicked.AddDynamic(this, &ThisClass::ReturnToMenuClicked);
    QuitButton->OnClicked.AddDynamic(this, &ThisClass::QuitClicked);
    StartButton->SetVisibility(ESlateVisibility::Collapsed);
    RetryButton->SetVisibility(ESlateVisibility::Collapsed);
    SettingsButton->SetVisibility(ESlateVisibility::Collapsed);
    ReturnMenuButton->SetVisibility(ESlateVisibility::Collapsed);
    QuitButton->SetVisibility(ESlateVisibility::Collapsed);
}

void UEGOpeningWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    APlayerController* Player = GetOwningPlayer();
    AEGDemoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AEGDemoGameMode>() : nullptr;
    UEGOpeningPresentation* Presentation = IsValid(GameMode) ? GameMode->FindComponentByClass<UEGOpeningPresentation>() : nullptr;
    if (!IsValid(Player) || !IsValid(GameMode)) return;
    const bool bStageRetry = GameMode->IsStageEntranceRetryNeeded();
    const bool bEndgameRetry = GameMode->IsEndgameCheckpointRetryNeeded();
    if (bEndgameRetry) bEndgameRecoveryMode = true;
    if (bEndgameRecoveryMode && !bEndgameRetry)
    {
        // The transaction is committed by GameMode; relinquish HUD suppression
        // before the newly created reward screen handles input.
        RemoveFromParent();
        return;
    }
    FString Error = bEndgameRetry ? TEXT("终局准备失败，请重试。") : bStageRetry ? GameMode->GetStageEntranceError() :
        (IsValid(Presentation) ? Presentation->GetLoadingError() : GameMode->GetOpeningPreloadError());
    if (bStageRetry && Error.IsEmpty()) Error = TEXT("敌人入场失败，请重试。");
    if (!IsValid(Presentation) && Error.IsEmpty())
    {
        if (bFoundPresentation) RemoveFromParent();
        return;
    }
    bFoundPresentation |= IsValid(Presentation);
    bWaitForPreparation |= bStarting || bStageRetry || (IsValid(Presentation) && Presentation->IsPreparingAfterStart());
    if (!bStageRetry && !bEndgameRetry && IsValid(Presentation) && !Presentation->IsOpeningActive() &&
        (!bWaitForPreparation || GameMode->CanPrepare()))
    {
        RemoveFromParent();
        return;
    }
    HideGameHUD();
    Elapsed += FMath::Max(0.f, DeltaSeconds);
    const bool bError = !Error.IsEmpty();
    if (StartupCover && !bStartupSceneRevealed)
    {
        const AEGBoardPlayerController* BoardPC = Cast<AEGBoardPlayerController>(Player);
        const AEGBoardSceneSettings* BoardScene = BoardPC ? BoardPC->GetSceneSettings() : nullptr;
        const ACameraActor* Camera = Cast<ACameraActor>(Player->GetViewTarget());
        const UCameraComponent* CameraComponent = IsValid(Camera) ? Camera->GetCameraComponent() : nullptr;
        const APlayerCameraManager* CameraManager = Player->PlayerCameraManager;
        // Require both the intended non-board view target and its updated camera
        // cache; SetViewTarget alone can precede the rendered view by a frame.
        const bool bCameraReady = IsValid(BoardScene) && IsValid(BoardScene->FixedCamera) && IsValid(Camera) &&
            Camera != BoardScene->FixedCamera.Get() && IsValid(CameraComponent) && IsValid(CameraManager) &&
            FVector::DistSquared(CameraManager->GetCameraLocation(), CameraComponent->GetComponentLocation()) <= FMath::Square(10.f);
        if (!bError && IsValid(Presentation) && Presentation->IsLoadingSceneVisible() && bCameraReady)
        {
            StartupRevealElapsed += FMath::Max(0.f, DeltaSeconds);
            const float Alpha = FMath::Clamp(StartupRevealElapsed / .3f, 0.f, 1.f);
            StartupCover->SetRenderOpacity(1.f - Alpha);
            if (Alpha >= 1.f)
            {
                bStartupSceneRevealed = true;
                StartupCover->SetVisibility(ESlateVisibility::Collapsed);
            }
        }
        else
        {
            StartupRevealElapsed = 0.f;
            StartupCover->SetRenderOpacity(1.f);
        }
    }
    if (bError && bStarting)
    {
        // Start can fail after the menu has faded out. Restore the error/recovery controls.
        bStarting = false;
        FadeElapsed = 0.f;
        SetRenderOpacity(1.f);
        if (StartButton) StartButton->SetIsEnabled(true);
    }
    const bool bMustRestart = GameMode->IsOpeningStartupFailure();
    const bool bSettingsOpen = IsValid(Settings) && Settings->GetVisibility() == ESlateVisibility::Visible;
    const bool bAwaiting = !bStarting && !bError && bStartupSceneRevealed && IsValid(Presentation) && Presentation->IsAwaitingStart();
    const bool bPreparing = !bError && IsValid(Presentation) && Presentation->IsPreparingAfterStart();
    const bool bDeparting = !bError && IsValid(Presentation) && Presentation->IsDeparturePlaying();
    const bool bWaiting = !bError && IsValid(Presentation) && Presentation->IsWaitingForRunResources();
    // The camera move is the transition, not a loading screen. Only show a
    // loading label before it starts or if real resource work outlasts it.
    const bool bLoading = !bError && !bAwaiting && (!bDeparting || bWaiting);
    ResourceWaitElapsed = bWaiting ? ResourceWaitElapsed + FMath::Max(0.f, DeltaSeconds) : 0.f;
    const float LoadingOpacity = bWaiting ? FMath::Clamp(ResourceWaitElapsed / .35f, 0.f, 1.f) : 1.f;
    // Fade only the menu decorations. Loading/recovery must remain visible after the camera arrives.
    SetRenderOpacity(1.f);
    if (bDeparting)
    {
        FadeElapsed += FMath::Max(0.f, DeltaSeconds);
    }
    else
    {
        FadeElapsed = 0.f;
        SetRenderOpacity(1.f);
    }
    for(UWidget* Decoration:MenuDecoration)if(Decoration)
        Decoration->SetRenderOpacity(bDeparting?1.f-FMath::Clamp(FadeElapsed/.4f,0.f,1.f):1.f);
    if(StartupCover&&bStartupSceneRevealed)
    {
        // Do not flash a full-screen cover for a wait that may end next frame.
        // The error/retry state still needs its high-contrast backdrop.
        StartupCover->SetVisibility(bError?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        StartupCover->SetRenderOpacity(bError?.96f:0.f);
    }
    // A single animated label is enough; avoid a second counter or spinner
    // implying progress that is not available for the entire operation.
    if(LoadingSpinner)LoadingSpinner->SetVisibility(ESlateVisibility::Collapsed);
    if(LoadingDetail)LoadingDetail->SetVisibility(ESlateVisibility::Collapsed);
    if (StatusText)
    {
        StatusText->SetVisibility((bError || bLoading) ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        StatusText->SetRenderOpacity(bError ? 1.f : LoadingOpacity);
        if (bError) StatusText->SetText(FText::FromString((bStageRetry || bEndgameRetry) ? Error :
            (bMustRestart ? TEXT("开局失败，请重新进入") : TEXT("加载失败，请重试"))));
        else
        {
            const int32 Dots = FMath::FloorToInt(Elapsed * 2.f) % 4;
            const TCHAR* Text = bPreparing ? TEXT("正在加载") : TEXT("正在准备");
            StatusText->SetText(FText::FromString(FString(Text) + FString::ChrN(Dots, TEXT('.'))));
        }
    }
    if (StatusLine)
    {
        StatusLine->SetVisibility(bLoading ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
        StatusLine->SetRenderOpacity(LoadingOpacity);
    }
    if (StartButton) StartButton->SetVisibility(bAwaiting && !bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (RetryButton)
    {
        RetryButton->SetVisibility(!bStarting && bError && !bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        if (UTextBlock* RetryLabel = Cast<UTextBlock>(RetryButton->GetContent()))
            RetryLabel->SetText(FText::FromString(bEndgameRetry ? TEXT("重试") : bStageRetry ? TEXT("重试入场") :
                (bMustRestart ? TEXT("重新进入") : TEXT("重试加载"))));
    }
    if (SettingsButton) SettingsButton->SetVisibility(!bStarting && !bSettingsOpen && (bAwaiting || bError) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (ReturnMenuButton) ReturnMenuButton->SetVisibility(!bStarting && bError && !bSettingsOpen ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
    if (QuitButton) QuitButton->SetVisibility(!bStarting && !bSettingsOpen && (bAwaiting || bError) ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UEGOpeningWidget::StartClicked()
{
    if (bStarting) return;
    if (AEGDemoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AEGDemoGameMode>() : nullptr)
    {
        if (GameMode->RequestOpeningStart())
        {
            bStarting = true;
            FadeElapsed = 0.f;
            if (StartButton) StartButton->SetIsEnabled(false);
        }
    }
}

void UEGOpeningWidget::RetryClicked()
{
    if (AEGDemoGameMode* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode<AEGDemoGameMode>() : nullptr)
    {
        if (bEndgameRecoveryMode)
        {
            if (!bRecoveryRetryInFlight && GameMode->IsEndgameCheckpointRetryNeeded())
            {
                TGuardValue<bool> Guard(bRecoveryRetryInFlight, true);
                GameMode->RetryEndgameCheckpoint();
            }
            return;
        }
        if (GameMode->IsStageEntranceRetryNeeded())
        {
            // Only the failed enemy creation is retried; GameMode retains all
            // card, income, and stage transaction authority.
            GameMode->RetryStageEntrance();
            return;
        }
        if (GameMode->IsOpeningStartupFailure())
        {
            UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")), true, TEXT("EGOpening=1"));
            return;
        }
        if (!GameMode->GetOpeningPreloadError().IsEmpty())
        {
            // Preserve the already accepted start request while retrying resources.
            GameMode->RetryOpeningPreload();
            return;
        }
        if (UEGOpeningPresentation* Presentation = GameMode->FindComponentByClass<UEGOpeningPresentation>())
            Presentation->RetryOpeningScene();
    }
}

void UEGOpeningWidget::SettingsClicked()
{
    if (!Settings && GetOwningPlayer())
    {
        Settings = CreateWidget<UEGMainMenuSettingsWidget>(GetOwningPlayer(), UEGMainMenuSettingsWidget::StaticClass());
        if (Settings)
        {
            Settings->OnSettingsClosed.AddUObject(this, &ThisClass::OnSettingsClosed);
            Settings->AddToViewport(510);
        }
    }
    if (Settings) Settings->OpenSettings();
}

void UEGOpeningWidget::OnSettingsClosed()
{
    if (StartButton && StartButton->IsVisible() && GetOwningPlayer()) StartButton->SetUserFocus(GetOwningPlayer());
}

void UEGOpeningWidget::ReturnToMenuClicked()
{
    if (GetWorld() && !GetWorld()->bIsTearingDown)
        UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Assets/Maps/Gameplay/L_ElyMainMenu")));
}

void UEGOpeningWidget::QuitClicked()
{
    if (GetWorld()) UKismetSystemLibrary::QuitGame(GetWorld(), GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UEGOpeningWidget::HideGameHUD()
{
    APlayerController* Player = GetOwningPlayer();
    AEGHUD* Host = IsValid(Player) ? Cast<AEGHUD>(Player->GetHUD()) : nullptr;
    if (HiddenHUDHost.Get()!=Host)
    {
        if (AEGHUD* Previous=HiddenHUDHost.Get()) Previous->SetOpeningHUDSuppressed(false);
        HiddenHUDHost=Host;
    }
    if (Host) Host->SetOpeningHUDSuppressed(true);
}

void UEGOpeningWidget::RestoreHUD()
{
    if (AEGHUD* Host=HiddenHUDHost.Get()) Host->SetOpeningHUDSuppressed(false);
    HiddenHUDHost.Reset();
}

void UEGOpeningWidget::NativeDestruct()
{
    RestoreHUD();
    if (Settings)
    {
        Settings->OnSettingsClosed.RemoveAll(this);
        Settings->RemoveFromParent();
        Settings = nullptr;
    }
    Super::NativeDestruct();
}
