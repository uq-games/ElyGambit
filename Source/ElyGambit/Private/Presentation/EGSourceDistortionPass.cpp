#include "Presentation/EGSourceDistortionPass.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/PostProcessComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/Material.h"

AEGSourceDistortionPass::AEGSourceDistortionPass()
{
 PrimaryActorTick.bCanEverTick=true;
 PrimaryActorTick.TickGroup=TG_PostUpdateWork;
 bReplicates=false;SetActorEnableCollision(false);
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 MapCapture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("NativeDisplacement"));
 DepthCapture=CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("NativeOcclusion"));
 CompositeVolume=CreateDefaultSubobject<UPostProcessComponent>(TEXT("NativeComposite"));
 for(auto* C:{MapCapture.Get(),DepthCapture.Get()})
 {
  C->SetupAttachment(GetRootComponent());C->bCaptureEveryFrame=false;C->bCaptureOnMovement=false;
  C->PostProcessBlendWeight=0;C->bAlwaysPersistRenderingState=false;
  C->ShowFlags.SetAtmosphere(false);C->ShowFlags.SetFog(false);
  C->ShowFlags.SetPostProcessing(false);C->ShowFlags.SetEyeAdaptation(false);
  C->ShowFlags.SetBloom(false);C->ShowFlags.SetMotionBlur(false);
  C->ShowFlags.SetAntiAliasing(false);C->ShowFlags.SetTemporalAA(false);
 }
 MapCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_UseShowOnlyList;
 MapCapture->CaptureSource=ESceneCaptureSource::SCS_SceneColorHDR;
 MapCapture->CompositeMode=ESceneCaptureCompositeMode::SCCM_Composite;
 DepthCapture->PrimitiveRenderMode=ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
 DepthCapture->CaptureSource=ESceneCaptureSource::SCS_SceneDepth;
 CompositeVolume->SetupAttachment(GetRootComponent());CompositeVolume->bUnbound=true;
 CompositeVolume->Priority=10.f;CompositeVolume->BlendWeight=0.f;
}
bool AEGSourceDistortionPass::Configure(UMaterialInterface* Composite)
{
 if(!IsValid(Composite)||!GetWorld()||GetNetMode()==NM_DedicatedServer
  ||!Composite->GetMaterial()||Composite->GetMaterial()->MaterialDomain!=MD_PostProcess)return false;
 if(CompositeParent&&CompositeParent!=Composite)return false;
 if(CompositeMID)return true;
 CompositeParent=Composite;CompositeMID=UMaterialInstanceDynamic::Create(Composite,this);
 if(!CompositeMID)return false;
 Displacement=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);
 SceneDepth=NewObject<UTextureRenderTarget2D>(this,NAME_None,RF_Transient);
 Displacement->RenderTargetFormat=RTF_RGBA16f;Displacement->ClearColor=FLinearColor(.498f,.498f,0,1);
 SceneDepth->RenderTargetFormat=RTF_R32f;SceneDepth->ClearColor=FLinearColor(1.e8f,0,0,0);
 for(auto* R:{Displacement.Get(),SceneDepth.Get()})
 {R->TargetGamma=1.f;R->bAutoGenerateMips=false;R->InitAutoFormat(1280,720);R->UpdateResourceImmediate(true);}
 MapCapture->TextureTarget=Displacement;DepthCapture->TextureTarget=SceneDepth;
 CompositeMID->SetTextureParameterValue(TEXT("NativeDisplacement"),Displacement);
 CompositeVolume->Settings.AddBlendable(CompositeMID,1.f);
 return true;
}
bool AEGSourceDistortionPass::RegisterSource(UPrimitiveComponent* Part,UMaterialInterface* Composite)
{
 if(!IsValid(Part)||!Part->GetWorld()||!Part->GetWorld()->IsGameWorld()
  ||Part->GetWorld()->GetNetMode()==NM_DedicatedServer||!Composite)return false;
 auto* World=Part->GetWorld();AEGSourceDistortionPass* Pass=nullptr;
 for(TActorIterator<AEGSourceDistortionPass> It(World);It;++It)if(IsValid(*It)){Pass=*It;break;}
 if(!Pass)
 {
  FActorSpawnParameters Params;Params.ObjectFlags|=RF_Transient;
  Pass=World->SpawnActor<AEGSourceDistortionPass>(Params);
 }
 if(!Pass||!Pass->Configure(Composite))
 {
  // Preserve the original primitive; registration never assumes ownership or
  // treats signed displacement as a main-scene color fallback.
  UE_LOG(LogTemp,Warning,TEXT("Source distortion registration rejected for %s using %s"),*GetPathNameSafe(Part),*GetPathNameSafe(Composite));
  return false;
 }
 Pass->Sources.AddUnique(Part);
 // Only the accumulation camera draws these source meshes. Main-scene depth
 // is captured separately and applied by their original-program adapter.
 Part->SetVisibleInSceneCaptureOnly(true);
 if(auto* MID=Cast<UMaterialInstanceDynamic>(Part->GetMaterial(0)))
  MID->SetTextureParameterValue(TEXT("NativeSceneDepth"),Pass->SceneDepth);
 return true;
}
bool AEGSourceDistortionPass::IsCompositeActive() const
{return CompositeVolume&&CompositeVolume->BlendWeight>0.f;}
void AEGSourceDistortionPass::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 Sources.RemoveAll([](const auto& P){return !P.IsValid()||!P->IsRegistered()
  ||!P->GetOwner()||P->GetOwner()->IsActorBeingDestroyed();});
 if(Sources.IsEmpty()){Destroy();return;}
 auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
 if(!Camera||!CompositeMID){CompositeVolume->BlendWeight=0;return;}
 MapCapture->ClearShowOnlyComponents();DepthCapture->HiddenComponents.Reset();
 int32 Visible=0;
 for(const auto& Ref:Sources)if(auto* P=Ref.Get())
 {
  DepthCapture->HiddenComponents.Add(P);
  if(!P->IsVisible()||!P->GetOwner()||P->GetOwner()->IsHidden())continue;
  MapCapture->ShowOnlyComponent(P);++Visible;
 }
 CompositeVolume->BlendWeight=Visible>0?1.f:0.f;
 if(!Visible)return;
 FVector2D Size(1280,720);
 if(const auto* Viewport=GetWorld()->GetGameViewport())Viewport->GetViewportSize(Size);
 const int32 Width=FMath::Max(1,FMath::RoundToInt(Size.X)),Height=FMath::Max(1,FMath::RoundToInt(Size.Y));
 if(Displacement->SizeX!=Width||Displacement->SizeY!=Height)
 {Displacement->ResizeTarget(Width,Height);SceneDepth->ResizeTarget(Width,Height);}
 for(auto* C:{MapCapture.Get(),DepthCapture.Get()})
 {C->SetWorldLocationAndRotation(Camera->GetCameraLocation(),Camera->GetCameraRotation());C->FOVAngle=Camera->GetFOVAngle();}
 // Ordered captures: source depth, signed accumulation, then the main view's
 // recovered Distortion_Apply material. Multiple casters share this one map.
 DepthCapture->CaptureScene();
 // HDR capture supplies premultiplied color plus inverse opacity. Composite
 // mode folds its black-background accumulation over the native neutral clear
 // without inventing a displacement bias or changing the source shader output.
 UKismetRenderingLibrary::ClearRenderTarget2D(this,Displacement,Displacement->ClearColor);
 MapCapture->CaptureScene();
}
void AEGSourceDistortionPass::EndPlay(const EEndPlayReason::Type Reason)
{
 if(CompositeVolume){CompositeVolume->BlendWeight=0;CompositeVolume->Settings.WeightedBlendables.Array.Reset();}
 if(MapCapture)MapCapture->TextureTarget=nullptr;
 if(DepthCapture)DepthCapture->TextureTarget=nullptr;
 Sources.Reset();CompositeMID=nullptr;CompositeParent=nullptr;Displacement=nullptr;SceneDepth=nullptr;
 Super::EndPlay(Reason);
}
