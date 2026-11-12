#include "Components/EGVillVMechanismComponent.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSkillNotify.h"
#include "Presentation/EGVillVShotVisual.h"
#include "Presentation/EGVillVCombatFX.h"
#include "Skills/EGSupportSkillUtils.h"
#include "Animation/AnimMontage.h"

void UEGVillVMechanismComponent::BeginAutoFire(FGuid UltimateAction)
{
 if(!IsCurrentBattle()||!UltimateAction.IsValid()||AutoUltimate==UltimateAction||!Definition||Pilot->GetHeroSkill()->GetLevel()<11)return;
 AutoUltimate=UltimateAction;AutoDevice=1-Current;AutoPosition=0.f;AutoShot.Invalidate();AutoTarget.Reset();
 AutoRemaining=DeviceAlive(AutoDevice)?FMath::Max(0.f,Definition->AutoFireSeconds):0.f;
}

void UEGVillVMechanismComponent::TickAutoFire(float Delta)
{
 if(AutoRemaining<=0.f||!Battle->IsCombatRunning()||!FMath::IsFinite(Delta)||Delta<=0.f)return;
 if(!DeviceAlive(AutoDevice)||AutoDevice==Current||Pilot->GetHeroSkill()->GetLevel()<11||!Pilot->IsAlive_Implementation())
 {AutoRemaining=0.f;AutoTarget.Reset();return;}
 const float Window=FMath::Min(Delta,AutoRemaining);AutoRemaining-=Window;
 auto* Device=GetDevice(AutoDevice);auto* U=Pilot.Get();auto* Combat=U->GetCombat();
 const auto* P=U->GetDefinition()->PresentationDefinition.Get();
 if(!P||!P->AttackSequence.IsValidIndex(AutoDevice)||!Definition->DeviceAttacks.IsValidIndex(AutoDevice))return;
 auto* Montage=P->AttackSequence[AutoDevice].Montage.LoadSynchronous();
 auto* Motion=Definition->DeviceAttacks[AutoDevice].LoadSynchronous();
 if(!Montage||!Motion||Montage->GetPlayLength()<=0.f)return;
 const float Range=U->GetRuntimeAttackRange();
 auto Eligible=[&](AEGChessUnit* T){return Combat->IsValidEnemy(T)&&FVector::DistSquared2D(Device->GetActorLocation(),T->GetActorLocation())<=FMath::Square(Range);};
 if(!Eligible(AutoTarget.Get()))
 {
  AutoTarget.Reset();float Nearest=MAX_flt;
  for(auto* T:Battle->Units())if(Eligible(T))
  {const float Dist=FVector::DistSquared2D(Device->GetActorLocation(),T->GetActorLocation());if(Dist<Nearest){Nearest=Dist;AutoTarget=T;}}
 }
 if(!AutoTarget.IsValid()){AutoPosition=0.f;AutoShot.Invalidate();return;}
 const float Desired=(AutoTarget->GetActorLocation()-Device->GetActorLocation()).Rotation().Yaw;
 Device->SetActorRotation(FRotator(0,FMath::FixedTurn(Device->GetActorRotation().Yaw,Desired,Definition->AimDegreesPerSecond*Window),0));
 if(FMath::Abs(FMath::FindDeltaAngleDegrees(Device->GetActorRotation().Yaw,Desired))>Definition->AimTolerance)return;
 const float Speed=U->GetAttributes()->GetAttackSpeed();if(Speed<=0.f)return;
 // Same authored shot notifications and live attack-speed clock as the occupied gun.
 float Travel=Window*FMath::Max(.1f,Speed)*Montage->RateScale;
 while(Travel>UE_SMALL_NUMBER&&IsCurrentBattle()&&DeviceAlive(AutoDevice)&&AutoDevice!=Current)
 {
  if(!AutoShot.IsValid())AutoShot=FGuid::NewGuid();
  const float End=FMath::Min(Montage->GetPlayLength(),AutoPosition+Travel);
  for(const auto& Notify:Montage->Notifies)
   if(const auto* N=Cast<UEGSkillNotify>(Notify.Notify);N&&Notify.GetTriggerTime()>=AutoPosition&&Notify.GetTriggerTime()<End)
    if(N->Event==TEXT("Fire1")||(AutoDevice==0&&(N->Event==TEXT("Fire2")||N->Event==TEXT("Fire3"))))
    {
     Device->ShowDevice(Motion,Notify.GetTriggerTime());FireAutomatic(N->Event);
     if(!IsCurrentBattle()||!DeviceAlive(AutoDevice))return;
    }
  Travel-=End-AutoPosition;AutoPosition=End;
  Device->ShowDevice(Motion,AutoPosition);
  if(AutoPosition>=Montage->GetPlayLength()){AutoPosition=0.f;AutoShot.Invalidate();}else break;
 }
}

