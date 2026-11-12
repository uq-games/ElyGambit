#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "UnrealClient.h"
#include "Actor/EGArtPiece.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGOpeningPresentation.h"
#include "Presentation/EGTeleportVisual.h"
#include "Components/EGBattleContextComponent.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "UObject/UnrealType.h"
#include "UI/Widget/EGOpeningWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGDemoPanel.h"

namespace
{
class FIntegratedOpeningStart1080PIE final : public IAutomationLatentCommand
{
public:
    bool Update() override
    {
        ULevelEditorPlaySettings* Settings = DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(), GetTransientPackage());
        Settings->NewWindowWidth = 1920;
        Settings->NewWindowHeight = 1080;
        FRequestPlaySessionParams Params;
        Params.EditorPlaySettings = Settings;
        TSharedRef<SWindow> Window = SNew(SWindow).ScreenPosition(FVector2D(0, 0)).ClientSize(FVector2D(1920, 1080))
            .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
            .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
        FSlateApplication::Get().AddWindow(Window);
        Params.CustomPIEWindow = Window;
        if (GUnrealEd->CheckForPlayerStart() == nullptr) FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
        GUnrealEd->RequestPlaySession(Params);
        return true;
    }
};

UObject* OpeningObjectProperty(UObject* Object, const TCHAR* Name)
{
    const FObjectPropertyBase* Property = Object ? FindFProperty<FObjectPropertyBase>(Object->GetClass(), FName(Name)) : nullptr;
    return Property ? Property->GetObjectPropertyValue_InContainer(Object) : nullptr;
}

void LogOpeningGeometry(FAutomationTestBase* Test, const TCHAR* Stage, APlayerController* Player, UEGOpeningPresentation* Presentation)
{
    AActor* View = Player ? Player->GetViewTarget() : nullptr;
    Test->AddInfo(FString::Printf(TEXT("[%s] camera=%s location=%s rotation=%s"), Stage,
        *GetNameSafe(View), View ? *View->GetActorLocation().ToString() : TEXT("none"),
        View ? *View->GetActorRotation().ToString() : TEXT("none")));
    for (const TCHAR* Name : {TEXT("Elysia"), TEXT("Corruption")})
    {
        AEGArtPiece* Piece = Cast<AEGArtPiece>(OpeningObjectProperty(Presentation, Name));
        const TCHAR* Source = TEXT("opening");
        if (!IsValid(Piece))
        {
            AEGDemoGameMode* Mode = Presentation ? Cast<AEGDemoGameMode>(Presentation->GetOwner()) : nullptr;
            UEGBoardMasterPresentation* Masters = Mode ? Mode->FindComponentByClass<UEGBoardMasterPresentation>() : nullptr;
            Piece = Cast<AEGArtPiece>(OpeningObjectProperty(Masters, FCString::Strcmp(Name, TEXT("Elysia")) == 0 ? TEXT("Friendly") : TEXT("Enemy")));
            Source = TEXT("board master");
        }
        if (!IsValid(Piece))
        {
            Test->AddInfo(FString::Printf(TEXT("[%s] %s opening performer absent"), Stage, Name));
            continue;
        }
        USkeletalMeshComponent* Body = Piece->Body;
        Test->AddInfo(FString::Printf(TEXT("[%s] %s source=%s hidden=%d actor_location=%s actor_rotation=%s actor_scale=%s body_location=%s body_rotation=%s body_scale=%s"),
            Stage, Name, Source, Piece->IsHidden()?1:0, *Piece->GetActorLocation().ToString(), *Piece->GetActorRotation().ToString(), *Piece->GetActorScale3D().ToString(),
            Body ? *Body->GetComponentLocation().ToString() : TEXT("none"), Body ? *Body->GetComponentRotation().ToString() : TEXT("none"),
            Body ? *Body->GetComponentScale().ToString() : TEXT("none")));
    }
    const FArrayProperty* Array = Presentation ? FindFProperty<FArrayProperty>(Presentation->GetClass(), TEXT("OpeningSetPieces")) : nullptr;
    const FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
    if (!Array || !Inner)
    {
        Test->AddInfo(FString::Printf(TEXT("[%s] generated opening geometry is unavailable"), Stage));
        return;
    }
    FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Presentation));
    TSet<FString> ReportedMeshes;
    for (int32 Index = 0; Index < Helper.Num(); ++Index)
    {
        AStaticMeshActor* Actor = Cast<AStaticMeshActor>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index)));
        UStaticMeshComponent* Component = IsValid(Actor) ? Actor->GetStaticMeshComponent() : nullptr;
        UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
        if (!IsValid(Mesh) || ReportedMeshes.Contains(Mesh->GetPathName())) continue;
        ReportedMeshes.Add(Mesh->GetPathName());
        const FBoxSphereBounds AssetBounds = Mesh->GetBounds();
        Test->AddInfo(FString::Printf(TEXT("[%s] mesh=%s actor_location=%s actor_rotation=%s actor_scale=%s world_bounds_origin=%s world_bounds_extent=%s asset_bounds_origin=%s asset_bounds_extent=%s"),
            Stage, *Mesh->GetPathName(), *Actor->GetActorLocation().ToString(), *Actor->GetActorRotation().ToString(), *Actor->GetActorScale3D().ToString(),
            *Component->Bounds.Origin.ToString(), *Component->Bounds.BoxExtent.ToString(), *AssetBounds.Origin.ToString(), *AssetBounds.BoxExtent.ToString()));
    }
}

