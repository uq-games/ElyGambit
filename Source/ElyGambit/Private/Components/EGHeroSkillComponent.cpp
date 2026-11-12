#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "Components/EGSkillPresentationCoordinator.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Skills/EGCorruptionBossBehavior.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGUltimateRingComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "EGGameplayTags.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Presentation/EGSkillProjectile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Presentation/EGAnimationPlayback.h"
#include "Presentation/EGSkillNotify.h"
#include "Presentation/EGSourceEffect.h"
#include "AbilitySystem/Effects/EGReviveAttackEffect.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGUltimateSignetVisual.h"
namespace
{
 // Particles keep their source lifetime. The authored attack montage has
 // already compressed its clip; multiplying that factor again makes VFX flash.
 float SourceEffectRate(const UAnimMontage* Montage,float MontageRate)
 {
  return MontageRate;
 }
}
UEGHeroSkillComponent::UEGHeroSkillComponent(){PrimaryComponentTick.bCanEverTick=true;}
void UEGHeroSkillComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(DeltaTime,TickType,TickFunction);
 if(TickType==LEVELTICK_PauseTick||(GetWorld()&&GetWorld()->IsPaused()))
 {
  // Only the pause-owning entry montage advances; do not advance shields,
  // status timers, combat behaviors, or another unit's action while paused.
  if(Battle.IsValid()&&Unit.IsValid()&&bActing&&Battle->IsHumanEntryPauseOwner(Unit.Get(),ActionId))
  {
   for(const auto& Effect:ActionEffects)if(Effect.IsValid())Battle->PermitHumanEntryVisualTick(Effect.Get(),ActionRound,ActionId);
   auto* Mesh=Unit->GetVisiblePresentationMesh();auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
   if(ActiveMontage&&Instance&&Presentation)Presentation->TickAction(Instance->Montage_GetPosition(ActiveMontage),false);
   if(bDeferredHumanEntryFinish&&DeferredHumanEntryRound==ActionRound&&DeferredHumanEntryAction==ActionId)
   {
    const auto* HumanVisual=Unit->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
    if(!HumanVisual||!HumanVisual->IsEntryMoviePendingFor(DeferredHumanEntryRound,DeferredHumanEntryAction))FinishAction(true);
   }
  }
  else if(bDeferredHumanEntryFinish)FinishAction(false);
  return;
 }
 // The pause safety guard may release ownership first (e.g. media timeout).
 // Never complete a stale action as a successful entry after that release.
 if(bDeferredHumanEntryFinish)
 {
  FinishAction(false);
  return;
 }
 if(Battle.IsValid()&&Battle->IsBattleActive()&&Battle->HasPresentationHold())
 {
  if(!bStoryHoldPaused)
  {
   bStoryHoldPaused=true;
   if(GetWorld()){GetWorld()->GetTimerManager().PauseTimer(Watchdog);GetWorld()->GetTimerManager().PauseTimer(TauntTimer);GetWorld()->GetTimerManager().PauseTimer(ReviveTimer);}
   auto* Mesh=Unit.IsValid()?Unit->GetVisiblePresentationMesh():nullptr;
   if(auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr)if(ActiveMontage)Instance->Montage_Pause(ActiveMontage);
  }
  if(BubbleEnd>0.)BubbleEnd+=DeltaTime;
  if(auto* Elysia=Cast<UEGElysiaSkillBehavior>(Behavior))Elysia->SuspendBattleClock(DeltaTime);
  if(auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Behavior))Human->SuspendBattleClock(DeltaTime);
  return;
 }
 if(bStoryHoldPaused)
 {
  bStoryHoldPaused=false;
  if(GetWorld()){GetWorld()->GetTimerManager().UnPauseTimer(Watchdog);GetWorld()->GetTimerManager().UnPauseTimer(TauntTimer);GetWorld()->GetTimerManager().UnPauseTimer(ReviveTimer);}
  auto* Mesh=Unit.IsValid()?Unit->GetVisiblePresentationMesh():nullptr;
  if(auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr)if(ActiveMontage)Instance->Montage_Resume(ActiveMontage);
  const auto Events=MoveTemp(HeldAnimationEvents);HeldAnimationEvents.Reset();
  const bool bSameAction=bActing&&ActionId==HeldAnimationAction&&ActionRound==HeldAnimationRound;
  HeldAnimationAction.Invalidate();HeldAnimationRound.Invalidate();
  if(bSameAction)for(FName Event:Events)AnimationEvent(Event);
 }
 if(Battle.IsValid()&&Battle->IsBattleActive()&&!Battle->IsCombatRunning()&&BubbleEnd>0)BubbleEnd+=DeltaTime;
 UpdateBubblePresentation();
 UpdateTauntMirror();
 if(!Battle.IsValid()||!Battle->IsBattleActive()||Battle->IsCombatRunning())for(auto It=SourceShields.CreateIterator();It;++It){It.Value().Remaining-=DeltaTime;if(It.Value().Remaining<=0)It.RemoveCurrent();}
 RefreshShieldTotal();
 RefreshIdleAssembly(DeltaTime);
 if(Behavior&&Unit.IsValid()&&Unit->HasAuthority()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&Battle.IsValid()&&Battle->IsCombatRunning())Behavior->TickBattle(DeltaTime);
 if(!bActing||!Behavior||!Unit.IsValid()||!Battle.IsValid()||!Battle->IsBattleActive())return;
 UpdateHumanBowRetime();
 auto* Mesh=Unit->GetVisiblePresentationMesh();auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
 if(ActiveMontage&&Instance)
 {
  const float Position=Instance->Montage_GetPosition(ActiveMontage);Behavior->TickAction(Position);
  if(Presentation)Presentation->TickAction(Position,Behavior->OverridesBodyPresentation());
 }
}
void UEGHeroSkillComponent::UpdateHumanBowRetime()
{
 if(HumanBowRetime!=EHumanBowRetime::Preparing||!bActing||bUltimate||ActionId!=HumanBowRetimeAction||!Unit.IsValid()||!ActiveMontage)return;
 auto* Mesh=Unit->GetVisiblePresentationMesh();auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
 if(!Instance||!Instance->Montage_IsPlaying(ActiveMontage))return;
 const float Position=Instance->Montage_GetPosition(ActiveMontage);
 if(!FMath::IsFinite(Position)||Position<HumanBowSwitchPosition)return;
 // The preceding frame may have crossed the phase boundary at the faster
 // preparation rate. Re-express only that overshoot at the intended native
 // rate; never rewind over a notify or across the already-fired Hit.
 if(Position<HumanBowHitPosition&&HumanBowPrepareRate>HumanBowAimRate&&HumanBowAimRate>0.f)
 {
  const float Corrected=HumanBowSwitchPosition+(Position-HumanBowSwitchPosition)*HumanBowAimRate/HumanBowPrepareRate;
  bool bCrossesNotify=false;
  for(const FAnimNotifyEvent& Event:ActiveMontage->Notifies)
  {
   const float Time=Event.GetTriggerTime();
   if(Time>Corrected&&Time<=Position){bCrossesNotify=true;break;}
  }
  if(!bCrossesNotify&&Corrected<Position)Instance->Montage_SetPosition(ActiveMontage,Corrected);
 }
 HumanBowRetime=EHumanBowRetime::Aiming;
 Unit->SetPresentationRate(HumanBowAimRate);
}
void UEGHeroSkillComponent::BeginHumanBowRecovery(FGuid ExpectedAction)
{
 if((HumanBowRetime!=EHumanBowRetime::Preparing&&HumanBowRetime!=EHumanBowRetime::Aiming)||
  !ExpectedAction.IsValid()||HumanBowRetimeAction!=ExpectedAction||!bActing||ActionId!=ExpectedAction||!Unit.IsValid())return;
 HumanBowRetime=EHumanBowRetime::Recovery;
 Unit->SetPresentationRate(HumanBowRecoveryRate);
}
void UEGHeroSkillComponent::StartActionAssembly(){if(Presentation)Presentation->StartActionAssembly();}
void UEGHeroSkillComponent::ClearActionAssembly(){if(Presentation)Presentation->ClearActionAssembly();}
bool UEGHeroSkillComponent::BeginBarrierTransition(UEGSourcePreviewDefinition* Definition,FGuid Round,FGuid Token,float& OutDuration)
{OutDuration=0.f;return Presentation&&Presentation->BeginBarrierTransition(Definition,Round,Token,OutDuration);}
bool UEGHeroSkillComponent::SampleBarrierTransition(FGuid Round,FGuid Token,float Position)
{return Presentation&&Presentation->SampleBarrierTransition(Round,Token,Position);}
void UEGHeroSkillComponent::EndBarrierTransition(FGuid Round,FGuid Token)
{if(Presentation)Presentation->EndBarrierTransition(Round,Token);}
bool UEGHeroSkillComponent::IsBarrierTransitionActive(FGuid Round,FGuid Token) const
{return Presentation&&Presentation->IsBarrierTransitionActive(Round,Token);}
UAnimSequenceBase* UEGHeroSkillComponent::GetActivePresentationAsset() const
{return ActiveMontage?static_cast<UAnimSequenceBase*>(ActiveMontage):ActiveAnimation.Get();}
void UEGHeroSkillComponent::BeginPlay()
{Super::BeginPlay();Unit=Cast<AEGChessUnit>(GetOwner());if(Unit.IsValid())Unit->OnAttributeChanged.AddUniqueDynamic(this,&ThisClass::ActionAttributeChanged);}
void UEGHeroSkillComponent::EndPlay(const EEndPlayReason::Type R)
{if(Unit.IsValid())Unit->OnAttributeChanged.RemoveDynamic(this,&ThisClass::ActionAttributeChanged);EndBattle();ClearImpactEffects();if(Behavior){Behavior->EndBattle();Behavior=nullptr;}Battle.Reset();OnStatusChanged.Clear();Super::EndPlay(R);}
void UEGHeroSkillComponent::ActionAttributeChanged(FName Attribute,float OldValue,float NewValue)
{
 auto* U=Unit.Get();
 if(Attribute!=TEXT("AttackSpeed")||!bActing||bUltimate||!U||!U->GetDefinition()||U->GetDefinition()->HeroKind!=EEGHeroKind::Pardofelis)return;
 if(!FMath::IsFinite(NewValue)||NewValue<=0){CancelAction();return;}
 const float Rate=FMath::Clamp(NewValue,.1f,3.f);
 U->SetPresentationRate(Rate);
 for(const auto& Effect:ActionEffects)if(Effect.IsValid())Effect->SetPlaybackRate(SourceEffectRate(ActiveMontage,Rate));
 const auto* Asset=GetActivePresentationAsset();auto* Mesh=U->GetVisiblePresentationMesh();auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
 const float Position=ActiveMontage&&Instance?Instance->Montage_GetPosition(ActiveMontage):0.f;
 if(Asset)GetWorld()->GetTimerManager().SetTimer(Watchdog,this,&ThisClass::Finish,FMath::Max(.01f,(Asset->GetPlayLength()-Position)/Rate)+.2f,false);
}
void UEGHeroSkillComponent::Configure(UEGBattleContextComponent* C,int32 L,bool A)
{
 if(!Presentation){Presentation=NewObject<UEGSkillPresentationCoordinator>(this);Presentation->Initialize(this);}
 DeathPresentationIndex=0;bHasDeathPresentationSnapshot=false;
 Unit=Cast<AEGChessUnit>(GetOwner());CancelAction();if(Presentation)Presentation->ResetTickOrdering();ClearImpactEffects();ClearProtection();
 ClearTaunt();
 if(Behavior){Behavior->EndBattle();Behavior=nullptr;}
 const FGuid NewRound=C?C->GetRoundId():FGuid();
 if(NewRound!=RevivalRound){ClearRevival();bReviveSpent=false;RevivalRound=NewRound;}
 Battle=C;Level=L;bAwakened=A;
 if(UClass* Class=Unit->GetDefinition()->GetBehaviorClass()){Behavior=NewObject<UEGHeroSkillBehavior>(this,Class);Behavior->Initialize(this,C);}
 if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->Configure(C);
 Battle=C;Level=L;bAwakened=A;bInFlight=false;AttackSequenceCursor=0;if(Unit.IsValid()){auto* ASC=Unit->GetAbilitySystemComponent();ASC->RemoveActiveGameplayEffectBySourceEffect(UEGPaintAttackEffect::StaticClass(),nullptr);ASC->RemoveActiveGameplayEffectBySourceEffect(UEGHuaArmorEffect::StaticClass(),nullptr);ASC->RemoveActiveGameplayEffectBySourceEffect(UEGMeteorStunEffect::StaticClass(),nullptr);}SourceShields.Reset();SetShield(0);
 if(Behavior)Behavior->OnBattleConfigured();OnStatusChanged.Broadcast();
}
bool UEGHeroSkillComponent::ConfigureCorruptionBoss()
{
 if(!Unit.IsValid()||!Unit->HasAuthority()||!Battle.IsValid()||!Battle->IsBattleActive()||!Unit->GetDefinition()||Unit->GetDefinition()->HeroKind!=EEGHeroKind::Elysia)return false;
 CancelAction();if(Behavior)Behavior->EndBattle();
 Behavior=NewObject<UEGCorruptionBossBehavior>(this);Behavior->Initialize(this,Battle.Get());Behavior->OnBattleConfigured();
 return true;
}
bool UEGHeroSkillComponent::IsTargetable() const
{
 if(Unit.IsValid()&&Unit->GetCombat()->IsBoardLandingPending())return false;
 if(Unit.IsValid()&&Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::VillV)
 {
  if(IEGCombatant::Execute_GetFaction(Unit.Get())==EEGFaction::Player)return false;
  if(const auto* Mechanism=Unit->FindComponentByClass<UEGVillVMechanismComponent>();Mechanism&&Mechanism->HasActiveDevices())return false;
 }
 return !bInFlight&&!bActionProtected&&!bReviving;
}
bool UEGHeroSkillComponent::BlocksManaGain() const {return !bRestoringStigmataMana&&Behavior&&Behavior->BlocksManaGain();}
bool UEGHeroSkillComponent::IsControlImmune() const {return Behavior&&Behavior->IsControlImmune();}
void UEGHeroSkillComponent::ClearControlForImmunity()
{if(IsControlImmune()){ClearTaunt();if(Unit.IsValid())Unit->GetAbilitySystemComponent()->RemoveActiveGameplayEffectBySourceEffect(UEGMeteorStunEffect::StaticClass(),nullptr);OnStatusChanged.Broadcast();}}
void UEGHeroSkillComponent::SetActionProtected(bool Value)
{bActionProtected=Value;if(Unit.IsValid())Unit->RefreshBoardCollision();}
bool UEGHeroSkillComponent::TryPreventDeath()
{return bReviving||(Behavior&&Behavior->TryPreventDeath());}
bool UEGHeroSkillComponent::BeginRevive(float Delay,float HealthRatio,float AttackBonus)
{
 auto* U=Unit.Get();
 if(!U||!U->HasAuthority()||bReviveSpent||!Battle.IsValid()||!Battle->IsBattleActive()||!FMath::IsFinite(Delay)||Delay<=0)return false;
 bReviveSpent=true;DiscardDeferredStigmataMana();CancelAction();bReviving=true;U->SetRuntimeState(EEGUnitState::Reviving);
 U->GetAbilitySystemComponent()->CancelAllAbilities();
 const FGuid Round=Battle->GetRoundId();TWeakObjectPtr<UEGHeroSkillComponent> Weak(this);
 GetWorld()->GetTimerManager().SetTimer(ReviveTimer,[Weak,Round,HealthRatio,AttackBonus]()
 {
  auto* S=Weak.Get();if(!S||!S->bReviving||!S->Unit.IsValid()||!S->Battle.IsValid()||!S->Battle->IsBattleActive()||S->Battle->GetRoundId()!=Round)return;
  auto* Owner=S->Unit.Get();auto* ASC=Owner->GetAbilitySystemComponent();
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Owner->GetAttributes()->GetMaxHealth()*FMath::Clamp(HealthRatio,.01f,1.f));
  if(AttackBonus>0&&!S->ReviveAttackEffect.IsValid())
  {
   auto Spec=ASC->MakeOutgoingSpec(UEGReviveAttackEffect::StaticClass(),1,ASC->MakeEffectContext());
   if(Spec.IsValid()){Spec.Data->SetSetByCallerMagnitude(FName(TEXT("ReviveAttackMultiplier")),1+AttackBonus);S->ReviveAttackEffect=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
  }
  Owner->GetCombat()->CompleteOrDeferBoardLanding(Owner->GetActorLocation(),Round,[Weak,Round]()
  {
   auto* Skill=Weak.Get();
   if(!Skill||!Skill->bReviving||!Skill->Unit.IsValid()||!Skill->Battle.IsValid()||!Skill->Battle->IsBattleActive()||Skill->Battle->GetRoundId()!=Round)return;
   auto* Revived=Skill->Unit.Get();Skill->bReviving=false;Revived->SetRuntimeState(EEGUnitState::Fighting);
   Revived->PresentRevive();Skill->OnStatusChanged.Broadcast();
   if(auto* Audio=Revived->FindComponentByClass<UEGCombatAudioComponent>())
    Audio->CommittedImpact(TEXT("Revive"),Round,FGuid::NewGuid(),false,INDEX_NONE,0,Revived->GetActorLocation());
   AEGChessNiagaraBurst::Play(Skill,TEXT("Revive"),Revived->GetActorLocation()-FVector(0,0,88),Revived->GetActorRotation());
  });
 },Delay,false);
 OnStatusChanged.Broadcast();return true;
}
void UEGHeroSkillComponent::ClearRevival()
{
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(ReviveTimer);
 bReviving=false;
 if(Unit.IsValid()&&ReviveAttackEffect.IsValid())Unit->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(ReviveAttackEffect);
 ReviveAttackEffect.Invalidate();
}

