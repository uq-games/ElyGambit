#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Character/EGChessUnit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGSourceSummonDefinition.h"
#include "Presentation/EGSourceEffect.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
namespace
{
 FVector SampleSummonOffset(const UEGSourceSummonDefinition* Motion,float Alpha)
 {
  const float Progress=FMath::Pow(FMath::Clamp(Alpha,0.f,1.f),Motion->DescentTimeExponent);
  const float Frame=Progress*(Motion->NormalizedOffsets.Num()-1);
  const int32 Low=FMath::Clamp(FMath::FloorToInt(Frame),0,Motion->NormalizedOffsets.Num()-1);
  const int32 High=FMath::Min(Low+1,Motion->NormalizedOffsets.Num()-1);
  return FMath::Lerp(Motion->NormalizedOffsets[Low],Motion->NormalizedOffsets[High],Frame-Low);
 }
}
AEGSkillProjectile::AEGSkillProjectile()
{
 PrimaryActorTick.bCanEverTick=true;Visual=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));SetRootComponent(Visual);
 static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere"));Visual->SetStaticMesh(Sphere.Object);
 Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);Visual->SetRelativeScale3D(FVector(.09));SetActorEnableCollision(false);
}
FEGThrownSummonFlightState AEGSkillProjectile::CaptureThrownSummonFlight() const
{
 FEGThrownSummonFlightState State;
 State.Start=Start;State.Destination=Destination;State.Location=GetActorLocation();
 State.Elapsed=Elapsed;State.Duration=Duration;State.FlightBasis=FlightBasis;State.FlightHeight=FlightHeight;
 State.bRebased=bFlightRebased;State.RebasedCurveStart=RebasedCurveStart;
 State.bHadCargo=Cargo.IsValid();if(State.bHadCargo)State.CargoLocation=Cargo->GetActorLocation();
 State.bHadRing=DescentRing.IsValid();if(State.bHadRing)State.RingLocation=DescentRing->GetActorLocation();
 return State;
}
bool AEGSkillProjectile::RebaseThrownSummon(FVector NewCurrent)
{
 return RebaseThrownSummon(NewCurrent,FVector(NewCurrent.X,NewCurrent.Y,Destination.Z));
}
bool AEGSkillProjectile::RebaseThrownSummon(FVector NewCurrent,FVector NewLanding)
{
 if(Kind!=EEGProjectileKind::ThrownSummon||!Cargo.IsValid()||NewCurrent.ContainsNaN()||NewLanding.ContainsNaN()||
    !FMath::IsFinite(Elapsed)||!FMath::IsFinite(Duration)||Duration<=0.f||Destination.ContainsNaN())return false;
 const float CurrentAlpha=FMath::Clamp(Elapsed/Duration,0.f,1.f);
 const float CurveAlpha=bFlightRebased?FMath::Lerp(RebasedCurveStart,1.f,CurrentAlpha):CurrentAlpha;
 const float Remaining=FMath::Max(0.f,Duration-Elapsed);
 const bool bWasLanding=Elapsed>=Duration;
 Start=NewCurrent;Destination=NewLanding;
 Duration=FMath::Max(.001f,Remaining);Elapsed=bWasLanding?Duration:0.f;
 RebasedCurveStart=CurveAlpha;bFlightRebased=true;
 SetActorLocation(NewCurrent,false,nullptr,ETeleportType::TeleportPhysics);
 Cargo->SetActorLocation(NewCurrent,false,nullptr,ETeleportType::TeleportPhysics);
 if(DescentRing.IsValid())DescentRing->SetActorLocation(Destination-FVector(0,0,88));
 return true;
}
void AEGSkillProjectile::RestoreThrownSummonFlight(const FEGThrownSummonFlightState& State)
{
 if(Kind!=EEGProjectileKind::ThrownSummon)return;
 Start=State.Start;Destination=State.Destination;Elapsed=State.Elapsed;Duration=State.Duration;
 FlightBasis=State.FlightBasis;FlightHeight=State.FlightHeight;
 bFlightRebased=State.bRebased;RebasedCurveStart=State.RebasedCurveStart;
 SetActorLocation(State.Location,false,nullptr,ETeleportType::TeleportPhysics);
 if(State.bHadCargo&&Cargo.IsValid())Cargo->SetActorLocation(State.CargoLocation,false,nullptr,ETeleportType::TeleportPhysics);
 if(State.bHadRing&&DescentRing.IsValid())DescentRing->SetActorLocation(State.RingLocation);
}
void AEGSkillProjectile::Tick(float Delta)
{
 Super::Tick(Delta);
 if(!Battle.IsValid()||!Battle->IsBattleActive()||Battle->GetRoundId()!=Round){Destroy();return;}
 if(!Battle->IsCombatRunning())return;
 if(Kind==EEGProjectileKind::MonsterBasic)
 {
  auto* U=Source.Get();auto* T=Target.Get();
  if(!IsValid(U)||!U->HasAuthority()||!IEGCombatant::Execute_IsAlive(U)||!U->GetCombat()->IsValidEnemy(T)||Elapsed>4.f)
  {Destroy();return;}
  const auto* Profile=U->GetDefinition()->MonsterAttack.Get();
  if(!Profile){Destroy();return;}
  if(Elapsed==0.f)
  {
   Visual->SetWorldScale3D(FVector(Profile->ProjectileRadius/50.f*(bMonsterUltimate?Profile->EnhancedProjectileScale:1.f)));
   if(auto* Mat=(bMonsterUltimate?Profile->SkillMaterial:U->GetDefinition()->GetAttackMaterial()).LoadSynchronous())Visual->SetMaterial(0,Mat);
  }
  Elapsed+=Delta;
  // Aim at the visible torso rather than the capsule origin above a small monster.
  const float Height=T->GetDefinition()->GetPresentationHeight();
  Destination=T->GetActorLocation()+FVector(0,0,Height>0?Height*.5f-88.f:0.f);
  const FVector Next=FMath::VInterpConstantTo(GetActorLocation(),Destination,Delta,Profile->ProjectileSpeed);
  for(auto* Candidate:Battle->Units())if(IsValid(Candidate)&&U->GetCombat()->IsValidEnemy(Candidate))
   if(const auto* Wall=Candidate->FindComponentByClass<UEGCorruptionCombatComponent>();Wall&&Wall->BlocksMovement(GetActorLocation(),Next,0.f))
   {Battle->Damage(U,Candidate,RawDamage,AttackId,!bMonsterUltimate);Destroy();return;}
  SetActorLocation(Next);
  if(FVector::DistSquared(Next,Destination)<=FMath::Square(Profile->ProjectileRadius))
  {
   if(Battle->Damage(U,T,RawDamage,AttackId,!bMonsterUltimate)&&!bMonsterUltimate)Battle->BasicHit(U,T,AttackId);
   Destroy();
  }
  return;
 }
 if(Elapsed==0&&Source.IsValid())
 {
  if(Kind==EEGProjectileKind::AreaImpact){Visual->SetStaticMesh(Source->GetDefinition()->GetProjectileMesh().LoadSynchronous());Visual->SetWorldScale3D(FVector(.8));}
  if(auto* Mat=Source->GetDefinition()->GetAttackMaterial().LoadSynchronous())Visual->SetMaterial(0,Mat);
  if(Kind==EEGProjectileKind::ThrownSummon)Visual->SetVisibility(false);
  if(Kind==EEGProjectileKind::ThrownSummon)
  {
   // Telegraph the fixed landing point for the whole descent. Radius follows the
   // actual impact source; the flat ring uses the shared placement-ring material.
   if(Source.IsValid()&&Source->GetDefinition())
    DescentRingRadius=FMath::Max(60.f,Source->GetDefinition()->GetSkillRules().AreaRadius);
   if(DescentRingRadius>0.f&&!DescentRing.IsValid())
    DescentRing=AEGDescentRing::SpawnTelegraph(GetWorld(),Destination-FVector(0,0,88),
     nullptr,DescentRingRadius,Duration);
  }
 }
 const float PreviousElapsed=Elapsed;Elapsed+=Delta;
 if((Kind==EEGProjectileKind::BasicAttack)&&Target.IsValid()&&IEGCombatant::Execute_IsAlive(Target.Get()))Destination=Target->GetActorLocation();
 const float A=FMath::Clamp(Elapsed/Duration,0.f,1.f);
 FVector P=FMath::Lerp(Start,Destination,A);
 if(Kind==EEGProjectileKind::ThrownSummon)
 {
  if(!Cargo.IsValid()||!IEGCombatant::Execute_IsAlive(Cargo.Get())||!Source.IsValid()||!IEGCombatant::Execute_IsAlive(Source.Get())){Destroy();return;}
  if(SummonMotion&&SummonMotion->NormalizedOffsets.Num()>=2)
  {
   if(bFlightRebased)
   {
    // Keep the remaining source-curve detail relative to the new segment's
    // endpoints. It begins exactly at Start and ends at the relocated landing.
    const FVector Current=SampleSummonOffset(SummonMotion,FMath::Lerp(RebasedCurveStart,1.f,A));
    const FVector First=SampleSummonOffset(SummonMotion,RebasedCurveStart);
    const FVector Last=SampleSummonOffset(SummonMotion,1.f);
    P+=FlightBasis.RotateVector((Current-FMath::Lerp(First,Last,A))*FlightHeight);
   }
   else P=Destination+FlightBasis.RotateVector(SampleSummonOffset(SummonMotion,A)*FlightHeight);
  }
  else if(!bFlightRebased)P.Z+=220*4*A*(1-A);
  if(bFlightRebased&&IEGCombatant::Execute_GetFaction(Cargo.Get())==EEGFaction::Player)
   for(auto* Candidate:Battle->Units())if(IsValid(Candidate))
    if(const auto* Wall=Candidate->FindComponentByClass<UEGCorruptionCombatComponent>();Wall&&Wall->IsWallActive())
     P=Wall->ConstrainPlayerLocation(P,UEGCombatComponent::BlockingRadius(Cargo.Get()));
 }
 if(Kind==EEGProjectileKind::ThrownSummon&&A>=1.f)
 {
  // Keep the summon airborne until a non-overlapping location is available.
  // Retry on the battle clock; no gameplay entry or impact is emitted yet.
  const bool Retry=PreviousElapsed<Duration||FMath::FloorToInt((PreviousElapsed-Duration)*4.f)!=FMath::FloorToInt((Elapsed-Duration)*4.f);
  bool Found=false;FVector Landing=Destination;
  if(Retry&&Cargo.IsValid())Landing=Cargo->GetCombat()->FindFreeBoardLocation(Destination,&Found);
  if(!Found)
  {
   P=Destination+FVector(0,0,FMath::Max(80.f,UEGCombatComponent::BlockingRadius(Cargo.Get())));
   SetActorLocation(P);if(Cargo.IsValid())Cargo->SetActorLocation(P);return;
  }
  // Collision clearance moves the body, not the telegraphed attack center.
  // Destination also remains the authoritative center after a flight rebase.
  P=Landing;
 }
 if(Kind==EEGProjectileKind::AreaImpact)P.Z+=500*(1-A);
 if(Kind==EEGProjectileKind::BasicAttack&&Source.IsValid())
  for(auto* Candidate:Battle->Units())if(IsValid(Candidate)&&Source->GetCombat()->IsValidEnemy(Candidate))
   if(const auto* Wall=Candidate->FindComponentByClass<UEGCorruptionCombatComponent>();Wall&&Wall->BlocksMovement(GetActorLocation(),P,0.f))
   {
    if(Battle->Damage(Source.Get(),Candidate,RawDamage,AttackId,true))Battle->BasicHit(Source.Get(),Candidate,AttackId);
    Destroy();return;
   }
 SetActorLocation(P);if((Kind==EEGProjectileKind::BasicAttack))SetActorRotation((Destination-Start).Rotation()+FRotator(-90,0,0));
 if(Cargo.IsValid())Cargo->SetActorLocation(P);
 if(A<1)return;
 if(Kind==EEGProjectileKind::ThrownSummon)
 {
  if(Cargo.IsValid())
  {
   Cargo->GetHeroSkill()->bInFlight=false;Cargo->RefreshBoardCollision();Cargo->SetActorHiddenInGame(false);Battle->EnterUnit(Cargo.Get());
   for(auto* Summoner:Battle->Units())if(Summoner&&Summoner->GetInstanceId()==Cargo->GetHeroSkill()->SummonerId)
   {Summoner->GetHeroSkill()->PresentSummonImpact(AttackId,Round,Destination-FVector(0,0,88));
    AEGChessNiagaraBurst::Play(Summoner->GetHeroSkill(),TEXT("SummonImpact"),Destination-FVector(0,0,60));
    if(auto* Audio=Summoner->FindComponentByClass<UEGCombatAudioComponent>())Audio->Land(AttackId,Round,Destination-FVector(0,0,88));break;}
   // Replace the descent warning with one expanding impact shockwave.
   if(DescentRing.IsValid()){DescentRing->Destroy();DescentRing.Reset();}
   if(DescentRingRadius>0.f)
    AEGDescentRing::SpawnShockwave(GetWorld(),Destination-FVector(0,0,88),
     nullptr,DescentRingRadius,.45f);
   Battle->Area(Source.Get(),Destination,Source.IsValid()?Source->GetDefinition()->GetSkillRules().AreaRadius:160.f,RawDamage,false,false,AttackId);Battle->RefreshTeam();
  }
 }
 else if(Kind==EEGProjectileKind::AreaImpact)Battle->Area(Source.Get(),Destination,Source.IsValid()?Source->GetDefinition()->GetSkillRules().AreaRadius:300.f,RawDamage,false,false,AttackId);
 else if(Target.IsValid()&&IEGCombatant::Execute_IsAlive(Target.Get())&&Target->GetHeroSkill()->IsTargetable())
 {
  const bool Applied=Battle->Damage(Source.Get(),Target.Get(),RawDamage,AttackId,Kind==EEGProjectileKind::BasicAttack);
  if(Applied&&Kind==EEGProjectileKind::BasicAttack&&Source.IsValid())Battle->BasicHit(Source.Get(),Target.Get(),AttackId);
 }
 Destroy();
}

void AEGSkillProjectile::EndPlay(const EEndPlayReason::Type Reason)
{
 if(DescentRing.IsValid()){DescentRing->Destroy();DescentRing.Reset();}
 if(DescentEffect.IsValid())DescentEffect->Destroy();
 if(Cargo.IsValid()&&SavedCargoMaterial)Cargo->GetVisiblePresentationMesh()->SetMaterial(0,SavedCargoMaterial);
 if(Kind==EEGProjectileKind::ThrownSummon&&Cargo.IsValid()&&Cargo->GetHeroSkill()->bInFlight)
 {
  if(Battle.IsValid()&&Battle->IsBattleActive()&&IEGCombatant::Execute_IsAlive(Cargo.Get()))Cargo->Die();
  Cargo->Destroy();
 }
 Super::EndPlay(Reason);
}
