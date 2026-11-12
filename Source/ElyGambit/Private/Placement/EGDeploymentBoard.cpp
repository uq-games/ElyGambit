#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Placement/EGBoardScene.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGMatchManager.h"
#include "EngineUtils.h"

AEGDeploymentBoard::AEGDeploymentBoard()
{
    PrimaryActorTick.bCanEverTick=false;
    Query=CreateDefaultSubobject<UBoxComponent>(TEXT("BoardQuery"));SetRootComponent(Query);
    Query->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Query->SetCollisionResponseToAllChannels(ECR_Ignore);
    Query->SetCollisionResponseToChannel(ECC_GameTraceChannel1,ECR_Block);
    Ghost=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ghost"));Ghost->SetupAttachment(Query);
    Highlight=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Highlight"));Highlight->SetupAttachment(Query);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
    for(UStaticMeshComponent* Mesh:{Ghost.Get(),Highlight.Get()})
    {
        Mesh->SetStaticMesh(Plane.Object);Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetCastShadow(false);Mesh->SetVisibility(false);
        Mesh->SetTranslucentSortPriority(2);
    }
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> EdgeMaterial(TEXT("/Game/Assets/Environments/Board/Materials/M_ChessBoardPerimeter.M_ChessBoardPerimeter"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SurfaceMaterial(TEXT("/Game/Assets/Environments/Board/Materials/M_ElysianBoardMuted.M_ElysianBoardMuted"));
    PerimeterMaterial=EdgeMaterial.Object;BoardSurfaceMaterial=SurfaceMaterial.Object;
    for(int32 I=0;I<6;++I)
    {
        auto* Line=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("BoardBoundary%d"),I));
        Line->SetupAttachment(Query);Line->SetStaticMesh(Plane.Object);
        Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);Line->SetCanEverAffectNavigation(false);
        Line->SetCastShadow(false);Line->SetVisibility(false);
        if(I<4)Perimeter.Add(Line);else FactionBoundaries.Add(Line);
    }
    for(int32 I=0;I<22;++I)
    {
        auto* Line=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("BoardPerimeterDetail%d"),I));
        Line->SetupAttachment(Query);Line->SetStaticMesh(Plane.Object);
        Line->SetCollisionEnabled(ECollisionEnabled::NoCollision);Line->SetCanEverAffectNavigation(false);
        Line->SetCastShadow(false);Line->SetVisibility(false);PerimeterDetails.Add(Line);
    }
    for(int32 I=0;I<2;++I)
    {
        auto* Pad=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("BoardMasterPad%d"),I));
        Pad->SetupAttachment(Query);Pad->SetStaticMesh(Plane.Object);
        Pad->SetCollisionEnabled(ECollisionEnabled::NoCollision);Pad->SetCanEverAffectNavigation(false);
        Pad->SetCastShadow(false);Pad->SetVisibility(false);MasterPads.Add(Pad);
    }
}

