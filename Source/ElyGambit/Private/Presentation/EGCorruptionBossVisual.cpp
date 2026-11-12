#include "Presentation/EGCorruptionBossVisual.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Skills/EGCorruptionBossBehavior.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AEGCorruptionBossVisual::AEGCorruptionBossVisual()
{
 PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.bTickEvenWhenPaused=true; bReplicates=false; SetActorEnableCollision(false);
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 // Same source mesh already used by the existing C1 crystal impact system.
 static ConstructorHelpers::FObjectFinder<UStaticMesh> SweepFragmentSource(TEXT("/Game/Assets/FXs/CreystalPack/StaticMesh/SM_Crystal_01"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> CrystalSource(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/MI_CorruptionCrystal"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingSource(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShardSource(TEXT("/Game/Assets/Environments/Board/Materials/M_ChessBoardPerimeter"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> BodyOverlaySource(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionBossOverlay"));
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SpreadImpactSource(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/NS_CorruptionSpreadImpact"));
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> AreaBurstSource(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Niagara/NS_CorruptionAreaBurst"));
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> SweepWaveSource(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Niagara/NS_CorruptionBrokenWavefront"));
 static ConstructorHelpers::FObjectFinder<UEGSourcePreviewDefinition> CastSource(TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/Assemblies/DA_Corruption_SKL02"));
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> WarningSound(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundWave/SW_Whoosh.SW_Whoosh"));
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> ImpactSound(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundWave/SW_impact_star.SW_impact_star"));
 BodyOverlayBase=BodyOverlaySource.Object;
 SpreadImpactSystem=SpreadImpactSource.Object;
 AreaBurstSystem=AreaBurstSource.Object;
 SweepWaveSystem=SweepWaveSource.Object;
 SweepFragmentMesh=SweepFragmentSource.Object;
 CrystalMaterial=CrystalSource.Object;
 CastAssembly=CastSource.Object;
 PulseWarningSound=WarningSound.Get();PulseImpactSound=ImpactSound.Get();
 auto Make=[this](const TCHAR* Name,UStaticMesh* Mesh,UMaterialInterface* Material)
 {
  auto* Component=CreateDefaultSubobject<UStaticMeshComponent>(Name);Component->SetupAttachment(GetRootComponent());
  Component->SetStaticMesh(Mesh);if(Material)Component->SetMaterial(0,Material);
  Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);Component->SetGenerateOverlapEvents(false);
  Component->SetCanEverAffectNavigation(false);Component->SetCastShadow(false);Component->SetVisibility(false);return Component;
 };
 Ring=Make(TEXT("CorruptionRing"),Plane.Object,RingSource.Object);
 InnerRing=Make(TEXT("CorruptionInnerRing"),Plane.Object,RingSource.Object);
 for(int32 Index=0;Index<8;++Index)Shards.Add(Make(*FString::Printf(TEXT("CorruptionShard%02d"),Index),Cube.Object,ShardSource.Object));
}

AEGCorruptionBossVisual* AEGCorruptionBossVisual::SpawnForBoss(AEGChessUnit* Boss)
{
 if(!IsValid(Boss)||!Boss->GetWorld()||Boss->GetNetMode()==NM_DedicatedServer)return nullptr;
 auto* Context=Boss->GetHeroSkill()?Boss->GetHeroSkill()->Battle.Get():nullptr;
 if(!IsValid(Context)||!Context->IsBattleActive())return nullptr;
 auto* Visual=Boss->GetWorld()->SpawnActor<AEGCorruptionBossVisual>();if(!Visual)return nullptr;
 Visual->SetOwner(Boss);Visual->Source=Boss;Visual->Battle=Context;Visual->Round=Context->GetRoundId();
 if(auto* Component=Boss->FindComponentByClass<UEGCorruptionCombatComponent>())
 {
  Visual->Corruption=Component;
  Visual->CastDelegate=Component->OnBossCastEvent.AddUObject(Visual,&ThisClass::HandleBossCast);
 }
 if(auto* Skill=Boss->GetHeroSkill())if(auto* Behavior=Cast<UEGCorruptionBossBehavior>(Skill->GetBehavior()))
 {
  Visual->FirstPulseBehavior=Behavior;
  Visual->FirstPulseDelegate=Behavior->OnFirstPulse.AddUObject(Visual,&ThisClass::HandleFirstPulse);
 }
 Visual->LastRealSeconds=FPlatformTime::Seconds();
 Visual->RingMaterial=Visual->Ring->CreateDynamicMaterialInstance(0);
 if(auto* Base=Visual->Shards[0]->GetMaterial(0))Visual->DimShardMaterial=UMaterialInstanceDynamic::Create(Base,Visual);
 Visual->ShardMaterial=Visual->Shards[0]->CreateDynamicMaterialInstance(0);
 if(Visual->DimShardMaterial)Visual->DimShardMaterial->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.12f,.015f,.2f,1.f));
 if(Visual->RingMaterial)Visual->InnerRing->SetMaterial(0,Visual->RingMaterial);
 if(Visual->ShardMaterial)for(const auto& Part:Visual->Shards)if(Part)Part->SetMaterial(0,Visual->ShardMaterial);
 Visual->SetTint(FLinearColor(.75f,.06f,1.f,1.f));return Visual;
}

void AEGCorruptionBossVisual::ReleaseCastPresentation()
{
 if(CastAction.IsValid())if(auto* Unit=Source.Get())if(auto* Skill=Unit->GetHeroSkill())Skill->EndBarrierTransition(Round,CastAction);
 CastAction.Invalidate();CastSourceDuration=0.f;
}
void AEGCorruptionBossVisual::HandleBossCast(EEGCorruptionBossCast Cast,EEGCorruptionBossCastEvent Event,FGuid Action,float CastDuration)
{
 if(Kind!=EVisualKind::Controller||!Action.IsValid())return;
 if(Event!=EEGCorruptionBossCastEvent::Started)
 {
  if(Action==CastAction)ReleaseCastPresentation();
  return;
 }
 if(!IsCurrent()||!Battle->IsCombatRunning()||!FMath::IsFinite(CastDuration)||CastDuration<=0.f||
    (Cast!=EEGCorruptionBossCast::Spread&&Cast!=EEGCorruptionBossCast::Mimic))return;
 ReleaseCastPresentation();
 auto* Unit=Source.Get();auto* Skill=Unit?Unit->GetHeroSkill():nullptr;
 float SourceDuration=0.f;
 if(Skill&&CastAssembly&&Skill->BeginBarrierTransition(CastAssembly,Round,Action,SourceDuration))
 {CastAction=Action;CastSourceDuration=FMath::Min(SourceDuration,1.2f);}
}
void AEGCorruptionBossVisual::HandleFirstPulse(EEGCorruptionFirstPulseEvent Event,FGuid EventRound,FGuid Action,FVector Point,float InRadius,float InDuration)
{
 if(Kind!=EVisualKind::Controller||!Action.IsValid()||EventRound!=Round)return;
 if(Event==EEGCorruptionFirstPulseEvent::Started)
 {
  if(!IsCurrent()||!Battle->IsCombatRunning()||Point.ContainsNaN()||!FMath::IsFinite(InRadius)||InRadius<=0.f||
     !FMath::IsFinite(InDuration)||InDuration<=0.f)return;
  if(FirstPulseWarning.IsValid())FirstPulseWarning->Destroy();
  if(FirstPulseAudio.IsValid())FirstPulseAudio->Stop();
  FirstPulseAction=Action;
  FirstPulseWarning=SpawnEffect(EVisualKind::SpreadWarning,Point,InRadius,InDuration);
  const auto* Settings=UEGGameAudioComponent::FindFor(this);const float Volume=Settings?Settings->GetUISoundVolume():1.f;
  if(IsValid(PulseWarningSound))FirstPulseAudio=UGameplayStatics::SpawnSoundAtLocation(this,PulseWarningSound,Point,FRotator::ZeroRotator,.15f*Volume);
  return;
 }
 if(Action!=FirstPulseAction)return;
 if(FirstPulseWarning.IsValid())FirstPulseWarning->Destroy();
 FirstPulseWarning.Reset();FirstPulseAction.Invalidate();
 if(FirstPulseAudio.IsValid())FirstPulseAudio->Stop();FirstPulseAudio.Reset();
 if(Event==EEGCorruptionFirstPulseEvent::Resolved&&IsCurrent()&&Battle->IsCombatRunning()&&
    !Point.ContainsNaN()&&FMath::IsFinite(InRadius)&&InRadius>0.f)
 {
  if(auto* Effect=SpawnEffect(EVisualKind::PulseBurst,Point,InRadius,.8f))
  {Effect->AttachAccent(SpreadImpactSystem,FMath::Clamp(InRadius/300.f,.5f,1.f),90.f);Effect->AttachCrystalFragments(8);}
  const auto* Settings=UEGGameAudioComponent::FindFor(this);const float Volume=Settings?Settings->GetUISoundVolume():1.f;
  if(IsValid(PulseImpactSound))FirstPulseAudio=UGameplayStatics::SpawnSoundAtLocation(this,PulseImpactSound,Point,FRotator::ZeroRotator,.2f*Volume);
 }
}

bool AEGCorruptionBossVisual::IsCurrent() const
{
 return Source.IsValid()&&Battle.IsValid()&&Battle->IsBattleActive()&&Battle->GetRoundId()==Round&&
  IEGCombatant::Execute_IsAlive(Source.Get())&&(Kind==EVisualKind::Controller||ParentVisual.IsValid());
}
void AEGCorruptionBossVisual::SetTint(FLinearColor Color)
{
 if(RingMaterial)RingMaterial->SetVectorParameterValue(TEXT("Tint"),Color);
 if(ShardMaterial)ShardMaterial->SetVectorParameterValue(TEXT("Tint"),Color);
}
void AEGCorruptionBossVisual::RestoreBodyOverlay()
{
 if(auto* Mesh=OverlayMesh.Get())if(Mesh->GetOverlayMaterial()==BodyOverlay)Mesh->SetOverlayMaterial(PreviousBodyOverlay);
 OverlayMesh.Reset();PreviousBodyOverlay=nullptr;
}
void AEGCorruptionBossVisual::RefreshBodyOverlay()
{
 if(Kind!=EVisualKind::Controller)return;
 USkeletalMeshComponent* Current=bSecondPhase&&Source.IsValid()?Source->GetVisiblePresentationMesh():nullptr;
 if(Current!=OverlayMesh.Get()){RestoreBodyOverlay();bOverlayRelinquished=false;}
 if(!Current||!bSecondPhase||bOverlayRelinquished)return;
 if(OverlayMesh.IsValid())
 {
  // A different presentation system took ownership; never overwrite it.
  if(Current->GetOverlayMaterial()!=BodyOverlay){PreviousBodyOverlay=nullptr;bOverlayRelinquished=true;}
  return;
 }
 if(!BodyOverlay&&!bOverlayLoadAttempted)
 {
  bOverlayLoadAttempted=true;
  if(BodyOverlayBase){BodyOverlay=UMaterialInstanceDynamic::Create(BodyOverlayBase,this);if(BodyOverlay){BodyOverlay->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.5f,.025f,.9f,1.f));BodyOverlay->SetScalarParameterValue(TEXT("Opacity"),.5f);BodyOverlay->SetScalarParameterValue(TEXT("Intensity"),1.8f);}}
 }
 if(BodyOverlay){PreviousBodyOverlay=Current->GetOverlayMaterial();OverlayMesh=Current;Current->SetOverlayMaterial(BodyOverlay);}
}
void AEGCorruptionBossVisual::SetSecondPhase(bool bEnabled)
{
 const bool bEntering=bEnabled&&!bSecondPhase;bSecondPhase=bEnabled;RefreshBodyOverlay();
 if(bEntering&&Source.IsValid())if(auto* Effect=SpawnEffect(EVisualKind::PhaseTransition,Source->GetActorLocation(),140.f,1.f))
 {Effect->AttachAccent(SpreadImpactSystem,1.f,120.f);Effect->AttachAccent(AreaBurstSystem,1.5f,120.f);Effect->AttachCrystalFragments(12);}
 Tick(0.f);
}
void AEGCorruptionBossVisual::SetEnraged(bool bEnabled)
{
 const bool bEntering=bEnabled&&!bEnraged;bEnraged=bEnabled;
 if(bEntering&&Source.IsValid())if(auto* Effect=SpawnEffect(EVisualKind::EnrageAccent,Source->GetActorLocation(),95.f,.65f))Effect->AttachAccent(SpreadImpactSystem,.55f);
}

AEGCorruptionBossVisual* AEGCorruptionBossVisual::SpawnEffect(EVisualKind EffectKind,FVector Location,float InRadius,float InDuration,AEGChessUnit* Target)
{
 if(Kind!=EVisualKind::Controller||!IsCurrent()||Location.ContainsNaN()||!FMath::IsFinite(InRadius)||InRadius<=0.f||!FMath::IsFinite(InDuration)||InDuration<=0.f)return nullptr;
 auto* Effect=SpawnForBoss(Source.Get());if(!Effect)return nullptr;
 if(Effect->Corruption.IsValid())Effect->Corruption->OnBossCastEvent.Remove(Effect->CastDelegate);
 Effect->CastDelegate.Reset();Effect->Corruption.Reset();
 if(Effect->FirstPulseBehavior.IsValid())Effect->FirstPulseBehavior->OnFirstPulse.Remove(Effect->FirstPulseDelegate);
 Effect->FirstPulseDelegate.Reset();Effect->FirstPulseBehavior.Reset();
 Effect->Kind=EffectKind;Effect->ParentVisual=this;Effect->FollowTarget=Target;Effect->Radius=InRadius;Effect->Duration=InDuration;
 Effect->SetActorLocation(Location-FVector(0.f,0.f,80.f));Effect->SweepOrigin=Effect->GetActorLocation();Children.Add(Effect);Effect->Tick(0.f);return Effect;
}
void AEGCorruptionBossVisual::ShowMark(AEGChessUnit* Target,int32 Stacks,int32 Threshold)
{
 if(!IsValid(Target))return;
 const TWeakObjectPtr<AEGChessUnit> Key(Target);
 if(Stacks<=0){if(auto* Existing=Marks.Find(Key))if(Existing->IsValid())Existing->Get()->Destroy();Marks.Remove(Key);return;}
 AEGCorruptionBossVisual* Mark=nullptr;if(auto* Existing=Marks.Find(Key))Mark=Existing->Get();
 if(!IsValid(Mark)){Mark=SpawnEffect(EVisualKind::Mark,Target->GetActorLocation(),72.f,3600.f,Target);if(Mark)Marks.Add(Key,Mark);}
 if(Mark){Mark->MarkCount=FMath::Clamp(Stacks,0,8);Mark->MarkThreshold=FMath::Clamp(Threshold,1,8);Mark->MarkFraction=FMath::Clamp(float(Stacks)/float(FMath::Max(1,Threshold)),0.f,1.f);Mark->Tick(0.f);}
}
void AEGCorruptionBossVisual::AttachAccent(UNiagaraSystem* System,float Scale,float Height)
{
 if(!IsValid(System)||!GetWorld()||GetNetMode()==NM_DedicatedServer||!FMath::IsFinite(Scale)||Scale<=0.f||!FMath::IsFinite(Height))return;
 auto* Particle=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);if(!Particle)return;
 AddInstanceComponent(Particle);Particle->SetupAttachment(GetRootComponent());Particle->SetRelativeLocation(FVector(0,0,Height));
 Particle->SetRelativeScale3D(FVector(Scale));Particle->SetAsset(System);Particle->SetAutoActivate(false);Particle->SetAutoDestroy(false);
 Particle->RegisterComponent();Particle->Activate(true);SweepParticles.Add(Particle);
}
void AEGCorruptionBossVisual::AttachCrystalFragments(int32 Count)
{
 if(!IsValid(SweepFragmentMesh))return;
 for(int32 Index=0;Index<FMath::Clamp(Count,0,24);++Index)
 {
  auto* Fragment=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
  if(!Fragment)continue;
  AddInstanceComponent(Fragment);Fragment->SetupAttachment(GetRootComponent());Fragment->SetStaticMesh(SweepFragmentMesh);
  if(CrystalMaterial)for(int32 Slot=0;Slot<SweepFragmentMesh->GetStaticMaterials().Num();++Slot)Fragment->SetMaterial(Slot,CrystalMaterial);
  Fragment->SetCollisionEnabled(ECollisionEnabled::NoCollision);Fragment->SetGenerateOverlapEvents(false);
  Fragment->SetCanEverAffectNavigation(false);Fragment->SetCastShadow(false);Fragment->SetVisibility(false);
  Fragment->RegisterComponent();SweepFragments.Add(Fragment);
 }
}
void AEGCorruptionBossVisual::ShowMarkBurst(FVector Location,float InRadius)
{
 if(auto* Effect=SpawnEffect(EVisualKind::MarkBurst,Location,InRadius,.75f))
 {Effect->AttachAccent(SpreadImpactSystem,.8f,100.f);Effect->AttachAccent(AreaBurstSystem,.65f,100.f);Effect->AttachCrystalFragments(12);}
}
void AEGCorruptionBossVisual::ShowSpreadWarning(FVector Location,float InRadius,float InDuration)
{
 ClearSpreadWarning();SpreadWarning=SpawnEffect(EVisualKind::SpreadWarning,Location,InRadius,InDuration);
}
void AEGCorruptionBossVisual::ClearSpreadWarning(){if(SpreadWarning.IsValid())SpreadWarning->Destroy();SpreadWarning.Reset();}
void AEGCorruptionBossVisual::ShowSpreadBurst(FVector Location,float InRadius)
{
 ClearSpreadWarning();auto* Effect=SpawnEffect(EVisualKind::SpreadBurst,Location,InRadius,.75f);if(!Effect)return;
 // The private impact is board-space; do not inherit the player's cue scale
 // or offset. Its visual footprint never changes the authoritative radius.
 Effect->AttachAccent(SpreadImpactSystem,FMath::Clamp(InRadius/300.f,.65f,1.2f),115.f);
 Effect->AttachAccent(AreaBurstSystem,FMath::Min(InRadius/350.f,1.2f),115.f);
 Effect->AttachCrystalFragments(12);
}
void AEGCorruptionBossVisual::ShowBarrierFailure(FVector WallCenter,float WallHalfWidth)
{
 auto* Effect=SpawnEffect(EVisualKind::BarrierFailure,WallCenter+FVector(0,0,80.f),WallHalfWidth,1.1f);if(!Effect)return;
 Effect->SweepEnd=Effect->SweepOrigin;
 if(const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>())
  if(const auto* Board=Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr)
  {
   const FTransform Basis=Board->GetActorTransform();
   FVector LocalEnd=Basis.InverseTransformPosition(Effect->SweepOrigin);
   LocalEnd.Y=FMath::Min(LocalEnd.Y,-.5f*Board->Rows*Board->CellDepth);
   Effect->SweepEnd=Basis.TransformPosition(LocalEnd);
   Effect->SetActorRotation(Board->GetActorRotation());
  }
 // These are scoped to the failure effect; the eight existing mark/skill
 // shards remain untouched for every other presentation kind.
 Effect->AttachCrystalFragments(24);
 // The private system distributes its particles across the actual wall width.
 // Keep sprite sizes in world centimetres rather than stretching their X axis.
 if(SweepWaveSystem)
 {
  auto* Particle=NewObject<UNiagaraComponent>(Effect,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
  if(Particle)
  {
   Effect->AddInstanceComponent(Particle);Particle->SetupAttachment(Effect->GetRootComponent());
   Particle->SetAsset(SweepWaveSystem);Particle->SetAutoActivate(false);Particle->SetAutoDestroy(false);
   Particle->RegisterComponent();
   Particle->SetVariableFloat(TEXT("User.WaveHalfWidth"),WallHalfWidth);
   Particle->Activate(true);Effect->SweepParticles.Add(Particle);
  }
 }
 Effect->Tick(0.f);
}

void AEGCorruptionBossVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);if(!IsCurrent()){Destroy();return;}
 if(FirstPulseAudio.IsValid())FirstPulseAudio->SetPaused(!Battle->IsCombatRunning()||(GetWorld()&&GetWorld()->IsPaused()));
 if(Kind==EVisualKind::Controller)RefreshBodyOverlay();
 if(Kind==EVisualKind::Controller&&CastAction.IsValid())
 {
  auto* Component=Corruption.Get();auto* Skill=Source.IsValid()?Source->GetHeroSkill():nullptr;
  if(!Component||!Skill||Component->GetBossCastActionId()!=CastAction)ReleaseCastPresentation();
  else if(Battle->IsCombatRunning()&&!Skill->SampleBarrierTransition(Round,CastAction,Component->GetBossCastProgress()*CastSourceDuration))ReleaseCastPresentation();
 }
 const double Now=FPlatformTime::Seconds();
 float VisualDelta=0.f;
 if(Battle->CanAdvancePresentationVisuals())
 {
  VisualDelta=GetWorld()&&GetWorld()->IsPaused()?static_cast<float>(FMath::Clamp(Now-LastRealSeconds,0.0,.1)):FMath::Max(0.f,DeltaSeconds);
  Age+=VisualDelta;
 }
 LastRealSeconds=Now;
 const bool bAdvanceParticles=Battle->CanAdvancePresentationVisuals();
 for(const auto& Particle:SweepParticles)if(IsValid(Particle)&&Particle->IsPaused()==bAdvanceParticles)Particle->SetPaused(!bAdvanceParticles);
 if(Kind!=EVisualKind::Controller&&Kind!=EVisualKind::Mark&&Age>=Duration){Destroy();return;}
 if(Kind==EVisualKind::Mark&&(!FollowTarget.IsValid()||!IEGCombatant::Execute_IsAlive(FollowTarget.Get()))){Destroy();return;}
 if(Kind==EVisualKind::Controller)SetActorLocation(Source->GetActorLocation()-FVector(0,0,80));
 else if(Kind==EVisualKind::Mark)SetActorLocation(FollowTarget->GetActorLocation()-FVector(0,0,80));
 const bool bController=Kind==EVisualKind::Controller;
 const bool bActive=!bController||bSecondPhase||bEnraged;
 const bool bPulseBurst=Kind==EVisualKind::PulseBurst;
 const bool bBurst=Kind==EVisualKind::MarkBurst||Kind==EVisualKind::SpreadBurst||bPulseBurst||Kind==EVisualKind::BarrierFailure||Kind==EVisualKind::PhaseTransition||Kind==EVisualKind::EnrageAccent;
 const float T=Duration>0.f?FMath::Clamp(Age/Duration,0.f,1.f):0.f;
 const float Extent=bController?85.f:Radius;
 if(Kind==EVisualKind::BarrierFailure)
 {
  SetActorLocation(FMath::Lerp(SweepOrigin,SweepEnd,FMath::Clamp(Age/.85f,0.f,1.f)));
  if(Age>=.85f&&!bSweepFinishing)
  {
   bSweepFinishing=true;
   for(const auto& Particle:SweepParticles)if(IsValid(Particle))Particle->Deactivate();
  }
 }
 // CPU particle accents use the same presentation gate as their visual owner.
 // Normally Niagara ticks itself; manually advance only during an owned pause.
 if(GetWorld()&&GetWorld()->IsPaused()&&VisualDelta>SMALL_NUMBER)
  for(const auto& Particle:SweepParticles)if(IsValid(Particle))Particle->AdvanceSimulation(1,VisualDelta);
 const float RingScale=Extent/50.f*(bBurst?FMath::Lerp(.35f,1.f,T):1.f);
 const bool bCrystalBurst=Kind==EVisualKind::MarkBurst||Kind==EVisualKind::SpreadBurst||bPulseBurst||Kind==EVisualKind::PhaseTransition;
 Ring->SetRelativeLocation(FVector(0,0,4));Ring->SetRelativeScale3D(FVector(RingScale,RingScale,1));Ring->SetVisibility(bActive&&Kind!=EVisualKind::BarrierFailure&&(!bCrystalBurst||bPulseBurst));
 const float InnerScale=RingScale*(Kind==EVisualKind::SpreadWarning?FMath::Lerp(.95f,.18f,T)*(1.f+.045f*FMath::Sin(Age*14.f)):.65f);
 InnerRing->SetRelativeLocation(FVector(0,0,6));InnerRing->SetRelativeScale3D(FVector(InnerScale,InnerScale,1));
 InnerRing->SetVisibility(bActive&&Kind!=EVisualKind::BarrierFailure&&!bCrystalBurst&&(Kind==EVisualKind::SpreadWarning||bBurst));
 if(Kind==EVisualKind::SpreadWarning&&RingMaterial)
 {
  RingMaterial->SetScalarParameterValue(TEXT("Glow"),1.f+.45f*FMath::Sin(Age*9.f));
  RingMaterial->SetScalarParameterValue(TEXT("Opacity"),.45f+.3f*T);
 }
 if(bPulseBurst&&RingMaterial)RingMaterial->SetScalarParameterValue(TEXT("Opacity"),.8f*FMath::Clamp((1.f-T)/.3f,0.f,1.f));
 SetTint(bEnraged?FLinearColor(1.5f,.03f,.65f,1.f):
  (Kind==EVisualKind::BarrierFailure?FLinearColor(.65f,.35f,1.6f,1.f):FLinearColor(.8f+.35f*MarkFraction,.04f,1.4f,1.f)));
 for(int32 I=0;I<Shards.Num();++I)
 {
  auto* Part=Shards[I].Get();if(!Part)continue;
  const float Angle=2.f*PI*float(I)/float(Shards.Num())+(bBurst?T:Age*.3f);
  const float Distance=Extent*(bBurst?FMath::Lerp(.25f,1.f,T):.7f);
  FVector Position(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,8.f+(bBurst?T*70.f:15.f));
  Part->SetRelativeLocation(Position);
  Part->SetRelativeRotation(FRotator(0.f,FMath::RadiansToDegrees(Angle),0.f));
  const float Fade=bBurst?1.f-T:1.f;
  Part->SetRelativeScale3D(FVector(.16f,.035f,.05f)*Fade);
  const bool bMarkSlot=Kind==EVisualKind::Mark&&I<MarkThreshold;
  if(bMarkSlot)
  {
   UMaterialInterface* SlotMaterial=I<MarkCount?ShardMaterial.Get():DimShardMaterial.Get();
   if(SlotMaterial&&Part->GetMaterial(0)!=SlotMaterial)Part->SetMaterial(0,SlotMaterial);
  }
  Part->SetVisibility(bActive&&Kind!=EVisualKind::BarrierFailure&&!bCrystalBurst&&(bController||bBurst||bMarkSlot));
 }
 if((Kind==EVisualKind::BarrierFailure||bCrystalBurst)&&SweepFragmentMesh)
 {
  const FVector MeshSize=SweepFragmentMesh->GetBounds().BoxExtent*2.f;
  const FVector SafeSize(FMath::Max(1.f,float(MeshSize.X)),FMath::Max(1.f,float(MeshSize.Y)),FMath::Max(1.f,float(MeshSize.Z)));
  const float Fade=Kind==EVisualKind::BarrierFailure?FMath::Clamp((1.1f-Age)/.25f,0.f,1.f):FMath::Clamp((1.f-T)/.35f,0.f,1.f);
  for(int32 I=0;I<SweepFragments.Num();++I)
  {
   auto* Fragment=SweepFragments[I].Get();if(!Fragment)continue;
   if(bCrystalBurst)
   {
    const float Angle=2.f*PI*(float(I)/float(FMath::Max(1,SweepFragments.Num())))+float(I%3)*.12f;
    const float Distance=FMath::Min(Extent,280.f)*(Kind==EVisualKind::MarkBurst?FMath::Lerp(.7f,.08f,T):FMath::Lerp(.12f,.85f,FMath::Sqrt(T)));
    const float Rise=Kind==EVisualKind::PhaseTransition?
     65.f+float((I*19)%90)+FMath::Sin(T*PI*.75f)*(70.f+float((I*17)%65)):
     65.f+FMath::Sin(T*PI*.75f)*(90.f+float((I*17)%45));
    const float Width=32.f+float((I*13)%24),Depth=16.f+float((I*7)%12),Height=75.f+float((I*19)%46);
    Fragment->SetRelativeLocation(FVector(FMath::Cos(Angle)*Distance,FMath::Sin(Angle)*Distance,Rise));
    Fragment->SetRelativeRotation(FRotator(-30.f+float((I*23)%60),FMath::RadiansToDegrees(Angle),20.f+float((I*37)%100)));
    Fragment->SetRelativeScale3D(FVector(Width/SafeSize.X,Depth/SafeSize.Y,Height/SafeSize.Z)*Fade);
    Fragment->SetVisibility(Fade>.01f);continue;
   }
   const float Lane=((float(I)+.5f)/24.f*2.f-1.f)*.94f;
   const float Edge=FMath::Lerp(1.f,.38f,FMath::Square(FMath::Abs(Lane)));
   const float Jitter=FMath::Sin(float(I)*7.13f);
   const float Phase=Age*3.f+float(I)*1.73f;
   const float X=FMath::Clamp(Lane+Jitter*.016f,-.96f,.96f)*Extent;
   const float Y=FMath::Sin(float(I)*2.7f)*24.f+FMath::Sin(Phase)*5.f;
   const float Z=FMath::Clamp(20.f+float((I*17)%61)+FMath::Sin(Phase)*3.f,20.f,80.f);
   const float Width=(24.f+float((I*13)%29))*Edge;
   const float Depth=9.f+float((I*7)%13);
   const float Height=(28.f+float((I*19)%41))*Edge;
   Fragment->SetRelativeLocation(FVector(X,Y,Z));
   Fragment->SetRelativeRotation(FRotator(-35.f+float((I*23)%70),float((I*47)%150)-75.f,20.f+float((I*37)%120)));
   Fragment->SetRelativeScale3D(FVector(Width/SafeSize.X,Depth/SafeSize.Y,Height/SafeSize.Z)*Fade);
   Fragment->SetVisibility(Fade>.01f);
  }
 }
 if(bController)
 {
  for(auto It=Marks.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Value().IsValid())It.RemoveCurrent();
  Children.RemoveAll([](const TWeakObjectPtr<AEGCorruptionBossVisual>& Child){return !Child.IsValid();});
 }
}
void AEGCorruptionBossVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 if(Corruption.IsValid())Corruption->OnBossCastEvent.Remove(CastDelegate);
 if(FirstPulseBehavior.IsValid())FirstPulseBehavior->OnFirstPulse.Remove(FirstPulseDelegate);
 if(FirstPulseAudio.IsValid())FirstPulseAudio->Stop();FirstPulseAudio.Reset();
 FirstPulseDelegate.Reset();FirstPulseBehavior.Reset();FirstPulseAction.Invalidate();FirstPulseWarning.Reset();
 CastDelegate.Reset();Corruption.Reset();ReleaseCastPresentation();
 for(const auto& Particle:SweepParticles)if(IsValid(Particle)){Particle->DeactivateImmediate();Particle->DestroyComponent();}
 SweepParticles.Reset();
 for(const auto& Fragment:SweepFragments)if(IsValid(Fragment))Fragment->DestroyComponent();
 SweepFragments.Reset();
 RestoreBodyOverlay();
 for(const auto& Child:Children)if(Child.IsValid())Child->Destroy();
 Children.Reset();Marks.Reset();SpreadWarning.Reset();ParentVisual.Reset();Source.Reset();FollowTarget.Reset();Battle.Reset();
 Super::EndPlay(Reason);
}
