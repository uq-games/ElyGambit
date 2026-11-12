#include "Presentation/EGUltimateSignetVisual.h"
#include "Components/MaterialBillboardComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

AEGUltimateSignetVisual::AEGUltimateSignetVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;SetActorEnableCollision(false);
 Surface=CreateDefaultSubobject<UMaterialBillboardComponent>(TEXT("SignetMaterialSurface"));SetRootComponent(Surface);
 Surface->SetHiddenInGame(false);Surface->SetVisibility(true);
 Surface->bUseEditorCompositing=false;
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Assets/UI/Materials/UltimateSignet20261109/M_UltimateSignet.M_UltimateSignet"));
 SignetMaterial=Material.Object;
 Surface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 Surface->SetGenerateOverlapEvents(false);Surface->SetCastShadow(false);

}
AEGUltimateSignetVisual* AEGUltimateSignetVisual::Spawn(UEGHeroSkillComponent* Skill)
{
 auto* Unit=Skill?Cast<AEGChessUnit>(Skill->GetOwner()):nullptr;
 auto* Config=Unit&&Unit->GetDefinition()?Unit->GetDefinition()->SkillDefinition.Get():nullptr;
 if(!IsValid(Unit)||!Config||!Config->UltimateSignet||!Skill->Battle.IsValid()||!Skill->Battle->IsCombatRunning()
  ||!Skill->ActionId.IsValid()||!IEGCombatant::Execute_IsAlive(Unit)||Unit->GetNetMode()==NM_DedicatedServer)return nullptr;
 FActorSpawnParameters Params;Params.Owner=Unit;Params.ObjectFlags|=RF_Transient;
 auto* V=Unit->GetWorld()->SpawnActor<AEGUltimateSignetVisual>(Params);if(!V)return nullptr;
 V->Caster=Unit;V->SkillOwner=Skill;V->Battle=Skill->Battle;V->Round=Skill->Battle->GetRoundId();V->Action=Skill->ActionId;V->Data=Config;
 if(!V->SignetMaterial){V->Destroy();return nullptr;}
 V->DisplayWidth=FMath::Clamp(Config->UltimateSignetSize,80.f,220.f);
 V->ImageAspect=float(Config->UltimateSignet->GetSizeY())/FMath::Max(1,Config->UltimateSignet->GetSizeX());
 V->DynamicMaterial=UMaterialInstanceDynamic::Create(V->SignetMaterial,V);
 V->DynamicMaterial->SetTextureParameterValue(TEXT("SignetTexture"),Config->UltimateSignet);
 V->DynamicMaterial->SetVectorParameterValue(TEXT("SignetColor"),Config->UltimateSignetColor);
 V->DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"),0.f);
 V->bWaitForHumanEntry=Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher;
 V->Surface->AddElement(V->DynamicMaterial,nullptr,false,V->DisplayWidth*.5f*V->ImageAspect,V->DisplayWidth*.5f,nullptr);
 if(auto* Mesh=Unit->GetVisiblePresentationMesh())
 {
  for(int32 Index=0;Index<Mesh->GetNumBones();++Index)
  {
   const FName Bone=Mesh->GetBoneName(Index);
   if(Bone.ToString().EndsWith(TEXT("Head"),ESearchCase::IgnoreCase)){V->HeadBone=Bone;break;}
  }
  V->AddTickPrerequisiteComponent(Mesh);
 }
 V->AddTickPrerequisiteComponent(Skill);
 // Human's ActionStarted establishes the entry phase just after this spawn.
 if(!V->bWaitForHumanEntry)V->Tick(0.f);
 return V;
}
void AEGUltimateSignetVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Caster.IsValid()||!SkillOwner.IsValid()||!Battle.IsValid()||!Data||!Battle->IsBattleActive()
  ||Battle->GetRoundId()!=Round||(!bWaitForHumanEntry&&!bPostEntryReceipt&&SkillOwner->ActionId!=Action)
  ||!IEGCombatant::Execute_IsAlive(Caster.Get())){Destroy();return;}
 if(bWaitForHumanEntry)
 {
  // The entry movie covers the board. Consume its existing authoritative
  // phase, displaying a short receipt only after a successful return to Origin.
  // Cancellation returns to Bow and discards this pending visual.
  const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(SkillOwner->GetBehavior());
  if(!Human){Destroy();return;}
  const auto Phase=Human->GetPresentationPhase();
  if(Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::EntryReleased)return;
  if(Phase!=EEGElysiaHerrscherPresentationPhase::Origin){Destroy();return;}
  bWaitForHumanEntry=false;bPostEntryReceipt=true;
 }
 const bool Running=Battle->IsCombatRunning()&&(!GetWorld()->IsPaused()||Battle->IsHumanEntryPauseOwner(Caster.Get(),Action));
 if(Running)Age+=FMath::Max(0.f,DeltaSeconds);
 if(Age>=1.25f){Destroy();return;}
 const float Rise=FMath::SmoothStep(0.f,1.f,FMath::Clamp(Age/.2f,0.f,1.f));
 const float Fade=FMath::Min(FMath::Clamp(Age/.1f,0.f,1.f),FMath::Clamp((1.25f-Age)/.35f,0.f,1.f));
 // Use the same authored, world-unit head anchor as the unit's vitals, not the
 // capsule's origin. Presentation bodies can be much taller than the capsule.
 const auto* Definition=Caster->GetDefinition();
 const float HeadHeight=Definition?FMath::Max(160.f,Definition->GetVitalsAnchorHeight()-88.f):180.f;
 FVector Anchor=Caster->GetActorLocation()+FVector(0,0,HeadHeight);
 if(auto* Mesh=Caster->GetVisiblePresentationMesh();Mesh&&!HeadBone.IsNone()&&Mesh->DoesSocketExist(HeadBone))
  Anchor=Mesh->GetSocketLocation(HeadBone);
 const float Lift=DisplayWidth*FMath::Lerp(-.35f,1.35f,Rise);
 FVector Location=Anchor+FVector(0,0,Lift);
 if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
 {
  // Rise in screen-up space, clearing the screen-space vitals as well as the
  // head; a world-Z-only rise overlaps health bars at the board's steep angle.
  Location=Anchor+Camera->GetActorUpVector()*Lift;
  // Shift slightly toward the camera so the character's head never hides the emblem.
  const FVector Facing=(Camera->GetCameraLocation()-Location).GetSafeNormal();Location+=Facing*45.f;
  SetActorLocation(Location);
 }
 else SetActorLocation(Location);
 // Material billboards take half extents; update them explicitly (component
 // scaling is cancelled by their camera-facing vertex transform).
 const float Size=DisplayWidth*.5f*FMath::Lerp(.55f,1.f,Rise);
 if(!Surface->Elements.IsEmpty()&&!FMath::IsNearlyEqual(Surface->Elements[0].BaseSizeY,Size))
 {
  Surface->Elements[0].BaseSizeX=Size*ImageAspect;
  Surface->Elements[0].BaseSizeY=Size;
  Surface->MarkRenderStateDirty();
 }
 DynamicMaterial->SetScalarParameterValue(TEXT("Opacity"),Fade);

}
void AEGUltimateSignetVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 Super::EndPlay(Reason);
}