FEGBoardPerimeterGeometry AEGDeploymentBoard::BuildPerimeterGeometry(int32 InColumns,int32 InRows,
    float InCellWidth,float InCellDepth,bool bIncreasingColumnIsScreenRight)
{
    FEGBoardPerimeterGeometry Result;
    if(InColumns<1||InRows<1||!FMath::IsFinite(InCellWidth)||!FMath::IsFinite(InCellDepth)||
        InCellWidth<=0.f||InCellDepth<=0.f)return Result;
    const float W=InColumns*InCellWidth,D=InRows*InCellDepth;
    const float S=bIncreasingColumnIsScreenRight?1.f:-1.f;
    const float XP=-S*W*.5f,XE=S*W*.5f,Near=-D*.5f,Far=D*.5f;
    const float Radius=FMath::Min(InCellWidth,InCellDepth)*.4f;
    Result.FriendlyCenter=FVector2D(XP-S*InCellWidth*.5f,Near+InCellDepth*.5f);
    Result.EnemyCenter=FVector2D(XE+S*InCellWidth*.5f,Far-InCellDepth*.5f);
    auto Add=[&](FVector2D A,FVector2D B){Result.Segments.Add({A,B});};
    Result.Segments.Reserve(26);
    // Four straight sides, with diagonal corners reserved for the two arcs.
    Add({XP,Near},{XP,Far-Radius});
    Add({XE,Near+Radius},{XE,Far});
    Add({XP,Near},{XE-S*Radius,Near});
    Add({XP+S*Radius,Far},{XE,Far});
    constexpr int32 ArcSteps=8;
    for(int32 Arc=0;Arc<2;++Arc)
    {
        auto Point=[&](float T)
        {
            const float Angle=T*PI*.5f;
            return Arc==0
                ? FVector2D(XP+S*Radius*(1.f-FMath::Cos(Angle)),Far-Radius+Radius*FMath::Sin(Angle))
                : FVector2D(XE-S*Radius*(1.f-FMath::Cos(Angle)),Near+Radius-Radius*FMath::Sin(Angle));
        };
        for(int32 I=0;I<ArcSteps;++I)Add(Point(static_cast<float>(I)/ArcSteps),Point(static_cast<float>(I+1)/ArcSteps));
    }
    // The outer three sides of each station meet the square corner and its
    // existing side; the fourth side is shared with the playable perimeter.
    const float PlayerOuter=XP-S*InCellWidth,EnemyOuter=XE+S*InCellWidth;
    Add({XP,Near},{PlayerOuter,Near});
    Add({PlayerOuter,Near},{PlayerOuter,Near+InCellDepth});
    Add({PlayerOuter,Near+InCellDepth},{XP,Near+InCellDepth});
    Add({XE,Far},{EnemyOuter,Far});
    Add({EnemyOuter,Far},{EnemyOuter,Far-InCellDepth});
    Add({EnemyOuter,Far-InCellDepth},{XE,Far-InCellDepth});
    return Result;
}

FEGBoardPerimeterGeometry AEGDeploymentBoard::GetPerimeterGeometry() const
{
    bool bIncreasingColumnIsScreenRight=true;
    if(GetWorld())for(TActorIterator<AEGBoardSceneSettings> It(GetWorld());It;++It)
        if(It->Board==this&&IsValid(It->FixedCamera))
        {
            const FVector LocalX=GetActorTransform().TransformVectorNoScale(FVector::ForwardVector);
            bIncreasingColumnIsScreenRight=FVector::DotProduct(LocalX,It->FixedCamera->GetActorRightVector())>=0.f;
            break;
        }
    return BuildPerimeterGeometry(Columns,Rows,CellWidth,CellDepth,bIncreasingColumnIsScreenRight);
}

