#include "Presentation/EGHumanEntranceComponent.h"

#include "Actor/EGArtPiece.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

namespace
{
 constexpr float Duration=1.35f;
 constexpr float BurstStart=.08f;
 constexpr float RevealSoundStart=.08f;
 const TCHAR* const RevealSoundPath=TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Entrance20261106/Audio/SW_HumanEntrance_Reveal.SW_HumanEntrance_Reveal");
}

#if WITH_DEV_AUTOMATION_TESTS
bool UEGHumanEntranceComponent::bForceInitializationFailureForTests=false;
#endif

UEGHumanEntranceComponent::UEGHumanEntranceComponent()
{
 PrimaryComponentTick.bCanEverTick=true;
 PrimaryComponentTick.bStartWithTickEnabled=false;
 SetIsReplicatedByDefault(false);
}

bool UEGHumanEntranceComponent::Play(AEGArtPiece* Piece)
{
 if(!IsValid(Piece)||Piece->IsActorBeingDestroyed()||!Piece->GetWorld()||!Piece->GetWorld()->IsGameWorld()||
    Piece->GetWorld()->bIsTearingDown||Piece->GetNetMode()==NM_DedicatedServer||Piece->IsHidden())return false;
 if(auto* Old=Piece->FindComponentByClass<UEGHumanEntranceComponent>())Old->Cancel();
 auto* Effect=NewObject<UEGHumanEntranceComponent>(Piece);
 if(!Effect)return false;
 Piece->AddInstanceComponent(Effect);
 Effect->RegisterComponent();
 if(Effect->BeginPresentation(Piece))
 {
  // CardManager deferred the voice for the removed card sequence. Start it
  // once with the already-playing entrance animation instead.
  Piece->StartDeferredPreparationEntranceVoice();
  return true;
 }
 Effect->Cancel(false); // CardManager owns the voice fallback on failure.
 return false;
}

bool UEGHumanEntranceComponent::BeginPresentation(AEGArtPiece* Piece)
{
 Target=Piece;
#if WITH_DEV_AUTOMATION_TESTS
 if(bForceInitializationFailureForTests)return false;
#endif
 if(!Piece->GetRootComponent())return false;
 if(FParse::Param(FCommandLine::Get(),TEXT("EGHumanEntranceCandidateNiagara")))
  BurstSystem=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Combat20261101/FX/NS_C2_Entry_Candidate.NS_C2_Entry_Candidate"));
 auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Base=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Entrance20261106/Materials/M_HumanEntranceBurst.M_HumanEntranceBurst"),nullptr,LOAD_NoWarn);
 if(Plane&&Base)
 {
  BurstCard=NewObject<UStaticMeshComponent>(Piece,MakeUniqueObjectName(Piece,UStaticMeshComponent::StaticClass(),TEXT("HumanEntranceBurst")),RF_Transient);
  if(BurstCard)
  {
   Piece->AddInstanceComponent(BurstCard);
   BurstCard->SetupAttachment(Piece->GetRootComponent());
   BurstCard->SetMobility(EComponentMobility::Movable);
   BurstCard->SetCollisionEnabled(ECollisionEnabled::NoCollision);
   BurstCard->SetGenerateOverlapEvents(false);
   BurstCard->SetCastShadow(false);
   BurstCard->SetReceivesDecals(false);
   BurstCard->SetStaticMesh(Plane);
   BurstCard->SetVisibility(false);
   BurstCard->RegisterComponent();
   BurstMaterial=BurstCard->CreateDynamicMaterialInstance(0,Base);
   if(!BurstMaterial){BurstCard->DestroyComponent();BurstCard=nullptr;}
  }
 }
 if(auto* Sound=LoadObject<USoundBase>(nullptr,RevealSoundPath,nullptr,LOAD_NoWarn))
 {
  RevealAudio=NewObject<UAudioComponent>(Piece,MakeUniqueObjectName(Piece,UAudioComponent::StaticClass(),TEXT("HumanEntranceRevealAudio")),RF_Transient);
  if(RevealAudio)
  {
   Piece->AddInstanceComponent(RevealAudio);
   RevealAudio->bAutoActivate=false;
   RevealAudio->bAutoDestroy=false;
   RevealAudio->bAllowSpatialization=false;
   RevealAudio->SetSound(Sound);
   RevealAudio->RegisterComponent();
  }
 }
 UpdatePresentation();
 SetComponentTickEnabled(true);
 return true;
}

void UEGHumanEntranceComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
 if(!Target.IsValid()||Target->IsActorBeingDestroyed()||!GetWorld()||GetWorld()->bIsTearingDown){Cancel();return;}
 const float Previous=Elapsed;
 Elapsed+=FMath::Max(0.f,DeltaTime);
 if(Previous<BurstStart&&Elapsed>=BurstStart&&Elapsed<Duration&&BurstSystem&&Target->GetRootComponent())
  Burst=UNiagaraFunctionLibrary::SpawnSystemAttached(BurstSystem,Target->GetRootComponent(),NAME_None,
   FVector(0.f,0.f,10.f),FRotator::ZeroRotator,FVector(1.5f),EAttachLocation::KeepRelativeOffset,
   true,ENCPoolMethod::None,true,false);
 if(Previous<RevealSoundStart&&Elapsed>=RevealSoundStart&&Elapsed<Duration&&IsValid(RevealAudio)&&RevealAudio->GetSound())
 {
  const float Offset=Elapsed-RevealSoundStart;
  const float SoundDuration=RevealAudio->GetSound()->GetDuration();
  if(FMath::IsFinite(SoundDuration)&&SoundDuration>Offset){RevealAudio->Play(Offset);bRevealSoundTriggered=true;}
 }
 if(Elapsed>=Duration){Cancel();return;}
 UpdatePresentation();
}

void UEGHumanEntranceComponent::UpdatePresentation()
{
 AEGArtPiece* Piece=Target.Get();
 if(!Piece||!IsValid(BurstCard)||!IsValid(BurstMaterial))return;
 FVector CameraRight=FVector(0.f,1.f,0.f),CameraUp=FVector(.55f,0.f,.84f);
 if(const auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)
  if(const auto* Camera=PC->PlayerCameraManager.Get())
  {
   const FRotationMatrix CameraAxes(Camera->GetCameraRotation());
   CameraRight=CameraAxes.GetUnitAxis(EAxis::Y);
   CameraUp=CameraAxes.GetUnitAxis(EAxis::Z);
  }
 const FQuat Face=FRotationMatrix::MakeFromXY(CameraRight,CameraUp).ToQuat();
 const FVector FaceNormal=FVector::CrossProduct(CameraRight,CameraUp).GetSafeNormal();
 const float BurstT=FMath::Clamp(Elapsed/1.05f,0.f,1.f);
 const float Alpha=FMath::Clamp(Elapsed/.18f,0.f,1.f)*(1.f-FMath::Clamp((Elapsed-.8f)/.5f,0.f,1.f));
 const float Scale=FMath::Lerp(1.2f,3.5f,BurstT);
 const FVector Center=Piece->GetActorLocation()+FVector(0.f,0.f,100.f*Piece->GetBoardModelScale());
 BurstCard->SetWorldTransform(FTransform(Face,Center-FaceNormal*50.f,FVector(Scale,Scale,1.f)));
 BurstMaterial->SetScalarParameterValue(TEXT("Opacity"),Alpha);
 BurstCard->SetVisibility(Alpha>0.f);
}

void UEGHumanEntranceComponent::ReleasePresentation(bool bDiscardDeferredVoice)
{
 if(bReleased)return;
 bReleased=true;
 SetComponentTickEnabled(false);
 if(IsValid(RevealAudio)){RevealAudio->Stop();RevealAudio->DestroyComponent();}
 RevealAudio=nullptr;
 if(bDiscardDeferredVoice&&Target.IsValid())Target->DiscardDeferredPreparationEntranceVoice();
 if(IsValid(Burst))Burst->DestroyComponent();
 Burst=nullptr;BurstSystem=nullptr;
 if(IsValid(BurstCard))BurstCard->DestroyComponent();
 BurstCard=nullptr;BurstMaterial=nullptr;Target.Reset();
}

void UEGHumanEntranceComponent::Cancel(bool bDiscardDeferredVoice)
{
 ReleasePresentation(bDiscardDeferredVoice);
 DestroyComponent();
}

void UEGHumanEntranceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 ReleasePresentation(true);
 Super::EndPlay(Reason);
}
