#include "Components/EGCombatComponent.h"
#include "EGGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Engine/OverlapResult.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatGeometry.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGCorruptionCombatComponent.h"

#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Data/EGHeroDefinition.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"

namespace
{
constexpr float ContactReachMargin=12.f;
// Placement, reservations, planning and movement all use the full physical
// radii. Zero extra clearance permits neighbouring one-cell bases to touch.
constexpr float PathClearance=0.f;
// Adjacent grid centres are exactly one diameter apart. Permit a millimetre
// of numerical tolerance consistently in placement, path checks and sweeps;
// genuine overlap and travelling through another body remain blocked.
constexpr float PathContactTolerance=.1f;

bool IsPilotOwnDevice(const AEGChessUnit* A,const AEGChessUnit* B)
{
 if(!IsValid(A)||!IsValid(B))return false;
 const EEGFaction Faction=A->GetFaction_Implementation();
 if(Faction==EEGFaction::Neutral||Faction!=B->GetFaction_Implementation())return false;
 const auto* ADevice=Cast<AEGVillVEmplacement>(A);
 const auto* BDevice=Cast<AEGVillVEmplacement>(B);
 return (ADevice&&ADevice->Pilot.Get()==B)||(BDevice&&BDevice->Pilot.Get()==A);
}

}

float UEGCombatComponent::BodyRadius(const AEGChessUnit* Other)
{
 if(!Other||!Other->GetDefinition())return 30.f;
 const auto* D=Other->GetDefinition();
 return D->MonsterAttack?D->MonsterAttack->BodyRadius:FMath::Clamp(D->GetPresentationPickRadius()*.9f,28.f,120.f);
}
float UEGCombatComponent::BlockingRadius(const AEGChessUnit* Other)
{
 const auto* Mode=Other&&Other->GetWorld()?Other->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(!Layout)return 80.f;
 const auto& Transform=Layout->GetActorTransform();
 const float Width=Transform.TransformVector(FVector(Layout->CellWidth,0,0)).Size2D();
 const float Depth=Transform.TransformVector(FVector(0,Layout->CellDepth,0)).Size2D();
 return FMath::Max(1.f,.5f*FMath::Min(Width,Depth));
}

bool UEGCombatComponent::IsSpaceOccupant(const AEGChessUnit* Other)
{
 if(!IsValid(Other)||!Other->GetDefinition()||!IEGCombatant::Execute_IsAlive(Other)||Other->GetHeroSkill()->bInFlight)return false;
 // The pilot can overlap her own seat and must never block a transfer. The
 // independent device actors retain their ordinary single-cell collision.
 if(!Other->IsAuxiliaryEmplacement()&&Other->GetDefinition()->HeroKind==EEGHeroKind::VillV)return false;
 if(Other->GetCombat()&&(Other->GetCombat()->bLandingReserved||Other->GetCombat()->bLandingPending))return false;
 return true;
}

