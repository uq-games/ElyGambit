#include "Skills/EGHumanArrowProjectile.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Engine/World.h"

namespace
{
// The piercing lane is wide sideways but its tip remains short along the
// flight direction. Otherwise a half-cell lane would hit far ahead of the arrow.
bool SweepUnit(FVector From,FVector To,const AEGChessUnit* Unit,float TipRadius,float HalfWidth,double& OutTime)
{
 const FVector Delta=To-From;const double Distance=Delta.Size2D();
 if(Distance<=UE_DOUBLE_SMALL_NUMBER)return false;
 const FVector Forward=FVector(Delta.X,Delta.Y,0.)/Distance,Side(-Forward.Y,Forward.X,0.);
 const FVector Relative=Unit->GetActorLocation()-From;
 double AlongExtent=UEGCombatComponent::BodyRadius(Unit),SideExtent=AlongExtent;
 const auto* Mode=Unit->GetWorld()->GetAuthGameMode<AEGChessGameMode>();
 const auto* Board=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 const auto Footprint=Unit->GetDefinition()->GetPlacementFootprint();
 if(Board&&(Footprint.X>1||Footprint.Y>1))
 {
  const FTransform Frame=Board->GetActorTransform();
  const FVector X=Frame.TransformVector(FVector(Footprint.X*.5*Board->CellWidth,0,0));
  const FVector Y=Frame.TransformVector(FVector(0,Footprint.Y*.5*Board->CellDepth,0));
  AlongExtent=FMath::Abs(FVector::DotProduct(X,Forward))+FMath::Abs(FVector::DotProduct(Y,Forward));
  SideExtent=FMath::Abs(FVector::DotProduct(X,Side))+FMath::Abs(FVector::DotProduct(Y,Side));
 }
 if(FMath::Abs(FVector::DotProduct(Relative,Side))>=SideExtent+HalfWidth)return false;
 const double Along=FVector::DotProduct(Relative,Forward);
 const double Entry=Along-(AlongExtent+TipRadius),Exit=Along+(AlongExtent+TipRadius);
 if(Exit<0.||Entry>Distance)return false;
 double EnterTime=FMath::Max(0.,Entry/Distance),ExitTime=FMath::Min(1.,Exit/Distance);
 const double CapsuleHalf=Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
 const double Height=Unit->GetDefinition()->GetPresentationHeight();
 const double Bottom=Unit->GetActorLocation().Z-CapsuleHalf;
 const double MinZ=(Height>0.?Bottom:Unit->GetActorLocation().Z-CapsuleHalf)-TipRadius;
 const double MaxZ=(Height>0.?Bottom+Height:Unit->GetActorLocation().Z+CapsuleHalf)+TipRadius;
 if(FMath::Abs(Delta.Z)<UE_DOUBLE_SMALL_NUMBER)
 {
  if(From.Z<MinZ||From.Z>MaxZ)return false;
 }
 else
 {
  double A=(MinZ-From.Z)/Delta.Z,B=(MaxZ-From.Z)/Delta.Z;
  if(A>B)Swap(A,B);EnterTime=FMath::Max(EnterTime,A);ExitTime=FMath::Min(ExitTime,B);
 }
 if(EnterTime>ExitTime)return false;
 OutTime=EnterTime;return true;
}
}

void AEGHumanArrowProjectile::InitializeArrow(UEGElysiaHerrscherSkillBehavior* InBehavior,FVector InDirection,
 float InSpeed,float InRadius,float InHalfWidth,float InLifetime,bool bInPiercing,float InLevelDistance)
{
 Behavior=InBehavior;Direction=InDirection.GetSafeNormal();Speed=InSpeed;Radius=InRadius;
 HalfWidth=InHalfWidth;MaxLifetime=InLifetime;bPiercing=bInPiercing;DistanceUntilLevel=FMath::Max(0.f,InLevelDistance);
 bInitialized=!Direction.IsNearlyZero()&&FMath::IsFinite(Speed)&&Speed>0.f&&FMath::IsFinite(MaxLifetime)&&MaxLifetime>0.f;
 SetActorRotation(Direction.Rotation());
}

bool AEGHumanArrowProjectile::CanContinue() const
{
 const auto* OwnerUnit=Source.Get();
 return bInitialized&&Battle.IsValid()&&Battle->IsBattleActive()&&Battle->GetRoundId()==Round&&
  IsValid(OwnerUnit)&&OwnerUnit->HasAuthority()&&!OwnerUnit->IsDeathPending()&&
  IEGCombatant::Execute_IsAlive(OwnerUnit)&&Behavior.IsValid()&&
  OwnerUnit->GetHeroSkill()->GetBehavior()==Behavior.Get();
}

