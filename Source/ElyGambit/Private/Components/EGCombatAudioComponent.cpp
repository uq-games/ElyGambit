#include "Components/EGCombatAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/EGChessUnit.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Kismet/GameplayStatics.h"

UEGCombatAudioComponent::UEGCombatAudioComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;PrimaryComponentTick.bTickEvenWhenPaused=true;}
void UEGCombatAudioComponent::BeginPlay(){Super::BeginPlay();if(auto* U=Cast<AEGChessUnit>(GetOwner()))U->OnDied.AddUniqueDynamic(this,&ThisClass::OwnerDied);}
void UEGCombatAudioComponent::OwnerDied(AEGChessUnit* Unit){Clear();}
void UEGCombatAudioComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction)
{
 Super::TickComponent(DeltaTime,TickType,ThisTickFunction);
 if(PendingEntryAudio.IsEmpty()){SetComponentTickEnabled(false);return;}
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(!IsCurrent()||!ActionId.IsValid()||!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit))
 {PendingEntryAudio.Reset();SetComponentTickEnabled(false);return;}
 auto* Presentation=Unit->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
 if(Presentation&&Presentation->IsEntryMoviePendingFor(RoundId,ActionId)&&!Presentation->IsEntryMoviePlaying())return;
 const auto Waiting=MoveTemp(PendingEntryAudio);PendingEntryAudio.Reset();
 TGuardValue<bool> Flush(bFlushingEntryAudio,true);
 for(const auto& Event:Waiting)ActionEvent(Event.Key,Event.Value);
 SetComponentTickEnabled(false);
}
void UEGCombatAudioComponent::Configure(UEGBattleContextComponent* Context)
{Clear();Battle=Context;RoundId=Context?Context->GetRoundId():FGuid();EventCount=SpawnedVoiceCount=0;LastNode=NAME_None;LastSoundPath.Reset();}
bool UEGCombatAudioComponent::IsCurrent() const
{return Battle.IsValid()&&Battle->IsBattleActive()&&RoundId.IsValid()&&Battle->GetRoundId()==RoundId;}
void UEGCombatAudioComponent::BeginAction(FGuid Id,bool Ultimate,int32 Index)
{
 auto* U=Cast<AEGChessUnit>(GetOwner());
 if(U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::VillV)
 {
  if(Ultimate||Index!=0)StopVillVGunLoop();
  if(Ultimate||Index!=1)StopVillVCannonCharge();
 }
 PendingEntryAudio.Reset();SetComponentTickEnabled(false);
 ActionId=Id;bUltimate=Ultimate;AttackIndex=Index;Submitted.Reset();SubmittedPulses.Reset();
}
void UEGCombatAudioComponent::ActionEvent(FName Node,const FVector& Point)
{
 if(!IsCurrent()||!ActionId.IsValid()||Submitted.Contains(Node))return;
 auto* U=Cast<AEGChessUnit>(GetOwner());
 if(!bFlushingEntryAudio&&bUltimate&&AttackIndex==0&&U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)
  if(auto* Movie=U->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();Movie&&Movie->IsEntryMoviePendingFor(RoundId,ActionId)&&!Movie->IsEntryMoviePlaying())
  {PendingEntryAudio.FindOrAdd(Node)=Point;SetComponentTickEnabled(true);return;}
 const bool VillV=U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::VillV;
 if(VillV&&Node==TEXT("GunEnd"))return;
 if(VillV&&Node==TEXT("CannonCharge")&&VillVCannonCharge.IsValid()&&VillVCannonCharge->IsPlaying())return;
 if(VillV&&Node==TEXT("GunStart")&&VillVGunStart.IsValid()&&VillVGunStart->IsPlaying())return;
 if(VillV&&(Node==TEXT("GunStart")||Node==TEXT("GunFire1")||Node==TEXT("GunFire2")||Node==TEXT("GunFire3"))
    &&VillVGunLoop.IsValid()&&VillVGunLoop->IsPlaying()){UpdateVillVGunLoopRate();return;}
 if(VillV&&Node==TEXT("GunStart"))
 {
  auto* Tail=VillVGunTail.Get();VillVGunTail.Reset();
  if(IsValid(Tail)&&Tail->IsPlaying())Tail->FadeOut(.05f,0.f);
 }
 const bool GunFire=VillV&&(Node==TEXT("GunFire1")||Node==TEXT("GunFire2")||Node==TEXT("GunFire3"));
 const FName Routed=GunFire?FName(TEXT("GunLoopFull")):VillV&&Node==TEXT("CannonFire")?FName(TEXT("CannonFireFull")):
  VillV&&Node==TEXT("GunStart")?FName(TEXT("GunStartFull")):VillV&&Node==TEXT("CannonCharge")?FName(TEXT("CannonChargeFull")):Node;
 const int32 Before=EventCount;
 Submitted.Add(Node);Play(Routed,ActionId,bUltimate,AttackIndex,Point,true);
 if(GunFire)UpdateVillVGunLoopRate();
 if(Routed!=Node&&EventCount>Before)LastNode=Node;
}
void UEGCombatAudioComponent::UpdateVillVGunLoopRate()
{
 auto* U=Cast<AEGChessUnit>(GetOwner());auto* Voice=VillVGunLoop.Get();
 auto* Skill=U?U->GetHeroSkill():nullptr;auto* Mesh=U?U->GetVisiblePresentationMesh():nullptr;
 auto* Anim=Mesh?Mesh->GetAnimInstance():nullptr;auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 if(!Voice||!Anim||!Montage)return;
 const float Rate=Anim->Montage_GetPlayRate(Montage);
 if(FMath::IsFinite(Rate)&&Rate>0.f)Voice->SetPitchMultiplier(FMath::Clamp(Rate,.125f,4.f));
}
void UEGCombatAudioComponent::ConfirmHit(FName Node,FGuid Id,AEGChessUnit* Target)
{if(Id==ActionId&&IsValid(Target))ActionEvent(Node,Target->GetActorLocation());}
void UEGCombatAudioComponent::CommittedImpact(FName Node,FGuid Round,FGuid Id,bool Ultimate,int32 Index,int32 Pulse,const FVector& Point)
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(!IsCurrent()||Round!=RoundId||!Id.IsValid()||Node.IsNone()||Pulse<0||Point.ContainsNaN()||
    !IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit))return;
 const FString Key=Id.ToString(EGuidFormats::Digits)+TEXT(":")+Node.ToString()+TEXT(":")+FString::FromInt(Pulse);
 if(CommittedImpacts.Contains(Key))return;
 CommittedImpacts.Add(Key);
 // A subsequent basic cancellation must not silence an already committed rain
 // impact. Owner death, round stop and EndPlay still clear all component voices.
 Play(Node,Id,Ultimate,Index,Point,false);
}
void UEGCombatAudioComponent::ActionPulse(FName Node,FGuid ExpectedAction,FGuid PulseToken,const FVector& Point)
{
 if(!IsCurrent()||!bUltimate||!ActionId.IsValid()||ExpectedAction!=ActionId||!PulseToken.IsValid()||SubmittedPulses.Contains(PulseToken))return;
 SubmittedPulses.Add(PulseToken);Play(Node,ActionId,true,AttackIndex,Point,true);
}
void UEGCombatAudioComponent::RegisterLaunch(FGuid Id)
{if(IsCurrent()&&Id.IsValid()){Launched.Add(Id);ActionEvent(TEXT("Release"),GetOwner()->GetActorLocation());}}
void UEGCombatAudioComponent::Land(FGuid Id,FGuid Round,const FVector& Point)
{if(IsCurrent()&&Round==RoundId&&Launched.Remove(Id)>0)Play(TEXT("Land"),Id,true,INDEX_NONE,Point,false);}
void UEGCombatAudioComponent::Play(FName Node,FGuid Id,bool Ultimate,int32 Index,const FVector& Point,bool TrackAction)
{
 auto* U=Cast<AEGChessUnit>(GetOwner());const auto* D=U?U->GetDefinition():nullptr;
 const auto* P=D?D->GetCombatAudioPresentation():nullptr;
 if(!IsCurrent()||!P||!P->bUseEventAudio||GetNetMode()==NM_DedicatedServer)return;
 Voices.RemoveAll([](const TObjectPtr<UAudioComponent>& V){return !IsValid(V)||!V->IsPlaying();});
 for(auto It=ActionVoices.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Key()->IsPlaying())It.RemoveCurrent();
 for(int32 EntryIndex=0;EntryIndex<P->AudioEvents.Num();++EntryIndex)
 {
  const auto& Entry=P->AudioEvents[EntryIndex];
  if(Entry.Node!=Node||Entry.bUltimate!=Ultimate||(Entry.bVoice&&!Ultimate)||(Entry.AttackIndex!=INDEX_NONE&&Entry.AttackIndex!=Index)||Entry.Sounds.IsEmpty())continue;
  // The run owns a preload handle. Never synchronously load audio in combat.
  const auto* Previous=PreviousVariants.Find(EntryIndex);
  const bool Avoid=Entry.bAvoidImmediateRepeat&&Entry.Sounds.Num()>1&&Previous&&Entry.Sounds.IsValidIndex(*Previous);
  int32 Choice=FMath::RandHelper(Entry.Sounds.Num()-(Avoid?1:0));
  if(Avoid&&Choice>=*Previous)++Choice;
  auto* Sound=Entry.Sounds[Choice].Get();if(!Sound)continue;
  PreviousVariants.Add(EntryIndex,Choice);
  const FVector Location=Entry.bAtTarget?Point:U->GetActorLocation();
  ++EventCount;LastNode=Node;LastSoundPath=Sound->GetPathName();
  auto* Voice=UGameplayStatics::SpawnSoundAtLocation(this,Sound,Location,FRotator::ZeroRotator,
   P->CombatAudioVolume*Entry.Volume,1.f,0.f,P->AudioAttenuation.Get(),Entry.ConcurrencyOverride.IsNull()?P->AudioConcurrency.Get():Entry.ConcurrencyOverride.Get(),true);
  if(Voice)
  {
   // Human transformation owns a short world pause. Its source voice and
   // transformation effects must keep sounding at their original pitch.
   if(D->HeroKind==EEGHeroKind::ElysiaHerrscher&&Ultimate&&Index==0)Voice->SetUISound(true);
   ++SpawnedVoiceCount;Voices.Add(Voice);
   const bool GunLoop=D->HeroKind==EEGHeroKind::VillV&&Node==TEXT("GunLoopFull");
   if(GunLoop)VillVGunLoop=Voice;
   else if(D->HeroKind==EEGHeroKind::VillV&&Node==TEXT("GunStartFull"))VillVGunStart=Voice;
   else if(D->HeroKind==EEGHeroKind::VillV&&Node==TEXT("GunEndFull"))VillVGunTail=Voice;
   else if(D->HeroKind==EEGHeroKind::VillV&&Node==TEXT("CannonChargeFull"))VillVCannonCharge=Voice;
   else if(TrackAction&&Entry.bStopOnCancel)ActionVoices.Add(Voice,Id);
  }
 }
}
void UEGCombatAudioComponent::StopVillVGunLoop(bool bPlayTail)
{
 auto* Start=VillVGunStart.Get();VillVGunStart.Reset();
 if(IsValid(Start)&&Start->IsPlaying())Start->FadeOut(.05f,0.f);
 auto* Voice=VillVGunLoop.Get();VillVGunLoop.Reset();
 if(!IsValid(Voice))return;
 const bool WasPlaying=Voice->IsPlaying();Voice->FadeOut(.05f,0.f);
 if(WasPlaying&&bPlayTail&&IsCurrent()&&GetOwner())Play(TEXT("GunEndFull"),FGuid(),false,0,GetOwner()->GetActorLocation(),false);
}
void UEGCombatAudioComponent::StopVillVCannonCharge()
{
 auto* Voice=VillVCannonCharge.Get();VillVCannonCharge.Reset();
 if(IsValid(Voice)&&Voice->IsPlaying())Voice->FadeOut(.05f,0.f);
}
void UEGCombatAudioComponent::EndAction(bool Completed)
{
 PendingEntryAudio.Reset();SetComponentTickEnabled(false);
 if(!Completed){StopVillVGunLoop();StopVillVCannonCharge();}
 if(!Completed)for(auto It=ActionVoices.CreateIterator();It;++It)
  if(It.Value()==ActionId){if(It.Key().IsValid())It.Key()->Stop();It.RemoveCurrent();}
 ActionId.Invalidate();Submitted.Reset();SubmittedPulses.Reset();
}
int32 UEGCombatAudioComponent::GetActiveVoiceCount() const
{int32 Count=0;for(const auto& V:Voices)if(IsValid(V)&&V->IsPlaying())++Count;return Count;}
void UEGCombatAudioComponent::Clear()
{SetComponentTickEnabled(false);PendingEntryAudio.Reset();VillVGunLoop.Reset();VillVGunStart.Reset();VillVGunTail.Reset();VillVCannonCharge.Reset();for(const auto& V:Voices)if(IsValid(V)){V->Stop();V->DestroyComponent();}Voices.Reset();ActionVoices.Reset();Submitted.Reset();SubmittedPulses.Reset();Launched.Reset();CommittedImpacts.Reset();PreviousVariants.Reset();ActionId.Invalidate();RoundId.Invalidate();Battle.Reset();}
void UEGCombatAudioComponent::EndPlay(const EEndPlayReason::Type Reason){if(auto* U=Cast<AEGChessUnit>(GetOwner()))U->OnDied.RemoveDynamic(this,&ThisClass::OwnerDied);Clear();Super::EndPlay(Reason);}
