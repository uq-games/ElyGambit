#include "Presentation/EGElysiaHerrscherPresentationComponent.h"

#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Presentation/EGElysiaStarSeaVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "Data/EGHeroDefinition.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Presentation/EGSkillNotify.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "FileMediaSource.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "UI/Widget/EGElysiaEntryMovieWidget.h"
#include "Presentation/EGHumanCameraShake.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
const FName MirrorSource(TEXT("ElysiaHerrscher.CrystalMirror"));
bool HasOriginVisual(EEGElysiaHerrscherPresentationPhase Phase)
{
 return Phase==EEGElysiaHerrscherPresentationPhase::Origin
  ||Phase==EEGElysiaHerrscherPresentationPhase::FinisherPending
  ||Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin
  ||Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased;
}
}

UEGElysiaHerrscherPresentationComponent::UEGElysiaHerrscherPresentationComponent()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.bTickEvenWhenPaused=true;
 PrimaryComponentTick.TickInterval=.1f;
 SetIsReplicatedByDefault(false);
}

void UEGElysiaHerrscherPresentationComponent::BeginPlay()
{
 Super::BeginPlay();
 if(GetNetMode()==NM_DedicatedServer){SetComponentTickEnabled(false);return;}
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->OnDied.AddUniqueDynamic(this,&ThisClass::HandleOwnerDied);
 RefreshBinding();
}

void UEGElysiaHerrscherPresentationComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(DeltaTime,TickType,TickFunction);
 RefreshBinding();
 UpdateEntryMovie();
 UpdateFeedbackShake();
 UpdateBoardFinisher();
 // Some project Niagara candidates are finite bursts. While the combat-owned
 // phase/shield is still active, restart only completed cosmetic systems.
 // This clock never starts, extends or ends the underlying gameplay state.
 if(auto* Behavior=BoundBehavior.Get();Behavior&&Behavior->GetPresentationRoundId()==BoundRound)
 {
  ApplySnapshot(Behavior->GetPresentationPhase());
  HandleStatusChanged();
  auto* Skill=BoundSkill.Get();
  if(Skill&&Skill->IsHumanBowChargePresentationActive())
  {
   if(!IsValid(BowCharge))BowCharge=SpawnEffect(BowChargeSystem,BowChargeTransform,false);
   else if(!BowCharge->IsActive())BowCharge->Activate(true);
  }
  else {DestroyEffect(BowCharge);BowCharge=nullptr;}
  const auto PendingPhase=static_cast<EEGElysiaHerrscherPresentationPhase>(PendingBurstPhase);
  if(PendingBurstAction.IsValid()&&Behavior->GetPresentationActionId()==PendingBurstAction&&Behavior->GetPresentationPhase()==PendingPhase)
  {
   if(SpawnPhaseBurst(PendingPhase))PendingBurstAction.Invalidate();
  }
  else PendingBurstAction.Invalidate();
 }
 RefreshEffectAttachments();
 Bursts.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Component){return !Component.IsValid();});
 BoardBursts.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Component){return !Component.IsValid();});
 ArrowImpacts.RemoveAll([](const TWeakObjectPtr<UNiagaraComponent>& Component){return !Component.IsValid();});
 SourceArrowImpacts.RemoveAll([](const TWeakObjectPtr<AEGSourceEffect>& Effect){return !Effect.IsValid();});
}

void UEGElysiaHerrscherPresentationComponent::RefreshBinding()
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;
 auto* Behavior=Skill?Cast<UEGElysiaHerrscherSkillBehavior>(Skill->GetBehavior()):nullptr;
 const FGuid Round=Behavior?Behavior->GetPresentationRoundId():FGuid();
 if(BoundSkill.Get()==Skill&&BoundBehavior.Get()==Behavior&&BoundRound==Round)return;
 Unbind();
 if(!Skill||!Behavior||!Round.IsValid()||Behavior->GetPresentationPhase()==EEGElysiaHerrscherPresentationPhase::BattleEnd
  ||!IEGCombatant::Execute_IsAlive(Unit)||GetNetMode()==NM_DedicatedServer)return;
 BoundSkill=Skill;BoundBehavior=Behavior;BoundRound=Round;
 Behavior->OnPresentationPhaseChanged.AddUObject(this,&ThisClass::HandlePhase);
 Skill->OnStatusChanged.AddUniqueDynamic(this,&ThisClass::HandleStatusChanged);
 PreloadOriginImpacts();
 PreloadEntryMovie();
 // A behavior may already be configured before this component's first tick.
 // Restore persistent state only: never replay a missed one-shot on rebind.
 ApplySnapshot(Behavior->GetPresentationPhase());
 HandleStatusChanged();
}

void UEGElysiaHerrscherPresentationComponent::Unbind()
{
 if(auto* Behavior=BoundBehavior.Get())Behavior->OnPresentationPhaseChanged.RemoveAll(this);
 if(auto* Skill=BoundSkill.Get())Skill->OnStatusChanged.RemoveDynamic(this,&ThisClass::HandleStatusChanged);
 if(OriginImpactPreload.IsValid()){OriginImpactPreload->CancelHandle();OriginImpactPreload.Reset();}
 BoundBehavior.Reset();BoundSkill.Reset();BoundRound.Invalidate();LastOriginCueAction.Invalidate();ClearEffects();
}