FVector UEGCombatComponent::FindFreeBoardLocation(FVector Desired,bool* bOutFound) const
{
 if(bOutFound)*bOutFound=false;
 TRACE_CPUPROFILER_EVENT_SCOPE(EGCombat_FindFreeBoardLocation);
 const auto* U=Cast<AEGChessUnit>(GetOwner());if(!U||!U->GetDefinition())return Desired;
 const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
 const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 const FTransform Basis=Layout?Layout->GetActorTransform():FTransform::Identity;
 const float HalfX=Layout?Layout->Columns*Layout->CellWidth*.5f:0.f,HalfY=Layout?Layout->Rows*Layout->CellDepth*.5f:0.f;
 // Constrain real board placements. Off-board diagnostic actors retain their coordinates.
 const bool Bounded=Layout!=nullptr;
 auto ClampToBoard=[&](FVector Point)
 {
  if(!Bounded)return Point;
  FVector Local=Basis.InverseTransformPosition(Point);
  const float InsetX=BlockingRadius(U)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::X).Size());
  const float InsetY=BlockingRadius(U)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size());
  const float MaxX=FMath::Max(0.f,HalfX-InsetX),MaxY=FMath::Max(0.f,HalfY-InsetY);
  Local.X=FMath::Clamp(Local.X,-MaxX,MaxX);Local.Y=FMath::Clamp(Local.Y,-MaxY,MaxY);
  return Basis.TransformPosition(Local);
 };
 auto Free=[&](FVector Point)
 {
  return ClampToBoard(Point).Equals(Point,.01f)&&IsBoardLocationFree(Point);
 };
 if(Free(Desired)){if(bOutFound)*bOutFound=true;return Desired;}
 // First project out of overlapping footprints. Ring search handles dense groups
 // and exact coincidence without depending on the other unit being able to act.
 FVector Projected=Desired;
 for(int32 Pass=0;Pass<8;++Pass)
 {
  for(const auto& Entry:*CandidateSnapshot())
  {
   auto* Other=Entry.Get();if(Other==U||!IsSpaceOccupant(Other))continue;
   FVector Delta=Projected-Other->GetActorLocation();Delta.Z=0;const float Distance=Delta.Size();
   const float Minimum=BlockingRadius(U)+BlockingRadius(Other)+PathClearance;
   if(Distance>=Minimum)continue;
   const FVector Direction=Distance>SMALL_NUMBER?Delta/Distance:FVector(U->GetUniqueID()<Other->GetUniqueID()?1.f:-1.f,0,0);
   Projected+=Direction*(Minimum-Distance);
  }
  Projected=ClampToBoard(Projected);if(Free(Projected)){if(bOutFound)*bOutFound=true;return Projected;}
 }
 const float Step=FMath::Max(30.f,BlockingRadius(U)*.5f);
 for(int32 Ring=1;Ring<=32;++Ring)for(int32 Angle=0;Angle<32;++Angle)
 {
  const FVector Candidate=ClampToBoard(Desired+FVector::ForwardVector.RotateAngleAxis(Angle*11.25f,FVector::UpVector)*(Step*Ring));
  if(Free(Candidate)){if(bOutFound)*bOutFound=true;return Candidate;}
 }
 return Desired;
}
bool UEGCombatComponent::IsBoardLocationFree(FVector Point,const AEGChessUnit* Ignore,const AEGChessUnit* IgnoreB,bool bRequireBoardBounds,const TArray<AEGChessUnit*>* IgnoreGroup) const
{
 const auto* U=Cast<AEGChessUnit>(GetOwner());if(!U||!GetWorld()||Point.ContainsNaN())return false;
 if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)
  for(const auto& Entry:*CandidateSnapshot())if(const auto* Candidate=Entry.Get())
   if(const auto* Boss=Candidate->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&!Boss->AllowsPlayerLocation(Point,BlockingRadius(U)))return false;
 const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();
 const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(Layout&&bRequireBoardBounds)
 {
  const FTransform Basis=Layout->GetActorTransform();const FVector Local=Basis.InverseTransformPosition(Point);
  const float X=BlockingRadius(U)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::X).Size());
  const float Y=BlockingRadius(U)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size());
  if(FMath::Abs(Local.X)>Layout->Columns*Layout->CellWidth*.5f-X+.1f||
     FMath::Abs(Local.Y)>Layout->Rows*Layout->CellDepth*.5f-Y+.1f)return false;
 }
 FCollisionQueryParams Params(SCENE_QUERY_STAT(BoardUnitPlacement),false,U);
 if(Ignore)Params.AddIgnoredActor(Ignore);
 if(IgnoreB)Params.AddIgnoredActor(IgnoreB);
 if(IgnoreGroup)for(const auto* Ignored:*IgnoreGroup)if(IsValid(Ignored))Params.AddIgnoredActor(Ignored);
 const auto* Capsule=U->GetCapsuleComponent();const float Radius=BlockingRadius(U);
 TArray<FOverlapResult> Overlaps;
 GetWorld()->OverlapMultiByObjectType(Overlaps,Point,FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),
    FCollisionShape::MakeCapsule(FMath::Max(1.f,Capsule->GetScaledCapsuleRadius()-PathContactTolerance),Capsule->GetScaledCapsuleHalfHeight()),Params);
 if(!Overlaps.IsEmpty())return false;
 for(const auto& Entry:*CandidateSnapshot())
 {
  auto* Other=Entry.Get();if(!IsValid(Other)||Other==U||Other==Ignore||Other==IgnoreB||(IgnoreGroup&&IgnoreGroup->Contains(Other)))continue;
  const auto* OtherCombat=Other->GetCombat();
  if(OtherCombat&&OtherCombat->bLandingReserved&&IEGCombatant::Execute_IsAlive(Other)&&
     FVector::DistSquared2D(Point,OtherCombat->LandingReservation)<FMath::Square(FMath::Max(0.f,Radius+BlockingRadius(Other)+PathClearance-PathContactTolerance)))return false;
  if(!IsSpaceOccupant(Other))continue;
  if(FVector::DistSquared2D(Point,Other->GetActorLocation())<FMath::Square(FMath::Max(0.f,Radius+BlockingRadius(Other)+PathClearance-PathContactTolerance)))return false;
 }
 return true;
}
bool UEGCombatComponent::ReserveBoardLanding(FVector Desired,FVector& OutLanding)
{
 const auto* U=Cast<AEGChessUnit>(GetOwner());const auto* Battle=U?U->GetHeroSkill()->Battle.Get():nullptr;
 if(!U||!U->HasAuthority()||U->IsDeathPending()||!Battle||!Battle->IsBattleActive()||!Battle->GetRoundId().IsValid())return false;
 bool Found=false;const FVector Free=FindFreeBoardLocation(Desired,&Found);if(!Found)return false;
 LandingReservation=Free;LandingReservationRound=Battle->GetRoundId();bLandingReserved=true;OutLanding=Free;return true;
}
void UEGCombatComponent::ReleaseBoardLanding(FGuid Round)
{if(Round.IsValid()&&LandingReservationRound==Round){bLandingReserved=false;LandingReservationRound.Invalidate();}}
void UEGCombatComponent::ClearBoardLanding()
{
 bLandingReserved=false;LandingReservationRound.Invalidate();bLandingPending=false;PendingLandingRound.Invalidate();
 PendingLandingRetry=0.f;PendingLandingComplete=nullptr;
}
void UEGCombatComponent::CancelBoardLanding(){ClearBoardLanding();}
void UEGCombatComponent::RelocateBoardLanding(FVector NewLocation)
{
 if(NewLocation.ContainsNaN())return;
 if(bLandingReserved)LandingReservation=NewLocation;
 if(bLandingPending)PendingLandingDesired=NewLocation;
}
void UEGCombatComponent::RestoreBoardLandingCoordinates(FVector Reserved,FVector Pending)
{
 if(bLandingReserved&&!Reserved.ContainsNaN())LandingReservation=Reserved;
 if(bLandingPending&&!Pending.ContainsNaN())PendingLandingDesired=Pending;
}
bool UEGCombatComponent::CompleteOrDeferBoardLanding(FVector Desired,FGuid Round,TFunction<void()> OnComplete)
{
 auto* U=Cast<AEGChessUnit>(GetOwner());const auto* Battle=U?U->GetHeroSkill()->Battle.Get():nullptr;
 if(!U||!U->HasAuthority()||U->IsDeathPending()||!IEGCombatant::Execute_IsAlive(U)||Desired.ContainsNaN()||
    !Round.IsValid()||!Battle||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round)return false;
 if(bLandingPending&&PendingLandingRound!=Round)return false;
 bLandingPending=true;PendingLandingRound=Round;PendingLandingDesired=Desired;PendingLandingComplete=MoveTemp(OnComplete);PendingLandingRetry=.25f;
 U->RefreshBoardCollision();
 return TryCompleteBoardLanding();
}
bool UEGCombatComponent::TryCompleteBoardLanding()
{
 auto* U=Cast<AEGChessUnit>(GetOwner());const auto* Battle=U?U->GetHeroSkill()->Battle.Get():nullptr;
 if(!bLandingPending||!U||!U->HasAuthority()||U->IsDeathPending()||!IEGCombatant::Execute_IsAlive(U)||
    !Battle||!Battle->IsBattleActive()||Battle->GetRoundId()!=PendingLandingRound)return false;
 bool Found=false;const FVector Safe=FindFreeBoardLocation(PendingLandingDesired,&Found);if(!Found)return false;
 const FGuid CompletedRound=PendingLandingRound;U->SetActorLocation(Safe,false,nullptr,ETeleportType::TeleportPhysics);
 bLandingPending=false;PendingLandingRound.Invalidate();ReleaseBoardLanding(CompletedRound);
 auto Complete=MoveTemp(PendingLandingComplete);PendingLandingComplete=nullptr;PendingLandingRetry=0.f;
 if(Complete)Complete();
 if(IsValid(U)&&!U->IsActorBeingDestroyed())U->RefreshBoardCollision();
 return true;
}
bool UEGCombatComponent::IsOverlappingFriendlyEmplacement() const
{
 const auto* U=Cast<AEGChessUnit>(GetOwner());
 if(!IsValid(U)||U->IsAuxiliaryEmplacement()||U->GetFaction_Implementation()==EEGFaction::Neutral)return false;
 for(const auto& Entry:*CandidateSnapshot())
 {
  const auto* Device=Cast<AEGVillVEmplacement>(Entry.Get());
  if(!IsValid(Device)||!IsSpaceOccupant(Device)||Device->GetFaction_Implementation()!=U->GetFaction_Implementation()||IsPilotOwnDevice(U,Device))continue;
  const float Minimum=FMath::Max(0.f,BlockingRadius(U)+BlockingRadius(Device)-.1f);
  if(FVector::DistSquared2D(U->GetActorLocation(),Device->GetActorLocation())<FMath::Square(Minimum))return true;
 }
 return false;
}
void UEGCombatComponent::ClearDeviceEscape()
{DeviceEscapeRoute.Reset();DeviceEscapeRound.Invalidate();DeviceEscapeRetry=0.f;}