void AEGDeploymentBoard::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    RefreshVisuals();
}
void AEGDeploymentBoard::BeginPlay()
{
    Super::BeginPlay();RefreshVisuals();
    // Apply the private board-only copy to the explicitly wired ground. Never
    // modify the imported source material or unrelated scenery using it.
    if(BoardSurfaceMaterial)for(TActorIterator<AEGBoardSceneSettings> It(GetWorld());It;++It)
        if(It->Board==this&&IsValid(It->FramingGround))
            if(auto* Mesh=It->FramingGround->FindComponentByClass<UStaticMeshComponent>();Mesh&&Mesh->GetStaticMesh()&&
                Mesh->GetStaticMesh()->GetPathName()==TEXT("/Game/Assets/Environments/Board/Board7x7/SM_ElysianBoard_Circular7x7_RestoredDetail.SM_ElysianBoard_Circular7x7_RestoredDetail"))
                Mesh->SetMaterial(0,BoardSurfaceMaterial);
    if(auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();Mode&&Mode->GetMatch())
    {
        Mode->GetMatch()->OnPhaseChanged.AddUniqueDynamic(this,&ThisClass::RefreshPhaseVisuals);
        RefreshPhaseVisuals(Mode->GetMatch()->GetPhase());
    }
}
void AEGDeploymentBoard::EndPlay(const EEndPlayReason::Type Reason)
{
    if(auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;Mode&&Mode->GetMatch())
        Mode->GetMatch()->OnPhaseChanged.RemoveDynamic(this,&ThisClass::RefreshPhaseVisuals);
    Super::EndPlay(Reason);
}
void AEGDeploymentBoard::RefreshPhaseVisuals(EEGMatchPhase Phase)
{
    for(UStaticMeshComponent* Line:FactionBoundaries)if(Line)Line->SetVisibility(NeutralRows>0&&Phase==EEGMatchPhase::Preparation);
}
void AEGDeploymentBoard::RefreshVisuals()
{
    Columns=FMath::Clamp(Columns,1,64);Rows=FMath::Clamp(Rows,1,64);
    CellWidth=FMath::Max(CellWidth,1.f);CellDepth=FMath::Max(CellDepth,1.f);
    Query->SetBoxExtent(FVector(Columns*CellWidth*.5f,Rows*CellDepth*.5f,2.f));
    Ghost->SetRelativeLocation(FVector(0,0,.5));
    Ghost->SetRelativeScale3D(FVector(Columns*CellWidth/100.,Rows*CellDepth/100.,1));
    Highlight->SetRelativeScale3D(FVector(CellWidth/100.,CellDepth/100.,1));
    if(GhostMaterial)
    {
        UMaterialInstanceDynamic* MID=Ghost->CreateDynamicMaterialInstance(0,GhostMaterial);
        MID->SetScalarParameterValue(TEXT("Columns"),Columns);MID->SetScalarParameterValue(TEXT("Rows"),Rows);
    }
    HighlightMID=HighlightMaterial?Highlight->CreateDynamicMaterialInstance(0,HighlightMaterial):nullptr;
    const float W=Columns*CellWidth,D=Rows*CellDepth;
    const float LineWidth=FMath::Clamp(FMath::Min(CellWidth,CellDepth)*.045f,6.f,16.f);
    auto SetLine=[&](UStaticMeshComponent* Line,FVector Position,FVector Size,const FLinearColor& Tint,float Yaw=0.f)
    {
        if(!Line)return;
        Line->SetRelativeLocation(Position);Line->SetRelativeRotation(FRotator(0.f,Yaw,0.f));Line->SetRelativeScale3D(Size/100.f);
        if(PerimeterMaterial)
        {
            auto* MID=Cast<UMaterialInstanceDynamic>(Line->GetMaterial(0));
            if(!MID||MID->Parent!=PerimeterMaterial)MID=Line->CreateDynamicMaterialInstance(0,PerimeterMaterial);
            if(MID)MID->SetVectorParameterValue(TEXT("Tint"),Tint);
        }
        Line->SetVisibility(PerimeterMaterial!=nullptr);
    };
    const FEGBoardPerimeterGeometry Geometry=GetPerimeterGeometry();
    if(MasterPads.Num()==2 && Geometry.Segments.Num()==26)
    {
        const FLinearColor PadTint(.24f,.09f,.18f,1.f);
        const FVector2D Centers[]={Geometry.FriendlyCenter,Geometry.EnemyCenter};
        for(int32 Index=0;Index<2;++Index)
            SetLine(MasterPads[Index],FVector(Centers[Index].X,Centers[Index].Y,.5f),
                FVector(CellWidth,CellDepth,100.f),PadTint);
    }
    if(Perimeter.Num()==4 && PerimeterDetails.Num()==22 && Geometry.Segments.Num()==26)
    {
        const FLinearColor Edge(4.f,.015f,1.1f,1.f);
        for(int32 Index=0;Index<Geometry.Segments.Num();++Index)
        {
            const FEGBoardPerimeterSegment& Segment=Geometry.Segments[Index];
            const FVector2D Delta=Segment.End-Segment.Start,Center=(Segment.Start+Segment.End)*.5f;
            UStaticMeshComponent* Line=Index<4?Perimeter[Index].Get():PerimeterDetails[Index-4].Get();
            SetLine(Line,FVector(Center.X,Center.Y,2.f),FVector(Delta.Size()+LineWidth,LineWidth,100.f),Edge,
                FMath::RadiansToDegrees(FMath::Atan2(Delta.Y,Delta.X)));
        }
    }
    if(FactionBoundaries.Num()==2)
    {
        SetLine(FactionBoundaries[0],FVector(0,FMath::Clamp(PlayerRows,1,Rows)*CellDepth-D*.5f,1.5f),FVector(W,LineWidth*.35f,100),FLinearColor(.18f,.25f,.29f));
        SetLine(FactionBoundaries[1],FVector(0,FMath::Clamp(PlayerRows+NeutralRows,1,Rows)*CellDepth-D*.5f,1.5f),FVector(W,LineWidth*.35f,100),FLinearColor(.29f,.18f,.23f));
        for(UStaticMeshComponent* Line:FactionBoundaries)Line->SetVisibility(false);
    }
    SetBoardVisualsVisible(false);
}

