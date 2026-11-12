#include "Presentation/EGAponiaDisciplineVisual.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

AEGAponiaDisciplineVisual::AEGAponiaDisciplineVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 SetActorEnableCollision(false);
}
AEGAponiaDisciplineVisual* AEGAponiaDisciplineVisual::Spawn(AEGChessUnit* Owner,UEGAponiaSkillBehavior* Behavior,UNiagaraSystem* Definition)
{
 if(!IsValid(Owner)||!IsValid(Behavior)||!IsValid(Definition)||Owner->GetNetMode()==NM_DedicatedServer)return nullptr;
 auto* V=Owner->GetWorld()->SpawnActor<AEGAponiaDisciplineVisual>();if(!V)return nullptr;
 V->SetOwner(Owner);V->Caster=Owner;V->State=Behavior;V->Data=Definition;
 V->Battle=Owner->GetHeroSkill()->Battle;V->Round=V->Battle.IsValid()?V->Battle->GetRoundId():FGuid();
 // The pack light-core variant makes the six discipline slots readable.
 // Its own emitters/materials own animation and graceful release.
 for(int32 Index=0;Index<6;++Index)
 {
  auto* Node=NewObject<UNiagaraComponent>(V);V->AddInstanceComponent(Node);Node->SetupAttachment(V->GetRootComponent());
  Node->SetAutoActivate(false);Node->SetAsset(Definition);Node->SetCastShadow(false);
  Node->SetCollisionEnabled(ECollisionEnabled::NoCollision);Node->RegisterComponent();V->Nodes.Add(Node);
 }
 V->Tick(0.f);return V;
}
void AEGAponiaDisciplineVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Caster.IsValid()||!State.IsValid()||!Data||!IEGCombatant::Execute_IsAlive(Caster.Get())||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round){Destroy();return;}
 const bool Running=Battle->IsCombatRunning();
 for(const auto& Node:Nodes)Node->SetPaused(!Running);
 if(!Running)return;
 Age+=FMath::Max(0.f,DeltaSeconds);SetActorLocation(Caster->GetActorLocation());
 if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
 {
  FVector Facing=Camera->GetCameraLocation()-GetActorLocation();Facing.Z=0.f;
  SetActorRotation(Facing.Rotation());
 }
 const int32 Count=FMath::Clamp(State->GetVisibleDiscipline(),0,6);
 for(int32 Index=0;Index<Nodes.Num();++Index)
 {
  const float Angle=2.f*PI*Index/6.f+PI/6.f;
  Nodes[Index]->SetRelativeLocation(FVector(FMath::Cos(Angle)*125.f,FMath::Sin(Angle)*125.f,15.f));
  Nodes[Index]->SetRelativeScale3D(FVector(.6f));
  if(Index<Count&&(Index>=PreviousCount||Nodes[Index]->IsComplete()))Nodes[Index]->Activate(true);
  else if(Index>=Count&&Index<PreviousCount)Nodes[Index]->DeactivateImmediate();
 }
 PreviousCount=Count;
}