void LogOriginalBoardGeometry(FAutomationTestBase* Test, UWorld* World, UEGOpeningPresentation* Presentation)
{
    TSet<const AStaticMeshActor*> OpeningActors;
    const FArrayProperty* Array = Presentation ? FindFProperty<FArrayProperty>(Presentation->GetClass(), TEXT("OpeningSetPieces")) : nullptr;
    const FObjectPropertyBase* Inner = Array ? CastField<FObjectPropertyBase>(Array->Inner) : nullptr;
    if (Array)
    {
        if (!Inner)
        {
            Test->AddError(TEXT("Opening set-piece array has an unsupported element type; cannot identify generated actors."));
            return;
        }
        FScriptArrayHelper Helper(Array, Array->ContainerPtrToValuePtr<void>(Presentation));
        Test->TestEqual(TEXT("No generated opening set pieces on the original platform"), Helper.Num(), 0);
        for (int32 Index = 0; Index < Helper.Num(); ++Index)
            if (const AStaticMeshActor* Actor = Cast<AStaticMeshActor>(Inner->GetObjectPropertyValue(Helper.GetRawPtr(Index))))
                OpeningActors.Add(Actor);
    }
    else
    {
        Test->AddError(TEXT("Opening set-piece array was not found; cannot verify the original platform has no generated set pieces."));
        return;
    }
    int32 Count = 0;
    for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
    {
        AStaticMeshActor* Actor = *It;
        if (!IsValid(Actor) || OpeningActors.Contains(Actor)) continue;
        UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
        UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
        if (!IsValid(Mesh) || !Mesh->GetPathName().Contains(TEXT("HI3ElysiaDuelArena"))) continue;
        ++Count;
        Test->AddInfo(FString::Printf(TEXT("[OriginalBoard] actor_label=%s actor_name=%s mesh=%s location=%s rotation=%s scale=%s world_bounds_origin=%s world_bounds_extent=%s"),
            *Actor->GetActorLabel(), *Actor->GetName(), *Mesh->GetPathName(),
            *Actor->GetActorLocation().ToString(), *Actor->GetActorRotation().ToString(), *Actor->GetActorScale3D().ToString(),
            *Component->Bounds.Origin.ToString(), *Component->Bounds.BoxExtent.ToString()));
    }
    Test->AddInfo(FString::Printf(TEXT("[OriginalBoard] %d original HI3ElysiaDuelArena StaticMeshActor instances; %d generated actors excluded."), Count, OpeningActors.Num()));
}

UEGOpeningWidget* FindOpeningWidget(UWorld* World, APlayerController* Player)
{
    TArray<UUserWidget*> Views;
    UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World, Views, UEGOpeningWidget::StaticClass(), false);
    for (UUserWidget* View : Views)
        if (View && View->GetOwningPlayer() == Player && View->IsInViewport())
            return Cast<UEGOpeningWidget>(View);
    return nullptr;
}

AEGArtPiece* MasterProperty(UEGBoardMasterPresentation* Masters, const TCHAR* Name)
{
    const FObjectPropertyBase* Property = Masters ? FindFProperty<FObjectPropertyBase>(Masters->GetClass(), FName(Name)) : nullptr;
    return Property ? Cast<AEGArtPiece>(Property->GetObjectPropertyValue_InContainer(Masters)) : nullptr;
}

int32 EnemyCount(const AEGDemoGameMode* Mode)
{
    int32 Count = 0;
    for (AEGChessUnit* Unit : Mode->GetCombatUnits())
        if (IsValid(Unit) && Unit->GetFaction_Implementation() == EEGFaction::Enemy) ++Count;
    return Count;
}