bool AEGDeploymentBoard::IsCellValid(FIntPoint C) const
{return C.X>=0 && C.Y>=0 && C.X<Columns && C.Y<Rows;}
bool AEGDeploymentBoard::IsPlayerCell(FIntPoint C) const
{return IsCellValid(C)&&C.Y<FMath::Clamp(PlayerRows,1,Rows);}
bool AEGDeploymentBoard::IsEnemyCell(FIntPoint C) const
{return IsCellValid(C)&&C.Y>=FMath::Clamp(PlayerRows,1,Rows)+FMath::Clamp(NeutralRows,0,Rows);}

bool AEGDeploymentBoard::TryWorldToCell(FVector P,FIntPoint& C) const
{
    C=FIntPoint(INDEX_NONE,INDEX_NONE);
    if(Columns<1||Rows<1||CellWidth<=0||CellDepth<=0||P.ContainsNaN()) return false;
    const FVector L=GetActorTransform().InverseTransformPosition(P);
    const FVector2D Pos(L.X+Columns*CellWidth*.5,L.Y+Rows*CellDepth*.5);
    if(Pos.X<0||Pos.Y<0||Pos.X>=Columns*CellWidth||Pos.Y>=Rows*CellDepth) return false;
    C=FIntPoint(FMath::FloorToInt(Pos.X/CellWidth),FMath::FloorToInt(Pos.Y/CellDepth));
    return IsCellValid(C);
}

FVector AEGDeploymentBoard::CellToWorld(FIntPoint C) const
{return GetActorTransform().TransformPosition(FVector((C.X+.5)*CellWidth-Columns*CellWidth*.5,(C.Y+.5)*CellDepth-Rows*CellDepth*.5,0));}

