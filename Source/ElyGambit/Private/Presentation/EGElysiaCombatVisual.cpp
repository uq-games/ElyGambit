#include "Presentation/EGElysiaCombatVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"

AEGElysiaCombatVisual::AEGElysiaCombatVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;
 SourceRoot=CreateDefaultSubobject<USceneComponent>(TEXT("OriginalSourceRoot"));SetRootComponent(SourceRoot);
 SetActorEnableCollision(false);
}
bool AEGElysiaCombatVisual::BuildSource(UEGSourcePreviewDefinition* Definition,float Scale)
{
 if(!IsValid(Definition)||Definition->Motions.IsEmpty()||!FMath::IsFinite(Scale)||Scale<=0)return false;
 auto* FX=GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,GetOwner(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!FX)return false;
 FX->Definition=Definition;FX->bAutoPlay=false;FX->bLoop=false;FX->FinishSpawning(FTransform::Identity);
 FX->SetActorTickEnabled(false);FX->AttachToActor(this,FAttachmentTransformRules::KeepRelativeTransform);
 FX->SetActorRelativeScale3D(FVector(Scale));FX->SetSourceTime(0);
 SourceVisual=FX;SourceDuration=Definition->Motions[0].Duration;return true;
}
UNiagaraComponent* AEGElysiaCombatVisual::SpawnCue(const FEGElysiaNiagaraCue& Cue,float RadiusScale,bool OneShot,float CueRainRadius,float BoardDiameter)
{
 if(Cue.System.IsNull()||Cue.Transform.ContainsNaN()||!FMath::IsFinite(RadiusScale)||RadiusScale<=0.f)return nullptr;
 auto* System=Cue.System.LoadSynchronous();if(!System)return nullptr;
 auto* Component=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
 if(!Component)return nullptr;
 AddInstanceComponent(Component);Component->SetupAttachment(SourceRoot);
 FTransform Transform=Cue.Transform;Transform.SetScale3D(Transform.GetScale3D()*RadiusScale);
 Component->SetRelativeTransform(Transform);Component->SetAsset(System);
 Component->SetAutoActivate(false);Component->SetAutoDestroy(OneShot);Component->RegisterComponent();
 if(FMath::IsFinite(CueRainRadius)&&CueRainRadius>0.f)Component->SetVariableFloat(TEXT("User.RainRadius"),CueRainRadius);
 if(FMath::IsFinite(BoardDiameter)&&BoardDiameter>0.f)Component->SetVariableFloat(TEXT("User.BoardDiameter"),BoardDiameter);
 Component->Activate(true);
 return Component;
}
AEGElysiaCombatVisual* AEGElysiaCombatVisual::Spawn(AEGChessUnit* Source,AEGChessUnit* Target,const FVector& From,const FVector& To,float Seconds,int32 Kind,bool Charged,bool Enhanced,bool Ricochet,float AreaRadius)
{
 if(!IsValid(Source)||Source->GetNetMode()==NM_DedicatedServer||!Source->GetDefinition())return nullptr;
 auto* Skill=Source->GetHeroSkill();
 const auto* Data=Cast<UEGElysiaSkillDefinition>(Source->GetDefinition()->SkillDefinition);
 auto* Context=Skill?Skill->Battle.Get():nullptr;
 if(!Data||!Context||!Context->IsBattleActive()||!FMath::IsFinite(Seconds)||Seconds<=0||From.ContainsNaN()||To.ContainsNaN())return nullptr;
 if(Kind<0||Kind>6||!FMath::IsFinite(AreaRadius)||AreaRadius<0.f)return nullptr;
 const bool bRain=Kind==2&&AreaRadius>0.f;
 if((Kind==2&&!bRain&&Data->bArrowRainUltimate)||(Kind==6&&Data->bArrowRainUltimate))return nullptr;
 // Resolve configured cues on first use if the asynchronous preload has not completed.
 const FEGElysiaNiagaraCue* Cue=Kind==0?(Ricochet&&!Data->RicochetCue.System.IsNull()?&Data->RicochetCue:
  (Charged||Enhanced)&&!Data->ChargedArrowCue.System.IsNull()?&Data->ChargedArrowCue:&Data->ArrowCue):
  Kind==3?&Data->ImpactCue:Kind==4?&Data->ChargeCue:Kind==6?&Data->DomeExitCue:nullptr;
 if(Kind!=1&&Kind!=2&&(!Cue||Cue->System.IsNull()))return nullptr;
 auto* Visual=Source->GetWorld()->SpawnActor<AEGElysiaCombatVisual>();if(!Visual)return nullptr;
 Visual->SetOwner(Source);Visual->Caster=Source;Visual->TargetUnit=Target;Visual->Battle=Context;
 Visual->Round=Context->GetRoundId();Visual->VisualKind=Kind;Visual->ActionToken=Skill->ActionId;
 Visual->Start=From;Visual->End=To;Visual->Duration=Seconds;Visual->SetActorLocation(From);
 Visual->bRainVisual=bRain;
 if(bRain)Visual->RainRadius=AreaRadius;
 const auto* Behavior=Cast<UEGElysiaSkillBehavior>(Skill->GetBehavior());
 const float Radius=!bRain&&Behavior?Behavior->GetEmpoweredRadius():FMath::Max(0.f,Data->EmpoweredRadius);
 const float RadiusScale=Radius/FMath::Max(1.f,Data->DomeCueReferenceRadius);
 if(bRain)
 {
  // The gameplay action supplies its locked ground point and world-space
  // radius. Do not project onto a different centre or create a second clock.
  Visual->LoopCue=Visual->SpawnCue(Data->RainCue,1.f,false,AreaRadius);
  Visual->PetalCue=Visual->SpawnCue(Data->RainPetalCue,AreaRadius/350.f,false,AreaRadius);
  FEGElysiaNiagaraCue Area=Data->RainAreaCue;
  Area.Transform.AddToTranslation(FVector(0.f,0.f,15.f));
  Area.Transform.SetScale3D(FVector::OneVector);
  Visual->RainAreaCue=Visual->SpawnCue(Area,1.f,false,0.f,2.f*AreaRadius);
 }
 else if(Kind==2)
 {
  // These assets and this shell are needed only by the legacy enemy field.
  // An ordinary arrow-rain actor neither loads nor creates the old dome.
  Visual->DomeShell=NewObject<UStaticMeshComponent>(Visual,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
  if(Visual->DomeShell)
  {
   Visual->AddInstanceComponent(Visual->DomeShell);Visual->DomeShell->SetupAttachment(Visual->SourceRoot);
   Visual->DomeShell->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere")));
   Visual->DomeShell->SetMaterial(0,LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/FXs/ARPGVFXPack02/Material_Inst/MI_ShieldSphere_Inst.MI_ShieldSphere_Inst")));
   Visual->DomeShell->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->DomeShell->SetGenerateOverlapEvents(false);
   Visual->DomeShell->SetCanEverAffectNavigation(false);Visual->DomeShell->SetCastShadow(false);Visual->DomeShell->SetVisibility(false);
   Visual->DomeShell->SetTranslucentSortPriority(1);Visual->DomeShell->RegisterComponent();
  }
  FVector FieldLocation=From;
  if(const auto* GM=Source->GetWorld()->GetAuthGameMode<AEGChessGameMode>())
   if(const auto* Board=GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr)
   {
    const FTransform BoardTransform=Board->GetActorTransform();
    if(!BoardTransform.ContainsNaN())
    {FVector Local=BoardTransform.InverseTransformPosition(From);Local.Z=0.f;FieldLocation=BoardTransform.TransformPosition(Local);}
   }
  if(FMath::IsFinite(Data->DomeFloorOffset))FieldLocation.Z+=FMath::Max(1.f,Data->DomeFloorOffset);
  Visual->SetActorLocation(FieldLocation);
  if(RadiusScale>0.f)
  {
   Visual->SpawnCue(Data->DomeEntryCue,RadiusScale,true);
   Visual->LoopCue=Visual->SpawnCue(Data->DomeLoopCue,RadiusScale,false);
   Visual->PetalCue=Visual->SpawnCue(Data->DomePetalCue,RadiusScale,false);
  }
  if(Visual->DomeShell)if(auto* Material=Data->DomeShellMaterial.LoadSynchronous())Visual->DomeShell->SetMaterial(0,Material);
  if(Visual->DomeShell&&Visual->DomeShell->GetStaticMesh()&&Visual->DomeShell->GetMaterial(0))
  {
   Visual->DomeShell->SetRelativeLocation(FVector::ZeroVector);
   Visual->DomeShell->SetRelativeScale3D(FVector(Radius/50.f,Radius/50.f,Radius*.4f/50.f));
   Visual->DomeShellMaterial=Visual->DomeShell->CreateDynamicMaterialInstance(0);
   if(Visual->DomeShellMaterial)
   {
    Visual->DomeShellMaterial->SetVectorParameterValue(TEXT("Tint"),FLinearColor(1.f,.22f,.55f,.08f));
    Visual->DomeShellMaterial->SetVectorParameterValue(TEXT("Color"),FLinearColor(1.f,.22f,.55f,.08f));
    Visual->DomeShellMaterial->SetScalarParameterValue(TEXT("Opacity"),.08f);
    Visual->DomeShell->SetVisibility(true);
   }
  }
 }
 else if(Kind!=1)
 {
  const float CueScale=Kind==6?RadiusScale:Kind==3?(Charged?1.5f:1.2f):1.f;
  if(Cue)Visual->LoopCue=Visual->SpawnCue(*Cue,CueScale,false);
  if(!IsValid(Visual->LoopCue)){Visual->Destroy();return nullptr;}
  if(Kind==6)Visual->PetalCue=Visual->SpawnCue(Data->DomeExitPetalCue,CueScale,true);
 }
 if(Kind==0)Visual->SetActorRotation((To-From).Rotation());
 return Visual;
}
void AEGElysiaCombatVisual::PulseRain(int32 PulseIndex)
{
 if(VisualKind!=2||!bRainVisual||bRainFinishing||PulseIndex<=LastRainPulse||!Caster.IsValid()||!IEGCombatant::Execute_IsAlive(Caster.Get())||
  !Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round)return;
 LastRainPulse=PulseIndex;
 const auto* Definition=Caster->GetDefinition();
 const auto* Data=Definition?Cast<UEGElysiaSkillDefinition>(Definition->SkillDefinition):nullptr;
 if(Data)SpawnCue(Data->RainImpactCue,1.f,true,RainRadius);
}
void AEGElysiaCombatVisual::FinishRain()
{
 if(VisualKind!=2||!bRainVisual||bRainFinishing)return;
 bRainFinishing=true;
 if(IsValid(LoopCue))LoopCue->Deactivate();
 if(IsValid(PetalCue))PetalCue->Deactivate();
 if(IsValid(RainAreaCue))RainAreaCue->DeactivateImmediate();
 // The actor remains owned/tracked by the originating battle during this
 // cosmetic tail; death, cancellation or round end still destroy it at once.
 SetLifeSpan(.65f);
}
AEGElysiaCombatVisual* AEGElysiaCombatVisual::FinishDome()
{
 if(VisualKind!=2||bRainVisual)return nullptr;
 AEGElysiaCombatVisual* Exit=nullptr;
 if(auto* Source=Caster.Get())
  if(const auto* Data=Source->GetDefinition()?Cast<UEGElysiaSkillDefinition>(Source->GetDefinition()->SkillDefinition):nullptr)
   if(!Data->DomeExitCue.System.IsNull()&&FMath::IsFinite(Data->DomeExitSeconds)&&Data->DomeExitSeconds>0.f)
    Exit=Spawn(Source,nullptr,GetActorLocation(),GetActorLocation(),Data->DomeExitSeconds,6);
 Destroy();
 return Exit;
}
void AEGElysiaCombatVisual::SetSeedStacks(int32 Count)
{
 if(VisualKind!=1||ShownSeedStacks==Count)return;
 if(IsValid(SourceVisual))SourceVisual->Destroy();SourceVisual=nullptr;ShownSeedStacks=Count;
 const auto* Data=Caster.IsValid()&&Caster->GetDefinition()?Cast<UEGElysiaSkillDefinition>(Caster->GetDefinition()->SkillDefinition):nullptr;
 if(Data&&Data->SeedAssemblies.IsValidIndex(Count-1))BuildSource(Data->SeedAssemblies[Count-1].LoadSynchronous(),1.f);
}
void AEGElysiaCombatVisual::CompleteFlight(const FVector& Contact)
{
 if(VisualKind!=0)return;
 if(!Contact.ContainsNaN())SetActorLocation(Contact);
 Destroy();
}
void AEGElysiaCombatVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Caster.IsValid()||!IEGCombatant::Execute_IsAlive(Caster.Get())||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round){Destroy();return;}
 auto* Skill=Caster->GetHeroSkill();if(!Skill){Destroy();return;}
 if(!Battle->IsCombatRunning())return;
 if(VisualKind==4&&(!ActionToken.IsValid()||Skill->ActionId!=ActionToken)){Destroy();return;}
 if(((VisualKind==2&&!bRainFinishing)||VisualKind==4)&&IsValid(LoopCue)&&!LoopCue->IsActive())LoopCue->Activate(true);
 if(VisualKind==2&&!bRainFinishing&&IsValid(PetalCue)&&!PetalCue->IsActive())PetalCue->Activate(true);
 if(bRainVisual&&!bRainFinishing&&IsValid(RainAreaCue)&&!RainAreaCue->IsActive())RainAreaCue->Activate(true);
 Age+=DeltaSeconds;
 if(VisualKind==0&&FlightDeadline>0.)Age=FMath::Clamp(float(GetWorld()->GetTimeSeconds()-(FlightDeadline-Duration)),0.f,Duration);
 if(VisualKind==0)
 {
  if(!TargetUnit.IsValid()||!IEGCombatant::Execute_IsAlive(TargetUnit.Get())){Destroy();return;}
  End=TargetUnit->GetActorLocation()+FVector(0,0,TargetUnit->GetDefinition()->GetPresentationHeight()*.45f-TargetUnit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
  SetActorLocation(FMath::Lerp(Start,End,FMath::Clamp(Age/Duration,0.f,1.f)));SetActorRotation((End-Start).Rotation());
 }
 else if(VisualKind==1)
 {
  if(!TargetUnit.IsValid()||!IEGCombatant::Execute_IsAlive(TargetUnit.Get())){Destroy();return;}
  SetActorLocation(TargetUnit->GetActorLocation()-FVector(0,0,88));
 }
 else if(VisualKind==4)SetActorLocation(Caster->GetActionOrigin());
 if(IsValid(SourceVisual))
 {
  const float SourceTime=VisualKind==0?FMath::Fmod(Age,.3f):
   (VisualKind==1||VisualKind==4)?FMath::Fmod(Age,FMath::Max(.001f,SourceDuration)):
   FMath::Min(Age/FMath::Max(Duration,.001f)*SourceDuration,FMath::Max(0.f,SourceDuration-.001f));
  SourceVisual->SetSourceTime(SourceTime);
 }
 // The behavior owns rain lifetime; action cancellation and EndBattle also
 // destroy it. A cosmetic clock never ends or extends a combat transaction.
 if(VisualKind!=2&&Age>=Duration)Destroy();
}
void AEGElysiaCombatVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 if(IsValid(PetalCue)){PetalCue->DeactivateImmediate();PetalCue->DestroyComponent();}
 PetalCue=nullptr;
 if(IsValid(SourceVisual))SourceVisual->Destroy();SourceVisual=nullptr;
 Super::EndPlay(Reason);
}