class FIntegratedOpeningPIE final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    double Deadline = 0.;
    int32 Step = 0;
    FString Directory;
    FString CaptureFile;
    bool bCaptureArmed = false;
    bool bCaptureRequested = false;
    bool bCaptureFailed = false;
    FDelegateHandle CaptureHandle;
    TWeakObjectPtr<UGameViewportClient> CaptureViewport;
    TWeakObjectPtr<UEGOpeningWidget> OpeningWidget;
    FVector DepartureCameraStart = FVector::ZeroVector;
    FVector MenuElysiaStart = FVector::ZeroVector;
    TWeakObjectPtr<AEGArtPiece> MenuCorruption;
    TWeakObjectPtr<AEGArtPiece> PreviousCorruption;
    FVector PreviousCorruptionLocation = FVector::ZeroVector;
    double PreviousCorruptionTime = -1.;
    float PreviousCorruptionScale = 0.f;
    float CorruptionTravel = 0.f;
    int32 CorruptionWalkSamples = 0;
    bool bCorruptionStayedVisible = true, bCorruptionMotionContinuous = true, bCorruptionScaleContinuous = true;
    bool bCorruptionHandoffObserved = false, bCorruptionNoOverlap = true, bCorruptionRouteLegal = true;
    bool bCorruptionWalkAnimationObserved = false;
    FString CorruptionTrace = TEXT("world_time,x,y,z,hidden,body_scale,actor_yaw\n");
    double MenuObservationStarted = -1.;
    double DepartureObservedAt = -1.;
    int32 NextTransitSecond = 1;
    FString CameraTrace = TEXT("world_time,departure_time,x,y,z,pitch,yaw,roll,fov\n");
    int32 CameraSamples = 0;
    bool bOrbitAscending = true, bOrbitOutsideBoard = true, bOrbitLooksAtBoard = true;
    float PreviousCameraHeight = -MAX_flt, PeakCameraHeight = -MAX_flt;
    bool bOriginalSceneLogged = false;
    double StageTwoObservedAt = -1.;
    bool bStageTwoPortalsObserved = false;
    bool bStageTwoHiddenObserved = false;
    bool bFacingSampleValid = false;
    bool bFacingHandoffObserved = false;
    bool bFacingBoneMissing = false;
    bool bFacingChecked = false;
    int32 FacingSamples = 0;
    int32 FacingPairs = 0;
    float WorstFacingJump = 0.f;
    float WorstFacingAllowance = 0.f;
    double PreviousFacingTime = 0.;
    float PreviousFacingYaw = 0.f;
    bool bPreviousFacingWasOpening = false;
    FString FacingTrace = TEXT("world_time,departure_time,actor_yaw,hip_yaw,is_opening\n");

    void SampleArrivalFacing(UWorld* World, AEGDemoGameMode* Mode, UEGOpeningPresentation* Presentation)
    {
        if (Step == 0 || bFacingChecked || !Mode->GetBoard()) return;
        FVector FriendlyAnchor, EnemyAnchor;
        if (!UEGBoardMasterPresentation::GetMasterAnchors(Mode->GetBoard(), FriendlyAnchor, EnemyAnchor)) return;
        AEGArtPiece* Piece = Cast<AEGArtPiece>(OpeningObjectProperty(Presentation, TEXT("Corruption")));
        const bool bOpeningPiece = IsValid(Piece);
        if (!IsValid(Piece))
            Piece = MasterProperty(Mode->FindComponentByClass<UEGBoardMasterPresentation>(), TEXT("Enemy"));
        if (!IsValid(Piece) || Piece->IsHidden() || !IsValid(Piece->Body)) return;
        const FVector Ground = EnemyAnchor - FVector(0.f, 0.f, Mode->GetBoard()->UnitCenterHeight);
        if (FVector::Dist2D(Piece->GetActorLocation(), Ground) > 180.f) return;
        USkeletalMeshComponent* Body = Piece->Body;
        const FName Left(TEXT("Bip001-L-Thigh")), Right(TEXT("Bip001-R-Thigh"));
        if (Body->GetBoneIndex(Left) == INDEX_NONE || Body->GetBoneIndex(Right) == INDEX_NONE)
        {
            bFacingBoneMissing = true;
            return;
        }
        // The two animated hip joints describe the visible body's heading,
        // including animation-bone rotation, not merely the actor transform.
        const FVector Across = Body->GetBoneLocation(Right) - Body->GetBoneLocation(Left);
        if (Across.SizeSquared2D() < 1.f) return;
        const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Across.Y, Across.X));
        const double Now = World->GetTimeSeconds();
        FacingTrace += FString::Printf(TEXT("%.6f,%.6f,%.6f,%.6f,%d\n"),
            Now, DepartureObservedAt >= 0. ? Now - DepartureObservedAt : -1., Piece->GetActorRotation().Yaw, Yaw, bOpeningPiece ? 1 : 0);
        ++FacingSamples;
        if (bFacingSampleValid)
        {
            const double DeltaSeconds = Now - PreviousFacingTime;
            // A long frame cannot establish continuity; don't turn a hitch into
            // a false turn failure, and require regular samples below.
            if (DeltaSeconds > 0. && DeltaSeconds <= .2)
            {
                const float Jump = FMath::Abs(FMath::FindDeltaAngleDegrees(PreviousFacingYaw, Yaw));
                const float Allowance = 30.f + 240.f * static_cast<float>(DeltaSeconds);
                if (FacingPairs == 0 || Jump - Allowance > WorstFacingJump - WorstFacingAllowance)
                { WorstFacingJump = Jump; WorstFacingAllowance = Allowance; }
                ++FacingPairs;
                if (bPreviousFacingWasOpening && !bOpeningPiece) bFacingHandoffObserved = true;
            }
        }
        PreviousFacingTime = Now;
        PreviousFacingYaw = Yaw;
        bPreviousFacingWasOpening = bOpeningPiece;
        bFacingSampleValid = true;
    }

    void StopCaptureDelegate()
    {
        if (CaptureHandle.IsValid()) UGameViewportClient::OnViewportRendered().Remove(CaptureHandle);
        CaptureHandle.Reset();
        CaptureViewport.Reset();
        bCaptureArmed = false;
    }

    bool Capture(UWorld* World, const TCHAR* Name, APlayerController* Player, UEGOpeningPresentation* Presentation)
    {
        UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
        if (!Viewport || !Viewport->Viewport || !Viewport->GetWindow().IsValid() || !Viewport->GetGameViewportWidget().IsValid()) return false;
        const auto WidgetWindow = FSlateApplication::Get().FindWidgetWindow(Viewport->GetGameViewportWidget().ToSharedRef());
        if (!WidgetWindow.IsValid()) return false;
        if (WidgetWindow != Viewport->GetWindow())
        {
            Test->AddError(TEXT("Game viewport widget is not in its dedicated PIE window."));
            bCaptureFailed = true;
            StopCaptureDelegate();
            return true;
        }
        if (!bCaptureArmed)
        {
            CaptureFile = Directory / (FString(Name) + TEXT(".png"));
            CaptureViewport = Viewport;
            bCaptureArmed = true;
            LogOpeningGeometry(Test, Name, Player, Presentation);
            // The global screenshot request can otherwise be consumed by an editor viewport.
            // Arm it only after this exact PIE game viewport renders and immediately before
            // that same viewport processes screenshot requests.
            CaptureHandle = UGameViewportClient::OnViewportRendered().AddLambda([this](FViewport* Rendered)
            {
                UGameViewportClient* Target = CaptureViewport.Get();
                if (!bCaptureRequested && Target && Rendered == Target->Viewport && !FScreenshotRequest::IsScreenshotRequested())
                {
                    FScreenshotRequest::RequestScreenshot(CaptureFile, true, false, false, FIntRect(), true);
                    bCaptureRequested = true;
                }
            });
            return false;
        }
        if (CaptureViewport.Get() != Viewport)
        {
            Test->AddError(TEXT("PIE game viewport changed while a capture was pending."));
            bCaptureFailed = true;
            StopCaptureDelegate();
            return true;
        }
        if (!bCaptureRequested || FScreenshotRequest::IsScreenshotRequested()) return false;
        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *CaptureFile) || Bytes.Num() < 36) return false;
        const int32 End = Bytes.Num() - 12;
        if (Bytes[End] || Bytes[End+1] || Bytes[End+2] || Bytes[End+3] ||
            Bytes[End+4] != 'I' || Bytes[End+5] != 'E' || Bytes[End+6] != 'N' || Bytes[End+7] != 'D') return false;
        const uint8 Signature[] = {137,80,78,71,13,10,26,10};
        for (int32 I = 0; I < 8; ++I)
            if (Bytes[I] != Signature[I])
            {
                Test->AddError(TEXT("PIE screenshot is not a PNG."));
                bCaptureFailed = true;
                StopCaptureDelegate();
                return true;
            }
        const auto ReadBE = [&Bytes](int32 Offset) -> uint32
        {return (uint32(Bytes[Offset]) << 24) | (uint32(Bytes[Offset+1]) << 16) | (uint32(Bytes[Offset+2]) << 8) | uint32(Bytes[Offset+3]);};
        const FIntPoint Size(static_cast<int32>(ReadBE(16)), static_cast<int32>(ReadBE(20)));
        if (Size != FIntPoint(1920, 1080))
        {
            Test->AddError(FString::Printf(TEXT("PIE screenshot is %dx%d instead of 1920x1080: %s"), Size.X, Size.Y, *CaptureFile));
            bCaptureFailed = true;
        }
        else Test->AddInfo(FString::Printf(TEXT("1920x1080 PIE game viewport with UI: %s. Inspect image content separately."), *CaptureFile));
        StopCaptureDelegate();
        bCaptureRequested = false;
        CaptureFile.Reset();
        return true;
    }

