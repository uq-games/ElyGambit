#include "Presentation/EGMonsterSkillVisual.h"
#include "Skills/EGMonsterSkillBehavior.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AEGMonsterSkillVisual::AEGMonsterSkillVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;SetActorEnableCollision(false);
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> Shield(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundCue/SC_Block.SC_Block"));
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> Heal(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundCue/SC_Heal.SC_Heal"));
 static ConstructorHelpers::FObjectFinderOptional<USoundBase> Speed(TEXT("/Game/Assets/FXs/UltraFXWarrior/Demo/Resource/Sounds/SoundWave/SW_Whoosh.SW_Whoosh"));
 ShieldSound=Shield.Get();HealSound=Heal.Get();SpeedSound=Speed.Get();
}
AEGMonsterSkillVisual* AEGMonsterSkillVisual::Attach(UEGMonsterSkillBehavior* InBehavior,AEGChessUnit* InSource,UEGBattleContextComponent* InBattle,FGuid InRound)
{
 if(!IsValid(InBehavior)||!IsValid(InSource)||!IsValid(InBattle)||!InBattle->IsBattleActive()||
    !InRound.IsValid()||InBattle->GetRoundId()!=InRound||!InSource->GetWorld()||InSource->GetNetMode()==NM_DedicatedServer)return nullptr;
 FActorSpawnParameters Params;Params.Owner=InSource;Params.ObjectFlags|=RF_Transient;
 auto* Visual=InSource->GetWorld()->SpawnActor<AEGMonsterSkillVisual>(Params);if(!Visual)return nullptr;
 Visual->Behavior=InBehavior;Visual->Source=InSource;Visual->Battle=InBattle;Visual->Round=InRound;
 Visual->EffectDelegate=InBehavior->OnMonsterEffect.AddUObject(Visual,&ThisClass::HandleEffect);
 return Visual;
}
void AEGMonsterSkillVisual::HandleEffect(EEGMonsterEffectCue Cue,AEGChessUnit* EventSource,AEGChessUnit* Target,FGuid EventRound,FGuid Action,float Seconds)
{
 if(!IsValid(EventSource)||EventSource!=Source.Get()||!IEGCombatant::Execute_IsAlive(EventSource)||!IsValid(Target)||!IEGCombatant::Execute_IsAlive(Target)||!Battle.IsValid()||
    !Battle->IsCombatRunning()||EventRound!=Round||Battle->GetRoundId()!=Round||!Action.IsValid()||
    !FMath::IsFinite(Seconds)||Seconds<0.f)return;
 AEGMonsterEffectCueVisual::Spawn(EventSource,Target,Battle.Get(),Round,Cue);
 // Team-wide effects produce one target event per recipient, but one sound per cast.
 if(LastSoundAction==Action)return;LastSoundAction=Action;
 USoundBase* Sound=Cue==EEGMonsterEffectCue::Shield?ShieldSound.Get():Cue==EEGMonsterEffectCue::Heal?HealSound.Get():SpeedSound.Get();
 if(!IsValid(Sound))return;
 const auto* Settings=UEGGameAudioComponent::FindFor(this);const float Volume=Settings?Settings->GetUISoundVolume():1.f;
 // Keep ownership of the only playing cue so pause and teardown cannot leave
 // an earlier, overlapping component playing after its source is removed.
 if(LastAudio.IsValid())LastAudio->Stop();
 LastAudio=UGameplayStatics::SpawnSoundAtLocation(this,Sound,EventSource->GetActorLocation(),FRotator::ZeroRotator,.18f*Volume);
}
void AEGMonsterSkillVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Source.IsValid()||!IEGCombatant::Execute_IsAlive(Source.Get())||!Battle.IsValid()||!Battle->IsBattleActive()||
    Battle->GetRoundId()!=Round||!Behavior.IsValid()||!Source->GetHeroSkill()||Source->GetHeroSkill()->GetBehavior()!=Behavior.Get())
 {Destroy();return;}
 if(LastAudio.IsValid())LastAudio->SetPaused(!Battle->IsCombatRunning());
}
void AEGMonsterSkillVisual::EndPlay(const EEndPlayReason::Type Reason)
{
 if(Behavior.IsValid())Behavior->OnMonsterEffect.Remove(EffectDelegate);
 EffectDelegate.Reset();Behavior.Reset();if(LastAudio.IsValid())LastAudio->Stop();LastAudio.Reset();
 Source.Reset();Battle.Reset();Round.Invalidate();Super::EndPlay(Reason);
}

