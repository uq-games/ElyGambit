#include "Presentation/EGSettlementPresentation.h"

#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/SceneComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/AudioComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Sound/SoundBase.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGChessAnimInstance.h"
#include "Game/EGMatchManager.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGBoardMasterActionComponent.h"
#include "Actor/EGArtPiece.h"
#include "Game/EGDemoGameMode.h"

namespace
{
bool Compatible(const USkeletalMeshComponent* Mesh, const UAnimSequence* Clip)
{
 return Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetSkeletalMeshAsset()->GetSkeleton() && Clip &&
  Mesh->GetSkeletalMeshAsset()->GetSkeleton() == Clip->GetSkeleton();
}

// The temporary rig never borrows the live body: AEGCombatVisual restores a
// borrowed mesh unconditionally on EndPlay, even after an external form change.
bool CanProxySettlementBody(AEGChessUnit* Unit, USkeletalMeshComponent* Body, UAnimSequence* Clip,
 UEGSourcePreviewDefinition* Source, TArray<FEGSettlementHiddenComponent>& Weapons, int32& BodyIndex)
{
 if (!Compatible(Body, Clip) || !Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()) ||
     !Source || Source->Motions.IsEmpty() || !Body->IsVisible()) return false;
 const auto& Motion = Source->Motions[0];
 if (!FMath::IsFinite(Motion.Duration) || Motion.Duration < Clip->GetPlayLength() || Motion.Parts.IsEmpty()) return false;
 const FEGSourcePreviewPart* BodyPart = nullptr;
 BodyIndex = INDEX_NONE;
 for (int32 Index = 0; Index < Motion.Parts.Num(); ++Index)
 {
  const auto& Part = Motion.Parts[Index];
  if (!Part.Mesh || (Part.Animation && (!Part.Mesh->GetSkeleton() ||
      Part.Animation->GetSkeleton() != Part.Mesh->GetSkeleton() || !Part.AnimationBlueprintClass ||
      !Part.AnimationBlueprintClass->IsChildOf(UEGCombatPartAnimInstance::StaticClass())))) return false;
  if (Part.PartId == TEXT("Body") || Motion.Parts.Num() == 1)
  {
   if (BodyPart) return false;
   BodyPart = &Part;
   BodyIndex = Index;
  }
 }
 if (!BodyPart || BodyPart->Mesh != Body->GetSkeletalMeshAsset() || BodyPart->Animation != Clip ||
     !BodyPart->Transform.Equals(FTransform::Identity, .001f) || !FMath::IsNearlyZero(BodyPart->StartTime) ||
     BodyPart->bLoop || !BodyPart->bVisible || BodyPart->Visibility ||
     !BodyPart->ParentPartId.IsNone() || !BodyPart->AttachBone.IsNone() || BodyPart->bUseSourceRenderBindings ||
     BodyPart->NativePassDefinition || !BodyPart->SectionVisibility.IsEmpty() ||
     (BodyPart->Duration > 0.f && BodyPart->Duration < Clip->GetPlayLength())) return false;
 for (TActorIterator<AEGCombatVisual> It(Body->GetWorld()); It; ++It)
  if (IsValid(*It) && !It->IsActorBeingDestroyed() &&
      (It->BodyMesh == Body || It->GetOwner() == Unit || It->GetOwner() == Body->GetOwner())) return false;
 TArray<USceneComponent*> Children;
 Body->GetChildrenComponents(true, Children);
 for (USceneComponent* Child : Children)
 {
  if (!IsValid(Child) || !Cast<UPrimitiveComponent>(Child)) continue;
  if (!Child->ComponentHasTag(TEXT("EGWeapon"))) return false;
  FEGSettlementHiddenComponent& Saved = Weapons.AddDefaulted_GetRef();
  Saved.Component = Child;
  Saved.bWasVisible = Child->IsVisible();
 }
 return true;
}

