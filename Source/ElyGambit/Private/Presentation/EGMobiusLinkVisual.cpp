#include "Presentation/EGMobiusLinkVisual.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Presentation/EGSourceEffect.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

AEGMobiusLinkVisual::AEGMobiusLinkVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));SetActorEnableCollision(false);
}
AEGMobiusLinkVisual* AEGMobiusLinkVisual::Spawn(AEGChessUnit* Source,const TArray<AEGChessUnit*>& Targets,float Duration,bool bConnection,const FVector* SourceOrigin)
{
 if(!Source||!Source->GetWorld()||Source->GetWorld()->GetNetMode()==NM_DedicatedServer||Duration<=0)return nullptr;
 auto* V=Source->GetWorld()->SpawnActor<AEGMobiusLinkVisual>();if(!V)return nullptr;
 V->SetOwner(Source);V->Caster=Source;V->Battle=Source->GetHeroSkill()->Battle;
 V->Round=V->Battle.IsValid()?V->Battle->GetRoundId():FGuid();V->bLink=bConnection;
 if(SourceOrigin&&!SourceOrigin->ContainsNaN()){V->NativeOrigin=*SourceOrigin;V->bFixedNativeOrigin=true;}
 for(auto* T:Targets)if(IsValid(T))V->Members.AddUnique(T);
 const auto* Definition=Source->GetDefinition();
 const auto* Data=Definition?Cast<UEGMobiusSkillDefinition>(Definition->SkillDefinition):nullptr;
 if(Data)
 {
  if(Data->bUseNiagaraPresentation&&!Data->bUseSourceLinks)
  {
   auto* System=Data->NiagaraLink.Get();
   const float Width=bConnection?Data->NiagaraLinkWidth:Data->NiagaraDischargeWidth;
   if(!IsValid(System)||!FMath::IsFinite(Width)||Width<=0.f){V->Destroy();return nullptr;}
   V->NiagaraSource=System;V->NativeWidth=Width;
   for(int32 I=0;I<FMath::Max(1,Targets.Num());++I)
   {
    auto* C=NewObject<UNiagaraComponent>(V);V->AddInstanceComponent(C);C->SetupAttachment(V->GetRootComponent());
    C->SetAutoActivate(false);C->SetAutoDestroy(false);C->SetAsset(System);C->RegisterComponent();
    C->SetVariableFloat(TEXT("User.BeamWidth"),Width);C->Activate(true);V->NiagaraSegments.Add(C);
   }
   V->Refresh(Duration);V->Tick(0);return V;
  }
  const auto& Reference=bConnection?Data->LinkSourceEffect:Data->DischargeSourceEffect;
  auto* Effect=Reference.LoadSynchronous();
  if(IsValid(Effect)&&Effect->Tracks.Num()==1&&!Effect->Tracks[0].Samples.IsEmpty()
     &&FMath::IsFinite(Effect->Tracks[0].Duration)&&Effect->Tracks[0].Duration>UE_KINDA_SMALL_NUMBER)
  {
   const auto& Track=Effect->Tracks[0];
   auto* SourceMesh=Track.Mesh.LoadSynchronous();auto* SourceMaterial=Track.Material.LoadSynchronous();
   if(IsValid(SourceMesh)&&IsValid(SourceMaterial)&&!Track.bSourceDepthOnly&&!Track.bSourceDistortion)
   {
    const float Width=bConnection?Data->LinkSourceWidth:Data->DischargeSourceWidth;
    if(!FMath::IsFinite(Width)||Width<=0.f){V->Destroy();return nullptr;}
    V->NativeSource=Effect;V->NativeWidth=FMath::Max(1.f,Width);
    const int32 Count=FMath::Max(1,Targets.Num());
    for(int32 I=0;I<Count;++I)
    {
     auto* C=NewObject<UStaticMeshComponent>(V);V->AddInstanceComponent(C);C->SetupAttachment(V->GetRootComponent());
     C->SetStaticMesh(SourceMesh);C->SetMaterial(0,SourceMaterial);
     C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);C->RegisterComponent();
     V->Segments.Add(C);V->NativeMaterials.Add(C->CreateDynamicMaterialInstance(0));
    }
    V->Refresh(Duration);V->Tick(0);return V;
   }
  }
  // An explicitly configured source must not silently become a generic bolt.
  if(!Reference.IsNull()){V->Destroy();return nullptr;}
 }
 V->Material=Source->GetDefinition()->GetAttackMaterial().LoadSynchronous();
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder"));
 // Four small segments per connection keep the bolts readable at board scale.
 for(int32 I=0;I<FMath::Max(1,Targets.Num())*4;++I)
 {
  auto* C=NewObject<UStaticMeshComponent>(V);V->AddInstanceComponent(C);C->SetupAttachment(V->GetRootComponent());
  C->SetStaticMesh(Mesh);if(V->Material)C->SetMaterial(0,V->Material);
  C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);C->RegisterComponent();V->Segments.Add(C);
 }
 if(bConnection)
 {
  auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane"));
  auto* Ring=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
  for(int32 I=0;I<Targets.Num();++I)
  {
   auto* C=NewObject<UStaticMeshComponent>(V);V->AddInstanceComponent(C);C->SetupAttachment(V->GetRootComponent());
   C->SetStaticMesh(Plane);C->SetMaterial(0,Ring);C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);C->RegisterComponent();
   if(auto* MID=C->CreateDynamicMaterialInstance(0))MID->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.15f,1.f,.45f));
   V->Marks.Add(C);
  }
 }
 V->Refresh(Duration);V->Tick(0);return V;
}
AEGMobiusLinkVisual* AEGMobiusLinkVisual::SpawnPulse(AEGChessUnit* Source,AEGChessUnit* From,AEGChessUnit* To)
{
 if(!IsValid(From)||!IsValid(To))return nullptr;
 const FVector Origin=From->GetActorLocation();
 auto* V=Spawn(Source,{To},.28f,false,&Origin);
 if(V){V->bPulse=true;V->PulseOrigin=From;V->NativeWidth*=1.6f;V->Tick(0.f);}
 return V;
}
void AEGMobiusLinkVisual::Refresh(float Duration){End=Age+FMath::Max(0.f,Duration);}

void AEGMobiusLinkVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Caster.IsValid()||!IEGCombatant::Execute_IsAlive(Caster.Get())||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round||Age>=End){Destroy();return;}
 const bool bRunning=Battle->IsCombatRunning();
 for(const auto& C:NiagaraSegments)if(IsValid(C.Get()))C->SetPaused(!bRunning);
 if(!bRunning)return;
 Age+=FMath::Max(0.f,DeltaSeconds);
 if(Age>=End){Destroy();return;}
 if(bPulse&&PulseOrigin.IsValid())NativeOrigin=PulseOrigin->GetActorLocation()+FVector(0,0,-88+FMath::Max(70.f,PulseOrigin->GetDefinition()->GetPresentationHeight()*.38f));
 auto PulseSpan=[this](FVector& Start,FVector& Finish)
 {
  if(!bPulse)return;
  const FVector Full=Finish-Start;
  const float Head=FMath::Clamp(Age/FMath::Max(.01f,End)*1.15f,0.f,1.f);
  Finish=Start+Full*Head;
  Start+=Full*FMath::Max(0.f,Head-.18f);
 };
 TArray<AEGChessUnit*> Visible;
 for(const auto& T:Members)if(T.IsValid()&&IEGCombatant::Execute_IsAlive(T.Get())&&T->GetHeroSkill()->IsTargetable())Visible.Add(T.Get());
 for(const auto& C:Segments)C->SetVisibility(false);
 for(const auto& C:Marks)C->SetVisibility(false);
 if(NiagaraSource)
 {
  const int32 Count=bLink?Visible.Num()-1:Visible.Num();
  auto Point=[](AEGChessUnit* U){return U->GetActorLocation()+FVector(0,0,-88+FMath::Max(70.f,U->GetDefinition()->GetPresentationHeight()*.38f));};
  for(int32 I=0;I<NiagaraSegments.Num();++I)
  {
   auto* C=NiagaraSegments[I].Get();if(!IsValid(C))continue;
   if(I>=Count){C->SetVisibility(false);continue;}
   FVector Start=bFixedNativeOrigin&&!bLink?NativeOrigin:Point(bLink?Visible[0]:Caster.Get());
   FVector Finish=Point(Visible[bLink?I+1:I]);
   PulseSpan(Start,Finish);
   C->SetVariableVec3(TEXT("User.BeamStart"),Start);C->SetVariableVec3(TEXT("User.BeamEnd"),Finish);
   C->SetVisibility(!Start.Equals(Finish,1.f));
  }
  return;
 }
 if(NativeSource)
 {
  const auto& Track=NativeSource->Tracks[0];
  const float Local=FMath::Fmod(Age,FMath::Max(.01f,Track.Duration));
  const float Frame=FMath::Clamp(Local/FMath::Max(.01f,Track.Duration),0.f,1.f)*(Track.Samples.Num()-1);
  const int32 Low=FMath::FloorToInt(Frame),High=FMath::Min(Low+1,Track.Samples.Num()-1);const float Blend=Frame-Low;
  const auto& A=Track.Samples[Low];const auto& B=Track.Samples[High];
  const int32 Count=bLink?Visible.Num()-1:Visible.Num();
  auto Point=[](AEGChessUnit* U){return U->GetActorLocation()+FVector(0,0,-88+FMath::Max(70.f,U->GetDefinition()->GetPresentationHeight()*.38f));};
  auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
  const FVector Size=Segments[0]->GetStaticMesh()->GetBounds().BoxExtent*2.f;
  for(int32 I=0;I<Count&&Segments.IsValidIndex(I);++I)
  {
   FVector Start=bFixedNativeOrigin&&!bLink?NativeOrigin:Point(bLink?Visible[0]:Caster.Get()),Finish=Point(Visible[bLink?I+1:I]);
   PulseSpan(Start,Finish);
   const FVector Delta=Finish-Start;const float Length=Delta.Size();if(Length<1.f)continue;
   const FVector Direction=Delta/Length,Middle=(Start+Finish)*.5f;
   FVector Side=FVector::CrossProduct(Camera?Camera->GetCameraLocation()-Middle:FVector::UpVector,Direction).GetSafeNormal();
   if(Side.IsNearlyZero())Side=FVector::CrossProduct(FVector::RightVector,Direction).GetSafeNormal();
   auto* C=Segments[I].Get();
   // Extracted ribbons run along X; the workshop bolt runs along Z and is
   // authored from 0 to 180cm. Account for both its axis and bounds origin.
   const bool bLongZ=Size.Z>Size.X;
   const FQuat Rotation=(bLongZ?FRotationMatrix::MakeFromZY(Direction,Side):FRotationMatrix::MakeFromXY(Direction,Side)).ToQuat();
   const FVector Scale=bLongZ
    ?FVector(NativeWidth/FMath::Max(1.f,Size.X),NativeWidth/FMath::Max(1.f,Size.Y),Length/FMath::Max(1.f,Size.Z))
    :FVector(Length/FMath::Max(1.f,Size.X),NativeWidth/FMath::Max(1.f,Size.Y),1.f);
   C->SetWorldLocationAndRotation(Middle-Rotation.RotateVector(C->GetStaticMesh()->GetBounds().Origin*Scale),Rotation);
   C->SetWorldScale3D(Scale);C->SetVisibility(true);
   if(auto* MID=NativeMaterials[I].Get())
   {
    MID->SetVectorParameterValue(TEXT("SourceColor"),FMath::Lerp(A.Color,B.Color,Blend));
    MID->SetVectorParameterValue(TEXT("SourceCustom1"),FMath::Lerp(A.CustomData1,B.CustomData1,Blend));
    MID->SetVectorParameterValue(TEXT("SourceCustom2"),FMath::Lerp(A.CustomData2,B.CustomData2,Blend));
    MID->SetVectorParameterValue(TEXT("SourceParticleUV"),A.TextureUV);
    MID->SetScalarParameterValue(TEXT("SourceAge"),Local);MID->SetScalarParameterValue(TEXT("SourceTimeline"),Age);
   }
  }
  return;
 }
 if(bLink&&Visible.Num()>1)for(int32 I=0;I<Visible.Num();++I)
 {
  auto* C=Marks[I].Get();auto* U=Visible[I];
  const float Radius=FMath::Clamp(U->GetDefinition()->GetPresentationHeight()*.3f,90.f,160.f);
  C->SetWorldLocation(U->GetActorLocation()+FVector(0,0,-84));C->SetWorldScale3D(FVector(Radius/50.f,Radius/50.f,1));C->SetVisibility(true);
 }
 auto Point=[](AEGChessUnit* U){return U->GetActorLocation()+FVector(0,0,-88+FMath::Max(70.f,U->GetDefinition()->GetPresentationHeight()*.38f));};
 const int32 Count=bLink?Visible.Num()-1:Visible.Num();
 for(int32 I=0;I<Count;++I)
 {
  FVector Start=bFixedNativeOrigin&&!bLink?NativeOrigin:Point(bLink?Visible[0]:Caster.Get()),Finish=Point(Visible[bLink?I+1:I]);
  PulseSpan(Start,Finish);
  FVector Side=FVector::CrossProduct((Finish-Start).GetSafeNormal(),FVector::UpVector).GetSafeNormal();
  FVector Prev=Start;
  for(int32 J=0;J<4;++J)
  {
   FVector Next=FMath::Lerp(Start,Finish,(J+1)/4.f);
   if(J<3)Next+=Side*((J%2?1:-1)*(bLink?9.f:22.f));
   auto* C=Segments[I*4+J].Get();const FVector Delta=Next-Prev;
   if(Delta.IsNearlyZero()){Prev=Next;continue;}
   C->SetWorldLocation((Prev+Next)*.5f);C->SetWorldRotation(FQuat::FindBetweenNormals(FVector::UpVector,Delta.GetSafeNormal()));
   const float Width=bLink?5.f:8.f*(1-Age/FMath::Max(.01f,End));
   C->SetWorldScale3D(FVector(Width/50.f,Width/50.f,Delta.Size()/100.f));C->SetVisibility(true);Prev=Next;
  }
 }
}
void AEGMobiusLinkVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 for(const auto& C:NiagaraSegments)if(IsValid(C.Get()))C->DeactivateImmediate();
 NiagaraSegments.Reset();NiagaraSource=nullptr;
 Super::EndPlay(Reason);
}