bool UEGCombatComponent::ResolveBoardOverlap()
{
 auto* U=Cast<AEGChessUnit>(GetOwner());
 if(!U||!U->HasAuthority()||U->IsAuxiliaryEmplacement()||!IsSpaceOccupant(U)||IEGCombatant::Execute_GetCombatState(U)!=EEGUnitState::Fighting)return false;
 // Existing friendly-device overlaps are resolved by ordinary movement once
 // combat starts, never by teleporting the other hero out of the seat.
 if(IsOverlappingFriendlyEmplacement())return false;
 bool Found=false;const FVector Before=U->GetActorLocation(),Free=FindFreeBoardLocation(Before,&Found);
 if(!Found)return false;
 if(Free.Equals(Before,.01f))return false;
 U->SetActorLocation(Free,false,nullptr,ETeleportType::TeleportPhysics);return true;
}
float UEGCombatComponent::GetEffectiveAttackRange(const AEGChessUnit* Other) const
{
 const auto* U=Cast<AEGChessUnit>(GetOwner());if(!U||!U->GetDefinition())return 0;
 const auto* D=U->GetDefinition();
 const float Range=U->GetRuntimeAttackRange();
 if(D->MonsterAttack&&D->MonsterAttack->AttackType!=EEGMonsterAttackType::Melee&&D->MonsterAttack->AttackRangeCells>0)
 {
  return Range;
 }
 return Other?FMath::Max(Range,BlockingRadius(U)+BlockingRadius(Other)+ContactReachMargin):Range;
}

UEGCombatComponent::UEGCombatComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Movement must advance on every frame; only combat decisions are throttled.
	PrimaryComponentTick.TickInterval = 0.f;
}

void UEGCombatComponent::BeginPlay()
{
	Super::BeginPlay();
	Unit = Cast<AEGChessUnit>(GetOwner());
}

bool UEGCombatComponent::CanAct() const
{
	const AEGChessUnit* OwnerUnit = Unit ? Unit.Get() : Cast<AEGChessUnit>(GetOwner());
	const auto* Battle=OwnerUnit&&OwnerUnit->GetHeroSkill()?OwnerUnit->GetHeroSkill()->Battle.Get():nullptr;
	return IsValid(OwnerUnit) && !OwnerUnit->IsAuxiliaryEmplacement() && OwnerUnit->HasAuthority() && IEGCombatant::Execute_IsAlive(OwnerUnit) &&
		   IEGCombatant::Execute_GetCombatState(OwnerUnit) == EEGUnitState::Fighting && (!Battle||!Battle->IsBattleActive()||Battle->IsCombatRunning()) && !HasExternalActionBlock() && !OwnerUnit->GetAbilitySystemComponent()->HasMatchingGameplayTag(EGTags::ControlStun) && !OwnerUnit->GetHeroSkill()->IsBusy();
}

bool UEGCombatComponent::IsValidEnemy(const AEGChessUnit* Other) const
{
	const AEGChessUnit* OwnerUnit = Cast<AEGChessUnit>(GetOwner());
	return IsValid(OwnerUnit) && IsValid(Other) && Other != OwnerUnit && Other->GetHeroSkill()->IsTargetable() && IEGCombatant::Execute_IsAlive(Other) &&
		   IEGCombatant::Execute_GetCombatState(Other) == EEGUnitState::Fighting &&
		   IEGCombatant::Execute_GetFaction(Other) != EEGFaction::Neutral &&
		   IEGCombatant::Execute_GetFaction(OwnerUnit) != EEGFaction::Neutral &&
		   IEGCombatant::Execute_GetFaction(Other) != IEGCombatant::Execute_GetFaction(OwnerUnit);
}

bool UEGCombatComponent::IsInAttackRange(const AEGChessUnit* Other) const
{
	return IsInAttackReach(Other);
}
bool UEGCombatComponent::IsInAttackReach(const AEGChessUnit* Other,float Extra) const
{
	const AEGChessUnit* OwnerUnit = Cast<AEGChessUnit>(GetOwner());
	if(!IsValidEnemy(Other)||!OwnerUnit->GetDefinition())return false;
	if(const auto* Boss=Other->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&Boss->IsWallActive())
	 return FVector::DistSquared2D(OwnerUnit->GetActorLocation(),Boss->WallContactPoint(OwnerUnit->GetActorLocation()))<=
	  FMath::Square(FMath::Max(0.f,GetEffectiveAttackRange(Other)+Extra)+.1f);
	return
		   EGCombatGeometry::DistanceSquared(OwnerUnit,Other) <=
			   FMath::Square(FMath::Max(0.f,GetEffectiveAttackRange(Other)+Extra)+.1f);
}

bool UEGCombatComponent::IsValidHealingTarget(const AEGChessUnit* Other) const
{
 const auto* OwnerUnit=Cast<AEGChessUnit>(GetOwner());
 return IsValid(OwnerUnit)&&IsValid(Other)&&Other!=OwnerUnit&&Other->GetDefinition()&&
  Other->GetHeroSkill()->IsTargetable()&&IEGCombatant::Execute_IsAlive(Other)&&
  IEGCombatant::Execute_GetCombatState(Other)==EEGUnitState::Fighting&&
  IEGCombatant::Execute_GetFaction(Other)==IEGCombatant::Execute_GetFaction(OwnerUnit)&&
  Other->GetAttributes()->GetHealth()<Other->GetAttributes()->GetMaxHealth()-KINDA_SMALL_NUMBER;
}
bool UEGCombatComponent::IsInHealingRange(const AEGChessUnit* Other) const
{return IsValidHealingTarget(Other)&&FVector::DistSquared2D(GetOwner()->GetActorLocation(),Other->GetActorLocation())<=FMath::Square(GetEffectiveAttackRange(Other)+.1f);}
AEGChessUnit* UEGCombatComponent::FindHealingTarget(bool bRequireRange) const
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EGCombat_FindHealingTarget);
 AEGChessUnit* Best=nullptr;float Ratio=2.f,Distance=MAX_flt;
 for(const auto& Entry:*CandidateSnapshot())if(auto* Other=Entry.Get();IsValidHealingTarget(Other))
 {
  const float D=FVector::DistSquared2D(GetOwner()->GetActorLocation(),Other->GetActorLocation());
  if(bRequireRange&&D>FMath::Square(GetEffectiveAttackRange(Other)+.1f))continue;
  const float R=Other->GetAttributes()->GetHealth()/FMath::Max(1.f,Other->GetAttributes()->GetMaxHealth());
  if((bRequireRange&&(R<Ratio-KINDA_SMALL_NUMBER||(FMath::IsNearlyEqual(R,Ratio)&&D<Distance)))||(!bRequireRange&&D<Distance))
  {Best=Other;Ratio=R;Distance=D;}
 }
 return Best;
}

AEGChessUnit* UEGCombatComponent::FindNearestEnemy() const
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EGCombat_FindNearestEnemy);
	AEGChessUnit* Best = nullptr;
	double BestDistance = TNumericLimits<double>::Max();
	const auto Snapshot=CandidateSnapshot();
	for (const auto& Entry:*Snapshot)
	{
		auto* Other=Entry.Get();
		if(Other==BlockedKnightTarget.Get()&&GetWorld()->GetTimeSeconds()<KnightRetryTime)continue;
		if (!IsValidEnemy(Other))
		{
			continue;
		}
		const double Distance = EGCombatGeometry::DistanceSquared(Cast<AEGChessUnit>(GetOwner()),Other);
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Best = Other;
		}
	}
	return Best;
}

