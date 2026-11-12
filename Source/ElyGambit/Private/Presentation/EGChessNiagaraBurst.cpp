#include "Presentation/EGChessNiagaraBurst.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#if WITH_EDITOR
#include "NiagaraEmitter.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScriptSourceBase.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#endif

AEGChessNiagaraBurst::AEGChessNiagaraBurst()
{
 PrimaryActorTick.bCanEverTick=true;
 Effect=CreateDefaultSubobject<UNiagaraComponent>(TEXT("Effect"));
 Effect->bAutoActivate=false;
 SetRootComponent(Effect);
 SetActorEnableCollision(false);
 bReplicates=false;
}
void AEGChessNiagaraBurst::Spawn(UEGHeroSkillComponent* Skill,UEGBattleContextComponent* Battle,UNiagaraSystem* System,const FVector& Location)
{
 FEGChessFXCue Cue;Cue.System=System;Cue.Duration=3.f;
 SpawnCue(Skill,Battle,Cue,Location);
}
AEGChessNiagaraBurst* AEGChessNiagaraBurst::SpawnCue(UEGHeroSkillComponent* Skill,UEGBattleContextComponent* Battle,const FEGChessFXCue& Cue,const FVector& Location,const FRotator& Rotation,bool AllowRoundEndTail)
{
 if(!IsValid(Skill)||!IsValid(Battle)||!IsValid(Cue.System)||!Battle->IsBattleActive()||Skill->Battle.Get()!=Battle
  ||Location.ContainsNaN()||Rotation.ContainsNaN()||Cue.Offset.ContainsNaN()||Cue.Rotation.ContainsNaN()||Cue.Scale.ContainsNaN()
  ||Cue.Scale.GetMin()<=0.f||!FMath::IsFinite(Cue.Duration)||Cue.Duration<=0.f)return nullptr;
 UWorld* World=Skill->GetWorld();
 if(!World||World->GetNetMode()==NM_DedicatedServer)return nullptr;
 auto* Unit=Cast<AEGChessUnit>(Skill->GetOwner());if(!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit))return nullptr;
 if(Cue.bActionBound&&!Skill->ActionId.IsValid())return nullptr;
 FActorSpawnParameters Params;Params.Owner=Unit;Params.ObjectFlags|=RF_Transient;
 auto* Burst=World->SpawnActor<AEGChessNiagaraBurst>(Location,Rotation,Params);if(!Burst)return nullptr;
 Burst->SkillOwner=Skill;Burst->BattleOwner=Battle;Burst->Round=Battle->GetRoundId();Burst->Action=Skill->ActionId;
 Burst->bAllowRoundEndTail=AllowRoundEndTail&&!Cue.bActionBound;
 Burst->Duration=Burst->bAllowRoundEndTail?FMath::Min(Cue.Duration,.5f):Cue.Duration;
 Burst->bActionBound=Cue.bActionBound;Burst->bActionClock=Cue.bActionClock;
 if(Cue.bAttachToOwner)
 {
  auto* Mesh=Unit->GetVisiblePresentationMesh();
  if(!Mesh||(!Cue.Bone.IsNone()&&!Mesh->DoesSocketExist(Cue.Bone))){Burst->Destroy();return nullptr;}
  Burst->AttachToComponent(Mesh,FAttachmentTransformRules::SnapToTargetNotIncludingScale,Cue.Bone);
  // Bone animation scale must not multiply the authored board size a second time.
  Burst->Effect->SetUsingAbsoluteScale(true);
  Burst->SetActorRelativeLocation(Cue.Offset);Burst->SetActorRelativeRotation(Cue.Rotation);
 }
 else Burst->SetActorLocationAndRotation(Location+Rotation.RotateVector(Cue.Offset),(Rotation.Quaternion()*Cue.Rotation.Quaternion()).Rotator());
 Burst->SetActorScale3D(Cue.Scale);
 Burst->Effect->SetAutoDestroy(false);Burst->Effect->SetAsset(Cue.System);
 Burst->Effect->OnSystemFinished.AddDynamic(Burst,&AEGChessNiagaraBurst::Finished);Burst->Effect->Activate(true);
 Burst->AddTickPrerequisiteComponent(Skill);
 return Burst;
}
AEGChessNiagaraBurst* AEGChessNiagaraBurst::Play(UEGHeroSkillComponent* Skill,FName Cue,const FVector& Location,const FRotator& Rotation)
{
 auto* Unit=Skill?Cast<AEGChessUnit>(Skill->GetOwner()):nullptr;
 const auto* Data=Unit&&Unit->GetDefinition()?Unit->GetDefinition()->SkillDefinition.Get():nullptr;
 const auto* Found=Data?Data->ChessFX.Find(Cue):nullptr;
 return Found?SpawnCue(Skill,Skill->Battle.Get(),*Found,Location,Rotation):nullptr;
}
void AEGChessNiagaraBurst::Swing(UEGHeroSkillComponent* Skill,int32 Index)
{
 auto* Unit=Skill?Cast<AEGChessUnit>(Skill->GetOwner()):nullptr;
 const auto* Data=Unit&&Unit->GetDefinition()?Unit->GetDefinition()->SkillDefinition.Get():nullptr;
 if(Data&&Data->BasicSwingFX.IsValidIndex(Index))SpawnCue(Skill,Skill->Battle.Get(),Data->BasicSwingFX[Index],Unit->GetActorLocation(),Unit->GetActorRotation());
}
void AEGChessNiagaraBurst::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 auto* Unit=SkillOwner.IsValid()?Cast<AEGChessUnit>(SkillOwner->GetOwner()):nullptr;
 const bool bRoundChanged=BattleOwner.IsValid()&&BattleOwner->IsBattleActive()&&BattleOwner->GetRoundId()!=Round;
 const bool bRoundStopped=BattleOwner.IsValid()&&!BattleOwner->IsBattleActive();
 if(!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit)||!BattleOwner.IsValid()||bRoundChanged||(bRoundStopped&&!bAllowRoundEndTail)
  ||(bActionBound&&SkillOwner->ActionId!=Action))
 {Destroy();return;}
 const bool bRunning=BattleOwner->IsCombatRunning()||(bRoundStopped&&bAllowRoundEndTail);
 Effect->SetPaused(!bRunning);
 if(!bRunning)return;
 float Rate=1.f;
 if(bActionClock)
 {
  const auto* Mesh=Unit->GetVisiblePresentationMesh();const auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;
  const auto* Montage=SkillOwner->GetActiveMontage();
  if(Anim&&Montage){const float R=Anim->Montage_GetPlayRate(Montage);if(FMath::IsFinite(R)&&R>0.f)Rate=R;}
 }
 Effect->SetCustomTimeDilation(Rate);Age+=FMath::Max(0.f,DeltaSeconds)*Rate;
 if(Age>=Duration)Destroy();
}
void AEGChessNiagaraBurst::Finished(UNiagaraComponent*) {Destroy();}
void AEGChessNiagaraBurst::EndPlay(const EEndPlayReason::Type Reason)
{
 if(IsValid(Effect)){Effect->OnSystemFinished.RemoveDynamic(this,&AEGChessNiagaraBurst::Finished);Effect->DeactivateImmediate();}
 Super::EndPlay(Reason);
}
FString AEGChessNiagaraBurst::AuditPrivateSystem(UNiagaraSystem* System)
{
#if WITH_EDITOR
 TSharedRef<FJsonObject> Root=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
 bool Good=IsValid(System)&&System->GetOutermost()->GetName().StartsWith(TEXT("/Game/Assets/FXs/Characters/"));
 if(Good)for(const auto& Handle:System->GetEmitterHandles())
 {
  auto* Instance=Handle.GetInstance().Emitter.Get();auto* Data=Handle.GetEmitterData();
  TSharedRef<FJsonObject> Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("id"),Handle.GetId().ToString());
  Row->SetStringField(TEXT("name"),Handle.GetName().ToString());
  bool Private=Instance&&Instance->IsIn(System)&&Instance->GetOutermost()==System->GetOutermost()&&Data;
  Row->SetStringField(TEXT("emitter"),Instance?Instance->GetPathName():TEXT(""));
  TArray<TSharedPtr<FJsonValue>> Renderers;
  if(Data)
  {
   for(const auto* Renderer:Data->GetRenderers())
   {
    Private=Private&&Renderer&&Renderer->IsIn(System)&&Renderer->GetOutermost()==System->GetOutermost();
    Renderers.Add(MakeShared<FJsonValueString>(Renderer?Renderer->GetPathName():TEXT("")));
   }
   const auto* Graph=Data->GraphSource.Get();
   Private=Private&&Graph&&Graph->IsIn(System)&&Graph->GetOutermost()==System->GetOutermost();
   Row->SetStringField(TEXT("graph"),Graph?Graph->GetPathName():TEXT(""));
   const auto* GraphProperty=Graph?FindFProperty<FObjectPropertyBase>(Graph->GetClass(),TEXT("NodeGraph")):nullptr;
   const UObject* NodeGraph=GraphProperty?GraphProperty->GetObjectPropertyValue_InContainer(Graph):nullptr;
   Private=Private&&NodeGraph&&NodeGraph->IsIn(System)&&NodeGraph->GetOutermost()==System->GetOutermost();
   Row->SetStringField(TEXT("node_graph"),NodeGraph?NodeGraph->GetPathName():TEXT(""));
   if(const auto* Nodes=NodeGraph?FindFProperty<FArrayProperty>(NodeGraph->GetClass(),TEXT("Nodes")):nullptr)
   {
    const auto* Inner=CastField<FObjectPropertyBase>(Nodes->Inner);FScriptArrayHelper Array(Nodes,Nodes->ContainerPtrToValuePtr<void>(NodeGraph));
    Private=Private&&Inner;
    if(Inner)for(int32 I=0;I<Array.Num();++I)
    {const UObject* Node=Inner->GetObjectPropertyValue(Array.GetRawPtr(I));Private=Private&&Node&&Node->IsIn(System)&&Node->GetOutermost()==System->GetOutermost();}
   }
   else Private=false;
  }
  Row->SetArrayField(TEXT("renderers"),Renderers);Row->SetBoolField(TEXT("private"),Private);Good=Good&&Private;
  Rows.Add(MakeShared<FJsonValueObject>(Row));
 }
 Root->SetStringField(TEXT("status"),Good?TEXT("PASS"):TEXT("FAIL"));Root->SetArrayField(TEXT("emitters"),Rows);
 FString Result;auto Writer=TJsonWriterFactory<>::Create(&Result);FJsonSerializer::Serialize(Root,Writer);return Result;
#else
 return TEXT("{\"status\":\"UNAVAILABLE_OUTSIDE_EDITOR\"}");
#endif
}