public:
    explicit FIntegratedOpeningPIE(FAutomationTestBase* InTest) : Test(InTest)
    {
        Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Opening20261108") / TEXT("PIE") /
            FGuid::NewGuid().ToString(EGuidFormats::Digits));
        IFileManager::Get().MakeDirectory(*Directory, true);
    }

    virtual ~FIntegratedOpeningPIE() override
    {
        FFileHelper::SaveStringToFile(FacingTrace, *(Directory / TEXT("ArrivalFacing.csv")));
        FFileHelper::SaveStringToFile(CameraTrace, *(Directory / TEXT("CameraTrace.csv")));
        FFileHelper::SaveStringToFile(CorruptionTrace, *(Directory / TEXT("CorruptionWalk.csv")));
        if (bCaptureRequested && FScreenshotRequest::IsScreenshotRequested() && FScreenshotRequest::GetFilename() == CaptureFile)
            FScreenshotRequest::Reset();
        StopCaptureDelegate();
    }

    bool Update() override
    {
        if (Deadline == 0.) Deadline = FPlatformTime::Seconds() + 90.;
        if (FPlatformTime::Seconds() > Deadline)
        {
            Test->AddError(FString::Printf(TEXT("Integrated opening timed out at step %d (screenshots require a graphical PIE viewport)."), Step));
            return true;
        }
        UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
        AEGDemoGameMode* Mode = World ? World->GetAuthGameMode<AEGDemoGameMode>() : nullptr;
        AEGBoardPlayerController* Player = World ? Cast<AEGBoardPlayerController>(World->GetFirstPlayerController()) : nullptr;
        UEGOpeningPresentation* Presentation = Mode ? Mode->FindComponentByClass<UEGOpeningPresentation>() : nullptr;
        if (!Mode || !Player || !Presentation || !Mode->GetMatch()) return false;
        const FString Error = Presentation->GetLoadingError();
        if (!Error.IsEmpty())
        {
            Test->AddError(FString::Printf(TEXT("Opening reported an error: %s"), *Error));
            return true;
        }
        SampleArrivalFacing(World, Mode, Presentation);
        if (Step>0 && Presentation->IsDeparturePlaying())
        {
            AEGArtPiece* Piece=Cast<AEGArtPiece>(OpeningObjectProperty(Presentation,TEXT("Corruption")));
            if (!Piece || Piece->IsHidden() || !IsValid(Piece->Body)) bCorruptionStayedVisible=false;
            if (Piece && IsValid(Piece->Body))
            {
                const FVector Position=Piece->GetActorLocation();
                const float Scale=static_cast<float>(Piece->Body->GetComponentScale().GetAbsMax());
                const double Now=World->GetTimeSeconds();
                CorruptionTrace+=FString::Printf(TEXT("%.6f,%.3f,%.3f,%.3f,%d,%.4f,%.3f\n"),Now,Position.X,Position.Y,Position.Z,Piece->IsHidden()?1:0,Scale,Piece->GetActorRotation().Yaw);
                if (PreviousCorruption.IsValid() && PreviousCorruption.Get()!=Piece)
                {
                    if (!Presentation->HasSafeCorruptionHandoffForTests()) bCorruptionNoOverlap=false;
                    PreviousCorruptionTime=-1.;
                    bCorruptionHandoffObserved=true;
                }
                else if (!PreviousCorruption.IsValid() && Presentation->HasSafeCorruptionHandoffForTests())
                {
                    PreviousCorruptionTime=-1.;
                    bCorruptionHandoffObserved=true;
                }
                if (bCorruptionHandoffObserved && IsValid(MenuCorruption.Get()) && !MenuCorruption->IsHidden()) bCorruptionNoOverlap=false;
                if (bCorruptionHandoffObserved) if (const auto* Animation=Piece->Body->GetAnimInstance())
                    bCorruptionWalkAnimationObserved|=Animation->IsAnyMontagePlaying();
                if (bCorruptionHandoffObserved) if (const auto* Board=Mode->GetBoard()) if (const auto* Grid=Board->LayoutSource.Get())
                {
                    const FVector Local=Grid->GetActorTransform().InverseTransformPosition(Position);
                    const float BoardHalf=Grid->Rows*Grid->CellDepth*.5f;
                    if (Local.Y>BoardHalf+1600.f ||
                        (Local.Y>BoardHalf+800.f && FMath::Abs(Local.X)>450.f) ||
                        (Local.Y<=BoardHalf+800.f && FVector2D(Local.X,Local.Y).Size()>2600.f)) bCorruptionRouteLegal=false;
                }
                if (PreviousCorruptionTime>=0.)
                {
                    const double Delta=Now-PreviousCorruptionTime;
                    const float Distance=static_cast<float>(FVector::Dist(Position,PreviousCorruptionLocation));
                    CorruptionTravel+=Distance;
                    if (Delta>0. && Delta<=.25 && Distance>500.f*Delta+6.f) bCorruptionMotionContinuous=false;
                    if (bCorruptionHandoffObserved && FMath::Abs(Scale-PreviousCorruptionScale)>.02f) bCorruptionScaleContinuous=false;
                }
                PreviousCorruption=Piece;PreviousCorruptionLocation=Position;PreviousCorruptionTime=Now;PreviousCorruptionScale=Scale;
                ++CorruptionWalkSamples;
            }
        }
        if (DepartureObservedAt >= 0. && Presentation->IsDeparturePlaying())
        {
            if (auto* View = Cast<ACameraActor>(Player->GetViewTarget()))
            {
                const FVector P = View->GetActorLocation();
                const FRotator R = View->GetActorRotation();
                const auto* Lens = View->GetCameraComponent();
                CameraTrace += FString::Printf(TEXT("%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f\n"),
                    World->GetTimeSeconds(),World->GetTimeSeconds()-DepartureObservedAt,P.X,P.Y,P.Z,R.Pitch,R.Yaw,R.Roll,Lens?Lens->FieldOfView:0.f);
                const float Age=static_cast<float>(World->GetTimeSeconds()-DepartureObservedAt);
                if (Age < 10.f && PreviousCameraHeight > -MAX_flt*.5f && P.Z+1.f < PreviousCameraHeight) bOrbitAscending=false;
                PreviousCameraHeight=static_cast<float>(P.Z);PeakCameraHeight=FMath::Max(PeakCameraHeight,static_cast<float>(P.Z));
                if (const auto* Board=Mode->GetBoard()) if (const auto* Grid=Board->LayoutSource.Get())
                {
                    const FVector Center=Grid->GetActorLocation();
                    const float Corner=FVector::Dist2D(Center,Grid->CellToWorld(FIntPoint::ZeroValue));
                    const float HalfCell=.5f*FVector2D(Grid->CellWidth,Grid->CellDepth).Size()*Grid->GetActorScale3D().GetAbsMax();
                    if (FVector::Dist2D(Center,P)<Corner+HalfCell) bOrbitOutsideBoard=false;
                    if (Age>=2.5f && FVector::DotProduct(View->GetActorForwardVector(),(Center-P).GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(10.f)))
                        bOrbitLooksAtBoard=false;
                }
                ++CameraSamples;
            }
        }
        UEGOpeningWidget* Widget = FindOpeningWidget(World, Player);
        if (Step == 0)
        {
            if (!Widget || !Presentation->IsAwaitingStart()) return false;
            ACameraActor* OpeningCamera = Cast<ACameraActor>(OpeningObjectProperty(Presentation, TEXT("Camera")));
            AEGArtPiece* Elysia = Cast<AEGArtPiece>(OpeningObjectProperty(Presentation, TEXT("Elysia")));
            AEGArtPiece* Corruption = Cast<AEGArtPiece>(OpeningObjectProperty(Presentation, TEXT("Corruption")));
            if (!OpeningCamera || Player->GetViewTarget() != OpeningCamera || !Elysia || !Corruption) return false;
            auto* Start = Cast<UButton>(Widget->GetWidgetFromName(TEXT("OpeningStart")));
            if (!Test->TestNotNull(TEXT("Start control"), Start)) return true;
            if (!Start->IsVisible()) return false; // Widget and component may tick in either order.
            if (MenuObservationStarted < 0.)
            {
                MenuObservationStarted = World->GetTimeSeconds();
                MenuElysiaStart = Elysia->GetActorLocation();
                MenuCorruption = Corruption;
                PreviousCorruption = Corruption;
                OpeningWidget = Widget;
                return false;
            }
            if (World->GetTimeSeconds() - MenuObservationStarted < .6) return false;
            Test->TestTrue(TEXT("Elysia stays on the platform before the start click"), Elysia->GetActorLocation().Equals(MenuElysiaStart, 1.f));
            USkeletalMesh* ElysiaMesh = Elysia->Body ? Elysia->Body->GetSkeletalMeshAsset() : nullptr;
            const UEGHeroDefinition* ElysiaHero = Elysia->HeroVisualDefinition;
            const bool bValidHeroPresentation = ElysiaHero &&
                (!ElysiaHero->bUseSplitConfiguration || ElysiaHero->PresentationDefinition);
            Test->TestTrue(TEXT("Opening Elysia uses her hero definition's skeletal mesh"),
                bValidHeroPresentation && ElysiaMesh && ElysiaMesh == ElysiaHero->GetMesh().Get());
            for (const int32 Slot : {0, 2, 7})
            {
                UMaterialInterface* Expected = ElysiaMesh && ElysiaMesh->GetMaterials().IsValidIndex(Slot)
                    ? ElysiaMesh->GetMaterials()[Slot].MaterialInterface.Get() : nullptr;
                UMaterialInterface* Actual = Elysia->Body ? Elysia->Body->GetMaterial(Slot) : nullptr;
                UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Actual);
                Test->TestTrue(FString::Printf(TEXT("Opening Elysia material slot %d uses the mesh material"), Slot),
                    Expected && Actual && (Actual == Expected || (Dynamic && Dynamic->Parent == Expected)));
            }
            UAnimSingleNodeInstance* IdlePlayer = Elysia->Body ? Elysia->Body->GetSingleNodeInstance() : nullptr;
            UAnimSequence* IdleClip = Cast<UAnimSequence>(OpeningObjectProperty(Presentation, TEXT("ElysiaIdle")));
            Test->TestTrue(TEXT("Elysia uses the opening idle clip on the title screen"),
                IdleClip && IdlePlayer && IdlePlayer->GetCurrentAsset() == IdleClip);
            Test->TestTrue(TEXT("Elysia uses the selected original standby animation"),
                IdleClip && IdleClip->GetPathName() == TEXT("/Game/Assets/Characters/Elysia/Combat20261101/Animations/A_C1_Standby_BoardInPlace.A_C1_Standby_BoardInPlace"));
            Test->TestTrue(TEXT("Elysia opening idle plays at the intended slower rate"),
                IdlePlayer && FMath::IsNearlyEqual(IdlePlayer->GetPlayRate(), .4f, .01f));
            UAnimSingleNodeInstance* StandPlayer = Corruption->Body ? Corruption->Body->GetSingleNodeInstance() : nullptr;
            UAnimSequence* StandClip = Cast<UAnimSequence>(OpeningObjectProperty(Presentation, TEXT("CorruptionStand")));
            Test->TestTrue(TEXT("Corruption uses the standing clip on the title screen"),
                StandClip && StandPlayer && StandPlayer->GetCurrentAsset() == StandClip);
            Test->TestTrue(TEXT("Corruption opening idle plays at the intended slower rate"),
                StandPlayer && FMath::IsNearlyEqual(StandPlayer->GetPlayRate(), .4f, .01f));
            Test->TestTrue(TEXT("Corruption uses the selected original boss standby animation"),
                StandClip && StandClip->GetPathName() == TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/Settlement20261109/A_Corruption_Idle.A_Corruption_Idle"));
            Test->TestTrue(TEXT("Default entry enables the opening gate"), Mode->IsPresentationInputBlocked());
            Test->TestFalse(TEXT("Battle preloading is not complete before start"), Mode->IsOpeningPreloadReady());
            Test->TestFalse(TEXT("The match has not begun on the title screen"), Mode->GetMatch()->HasStarted());
            Test->TestEqual(TEXT("No enemy is present before players arrive"), EnemyCount(Mode), 0);
            if (AEGCardManager* Cards = Mode->GetCards()) Test->TestEqual(TEXT("No opening cards before arrival"), Cards->GetHand().Num(), 0);
            if (Player->GetMainHUD()) Test->TestTrue(TEXT("HUD hidden on title screen"), Player->GetMainHUD()->GetVisibility() == ESlateVisibility::Collapsed);
            if (Mode->GetDemoPanel()) Test->TestTrue(TEXT("Independent phase and reward panel hidden on title screen"), Mode->GetDemoPanel()->GetVisibility() == ESlateVisibility::Collapsed);
            if (!bOriginalSceneLogged)
            {
                LogOriginalBoardGeometry(Test, World, Presentation);
                bOriginalSceneLogged = true;
            }
            if (!Capture(World, TEXT("Menu"), Player, Presentation)) return false;
            if (bCaptureFailed) return true;
            if (AActor* View = Player->GetViewTarget()) DepartureCameraStart = View->GetActorLocation();
            PreviousCorruptionLocation=Corruption->GetActorLocation();PreviousCorruptionTime=World->GetTimeSeconds();
            PreviousCorruptionScale=static_cast<float>(Corruption->Body->GetComponentScale().GetAbsMax());
            // This is a synthetic UMG button broadcast; it is not a physical mouse test.
            Start->OnClicked.Broadcast();
            Test->TestTrue(TEXT("Start click enters actual loading state"), Presentation->IsPreparingAfterStart());
            Test->TestFalse(TEXT("A repeated start request is rejected"), Mode->RequestOpeningStart());
            Test->TestFalse(TEXT("Start click alone does not grant cards or create enemies"), Mode->GetMatch()->HasStarted());
            Step = 1;
            return false;
        }
        if (Step == 1)
        {
            if (!bCaptureArmed && Presentation->IsDeparturePlaying())
            {
                // A cached run may finish loading within a frame; it is not a
                // failure and we must not fabricate a longer loading screen.
                Test->AddInfo(TEXT("Battle dependencies completed before a loading screenshot could be requested."));
                Step = 2;
                return false;
            }
            if (!bCaptureArmed)
            {
                if (!Widget || !Presentation->IsPreparingAfterStart()) return false;
                UTextBlock* Status = Cast<UTextBlock>(Widget->GetWidgetFromName(TEXT("LoadingStatus")));
                if (!Status || !Status->IsVisible() || !Status->GetText().ToString().StartsWith(TEXT("正在加载"))) return false;
                Test->TestFalse(TEXT("Loading text does not invent a percentage"), Status->GetText().ToString().Contains(TEXT("%")));
                Test->TestTrue(TEXT("Loading overlay remains visible"), Widget->GetRenderOpacity() > .99f);
                Test->TestTrue(TEXT("Input remains blocked while loading"), Mode->IsPresentationInputBlocked());
            }
            if (!Capture(World, TEXT("Loading"), Player, Presentation)) return false;
            if (bCaptureFailed) return true;
            Step = 2;
            return false;
        }
        if (Step == 2)
        {
            if (!bCaptureArmed)
            {
                if (Mode->GetMatch()->HasStarted())
                {
                    Test->AddError(TEXT("The complete departure elapsed before its screenshot was requested."));
                    return true;
                }
                if (!Presentation->IsDeparturePlaying()) return false;
                if (DepartureObservedAt < 0.) DepartureObservedAt = World->GetTimeSeconds();
                // Camera departure now overlaps battle preloading. Readiness
                // is required at the final commit, not before camera motion.
                Test->TestFalse(TEXT("Camera motion does not start gameplay before arrival"), Mode->GetMatch()->HasStarted());
                Test->TestTrue(TEXT("Input stays blocked while masters are moving"), Mode->IsPresentationInputBlocked());
                if (Player->GetViewTarget() && FVector::Dist(Player->GetViewTarget()->GetActorLocation(), DepartureCameraStart) < 20.f) return false;
            }
            if (!Capture(World, TEXT("Transit"), Player, Presentation)) return false;
            if (bCaptureFailed) return true;
            // Detailed screenshots deliberately opt in: 14 synchronous 1080p
            // captures perturb live frame times and cannot measure smoothness.
            Step = FParse::Param(FCommandLine::Get(),TEXT("EGOpeningCaptureOrbit")) ? 20 : 3;
            return false;
        }
        if (Step == 20)
        {
            if (!Presentation->IsDeparturePlaying() && !bCaptureArmed)
            {
                Test->AddError(TEXT("Camera completed before the 14 one-second orbit samples were captured."));
                return true;
            }
            if (World->GetTimeSeconds() - DepartureObservedAt < NextTransitSecond) return false;
            const FString Name=FString::Printf(TEXT("Orbit_%02d"),NextTransitSecond);
            if (!Capture(World, *Name, Player, Presentation)) return false;
            if (bCaptureFailed) return true;
            if (++NextTransitSecond > 14) Step = 3;
            return false;
        }
        if (Step == 3)
        {
            if (!Mode->GetMatch()->HasStarted()) return false;
            if (!bCaptureRequested && !Mode->IsPresentationInputBlocked())
            {
                Test->AddError(TEXT("The enemy entrance completed before its screenshot was requested."));
                return true;
            }
            Test->TestNotNull(TEXT("Card manager after player arrival"), Mode->GetCards());
            if (Mode->GetCards()) Test->TestEqual(TEXT("Exactly two opening cards"), Mode->GetCards()->GetHand().Num(), 2);
            Test->TestTrue(TEXT("First-stage enemies were created"), EnemyCount(Mode) > 0);
            auto* Masters = Mode->FindComponentByClass<UEGBoardMasterPresentation>();
            AEGArtPiece* Friendly = MasterProperty(Masters, TEXT("Friendly"));
            AEGArtPiece* Enemy = MasterProperty(Masters, TEXT("Enemy"));
            if (Test->TestNotNull(TEXT("Friendly master after arrival"), Friendly) &&
                Test->TestNotNull(TEXT("Enemy master after arrival"), Enemy) &&
                Test->TestNotNull(TEXT("Board after arrival"), Mode->GetBoard()))
            {
                Test->TestFalse(TEXT("Friendly master is visible after arrival"),Friendly->IsHidden());
                Test->TestFalse(TEXT("Enemy master is visible after arrival"),Enemy->IsHidden());
                FVector FriendlyAnchor, EnemyAnchor;
                if (Test->TestTrue(TEXT("Master anchors can be resolved"), UEGBoardMasterPresentation::GetMasterAnchors(Mode->GetBoard(), FriendlyAnchor, EnemyAnchor)))
                {
                    const FVector GroundOffset(0.f, 0.f, Mode->GetBoard()->UnitCenterHeight);
                    Test->TestTrue(TEXT("Friendly master is at the player position"), Friendly->GetActorLocation().Equals(FriendlyAnchor - GroundOffset, 1.f));
                    Test->TestTrue(TEXT("Enemy master is at the opponent position"), Enemy->GetActorLocation().Equals(EnemyAnchor - GroundOffset, 1.f));
                }
            }
            if (!Capture(World, TEXT("Entrance"), Player, Presentation)) return false;
            if (bCaptureFailed) return true;
            Step = 4;
            return false;
        }
        if (Step == 4)
        {
            if (Mode->IsPresentationInputBlocked()) return false;
            if (OpeningWidget.IsValid() && OpeningWidget->IsInViewport()) return false;
            if (!bFacingChecked)
            {
                bFacingChecked = true;
                Test->TestFalse(TEXT("Arrival facing bones exist on both visible performers"), bFacingBoneMissing);
                Test->TestTrue(TEXT("Arrival facing has enough consecutive animation samples"), FacingSamples >= 4 && FacingPairs >= 3);
                Test->TestTrue(TEXT("Arrival facing covers the opening-to-board master handoff"), bFacingHandoffObserved);
                Test->TestTrue(FString::Printf(TEXT("Visible hip heading has no abrupt arrival turn (worst %.1f degrees, allowance %.1f)"), WorstFacingJump, WorstFacingAllowance),
                    WorstFacingJump <= WorstFacingAllowance);
            }
            auto* Settings = Player->GetSceneSettings();
            if (!Test->TestNotNull(TEXT("Board scene settings"), Settings)) return true;
            Test->TestTrue(TEXT("Orbit was sampled throughout the camera journey"),CameraSamples>=60);
            Test->TestTrue(TEXT("Camera rises before the final descent"),bOrbitAscending);
            Test->TestTrue(TEXT("Camera stays outside the board footprint"),bOrbitOutsideBoard);
            Test->TestTrue(TEXT("Camera faces the board center after the opening blend"),bOrbitLooksAtBoard);
            Test->TestTrue(TEXT("Camera reaches above the final gameplay position"),IsValid(Settings->FixedCamera.Get()) && PeakCameraHeight>=Settings->FixedCamera->GetActorLocation().Z+500.f);
            Test->TestTrue(TEXT("Both corruption performers hand off fully outside the camera"),Presentation->HasSafeCorruptionHandoffForTests() && bCorruptionHandoffObserved && bCorruptionNoOverlap);
            const auto& Evidence=Presentation->GetCorruptionHandoffEvidenceForTests();
            const auto OutsideSnapshot=[&](const FVector& Center,double Radius)
            {
                const FVector Local=Evidence.CameraTransform.InverseTransformPosition(Center);
                const double Horizontal=FMath::Tan(FMath::DegreesToRadians(Evidence.FieldOfView*.5));
                const double Vertical=Horizontal/FMath::Max(1.,static_cast<double>(Evidence.AspectRatio));
                return Local.X+Radius<0. ||
                    (FMath::Abs(Local.Y)-Local.X*Horizontal)/FMath::Sqrt(1.+Horizontal*Horizontal)>Radius ||
                    (FMath::Abs(Local.Z)-Local.X*Vertical)/FMath::Sqrt(1.+Vertical*Vertical)>Radius;
            };
            Test->TestTrue(TEXT("Handoff source is outside the actual camera frustum"),Evidence.SourceRadius>0. && OutsideSnapshot(Evidence.SourceCenter,Evidence.SourceRadius));
            Test->TestTrue(TEXT("Handoff destination is outside the actual camera frustum"),Evidence.DestinationRadius>0. && OutsideSnapshot(Evidence.DestinationCenter,Evidence.DestinationRadius));
            Test->TestTrue(TEXT("Active corruption performer remains visible"),bCorruptionStayedVisible && CorruptionWalkSamples>=60);
            Test->TestTrue(TEXT("Board-size corruption walks continuously after the offscreen handoff"),bCorruptionMotionContinuous && CorruptionTravel>1500.f);
            Test->TestTrue(TEXT("Board-size corruption remains at fixed scale"),bCorruptionScaleContinuous);
            Test->TestTrue(TEXT("Corruption stays in the stair opening and inner platform"),bCorruptionRouteLegal);
            Test->TestTrue(TEXT("Corruption's actual animation instance played a walk montage"),bCorruptionWalkAnimationObserved);
            Test->TestTrue(TEXT("Fixed board camera is restored"), Player->GetViewTarget() == Settings->FixedCamera.Get());
            Test->TestTrue(TEXT("Preparation is available after enemy entrance"), Mode->CanPrepare());
            Test->TestTrue(TEXT("HUD has been restored"), Player->GetMainHUD() && Player->GetMainHUD()->GetVisibility() != ESlateVisibility::Collapsed);
            Test->TestTrue(TEXT("Independent phase and reward panel restored"), Mode->GetDemoPanel() && Mode->GetDemoPanel()->GetVisibility() != ESlateVisibility::Collapsed);
            Test->TestEqual(TEXT("Cards remain issued exactly once"), Mode->GetCards() ? Mode->GetCards()->GetHand().Num() : -1, 2);
            if (!Capture(World, TEXT("Ready"), Player, Presentation)) return false;
            if (bCaptureFailed) return true;
            const auto Hand = Mode->GetCards()->GetHand();
            if (!Test->TestTrue(TEXT("Deploy opening card for controlled stage progression"),
                Hand.Num() > 0 && Mode->GetCards()->TryDeployHandCard(Hand[0].InstanceId,FIntPoint(3,1)))) return true;
            if (!Test->TestTrue(TEXT("Enter first combat through normal gate"),Mode->EnterCombat())) return true;
            Step=5;
            return false;
        }
        if (Step==5)
        {
            if(!Mode->BattleRules || !Mode->BattleRules->IsCombatRunning())return false;
            Test->TestTrue(TEXT("Controlled victory starts real settlement"),Mode->ResolveCombat(true));
            Step=6;return false;
        }
        if(Step==6)
        {
            if(Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward)
            {
                const auto Offer=Mode->GetStigmataOffer();
                if(Offer.Num()>0)Test->TestTrue(TEXT("Choose first-stage reward"),Mode->ChooseStigmata(Offer[0],Mode->GetStigmataRevision()));
            }
            if(Mode->GetMatch()->GetStageNumber()!=2)return false;
            if(StageTwoObservedAt<0.)StageTwoObservedAt=World->GetTimeSeconds();
            int32 Enemies=0,Hidden=0,Portals=0;
            for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit)&&Unit->IsAlive_Implementation()&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
            {++Enemies;if(Unit->IsHidden())++Hidden;}
            for(TActorIterator<AEGTeleportVisual> It(World);It;++It)++Portals;
            bStageTwoHiddenObserved|=Enemies>0&&Hidden==Enemies;
            bStageTwoPortalsObserved|=Enemies>0&&Portals==Enemies;
            if(World->GetTimeSeconds()-StageTwoObservedAt<.95)return false;
            if(!Capture(World,TEXT("StageTwoArrival"),Player,Presentation))return false;
            if(bCaptureFailed)return true;
            Step=7;return false;
        }
        if(Step==7)
        {
            if(Mode->IsPresentationInputBlocked())return false;
            Test->TestTrue(TEXT("Stage two hides every monster before reveal"),bStageTwoHiddenObserved);
            Test->TestTrue(TEXT("Stage two creates one portal per monster"),bStageTwoPortalsObserved);
            Test->TestTrue(TEXT("Preparation resumes after stage two arrival"),Mode->CanPrepare());
            for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit)&&Unit->IsAlive_Implementation()&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
                Test->TestFalse(TEXT("Stage two monster visible after arrival"),Unit->IsHidden());
            return true;
        }
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGIntegratedOpeningPIETest, "ElyGambit.Integration.Opening.DefaultEntryPIE",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGIntegratedOpeningPIETest::RunTest(const FString&)
{
    if (!GEditor || !GUnrealEd || !FSlateApplication::IsInitialized() || GEditor->PlayWorld)
    {
        AddError(TEXT("Integrated opening test requires an idle editor."));
        return false;
    }
    FString DefaultMap;
    if (!GConfig || !GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"), TEXT("GameDefaultMap"), DefaultMap, GEngineIni) || DefaultMap.IsEmpty())
    {
        AddError(TEXT("No configured default game map."));
        return false;
    }
    TestEqual(TEXT("Default entry uses the existing menu bootstrap"), DefaultMap, FString(TEXT("/Game/Assets/Maps/Gameplay/L_ElyMainMenu")));
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(DefaultMap));
    ADD_LATENT_AUTOMATION_COMMAND(FIntegratedOpeningStart1080PIE());
    ADD_LATENT_AUTOMATION_COMMAND(FIntegratedOpeningPIE(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