void UEGVillVMechanismComponent::FireAutomatic(FName Event)
{
 if(!IsCurrentBattle()||!DeviceAlive(AutoDevice)||AutoDevice==Current||!Definition)return;
 auto* U=Pilot.Get();auto* T=AutoTarget.Get();auto* Device=GetDevice(AutoDevice);
 if(!U->GetCombat()->IsValidEnemy(T)||FVector::DistSquared2D(Device->GetActorLocation(),T->GetActorLocation())>FMath::Square(U->GetRuntimeAttackRange()))return;
 const bool Cannon=AutoDevice==1;const int32 Barrel=Event==TEXT("Fire2")?1:Event==TEXT("Fire3")?2:0;
 const FName Bone=Cannon?FName(TEXT("CannonFire")):FName(*FString::Printf(TEXT("FirePoint%d"),Barrel+1));
 FTransform Muzzle;const bool HaveMuzzle=Device->MuzzleFrame(Bone,Muzzle);const FVector Point=T->GetActorLocation();
 const FGuid Shot=AutoShot;
 const float Damage=U->GetAttributes()->GetAttackPower()*(Cannon?Definition->CannonMultiplier:1.f/3.f);
 // Attribute damage to the hero, without BasicHit/mana/ultimate observers.
 if(Cannon)
 {
  const auto Targets=U->GetCombat()->FindEnemiesInRange(Point,EGSupport::Radius(U,Definition->CannonRadius/500.f));
  for(auto* Enemy:Targets){if(!IsCurrentBattle()||!DeviceAlive(AutoDevice))return;Battle->Damage(U,Enemy,Damage,Shot);}
 }
 else Battle->Damage(U,T,Damage,Shot);
 // An unoccupied device has no pilot montage/audio action. Submit a finite shot
 // at its own location; owner death and round stop use the existing audio cleanup.
 if(IsCurrentBattle()&&DeviceAlive(AutoDevice))
  if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())
   Audio->CommittedImpact(Cannon?TEXT("AutoCannonShot"):TEXT("AutoGunShot"),Battle->GetRoundId(),Shot,false,AutoDevice,Barrel,HaveMuzzle?Muzzle.GetLocation():Device->GetActorLocation());
 if(!HaveMuzzle||!IsCurrentBattle()||!DeviceAlive(AutoDevice)||!Definition->SourceCombatFX)return;
 Muzzle.SetRotation(Muzzle.GetRotation()*FQuat::FindBetweenNormals(FVector::ForwardVector,FVector::UpVector));Muzzle.SetScale3D(FVector::OneVector);
 V2ShotEffects.RemoveAll([](const auto& V){return !V.IsValid();});
 const int32 Limit=FMath::Clamp(Definition->SourceCombatFX->MaxConcurrentV2Shots,1,24);
 while(V2ShotEffects.Num()>=Limit){if(V2ShotEffects[0].IsValid())V2ShotEffects[0]->Destroy();V2ShotEffects.RemoveAt(0);}
 if(auto* V=AEGVillVShotVisual::SpawnV2(U->GetHeroSkill(),Device,Definition->SourceCombatFX,AutoDevice*2,Barrel,Bone,Muzzle,Point,Shot,true))V2ShotEffects.Add(V);
}