void TryCreateSettlementProxy(AActor* Owner, AEGChessUnit* Unit, USkeletalMeshComponent* Original,
 UAnimSequence* Clip, UEGSourcePreviewDefinition* Source, FEGSettlementPose& Pose)
{
 if (!Source || !IsValid(Owner) || !IsValid(Unit) || !IsValid(Original)) return;
 int32 BodyIndex=INDEX_NONE;
 if (!CanProxySettlementBody(Unit,Original,Clip,Source,Pose.HiddenWeapons,BodyIndex))
 {
  Pose.HiddenWeapons.Reset();
  UE_LOG(LogTemp,Warning,TEXT("Settlement assembly skipped for %s: displayed rig cannot be safely proxied"),*GetNameSafe(Unit));
  return;
 }
 AEGCombatVisual* Visual=Owner->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),Original->GetComponentTransform(),Owner,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->IsPendingKillPending() || !IsValid(Unit) || Pose.Unit.Get()!=Unit || Pose.Body.Get()!=Original)
 {if(IsValid(Visual))Visual->Destroy();return;}
 if (!Visual){Pose.HiddenWeapons.Reset();return;}
 Pose.Assembly=Visual;
 Visual->Definition=Source;Visual->bAutoPlay=false;Visual->bLoop=false;
 Visual->FinishSpawning(Original->GetComponentTransform());
 if (!IsValid(Owner) || Owner->IsActorBeingDestroyed() || Owner->IsPendingKillPending() || !IsValid(Unit) || Pose.Unit.Get()!=Unit || Pose.Body.Get()!=Original || !IsValid(Visual))
 {if(IsValid(Visual))Visual->Destroy();return;}
 Visual->SetActorTickEnabled(false);Visual->SetSourceTime(0.f);
 TArray<USkeletalMeshComponent*> Parts;Visual->GetComponents(Parts);
 TArray<USkeletalMeshComponent*> IndexedParts;
 bool bComplete=true;
 const auto& Motion=Source->Motions[0];
 for(int32 Index=0;Index<Motion.Parts.Num();++Index)
 {
  USkeletalMeshComponent* Found=nullptr;
  const FName PartTag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),Index));
  for(USkeletalMeshComponent* Part:Parts)if(IsValid(Part)&&Part->ComponentHasTag(PartTag)){Found=Part;break;}
  IndexedParts.Add(Found);
  const auto& Entry=Motion.Parts[Index];
  if(!Found||Found->GetSkeletalMeshAsset()!=Entry.Mesh||
     (Entry.Animation&&!Cast<UEGCombatPartAnimInstance>(Found->GetAnimInstance())))bComplete=false;
  if(Found&&(!Entry.ParentPartId.IsNone()||!Entry.AttachBone.IsNone()))
  {
   int32 Parent=INDEX_NONE;
   for(int32 Previous=0;Previous<Index;++Previous)if(Motion.Parts[Previous].PartId==Entry.ParentPartId){Parent=Previous;break;}
   if(Parent==INDEX_NONE||Found->GetAttachParent()!=IndexedParts[Parent]||
      (!Entry.AttachBone.IsNone()&&Found->GetAttachSocketName()!=Entry.AttachBone))bComplete=false;
  }
 }
 USkeletalMeshComponent* ProxyBody=IndexedParts.IsValidIndex(BodyIndex)?IndexedParts[BodyIndex]:nullptr;
 if(!bComplete||!ProxyBody||!Pose.BodyAsset.IsValid())
 {Pose.Assembly=nullptr;Visual->Destroy();Pose.HiddenWeapons.Reset();return;}
 const bool bHasBodyMaterialCurves=!Motion.Parts[BodyIndex].MaterialCurves.IsEmpty();
 for(int32 Slot=0;Slot<Original->GetNumMaterials();++Slot)
 {
  UMaterialInterface* Material=Original->GetMaterial(Slot);
  if(bHasBodyMaterialCurves && Material)
  {
   // UE only accepts Material or MIC as an MID parent. A live MID is not a
   // valid parent: create from its legal parent and copy its overrides instead.
   auto* LiveMID=Cast<UMaterialInstanceDynamic>(Material);
   UMaterialInterface* Parent=LiveMID?LiveMID->Parent.Get():Material;
   if(!Parent||(!Parent->IsA<UMaterial>()&&!Parent->IsA<UMaterialInstanceConstant>()))
   {Pose.Assembly=nullptr;Visual->Destroy();Pose.HiddenWeapons.Reset();return;}
   UMaterialInstanceDynamic* PrivateMaterial=UMaterialInstanceDynamic::Create(Parent,ProxyBody);
   if(!PrivateMaterial||PrivateMaterial->Parent!=Parent)
   {Pose.Assembly=nullptr;Visual->Destroy();Pose.HiddenWeapons.Reset();return;}
   if(LiveMID)PrivateMaterial->CopyParameterOverrides(LiveMID);
   ProxyBody->SetMaterial(Slot,PrivateMaterial);
  }
  else ProxyBody->SetMaterial(Slot,Material);
 }
 for(int32 LOD=0;LOD<Pose.BodyAsset->GetLODNum();++LOD)
  for(int32 Slot=0;Slot<Pose.BodyAsset->GetMaterials().Num();++Slot)
   ProxyBody->ShowMaterialSection(Slot,INDEX_NONE,Original->IsMaterialSectionShown(Slot,LOD),LOD);
 for(const auto& Bone:Pose.BodyAsset->GetRefSkeleton().GetRefBoneInfo())
  if(Original->IsBoneHiddenByName(Bone.Name))ProxyBody->HideBoneByName(Bone.Name,EPhysBodyOp::PBO_None);
 // The first evaluation preceded the private materials above; apply frame 0
 // again so the proxy is correct before the live body is hidden.
 if(bHasBodyMaterialCurves)Visual->SetSourceTime(0.f);
 if(!IsValid(Owner)||Owner->IsActorBeingDestroyed()||Owner->IsPendingKillPending()||!IsValid(Unit)||
    Pose.Unit.Get()!=Unit||Pose.Body.Get()!=Original||!IsValid(Visual))
 {if(IsValid(Visual))Visual->Destroy();return;}
 for(const auto& Weapon:Pose.HiddenWeapons)
  if(USceneComponent* Component=Weapon.Component.Get())Component->SetVisibility(false,false);
 Original->SetVisibility(false,false);Pose.bBodyHidden=true;
}

void RestoreSettlementWeapons(FEGSettlementPose& Pose)
{
 for (const auto& Saved : Pose.HiddenWeapons)
  if (USceneComponent* Component = Saved.Component.Get()) Component->SetVisibility(Saved.bWasVisible, false);
 Pose.HiddenWeapons.Reset();
}