void AEGHumanArrowProjectile::Tick(float DeltaSeconds)
{
 // The base projectile has a different endpoint-based damage policy.
 AActor::Tick(DeltaSeconds);
 if(!CanContinue()){Destroy();return;}
 if(!Battle->IsCombatRunning())return;
 float Remaining=FMath::Min(FMath::Max(0.f,DeltaSeconds),FMath::Max(0.f,MaxLifetime-Elapsed));
 if(Remaining<=0.f){Destroy();return;}
 while(Remaining>0.f&&CanContinue())
 {
  if(bPiercing&&DistanceUntilLevel<=KINDA_SMALL_NUMBER)
  {
   DistanceUntilLevel=0.f;
   if(Direction.Z!=0.f){Direction=Direction.GetSafeNormal2D();SetActorRotation(Direction.Rotation());}
  }
  const float Step=bPiercing&&DistanceUntilLevel>0.f?FMath::Min(Remaining,DistanceUntilLevel/Speed):Remaining;
  const FVector From=GetActorLocation();FVector To=From+Direction*Speed*Step;
  AEGChessUnit* WallTarget=nullptr;
  for(auto* Candidate:Battle->Units())if(Source->GetCombat()->IsValidEnemy(Candidate))
   if(const auto* Wall=Candidate->FindComponentByClass<UEGCorruptionCombatComponent>();Wall&&Wall->BlocksMovement(From,To,0.f))
   {
    float Low=0.f,High=1.f;
    for(int32 I=0;I<16;++I){const float Middle=(Low+High)*.5f;if(Wall->BlocksMovement(From,FMath::Lerp(From,To,Middle),0.f))High=Middle;else Low=Middle;}
    To=FMath::Lerp(From,To,High);WallTarget=Candidate;break;
   }
  struct FContact {TWeakObjectPtr<AEGChessUnit> Unit;double Time;};
  TArray<FContact> Contacts;
  for(auto* Candidate:Battle->Units())
  {
   // The wall and its owner are one logical contact. The owner's capsule may
   // extend in front of the wall; never count it separately in the same sweep.
   if(Candidate==WallTarget||!Source->GetCombat()->IsValidEnemy(Candidate)||HitUnits.Contains(Candidate))continue;
   double Time=0.;if(SweepUnit(From,To,Candidate,Radius,HalfWidth,Time))Contacts.Add({Candidate,Time});
  }
  Contacts.Sort([](const FContact& A,const FContact& B){return A.Time<B.Time;});
  for(const FContact& Contact:Contacts)
  {
   if(!CanContinue())break;
   auto* Hit=Contact.Unit.Get();if(!Source->GetCombat()->IsValidEnemy(Hit))continue;
   HitUnits.Add(Hit);const FVector Point=FMath::Lerp(From,To,Contact.Time);
   SetActorLocation(Point);
   const bool bApplied=Battle->Damage(Source.Get(),Hit,RawDamage,AttackId,true);
   if(!CanContinue())break;
   if(bApplied)
   {
    if(auto* Audio=Source->FindComponentByClass<UEGCombatAudioComponent>())
     Audio->CommittedImpact(TEXT("HumanHit"),Round,AttackId,false,SourceAttackIndex,0,Point);
    if(!CanContinue())break;
    Battle->BasicHit(Source.Get(),Hit,AttackId);
    if(!CanContinue())break;
    OnArrowImpact(Hit,Point);
    if(!IsValid(this))return;
   }
   if(!bPiercing){Destroy();return;}
  }
  if(WallTarget&&CanContinue()&&Source->GetCombat()->IsValidEnemy(WallTarget))
  {
   SetActorLocation(To);
   if(Battle->Damage(Source.Get(),WallTarget,RawDamage,AttackId,true))Battle->BasicHit(Source.Get(),WallTarget,AttackId);
   Destroy();return;
  }
  if(!CanContinue()){Destroy();return;}
  SetActorLocation(To);Elapsed+=Step;Remaining-=Step;
  if(bPiercing&&DistanceUntilLevel>0.f)DistanceUntilLevel=FMath::Max(0.f,DistanceUntilLevel-Speed*Step);
 }
 if(!CanContinue()){Destroy();return;}
 if(Elapsed>=MaxLifetime){OnArrowExpired();if(IsValid(this))Destroy();}
}