void UEGElysiaHerrscherPresentationComponent::PreloadOriginImpacts()
{
 TArray<FSoftObjectPath> Paths;
 const auto Add=[&Paths](const FSoftObjectPath& Path){if(Path.IsValid())Paths.AddUnique(Path);};
 for(const auto& Source:SourceOriginAttackImpacts)if(IsValid(Source))
 {
  Add(Source->SourceDistortionComposite.ToSoftObjectPath());
  for(const auto& Track:Source->Tracks)
  {Add(Track.Mesh.ToSoftObjectPath());Add(Track.Material.ToSoftObjectPath());Add(Track.NativePassDefinition.ToSoftObjectPath());}
 }
 if(!Paths.IsEmpty())OriginImpactPreload=UAssetManager::GetStreamableManager().RequestAsyncLoad(MoveTemp(Paths));
}

void UEGElysiaHerrscherPresentationComponent::HandlePhase(EEGElysiaHerrscherPresentationPhase Phase,FGuid Round,FGuid Action)
{
 auto* Behavior=BoundBehavior.Get();
 if(!Behavior||!Round.IsValid()||Round!=BoundRound||Behavior->GetPresentationRoundId()!=Round
  ||Behavior->GetPresentationPhase()!=Phase)return;
 if(Phase==EEGElysiaHerrscherPresentationPhase::BattleEnd){Unbind();return;}
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(!Unit||!IEGCombatant::Execute_IsAlive(Unit)){ClearEffects();return;}
 if(Phase==EEGElysiaHerrscherPresentationPhase::Entering&&Action.IsValid()&&Action==Behavior->GetPresentationActionId())
  BeginEntryMovie(Round,Action);
 else if(bEntryMoviePending&&(Action!=EntryMovieAction||
  (Phase!=EEGElysiaHerrscherPresentationPhase::Entering&&Phase!=EEGElysiaHerrscherPresentationPhase::EntryReleased)))
  StopEntryMovie();
 if(Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin&&Action.IsValid()&&Action==Behavior->GetPresentationActionId())
  BeginBoardFinisher(Action);
 else if(Phase!=EEGElysiaHerrscherPresentationPhase::FinisherReleased||Action!=BoardFinisherAction)
  EndBoardFinisher();
 if(Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin
  ||Phase==EEGElysiaHerrscherPresentationPhase::ReturnBow)ClearBursts();
 if(Phase==EEGElysiaHerrscherPresentationPhase::ReturnBow)OriginContactMasks.Reset();
 if((Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::Origin||
     Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin||Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased)
  &&Action.IsValid()&&Action==Behavior->GetPresentationActionId())
 {
  if(!SpawnPhaseBurst(Phase)){PendingBurstAction=Action;PendingBurstPhase=static_cast<int32>(Phase);}
 }
 ApplySnapshot(Phase);
 HandleStatusChanged();
 if(Phase==EEGElysiaHerrscherPresentationPhase::Origin)PlayFeedbackShake(.45f);
 else if(Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased)PlayFeedbackShake(.9f);
 else if(Phase==EEGElysiaHerrscherPresentationPhase::ReturnBow)StopFeedbackShake();
}

void UEGElysiaHerrscherPresentationComponent::PlayFeedbackShake(float Scale)
{
 StopFeedbackShake();
 if(!GetWorld()||GetNetMode()==NM_DedicatedServer)return;
 auto* PC=GetWorld()->GetFirstPlayerController();
 if(!PC||!PC->IsLocalController()||!PC->PlayerCameraManager)return;
 FeedbackCamera=PC->PlayerCameraManager;
 auto* Shake=Cast<UEGHumanCameraShake>(FeedbackCamera->StartCameraShake(UEGHumanCameraShake::StaticClass(),Scale));
 if(!Shake)return;
 FeedbackShake=Shake;FeedbackLease=FGuid::NewGuid();Shake->SetLease(FeedbackLease);
 FeedbackStartedAt=GetWorld()->GetTimeSeconds();RefreshTickRate();
}
void UEGElysiaHerrscherPresentationComponent::StopFeedbackShake()
{
 if(FeedbackCamera.IsValid()&&FeedbackShake.IsValid()&&FeedbackShake->HasLease(FeedbackLease))
  FeedbackCamera->StopCameraShake(FeedbackShake.Get(),true);
 FeedbackShake.Reset();FeedbackCamera.Reset();FeedbackLease.Invalidate();RefreshTickRate();
}
void UEGElysiaHerrscherPresentationComponent::UpdateFeedbackShake()
{
 if(!FeedbackLease.IsValid())return;
 if(!FeedbackShake.IsValid()||!FeedbackShake->HasLease(FeedbackLease)||!GetWorld()){StopFeedbackShake();return;}
 const float Elapsed=FMath::Max(0.f,float(GetWorld()->GetTimeSeconds()-FeedbackStartedAt));
 FeedbackShake->SetVisualElapsed(Elapsed);
 if(Elapsed>=.32f)StopFeedbackShake();
}

void UEGElysiaHerrscherPresentationComponent::ApplySnapshot(EEGElysiaHerrscherPresentationPhase Phase)
{
 if(HasOriginVisual(Phase))
 {
  if(StarSeaClass)
  {
   // A second activation can arrive while the previous visual is fading out.
   // It is cosmetic and belongs to this unit; discard only this old instance.
   if(bStarSeaDismissing&&IsValid(StarSea)){StarSea->Destroy();StarSea=nullptr;}
   bStarSeaDismissing=false;
   if(!IsValid(StarSea)&&!bStarSeaInitFailed)
   {
    auto* World=GetWorld();
    const auto* GM=World?World->GetAuthGameMode<AEGChessGameMode>():nullptr;
    auto* Layout=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
    if(World&&Layout)
    {
     FActorSpawnParameters Params;Params.Owner=GetOwner();Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
     auto* Candidate=World->SpawnActor<AEGElysiaStarSeaVisual>(StarSeaClass,FTransform::Identity,Params);
     if(Candidate&&Candidate->InitializeFromBoard(Layout)){StarSea=Candidate;StarSea->BeginReveal();}
     else {if(Candidate)Candidate->Destroy();bStarSeaInitFailed=true;}
    }
   }
  }
  else
  {
   if(!IsValid(OriginStarfield))OriginStarfield=SpawnEffect(OriginStarfieldSystem,StarfieldTransform,false);
   else if(!OriginStarfield->IsActive())OriginStarfield->Activate(true);
  }
 }
 else
 {
  DestroyEffect(OriginStarfield);OriginStarfield=nullptr;
  bStarSeaInitFailed=false;
  if(IsValid(StarSea)&&!bStarSeaDismissing){bStarSeaDismissing=true;StarSea->BeginDismiss();}
 }
}