void ReleaseSettlementPose(FEGSettlementPose& Pose, bool bBeforeTransition = false)
{
 AEGChessUnit* Unit = Pose.Unit.Get();
 if (Unit) Unit->ReleasePresentationLease(Pose.LeaseHandle);
 Pose.LeaseHandle.Reset();
 AEGCombatVisual* Assembly = Pose.Assembly.Get();
 Pose.Assembly = nullptr;
 USkeletalMeshComponent* Body = Pose.Body.Get();
 const bool bSameBody = IsValid(Unit) && (bBeforeTransition || Unit->IsAlive_Implementation()) && Body &&
  Unit->GetVisiblePresentationMesh() == Body && Body->GetSkeletalMeshAsset() == Pose.BodyAsset.Get();
 if (bSameBody)
 {
  if (Pose.bBodyHidden && !Body->IsVisible()) Body->SetVisibility(true, false);
  RestoreSettlementWeapons(Pose);
  if (Body->GetAnimInstance()==Pose.OriginalInstance.Get() && static_cast<int32>(Body->GetAnimationMode())==Pose.OriginalAnimationMode)
  {
   if (auto* Single=Pose.SingleInstance.Get();Single&&Single->GetCurrentAsset()==Pose.Clip)
   {
    Body->SetAnimation(Pose.OriginalSingleAsset);
    Single->SetLooping(Pose.bSingleWasLooping);
    Single->SetPosition(Pose.SingleOriginalTime,false);
    Single->SetPlayRate(Pose.SingleOriginalRate);
    Single->SetPlaying(Pose.bSingleWasPlaying);
   }
   if (auto* Legacy=Pose.LegacyInstance.Get())Legacy->SetLocomotionSpeed(Pose.LegacyOriginalSpeed);
   if(Pose.bPauseWasChanged&&Body->bPauseAnims==Pose.bAppliedPauseAnims)Body->bPauseAnims=Pose.bOriginalPauseAnims;
  }
 }
 else Pose.HiddenWeapons.Reset();
 Pose.bBodyHidden = false;
 if (IsValid(Unit) && (bBeforeTransition || Unit->IsAlive_Implementation()))
  if (UEGCombatAnimInstance* Instance = Pose.AnimInstance.Get())
   if (!Instance->bDead && !Instance->IsActionActive() && Instance->PassiveClip == Pose.Clip) Instance->ResetSourcePose();
 Pose.AnimInstance.Reset();
 Pose.OriginalInstance.Reset();Pose.LegacyInstance.Reset();Pose.SingleInstance.Reset();Pose.OriginalSingleAsset=nullptr;
 Pose.Body.Reset();
 Pose.BodyAsset.Reset();
 Pose.Unit.Reset();
 // The body is already returned before destruction can invoke other observers.
 if (IsValid(Assembly)) Assembly->Destroy();
}

void SetPuppetPose(USkeletalMeshComponent* Mesh, UAnimSequence* Clip, float Time)
{
 if (!Compatible(Mesh, Clip)) return;
 if (Mesh->GetAnimationMode() != EAnimationMode::AnimationSingleNode) Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
 if (!Mesh->GetSingleNodeInstance() || Mesh->GetSingleNodeInstance()->GetCurrentAsset() != Clip) Mesh->SetAnimation(Clip);
 if (UAnimSingleNodeInstance* Instance = Mesh->GetSingleNodeInstance())
 {
  Instance->SetPlaying(false);
  Instance->SetPosition(FMath::Clamp(Time, 0.f, Clip->GetPlayLength()), false);
 }
}

void SetUnitPose(AEGChessUnit* Unit, UAnimSequence* Clip, float Time)
{
 if (!IsValid(Unit) || !Unit->IsAlive_Implementation()) return;
 USkeletalMeshComponent* Mesh = Unit->GetVisiblePresentationMesh();
 if (!Compatible(Mesh, Clip)) return;
 if (UEGCombatAnimInstance* Instance = Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance()))
  Instance->SetSourcePose(Clip, FMath::Clamp(Time, 0.f, Clip->GetPlayLength()), 0);
}

}

