#include "Presentation/EGCorruptionPhantomVisual.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGMonsterSkillVisual.h"
#include "Skills/EGMonsterSkillBehavior.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Animation/AnimSequence.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AEGCorruptionPhantomVisual::AEGCorruptionPhantomVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));SetActorEnableCollision(false);
 Body=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("PhantomBody"));Body->SetupAttachment(GetRootComponent());
 Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);Body->SetGenerateOverlapEvents(false);
 Body->SetCanEverAffectNavigation(false);Body->SetCastShadow(false);
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Ghost(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionPhantom"));
 PhantomMaterial=Ghost.Object;
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Accent(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/NS_CorruptionSpreadImpact"));
 PhantomAccentSystem=Accent.Object;
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Burst(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Niagara/NS_CorruptionAreaBurst"));
 PhantomBurstSystem=Burst.Object;
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> ImpactSound(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundWave/SW_impact_star.SW_impact_star"));
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> ShieldSound(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundCue/SC_Block.SC_Block"));
 MimicImpactSound=ImpactSound.Get();MimicShieldSound=ShieldSound.Get();
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> BeamMaterial(TEXT("/Game/Assets/Environments/Board/Materials/M_ChessBoardPerimeter"));
 CastBeam=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhantomCastBeam"));CastBeam->SetupAttachment(GetRootComponent());
 CastBeam->SetStaticMesh(Cube.Object);if(BeamMaterial.Object)CastBeam->SetMaterial(0,BeamMaterial.Object);
 CastBeam->SetCollisionEnabled(ECollisionEnabled::NoCollision);CastBeam->SetCanEverAffectNavigation(false);CastBeam->SetCastShadow(false);CastBeam->SetVisibility(false);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMaterial(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 CastAreaRing=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PhantomAreaRing"));CastAreaRing->SetupAttachment(GetRootComponent());
 CastAreaRing->SetStaticMesh(Plane.Object);if(RingMaterial.Object)CastAreaRing->SetMaterial(0,RingMaterial.Object);
 CastAreaRing->SetCollisionEnabled(ECollisionEnabled::NoCollision);CastAreaRing->SetCanEverAffectNavigation(false);CastAreaRing->SetCastShadow(false);CastAreaRing->SetVisibility(false);
}
AEGCorruptionPhantomVisual* AEGCorruptionPhantomVisual::Spawn(AEGChessUnit* Boss,const UEGHeroDefinition* BaseDefinition,FVector Location,FRotator Facing,float WindupDuration,FGuid ActionId)
{
 if(!IsValid(Boss)||!IsValid(BaseDefinition)||!Boss->GetWorld()||Boss->GetNetMode()==NM_DedicatedServer||
    Location.ContainsNaN()||Facing.ContainsNaN()||!FMath::IsFinite(WindupDuration)||WindupDuration<=0.f||!ActionId.IsValid())return nullptr;
 auto* Context=Boss->GetHeroSkill()?Boss->GetHeroSkill()->Battle.Get():nullptr;
 auto* Component=Boss->FindComponentByClass<UEGCorruptionCombatComponent>();
 if(!IsValid(Context)||!Context->IsCombatRunning()||!IsValid(Component)||
    Component->GetBossCastKind()!=EEGCorruptionBossCast::Mimic||Component->GetBossCastActionId()!=ActionId)return nullptr;
 // Offset in the board's actual basis toward the player side, keeping the
 // cosmetic actor inside its footprint. This does not claim a combat cell.
 if(const auto* Mode=Boss->GetWorld()->GetAuthGameMode<AEGChessGameMode>())
  if(const auto* Board=Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr)
  {
   const FTransform Basis=Board->GetActorTransform();
   FVector Local=Basis.InverseTransformPosition(Boss->GetActorLocation());
   const float HalfX=FMath::Max(0.f,Board->Columns*Board->CellWidth*.5f-Board->CellWidth*.2f);
   const float HalfY=FMath::Max(0.f,Board->Rows*Board->CellDepth*.5f-Board->CellDepth*.2f);
   Local.X=FMath::Clamp(Local.X+(Local.X>0.f?-1.f:1.f)*Board->CellWidth*.9f,-HalfX,HalfX);
   Local.Y=FMath::Clamp(Local.Y-Board->CellDepth*.25f,-HalfY,HalfY);
   const float OriginalZ=Boss->GetActorLocation().Z;
   Location=Basis.TransformPosition(Local);Location.Z=OriginalZ;
  }
 USkeletalMesh* Mesh=BaseDefinition->GetMesh().LoadSynchronous();if(!IsValid(Mesh))return nullptr;
 auto* Visual=Boss->GetWorld()->SpawnActor<AEGCorruptionPhantomVisual>(Location,Facing);if(!Visual)return nullptr;
 Visual->SetOwner(Boss);Visual->Source=Boss;Visual->Battle=Context;Visual->Round=Context->GetRoundId();
 Visual->Corruption=Component;Visual->CastAction=ActionId;
 Visual->CastDelegate=Component->OnBossCastEvent.AddUObject(Visual,&ThisClass::HandleBossCast);
 if(!Context->IsCombatRunning()||Context->GetRoundId()!=Visual->Round||
    Component->GetBossCastKind()!=EEGCorruptionBossCast::Mimic||Component->GetBossCastActionId()!=ActionId)
 {Visual->Destroy();return nullptr;}
 const float BoardScale=FEGBoardModelSizing::Resolve(BaseDefinition,Visual);
 Visual->Body->SetSkeletalMesh(Mesh);Visual->Body->SetRelativeScale3D(BaseDefinition->GetMeshScale()*BoardScale);
 Visual->Body->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 Visual->Body->bSuppressNotifyEventDispatch=true;
 Visual->Body->SetRelativeRotation(BaseDefinition->GetPresentationRotation());
 Visual->Body->SetRelativeLocation(FVector(0,0,-88.f+BaseDefinition->GetPresentationGroundOffset()*BoardScale));
 Visual->CastBeamMID=Visual->CastBeam->CreateDynamicMaterialInstance(0);
 if(Visual->CastBeamMID)Visual->CastBeamMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1.5f,.2f,2.5f,1.f));
 Visual->CastAreaRingMID=Visual->CastAreaRing->CreateDynamicMaterialInstance(0);
 if(Visual->CastAreaRingMID){Visual->CastAreaRingMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.9f,.05f,1.8f,1.f));Visual->CastAreaRingMID->SetScalarParameterValue(TEXT("Glow"),1.5f);}
 if(Visual->PhantomMaterial)
 {
  Visual->PhantomMID=UMaterialInstanceDynamic::Create(Visual->PhantomMaterial,Visual);
  if(Visual->PhantomMID)
  {
   Visual->PhantomMID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1.f,.16f,1.7f,1.f));
   Visual->PhantomMID->SetScalarParameterValue(TEXT("Opacity"),.82f);
   Visual->PhantomMID->SetScalarParameterValue(TEXT("Glow"),2.1f);
   for(int32 Slot=0;Slot<Mesh->GetMaterials().Num();++Slot)Visual->Body->SetMaterial(Slot,Visual->PhantomMID);
  }
 }
 if(auto* Animation=BaseDefinition->GetSkillAnimation().LoadSynchronous())Visual->Body->PlayAnimation(Animation,false);
 // Use the existing authored source rig, which carries body, attachments,
 // weapons and their timing. Never bind or mutate the live player's body.
 if(const auto* Presentation=BaseDefinition->PresentationDefinition.Get())
 {
  UEGSourcePreviewDefinition* SourceDefinition=Presentation->SkillAssembly.LoadSynchronous();
  if(!SourceDefinition&&!Presentation->UltimateAssemblies.IsEmpty())SourceDefinition=Presentation->UltimateAssemblies[0].LoadSynchronous();
  if(!SourceDefinition&&!Presentation->AttackAssemblies.IsEmpty())SourceDefinition=Presentation->AttackAssemblies[0].LoadSynchronous();
  if(SourceDefinition&&!SourceDefinition->Motions.IsEmpty()&&SourceDefinition->Motions[0].Duration>0.f)
  {
   // Mimic borrows the selected hero's body and weapon motion, but the boss
   // resolves a simplified, separate effect. Never play the hero's original
   // damage-looking effect tracks on top of that authoritative result.
   // The duplicate is transient and leaves the shared source definition intact.
   auto* GhostDefinition=DuplicateObject<UEGSourcePreviewDefinition>(SourceDefinition,Visual);
   if(!GhostDefinition)
   {
    UE_LOG(LogTemp,Warning,TEXT("Corruption mimic could not copy the motion definition; using its notify-suppressed body fallback"));
    Visual->SpawnPhantomAccent(.65f);Visual->Tick(0.f);return Visual;
   }
   for(auto& Motion:GhostDefinition->Motions)Motion.Effects.Reset();
   const FTransform Transform(Facing+BaseDefinition->GetPresentationRotation(),
       Location+FVector(0,0,-88.f+BaseDefinition->GetPresentationGroundOffset()),BaseDefinition->GetMeshScale());
   auto* Rig=Boss->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),Transform,Visual,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if(Rig)
   {
    Rig->Definition=GhostDefinition;Rig->bAutoPlay=false;Rig->bLoop=false;
    Rig->FinishSpawning(Transform);Rig->SetActorTickEnabled(false);
    Rig->SetBoardModelScale(BoardScale,BaseDefinition->GetPresentationGroundOffset());Rig->SetSourceTime(0.f);
    // The authored body graph is a combat graph, not a cosmetic-part graph.
    // Bind our own body so source sampling reaches its sequence evaluator.
    Visual->Body->SetAnimInstanceClass(BaseDefinition->GetAnimationClass().LoadSynchronous());
    if(auto* Instance=Cast<UEGCombatAnimInstance>(Visual->Body->GetAnimInstance()))
    {
     Instance->Configure(BaseDefinition);Rig->BindBody(Visual->Body,true);
     Visual->Body->SetComponentTickEnabled(false);
    }
    Visual->Assembly=Rig;
    Visual->MotionDuration=GhostDefinition->Motions[0].Duration;
   }
  }
 }
 Visual->SpawnPhantomAccent(.65f);Visual->Tick(0.f);return Visual;
}
void AEGCorruptionPhantomVisual::HandleBossCast(EEGCorruptionBossCast Cast,EEGCorruptionBossCastEvent Event,FGuid Action,float Duration)
{
 if(Cast==EEGCorruptionBossCast::Mimic&&Action==CastAction&&Event==EEGCorruptionBossCastEvent::Cancelled)Destroy();
}
void AEGCorruptionPhantomVisual::SpawnPhantomAccent(float Scale,FVector RelativeLocation,UNiagaraSystem* System)
{
 if(!System)System=PhantomAccentSystem;
 if(!IsValid(System)||!GetWorld()||GetNetMode()==NM_DedicatedServer||PhantomAccents.Num()>=24||!FMath::IsFinite(Scale)||Scale<=0.f||RelativeLocation.ContainsNaN())return;
 auto* Accent=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);if(!Accent)return;
 AddInstanceComponent(Accent);Accent->SetupAttachment(GetRootComponent());Accent->SetRelativeLocation(RelativeLocation);
 Accent->SetRelativeScale3D(FVector(Scale));Accent->SetAsset(System);Accent->SetAutoActivate(false);Accent->SetAutoDestroy(false);
 Accent->RegisterComponent();Accent->Activate(true);PhantomAccents.Add(Accent);
}
void AEGCorruptionPhantomVisual::NotifyCast(FVector TargetLocation,float Radius,EEGCorruptionMimicCue Cue,bool bShieldApplied)
{
 if(CastAge>=0.f||TargetLocation.ContainsNaN()||!FMath::IsFinite(Radius)||Radius<0.f||!Source.IsValid()||!Battle.IsValid()||!Battle->IsCombatRunning()||Battle->GetRoundId()!=Round||
    !Corruption.IsValid()||Corruption->GetBossCastKind()!=EEGCorruptionBossCast::Mimic||Corruption->GetBossCastActionId()!=CastAction)return;
 CastAge=0.f;
 if(Cue==EEGCorruptionMimicCue::SelfShield&&!bShieldApplied)return;
 // NotifyCast is accepted only once for this Round/Action. The short local
 // voice is owned by this visual and stopped on cancellation or teardown.
 USoundBase* Sound=Cue==EEGCorruptionMimicCue::SelfShield?MimicShieldSound.Get():MimicImpactSound.Get();
 if(IsValid(Sound))
 {
  const auto* Settings=UEGGameAudioComponent::FindFor(this);const float Volume=Settings?Settings->GetUISoundVolume():1.f;
  const FVector SoundPoint=Cue==EEGCorruptionMimicCue::AllTargets?GetActorLocation():TargetLocation;
  CastAudio=UGameplayStatics::SpawnSoundAtLocation(this,Sound,SoundPoint,FRotator::ZeroRotator,.16f*Volume);
 }
 if(Cue==EEGCorruptionMimicCue::AllTargets)
 {
  // The authority resolves this as a field-wide attack. Accent surviving
  // enemies individually rather than drawing a misleading single-target beam.
  if(auto* Combat=Source->GetCombat())for(auto* Target:Battle->Units())
   if(IsValid(Target)&&Combat->IsValidEnemy(Target))
    SpawnPhantomAccent(.55f,GetActorTransform().InverseTransformPosition(Target->GetActorLocation()),PhantomBurstSystem);
  return;
 }
 if(Cue==EEGCorruptionMimicCue::SelfShield)
  AEGMonsterEffectCueVisual::Spawn(Source.Get(),Source.Get(),Battle.Get(),Round,EEGMonsterEffectCue::Shield);
 if(Cue==EEGCorruptionMimicCue::Area)
 {
  const FVector Start=GetActorLocation()+FVector(0,0,80.f);
  FVector Delta=TargetLocation-Start;if(Delta.SizeSquared()<FMath::Square(120.f))Delta=FVector(0.f,0.f,120.f);
  const float Length=Delta.Size();
  CastBeam->SetWorldLocationAndRotation(Start+Delta*.5f,Delta.Rotation());
  CastBeam->SetWorldScale3D(FVector(Length/100.f,.12f,.12f));CastBeam->SetVisibility(true);
  if(Radius>0.f)
  {
   // Cosmetic flash consumes the radius supplied by the committed combat
   // result and remains fixed at that point; it never participates in damage.
   CastAreaRadius=Radius;
   CastAreaRing->SetWorldLocationAndRotation(TargetLocation-FVector(0,0,75.f),FRotator::ZeroRotator);
   CastAreaRing->SetWorldScale3D(FVector(Radius/50.f));CastAreaRing->SetVisibility(true);
  }
 }
 // SelfShield receives the boss position from authority; no damaging beam.
 const FVector HitOffset=GetActorTransform().InverseTransformPosition(TargetLocation+FVector(0,0,95.f));
 SpawnPhantomAccent(.9f,HitOffset);
 if(IsValid(PhantomBurstSystem))SpawnPhantomAccent(.7f,HitOffset,PhantomBurstSystem);
}
void AEGCorruptionPhantomVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Source.IsValid()||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round||
    !IEGCombatant::Execute_IsAlive(Source.Get())){Destroy();return;}
 const bool bRunning=Battle->IsCombatRunning();
 if(CastAudio.IsValid())CastAudio->SetPaused(!bRunning);
 for(const auto& Accent:PhantomAccents)if(IsValid(Accent)&&Accent->IsPaused()==bRunning)Accent->SetPaused(!bRunning);
 if(!bRunning)return;
 auto* Component=Corruption.Get();
 if(!Component||(CastAge<0.f&&(Component->GetBossCastKind()!=EEGCorruptionBossCast::Mimic||Component->GetBossCastActionId()!=CastAction))){Destroy();return;}
 Age+=FMath::Max(0.f,DeltaSeconds);
 if(CastAge>=0.f){CastAge+=FMath::Max(0.f,DeltaSeconds);if(CastAge>=.2f)CastBeam->SetVisibility(false);}
 if(CastAreaRadius>0.f)
 {
  const float T=FMath::Clamp(CastAge/.65f,0.f,1.f);
  const float Scale=CastAreaRadius/50.f*FMath::Lerp(.55f,1.f,T);
  CastAreaRing->SetWorldScale3D(FVector(Scale,Scale,1.f));
  if(CastAreaRingMID)CastAreaRingMID->SetScalarParameterValue(TEXT("Opacity"),.85f*FMath::Clamp((1.f-T)/.35f,0.f,1.f));
  if(T>=1.f)CastAreaRing->SetVisibility(false);
 }
 constexpr float DissolveDuration=.85f;
 if(CastAge>=DissolveDuration){Destroy();return;}
 if(!bDissolveAccentStarted&&CastAge>=.55f)
 {bDissolveAccentStarted=true;SpawnPhantomAccent(.65f);}
 // Both source assemblies and the fallback body share the same reveal and fade.
 const float Reveal=FMath::Clamp(Age/.16f,0.f,1.f);
 const float Fade=CastAge<0.f?1.f:FMath::Clamp((DissolveDuration-CastAge)/.25f,0.f,1.f);
 if(PhantomMID){PhantomMID->SetScalarParameterValue(TEXT("Opacity"),.82f*Reveal*Fade);PhantomMID->SetScalarParameterValue(TEXT("Glow"),2.1f+Reveal*Fade*.4f);}
 if(IsValid(Assembly))
 {
  // Finish the authored follow-through after the authoritative hit rather
  // than jumping straight to a static terminal pose. This never delays damage.
  const float Progress=CastAge>=0.f?.7f+.3f*FMath::Clamp(CastAge/.65f,0.f,1.f):.7f*Component->GetBossCastProgress();
  const float Sample=FMath::Clamp(Progress,0.f,1.f)*MotionDuration;
  Assembly->SetSourceTime(Sample);
  if(Assembly->BodyMesh==Body)
  {
   Body->TickAnimation(FMath::Clamp(DeltaSeconds,0.f,.1f),false);
   Body->RefreshBoneTransforms();Body->UpdateBounds();
   Body->MarkRenderTransformDirty();Body->MarkRenderDynamicDataDirty();
   Assembly->SetSourceTime(Sample);
  }
  // The source rig can change meshes/material bindings while seeking. Apply
  // the ghost appearance afterwards to every generated body and weapon part.
  if(PhantomMID)
  {
   TArray<USkeletalMeshComponent*> Parts;Assembly->GetComponents<USkeletalMeshComponent>(Parts);
   for(auto* Part:Parts)if(IsValid(Part))
    for(int32 Slot=0;Slot<Part->GetNumMaterials();++Slot)Part->SetMaterial(Slot,PhantomMID);
  }
 }
}
void AEGCorruptionPhantomVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 if(Corruption.IsValid())Corruption->OnBossCastEvent.Remove(CastDelegate);
 if(CastAudio.IsValid())CastAudio->Stop();CastAudio.Reset();
 CastDelegate.Reset();
 for(const auto& Accent:PhantomAccents)if(IsValid(Accent)){Accent->DeactivateImmediate();Accent->DestroyComponent();}
 PhantomAccents.Reset();
 if(IsValid(Assembly))Assembly->Destroy();
 Assembly=nullptr;
 FEGBoardModelSizing::Forget(this);
 Source.Reset();Battle.Reset();Corruption.Reset();CastAction.Invalidate();Round.Invalidate();Super::EndPlay(Reason);
}