void UEGHeroSkillComponent::RefreshSpeed()
{
 auto* U=Unit.Get();if(!U||!U->GetDefinition())return;
 float Bonus=0.f;
 if(Battle.IsValid()&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)
 {
  Bonus+=Battle->TeamSpeed();

 }
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),FMath::Min(3.f,(U->GetDefinition()->HeroKind==EEGHeroKind::Generic?U->GetDefinition()->EvaluateStats(Level).AttackSpeed:U->GetDefinition()->GetBaseStats().AttackSpeed)*(1+Bonus)));
}
void UEGHeroSkillComponent::ApplyPersistentLevel(int32 NewLevel)
{
 auto* U=Unit.Get();if(!U||!U->GetDefinition())return;Level=NewLevel;
 const bool Alive=IEGCombatant::Execute_IsAlive(U);const float Ratio=U->GetAttributes()->GetHealth()/FMath::Max(1.f,U->GetAttributes()->GetMaxHealth());
 const auto Stats=U->GetDefinition()->EvaluateStats(Level);auto* ASC=U->GetAbilitySystemComponent();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),Stats.MaxHealth);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),Stats.AttackPower);
 if(Alive)ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),U->GetAttributes()->GetMaxHealth()*Ratio);
 RefreshSpeed();
 OnStatusChanged.Broadcast();
}
void UEGHeroSkillComponent::SetShield(float Value)
{
 Value=FMath::IsFinite(Value)?FMath::Max(0.f,Value):0.f;
 double Total=Value;for(const auto& Pair:SourceShields)Total+=Pair.Value.Value;
 if(Total>TNumericLimits<float>::Max())return;
 LegacyShield=Value;RefreshShieldTotal();
}
void UEGHeroSkillComponent::RefreshShieldTotal()
{
 double Sum=LegacyShield;for(const auto& Pair:SourceShields)Sum+=Pair.Value.Value;
 const float Total=static_cast<float>(FMath::Min(Sum,static_cast<double>(TNumericLimits<float>::Max())));
 const bool bChanged=Shield!=Total;Shield=Total;
 // The source ledger owns lifetime and absorption; GAS exposes its derived total.
 auto* U=Cast<AEGChessUnit>(GetOwner());
 if(U&&U->HasAuthority()&&U->GetAttributes()->GetShield()!=Total)
  U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetShieldAttribute(),Total);
 if(bChanged)OnStatusChanged.Broadcast();
}
bool UEGHeroSkillComponent::GrantSourceShield(FName Source,float Value,float Duration,AEGChessUnit* ShieldSource,bool bKeepStronger)
{
 if(!GetOwner()->HasAuthority()||!FMath::IsFinite(Value)||!FMath::IsFinite(Duration)||Value<=0||Duration<=0)return false;
 auto* Caster=ShieldSource?ShieldSource:Cast<AEGChessUnit>(GetOwner());
 if(!IsValid(Caster)||!Caster->HasAuthority()||Caster->GetWorld()!=GetWorld())return false;
 Value*=Caster->GetAttributes()->GetShieldMultiplier();
 if(!FMath::IsFinite(Value)||Value<=0.f)return false;
 if(bKeepStronger)Value=FMath::Max(Value,GetSourceShield(Source));
 double Total=LegacyShield+static_cast<double>(Value);
 for(const auto& Pair:SourceShields)if(Pair.Key!=Source)Total+=Pair.Value.Value;
 if(Total>TNumericLimits<float>::Max())return false;
 auto& Entry=SourceShields.FindOrAdd(Source);Entry.Value=Value;Entry.Remaining=Duration;RefreshShieldTotal();
 return true;
}
float UEGHeroSkillComponent::GetSourceShield(FName Source) const
{const auto* Entry=SourceShields.Find(Source);return Entry?Entry->Value:0.f;}
float UEGHeroSkillComponent::AbsorbShield(float Amount)
{
 if(!FMath::IsFinite(Amount)||Amount<=0)return 0;
 float Left=Amount;TArray<FName> Keys;SourceShields.GetKeys(Keys);
 Keys.Sort([this](FName A,FName B){return SourceShields[A].Remaining<SourceShields[B].Remaining;});
 for(FName Key:Keys){auto& Entry=SourceShields[Key];const float Used=FMath::Min(Left,Entry.Value);Entry.Value-=Used;Left-=Used;if(Key==TEXT("StigmataDuskStreet")&&Battle.IsValid())if(auto* Stigmata=Battle->GetStigmata())Stigmata->RecordShieldAbsorbed(TEXT("DuskStreet"),Used);if(Entry.Value<=0)SourceShields.Remove(Key);if(Left<=0)break;}
 const float Used=FMath::Min(Left,LegacyShield);LegacyShield-=Used;Left-=Used;RefreshShieldTotal();return Amount-Left;
}
void UEGHeroSkillComponent::RemoveSourceShield(FName Source)
{if(SourceShields.Remove(Source)>0)RefreshShieldTotal();}
void UEGHeroSkillComponent::RefundUncommittedUltimate()
{
 if(!bActing||!bUltimate||!Unit.IsValid()||SpentMana<=0)return;
 Unit->RestoreMana(SpentMana);SpentMana=0;
}
void UEGHeroSkillComponent::RestoreStigmataMana(float Amount)
{
 if(!Unit.IsValid()||!Unit->HasAuthority()||!FMath::IsFinite(Amount)||Amount<=0)return;
 if(Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher&&Behavior&&Behavior->BlocksManaGain())return;
 TGuardValue<bool> Guard(bRestoringStigmataMana,true);
 Unit->RestoreMana(Amount);
}
void UEGHeroSkillComponent::DiscardDeferredStigmataMana()
{if(Battle.IsValid())if(auto* Stigmata=Battle->GetStigmata())Stigmata->DiscardDeferredMana(Unit.Get());}
bool UEGHeroSkillComponent::CommitStigmataUltimate(FGuid Id)
{
 auto* Stigmata=Battle.IsValid()?Battle->GetStigmata():nullptr;
 if(!Stigmata||!Stigmata->NeedsUltimateEvents())
 {
  if(bActing&&bUltimate&&SpentMana>0&&Id==ActionId&&Unit.IsValid()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&Battle.IsValid()&&Battle->IsBattleActive()&&Battle->GetRoundId()==ActionRound)
   Battle->RecordSuccessfulUltimate(Unit.Get(),Id);
  return true;
 }
 if(!bActing||!bUltimate||SpentMana<=0||Id!=ActionId||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get())||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=ActionRound)return false;
 Stigmata->CommitUltimate(Unit.Get(),Id,SpentMana);
 return bActing&&bUltimate&&Id==ActionId&&Unit.IsValid()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&Battle.IsValid()&&Battle->IsBattleActive()&&Battle->GetRoundId()==ActionRound;
}
bool UEGHeroSkillComponent::IsTauntActive() const
{
 return ForcedTarget.IsValid()&&GetWorld()&&GetWorld()->GetTimerManager().TimerExists(TauntTimer)
  &&GetWorld()->GetTimerManager().GetTimerRemaining(TauntTimer)>0.f;
}
void UEGHeroSkillComponent::ClearTaunt()
{
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(TauntTimer);
 ForcedTarget.Reset();TauntEnd=0;
}
void UEGHeroSkillComponent::ClearTauntIfSource(AEGChessUnit* Source)
{if(ForcedTarget.Get()==Source){ClearTaunt();OnStatusChanged.Broadcast();}}
void UEGHeroSkillComponent::UpdateTauntMirror()
{
 if(!ForcedTarget.IsValid())
 {
  if(GetWorld()&&GetWorld()->GetTimerManager().TimerExists(TauntTimer)){ClearTaunt();OnStatusChanged.Broadcast();}
  return;
 }
 if(IsTauntActive())TauntEnd=GetWorld()->GetTimeSeconds()+GetWorld()->GetTimerManager().GetTimerRemaining(TauntTimer);
}
void UEGHeroSkillComponent::SetTaunt(AEGChessUnit* Source,float Duration)
{
 if(IsControlImmune()||!Battle.IsValid()||!Battle->IsBattleActive()||!IsValid(Source)||!FMath::IsFinite(Duration)||Duration<=0||!GetWorld())return;
 ForcedTarget=Source;
 const FGuid Round=Battle->GetRoundId();TWeakObjectPtr<UEGHeroSkillComponent> Weak(this);
 GetWorld()->GetTimerManager().SetTimer(TauntTimer,[Weak,Round]()
 {if(Weak.IsValid()&&Weak->Battle.IsValid()&&Weak->Battle->GetRoundId()==Round){Weak->ClearTaunt();Weak->OnStatusChanged.Broadcast();}},Duration,false);
 UpdateTauntMirror();OnStatusChanged.Broadcast();
}
bool UEGHeroSkillComponent::BeginAction(bool Ultimate)
{
 auto* U=Unit.Get();if(!U)U=Cast<AEGChessUnit>(GetOwner());Unit=U;
 if(!U||!U->HasAuthority()||U->GetAbilitySystemComponent()->HasMatchingGameplayTag(EGTags::ControlStun)||!Battle.IsValid()||!Battle->IsCombatRunning()||IsBusy()||U->GetCombat()->HasExternalActionBlock()||!IEGCombatant::Execute_IsAlive(U))return false;
 const auto* D=U->GetDefinition();if(!D||(D->HeroKind==EEGHeroKind::Generic&&!D->MonsterAttack)||(Ultimate&&!D->bCardSkillsEnabled))return false;
 if(Ultimate&&bSummon&&!bAllowSummonUltimate)return false;
 if(Behavior&&!Behavior->AllowsAction(Ultimate))return false;
 auto* T=U->GetCombat()->GetTarget();if(!T)T=U->GetCombat()->FindNearestEnemy();
 const bool Healer=D->MonsterAttack&&D->MonsterAttack->AttackType==EEGMonsterAttackType::Heal;
 if(Healer&&!Ultimate)T=U->GetCombat()->FindHealingTarget();
 if(!Ultimate)if(auto* Devices=U->FindComponentByClass<UEGVillVMechanismComponent>())T=Devices->FiringTarget(T);
 if(!Ultimate&&(!T||!(Healer?U->GetCombat()->IsInHealingRange(T):U->GetCombat()->IsInAttackRange(T))))return false;
 if(Ultimate&&(D->HeroKind==EEGHeroKind::Elysia||D->HeroKind==EEGHeroKind::ElysiaHerrscher||D->HeroKind==EEGHeroKind::GriseoAdult))
 {
  if(!U->GetCombat()->IsValidEnemy(T))T=U->GetCombat()->FindNearestEnemy();
  if(!U->GetCombat()->IsValidEnemy(T))return false;
 }
 if((D->MonsterAttack||D->HeroKind==EEGHeroKind::ElysiaHerrscher)&&T)
 {
  const FVector Direction=(T->GetActorLocation()-U->GetActorLocation()).GetSafeNormal2D();
  if(!Direction.IsNearlyZero())U->SetActorRotation(Direction.Rotation());
  FEGAnimationPlayback::Speed(U->GetVisiblePresentationMesh(),0.f);
 }
 UAnimSequence* Animation=(Ultimate?D->GetSkillAnimation():D->GetAttackAnimation()).LoadSynchronous();
 // Missing/invalid animation cannot silently deal damage from a parallel timer.
 if(!Animation)return false;
 UAnimMontage* Montage=D->GetAnimationClass().IsNull()?nullptr:D->GetPresentationMontage(Ultimate?EGPresentationAction::Skill:EGPresentationAction::Attack);
 int32 BasicIndex=INDEX_NONE,VisualIndex=INDEX_NONE;
 if(!Ultimate&&Montage&&D->bUseSplitConfiguration&&D->PresentationDefinition&&!D->PresentationDefinition->AttackSequence.IsEmpty())
 {
  const auto& Sequence=D->PresentationDefinition->AttackSequence;
  BasicIndex=Behavior?Behavior->PresentationIndex(false,AttackSequenceCursor%Sequence.Num()):AttackSequenceCursor%Sequence.Num();
  VisualIndex=Presentation?Presentation->SelectAttackPresentationIndex(BasicIndex):BasicIndex;
  if(!Sequence.IsValidIndex(BasicIndex)||!Sequence.IsValidIndex(VisualIndex))return false;
  const auto& Entry=Sequence[VisualIndex];
  Animation=Entry.Animation.LoadSynchronous();Montage=Entry.Montage.LoadSynchronous();
  if(!Animation||!Montage||Animation->GetSkeleton()!=Montage->GetSkeleton())return false;
 }
 ActiveUltimateIndex=INDEX_NONE;
 if(Ultimate&&D->PresentationDefinition&&!D->PresentationDefinition->UltimateSequence.IsEmpty())
 {
  ActiveUltimateIndex=Behavior?Behavior->PresentationIndex(true,0):0;
  const auto& Sequence=D->PresentationDefinition->UltimateSequence;
  if(!Sequence.IsValidIndex(ActiveUltimateIndex))return false;
  Animation=Sequence[ActiveUltimateIndex].Animation.LoadSynchronous();Montage=Sequence[ActiveUltimateIndex].Montage.LoadSynchronous();
  if(!Animation||!Montage||Animation->GetSkeleton()!=Montage->GetSkeleton())return false;
 }
 if(!D->GetAnimationClass().IsNull()&&!Montage)return false;
 if(Ultimate&&(U->GetAttributes()->GetMaxMana()<=0||U->GetAttributes()->GetMana()<U->GetAttributes()->GetMaxMana()))return false;
 ActiveAnimation=Animation;ActiveMontage=Montage;
 ActiveBasicAttackIndex=BasicIndex;
 ActiveVisualAttackIndex=VisualIndex;
 bActing=true;bUltimate=Ultimate;Submitted.Reset();ActionId=FGuid::NewGuid();ActionRound=Battle->GetRoundId();ActionTarget=T;
 AttackSnapshot=U->GetAttributes()->GetAttackPower();HealthSnapshot=U->GetAttributes()->GetMaxHealth();
 const int32 Tier=FMath::Clamp((Level-1)/FMath::Max(1,D->GetSkillTierStep()),0,2);ActionTier=Tier;Multiplier=D->GetSkillMultipliers()[Tier];Secondary=D->GetSkillSecondary()[Tier];
 LastPoint=T?T->GetActorLocation():U->GetActorLocation()+U->GetActorForwardVector()*120;
 // Tokens must exist before Montage_Play binds its callbacks. No cost is committed until playback succeeds.
 U->GetAbilitySystemComponent()->AddLooseGameplayTag(EGTags::Acting);
 if(Ultimate)U->GetAbilitySystemComponent()->AddLooseGameplayTag(EGTags::Casting);
 SpentMana=0;
 if(Behavior)Behavior->BeforeActionPresentation();
 StageAssembly=nullptr;
 if(!U->TryPresentAction(T?T:U,Ultimate)){CancelAction();return false;}
 // PlayAction resets its base pose; restore Vill-V's occupied firing pose in
 // the same game frame, before the montage is evaluated by the AnimGraph.
 if(!Ultimate)if(auto* Devices=U->FindComponentByClass<UEGVillVMechanismComponent>())Devices->RestoreAttackBasePose();
 if(auto* Ring=U->FindComponentByClass<UEGUltimateRingComponent>())Ring->Refresh();
 if(Ultimate)
 {
  auto* ASC=U->GetAbilitySystemComponent();SpentMana=U->GetAttributes()->GetMana();
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0);
 }
 if(auto* Stigmata=Battle->GetStigmata())Stigmata->RegisterAction(U,ActionId,T,Ultimate);
 ++ActionCount;
 if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())
  Audio->BeginAction(ActionId,bUltimate,bUltimate&&D->HeroKind==EEGHeroKind::ElysiaHerrscher?ActiveUltimateIndex:ActiveBasicAttackIndex);
 StartActionAssembly();
 if(Ultimate)
 {
  AEGUltimateSignetVisual::Spawn(this);
  AEGChessNiagaraBurst::Play(this,TEXT("UltimateBloom"),U->GetActorLocation()-FVector(0,0,70),U->GetActorRotation());
 }
 if(Behavior)
 {
  FEGSkillActionContext Context;Context.RoundId=ActionRound;Context.ActionId=ActionId;Context.Target=ActionTarget;
  Context.bUltimate=bUltimate;Context.Tier=ActionTier;Context.BasicAttackIndex=ActiveBasicAttackIndex;
  Context.Attack=AttackSnapshot;Context.MaxHealth=HealthSnapshot;Context.Multiplier=Multiplier;Context.Secondary=Secondary;
  if(Ultimate)if(auto* Corruption=U->FindComponentByClass<UEGCorruptionCombatComponent>())Corruption->RememberUltimate(Context.ActionId);
  Behavior->ActionStarted(Context);
 }
 if(!Ultimate)AttackSequenceCursor=(AttackSequenceCursor+1)%MAX_int32;
 // These montages own their stroke and recovery timing. Attack speed
 // scales the authored cycle only once.
 const bool bAuthoredBasicCycle=D->HeroKind==EEGHeroKind::Pardofelis||D->HeroKind==EEGHeroKind::VillV||D->HeroKind==EEGHeroKind::Elysia||D->HeroKind==EEGHeroKind::ElysiaHerrscher;
 float TimingScale=bAuthoredBasicCycle?1.f:Animation->GetPlayLength();
 if(!Ultimate&&D->HeroKind==EEGHeroKind::ElysiaHerrscher&&Montage)
 {
  const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Behavior);
  if(Human&&Human->IsOriginActive())
  {
   // Origin keeps its authored motion timing instead of compressing every
   // multi-second staff or wave animation into a single attack interval.
   const auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(D->SkillDefinition);
   TimingScale=Rules&&FMath::IsFinite(Rules->OriginActionPlaybackRate)&&Rules->OriginActionPlaybackRate>0.f
    ?Rules->OriginActionPlaybackRate:1.f;
  }
  else
  {
   // Bow attacks retain their configured one-interval release cadence.
   for(const auto& N:Montage->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify);Notify&&Notify->Event==TEXT("Finish")&&N.GetTime()>0.f)
   {TimingScale=N.GetTime();break;}
  }
 }
 const float UltimateRate=D->bUseSplitConfiguration&&D->PresentationDefinition?D->PresentationDefinition->UltimatePlaybackRate:1.f;
 const float CycleScale=(D->HeroKind==EEGHeroKind::VillV||D->HeroKind==EEGHeroKind::Elysia||D->HeroKind==EEGHeroKind::ElysiaHerrscher)?1.f:(Behavior?FMath::Max(.1f,Behavior->BasicCycleScale()):1.f);
 const float Rate=Ultimate?FMath::Clamp(UltimateRate,.1f,4.f):FMath::Max(.1f,U->GetAttributes()->GetAttackSpeed()*TimingScale/CycleScale);
 float InitialRate=Rate;
 HumanBowRetime=EHumanBowRetime::None;HumanBowRetimeAction.Invalidate();
 const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Behavior);
 if(!Ultimate&&D->HeroKind==EEGHeroKind::ElysiaHerrscher&&Montage&&Human&&!Human->IsOriginActive())
 {
  float Hit=-1.f,Finish=-1.f;
  for(const auto& N:Montage->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify))
  {if(Notify->Event==TEXT("Hit")&&Hit<0.f)Hit=N.GetTime();if(Notify->Event==TEXT("Finish")&&Finish<0.f)Finish=N.GetTime();}
  const float AssetRate=Montage->RateScale,Speed=FMath::Max(.1f,U->GetAttributes()->GetAttackSpeed());
  const float OldEffectiveRate=Rate*AssetRate;
  if(FMath::IsFinite(Hit)&&FMath::IsFinite(Finish)&&FMath::IsFinite(AssetRate)&&Hit>0.f&&Finish>Hit&&AssetRate>.01f&&OldEffectiveRate>.01f)
  {
   const float OldHitSeconds=Hit/OldEffectiveRate;
   const float PrepareSeconds=FMath::Min(.1f/Speed,OldHitSeconds*.25f);
   const float AimWindow=OldHitSeconds-PrepareSeconds;
   const float Switch=Hit-AimWindow*Speed;
   const float PrepareRate=Switch/(PrepareSeconds*AssetRate),AimRate=Speed/AssetRate;
   if(FMath::IsFinite(PrepareRate)&&FMath::IsFinite(AimRate)&&Switch>0.f&&Switch<Hit&&AimWindow>.01f&&PrepareRate>AimRate&&PrepareRate<=40.f)
   {
    HumanBowRetime=EHumanBowRetime::Preparing;HumanBowRetimeAction=ActionId;
    HumanBowSwitchPosition=Switch;HumanBowHitPosition=Hit;HumanBowPrepareRate=PrepareRate;
    HumanBowAimRate=AimRate;HumanBowRecoveryRate=Rate;InitialRate=PrepareRate;
   }
  }
 }
 U->SetPresentationRate(InitialRate);
 // Runtime montages may intentionally trim source recovery/posing frames.
 GetWorld()->GetTimerManager().SetTimer(Watchdog,this,&ThisClass::Finish,GetActivePresentationAsset()->GetPlayLength()/Rate+.2f,false);
 return true;
}
void UEGHeroSkillComponent::AnimationEvent(FName Event)
{
 if(!bActing||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=ActionRound||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 if(Battle->HasPresentationHold())
 {
  if(HeldAnimationAction!=ActionId||HeldAnimationRound!=ActionRound){HeldAnimationEvents.Reset();HeldAnimationAction=ActionId;HeldAnimationRound=ActionRound;}
  HeldAnimationEvents.Add(Event);return;
 }
 if(Event.ToString().StartsWith(TEXT("Audio_")))
 {if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent(FName(*Event.ToString().RightChop(6)),LastPoint);return;}
 if(Event==TEXT("Finish"))
 {
  const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Behavior);
  const auto* HumanVisual=Unit->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
  if(bUltimate&&Human&&HumanVisual&&HumanVisual->IsEntryMoviePendingFor(ActionRound,ActionId)&&
   (Human->GetPresentationPhase()==EEGElysiaHerrscherPresentationPhase::Entering||
    Human->GetPresentationPhase()==EEGElysiaHerrscherPresentationPhase::EntryReleased)&&
   Battle->IsHumanEntryPauseOwner(Unit.Get(),ActionId))
  {
   bDeferredHumanEntryFinish=true;DeferredHumanEntryRound=ActionRound;DeferredHumanEntryAction=ActionId;
   return;
  }
  FinishAction(true);return;
 }
 if(Event==TEXT("HitSecond")&&(bUltimate||ActiveBasicAttackIndex!=2||Unit->GetDefinition()->HeroKind!=EEGHeroKind::Pardofelis))return;
 if(Submitted.Contains(Event))return;
 const bool bSakuraFanVisual=bUltimate&&Unit->GetDefinition()->HeroKind==EEGHeroKind::Sakura&&Event==TEXT("FanStart");
 Submitted.Add(Event);if(!bSakuraFanVisual)++CommitCount;Submit(Event);
}
void UEGHeroSkillComponent::Submit(FName Event)
{
 auto* U=Unit.Get();auto* D=U->GetDefinition();
 if(bUltimate&&!D->bCardSkillsEnabled)return;
 const bool bSakuraFanVisual=bUltimate&&D->HeroKind==EEGHeroKind::Sakura&&Event==TEXT("FanStart");
 if(!bSakuraFanVisual&&!(D->GetCombatAudioPresentation()&&D->GetCombatAudioPresentation()->bUseEventAudio))
  if(auto* Sound=(bUltimate?D->GetSkillSound():D->GetAttackSound()).LoadSynchronous())UGameplayStatics::PlaySoundAtLocation(this,Sound,U->GetActorLocation(),.3f);
 if(Behavior)
 {
  FEGSkillActionContext Context;Context.RoundId=ActionRound;Context.ActionId=ActionId;Context.Target=ActionTarget;
  Context.bUltimate=bUltimate;Context.Tier=ActionTier;
  Context.BasicAttackIndex=ActiveBasicAttackIndex;
  Context.Attack=AttackSnapshot;Context.MaxHealth=HealthSnapshot;Context.Multiplier=Multiplier;Context.Secondary=Secondary;
  Behavior->ActionEvent(Event,Context);
  if(Event==TEXT("Hit"))BeginHumanBowRecovery(Context.ActionId);
  if(IsValid(U)&&bActing&&bUltimate&&SpentMana>0&&Event==TEXT("Release")&&Battle.IsValid()&&Battle->IsBattleActive()&&Battle->GetRoundId()==Context.RoundId)
  {
   if(auto* Corruption=U->FindComponentByClass<UEGCorruptionCombatComponent>())Corruption->UltimateCommitted(Context.ActionId);
  }
  return;
 }
 // Unsupported definitions cannot submit gameplay events without a behavior.
}

