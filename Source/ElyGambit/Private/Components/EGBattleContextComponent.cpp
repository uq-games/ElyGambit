#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGUltimateRingComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Actor/EGCorruptionSeed.h"
#include "Components/EGCombatGeometry.h"
#include "Components/EGHitFeedbackComponent.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/Effects/EGDamageEffect.h"
#include "AbilitySystemComponent.h"
#include "EGGameplayTags.h"
#include "Presentation/EGSkillProjectile.h"
#include "Components/StaticMeshComponent.h"
#include "Presentation/EGPresentationBurst.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGSourceSummonDefinition.h"
#include "Presentation/EGSourceEffect.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Game/EGCardManager.h"
#include "AbilitySystem/Effects/EGTeamSpeedEffect.h"
#include "AbilitySystem/Effects/EGEdenManaEffect.h"
#include "GameFramework/PlayerController.h"
#include "Components/SkeletalMeshComponent.h"
UEGBattleContextComponent::UEGBattleContextComponent(){PrimaryComponentTick.bCanEverTick=true;}
bool UEGBattleContextComponent::BeginHumanEntryPause(AEGChessUnit* Unit,FGuid Round,FGuid Action)
{
 auto* World=GetWorld();
 if(!World||World->GetNetMode()!=NM_Standalone||World->IsPaused()||bHumanPauseOwned||bHumanPausePending||!bActive||Round!=RoundId||!Action.IsValid()||
    !IsValid(Unit)||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit))return false;
 auto* PC=World->GetFirstPlayerController();
 auto* Mesh=Unit->GetVisiblePresentationMesh();auto* Skill=Unit->GetHeroSkill();
 if(!World->GetAuthGameMode<AEGChessGameMode>()||!PC||!PC->PlayerState||!Mesh||!Skill||!Skill->IsActionRunning()||Skill->ActionId!=Action)return false;
 HumanPauseUnit=Unit;HumanPauseController=PC;HumanPauseMesh=Mesh;HumanPauseSkill=Skill;
 HumanPauseRound=Round;HumanPauseAction=Action;bHumanPausePending=true;
 // The world captures IsPaused before actor ticks. Acquiring in that tick's
 // middle would let a subset of units complete another frame. Queue the pause
 // at the next world's tick start, before the engine snapshots pause state.
 HumanPausePendingDelegate=FWorldDelegates::OnWorldTickStart.AddUObject(this,&ThisClass::OnHumanPauseWorldTick);
 return true;
}
void UEGBattleContextComponent::OnHumanPauseWorldTick(UWorld* World,ELevelTick TickType,float DeltaSeconds)
{
 if(World!=GetWorld()||TickType!=LEVELTICK_All||!bHumanPausePending)return;
 FWorldDelegates::OnWorldTickStart.Remove(HumanPausePendingDelegate);HumanPausePendingDelegate.Reset();bHumanPausePending=false;
 auto* Unit=HumanPauseUnit.Get();auto* PC=HumanPauseController.Get();
 auto* Mesh=HumanPauseMesh.Get();auto* Skill=HumanPauseSkill.Get();
 if(!World||!World->GetAuthGameMode<AEGChessGameMode>()||World->IsPaused()||!bActive||HumanPauseRound!=RoundId||!IsValid(Unit)||Unit->IsDeathPending()||
    !IEGCombatant::Execute_IsAlive(Unit)||!PC||!PC->PlayerState||!Mesh||!Skill||!Skill->IsActionRunning()||Skill->ActionId!=HumanPauseAction)
 {
  HumanPauseUnit.Reset();HumanPauseController.Reset();HumanPauseMesh.Reset();HumanPauseSkill.Reset();
  HumanPauseRound.Invalidate();HumanPauseAction.Invalidate();return;
 }
 HumanPauseStartedReal=FPlatformTime::Seconds();
 bHumanPauseContextTick=PrimaryComponentTick.bTickEvenWhenPaused;
 bHumanPauseMeshTick=Mesh->PrimaryComponentTick.bTickEvenWhenPaused;
 bHumanPauseSkillTick=Skill->PrimaryComponentTick.bTickEvenWhenPaused;
 bHumanPauseOwned=true;
 const FCanUnpause Unpause=FCanUnpause::CreateUObject(this,&ThisClass::CanReleaseHumanEntryPause);
 HumanPauseRequestHandle=Unpause.GetHandle();
 if(!PC->SetPause(true,Unpause)||!World->IsPaused())
 {
  EndHumanEntryPause(HumanPauseRound,HumanPauseAction);return;
 }
 PrimaryComponentTick.bTickEvenWhenPaused=true;
 Mesh->PrimaryComponentTick.bTickEvenWhenPaused=true;
 Skill->PrimaryComponentTick.bTickEvenWhenPaused=true;
}
void UEGBattleContextComponent::EndHumanEntryPause(FGuid Round,FGuid Action)
{
 if((!bHumanPauseOwned&&!bHumanPausePending)||Round!=HumanPauseRound||Action!=HumanPauseAction)return;
 if(bHumanPausePending)
 {
  FWorldDelegates::OnWorldTickStart.Remove(HumanPausePendingDelegate);HumanPausePendingDelegate.Reset();bHumanPausePending=false;
  HumanPauseUnit.Reset();HumanPauseController.Reset();HumanPauseMesh.Reset();HumanPauseSkill.Reset();
  HumanPauseRound.Invalidate();HumanPauseAction.Invalidate();return;
 }
 bHumanPauseOwned=false;
 for(const auto& Item:HumanPauseVisualTicks)if(auto* Visual=Item.Actor.Get())Visual->PrimaryActorTick.bTickEvenWhenPaused=Item.bOriginal;
 HumanPauseVisualTicks.Reset();
 if(auto* Mesh=HumanPauseMesh.Get())Mesh->PrimaryComponentTick.bTickEvenWhenPaused=bHumanPauseMeshTick;
 if(auto* Skill=HumanPauseSkill.Get())Skill->PrimaryComponentTick.bTickEvenWhenPaused=bHumanPauseSkillTick;
 PrimaryComponentTick.bTickEvenWhenPaused=bHumanPauseContextTick;
 auto* World=GetWorld();
 if(World)if(auto* GameMode=World->GetAuthGameMode<AEGChessGameMode>())GameMode->ReleasePauseRequest(HumanPauseRequestHandle);
 HumanPauseUnit.Reset();HumanPauseController.Reset();HumanPauseMesh.Reset();HumanPauseSkill.Reset();
 HumanPauseRound.Invalidate();HumanPauseAction.Invalidate();HumanPauseRequestHandle.Reset();HumanPauseStartedReal=0.;
}
bool UEGBattleContextComponent::IsHumanEntryPauseOwner(const AEGChessUnit* Unit,FGuid Action) const
{return bHumanPauseOwned&&HumanPauseUnit.Get()==Unit&&HumanPauseRound==RoundId&&HumanPauseAction==Action;}
void UEGBattleContextComponent::PermitHumanEntryVisualTick(AActor* Visual,FGuid Round,FGuid Action)
{
 if(!bHumanPauseOwned||Round!=HumanPauseRound||Action!=HumanPauseAction||!IsValid(Visual))return;
 for(const auto& Item:HumanPauseVisualTicks)if(Item.Actor.Get()==Visual)return;
 HumanPauseVisualTicks.Add({Visual,!!Visual->PrimaryActorTick.bTickEvenWhenPaused});
 Visual->PrimaryActorTick.bTickEvenWhenPaused=true;
}
TArray<AEGChessUnit*> UEGBattleContextComponent::Units() const
{TArray<AEGChessUnit*> R;if(Mode.IsValid())for(const auto& U:Mode->GetBattleUnits())if(U.IsValid())R.Add(U.Get());return R;}
bool UEGBattleContextComponent::SetEnemyDamageCorruptionSnapshot(int32 Corruption,bool bEnabled)
{
 if(!bActive||bOpeningStarted||!GetOwner()||!GetOwner()->HasAuthority())return false;
 EnemyDamageMultiplier=!bEnabled?1.f:Corruption<40?.95f:Corruption>70?1.05f:1.f;
 return true;
}
void UEGBattleContextComponent::Start(const TArray<FEGHeroInstance>& Heroes)
{
 Stop();Mode=Cast<AEGChessGameMode>(GetOwner());RoundId=FGuid::NewGuid();bActive=true;
 SummonBonus=AttackCount=0;
 SuccessfulUltimateActions.Reset();FirstUltimateBattleSeconds=-1.f;CombatStartedAtWorld=0.;
 GoldRecipients.Reset();AwardedGold=0;

 for(const auto& H:Heroes)if(H.bDeployed&&H.Definition)
 {
  DeployedIds.Add(H.InstanceId);

 }
 if(!Stigmata){Stigmata=NewObject<UEGStigmataBattleComponent>(GetOwner());GetOwner()->AddInstanceComponent(Stigmata);Stigmata->RegisterComponent();}
 Stigmata->Start(this,Heroes);
 for(auto* U:Units())
 {
  FEGHeroInstance H;const bool Hero=IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player&&Mode->GetRoster()->GetHero(U->GetInstanceId(),H);
  const int32 Level=Hero?H.Level:U->GetHeroSkill()->GetLevel();const bool Awakened=Hero&&H.bAwakened;
  const auto Kind=U->GetDefinition()?U->GetDefinition()->HeroKind:EEGHeroKind::Generic;
  if(Kind==EEGHeroKind::Griseo||Kind==EEGHeroKind::GriseoAdult||Kind==EEGHeroKind::VillV||Kind==EEGHeroKind::ElysiaHerrscher)
  {
   TWeakObjectPtr<AEGChessUnit> Weak=U;const FGuid Round=RoundId;
   EnqueueOpening(U,[this,Weak,Round,Level,Awakened,Hero]()
   {
    if(bActive&&RoundId==Round&&Weak.IsValid()&&IEGCombatant::Execute_IsAlive(Weak.Get()))
     {Weak->GetHeroSkill()->Configure(this,Level,Hero?Awakened:Weak->GetHeroSkill()->bAwakened);if(Hero&&Stigmata&&Weak.IsValid()&&bActive&&RoundId==Round)Stigmata->ApplyInitialMana(Weak.Get());}
   });
  }
   else {U->GetHeroSkill()->Configure(this,Level,Awakened);if(Hero&&Stigmata)Stigmata->ApplyInitialMana(U);}
  EnterUnit(U);
 }
 RefreshTeam();
}
void UEGBattleContextComponent::Stop()
{
 EnemyDamageMultiplier=1.f;
 PresentationHolds.Reset();
 SyncPresentationPause();
 if(bHumanPauseOwned||bHumanPausePending)EndHumanEntryPause(HumanPauseRound,HumanPauseAction);
 bRunning=false;bOpeningStarted=false;OpeningItems.Reset();OpeningIndex=0;OpeningElapsed=OpeningLastAudioTime=0;
 if(Stigmata)Stigmata->Stop();
 const FGuid EndingRound=RoundId;bActive=false;RoundId.Invalidate();CountedAttacks.Reset();Entered.Reset();DeployedIds.Reset();
 for(auto* U:Units())if(IsValid(U))
 {U->OnCombatResolved.RemoveDynamic(this,&ThisClass::CombatResolved);U->OnDied.RemoveDynamic(this,&ThisClass::SummonDied);if(auto* Corruption=U->FindComponentByClass<UEGCorruptionCombatComponent>())Corruption->EndCombat();}
 for(const auto& Pair:TeamSpeedEffects)if(Pair.Key.IsValid())Pair.Key->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(Pair.Value);
 TeamSpeedEffects.Reset();TeamSpeedStates.Reset();
 for(const auto& Pair:KnightAuraEffects)if(Pair.Key.IsValid())Pair.Key->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(Pair.Value);
 KnightAuraEffects.Reset();KnightAuraBonuses.Reset();KnightAuras.Reset();KnightOwners.Reset();
 for(const auto& Pair:EdenSongEffects)if(Pair.Key.IsValid())Pair.Key->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(Pair.Value);
 EdenSongEffects.Reset();
 for(auto* U:Units())if(IsValid(U)){U->GetHitFeedback()->ClearFeedback();U->GetHeroSkill()->EndBattle();}
 for(auto& S:Summons)if(S.IsValid()){S->OnDied.RemoveDynamic(this,&ThisClass::SummonDied);S->Destroy();}
 for(TActorIterator<AEGSkillProjectile> It(GetWorld());It;++It)if(It->Battle.Get()==this)It->Destroy();
 for(TActorIterator<AEGCorruptionSeed> It(GetWorld());It;++It)if(It->BelongsTo(this))It->Destroy();
 for(TActorIterator<AEGPresentationBurst> It(GetWorld());It;++It)if(EndingRound.IsValid()&&It->RoundId==EndingRound)It->Destroy();
 Summons.Reset();
}
void UEGBattleContextComponent::EnqueueOpening(AEGChessUnit* Unit,TFunction<void()> Action,bool bFirst)
{
 if(!bActive||bOpeningStarted||!IsValid(Unit)||!Action)return;
 for(auto& Item:OpeningItems)if(Item.Unit.Get()==Unit){if(bFirst)Item.Actions.Insert(MoveTemp(Action),0);else Item.Actions.Add(MoveTemp(Action));return;}
 auto& Item=OpeningItems.AddDefaulted_GetRef();Item.Unit=Unit;Item.Sequence=OpeningItems.Num();
 const auto Faction=IEGCombatant::Execute_GetFaction(Unit);Item.Group=Faction==EEGFaction::Player?0:2;
 FIntPoint Cell(0,0);
 if(Mode.IsValid()&&Mode->GetBoard()&&Mode->GetBoard()->LayoutSource)
  Mode->GetBoard()->LayoutSource->TryWorldToCell(Unit->GetActorLocation(),Cell);
 Item.Row=Faction==EEGFaction::Player?-Cell.Y:Cell.Y;Item.Column=Cell.X;Item.Actions.Add(MoveTemp(Action));
}
void UEGBattleContextComponent::SetPresentationHold(FGuid Token,bool bHold)
{
 if(!Token.IsValid())return;
 if(bHold&&bActive)PresentationHolds.Add(Token);else PresentationHolds.Remove(Token);
 SyncPresentationPause();
}
bool UEGBattleContextComponent::CanAdvancePresentationVisuals() const
{
 return bRunning&&CanAdvancePresentationClock();
}
bool UEGBattleContextComponent::CanAdvancePresentationVisualsForToken(FGuid Token) const
{
 return Token.IsValid()&&PresentationHolds.Num()==1&&PresentationHolds.Contains(Token)
  &&CanAdvancePresentationVisuals();
}
bool UEGBattleContextComponent::CanAdvancePresentationClock() const
{
 const UWorld* World=GetWorld();
 if(!bActive||!RoundId.IsValid()||!World)return false;
 if(!World->IsPaused())return true;
 const auto* GameMode=World->GetAuthGameMode<AEGChessGameMode>();
 return bPresentationPauseOwned&&!PresentationHolds.IsEmpty()&&GameMode&&GameMode->IsOnlyPauseRequest(PresentationPauseRequestHandle);
}
void UEGBattleContextComponent::SyncPresentationPause()
{
 auto* World=GetWorld();
 if(!World)return;
 if(!PresentationHolds.IsEmpty()&&!bPresentationPauseOwned)
 {
  if(auto* Controller=World->GetFirstPlayerController();Controller&&Controller->PlayerState&&World->GetAuthGameMode<AEGChessGameMode>())
  {
   const FCanUnpause Request=FCanUnpause::CreateUObject(this,&ThisClass::CanReleasePresentationPause);
   PresentationPauseRequestHandle=Request.GetHandle();
   // PlayerController::SetPause deliberately ignores an already-paused world;
   // register with GameMode so overlapping owners each retain their request.
   if(World->GetAuthGameMode<AEGChessGameMode>()->SetPause(Controller,Request)&&World->IsPaused())
   {PresentationPauseController=Controller;bPresentationPauseOwned=true;}
   else
   {World->GetAuthGameMode<AEGChessGameMode>()->ReleasePauseRequest(PresentationPauseRequestHandle);PresentationPauseRequestHandle.Reset();}
  }
 }
 else if(PresentationHolds.IsEmpty()&&bPresentationPauseOwned)
 {
  bPresentationPauseOwned=false;
  if(auto* GameMode=World->GetAuthGameMode<AEGChessGameMode>())GameMode->ReleasePauseRequest(PresentationPauseRequestHandle);
  PresentationPauseRequestHandle.Reset();
  PresentationPauseController.Reset();
 }
}
void UEGBattleContextComponent::EnqueueTeamOpening(TFunction<void()> Action)
{
 if(!bActive||bOpeningStarted||!Action)return;
 auto& Item=OpeningItems.AddDefaulted_GetRef();Item.Group=1;Item.Sequence=OpeningItems.Num();Item.Actions.Add(MoveTemp(Action));
}
void UEGBattleContextComponent::EnqueueFinalOpening(TFunction<void()> Action)
{
 if(!bActive||bOpeningStarted||!Action)return;
 auto& Item=OpeningItems.AddDefaulted_GetRef();Item.Group=3;Item.Sequence=OpeningItems.Num();Item.Actions.Add(MoveTemp(Action));
}
void UEGBattleContextComponent::BeginOpening()
{
 if(!bActive||bOpeningStarted)return;bOpeningStarted=true;OpeningIndex=0;
 OpeningItems.StableSort([](const FOpeningItem& A,const FOpeningItem& B)
 {
  if(A.Group!=B.Group)return A.Group<B.Group;
  if(A.Row!=B.Row)return A.Row<B.Row;
  if(A.Column!=B.Column)return A.Column<B.Column;
  return A.Sequence<B.Sequence;
 });
 AdvanceOpening();
}
#if WITH_DEV_AUTOMATION_TESTS
void UEGBattleContextComponent::CompleteOpeningForTests()
{
 if(bActive&&!bOpeningStarted)BeginOpening();
 const FGuid Round=RoundId;
 while(bActive&&RoundId==Round&&bOpeningStarted&&!bRunning)AdvanceOpening();
}
void UEGBattleContextComponent::AdvanceOpeningForTests(float RealSeconds)
{AccumulateOpeningTime(FMath::Max(0.f,RealSeconds));}
#endif
void UEGBattleContextComponent::AdvanceOpening()
{
 if(!bActive||!bOpeningStarted||bRunning)return;
 if(OpeningIndex>=OpeningItems.Num())
 {
   const FGuid Round=RoundId;bRunning=true;CombatStartedAtWorld=GetWorld()?GetWorld()->GetTimeSeconds():0.;OpeningItems.Reset();OnCombatRunning.Broadcast(Round);return;
 }
 if(OpeningItems[OpeningIndex].Group!=1&&OpeningItems[OpeningIndex].Group!=3)
 {
  auto* Unit=OpeningItems[OpeningIndex].Unit.Get();
  if(!IsValid(Unit)||!IEGCombatant::Execute_IsAlive(Unit)){++OpeningIndex;AdvanceOpening();return;}
 }
 const FGuid Round=RoundId;const int32 Group=OpeningItems[OpeningIndex].Group;
 const TWeakObjectPtr<AEGChessUnit> Unit=OpeningItems[OpeningIndex].Unit;
 auto Actions=MoveTemp(OpeningItems[OpeningIndex].Actions);
 OpeningElapsed=0;OpeningLastAudioTime=GetWorld()?GetWorld()->GetAudioTimeSeconds():0;++OpeningIndex;
 for(auto& Action:Actions)
 {
  if(!bActive||RoundId!=Round||!bOpeningStarted)return;
  if(Group!=1&&Group!=3&&(!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get())))return;
  if(Action)Action();
 }
}
void UEGBattleContextComponent::RecordSuccessfulUltimate(AEGChessUnit* Unit,FGuid ActionId)
{
 if(!IsCombatRunning()||!IsValid(Unit)||!Unit->HasAuthority()||Unit->IsDeathPending()||!IEGCombatant::Execute_IsAlive(Unit)||
    IEGCombatant::Execute_GetFaction(Unit)!=EEGFaction::Player||!DeployedIds.Contains(Unit->GetInstanceId())||!ActionId.IsValid()||SuccessfulUltimateActions.Contains(ActionId))return;
 SuccessfulUltimateActions.Add(ActionId);
 if(FirstUltimateBattleSeconds<0.f)
 {
  const auto* Demo=Mode.IsValid()?Cast<AEGDemoGameMode>(Mode.Get()):nullptr;
  FirstUltimateBattleSeconds=Demo?Demo->GetBattleSeconds():GetWorld()?FMath::Max(0.f,static_cast<float>(GetWorld()->GetTimeSeconds()-CombatStartedAtWorld)):0.f;
 }
}
void UEGBattleContextComponent::TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)
{
 Super::TickComponent(DeltaTime,TickType,TickFunction);
 if(bHumanPauseOwned)
 {
  auto* Unit=HumanPauseUnit.Get();auto* Skill=HumanPauseSkill.Get();
  if(!bActive||HumanPauseRound!=RoundId||!IsValid(Unit)||Unit->IsDeathPending()||
     !IEGCombatant::Execute_IsAlive(Unit)||!Skill||!Skill->IsActionRunning()||Skill->ActionId!=HumanPauseAction||FPlatformTime::Seconds()-HumanPauseStartedReal>15.)
   EndHumanEntryPause(HumanPauseRound,HumanPauseAction);
  if(GetWorld()&&GetWorld()->IsPaused())return;
 }
 if(bActive&&bOpeningStarted&&!bRunning&&GetWorld())
 {
  const double Now=GetWorld()->GetAudioTimeSeconds();
  const double Delta=FMath::Max(0.0,Now-OpeningLastAudioTime);OpeningLastAudioTime=Now;
  AccumulateOpeningTime(Delta);
 }
}
void UEGBattleContextComponent::AccumulateOpeningTime(double Delta)
{
 if(!bActive||!bOpeningStarted||bRunning||HasPresentationHold())return;
 // Each newly presented item gets its own visible second. A long frame may
 // delay the queue, but must not execute unseen items together on recovery.
 OpeningElapsed+=Delta;if(OpeningElapsed>=1.0)AdvanceOpening();
}
void UEGBattleContextComponent::EndPlay(const EEndPlayReason::Type Reason){Stop();Super::EndPlay(Reason);}
void UEGBattleContextComponent::EnterUnit(AEGChessUnit* U)
{
 if(!U||Entered.Contains(U->GetInstanceId()))return;Entered.Add(U->GetInstanceId());
 U->OnCombatResolved.AddUniqueDynamic(this,&ThisClass::CombatResolved);
 U->OnDied.AddUniqueDynamic(this,&ThisClass::SummonDied);
 U->GetHeroSkill()->RefreshSpeed();
 const auto Faction=IEGCombatant::Execute_GetFaction(U);
 if(const auto* State=TeamSpeedStates.Find(Faction);State&&!State->bSnapshotOnly&&(!State->bHeroesOnly||(!U->GetHeroSkill()->bSummon&&!U->IsAuxiliaryEmplacement()&&U->GetDefinition()&&U->GetDefinition()->HeroKind!=EEGHeroKind::Generic))&&GetTeamBuffSecondsFor(Faction)>0)ApplyTeamSpeedTo(U,GetTeamBuffSecondsFor(Faction),State->Bonus);
 RefreshKnightAuras();
}
int32 UEGBattleContextComponent::SummonCount() const
{int32 N=0;for(const auto& S:Summons)if(S.IsValid()&&IEGCombatant::Execute_IsAlive(S.Get())&&Entered.Contains(S->GetInstanceId()))++N;return N;}
float UEGBattleContextComponent::TeamSpeed() const {return 0.f;}

