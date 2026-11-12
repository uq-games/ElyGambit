#include "UI/EGVitalsLayoutSubsystem.h"
#include "UI/Widget/EGDemoPanel.h"
#include "Character/EGChessUnit.h"
#include "Placement/EGPlacementVisuals.h"
#include "Data/EGHeroDefinition.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"

void UEGVitalsLayoutSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 AfterActorsHandle=FWorldDelegates::OnWorldPostActorTick.AddUObject(this,&ThisClass::AfterActors);
}
void UEGVitalsLayoutSubsystem::AfterActors(UWorld* World,ELevelTick TickType,float DeltaTime)
{
 if(World==GetWorld()&&TickType!=LEVELTICK_ViewportsOnly&&!World->IsPaused())Tick(DeltaTime);
}

bool UEGVitalsLayoutSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
 const auto* World=Cast<UWorld>(Outer);
 return World&&(World->WorldType==EWorldType::Game||World->WorldType==EWorldType::PIE);
}
TStatId UEGVitalsLayoutSubsystem::GetStatId() const
{
 RETURN_QUICK_DECLARE_CYCLE_STAT(UEGVitalsLayoutSubsystem,STATGROUP_Tickables);
}
void UEGVitalsLayoutSubsystem::Register(UWidgetComponent* Component)
{
 if(!IsValid(Component)||Component->GetWorld()!=GetWorld())return;
 if(auto* Existing=Entries.FindByPredicate([Component](const FEntry& Entry){return Entry.Component.Get()==Component;}))
 {Existing->AuthoredAnchor=Component->GetRelativeLocation();Existing->bHasHeadAnchor=false;
  Existing->PreparationBody.Reset();Existing->PreparationMesh.Reset();Existing->PreparationHead=NAME_None;Existing->NextPreparationSearchTime=0.0;}
 else Entries.Add({Component,Component->GetRelativeLocation()});
}
void UEGVitalsLayoutSubsystem::Deinitialize()
{
 FWorldDelegates::OnWorldPostActorTick.Remove(AfterActorsHandle);AfterActorsHandle.Reset();
 for(auto& Entry:Entries)
  if(auto* Component=Entry.Component.Get())
  {
   Component->SetRelativeLocation(Entry.AuthoredAnchor);
   if(auto* Widget=Component->GetUserWidgetObject())Widget->SetRenderTranslation(FVector2D::ZeroVector);
  }
 Entries.Reset();
 Super::Deinitialize();
}
void UEGVitalsLayoutSubsystem::Tick(float)
{
 Entries.RemoveAll([](const FEntry& Entry){return !Entry.Component.IsValid();});
 for(auto& Entry:Entries)
 {
  auto* Component=Entry.Component.Get();
  auto* Widget=Component?Cast<UEGUnitVitals>(Component->GetUserWidgetObject()):nullptr;
  if(!Component||!Widget)continue;
  // Vitals stay on their owner's head even when other widgets overlap them.
  if(!Widget->GetRenderTransform().Translation.IsNearlyZero())
   Widget->SetRenderTranslation(FVector2D::ZeroVector);
  if(!Component->IsVisible()||!Component->GetOwner()||Component->GetOwner()->IsHidden()||!Widget->IsVisible())
   continue;
  Widget->SynchronizePresentationState();
  const auto* CombatGroup=Widget->GetWidgetFromName(TEXT("CombatGroup"));
  if(Widget->DisplayMode==EEGVitalsMode::Combat&&CombatGroup&&!CombatGroup->IsVisible())
   continue;
  // Prefer the visible body's head over the authored height, which can drift after
  // model adaptation. Weapon, hair and effect bounds do not participate.
  auto* Unit=Cast<AEGChessUnit>(Component->GetOwner());
  const bool bPilot=Unit&&Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::VillV;
  // Preparation may switch between the entrance rig and the idle body. Resolve
  // their actual head rather than a weapon/hair-inflated bounds or a fixed height.
  FVector Anchor=Entry.AuthoredAnchor;
  if(auto* Piece=Cast<AEGDeployedPiece>(Component->GetOwner()))
  {
   auto* Cached=Entry.PreparationBody.Get();
   const bool bUsable=IsValid(Cached)&&Cached->IsRegistered()&&Cached->IsVisible()&&!Cached->bHiddenInGame&&
    Cached->GetOwner()&&!Cached->GetOwner()->IsHidden()&&Cached->GetSkeletalMeshAsset()==Entry.PreparationMesh.Get()&&
    !Entry.PreparationHead.IsNone();
   if(!bUsable)
   {
    // A previously discovered body changed or disappeared: immediately look
    // for its replacement; throttle only repeated unsuccessful discovery.
    if(Cached||Entry.PreparationMesh.IsValid())Entry.NextPreparationSearchTime=0.0;
    Entry.PreparationBody.Reset();Entry.PreparationMesh.Reset();Entry.PreparationHead=NAME_None;
    const double Now=GetWorld()->GetTimeSeconds();
    if(Now>=Entry.NextPreparationSearchTime)
    {
     // Failure can occur for non-humanoid pieces; retry occasionally instead of
     // traversing every frame. A re-registration immediately invalidates cache.
     Entry.NextPreparationSearchTime=Now+1.0;
     TArray<AActor*> Visuals;Piece->GetAttachedActors(Visuals,true,true);Visuals.Insert(Piece,0);
     for(const AActor* Visual:Visuals)
     {
      if(!IsValid(Visual)||Visual->IsHidden())continue;
      TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
      for(auto* Mesh:Meshes)
      {
       if(!IsValid(Mesh)||!Mesh->IsVisible()||Mesh->bHiddenInGame||!Mesh->GetSkeletalMeshAsset()||Mesh==Piece->Outline)continue;
       for(int32 Index=0;Index<Mesh->GetNumBones();++Index)
       {
        const FName Bone=Mesh->GetBoneName(Index);
        if(!Bone.ToString().EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))continue;
        Entry.PreparationBody=Mesh;Entry.PreparationMesh=Mesh->GetSkeletalMeshAsset();Entry.PreparationHead=Bone;
        break;
       }
       if(Entry.PreparationBody.IsValid())break;
      }
      if(Entry.PreparationBody.IsValid())break;
     }
    }
    Cached=Entry.PreparationBody.Get();
   }
   if(IsValid(Cached)&&!Entry.PreparationHead.IsNone()&&Component->GetAttachParent())
   {
    // Head bones sit inside the skull. Reserve world-space room for the crown
    // and a visible gap; the screen-space widget itself has an additional margin.
    const FVector AboveHead=Cached->GetBoneLocation(Entry.PreparationHead)+FVector(0.f,0.f,45.f);
    if(!AboveHead.ContainsNaN())
     Anchor=Component->GetAttachParent()->GetComponentTransform().InverseTransformPosition(AboveHead);
   }
  }
  auto* Body=Unit?Unit->GetVisiblePresentationMesh():nullptr;
  if(Body&&Body->IsVisible()&&!Body->bHiddenInGame&&Body->GetSkeletalMeshAsset())
  {
   static const FName HeadNames[]={TEXT("Bip001 Head"),TEXT("Bip001_Head"),TEXT("Bip001-Head"),TEXT("Bip01 Head"),TEXT("Bip01_Head"),TEXT("Bip01-Head"),TEXT("Head"),TEXT("Bip001Head")};
   for(const FName Name:HeadNames)
    if(Body->GetBoneIndex(Name)!=INDEX_NONE)
    {
     const FVector Head=Body->GetSocketLocation(Name)+FVector(0,0,35.f);
     if(!Head.ContainsNaN()&&Component->GetAttachParent())
     {
      Anchor=Component->GetAttachParent()->GetComponentTransform().InverseTransformPosition(Head);
      Entry.LastHeadAnchor=Anchor;Entry.bHasHeadAnchor=true;
     }
     break;
    }
  }
  // A one-frame visibility handover must not return the pilot HUD to ground height.
  if(bPilot&&Entry.bHasHeadAnchor)Anchor=Entry.LastHeadAnchor;
  Component->SetRelativeLocation(Anchor);
 }
}
