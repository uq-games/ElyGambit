#include "Presentation/EGCorruptionBarrierVisual.h"
#include "Presentation/EGBarrierCameraShake.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "CoreGlobals.h"
#include "HAL/PlatformTime.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

AEGCorruptionBarrierVisual::AEGCorruptionBarrierVisual()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.bTickEvenWhenPaused=true;
 bReplicates=false;
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 SetActorEnableCollision(false);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> SurfaceMat(TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Combat20261101/Materials/M_C2_StarSea_Border"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> DigitalMat(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionDigitalWall"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> SourceStyleMat(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionSourceStyleWall"));
 static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> ExternalScanMat(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionExternalScanWall"),LOAD_Quiet|LOAD_NoWarn);
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> BarMat(TEXT("/Game/Assets/Environments/Board/Materials/M_ChessBoardPerimeter"));
 static ConstructorHelpers::FObjectFinder<UNiagaraSystem> Impact(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/NS_CorruptionSpreadImpact"));
 WallImpactSystem=Impact.Object;
 auto Make=[this](const TCHAR* Name,UStaticMesh* Mesh,UMaterialInterface* Mat)
 {
  auto* C=CreateDefaultSubobject<UStaticMeshComponent>(Name);
  C->SetupAttachment(GetRootComponent());C->SetStaticMesh(Mesh);if(Mat)C->SetMaterial(0,Mat);
  C->SetCollisionEnabled(ECollisionEnabled::NoCollision);C->SetGenerateOverlapEvents(false);
  C->SetCanEverAffectNavigation(false);C->SetCastShadow(false);C->SetVisibility(false);
  return C;
 };
 auto* ExternalScan=ExternalScanMat.Get();
 Surface=Make(TEXT("WallSurface"),Plane.Object,ExternalScan?ExternalScan:(SourceStyleMat.Succeeded()?SourceStyleMat.Object.Get():(DigitalMat.Succeeded()?DigitalMat.Object.Get():SurfaceMat.Object.Get())));
 if(ExternalScan||SourceStyleMat.Succeeded())Surface->ComponentTags.Add(TEXT("EGSourceStyleCorruptionWall"));
 else if(DigitalMat.Succeeded())Surface->ComponentTags.Add(TEXT("EGDigitalCorruptionWall"));
 Backplane=Make(TEXT("WallBackplane"),Plane.Object,SurfaceMat.Object);
 Backplane->SetTranslucentSortPriority(0);Surface->SetTranslucentSortPriority(1);
 for(int32 I=0;I<16;++I)Bars.Add(Make(*FString::Printf(TEXT("WallGrid%02d"),I),Cube.Object,BarMat.Object));
 for(int32 I=0;I<20;++I)Fragments.Add(Make(*FString::Printf(TEXT("DigitalFragment%02d"),I),Cube.Object,BarMat.Object));
 for(int32 I=0;I<8;++I)Cracks.Add(Make(*FString::Printf(TEXT("WallCrack%02d"),I),Cube.Object,BarMat.Object));
}

bool AEGCorruptionBarrierVisual::InitializeVisual(FVector Center,FVector WidthDirection,float Width,float Height)
{
 WidthDirection.Z=0.f;
 if(bInitialized||GetNetMode()==NM_DedicatedServer||Center.ContainsNaN()||WidthDirection.ContainsNaN()||
    !FMath::IsFinite(Width)||!FMath::IsFinite(Height)||Width<=0.f||Height<=0.f||!WidthDirection.Normalize())return false;
 if(auto* Boss=Cast<AEGChessUnit>(GetOwner()))
 {
  auto* Context=Boss->GetHeroSkill()?Boss->GetHeroSkill()->Battle.Get():nullptr;
  if(!IsValid(Context)||!Context->IsBattleActive())return false;
  Source=Boss;Battle=Context;Round=Context->GetRoundId();bContextBound=true;
 }
 WallWidth=Width;WallHeight=Height;bInitialized=true;LastRealSeconds=FPlatformTime::Seconds();
 SetActorLocationAndRotation(Center,FRotationMatrix::MakeFromXZ(WidthDirection,FVector::UpVector).Rotator());
 // Plane's original XY normal is Z: rotate it upright into the XZ wall.
 Surface->SetRelativeLocation(FVector(0,0,Height*.5f));Surface->SetRelativeRotation(FRotator(0.f,0.f,90.f));
 Surface->SetRelativeScale3D(FVector(Width/100.f,Height/100.f,1.f));
 Backplane->SetRelativeLocation(FVector(0,3.f,Height*.5f));Backplane->SetRelativeRotation(FRotator(0.f,0.f,90.f));
 Backplane->SetRelativeScale3D(FVector(Width/100.f,Height/100.f,1.f));
 SurfaceMaterial=Surface->CreateDynamicMaterialInstance(0);
 if(SurfaceMaterial)
 {
  const bool bSourceStyle=Surface->ComponentHasTag(TEXT("EGSourceStyleCorruptionWall"));
  const bool bDigital=Surface->ComponentHasTag(TEXT("EGDigitalCorruptionWall"));
  SurfaceMaterial->SetVectorParameterValue(TEXT("DomainTint"),bSourceStyle?FLinearColor(.52f,.025f,.95f,1.f):(bDigital?FLinearColor(.20f,.022f,.34f,1.f):FLinearColor(.32f,.055f,.52f,1.f)));
  SurfaceMaterial->SetScalarParameterValue(TEXT("DomainEmissive"),bSourceStyle?2.1f:(bDigital?1.35f:.55f));
  if(bSourceStyle)
  {
   SurfaceMaterial->SetScalarParameterValue(TEXT("GridColumns"),32.f);
   SurfaceMaterial->SetScalarParameterValue(TEXT("GridRows"),FMath::Clamp(32.f*Height/Width,2.f,32.f));
   SurfaceMaterial->SetScalarParameterValue(TEXT("GridStrength"),.4f);
   SurfaceMaterial->SetScalarParameterValue(TEXT("ExternalScanStrength"),.8f);
   SurfaceMaterial->SetScalarParameterValue(TEXT("GridSharpness"),26.f);
   SurfaceMaterial->SetScalarParameterValue(TEXT("PointBaseline"),1.f);
   SurfaceMaterial->SetScalarParameterValue(TEXT("NoiseStrength"),.55f);
  }
 }
 BackplaneMaterial=Backplane->CreateDynamicMaterialInstance(0);
 if(BackplaneMaterial){BackplaneMaterial->SetVectorParameterValue(TEXT("DomainTint"),FLinearColor(.018f,.004f,.032f,1.f));BackplaneMaterial->SetScalarParameterValue(TEXT("DomainEmissive"),.7f);}
 BarMaterial=Bars[0]->CreateDynamicMaterialInstance(0);
 if(BarMaterial){BarMaterial->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.22f,.018f,.38f,1.f));for(const auto& B:Bars)if(B)B->SetMaterial(0,BarMaterial);for(const auto& F:Fragments)if(F)F->SetMaterial(0,BarMaterial);for(const auto& C:Cracks)if(C)C->SetMaterial(0,BarMaterial);}
 Tick(0.f);return true;
}
void AEGCorruptionBarrierVisual::SetPresentationToken(FGuid Token)
{
 PresentationToken=Token;
 // Do not carry time across hold ownership changes.
 LastRealSeconds=FPlatformTime::Seconds();
}
void AEGCorruptionBarrierVisual::SetIntroRevealDelay(float Seconds)
{
 IntroRevealDelay=FMath::IsFinite(Seconds)?FMath::Clamp(Seconds,0.f,10.f):0.f;
}
void AEGCorruptionBarrierVisual::SetIntroElapsed(float Seconds)
{
 if(!bInitialized||!PresentationToken.IsValid()||!Battle.IsValid()||!FMath::IsFinite(Seconds)||
    !Battle->CanAdvancePresentationVisualsForToken(PresentationToken))return;
 Age=FMath::Max(Age,FMath::Max(0.f,Seconds));
 // The owning combat component may tick after this actor, so reflect the
 // authoritative sample in this frame without starting a second clock.
 Tick(0.f);
}
void AEGCorruptionBarrierVisual::SetIntegrity(float Fraction)
{
 if(!bInitialized||!FMath::IsFinite(Fraction)||EndAge>=0.f)return;
 const float NewValue=FMath::Clamp(Fraction,0.f,1.f);
 if(NewValue<Integrity)
 {
  HitFlash=1.f;
  if(LastHitAccentAge<0.f||Age-LastHitAccentAge>=.15f)
  {
   LastHitAccentAge=Age;
   const float Side=FMath::Sin(Age*17.f)*WallWidth*.28f;
   SpawnWallAccent(FVector(Side,0.f,WallHeight*.38f),.45f);
  }
 }
 Integrity=NewValue;
}
void AEGCorruptionBarrierVisual::Dissolve(bool bInBroken)
{
 if(!bInitialized||EndAge>=0.f)return;
 if(bInBroken)
  for(int32 I=-1;I<=1;++I)SpawnWallAccent(FVector(I*WallWidth*.3f,0.f,WallHeight*.48f),.55f);
 else SpawnWallAccent(FVector(0.f,0.f,WallHeight*.3f),.35f);
 bBroken=bInBroken;EndAge=Age;SetLifeSpan(.65f);
}
void AEGCorruptionBarrierVisual::SpawnWallAccent(FVector RelativeLocation,float Scale)
{
 if(!IsValid(WallImpactSystem)||!GetWorld()||GetNetMode()==NM_DedicatedServer||RelativeLocation.ContainsNaN()||!FMath::IsFinite(Scale)||Scale<=0.f)return;
 WallParticles.RemoveAll([](const TObjectPtr<UNiagaraComponent>& Particle){return !IsValid(Particle)||!Particle->IsActive();});
 if(WallParticles.Num()>=8)return;
 auto* Particle=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);if(!Particle)return;
 AddInstanceComponent(Particle);Particle->SetupAttachment(GetRootComponent());Particle->SetRelativeLocation(RelativeLocation);
 Particle->SetRelativeScale3D(FVector(Scale));Particle->SetAsset(WallImpactSystem);Particle->SetAutoActivate(false);Particle->SetAutoDestroy(true);
 Particle->RegisterComponent();Particle->Activate(true);WallParticles.Add(Particle);
}
void AEGCorruptionBarrierVisual::StopRevealShake()
{
 auto* Manager=ShakeManager.Get();auto* Shake=RevealShake.Get();
 if(IsValid(Manager)&&IsValid(Shake)&&Shake->HasLease(ShakeLease)&&Shake->IsActive())
 {
  Manager->StopCameraShake(Shake,true);
  // When the world is paused, remove our last cached offset without taking
  // ownership of any other shake or a newly switched view target.
  if(GetWorld()&&GetWorld()->IsPaused()&&ShakeViewTarget.IsValid()&&Manager->GetViewTarget()==ShakeViewTarget.Get())Manager->UpdateCamera(0.f);
 }
 RevealShake.Reset();ShakeManager.Reset();ShakeViewTarget.Reset();ShakeLease.Invalidate();
}
#if WITH_DEV_AUTOMATION_TESTS
bool AEGCorruptionBarrierVisual::HasRevealShakeForTests() const
{
 const auto* Shake=RevealShake.Get();return IsValid(Shake)&&Shake->IsActive()&&Shake->HasLease(ShakeLease);
}
#endif
void AEGCorruptionBarrierVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);if(!bInitialized)return;
 if(bContextBound&&(!Source.IsValid()||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round||
    !IEGCombatant::Execute_IsAlive(Source.Get()))){Destroy();return;}
 const double Now=FPlatformTime::Seconds();
 const bool bCanAdvance=!bContextBound||(Battle.IsValid()&&
  (PresentationToken.IsValid()?Battle->CanAdvancePresentationVisualsForToken(PresentationToken):Battle->CanAdvancePresentationVisuals()));
 float VisualDelta=bCanAdvance?FMath::Max(0.f,DeltaSeconds):0.f;
 if(GetWorld()&&GetWorld()->IsPaused())
  VisualDelta=bContextBound&&bCanAdvance
   ?static_cast<float>(FMath::Clamp(Now-LastRealSeconds,0.0,.1)):0.f;
 LastRealSeconds=Now;
 if(!PresentationToken.IsValid())Age+=VisualDelta;
 const float ParticleDelta=FMath::Max(0.f,Age-LastParticleAge);LastParticleAge=Age;
 if(GetWorld()&&GetWorld()->IsPaused()&&bCanAdvance&&ParticleDelta>SMALL_NUMBER)
  for(const auto& Particle:WallParticles)if(IsValid(Particle))Particle->AdvanceSimulation(1,FMath::Min(ParticleDelta,.1f));
 WallParticles.RemoveAll([](const TObjectPtr<UNiagaraComponent>& Particle){return !IsValid(Particle);});
 HitFlash=FMath::Max(0.f,HitFlash-VisualDelta*3.f);
 const float RevealAge=FMath::Max(0.f,Age-IntroRevealDelay);
 const float Reveal=FMath::Clamp(RevealAge/.7f,0.f,1.f);
 const float Fade=EndAge<0.f?1.f:1.f-FMath::Clamp((Age-EndAge)/.6f,0.f,1.f);
 const bool bSourceStyle=Surface->ComponentHasTag(TEXT("EGSourceStyleCorruptionWall"));
 const float IntroFlash=RevealAge<.3f?FMath::Sin(PI*FMath::Clamp(RevealAge/.3f,0.f,1.f)):0.f;
 if(!bRevealEffectsStarted&&RevealAge>0.f&&bCanAdvance)
 {
  bRevealEffectsStarted=true;
  SpawnWallAccent(FVector(-WallWidth*.46f,0.f,WallHeight*.18f),.5f);
  SpawnWallAccent(FVector(WallWidth*.46f,0.f,WallHeight*.18f),.5f);
  // A delayed frame must never backfill an already-expired shake.
  if(bContextBound&&RevealAge<.32f&&GetWorld())
   if(auto* PC=GetWorld()->GetFirstPlayerController();PC&&PC->IsLocalController()&&IsValid(PC->PlayerCameraManager))
   {
    auto* Manager=PC->PlayerCameraManager.Get();
    if(auto* Shake=Cast<UEGBarrierCameraShake>(Manager->StartCameraShake(UEGBarrierCameraShake::StaticClass())))
    {ShakeLease=FGuid::NewGuid();Shake->SetLease(ShakeLease);ShakeManager=Manager;RevealShake=Shake;ShakeViewTarget=Manager->GetViewTarget();}
   }
 }
 if(RevealShake.IsValid())
 {
  if(!ShakeManager.IsValid()||!ShakeViewTarget.IsValid()||!RevealShake->HasLease(ShakeLease)||!RevealShake->IsActive()||ShakeManager->GetViewTarget()!=ShakeViewTarget.Get()||RevealAge>=.32f)StopRevealShake();
  else if(bCanAdvance)
  {
   RevealShake->SetVisualElapsed(RevealAge);
   if(GetWorld()&&GetWorld()->IsPaused()&&ShakeManager.IsValid()&&ShakeViewTarget.IsValid()&&
      ShakeManager->GetViewTarget()==ShakeViewTarget.Get()&&LastShakeCameraFrame!=GFrameCounter)
   {LastShakeCameraFrame=GFrameCounter;ShakeManager->UpdateCamera(0.f);}
  }
 }
 if(SurfaceMaterial)
 {
  SurfaceMaterial->SetScalarParameterValue(TEXT("DomainOpacity"),(bSourceStyle?.8f:(Surface->ComponentHasTag(TEXT("EGDigitalCorruptionWall"))?.42f:.26f))*Reveal*Fade);
  if(bSourceStyle){SurfaceMaterial->SetScalarParameterValue(TEXT("DomainEmissive"),3.f+3.f*IntroFlash+3.f*HitFlash);SurfaceMaterial->SetScalarParameterValue(TEXT("VisualTime"),RevealAge);}
 }
 if(BackplaneMaterial)BackplaneMaterial->SetScalarParameterValue(TEXT("DomainOpacity"),(.24f+.08f*HitFlash)*Reveal*Fade);
 Surface->SetVisibility(Fade>.01f&&Reveal>.001f);
 Backplane->SetVisibility(Fade>.01f&&Reveal>.001f);
 if(BarMaterial)BarMaterial->SetVectorParameterValue(TEXT("Tint"),FLinearColor(.8f+1.5f*HitFlash,.04f+.45f*HitFlash,1.6f+2.f*HitFlash,1.f)*Fade);
 // The sampled reference has no heavy geometric grid bars. Keep dormant
 // components for compatibility with existing presentations and tests.
 for(int32 I=0;I<Bars.Num();++I)
 {
  auto* C=Bars[I].Get();if(!C)continue;
  const bool bVertical=I<9;
  const float P=bVertical?float(I)/8.f:float(I-9)/6.f;
  C->SetRelativeLocation(bVertical?FVector((P-.5f)*WallWidth,0,WallHeight*.5f):FVector(0,0,P*WallHeight));
  C->SetRelativeScale3D(bVertical?FVector(.026f,.025f,WallHeight*Reveal/100.f):FVector(WallWidth*Reveal/100.f,.025f,.026f));
  C->SetVisibility(false);
 }
 for(int32 I=0;I<Fragments.Num();++I)
 {
  auto* C=Fragments[I].Get();if(!C)continue;
  const float X=(float((I*7)%19)/18.f-.5f)*WallWidth;
  const float Z=(.12f+.76f*float((I*11)%17)/16.f)*WallHeight;
  const float Drift=EndAge<0.f?0.f:(Age-EndAge)*(bBroken?120.f:35.f);
  C->SetRelativeLocation(FVector(X+(I%2?Drift:-Drift),2.f,Z+Drift*(I%3-1)));
  C->SetRelativeScale3D(FVector((.015f+.009f*((I/3)%3))*WallWidth/100.f,.012f,(.006f+.006f*(I%2))*WallHeight/100.f)*Reveal*Fade);
  const bool bScan=FMath::Sin(Age*4.f+I*1.7f)>.10f;
  C->SetVisibility(Fade>.01f&&Reveal>.001f&&I%2==0&&(EndAge>=0.f||bScan)&&float(I)/Fragments.Num()<.25f+.75f*Integrity);
 }
 // A few thin, staggered fissures communicate damage without obscuring the board.
 for(int32 I=0;I<Cracks.Num();++I)
 {
  auto* C=Cracks[I].Get();if(!C)continue;
  const float X=(float(I%4)-1.5f)*WallWidth*.17f;
  const float Z=(.25f+.4f*float(I/4))*WallHeight;
  const float Spread=EndAge>=0.f&&bBroken?(Age-EndAge)*80.f:0.f;
  C->SetRelativeLocation(FVector(X+(I%2?Spread:-Spread),-2.f,Z));
  C->SetRelativeRotation(FRotator::ZeroRotator);
  C->SetRelativeScale3D((I%2?FVector(.035f,.025f,WallHeight*.12f/100.f):FVector(WallWidth*.055f/100.f,.025f,.035f))*Reveal*Fade);
  C->SetVisibility(Fade>.01f&&Reveal>.001f&&Integrity<1.f-float(I+1)/float(Cracks.Num()+1));
 }
}
void AEGCorruptionBarrierVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 StopRevealShake();
 for(const auto& Particle:WallParticles)if(IsValid(Particle)){Particle->DeactivateImmediate();Particle->DestroyComponent();}
 WallParticles.Reset();Source.Reset();Battle.Reset();PresentationToken.Invalidate();
 Super::EndPlay(Reason);
}
