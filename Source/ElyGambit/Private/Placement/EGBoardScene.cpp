#include "Placement/EGBoardScene.h"
#include "Components/TextBlock.h"
#include "Game/EGCardManager.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "UI/EGCardDragOperation.h"
#include "UI/Widget/EGShopCard.h"
#include "UI/Widget/EGPauseMenuWidget.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/EGUnitInformation.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/HUD/EGHUD.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGRunResourceComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "TimerManager.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Engine/GameInstance.h"
#include "UI/EGWidgetEditorLibrary.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Widgets/SViewport.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Presentation/EGBoardFraming.h"
#include "Presentation/EGVictoryEndingPresentation.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"

namespace
{
bool IsPresentationInputBlocked(const AEGBoardPlayerController* PC)
{
    const UWorld* World = IsValid(PC) ? PC->GetWorld() : nullptr;
    const AEGDemoGameMode* Mode = World ? World->GetAuthGameMode<AEGDemoGameMode>() : nullptr;
    if (!IsValid(Mode)) return false;
    const UEGMatchManager* Match = Mode->GetMatch();
    return Mode->IsPresentationInputBlocked() || Mode->IsEndgameCheckpointRetryNeeded() || (Match && Match->GetPhase() == EEGMatchPhase::Finished);
}

class FEGPlacementInputRouter final : public IInputProcessor
{
    TWeakObjectPtr<AEGBoardPlayerController> PC;
    FVector2D PressPoint=FVector2D::ZeroVector;
    bool bPendingWorldDrag=false;
    bool bPendingSelection=false;
    bool bPendingPrimaryClick=false;
    float HoverElapsed=0.f;
    bool CancelShopDrag()
    {
        auto* Operation=Cast<UEGShopDragOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent());
        if(!PC.IsValid()||!Operation||Operation->HUD.Get()!=PC->GetMainHUD())return false;
        UEGGameAudioComponent::PlayUI(PC.Get(),EEGGameAudioCue::Cancel);
        PC->CancelActiveDrag();
        return true;
    }
public:
    explicit FEGPlacementInputRouter(AEGBoardPlayerController* InPC):PC(InPC){}
    virtual void Tick(float Delta,FSlateApplication& App,TSharedRef<ICursor> Cursor) override
    {
        if(PC.IsValid()&&PC->IsPauseMenuOpen()){bPendingWorldDrag=bPendingSelection=bPendingPrimaryClick=false;HoverElapsed=0.f;return;}
        if (PC.IsValid() && IsPresentationInputBlocked(PC.Get()))
        {
            bPendingWorldDrag = bPendingSelection = bPendingPrimaryClick = false;
            HoverElapsed = 0.f;
            if (PC->GetDragState() != EEGPlacementDragState::Idle) PC->CancelActiveDrag();
            return;
        }
        if(PC.IsValid()&&PC->GetDragState()!=EEGPlacementDragState::Idle&&
            (!PC->IsInsideViewport(App.GetCursorPos())||!PC->IsDragSourceValid()))
            PC->CancelActiveDrag();
        if(PC.IsValid()&&PC->GetDragState()==EEGPlacementDragState::Idle&&!App.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton))
        {
            HoverElapsed+=Delta;
            if(HoverElapsed>=.12f){HoverElapsed=0.f;PC->UpdateHoverInformationAtScreenPosition(App.GetCursorPos());}
        }
        else HoverElapsed=0.f;
    }
    virtual bool HandleKeyDownEvent(FSlateApplication& App,const FKeyEvent& Event) override
    {
        if(PC.IsValid()&&PC->IsPauseMenuOpen()&&Event.GetKey()==EKeys::Escape)
        {if(!Event.IsRepeat())PC->HandlePauseEscape();return true;}
        if (PC.IsValid() && IsPresentationInputBlocked(PC.Get()))
        {
            bPendingWorldDrag = bPendingSelection = bPendingPrimaryClick = false;
            if(Event.GetKey()==EKeys::Escape)
            {
                const auto* Mode=PC->GetWorld()?PC->GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
                const auto* Match=Mode?Mode->GetMatch():nullptr;const auto* HUD=PC->GetHUD<AEGHUD>();
                if(Mode&&Match&&Match->GetPhase()==EEGMatchPhase::Finished&&!Mode->IsPresentationInputBlocked()&&
                   !Mode->IsEndgameCheckpointRetryNeeded()&&HUD&&!HUD->ShouldHideFinalResult())
                {if(!Event.IsRepeat())PC->OpenPauseMenu();return true;}
            }
            return false; // Opening controls still need this event.
        }
        if(PC.IsValid()&&Event.GetKey()==EKeys::Escape)
        {
            if(Event.IsRepeat())return true;
            bPendingWorldDrag=false;bPendingSelection=false;bPendingPrimaryClick=false;
            if(auto* View=PC->GetMainHUD())if(View->CancelHandReorder()){UEGGameAudioComponent::PlayUI(PC.Get(),EEGGameAudioCue::Cancel);return true;}
            if(CancelShopDrag())return true;
            if(PC->GetDragState()!=EEGPlacementDragState::Idle||PC->HasPendingShopDeployment()){UEGGameAudioComponent::PlayUI(PC.Get(),EEGGameAudioCue::Cancel);PC->CancelActiveDrag();return true;}
            if(auto* HUD=PC->GetHUD<AEGHUD>())if(auto* Panel=HUD->GetRunPanel())if(Panel->HasStigmataEscapePriority())return false;
            if(auto* View=PC->GetMainHUD())if(View->DismissPanels())return true;
            // Do not enter a nested menu over another owner's world pause.
            if(PC->GetWorld()&&PC->GetWorld()->IsPaused())return true;
            if(PC->OpenPauseMenu())return true;
            if(const auto* Mode=PC->GetWorld()?PC->GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr)
                if(Mode->GetMatch()&&Mode->GetMatch()->GetPhase()==EEGMatchPhase::Settlement)return true;
            return false;
        }
        return false;
    }
    virtual bool HandleMouseMoveEvent(FSlateApplication& App,const FPointerEvent& Event) override
    {
        if(PC.IsValid()&&PC->IsPauseMenuOpen())return false;
        if (PC.IsValid() && IsPresentationInputBlocked(PC.Get()))
        {
            bPendingWorldDrag = bPendingSelection = bPendingPrimaryClick = false;
            return false; // Opening controls still need this event.
        }
        if(bPendingWorldDrag&&PC.IsValid())
        {
            if(!Event.IsMouseButtonDown(EKeys::LeftMouseButton))bPendingWorldDrag=false;
            else if(FVector2D::Distance(PressPoint,Event.GetScreenSpacePosition())>=App.GetDragTriggerDistance())
            {bPendingWorldDrag=false;bPendingSelection=false;PC->TryBeginWorldDragAtScreen(PressPoint);}
        }
        if(PC.IsValid()&&PC->GetDragState()!=EEGPlacementDragState::Idle)
            PC->UpdateDragAtScreenPosition(Event.GetScreenSpacePosition());

        return false;
    }
    virtual bool HandleMouseButtonDownEvent(FSlateApplication& App,const FPointerEvent& Event) override
    {
        if(PC.IsValid()&&PC->IsPauseMenuOpen()){bPendingWorldDrag=bPendingSelection=bPendingPrimaryClick=false;return false;}
        if (PC.IsValid() && IsPresentationInputBlocked(PC.Get()))
        {
            bPendingWorldDrag = bPendingSelection = bPendingPrimaryClick = false;
            return false; // Opening controls still need this event.
        }
        if(PC.IsValid()&&Event.GetEffectingButton()==EKeys::RightMouseButton)
            if(auto* View=PC->GetMainHUD())if(View->CancelHandReorder()){UEGGameAudioComponent::PlayUI(PC.Get(),EEGGameAudioCue::Cancel);return true;}
        if(Event.GetEffectingButton()==EKeys::RightMouseButton&&CancelShopDrag())
        {bPendingWorldDrag=bPendingSelection=bPendingPrimaryClick=false;return true;}
        if(PC.IsValid()&&Event.GetEffectingButton()==EKeys::RightMouseButton&&PC->GetDragState()!=EEGPlacementDragState::Idle)
        {bPendingWorldDrag=bPendingSelection=bPendingPrimaryClick=false;UEGGameAudioComponent::PlayUI(PC.Get(),EEGGameAudioCue::Cancel);PC->CancelActiveDrag();return true;}
        bPendingPrimaryClick=PC.IsValid()&&Event.GetEffectingButton()==EKeys::LeftMouseButton&&
            PC->GetDragState()==EEGPlacementDragState::Idle&&PC->IsInsideViewport(Event.GetScreenSpacePosition())&&
            PC->GetMainHUD();
        bPendingWorldDrag=bPendingPrimaryClick&&!PC->GetMainHUD()->IsPointBlocked(Event.GetScreenSpacePosition());
        bPendingSelection=bPendingWorldDrag;PressPoint=Event.GetScreenSpacePosition();return false;
    }
    virtual bool HandleMouseButtonUpEvent(FSlateApplication& App,const FPointerEvent& Event) override
    {
        if(PC.IsValid()&&PC->IsPauseMenuOpen()){bPendingWorldDrag=bPendingSelection=bPendingPrimaryClick=false;return false;}
        if (PC.IsValid() && IsPresentationInputBlocked(PC.Get()))
        {
            bPendingWorldDrag = bPendingSelection = bPendingPrimaryClick = false;
            return false; // Opening controls still need this event.
        }
        if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
        {
            const bool bClick=bPendingPrimaryClick&&PC.IsValid()&&PC->GetDragState()==EEGPlacementDragState::Idle&&
                PC->IsInsideViewport(Event.GetScreenSpacePosition())&&!UWidgetBlueprintLibrary::IsDragDropping()&&
                FVector2D::Distance(PressPoint,Event.GetScreenSpacePosition())<App.GetDragTriggerDistance();
            const bool bSelect=bPendingSelection&&bClick;
            bPendingWorldDrag=bPendingSelection=bPendingPrimaryClick=false;
            // Dispatch ordinary outside clicks even when a child button handles
            // its own mouse-up. Drag releases keep their separate transaction path.
            if(bClick)if(auto* View=PC->GetMainHUD())View->HandleOutsideClick(Event.GetScreenSpacePosition());
            if(bSelect)PC->UpdateInspectionAtScreenPosition(Event.GetScreenSpacePosition());
        }
        if(PC.IsValid()&&Event.GetEffectingButton()==EKeys::LeftMouseButton&&PC->GetDragState()!=EEGPlacementDragState::Idle)
        {
            // One guarded transaction handles both paths. Always let Slate receive
            // mouse-up too: world gestures must release viewport capture, and hand
            // gestures must dispose their drag operation. Later drops are no-ops.
            PC->TryCompleteDragAtScreenPosition(Event.GetScreenSpacePosition());return false;
        }
        return false;
    }
};
}

