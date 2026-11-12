#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGBoardMasterActionComponent.h"
#include "Actor/EGArtPiece.h"
#include "Actor/EGBoard.h"
#include "Camera/CameraActor.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "EngineUtils.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Presentation/EGBoardModelSizing.h"

namespace
{
FRotator BoardMasterFacing(bool bFriendly,const UEGHeroDefinition* Hero,const FVector& Position,const FVector& Facing)
{
    // The Corruption master's Boss standby uses a different forward axis from
    // the combat definition. Keep this correction local to the board performer.
    return (Facing-Position).Rotation()+Hero->GetPresentationRotation()+FRotator(0.f,bFriendly?0.f:90.f,0.f);
}
}

UEGBoardMasterPresentation::UEGBoardMasterPresentation()
{
    PrimaryComponentTick.bCanEverTick=true;
    PrimaryComponentTick.bStartWithTickEnabled=true;
}

bool UEGBoardMasterPresentation::GetMasterAnchors(const AEGBoard* Board, FVector& OutFriendly, FVector& OutEnemy)
{
    if (!IsValid(Board) || !Board->GetWorld()) return false;
    const AEGDeploymentBoard* Grid = Board->LayoutSource;
    const int32 Columns = IsValid(Grid) ? Grid->Columns : Board->Dimensions.X;
    const int32 Rows = IsValid(Grid) ? Grid->Rows : Board->Dimensions.Y;
    if (Columns < 1 || Rows < 1) return false;

    if (IsValid(Grid))
    {
        const FEGBoardPerimeterGeometry Geometry=Grid->GetPerimeterGeometry();
        if (Geometry.Segments.IsEmpty()) return false;
        const FTransform Transform=Grid->GetActorTransform();
        OutFriendly=Transform.TransformPosition(FVector(Geometry.FriendlyCenter.X,Geometry.FriendlyCenter.Y,0.f))+FVector(0,0,Board->UnitCenterHeight);
        OutEnemy=Transform.TransformPosition(FVector(Geometry.EnemyCenter.X,Geometry.EnemyCenter.Y,0.f))+FVector(0,0,Board->UnitCenterHeight);
        return true;
    }
    // Select the outside column using the actual level camera. Row -1 is the
    // player's side, while row Rows is the opponent's side.
    bool bIncreasingColumnIsScreenRight = true;
    for (TActorIterator<AEGBoardSceneSettings> It(Board->GetWorld()); It; ++It)
    {
        const AEGBoardSceneSettings* Scene = *It;
        if (Scene->FixedCamera && (Scene->CombatBoard == Board || (Grid && Scene->Board == Grid)))
        {
            const FVector ColumnDirection = Board->CellToWorld(FIntPoint(1, 0)) - Board->CellToWorld(FIntPoint(0, 0));
            bIncreasingColumnIsScreenRight = FVector::DotProduct(ColumnDirection, Scene->FixedCamera->GetActorRightVector()) >= 0.f;
            break;
        }
    }
    const int32 FriendlyColumn=bIncreasingColumnIsScreenRight ? -1 : Columns;
    const int32 EnemyColumn=bIncreasingColumnIsScreenRight ? Columns : -1;
    // Stay outside the side edge, while moving away from the bottom hand row.
    OutFriendly = FMath::Lerp(Board->CellToWorld({FriendlyColumn,-1}),Board->CellToWorld({FriendlyColumn,0}),.9f);
    OutEnemy = FMath::Lerp(Board->CellToWorld({EnemyColumn,Rows}),Board->CellToWorld({EnemyColumn,Rows-1}),.9f);
    // The legacy board returns ground-height cells, unlike LayoutSource.
    if (!IsValid(Grid))
    {
        OutFriendly.Z+=Board->UnitCenterHeight;
        OutEnemy.Z+=Board->UnitCenterHeight;
    }
    return true;
}

float UEGBoardMasterPresentation::GetDisplayScale(const UEGHeroDefinition* Hero,const UObject* WorldContext)
{
    return Hero ? 1.5f*FEGBoardModelSizing::Resolve(Hero,WorldContext) : 1.f;
}

