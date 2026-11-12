#include "Placement/EGPlacementVisuals.h"
#include "UI/EGVitalsLayoutSubsystem.h"
#include "Engine/World.h"
#include "Placement/EGDeploymentBoard.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Engine/StaticMesh.h"
#include "Components/WidgetComponent.h"
#include "UI/Widget/EGDemoPanel.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/GameInstance.h"

AEGDeployedPiece::AEGDeployedPiece()
{
    PickQuery=CreateDefaultSubobject<UBoxComponent>(TEXT("PieceQuery"));PickQuery->SetupAttachment(ArtRoot);
    PickQuery->SetBoxExtent(FVector(48,48,85));PickQuery->SetRelativeLocation(FVector(0,0,85));
    PickQuery->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    PickQuery->SetCollisionResponseToAllChannels(ECR_Ignore);
    PickQuery->SetCollisionResponseToChannel(ECC_GameTraceChannel2,ECR_Block);
    PreparationVitals=CreateDefaultSubobject<UWidgetComponent>(TEXT("PreparationVitals"));
    PreparationVitals->SetupAttachment(GetRootComponent());PreparationVitals->SetWidgetSpace(EWidgetSpace::Screen);
    // Screen-space clearance stays legible even with the steep board camera:
    // the head bone is inside the skull, and some hairstyles extend above it.
    // A higher pivot moves only this preparation widget, never combat vitals.
    PreparationVitals->SetDrawSize(FVector2D(128,72));PreparationVitals->SetPivot(FVector2D(.5,1.45));
    PreparationVitals->SetRelativeLocation(FVector(0,0,225));PreparationVitals->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    PreparationVitals->SetWidgetClass(UEGUnitVitals::StaticClass());
}
void AEGDeployedPiece::AssignPlacement(AEGDeploymentBoard* InBoard,FIntPoint InCell,FGuid InId)
{Board=InBoard;Cell=InCell;InstanceId=InId;}
void AEGDeployedPiece::RefreshPreparationVitals()
{
 if(HeroVisualDefinition&&HeroVisualDefinition->GetVitalsAnchorHeight()>0)
 {
  const float H=HeroVisualDefinition->GetVitalsAnchorHeight();
  // Authored fallback; the shared post-pose layout subsystem follows the
  // currently visible head for every preparation piece when available.
  PreparationVitals->SetRelativeLocation(FVector(0,0,H));
  PickQuery->SetBoxExtent(FVector(HeroVisualDefinition->GetPresentationPickRadius(),HeroVisualDefinition->GetPresentationPickRadius(),H*.5f));
  PickQuery->SetRelativeLocation(FVector(0,0,H*.5f));
 }
 if(!RosterId.IsValid()||!GetGameInstance())return;
 if(auto* Class=UEGUnitVitals::ResolveWidgetClass())
  if(PreparationVitals->GetWidgetClass()!=Class)PreparationVitals->SetWidgetClass(Class);
 PreparationVitals->InitWidget();
 if(auto* Layout=GetWorld()->GetSubsystem<UEGVitalsLayoutSubsystem>())Layout->Register(PreparationVitals);
 if(auto* W=Cast<UEGUnitVitals>(PreparationVitals->GetUserWidgetObject()))W->SetPreparationHero(GetGameInstance()->GetSubsystem<UEGHeroRoster>(),RosterId);
}
void AEGDeployedPiece::SetPreparationVitalsVisible(bool bVisible){PreparationVitals->SetVisibility(bVisible);}
AEGDeploymentBoard* AEGDeployedPiece::GetBoard() const{return Board.Get();}
void AEGDeployedPiece::EndPlay(const EEndPlayReason::Type Reason)
{
    if(Board.IsValid()) Board->ReleaseFootprint(Cell,this);
    Board.Reset();Super::EndPlay(Reason);
}

AEGPlacementPreview::AEGPlacementPreview()
{
    Ring=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlacementRing"));Ring->SetupAttachment(ArtRoot);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
    Ring->SetStaticMesh(Plane.Object);Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Ring->SetCastShadow(false);Ring->SetRelativeLocation(FVector(0,0,1));
    Ring->SetRelativeScale3D(FVector(1.5,1.5,1));Ring->SetTranslucentSortPriority(3);
    // Keep the legacy component for Blueprint compatibility; the board cell is
    // the placement indicator, so the preview must not draw a plane underfoot.
    Ring->SetVisibility(false);Ring->SetHiddenInGame(true);
    SetActorEnableCollision(false);SetActorHiddenInGame(true);
}
void AEGPlacementPreview::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Ring->SetVisibility(false);Ring->SetHiddenInGame(true);
    RingMID=RingMaterial?Ring->CreateDynamicMaterialInstance(0,RingMaterial):nullptr;
    HidePreview();
}
void AEGPlacementPreview::ShowAt(FVector Position,bool bLegal)
{
    SetPresentationActive(true);
    Ring->SetVisibility(false);Ring->SetHiddenInGame(true);
    SetActorLocation(Position);SetActorHiddenInGame(false);
    if(RingMID) RingMID->SetVectorParameterValue(TEXT("Tint"),bLegal?FLinearColor(.2f,.85f,.7f,1.f):FLinearColor(1.f,.12f,.18f,1.f));
}
void AEGPlacementPreview::BeginPlay()
{
 SetPresentationActive(true);Super::BeginPlay();HidePreview();
}
void AEGPlacementPreview::SetPresentationActive(bool bActive)
{
 if(bActive==bPresentationActive)return;
 bPresentationActive=bActive;
 if(bActive)
 {
  for(const auto& C:SuspendedComponents)if(C.IsValid())C->SetComponentTickEnabled(true);
  for(const auto& A:SuspendedActors)if(A.IsValid())A->SetActorTickEnabled(true);
  SuspendedComponents.Reset();SuspendedActors.Reset();
 }
 else
 {
  TArray<AActor*> Visuals;GetAttachedActors(Visuals,true,true);Visuals.Add(this);
  for(auto* Visual:Visuals)
  {
   if(Visual->IsActorTickEnabled()){SuspendedActors.Add(Visual);Visual->SetActorTickEnabled(false);}
   TInlineComponentArray<UActorComponent*> Components(Visual);
   for(auto* C:Components)if(C->IsComponentTickEnabled()){SuspendedComponents.Add(C);C->SetComponentTickEnabled(false);}
  }
 }
}
void AEGPlacementPreview::HidePreview(){SetActorHiddenInGame(true);SetPresentationActive(false);}
