#include "Components/EGVillVMechanismComponent.h"
#include "Character/EGChessUnit.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGVillVCombatFX.h"
#include "Presentation/EGVillVShotVisual.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Curves/CurveFloat.h"

UEGVillVMechanismComponent::UEGVillVMechanismComponent(){PrimaryComponentTick.bCanEverTick=true;}
bool UEGVillVMechanismComponent::IsCurrentBattle() const
{return Pilot.IsValid()&&Pilot->HasAuthority()&&Battle.IsValid()&&Battle->IsBattleActive()&&Round.IsValid()&&Battle->GetRoundId()==Round;}
AEGVillVEmplacement* UEGVillVMechanismComponent::GetDevice(int32 I) const{return Devices.IsValidIndex(I)?Devices[I].Get():nullptr;}
bool UEGVillVMechanismComponent::DeviceAlive(int32 I) const{return IsValid(GetDevice(I))&&IEGCombatant::Execute_IsAlive(GetDevice(I));}
bool UEGVillVMechanismComponent::HasActiveDevices() const
{return IsCurrentBattle()&&(DeviceAlive(0)||DeviceAlive(1));}
void UEGVillVMechanismComponent::Start(UEGBattleContextComponent* Context)
{
 Stop();Pilot=Cast<AEGChessUnit>(GetOwner());Battle=Context;Round=Context?Context->GetRoundId():FGuid();
 if(!IsCurrentBattle())return;
 Definition=Cast<UEGVillVSkillDefinition>(Pilot->GetDefinition()->SkillDefinition);if(!Definition)return;
 auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>();FEGHeroInstance H;
 auto* Grid=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
 if(!Grid||!GM->GetRoster()->GetHero(Pilot->GetInstanceId(),H)||!H.bVillVLayout)return;
 const FIntPoint Cells[2]={H.GatlingCell,H.CannonCell};
 UEGHeroDefinition* Defs[2]={Definition->GatlingDefinition.LoadSynchronous(),Definition->CannonDefinition.LoadSynchronous()};
 Devices.SetNum(2);SpawnClock=0;bEnhancedPending=false;bUltimateSwitch=false;Current=0;
 bOpeningEntry=true;bEntryCarriesFire=false;GroundTravelCount=JumpTransferCount=0;
 for(int32 I=0;I<2;++I)
 {
  Devices[I]=Cast<AEGVillVEmplacement>(GM->SpawnBattleEmplacement(Defs[I],Pilot.Get(),I,Grid->CellToWorld(Cells[I])+FVector(0,0,88)));
  if(Devices[I]){Devices[I]->OnDied.AddUniqueDynamic(this,&ThisClass::DeviceDied);if(Definition->DeviceIdles.IsValidIndex(I))Devices[I]->ShowDevice(Definition->DeviceIdles[I].LoadSynchronous(),0);}
 }
 if(!DeviceAlive(0)||!DeviceAlive(1)){UE_LOG(LogTemp,Error,TEXT("Vill-V failed to create both source devices"));Pilot->Die();return;}
 StartTravel(0);Pilot->RefreshBoardCollision();
}
bool UEGVillVMechanismComponent::StartEnemyLayout(UEGBattleContextComponent* Context,FIntPoint GatlingCell,FIntPoint CannonCell)
{
 auto* Owner=Cast<AEGChessUnit>(GetOwner());
 auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 auto* Board=GM?GM->GetBoard():nullptr;
 auto* Grid=Board?Board->LayoutSource.Get():nullptr;
 if(!Owner||!Owner->HasAuthority()||IEGCombatant::Execute_GetFaction(Owner)!=EEGFaction::Enemy
    ||!Context||!Context->IsBattleActive()||!Grid||HasActiveDevices()||GatlingCell==CannonCell)return false;
 const FIntPoint Cells[2]={GatlingCell,CannonCell};
 for(const FIntPoint Cell:Cells)
 {
  if(!Board->IsDeploymentCell(Cell,EEGFaction::Enemy)||Board->GetOccupant(Cell))return false;
  for(auto* Other:Context->Units())
  {
   FIntPoint OtherCell;
   if(IsValid(Other)&&IEGCombatant::Execute_IsAlive(Other)&&Grid->TryWorldToCell(Other->GetActorLocation(),OtherCell)&&OtherCell==Cell)return false;
  }
 }
 auto* Rules=Owner->GetDefinition()?Cast<UEGVillVSkillDefinition>(Owner->GetDefinition()->SkillDefinition):nullptr;
 if(!Rules)return false;
 UEGHeroDefinition* Defs[2]={Rules->GatlingDefinition.LoadSynchronous(),Rules->CannonDefinition.LoadSynchronous()};
 if(!Defs[0]||!Defs[1])return false;
 Stop();Pilot=Owner;Battle=Context;Round=Context->GetRoundId();Definition=Rules;
 Devices.SetNum(2);SpawnClock=0;Current=0;Destination=0;
 bOpeningEntry=false;bEntryCarriesFire=false;GroundTravelCount=JumpTransferCount=0;
 for(int32 I=0;I<2;++I)
 {
  Devices[I]=Cast<AEGVillVEmplacement>(GM->SpawnBattleEmplacement(Defs[I],Owner,I,Grid->CellToWorld(Cells[I])+FVector(0,0,88)));
  if(Devices[I])
  {
   Devices[I]->OnDied.AddUniqueDynamic(this,&ThisClass::DeviceDied);
   if(Definition->DeviceIdles.IsValidIndex(I))Devices[I]->ShowDevice(Definition->DeviceIdles[I].LoadSynchronous(),0);
  }
 }
 if(!DeviceAlive(0)||!DeviceAlive(1)){Stop();return false;}
 Phase=EEGVillVPhase::Operating;
 Owner->SetActorLocation(GetDevice(0)->GetActorLocation());
 Owner->SetActorRotation(GetDevice(0)->GetActorRotation());
 Owner->RefreshBoardCollision();
 return true;
}
void UEGVillVMechanismComponent::ClearSeatedBody()
{
 if(SeatedBody){SeatedBody->Destroy();SeatedBody=nullptr;if(Pilot.IsValid())Pilot->GetVisiblePresentationMesh()->SetVisibility(true);}
}
void UEGVillVMechanismComponent::ClearV2Shots()
{
 const auto Snapshot=MoveTemp(V2ShotEffects);V2ShotEffects.Reset();
 for(const auto& Effect:Snapshot)if(Effect.IsValid())Effect->Destroy();
}
void UEGVillVMechanismComponent::Stop()
{
 if(bStopping)return;TGuardValue<bool> Guard(bStopping,true);ClearV2Shots();ClearSeatedBody();HeldAttackBody=nullptr;
 if(Pilot.IsValid())if(auto* Audio=Pilot->FindComponentByClass<UEGCombatAudioComponent>()){Audio->StopVillVGunLoop(false);Audio->StopVillVCannonCharge();}
 if(ChargeEffect.IsValid())ChargeEffect->Destroy();ChargeEffect.Reset();
 for(const auto& Effect:ShotEffects)if(Effect.IsValid())Effect->Destroy();ShotEffects.Reset();
 const auto Snapshot=Devices;Devices.Reset();
 for(const auto& Entry:Snapshot)if(auto* D=Entry.Get();IsValid(D)){D->OnDied.RemoveDynamic(this,&ThisClass::DeviceDied);D->Destroy();}
 if(Phase!=EEGVillVPhase::Retired)Phase=EEGVillVPhase::Inactive;
 AutoRemaining=AutoPosition=0.f;AutoDevice=INDEX_NONE;AutoUltimate.Invalidate();AutoShot.Invalidate();AutoTarget.Reset();
 Route.Reset();Round.Invalidate();Battle.Reset();Definition=nullptr;bUltimateSwitch=false;bActionEnhanced=false;bEnhancedPending=false;bRestartJumpQueued=false;bFiredThisAction=false;bHoldDeviceFiringPose=false;bJumpPlaybackStarted=false;bHasApproach=false;GunAimLostSeconds=0.f;
}
void UEGVillVMechanismComponent::EndPlay(const EEndPlayReason::Type R){Stop();Pilot.Reset();Super::EndPlay(R);}
int32 UEGVillVMechanismComponent::UltimatePresentation() const{return DeviceAlive(1-Current)?Current:2+Current;}
bool UEGVillVMechanismComponent::Muzzle(FVector& Location) const{return DeviceAlive(Current)&&GetDevice(Current)->Muzzle(Location);}
bool UEGVillVMechanismComponent::OrientForGroundOffset(int32 Index,const FVector& Offset,const FVector& Toward,FVector& GroundPoint)
{
 auto* Device=GetDevice(Index);auto* GM=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
 auto* Grid=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;if(!Device||!Grid)return false;
 const float Preferred=(Toward-Device->GetActorLocation()).Rotation().Yaw-Offset.Rotation().Yaw;
 float Best=TNumericLimits<float>::Max(),BestYaw=0;bool Found=false;
 for(int32 I=0;I<16;++I)
 {
  const float Yaw=Preferred+I*22.5f;FVector Point=Device->GetActorLocation()+FRotator(0,Yaw,0).RotateVector(Offset);FIntPoint Cell;
  if(!Grid->TryWorldToCell(Point,Cell))continue;
  const FVector Local=Grid->GetActorTransform().InverseTransformPosition(Point);
  if(FMath::Abs(Local.X)>Grid->Columns*Grid->CellWidth*.5f-60||FMath::Abs(Local.Y)>Grid->Rows*Grid->CellDepth*.5f-60)continue;
  const float Cost=FVector::DistSquared2D(Point,Toward);
  if(Cost<Best&&Pilot->GetCombat()->IsBoardLocationFree(Point,Device)){Best=Cost;BestYaw=Yaw;GroundPoint=Point;Found=true;}
 }
 if(Found)Device->SetActorRotation(FRotator(0,BestYaw,0));return Found;
}
void UEGVillVMechanismComponent::StartTravel(int32 Index)
{
 HeldAttackBody=nullptr;bHoldDeviceFiringPose=false;
 if(!IsCurrentBattle())return;
 if(!DeviceAlive(Index))Index=1-Index;
 if(!DeviceAlive(Index)){Phase=EEGVillVPhase::Retired;Pilot->Die();return;}
 if(!bOpeningEntry){StartJump(Index);return;}
 ClearSeatedBody();Destination=Index;Phase=EEGVillVPhase::Moving;Route.Reset();RouteRetry=0;bHasApproach=false;++GroundTravelCount;
 Pilot->GetCombat()->ResetBoardPursuitStall();
 BuildRoute();
}
bool UEGVillVMechanismComponent::BuildRoute()
{
 Route.Reset();RouteRetry=.6f+(Pilot->GetUniqueID()%7)*.05f;if(!IsCurrentBattle()||!DeviceAlive(Destination))return false;
 if(!bHasApproach)
 {
  const FVector Offset=Definition->EntryApproachOffsets.IsValidIndex(Destination)?Definition->EntryApproachOffsets[Destination]:FVector(-220,0,0);
  bHasApproach=OrientForGroundOffset(Destination,Offset,Pilot->GetActorLocation(),Approach);
  if(!bHasApproach)return false;
  Approach.Z=Pilot->GetActorLocation().Z;
 }
 const TArray<FVector> Goals{Approach};FVector Selected;
 // The destination is a composite pilot/device seat. Other devices, including
 // the previous seat, remain obstacles except for ordinary outward escape.
 if(Pilot->GetCombat()->PlanBoardRoute(Goals,Route,Selected,GetDevice(Destination)))return true;
 bHasApproach=false;return false;
}
void UEGVillVMechanismComponent::StartEntry()
{
 if(!IsCurrentBattle()||!DeviceAlive(Destination))return;
 if(!Pilot->GetCombat()->IsBoardLocationFree(GetDevice(Destination)->GetActorLocation(),GetDevice(Destination)))
 {bHasApproach=false;Route.Reset();RouteRetry=.5f+(Pilot->GetUniqueID()%7)*.05f;return;}
 const int32 EntryIndex=Destination;
 bEntryCarriesFire=false;
 if(!Definition->EntryActions.IsValidIndex(EntryIndex)||!Definition->EntryBodies.IsValidIndex(EntryIndex))return;
 Phase=EEGVillVPhase::Entering;ClearSeatedBody();Pilot->SetActorLocation(GetDevice(Destination)->GetActorLocation());Pilot->SetActorRotation(GetDevice(Destination)->GetActorRotation());
 ActionWeaponOffset=bUltimateSwitch?Definition->EntryActions[Destination].Montage.LoadSynchronous()->GetPlayLength():0.f;
 if(!Pilot->GetHeroSkill()->PlayTraversalStage(Definition->EntryActions[EntryIndex],Definition->EntryBodies[EntryIndex].LoadSynchronous(),1.5f))StartTravel(Destination);
}
void UEGVillVMechanismComponent::BeginAction(const FEGSkillActionContext& C)
{
 if(ChargeEffect.IsValid())ChargeEffect->Destroy();ChargeEffect.Reset();
 if(C.bUltimate){HeldAttackBody=nullptr;if(DeviceAlive(1-Current))ClearV2Shots();}
 bHoldDeviceFiringPose=false;
 bActionEnhanced=bEnhancedPending;ActionWeaponOffset=0;bFiredThisAction=false;
 bEntryCarriesFire=false;
 if(!C.bUltimate){RefreshSeatedBasePose();return;}
 bActionEnhanced=true;
 if(DeviceAlive(1-Current))
 {
  bUltimateSwitch=true;Destination=1-Current;Phase=EEGVillVPhase::Leaving;
  LeaveOrigin=Pilot->GetActorLocation();LeaveRotation=Pilot->GetActorRotation();
  // Keep the occupied seat stable; subsequent travel is the original jump entry, not ground pursuit.
 }
 else {bEnhancedPending=true;bUltimateSwitch=false;}
 if(auto* Audio=Pilot->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent((DeviceAlive(1-Current)?1-Current:Current)==0?TEXT("VoiceToGun"):TEXT("VoiceToCannon"),Pilot->GetActorLocation());
}
void UEGVillVMechanismComponent::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle())return;
 if(Event==TEXT("Depart")&&Phase==EEGVillVPhase::Leaving)
 {
  const FVector Offset=Definition->ExitGroundOffsets.IsValidIndex(Current)?Definition->ExitGroundOffsets[Current]:FVector(-220,0,0);
  if(Definition->ExitProgressCurves.IsValidIndex(Current)&&Definition->ExitProgressCurves[Current])
  {
   // Sample the actual departing montage clock before it is invalidated. The
   // rebased source pose and actor travel must never both apply the same delta.
   if(auto* M=Pilot->GetHeroSkill()->GetActiveMontage())if(auto* I=Pilot->GetVisiblePresentationMesh()->GetAnimInstance())
   {
    const float Position=I->Montage_GetPosition(M);
    // Queued end notifies can run after UE removes the active montage instance.
    // Montage_GetPosition then returns zero, which must not rewind departure.
    UpdateDeparture(Position>0.f?Position:M->GetPlayLength());
   }
  }
  else if(auto* D=GetDevice(Current))
  {
   const FVector Desired=D->GetActorLocation()+D->GetActorRotation().RotateVector(Offset);
   TArray<FVector> Departure{Desired};Pilot->GetCombat()->AdvanceBoardRoute(Departure,FVector::Dist2D(Pilot->GetActorLocation(),Desired),D,nullptr,nullptr,false);
  }
  Pilot->GetHeroSkill()->SuspendForTraversal();StartJump(Destination);
 }
 else if(Event==TEXT("Dock")&&(Phase==EEGVillVPhase::Entering||Phase==EEGVillVPhase::Jumping))
 {
  if(!DeviceAlive(Destination)){Pilot->GetHeroSkill()->SuspendForTraversal();StartJump(1-Destination);return;}
   if(Phase==EEGVillVPhase::Jumping&&(!bJumpPlaybackStarted||!Pilot->GetCombat()->IsBoardLocationFree(GetDevice(Destination)->GetActorLocation(),GetDevice(Destination))))
  {SuspendJumpForRetry();return;}
  Pilot->SetActorRotation(GetDevice(Destination)->GetActorRotation());
  Current=Destination;bOpeningEntry=false;Phase=EEGVillVPhase::Operating;
  if(bUltimateSwitch){bEnhancedPending=true;bActionEnhanced=true;if(!Pilot->GetHeroSkill()->CommitStigmataUltimate(C.ActionId))return;}
 }
}
void UEGVillVMechanismComponent::ActionEnded(bool bCompleted)
{
 // Retain only a successfully fired occupied-body clip; this is a cosmetic
 // pose, not an action, notify source, damage timer or target authority.
 HeldAttackBody=bCompleted&&bFiredThisAction&&IsCurrentBattle()&&FiringTarget()&&Pilot.IsValid()?CurrentBasicBody():nullptr;
 if(ChargeEffect.IsValid())ChargeEffect->Destroy();ChargeEffect.Reset();
 bHoldDeviceFiringPose=false;
 if(bCompleted&&bFiredThisAction&&IsCurrentBattle()&&DeviceAlive(Current)&&Definition&&FiringTarget())
 {
  const auto& Attacks=bActionEnhanced&&Definition->EnhancedDeviceAttacks.IsValidIndex(Current)?Definition->EnhancedDeviceAttacks:Definition->DeviceAttacks;
  if(Attacks.IsValidIndex(Current))if(auto* Motion=Attacks[Current].LoadSynchronous();Motion&&!Motion->Motions.IsEmpty())
  {
   GetDevice(Current)->ShowDevice(Motion,Motion->Motions[0].Duration);
   bHoldDeviceFiringPose=true;
  }
 }
 bFiredThisAction=false;
 // Successful cycles leave source hit/smoke tails on their own finite clocks.
 // Keep weak ownership so Stop still clears them at settlement/round teardown.
 if(!bCompleted){ClearV2Shots();for(const auto& Effect:ShotEffects)if(Effect.IsValid())Effect->Destroy();ShotEffects.Reset();}
 else ShotEffects.RemoveAll([](const auto& Effect){return !Effect.IsValid();});
 if(bStopping||bHandlingDeath||!IsCurrentBattle())return;
 if(Phase==EEGVillVPhase::Jumping)
 {bRestartJumpQueued=true;QueuedJump=DeviceAlive(Destination)?Destination:1-Destination;return;}
 if(!bCompleted&&(Phase==EEGVillVPhase::Leaving||Phase==EEGVillVPhase::Entering))
 {bRestartJumpQueued=true;QueuedJump=DeviceAlive(Current)?Current:1-Current;}
 bActionEnhanced=false;bUltimateSwitch=false;ActionWeaponOffset=0;
}
void UEGVillVMechanismComponent::SampleAction(float Position)
{
 if(Phase==EEGVillVPhase::Leaving)UpdateDeparture(Position);
 if(Phase==EEGVillVPhase::Jumping)UpdateJump(Position);
 // Sample from the start of the attack, including cannon charging. Damage is
 // not a playback clock: an armored target must not freeze the device pose.
 if(!IsCurrentBattle()||Phase!=EEGVillVPhase::Operating||!DeviceAlive(Current)||!Definition->DeviceAttacks.IsValidIndex(Current))return;
 RefreshSeatedBasePose();
 const auto& Attacks=bActionEnhanced&&Definition->EnhancedDeviceAttacks.IsValidIndex(Current)?Definition->EnhancedDeviceAttacks:Definition->DeviceAttacks;
 if(Position>=ActionWeaponOffset)
 {
  GetDevice(Current)->ShowDevice(Attacks[Current].LoadSynchronous(),Position-ActionWeaponOffset);
 }
}
void UEGVillVMechanismComponent::RefreshSeatedBody()
{
 if(!IsCurrentBattle()||!DeviceAlive(Current)||!Definition->SeatedBodies.IsValidIndex(Current))return;
 auto* D=Definition->SeatedBodies[Current].LoadSynchronous();const auto* P=Pilot->GetDefinition()->PresentationDefinition.Get();if(!D||!P)return;
 if(SeatedBody&&SeatedBody->Definition!=D)ClearSeatedBody();
 if(!SeatedBody)
 {
  SeatedBody=GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,Pilot.Get(),nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);if(!SeatedBody)return;
  SeatedBody->Definition=D;SeatedBody->bAutoPlay=false;SeatedBody->FinishSpawning(FTransform::Identity);SeatedBody->SetActorTickEnabled(false);
  SeatedBody->AttachToComponent(Pilot->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);
  SeatedBody->SetActorRelativeTransform(FTransform(P->PresentationRotation,FVector(0,0,-88+P->PresentationGroundOffset),P->MeshScale));
  SeatedBody->SetBoardModelScale(FEGBoardModelSizing::Resolve(Pilot->GetDefinition(),Pilot.Get()),P->PresentationGroundOffset);
 }
 SeatedBody->BindBody(Pilot->GetVisiblePresentationMesh(),true);
 const float Length=D->Motions.IsEmpty()?1.f:D->Motions[0].Duration;SeatedBody->SetSourceTime(FMath::Fmod(SpawnClock,FMath::Max(.01f,Length)));
 Pilot->GetVisiblePresentationMesh()->SetVisibility(true);
 // SetSourceTime above sampled the idle seat. Restore the continuous attack
 // pose after it so the idle helper cannot overwrite the held body pose.
 RefreshSeatedBasePose();
}
UAnimSequence* UEGVillVMechanismComponent::CurrentBasicBody() const
{
 const auto* Hero=Pilot.IsValid()?Pilot->GetDefinition():nullptr;
 const auto* P=Hero?Hero->PresentationDefinition.Get():nullptr;
 return P&&P->AttackSequence.IsValidIndex(Current)?P->AttackSequence[Current].Animation.LoadSynchronous():nullptr;
}
void UEGVillVMechanismComponent::RefreshSeatedBasePose()
{
 if(!IsCurrentBattle()||!IsOperating()||!DeviceAlive(Current)||!Definition->SeatedBodies.IsValidIndex(Current))return;
 auto* Skill=Pilot->GetHeroSkill();
 auto* Mesh=Pilot->GetVisiblePresentationMesh();
 auto* Instance=Mesh?Cast<UEGCombatAnimInstance>(Mesh->GetAnimInstance()):nullptr;
 if(Instance&&Skill)
 {
  if(Skill->IsActionRunning()&&!Skill->IsCasting())
  {
   if(auto* Clip=Skill->GetActiveAnimation();Clip&&Clip==CurrentBasicBody())
   {
    // Stable occupied firing base beneath the FullBody montage, including its
    // blend in/out. It is visual only; notifies stay on the montage.
    Instance->SetSourcePose(Clip,0.f,0);
    return;
   }
  }
  else if(HeldAttackBody&&HeldAttackBody==CurrentBasicBody()&&FiringTarget())
  {
   Instance->SetSourcePose(HeldAttackBody,0.f,0);
   return;
  }
 }
 const auto* D=Definition->SeatedBodies[Current].Get();if(!D||D->Motions.IsEmpty())return;
 const auto& Motion=D->Motions[0];
 for(const auto& Part:Motion.Parts)if(Part.PartId==TEXT("Body")&&Part.Animation)
 {
  // FullBody montages blend over the occupied seat, never the ground standby.
  // This only supplies the AnimBP base pose; attack notifies remain montage-owned.
  if(auto* I=Cast<UEGCombatAnimInstance>(Pilot->GetVisiblePresentationMesh()->GetAnimInstance()))
   I->SetSourcePose(Part.Animation,FMath::Fmod(SpawnClock,FMath::Max(.01f,Part.Animation->GetPlayLength())),0);
  break;
 }
}
void UEGVillVMechanismComponent::TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Tick,Function);
 if(!IsCurrentBattle()||!Definition||(Battle->IsCombatRunning()&&IEGCombatant::Execute_GetCombatState(Pilot.Get())!=EEGUnitState::Fighting))return;
 if(auto* Audio=Pilot->FindComponentByClass<UEGCombatAudioComponent>())
 {
  const bool CannotContinue=!FiringTarget()||(!Pilot->GetHeroSkill()->IsActionRunning()&&Pilot->GetAttributes()->GetAttackSpeed()<=0.f);
  const bool GunAvailable=Phase==EEGVillVPhase::Operating&&Current==0&&DeviceAlive(0)&&!CannotContinue;
  GunAimLostSeconds=GunAvailable&&!IsAimReady()?GunAimLostSeconds+FMath::Max(0.f,Delta):0.f;
  if(!GunAvailable||GunAimLostSeconds>.15f)Audio->StopVillVGunLoop();
  if(Phase!=EEGVillVPhase::Operating||Current!=1||!DeviceAlive(1)||CannotContinue)Audio->StopVillVCannonCharge();
 }
 // FinishAction is still tearing down the old montage in ActionEnded. Start its
 // replacement on the next component tick rather than reentering that teardown.
 if(bRestartJumpQueued){bRestartJumpQueued=false;StartTravel(QueuedJump);return;}
 TickAutoFire(Delta);
 if(!IsCurrentBattle())return;
 SpawnClock+=Delta;
 if(bHoldDeviceFiringPose&&(Phase!=EEGVillVPhase::Operating||!DeviceAlive(Current)||!FiringTarget()))bHoldDeviceFiringPose=false;
 for(int32 I=0;I<2;++I)if(DeviceAlive(I))
 {
  const bool bHoldCurrentDevice=I==Current&&Phase==EEGVillVPhase::Operating&&bHoldDeviceFiringPose;
  if(!(I==AutoDevice&&AutoRemaining>0.f)&&!bHoldCurrentDevice&&(I!=Current||Phase!=EEGVillVPhase::Operating||!Pilot->GetHeroSkill()->IsActionRunning())&&Definition->DeviceIdles.IsValidIndex(I))
   if(auto* Idle=Definition->DeviceIdles[I].LoadSynchronous();Idle&&!Idle->Motions.IsEmpty())GetDevice(I)->ShowDevice(Idle,FMath::Fmod(SpawnClock,FMath::Max(.01f,Idle->Motions[0].Duration)));
 }
 if(Phase==EEGVillVPhase::Operating)
 {
  if(!DeviceAlive(Current)){HeldAttackBody=nullptr;DeviceDied(GetDevice(Current));return;}
  if(HeldAttackBody&&!Pilot->GetHeroSkill()->IsActionRunning()&&!FiringTarget())HeldAttackBody=nullptr;
  Pilot->SetActorLocation(GetDevice(Current)->GetActorLocation());UpdateAim(Delta);
  RefreshSeatedBasePose();
  if(!Pilot->GetHeroSkill()->IsActionRunning())RefreshSeatedBody();return;
 }
 if(Phase==EEGVillVPhase::Jumping)
 {
  if(!DeviceAlive(Destination)){Pilot->GetHeroSkill()->SuspendForTraversal();StartJump(1-Destination);return;}
  if(!bJumpPlaybackStarted){RouteRetry-=FMath::Max(0.f,Delta);if(RouteRetry<=0.f)StartJumpPlayback();}
  return;
 }
 if(Phase!=EEGVillVPhase::Moving)return;
 if(!DeviceAlive(Destination)){StartTravel(1-Destination);return;}
 RouteRetry-=Delta;if(Route.IsEmpty()&&RouteRetry<=0)BuildRoute();
 if(!Route.IsEmpty()&&bHasApproach&&Pilot->GetCombat()->IsBoardPursuitStalled(Approach,Delta))
 {Route.Reset();RouteRetry=3.f;Pilot->GetCombat()->ResetBoardPursuitStall();}
 if(Route.IsEmpty()){if(bHasApproach&&FVector::DistSquared2D(Pilot->GetActorLocation(),Approach)<16)StartEntry();return;}
 const FVector Direction=(Route[0]-Pilot->GetActorLocation()).GetSafeNormal2D();
 if(!Pilot->GetCombat()->AdvanceBoardRoute(Route,Delta*Definition->TravelSpeed,GetDevice(Destination)))
 {Route.Reset();bHasApproach=false;RouteRetry=.25f+(Pilot->GetUniqueID()%7)*.05f;return;}
 if(!Direction.IsNearlyZero())Pilot->SetActorRotation(Direction.Rotation());
 FEGAnimationPlayback::Speed(Pilot->GetVisiblePresentationMesh(),Definition->TravelSpeed);
 if(Route.IsEmpty())StartEntry();
}
void UEGVillVMechanismComponent::DeviceDied(AEGChessUnit* Device)
{
 if(Device==GetDevice(Current))ClearV2Shots();
 HeldAttackBody=nullptr;bHoldDeviceFiringPose=false;
 if(bStopping||bHandlingDeath||!IsCurrentBattle())return;TGuardValue<bool> Guard(bHandlingDeath,true);
 if(!DeviceAlive(0)&&!DeviceAlive(1))
 {Phase=EEGVillVPhase::Retired;ClearSeatedBody();Pilot->GetHeroSkill()->CancelAction();Pilot->Die();return;}
 if(Phase==EEGVillVPhase::Operating&&Device==GetDevice(Current))
 {
  Pilot->GetHeroSkill()->CancelAction();ClearSeatedBody();bUltimateSwitch=false;bActionEnhanced=false;Destination=1-Current;

  const auto* P=Pilot->GetDefinition()->PresentationDefinition.Get();Phase=EEGVillVPhase::Leaving;
  LeaveOrigin=Pilot->GetActorLocation();LeaveRotation=Pilot->GetActorRotation();
  if(!P||!P->UltimateSequence.IsValidIndex(Current)||!P->UltimateAssemblies.IsValidIndex(Current)||!Pilot->GetHeroSkill()->PlayTraversalStage(P->UltimateSequence[Current],P->UltimateAssemblies[Current].LoadSynchronous(),1.5f))StartTravel(Destination);
 }
 else if((Phase==EEGVillVPhase::Moving||Phase==EEGVillVPhase::Entering||Phase==EEGVillVPhase::Jumping||Phase==EEGVillVPhase::Leaving)&&!DeviceAlive(Destination))
 {Pilot->GetHeroSkill()->SuspendForTraversal();StartTravel(1-Destination);}
}


