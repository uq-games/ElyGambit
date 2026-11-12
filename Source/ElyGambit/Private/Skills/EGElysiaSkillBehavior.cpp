#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGSupportSkillUtils.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatGeometry.h"
#include "Components/EGCombatAudioComponent.h"
#include "Presentation/EGElysiaCombatVisual.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"

bool UEGElysiaSkillBehavior::UsesArrowRain() const
{
 const auto* D=Unit.IsValid()&&Unit->GetDefinition()?Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
 return D&&D->bArrowRainUltimate;
}
bool UEGElysiaSkillBehavior::IsEmpowered() const
{return !UsesArrowRain()&&DomeEndTime>0&&Unit.IsValid()&&Unit->GetWorld()&&Unit->GetWorld()->GetTimeSeconds()<DomeEndTime;}
float UEGElysiaSkillBehavior::GetEmpoweredRadius() const
{
 if(IsArrowRainActive())return RainRadius;
 const auto* U=Unit.Get();
 const auto* D=U&&U->GetDefinition()?Cast<UEGElysiaSkillDefinition>(U->GetDefinition()->SkillDefinition):nullptr;
 if(!D)return 0.f;
 const auto* GM=U->GetWorld()?U->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Board=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
 if(!D->bArrowRainUltimate)
 {
  if(!(D->EmpoweredRadius<=0.f))return D->EmpoweredRadius;
  return Board?2.f*FMath::Max(Board->CellWidth*Board->GetActorScale3D().X,Board->CellDepth*Board->GetActorScale3D().Y):0.f;
 }
 if(!Board)return 0.f;
 const auto Basis=Board->GetActorTransform();
 const float Cell=FMath::Min(Board->CellWidth*Basis.GetScaledAxis(EAxis::X).Size(),Board->CellDepth*Basis.GetScaledAxis(EAxis::Y).Size());
 return FMath::IsFinite(Cell)&&Cell>0.f?Cell*D->ArrowRainRadiusCells:0.f;
}
float UEGElysiaSkillBehavior::GetArrowRainRemaining() const
{return IsArrowRainActive()&&Unit.IsValid()&&Unit->GetWorld()?FMath::Max(0.f,float(RainEndTime-Unit->GetWorld()->GetTimeSeconds())):0.f;}
FEGElysiaPassiveState UEGElysiaSkillBehavior::GetPassiveState() const
{
 FEGElysiaPassiveState State;
 if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return State;
 const auto* D=Unit->GetDefinition()?Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
 if(!D)return State;
 const bool bEmpowered=IsEmpowered();
 State.Phase=bEmpowered?EEGElysiaPassivePhase::Empowered:EEGElysiaPassivePhase::Normal;
 State.Progress=bEmpowered?0:NormalCursor;
 State.Threshold=FMath::Max(1,D->ChargedAttackInterval);
 State.bNextAttackEnhanced=PresentationIndex(false,0)==4;
 State.RemainingSeconds=bEmpowered?FMath::Max(0.f,float(DomeEndTime-Unit->GetWorld()->GetTimeSeconds())):0.f;
 State.RoundId=RoundId;State.ActionId=ActiveAction;
 return State;
}
void UEGElysiaSkillBehavior::ExpireEmpowerment()
{
 if(DomeEndTime<=0||IsEmpowered())return;
 DomeRemaining=0;DomeStartTime=DomeEndTime=0;NormalCursor=0;EmpoweredCursor=0;
 if(IsValid(DomeVisual))if(auto* Exit=DomeVisual->FinishDome())TrackVisual(Exit,FGuid());
 DomeVisual=nullptr;bStatusDirty=true;
}
int32 UEGElysiaSkillBehavior::PresentationIndex(bool Ultimate,int32 Default) const
{
 if(Ultimate)return 0;
 if(IsEmpowered())return 4;
 if(DomeEndTime>0)return 0;
 const auto* D=Unit.IsValid()&&Unit->GetDefinition()?Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
 const int32 Interval=D?FMath::Max(1,D->ChargedAttackInterval):5;
 const int32 Cursor=FMath::Max(0,NormalCursor)%Interval;
 return Cursor==Interval-1?4:Cursor%4;
}
int32 UEGElysiaSkillBehavior::GetSeedStacks(AEGChessUnit* Target) const
{return 0;}
void UEGElysiaSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 ExpireEmpowerment();
 bBoardBackstep=false;bActionEmpowered=!C.bUltimate&&IsEmpowered();ActiveAction=C.ActionId;bLastUltimate=C.bUltimate;bLastCharged=!C.bUltimate&&C.BasicAttackIndex==4;
 bStatusDirty=true;
 if(IsValid(ChargeVisual))ChargeVisual->Destroy();ChargeVisual=nullptr;
 if(C.bUltimate&&IsCurrentBattle())
 {
  // Capture once at cast start, even if the target moves or dies before Release.
  UltimatePoint=Unit->GetActorLocation();
  AEGChessUnit* Anchor=Unit.Get();
  if(Unit->GetCombat()->IsValidEnemy(C.Target.Get())){Anchor=C.Target.Get();UltimatePoint=Anchor->GetActorLocation();}
  // The enemy and caster can have different capsule heights. Snapshot the
  // target's ground with its location so the burst cannot sink below the board.
  UltimateGroundPoint=UltimatePoint-FVector(0,0,Anchor->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
  if(UsesArrowRain())
  {
   // Capsules differ greatly (for example, a chest's origin is not its
   // contact plane). Lock XY at cast start and project onto the actual board
   // rather than embedding ground-facing Niagara underneath its surface.
   const auto* GM=Unit->GetWorld()?Unit->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
   const auto* Board=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
   if(Board)
   {
    const FTransform Basis=Board->GetActorTransform();
    const FVector Scale=Basis.GetScale3D();
    if(!Basis.ContainsNaN()&&!FMath::IsNearlyZero(Scale.X)&&!FMath::IsNearlyZero(Scale.Y)&&!FMath::IsNearlyZero(Scale.Z))
    {
     FVector Local=Basis.InverseTransformPosition(UltimatePoint);Local.Z=0.f;
     const FVector BoardPoint=Basis.TransformPosition(Local);
     if(!BoardPoint.ContainsNaN())UltimateGroundPoint=BoardPoint;
    }
   }
  }
 }
 else if(bLastCharged&&Unit.IsValid()&&Skill.IsValid()&&Skill->ActionId==C.ActionId&&
         Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition)&&
         !Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition)->bBasicOnlyPresentation)
 {
  const FVector Origin=Unit->GetActionOrigin();
  ChargeVisual=AEGElysiaCombatVisual::Spawn(Unit.Get(),C.Target.Get(),Origin,Origin,30.f,4,true);
 }
}
void UEGElysiaSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId||!C.ActionId.IsValid()||C.ActionId!=ActiveAction||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const auto* D=Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 if(Event==TEXT("BoardBackstep"))
 {
  // The in-place private montage opts in after its hit event. Movement never submits damage.
  if(C.bUltimate||C.BasicAttackIndex!=1||C.ActionId!=ActiveAction||!Fired.Contains(C.ActionId)||bBoardBackstep)return;
  auto* U=Unit.Get();auto* Mesh=U->GetVisiblePresentationMesh();
  auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;auto* Montage=Skill->GetActiveMontage();
  if(!Instance||!Montage)return;
  BackstepStart=U->GetActorLocation();BackstepEnd=BackstepStart;
  const FVector Away=C.Target.IsValid()?(BackstepStart-C.Target->GetActorLocation()).GetSafeNormal2D():-U->GetActorForwardVector();
  const auto* GM=U->GetWorld()->GetAuthGameMode<AEGChessGameMode>();
  const auto* Board=GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
  // No board means no retreat. A blocked step shortens in place, never searches sideways.
  if(!Board||Away.IsNearlyZero())return;
  const FTransform Basis=Board->GetActorTransform();
  const float Radius=UEGCombatComponent::BlockingRadius(U);
  const float HalfX=FMath::Max(0.f,Board->Columns*Board->CellWidth*.5f-Radius/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::X).Size()));
  const float HalfY=FMath::Max(0.f,Board->Rows*Board->CellDepth*.5f-Radius/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size()));
  const float Distance=FMath::Min(150.f,Board->CellDepth*Basis.GetScaledAxis(EAxis::Y).Size()*.3f);
  for(int32 Step=10;Step>0;--Step)
  {
   const FVector Desired=BackstepStart+Away*(Distance*Step/10.f);
   const FVector Local=Basis.InverseTransformPosition(Desired);
   if(FMath::Abs(Local.X)>HalfX||FMath::Abs(Local.Y)>HalfY)continue;
   if(U->GetCombat()->IsBoardPathClear(BackstepStart,Desired)&&U->GetCombat()->FindFreeBoardLocation(Desired).Equals(Desired,.1f)){BackstepEnd=Desired;break;}
  }
  BackstepPosition=Instance->Montage_GetPosition(Montage);bBoardBackstep=!BackstepEnd.Equals(BackstepStart,.1f);return;
 }
 if(C.bUltimate)
 {
  if(Event!=TEXT("Release")||Fired.Contains(C.ActionId))return;
  if(!D->bArrowRainUltimate){Fired.Add(C.ActionId);return;} // Legacy enemy: its field begins on successful Finish.
  // Commit the original action exactly once before the independent rain is
  // published. Damage keeps this ID for Stigmata ownership/ultimate effects.
  const float Radius=GetEmpoweredRadius();
  if(IsArrowRainActive()||!D->HasValidSkillRules()||!FMath::IsFinite(Radius)||Radius<=0.f||!FMath::IsFinite(C.Attack)||C.Attack<0.f
   ||!FMath::IsFinite(C.Multiplier)||C.Multiplier<0.f)return;
  const float RawPerPulse=C.Attack*C.Multiplier*D->ArrowRainTotalMultiplier*Unit->GetAttributes()->GetSkillMultiplier()/float(D->ArrowRainPulses);
  if(!FMath::IsFinite(RawPerPulse)||RawPerPulse<0.f)return;
  Fired.Add(C.ActionId);
  if(!Skill.IsValid()||!Skill->CommitStigmataUltimate(C.ActionId)||!IsCurrentBattle()
   ||ActiveAction!=C.ActionId||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
  const auto* Stigmata=Battle->GetStigmata();
  const float Bonus=Stigmata&&Stigmata->Has(TEXT("Flawless"))&&Stigmata->IsPlayerHero(Unit.Get())
   &&FMath::IsFinite(Stigmata->FlawlessSeconds)?FMath::Max(0.f,Stigmata->FlawlessSeconds):0.f;
  RainInterval=double(D->ArrowRainSeconds)/double(D->ArrowRainPulses);
  const double Duration=double(D->ArrowRainSeconds)+Bonus;
  if(!FMath::IsFinite(RainInterval)||RainInterval<=0.||!FMath::IsFinite(Duration)||Duration<=0.)return;
  RainPulseLimit=FMath::Clamp(FMath::FloorToInt(FMath::Min(4096.,Duration/RainInterval+1.e-6)),1,4096);
  RainPulseIndex=0;RainRadius=Radius;RainCenter=UltimateGroundPoint;
  RainDamage=RawPerPulse;
  RainStartTime=Unit->GetWorld()->GetTimeSeconds();RainEndTime=RainStartTime+Duration;
  RainActionId=C.ActionId;DomeRemaining=float(Duration);bStatusDirty=true;
  if(Bonus>0.f)Battle->GetStigmata()->EmitTrigger(TEXT("Flawless"),Unit.Get());
  if(!IsCurrentBattle()||RainActionId!=C.ActionId||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
  if(!D->bBasicOnlyPresentation)
  {
   DomeVisual=AEGElysiaCombatVisual::Spawn(Unit.Get(),nullptr,RainCenter,RainCenter,float(Duration),2,false,false,false,RainRadius);
   if(IsValid(DomeVisual))TrackVisual(DomeVisual,FGuid());
  }
  return;
 }
 if(Event!=TEXT("Hit")||Fired.Contains(C.ActionId)||!Unit->GetCombat()->IsValidEnemy(C.Target.Get())||!Unit->GetCombat()->IsInAttackRange(C.Target.Get()))return;
 if(IsValid(ChargeVisual))ChargeVisual->Destroy();ChargeVisual=nullptr;
 Fired.Add(C.ActionId);
 FHit H;H.Target=C.Target;H.Id=C.ActionId;H.DueTime=Unit->GetWorld()->GetTimeSeconds()+D->ArrowFlightSeconds;H.Attack=C.Attack;H.Charged=C.BasicAttackIndex==4;H.Tier=C.Tier;H.AttackIndex=C.BasicAttackIndex;
 const FVector End=C.Target->GetActorLocation()+FVector(0,0,C.Target->GetDefinition()->GetPresentationHeight()*.45f-C.Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
 if(!D->bBasicOnlyPresentation)if(auto* V=AEGElysiaCombatVisual::Spawn(Unit.Get(),C.Target.Get(),Unit->GetActionOrigin(),End,D->ArrowFlightSeconds,0,H.Charged))
 {V->SetFlightDeadline(H.DueTime);H.Visual=V;TrackVisual(V,C.ActionId);}
 Pending.Add(H);
}
void UEGElysiaSkillBehavior::Resolve(const FHit& H)
{
 auto* U=Unit.Get();auto* T=H.Target.Get();
 if(!IsCurrentBattle()||!U->HasAuthority()||!IEGCombatant::Execute_IsAlive(U)||!U->GetCombat()->IsValidEnemy(T))return;
 const auto* D=Cast<UEGElysiaSkillDefinition>(U->GetDefinition()->SkillDefinition);if(!D)return;
 const FGuid HitRound=RoundId;
 const FVector Contact=T->GetActorLocation()+FVector(0,0,T->GetDefinition()->GetPresentationHeight()*.45f-T->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
 if(H.Visual.IsValid())H.Visual->CompleteFlight(Contact);
 if(H.Ricochet)
 {
  const FVector Point=T->GetActorLocation();
  if(Battle->Damage(U,T,H.Attack,H.Id)&&IsCurrentBattle()&&RoundId==HitRound&&IsValid(U)&&IEGCombatant::Execute_IsAlive(U))
  {
   ++RicochetHitCount;
   if(!D->bBasicOnlyPresentation)if(auto* V=AEGElysiaCombatVisual::Spawn(U,T,Contact,Contact,.24f,3,false,true,true))TrackVisual(V,H.Id);
  }
  return;
 }
 const FVector HitPoint=T->GetActorLocation();
 FVector HitDomeCenter=FVector::ZeroVector;bool bMayRicochet=false;
 const bool bRainBounce=UsesArrowRain()&&H.Tier>=2&&IsArrowRainActive()&&H.DueTime>=RainStartTime&&H.DueTime<RainEndTime;
 const float DomeRadius=bRainBounce?EGSupport::Radius(U,D->EmpoweredRadius/500.f):GetEmpoweredRadius();
 if(bRainBounce){HitDomeCenter=HitPoint;bMayRicochet=true;}
 if(!UsesArrowRain()&&H.Charged)for(int32 Index=DomeWindows.Num()-1;Index>=0;--Index)
 {
  const auto& Window=DomeWindows[Index];
  if(H.DueTime>=Window.Start&&H.DueTime<Window.End)
  {HitDomeCenter=Window.Center;bMayRicochet=DomeRadius>0.f&&EGCombatGeometry::PointDistanceSquared(HitDomeCenter,T)<=FMath::Square(DomeRadius);break;}
 }
 const float Raw=H.Attack*(H.Charged?D->ChargedMultiplier:D->NormalAttackMultiplier);
 if(!Battle->Damage(U,T,Raw,H.Id,true))return;
 if(!IsCurrentBattle()||RoundId!=HitRound||!IsValid(U)||!IEGCombatant::Execute_IsAlive(U))return;
 ++PrimaryHitCount;if(H.Charged)++ChargedHitCount;
 if(bMayRicochet)
 {
  AEGChessUnit* Best=nullptr;double BestDistance=TNumericLimits<double>::Max();
  for(auto* Other:Battle->Units())
  {
   if(Other==T||!U->GetCombat()->IsValidEnemy(Other)||EGCombatGeometry::PointDistanceSquared(HitDomeCenter,Other)>FMath::Square(DomeRadius))continue;
   const double Distance=FVector::DistSquared2D(HitPoint,Other->GetActorLocation());
   if(Distance<BestDistance){Best=Other;BestDistance=Distance;}
  }
  if(Best)
  {
   FHit Bounce;Bounce.Target=Best;Bounce.Id=H.Id;Bounce.DueTime=U->GetWorld()->GetTimeSeconds()+D->ArrowFlightSeconds;Bounce.Attack=H.Attack*D->DomeRicochetMultiplier*U->GetAttributes()->GetSkillMultiplier();Bounce.Ricochet=true;
   const FVector End=Best->GetActorLocation()+FVector(0,0,Best->GetDefinition()->GetPresentationHeight()*.45f-Best->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
   if(!D->bBasicOnlyPresentation)if(auto* V=AEGElysiaCombatVisual::Spawn(U,Best,Contact,End,D->ArrowFlightSeconds,0,false,true,true))
   {V->SetFlightDeadline(Bounce.DueTime);Bounce.Visual=V;TrackVisual(V,H.Id);}
   Pending.Add(Bounce);
  }
 }
 if(!D->bBasicOnlyPresentation&&IsValid(T))if(auto* V=AEGElysiaCombatVisual::Spawn(U,T,Contact,Contact,.24f,3,H.Charged))TrackVisual(V,H.Id);
 if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())
 {
  if(D->bArrowRainUltimate)Audio->CommittedImpact(TEXT("Hit"),HitRound,H.Id,false,H.AttackIndex,0,HitPoint);
  else Audio->ConfirmHit(TEXT("Hit"),H.Id,T); // Preserve legacy enemy audio policy.
 }
 Battle->BasicHit(U,T,H.Id);
 if(IsCurrentBattle()&&RoundId==HitRound)bStatusDirty=true;
}
void UEGElysiaSkillBehavior::ActionEnded(bool Completed)
{
 if(IsValid(ChargeVisual))ChargeVisual->Destroy();ChargeVisual=nullptr;
 bBoardBackstep=false;
 if(bLastUltimate&&UsesArrowRain()&&!Fired.Contains(ActiveAction)&&Skill.IsValid())Skill->RefundUncommittedUltimate();
 if(Completed&&bLastUltimate&&!UsesArrowRain()&&Fired.Contains(ActiveAction)&&IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(Unit.Get()))
 {
  if(!Skill->CommitStigmataUltimate(ActiveAction))return;
  if(const auto* D=Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition))
  {
   const auto* Stigmata=Battle->GetStigmata();
   const float Bonus=Stigmata&&Stigmata->Has(TEXT("Flawless"))&&Stigmata->IsPlayerHero(Unit.Get())?Stigmata->FlawlessSeconds:0.f;
   DomeRemaining=FMath::Max(0.f,D->EmpoweredSeconds+Bonus);DomeStartTime=Unit->GetWorld()->GetTimeSeconds();DomeEndTime=DomeStartTime+DomeRemaining;DomeCenter=UltimatePoint;EmpoweredCursor=0;bStatusDirty=true;
   if(Bonus>0.f)Battle->GetStigmata()->EmitTrigger(TEXT("Flawless"),Unit.Get());
   if(DomeRemaining>0.f){FDomeWindow Window;Window.Start=DomeStartTime;Window.End=DomeEndTime;Window.Center=DomeCenter;DomeWindows.Add(Window);}
   if(DomeRemaining>0.f&&!D->bBasicOnlyPresentation)DomeVisual=AEGElysiaCombatVisual::Spawn(Unit.Get(),nullptr,UltimateGroundPoint,UltimateGroundPoint,DomeRemaining,2);
  }
 }
 else if(Completed&&!bLastUltimate)
 {
  if(!bActionEmpowered&&!IsEmpowered())
  {
   const auto* D=Unit.IsValid()&&Unit->GetDefinition()?Cast<UEGElysiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
   NormalCursor=(NormalCursor+1)%(D?FMath::Max(1,D->ChargedAttackInterval):5);
  }
 }
 if(!Completed)
 {
  // Only cancellation of the rain's own cast can cancel it. A subsequent
  // basic attack has a different action token and must not erase the rain.
  if(ActiveAction.IsValid()&&ActiveAction==RainActionId)EndArrowRain(false);
  Pending.RemoveAll([this](const FHit& H){return H.Id==ActiveAction;});
  for(auto It=VisualActions.CreateIterator();It;++It)if(It.Value()==ActiveAction)
  {if(It.Key().IsValid())It.Key()->Destroy();It.RemoveCurrent();}
 }
 if(Unit.IsValid()&&!IEGCombatant::Execute_IsAlive(Unit.Get())){Pending.Reset();ClearVisuals();}
 ActiveAction.Invalidate();
 bStatusDirty=true;
}
void UEGElysiaSkillBehavior::EndArrowRain(bool bNatural)
{
 if(!RainActionId.IsValid())return;
 RainActionId.Invalidate();RainStartTime=RainEndTime=RainInterval=0.;RainRadius=0.f;RainDamage=0.f;RainPulseLimit=0;
 DomeRemaining=0.f;bStatusDirty=true;
 if(IsValid(DomeVisual))
 {
  if(bNatural)DomeVisual->FinishRain();
  else DomeVisual->Destroy();
 }
 DomeVisual=nullptr;
}
void UEGElysiaSkillBehavior::ResolveRainPulse()
{
 auto* U=Unit.Get();
 if(!IsCurrentBattle()||!IsArrowRainActive()||!U||!U->HasAuthority()||!IEGCombatant::Execute_IsAlive(U))return;
 const FGuid PulseRound=RoundId,PulseAction=RainActionId;
 const float Radius=RainRadius,Raw=RainDamage;const FVector Center=RainCenter;
 const int32 Pulse=++RainPulseIndex;
 if(IsValid(DomeVisual))DomeVisual->PulseRain(Pulse);
 TSet<AEGChessUnit*> Visited;bool bPlayedImpact=false;
 const auto Targets=Battle->Units();
 for(auto* Target:Targets)
 {
  // Damage callbacks may synchronously kill the caster, stop the battle or
  // start another round. Never continue an old pulse across that boundary.
  if(!IsCurrentBattle()||RoundId!=PulseRound||RainActionId!=PulseAction||!IsValid(U)||!IEGCombatant::Execute_IsAlive(U))return;
  if(!IsValid(Target)||Visited.Contains(Target)||!U->GetCombat()->IsValidEnemy(Target)
   ||EGCombatGeometry::PointDistanceSquared(Center,Target)>FMath::Square(Radius))continue;
  Visited.Add(Target);const FVector HitPoint=Target->GetActorLocation();
  const bool bApplied=Battle->Damage(U,Target,Raw,PulseAction);
  if(bApplied&&!bPlayedImpact&&IsCurrentBattle()&&RoundId==PulseRound&&RainActionId==PulseAction&&IsValid(U)&&IEGCombatant::Execute_IsAlive(U))
  {
   bPlayedImpact=true;
   if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())
    Audio->CommittedImpact(TEXT("RainHit"),PulseRound,PulseAction,true,0,Pulse,HitPoint);
  }
 }
}
void UEGElysiaSkillBehavior::TickAction(float Position)
{
 if(!bBoardBackstep||!IsCurrentBattle()||!ActiveAction.IsValid()||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get())||!FMath::IsFinite(Position))return;
 const float Alpha=FMath::Clamp((Position-BackstepPosition)/.45f,0.f,1.f);
 const FVector Desired=FMath::Lerp(BackstepStart,BackstepEnd,Alpha);
 // Other units may enter the landing footprint after takeoff. Stop rather than displace them.
 if(!Unit->GetCombat()->IsBoardPathClear(Unit->GetActorLocation(),Desired)||!Unit->GetCombat()->FindFreeBoardLocation(Desired).Equals(Desired,.1f)){bBoardBackstep=false;return;}
 FHitResult Hit;Unit->SetActorLocation(Desired,true,&Hit);
 if(Hit.bBlockingHit||Alpha>=1.f)bBoardBackstep=false;
}
void UEGElysiaSkillBehavior::TickBattle(float Delta)
{
 if(!IsCurrentBattle()||!Unit.IsValid()||!Unit->GetWorld())return;
 const FGuid TickRound=RoundId;
 const double Now=Unit->GetWorld()->GetTimeSeconds();
 // Resolve in scheduled order before expiring the field. The due timestamp,
 // not frame-end time, decides whether the hit occurred within the field.
 TArray<FHit> Due;
 for(int32 I=Pending.Num()-1;I>=0;--I)if(Pending[I].DueTime<=Now){Due.Add(Pending[I]);Pending.RemoveAt(I);}
 Due.Sort([](const FHit& A,const FHit& B){return A.DueTime<B.DueTime;});
 for(const FHit& Hit:Due){if(!IsCurrentBattle()||RoundId!=TickRound)break;Resolve(Hit);}
 if(!IsCurrentBattle()||RoundId!=TickRound)return;
 if(IsArrowRainActive())
 {
  const FGuid TickRain=RainActionId;
  if(!Unit->HasAuthority()){EndArrowRain(false);return;}
  while(IsCurrentBattle()&&RoundId==TickRound&&RainActionId==TickRain&&Unit.IsValid()&&Unit->HasAuthority()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&RainPulseIndex<RainPulseLimit
   &&RainStartTime+double(RainPulseIndex+1)*RainInterval<=Now+1.e-6)
  {
   const int32 Previous=RainPulseIndex;ResolveRainPulse();
   // A callback can invalidate authority or change generations synchronously;
   // never spin if a pulse could not advance its sequence index.
   if(RainPulseIndex==Previous)break;
  }
  if(!IsCurrentBattle()||RoundId!=TickRound)return;
  if(!Unit.IsValid()||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get())){EndArrowRain(false);return;}
  if(RainActionId==TickRain)
  {
   DomeRemaining=GetArrowRainRemaining();
   if(Now+1.e-6>=RainEndTime&&RainPulseIndex>=RainPulseLimit)EndArrowRain(true);
  }
 }
 if(DomeEndTime>0){DomeRemaining=FMath::Max(0.f,float(DomeEndTime-Now));ExpireEmpowerment();}
 DomeWindows.RemoveAll([Now](const FDomeWindow& Window){return Window.End<=Now;});
 for(auto It=Seeds.CreateIterator();It;++It)if(!It.Key().IsValid()||!IEGCombatant::Execute_IsAlive(It.Key().Get())){It.RemoveCurrent();bStatusDirty=true;}
 for(auto It=SeedVisuals.CreateIterator();It;++It)if(!It.Key().IsValid()||!IEGCombatant::Execute_IsAlive(It.Key().Get()))
 {if(It.Value().IsValid())It.Value()->Destroy();It.RemoveCurrent();}
 Transients.RemoveAll([](const auto& V){return !IsValid(V);});
 for(auto It=VisualActions.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
 // Defer observers until the transaction and its cleanup are complete.
 if(bStatusDirty&&Skill.IsValid()){bStatusDirty=false;Skill->OnStatusChanged.Broadcast();}
}
void UEGElysiaSkillBehavior::SuspendBattleClock(float Delta)
{
 if(!FMath::IsFinite(Delta)||Delta<=0.f)return;
 for(auto& Hit:Pending){Hit.DueTime+=Delta;if(Hit.Visual.IsValid())Hit.Visual->SetFlightDeadline(Hit.DueTime);}
 for(auto& Window:DomeWindows){Window.Start+=Delta;Window.End+=Delta;}
 if(DomeEndTime>0.){DomeStartTime+=Delta;DomeEndTime+=Delta;}
 if(RainActionId.IsValid()){RainStartTime+=Delta;RainEndTime+=Delta;}
}
void UEGElysiaSkillBehavior::TrackVisual(AEGElysiaCombatVisual* Visual,FGuid Action)
{if(IsValid(Visual)){Transients.Add(Visual);VisualActions.Add(Visual,Action);}}
void UEGElysiaSkillBehavior::ClearVisuals()
{if(IsValid(ChargeVisual))ChargeVisual->Destroy();ChargeVisual=nullptr;if(IsValid(DomeVisual))DomeVisual->Destroy();DomeVisual=nullptr;SeedVisuals.Reset();for(const auto& V:Transients)if(IsValid(V))V->Destroy();Transients.Reset();VisualActions.Reset();}
void UEGElysiaSkillBehavior::EndBattle()
{bBoardBackstep=false;EndArrowRain(false);Pending.Reset();Seeds.Reset();DomeWindows.Reset();ClearVisuals();Fired.Reset();NormalCursor=0;EmpoweredCursor=0;DomeRemaining=0;DomeStartTime=DomeEndTime=0;DomeCenter=FVector::ZeroVector;bActionEmpowered=false;bStatusDirty=false;PrimaryHitCount=ChargedHitCount=RicochetHitCount=SeedsApplied=0;RainPulseIndex=0;ActiveAction.Invalidate();Super::EndBattle();}