bool UEGCombatComponent::IsBoardPathClear(FVector Start,FVector End,const AEGChessUnit* IgnoreA,const AEGChessUnit* IgnoreB,const AEGChessUnit** OutBlocker,const TCHAR** OutReason,const TArray<AEGChessUnit*>* IgnoreGroup,bool bIgnoreBarrier,bool bRequireBoardBounds) const
{
 if(OutBlocker)*OutBlocker=nullptr;if(OutReason)*OutReason=TEXT("Clear");
 const auto Reject=[&](const TCHAR* Reason,const AEGChessUnit* Blocker){if(OutBlocker)*OutBlocker=Blocker;if(OutReason)*OutReason=Reason;return false;};
 const auto* Moving=Cast<AEGChessUnit>(GetOwner());if(!Moving)return Reject(TEXT("NoOwner"),nullptr);
 if(!bIgnoreBarrier)for(const auto& Entry:*CandidateSnapshot())if(const auto* Candidate=Entry.Get())
  if(const auto* Boss=Candidate->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&Boss->BlocksMovement(Start,End,BlockingRadius(Moving)))
   return Reject(TEXT("CorruptionBarrier"),Candidate);
 const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(Layout&&bRequireBoardBounds)
 {
  const FTransform Basis=Layout->GetActorTransform();const FVector Local=Basis.InverseTransformPosition(End);
  const float X=BlockingRadius(Moving)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::X).Size());
  const float Y=BlockingRadius(Moving)/FMath::Max(.01f,Basis.GetScaledAxis(EAxis::Y).Size());
   if(FMath::Abs(Local.X)>Layout->Columns*Layout->CellWidth*.5f-X+.1f||FMath::Abs(Local.Y)>Layout->Rows*Layout->CellDepth*.5f-Y+.1f)return Reject(TEXT("BoardBounds"),nullptr);
 }
 for(const auto& Entry:*CandidateSnapshot())
 {
  auto* Blocker=Entry.Get();if(!IsValid(Blocker)||Blocker==Moving||Blocker==IgnoreA||Blocker==IgnoreB||(IgnoreGroup&&IgnoreGroup->Contains(Blocker)))continue;
  const auto* OtherCombat=Blocker->GetCombat();
  if(OtherCombat&&OtherCombat->bLandingReserved&&IEGCombatant::Execute_IsAlive(Blocker))
  {
   const FVector Center=OtherCombat->LandingReservation;const float Distance=FMath::Max(0.f,BlockingRadius(Moving)+BlockingRadius(Blocker)+PathClearance-PathContactTolerance);
   if(FVector::DistSquared2D(FMath::ClosestPointOnSegment(Center,Start,End),Center)<FMath::Square(Distance))return Reject(TEXT("LandingReservation"),Blocker);
  }
  if(!IsSpaceOccupant(Blocker))continue;
  const float Radius=FMath::Max(0.f,BlockingRadius(Moving)+BlockingRadius(Blocker)+PathClearance-PathContactTolerance);
  FVector Center=Blocker->GetActorLocation();Center.Z=Start.Z;
  const float Before=FVector::DistSquared2D(Start,Center);
  if(Before<FMath::Square(Radius)&&FVector::DistSquared2D(End,Center)>Before)
  {
   // An overlapping unit may escape outward, but cannot cross through the
   // other center and claim the opposite side as an escape.
   const FVector Away=Start-Center,Movement=End-Start;
   if(Before<=KINDA_SMALL_NUMBER||Away.X*Movement.X+Away.Y*Movement.Y>=0.f)
   {
    continue;
   }
  }
  const FVector Nearest=FMath::ClosestPointOnSegment(Center,Start,End);
  if(FVector::DistSquared2D(Nearest,Center)<FMath::Square(Radius))return Reject(TEXT("BodyGeometry"),Blocker);
 }
 return true;
}

void UEGCombatComponent::ClearChase()
{
 ReservedTarget.Reset();bHasContact=false;ChaseRoute.Reset();ReplanWait=0.f;
}

bool UEGCombatComponent::PlanChase(AEGChessUnit* Other)
{
 ClearChase();
 auto* U=Cast<AEGChessUnit>(GetOwner());
 const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(!U||!Other||!Layout)return false;
 const FVector Start=U->GetActorLocation();
 const auto Snapshot=CandidateSnapshot();
 struct FCellGoal {FVector Point;float EnemyDistance;float StartDistance;bool bInRange;};
 TArray<FCellGoal> Cells;
 const auto* Boss=Other->FindComponentByClass<UEGCorruptionCombatComponent>();
 const bool bHealer=U->GetDefinition()->MonsterAttack&&U->GetDefinition()->MonsterAttack->AttackType==EEGMonsterAttackType::Heal;
 for(int32 Y=0;Y<Layout->Rows;++Y)for(int32 X=0;X<Layout->Columns;++X)
 {
  const FIntPoint Cell(X,Y);FVector P=Layout->CellToWorld(Cell);
  P.Z=Start.Z;
  if(!IsBoardLocationFree(P))continue;
  bool Claimed=false;
  for(const auto& Entry:*Snapshot)
  {
   const auto* OtherUnit=Entry.Get();if(OtherUnit==U||!IsSpaceOccupant(OtherUnit))continue;
   FIntPoint Occupied;
   if(Layout->TryWorldToCell(OtherUnit->GetActorLocation(),Occupied)&&Occupied==Cell){Claimed=true;break;}
   const auto* OtherCombat=OtherUnit->GetCombat();
   FIntPoint ReservedCell;
   if(OtherCombat&&OtherCombat->bHasContact&&
      (OtherCombat->IsValidEnemy(OtherCombat->ReservedTarget.Get())||OtherCombat->IsValidHealingTarget(OtherCombat->ReservedTarget.Get()))&&
      Layout->TryWorldToCell(OtherCombat->ReservedContact,ReservedCell)&&ReservedCell==Cell)
   {Claimed=true;break;}
  }
  if(Claimed)continue;
  const FVector Contact=Boss&&Boss->IsWallActive()?Boss->WallContactPoint(P):
   (bHealer?Other->GetActorLocation():EGCombatGeometry::ClosestPoint(P,Other));
  const float Distance=FVector::DistSquared2D(P,Contact);
  Cells.Add({P,Distance,float(FVector::DistSquared2D(Start,P)),Distance<=FMath::Square(GetEffectiveAttackRange(Other)+.1f)});
 }
 // Prefer an attack/healing cell; if all are unreachable, choose the
 // reachable legal cell closest to the target, not merely the closest route.
 Cells.Sort([](const FCellGoal& A,const FCellGoal& B)
 {
  if(A.bInRange!=B.bInRange)return A.bInRange;
  if(A.bInRange)return A.StartDistance<B.StartDistance;
  if(!FMath::IsNearlyEqual(A.EnemyDistance,B.EnemyDistance))return A.EnemyDistance<B.EnemyDistance;
  return A.StartDistance<B.StartDistance;
 });
 TArray<FVector> Goals;Goals.Reserve(Cells.Num());for(const auto& Cell:Cells)Goals.Add(Cell.Point);
 if(Goals.IsEmpty())return false;
 FVector Goal;
 if(!PlanBoardRoute(Goals,ChaseRoute,Goal,nullptr,nullptr,true))return false;
 ReservedTarget=Other;ReservedContact=Goal;ReservedContactOffset=Goal-Other->GetActorLocation();bHasContact=true;ReplanWait=.5f+(U->GetUniqueID()%7)*.05f;
 return true;
}