void UEGElysiaHerrscherPresentationComponent::HandleStatusChanged()
{
 const auto* Behavior=BoundBehavior.Get();const auto* Skill=BoundSkill.Get();
 const auto* Unit=Cast<AEGChessUnit>(GetOwner());
 const bool bShow=Behavior&&Skill&&Unit&&IEGCombatant::Execute_IsAlive(Unit)
  &&Behavior->GetPresentationRoundId()==BoundRound&&Behavior->GetPresentationPhase()!=EEGElysiaHerrscherPresentationPhase::BattleEnd
  &&Skill->GetSourceShield(MirrorSource)>0.f;
 if(bShow)
 {
  if(!IsValid(CrystalMirror))CrystalMirror=SpawnEffect(CrystalMirrorSystem,MirrorTransform,false);
  else if(!CrystalMirror->IsActive())CrystalMirror->Activate(true);
 }
 else {DestroyEffect(CrystalMirror);CrystalMirror=nullptr;}
}

void UEGElysiaHerrscherPresentationComponent::HandleOwnerDied(AEGChessUnit* Unit){Unbind();}

void UEGElysiaHerrscherPresentationComponent::ShowArrowImpact(const FVector& WorldPoint,const FRotator& FlightRotation,UEGSourceEffectDefinition* SourceImpact,bool bCharged)
{
 auto* Owner=GetOwner();
 if(!BoundBehavior.IsValid()||!BoundRound.IsValid()||!Owner||!Owner->GetRootComponent()
  ||WorldPoint.ContainsNaN()||FlightRotation.ContainsNaN()||ArrowImpactScale.ContainsNaN()||GetNetMode()==NM_DedicatedServer)return;
 if(!ArrowImpactSystem)return;
 auto* Component=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
 if(!Component)return;
 Owner->AddInstanceComponent(Component);Component->SetupAttachment(Owner->GetRootComponent());
 Component->SetAbsolute(true,true,true);
 Component->SetAutoActivate(false);Component->SetAutoDestroy(true);Component->SetAsset(ArrowImpactSystem);
 Component->RegisterComponent();Component->SetWorldLocationAndRotation(WorldPoint,FlightRotation);Component->SetWorldScale3D(ArrowImpactScale*(bCharged?1.35f:1.f));
 Component->Activate(true);ArrowImpacts.Add(Component);
}

void UEGElysiaHerrscherPresentationComponent::ShowOriginAttackImpact(const FVector& WorldPoint,bool bWave,FGuid Action)
{
 auto* Owner=Cast<AEGChessUnit>(GetOwner());auto* Skill=BoundSkill.Get();auto* Behavior=BoundBehavior.Get();
 UNiagaraSystem* System=bWave?OriginWaveSystem.Get():ScepterImpactSystem.Get();
 if(!bWave && Behavior && OriginAttackSystems.IsValidIndex(Behavior->GetOriginAttackCount()) && OriginAttackSystems[Behavior->GetOriginAttackCount()])
  System=OriginAttackSystems[Behavior->GetOriginAttackCount()].Get();
 const FTransform& Offset=bWave?OriginWaveTransform:ScepterImpactTransform;
 if(!Owner||!Owner->GetRootComponent()||!Skill||!Behavior||!BoundRound.IsValid()||
  Behavior->GetPresentationRoundId()!=BoundRound||!Behavior->IsOriginActive()||!Action.IsValid()||
  !Skill->IsActionRunning()||Skill->ActionId!=Action||LastOriginCueAction==Action||
  WorldPoint.ContainsNaN()||Offset.ContainsNaN()||GetNetMode()==NM_DedicatedServer)return;
 const FVector Start=Owner->GetActionOrigin();
 const FVector Direction=(WorldPoint-Start).GetSafeNormal2D();
 const FTransform WorldBase(Direction.IsNearlyZero()?Owner->GetActorRotation():Direction.Rotation(),bWave?Start:WorldPoint);
 bool bSourceSpawned=false;
 const int32 Index=Behavior->GetOriginAttackCount();
 if(OriginImpactPreload.IsValid()&&OriginImpactPreload->HasLoadCompleted()&&
  SourceOriginAttackImpacts.IsValidIndex(Index)&&IsValid(SourceOriginAttackImpacts[Index]))
 {
  auto* Source=SourceOriginAttackImpacts[Index].Get();
  // The recovered definitions are unanchored animation-assembly inputs. A
  // private transient copy changes only the placement contract; source tracks,
  // colors and assets are untouched. The combat Hit supplies the contact point.
  const bool bCompositeReady=Source->SourceDistortionComposite.IsNull()||Source->SourceDistortionComposite.ToSoftObjectPath().ResolveObject();
  const bool bReady=bCompositeReady&&!Source->Tracks.IsEmpty()&&Source->Tracks.ContainsByPredicate([](const FEGSourceEffectTrack& Track)
   {return !Track.Mesh.ToSoftObjectPath().ResolveObject()||!Track.Material.ToSoftObjectPath().ResolveObject()||
    (!Track.NativePassDefinition.IsNull()&&!Track.NativePassDefinition.ToSoftObjectPath().ResolveObject());})==false;
  if(bReady&&Source->AnchorBone.IsNone())
  {
   if(RuntimeOriginImpacts.Num()<=Index){RuntimeOriginImpacts.SetNum(Index+1);RuntimeOriginImpactSources.SetNum(Index+1);}
   if(RuntimeOriginImpactSources[Index]!=Source||!IsValid(RuntimeOriginImpacts[Index]))
   {
    RuntimeOriginImpacts[Index]=nullptr;RuntimeOriginImpactSources[Index]=nullptr;
    auto* Copy=DuplicateObject<UEGSourceEffectDefinition>(Source,this);
    if(Copy){Copy->bTargetGround=true;RuntimeOriginImpacts[Index]=Copy;RuntimeOriginImpactSources[Index]=Source;}
   }
   auto* Mesh=Owner->GetVisiblePresentationMesh();
   if(auto* Effect=AEGSourceEffect::SpawnAt(Skill,Mesh,RuntimeOriginImpacts[Index],1.f,WorldPoint,WorldBase.Rotator(),1.f))
   {SourceArrowImpacts.Add(Effect);bSourceSpawned=true;}
  }
 }
 // The wave remains an independent direction cue; single-target Niagara is
 // the fallback if its source impact cannot be instantiated.
 if(!System||(!bWave&&bSourceSpawned)){if(bSourceSpawned)LastOriginCueAction=Action;return;}
 auto* Component=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
 if(!Component)return;
 Owner->AddInstanceComponent(Component);Component->SetupAttachment(Owner->GetRootComponent());
 // Contact is fixed in world space even if the caster immediately moves on.
 Component->SetAbsolute(true,true,true);
 Component->SetAutoActivate(false);Component->SetAutoDestroy(true);Component->SetAsset(System);
 Component->RegisterComponent();Component->SetWorldTransform(Offset*WorldBase);Component->Activate(true);
 ArrowImpacts.Add(Component);LastOriginCueAction=Action;
}

