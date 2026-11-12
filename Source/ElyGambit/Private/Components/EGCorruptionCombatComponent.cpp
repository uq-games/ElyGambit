#include "Components/EGCorruptionCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include "Actor/EGBoard.h"
#include "Actor/EGCorruptionSeed.h"
#include "Actor/EGCorruptionBarrier.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatGeometry.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Game/EGChessGameMode.h"
#include "Data/EGHeroDefinition.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGCorruptionBarrierVisual.h"
#include "Presentation/EGCorruptionPhantomVisual.h"
#include "Presentation/EGCorruptionBossVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGSkillProjectile.h"
#include "Game/EGDemoGameMode.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGRunDefinition.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

namespace
{
 const FName BossPhaseShield(TEXT("CorruptionBoss.PhaseShield"));
 constexpr float BarrierOpenDelay=1.f;
 constexpr float WallPushbackDuration=.4f;
 constexpr float WallClearance=1.f;
 constexpr float BossSpreadWarningSeconds=2.f;
 constexpr float BossMimicWindupSeconds=1.f;
 constexpr float BossMimicRecoverySeconds=.5f;
 float SafeRule(float Value,float Fallback,float Minimum=0.f)
 {return FMath::IsFinite(Value)?FMath::Max(Minimum,Value):Fallback;}
 // A wall plan has a small finite set of already validated landing slots.
 // Minimise total squared travel without changing those slots or relaxing any
 // collision rules. The deterministic assignment is O(N^3), N<=board capacity.
 bool AssignMinimumTravel(const TArray<TArray<double>>& Costs,TArray<int32>& OutSlots)
 {
  const int32 Count=Costs.Num();OutSlots.Init(INDEX_NONE,Count);
  if(Count==0)return true;
  constexpr double Infinite=1.e30;
  TArray<double> U,V;U.Init(0.,Count+1);V.Init(0.,Count+1);
  TArray<int32> Owner,Previous;Owner.Init(0,Count+1);Previous.Init(0,Count+1);
  for(int32 Row=1;Row<=Count;++Row)
  {
   Owner[0]=Row;int32 Column=0;
   TArray<double> Best;Best.Init(Infinite,Count+1);
   TArray<bool> Used;Used.Init(false,Count+1);
   do
   {
    Used[Column]=true;const int32 ActiveRow=Owner[Column];
    double Delta=Infinite;int32 Next=0;
    for(int32 Candidate=1;Candidate<=Count;++Candidate)if(!Used[Candidate])
    {
     const double Reduced=Costs[ActiveRow-1][Candidate-1]-U[ActiveRow]-V[Candidate];
     if(Reduced<Best[Candidate]){Best[Candidate]=Reduced;Previous[Candidate]=Column;}
     if(Best[Candidate]<Delta){Delta=Best[Candidate];Next=Candidate;}
    }
    if(Next==0||Delta>=Infinite*.5)return false;
    for(int32 J=0;J<=Count;++J)
     if(Used[J]){U[Owner[J]]+=Delta;V[J]-=Delta;}else Best[J]-=Delta;
    Column=Next;
   }while(Owner[Column]!=0);
   do
   {
    const int32 Prior=Previous[Column];Owner[Column]=Owner[Prior];Column=Prior;
   }while(Column!=0);
  }
  for(int32 Column=1;Column<=Count;++Column)if(Owner[Column]>0)OutSlots[Owner[Column]-1]=Column-1;
  for(int32 Slot:OutSlots)if(Slot==INDEX_NONE)return false;
  return true;
 }
 EEGHeroKind BaseMimicKind(EEGHeroKind Kind)
 {
  switch(Kind)
  {
  case EEGHeroKind::CollapsedHua:return EEGHeroKind::Hua;
  case EEGHeroKind::GriseoAdult:return EEGHeroKind::Griseo;
  default:return Kind;
  }
 }
}