AEGMonsterEffectCueVisual::AEGMonsterEffectCueVisual()
{
 PrimaryActorTick.bCanEverTick=true;bReplicates=false;SetActorEnableCollision(false);
 SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> RingMat(TEXT("/Game/Assets/Environments/Board/Materials/MI_ChessPlacementRing"));
 static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlyphMat(TEXT("/Game/Assets/Environments/Board/Materials/M_ChessBoardPerimeter"));
 Ring=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ring"));Ring->SetupAttachment(GetRootComponent());Ring->SetStaticMesh(Plane.Object);
 Ring->SetMaterial(0,RingMat.Object);Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);Ring->SetCastShadow(false);
 for(int32 I=0;I<4;++I)
 {
  auto* Part=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("Glyph%d"),I));Part->SetupAttachment(GetRootComponent());
  Part->SetStaticMesh(Cube.Object);Part->SetMaterial(0,GlyphMat.Object);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
  Part->SetCastShadow(false);Part->SetVisibility(false);Glyph.Add(Part);
 }
}
AEGMonsterEffectCueVisual* AEGMonsterEffectCueVisual::Spawn(AEGChessUnit* InSource,AEGChessUnit* InTarget,UEGBattleContextComponent* InBattle,FGuid InRound,EEGMonsterEffectCue Cue)
{
 if(!IsValid(InSource)||!IsValid(InTarget)||!IsValid(InBattle)||!InBattle->IsCombatRunning()||
    InBattle->GetRoundId()!=InRound||!InSource->GetWorld()||InSource->GetNetMode()==NM_DedicatedServer)return nullptr;
 FActorSpawnParameters Params;Params.Owner=InSource;Params.ObjectFlags|=RF_Transient;
 auto* V=InSource->GetWorld()->SpawnActor<AEGMonsterEffectCueVisual>(Params);if(!V)return nullptr;
 V->Source=InSource;V->Target=InTarget;V->Battle=InBattle;V->Round=InRound;
 V->Color=Cue==EEGMonsterEffectCue::Shield?FLinearColor(2.f,1.25f,.2f,1.f):Cue==EEGMonsterEffectCue::Heal?FLinearColor(.15f,2.f,.55f,1.f):FLinearColor(.15f,1.4f,2.f,1.f);
 V->RingMaterial=V->Ring->CreateDynamicMaterialInstance(0);
 for(const auto& Entry:V->Glyph){auto* Part=Entry.Get();auto* MID=Part?Part->CreateDynamicMaterialInstance(0):nullptr;V->GlyphMaterials.Add(MID);}
 const auto Bar=[V](int32 Index,FVector Location,float Angle,float Length)
 {
  auto* Part=V->Glyph[Index].Get();Part->SetRelativeLocation(Location);Part->SetRelativeRotation(FRotator(0,Angle,0));
  Part->SetRelativeScale3D(FVector(Length/100.f,.14f,.04f));Part->SetVisibility(true);
 };
 if(Cue==EEGMonsterEffectCue::Heal){Bar(0,FVector(0,0,8),0,115);Bar(1,FVector(0,0,9),90,115);}
 else if(Cue==EEGMonsterEffectCue::Shield)
 {Bar(0,FVector(0,-45,8),0,90);Bar(1,FVector(45,0,8),90,90);Bar(2,FVector(0,45,8),0,90);Bar(3,FVector(-45,0,8),90,90);}
 else
 {Bar(0,FVector(-25,-25,8),45,65);Bar(1,FVector(-25,25,8),-45,65);Bar(2,FVector(25,-25,8),45,65);Bar(3,FVector(25,25,8),-45,65);}
 V->Tick(0.f);return V;
}
void AEGMonsterEffectCueVisual::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
 if(!Source.IsValid()||!IEGCombatant::Execute_IsAlive(Source.Get())||!Target.IsValid()||!IEGCombatant::Execute_IsAlive(Target.Get())||
    !Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round){Destroy();return;}
 if(!Battle->IsCombatRunning())return;
 Age+=FMath::Max(0.f,DeltaSeconds);if(Age>=.9f){Destroy();return;}
 const float Height=Target->GetCapsuleComponent()?Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():88.f;
 const auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
 const FVector ToCamera=Camera?(Camera->GetCameraLocation()-Target->GetActorLocation()).GetSafeNormal():FVector(1,0,0);
 FVector Right=FVector::CrossProduct(FVector::UpVector,ToCamera).GetSafeNormal();if(Right.IsNearlyZero())Right=FVector(0,1,0);
 // A small billboard to the side and above the body remains readable from the
 // fixed board camera without covering a face or implying an area of effect.
 const FVector Position=Target->GetActorLocation()+Right*(Height+70.f)+FVector(0,0,Height*.8f);
 SetActorLocationAndRotation(Position,FRotationMatrix::MakeFromXZ(Right,ToCamera).Rotator());
 const float T=FMath::Clamp(Age/.9f,0.f,1.f),Fade=FMath::Clamp((1.f-T)/.3f,0.f,1.f);
 const float Radius=FMath::Lerp(1.45f,2.f,1.f-FMath::Square(1.f-T));Ring->SetRelativeScale3D(FVector(Radius,Radius,1));
 if(RingMaterial)
 {
  RingMaterial->SetVectorParameterValue(TEXT("Tint"),Color);
  RingMaterial->SetScalarParameterValue(TEXT("Opacity"),.85f*Fade);
  RingMaterial->SetScalarParameterValue(TEXT("Glow"),1.3f);
 }
 for(const auto& MID:GlyphMaterials)if(IsValid(MID))MID->SetVectorParameterValue(TEXT("Tint"),Color*Fade);
}