void UEGElysiaHerrscherPresentationComponent::ShowOriginAttackContact(const FVector& WorldPoint,FGuid Round,FGuid Action,int32 ContactIndex,int32 AttackIndex)
{
 auto* Owner=Cast<AEGChessUnit>(GetOwner());auto* Behavior=BoundBehavior.Get();
 if(!Owner||!Owner->GetRootComponent()||!IEGCombatant::Execute_IsAlive(Owner)||!Behavior||
  !Round.IsValid()||Round!=BoundRound||Behavior->GetPresentationRoundId()!=Round||!Behavior->IsOriginActive()||
  !Action.IsValid()||ContactIndex<0||ContactIndex>2||AttackIndex<0||WorldPoint.ContainsNaN()||
  ScepterImpactTransform.ContainsNaN()||GetNetMode()==NM_DedicatedServer)return;
 uint8& Mask=OriginContactMasks.FindOrAdd(Action);
 const uint8 Bit=uint8(1u<<ContactIndex);
 if((Mask&Bit)!=0)return;
 Mask|=Bit;
 if(ContactIndex==0)
 {
  // The main source-derived impact is still tied to the active action.
  ShowOriginAttackImpact(WorldPoint,false,Action);
  return;
 }
 // Follow-up bursts use the same configured attack visual at a smaller scale.
 // The combat owner has already confirmed the hit; unlike the main source
 // effect, these must also work just after a normally completed montage.
 UNiagaraSystem* System=OriginAttackSystems.IsValidIndex(AttackIndex)&&OriginAttackSystems[AttackIndex]
  ?OriginAttackSystems[AttackIndex].Get():ScepterImpactSystem.Get();
 if(!System)return;
 const FVector Direction=(WorldPoint-Owner->GetActionOrigin()).GetSafeNormal2D();
 const FTransform Base(Direction.IsNearlyZero()?Owner->GetActorRotation():Direction.Rotation(),WorldPoint);
 auto* Component=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
 if(!Component)return;
 Owner->AddInstanceComponent(Component);Component->SetupAttachment(Owner->GetRootComponent());
 Component->SetAbsolute(true,true,true);Component->SetAutoActivate(false);Component->SetAutoDestroy(true);Component->SetAsset(System);
 Component->RegisterComponent();
 FTransform Placement=ScepterImpactTransform*Base;
 Placement.SetScale3D(Placement.GetScale3D()*(ContactIndex==1?.7f:.85f));
 Component->SetWorldTransform(Placement);Component->Activate(true);ArrowImpacts.Add(Component);
}

UNiagaraComponent* UEGElysiaHerrscherPresentationComponent::SpawnEffect(UNiagaraSystem* System,const FTransform& Transform,bool bOneShot,bool bDuringPause)
{
 auto* Owner=GetOwner();auto* Unit=Cast<AEGChessUnit>(Owner);
 auto* Body=Unit?Unit->GetVisiblePresentationMesh():nullptr;
 if(!System||!IsValid(Body)||!Body->IsRegistered()||!Body->GetSkeletalMeshAsset()||!GetWorld()
  ||Transform.ContainsNaN()||GetNetMode()==NM_DedicatedServer)return nullptr;
 auto* Component=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
 if(!Component)return nullptr;
 // The visible body can belong to a deployed Piece rather than this Unit.
 // Attach to the mesh component itself, never a 100x imported bone socket.
 Owner->AddInstanceComponent(Component);Component->SetupAttachment(Body);Component->SetRelativeTransform(Transform);
 Component->SetAutoActivate(false);Component->SetAutoDestroy(bOneShot);Component->SetAsset(System);
 if(bDuringPause){Component->SetForceSolo(true);Component->PrimaryComponentTick.bTickEvenWhenPaused=true;}
 Component->RegisterComponent();Component->Activate(true);
 return Component;
}