AEGChessUnit* UEGBattleContextComponent::FindCan(FGuid Id) const
{for(const auto& S:Summons)if(S.IsValid()&&IEGCombatant::Execute_IsAlive(S.Get())&&S->GetDefinition()->HeroKind==EEGHeroKind::Can&&S->GetHeroSkill()->SummonerId==Id)return S.Get();return nullptr;}
bool UEGBattleContextComponent::HasLivingCan(FGuid Id) const{return FindCan(Id)!=nullptr;}
bool UEGBattleContextComponent::HasLivingSummoner(FGuid Id) const
{for(auto* U:Units())if(IsValid(U)&&U->GetInstanceId()==Id&&IEGCombatant::Execute_IsAlive(U))return true;return false;}
AEGChessUnit* UEGBattleContextComponent::SpawnSummon(UEGHeroDefinition* D,FGuid OwnerId,int32 OwnerLevel,FVector Point,bool InFlight)
{return SpawnSummonConfigured(D,OwnerId,OwnerLevel,Point,InFlight,0.f,0.f);}
AEGChessUnit* UEGBattleContextComponent::SpawnSummonConfigured(UEGHeroDefinition* D,FGuid OwnerId,int32 OwnerLevel,FVector Point,bool InFlight,float HealthRatio,float AttackRatio)
{
 if(!bActive||!D||!Mode.IsValid())return nullptr;
 if(D->HeroKind==EEGHeroKind::Can&&FindCan(OwnerId))return nullptr;
 if(auto* L=Mode->GetBoard()->LayoutSource.Get())
 {
  FVector Local=L->GetActorTransform().InverseTransformPosition(Point-FVector(0,0,88));
  Local.X=FMath::Clamp(Local.X,-L->Columns*L->CellWidth*.5f+25,L->Columns*L->CellWidth*.5f-25);
  Local.Y=FMath::Clamp(Local.Y,-L->Rows*L->CellDepth*.5f+25,L->Rows*L->CellDepth*.5f-25);Local.Z=0;
  Point=L->GetActorTransform().TransformPosition(Local)+FVector(0,0,88);
 }
 AEGChessUnit* Summoner=nullptr;for(auto* Candidate:Units())if(Candidate&&Candidate->GetInstanceId()==OwnerId){Summoner=Candidate;break;}
 if(!Summoner||!IEGCombatant::Execute_IsAlive(Summoner))return nullptr;
 if(D->HeroKind==EEGHeroKind::Knight)
 {
  if(KnightOwners.Contains(OwnerId))return nullptr;
  KnightOwners.Add(OwnerId);
 }
 auto* U=Mode->SpawnBattleSummon(D,OwnerLevel+SummonBonus,Point,IEGCombatant::Execute_GetFaction(Summoner),!InFlight);
 if(!U){if(D->HeroKind==EEGHeroKind::Knight)KnightOwners.Remove(OwnerId);return nullptr;}
 auto* S=U->GetHeroSkill();S->Configure(this,OwnerLevel+SummonBonus,false);S->bSummon=true;S->SummonerId=OwnerId;S->bInFlight=InFlight;
 if(D->HeroKind==EEGHeroKind::Knight)
 {
  if(HealthRatio>0)
  {
   auto* ASC=U->GetAbilitySystemComponent();const float Health=Summoner->GetAttributes()->GetMaxHealth()*HealthRatio;
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),Health);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Health);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),Summoner->GetAttributes()->GetAttackPower()*AttackRatio);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxManaAttribute(),0.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
  }
  U->OnDestroyed.AddUniqueDynamic(this,&ThisClass::SummonDestroyed);
 }
 U->RefreshBoardCollision();
 U->SetActorHiddenInGame(InFlight);Summons.Add(U);U->OnDied.AddUniqueDynamic(this,&ThisClass::SummonDied);
 if(!InFlight)
 {
  EnterUnit(U);if(D->HeroKind!=EEGHeroKind::Knight)Visual(U,U->GetActorLocation()-FVector(0,0,88),1.8f);
 }
 if(Stigmata)Stigmata->ApplySummon(U);
 RefreshTeam();return U;
}
AEGChessUnit* UEGBattleContextComponent::SpawnKnight(AEGChessUnit* Owner,float HealthRatio,float AttackRatio)
{
 if(!bActive||!IsValid(Owner)||!Owner->HasAuthority()||!IEGCombatant::Execute_IsAlive(Owner)||!Owner->GetDefinition()
  ||!FMath::IsFinite(HealthRatio)||HealthRatio<=0||!FMath::IsFinite(AttackRatio)||AttackRatio<0)return nullptr;
 const auto Id=Owner->GetInstanceId();
 if(KnightOwners.Contains(Id))
 {
  for(const auto& Entry:Summons)if(auto* Existing=Entry.Get();IsValid(Existing)&&IEGCombatant::Execute_IsAlive(Existing)
    &&Existing->GetDefinition()->HeroKind==EEGHeroKind::Knight&&Existing->GetHeroSkill()->SummonerId==Id)return Existing;
  return nullptr;
 }
 auto* Definition=Owner->GetDefinition()->SummonDefinition.Get();
 if(!Definition||Definition->HeroKind!=EEGHeroKind::Knight)return nullptr;
 FVector Forward=Owner->GetActorForwardVector();double Nearest=DBL_MAX;
 for(auto* Enemy:Units())if(IsValid(Enemy)&&IEGCombatant::Execute_IsAlive(Enemy)
   &&IEGCombatant::Execute_GetFaction(Enemy)!=EEGFaction::Neutral&&IEGCombatant::Execute_GetFaction(Enemy)!=IEGCombatant::Execute_GetFaction(Owner))
 {
  const double Distance=FVector::DistSquared2D(Owner->GetActorLocation(),Enemy->GetActorLocation());
  if(Distance<Nearest){Nearest=Distance;Forward=(Enemy->GetActorLocation()-Owner->GetActorLocation()).GetSafeNormal2D();}
 }
 auto* Knight=SpawnSummonConfigured(Definition,Id,Owner->GetHeroSkill()->GetLevel(),Owner->GetActorLocation()+Forward*(UEGCombatComponent::BlockingRadius(Owner)+180.f),false,HealthRatio,AttackRatio);
 if(!Knight)return nullptr;
 Knight->SetActorRotation(Forward.Rotation());
 return Knight;
}
void UEGBattleContextComponent::RegisterAdultKnightAura(AEGChessUnit* Knight,float Bonus)
{
 if(!bActive||!IsValid(Knight)||!Knight->HasAuthority()||!IEGCombatant::Execute_IsAlive(Knight)
  ||!Summons.Contains(Knight)||!Knight->GetDefinition()||Knight->GetDefinition()->HeroKind!=EEGHeroKind::Knight||!FMath::IsFinite(Bonus)||Bonus<0)return;
 KnightAuras.Add(Knight,Bonus);RefreshKnightAuras();
}
void UEGBattleContextComponent::RefreshKnightAuras()
{
 if(!bActive)return;
 for(auto It=KnightAuras.CreateIterator();It;++It)if(!It.Key().IsValid()||!IEGCombatant::Execute_IsAlive(It.Key().Get()))It.RemoveCurrent();
 for(auto* Receiver:Units())
 {
  if(!IsValid(Receiver))continue;
  float Bonus=0;
  if(IEGCombatant::Execute_IsAlive(Receiver)&&!Receiver->IsAuxiliaryEmplacement()&&!Receiver->GetHeroSkill()->bSummon
   &&Receiver->GetDefinition()&&Receiver->GetDefinition()->HeroKind!=EEGHeroKind::Generic)
   for(const auto& Source:KnightAuras)if(IEGCombatant::Execute_GetFaction(Source.Key.Get())==IEGCombatant::Execute_GetFaction(Receiver))Bonus=FMath::Max(Bonus,Source.Value);
  if(const auto* Existing=KnightAuraBonuses.Find(Receiver);Existing&&FMath::IsNearlyEqual(*Existing,Bonus))continue;
  auto* ASC=Receiver->GetAbilitySystemComponent();
  if(auto* Handle=KnightAuraEffects.Find(Receiver)){ASC->RemoveActiveGameplayEffect(*Handle);KnightAuraEffects.Remove(Receiver);}
  KnightAuraBonuses.Remove(Receiver);
  if(Bonus<=0)continue;
  auto Spec=ASC->MakeOutgoingSpec(UEGTeamSpeedEffect::StaticClass(),1,ASC->MakeEffectContext());if(!Spec.IsValid())continue;
  Spec.Data->SetDuration(FGameplayEffectConstants::INFINITE_DURATION,true);
  Spec.Data->SetSetByCallerMagnitude(EGTags::TeamAttackSpeed,1+Bonus);
  KnightAuraEffects.Add(Receiver,ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data));KnightAuraBonuses.Add(Receiver,Bonus);
 }
}
void UEGBattleContextComponent::SummonDied(AEGChessUnit* U)
{
 if(IsValid(U))if(auto* Effect=TeamSpeedEffects.Find(U)){U->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(*Effect);TeamSpeedEffects.Remove(U);}
 if(IsValid(U))if(auto* Effect=EdenSongEffects.Find(U)){U->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(*Effect);EdenSongEffects.Remove(U);}
 KnightAuras.Remove(U);RefreshKnightAuras();RefreshTeam();
 for(auto* Other:Units())if(IsValid(Other))Other->GetHeroSkill()->ClearTauntIfSource(U);
}
void UEGBattleContextComponent::SummonDestroyed(AActor* Actor)
{if(auto* U=Cast<AEGChessUnit>(Actor)){KnightAuras.Remove(U);RefreshKnightAuras();}}
void UEGBattleContextComponent::RefreshTeam()
{
 if(!bActive||bRefreshing)return;TGuardValue<bool> Guard(bRefreshing,true);
 for(auto* U:Units())U->GetHeroSkill()->RefreshSpeed();
}
void UEGBattleContextComponent::RefreshProtectionFields()
{
 if(!bActive||bRefreshingProtection)return;
 TGuardValue<bool> Guard(bRefreshingProtection,true);
 const auto Snapshot=Units();
 for(auto* U:Snapshot)if(IsValid(U))if(auto* Behavior=U->GetHeroSkill()->GetBehavior())Behavior->RefreshProtectionField();
}
bool UEGBattleContextComponent::Damage(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,FGuid ActionId,bool bBasicAttack)
{
 const bool Applied=bActive&&FEGCombatExecution::Damage(Source,Target,Raw,0,1,FGameplayEffectContextHandle(),ActionId,false,1.f,bBasicAttack);
 // The target's HitFeedback component owns contact visuals for every damage path.
 return Applied;
}
void UEGBattleContextComponent::Heal(AEGChessUnit* Target,float Amount)
{
 if(bActive)FEGCombatExecution::Heal(Target,Amount);
}
void UEGBattleContextComponent::Heal(AEGChessUnit* Source,AEGChessUnit* Target,float Amount)
{
 if(bActive)FEGCombatExecution::Heal(Source,Target,Amount);
}
void UEGBattleContextComponent::Area(AEGChessUnit* Source,FVector Point,float Radius,float Raw,bool Cone,bool Taunt,FGuid ActionId)
{
 if(!bActive||!Source)return;
 const auto Snapshot=Units();TSet<AEGChessUnit*> Visited;
 for(auto* T:Snapshot)
 {
  if(!Source->GetCombat()->IsValidEnemy(T)||Visited.Contains(T)||EGCombatGeometry::PointDistanceSquared(Point,T)>FMath::Square(Radius))continue;
  Visited.Add(T);FVector D=EGCombatGeometry::ClosestPoint(Point,T)-Point;D.Z=0;
  if(Cone&&FVector::DotProduct(Source->GetActorForwardVector(),D.GetSafeNormal())<0)continue;
  Damage(Source,T,Raw,ActionId);
  if(Taunt&&IsValid(T)&&IEGCombatant::Execute_IsAlive(T))T->GetHeroSkill()->SetTaunt(Source,Source->GetDefinition()->GetSkillRules().TauntDuration);
 }
 Visual(Source,Point-FVector(0,0,85),Radius,true);
}
void UEGBattleContextComponent::BasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid Id)
{
 if(!bActive||!IsValid(Source)||CountedAttacks.Contains(Id)||!Id.IsValid())return;
 if(!Source->HasAuthority()||Source->IsAuxiliaryEmplacement()||Source->GetDefinition()->HeroKind==EEGHeroKind::Knight)return;
 const FGuid HitRound=RoundId;
 CountedAttacks.Add(Id);
 auto* S=Source->GetHeroSkill();
 if(auto* Behavior=S->GetBehavior())Behavior->OwnerBasicHit(Target,Id);
 // Human Herrscher grants bow mana at attack Finish, independent of contacts.
 if(IEGCombatant::Execute_IsAlive(Source)&&(!S->bSummon||S->bAllowSummonUltimate)&&!S->IsCasting()&&Source->GetDefinition()->HeroKind!=EEGHeroKind::ElysiaHerrscher)
  Source->RestoreMana(Source->GetAttributes()->GetManaOnHit());
 // Callbacks may synchronously settle the round. Iterate a snapshot and validate
 // the generation after every callback; never notify a newly started battle.
 const auto Snapshot=Units();
 for(auto* Observer:Snapshot)
 {
  if(!bActive||RoundId!=HitRound||!IsValid(Source))return;
  if(IsValid(Observer)&&IEGCombatant::Execute_IsAlive(Observer))
   if(auto* Behavior=Observer->GetHeroSkill()->GetBehavior())Behavior->AllyBasicHit(Source,Target,Id);
 }
 if(!bActive||RoundId!=HitRound||!IsValid(Source))return;
 if(Stigmata)Stigmata->BasicHit(Source,Target,Id);
 if(!bActive||RoundId!=HitRound||!IsValid(Source))return;
 if(IEGCombatant::Execute_GetFaction(Source)!=EEGFaction::Player)return;
 ++AttackCount;
}
void UEGBattleContextComponent::ApplyEdenSong(AEGChessUnit* Source,float ManaPerSecond,int32 Seconds)
{
 if(!bActive||!IsValid(Source)||!Source->HasAuthority()||!FMath::IsFinite(ManaPerSecond)||ManaPerSecond<=0||Seconds<1)return;
 const auto Faction=IEGCombatant::Execute_GetFaction(Source);
 for(auto* Target:Units())
 {
  if(!IsValid(Target)||Target==Source||!IEGCombatant::Execute_IsAlive(Target)||Target->IsAuxiliaryEmplacement()||Target->GetHeroSkill()->bSummon||!Target->GetDefinition()||Target->GetDefinition()->HeroKind==EEGHeroKind::Generic||IEGCombatant::Execute_GetFaction(Target)!=Faction||Target->GetAttributes()->GetMaxMana()<=0)continue;
  auto* ASC=Target->GetAbilitySystemComponent();
  if(auto* Old=EdenSongEffects.Find(Target))ASC->RemoveActiveGameplayEffect(*Old);
  // Target ASC owns the periodic spec; source death cannot cancel the song.
  auto Spec=ASC->MakeOutgoingSpec(UEGEdenManaEffect::StaticClass(),1,ASC->MakeEffectContext());
  if(!Spec.IsValid())continue;
  Spec.Data->SetSetByCallerMagnitude(EGTags::Mana,ManaPerSecond);
  // Keep the tick at exactly N seconds ahead of GE expiration ordering.
  Spec.Data->SetDuration(float(Seconds)+.001f,true);
  EdenSongEffects.Add(Target,ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data));
 }
}
void UEGBattleContextComponent::Launch(AEGChessUnit* Source,AEGChessUnit* Target,FVector Point,float Damage,EEGProjectileKind Kind,FGuid AttackId,AEGChessUnit* Cargo)
{
 if(!bActive||!Source)return;
 auto* P=GetWorld()->SpawnActor<AEGSkillProjectile>();if(!P)return;
 P->Battle=this;P->Round=RoundId;P->Source=Source;P->Target=Target;P->AttackId=AttackId;P->Cargo=Cargo;P->Kind=Kind;P->RawDamage=Damage;
 P->Start=Source->GetActionOrigin();P->Destination=Point;P->Duration=FMath::Max(.01f,Source->GetDefinition()->GetSkillRules().ProjectileDuration);
 // Griseo's original paint timeline already renders the projectile. The actor only owns travel/hit authority.
 if(Kind==EEGProjectileKind::BasicAttack&&Source->GetDefinition()->HeroKind==EEGHeroKind::Griseo)P->Visual->SetVisibility(false);
 if(Kind==EEGProjectileKind::ThrownSummon&&IsValid(Cargo))
 {
  P->SummonMotion=Cargo->GetDefinition()->SummonPresentation.LoadSynchronous();
  if(P->SummonMotion&&P->SummonMotion->IsValidMotion())
  {
   auto* Summoner=Source;
   for(auto* U:Units())if(U->GetInstanceId()==Cargo->GetHeroSkill()->SummonerId){Summoner=U;break;}
   P->Duration=FMath::Max(.01f,P->SummonMotion->Duration);
   P->FlightHeight=Summoner->GetDefinition()->GetPresentationHeight()*P->SummonMotion->HeightInSummonerUnits;
   P->FlightBasis=Summoner->GetVisiblePresentationMesh()->GetComponentQuat();
   if(P->SummonMotion->bDropFromTargetPortal)
    if(auto* Ring=Summoner->FindComponentByClass<UEGUltimateRingComponent>();Ring&&Ring->bTargetOverhead)
    {
     const float TargetHeight=Target&&Target->GetDefinition()?Target->GetDefinition()->GetPresentationHeight():0.f;
     const FVector Portal=Ring->ResolveOverheadCenter(Point,TargetHeight);
     P->FlightHeight=FMath::Max(1.f,Portal.Z-(Point.Z-88.f));Ring->BeginDescent(P);
    }
   P->Start=Point+P->FlightBasis.RotateVector(P->SummonMotion->NormalizedOffsets[0]*P->FlightHeight);
   Cargo->SetActorLocation(P->Start);Cargo->SetActorRotation(Summoner->GetActorRotation());
   // Portal drops retain the live cat silhouette. The legacy whole-body CG
   // material remains a source reference and is used only by the old mode.
   if(!P->SummonMotion->bDropFromTargetPortal)if(auto* Material=P->SummonMotion->DescentMaterial.LoadSynchronous())
   {P->SavedCargoMaterial=Cargo->GetVisiblePresentationMesh()->GetMaterial(0);Cargo->GetVisiblePresentationMesh()->SetMaterial(0,Material);}
   if(auto* Effect=P->SummonMotion->DescentEffect.LoadSynchronous())
    P->DescentEffect=AEGSourceEffect::Spawn(Cargo->GetHeroSkill(),Cargo->GetVisiblePresentationMesh(),Effect,1.f,true);
  }
  else P->SummonMotion=nullptr;
 }
 if(Kind==EEGProjectileKind::ThrownSummon&&Cargo)
  for(auto* Summoner:Units())if(Summoner->GetInstanceId()==Cargo->GetHeroSkill()->SummonerId)
   {if(auto* Audio=Summoner->FindComponentByClass<UEGCombatAudioComponent>())Audio->RegisterLaunch(AttackId);break;}
 P->SetActorLocation(P->Start);if(Kind==EEGProjectileKind::AreaImpact)Visual(Source,Point-FVector(0,0,88),Source->GetDefinition()->GetSkillRules().AreaRadius,true);
}

