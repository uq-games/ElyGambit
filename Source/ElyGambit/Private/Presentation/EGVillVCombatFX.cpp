#include "Presentation/EGVillVCombatFX.h"
#include "EGVillVFXsFrame.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

AEGVillVCombatFX::AEGVillVCombatFX()
{
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickGroup=TG_PostPhysics;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 SetActorEnableCollision(false);bReplicates=false;
}
bool AEGVillVCombatFX::IsCurrent() const
{
 return Skill.IsValid()&&Pilot.IsValid()&&Device.IsValid()&&Battle.IsValid()
  &&Battle->IsBattleActive()&&Battle->GetRoundId()==Round
  &&IEGCombatant::Execute_IsAlive(Pilot.Get())&&IEGCombatant::Execute_IsAlive(Device.Get());
}
AEGVillVCombatFX* AEGVillVCombatFX::Spawn(UEGHeroSkillComponent* InSkill,AEGVillVEmplacement* InDevice,UEGVillVCombatFXDefinition* InDefinition,int32 Mode,bool Enhanced,EEGVillVCombatFXStage InStage,const FVector& Point)
{
 if(!IsValid(InSkill)||!IsValid(InDevice)||!IsValid(InDefinition)||!InSkill->Battle.IsValid()
  ||!InSkill->Battle->IsBattleActive()||!InSkill->GetWorld()||InSkill->GetWorld()->GetNetMode()==NM_DedicatedServer
  ||Mode<0||Mode>1||Point.ContainsNaN())return nullptr;
 const int32 Index=Mode*2+(Enhanced?1:0);if(!InDefinition->Presets.IsValidIndex(Index))return nullptr;
 const auto& Preset=InDefinition->Presets[Index];
 const auto& Selected=InStage==EEGVillVCombatFXStage::Charge?Preset.Charge:InStage==EEGVillVCombatFXStage::Release?Preset.Release:InStage==EEGVillVCombatFXStage::SecondaryImpact?Preset.SecondaryImpact:Preset.Shot;
 if(Selected.IsEmpty())return nullptr;
 FTransform Muzzle;if(!InDevice->MuzzleFrame(Muzzle))return nullptr;
 const FVector Origin=Muzzle.GetLocation();
 // Validate the whole carrier before allocating any render components.
 for(const auto& Layer:Selected)
 {
  if(!IsValid(Layer.System)||(!Layer.bNativeSimulation&&(!IsValid(Layer.Samples)||Layer.Samples->Tracks.IsEmpty()))
   ||!FMath::IsFinite(Layer.StartTime)||Layer.StartTime<0||!FMath::IsFinite(Layer.EffectScale)||Layer.EffectScale<=0
   ||!FMath::IsFinite(Layer.FlightDuration)||Layer.FlightDuration<=0
   ||(Layer.bNativeSimulation&&(!FMath::IsFinite(Layer.NativeDuration)||Layer.NativeDuration<=0)))return nullptr;
 }
 FActorSpawnParameters Params;Params.Owner=InSkill->GetOwner();Params.ObjectFlags|=RF_Transient;
 auto* FX=InSkill->GetWorld()->SpawnActor<AEGVillVCombatFX>(Params);if(!FX)return nullptr;
 FX->Skill=InSkill;FX->Pilot=Cast<AEGChessUnit>(InSkill->GetOwner());FX->Device=InDevice;
 FX->Battle=InSkill->Battle;FX->Round=InSkill->Battle->GetRoundId();FX->Action=InSkill->ActionId;
 FX->Definition=InDefinition;FX->EffectLayers=Selected;FX->Stage=InStage;FX->PresetIndex=Index;FX->ImpactPoint=Point;FX->ShotOrigin=Origin;
 FX->Launch=Muzzle;
 FX->Components.SetNum(Selected.Num());
 for(const auto& Layer:Selected)
  if(Layer.bNativeSimulation)FX->Duration=FMath::Max(FX->Duration,Layer.StartTime+Layer.NativeDuration);
  else for(const auto& Track:Layer.Samples->Tracks)FX->Duration=FMath::Max(FX->Duration,Layer.StartTime+Track.StartTime+Track.Duration);
 FX->AddTickPrerequisiteComponent(InSkill);FX->AddTickPrerequisiteActor(InDevice);
 if(!FX->IsCurrent()){FX->Destroy();return nullptr;}
 // Charging is driven by the actual sampled montage clock, including live speed changes.
 if(InStage!=EEGVillVCombatFXStage::Charge)FX->Evaluate();
 return FX;
}
void AEGVillVCombatFX::SampleAction(float Position)
{
 if(Stage!=EEGVillVCombatFXStage::Charge||!FMath::IsFinite(Position))return;
 if(!IsCurrent()||Skill->ActionId!=Action){Destroy();return;}
 SourceTime=FMath::Max(0.f,Position);Evaluate();
}
void AEGVillVCombatFX::Tick(float Dt)
{
 Super::Tick(Dt);
 if(!IsCurrent()||(Stage==EEGVillVCombatFXStage::Charge&&Skill->ActionId!=Action)){Destroy();return;}
 if(Stage==EEGVillVCombatFXStage::Charge)return;
 // Fired particles run on source seconds. Gameplay damage already settled at
 // the fire notify; projectile travel never authorizes or delays damage.
 SourceTime+=FMath::Max(0.f,Dt);
 if(SourceTime>Duration+.05f){Destroy();return;}
 Evaluate();
}
void AEGVillVCombatFX::Evaluate()
{
 VisibleLayers=0;if(!IsCurrent())return;
 FTransform Muzzle=Launch;
 if(Stage==EEGVillVCombatFXStage::Charge)
 {
  if(!Device->MuzzleFrame(Muzzle))return;
 }
 // A visual-only shell follows the same source clock and path as the projectile layers.
 // It cannot collide or apply damage; existing gameplay has already resolved the hit.
 if(Stage==EEGVillVCombatFXStage::Shot && PresetIndex>=2 && IsValid(Definition) && IsValid(Definition->CannonProjectileMesh))
 {
  const FEGVillVFXsLayer* Projectile=EffectLayers.FindByPredicate([](const FEGVillVFXsLayer& L){return L.Anchor==EEGVillVFXsAnchor::Projectile;});
  const float Age=Projectile?SourceTime-Projectile->StartTime:-1.f;
  if(Projectile && Age>=0.f && Age<Projectile->FlightDuration)
  {
   if(!IsValid(CannonProjectileComponent))
   {
    CannonProjectileComponent=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient);
    AddInstanceComponent(CannonProjectileComponent);CannonProjectileComponent->SetupAttachment(GetRootComponent());
    CannonProjectileComponent->SetMobility(EComponentMobility::Movable);CannonProjectileComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    CannonProjectileComponent->SetStaticMesh(Definition->CannonProjectileMesh);
    if(IsValid(Definition->CannonProjectileMaterial))CannonProjectileComponent->SetMaterial(0,Definition->CannonProjectileMaterial);
    CannonProjectileComponent->RegisterComponent();
   }
   const FVector Direction=(ImpactPoint-ShotOrigin).GetSafeNormal();
   const FVector Extent=Definition->CannonProjectileMesh->GetBounds().BoxExtent;
   const float Longest=FMath::Max3(Extent.X,Extent.Y,Extent.Z);
   const FRotator Rotation=Extent.X>=Extent.Y&&Extent.X>=Extent.Z?FRotationMatrix::MakeFromX(Direction).Rotator():Extent.Y>=Extent.Z?FRotationMatrix::MakeFromY(Direction).Rotator():FRotationMatrix::MakeFromZ(Direction).Rotator();
   CannonProjectileComponent->SetWorldLocationAndRotation(FMath::Lerp(ShotOrigin,ImpactPoint,FMath::Clamp(Age/Projectile->FlightDuration,0.f,1.f)),Rotation);
   CannonProjectileComponent->SetWorldScale3D(FVector(Definition->CannonProjectileLength/FMath::Max(2.f*Longest,1.f)));
   CannonProjectileComponent->SetVisibility(true);
  }
  else if(IsValid(CannonProjectileComponent))CannonProjectileComponent->SetVisibility(false);
 }
 FVector CameraLocation=ShotOrigin+FVector(500,-1000,600);FRotator ViewRotation=(ImpactPoint-CameraLocation).Rotation();
 if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0)){CameraLocation=Camera->GetCameraLocation();ViewRotation=Camera->GetCameraRotation();}
 for(int32 I=0;I<EffectLayers.Num();++I)
 {
  const auto& Layer=EffectLayers[I];const float Age=SourceTime-Layer.StartTime;float Last=Layer.bNativeSimulation?Layer.NativeDuration:0.f;
  if(!Layer.bNativeSimulation)for(const auto& Track:Layer.Samples->Tracks)Last=FMath::Max(Last,Track.StartTime+Track.Duration);
  auto& Component=Components[I];
  if(Age<0||Age>Last){if(IsValid(Component))Component->SetVisibility(false);continue;}
  const bool bNewComponent=!IsValid(Component);
  if(bNewComponent)
  {
   Component=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient);
   AddInstanceComponent(Component);Component->SetAutoActivate(false);Component->SetAsset(Layer.System);
   Component->SetAutoDestroy(false);Component->SetUsingAbsoluteLocation(true);Component->RegisterComponent();
  }
  FTransform Anchor=Muzzle;
  if(Layer.Anchor==EEGVillVFXsAnchor::Target){Anchor.SetLocation(ImpactPoint);Anchor.SetRotation(FQuat::Identity);}
  else if(Layer.Anchor==EEGVillVFXsAnchor::Projectile)
  {
   Anchor.SetLocation(FMath::Lerp(ShotOrigin,ImpactPoint,FMath::Clamp(Age/Layer.FlightDuration,0.f,1.f)));
   const FVector Direction=(ImpactPoint-ShotOrigin).GetSafeNormal();
   if(!Direction.IsNearlyZero())Anchor.SetRotation(FRotationMatrix::MakeFromYZ(Direction,FVector::UpVector).ToQuat());
  }
  if(Layer.bNativeSimulation)
  {
   Component->SetWorldLocationAndRotation(Anchor.GetLocation(),Anchor.GetRotation());
   Component->SetWorldScale3D(FVector(Layer.EffectScale));
   // ToonProjectiles2 drives particle dimensions with its own User.Scale.
   if(Layer.System->GetName().StartsWith(TEXT("NS_ToonCannon")))Component->SetVariableFloat(TEXT("User.Scale"),Layer.EffectScale);
   if(bNewComponent)Component->Activate(true);
   Component->SetVisibility(true);
  }
  else EGEvaluateVillVFXsFrame(Layer,Component,Age,Anchor,Muzzle,ImpactPoint,Layer.EffectScale,CameraLocation,ViewRotation,BirthAnchors,I*100000);
  ++VisibleLayers;
 }
}
void AEGVillVCombatFX::ClearComponents()
{
 for(UNiagaraComponent* C:Components)if(IsValid(C)){C->DeactivateImmediate();C->DestroyComponent();}
 Components.Reset();if(IsValid(CannonProjectileComponent)){CannonProjectileComponent->DestroyComponent();CannonProjectileComponent=nullptr;}BirthAnchors.Reset();VisibleLayers=0;
}
void AEGVillVCombatFX::EndPlay(const EEndPlayReason::Type Reason){ClearComponents();Super::EndPlay(Reason);}
void AEGVillVCombatFX::Destroyed(){ClearComponents();Super::Destroyed();}