bool UEGVillVMechanismComponent::CaptureV2Muzzle(FName Event,FTransform& Muzzle) const
{
 if(!IsCurrentBattle()||!IsOperating()||!DeviceAlive(Current)||!Definition||!Definition->SourceCombatFX
    ||!Definition->SourceCombatFX->ShotPresetsV2.IsValidIndex(Current*2+(bActionEnhanced?1:0)))return false;
 const int32 Barrel=Event==TEXT("Fire1")?0:Event==TEXT("Fire2")?1:Event==TEXT("Fire3")?2:INDEX_NONE;
 if(Barrel==INDEX_NONE||(Current==1&&Barrel!=0))return false;
 const FName Bone=Current==0?FName(*FString::Printf(TEXT("FirePoint%d"),Barrel+1)):FName(TEXT("CannonFire"));
 if(!GetDevice(Current)->MuzzleFrame(Bone,Muzzle)||Muzzle.ContainsNaN())return false;
 // Source muzzle +Z is the authored V2 effects' +X; socket scale is discarded.
 Muzzle.SetRotation(Muzzle.GetRotation()*FQuat::FindBetweenNormals(FVector::ForwardVector,FVector::UpVector));
 Muzzle.SetScale3D(FVector::OneVector);
 return !Muzzle.ContainsNaN();
}
void UEGVillVMechanismComponent::PresentV2Shot(FName Event,const FTransform& Muzzle,const FVector& Target,const FGuid& Action,const FGuid& ShotRound,AEGVillVEmplacement* ShotDevice,bool bEnhanced)
{
 if(!IsCurrentBattle()||Round!=ShotRound||!IsOperating()||!DeviceAlive(Current)||GetDevice(Current)!=ShotDevice
    ||bActionEnhanced!=bEnhanced||!Definition||!Pilot.IsValid())return;
 auto* Skill=Pilot->GetHeroSkill();auto* Config=Definition->SourceCombatFX.Get();
 if(!IsValid(Skill)||!IsValid(Config)||!Action.IsValid()||Skill->ActionId!=Action||!Skill->IsActionRunning())return;
 const int32 Barrel=Event==TEXT("Fire1")?0:Event==TEXT("Fire2")?1:Event==TEXT("Fire3")?2:INDEX_NONE;
 if(Barrel==INDEX_NONE||(Current==1&&Barrel!=0))return;
 const int32 PresetIndex=Current*2+(bActionEnhanced?1:0);
 if(!Config->ShotPresetsV2.IsValidIndex(PresetIndex))return;
 V2ShotEffects.RemoveAll([](const auto& Item){return !Item.IsValid();});
 const int32 Limit=FMath::Clamp(Config->MaxConcurrentV2Shots,1,24);
 while(V2ShotEffects.Num()>=Limit)
 {
  const auto Oldest=V2ShotEffects[0];V2ShotEffects.RemoveAt(0);
  if(Oldest.IsValid())Oldest->Destroy();
  UE_LOG(LogTemp,Warning,TEXT("VillV V2 shot cap %d reached; oldest visual removed."),Limit);
 }
 const FName Bone=Current==0?FName(*FString::Printf(TEXT("FirePoint%d"),Barrel+1)):FName(TEXT("CannonFire"));
 if(auto* Visual=AEGVillVShotVisual::SpawnV2(Skill,GetDevice(Current),Config,PresetIndex,Barrel,Bone,Muzzle,Target,Action))
  V2ShotEffects.Add(Visual);
}