AActor* AEGDeploymentBoard::GetOccupant(FIntPoint C) const
{
    const auto* Ptr=Occupants.Find(C);
    return Ptr && Ptr->IsValid()?Ptr->Get():nullptr;
}
bool AEGDeploymentBoard::IsCellAvailable(FIntPoint C) const{return IsPlayerCell(C)&&!GetOccupant(C);}
bool AEGDeploymentBoard::MakeFootprintCells(FIntPoint Center,FIntPoint Size,TArray<FIntPoint>& OutCells)
{
    OutCells.Reset();
    if(Size.X<1||Size.Y<1||Size.X>63||Size.Y>63||(Size.X%2)==0||(Size.Y%2)==0)return false;
    const int64 MinX=static_cast<int64>(Center.X)-Size.X/2,MaxX=static_cast<int64>(Center.X)+Size.X/2;
    const int64 MinY=static_cast<int64>(Center.Y)-Size.Y/2,MaxY=static_cast<int64>(Center.Y)+Size.Y/2;
    if(MinX<MIN_int32||MaxX>MAX_int32||MinY<MIN_int32||MaxY>MAX_int32)return false;
    OutCells.Reserve(Size.X*Size.Y);
    for(int64 Y=MinY;Y<=MaxY;++Y)for(int64 X=MinX;X<=MaxX;++X)
        OutCells.Emplace(static_cast<int32>(X),static_cast<int32>(Y));
    return true;
}
bool AEGDeploymentBoard::CanOccupyFootprint(FIntPoint Center,FIntPoint Size,AActor* IgnorePiece) const
{
    TArray<FIntPoint> Cells;
    if(!IsPlayerCell(Center)||!MakeFootprintCells(Center,Size,Cells))return false;
    for(const FIntPoint Cell:Cells)
    {
        if(!IsPlayerCell(Cell))return false;
        if(AActor* Occupant=GetOccupant(Cell);Occupant&&Occupant!=IgnorePiece)return false;
    }
    return true;
}
bool AEGDeploymentBoard::TryOccupyFootprint(FIntPoint Center,FIntPoint Size,AActor* Piece)
{
    if(!IsValid(Piece)||!Piece->IsA<AEGDeployedPiece>()||Piece->GetWorld()!=GetWorld()||!CanOccupyFootprint(Center,Size))return false;
    for(auto It=OccupiedAnchors.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
    const TWeakObjectPtr<AActor> Key(Piece);
    if(OccupiedAnchors.Contains(Key))return false;
    // Refuse inconsistent pre-existing mappings rather than silently stealing them.
    for(const auto& Pair:Occupants)if(Pair.Value.Get()==Piece)return false;
    TArray<FIntPoint> Cells;if(!MakeFootprintCells(Center,Size,Cells))return false;
    for(const FIntPoint Cell:Cells)Occupants.Add(Cell,Piece);
    OccupiedAnchors.Add(Key,Center);return true;
}
bool AEGDeploymentBoard::TryMoveFootprint(AActor* Piece,FIntPoint Previous,FIntPoint Target,FIntPoint Size)
{
    if(!IsValid(Piece)||!Piece->IsA<AEGDeployedPiece>()||Piece->GetWorld()!=GetWorld())return false;
    const TWeakObjectPtr<AActor> Key(Piece);
    const FIntPoint* Anchor=OccupiedAnchors.Find(Key);
    TArray<FIntPoint> OldCells,NewCells;
    if(!Anchor||*Anchor!=Previous||!MakeFootprintCells(Previous,Size,OldCells)||
       !MakeFootprintCells(Target,Size,NewCells)||!CanOccupyFootprint(Target,Size,Piece))return false;
    for(const FIntPoint Cell:OldCells)if(GetOccupant(Cell)!=Piece)return false;
    int32 Registered=0;for(const auto& Pair:Occupants)if(Pair.Value.Get()==Piece)++Registered;
    if(Registered!=OldCells.Num())return false;
    for(const FIntPoint Cell:OldCells)Occupants.Remove(Cell);
    for(const FIntPoint Cell:NewCells)Occupants.Add(Cell,Piece);
    OccupiedAnchors.Add(Key,Target);return true;
}
bool AEGDeploymentBoard::ReleaseFootprint(FIntPoint Center,AActor* Piece)
{
    if(!Piece)return false;
    const TWeakObjectPtr<AActor> Key(Piece);
    const FIntPoint* Anchor=OccupiedAnchors.Find(Key);
    if(!Anchor||*Anchor!=Center)return false;
    bool bRemoved=false;
    for(auto It=Occupants.CreateIterator();It;++It)
        if(It.Value().Get(true)==Piece){It.RemoveCurrent();bRemoved=true;}
    OccupiedAnchors.Remove(Key);return bRemoved;
}
bool AEGDeploymentBoard::TryOccupyCell(FIntPoint C,AActor* Piece){return TryOccupyFootprint(C,FIntPoint(1,1),Piece);}
bool AEGDeploymentBoard::ReleaseCell(FIntPoint C,AActor* Piece){return ReleaseFootprint(C,Piece);}
int32 AEGDeploymentBoard::GetOccupiedCount() const
{
    int32 Count=0;for(const auto& P:Occupants) Count+=P.Value.IsValid()?1:0;return Count;
}
void AEGDeploymentBoard::SetBoardVisualsVisible(bool bVisible)
{
    bVisualsVisible=bVisible;Ghost->SetVisibility(bVisible||bAlwaysShowGrid);
    if(!bVisible) ClearHoveredCell();
}
void AEGDeploymentBoard::SetHoveredCell(FIntPoint C,bool bLegal)
{SetHoveredFootprint(C,FIntPoint(1,1),bLegal);}
void AEGDeploymentBoard::SetHoveredFootprint(FIntPoint C,FIntPoint Size,bool bLegal)
{
    TArray<FIntPoint> Cells;
    if(!IsCellValid(C)||!bVisualsVisible||!MakeFootprintCells(C,Size,Cells)){ClearHoveredCell();return;}
    FVector L=GetActorTransform().InverseTransformPosition(CellToWorld(C));L.Z=.8;
    Highlight->SetRelativeLocation(L);Highlight->SetRelativeScale3D(FVector(CellWidth*Size.X/100.,CellDepth*Size.Y/100.,1));Highlight->SetVisibility(true);
    // PIE duplication / Blueprint component reconstruction can replace the material
    // slot independently of the transient cache. Always drive the displayed MID.
    HighlightMID=Cast<UMaterialInstanceDynamic>(Highlight->GetMaterial(0));
    if(!HighlightMID&&HighlightMaterial)HighlightMID=Highlight->CreateDynamicMaterialInstance(0,HighlightMaterial);
    if(HighlightMID) HighlightMID->SetScalarParameterValue(TEXT("IsLegal"),bLegal?1.f:0.f);
}
void AEGDeploymentBoard::ClearHoveredCell(){Highlight->SetVisibility(false);}