bool UEGCombatComponent::PlanBoardRoute(const TArray<FVector>& InGoals,TArray<FVector>& OutRoute,FVector& OutGoal,const AEGChessUnit* IgnoreA,const AEGChessUnit* IgnoreB,bool bPreferGoalOrder) const
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EGCombat_PlanBoardRoute);
 const double SearchStarted=FPlatformTime::Seconds();
 ON_SCOPE_EXIT
 {
  const double Now=FPlatformTime::Seconds();
  ++MovementSearchCount;MovementSearchMilliseconds+=(Now-SearchStarted)*1000.;
  static double LastReport=0.;
  if(Now-SearchStarted>.004&&Now-LastReport>5.)
  {LastReport=Now;UE_LOG(LogTemp,Verbose,TEXT("Board route search %.2f ms, unit %s, goals %d, waypoints %d"),
    (Now-SearchStarted)*1000.,*GetNameSafe(GetOwner()),InGoals.Num(),OutRoute.Num());}
 };
 OutRoute.Reset();
 const auto* U=Cast<AEGChessUnit>(GetOwner());
 const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const auto* Layout=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(!U||!Layout||InGoals.IsEmpty())return false;
 const FVector Start=U->GetActorLocation();TArray<FVector> Goals=InGoals;
 if(!bPreferGoalOrder)Goals.Sort([Start](const FVector& A,const FVector& B){return FVector::DistSquared2D(Start,A)<FVector::DistSquared2D(Start,B);});
 for(int32 GoalIndex=0;GoalIndex<Goals.Num();++GoalIndex)if((!bPreferGoalOrder||GoalIndex==0)&&IsBoardPathClear(Start,Goals[GoalIndex],IgnoreA,IgnoreB))
 {OutRoute.Add(Goals[GoalIndex]);OutGoal=Goals[GoalIndex];return true;}
 // Subdivide the board for planning only. The unit moves continuously between
 // waypoints and never snaps to these temporary search samples.
 constexpr int32 Subdivision=4;
 const int32 NX=Layout->Columns*Subdivision+1,NY=Layout->Rows*Subdivision+1,Count=NX*NY;
 if(NX<2||NY<2||Count>10000)return false;
 const FTransform Basis=Layout->GetActorTransform();
 auto Point=[&](int32 I)
 {
  const int32 X=I%NX,Y=I/NX;
  FVector P=Basis.TransformPosition(FVector((float(X)/Subdivision-Layout->Columns*.5f)*Layout->CellWidth,
   (float(Y)/Subdivision-Layout->Rows*.5f)*Layout->CellDepth,0));P.Z=Start.Z;return P;
 };
 const FVector Local=Basis.InverseTransformPosition(Start);
 const int32 SX=FMath::Clamp(FMath::RoundToInt((Local.X/Layout->CellWidth+Layout->Columns*.5f)*Subdivision),0,NX-1);
 const int32 SY=FMath::Clamp(FMath::RoundToInt((Local.Y/Layout->CellDepth+Layout->Rows*.5f)*Subdivision),0,NY-1);
 int32 First=INDEX_NONE;double FirstDistance=DBL_MAX;
 for(int32 Y=FMath::Max(0,SY-2);Y<=FMath::Min(NY-1,SY+2);++Y)for(int32 X=FMath::Max(0,SX-2);X<=FMath::Min(NX-1,SX+2);++X)
 {
  const int32 I=Y*NX+X;const FVector P=Point(I);const double Distance=FVector::DistSquared2D(Start,P);
  if(Distance<FirstDistance&&IsBoardPathClear(Start,P,IgnoreA,IgnoreB)){First=I;FirstDistance=Distance;}
 }
 if(First==INDEX_NONE)return false;
 TArray<float> Cost;Cost.Init(TNumericLimits<float>::Max(),Count);
 TArray<int32> Parent;Parent.Init(INDEX_NONE,Count);
 TArray<bool> Closed;Closed.Init(false,Count);
 struct FOpenEntry {int32 Node;float Score;float G;};
 TArray<FOpenEntry> Open;Cost[First]=FMath::Sqrt(FirstDistance);
 const float Step=FMath::Max(Layout->CellWidth*Basis.GetScaledAxis(EAxis::X).Size(),Layout->CellDepth*Basis.GetScaledAxis(EAxis::Y).Size())/Subdivision;
 auto Heuristic=[&](FVector P){float Best=MAX_flt;for(const auto& G:Goals)Best=FMath::Min(Best,float(FVector::Dist2D(P,G)));return Best;};
 // Binary min-heap keeps a crowded search bounded without scanning the full
 // open set and every candidate goal again for every expanded node.
 auto Push=[&](FOpenEntry Entry)
 {
  int32 I=Open.Add(Entry);
  while(I>0){const int32 ParentIndex=(I-1)/2;if(Open[ParentIndex].Score<=Open[I].Score)break;Open.Swap(ParentIndex,I);I=ParentIndex;}
 };
 auto Pop=[&]()
 {
  const FOpenEntry Result=Open[0];const FOpenEntry Tail=Open.Pop(EAllowShrinking::No);
  if(!Open.IsEmpty())
  {
   Open[0]=Tail;int32 I=0;
   for(;;){const int32 L=I*2+1,R=L+1;if(L>=Open.Num())break;const int32 Child=R<Open.Num()&&Open[R].Score<Open[L].Score?R:L;
    if(Open[I].Score<=Open[Child].Score)break;Open.Swap(I,Child);I=Child;}
  }
  return Result;
 };
 Push({First,Cost[First]+Heuristic(Point(First)),Cost[First]});
 int32 Last=INDEX_NONE,BestGoalIndex=Goals.Num();FVector Goal=FVector::ZeroVector;
 while(!Open.IsEmpty())
 {
  const FOpenEntry Entry=Pop();const int32 Current=Entry.Node;
  if(Closed[Current]||Entry.G>Cost[Current])continue;Closed[Current]=true;
  const FVector Here=Point(Current);
  for(int32 GoalIndex=0;GoalIndex<BestGoalIndex;++GoalIndex)
  {
   const FVector Candidate=Goals[GoalIndex];
   if(FVector::DistSquared2D(Here,Candidate)<=FMath::Square(Step*1.5f)&&IsBoardPathClear(Here,Candidate,IgnoreA,IgnoreB))
   {Last=Current;Goal=Candidate;BestGoalIndex=GoalIndex;break;}
  }
  if(Last!=INDEX_NONE&&(!bPreferGoalOrder||BestGoalIndex==0))break;
  const int32 X=Current%NX,Y=Current/NX;
  for(int32 DY=-1;DY<=1;++DY)for(int32 DX=-1;DX<=1;++DX)
  {
   if(!DX&&!DY)continue;const int32 XX=X+DX,YY=Y+DY;if(XX<0||XX>=NX||YY<0||YY>=NY)continue;
   const int32 Next=YY*NX+XX;if(Closed[Next])continue;const FVector There=Point(Next);
   if(!IsBoardPathClear(Here,There,IgnoreA,IgnoreB))continue;
   const float NewCost=Cost[Current]+FVector::Dist2D(Here,There);
   if(NewCost<Cost[Next]){Cost[Next]=NewCost;Parent[Next]=Current;Push({Next,NewCost+Heuristic(There),NewCost});}
  }
 }
 if(Last==INDEX_NONE)return false;
 TArray<FVector> Reverse;for(int32 I=Last;I!=INDEX_NONE;I=Parent[I])Reverse.Add(Point(I));
 TArray<FVector> Raw;for(int32 I=Reverse.Num()-1;I>=0;--I)Raw.Add(Reverse[I]);Raw.Add(Goal);
 // Skip unnecessary grid samples while retaining the same collision test.
 FVector From=Start;int32 I=0;
 while(I<Raw.Num())
 {
  int32 Furthest=I;
  for(int32 J=Raw.Num()-1;J>I;--J)if(IsBoardPathClear(From,Raw[J],IgnoreA,IgnoreB)){Furthest=J;break;}
  OutRoute.Add(Raw[Furthest]);From=Raw[Furthest];I=Furthest+1;
 }
 OutGoal=Goal;return true;
}