AEGBoardSceneSettings::AEGBoardSceneSettings()
{
    PrimaryActorTick.bCanEverTick = false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

AEGBoardPlayerController::AEGBoardPlayerController()
{
    GameAudio = CreateDefaultSubobject<UEGGameAudioComponent>(TEXT("GameAudio"));
    bAutoManageActiveCameraTarget = false;
    bShowMouseCursor = true;
}

bool AEGBoardPlayerController::IsPauseMenuOpen() const
{return IsValid(PauseMenu)&&PauseMenu->IsMenuOpen();}

void AEGBoardPlayerController::HandlePauseEscape()
{if(IsPauseMenuOpen())PauseMenu->HandleEscape();}

bool AEGBoardPlayerController::OpenPauseMenu()
{
    UWorld* World=GetWorld();
    auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    auto* Match=Mode?Mode->GetMatch():nullptr;
    if(!IsLocalController()||!World||World->bIsTearingDown||World->GetNetMode()!=NM_Standalone||!Mode||!Match||
       Mode->IsPresentationInputBlocked()||Mode->IsEndgameCheckpointRetryNeeded()||Match->GetPhase()==EEGMatchPhase::Settlement)return false;
    if(Match->GetPhase()==EEGMatchPhase::Finished)
    {
        const auto* HUD=GetHUD<AEGHUD>();if(!HUD||HUD->ShouldHideFinalResult())return false;
        // The HUD reveals the result at the black peak; the ending actor owns
        // the final fade until it is destroyed. Do not freeze that handoff.
        for(TActorIterator<AEGVictoryEndingPresentation> It(World);It;++It)
            if(IsValid(*It)&&!It->IsActorBeingDestroyed())return false;
    }
    if(IsPauseMenuOpen())return true;
    if(World->IsPaused())return false;
    if(auto* HUD=GetHUD<AEGHUD>())if(auto* Panel=HUD->GetRunPanel())if(Panel->HasStigmataEscapePriority())return false;
    if(GetDragState()!=EEGPlacementDragState::Idle||UWidgetBlueprintLibrary::IsDragDropping())
    {CancelActiveDrag();return false;}
    if(auto* View=GetMainHUD())View->DismissPanels();
    if(!PauseMenu)PauseMenu=CreateWidget<UEGPauseMenuWidget>(this,UEGPauseMenuWidget::StaticClass());
    return IsValid(PauseMenu)&&PauseMenu->OpenMenu();
}

void AEGBoardPlayerController::PlayerTick(float DeltaSeconds)
{
    Super::PlayerTick(DeltaSeconds);
    UpdateBoardFraming();
}

void AEGBoardPlayerController::UpdateBoardFraming()
{
    if(!IsLocalController()||!IsValid(SceneSettings)||!SceneSettings->bFrameActiveBoard||
       !IsValid(SceneSettings->FixedCamera)||!IsValid(SceneSettings->Board))return;
    int32 Width=0,Height=0;GetViewportSize(Width,Height);
    if(Width<=0||Height<=0||FramedViewport==FIntPoint(Width,Height))return;
    if(SceneSettings->FrameCameraForViewport(FVector2D(Width,Height)))FramedViewport=FIntPoint(Width,Height);
}

bool AEGBoardSceneSettings::FrameCameraForViewport(FVector2D Viewport)
{
    if(!bFrameActiveBoard||!IsValid(Board)||!IsValid(FixedCamera)||!FMath::IsFinite(FramingUnitHeight)||FramingUnitHeight<=0)return false;
    auto* Grid=Board.Get();auto* Camera=FixedCamera.Get();
    auto* Component=Camera->GetCameraComponent();
    const FTransform Transform=Grid->GetActorTransform();
    TArray<FVector> Points;
    for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})
    {
        Points.Add(Transform.TransformPosition(FVector(X*Grid->Columns*Grid->CellWidth*.5f,Y*Grid->Rows*Grid->CellDepth*.5f,0)));
        Points.Add(Transform.TransformPosition(FVector(X*(Grid->Columns-1)*Grid->CellWidth*.5f,Y*(Grid->Rows-1)*Grid->CellDepth*.5f,FramingUnitHeight)));
    }
    FVector Location;
    TArray<FVector> Ground;
    if(IsValid(FramingGround))
    {
        FVector Centre,Extent;FramingGround->GetActorBounds(false,Centre,Extent);
        for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})Ground.Add(Centre+FVector(X*Extent.X,Y*Extent.Y,Extent.Z));
    }
    bool Solved=false;
    if(bFramePlayableGrid)
    {
        TArray<FVector> Edges,Cells;
        for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})
        {
            Edges.Add(Transform.TransformPosition(FVector(X*Grid->Columns*Grid->CellWidth*.5f,Y*Grid->Rows*Grid->CellDepth*.5f,0)));
            const FVector Cell(X*(Grid->Columns-1)*Grid->CellWidth*.5f,Y*(Grid->Rows-1)*Grid->CellDepth*.5f,0);
            Cells.Add(Transform.TransformPosition(Cell));
            Cells.Add(Transform.TransformPosition(Cell+FVector(0,0,FramingUnitHeight)));
        }
        Solved=FEGBoardFraming::SolvePlayableGrid(Edges,Cells,Grid->GetActorLocation(),Camera->GetActorRotation(),Component->FieldOfView,Viewport,Location);
    }
    else Solved=Ground.IsEmpty()
        ? FEGBoardFraming::Solve(Points,Grid->GetActorLocation(),Camera->GetActorRotation(),Component->FieldOfView,Viewport,Location)
        : FEGBoardFraming::SolveReference(Ground,Points,Grid->GetActorLocation(),Camera->GetActorRotation(),Component->FieldOfView,Viewport,Location);
    if(Solved)
    {
        Component->bConstrainAspectRatio=false;
        Component->bOverrideAspectRatioAxisConstraint=true;
        Component->AspectRatioAxisConstraint=AspectRatio_MaintainXFOV;
        Camera->SetActorLocation(Location);
        return true;
    }
    return false;
}

void AEGBoardPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
    if(PauseMenu){PauseMenu->CloseMenu();PauseMenu=nullptr;}
    CancelActiveDrag();
    if(FSlateApplication::IsInitialized())
    {
        if(InputRouter.IsValid())FSlateApplication::Get().UnregisterInputPreProcessor(InputRouter);
        FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
    }
    InputRouter.Reset();
    if(IsValid(Preview)&&Preview->GetOwner()==this)Preview->Destroy();
    Preview=nullptr;
    if(UIHost.IsValid())UIHost->ShutdownOverlay();
    UIHost.Reset();
    SceneSettings=nullptr;Super::EndPlay(Reason);
}

void AEGBoardPlayerController::BeginPlay()
{
    Super::BeginPlay();
    int32 Count = 0;
    for (TActorIterator<AEGBoardSceneSettings> It(GetWorld()); It; ++It)
    {
        SceneSettings = *It;
        ++Count;
    }
    if (Count != 1 || !IsValid(SceneSettings) || !IsValid(SceneSettings->FixedCamera))
    {
        UE_LOG(LogTemp, Error, TEXT("CardDeployment requires exactly one SceneSettings with an explicit FixedCamera."));
        SceneSettings = nullptr;
        return;
    }
    // The opening component owns the view while its presentation gate is active.
    // It restores this same fixed camera when both masters have reached their places.
    if (!IsPresentationInputBlocked(this)) SetViewTargetWithBlend(SceneSettings->FixedCamera, 0.f);
    SetIgnoreMoveInput(true);
    SetIgnoreLookInput(true);
    FInputModeGameAndUI Mode;
    Mode.SetHideCursorDuringCapture(false);
    Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
    SetInputMode(Mode);
    bShowMouseCursor = true;
    // HUD may own its widget class while this controller has no override. Retry
    // the handshake after SceneSettings is ready even when HUD began play first.
    if(IsValid(SceneSettings->Manager))
    {
        SceneSettings->Manager->InitializeCards();
        if(IsLocalController())
            if(auto* GameMode=GetWorld()->GetAuthGameMode<AEGChessGameMode>())
                if(auto* Match=GameMode->GetMatch())GameAudio->InitializeForMatch(Match);
        InitializeUI(Cast<AEGHUD>(GetHUD()));
    }
    if(IsLocalController()&&FSlateApplication::IsInitialized())
    {
        InputRouter=MakeShared<FEGPlacementInputRouter>(this);
        FSlateApplication::Get().RegisterInputPreProcessor(InputRouter,0);
        ActivationHandle=FSlateApplication::Get().OnApplicationActivationStateChanged().AddUObject(this,&ThisClass::ApplicationActivationChanged);
    }
}

bool AEGBoardPlayerController::InitializeUI(AEGHUD* HUD)
{
    if(!IsLocalController()||!IsValid(HUD)||HUD->GetOwningPlayerController()!=this||
        !IsValid(SceneSettings)||!IsValid(SceneSettings->Manager))return false;
    if(UIHost.Get()==HUD&&HUD->GetMainWidget())return true;
    FEGWidgetControllerParams Params;
    Params.PlayerController=this;Params.HandSource=SceneSettings->Manager;
    if(auto* GameMode=GetWorld()->GetAuthGameMode<AEGChessGameMode>())Params.MatchSource=GameMode->GetMatch();
    if(!HUD->InitOverlay(Params,MainHUDClass))return false;
    UIHost=HUD;return true;
}

UEGMainHUD* AEGBoardPlayerController::GetMainHUD() const
{
    const auto* HUD=Cast<AEGHUD>(GetHUD());
    return IsValid(HUD)?HUD->GetMainWidget():nullptr;
}

