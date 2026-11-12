#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGElysiaOriginKnockupComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Skills/EGHumanArrowProjectile.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"

namespace
{
const FName CrystalShield(TEXT("ElysiaHerrscher.CrystalMirror"));
const AEGDeploymentBoard* Layout(const AEGChessUnit* Unit)
{
 const auto* GM=Unit&&Unit->GetWorld()?Unit->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 return GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
}
bool InCellRadius(const AEGDeploymentBoard* Board,const AEGChessUnit* Center,const AEGChessUnit* Candidate,int32 Radius)
{
 if(!Board||!Center||!Candidate||!Candidate->GetDefinition())return false;
 FIntPoint A,B;if(!Board->TryWorldToCell(Center->GetActorLocation(),A)||!Board->TryWorldToCell(Candidate->GetActorLocation(),B))return false;
 const auto Extent=Candidate->GetDefinition()->GetPlacementFootprint();
 return FMath::Max(0,FMath::Abs(A.X-B.X)-Extent.X/2)<=Radius&&FMath::Max(0,FMath::Abs(A.Y-B.Y)-Extent.Y/2)<=Radius;
}
bool IntersectsForwardLane(const AEGDeploymentBoard* Board,const AEGChessUnit* Owner,const AEGChessUnit* Target,const AEGChessUnit* Candidate,float WidthCells)
{
 if(!Board||!Owner||!Target||!Candidate||!Candidate->GetDefinition())return false;
 FIntPoint Cell;if(!Board->TryWorldToCell(Candidate->GetActorLocation(),Cell))return false;
 const FTransform Frame=Board->GetActorTransform();
 const FVector Start=Frame.InverseTransformPosition(Owner->GetActorLocation());
 const FVector End=Frame.InverseTransformPosition(Target->GetActorLocation());
 FVector2D Direction(End.X-Start.X,End.Y-Start.Y);if(!Direction.Normalize())return false;
 const auto Extent=Candidate->GetDefinition()->GetPlacementFootprint();
 const float HalfWidth=WidthCells*.5f*FMath::Min(Board->CellWidth,Board->CellDepth);
 for(int32 X=-Extent.X/2;X<=Extent.X/2;++X)for(int32 Y=-Extent.Y/2;Y<=Extent.Y/2;++Y)
 {
  const FIntPoint Occupied=Cell+FIntPoint(X,Y);if(!Board->IsCellValid(Occupied))continue;
  const FVector P=Frame.InverseTransformPosition(Board->CellToWorld(Occupied));
  const FVector2D Delta(P.X-Start.X,P.Y-Start.Y);
  const float Along=FVector2D::DotProduct(Delta,Direction);
  const float Across=FMath::Abs(Delta.X*Direction.Y-Delta.Y*Direction.X);
  // Project the occupied cell onto the lane normal and direction. Strict
  // overlap excludes a neighboring cell that only touches the lane edge.
  const float AcrossExtent=.5f*(FMath::Abs(Direction.Y)*Board->CellWidth+FMath::Abs(Direction.X)*Board->CellDepth);
  const float AlongExtent=.5f*(FMath::Abs(Direction.X)*Board->CellWidth+FMath::Abs(Direction.Y)*Board->CellDepth);
  if(Along+AlongExtent>0.f&&Across<HalfWidth+AcrossExtent-KINDA_SMALL_NUMBER)return true;
 }
 return false;
}
}