AEGChessUnit* UEGVillVMechanismComponent::FiringTarget(AEGChessUnit* Preferred) const
{
 if(!IsCurrentBattle()||!IsOperating()||!DeviceAlive(Current))return nullptr;
 auto* Combat=Pilot->GetCombat();
 if(Combat->IsValidEnemy(Preferred)&&Combat->IsInAttackRange(Preferred))return Preferred;
 auto* T=Combat->GetTarget();if(Combat->IsValidEnemy(T)&&Combat->IsInAttackRange(T))return T;
 T=Combat->FindNearestEnemy();return Combat->IsInAttackRange(T)?T:nullptr;
}
bool UEGVillVMechanismComponent::IsAimReady(AEGChessUnit* Target) const
{
 Target=FiringTarget(Target);if(!Target||!Definition)return false;
 const float Desired=(Target->GetActorLocation()-GetDevice(Current)->GetActorLocation()).Rotation().Yaw;
 return FMath::Abs(FMath::FindDeltaAngleDegrees(GetDevice(Current)->GetActorRotation().Yaw,Desired))<=Definition->AimTolerance;
}
void UEGVillVMechanismComponent::UpdateAim(float Delta)
{
 auto* T=FiringTarget();auto* D=GetDevice(Current);if(!D)return;
 if(T)
 {
  const float Desired=(T->GetActorLocation()-D->GetActorLocation()).Rotation().Yaw;
  const float Yaw=FMath::FixedTurn(D->GetActorRotation().Yaw,Desired,Definition->AimDegreesPerSecond*FMath::Max(0.f,Delta));
  D->SetActorRotation(FRotator(0,Yaw,0));
 }
 Pilot->SetActorRotation(D->GetActorRotation());
}
void UEGVillVMechanismComponent::StartJump(int32 Index)
{
 HeldAttackBody=nullptr;bHoldDeviceFiringPose=false;
 if(!IsCurrentBattle())return;
 if(!DeviceAlive(Index))Index=1-Index;
 if(!DeviceAlive(Index)){Phase=EEGVillVPhase::Retired;Pilot->Die();return;}
 ClearSeatedBody();Destination=Index;Phase=EEGVillVPhase::Jumping;Route.Reset();bFiredThisAction=false;
 bJumpPlaybackStarted=false;RouteRetry=0.f;++JumpTransferCount;
 bEntryCarriesFire=bUltimateSwitch;
 StartJumpPlayback();
}
bool UEGVillVMechanismComponent::StartJumpPlayback()
{
 if(!IsCurrentBattle()||!DeviceAlive(Destination))return false;
 // Her body is non-blocking while seated and in flight. The destination
 // device remains blocking, and the dock must still be clear of other units.
 if(!Pilot->GetCombat()->IsBoardLocationFree(GetDevice(Destination)->GetActorLocation(),GetDevice(Destination)))
 {RouteRetry=.5f+(Pilot->GetUniqueID()%7)*.05f;return false;}
 JumpOrigin=Pilot->GetActorLocation();JumpRotation=Pilot->GetActorRotation();
 const int32 Index=Destination;
 const int32 JumpIndex=Index+(bEntryCarriesFire?2:0);
 const auto& Actions=Definition->JumpActions.IsValidIndex(JumpIndex)?Definition->JumpActions:Definition->EntryActions;
 const auto& Bodies=Definition->JumpBodies.IsValidIndex(JumpIndex)?Definition->JumpBodies:Definition->EntryBodies;
 if(!Actions.IsValidIndex(JumpIndex)||!Bodies.IsValidIndex(JumpIndex)){RouteRetry=1.f;return false;}
 const auto* Base=Actions[Index].Montage.LoadSynchronous();ActionWeaponOffset=bEntryCarriesFire&&Base?Base->GetPlayLength():0.f;
 if(!Pilot->GetHeroSkill()->PlayTraversalStage(Actions[JumpIndex],Bodies[JumpIndex].LoadSynchronous(),1.5f))
 {RouteRetry=1.f;return false;}
 bJumpPlaybackStarted=true;return true;
}
void UEGVillVMechanismComponent::SuspendJumpForRetry()
{
 if(!IsCurrentBattle()||Phase!=EEGVillVPhase::Jumping)return;
 bJumpPlaybackStarted=false;RouteRetry=.5f+(Pilot->GetUniqueID()%7)*.05f;
 Pilot->GetHeroSkill()->SuspendForTraversal();
}
void UEGVillVMechanismComponent::UpdateJump(float Position)
{
 if(!IsCurrentBattle()||!DeviceAlive(Destination))return;
 const float Start=Definition->JumpWarpStarts.IsValidIndex(Destination)?Definition->JumpWarpStarts[Destination]:.08f;
 const float End=Definition->JumpWarpEnds.IsValidIndex(Destination)?Definition->JumpWarpEnds[Destination]:.65f;
 const auto* Curve=Definition->JumpProgressCurves.IsValidIndex(Destination)?Definition->JumpProgressCurves[Destination].Get():nullptr;
 const float A=Curve?FMath::Clamp(Curve->GetFloatValue(Position),0.f,1.f):FMath::SmoothStep(0.f,1.f,FMath::Clamp((Position-Start)/FMath::Max(.01f,End-Start),0.f,1.f));
 if(A>.9f&&!Pilot->GetCombat()->IsBoardLocationFree(GetDevice(Destination)->GetActorLocation(),GetDevice(Destination)))
 {SuspendJumpForRetry();return;}
 Pilot->SetActorLocation(FMath::Lerp(JumpOrigin,GetDevice(Destination)->GetActorLocation(),A));
 const float Yaw=JumpRotation.Yaw+FMath::FindDeltaAngleDegrees(JumpRotation.Yaw,GetDevice(Destination)->GetActorRotation().Yaw)*A;
 Pilot->SetActorRotation(FRotator(0,Yaw,0));
}
void UEGVillVMechanismComponent::UpdateDeparture(float Position)
{
 if(!IsCurrentBattle()||!Definition||!Definition->ExitProgressCurves.IsValidIndex(Current)||!Definition->ExitGroundOffsets.IsValidIndex(Current))return;
 const auto* Curve=Definition->ExitProgressCurves[Current].Get();if(!Curve)return;
 // Follow the native curve but keep the last safe location if another unit
 // blocks its next segment. Edge seats may briefly leave the board; the wall
 // and other bodies still block, and the jump retains its own landing check.
 const FVector Desired=LeaveOrigin+LeaveRotation.RotateVector(Definition->ExitGroundOffsets[Current])*Curve->GetFloatValue(Position);
 TArray<FVector> Departure{Desired};
 Pilot->GetCombat()->AdvanceBoardRoute(Departure,FVector::Dist2D(Pilot->GetActorLocation(),Desired),GetDevice(Current),nullptr,nullptr,false);
}