bool AEGBoardPlayerController::IsInsideViewport(FVector2D P) const
{
    const auto View=GetWorld()&&GetWorld()->GetGameViewport()?GetWorld()->GetGameViewport()->GetGameViewportWidget():nullptr;
    if(!View.IsValid()||P.ContainsNaN())return false;
    const auto& G=View->GetCachedGeometry();const auto L=G.AbsoluteToLocal(P),S=G.GetLocalSize();
    return S.X>0&&S.Y>0&&L.X>=1&&L.Y>=1&&L.X<S.X-1&&L.Y<S.Y-1;
}
bool AEGBoardPlayerController::ScreenRay(FVector2D P,FVector& Origin,FVector& Direction) const
{
    if(!IsInsideViewport(P))return false;
    auto View=GetWorld()->GetGameViewport()->GetGameViewportWidget();
    const auto& G=View->GetCachedGeometry();const FVector2D Local=G.AbsoluteToLocal(P),Size=G.GetLocalSize();
    int32 W,H;GetViewportSize(W,H);
    return DeprojectScreenPositionToWorld(Local.X*W/Size.X,Local.Y*H/Size.Y,Origin,Direction);
}
bool AEGBoardPlayerController::IsDragSourceValid() const
{
    if (IsPresentationInputBlocked(this)) return false;
    const auto* View=GetMainHUD();
    if(!IsValid(SceneSettings)||!IsValid(SceneSettings->Manager)||!IsValid(View)||!View->IsInViewport())return false;
    if(DragSource==EEGDragSource::Shop)
    {
        auto* M=SceneSettings->Manager.Get();const auto Offers=M->GetShop();
        return M->CanTrade()&&M->GetShopSlotRevision(DraggedShopSlot)==DraggedShopRevision&&Offers.IsValidIndex(DraggedShopSlot)&&Offers[DraggedShopSlot]==DraggedShopDefinition;
    }
    if(!SceneSettings->Manager->CanEditPlacement())return false;
    return DraggedCardId.IsValid()?(SceneSettings->Manager->HasHandCard(DraggedCardId)||SceneSettings->Manager->HasReserveHero(DraggedCardId)):SceneSettings->Manager->OwnsPiece(DraggedPiece.Get());
}
bool AEGBoardPlayerController::BoardCellAt(FVector2D P,FIntPoint& Cell) const
{
    UEGMainHUD* MainHUD=GetMainHUD();
    if(!IsValid(SceneSettings)||!IsValid(SceneSettings->Board)||!IsValid(MainHUD)||MainHUD->IsPointBlocked(P))return false;
    // A fresh or resizing canvas may not yet have usable target geometry. Do not
    // snap an owned drag to the board until both sale targets can be evaluated.
    if(MainHUD->bBoardPresentation&&DragState!=EEGPlacementDragState::Idle&&
        DragSource!=EEGDragSource::Shop&&!MainHUD->HasSellZoneHitGeometry())return false;
    FVector O,D;if(!ScreenRay(P,O,D))return false;
    FHitResult Hit;
    const bool bDirect=GetWorld()->LineTraceSingleByChannel(Hit,O,O+D*100000.f,ECC_GameTraceChannel1)&&
        Hit.GetActor()==SceneSettings->Board&&SceneSettings->Board->TryWorldToCell(Hit.ImpactPoint,Cell);
    auto* M=SceneSettings->Manager.Get();auto* Grid=SceneSettings->Board.Get();
    if(!M||!M->UsesRoundShopRules()||DragState==EEGPlacementDragState::Idle||DragSource==EEGDragSource::Shop)return bDirect;
    // An occupied friendly cell is deliberately returned as-is, so release cancels.
    if(bDirect&&Grid->IsPlayerCell(Cell))return true;
    // A neutral row is an explicit forbidden drop target, not a request to snap to a friendly cell.
    if(bDirect&&!Grid->IsEnemyCell(Cell))return false;
    const FVector N=Grid->GetActorUpVector();const double Den=FVector::DotProduct(D,N);
    if(FMath::Abs(Den)<.0001)return false;
    const double T=FVector::DotProduct(Grid->GetActorLocation()-O,N)/Den;if(T<=0)return false;
    const FVector Point=O+D*T;double Best=TNumericLimits<double>::Max();bool Found=false;
    for(int32 Y=0;Y<Grid->PlayerRows;++Y)for(int32 X=0;X<Grid->Columns;++X)
    {
        const FIntPoint Candidate(X,Y);
        const auto Result=DraggedCardId.IsValid()?(M->HasReserveHero(DraggedCardId)?M->EvaluateReservePlacement(DraggedCardId,Candidate):M->EvaluateHandPlacement(DraggedCardId,Candidate)):M->EvaluatePieceMove(DraggedPiece.Get(),Candidate);
        if(!Result.IsAllowed())continue;
        const double Distance=FVector::DistSquared(Point,Grid->CellToWorld(Candidate));
        if(Distance<Best){Best=Distance;Cell=Candidate;Found=true;}
    }
    return Found;
}
bool AEGBoardPlayerController::BeginHandCardDrag(FGuid Id)
{return BeginHandCardDragForOperation(Id,nullptr);}
bool AEGBoardPlayerController::BeginHandCardDragForOperation(FGuid Id,UEGCardDragOperation* ExpectedOperation)
{
    if (IsPresentationInputBlocked(this)) return false;
    if(FSlateApplication::IsInitialized()&&UWidgetBlueprintLibrary::IsDragDropping()&&
       (!ExpectedOperation||UWidgetBlueprintLibrary::GetDragDroppingContent()!=ExpectedOperation))return false;
    UEGMainHUD* MainHUD=GetMainHUD();
    if(DragState!=EEGPlacementDragState::Idle||!IsValid(SceneSettings)||!IsValid(SceneSettings->Manager)||
        !SceneSettings->Manager->CanEditPlacement()||(!SceneSettings->Manager->HasHandCard(Id)&&!SceneSettings->Manager->HasReserveHero(Id))||!IsValid(MainHUD))return false;
    if(auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>())
    {
        auto* Manager=SceneSettings->Manager.Get();
        auto* Card=Manager->FindHandCardDefinition(Id);
        if(!Card)Card=Manager->FindHeroCardDefinition(Id);
        if(Card&&Mode->CanPrepare())Mode->EnsureHeroResources(Card->HeroDefinition);
    }
    DragSource=EEGDragSource::HandCard;DraggedCardId=Id;DragState=EEGPlacementDragState::DraggingHandCard2D;
    UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Drag);
    CancelPendingShopDeployment();
    // Sale plates become visible with this drag, so seed them with the real value
    // before they are shown instead of leaving the authored placeholder.
    MainHUD->SetSellZoneSaleValue(SceneSettings->Manager->IsSaleProtected(Id)?-1:SceneSettings->Manager->GetSaleValue(Id));
    MainHUD->SetDragActive(true);
    if(auto* Source=MainHUD->FindCardWidget(Id)){Source->SetRenderOpacity(.3f);DragOrigin=UEGWidgetEditorLibrary::WidgetScreenPosition(Source,FVector2D(.5f,0));}
    if(MainHUD->IsShopExpanded())
    {
        auto* M=SceneSettings->Manager.Get();UEGHeroDefinition* Definition=nullptr;UTexture2D* Artwork=nullptr;
        if(auto* Card=M->FindHandCardDefinition(Id)){Definition=Card->HeroDefinition;Artwork=Card->Artwork;}
        else if(auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())
        {
            if(auto* Roster=GM->GetRoster())Definition=Roster->ResolveDefinition(Id);
            if(auto* ReserveCard=M->FindHeroCardDefinition(Id))Artwork=ReserveCard->Artwork;
        }
        MainHUD->SetShopContextDragPresentation(Definition,Artwork);
    }
    return true;
}
bool AEGBoardPlayerController::BeginShopCardDrag(int32 Slot,int32 Revision)
{
    if (IsPresentationInputBlocked(this)) return false;
    if(FSlateApplication::IsInitialized()&&UWidgetBlueprintLibrary::IsDragDropping())return false;
    auto* HUD=GetMainHUD();auto* M=SceneSettings?SceneSettings->Manager.Get():nullptr;
    if(DragState!=EEGPlacementDragState::Idle||!M||!HUD||!M->CanTrade()||Revision==INDEX_NONE||M->GetShopSlotRevision(Slot)!=Revision)return false;
    const auto Offers=M->GetShop();if(!Offers.IsValidIndex(Slot)||!IsValid(Offers[Slot]))return false;
    if(auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>())
        if(Mode->CanPrepare())Mode->EnsureHeroResources(Offers[Slot]->HeroDefinition);
    DragSource=EEGDragSource::Shop;DraggedShopSlot=Slot;DraggedShopRevision=Revision;DraggedShopDefinition=Offers[Slot];
    UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Drag);
    CancelPendingShopDeployment();
    DragState=EEGPlacementDragState::DraggingHandCard2D;
    // Shop offers never sell: flag the origin first so the plates collapse before
    // any drag presentation tick can show them.
    HUD->SetShopDragPresentation(Slot,DraggedShopDefinition);HUD->SetDragActive(true);
    return true;
}
void AEGBoardPlayerController::AttachCardOperation(UEGCardDragOperation* Operation){CardOperation=Operation;}
bool AEGBoardPlayerController::BeginPlacedPieceDrag(AEGDeployedPiece* Piece)
{
    if (IsPresentationInputBlocked(this)) return false;
    if(FSlateApplication::IsInitialized()&&UWidgetBlueprintLibrary::IsDragDropping())return false;
    UEGMainHUD* MainHUD=GetMainHUD();
    if(DragState!=EEGPlacementDragState::Idle||!IsValid(SceneSettings)||!IsValid(SceneSettings->Manager)||
        !SceneSettings->Manager->CanEditPlacement()||!SceneSettings->Manager->OwnsPiece(Piece)||!IsValid(MainHUD))return false;
    DragSource=EEGDragSource::PlacedPiece;DraggedPiece=Piece;DragState=EEGPlacementDragState::DraggingPlacedPiece;
    UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Drag);
    CancelPendingShopDeployment();
    DragOrigin=UEGWidgetEditorLibrary::WorldScreenPosition(this,Piece->GetActorLocation());
    bSourceWasHidden=Piece->IsHidden();
    bSourceVitalsWereVisible=Piece->PreparationVitals&&Piece->PreparationVitals->IsVisible();
    bSourceVisualSuppressed=true;
    // AEGArtPiece forwards hiding to its separately spawned preparation assembly.
    // Keep the selected part alive and occupying its original cell until release.
    Piece->SetActorHiddenInGame(true);
    Piece->SetPreparationVitalsVisible(false);
    // Same rule as hand drags: the visible sale plates start at the real value.
    MainHUD->SetSellZoneSaleValue(SceneSettings->Manager->IsSaleProtected(Piece->InstanceId)?-1:SceneSettings->Manager->GetSaleValue(Piece->InstanceId));
    MainHUD->SetDragActive(true);
    if(MainHUD->CardWidgetClass)
    {
        WorldDragVisual=CreateWidget<UEGHandCard>(this,MainHUD->CardWidgetClass);
        WorldDragVisual->Configure(Piece->InstanceId,Piece->CardDefinition&&Piece->CardDefinition->Artwork?
            Piece->CardDefinition->Artwork.Get():MainHUD->CardArtwork.Get());
        WorldDragVisual->SetDefinitionAppearance(Piece->CardDefinition?Piece->CardDefinition->HeroDefinition.Get():nullptr);
        WorldDragVisual->SetVisibility(ESlateVisibility::HitTestInvisible);
        WorldDragVisual->AddToViewport(100);WorldDragVisual->SetDesiredSizeInViewport(FVector2D(EGPearlGold::HandCardWidth,EGPearlGold::HandCardHeight));
        WorldDragVisual->SetAlignmentInViewport(FVector2D(.5,.5));
    }
    return true;
}
bool AEGBoardPlayerController::TryBeginWorldDragAtScreen(FVector2D P)
{
    if (IsPresentationInputBlocked(this)) return false;
    UEGMainHUD* MainHUD=GetMainHUD();
    if(DragState!=EEGPlacementDragState::Idle||!IsValid(MainHUD)||MainHUD->IsPointBlocked(P))return false;
    FVector O,D;if(!ScreenRay(P,O,D))return false;
    FHitResult Hit;
    if(!GetWorld()->LineTraceSingleByChannel(Hit,O,O+D*100000.f,ECC_GameTraceChannel2))return false;
    if(!BeginPlacedPieceDrag(Cast<AEGDeployedPiece>(Hit.GetActor())))return false;
    UpdateDragAtScreenPosition(P);return true;
}
void AEGBoardPlayerController::ClearDragVisuals()
{
    UEGMainHUD* MainHUD=GetMainHUD();
    if(IsValid(Preview))Preview->HidePreview();
    if(IsValid(SceneSettings)&&IsValid(SceneSettings->Board))SceneSettings->Board->SetBoardVisualsVisible(false);
    if(IsValid(MainHUD))MainHUD->SetSellZoneFeedback(0);
}
void AEGBoardPlayerController::UpdateDragAtScreenPosition(FVector2D P)
{
 auto* HUD=GetMainHUD();
 if(DragState==EEGPlacementDragState::Idle||DragState==EEGPlacementDragState::Committing||DragState==EEGPlacementDragState::Cancelling)return;
 if(!IsDragSourceValid()||!IsInsideViewport(P)){CancelActiveDrag();return;}
 const bool bWasLegalPreview=bAudioLegalPreview;
 const bool bWasSellTarget=bAudioSellTarget;
 bAudioLegalPreview=false;
 bAudioSellTarget=false;
 auto* M=SceneSettings->Manager.Get();bool InCards=HUD->IsInCardArea(P);
 UEGHeroDefinition* FeedbackDefinition=nullptr;UTexture2D* FeedbackArtwork=nullptr;
 const bool bShop=DragSource==EEGDragSource::Shop;
 const bool InShop=(bShop||DragSource==EEGDragSource::HandCard)&&HUD->IsInShopArea(P);
 if(bShop&&DraggedShopDefinition){FeedbackDefinition=DraggedShopDefinition->HeroDefinition;FeedbackArtwork=DraggedShopDefinition->Artwork;}
 else if(DraggedCardId.IsValid())
 {
  if(auto* Card=M->FindHandCardDefinition(DraggedCardId)){FeedbackDefinition=Card->HeroDefinition;FeedbackArtwork=Card->Artwork;}
  else if(auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())FeedbackDefinition=GM->GetRoster()->ResolveDefinition(DraggedCardId);
 }
 else if(auto* Piece=DraggedPiece.Get())
 {
  if(auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())FeedbackDefinition=GM->GetRoster()->ResolveDefinition(Piece->RosterId);
  if(Piece->CardDefinition)FeedbackArtwork=Piece->CardDefinition->Artwork;
 }
 const int32 SellZone=HUD->GetSellZoneAt(P);HUD->SetSellZoneFeedback(SellZone);
 // Both drag visuals are centred on the pointer. Use their actual painted size,
 // even while transparent in model mode, rather than waiting for pointer entry.
 const UWidget* CardVisual=CardOperation&&CardOperation->DefaultDragVisual?CardOperation->DefaultDragVisual.Get():WorldDragVisual.Get();
 const auto& HUDGeometry=HUD->GetCachedGeometry();
 const FVector2D FallbackSize=HUDGeometry.LocalToAbsolute(FVector2D(EGPearlGold::HandCardWidth,EGPearlGold::HandCardHeight))-HUDGeometry.LocalToAbsolute(FVector2D::ZeroVector);
 FVector2D CardSize(FMath::Abs(FallbackSize.X),FMath::Abs(FallbackSize.Y));
 if(CardVisual&&CardVisual->GetCachedGeometry().GetLocalSize().X>0.f&&CardVisual->GetCachedGeometry().GetLocalSize().Y>0.f)
  CardSize=FVector2D(CardVisual->GetCachedGeometry().GetRenderBoundingRect().GetSize2f());
 const bool bCardTouchesSellZone=HUD->IsDragCardTouchingSellZone(P,CardSize);
 TArray<FVector2D> LegalCellCorners;
 if(DragSource==EEGDragSource::PlacedPiece)
 {
  auto* Grid=SceneSettings->Board.Get();
  DragOrigin=UEGWidgetEditorLibrary::WorldScreenPosition(this,Grid->CellToWorld(DraggedPiece->Cell));
  // Re-evaluate the small friendly grid against the same rules used on release.
  // No preview actor or cached visual can reserve, move or free an actual cell.
  const FVector HalfX=Grid->GetActorTransform().TransformVector(FVector(Grid->CellWidth*.46,0,0));
  const FVector HalfY=Grid->GetActorTransform().TransformVector(FVector(0,Grid->CellDepth*.46,0));
  for(int32 Y=0;Y<Grid->Rows;++Y)for(int32 X=0;X<Grid->Columns;++X)
  {
   const FIntPoint Candidate(X,Y);
   if(!Grid->IsPlayerCell(Candidate)||!M->EvaluatePieceMove(DraggedPiece.Get(),Candidate).IsAllowed())continue;
   const FVector Center=Grid->CellToWorld(Candidate);
   for(const FVector& Corner:{Center-HalfX-HalfY,Center+HalfX-HalfY,Center+HalfX+HalfY,Center-HalfX+HalfY})
    LegalCellCorners.Add(UEGWidgetEditorLibrary::WorldScreenPosition(this,Corner));
  }
 }
 bool Legal=false;
 // One presentation switch covers both hand and placed-piece drags. Run it for
 // sale hover too, before that branch returns. No transaction happens here.
 auto PresentCard=[&](bool bShowCard,bool bSuppressBoardMove)
 {
  // The old overlay also drew a card over the 3D preview. The drag operation (or
  // the world-drag widget) is now the only card visual; outside UI we show the model.
  HUD->UpdateDragFeedback(FeedbackDefinition,FeedbackArtwork,DragOrigin,P,false,false);
  if(DragSource==EEGDragSource::PlacedPiece)
   HUD->UpdateBoardMoveFeedback(bSuppressBoardMove?TArray<FVector2D>():LegalCellCorners,DragOrigin,P,Legal,!bShowCard);
  if(CardOperation&&CardOperation->DefaultDragVisual)CardOperation->DefaultDragVisual->SetRenderOpacity(bShowCard?1.f:0.f);
  if(WorldDragVisual)
  {
   WorldDragVisual->SetRenderOpacity(bShowCard?1.f:0.f);
   auto View=GetWorld()->GetGameViewport()->GetGameViewportWidget();const auto G=View->GetCachedGeometry();
   const FVector2D Local=G.AbsoluteToLocal(P),Size=G.GetLocalSize();int32 Width,Height;GetViewportSize(Width,Height);
   if(Size.X>0&&Size.Y>0)WorldDragVisual->SetPositionInViewport(FVector2D(Local.X*Width/Size.X,Local.Y*Height/Size.Y),true);
  }
 };
 if(SellZone)
 {
  // Keep the original model-to-card transition while hovering a sale target.
  PresentCard(true,true);
  if(Preview)Preview->HidePreview();SceneSettings->Board->SetBoardVisualsVisible(false);DragState=EEGPlacementDragState::HoveringSellZone;
  const FGuid SaleId=DraggedPiece.IsValid()?DraggedPiece->InstanceId:DraggedCardId;
  const bool bProtected=M->IsSaleProtected(SaleId);
  bAudioSellTarget=!bShop&&!bProtected;
  if(bAudioSellTarget&&!bWasSellTarget)UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::SellZone);
  const int64 Value=M->GetSaleValue(SaleId);
  if(auto* View=Cast<UEGOverlayWidgetController>(HUD->GetWidgetController()))View->PresentRuleFeedback(FText::FromString(bProtected?TEXT("爱莉希雅不可出售"):FString::Printf(TEXT("松开出售  +%lld 崩坏能"),Value)));
  HUD->SetSellZoneSaleValue(bProtected?-1:Value);
  return;
 }
 // A shop offer released over the animated compact shop returns the offer without
 // spending gold, replacing the offer or reserving a cell.
 if(bShop&&InShop)
 {
  PresentCard(true,true);
  if(Preview)Preview->HidePreview();SceneSettings->Board->SetBoardVisualsVisible(false);
  DragState=EEGPlacementDragState::HoveringShopReturn;
  HUD->SetSellZoneFeedback(0);
  if(auto* View=Cast<UEGOverlayWidgetController>(HUD->GetWidgetController()))View->PresentRuleFeedback(FText::FromString(TEXT("松开返回商店")));
  return;
 }
 // Suppress board presentation while the pointer is over ordinary HUD controls.
 // Sale, hand and shop keep their existing precedence and release evaluation.
 if(!InCards&&!InShop&&HUD->IsPointBlocked(P))
 {
  PresentCard(true,true);
  if(Preview)Preview->HidePreview();
  SceneSettings->Board->SetBoardVisualsVisible(false);
  DragState=EEGPlacementDragState::DraggingHandCard2D;
  if(auto* View=Cast<UEGOverlayWidgetController>(HUD->GetWidgetController()))
   View->PresentRuleFeedback(FText::FromString(TEXT("拖入棋盘部署或移动  Esc 取消")));
  return;
 }
 auto* PreviousPreview=Preview.Get();
 SceneSettings->Board->SetBoardVisualsVisible(false);
 FIntPoint Cell;FEGPlacementEvaluation Result;
 if(!InCards&&!InShop)
 {
  if(BoardCellAt(P,Cell))
  {
   if(bShop){Result=M->EvaluateShopPlacement(DraggedShopSlot,DraggedShopRevision,Cell);Legal=Result.IsAllowed();}
   else if(DraggedCardId.IsValid()&&M->HasReserveHero(DraggedCardId)){Result=M->EvaluateReservePlacement(DraggedCardId,Cell);Legal=Result.IsAllowed();}
   else {Result=DraggedCardId.IsValid()?M->EvaluateHandPlacement(DraggedCardId,Cell):M->EvaluatePieceMove(DraggedPiece.Get(),Cell);Legal=Result.IsAllowed();}
   SceneSettings->Board->SetBoardVisualsVisible(true);
   const FIntPoint HighlightSize=Result.Action==EEGPlacementAction::Merge||!FeedbackDefinition
      ?FIntPoint(1,1):FeedbackDefinition->GetPlacementFootprint();
   SceneSettings->Board->SetHoveredFootprint(Result.Action==EEGPlacementAction::Merge?Result.TargetCell:Cell,HighlightSize,Legal);
  }
  {
   UEGHeroDefinition* D=nullptr;
   if(bShop&&DraggedShopDefinition)D=DraggedShopDefinition->HeroDefinition;
   else if(DraggedCardId.IsValid())
   {
    if(auto* Card=M->FindHandCardDefinition(DraggedCardId))D=Card->HeroDefinition;
    else if(auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())D=GM->GetRoster()->ResolveDefinition(DraggedCardId);
   }
   else if(DraggedPiece.IsValid())
   {
    if(DraggedPiece->PartRole!=EEGDeploymentPart::Hero)D=DraggedPiece->HeroVisualDefinition;
    else if(auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())D=GM->GetRoster()->ResolveDefinition(DraggedPiece->RosterId);
   }
   auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>();
   if(Mode)
   {
    auto* Resources=Mode->FindComponentByClass<UEGRunResourceComponent>();
    Preview=Resources?Resources->AcquirePreview(D):nullptr;
   }
   else
   {
    if(!Preview){FActorSpawnParameters Params;Params.Owner=this;Preview=GetWorld()->SpawnActor<AEGPlacementPreview>(PreviewClass?PreviewClass.Get():AEGPlacementPreview::StaticClass(),Params);}
    if(D&&Preview&&Preview->HeroVisualDefinition!=D)Preview->ApplyHeroDefinition(D);
   }
   if(PreviousPreview&&PreviousPreview!=Preview)PreviousPreview->HidePreview();
   bool bShown=false;
   FVector O,V;if(D&&Preview&&Preview->Body&&Preview->Body->GetSkeletalMeshAsset()&&ScreenRay(P,O,V))
   {
    const FVector N=SceneSettings->Board->GetActorUpVector();const FVector Plane=SceneSettings->Board->GetActorLocation();
    const float Den=FVector::DotProduct(V,N);
    if(FMath::Abs(Den)>.0001f){const float T=FVector::DotProduct(Plane-O,N)/Den;if(T>0){Preview->ShowAt(O+V*T,Legal);bShown=true;}}
   }
   if(Preview&&!bShown)Preview->HidePreview();
  }
  DragState=Legal?EEGPlacementDragState::PreviewValid:EEGPlacementDragState::PreviewInvalid;
  bAudioLegalPreview=Legal;
  if(Legal&&!bWasLegalPreview)UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::ValidTarget);
  if(auto* View=Cast<UEGOverlayWidgetController>(HUD->GetWidgetController()))View->PresentRuleFeedback(Result.Message.IsEmpty()?FText::FromString(Legal?TEXT("松开部署或移动  Esc 取消"):TEXT("不可放置  松开保留原状态")):Result.Message);
 }
 else
 {
  if(Preview)Preview->HidePreview();
  DragState=EEGPlacementDragState::DraggingHandCard2D;
  if(auto* View=Cast<UEGOverlayWidgetController>(HUD->GetWidgetController()))View->PresentRuleFeedback(FText::FromString(bShop?TEXT("拖入棋盘购买并部署  手牌区松开购买  Esc 取消"):DraggedPiece.IsValid()&&M->CanWithdraw(DraggedPiece.Get())?TEXT("松开返回手牌  保留等级、合成与强化"):HUD->ReserveTargetAt(P).IsValid()?TEXT("同名卡松开合成"):TEXT("松开取消  保留原位置与卡牌")));
 }
 // This anticipates the appearance change only. Release still re-evaluates the
 // pointer through the existing sale transaction; an overlapping edge never sells.
 if(bCardTouchesSellZone)
 {
  if(Preview)Preview->HidePreview();
  SceneSettings->Board->SetBoardVisualsVisible(false);
 }
 PresentCard(InCards||InShop||bCardTouchesSellZone||!Preview||Preview->IsHidden(),InCards||InShop||bCardTouchesSellZone);
}
bool AEGBoardPlayerController::TryCompleteDragAtScreenPosition(FVector2D P)
{
 if (IsPresentationInputBlocked(this)) { CancelActiveDrag(); return false; }
 if(DragState==EEGPlacementDragState::Idle||DragState==EEGPlacementDragState::Committing||DragState==EEGPlacementDragState::Cancelling)return false;
 DragState=EEGPlacementDragState::Committing;bool Success=false;auto* HUD=GetMainHUD();
 EEGGameAudioCue SuccessCue=EEGGameAudioCue::Place;
 bool bPlaySuccessCue=false;
 const bool bUserCanEdit=IsValid(SceneSettings)&&IsValid(SceneSettings->Manager)&&SceneSettings->Manager->CanEditPlacement();
 if(IsInsideViewport(P)&&SceneSettings&&SceneSettings->Manager&&HUD)
 {
  auto* M=SceneSettings->Manager.Get();
  if(DragSource==EEGDragSource::Shop)
  {
   // Returning to the animating compact shop is tested before any board/hand
   // commit, so a release there can never spend gold or replace the offer.
   const bool bReturnToShop=HUD->IsInShopArea(P);
   if(!bReturnToShop&&IsDragSourceValid())
   {
    if(HUD->IsInCardArea(P))
    {
     Success=M->BuyShopSlotExpected(DraggedShopSlot,DraggedShopRevision);
     SuccessCue=EEGGameAudioCue::Purchase;bPlaySuccessCue=true;
    }
    else {FIntPoint Cell;if(BoardCellAt(P,Cell))
    {
     const auto Evaluation=M->EvaluateShopPlacement(DraggedShopSlot,DraggedShopRevision,Cell);
     SuccessCue=Evaluation.Action==EEGPlacementAction::Merge?EEGGameAudioCue::Merge:EEGGameAudioCue::Place;
     bPlaySuccessCue=true;Success=RequestShopDeployment(DraggedShopSlot,DraggedShopRevision,Cell);
    }}
   }
  }
  else if(HUD->GetSellZoneAt(P))
  {
   if(DraggedPiece.IsValid())Success=M->TrySellPlacedPiece(DraggedPiece.Get());
   else Success=M->HasReserveHero(DraggedCardId)?M->TrySellReserveHero(DraggedCardId):M->TrySellHandCard(DraggedCardId);
   SuccessCue=EEGGameAudioCue::Sell;bPlaySuccessCue=true;
  }
  else if(HUD->IsInCardArea(P))
  {
   if(DraggedPiece.IsValid())Success=M->TryWithdraw(DraggedPiece.Get());
   else if(auto Target=HUD->ReserveTargetAt(P);Target.IsValid())
   {Success=M->TryMergeIntoHero(DraggedCardId,Target);SuccessCue=EEGGameAudioCue::Merge;bPlaySuccessCue=true;}
  }
  else
  {
   FIntPoint Cell;if(BoardCellAt(P,Cell))
   {
    const auto Evaluation=DraggedCardId.IsValid()
     ?(M->HasReserveHero(DraggedCardId)?M->EvaluateReservePlacement(DraggedCardId,Cell):M->EvaluateHandPlacement(DraggedCardId,Cell))
     :M->EvaluatePieceMove(DraggedPiece.Get(),Cell);
    SuccessCue=Evaluation.Action==EEGPlacementAction::Merge?EEGGameAudioCue::Merge:EEGGameAudioCue::Place;
    bPlaySuccessCue=true;
    if(DraggedCardId.IsValid())Success=M->HasReserveHero(DraggedCardId)?M->TryDeployReserve(DraggedCardId,Cell):M->TryDeployHandCard(DraggedCardId,Cell);
    else if(DraggedPiece.IsValid())Success=M->TryMovePlacedPiece(DraggedPiece.Get(),Cell);
   }
  }
 }
 if(Success&&bPlaySuccessCue)UEGGameAudioComponent::PlayUI(this,SuccessCue);
 else if(!Success&&bUserCanEdit)UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Cancel);
 FinishDrag();return Success;
}
void AEGBoardPlayerController::FinishDrag()
{
    // Invalidate callbacks before any HUD/delegate cleanup can re-enter Slate.
    if(CardOperation)CardOperation->MarkFinished();
    if(auto* ShopOp=Cast<UEGShopDragOperation>(CardOperation))ShopOp->bConsumed=true;
    UEGMainHUD* MainHUD=GetMainHUD();
    ClearDragVisuals();
    if(bSourceVisualSuppressed)
    {
        auto* Manager=IsValid(SceneSettings)?SceneSettings->Manager.Get():nullptr;
        // Sale/withdraw may destroy the source. Combat owns preparation visibility
        // once the phase changes, so cleanup must never resurrect that model.
        if(DraggedPiece.IsValid()&&IsValid(Manager)&&Manager->CanEditPlacement()&&Manager->OwnsPiece(DraggedPiece.Get()))
        {
            DraggedPiece->SetActorHiddenInGame(bSourceWasHidden);
            DraggedPiece->SetPreparationVitalsVisible(bSourceVitalsWereVisible);
        }
        bSourceVisualSuppressed=false;
    }
    if(IsValid(MainHUD))MainHUD->SetDragActive(false);
    if(IsValid(MainHUD))if(auto* Source=MainHUD->FindCardWidget(DraggedCardId))Source->SetRenderOpacity(1.f);
    if(WorldDragVisual){WorldDragVisual->RemoveFromParent();WorldDragVisual=nullptr;}
    CardOperation=nullptr;DraggedCardId.Invalidate();DraggedPiece.Reset();DraggedShopDefinition=nullptr;DraggedShopSlot=DraggedShopRevision=INDEX_NONE;DragSource=EEGDragSource::None;DragState=EEGPlacementDragState::Idle;
    bAudioLegalPreview=false;bAudioSellTarget=false;
}
void AEGBoardPlayerController::CancelActiveDrag()
{
    CancelPendingShopDeployment();
    if(auto* View=GetMainHUD())View->CancelHandReorder();
    if(DragState==EEGPlacementDragState::Idle||DragState==EEGPlacementDragState::Cancelling||DragState==EEGPlacementDragState::Committing)return;
    DragState=EEGPlacementDragState::Cancelling;
    TSharedPtr<FDragDropOperation> SlateOperation;
    if(CardOperation&&FSlateApplication::IsInitialized()&&UWidgetBlueprintLibrary::GetDragDroppingContent()==CardOperation.Get())
        SlateOperation=FSlateApplication::Get().GetDragDroppingContent();
    FinishDrag();
    UEGCardDragOperation::CancelSlateDragDeferred(SlateOperation);
}
void AEGBoardPlayerController::CancelPendingShopDeployment() {}
bool AEGBoardPlayerController::RequestShopDeployment(int32 Slot,int32 Revision,FIntPoint Cell)
{
 auto* Manager=IsValid(SceneSettings)?SceneSettings->Manager.Get():nullptr;
 if(!IsValid(Manager))return false;
 // The manager remains the sole transaction authority; background presentation work cannot delay a release.
 return Manager->TryBuyShopAndDeploy(Slot,Revision,Cell);
}
void AEGBoardPlayerController::ApplicationActivationChanged(bool bActive){if(!bActive)CancelActiveDrag();}