bool UEGCombatComponent::AdvanceBoardRoute(TArray<FVector>& Route,float Travel,const AEGChessUnit* IgnoreA,const AEGChessUnit* IgnoreB,float* OutRemainingTravel,bool bRequireBoardBounds)
{
 ON_SCOPE_EXIT{if(OutRemainingTravel)*OutRemainingTravel=Travel>.01f?Travel:0.f;};
 auto* U=Cast<AEGChessUnit>(GetOwner());if(!U||!U->HasAuthority())return false;
 Travel=FMath::Max(0.f,Travel);
 while(Travel>0.f&&!Route.IsEmpty())
 {
  // Reaching a waypoint can leave only floating-point residue in this frame's
  // budget. It is not a blocked movement and must not discard the route.
  if(Travel<=.01f){Travel=0.f;break;}
  const FVector Here=U->GetActorLocation(),Next=Route[0];const float Distance=FVector::Dist2D(Here,Next);
  if(Distance<.5f){Route.RemoveAt(0);continue;}
  const FVector End=Here+(Next-Here).GetSafeNormal2D()*FMath::Min(Travel,Distance);
  const auto ReportBlock=[&](const TCHAR* Reason,const AActor* Blocker,const FHitResult* Hit)
  {
   LastMovementBlockReason=Reason;
   LastMovementBlocker=IsValid(Blocker)?Blocker->GetFName():NAME_None;
   if(!UE_LOG_ACTIVE(LogTemp,Verbose))return;
   const double Now=FPlatformTime::Seconds();if(Now-LastMovementBlockReport<5.)return;LastMovementBlockReport=Now;
   const auto* Capsule=U->GetCapsuleComponent();
   UE_LOG(LogTemp,Verbose,TEXT("Board movement blocked unit=%s reason=%s blocker=%s penetration=%d start=%s end=%s logicalRadius=%.2f capsuleRadius=%.2f capsuleHalfHeight=%.2f blockerLocation=%s"),
    *GetNameSafe(U),Reason,*GetNameSafe(Blocker),Hit&&Hit->bStartPenetrating?1:0,*Here.ToCompactString(),*End.ToCompactString(),BlockingRadius(U),
    Capsule->GetScaledCapsuleRadius(),Capsule->GetScaledCapsuleHalfHeight(),Blocker?*Blocker->GetActorLocation().ToCompactString():TEXT("none"));
  };
  const AEGChessUnit* GeometricBlocker=nullptr;const TCHAR* GeometricReason=nullptr;
  if(!IsBoardPathClear(Here,End,IgnoreA,IgnoreB,&GeometricBlocker,&GeometricReason,nullptr,false,bRequireBoardBounds))
  {ReportBlock(GeometricReason,GeometricBlocker,nullptr);return false;}
  FHitResult Hit;
  // Use the same full capsule for both ordinary movement and pilot docking.
  // An initially overlapping pawn may be ignored only for an outward escape;
  // the geometry test above still prevents crossing through its centre.
  {
   FCollisionQueryParams Params(SCENE_QUERY_STAT(BoardRouteSweep),false,U);
   if(IgnoreA)Params.AddIgnoredActor(IgnoreA);if(IgnoreB)Params.AddIgnoredActor(IgnoreB);
   const auto* Capsule=U->GetCapsuleComponent();const float Radius=FMath::Max(1.f,Capsule->GetScaledCapsuleRadius()-PathContactTolerance);
   const auto Shape=FCollisionShape::MakeCapsule(Radius,Capsule->GetScaledCapsuleHalfHeight());
   const int32 MaxAttempts=CandidateSnapshot()->Num()+1;
   for(int32 Attempt=0;Attempt<MaxAttempts;++Attempt)
   {
    if(!GetWorld()->SweepSingleByObjectType(Hit,Here,End,FQuat::Identity,FCollisionObjectQueryParams(ECC_Pawn),Shape,Params))break;
    const auto* Existing=Cast<AEGChessUnit>(Hit.GetActor());
    if(!Hit.bStartPenetrating||!Existing||FVector::DistSquared2D(End,Existing->GetActorLocation())<=FVector::DistSquared2D(Here,Existing->GetActorLocation()))
    {ReportBlock(TEXT("PhysicalSweep"),Hit.GetActor(),&Hit);return false;}
    if(Attempt==MaxAttempts-1){ReportBlock(TEXT("PhysicalOverlapLimit"),Existing,&Hit);return false;}
    Params.AddIgnoredActor(Existing);Hit=FHitResult();
   }
  }
  U->SetActorLocation(End,false,&Hit);
  Travel-=FVector::Dist2D(Here,U->GetActorLocation());
  if(Hit.bBlockingHit||U->GetActorLocation().Equals(Here,.001f))
  {ReportBlock(TEXT("NoDisplacement"),Hit.GetActor(),&Hit);return false;}
  if(!Route.IsEmpty()&&FVector::DistSquared2D(U->GetActorLocation(),Route[0])<.25f)Route.RemoveAt(0);
 }
 return true;
}
void UEGCombatComponent::ResetBoardPursuitStall()
{bHasStallGoal=false;KnightStallSeconds=0.f;KnightBestDistance=MAX_flt;}
bool UEGCombatComponent::IsBoardPursuitStalled(FVector Goal,float Delta)
{
 const auto* U=Cast<AEGChessUnit>(GetOwner());if(!U||!FMath::IsFinite(Delta))return false;
 const float Remaining=FVector::Dist2D(U->GetActorLocation(),Goal);
 if(!bHasStallGoal||FVector::DistSquared2D(StallGoal,Goal)>FMath::Square(BlockingRadius(U)*.5f))
 {StallGoal=Goal;StallAnchor=U->GetActorLocation();bHasStallGoal=true;KnightBestDistance=Remaining;KnightStallSeconds=0.f;return false;}
 // Moving around an obstacle may temporarily increase straight-line distance.
 // Do not abandon a valid route just because that detour takes over two seconds.
 if(Remaining<KnightBestDistance-10.f||FVector::DistSquared2D(StallAnchor,U->GetActorLocation())>FMath::Square(FMath::Max(30.f,BlockingRadius(U)*.35f)))
 {StallAnchor=U->GetActorLocation();KnightBestDistance=Remaining;KnightStallSeconds=0.f;return false;}
 KnightStallSeconds+=FMath::Max(0.f,Delta);
 if(KnightStallSeconds<2.f)return false;
 KnightStallSeconds=0.f;KnightBestDistance=Remaining;return true;
}