UNiagaraComponent* UEGElysiaHerrscherPresentationComponent::SpawnBoardEffect(UNiagaraSystem* System,const FTransform& Transform)
{
 auto* Owner=GetOwner();auto* World=GetWorld();
 const auto* GM=World?World->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Board=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
 if(!System||!Owner||!Owner->GetRootComponent()||!IsValid(Board)||Board->Columns<1||Board->Rows<1||Transform.ContainsNaN()||
  !FMath::IsFinite(Board->CellWidth)||!FMath::IsFinite(Board->CellDepth)||Board->CellWidth<=0.f||Board->CellDepth<=0.f)return nullptr;
 const FVector Center=(Board->CellToWorld(FIntPoint(0,0))+Board->CellToWorld(FIntPoint(Board->Columns-1,Board->Rows-1)))*.5f;
 if(Center.ContainsNaN())return nullptr;
 const FVector BoardScale=Board->GetActorScale3D();
 const float Width=Board->Columns*Board->CellWidth*FMath::Abs(BoardScale.X);
 const float Depth=Board->Rows*Board->CellDepth*FMath::Abs(BoardScale.Y);
 if(!FMath::IsFinite(Width)||!FMath::IsFinite(Depth)||Width<=0.f||Depth<=0.f)return nullptr;
 const float BoardDiameter=FMath::Sqrt(Width*Width+Depth*Depth);
 if(!FMath::IsFinite(BoardDiameter)||BoardDiameter<=0.f)return nullptr;
 // Private finisher systems are authored against a 1000 cm board reference.
 const FTransform Base(Board->GetActorQuat(),Center,FVector(Width/1000.f,Depth/1000.f,1.f));
 auto* Component=NewObject<UNiagaraComponent>(Owner,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
 if(!Component)return nullptr;
 Owner->AddInstanceComponent(Component);Component->SetupAttachment(Owner->GetRootComponent());
 Component->SetAbsolute(true,true,true);Component->SetAutoActivate(false);Component->SetAutoDestroy(true);Component->SetAsset(System);
 Component->RegisterComponent();Component->SetWorldTransform(Transform*Base);
 // Sprite size is authored in world cm by the private Niagara systems; it
 // does not inherit component scale. The diagonal covers the board corners.
 Component->SetVariableFloat(TEXT("User.BoardDiameter"),BoardDiameter);
 Component->Activate(true);
 return Component;
}

bool UEGElysiaHerrscherPresentationComponent::SpawnPhaseBurst(EEGElysiaHerrscherPresentationPhase Phase)
{
 const bool bEntry=Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::Origin;
 const bool bStart=Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin;
 UNiagaraSystem* System=bEntry?(bStart?EntryStartSystem.Get():EntryBurstSystem.Get()):(bStart?FinisherStartSystem.Get():FinisherBurstSystem.Get());
 if(!System)return true;
 UNiagaraComponent* Effect=bEntry?SpawnEffect(System,EntryTransform,true,true):SpawnBoardEffect(System,FinisherTransform);
 if(!Effect)return false;
 if(bEntry)Bursts.Add(Effect);else BoardBursts.Add(Effect);
 return true;
}

void UEGElysiaHerrscherPresentationComponent::RefreshEffectAttachments()
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 auto* Body=Unit?Unit->GetVisiblePresentationMesh():nullptr;
 const bool bReady=IsValid(Body)&&Body->IsRegistered()&&Body->GetSkeletalMeshAsset();
 const auto Refresh=[Body,bReady](UNiagaraComponent* Component)
 {
  if(!IsValid(Component))return;
  if(!bReady){Component->SetVisibility(false);return;}
  if(Component->GetAttachParent()!=Body)Component->AttachToComponent(Body,FAttachmentTransformRules::KeepRelativeTransform);
  Component->SetVisibility(true);
 };
 Refresh(CrystalMirror);Refresh(OriginStarfield);Refresh(BowCharge);
 for(const auto& Burst:Bursts)Refresh(Burst.Get());
}

void UEGElysiaHerrscherPresentationComponent::DestroyEffect(UNiagaraComponent* Component)
{
 if(!IsValid(Component))return;
 Component->DeactivateImmediate();
 if(auto* Owner=GetOwner())Owner->RemoveInstanceComponent(Component);
 Component->DestroyComponent();
}

void UEGElysiaHerrscherPresentationComponent::ClearBursts()
{
 for(const auto& Burst:Bursts)DestroyEffect(Burst.Get());
 for(const auto& Burst:BoardBursts)DestroyEffect(Burst.Get());
 Bursts.Reset();BoardBursts.Reset();PendingBurstAction.Invalidate();PendingBurstPhase=-1;
}

void UEGElysiaHerrscherPresentationComponent::ClearEffects()
{
 OriginContactMasks.Reset();
 StopFeedbackShake();
 StopEntryMovie();
 EndBoardFinisher();
 ClearBursts();
 for(const auto& Impact:ArrowImpacts)DestroyEffect(Impact.Get());
 for(const auto& Impact:SourceArrowImpacts)if(Impact.IsValid())Impact->Destroy();
 SourceArrowImpacts.Reset();
 RuntimeOriginImpacts.Reset();RuntimeOriginImpactSources.Reset();
 ArrowImpacts.Reset();DestroyEffect(BowCharge);DestroyEffect(CrystalMirror);DestroyEffect(OriginStarfield);
 BowCharge=nullptr;CrystalMirror=nullptr;OriginStarfield=nullptr;
 if(IsValid(StarSea))StarSea->Destroy();
 StarSea=nullptr;bStarSeaDismissing=false;bStarSeaInitFailed=false;
}

