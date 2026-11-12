#include "Presentation/EGVillVFXsPreview.h"
#include "EGVillVFXsFrame.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Engine/World.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#endif

AEGVillVFXsPreview::AEGVillVFXsPreview()
{
    PrimaryActorTick.bCanEverTick=true;
    bAllowTickBeforeBeginPlay=true;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("PreviewRoot")));
    SetActorEnableCollision(false);
    bReplicates=false;
}
void AEGVillVFXsPreview::ResetPlayback()
{
    for(UNiagaraComponent* Component:EffectComponents)if(IsValid(Component)){Component->DeactivateImmediate();Component->DestroyComponent();}
    EffectComponents.Reset();BirthAnchors.Reset();
    if(IsValid(RigActor))RigActor->Destroy();
    RigActor=nullptr;BuiltPreset=INDEX_NONE;BuiltDefinition=nullptr;
}
void AEGVillVFXsPreview::BeginPlay()
{
    Super::BeginPlay();
    if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
    {
        EnableInput(PC);
        InputComponent->BindKey(EKeys::SpaceBar,IE_Pressed,this,&AEGVillVFXsPreview::Play);
        InputComponent->BindKey(EKeys::S,IE_Pressed,this,&AEGVillVFXsPreview::Stop);
        InputComponent->BindKey(EKeys::N,IE_Pressed,this,&AEGVillVFXsPreview::NextPreset);
        InputComponent->BindKey(EKeys::O,IE_Pressed,this,&AEGVillVFXsPreview::PlayOnce);
        InputComponent->BindKey(EKeys::L,IE_Pressed,this,&AEGVillVFXsPreview::ToggleLoop);
    }
    if(bAutoPlay)Play();
}
void AEGVillVFXsPreview::EndPlay(const EEndPlayReason::Type Reason){ResetPlayback();Super::EndPlay(Reason);}
void AEGVillVFXsPreview::Destroyed(){ResetPlayback();Super::Destroyed();}
void AEGVillVFXsPreview::Play(){ResetPlayback();PreviewTime=0; bPlaying=true;Evaluate();}
void AEGVillVFXsPreview::PlayOnce(){bLoop=false;Play();}
void AEGVillVFXsPreview::Stop(){bPlaying=false;PreviewTime=0;ResetPlayback();}
void AEGVillVFXsPreview::NextPreset(){PresetIndex=Definition&&Definition->Presets.Num()?(PresetIndex+1)%Definition->Presets.Num():0;Play();}
void AEGVillVFXsPreview::Seek(float Time)
{
    if(!FMath::IsFinite(Time))return;
    if(Time<PreviewTime)ResetPlayback();
    PreviewTime=FMath::Max(0.f,Time);Evaluate();
}
void AEGVillVFXsPreview::Tick(float Dt)
{
    Super::Tick(Dt);
    if(!Definition||!Definition->Presets.IsValidIndex(PresetIndex)){ResetPlayback();return;}
    if(!bPlaying)return;
    PreviewTime+=FMath::Max(0.f,Dt);
    const float Duration=FMath::Max(.01f,Definition->Presets[PresetIndex].Duration);
    if(PreviewTime>=Duration){if(!bLoop){Stop();return;}ResetPlayback();PreviewTime=FMath::Fmod(PreviewTime,Duration);}
    Evaluate();
}
void AEGVillVFXsPreview::Evaluate()
{
    if(!GetWorld()||GetNetMode()==NM_DedicatedServer||!Definition||!Definition->Presets.IsValidIndex(PresetIndex))return;
    const auto& Preset=Definition->Presets[PresetIndex];
    if(BuiltPreset!=PresetIndex||BuiltDefinition!=Definition)
    {
        ResetPlayback();BuiltPreset=PresetIndex;BuiltDefinition=Definition;
        FActorSpawnParameters Params;Params.Owner=this;Params.ObjectFlags|=RF_Transient;
        RigActor=GetWorld()->SpawnActor<AEGSourceCharacterPreview>(GetActorLocation(),GetActorRotation(),Params);
        if(RigActor){RigActor->Definition=Preset.Rig;RigActor->bAutoPlay=false;RigActor->bLoop=true;RigActor->Pause();RigActor->SetActorScale3D(GetActorScale3D());}
        EffectComponents.SetNum(Preset.Layers.Num());
    }
    if(IsValid(RigActor)){RigActor->SetActorTransform(GetActorTransform());RigActor->SetPreviewTime(PreviewTime);}
    FTransform Muzzle=GetActorTransform();Muzzle.SetScale3D(FVector::OneVector);
    if(IsValid(RigActor))
    {
        FTransform DeviceMuzzle;
        if(!Preset.MuzzlePartId.IsNone()&&RigActor->GetVisiblePartBoneTransform(Preset.MuzzlePartId,Preset.MuzzleBone,DeviceMuzzle))
        {Muzzle=DeviceMuzzle;Muzzle.SetScale3D(FVector::OneVector);}
    }
    const FVector Target=GetActorTransform().TransformPosition(TargetOffset);
    FRotator ViewRotation=GetActorRotation();FVector CameraLocation=GetActorLocation()+FVector(500,-1000,600);
    if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0)){CameraLocation=Camera->GetCameraLocation();ViewRotation=Camera->GetCameraRotation();}
#if WITH_EDITOR
    else if(GEditor&&GEditor->GetActiveViewport())
    {
        auto* Client=static_cast<FEditorViewportClient*>(GEditor->GetActiveViewport()->GetClient());
        if(Client){CameraLocation=Client->GetViewLocation();ViewRotation=Client->GetViewRotation();}
    }
#endif
    for(int32 LayerIndex=0;LayerIndex<Preset.Layers.Num();++LayerIndex)
    {
        const auto& Layer=Preset.Layers[LayerIndex];if(!Layer.Samples||!Layer.System)continue;
        const float Age=PreviewTime-Layer.StartTime;float Last=0;
        for(const auto& Track:Layer.Samples->Tracks)Last=FMath::Max(Last,Track.StartTime+Track.Duration);
        auto& Component=EffectComponents[LayerIndex];
        if(Age<0||Age>Last){if(IsValid(Component))Component->SetVisibility(false);continue;}
        if(!IsValid(Component))
        {
            Component=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient|RF_DuplicateTransient|RF_TextExportTransient);
            Component->SetAutoActivate(false);Component->SetAsset(Layer.System);Component->SetAutoDestroy(false);Component->SetUsingAbsoluteLocation(true);Component->RegisterComponent();
        }
        FTransform Anchor=Muzzle;
        if(Layer.Anchor==EEGVillVFXsAnchor::Target){Anchor.SetLocation(Target);Anchor.SetRotation(FQuat::Identity);}
        else if(Layer.Anchor==EEGVillVFXsAnchor::Projectile)
        {
            Anchor.SetLocation(FMath::Lerp(Muzzle.GetLocation(),Target,FMath::Clamp(Age/FMath::Max(.01f,Layer.FlightDuration),0.f,1.f)));
            // The extracted Unity +Z forward axis is UE +Y, not actor +X.
            Anchor.SetRotation(FRotationMatrix::MakeFromYZ((Target-Muzzle.GetLocation()).GetSafeNormal(),FVector::UpVector).ToQuat());
        }
        EGEvaluateVillVFXsFrame(Layer,Component,Age,Anchor,Muzzle,Target,bUseSourceScale?1.f:FMath::Max(.01f,BoardEffectScale*Layer.EffectScale),CameraLocation,ViewRotation,BirthAnchors,LayerIndex*100000);
    }
}
