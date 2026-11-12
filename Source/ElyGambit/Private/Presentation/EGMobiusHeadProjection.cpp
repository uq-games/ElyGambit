#include "Presentation/EGMobiusHeadProjection.h"
#include "Presentation/EGCombatVisual.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

AEGMobiusHeadProjection::AEGMobiusHeadProjection()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 SetActorEnableCollision(false);
}
AEGMobiusHeadProjection* AEGMobiusHeadProjection::Spawn(AEGChessUnit* Source,AEGChessUnit* Victim)
{
 if(!IsValid(Source)||!IsValid(Victim)||Source->GetNetMode()==NM_DedicatedServer)return nullptr;
 const auto* Hero=Source->GetDefinition();
 const auto* Data=Hero?Cast<UEGMobiusSkillDefinition>(Hero->SkillDefinition):nullptr;
 auto* Definition=Data?Data->SerpentBody.LoadSynchronous():nullptr;
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/FXs/Characters/Mobius/FiveHeroRework20261108/Materials/M_Mobius_HeadProjection.M_Mobius_HeadProjection"));
 if(!Definition||!Material)return nullptr;
 auto* V=Source->GetWorld()->SpawnActor<AEGMobiusHeadProjection>();if(!V)return nullptr;
 V->SetOwner(Source);V->Caster=Source;V->Target=Victim;V->Battle=Source->GetHeroSkill()->Battle;
 V->Round=V->Battle.IsValid()?V->Battle->GetRoundId():FGuid();
 V->Start=Source->GetActorLocation()+FVector(0,0,125);V->Finish=Victim->GetActorLocation()+FVector(0,0,85);
 const FRotator Facing=(V->Finish-V->Start).Rotation();
 const FTransform Transform(Facing+FRotator(0,-90,0),V->Start,FVector(2.2f));
 auto* Rig=Source->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),Transform,V,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!Rig){V->Destroy();return nullptr;}
 V->Rig=Rig;Rig->Definition=Definition;Rig->bAutoPlay=false;Rig->bLoop=false;Rig->FinishSpawning(Transform);
 Rig->SetActorTickEnabled(false);Rig->SetSourceTime(4.15f);
 FVector Head;
 if(!Rig->GetVisiblePartBoneLocation(TEXT("Mobius_SKL02_Snake"),TEXT("Bip001-Head"),Head)){V->Destroy();return nullptr;}
 V->HeadOffset=Head-Rig->GetActorLocation();
 TArray<USkeletalMeshComponent*> Parts;Rig->GetComponents(Parts);
 for(auto* Part:Parts)
 {
  Part->SetCastShadow(false);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  for(int32 Slot=0;Slot<Part->GetNumMaterials();++Slot)
  {
   UTexture* Texture=nullptr;
   if(auto* Original=Part->GetMaterial(Slot))Original->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseTexture")),Texture);
   auto* MID=UMaterialInstanceDynamic::Create(Material,V);
   if(Texture)MID->SetTextureParameterValue(TEXT("BaseTexture"),Texture);
   MID->SetScalarParameterValue(TEXT("HeadRadius"),140.f);
   Part->SetMaterial(Slot,MID);V->Materials.Add(MID);
  }
 }
 V->Tick(0.f);return V;
}
void AEGMobiusHeadProjection::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Caster.IsValid()||!IEGCombatant::Execute_IsAlive(Caster.Get())||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round||!IsValid(Rig)){Destroy();return;}
 if(!Battle->IsCombatRunning())return;
 Age+=FMath::Max(0.f,DeltaSeconds);
 if(Age>=.38f){Destroy();return;}
 if(Target.IsValid())Finish=Target->GetActorLocation()+FVector(0,0,85);
 const FVector Head=FMath::Lerp(Start,Finish,FMath::Clamp(Age/.28f,0.f,1.f));
 Rig->SetActorLocation(Head-HeadOffset);
 const float Alpha=FMath::Min(Age/.045f,1.f)*FMath::Clamp((.38f-Age)/.1f,0.f,1.f);
 for(const auto& MID:Materials)
 {
  MID->SetVectorParameterValue(TEXT("HeadCenter"),FLinearColor(Head.X,Head.Y,Head.Z,1));
  MID->SetScalarParameterValue(TEXT("ProjectionOpacity"),Alpha*.85f);
 }
}
void AEGMobiusHeadProjection::EndPlay(const EEndPlayReason::Type Reason)
{
 if(IsValid(Rig))Rig->Destroy();Rig=nullptr;Materials.Reset();Super::EndPlay(Reason);
}