void UEGBattleContextComponent::Visual(AEGChessUnit* Source,FVector Point,float Size,bool Area)
{
 if(!bActive||!IsValid(Source))return;
 const auto* Hero=Source->GetDefinition();
 if(Area&&Hero&&(Hero->HeroKind==EEGHeroKind::Hua||Hero->HeroKind==EEGHeroKind::Sakura))
 {
  const auto* Data=Hero->SkillDefinition.Get();
  if(Data&&FMath::IsFinite(Size)&&Size>0.f&&FMath::IsFinite(Data->AreaPresentationReferenceRadius)&&Data->AreaPresentationReferenceRadius>0.f)
   if(auto* Skill=Source->GetHeroSkill())
    Skill->PresentSourceEffectAt(Data->AreaPresentation.LoadSynchronous(),Point,Source->GetActorRotation(),Size/Data->AreaPresentationReferenceRadius);
  return;
 }
 auto* FX=AEGPresentationBurst::Spawn(GetWorld(),Point,(Area?Source->GetDefinition()->GetSkillMaterial():Source->GetDefinition()->GetAttackMaterial()).LoadSynchronous(),Area?Size/100.f:Size);
 if(FX){FX->RoundId=RoundId;if(Area)FX->FixedAreaRadius=Size;}
}

bool UEGBattleContextComponent::AwardBattleGold(AEGChessUnit* Source,int32 Amount)
{
 if(!bActive||!IsValid(Source)||!Source->HasAuthority()||!DeployedIds.Contains(Source->GetInstanceId())||Amount<=0||GoldRecipients.Contains(Source->GetInstanceId()))return false;
 auto* GM=Cast<AEGDemoGameMode>(Mode.Get());if(!GM||!GM->GetCards())return false;
 GoldRecipients.Add(Source->GetInstanceId());AwardedGold+=Amount;
 GM->GetCards()->AwardGold(Amount);return true;
}
float UEGBattleContextComponent::GetTeamBuffSeconds() const
{return GetTeamBuffSecondsFor(EEGFaction::Player);}
float UEGBattleContextComponent::GetTeamBuffSecondsFor(EEGFaction Faction) const
{const auto* State=TeamSpeedStates.Find(Faction);return bActive&&GetWorld()&&State?FMath::Max(0.f,float(State->End-GetWorld()->GetTimeSeconds())):0.f;}
bool UEGBattleContextComponent::ApplyTeamSpeedTo(AEGChessUnit* U,float Duration,float Bonus)
{
 if(!IsValid(U)||!IEGCombatant::Execute_IsAlive(U)||Duration<=0)return false;
 auto* ASC=U->GetAbilitySystemComponent();
 if(!ASC)return false;
 const FGuid AppliedRound=RoundId;const TWeakObjectPtr<AEGChessUnit> WeakTarget(U);
 const TWeakObjectPtr<UAbilitySystemComponent> WeakASC(ASC);
 if(auto* Old=TeamSpeedEffects.Find(U))ASC->RemoveActiveGameplayEffect(*Old);
 if(!bActive||RoundId!=AppliedRound||!WeakTarget.IsValid()||!WeakASC.IsValid())return false;
 auto Spec=ASC->MakeOutgoingSpec(UEGTeamSpeedEffect::StaticClass(),1,ASC->MakeEffectContext());
 if(!Spec.IsValid())return false;
 Spec.Data->SetDuration(Duration,true);Spec.Data->SetSetByCallerMagnitude(EGTags::TeamAttackSpeed,1+Bonus);
 const FActiveGameplayEffectHandle Applied=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 if(!Applied.IsValid())return false;
 if(!bActive||RoundId!=AppliedRound||!WeakTarget.IsValid())
 {if(WeakASC.IsValid())WeakASC->RemoveActiveGameplayEffect(Applied);return false;}
 TeamSpeedEffects.Add(WeakTarget.Get(),Applied);return true;
}
void UEGBattleContextComponent::ApplyTeamSpeed(AEGChessUnit* Source,float Bonus,float Duration,bool bSnapshotOnly,bool bHeroesOnly,TFunction<void(AEGChessUnit*)> OnApplied)
{
 if(!bActive||!IsValid(Source)||!Source->HasAuthority()||Bonus<0||Duration<=0||!FMath::IsFinite(Bonus)||!FMath::IsFinite(Duration))return;
 const auto Faction=IEGCombatant::Execute_GetFaction(Source);if(Faction==EEGFaction::Neutral)return;
 auto& State=TeamSpeedStates.FindOrAdd(Faction);State.Bonus=Bonus;State.End=GetWorld()->GetTimeSeconds()+Duration;State.bSnapshotOnly=bSnapshotOnly;State.bHeroesOnly=bHeroesOnly;
 const FGuid AppliedRound=RoundId;const TWeakObjectPtr<AEGChessUnit> WeakSource(Source);
 for(auto* U:Units())
 {
  if(!bActive||RoundId!=AppliedRound||!WeakSource.IsValid()||!IEGCombatant::Execute_IsAlive(WeakSource.Get()))return;
  if(!IsValid(U)||IEGCombatant::Execute_GetFaction(U)!=Faction||
   (bHeroesOnly&&(U->GetHeroSkill()->bSummon||U->IsAuxiliaryEmplacement()||!U->GetDefinition()||U->GetDefinition()->HeroKind==EEGHeroKind::Generic))||
   (bSnapshotOnly&&!U->GetHeroSkill()->IsTargetable()))continue;
  const TWeakObjectPtr<AEGChessUnit> WeakTarget(U);
  const bool bApplied=ApplyTeamSpeedTo(U,Duration,Bonus);
  if(bApplied&&OnApplied&&bActive&&RoundId==AppliedRound&&WeakSource.IsValid()&&IEGCombatant::Execute_IsAlive(WeakSource.Get())&&
   WeakTarget.IsValid()&&IEGCombatant::Execute_IsAlive(WeakTarget.Get()))OnApplied(WeakTarget.Get());
 }
}
void UEGBattleContextComponent::EffectiveBasicHeal(AEGChessUnit* Source,FGuid Id,float RestoredHealth)
{
 if(!bActive||!IsValid(Source)||!Source->HasAuthority()||!Id.IsValid()||!FMath::IsFinite(RestoredHealth)||RestoredHealth<=0||CountedAttacks.Contains(Id)||!IEGCombatant::Execute_IsAlive(Source)||Source->GetHeroSkill()->IsCasting())return;
 CountedAttacks.Add(Id);Source->RestoreMana(Source->GetAttributes()->GetManaOnHit());
}
void UEGBattleContextComponent::CombatResolved(const FEGCombatResolution& Receipt)
{
 if(!bActive||Receipt.bHealing||Receipt.bLinkedDamage)return;
 const FGuid ObservedRound=RoundId;
 if(Stigmata)Stigmata->AfterDamage(Receipt);
 if(!bActive||RoundId!=ObservedRound)return;
 const auto Snapshot=Units();
 for(auto* U:Snapshot)
 {
  if(!bActive||RoundId!=ObservedRound)return;
  if(!IsValid(U))continue;
  if(auto* Behavior=U->GetHeroSkill()->GetBehavior())Behavior->DamageResolved(Receipt);
  if(!bActive||RoundId!=ObservedRound)return;
  if(IsValid(U))if(auto* Corruption=U->FindComponentByClass<UEGCorruptionCombatComponent>())Corruption->ObserveDamage(Receipt);
 }
}