void UEGBoardMasterPresentation::UpdateDisplayScale(AEGArtPiece* Body,float Reveal,float& BaseScale) const
{
    if (!IsValid(Body) || !Body->HeroVisualDefinition) return;
    BaseScale=GetDisplayScale(Body->HeroVisualDefinition,Body);
    Body->SetMasterModelScale(BaseScale*FMath::Lerp(.85f,1.f,Reveal));
}

void UEGBoardMasterPresentation::Install(AEGDemoGameMode* Mode)
{
    if (!Mode || !Mode->RunDefinition || !Mode->RunDefinition->bSevenStageRules || !Mode->GetBoard() ||
        Mode->FindComponentByClass<UEGBoardMasterPresentation>()) return;
    auto* Master=NewObject<UEGBoardMasterPresentation>(Mode);
    Mode->AddInstanceComponent(Master);
    Master->RegisterComponent();
    Master->Refresh();
    Mode->GetCards()->OnBoardChanged.AddUniqueDynamic(Master,&ThisClass::Refresh);
    Mode->GetMatch()->OnPhaseChanged.AddUniqueDynamic(Master,&ThisClass::PhaseChanged);
}

AEGArtPiece* UEGBoardMasterPresentation::FindOrCreateMaster(UWorld* World,bool bFriendly,UEGHeroDefinition* Hero,const FVector& Position,const FVector& Facing,float CenterHeight)
{
 if(!World||World->bIsTearingDown||!Hero)return nullptr;
 const FName Tag=bFriendly?TEXT("EGFriendlyBoardMaster"):TEXT("EGEnemyBoardMaster");
 for(TActorIterator<AEGArtPiece> It(World);It;++It)if(It->ActorHasTag(Tag)&&!It->IsActorBeingDestroyed())return *It;
 const FTransform Transform(BoardMasterFacing(bFriendly,Hero,Position,Facing),Position-FVector(0,0,CenterHeight));
 auto* Body=World->SpawnActorDeferred<AEGArtPiece>(AEGArtPiece::StaticClass(),Transform,World->GetAuthGameMode(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!Body)return nullptr;
 Body->Tags.Add(Tag);Body->HeroVisualDefinition=Hero;Body->SetActorEnableCollision(false);Body->SetActorHiddenInGame(true);
 Body->FinishSpawning(Transform);Body->SetMasterModelScale(GetDisplayScale(Hero,Body));
 if(!UEGBoardMasterActionComponent::Install(Body,bFriendly)){Body->Destroy();return nullptr;}
 return Body;
}
AEGArtPiece* UEGBoardMasterPresentation::CreateBody(UEGHeroDefinition* Hero,const FVector& Position,const FVector& Facing,bool bFriendly)
{
    auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
    if (!Mode || !Hero || !Mode->GetBoard() || !GetWorld() || GetWorld()->bIsTearingDown) return nullptr;
    return FindOrCreateMaster(GetWorld(),bFriendly,Hero,Position,Facing,Mode->GetBoard()->UnitCenterHeight);
}

void UEGBoardMasterPresentation::PhaseChanged(EEGMatchPhase) {Refresh();}
void UEGBoardMasterPresentation::SetSettlementPresenter(AActor* Presenter,bool bActive)
{
    SettlementPresenters.RemoveAll([Presenter,bActive](const auto& Entry)
    {return !Entry.IsValid() || (!bActive && Entry.Get()==Presenter);});
    if(bActive && IsValid(Presenter))SettlementPresenters.AddUnique(Presenter);
    Refresh();
}
void UEGBoardMasterPresentation::Refresh()
{
    auto* Mode=Cast<AEGDemoGameMode>(GetOwner());
    if (!Mode || !Mode->GetMatch() || !Mode->GetCards() || !Mode->RunDefinition) return;
    FVector FriendlyPosition,EnemyPosition;
    if (!GetMasterAnchors(Mode->GetBoard(),FriendlyPosition,EnemyPosition)) return;
    const auto Phase=Mode->GetMatch()->GetPhase();
    const int32 Stage=Mode->GetMatch()->GetStageNumber();
    SettlementPresenters.RemoveAll([](const auto& Entry){return !Entry.IsValid();});
    const bool bBoard=Phase==EEGMatchPhase::Preparation || Phase==EEGMatchPhase::Combat || Phase==EEGMatchPhase::Resolution ||
        Phase==EEGMatchPhase::Settlement || Phase==EEGMatchPhase::Finished || !SettlementPresenters.IsEmpty();
    // The board master is an independent presentation, even when the same
    // character (or a different form) is deployed as a piece.
    const bool bFriendly=bBoard;
    const bool bEnemy=bBoard && (Stage!=7 || !SettlementPresenters.IsEmpty());
    if (!IsValid(Friendly) && bFriendly)
    {
        auto* DefaultMaster=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"));
        Friendly=CreateBody(DefaultMaster,FriendlyPosition,EnemyPosition,true);
    }
    if (!IsValid(Enemy) && bEnemy && Mode->RunDefinition->Stages.Num()==7 && !Mode->RunDefinition->Stages[6].Enemies.IsEmpty())
    {
        Enemy=CreateBody(Mode->RunDefinition->Stages[6].Enemies[0].Definition,EnemyPosition,FriendlyPosition,false);
    }
    auto Reposition=[&](AEGArtPiece* Body,const FVector& Position,const FVector& Facing,bool bFriendlyMaster)
    {
        if (!IsValid(Body) || !Body->HeroVisualDefinition) return;
        const FVector Ground=Position-FVector(0,0,Mode->GetBoard()->UnitCenterHeight);
        const FRotator Rotation=BoardMasterFacing(bFriendlyMaster,Body->HeroVisualDefinition,Position,Facing);
        if (!Body->GetActorLocation().Equals(Ground,.01f) || !Body->GetActorRotation().Equals(Rotation,.01f))
        {
            Body->SetActorLocationAndRotation(Ground,Rotation,false,nullptr,ETeleportType::TeleportPhysics);
        }
    };
    Reposition(Friendly,FriendlyPosition,EnemyPosition,true);
    Reposition(Enemy,EnemyPosition,FriendlyPosition,false);
    UpdateDisplayScale(Friendly,FriendlyReveal,FriendlyBaseScale);
    UpdateDisplayScale(Enemy,EnemyReveal,EnemyBaseScale);
    auto Show=[](AEGArtPiece* Body,bool Visible,float& Reveal,float BaseScale)
    {
        if (!IsValid(Body)) return;
        if (Visible && Body->IsHidden())
        {
            Reveal=0.f;
            Body->SetMasterModelScale(.85f*BaseScale);
            Body->SetActorHiddenInGame(false);
        }
        else if (!Visible && !Body->IsHidden())
        {
            // Visibility changes never replace the master or its animation instance.
            Body->SetActorHiddenInGame(true);
            Body->SetMasterModelScale(BaseScale);
            Reveal=1.f;
        }
    };
    Show(Friendly,bFriendly,FriendlyReveal,FriendlyBaseScale);Show(Enemy,bEnemy,EnemyReveal,EnemyBaseScale);
}

void UEGBoardMasterPresentation::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
    auto Advance=[DeltaTime](AEGArtPiece* Body,float& Reveal,float BaseScale)
    {
        if (!IsValid(Body) || Body->IsHidden()) { Reveal=1.f; return; }
        if (Reveal>=1.f) return;
        Reveal=FMath::Min(1.f,Reveal+FMath::Max(0.f,DeltaTime)/.12f);
        Body->SetMasterModelScale(BaseScale*FMath::Lerp(.85f,1.f,Reveal));
    };
    Advance(Friendly,FriendlyReveal,FriendlyBaseScale);Advance(Enemy,EnemyReveal,EnemyBaseScale);
}

void UEGBoardMasterPresentation::EndPlay(const EEndPlayReason::Type Reason)
{
    if (auto* Mode=Cast<AEGDemoGameMode>(GetOwner()))
    {
        if (IsValid(Mode->GetCards())) Mode->GetCards()->OnBoardChanged.RemoveDynamic(this,&ThisClass::Refresh);
        if (IsValid(Mode->GetMatch())) Mode->GetMatch()->OnPhaseChanged.RemoveDynamic(this,&ThisClass::PhaseChanged);
    }
    if (IsValid(Friendly)) Friendly->Destroy();
    if (IsValid(Enemy)) Enemy->Destroy();
    Friendly=nullptr;
    Enemy=nullptr;
    Super::EndPlay(Reason);
}