void UEGCombatComponent::RequestChase_Implementation(AEGChessUnit* Other)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EGCombat_RequestChase);
 if(auto* U=Cast<AEGChessUnit>(GetOwner());U&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::VillV)return;
	if (!CanAct() || (!IsValidEnemy(Other)&&!IsValidHealingTarget(Other)))
	{
		return;
	}
	const auto* GameMode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
	if (Unit->GetDefinition()->GetBoardMovement()||(GameMode&&GameMode->GetBoard()&&GameMode->GetBoard()->LayoutSource.Get()))
    {
        const float Delta=DecisionDeltaSeconds>0.f?DecisionDeltaSeconds:GetWorld()->GetDeltaSeconds();
        ReplanWait-=Delta;
        // Reconsider a waiting position periodically, without repeatedly
        // replacing a valid route merely because it is a fallback cell.
        bool NeedsPlan=ReservedTarget.Get()!=Other;
        if(!NeedsPlan&&ReplanWait<=0.f)
        {
         // Retain a valid destination while the target moves; replacing it on
         // a timer makes actors oscillate between equivalent contact slots.
         bool GoalInReach=false;
         if(bHasContact)
         {
          FVector ContactTarget=IsValidHealingTarget(Other)?Other->GetActorLocation():EGCombatGeometry::ClosestPoint(ReservedContact,Other);
          if(const auto* Boss=Other->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&Boss->IsWallActive())ContactTarget=Boss->WallContactPoint(ReservedContact);
          GoalInReach=FVector::DistSquared2D(ReservedContact,ContactTarget)<=FMath::Square(GetEffectiveAttackRange(Other));
         }
         NeedsPlan=!bHasContact||(!GoalInReach&&ChaseRoute.IsEmpty())||!IsBoardLocationFree(ReservedContact);
         if(!NeedsPlan)ReplanWait=.5f+(Unit->GetUniqueID()%7)*.05f;
        }
        if(NeedsPlan)
        {
         PlanChase(Other);
         if(!bHasContact){ReservedTarget=Other;ReplanWait=.5f+(Unit->GetUniqueID()%7)*.05f;LastPursuitReason=TEXT("NoReachableContact");}
        }
        else if(!bHasContact)LastPursuitReason=TEXT("WaitingRetry");
        const FVector MovementStart=Unit->GetActorLocation();
        const bool bHadRoute=!ChaseRoute.IsEmpty();
        float RemainingTravel=0.f;
        bool Advanced=AdvanceBoardRoute(ChaseRoute,Unit->GetAttributes()->GetMoveSpeed()*Delta,nullptr,nullptr,&RemainingTravel);
        const bool StillOutOfReach=IsValidEnemy(Other)?!IsInAttackReach(Other):!IsInHealingRange(Other);
        if(Advanced&&bHasContact&&bHadRoute&&ChaseRoute.IsEmpty()&&RemainingTravel>0.f&&StillOutOfReach)
        {
         // The target may have moved while we were approaching its old cell.
         // Replan once and use this frame's remaining movement budget. A unit
         // already waiting at its fallback has no route and is retried only
         // on the regular timer, so it does not search every frame.
         PlanChase(Other);
         if(bHasContact)Advanced=AdvanceBoardRoute(ChaseRoute,RemainingTravel);
         else {ReservedTarget=Other;ReplanWait=.5f+(Unit->GetUniqueID()%7)*.05f;LastPursuitReason=TEXT("NoReachableContact");}
        }
        if(!Advanced)
        {ClearChase();ReservedTarget=Other;ReplanWait=.25f+(Unit->GetUniqueID()%7)*.05f;LastPursuitReason=TEXT("Blocked");}
        else if(FVector::DistSquared2D(MovementStart,Unit->GetActorLocation())>KINDA_SMALL_NUMBER)LastPursuitReason=TEXT("Moving");
        if(UE_LOG_ACTIVE(LogTemp,Verbose))
        {
         MovementDiagnosticTime+=Delta;MovementDiagnosticDistance+=FVector::Dist2D(MovementStart,Unit->GetActorLocation());
         if(MovementDiagnosticTime>=5.f)
         {
          UE_LOG(LogTemp,Verbose,TEXT("Board pursuit %s configured %.1f cm/s, actual %.1f cm/s over %.1fs, route points %d, searches %llu total %.2f ms, state %s"),
           *GetNameSafe(Unit.Get()),Unit->GetAttributes()->GetMoveSpeed(),MovementDiagnosticDistance/MovementDiagnosticTime,MovementDiagnosticTime,ChaseRoute.Num(),
           static_cast<unsigned long long>(MovementSearchCount),MovementSearchMilliseconds,LastPursuitReason);
          MovementDiagnosticTime=MovementDiagnosticDistance=0.f;
         }
        }
        if(KnightPursuitTarget.Get()!=Other){KnightPursuitTarget=Other;ResetBoardPursuitStall();}
        const FVector ProgressGoal=bHasContact?ReservedContact:Other->GetActorLocation();
        if(!ChaseRoute.IsEmpty()&&IsBoardPursuitStalled(ProgressGoal,Delta))
        {BlockedKnightTarget=Other;KnightRetryTime=GetWorld()->GetTimeSeconds()+3.f;Target.Reset();ClearChase();ResetBoardPursuitStall();}
        return;
    }
    if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
    {
        const UPathFollowingComponent* Path = AI->GetPathFollowingComponent();
		if (Path && Path->GetMoveGoal() == Other && AI->GetMoveStatus() == EPathFollowingStatus::Moving)
		{
			return;
		}
		// A NavMesh is required. TODO: expose path failure / unreachable-target policy.
		AI->MoveToActor(Other, FMath::Max(1.f, Unit->GetRuntimeAttackRange() * 0.8f), false);
	}
}

void UEGCombatComponent::ResetCombat()
{
	Target.Reset();
 ClearChase();
 ClearDeviceEscape();
 ClearBoardLanding();
 MovementDiagnosticTime=MovementDiagnosticDistance=0.f;
 MovementSearchCount=0;MovementSearchMilliseconds=0.;LastPursuitReason=TEXT("WaitingRetry");
 LastMovementBlockReport=0.;LastMovementBlockReason=TEXT("None");LastMovementBlocker=NAME_None;
 BlockedKnightTarget.Reset();KnightPursuitTarget.Reset();ResetBoardPursuitStall();KnightRetryTime=0;
	AttackWait = UltimateRetryWait = DecisionWait = 0.f;
	if (const AEGChessUnit* OwnerUnit = Cast<AEGChessUnit>(GetOwner()))
	{
		if (AAIController* AI = Cast<AAIController>(OwnerUnit->GetController()))
		{
			AI->StopMovement();
		}
	}
}

void UEGCombatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
 if(auto* U=Cast<AEGChessUnit>(GetOwner());U&&U->IsAuxiliaryEmplacement())return;
 if(bLandingPending)
 {
  auto* U=Cast<AEGChessUnit>(GetOwner());const auto* Battle=U?U->GetHeroSkill()->Battle.Get():nullptr;
  if(!U||U->IsDeathPending()||!IEGCombatant::Execute_IsAlive(U)||!Battle||!Battle->IsBattleActive()||Battle->GetRoundId()!=PendingLandingRound)
  {
   const FGuid OldRound=PendingLandingRound;bLandingPending=false;PendingLandingRound.Invalidate();PendingLandingComplete=nullptr;
   ReleaseBoardLanding(OldRound);
  }
  else
  {
   PendingLandingRetry-=FMath::Max(0.f,DeltaTime);
   if(PendingLandingRetry<=0.f){PendingLandingRetry=.25f;TryCompleteBoardLanding();}
   if(bLandingPending)return;
  }
 }
 if(auto* OwnerUnit=Cast<AEGChessUnit>(GetOwner());OwnerUnit&&OwnerUnit->HasAuthority())
 {OwnerUnit->RefreshBoardCollision();}
	AttackWait = FMath::Max(0.f, AttackWait - DeltaTime);
	UltimateRetryWait = FMath::Max(0.f, UltimateRetryWait - DeltaTime);
	DecisionWait = FMath::Max(0.f, DecisionWait - DeltaTime);
	if (!CanAct()){ClearChase();return;}
 const auto* ActiveBattle=Unit&&Unit->GetHeroSkill()?Unit->GetHeroSkill()->Battle.Get():nullptr;
 if(ActiveBattle&&ActiveBattle->IsCombatRunning()&&IsOverlappingFriendlyEmplacement())
 {
  const FGuid Round=ActiveBattle->GetRoundId();
  if(DeviceEscapeRound!=Round){ClearDeviceEscape();DeviceEscapeRound=Round;ClearChase();}
  DeviceEscapeRetry=FMath::Max(0.f,DeviceEscapeRetry-FMath::Max(0.f,DeltaTime));
  if(DeviceEscapeRoute.IsEmpty()&&DeviceEscapeRetry<=0.f)
  {
   bool Found=false;const FVector Free=FindFreeBoardLocation(Unit->GetActorLocation(),&Found);FVector Goal;
   if(Found)PlanBoardRoute({Free},DeviceEscapeRoute,Goal);
   DeviceEscapeRetry=.5f+(Unit->GetUniqueID()%7)*.05f;
  }
  const float Speed=Unit->GetAttributes()->GetMoveSpeed();
  if(!DeviceEscapeRoute.IsEmpty()&&FMath::IsFinite(Speed)&&Speed>0.f&&
     !AdvanceBoardRoute(DeviceEscapeRoute,Speed*FMath::Max(0.f,DeltaTime)))
  {DeviceEscapeRoute.Reset();DeviceEscapeRetry=.25f+(Unit->GetUniqueID()%7)*.05f;}
  return;
 }
 if(DeviceEscapeRound.IsValid())ClearDeviceEscape();
	TGuardValue<float> DeltaGuard(DecisionDeltaSeconds,DeltaTime);
 // Refresh pursuit before movement AND ultimate decisions. ActionTarget is a
 // separate snapshot owned by HeroSkill; CanAct prevents retargeting a swing.
 const auto* D=Unit->GetDefinition();
 const auto* Skill=Unit->GetHeroSkill();
 const bool bHealer=D->MonsterAttack&&D->MonsterAttack->AttackType==EEGMonsterAttackType::Heal;
 if(bHealer){Target=FindHealingTarget();if(!Target.IsValid())Target=FindHealingTarget(false);}
 else if(Skill->IsTauntActive()&&IsValidEnemy(Skill->ForcedTarget.Get()))
  Target=Skill->ForcedTarget;
  else if(!IsValidEnemy(Target.Get()))
  Target=FindNearestEnemy();
 // A pursuit claims a whole cell. Finish moving to its centre before
 // attacking, even if the body crosses the range threshold on the way.
 if(bHasContact&&ReservedTarget==Target&&!ChaseRoute.IsEmpty())
 {RequestChase(Target.Get());return;}
 // Authored cycles chain at the montage endpoint without a 10 Hz pause.
 const bool bDecide=D->MonsterAttack||D->HeroKind==EEGHeroKind::Pardofelis||D->HeroKind==EEGHeroKind::VillV||DecisionWait<=KINDA_SMALL_NUMBER;
	if(!bDecide)
	{
		if(IsValidEnemy(Target.Get())&&!IsInAttackRange(Target.Get()))RequestChase(Target.Get());
		return;
	}
	DecisionWait=.1f;
	const UEGAttributeSet* Stats = Unit->GetAttributes();
	// Targetless self/support/summon skills may still cast when no enemy remains.
	if (Unit->GetDefinition()->bCardSkillsEnabled && Stats->GetMaxMana() > 0.f && Stats->GetMana() >= Stats->GetMaxMana() && UltimateRetryWait <= 0.f)
	{
		UltimateRetryWait = 0.5f;
		Unit->RequestUltimate();
		if (!CanAct())
		{
			return;
		}
	}
	if (!Target.IsValid())
	{
  ClearChase();
  if(bHealer)if(auto* AI=Cast<AAIController>(Unit->GetController()))AI->StopMovement();
		return;
	}
	if (!(bHealer?IsInHealingRange(Target.Get()):IsInAttackRange(Target.Get())))
	{
		RequestChase(Target.Get());
		return;
	}
	if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
	{
		AI->StopMovement();
	}
 LastPursuitReason=TEXT("AtContact");ClearChase();
	if (AttackWait <= KINDA_SMALL_NUMBER && Stats->GetAttackSpeed() > 0.f && Unit->RequestBasicAttack())
	{
        // These heroes' attack speed scales the authored montage cycle.
        // The montage owns recovery; do not add a second fixed cooldown.
		const auto Kind=Unit->GetDefinition()->HeroKind;
		AttackWait = Kind==EEGHeroKind::Pardofelis||Kind==EEGHeroKind::VillV?0.f:1.f/Stats->GetAttackSpeed();
	}
}

void UEGCombatComponent::SetCombatants(const TArray<TWeakObjectPtr<AEGChessUnit>>& Units)
{Combatants=MakeShared<FEGCombatantList>(Units);}
void UEGCombatComponent::SetCombatants(TSharedRef<const FEGCombatantList> Units){Combatants=Units;}
TSharedRef<const FEGCombatantList> UEGCombatComponent::CandidateSnapshot() const
{
 if(Combatants.IsValid())return Combatants.ToSharedRef();
 auto Result=MakeShared<FEGCombatantList>();for(TActorIterator<AEGChessUnit> It(GetWorld());It;++It)Result->Add(*It);return Result;
}
TArray<AEGChessUnit*> UEGCombatComponent::FindEnemiesInRange(FVector Origin,float Radius) const
{
 TArray<AEGChessUnit*> Result;TSet<AEGChessUnit*> Visited;
 const auto Snapshot=CandidateSnapshot();
 for(const auto& Entry:*Snapshot)if(auto* Other=Entry.Get();IsValidEnemy(Other)&&!Visited.Contains(Other)&&EGCombatGeometry::PointDistanceSquared(Origin,Other)<=FMath::Square(FMath::Max(0.f,Radius))){Visited.Add(Other);Result.Add(Other);}
 return Result;
}