void UEGElysiaHerrscherPresentationComponent::BeginBoardFinisher(FGuid Action)
{
 EndBoardFinisher();
 auto* Unit=Cast<AEGChessUnit>(GetOwner());auto* Skill=BoundSkill.Get();auto* World=GetWorld();
 const auto* Definition=Unit?Unit->GetDefinition():nullptr;
 const auto* GM=World?World->GetAuthGameMode<AEGChessGameMode>():nullptr;
 auto* Board=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
 auto* Assembly=BoardFinisherAssembly.LoadSynchronous();
 auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 if(!Unit||!Skill||!World||!Definition||!IsValid(Board)||Board->Columns<1||Board->Rows<1||
  !Assembly||Assembly->Motions.IsEmpty()||!Montage||BoardFinisherOffset.ContainsNaN()||!Skill->IsActionRunning()||Skill->ActionId!=Action)return;
 const int32 PlayerRows=FMath::Clamp(Board->PlayerRows,0,Board->Rows);
 const int32 EnemyStart=FMath::Clamp(PlayerRows+Board->NeutralRows,0,Board->Rows);
 if(PlayerRows<1||EnemyStart>=Board->Rows||!FMath::IsFinite(Board->CellDepth)||Board->CellDepth<=0.f)return;
 const EEGFaction Faction=Unit->GetFaction_Implementation();
 if(Faction!=EEGFaction::Player&&Faction!=EEGFaction::Enemy)return;
 float LandingTime=MAX_flt;
 for(const FAnimNotifyEvent& Event:Montage->Notifies)
  if(const auto* Notify=Cast<UEGSkillNotify>(Event.Notify);Notify&&(Notify->Event==TEXT("Hit")||Notify->Event==TEXT("Release")))
   LandingTime=FMath::Min(LandingTime,Event.GetTriggerTime());
 if(!FMath::IsFinite(LandingTime)||LandingTime<=0.f||LandingTime==MAX_flt)return;
 const float PlayerY=(.5f*PlayerRows-.5f*Board->Rows)*Board->CellDepth;
 const float EnemyY=(.5f*(EnemyStart+Board->Rows)-.5f*Board->Rows)*Board->CellDepth;
 const FVector PlayerCenter=Board->GetActorTransform().TransformPosition(FVector(0,PlayerY,0));
 const FVector EnemyCenter=Board->GetActorTransform().TransformPosition(FVector(0,EnemyY,0));
 const FVector Start=Faction==EEGFaction::Player?PlayerCenter:EnemyCenter;
 const FVector End=Faction==EEGFaction::Player?EnemyCenter:PlayerCenter;
 const FVector Heading=(End-Start).GetSafeNormal2D();
 const FVector Scale=Definition->GetMeshScale();
 if(Start.ContainsNaN()||End.ContainsNaN()||Heading.IsNearlyZero()||Scale.ContainsNaN()||Definition->GetPresentationRotation().ContainsNaN()||
  !FMath::IsFinite(Definition->GetPresentationGroundOffset()))return;
 const FTransform Base(Heading.Rotation().Quaternion()*Definition->GetPresentationRotation().Quaternion(),
  Start+FVector(0,0,Definition->GetPresentationGroundOffset()),Scale);
 const FTransform Transform=BoardFinisherOffset*Base;
 auto* Visual=World->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),Transform,Unit,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!Visual)return;
 Visual->Definition=Assembly;Visual->MotionIndex=0;Visual->bAutoPlay=false;Visual->bLoop=false;
 Visual->FinishSpawning(Transform);Visual->SetActorTickEnabled(false);
 BoardFinisher=Visual;BoardFinisherAction=Action;
 BoardFinisherStart=Transform;BoardFinisherTravel=End-Start;BoardFinisherUp=Board->GetActorUpVector();
 BoardFinisherLandingTime=LandingTime;
 BoardFinisherLeapHeight=Board->CellDepth*FMath::Abs(Board->GetActorScale3D().Y)*.45f;
 BoardFinisherClockMesh=Unit->GetVisiblePresentationMesh();
 if(BoardFinisherClockMesh.IsValid())AddTickPrerequisiteComponent(BoardFinisherClockMesh.Get());
 Visual->SetSourceTime(0.f);RefreshTickRate();
}

void UEGElysiaHerrscherPresentationComponent::UpdateBoardFinisher()
{
 if(!IsValid(BoardFinisher))
 {
  // An externally destroyed cosmetic actor must not leave per-frame ticking on.
  if(BoardFinisherAction.IsValid())EndBoardFinisher();
  return;
 }
 auto* Skill=BoundSkill.Get();auto* Behavior=BoundBehavior.Get();auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(!Skill||!Behavior||!Unit||!IEGCombatant::Execute_IsAlive(Unit)||!Skill->IsActionRunning()||
  Skill->ActionId!=BoardFinisherAction||Behavior->GetPresentationRoundId()!=BoundRound||
  Behavior->GetPresentationActionId()!=BoardFinisherAction)
 {EndBoardFinisher();return;}
 auto* Mesh=Unit->GetVisiblePresentationMesh();auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
 if(Mesh!=BoardFinisherClockMesh.Get())
 {
  if(BoardFinisherClockMesh.IsValid())RemoveTickPrerequisiteComponent(BoardFinisherClockMesh.Get());
  BoardFinisherClockMesh=Mesh;
  if(Mesh)AddTickPrerequisiteComponent(Mesh);
 }
 auto* Montage=Skill->GetActiveMontage();
 if(Anim&&Montage)
 {
  const float Time=Anim->Montage_GetPosition(Montage);
  if(!FMath::IsFinite(Time)||BoardFinisherLandingTime<=0.f){EndBoardFinisher();return;}
  const float Alpha=FMath::Clamp((Time-.2f*BoardFinisherLandingTime)/(.8f*BoardFinisherLandingTime),0.f,1.f);
  const float Eased=Alpha*Alpha*(3.f-2.f*Alpha);
  FTransform Transform=BoardFinisherStart;
  Transform.SetLocation(BoardFinisherStart.GetLocation()+BoardFinisherTravel*Eased+
   BoardFinisherUp*(BoardFinisherLeapHeight*FMath::Sin(PI*Alpha)));
  BoardFinisher->SetActorTransform(Transform);BoardFinisher->SetSourceTime(Time);
 }
}

