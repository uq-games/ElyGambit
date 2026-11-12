#include "Presentation/EGMobiusNiagaraEffect.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"

AEGMobiusNiagaraEffect::AEGMobiusNiagaraEffect()
{
 PrimaryActorTick.bCanEverTick=true;
 bReplicates=false;
 Effect=CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
 Effect->SetAutoActivate(false);
 SetRootComponent(Effect);
 SetActorEnableCollision(false);
}
AEGMobiusNiagaraEffect* AEGMobiusNiagaraEffect::Spawn(UEGHeroSkillComponent* Skill,UNiagaraSystem* System,const FVector& Location,const FRotator& Rotation,float Lifetime,bool ActionBound,AEGChessUnit* InTarget)
{
 if(!IsValid(Skill)||!IsValid(System)||!Skill->Battle.IsValid()||!Skill->Battle->IsBattleActive()
    ||Location.ContainsNaN()||Rotation.ContainsNaN()||!FMath::IsFinite(Lifetime)||Lifetime<=0.f)return nullptr;
 UWorld* World=Skill->GetWorld();
 if(!World||World->GetNetMode()==NM_DedicatedServer)return nullptr;
 FActorSpawnParameters Params;Params.Owner=Skill->GetOwner();Params.ObjectFlags|=RF_Transient;
 auto* Visual=World->SpawnActor<AEGMobiusNiagaraEffect>(Location,Rotation,Params);
 if(!Visual)return nullptr;
 Visual->SkillOwner=Skill;Visual->BattleOwner=Skill->Battle;Visual->Round=Skill->Battle->GetRoundId();
 Visual->Action=Skill->ActionId;Visual->Duration=Lifetime;Visual->bActionBound=ActionBound;
 Visual->Target=InTarget;Visual->bTargetBound=InTarget!=nullptr;
 Visual->Effect->SetAsset(System);Visual->Effect->Activate(true);
 return Visual;
}
void AEGMobiusNiagaraEffect::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 auto* Skill=SkillOwner.Get();auto* Battle=BattleOwner.Get();
 auto* Unit=Skill?Cast<AEGChessUnit>(Skill->GetOwner()):nullptr;
 if(!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit)||!Battle||!Battle->IsBattleActive()
    ||Battle->GetRoundId()!=Round||(bActionBound&&Skill->ActionId!=Action)
    ||(bTargetBound&&(!Target.IsValid()||!IEGCombatant::Execute_IsAlive(Target.Get())))) {Destroy();return;}
 const bool bRunning=Battle->IsCombatRunning();Effect->SetPaused(!bRunning);
 if(!bRunning)return;
 float Rate=1.f;
 if(bActionBound)
 {
  const auto* Mesh=Unit->GetVisiblePresentationMesh();const auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
  const auto* Montage=Skill->GetActiveMontage();
  if(Anim&&Montage){const float R=Anim->Montage_GetPlayRate(Montage);if(FMath::IsFinite(R)&&R>0.f)Rate=R;}
 }
 Effect->SetCustomTimeDilation(Rate);Age+=FMath::Max(0.f,DeltaSeconds)*Rate;
 if(Age>=Duration)Destroy();
}
void AEGMobiusNiagaraEffect::EndPlay(const EEndPlayReason::Type Reason)
{
 if(IsValid(Effect))Effect->DeactivateImmediate();
 Super::EndPlay(Reason);
}