void UEGHeroSkillComponent::Finish()
{FinishAction(false);}
void UEGHeroSkillComponent::FinishAction(bool bCompleted)
{
 HeldAnimationEvents.Reset();HeldAnimationAction.Invalidate();HeldAnimationRound.Invalidate();
 HumanBowRetime=EHumanBowRetime::None;HumanBowRetimeAction.Invalidate();
 bDeferredHumanEntryFinish=false;DeferredHumanEntryRound.Invalidate();DeferredHumanEntryAction.Invalidate();
 if(Unit.IsValid())if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->EndAction(bCompleted);
 if(bActing&&Behavior)Behavior->ActionEnded(bCompleted);
 if(bActing&&Presentation)Presentation->FinishActionPresentation(bCompleted);
 ClearActionAssembly();
 SetActionProtected(false);
 for(const auto& Effect:ActionEffects)if(Effect.IsValid())Effect->Destroy();
 ActionEffects.Reset();PlayedEffects.Reset();
 if(bActing&&Unit.IsValid())
 {
  auto* ASC=Unit->GetAbilitySystemComponent();ASC->RemoveLooseGameplayTag(EGTags::Acting);
  if(bUltimate)ASC->RemoveLooseGameplayTag(EGTags::Casting);
 }
 bActing=false;bUltimate=false;StageAssembly=nullptr;ActiveAnimation=nullptr;ActiveMontage=nullptr;ActionTarget.Reset();
 if(Unit.IsValid())if(auto* Ring=Unit->FindComponentByClass<UEGUltimateRingComponent>())Ring->Refresh();
 ActiveBasicAttackIndex=INDEX_NONE;
 ActiveVisualAttackIndex=INDEX_NONE;
 ActionRound.Invalidate();ActionId.Invalidate();Submitted.Reset();
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(Watchdog);
 if(Unit.IsValid())if(auto* Devices=Unit->FindComponentByClass<UEGVillVMechanismComponent>())Devices->RestoreOperatingPresentation();
}
void UEGHeroSkillComponent::PresentSourceEffect(UEGSourceEffectDefinition* Effect,USkeletalMeshComponent* Mesh)
{
 auto* U=Unit.Get();
 if(!IsValid(Effect)||!U||!U->GetDefinition()||U->GetDefinition()->HeroKind==EEGHeroKind::VillV||!U->HasAuthority()||!bActing||!ActionId.IsValid()
    ||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=ActionRound
    ||!IEGCombatant::Execute_IsAlive(U)||Mesh!=U->GetVisiblePresentationMesh()||PlayedEffects.Contains(Effect))return;
 const float Rate=ActiveMontage&&Mesh->GetAnimInstance()?Mesh->GetAnimInstance()->Montage_GetPlayRate(ActiveMontage):1.f;
 if(auto* Visual=AEGSourceEffect::Spawn(this,Mesh,Effect,SourceEffectRate(ActiveMontage,Rate)))
 {PlayedEffects.Add(Effect);ActionEffects.Add(Visual);if(Battle->IsHumanEntryPauseOwner(U,ActionId))Battle->PermitHumanEntryVisualTick(Visual,ActionRound,ActionId);}
}
AEGSourceEffect* UEGHeroSkillComponent::PresentSourceEffectAt(UEGSourceEffectDefinition* Effect,const FVector& Point,const FRotator& Rotation,float ScaleMultiplier)
{
 auto* U=Unit.Get();auto* Mesh=U?U->GetVisiblePresentationMesh():nullptr;
 if(!IsValid(Effect)||!U||!U->HasAuthority()||!IsCasting()||!ActionId.IsValid()
    ||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=ActionRound
    ||!IEGCombatant::Execute_IsAlive(U)||!IsValid(Mesh)||PlayedEffects.Contains(Effect))return nullptr;
 const float Rate=ActiveMontage&&Mesh->GetAnimInstance()?Mesh->GetAnimInstance()->Montage_GetPlayRate(ActiveMontage):1.f;
 auto* Visual=AEGSourceEffect::SpawnAt(this,Mesh,Effect,SourceEffectRate(ActiveMontage,Rate),Point,Rotation,ScaleMultiplier);
 if(Visual){PlayedEffects.Add(Effect);ActionEffects.Add(Visual);if(Battle->IsHumanEntryPauseOwner(U,ActionId))Battle->PermitHumanEntryVisualTick(Visual,ActionRound,ActionId);}
 return Visual;
}
FVector UEGHeroSkillComponent::GetPresentationTargetGround(float HeightFraction) const
{
 const auto* Target=ActionTarget.Get();
 const auto* D=Unit.IsValid()?Unit->GetDefinition():nullptr;
 const bool Locked=bUltimate&&D&&(D->HeroKind==EEGHeroKind::Elysia||D->HeroKind==EEGHeroKind::GriseoAdult);
 return (Locked?LastPoint:IsValid(Target)?Target->GetActorLocation():LastPoint)-FVector(0,0,88)+
  FVector(0,0,IsValid(Target)&&Target->GetDefinition()?Target->GetDefinition()->GetPresentationHeight()*HeightFraction:0.f);
}
void UEGHeroSkillComponent::PresentSummonImpact(FGuid Token,FGuid Round,FVector Ground)
{
 auto* U=Unit.Get();
 // Landing belongs to the already launched projectile/round, even after the
 // summoner has finished the throw and begun another basic attack.
 if(!U||!U->HasAuthority()||!IEGCombatant::Execute_IsAlive(U)||!Token.IsValid()||Ground.ContainsNaN()
    ||!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round||LandedImpactIds.Contains(Token)||!U->GetDefinition()
    ||!U->GetDefinition()->bUseSplitConfiguration||!U->GetDefinition()->PresentationDefinition)return;
 auto* Effect=U->GetDefinition()->PresentationDefinition->SummonImpactEffect.LoadSynchronous();
 if(!Effect||!Effect->bTargetGround)return;
 if(auto* Visual=AEGSourceEffect::Spawn(this,U->GetVisiblePresentationMesh(),Effect,1.f,true))
 {
  Visual->SetActorLocation(Ground);LandedImpactIds.Add(Token);
  ImpactEffects.RemoveAll([](const auto& Entry){return !Entry.IsValid();});ImpactEffects.Add(Visual);
 }
}
void UEGHeroSkillComponent::ClearImpactEffects()
{for(const auto& Effect:ImpactEffects)if(Effect.IsValid())Effect->Destroy();ImpactEffects.Reset();LandedImpactIds.Reset();}
void UEGHeroSkillComponent::CaptureDeathPresentation()
{
 if(bHasDeathPresentationSnapshot)return;
 DeathPresentationIndex=Behavior?FMath::Max(0,Behavior->DeathPresentationIndex()):0;
 bHasDeathPresentationSnapshot=true;
}
void UEGHeroSkillComponent::ResolveDeathPresentation(UAnimSequence*& OutAnimation,UEGSourcePreviewDefinition*& OutAssembly) const
{
 OutAnimation=nullptr;OutAssembly=nullptr;
 const auto* U=Cast<AEGChessUnit>(GetOwner());const auto* D=U?U->GetDefinition():nullptr;if(!D)return;
 OutAnimation=D->GetDeathAnimation().LoadSynchronous();
 const auto* P=D->PresentationDefinition.Get();if(!P)return;
 OutAssembly=P->DeathAssembly.LoadSynchronous();
 if(!bHasDeathPresentationSnapshot||!P->DeathVariants.IsValidIndex(DeathPresentationIndex))return;
 const auto& Variant=P->DeathVariants[DeathPresentationIndex];
 auto* Animation=Variant.Animation.LoadSynchronous();auto* Assembly=Variant.Assembly.LoadSynchronous();
 if(!Animation||!Assembly||Assembly->Motions.IsEmpty()||Assembly->Motions[0].Duration<=0.f)return;
 const auto* Visible=U->GetVisiblePresentationMesh();const auto* Mesh=Visible?Visible->GetSkeletalMeshAsset():nullptr;
 if(!Mesh||!Mesh->GetSkeleton()||Animation->GetSkeleton()!=Mesh->GetSkeleton())return;
 const auto& Parts=Assembly->Motions[0].Parts;
 const bool bMatchingBody=Parts.ContainsByPredicate([Animation,Mesh,&Parts](const FEGSourcePreviewPart& Part)
  {return (Part.PartId==TEXT("Body")||Parts.Num()==1)&&Part.Mesh&&Part.Mesh->GetSkeleton()==Mesh->GetSkeleton()&&Part.Animation==Animation;});
 if(!bMatchingBody)return;
 OutAnimation=Animation;OutAssembly=Assembly;
}
void UEGHeroSkillComponent::CancelAction()
{Finish();ActionId.Invalidate();if(Unit.IsValid())Unit->CancelPresentationAction();}