void UEGElysiaHerrscherPresentationComponent::EndBoardFinisher()
{
 if(BoardFinisherClockMesh.IsValid())RemoveTickPrerequisiteComponent(BoardFinisherClockMesh.Get());
 BoardFinisherClockMesh.Reset();
 if(IsValid(BoardFinisher))BoardFinisher->Destroy();
 BoardFinisher=nullptr;BoardFinisherAction.Invalidate();
 BoardFinisherStart=FTransform::Identity;BoardFinisherTravel=FVector::ZeroVector;BoardFinisherUp=FVector::UpVector;
 BoardFinisherLandingTime=0.f;BoardFinisherLeapHeight=0.f;
 RefreshTickRate();
}

void UEGElysiaHerrscherPresentationComponent::RefreshTickRate()
{
 const float Interval=(bEntryMoviePending||BoardFinisherAction.IsValid()||FeedbackLease.IsValid())?0.f:.1f;
 if(GetComponentTickInterval()!=Interval)
 {
  SetComponentTickInterval(Interval);
  // Reset an existing tick cooldown, including when the world is paused.
  if(IsComponentTickEnabled()){SetComponentTickEnabled(false);SetComponentTickEnabled(true);}
 }
}

void UEGElysiaHerrscherPresentationComponent::PreloadEntryMovie()
{
 auto* World=GetWorld();
 auto* Controller=World?World->GetFirstPlayerController():nullptr;
 if(!bEntryMovieEnabled||EntryMovieSource.IsNull()||!Controller||!Controller->IsLocalController()||EntryMoviePlayer||EntryMovieLoad.IsValid())return;
 const uint64 Generation=++EntryMovieGeneration;
 if(EntryMovieSource.IsValid()){OpenLoadedEntryMovie(Generation);return;}
 TSharedPtr<FStreamableHandle> Load=UAssetManager::GetStreamableManager().RequestAsyncLoad(EntryMovieSource.ToSoftObjectPath(),
  FStreamableDelegate::CreateWeakLambda(this,[this,Generation](){OpenLoadedEntryMovie(Generation);}));
 if(Generation!=EntryMovieGeneration){if(Load.IsValid())Load->CancelHandle();return;}
 EntryMovieLoad=MoveTemp(Load);
 if(!EntryMovieLoad.IsValid()&&Generation==EntryMovieGeneration)
 {
  bEntryMovieFailed=true;
  if(bEntryMoviePending)StopEntryMovie();
 }
}

void UEGElysiaHerrscherPresentationComponent::OpenLoadedEntryMovie(uint64 Generation)
{
 if(Generation!=EntryMovieGeneration)return;
 LoadedEntryMovie=EntryMovieSource.Get();
 if(!LoadedEntryMovie)
 {
  bEntryMovieFailed=true;
  if(bEntryMoviePending)StopEntryMovie();
  return;
 }
 EntryMoviePlayer=NewObject<UMediaPlayer>(this,NAME_None,RF_Transient);
 if(!EntryMoviePlayer)
 {
  bEntryMovieFailed=true;
  if(bEntryMoviePending)StopEntryMovie();
  return;
 }
 EntryMoviePlayer->PlayOnOpen=false;
 EntryMoviePlayer->NativeAudioOut=false;
 EntryMoviePlayer->OnMediaOpened.AddDynamic(this,&ThisClass::OnEntryMovieOpened);
 EntryMoviePlayer->OnMediaOpenFailed.AddDynamic(this,&ThisClass::OnEntryMovieOpenFailed);
 EntryMoviePlayer->OnEndReached.AddDynamic(this,&ThisClass::OnEntryMovieEnded);
 // Both success and failure callbacks may run before OpenSource returns.
 UMediaPlayer* Player=EntryMoviePlayer;
 const bool bAccepted=Player->OpenSource(LoadedEntryMovie);
 if(!bAccepted&&Generation==EntryMovieGeneration&&EntryMoviePlayer==Player)
 {
  bEntryMovieFailed=true;
  if(bEntryMoviePending)StopEntryMovie();
 }
}

void UEGElysiaHerrscherPresentationComponent::BeginEntryMovie(FGuid Round,FGuid Action)
{
 if(!bEntryMovieEnabled||EntryMovieSource.IsNull()||!Round.IsValid()||!Action.IsValid())return;
 auto* World=GetWorld();auto* Controller=World?World->GetFirstPlayerController():nullptr;
 if(!Controller||!Controller->IsLocalController())return;
 if(bEntryMoviePending)StopEntryMovie();
 if(bEntryMovieFailed){StopEntryMovie();return;}
 bEntryMoviePending=true;bEntryMoviePlaying=false;bEntryMovieHadPause=false;
 EntryMovieRound=Round;EntryMovieAction=Action;EntryMovieRequestedAt=FPlatformTime::Seconds();
 RefreshTickRate();
 PreloadEntryMovie();
}