AEGBoardGameMode::AEGBoardGameMode()
{
    PlayerControllerClass = AEGBoardPlayerController::StaticClass();
    DefaultPawnClass = nullptr;
    HUDClass = AEGHUD::StaticClass();
}

void AEGBoardPlayerController::FindInspectableAtScreen(FVector2D P,AEGChessUnit*& Unit,AEGDeployedPiece*& Piece)
{
 Unit=nullptr;Piece=nullptr;auto* View=GetMainHUD();
 if(!View||!SceneSettings||!SceneSettings->Manager||!IsInsideViewport(P)||View->IsPointBlocked(P))return;
 float Best=FMath::Square(64.f);
 if(auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>())
  for(const auto& Ref:GM->GetBattleUnits())if(auto* U=Ref.Get();IsValid(U)&&!U->IsHidden())
  {const float Height=U->GetDefinition()?U->GetDefinition()->GetPresentationHeight():0.f;const float D=FVector2D::DistSquared(P,UEGWidgetEditorLibrary::WorldScreenPosition(this,U->GetActorLocation()+FVector(0,0,Height>0?Height*.5f-88.f:25.f)));if(D<Best){Best=D;Unit=U;}}
 if(!Unit)for(auto* U:SceneSettings->Manager->GetDeploymentParts())if(IsValid(U)&&!U->IsHidden())
 {const float Height=U->HeroVisualDefinition?U->HeroVisualDefinition->GetPresentationHeight():0.f;const float D=FVector2D::DistSquared(P,UEGWidgetEditorLibrary::WorldScreenPosition(this,U->GetActorLocation()+FVector(0,0,Height>0?Height*.5f:110.f)));if(D<Best){Best=D;Piece=U;}}
}
void AEGBoardPlayerController::UpdateInspectionAtScreenPosition(FVector2D P)
{
 auto* View=GetMainHUD();if(!View)return;
 AEGChessUnit* Unit=nullptr;AEGDeployedPiece* Piece=nullptr;FindInspectableAtScreen(P,Unit,Piece);
 View->HideCardInformation(true);bWorldInformationVisible=false;
 if(Unit)View->InspectUnit(Unit);
 else if(Piece&&GetGameInstance())View->InspectHero(GetGameInstance()->GetSubsystem<UEGHeroRoster>(),Piece->RosterId);
 else View->InspectUnit(nullptr);
}
void AEGBoardPlayerController::UpdateHoverInformationAtScreenPosition(FVector2D P)
{
 auto* View=GetMainHUD();if(!View||!View->bBoardPresentation)return;
 AEGChessUnit* Unit=nullptr;AEGDeployedPiece* Piece=nullptr;
 if(!View->HasHeroSelection())FindInspectableAtScreen(P,Unit,Piece);
 UEGHeroDefinition* Definition=nullptr;int32 Level=1;bool Awakened=false;
 FEGStatBlock Stats;float Health=0,Mana=0;
 if(Unit&&Unit->GetDefinition()&&Unit->GetHeroSkill()&&Unit->GetAttributes())
 {
  Definition=Unit->GetDefinition();Level=Unit->GetHeroSkill()->GetLevel();Awakened=Unit->GetHeroSkill()->bAwakened;
  const auto* Attributes=Unit->GetAttributes();Health=Attributes->GetHealth();Mana=Attributes->GetMana();
  Stats.MaxHealth=Attributes->GetMaxHealth();Stats.MaxMana=Attributes->GetMaxMana();Stats.AttackPower=Attributes->GetAttackPower();Stats.Defense=Attributes->GetDefense();
 }
 else if(Piece&&GetGameInstance())
 {
  auto* Roster=GetGameInstance()->GetSubsystem<UEGHeroRoster>();FEGHeroInstance Hero;
  if(Roster&&Roster->GetHero(Piece->RosterId,Hero))
  {Definition=Roster->ResolveDefinition(Piece->RosterId);Level=Hero.Level;Awakened=Hero.bAwakened;if(Definition){Stats=Definition->EvaluateStats(Level);Health=Stats.MaxHealth;}}
 }
 if(!Definition)
 {
  if(bWorldInformationVisible)View->HideCardInformation(false,TEXT("World"));bWorldInformationVisible=false;return;
 }
 auto* Controller=Cast<UEGOverlayWidgetController>(View->GetWidgetController());
 const FString Status=EGUnitInformation::Status(Unit);
 const FString Body=FString::Printf(TEXT("Lv.%d  %s\n生命 %.0f / %.0f    法力 %.0f / %.0f\n攻击 %.0f    防御 %.0f\n\n"),Level,Awakened?TEXT("已觉醒"):TEXT("未觉醒"),Health,Stats.MaxHealth,Mana,Stats.MaxMana,Stats.AttackPower,Stats.Defense)+(Status.IsEmpty()?FString():Status+TEXT("\n\n"));
 View->ShowWorldInformation(Definition,FText::FromString(Body),Health,Stats.MaxHealth,Mana,Stats.MaxMana);
 bWorldInformationVisible=true;
}