void UEGHeroSkillComponent::EndBattle()
{CancelAction();if(Unit.IsValid())Unit->GetCombat()->CancelBoardLanding();if(Unit.IsValid())Unit->GetAbilitySystemComponent()->RemoveActiveGameplayEffectBySourceEffect(UEGPaintAttackEffect::StaticClass(),nullptr);if(Unit.IsValid()){auto* ASC=Unit->GetAbilitySystemComponent();ASC->RemoveActiveGameplayEffectBySourceEffect(UEGHuaArmorEffect::StaticClass(),nullptr);ASC->RemoveActiveGameplayEffectBySourceEffect(UEGMeteorStunEffect::StaticClass(),nullptr);}SourceShields.Reset();SetShield(0);if(Unit.IsValid())if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->Clear();if(Presentation)Presentation->ResetTickOrdering();if(Behavior)Behavior->EndBattle();ClearProtection();ClearRevival();if(!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))ClearImpactEffects();ClearTaunt();OnStatusChanged.Broadcast();}

void UEGHeroSkillComponent::ClearIdleAssembly(){if(Presentation)Presentation->ClearIdleAssembly();}
void UEGHeroSkillComponent::PresentIdleHit(){if(Presentation)Presentation->PresentIdleHit();}
void UEGHeroSkillComponent::RefreshIdleAssembly(float DeltaTime){if(Presentation)Presentation->RefreshIdleAssembly(DeltaTime);}
bool UEGHeroSkillComponent::GetAssemblyBoneLocation(FName Part,FName Bone,FVector& Location)
{return Presentation&&Presentation->GetAssemblyBoneLocation(Part,Bone,Location);}
bool UEGHeroSkillComponent::IsHumanBowChargePresentationActive() const
{return Presentation&&Presentation->IsHumanBowChargeStarted();}