void UEGElysiaHerrscherPresentationComponent::UpdateEntryMovie()
{
 if(!bEntryMoviePending)return;
 const double Elapsed=FPlatformTime::Seconds()-EntryMovieRequestedAt;
 auto* Unit=Cast<AEGChessUnit>(GetOwner());auto* Skill=BoundSkill.Get();auto* Behavior=BoundBehavior.Get();
 auto* World=GetWorld();auto* GM=World?World->GetAuthGameMode<AEGChessGameMode>():nullptr;
 auto* Battle=GM?GM->BattleRules.Get():nullptr;
 const auto Phase=Behavior?Behavior->GetPresentationPhase():EEGElysiaHerrscherPresentationPhase::BattleEnd;
 if(!Unit||!IEGCombatant::Execute_IsAlive(Unit)||!Skill||!Behavior||!Battle||!Battle->IsCombatRunning()||
  BoundRound!=EntryMovieRound||Battle->GetRoundId()!=EntryMovieRound||Behavior->GetPresentationRoundId()!=EntryMovieRound||
  Behavior->GetPresentationActionId()!=EntryMovieAction||!Skill->IsActionRunning()||Skill->ActionId!=EntryMovieAction||
  (Phase!=EEGElysiaHerrscherPresentationPhase::Entering&&Phase!=EEGElysiaHerrscherPresentationPhase::EntryReleased)||
  bEntryMovieFailed||Elapsed>=14.0)
 {StopEntryMovie();return;}
 const bool bOwnsPause=Battle->IsHumanEntryPauseOwner(Unit,EntryMovieAction);
 if(!bOwnsPause)
 {
  if(bEntryMovieHadPause||Elapsed>=2.0)StopEntryMovie();
  return;
 }
 bEntryMovieHadPause=true;
 if(!bEntryMovieOpened)
 {
  if(Elapsed>=2.0)StopEntryMovie();
  return;
 }
 if(!bEntryMoviePlaying)
 {
  auto* Controller=World->GetFirstPlayerController();
  if(!Controller||!Controller->IsLocalController()||!EntryMoviePlayer){StopEntryMovie();return;}
  EntryMovieTexture=NewObject<UMediaTexture>(this,NAME_None,RF_Transient);
  EntryMovieWidget=CreateWidget<UEGElysiaEntryMovieWidget>(Controller,UEGElysiaEntryMovieWidget::StaticClass());
  if(!EntryMovieTexture||!EntryMovieWidget){StopEntryMovie();return;}
  EntryMovieTexture->SetMediaPlayer(EntryMoviePlayer);EntryMovieTexture->UpdateResource();
  EntryMovieWidget->SetMovieTexture(EntryMovieTexture);EntryMovieWidget->AddToViewport(10000);
  bEntryMoviePlaying=true;
  UMediaPlayer* Player=EntryMoviePlayer;
  const bool bStarted=Player->Play();
  if(!bStarted&&bEntryMoviePending&&EntryMoviePlayer==Player){StopEntryMovie();return;}
 }
 // Media dimensions and advancing clock are only a decoded-frame candidate;
 // platform-specific first-frame visibility still requires a rendered check.
 if(bEntryMoviePlaying&&EntryMoviePlayer&&EntryMovieTexture&&EntryMovieWidget&&
  EntryMoviePlayer->GetTime().GetTotalSeconds()>.05&&EntryMovieTexture->GetWidth()>2&&EntryMovieTexture->GetHeight()>2)
  EntryMovieWidget->SetMovieVisible(true);
}

void UEGElysiaHerrscherPresentationComponent::StopEntryMovie()
{
 ++EntryMovieGeneration;
 bEntryMoviePending=false;bEntryMoviePlaying=false;bEntryMovieOpened=false;bEntryMovieFailed=false;bEntryMovieHadPause=false;
 EntryMovieRound.Invalidate();EntryMovieAction.Invalidate();EntryMovieRequestedAt=0.0;
 if(EntryMovieLoad.IsValid()){EntryMovieLoad->CancelHandle();EntryMovieLoad.Reset();}
 if(EntryMoviePlayer)
 {
  EntryMoviePlayer->OnMediaOpened.RemoveDynamic(this,&ThisClass::OnEntryMovieOpened);
  EntryMoviePlayer->OnMediaOpenFailed.RemoveDynamic(this,&ThisClass::OnEntryMovieOpenFailed);
  EntryMoviePlayer->OnEndReached.RemoveDynamic(this,&ThisClass::OnEntryMovieEnded);
  EntryMoviePlayer->Close();
 }
 if(EntryMovieWidget){EntryMovieWidget->SetMovieTexture(nullptr);EntryMovieWidget->RemoveFromParent();EntryMovieWidget=nullptr;}
 if(EntryMovieTexture){EntryMovieTexture->SetMediaPlayer(nullptr);EntryMovieTexture->UpdateResource();EntryMovieTexture=nullptr;}
 EntryMoviePlayer=nullptr;LoadedEntryMovie=nullptr;
 RefreshTickRate();
}

void UEGElysiaHerrscherPresentationComponent::OnEntryMovieOpened(FString Url){bEntryMovieOpened=true;}
void UEGElysiaHerrscherPresentationComponent::OnEntryMovieOpenFailed(FString Url)
{
 bEntryMovieFailed=true;
 if(bEntryMoviePending)StopEntryMovie();
}
void UEGElysiaHerrscherPresentationComponent::OnEntryMovieEnded(){if(bEntryMoviePending)StopEntryMovie();}

void UEGElysiaHerrscherPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->OnDied.RemoveDynamic(this,&ThisClass::HandleOwnerDied);
 Unbind();Super::EndPlay(Reason);
}