UEGCorruptionCombatComponent::UEGCorruptionCombatComponent()
{
 PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bTickEvenWhenPaused=true;
 BarrierIntroAssembly=TSoftObjectPtr<UEGSourcePreviewDefinition>(FSoftObjectPath(TEXT("/Game/Assets/Characters/ElysiaCorruption/Combat20261101/Animations/SourceWall20261108/DA_Corruption_SummonWall_Assembly.DA_Corruption_SummonWall_Assembly")));
}
bool UEGCorruptionCombatComponent::IsCurrentBattle() const
{return Battle.IsValid()&&Battle->IsBattleActive()&&RoundId.IsValid()&&Battle->GetRoundId()==RoundId;}
bool UEGCorruptionCombatComponent::IsBarrierIntroPresentationActive() const
{
 const auto* Unit=Cast<AEGChessUnit>(GetOwner());
 return bBarrierIntroUsesSourcePose&&Unit&&Unit->GetHeroSkill()&&
  Unit->GetHeroSkill()->IsBarrierTransitionActive(RoundId,BarrierBlockToken);
}
#if WITH_DEV_AUTOMATION_TESTS
void UEGCorruptionCombatComponent::CompleteBarrierIntroForTests()
{
 if(!Battle.IsValid()||!Battle->CanAdvancePresentationVisualsForToken(BarrierBlockToken))return;
 // Advance the real implementation in small samples: jumping straight to the
 // last frame artificially forces every obstructed actor into the fallback.
 constexpr double Step=.025;
 const int32 MaxSteps=FMath::Clamp(FMath::CeilToInt(BarrierIntroDuration/float(Step))+2,2,2048);
 for(int32 I=0;I<MaxSteps&&BarrierIntroRemaining>0.f&&IsCurrentBattle();++I)
 {
  BarrierIntroStartReal-=Step;
  TickComponent(0.f,LEVELTICK_All,nullptr);
 }
}
#endif
UEGCorruptionCombatComponent* UEGCorruptionCombatComponent::AttachCopy(AEGChessUnit* Unit,UEGBattleContextComponent* Context,EEGCorruptionRole Role)
{
 if(!IsValid(Unit)||!Unit->HasAuthority()||!IsValid(Context)||!Context->IsBattleActive())return nullptr;
 auto* Component=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
 if(!Component){Component=NewObject<UEGCorruptionCombatComponent>(Unit);Unit->AddInstanceComponent(Component);Component->RegisterComponent();}
 Component->Battle=Context;Component->RoundId=Context->GetRoundId();Component->CopyRole=Role;
 Component->bCopy=true;Component->bBoss=false;Component->FallbackAttackClock=0.f;Component->UltimateActions.Reset();Component->UltimateActionOrder.Reset();Component->LastCommittedAction.Invalidate();
 return Component;
}
UEGCorruptionCombatComponent* UEGCorruptionCombatComponent::AttachBoss(AEGChessUnit* Unit,UEGBattleContextComponent* Context,float Health,float Attack,int32 Corruption)
{
 if(!IsValid(Unit)||!Unit->HasAuthority()||!IsValid(Context)||!Context->IsBattleActive()||!FMath::IsFinite(Health)||Health<=0||!FMath::IsFinite(Attack)||Attack<=0)return nullptr;
 if(Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia&&!Unit->GetHeroSkill()->ConfigureCorruptionBoss())return nullptr;
 auto* Component=AttachCopy(Unit,Context,EEGCorruptionRole::Ranged);if(!Component)return nullptr;
 Component->bCopy=false;Component->bBoss=true;Component->bPhaseTwo=false;Component->bPhaseShieldPending=false;Component->PendingPhaseShield=0;Component->BaseMaxHealth=Health;Component->SeedClock=0;
 Component->BossPhase=EEGCorruptionBossPhase::First;Component->BarrierHealth=Component->BarrierMaxHealth=Component->BarrierRemaining=0.f;
 Unit->OnDied.AddUniqueDynamic(Component,&ThisClass::BossDied);
 Component->WeaknessRemaining=Component->EffectiveBattleSeconds=Component->BarrierIntroRemaining=Component->BarrierIntroDuration=0.f;
 Component->ActiveBossCast=EEGCorruptionBossCast::None;Component->ActiveBossCastAction.Invalidate();
 Component->ActiveBossCastDuration=Component->ActiveBossCastElapsed=0.f;
 Component->MimicRecoveryRemaining=Component->SpecialSkillGapRemaining=0.f;
 Component->bFirstSpreadResolved=Component->bMimicStarted=Component->bJustCompletedSpecialSkill=false;
 Component->bBarrierIntroUsesSourcePose=false;
 Component->bBarrierOpenSoundPlayed=false;
 Component->WallHalfWidth=0.f;Component->WallCenter=FVector::ZeroVector;
 Component->bBarrierBroken=Component->bEnraged=Component->bTransitionPending=false;
 Component->MarkStacks.Reset();Component->MarkedActions.Reset();Component->LastMimic.Reset();Component->LastMimicKind=EEGHeroKind::Generic;
 auto* ASC=Unit->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),Attack);
 Component->UpdateCorruption(Corruption);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Unit->GetAttributes()->GetMaxHealth());
 Component->OpeningMaxHealth=Unit->GetAttributes()->GetMaxHealth();
 Component->OpeningThreshold=0.f;
 if(IsValid(Component->BossVisual))Component->BossVisual->Destroy();
 Component->BossVisual=AEGCorruptionBossVisual::SpawnForBoss(Unit);
 Component->VisualSeed.Reset();Component->CurrentPhantom.Reset();
 return Component;
}
void UEGCorruptionCombatComponent::UpdateCorruption(int32 Corruption)
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());if(!bBoss||!IsCurrentBattle()||!Unit||!Unit->HasAuthority())return;
 CurrentCorruption=Corruption;
 const float Multiplier=FMath::Clamp(Corruption<40?LowHealthMultiplier:Corruption<=70?MediumHealthMultiplier:HighHealthBaseMultiplier+HighHealthStepMultiplier*FMath::FloorToInt((Corruption-70)/float(FMath::Max(1,HighHealthStepSize))),FMath::Max(.01f,LowHealthMultiplier),FMath::Max(LowHealthMultiplier,MaximumHealthMultiplier));
 const float LifeMultiplier=BossPhase==EEGCorruptionBossPhase::Second?SafeRule(SecondLifeHealthMultiplier,1.f,.01f):SafeRule(FirstLifeHealthMultiplier,1.f,.01f);
 Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),BaseMaxHealth*Multiplier*LifeMultiplier);
}
void UEGCorruptionCombatComponent::AttributeChanged(FName Attribute,float OldValue,float NewValue)
{
 // Phase changes are committed after the damage GE; attribute delegates can
 // run inside that GE and are not a safe place to publish a new phase.
}
void UEGCorruptionCombatComponent::BossDied(AEGChessUnit* Unit)
{
 if(Unit==GetOwner()&&bBoss)EndCombat();
}
void UEGCorruptionCombatComponent::BeginPhaseTwo()
{
 FinishBarrier(true);
}
float UEGCorruptionCombatComponent::GetPhaseShieldRemaining() const
{
 return BossPhase==EEGCorruptionBossPhase::Barrier?BarrierHealth:0.f;
}
void UEGCorruptionCombatComponent::CommitPhaseShield(bool bCommitTransition,bool bCommitBreak)
{
 if(bTransitionPending&&bCommitTransition){bTransitionPending=false;EnterBarrier();return;}
 if(bCommitBreak&&BossPhase==EEGCorruptionBossPhase::Barrier&&BarrierHealth<=0.f&&BarrierIntroRemaining<=0.f)FinishBarrier(true);
}
float UEGCorruptionCombatComponent::FilterDamage(AEGChessUnit* Source,FGuid ActionId,float Damage,float ShieldDamageMultiplier,float* OutPhaseShieldAbsorbed,bool bExecution)
{
 if(OutPhaseShieldAbsorbed)*OutPhaseShieldAbsorbed=0.f;
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(!bBoss||!IsCurrentBattle()||!Unit||Damage<=0)return Damage;
 if(BossPhase==EEGCorruptionBossPhase::Barrier)
 {
  if(BarrierIntroRemaining>0.f)return 0.f;
  const float Absorbed=FMath::Min(BarrierHealth,Damage*FMath::Max(1.f,ShieldDamageMultiplier));
  BarrierHealth=FMath::Max(0.f,BarrierHealth-Absorbed);
  if(OutPhaseShieldAbsorbed)*OutPhaseShieldAbsorbed=Absorbed;
  if(IsValid(BarrierVisual))BarrierVisual->SetIntegrity(BarrierMaxHealth>0.f?BarrierHealth/BarrierMaxHealth:0.f);
  return 0.f;
 }
 if(BossPhase==EEGCorruptionBossPhase::Second)return WeaknessRemaining>0.f?Damage*SafeRule(BreakDamageMultiplier,1.2f,1.f):Damage;
 if(BossPhase!=EEGCorruptionBossPhase::First||bTransitionPending)return 0.f;
 const float Health=Unit->GetAttributes()->GetHealth();
 const float ShieldEquivalent=bExecution?0.f:FMath::Max(0.f,Unit->GetAttributes()->GetShield())/FMath::Max(1.f,ShieldDamageMultiplier);
 if(Damage<Health+ShieldEquivalent)return Damage;
 bTransitionPending=true;
 // Keep an internal one-point sentinel so the first pool cannot emit death.
 // Its public phase-health view reports zero until the second pool is installed.
 return FMath::Max(0.f,Health-1.f+ShieldEquivalent);
}
float UEGCorruptionCombatComponent::GetPhaseHealth() const
{
 const auto* Unit=Cast<AEGChessUnit>(GetOwner());
 return BossPhase==EEGCorruptionBossPhase::Barrier||bTransitionPending||!Unit?0.f:Unit->GetAttributes()->GetHealth();
}
float UEGCorruptionCombatComponent::GetPhaseMaxHealth() const
{
 const auto* Unit=Cast<AEGChessUnit>(GetOwner());
 return Unit?Unit->GetAttributes()->GetMaxHealth():0.f;
}
void UEGCorruptionCombatComponent::SetEffectiveBattleSeconds(float Seconds)
{
 if(!bBoss||!IsCurrentBattle()||!FMath::IsFinite(Seconds))return;
 EffectiveBattleSeconds=FMath::Max(0.f,Seconds);
 const bool bNowEnraged=EffectiveBattleSeconds>=60.f;
 if(bEnraged!=bNowEnraged){bEnraged=bNowEnraged;if(IsValid(BossVisual))BossVisual->SetEnraged(bEnraged);}
}
void UEGCorruptionCombatComponent::BeginBossCast(EEGCorruptionBossCast Kind,float Duration,FGuid Action)
{
 if(Kind==EEGCorruptionBossCast::None||!IsCurrentBattle()||BossPhase!=EEGCorruptionBossPhase::Second)return;
 if(ActiveBossCast!=EEGCorruptionBossCast::None)EndBossCast(EEGCorruptionBossCastEvent::Cancelled);
 if(!IsCurrentBattle()||BossPhase!=EEGCorruptionBossPhase::Second)return;
 ActiveBossCast=Kind;ActiveBossCastAction=Action.IsValid()?Action:FGuid::NewGuid();
 ActiveBossCastDuration=FMath::Max(.01f,Duration);ActiveBossCastElapsed=0.f;
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->GetCombat()->AddExternalActionBlock(ActiveBossCastAction);
 OnBossCastEvent.Broadcast(ActiveBossCast,EEGCorruptionBossCastEvent::Started,ActiveBossCastAction,ActiveBossCastDuration);
}
void UEGCorruptionCombatComponent::EndBossCast(EEGCorruptionBossCastEvent Event)
{
 if(ActiveBossCast==EEGCorruptionBossCast::None)return;
 const EEGCorruptionBossCast Kind=ActiveBossCast;const FGuid Action=ActiveBossCastAction;const float Duration=ActiveBossCastDuration;
 ActiveBossCast=EEGCorruptionBossCast::None;ActiveBossCastAction.Invalidate();ActiveBossCastDuration=ActiveBossCastElapsed=0.f;
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->GetCombat()->RemoveExternalActionBlock(Action);
 OnBossCastEvent.Broadcast(Kind,Event,Action,Duration);
}
void UEGCorruptionCombatComponent::NotifySpreadStarted(AEGCorruptionSeed* Seed,FVector Location,float Radius,float Duration,FGuid DamageAction)
{
 if(!bBoss||BossPhase!=EEGCorruptionBossPhase::Second||!IsCurrentBattle()||!IsValid(Seed)||!Seed->BelongsTo(Battle.Get()))return;
 const FGuid CastRound=RoundId;
 VisualSeed=Seed;BeginBossCast(EEGCorruptionBossCast::Spread,Duration,DamageAction);
 if(IsCurrentSpread(Seed,CastRound,DamageAction)&&Battle->IsCombatRunning()&&IsValid(BossVisual))BossVisual->ShowSpreadWarning(Location,Radius,Duration);
}
void UEGCorruptionCombatComponent::NotifySpreadResolved(AEGCorruptionSeed* Seed,FVector Location,float Radius)
{
 if(!VisualSeed.HasSameIndexAndSerialNumber(TWeakObjectPtr<AEGCorruptionSeed>(Seed)))return;
 VisualSeed.Reset();
 if(bBoss&&BossPhase==EEGCorruptionBossPhase::Second&&IsCurrentBattle())
 {
  bFirstSpreadResolved=true;SpecialSkillGapRemaining=SafeRule(SpecialSkillGapSeconds,1.f,1.f);bJustCompletedSpecialSkill=true;
  EndBossCast(EEGCorruptionBossCastEvent::Resolved);
 }
 if(IsValid(BossVisual))
 {
  BossVisual->ClearSpreadWarning();
  if(bBoss&&BossPhase==EEGCorruptionBossPhase::Second&&IsCurrentBattle())BossVisual->ShowSpreadBurst(Location,Radius);
 }
}
void UEGCorruptionCombatComponent::NotifySpreadEnded(AEGCorruptionSeed* Seed)
{
 if(!VisualSeed.HasSameIndexAndSerialNumber(TWeakObjectPtr<AEGCorruptionSeed>(Seed)))return;
 VisualSeed.Reset();if(ActiveBossCast==EEGCorruptionBossCast::Spread)EndBossCast(EEGCorruptionBossCastEvent::Cancelled);
 if(IsValid(BossVisual))BossVisual->ClearSpreadWarning();
}
bool UEGCorruptionCombatComponent::IsCurrentSpread(const AEGCorruptionSeed* Seed,FGuid Round,FGuid Action) const
{
 return IsCurrentBattle()&&BossPhase==EEGCorruptionBossPhase::Second&&Round==RoundId&&Action.IsValid()&&
  ActiveBossCast==EEGCorruptionBossCast::Spread&&ActiveBossCastAction==Action&&VisualSeed.Get()==Seed;
}
float UEGCorruptionCombatComponent::GetOutgoingDamageMultiplier() const
{
 return bBoss&&bEnraged?1.f+.1f*float(1+FMath::FloorToInt(FMath::Max(0.f,EffectiveBattleSeconds-60.f)/5.f)):1.f;
}
bool UEGCorruptionCombatComponent::BlocksMovement(FVector Start,FVector End,float Radius) const
{
 if(!IsWallActive()||WallHalfWidth<=0.f||WallNormal.IsNearlyZero())return false;
 const float A=FVector::DotProduct(Start-WallCenter,WallNormal),B=FVector::DotProduct(End-WallCenter,WallNormal);
 const float R=FMath::Max(0.f,Radius)+AEGCorruptionBarrier::HalfThickness;
 // Permit escape from an overlapping side but never crossing through the wall.
 if(A<=-R&&B<=-R)return false;
 if(A>= R&&B>= R)return false;
 if(A<0.f&&B<A)return false;
 if(A>0.f&&B>A)return false;
 const FVector Mid=(Start+End)*.5f;
 return FMath::Abs(FVector::DotProduct(Mid-WallCenter,WallWidthDirection))<=WallHalfWidth+R;
}
FVector UEGCorruptionCombatComponent::WallContactPoint(FVector From) const
{
 if(WallHalfWidth<=0.f)return GetOwner()?GetOwner()->GetActorLocation():From;
 const float Along=FMath::Clamp(FVector::DotProduct(From-WallCenter,WallWidthDirection),-WallHalfWidth,WallHalfWidth);
 FVector Result=WallCenter+WallWidthDirection*Along;Result.Z=From.Z;return Result;
}
bool UEGCorruptionCombatComponent::AllowsPlayerLocation(FVector Point,float Radius) const
{
 return !IsWallActive()||WallHalfWidth<=0.f||FVector::DotProduct(Point-WallCenter,WallNormal)<=-FMath::Max(0.f,Radius)-AEGCorruptionBarrier::HalfThickness-WallClearance;
}
FVector UEGCorruptionCombatComponent::ConstrainPlayerLocation(FVector Point,float Radius) const
{
 if(!IsWallActive()||WallHalfWidth<=0.f||Point.ContainsNaN())return Point;
 // A strictly interior destination also absorbs floating-point error when a
 // large source-curve excursion is projected back from behind the wall.
 const float Limit=-FMath::Max(0.f,Radius)-AEGCorruptionBarrier::HalfThickness-WallClearance-1.f;
 const float Distance=FVector::DotProduct(Point-WallCenter,WallNormal);
 return Distance>Limit?Point-WallNormal*(Distance-Limit):Point;
}
void UEGCorruptionCombatComponent::EnterBarrier()
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(!IsCurrentBattle()||!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit))return;
 BossPhase=EEGCorruptionBossPhase::Barrier;bPhaseTwo=false;bBarrierBroken=false;
 BarrierMaxHealth=FMath::Max(1.f,OpeningMaxHealth*SafeRule(BarrierHealthRatio,.4f,.01f));BarrierHealth=BarrierMaxHealth;BarrierRemaining=SafeRule(BarrierDurationSeconds,8.f,.1f);
 BarrierIntroDuration=.7f;BarrierIntroRemaining=BarrierIntroDuration;bBarrierIntroUsesSourcePose=false;bBarrierOpenSoundPlayed=false;BarrierPushbackFallbackCount=0;WallPushbackLastElapsed=0.f;WallPushbacks.Reset();BarrierBlockToken=FGuid::NewGuid();
 Unit->GetCombat()->AddExternalActionBlock(BarrierBlockToken);
 Unit->GetHeroSkill()->CancelAction();
 // Reconfiguration ends the old behavior, including an independently ticking rain.
 Unit->GetHeroSkill()->ConfigureCorruptionBoss();
 for(TActorIterator<AEGCorruptionSeed> It(GetWorld());It;++It)if(It->BelongsTo(Battle.Get()))It->Destroy();
 for(TActorIterator<AEGSkillProjectile> It(GetWorld());It;++It)
  if(It->Battle.Get()==Battle.Get()&&It->Source.Get()==Unit)It->Destroy();
 const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
 const auto* Board=Mode?Mode->GetBoard():nullptr;
 const auto* Layout=Board?Board->LayoutSource.Get():nullptr;
 struct FOriginalUnit {TWeakObjectPtr<AEGChessUnit> Actor;FVector Location,Reserved,Pending;};
 struct FOriginalFlight {TWeakObjectPtr<AEGSkillProjectile> Actor;FEGThrownSummonFlightState State;};
 TArray<FOriginalUnit> OriginalUnits;TArray<FOriginalFlight> OriginalFlights;
 for(auto* Other:Battle->Units())if(IsValid(Other)&&(Other==Unit||IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player))
 {
  FOriginalUnit& Record=OriginalUnits.AddDefaulted_GetRef();Record.Actor=Other;Record.Location=Other->GetActorLocation();
  Other->GetCombat()->GetBoardLandingCoordinates(Record.Reserved,Record.Pending);
 }
 for(TActorIterator<AEGSkillProjectile> It(GetWorld());It;++It)
  if(It->Battle.Get()==Battle.Get()&&It->Kind==EEGProjectileKind::ThrownSummon)
  {FOriginalFlight& Record=OriginalFlights.AddDefaulted_GetRef();Record.Actor=*It;Record.State=It->CaptureThrownSummonFlight();}
 const auto FailAndRecover=[&]()
 {
  for(const auto& Record:OriginalUnits)if(auto* Other=Record.Actor.Get())
  {
   Other->GetCombat()->RestoreBoardLandingCoordinates(Record.Reserved,Record.Pending);
   Other->SetActorLocation(Record.Location,false,nullptr,ETeleportType::TeleportPhysics);
  }
  for(const auto& Record:OriginalFlights)if(auto* Flight=Record.Actor.Get())Flight->RestoreThrownSummonFlight(Record.State);
  if(IsValid(BarrierActor))BarrierActor->Destroy();BarrierActor=nullptr;
  if(IsValid(BarrierVisual))BarrierVisual->Destroy();BarrierVisual=nullptr;
  Unit->GetHeroSkill()->EndBarrierTransition(RoundId,BarrierBlockToken);
  Battle->SetPresentationHold(BarrierBlockToken,false);Unit->GetCombat()->RemoveExternalActionBlock(BarrierBlockToken);
  BarrierBlockToken.Invalidate();BarrierIntroRemaining=BarrierIntroDuration=BarrierRemaining=BarrierHealth=0.f;bBarrierIntroUsesSourcePose=false;BossPhase=EEGCorruptionBossPhase::Finished;
  const FGuid FailedRound=RoundId;
  if(auto* Demo=GetWorld()->GetAuthGameMode<AEGDemoGameMode>())Demo->RequestEncounterRecovery(FailedRound);
  else EndCombat();
 };
 if(!Layout){UE_LOG(LogTemp,Warning,TEXT("Corruption wall: no board geometry; recovering encounter"));FailAndRecover();return;}
 if(Layout)
 {
  const FTransform Basis=Layout->GetActorTransform();
  WallWidthDirection=Basis.GetUnitAxis(EAxis::X);WallWidthDirection.Z=0.f;WallWidthDirection.Normalize();
  WallNormal=Basis.GetUnitAxis(EAxis::Y);WallNormal.Z=0.f;WallNormal.Normalize();
  const int32 WallRow=FMath::Clamp(4,0,Layout->Rows-1);
  const float LocalY=(WallRow+.5f)*Layout->CellDepth-Layout->Rows*Layout->CellDepth*.5f;
  WallCenter=Basis.TransformPosition(FVector(0,LocalY,0));
  WallHalfWidth=.5f*Layout->Columns*Layout->CellWidth*Basis.GetScaledAxis(EAxis::X).Size();
  const float WallHeight=Layout->CellDepth*Basis.GetScaledAxis(EAxis::Y).Size()*1.5f;
  const float BackRowY=(Layout->Rows-.5f)*Layout->CellDepth-Layout->Rows*Layout->CellDepth*.5f;
  FVector BossPosition=Basis.TransformPosition(FVector(0,BackRowY,0));BossPosition.Z=Unit->GetActorLocation().Z;
  WallOriginalBossPosition=Unit->GetActorLocation();
  Unit->SetActorLocation(BossPosition,false,nullptr,ETeleportType::TeleportPhysics);
  // Plan against the real battle space, ignoring only this transaction's own
  // participants; do not move actors or reservations while searching.
  TArray<AEGChessUnit*> Players;
  for(auto* Other:Battle->Units())
   if(IsValid(Other)&&Other!=Unit&&IEGCombatant::Execute_IsAlive(Other)&&IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player)
    Players.Add(Other);
  Players.Sort([&](const AEGChessUnit& A,const AEGChessUnit& B)
  {
   const FVector LA=Basis.InverseTransformPosition(A.GetActorLocation()),LB=Basis.InverseTransformPosition(B.GetActorLocation());
   const int32 RowA=FMath::RoundToInt(LA.Y),RowB=FMath::RoundToInt(LB.Y);
   if(RowA!=RowB)return RowA<RowB;
   if(LA.X!=LB.X)return LA.X<LB.X;
   return A.GetUniqueID()<B.GetUniqueID();
  });
  WallPushbacks.Reset();
  const float ScaleX=FMath::Max(.01f,Basis.GetScaledAxis(EAxis::X).Size());
  const float ScaleY=FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size());
  const float HalfX=Layout->Columns*Layout->CellWidth*.5f;
  const float Bottom=-Layout->Rows*Layout->CellDepth*.5f;
  const float PlayerTop=Bottom+FMath::Clamp(Layout->PlayerRows,1,Layout->Rows)*Layout->CellDepth;
  // Try a real separation margin first. If a full formation requires exact
  // tangency, retry without it; positive margin cannot fit seven full bases
  // across a seven-cell board.
  bool bPlanned=false;
  for(const float Separation:{.5f,0.f})
  {
   WallPushbacks.Reset();bool bAll=true;
   for(auto* Other:Players)
   {
    const float Radius=UEGCombatComponent::BlockingRadius(Other);
    const float RX=Radius/ScaleX,RY=Radius/ScaleY;
    const float StepX=FMath::Max(1.f,RX*.25f),StepY=FMath::Max(1.f,(2.f*Radius+Separation+.05f)/ScaleY);
    const float MinX=-HalfX+RX,MaxX=HalfX-RX,MinY=Bottom+RY,MaxY=PlayerTop-RY;
    const FVector SourceLocal=Basis.InverseTransformPosition(Other->GetActorLocation());
    bool bFound=false;FVector Best=FVector::ZeroVector;
    for(float Y=MinY;Y<=MaxY+.01f&&!bFound;Y+=StepY)
    {
     float BestDistance=MAX_flt;
     TArray<float> CandidateXs;
     CandidateXs.Add(FMath::Clamp(float(SourceLocal.X),MinX,MaxX));
     CandidateXs.Add(MinX);CandidateXs.Add(MaxX);
     // Tangent positions preserve a unit's lane much better than rounding it
     // to the search grid. Keep coarse samples for irregular external blockers.
     for(const auto& Entry:WallPushbacks)if(const auto* Placed=Entry.Unit.Get())
     {
      const FVector PlacedLocal=Basis.InverseTransformPosition(Entry.End);
      const float Required=Radius+UEGCombatComponent::BlockingRadius(Placed)+Separation+.05f;
      const float DY=(Y-float(PlacedLocal.Y))*ScaleY;
      if(FMath::Abs(DY)<Required)
      {
       const float DX=FMath::Sqrt(FMath::Max(0.f,FMath::Square(Required)-FMath::Square(DY)))/ScaleX;
       CandidateXs.Add(float(PlacedLocal.X)-DX);CandidateXs.Add(float(PlacedLocal.X)+DX);
      }
     }
     for(float X=MinX;X<=MaxX+.01f;X+=StepX)CandidateXs.Add(FMath::Min(X,MaxX));
     for(const float X:CandidateXs)
     {
      if(X<MinX-.001f||X>MaxX+.001f)continue;
      FVector Point=Basis.TransformPosition(FVector(FMath::Clamp(X,MinX,MaxX),FMath::Min(Y,MaxY),SourceLocal.Z));
      Point.Z=Other->GetActorLocation().Z;
      if(!Other->GetCombat()->IsBoardLocationFree(Point,nullptr,nullptr,true,&Players))continue;
      bool bClear=true;
      for(const auto& Entry:WallPushbacks)if(const auto* Placed=Entry.Unit.Get())
      {
       const float Required=Radius+UEGCombatComponent::BlockingRadius(Placed)+Separation;
       if(FVector::DistSquared2D(Point,Entry.End)<FMath::Square(Required)-.01f){bClear=false;break;}
      }
      const float Distance=FMath::Abs(X-SourceLocal.X);
      if(bClear&&Distance<BestDistance){BestDistance=Distance;Best=Point;bFound=true;}
     }
    }
    if(!bFound){bAll=false;break;}
    FWallPushback& Entry=WallPushbacks.AddDefaulted_GetRef();
    Entry.Unit=Other;Entry.Start=Other->GetActorLocation();Entry.End=Best;
    Other->GetCombat()->GetBoardLandingCoordinates(Entry.OriginalReserved,Entry.OriginalPending);
    for(const auto& FlightRecord:OriginalFlights)if(auto* Flight=FlightRecord.Actor.Get())
     if(Flight->Cargo.Get()==Other&&Other->GetHeroSkill()->bInFlight)
     {Entry.Flight=Flight;Entry.OriginalFlight=MakeShared<FEGThrownSummonFlightState>(FlightRecord.State);Entry.Landing=Best;Entry.Landing.Z=Flight->Destination.Z;break;}
   }
   if(bAll){bPlanned=true;break;}
  }
  if(!bPlanned)
  {UE_LOG(LogTemp,Warning,TEXT("Corruption wall: no legal player-side formation; recovering encounter"));WallPushbacks.Reset();FailAndRecover();return;}
  // The bottom-up packing above selects the occupied space, not which unit
  // must traverse to each slot. Greedy assignment can send a back-left hero
  // across the entire board while occupying its nearby slot with another
  // unit. Reassign equal-radius actors globally, keeping exactly the same
  // validated positions and checking each actor against nonparticipants.
  TArray<FVector> Slots;TArray<float> SlotRadii;
  for(const auto& Entry:WallPushbacks)
  {
   Slots.Add(Entry.End);SlotRadii.Add(UEGCombatComponent::BlockingRadius(Entry.Unit.Get()));
  }
  TArray<TArray<double>> Costs;Costs.SetNum(WallPushbacks.Num());
  for(int32 I=0;I<WallPushbacks.Num();++I)
  {
   auto* Other=WallPushbacks[I].Unit.Get();Costs[I].Init(1.e30,Slots.Num());
   if(!IsValid(Other))continue;
   const float Radius=UEGCombatComponent::BlockingRadius(Other);
   for(int32 J=0;J<Slots.Num();++J)
   {
    // Equal radii retain the original pairwise guarantees for every slot.
    if(Radius!=SlotRadii[J])continue;
    FVector Point=Slots[J];Point.Z=WallPushbacks[I].Start.Z;
    if(Other->GetCombat()->IsBoardLocationFree(Point,nullptr,nullptr,true,&Players))
     Costs[I][J]=FVector::DistSquared2D(WallPushbacks[I].Start,Point);
   }
  }
  TArray<int32> Assignment;
  if(!AssignMinimumTravel(Costs,Assignment))
  {UE_LOG(LogTemp,Warning,TEXT("Corruption wall: could not assign validated formation; recovering encounter"));WallPushbacks.Reset();FailAndRecover();return;}
  for(int32 I=0;I<WallPushbacks.Num();++I)
  {
   auto& Entry=WallPushbacks[I];Entry.End=Slots[Assignment[I]];Entry.End.Z=Entry.Start.Z;
   if(auto* Flight=Entry.Flight.Get()){Entry.Landing=Entry.End;Entry.Landing.Z=Flight->Destination.Z;}
  }
  for(const auto& Entry:WallPushbacks)if(auto* Other=Entry.Unit.Get())
  {
   FVector Reserved,Pending;Other->GetCombat()->GetBoardLandingCoordinates(Reserved,Pending);
   UE_LOG(LogTemp,Display,TEXT("CorruptionPushbackPlan actor=%s definition=%s start_local=%s target_local=%s radius=%.3f emplacement=%d in_flight=%d reserved=%d pending=%d reserved_local=%s pending_local=%s"),
    *GetNameSafe(Other),*GetNameSafe(Other->GetDefinition()),*Basis.InverseTransformPosition(Entry.Start).ToCompactString(),
    *Basis.InverseTransformPosition(Entry.End).ToCompactString(),UEGCombatComponent::BlockingRadius(Other),Other->IsAuxiliaryEmplacement()?1:0,
    Other->GetHeroSkill()->bInFlight?1:0,Other->GetCombat()->IsBoardLandingReserved()?1:0,Other->GetCombat()->IsBoardLandingPending()?1:0,
    *Basis.InverseTransformPosition(Reserved).ToCompactString(),*Basis.InverseTransformPosition(Pending).ToCompactString());
  }
  // Reservations move atomically with the plan. Projectiles retain their
  // remaining flight time and point at the same reserved landing.
  for(const auto& Entry:WallPushbacks)if(auto* Other=Entry.Unit.Get())
  {
   Other->GetCombat()->RelocateBoardLanding(Entry.Flight.IsValid()?Entry.Landing:Entry.End);
   if(auto* Flight=Entry.Flight.Get())if(!Flight->RebaseThrownSummon(Entry.Start,Entry.Landing))
   {UE_LOG(LogTemp,Warning,TEXT("Corruption wall: failed to rebase thrown summon; recovering encounter"));WallPushbacks.Reset();FailAndRecover();return;}
  }
  BarrierActor=GetWorld()->SpawnActor<AEGCorruptionBarrier>();
  if(BarrierActor)
  {
   BarrierActor->SetOwner(Unit);
   if(!BarrierActor->InitializeWall(WallCenter,WallWidthDirection,2.f*WallHalfWidth,WallHeight)){BarrierActor->Destroy();BarrierActor=nullptr;}
  }
  if(IsValid(BarrierActor)&&!WallPushbacks.IsEmpty())BarrierActor->SetBlocking(false);
  if(!IsValid(BarrierActor)){WallPushbacks.Reset();UE_LOG(LogTemp,Warning,TEXT("Corruption wall: actor initialization failed; recovering encounter"));FailAndRecover();return;}
  if(Unit->GetNetMode()!=NM_DedicatedServer)
  {
   BarrierVisual=GetWorld()->SpawnActor<AEGCorruptionBarrierVisual>();
   if(BarrierVisual)
   {
    BarrierVisual->SetOwner(Unit);
    if(!BarrierVisual->InitializeVisual(WallCenter,WallWidthDirection,2.f*WallHalfWidth,WallHeight))
    {BarrierVisual->Destroy();BarrierVisual=nullptr;}
   }
  }
 }
 if(Unit->GetNetMode()!=NM_DedicatedServer)
 {
  float SourceDuration=0.f;
  if(auto* Assembly=BarrierIntroAssembly.LoadSynchronous();Assembly&&
     Unit->GetHeroSkill()->BeginBarrierTransition(Assembly,RoundId,BarrierBlockToken,SourceDuration))
  {BarrierIntroDuration=SourceDuration;bBarrierIntroUsesSourcePose=true;}
 }
 if(IsValid(BarrierVisual))
 {
  BarrierVisual->SetIntroRevealDelay(bBarrierIntroUsesSourcePose?BarrierOpenDelay:0.f);
  BarrierVisual->SetPresentationToken(BarrierBlockToken);
 }
 // Start only after synchronous asset/assembly creation. Setup time must never
 // consume the opening pose or the independent eight-second wall challenge.
 BarrierIntroRemaining=BarrierIntroDuration;
 BarrierIntroStartReal=BarrierIntroLastReal=FPlatformTime::Seconds();
 Battle->SetPresentationHold(BarrierBlockToken,true);
 if(IsValid(BarrierVisual))BarrierVisual->SetIntroElapsed(0.f);
 if(!bBarrierIntroUsesSourcePose)
 {UEGGameAudioComponent::PlayBossEffect(Unit,Battle.Get(),EEGBossEffectSound::BarrierOpen,BarrierBlockToken);bBarrierOpenSoundPlayed=true;}
}
void UEGCorruptionCombatComponent::RecoverBarrierPushback()
{
 const FGuid FailedRound=RoundId;
 for(const auto& Entry:WallPushbacks)if(auto* Other=Entry.Unit.Get())
 {
  Other->GetCombat()->RestoreBoardLandingCoordinates(Entry.OriginalReserved,Entry.OriginalPending);
  Other->SetActorLocation(Entry.Start,false,nullptr,ETeleportType::TeleportPhysics);
  if(auto* Flight=Entry.Flight.Get())if(Entry.OriginalFlight)Flight->RestoreThrownSummonFlight(*Entry.OriginalFlight);
 }
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->SetActorLocation(WallOriginalBossPosition,false,nullptr,ETeleportType::TeleportPhysics);
 auto* Demo=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 EndCombat();
 if(Demo)Demo->RequestEncounterRecovery(FailedRound);
}
void UEGCorruptionCombatComponent::FinishBarrier(bool bBroken)
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());
 if(BossPhase!=EEGCorruptionBossPhase::Barrier||!IsCurrentBattle()||!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit))return;
 BossPhase=EEGCorruptionBossPhase::Second;bPhaseTwo=true;bBarrierBroken=bBroken;
 if(IsValid(BossVisual)){BossVisual->SetSecondPhase(true);if(!bBroken)BossVisual->ShowBarrierFailure(WallCenter,WallHalfWidth);}
 UEGGameAudioComponent::PlayBossEffect(Unit,Battle.Get(),bBroken?EEGBossEffectSound::BarrierBreak:EEGBossEffectSound::BarrierFailure);
 Unit->GetHeroSkill()->EndBarrierTransition(RoundId,BarrierBlockToken);bBarrierIntroUsesSourcePose=false;
 BarrierRemaining=0.f;BarrierHealth=0.f;BarrierIntroRemaining=0.f;
 Battle->SetPresentationHold(BarrierBlockToken,false);Unit->GetCombat()->RemoveExternalActionBlock(BarrierBlockToken);BarrierBlockToken.Invalidate();
 if(IsValid(BarrierActor))BarrierActor->Destroy();BarrierActor=nullptr;
 if(IsValid(BarrierVisual)){BarrierVisual->Dissolve(bBroken);BarrierVisual=nullptr;}
 UpdateCorruption(CurrentCorruption);
 Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Unit->GetAttributes()->GetMaxHealth());
 WeaknessRemaining=bBroken?SafeRule(BreakWeaknessSeconds,4.f):0.f;
 SpreadClock=SafeRule(SpreadIntervalSeconds,7.f,.1f)-SafeRule(FirstSpreadDelaySeconds,3.f,.1f);
 MimicClock=0.f;SpreadDelay=MimicDelay=MimicRecoveryRemaining=SpecialSkillGapRemaining=0.f;
 bFirstSpreadResolved=bMimicStarted=bJustCompletedSpecialSkill=false;
 if(!bBroken)
 {
  const auto Targets=Battle->Units();
  for(auto* Target:Targets)if(IsCurrentBattle()&&IsValid(Target)&&Unit->GetCombat()->IsValidEnemy(Target))
   FEGCombatExecution::Damage(Unit,Target,Target->GetAttributes()->GetMaxHealth()*SafeRule(BarrierFailureHealthRatio,.2f),0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid());
 }
 if(IsCurrentBattle())if(auto* Audio=UEGGameAudioComponent::FindFor(Unit))Audio->StartStoryDialogue(7,true);
 if(IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(Unit))OnPhaseTwo.Broadcast(Unit);
}
void UEGCorruptionCombatComponent::RememberBasicAttack(FGuid Id)
{
 if(!bBoss||BossPhase!=EEGCorruptionBossPhase::Second||!Id.IsValid())return;
 if(MarkedActions.Num()>256)MarkedActions.Reset();
 MarkedActions.Add(Id);
}
void UEGCorruptionCombatComponent::RememberUltimate(FGuid Id)
{
 if(!bCopy||!IsCurrentBattle()||!Id.IsValid()||UltimateActions.Contains(Id))return;
 UltimateActions.Add(Id);UltimateActionOrder.Add(Id);
 if(UltimateActionOrder.Num()>256){UltimateActions.Remove(UltimateActionOrder[0]);UltimateActionOrder.RemoveAt(0);}
}
void UEGCorruptionCombatComponent::UltimateCommitted(FGuid Id)
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());if(!bCopy||CopyRole!=EEGCorruptionRole::Support||!IsCurrentBattle()||!Unit||!Id.IsValid()||LastCommittedAction==Id)return;
 LastCommittedAction=Id;
 for(auto* Other:Battle->Units())if(IsValid(Other)&&Other!=Unit&&IEGCombatant::Execute_IsAlive(Other)&&IEGCombatant::Execute_GetFaction(Other)==IEGCombatant::Execute_GetFaction(Unit)&&Other->GetHeroSkill()->IsTargetable()&&Other->GetAttributes()->GetMaxMana()>0.f&&FVector::DistSquared2D(Unit->GetActorLocation(),Other->GetActorLocation())<=FMath::Square(FMath::Max(0.f,SupportRadius)))Other->RestoreMana(FMath::Max(0.f,SupportMana));
}
void UEGCorruptionCombatComponent::ObserveDamage(const FEGCombatResolution& Receipt)
{
 auto* Unit=Cast<AEGChessUnit>(GetOwner());if(!IsCurrentBattle()||!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit)||Receipt.SourceId!=Unit->GetInstanceId()||Receipt.bHealing||Receipt.bLinkedDamage)return;
 const float Effective=FMath::Max(0.f,-Receipt.HealthDelta)+FMath::Max(0.f,Receipt.ShieldAbsorbed);if(Effective<=0)return;
 if(bBoss)
 {
  if(BossPhase!=EEGCorruptionBossPhase::Second||!MarkedActions.Contains(Receipt.ActionId))return;
  MarkedActions.Remove(Receipt.ActionId);
  AEGChessUnit* Hit=nullptr;for(auto* Candidate:Battle->Units())if(IsValid(Candidate)&&Candidate->GetInstanceId()==Receipt.TargetId){Hit=Candidate;break;}
  if(!Hit)return;
  int32& Stacks=MarkStacks.FindOrAdd(Hit);++Stacks;
  const int32 Threshold=FMath::Max(1,MarkHitThreshold);
  if(IsValid(BossVisual))BossVisual->ShowMark(Hit,Stacks>=Threshold?0:Stacks,Threshold);
  if(Stacks>=Threshold)
  {
   MarkStacks.Remove(Hit);
   const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
   const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
   const float Radius=Layout?Layout->CellWidth*Layout->GetActorScale3D().X*.7f:200.f;
   if(IsValid(BossVisual))BossVisual->ShowMarkBurst(Hit->GetActorLocation(),Radius);
   Battle->Area(Unit,Hit->GetActorLocation(),Radius,Unit->GetAttributes()->GetAttackPower()*SafeRule(MarkDamageMultiplier,.6f),false,false,FGuid::NewGuid());
  }
  return;
 }
 if(!bCopy)return;
 if(CopyRole==EEGCorruptionRole::Melee){Battle->Heal(Unit,Effective*.15f);return;}
 if(CopyRole!=EEGCorruptionRole::Ranged||!UltimateActions.Contains(Receipt.ActionId))return;
 AEGChessUnit* Target=nullptr;for(auto* Candidate:Battle->Units())if(IsValid(Candidate)&&Candidate->GetInstanceId()==Receipt.TargetId){Target=Candidate;break;}
 if(!Target)return;
 const FVector Point=Target->GetActorLocation();const auto Snapshot=Battle->Units();
 for(auto* Other:Snapshot)if(IsCurrentBattle()&&IsValid(Other)&&Other!=Target&&Unit->GetCombat()->IsValidEnemy(Other)&&FVector::DistSquared2D(Point,Other->GetActorLocation())<=FMath::Square(200.f))
  FEGCombatExecution::Damage(Unit,Other,Effective*.4f,0,1,FGameplayEffectContextHandle(),Receipt.ActionId,true);
}
void UEGCorruptionCombatComponent::TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Tick,Function);
 auto* Unit=Cast<AEGChessUnit>(GetOwner());if(!IsCurrentBattle()||!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit))return;
 if(bBoss&&BossPhase==EEGCorruptionBossPhase::Barrier&&BarrierIntroRemaining>0.f)
 {
  const double Now=FPlatformTime::Seconds();
  if(!Battle->CanAdvancePresentationVisualsForToken(BarrierBlockToken))
  {
   BarrierIntroStartReal+=FMath::Max(0.,Now-BarrierIntroLastReal);
   BarrierIntroLastReal=Now;return;
  }
  BarrierIntroLastReal=Now;
  const float Elapsed=FMath::Max(0.f,float(Now-BarrierIntroStartReal));
  if(bBarrierIntroUsesSourcePose&&!Unit->GetHeroSkill()->SampleBarrierTransition(RoundId,BarrierBlockToken,FMath::Min(Elapsed,BarrierIntroDuration)))
  {
   Unit->GetHeroSkill()->EndBarrierTransition(RoundId,BarrierBlockToken);
   bBarrierIntroUsesSourcePose=false;BarrierIntroDuration=.7f;
   if(IsValid(BarrierVisual))BarrierVisual->SetIntroRevealDelay(0.f);
  }
  const float PushDelta=FMath::Max(0.f,Elapsed-WallPushbackLastElapsed);
  WallPushbackLastElapsed=Elapsed;
  TArray<AEGChessUnit*> Group;TArray<FVector> Current,Proposed;
  for(const auto& Entry:WallPushbacks)
  {
   auto* Other=Entry.Unit.Get();Group.Add(Other);
   const FVector Here=IsValid(Other)?Other->GetActorLocation():Entry.End;
   FVector Next=Here;
   if(IsValid(Other)&&IEGCombatant::Execute_IsAlive(Other))
   {
    // First open room toward the player edge; only then shift along the row.
    // Parallel neighbours can therefore separate before small lane changes.
    const FVector Whole=Entry.End-Entry.Start,Remaining=Entry.End-Here;
    const float Distance=FMath::Abs(FVector::DotProduct(Whole,WallNormal))+
     FMath::Abs(FVector::DotProduct(Whole,WallWidthDirection));
    float Budget=Distance/WallPushbackDuration*PushDelta;
    const float NormalRemaining=float(FVector::DotProduct(Remaining,WallNormal));
    const float WidthRemaining=float(FVector::DotProduct(Remaining,WallWidthDirection));
    float AlongNormal=0.f,AlongWidth=0.f;
    if(Entry.bWidthFirst)
    {
     AlongWidth=FMath::Clamp(WidthRemaining,-Budget,Budget);
     Budget=FMath::Max(0.f,Budget-FMath::Abs(AlongWidth));
     AlongNormal=FMath::Clamp(NormalRemaining,-Budget,Budget);
    }
    else
    {
     AlongNormal=FMath::Clamp(NormalRemaining,-Budget,Budget);
     Budget=FMath::Max(0.f,Budget-FMath::Abs(AlongNormal));
     AlongWidth=FMath::Clamp(WidthRemaining,-Budget,Budget);
    }
    Next+=WallNormal*AlongNormal+WallWidthDirection*AlongWidth;
   }
   Current.Add(Here);Proposed.Add(Next);
  }
  if(Elapsed>=BarrierIntroDuration&&!WallPushbacks.IsEmpty())
  {
   const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
   const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
   bool bStillLegal=Layout!=nullptr;
   for(int32 I=0;bStillLegal&&I<Group.Num();++I)if(auto* Other=Group[I])
   {
    if(!IEGCombatant::Execute_IsAlive(Other))continue;
    const auto& Entry=WallPushbacks[I];const FTransform Basis=Layout->GetActorTransform();
    const FVector Local=Basis.InverseTransformPosition(Entry.End);
    const float RY=UEGCombatComponent::BlockingRadius(Other)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size());
    const float Bottom=-Layout->Rows*Layout->CellDepth*.5f;
    const float Top=Bottom+FMath::Clamp(Layout->PlayerRows,1,Layout->Rows)*Layout->CellDepth;
    bStillLegal=Local.Y-RY>=Bottom-.1f&&Local.Y+RY<=Top+.1f&&
     Other->GetCombat()->IsBoardLocationFree(Entry.End,nullptr,nullptr,true,&Group);
    for(int32 J=0;bStillLegal&&J<I;++J)if(auto* Previous=Group[J])
     if(IEGCombatant::Execute_IsAlive(Previous))
     {
      const float Radius=UEGCombatComponent::BlockingRadius(Other)+UEGCombatComponent::BlockingRadius(Previous);
      bStillLegal=FVector::Dist2D(Entry.End,WallPushbacks[J].End)+.01f>=Radius;
     }
   }
   if(!bStillLegal)
   {UE_LOG(LogTemp,Warning,TEXT("Corruption wall: formation changed during transition; recovering encounter"));RecoverBarrierPushback();return;}
  }
  const auto PairSafe=[](FVector A,FVector B,FVector NewA,FVector NewB,float Radius)
  {
   FVector Offset=A-B,Travel=(NewA-A)-(NewB-B);Offset.Z=Travel.Z=0.f;
   const double StartDistance=Offset.SizeSquared();
   const double Required=FMath::Square(FMath::Max(0.f,Radius-.01f));
   if(StartDistance<Required)
    return FVector::DotProduct(Offset,Travel)>=0.&&FVector::DistSquared2D(NewA,NewB)>=StartDistance;
   const double TravelSq=Travel.SizeSquared();
   const double T=TravelSq>SMALL_NUMBER?FMath::Clamp(-double(FVector::DotProduct(Offset,Travel))/TravelSq,0.,1.):0.;
   return (Offset+Travel*T).SizeSquared()>=Required;
  };
  for(int32 I=0;I<Group.Num();++I)if(auto* Other=Group[I])
   if(!Other->GetCombat()->IsBoardPathClear(Current[I],Proposed[I],nullptr,nullptr,nullptr,nullptr,&Group,true))Proposed[I]=Current[I];
  // Each pass only freezes candidates, so at most N changes per pass and
  // the loop terminates without inventing a second movement authority.
  bool bChanged=true;
  for(int32 Pass=0;bChanged&&Pass<Group.Num()+1;++Pass)
  {
   bChanged=false;
   for(int32 I=0;I<Group.Num();++I)for(int32 J=0;J<I;++J)
   {
    auto* A=Group[I];auto* B=Group[J];if(!IsValid(A)||!IsValid(B))continue;
    const float Radius=UEGCombatComponent::BlockingRadius(A)+UEGCombatComponent::BlockingRadius(B);
    if(PairSafe(Current[I],Current[J],Proposed[I],Proposed[J],Radius))continue;
    const bool bCanMoveA=PairSafe(Current[I],Current[J],Proposed[I],Current[J],Radius);
    const bool bCanMoveB=PairSafe(Current[I],Current[J],Current[I],Proposed[J],Radius);
    if(bCanMoveA&&!Proposed[J].Equals(Current[J],.001f)){Proposed[J]=Current[J];bChanged=true;}
    else if(bCanMoveB&&!Proposed[I].Equals(Current[I],.001f)){Proposed[I]=Current[I];bChanged=true;}
    else
    {
     if(!Proposed[I].Equals(Current[I],.001f)){Proposed[I]=Current[I];bChanged=true;}
     if(!Proposed[J].Equals(Current[J],.001f)){Proposed[J]=Current[J];bChanged=true;}
    }
   }
  }
  if(PushDelta>SMALL_NUMBER)for(int32 I=0;I<Group.Num();++I)
   if(IsValid(Group[I])&&Proposed[I].Equals(Current[I],.001f)&&
      FMath::Abs(FVector::DotProduct(WallPushbacks[I].End-Current[I],WallWidthDirection))>.1f)
    WallPushbacks[I].bWidthFirst=!WallPushbacks[I].bWidthFirst;
  // When an axis-aligned move is blocked, try a bounded local step toward
  // the destination at several angles. Narrow passages between circular
  // bases can require simultaneous lateral and backward movement. Every
  // candidate is swept against the same live group and battle obstacles.
  if(PushDelta>SMALL_NUMBER)for(int32 I=0;I<Group.Num();++I)
  {
   auto* Moving=Group[I];
   if(!IsValid(Moving)||!IEGCombatant::Execute_IsAlive(Moving)||
      !Proposed[I].Equals(Current[I],.001f)||Current[I].Equals(WallPushbacks[I].End,.1f))continue;
   const FVector Whole=WallPushbacks[I].End-WallPushbacks[I].Start;
   const float TravelBudget=(FMath::Abs(FVector::DotProduct(Whole,WallNormal))+
    FMath::Abs(FVector::DotProduct(Whole,WallWidthDirection)))/WallPushbackDuration*PushDelta;
   const float GoalDistance=FVector::Dist2D(Current[I],WallPushbacks[I].End);
   const float MaxStep=FMath::Min3(40.f,TravelBudget,GoalDistance);
   const FVector GoalDirection=(WallPushbacks[I].End-Current[I]).GetSafeNormal2D();
   float BestProgress=.01f;FVector BestCandidate=Current[I];
   if(MaxStep>SMALL_NUMBER&&!GoalDirection.IsNearlyZero())
    for(const float Fraction:{1.f,.5f,.25f,.125f})for(int32 AngleIndex=0;AngleIndex<24;++AngleIndex)
    {
     const FVector Candidate=Current[I]+GoalDirection.RotateAngleAxis(15.f*AngleIndex,FVector::UpVector)*(MaxStep*Fraction);
     const float Progress=GoalDistance-FVector::Dist2D(Candidate,WallPushbacks[I].End);
     if(Progress<=BestProgress)continue;
     bool bSafe=true;
     for(int32 J=0;bSafe&&J<Group.Num();++J)if(J!=I&&IsValid(Group[J]))
     {
      const float Radius=UEGCombatComponent::BlockingRadius(Moving)+UEGCombatComponent::BlockingRadius(Group[J]);
      bSafe=PairSafe(Current[I],Current[J],Candidate,Proposed[J],Radius);
     }
     if(!bSafe||!Moving->GetCombat()->IsBoardPathClear(Current[I],Candidate,nullptr,nullptr,nullptr,nullptr,&Group,true))continue;
     BestProgress=Progress;BestCandidate=Candidate;
    }
   if(!BestCandidate.Equals(Current[I],.001f)){Proposed[I]=BestCandidate;continue;}
   // Keep the small escape step for a deadlock where every route initially
   // moves away from the goal; its complete segment is checked below.
   float Nearest=TNumericLimits<float>::Max(),Away=1.f;
   for(int32 J=0;J<Group.Num();++J)if(J!=I&&IsValid(Group[J])&&IEGCombatant::Execute_IsAlive(Group[J]))
   {
    const float Distance=FVector::DistSquared2D(Current[I],Current[J]);
    if(Distance<Nearest)
    {
     Nearest=Distance;
     Away=FVector::DotProduct(Current[I]-Current[J],WallWidthDirection)<0.f?-1.f:1.f;
    }
   }
   for(const float Direction:{Away,-Away})
   {
    const FVector Candidate=Current[I]+WallWidthDirection*(2.f*Direction);
    if(!Moving->GetCombat()->IsBoardPathClear(Current[I],Candidate,nullptr,nullptr,nullptr,nullptr,&Group,true))continue;
    bool bSafe=true;
    for(int32 J=0;bSafe&&J<Group.Num();++J)if(J!=I&&IsValid(Group[J]))
    {
     const float Radius=UEGCombatComponent::BlockingRadius(Moving)+UEGCombatComponent::BlockingRadius(Group[J]);
     bSafe=PairSafe(Current[I],Current[J],Candidate,Proposed[J],Radius);
    }
    if(bSafe){Proposed[I]=Candidate;break;}
   }
  }
  if(Elapsed>=BarrierIntroDuration)
  {
   bool bNeedsFallback=false;
   for(int32 I=0;I<Group.Num();++I)
    if(IsValid(Group[I])&&IEGCombatant::Execute_IsAlive(Group[I])&&!Proposed[I].Equals(WallPushbacks[I].End,.1f))
    {bNeedsFallback=true;break;}
   const auto* DiagnosticMode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
   const auto* DiagnosticLayout=DiagnosticMode&&DiagnosticMode->GetBoard()?DiagnosticMode->GetBoard()->LayoutSource.Get():nullptr;
   const FTransform DiagnosticBasis=DiagnosticLayout?DiagnosticLayout->GetActorTransform():FTransform::Identity;
   if(bNeedsFallback)for(int32 I=0;I<Group.Num();++I)if(auto* Other=Group[I])
   {
    const auto& Entry=WallPushbacks[I];const AEGChessUnit* Nearest=nullptr;float Distance=MAX_flt;
    const AEGChessUnit* DirectBlocker=nullptr;
    for(int32 J=0;J<Group.Num();++J)if(J!=I&&IsValid(Group[J]))
    {
     const float D=FVector::Dist2D(Current[I],Current[J]);
     if(D<Distance){Distance=D;Nearest=Group[J];}
     const float R=UEGCombatComponent::BlockingRadius(Other)+UEGCombatComponent::BlockingRadius(Group[J]);
     if(!DirectBlocker&&!PairSafe(Current[I],Current[J],Entry.End,Current[J],R))DirectBlocker=Group[J];
    }
    const AEGChessUnit* StaticBlocker=nullptr;const TCHAR* Reason=TEXT("Clear");
    Other->GetCombat()->IsBoardPathClear(Current[I],Entry.End,nullptr,nullptr,&StaticBlocker,&Reason,&Group,true);
    FVector Reserved,Pending;Other->GetCombat()->GetBoardLandingCoordinates(Reserved,Pending);
    UE_LOG(LogTemp,Display,TEXT("CorruptionPushbackDiagnostic actor=%s definition=%s start_local=%s current_local=%s target_local=%s radius=%.3f fallback=%d in_flight=%d flight_actor=%d reserved=%d pending=%d reserved_local=%s pending_local=%s nearest=%s nearest_distance=%.3f nearest_radius=%.3f direct_blocker=%s path_reason=%s static_blocker=%s"),
     *GetNameSafe(Other),*GetNameSafe(Other->GetDefinition()),*DiagnosticBasis.InverseTransformPosition(Entry.Start).ToCompactString(),
     *DiagnosticBasis.InverseTransformPosition(Current[I]).ToCompactString(),*DiagnosticBasis.InverseTransformPosition(Entry.End).ToCompactString(),
     UEGCombatComponent::BlockingRadius(Other),Proposed[I].Equals(Entry.End,.1f)?0:1,Other->GetHeroSkill()->bInFlight?1:0,Entry.OriginalFlight?1:0,
     Other->GetCombat()->IsBoardLandingReserved()?1:0,Other->GetCombat()->IsBoardLandingPending()?1:0,
     *DiagnosticBasis.InverseTransformPosition(Reserved).ToCompactString(),*DiagnosticBasis.InverseTransformPosition(Pending).ToCompactString(),
     *GetNameSafe(Nearest),Distance,Nearest?UEGCombatComponent::BlockingRadius(Nearest):0.f,*GetNameSafe(DirectBlocker),Reason,*GetNameSafe(StaticBlocker));
   }
  }
  bool bAllArrived=true;
  for(int32 I=0;I<WallPushbacks.Num();++I)
  {
   auto& Entry=WallPushbacks[I];auto* Other=Group[I];
   if(!IsValid(Other)||!IEGCombatant::Execute_IsAlive(Other))continue;
   if(Elapsed>=BarrierIntroDuration&&!Proposed[I].Equals(Entry.End,.1f))
   {Proposed[I]=Entry.End;++BarrierPushbackFallbackCount;}
   if(!Proposed[I].Equals(Entry.End,.1f))bAllArrived=false;
   if(auto* Flight=Entry.Flight.Get())
   {
    if(!Flight->RebaseThrownSummon(Proposed[I],Entry.Landing))
    {UE_LOG(LogTemp,Warning,TEXT("Corruption wall: flight changed during transition; recovering encounter"));RecoverBarrierPushback();return;}
   }
   else if(Entry.OriginalFlight&&Other->GetHeroSkill()->bInFlight)
   {UE_LOG(LogTemp,Warning,TEXT("Corruption wall: flight disappeared during transition; recovering encounter"));RecoverBarrierPushback();return;}
   else Other->SetActorLocation(Proposed[I],false,nullptr,ETeleportType::TeleportPhysics);
  }
  if(bAllArrived&&!WallPushbacks.IsEmpty())
  {
   if(IsValid(BarrierActor))BarrierActor->SetBlocking(true);
   if(Elapsed>=BarrierIntroDuration)
   {
    if(BarrierPushbackFallbackCount>0)UE_LOG(LogTemp,Warning,TEXT("Corruption wall: %d obstructed pushbacks used final placement fallback"),BarrierPushbackFallbackCount);
    WallPushbacks.Reset();
   }
  }
  if(IsValid(BarrierVisual))BarrierVisual->SetIntroElapsed(Elapsed);
  if(!bBarrierOpenSoundPlayed&&Elapsed>=(bBarrierIntroUsesSourcePose?BarrierOpenDelay:0.f))
  {UEGGameAudioComponent::PlayBossEffect(Unit,Battle.Get(),EEGBossEffectSound::BarrierOpen,BarrierBlockToken);bBarrierOpenSoundPlayed=true;}
  BarrierIntroRemaining=FMath::Max(0.f,BarrierIntroDuration-Elapsed);
  if(BarrierIntroRemaining<=0.f)
  {
   Unit->GetHeroSkill()->EndBarrierTransition(RoundId,BarrierBlockToken);bBarrierIntroUsesSourcePose=false;
   if(IsValid(BarrierVisual))BarrierVisual->SetPresentationToken(FGuid());
   Battle->SetPresentationHold(BarrierBlockToken,false);
  }
  return;
 }
 if(GetWorld()&&GetWorld()->IsPaused())return;
 if(!Battle->IsCombatRunning())return;
 if(bBoss&&bPhaseTwo&&!bPhaseShieldPending&&GetPhaseShieldRemaining()<=0&&ShieldVisual){ShieldVisual->DestroyComponent();ShieldVisual=nullptr;}
 const auto* VillV=Unit->FindComponentByClass<UEGVillVMechanismComponent>();
 if(bCopy&&Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::VillV&&(!VillV||!VillV->HasActiveDevices()))
 {
  FallbackAttackClock+=Delta;
  const float Interval=1.f/FMath::Max(.1f,Unit->GetAttributes()->GetAttackSpeed());
  if(FallbackAttackClock>=Interval)
  {
   auto* Target=Unit->GetCombat()->FindNearestEnemy();
   if(Target){FallbackAttackClock=0.f;Battle->Launch(Unit,Target,Target->GetActorLocation(),Unit->GetAttributes()->GetAttackPower(),EEGProjectileKind::BasicAttack,FGuid::NewGuid());}
  }
 }
 if(!bBoss)return;
 if(BossPhase==EEGCorruptionBossPhase::Barrier)
 {
  BarrierRemaining=FMath::Max(0.f,BarrierRemaining-Delta);
  if(BarrierHealth<=0.f)FinishBarrier(true);else if(BarrierRemaining<=0.f)FinishBarrier(false);
  return;
 }
 if(BossPhase!=EEGCorruptionBossPhase::Second)return;
 WeaknessRemaining=FMath::Max(0.f,WeaknessRemaining-Delta);
 auto* Target=Unit->GetCombat()->GetTarget();if(!Unit->GetCombat()->IsValidEnemy(Target))Target=Unit->GetCombat()->FindNearestEnemy();
 const float Step=FMath::Max(0.f,Delta);
 SpreadClock+=Step;MimicClock+=Step;
 if(ActiveBossCast!=EEGCorruptionBossCast::None)ActiveBossCastElapsed=FMath::Min(ActiveBossCastDuration,ActiveBossCastElapsed+Step);
 if(bJustCompletedSpecialSkill)bJustCompletedSpecialSkill=false;
 else SpecialSkillGapRemaining=FMath::Max(0.f,SpecialSkillGapRemaining-Step);
 MimicRecoveryRemaining=FMath::Max(0.f,MimicRecoveryRemaining-Step);
 if(MimicDelay>0.f)
 {
  MimicDelay=FMath::Max(0.f,MimicDelay-Step);
  if(MimicDelay<=0.f&&Target)
  {
   const FGuid MimicAction=ActiveBossCastAction;
   if(ActiveBossCast!=EEGCorruptionBossCast::Mimic||!MimicAction.IsValid())return;
   MimicRecoveryRemaining=BossMimicRecoverySeconds;
   SpecialSkillGapRemaining=SafeRule(SpecialSkillGapSeconds,1.f,1.f);bJustCompletedSpecialSkill=true;
   if(!IsCurrentBattle()||!Battle->IsCombatRunning()||BossPhase!=EEGCorruptionBossPhase::Second||
      !IEGCombatant::Execute_IsAlive(Unit)||!Unit->GetCombat()->IsValidEnemy(Target))return;
   const FGuid CastRound=RoundId;
   const TWeakObjectPtr<UEGCorruptionCombatComponent> WeakSelf(this);
   const TWeakObjectPtr<AEGChessUnit> WeakUnit(Unit);
   const auto IsMimicCurrent=[WeakSelf,WeakUnit,CastRound,MimicAction]()
   {
    const auto* Self=WeakSelf.Get();const auto* Caster=WeakUnit.Get();
    return Self&&IsValid(Caster)&&IEGCombatant::Execute_IsAlive(Caster)&&Self->IsCurrentBattle()&&
     Self->Battle->IsCombatRunning()&&Self->RoundId==CastRound&&Self->BossPhase==EEGCorruptionBossPhase::Second&&
     Self->ActiveBossCast==EEGCorruptionBossCast::Mimic&&Self->ActiveBossCastAction==MimicAction;
   };
   const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
   const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
   const float Cell=Layout?Layout->CellWidth*Layout->GetActorScale3D().X:250.f;
   const float Attack=Unit->GetAttributes()->GetAttackPower()*SafeRule(MimicDamageMultiplier,1.f);
   const FGuid Id=MimicAction;
   const FVector MimicTargetPoint=Target->GetActorLocation();
   if(!IsMimicCurrent())return;
   const auto MimicArea=[&](float Radius,float Raw)
   {
    if(!IsMimicCurrent()||!Unit->GetCombat()->IsValidEnemy(Target))return;
    const FVector Point=Target->GetActorLocation();const auto Victims=Battle->Units();TSet<AEGChessUnit*> Visited;
    for(auto* Victim:Victims)
    {
     if(!IsMimicCurrent())return;
     if(!Unit->GetCombat()->IsValidEnemy(Victim)||Visited.Contains(Victim)||
        EGCombatGeometry::PointDistanceSquared(Point,Victim)>FMath::Square(Radius))continue;
     Visited.Add(Victim);Battle->Damage(Unit,Victim,Raw,Id);
    }
    if(IsMimicCurrent())Battle->Visual(Unit,Point-FVector(0,0,85),Radius,true);
   };
   bool bMimicShieldApplied=false;
   switch(MimicKind)
   {
   case EEGHeroKind::Griseo:bMimicShieldApplied=Unit->GetHeroSkill()->GrantSourceShield(TEXT("CorruptionMimic"),OpeningMaxHealth*.08f,4.f);break;
   case EEGHeroKind::Aponia:
   {
    const auto Victims=Battle->Units();
    for(auto* Victim:Victims)
    {
     if(!IsMimicCurrent())break;
     if(Unit->GetCombat()->IsValidEnemy(Victim))Battle->Damage(Unit,Victim,Attack*.45f,Id);
    }
    break;
   }
   case EEGHeroKind::Sakura: MimicArea(Cell*.6f,Attack*1.1f);break;
   case EEGHeroKind::Mobius: MimicArea(Cell,Attack*.8f);break;
   case EEGHeroKind::Hua: MimicArea(Cell*.8f,Attack*.9f);break;
   case EEGHeroKind::Eden: MimicArea(Cell*.8f,Attack*.65f);if(IsMimicCurrent())Unit->RestoreMana(10.f);break;
   case EEGHeroKind::VillV: MimicArea(Cell*.7f,Attack*.9f);break;
   case EEGHeroKind::Pardofelis: MimicArea(Cell*.65f,Attack*.8f);break;
   case EEGHeroKind::Elysia: MimicArea(Cell,Attack*.8f);break;
   default: MimicArea(Cell*.75f,Attack*.8f);break;
   }
   if(!IsMimicCurrent())return;
   if(auto* Phantom=CurrentPhantom.Get())
   {
    // Radius reflects only attacks with an actual area. A zero radius is a
    // single target/all-field/shield cue, not a claimed damage boundary.
    float CueRadius=0.f;
    switch(MimicKind)
    {
    case EEGHeroKind::Sakura:CueRadius=Cell*.6f;break;
    case EEGHeroKind::Mobius:case EEGHeroKind::Elysia:CueRadius=Cell;break;
    case EEGHeroKind::Hua:case EEGHeroKind::Eden:CueRadius=Cell*.8f;break;
    case EEGHeroKind::VillV:CueRadius=Cell*.7f;break;
    case EEGHeroKind::Pardofelis:CueRadius=Cell*.65f;break;
    case EEGHeroKind::Aponia:case EEGHeroKind::Griseo:break;
    default:CueRadius=Cell*.75f;break;
    }
    const auto Cue=MimicKind==EEGHeroKind::Griseo?EEGCorruptionMimicCue::SelfShield:
     MimicKind==EEGHeroKind::Aponia?EEGCorruptionMimicCue::AllTargets:EEGCorruptionMimicCue::Area;
    Phantom->NotifyCast(MimicKind==EEGHeroKind::Griseo?Unit->GetActorLocation():MimicTargetPoint,CueRadius,Cue,
     bMimicShieldApplied&&Unit->GetHeroSkill()->GetSourceShield(TEXT("CorruptionMimic"))>0.f);
   }
   else if(MimicKind==EEGHeroKind::Generic&&IsValid(BossVisual))
    BossVisual->ShowMarkBurst(MimicTargetPoint,Cell*.75f);
   if(IsMimicCurrent())EndBossCast(EEGCorruptionBossCastEvent::Resolved);
  }
  else if(MimicDelay<=0.f)EndBossCast(EEGCorruptionBossCastEvent::Cancelled);
 }
 if(!IsCurrentBattle()||BossPhase!=EEGCorruptionBossPhase::Second)return;
 const bool bReady=Target&&Unit->GetCombat()->CanAct()&&!VisualSeed.IsValid()&&MimicDelay<=0.f&&MimicRecoveryRemaining<=0.f&&
  SpecialSkillGapRemaining<=0.f&&ActiveBossCast==EEGCorruptionBossCast::None;
 const bool bMimicDue=bFirstSpreadResolved&&(!bMimicStarted||MimicClock>=SafeRule(MimicIntervalSeconds,10.f,.1f));
 if(bReady&&!bMimicDue&&SpreadClock>=SafeRule(SpreadIntervalSeconds,7.f,.1f))
 {
  if(AEGCorruptionSeed::Spawn(Battle.Get(),Unit,Target->GetActorLocation(),Unit->GetAttributes()->GetAttackPower()*SafeRule(SpreadDamageMultiplier,1.1f),BossSpreadWarningSeconds,.8f,1))SpreadClock=0.f;
  else SpreadClock=SafeRule(SpreadIntervalSeconds,7.f,.1f)-.25f;
  return;
 }
 if(bReady&&bMimicDue)
 {
  MimicClock=0.f;MimicKind=EEGHeroKind::Generic;
  if(CurrentPhantom.IsValid())CurrentPhantom->Destroy();CurrentPhantom.Reset();
  TArray<AEGChessUnit*> Choices;
  for(auto* Other:Battle->Units())if(IsValid(Other)&&Other!=Unit&&IEGCombatant::Execute_IsAlive(Other)&&
   IEGCombatant::Execute_GetFaction(Other)==EEGFaction::Player&&Other->GetDefinition()&&!Other->GetHeroSkill()->bSummon&&
   !Other->IsAuxiliaryEmplacement()&&Other->GetDefinition()->HeroKind!=EEGHeroKind::ElysiaHerrscher)Choices.Add(Other);
  const bool bDifferentKindAvailable=Choices.ContainsByPredicate([this](const AEGChessUnit* Candidate)
   {return Candidate&&Candidate->GetDefinition()&&BaseMimicKind(Candidate->GetDefinition()->HeroKind)!=LastMimicKind;});
  if(bDifferentKindAvailable)Choices.RemoveAll([this](const AEGChessUnit* Candidate)
   {return Candidate&&Candidate->GetDefinition()&&BaseMimicKind(Candidate->GetDefinition()->HeroKind)==LastMimicKind;});
  const UEGHeroDefinition* MimicBase=nullptr;
  if(!Choices.IsEmpty())
  {
   auto* Chosen=Choices[FMath::RandRange(0,Choices.Num()-1)];LastMimic=Chosen;
   MimicKind=BaseMimicKind(Chosen->GetDefinition()->HeroKind);LastMimicKind=MimicKind;
   MimicBase=Chosen->GetDefinition();
   if(const auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>();Mode&&Mode->RunDefinition)
    for(const auto& Card:Mode->RunDefinition->CardPool)if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==MimicKind){MimicBase=Card->HeroDefinition;break;}
  }
  MimicDelay=BossMimicWindupSeconds;bMimicStarted=true;
  const FGuid CastRound=RoundId;
  const TWeakObjectPtr<UEGCorruptionCombatComponent> WeakSelf(this);
  BeginBossCast(EEGCorruptionBossCast::Mimic,BossMimicWindupSeconds);
  if(!WeakSelf.IsValid()||!IsCurrentBattle()||RoundId!=CastRound||BossPhase!=EEGCorruptionBossPhase::Second||
     ActiveBossCast!=EEGCorruptionBossCast::Mimic||!ActiveBossCastAction.IsValid()||!IEGCombatant::Execute_IsAlive(Unit))return;
  if(IsValid(MimicBase))
   CurrentPhantom=AEGCorruptionPhantomVisual::Spawn(Unit,MimicBase,Unit->GetActorLocation()+FVector(0,150,0),Unit->GetActorRotation(),
    ActiveBossCastDuration,ActiveBossCastAction);
 }
}
void UEGCorruptionCombatComponent::EndCombat()
{
 EndBossCast(EEGCorruptionBossCastEvent::Cancelled);
 if(bBoss)UEGGameAudioComponent::StopBossEffects(GetOwner(),Battle.Get());
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))if(Unit->GetHeroSkill())Unit->GetHeroSkill()->EndBarrierTransition(RoundId,BarrierBlockToken);
 bBarrierIntroUsesSourcePose=false;bBarrierOpenSoundPlayed=false;BarrierIntroRemaining=BarrierIntroDuration=0.f;WallPushbacks.Reset();
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))
 {Unit->OnAttributeChanged.RemoveDynamic(this,&ThisClass::AttributeChanged);Unit->OnDied.RemoveDynamic(this,&ThisClass::BossDied);}
 if(Battle.IsValid()&&BarrierBlockToken.IsValid())Battle->SetPresentationHold(BarrierBlockToken,false);
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->GetCombat()->RemoveExternalActionBlock(BarrierBlockToken);
 BarrierBlockToken.Invalidate();if(IsValid(BarrierVisual))BarrierVisual->Destroy();BarrierVisual=nullptr;
 if(IsValid(BarrierActor))BarrierActor->Destroy();BarrierActor=nullptr;
 VisualSeed.Reset();if(IsValid(BossVisual))BossVisual->Destroy();BossVisual=nullptr;
 if(CurrentPhantom.IsValid())CurrentPhantom->Destroy();CurrentPhantom.Reset();
 BossPhase=EEGCorruptionBossPhase::Finished;MarkStacks.Reset();MarkedActions.Reset();LastMimic.Reset();
 if(ShieldVisual){ShieldVisual->DestroyComponent();ShieldVisual=nullptr;}
 if(auto* Unit=Cast<AEGChessUnit>(GetOwner()))Unit->GetHeroSkill()->RemoveSourceShield(BossPhaseShield);
 bPhaseShieldPending=false;PendingPhaseShield=0.f;
 MimicDelay=MimicRecoveryRemaining=SpecialSkillGapRemaining=0.f;
 bFirstSpreadResolved=bMimicStarted=bJustCompletedSpecialSkill=false;
 OnBossCastEvent.Clear();OnPhaseTwo.Clear();Battle.Reset();RoundId.Invalidate();UltimateActions.Reset();UltimateActionOrder.Reset();
}
void UEGCorruptionCombatComponent::EndPlay(const EEndPlayReason::Type Reason)
{EndCombat();Super::EndPlay(Reason);}