void UEGElysiaHerrscherSkillBehavior::Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle)
{
 Super::Initialize(InSkill,InBattle);Phase=EPhase::Bow;BowCursor=OriginCursor=0;OriginEndTime=0;
 PendingOriginStrikes.Reset();
 PresentationPhase=EEGElysiaHerrscherPresentationPhase::Bow;PresentationAction.Invalidate();
}
void UEGElysiaHerrscherSkillBehavior::PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase NewPhase,FGuid Action)
{
 PresentationPhase=NewPhase;PresentationAction=Action;
 OnPresentationPhaseChanged.Broadcast(NewPhase,RoundId,Action);
}
void UEGElysiaHerrscherSkillBehavior::OnBattleConfigured()
{
 if(!IsCurrentBattle()||!Unit->HasAuthority())return;
 const auto* D=Cast<UEGElysiaHerrscherSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 auto* ASC=Unit->GetAbilitySystemComponent();
 ManaEffectRemovedDelegate=ASC->OnAnyGameplayEffectRemovedDelegate().AddUObject(this,&ThisClass::ManaEffectRemoved);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxManaAttribute(),D->MaxMana);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),D->InitialMana);
 Skill->GrantSourceShield(CrystalShield,Unit->GetAttributes()->GetMaxHealth()*D->ShieldHealthRatio*Unit->GetAttributes()->GetSkillMultiplier(),TNumericLimits<float>::Max());
 Skill->ClearControlForImmunity();
}
bool UEGElysiaHerrscherSkillBehavior::AllowsAction(bool Ultimate) const
{return Ultimate?Phase==EPhase::Bow:Phase==EPhase::Bow||(Phase==EPhase::Origin&&Unit.IsValid()&&Unit->GetWorld()&&Unit->GetWorld()->GetTimeSeconds()<OriginEndTime);}
bool UEGElysiaHerrscherSkillBehavior::BlocksCombat() const
{return Phase==EPhase::FinisherPending;}
bool UEGElysiaHerrscherSkillBehavior::BlocksManaGain() const
{return Phase==EPhase::Entering||Phase==EPhase::Origin||Phase==EPhase::FinisherPending||Phase==EPhase::Finishing;}
void UEGElysiaHerrscherSkillBehavior::ManaEffectRemoved(const FActiveGameplayEffect& Effect)
{SuppressedManaEffects.Remove(Effect.Handle);}
bool UEGElysiaHerrscherSkillBehavior::SuppressManaModifier(FActiveGameplayEffectHandle Handle)
{
 if(!Handle.IsValid())return false;
 // Expired/removed effects cannot regain a modifier and must not retain state.
 for(auto It=SuppressedManaEffects.CreateIterator();It;++It)if(!It->GetOwningAbilitySystemComponent())It.RemoveCurrent();
 if(BlocksManaGain())SuppressedManaEffects.Add(Handle);
 return SuppressedManaEffects.Contains(Handle);
}
bool UEGElysiaHerrscherSkillBehavior::IsOriginActive() const
{return Phase==EPhase::Origin||Phase==EPhase::FinisherPending||Phase==EPhase::Finishing;}
FEGElysiaPassiveState UEGElysiaHerrscherSkillBehavior::GetPassiveState() const
{
 FEGElysiaPassiveState State;
 if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return State;
 const auto* D=Unit->GetDefinition()?Cast<UEGElysiaHerrscherSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
 if(!D)return State;
 switch(Phase)
 {
 case EPhase::Bow: State.Phase=EEGElysiaPassivePhase::Bow;break;
 case EPhase::Entering: State.Phase=EEGElysiaPassivePhase::Entering;break;
 case EPhase::Origin: State.Phase=EEGElysiaPassivePhase::Origin;break;
 case EPhase::FinisherPending: State.Phase=EEGElysiaPassivePhase::FinisherPending;break;
 case EPhase::Finishing: State.Phase=EEGElysiaPassivePhase::Finishing;break;
 }
 const bool bOrigin=IsOriginActive();
 State.Progress=bOrigin?0:BowCursor;
 State.Threshold=bOrigin?0:FMath::Max(1,D->BowAttackInterval);
 State.bNextAttackEnhanced=Phase==EPhase::Bow&&State.Progress==State.Threshold-1;
 State.RemainingSeconds=Phase==EPhase::Origin&&Unit->GetWorld()?FMath::Max(0.f,float(OriginEndTime-Unit->GetWorld()->GetTimeSeconds())):0.f;
 State.RoundId=RoundId;State.ActionId=ActiveAction;
 return State;
}
int32 UEGElysiaHerrscherSkillBehavior::PresentationIndex(bool Ultimate,int32 DefaultIndex) const
{
 if(Ultimate)return 0;
 const auto* D=Unit.IsValid()&&Unit->GetDefinition()?Cast<UEGElysiaHerrscherSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
 const bool bOrigin=Phase==EPhase::Origin;
 if(bOrigin)return 3+OriginCursor%2;
 const int32 Interval=D?FMath::Max(1,bOrigin?D->OriginAttackInterval:D->BowAttackInterval):3;
 const int32 Cursor=FMath::Max(0,bOrigin?OriginCursor:BowCursor)%Interval;
 return (bOrigin?3:0)+(Cursor==Interval-1?2:Cursor%2);
}
int32 UEGElysiaHerrscherSkillBehavior::IdlePresentationIndex() const {return IsOriginActive()?1:0;}
int32 UEGElysiaHerrscherSkillBehavior::DeathPresentationIndex() const
{
 switch(PresentationPhase)
 {
 case EEGElysiaHerrscherPresentationPhase::EntryReleased:
 case EEGElysiaHerrscherPresentationPhase::Origin:
 case EEGElysiaHerrscherPresentationPhase::FinisherPending:
 case EEGElysiaHerrscherPresentationPhase::FinisherBegin:
 case EEGElysiaHerrscherPresentationPhase::FinisherReleased:return 1;
 default:return 0;
 }
}
bool UEGElysiaHerrscherSkillBehavior::IsActionCurrent(const FEGSkillActionContext& C) const
{
 return IsCurrentBattle()&&C.RoundId==RoundId&&C.ActionId.IsValid()&&C.ActionId==ActiveAction&&Unit->HasAuthority()&&
  IEGCombatant::Execute_IsAlive(Unit.Get())&&Skill->IsActionRunning()&&Skill->ActionId==C.ActionId&&Skill->GetBehavior()==this;
}
bool UEGElysiaHerrscherSkillBehavior::IsOriginStrikeCurrent(FGuid Action,double HitTime) const
{
 if(!IsCurrentBattle()||!Battle->IsCombatRunning()||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get())||
    Phase!=EPhase::Origin||!Unit->GetWorld()||HitTime>=OriginEndTime)return false;
 const auto* Strike=PendingOriginStrikes.FindByPredicate([Action](const FPendingOriginStrike& P){return P.Context.ActionId==Action;});
 return Strike&&Strike->Context.RoundId==RoundId&&(Strike->bActionCompleted||
  (Skill->IsActionRunning()&&Skill->ActionId==Action&&ActiveAction==Action));
}
void UEGElysiaHerrscherSkillBehavior::ApplyOriginStrike(const FPendingOriginStrike& Strike,float Fraction,int32 ContactIndex,double HitTime)
{
 const auto& C=Strike.Context;
 bool bAnyHit=false;FVector FirstHitLocation=Strike.ImpactPoint;
 for(const auto& Entry:Strike.Targets)
 {
  if(!IsOriginStrikeCurrent(C.ActionId,HitTime))return;
  auto* Target=Entry.Unit.Get();
  if(!Unit->GetCombat()->IsValidEnemy(Target))continue;
  const FVector HitPoint=Target->GetActorLocation();
  if(Battle->Damage(Unit.Get(),Target,C.Attack*Entry.Multiplier*Fraction,C.ActionId,Entry.bMain))
  {
   // Prefer the selected target whenever it actually takes damage, even if
   // the battle's unit iteration visits a splash victim first.
   if(!bAnyHit||Entry.bMain)FirstHitLocation=HitPoint;
   bAnyHit=true;
  }
 }
 if(!bAnyHit||!IsOriginStrikeCurrent(C.ActionId,HitTime))return;
 // Damage itself drives per-contact floating numbers and hit flash. Play the
 // authored attack cue and the single basic-hit transaction only on contact 1.
 if(ContactIndex==0)
 {
  if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())
   Audio->CommittedImpact(TEXT("OriginHit"),C.RoundId,C.ActionId,false,C.BasicAttackIndex,0,FirstHitLocation);
 }
 if(IsOriginStrikeCurrent(C.ActionId,HitTime))if(auto* Visual=Unit->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>())
  Visual->ShowOriginAttackContact(FirstHitLocation,C.RoundId,C.ActionId,ContactIndex,Strike.AttackIndex);
 if(ContactIndex==0&&IsOriginStrikeCurrent(C.ActionId,HitTime))Battle->BasicHit(Unit.Get(),C.Target.Get(),C.ActionId);
}
void UEGElysiaHerrscherSkillBehavior::TickOriginStrikes()
{
 if(!IsCurrentBattle()||!Unit->GetWorld())return;
 const double Now=Unit->GetWorld()->GetTimeSeconds();
 // A frame may span the origin deadline. Settle only contacts whose scheduled
 // time was strictly before it, before switching form. Advance the entry
 // before damage: callbacks may synchronously cancel or end the battle.
 for(;;)
 {
  int32 Index=INDEX_NONE;double Earliest=TNumericLimits<double>::Max();
  for(int32 I=0;I<PendingOriginStrikes.Num();++I)
  {
   const auto& P=PendingOriginStrikes[I];
   if(P.Remaining>0&&P.NextHitTime<=Now&&P.NextHitTime<OriginEndTime&&P.NextHitTime<Earliest)
   {Index=I;Earliest=P.NextHitTime;}
  }
  if(Index==INDEX_NONE)break;
  const FPendingOriginStrike Snapshot=PendingOriginStrikes[Index];
  PendingOriginStrikes[Index].NextHitTime+=Snapshot.ContactInterval;
  --PendingOriginStrikes[Index].Remaining;
  ApplyOriginStrike(Snapshot,.2f,3-Snapshot.Remaining,Snapshot.NextHitTime);
  const int32 Current=PendingOriginStrikes.IndexOfByPredicate([&Snapshot](const FPendingOriginStrike& P){return P.Context.ActionId==Snapshot.Context.ActionId;});
  if(Current!=INDEX_NONE&&PendingOriginStrikes[Current].Remaining<=0)PendingOriginStrikes.RemoveAt(Current);
  if(!IsCurrentBattle()||!Battle->IsCombatRunning()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 }
}
void UEGElysiaHerrscherSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 ActiveAction=C.ActionId;bActionUltimate=C.bUltimate;bActionOrigin=Phase==EPhase::Origin;
 bPassiveStatusDirty=true;
 bActionFinisher=C.bUltimate&&bFinisherStarting;bFinisherReleased=false;
 if(C.bUltimate&&!bActionFinisher)
 {
  Phase=EPhase::Entering;bEntryReleased=false;
  // Reevaluate Mana to mark any non-periodic modifiers already active before
  // entry. GAS owns the value; no separate mana balance is introduced.
  auto* ASC=Unit->GetAbilitySystemComponent();const auto Mana=UEGAttributeSet::GetManaAttribute();
  ASC->SetNumericAttributeBase(Mana,ASC->GetNumericAttributeBase(Mana));
  // A failed acquisition leaves existing user/engine pause ownership intact.
  Battle->BeginHumanEntryPause(Unit.Get(),RoundId,C.ActionId);
  PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::Entering,C.ActionId);
 }
 else if(bActionFinisher)PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::FinisherBegin,C.ActionId);
}
void UEGElysiaHerrscherSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsActionCurrent(C))return;
 if(!C.bUltimate&&bActionOrigin&&Unit->GetWorld()->GetTimeSeconds()>=OriginEndTime)return;
 const auto* D=Cast<UEGElysiaHerrscherSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 if(C.bUltimate&&!bActionFinisher)
 {
  if(Event==TEXT("Release")&&!bEntryReleased)
  {
   // Release commits entry: the existing source shield is refilled here,
   // while the eight-second combat phase still starts only on Finish.
   bEntryReleased=true;
   if(!Skill->CommitStigmataUltimate(C.ActionId)||!IsActionCurrent(C))return;
   Skill->GrantSourceShield(CrystalShield,Unit->GetAttributes()->GetMaxHealth()*D->ShieldHealthRatio*Unit->GetAttributes()->GetSkillMultiplier(),TNumericLimits<float>::Max());
   if(!IsActionCurrent(C))return;
   Skill->ClearControlForImmunity();if(!IsActionCurrent(C))return;
   PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::EntryReleased,C.ActionId);
  }
  return;
 }
 if(C.bUltimate)
 {
  if((Event!=TEXT("Hit")&&Event!=TEXT("Release"))||bFinisherReleased)return;
  bFinisherReleased=true;
  PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::FinisherReleased,C.ActionId);
  if(!IsActionCurrent(C))return;
  AEGChessUnit* Main=C.Target.Get();if(!Unit->GetCombat()->IsValidEnemy(Main))Main=Unit->GetCombat()->FindNearestEnemy();
  if(!Unit->GetCombat()->IsValidEnemy(Main))return;
  if(auto* Stigmata=Battle->GetStigmata();Stigmata&&Stigmata->Has(TEXT("Flawless"))&&Stigmata->IsPlayerHero(Unit.Get()))Stigmata->EmitTrigger(TEXT("Flawless"),Unit.Get());
  TSet<TWeakObjectPtr<AEGChessUnit>> Seen;bool bPlayedImpact=false;
  const auto Targets=Battle->Units();
  for(auto* Target:Targets)
  {
   if(!IsActionCurrent(C))break;
   if(!Unit->GetCombat()->IsValidEnemy(Target)||Seen.Contains(Target))continue;
   const auto* Stigmata=Battle->GetStigmata();
   const float Bonus=Stigmata&&Stigmata->Has(TEXT("Flawless"))&&Stigmata->IsPlayerHero(Unit.Get())?Stigmata->FlawlessFinisherMultiplier:1.f;
   Seen.Add(Target);
   const bool bHit=Battle->Damage(Unit.Get(),Target,C.Attack*D->FinisherMain*Bonus*Unit->GetAttributes()->GetSkillMultiplier(),C.ActionId);
   if(bHit&&!bPlayedImpact&&IsActionCurrent(C))
   {
    bPlayedImpact=true;FVector Center=Unit->GetActorLocation();
    if(const auto* Board=Layout(Unit.Get());Board&&Board->Columns>0&&Board->Rows>0)
    {
     const int32 PlayerRows=FMath::Clamp(Board->PlayerRows,0,Board->Rows);
     const int32 EnemyStart=FMath::Clamp(PlayerRows+Board->NeutralRows,0,Board->Rows);
     const EEGFaction Faction=Unit->GetFaction_Implementation();
     if(PlayerRows>0&&EnemyStart<Board->Rows&&FMath::IsFinite(Board->CellDepth)&&Board->CellDepth>0.f&&
        (Faction==EEGFaction::Player||Faction==EEGFaction::Enemy))
     {
      const float PlayerY=(.5f*PlayerRows-.5f*Board->Rows)*Board->CellDepth;
      const float EnemyY=(.5f*(EnemyStart+Board->Rows)-.5f*Board->Rows)*Board->CellDepth;
      Center=Board->GetActorTransform().TransformPosition(FVector(0,Faction==EEGFaction::Player?EnemyY:PlayerY,0));
     }
    }
    if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())
     Audio->CommittedImpact(TEXT("FinisherHit"),C.RoundId,C.ActionId,true,1,0,Center);
   }
  }
  return;
 }
 if(Event!=TEXT("Hit")||SubmittedHits.Contains(C.ActionId))return;
 SubmittedHits.Add(C.ActionId);
 AEGChessUnit* Main=C.Target.Get();if(!Unit->GetCombat()->IsValidEnemy(Main))return;
 const auto* Board=Layout(Unit.Get());
 const bool bPierce=!bActionOrigin&&C.BasicAttackIndex==2;
 if(!bActionOrigin)
 {
  FVector Start=Unit->GetActionOrigin()+FVector(0,0,D->ArrowFallbackHeightOffset);
  FVector BoneLocation;
  if(!D->ArrowLaunchBone.IsNone())
  {
   if(Skill->GetAssemblyBoneLocation(TEXT("Body"),D->ArrowLaunchBone,BoneLocation))Start=BoneLocation;
   else if(const auto* Mesh=Unit->GetVisiblePresentationMesh();Mesh&&Mesh->DoesSocketExist(D->ArrowLaunchBone))Start=Mesh->GetSocketLocation(D->ArrowLaunchBone);
  }
  const float Height=Main->GetDefinition()->GetPresentationHeight();
  const float HalfCapsule=Main->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
  const FVector Aim=Main->GetActorLocation()+FVector(0,0,Height>0.f?Height*.5f-HalfCapsule:0.f);
  const FVector Direction=(Aim-Start).GetSafeNormal();
  if(Direction.IsNearlyZero())return;
  FActorSpawnParameters Params;Params.Owner=Unit.Get();Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  UClass* ProjectileClass=D->ArrowProjectileClass?D->ArrowProjectileClass.Get():AEGHumanArrowProjectile::StaticClass();
  auto* Arrow=Unit->GetWorld()->SpawnActor<AEGHumanArrowProjectile>(ProjectileClass,Start,Direction.Rotation(),Params);
  if(!Arrow)return;
  Arrow->Battle=Battle;Arrow->Round=C.RoundId;Arrow->Source=Unit;Arrow->Target=Main;Arrow->AttackId=C.ActionId;Arrow->SourceAttackIndex=C.BasicAttackIndex;
  Arrow->RawDamage=C.Attack*(bPierce?D->PiercingArrow:D->BowAttack);
  float HalfWidth=D->ArrowRadius;
  if(bPierce&&Board)
  {
   const FVector Scale=Board->GetActorScale3D();
   HalfWidth=FMath::Max(HalfWidth,D->ArrowLaneWidthCells*.5f*FMath::Min(Board->CellWidth*FMath::Abs(Scale.X),Board->CellDepth*FMath::Abs(Scale.Y)));
  }
  Arrow->InitializeArrow(this,Direction,D->ArrowSpeed,D->ArrowRadius,HalfWidth,D->ArrowLifetime,bPierce,FVector::Distance(Start,Aim));
  // A released projectile owns its hit lifecycle independently of the action.
  // Commit the counter before mana callbacks can cancel/reenter this action.
  if(IsActionCurrent(C))
  {
   BowCursor=(BowCursor+1)%FMath::Max(1,D->BowAttackInterval);bPassiveStatusDirty=true;
   Unit->RestoreMana(Unit->GetAttributes()->GetManaOnHit());
  }
  return;
 }
 const double FirstHitTime=Unit->GetWorld()->GetTimeSeconds();
 FPendingOriginStrike Strike;Strike.Context=C;Strike.ImpactPoint=Main->GetActorLocation();Strike.AttackIndex=OriginCursor;
 // The extra two contacts are 0.12 and 0.24 seconds in animation time.
 // Snapshot the actual montage speed (including the asset's RateScale) at
 // the committed notify; completed actions may outlive their montage.
 float EffectiveRate=FMath::Max(.1f,Unit->GetAttributes()->GetAttackSpeed()*D->OriginActionPlaybackRate);
 if(const auto* Montage=Skill->GetActiveMontage())
 {
  if(const auto* Mesh=Unit->GetVisiblePresentationMesh())if(const auto* Anim=Mesh->GetAnimInstance())
  {
   const float Rate=Anim->Montage_GetPlayRate(Montage)*Montage->RateScale;
   if(FMath::IsFinite(Rate)&&Rate>0.f)EffectiveRate=Rate;
  }
 }
 Strike.ContactInterval=.12/FMath::Max(.01f,EffectiveRate);
 Strike.NextHitTime=FirstHitTime+Strike.ContactInterval;
 TSet<TWeakObjectPtr<AEGChessUnit>> Seen;
 const auto Targets=Battle->Units();
 for(auto* Target:Targets)
 {
  if(!IsActionCurrent(C))break;
  if(!Unit->GetCombat()->IsValidEnemy(Target)||Seen.Contains(Target))continue;
  float Multiplier=0.f;
  if(Target==Main)Multiplier=D->GetScepterMain(Skill->GetLevel());
  else if(InCellRadius(Board,Main,Target,D->ScepterSplashRadiusCells))Multiplier=D->GetScepterSplash(Skill->GetLevel());
  if(Multiplier<=0.f)continue;
  Seen.Add(Target);FOriginStrikeTarget Entry;Entry.Unit=Target;Entry.Multiplier=Multiplier;Entry.bMain=Target==Main;Strike.Targets.Add(Entry);
 }
 if(Strike.Targets.IsEmpty()||!IsActionCurrent(C))return;
 PendingOriginStrikes.Add(Strike);
 ApplyOriginStrike(Strike,.6f,0,FirstHitTime);
}
void UEGElysiaHerrscherSkillBehavior::ReturnToBow()
{
 PendingOriginStrikes.Reset();
 Phase=EPhase::Bow;BowCursor=OriginCursor=0;OriginEndTime=0;bEntryReleased=bFinisherReleased=false;
 if(Unit.IsValid()&&Unit->HasAuthority())Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
 if(Skill.IsValid())Skill->DiscardDeferredStigmataMana();
 if(Skill.IsValid())Skill->OnStatusChanged.Broadcast();
 PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::ReturnBow,ActiveAction);
}
void UEGElysiaHerrscherSkillBehavior::ActionEnded(bool Completed)
{
 if(Completed)
 {for(auto& Strike:PendingOriginStrikes)if(Strike.Context.ActionId==ActiveAction)Strike.bActionCompleted=true;}
 else PendingOriginStrikes.RemoveAll([this](const FPendingOriginStrike& Strike){return Strike.Context.ActionId==ActiveAction;});
 if(bActionUltimate&&!bActionFinisher&&Battle.IsValid())Battle->EndHumanEntryPause(RoundId,ActiveAction);
 if(!Completed&&bActionFinisher)
 {
  for(const auto& Reaction:FinisherReactions)if(Reaction.IsValid())Reaction->FinishReaction();
  FinisherReactions.Reset();
 }
 if(bActionUltimate)
 {
  if(bActionFinisher){ReturnToBow();}
  else if(Completed&&bEntryReleased&&IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(Unit.Get()))
  {
   const auto* D=Cast<UEGElysiaHerrscherSkillDefinition>(Unit->GetDefinition()->SkillDefinition);
   if(D)
   {
    Phase=EPhase::Origin;OriginCursor=0;OriginEndTime=Unit->GetWorld()->GetTimeSeconds()+D->OriginSeconds;
    Skill->OnStatusChanged.Broadcast();
    PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::Origin,ActiveAction);
   }
   else ReturnToBow();
  }
  else
  {
   const bool bRefund=!bEntryReleased&&IsCurrentBattle()&&Unit.IsValid()&&!Unit->IsDeathPending()&&IEGCombatant::Execute_IsAlive(Unit.Get());
   const FGuid RefundingAction=ActiveAction;ReturnToBow();
   // ReturnToBow releases the mana gate first. Events can reenter and cancel;
   // only the same still-live transaction may receive its uncommitted cost.
   if(bRefund&&IsCurrentBattle()&&Unit.IsValid()&&!Unit->IsDeathPending()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&
      Skill.IsValid()&&Skill->IsActionRunning()&&Skill->ActionId==RefundingAction)Skill->RefundUncommittedUltimate();
  }
 }
 else if(Completed&&IsCurrentBattle())
 {
  if(bActionOrigin)OriginCursor=(OriginCursor+1)%2;
 }
 ActiveAction.Invalidate();bActionUltimate=bActionOrigin=bActionFinisher=false;bPassiveStatusDirty=true;
}
void UEGElysiaHerrscherSkillBehavior::TickBattle(float DeltaSeconds)
{
 FinisherReactions.RemoveAll([](const auto& Reaction){return !Reaction.IsValid();});
 if(!IsCurrentBattle()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 if(Phase==EPhase::Origin)TickOriginStrikes();
 if(!IsCurrentBattle()||!Battle->IsCombatRunning()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 // Observers run after the preceding action transaction has finished.
 if(bPassiveStatusDirty&&Skill.IsValid())
 {
  bPassiveStatusDirty=false;Skill->OnStatusChanged.Broadcast();
  if(!IsCurrentBattle()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 }
 if(Phase==EPhase::Origin&&Unit->GetWorld()->GetTimeSeconds()>=OriginEndTime)
 {
  PendingOriginStrikes.Reset();
  Phase=EPhase::FinisherPending;bPassiveStatusDirty=true;PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::FinisherPending,Skill.IsValid()&&Skill->IsActionRunning()?Skill->ActionId:FGuid());
  if(!IsCurrentBattle()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
  if(Skill->IsActionRunning())Skill->CancelAction();
  if(!IsCurrentBattle()||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
  if(Skill->GetLevel()<11){ReturnToBow();return;}
 }
 if(Phase!=EPhase::FinisherPending)return;
 auto* Target=Unit->GetCombat()->FindNearestEnemy();
 if(!Unit->GetCombat()->IsValidEnemy(Target))
 {
  if(Skill->IsActionRunning())Skill->CancelAction();
  if(IsCurrentBattle())ReturnToBow();
  return;
 }
 if(Skill->IsActionRunning())return;
  Phase=EPhase::Finishing;bFinisherStarting=true;bPassiveStatusDirty=true;
 TWeakObjectPtr<UEGElysiaHerrscherSkillBehavior> WeakThis(this);
 const bool bStarted=Skill->BeginInternalUltimateStage(1,Target);
 if(!WeakThis.IsValid())return;
 bFinisherStarting=false;
 if(!bStarted&&IsCurrentBattle())ReturnToBow();
}
void UEGElysiaHerrscherSkillBehavior::SuspendBattleClock(float DeltaSeconds)
{
 if(OriginEndTime>0.&&FMath::IsFinite(DeltaSeconds)&&DeltaSeconds>0.f)
 {
  OriginEndTime+=DeltaSeconds;
  for(auto& Strike:PendingOriginStrikes)Strike.NextHitTime+=DeltaSeconds;
 }
}
void UEGElysiaHerrscherSkillBehavior::EndBattle()
{
 if(Battle.IsValid())Battle->EndHumanEntryPause(RoundId,ActiveAction);
 for(const auto& Reaction:FinisherReactions)if(Reaction.IsValid())Reaction->FinishReaction();
 FinisherReactions.Reset();
 const FGuid EndAction=ActiveAction;
 PendingOriginStrikes.Reset();
 Phase=EPhase::Bow;BowCursor=OriginCursor=0;OriginEndTime=0;ActiveAction.Invalidate();SubmittedHits.Reset();
 // Retain weak effect handles until this Behavior is discarded: a still-active
 // GE must not resurrect its suppressed mana after Stop or death.
 if(ManaEffectRemovedDelegate.IsValid()&&Unit.IsValid())Unit->GetAbilitySystemComponent()->OnAnyGameplayEffectRemovedDelegate().Remove(ManaEffectRemovedDelegate);
 ManaEffectRemovedDelegate.Reset();
 if(PresentationPhase!=EEGElysiaHerrscherPresentationPhase::BattleEnd)PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase::BattleEnd,EndAction);
 bActionUltimate=bActionOrigin=bEntryReleased=bFinisherStarting=bActionFinisher=bFinisherReleased=bPassiveStatusDirty=false;
 Super::EndBattle();
}