AEGSettlementPresentation::AEGSettlementPresentation()
{
 PrimaryActorTick.bCanEverTick = true;
 Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 SetRootComponent(Root);
 Projectile = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Projectile"));
 Projectile->SetupAttachment(Root);
 MasterProjectileFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MasterProjectileFX"));
 MasterHitFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MasterHitFX"));
 MasterFlashFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("MasterFlashFX"));
 for (UNiagaraComponent* FX : {MasterProjectileFX.Get(), MasterHitFX.Get(), MasterFlashFX.Get()})
 {
  FX->SetupAttachment(Root);
  FX->SetAutoActivate(false);
  FX->SetAutoDestroy(false);
 }
 // Elysia's silhouette is her original crystal arrow, not a second spherical projectile.
 FriendlyProjectileSystem=FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_CrystalProjectile_R2.NS_CrystalProjectile_R2"));
 FriendlyHitSystem=FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_CrystalHit_R2.NS_CrystalHit_R2"));
 EnemyProjectileSystem=FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_CorruptionProjectile_R2.NS_CorruptionProjectile_R2"));
 EnemyHitSystem=FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_CorruptionHit_R2.NS_CorruptionHit_R2"));
 EnemyFlashSystem=FSoftObjectPath(TEXT("/Game/Assets/FXs/Shared/BoardPolish20261109/NS_CorruptionFlash_R2.NS_CorruptionFlash_R2"));
 LaunchAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("LaunchAudio"));
 ImpactAudio=CreateDefaultSubobject<UAudioComponent>(TEXT("ImpactAudio"));
 for(auto* Audio:{LaunchAudio.Get(),ImpactAudio.Get()})
 {Audio->SetupAttachment(Root);Audio->bAutoActivate=false;Audio->bAutoDestroy=false;Audio->bIsUISound=true;Audio->bAllowSpatialization=false;}
 LaunchSound=FSoftObjectPath(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundWave/SW_Whoosh.SW_Whoosh"));
 ImpactSound=FSoftObjectPath(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundWave/SW_impact_star.SW_impact_star"));
 Projectile->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Projectile->SetVisibility(false);
}

TArray<FSoftObjectPath> AEGSettlementPresentation::GetPreloadPaths()
{
 TArray<FSoftObjectPath> Paths={
  FSoftObjectPath(TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia")),
  FSoftObjectPath(TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption")),
  FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Combat20261101/FX/NS_C2_Entry_Candidate.NS_C2_Entry_Candidate")),
  FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Combat20261101/Animations/A_C2_Idle_Ultra_BoardInPlace.A_C2_Idle_Ultra_BoardInPlace")),
  FSoftObjectPath(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing.MI_ChessPlacementRing")),
  FSoftObjectPath(TEXT("/Engine/BasicShapes/Plane.Plane")),FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere"))};
 const auto* Defaults=GetDefault<AEGSettlementPresentation>();
 Paths.AddUnique(Defaults->LaunchSound.ToSoftObjectPath());
 Paths.AddUnique(Defaults->ImpactSound.ToSoftObjectPath());
 Paths.Append(UEGBoardMasterActionComponent::GetPreloadPaths());
 for(const auto& Ref:{Defaults->FriendlyProjectileSystem,Defaults->FriendlyHitSystem,Defaults->FriendlyFlashSystem,
      Defaults->EnemyProjectileSystem,Defaults->EnemyHitSystem,Defaults->EnemyFlashSystem})
  if(!Ref.IsNull())Paths.AddUnique(Ref.ToSoftObjectPath());
 return Paths;
}
void AEGSettlementPresentation::PresentCopyTransform(AEGChessUnit* Unit)
{
 if (!IsValid(Unit) || !Unit->IsAlive_Implementation() || !Unit->GetRootComponent()) return;
 const FName MarkerTag(TEXT("EGCorruptionCopyMarker"));
 if (!Unit->GetComponentsByTag(UStaticMeshComponent::StaticClass(), MarkerTag).IsEmpty()) return;
 UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
 UMaterialInterface* Ring = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing.MI_ChessPlacementRing"));
 if (Plane && Ring)
 {
  UStaticMeshComponent* Marker = NewObject<UStaticMeshComponent>(Unit, NAME_None, RF_Transient);
  Unit->AddInstanceComponent(Marker);
  Marker->ComponentTags.Add(MarkerTag);
  Marker->SetStaticMesh(Plane);
  Marker->SetMaterial(0, Ring);
  Marker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Marker->SetCanEverAffectNavigation(false);
  Marker->SetCastShadow(false);
  Marker->SetupAttachment(Unit->GetRootComponent());
  Marker->RegisterComponent();
  Marker->SetRelativeLocation(FVector(0.f, 0.f, -84.f));
  Marker->SetRelativeScale3D(FVector(1.1f));
  if (UMaterialInstanceDynamic* Material = Marker->CreateDynamicMaterialInstance(0))
   Material->SetVectorParameterValue(TEXT("Tint"), FLinearColor(.55f, .08f, .85f, .8f));
 }
 if (UEGHeroSkillComponent* Skill = Unit->GetHeroSkill())
 {
  if (UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Combat20261101/FX/NS_C2_Entry_Candidate.NS_C2_Entry_Candidate")))
  {
   FEGChessFXCue Cue;
   Cue.System = System;
   Cue.Duration = .8f;
   Cue.Scale = FVector(.5f);
   AEGChessNiagaraBurst::SpawnCue(Skill, Skill->Battle.Get(), Cue, Unit->GetActorLocation());
  }
 }
}
void AEGSettlementPresentation::StartPresentation(const TArray<AEGChessUnit*>& Units, AEGChessUnit* Elysia,
 const FVector& ElysiaFallback, const FVector& CorruptionLocation, bool bInVictory)
{
 if (bStarted) return;
 bStarted = true;
 if(auto* Mode=Cast<AEGDemoGameMode>(GetOwner()))
 {
  MasterPresentation=Mode->FindComponentByClass<UEGBoardMasterPresentation>();
  if(MasterPresentation.IsValid())MasterPresentation->SetSettlementPresenter(this,true);
 }
 LastAudioClockSeconds = GetWorld() ? GetWorld()->GetAudioTimeSeconds() : -1.0;
 bVictory = bInVictory;
 // GameMode historically passed the center-of-edge locations. Resolve the
 // same corner anchors used by the board masters without moving a combat unit.
 FVector FriendlyAnchor = ElysiaFallback;
 FVector EnemyAnchor = CorruptionLocation;
 float CenterHeight = 88.f;
 if (const AEGDemoGameMode* Mode = Cast<AEGDemoGameMode>(GetOwner()))
 {
  if (const AEGBoard* Board = Mode->GetBoard())
  {
   CenterHeight = Board->UnitCenterHeight;
   UEGBoardMasterPresentation::GetMasterAnchors(Board, FriendlyAnchor, EnemyAnchor);
  }
 }
 // The master is a separate character from every combat piece. In particular,
 // neither a deployed C1 nor a transformed C2 may change its appearance.
 (void)Elysia;
 UEGHeroDefinition* ElysiaDef = LoadObject<UEGHeroDefinition>(nullptr, TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"));
 if (ElysiaDef && ElysiaDef->bUseSplitConfiguration && !ElysiaDef->PresentationDefinition) ElysiaDef = nullptr;
 UEGHeroDefinition* CorruptionDef = LoadObject<UEGHeroDefinition>(nullptr, TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
 if (CorruptionDef && CorruptionDef->bUseSplitConfiguration && !CorruptionDef->PresentationDefinition) CorruptionDef = nullptr;
 const FVector PlayerLocation = FriendlyAnchor;
 FriendlyMaster=MasterPresentation.IsValid()?MasterPresentation->GetMaster(true):nullptr;
 EnemyMaster=MasterPresentation.IsValid()?MasterPresentation->GetMaster(false):nullptr;
 // Standalone/non-campaign worlds also resolve one persistent actor per side.
 if(!FriendlyMaster.IsValid())FriendlyMaster=UEGBoardMasterPresentation::FindOrCreateMaster(GetWorld(),true,ElysiaDef,PlayerLocation,EnemyAnchor,CenterHeight);
 if(!EnemyMaster.IsValid())EnemyMaster=UEGBoardMasterPresentation::FindOrCreateMaster(GetWorld(),false,CorruptionDef,EnemyAnchor,PlayerLocation,CenterHeight);
 FriendlyBody=FriendlyMaster.IsValid()?FriendlyMaster->Body.Get():nullptr;
 EnemyBody=EnemyMaster.IsValid()?EnemyMaster->Body.Get():nullptr;
 if(FriendlyMaster.IsValid())FriendlyMaster->SetActorHiddenInGame(false);
 if(EnemyMaster.IsValid())EnemyMaster->SetActorHiddenInGame(false);
 AttackerAction=UEGBoardMasterActionComponent::Install(bVictory?FriendlyMaster.Get():EnemyMaster.Get(),bVictory);
 DefenderAction=UEGBoardMasterActionComponent::Install(bVictory?EnemyMaster.Get():FriendlyMaster.Get(),!bVictory);
 // Reject overlapping presentation requests before taking any piece leases or
 // listening to another request's notify. Destruction cannot cancel its owner.
 if ((AttackerAction.IsValid() && AttackerAction->IsBusy()) ||
     (DefenderAction.IsValid() && DefenderAction->IsBusy()))
 {
  UE_LOG(LogTemp,Verbose,TEXT("Ignoring overlapping master settlement presentation"));
  Destroy();
  return;
 }
 FlightDuration=bVictory?.48f:.6f;
 ImpactTime=MAX_flt;
 From = (bVictory ? PlayerLocation : EnemyAnchor) + FVector(0.f, 0.f, 50.f);
 To = (bVictory ? EnemyAnchor : PlayerLocation) + FVector(0.f, 0.f, 50.f);
 if (GetNetMode() != NM_DedicatedServer)
 {
  MasterProjectileFX->SetAsset((bVictory ? FriendlyProjectileSystem : EnemyProjectileSystem).LoadSynchronous());
  MasterHitFX->SetAsset((bVictory ? FriendlyHitSystem : EnemyHitSystem).LoadSynchronous());
  MasterFlashFX->SetAsset((bVictory ? FriendlyFlashSystem : EnemyFlashSystem).LoadSynchronous());
  const FRotator Direction = (To - From).Rotation();
  const float ProjectileScale = bVictory ? 1.f : 1.5f, HitScale = bVictory ? 1.3f : 1.4f, FlashScale = .65f;
  MasterProjectileFX->SetVariableFloat(TEXT("User.Scale"),bVictory?3.f:2.f);
  MasterProjectileFX->SetWorldScale3D(FVector(ProjectileScale));
  MasterHitFX->SetWorldScale3D(FVector(HitScale));
  MasterFlashFX->SetWorldScale3D(FVector(FlashScale));
  MasterProjectileFX->SetWorldLocationAndRotation(From, Direction);
  MasterFlashFX->SetWorldLocationAndRotation(From, Direction);
  MasterHitFX->SetWorldLocationAndRotation(To, Direction);
  LaunchAudio->SetSound(LaunchSound.LoadSynchronous());
  ImpactAudio->SetSound(ImpactSound.LoadSynchronous());
  const auto* AudioSettings=UEGGameAudioComponent::FindFor(this);
  const float Volume=AudioSettings?AudioSettings->GetUISoundVolume():1.f;
  LaunchAudio->SetVolumeMultiplier(Volume);ImpactAudio->SetVolumeMultiplier(Volume);
 }
 if (const UEGElysiaSkillDefinition* Skill = ElysiaDef ? Cast<UEGElysiaSkillDefinition>(ElysiaDef->SkillDefinition) : nullptr)
 {
  if (bVictory) if (UEGSourcePreviewDefinition* Impact = Skill->ImpactAssembly.LoadSynchronous())
  {
   ImpactVisual = GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(), FTransform(To), this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if (ImpactVisual)
   {
    ImpactVisual->Definition = Impact;
    ImpactVisual->bAutoPlay = false;
    ImpactVisual->bLoop = false;
    ImpactVisual->FinishSpawning(FTransform(To));
    ImpactVisual->SetActorTickEnabled(false);
    ImpactVisual->SetActorHiddenInGame(true);
   }
  }
  if (bVictory && !MasterProjectileFX->GetAsset()) if (UEGSourcePreviewDefinition* Arrow = Skill->ArrowAssembly.LoadSynchronous())
  {
   ArrowVisual = GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(), FTransform::Identity, this, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
   if (ArrowVisual)
   {
    ArrowVisual->Definition = Arrow;
    ArrowVisual->bAutoPlay = false;
    ArrowVisual->bLoop = false;
    ArrowVisual->FinishSpawning(FTransform::Identity);
    ArrowVisual->SetActorTickEnabled(false);
    ArrowVisual->SetActorScale3D(FVector(2.2f));
    ArrowVisual->SetActorHiddenInGame(true);
   }
  }
 }
 if (UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
 {
  Projectile->SetStaticMesh(Shape);
  Projectile->SetWorldScale3D(FVector(.10f));
 }
 for (AEGChessUnit* Unit : Units)
 {
  if (!IsValid(Unit) ||
      Poses.ContainsByPredicate([Unit](const FEGSettlementPose& Pose){return Pose.Unit.Get()==Unit;})) continue;
  // Dead units retain their existing death/dissolve presentation. A separate
  // idle proxy would make an enemy appear alive and overlap its death mesh.
  if (!Unit->IsAlive_Implementation()) continue;
  const UEGHeroDefinition* Def = Unit->GetDefinition();
  USkeletalMeshComponent* Original = Unit->GetVisiblePresentationMesh();
  if (!Def || !Original || (Def->bUseSplitConfiguration && !Def->PresentationDefinition)) continue;
  UAnimSequence* Clip = nullptr;
  const auto* Presentation = Def->PresentationDefinition.Get();
  UEGSourcePreviewDefinition* SettlementAssembly=nullptr;
  bool bFormClip=false;
  if (Presentation && Def->HeroKind==EEGHeroKind::ElysiaHerrscher)
  {
   const auto* OwnerMode=Cast<AEGChessGameMode>(GetOwner());
   const int32 Form=OwnerMode&&OwnerMode->WasHumanOriginAtSettlement(Unit)?1:0;
   if(Presentation->SettlementIdleVariants.IsValidIndex(Form))
   {
    const auto& Variant=Presentation->SettlementIdleVariants[Form];
    Clip=Variant.Animation.LoadSynchronous();
    SettlementAssembly=Variant.Assembly.LoadSynchronous();
   }
   if(Form==1&&!Compatible(Original,Clip))
    Clip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Combat20261101/Animations/A_C2_Idle_Ultra_BoardInPlace.A_C2_Idle_Ultra_BoardInPlace"));
   bFormClip=Compatible(Original,Clip);
  }
  if (!Compatible(Original,Clip))
  {
   SettlementAssembly=nullptr;
   Clip=Def->GetIdleAnimation().LoadSynchronous();
  }
  if (!Compatible(Original,Clip)&&Presentation) Clip=Presentation->SettlementIdleAnimation.LoadSynchronous();
  auto* AnyInstance=Original->GetAnimInstance();
  auto* AnimInstance=Cast<UEGCombatAnimInstance>(AnyInstance);
  auto* Legacy=Cast<UEGChessAnimInstance>(AnyInstance);
  auto* Single=Original->GetSingleNodeInstance();
  if ((!AnimInstance||Compatible(Original,Clip)) && (!AnimInstance||!AnimInstance->IsActionActive()))
  {
   FDelegateHandle LeaseHandle;
   const auto Lease = FEGPresentationReleaseLease::CreateUObject(this,&ThisClass::OnUnitPresentationWillRelease);
   if (!Unit->TryAcquirePresentationReleaseLease(Lease,LeaseHandle)) continue;
   FEGSettlementPose& Pose = Poses.AddDefaulted_GetRef();
   Pose.LeaseHandle = LeaseHandle;
   Pose.Clip = Clip;
   Pose.bAnimate=(Unit->GetFaction_Implementation()==EEGFaction::Player)==bVictory;
   Pose.Unit=Unit;Pose.Body=Original;Pose.BodyAsset=Original->GetSkeletalMeshAsset();
   Pose.AnimInstance=AnimInstance;Pose.OriginalInstance=AnyInstance;Pose.LegacyInstance=Legacy;Pose.SingleInstance=Single;
   Pose.OriginalAnimationMode=static_cast<int32>(Original->GetAnimationMode());
   Pose.bOriginalPauseAnims=Original->bPauseAnims;
   if(Legacy)
   {
    Pose.LegacyOriginalSpeed=Legacy->GetLocomotionSpeed();
    Legacy->StopAction();Legacy->SetLocomotionSpeed(0.f);Legacy->TickPresentation(.2f);
   }
   if(Single&&Compatible(Original,Clip))
   {
    Pose.OriginalSingleAsset=Single->GetCurrentAsset();Pose.SingleOriginalTime=Single->GetCurrentTime();
    Pose.SingleOriginalRate=Single->GetPlayRate();Pose.bSingleWasPlaying=Single->IsPlaying();Pose.bSingleWasLooping=Single->IsLooping();
    SetPuppetPose(Original,Clip,0.f);
   }
   if(!AnimInstance)
   {
    // Legacy/custom graphs can keep their idle while winning; losing pieces
    // are frozen without replacing their graph or touching dead actors.
    Pose.bAppliedPauseAnims=!Pose.bAnimate;
    Pose.bPauseWasChanged=Original->bPauseAnims!=Pose.bAppliedPauseAnims;
    Original->bPauseAnims=Pose.bAppliedPauseAnims;
    if(!Pose.bAnimate&&(Legacy||Single))
    {
     Original->bPauseAnims=false;
     Original->TickAnimation(0.f,false);Original->RefreshBoneTransforms();
     Original->bPauseAnims=Pose.bAppliedPauseAnims;
    }
   }
     if (Presentation&&AnimInstance)
      TryCreateSettlementProxy(this,Unit,Original,Clip,bFormClip?SettlementAssembly:
       Presentation->SettlementIdleAnimation.Get()==Clip?Presentation->SettlementIdleAssembly.LoadSynchronous():nullptr,Pose);
     if (IsActorBeingDestroyed() || IsPendingKillPending()) return;
     if (Pose.Unit.Get()!=Unit) continue;
    if(AnimInstance)SetUnitPose(Unit, Clip, 0.f);
  }
 }
 if(!AttackerAction.IsValid()||!AttackerAction->Attack(this))
 {
  UE_LOG(LogTemp,Error,TEXT("Master attack presentation unavailable; resolving existing settlement without animation"));
  bImpacted=true;OnImpact.Broadcast();
 }
 else MasterReleaseHandle=AttackerAction->OnRelease.AddUObject(this,&ThisClass::OnMasterRelease);
 Tick(0.f);
}

AEGArtPiece* AEGSettlementPresentation::GetMasterActor(bool bFriendly) const
{return bFriendly?FriendlyMaster.Get():EnemyMaster.Get();}

float AEGSettlementPresentation::GetDurationSeconds() const
{
 return AEGDemoGameMode::SettlementDurationSeconds;
}

void AEGSettlementPresentation::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 const auto* OwnerMode=Cast<AEGChessGameMode>(GetOwner());
 const bool bFinalResult=OwnerMode&&OwnerMode->GetMatch()&&OwnerMode->GetMatch()->GetPhase()==EEGMatchPhase::Finished;
 if (!bStarted || (bFinished&&!bFinalResult)) return;
 UWorld* World = GetWorld();
 const double Now = World ? World->GetAudioTimeSeconds() : -1.0;
 float Elapsed = FMath::Max(0.f, DeltaSeconds);
 if (World && World->bInTick && LastAudioClockSeconds >= 0.0)
  Elapsed = static_cast<float>(FMath::Max(0.0, Now - LastAudioClockSeconds));
 LastAudioClockSeconds = Now;
 Age = bFinalResult ? Age+Elapsed : FMath::Min(GetDurationSeconds(), Age + Elapsed);
 for (FEGSettlementPose& Pose : Poses)
 {
  if (AEGChessUnit* Unit = Pose.Unit.Get(); IsValid(Unit) && Unit->IsAlive_Implementation() &&
           Unit->GetVisiblePresentationMesh() == Pose.Body.Get() && Pose.Body.IsValid() &&
           Pose.Body->GetSkeletalMeshAsset() == Pose.BodyAsset.Get() &&
           Pose.Body->GetAnimInstance() == Pose.OriginalInstance.Get() &&
           static_cast<int32>(Pose.Body->GetAnimationMode())==Pose.OriginalAnimationMode &&
           (!Pose.AnimInstance.IsValid()||!Pose.AnimInstance->IsActionActive()) &&
           (!Pose.LegacyInstance.IsValid()||!Pose.LegacyInstance->IsActionActive()) &&
           (!Pose.bBodyHidden || !Pose.Body->IsVisible()))
  {
   const float Length=Pose.Clip?Pose.Clip->GetPlayLength():0.f;
   const float PoseTime=Pose.bAnimate&&FMath::IsFinite(Length)&&Length>SMALL_NUMBER?FMath::Fmod(Age,Length):0.f;
   if(Pose.AnimInstance.IsValid())SetUnitPose(Unit, Pose.Clip, PoseTime);
   else if(Pose.SingleInstance.IsValid()&&Compatible(Pose.Body.Get(),Pose.Clip))SetPuppetPose(Pose.Body.Get(),Pose.Clip,PoseTime);
   if (IsValid(Pose.Assembly))
   {
    Pose.Assembly->SetActorTransform(Pose.Body->GetComponentTransform());
    Pose.Assembly->SetSourceTime(PoseTime);
   }
  }
  else ReleaseSettlementPose(Pose);
 }
 if (!bImpacted && Age >= ImpactTime)
 {
  bImpacted = true;
  if(DefenderAction.IsValid())DefenderAction->React(this);
  if (MasterProjectileFX->IsActive()) MasterProjectileFX->DeactivateImmediate();
  if (MasterHitFX->GetAsset()) MasterHitFX->Activate(true);
  if(GetNetMode()!=NM_DedicatedServer)ImpactAudio->Play();
  OnImpact.Broadcast();
  if (IsActorBeingDestroyed() || IsPendingKillPending()) return;
 }
 const bool bFlying = bMasterFlightStarted && !bImpacted && Age < ImpactTime;
 const FVector ProjectileLocation = FMath::Lerp(From, To, FMath::Clamp((Age - FlightStart) / FMath::Max(.01f, ImpactTime - FlightStart), 0.f, 1.f));

 if (MasterProjectileFX->GetAsset())
 {
  if (bFlying) MasterProjectileFX->SetWorldLocation(ProjectileLocation);
 }
 if (IsValid(ArrowVisual))
 {
  ArrowVisual->SetActorHiddenInGame(!bFlying);
  ArrowVisual->SetActorLocation(ProjectileLocation);
  ArrowVisual->SetActorRotation((To - From).Rotation());
  if (bFlying) ArrowVisual->SetSourceTime(FMath::Fmod(Age - FlightStart, .3f));
 }
 else if (!MasterProjectileFX->GetAsset() && Projectile->GetStaticMesh())
 {
  Projectile->SetVisibility(bFlying);
  Projectile->SetWorldLocation(ProjectileLocation);
 }
 // Limit even looping pack systems to a single settlement gesture. The final
 // result actor can remain alive throughout the result screen.
 if (bMasterFlightStarted && Age - FlightStart >= .9f && MasterFlashFX->IsActive())
  MasterFlashFX->DeactivateImmediate();
 if (bImpacted && Age - ImpactTime >= 1.35f && MasterHitFX->IsActive())
  MasterHitFX->DeactivateImmediate();
 if (IsValid(ImpactVisual))
 {
  const float ImpactAge = Age - ImpactTime;
  ImpactVisual->SetActorHiddenInGame(!bImpacted || ImpactAge >= .35f);
  if (bImpacted && ImpactAge < .35f) ImpactVisual->SetSourceTime(FMath::Max(0.f, ImpactAge));
 }
 if (!bFinished&&Age >= GetDurationSeconds())
 {
  bFinished = true;
  OnFinished.Broadcast();
 }
}

void AEGSettlementPresentation::OnMasterRelease()
{
 if(bMasterFlightStarted||bImpacted||IsActorBeingDestroyed())return;
 bMasterFlightStarted=true;FlightStart=Age;ImpactTime=Age+FlightDuration;
 auto* Attacker=bVictory?FriendlyBody.Get():EnemyBody.Get();
 auto* Defender=bVictory?EnemyBody.Get():FriendlyBody.Get();
 if(Attacker)From=Attacker->GetSocketLocation(bVictory?TEXT("Bip001-L-Hand"):TEXT("Bip001-R-Hand"));
 if(Defender)To=Defender->GetSocketLocation(TEXT("Bip001-Spine2"));
 const FRotator Direction=(To-From).Rotation();
 MasterProjectileFX->SetWorldLocationAndRotation(From,Direction);
 MasterFlashFX->SetWorldLocationAndRotation(From,Direction);
 MasterHitFX->SetWorldLocationAndRotation(To,Direction);
 if(MasterProjectileFX->GetAsset())MasterProjectileFX->Activate(true);
 if(MasterFlashFX->GetAsset())MasterFlashFX->Activate(true);
 if(GetNetMode()!=NM_DedicatedServer)LaunchAudio->Play();
}

void AEGSettlementPresentation::OnUnitPresentationWillRelease(AEGChessUnit* Unit)
{
 // Called before Die/RestorePiece changes the owner or death code can hide it.
 for (FEGSettlementPose& Pose : Poses)
  if (Pose.Unit.Get() == Unit) ReleaseSettlementPose(Pose, true);
}

void AEGSettlementPresentation::EndPlay(const EEndPlayReason::Type Reason)
{
 if(IsValid(LaunchAudio))LaunchAudio->Stop();
 if(IsValid(ImpactAudio))ImpactAudio->Stop();
 for (UNiagaraComponent* FX : {MasterProjectileFX.Get(), MasterHitFX.Get(), MasterFlashFX.Get()})
  if (IsValid(FX)) FX->DeactivateImmediate();
 if(AttackerAction.IsValid())
 {AttackerAction->OnRelease.Remove(MasterReleaseHandle);AttackerAction->Cancel(this);}
 if(DefenderAction.IsValid())DefenderAction->Cancel(this);
 for (FEGSettlementPose& Pose : Poses) ReleaseSettlementPose(Pose);
 if (IsValid(ArrowVisual)) ArrowVisual->Destroy();
 if (IsValid(ImpactVisual)) ImpactVisual->Destroy();
 ImpactVisual = nullptr;
 ArrowVisual = nullptr;
 OnImpact.Clear();
 OnFinished.Clear();
 if(MasterPresentation.IsValid())MasterPresentation->SetSettlementPresenter(this,false);
 MasterPresentation.Reset();
 Super::EndPlay(Reason);
}