bool UEGHeroSkillComponent::IsBusy() const
{return bActing||bInFlight||(Unit.IsValid()&&Unit->GetCombat()->IsBoardLandingPending())||(Behavior&&Behavior->BlocksCombat());}
bool UEGHeroSkillComponent::BeginInternalUltimateStage(int32 SequenceIndex,AEGChessUnit* Target)
{
 auto* U=Unit.Get();
 if(!U||!U->HasAuthority()||!IEGCombatant::Execute_IsAlive(U)||bActing||U->GetCombat()->HasExternalActionBlock()||!Battle.IsValid()||!Battle->IsCombatRunning()||
    !U->GetDefinition()||U->GetDefinition()->HeroKind!=EEGHeroKind::ElysiaHerrscher||!U->GetCombat()->IsValidEnemy(Target))return false;
 const auto* D=U->GetDefinition();const auto* P=D->PresentationDefinition.Get();
 if(!P||!P->UltimateSequence.IsValidIndex(SequenceIndex))return false;
 const auto& Entry=P->UltimateSequence[SequenceIndex];auto* Clip=Entry.Animation.LoadSynchronous();auto* Montage=Entry.Montage.LoadSynchronous();
 if(!Clip||!Montage||Clip->GetSkeleton()!=Montage->GetSkeleton())return false;
 ActiveAnimation=Clip;ActiveMontage=Montage;ActiveBasicAttackIndex=INDEX_NONE;ActiveUltimateIndex=SequenceIndex;
 bActing=true;bUltimate=true;Submitted.Reset();ActionId=FGuid::NewGuid();ActionRound=Battle->GetRoundId();ActionTarget=Target;
 AttackSnapshot=U->GetAttributes()->GetAttackPower();HealthSnapshot=U->GetAttributes()->GetMaxHealth();
 ActionTier=FMath::Clamp((Level-1)/FMath::Max(1,D->GetSkillTierStep()),0,2);Multiplier=D->GetSkillMultipliers()[ActionTier];Secondary=D->GetSkillSecondary()[ActionTier];
 LastPoint=Target->GetActorLocation();SpentMana=0.f;
 U->GetAbilitySystemComponent()->AddLooseGameplayTag(EGTags::Acting);U->GetAbilitySystemComponent()->AddLooseGameplayTag(EGTags::Casting);
 if(Behavior)Behavior->BeforeActionPresentation();StageAssembly=nullptr;
 if(!U->TryPresentAction(Target,true)){CancelAction();return false;}
  if(auto* Stigmata=Battle->GetStigmata())Stigmata->RegisterUltimateContinuation(U,ActionId,Target);
 ++ActionCount;
 if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->BeginAction(ActionId,true,SequenceIndex);
 StartActionAssembly();
 if(Behavior)
 {
  FEGSkillActionContext C;C.RoundId=ActionRound;C.ActionId=ActionId;C.Target=ActionTarget;C.bUltimate=true;C.Tier=ActionTier;
  C.Attack=AttackSnapshot;C.MaxHealth=HealthSnapshot;C.Multiplier=Multiplier;C.Secondary=Secondary;Behavior->ActionStarted(C);
 }
 const float Rate=FMath::Clamp(P->UltimatePlaybackRate,.1f,4.f);U->SetPresentationRate(Rate);
 GetWorld()->GetTimerManager().SetTimer(Watchdog,this,&ThisClass::Finish,Montage->GetPlayLength()/Rate+.2f,false);
 return true;
}
void UEGHeroSkillComponent::SuspendForTraversal()
{
 if(Unit.IsValid())Unit->CancelPresentationAction();
 ClearActionAssembly();ClearIdleAssembly();ActiveAnimation=nullptr;ActiveMontage=nullptr;StageAssembly=nullptr;
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(Watchdog);
}
bool UEGHeroSkillComponent::PlayTraversalStage(const FEGAttackPresentation& Entry,UEGSourcePreviewDefinition* Assembly,float Rate)
{
 if(!Unit.IsValid()||!Unit->HasAuthority()||!Battle.IsValid()||!Battle->IsBattleActive())return false;
 auto* Clip=Entry.Animation.LoadSynchronous();auto* Montage=Entry.Montage.LoadSynchronous();
 if(!Clip||!Montage||Clip->GetSkeleton()!=Montage->GetSkeleton())return false;
 SuspendForTraversal();
 // Playback-instance validation rejects old montage notifications; event-name
 // deduplication is local to this traversal stage, not a previous seat/exit.
 Submitted.Reset();
 if(!bActing)
 {
  bActing=true;bUltimate=false;ActionId=FGuid::NewGuid();ActionRound=Battle->GetRoundId();Submitted.Reset();
  ActiveBasicAttackIndex=INDEX_NONE;Unit->GetAbilitySystemComponent()->AddLooseGameplayTag(EGTags::Acting);
  if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->BeginAction(ActionId,false,INDEX_NONE);
 }
 if(Behavior)Behavior->BeforeActionPresentation();
 ActiveAnimation=Clip;ActiveMontage=Montage;StageAssembly=Assembly;
 if(!Unit->TryPresentAction(Unit.Get(),bUltimate)){CancelAction();return false;}
 Unit->SetPresentationRate(Rate);StartActionAssembly();
 GetWorld()->GetTimerManager().SetTimer(Watchdog,this,&ThisClass::Finish,Montage->GetPlayLength()/Rate+.2f,false);return true;
}
