#include "Skills/EGSakuraVillVSkillBehaviors.h"
#include "Skills/EGSupportSkillUtils.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGBoardShotVisual.h"
#include "Presentation/EGVillVShotVisual.h"
#include "Presentation/EGVillVCombatFX.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGFiveHeroPresentation.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "NiagaraSystem.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Animation/AnimInstance.h"
#include "Math/RotationMatrix.h"

namespace
{
// V2 shot presentation uses the actual visible target body, sampled before
// damage can destroy or change it. No body/bounds means the old point is kept.
FVector VillVVisualImpactPoint(const AEGChessUnit* Target,const FVector& Fallback)
{
 if(!IsValid(Target))return Fallback;
 const auto* Mesh=Target->GetVisiblePresentationMesh();
 if(!IsValid(Mesh)||!Mesh->IsRegistered()||!Mesh->IsVisible()||!Mesh->GetSkeletalMeshAsset())return Fallback;
 const FBoxSphereBounds& Bounds=Mesh->Bounds;
 if(Bounds.Origin.ContainsNaN()||Bounds.BoxExtent.ContainsNaN()||Bounds.BoxExtent.Z<=1.)return Fallback;
 // 55% of visible bounds height; this is strictly cosmetic.
 const FVector Point=Bounds.Origin+FVector(0,0,Bounds.BoxExtent.Z*.10);
 return Point.ContainsNaN()?Fallback:Point;
}

struct FSakuraImpactFrame
{
 FVector Point;
 FRotator Rotation;
};

// Cosmetic only: sample the target before damage can hide or destroy it.
// Present flat impact strokes just in front of the visible body, facing the
// fixed player camera while retaining each track's authored local rotation.
FSakuraImpactFrame SakuraImpactFrame(const AEGChessUnit* Caster,const AEGChessUnit* Target,const FRotator& FallbackRotation)
{
 const FVector Fallback=IsValid(Target)?Target->GetActorLocation():FVector::ZeroVector;
 FSakuraImpactFrame Result{VillVVisualImpactPoint(Target,Fallback),FallbackRotation};
 if(!IsValid(Caster)||!IsValid(Target)||!Caster->GetWorld()||Caster->GetWorld()->GetNetMode()==NM_DedicatedServer)return Result;
 const auto* Mesh=Target->GetVisiblePresentationMesh();
 const auto* Camera=UGameplayStatics::GetPlayerCameraManager(Caster,0);
 if(!IsValid(Mesh)||!Mesh->IsRegistered()||!Mesh->IsVisible()||!Mesh->GetSkeletalMeshAsset()||!IsValid(Camera))return Result;
 const FBoxSphereBounds& Bounds=Mesh->Bounds;
 const FVector CameraLocation=Camera->GetCameraLocation();
 if(Bounds.Origin.ContainsNaN()||Bounds.BoxExtent.ContainsNaN()||CameraLocation.ContainsNaN()
    ||Bounds.BoxExtent.GetMin()<=0.f||Result.Point.ContainsNaN())return Result;
 const FBox Box=Bounds.GetBox();
 if(!Box.IsValid||!Box.IsInsideOrOn(Result.Point))return Result;
 const FVector ToCamera=CameraLocation-Result.Point;
 const double CameraDistance=ToCamera.Size();
 if(!FMath::IsFinite(CameraDistance)||CameraDistance<=1.0)return Result;
 const FVector Direction=ToCamera/CameraDistance;
 double Exit=MAX_dbl;
 for(int32 Axis=0;Axis<3;++Axis)
 {
  const double D=Direction[Axis];if(FMath::Abs(D)<UE_DOUBLE_SMALL_NUMBER)continue;
  const double Edge=D>0?Box.Max[Axis]:Box.Min[Axis];
  Exit=FMath::Min(Exit,(Edge-Result.Point[Axis])/D);
 }
 if(!FMath::IsFinite(Exit)||Exit<0||Exit==MAX_dbl)return Result;
 const double Distance=FMath::Min3(Exit+3.0,180.0,CameraDistance-1.0);
 if(Distance<=0.0)return Result;
 Result.Point+=Direction*Distance;
 Result.Rotation=FRotationMatrix::MakeFromZ(Direction).Rotator();
 return Result;
}

// Both distance and angle are measured on the board plane, against actor centers.
bool SakuraSlashContains(const FVector& Origin,const FVector& Forward,const FVector& Target,float Radius)
{
 if(Origin.ContainsNaN()||Forward.ContainsNaN()||Target.ContainsNaN()||!FMath::IsFinite(Radius)||Radius<=0.f)return false;
 const FVector Delta(Target.X-Origin.X,Target.Y-Origin.Y,0.f);
 const FVector Direction=Forward.GetSafeNormal2D();
 const double DistanceSquared=Delta.SizeSquared2D();
 if(Direction.IsNearlyZero()||DistanceSquared>FMath::Square(double(Radius)))return false;
 // A target at the origin belongs to the sector; its direction is undefined.
 if(DistanceSquared<=UE_DOUBLE_SMALL_NUMBER)return true;
 constexpr double CosHalfAngle=0.8660254037844386; // cos(30 degrees)
 return FVector::DotProduct(Direction,Delta)+UE_DOUBLE_SMALL_NUMBER>=CosHalfAngle*FMath::Sqrt(DistanceSquared);
}
#if WITH_DEV_AUTOMATION_TESTS
FVector VillVViewAdjustedImpact(const FBox& Box,const FVector& Point,const FVector& Camera,double Height,double Fraction,double Maximum)
{
 if(!Box.IsValid||Point.ContainsNaN()||Camera.ContainsNaN()||!Box.IsInsideOrOn(Point)||
    !FMath::IsFinite(Height)||!FMath::IsFinite(Fraction)||!FMath::IsFinite(Maximum))return Point;
 const FVector ToCamera=Camera-Point;
 const double CameraDistance=ToCamera.Size();
 if(!FMath::IsFinite(CameraDistance)||CameraDistance<=1.0)return Point;
 const FVector Direction=ToCamera/CameraDistance;
 double Exit=MAX_dbl;
 for(int32 Axis=0;Axis<3;++Axis)
 {
  const double D=Direction[Axis];if(FMath::Abs(D)<UE_DOUBLE_SMALL_NUMBER)continue;
  const double Edge=D>0?Box.Max[Axis]:Box.Min[Axis];
  Exit=FMath::Min(Exit,(Edge-Point[Axis])/D);
 }
 const double Limit=FMath::Min3(FMath::Max(0.0,Height)*FMath::Clamp(Fraction,0.0,1.0),FMath::Max(0.0,Maximum),CameraDistance-1.0);
 if(!FMath::IsFinite(Exit)||Exit<0||Limit<=0)return Point;
 return Point+Direction*FMath::Min(Exit+2.0,Limit);
}
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVImpactViewOffset,"ElyGambit.VillVCombat.ImpactViewOffsetBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVImpactViewOffset::RunTest(const FString&)
{
 const FVector Origin=FVector::ZeroVector,FarCamera(1000,0,0);
 const FBox Small(FVector(-20),FVector(20)),Large(FVector(-200),FVector(200));
 TestTrue(TEXT("Small target reaches near surface without floating far away"),VillVViewAdjustedImpact(Small,Origin,FarCamera,120,.4,150).Equals(FVector(22,0,0),.01));
 TestTrue(TEXT("Large target offset respects maximum"),VillVViewAdjustedImpact(Large,Origin,FarCamera,500,.4,150).Equals(FVector(150,0,0),.01));
 TestTrue(TEXT("Visual point outside bounds remains unchanged"),VillVViewAdjustedImpact(Small,FVector(30,0,0),FarCamera,120,.4,150).Equals(FVector(30,0,0),.01));
 TestTrue(TEXT("Camera inside bounds is never crossed"),VillVViewAdjustedImpact(Large,Origin,FVector(10,0,0),500,.4,150).Equals(FVector(9,0,0),.01));
 TestTrue(TEXT("Disabled adjustment keeps original point"),VillVViewAdjustedImpact(Small,Origin,FarCamera,120,0,150).Equals(Origin,.01));
 return true;
}
#endif

bool UEGSakuraSkillBehavior::PlayConfiguredEffect(const TSoftObjectPtr<UEGSourceEffectDefinition>& Effect,const FVector& Point,const FRotator& Rotation,FGuid Action,FGuid Round,bool bExecution,bool bAllowCompleted)
{
 auto* U=Unit.Get();auto* S=Skill.Get();auto* Mesh=U?U->GetVisiblePresentationMesh():nullptr;
 if(Effect.IsNull()||!Action.IsValid()||!Round.IsValid()||!IsCurrentBattle()||RoundId!=Round||!U||!S||!IsValid(Mesh)
    ||Point.ContainsNaN()||Rotation.ContainsNaN()||(!bAllowCompleted&&(!S->IsActionRunning()||S->ActionId!=Action))||!IEGCombatant::Execute_IsAlive(U))return false;
 auto* Definition=Effect.LoadSynchronous();
 if(!IsValid(Definition)||!Definition->AnchorBone.IsNone()||!IsCurrentBattle()||RoundId!=Round
    ||!Unit.IsValid()||!Skill.IsValid()||!IsValid(Mesh)||(!bAllowCompleted&&(!S->IsActionRunning()||S->ActionId!=Action))||!IEGCombatant::Execute_IsAlive(U))return false;
 auto* Anim=Mesh->GetAnimInstance();auto* Montage=S->GetActiveMontage();
 const float MontageRate=Anim&&Montage&&S->ActionId==Action?Anim->Montage_GetPlayRate(Montage):1.f;
 if(!FMath::IsFinite(MontageRate)||MontageRate<=0.f)return false;
 // Execution accents are authored as brief tails, even at slow action rates.
 const float Rate=bExecution?FMath::Max(1.f,MontageRate):MontageRate;
 auto* Visual=AEGSourceEffect::Spawn(S,Mesh,Definition,Rate,true);
 if(!Visual)return false;
 if(!IsCurrentBattle()||RoundId!=Round||!Unit.IsValid()||!Skill.IsValid()||(!bAllowCompleted&&(!S->IsActionRunning()||S->ActionId!=Action))
    ||!IEGCombatant::Execute_IsAlive(U)){Visual->Destroy();return false;}
 Visual->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
 Visual->SetActorTransform(FTransform(Rotation,Point,Visual->GetActorScale3D()));
 auto& Effects=bExecution?ExecutionEffects:ConfiguredEffects;
 Effects.RemoveAll([](const auto& Item){return !Item.IsValid();});
 Effects.Add(Visual);
 return true;
}

void UEGSakuraSkillBehavior::ClearConfiguredEffects()
{
 for(const auto& Effect:ConfiguredEffects)if(Effect.IsValid())Effect->Destroy();
 ConfiguredEffects.Reset();
 // Stop invalidates the battle round before calling CancelAction/EndBattle.
 // Preserve only confirmed executions for a living caster during that stop;
 // round-bound SourceFX owns its remaining lifetime and watches death/new rounds.
 const bool bKeepExecutionTail=Battle.IsValid()&&!Battle->IsBattleActive()
    &&Unit.IsValid()&&IEGCombatant::Execute_IsAlive(Unit.Get());
 if(!bKeepExecutionTail)for(const auto& Effect:ExecutionEffects)if(Effect.IsValid())Effect->Destroy();
 ExecutionEffects.Reset();
}

void UEGSakuraSkillBehavior::Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle)
{
 Super::Initialize(InSkill,InBattle);
 EvadeCooldownRemaining=0.f;bDelayedAttackReady=false;PendingSlashes.Reset();EvadedActions.Reset();
 if(Unit.IsValid())Unit->OnAttributeChanged.AddUniqueDynamic(this,&ThisClass::OwnerAttributeChanged);
}
void UEGSakuraSkillBehavior::OwnerAttributeChanged(FName Attribute,float OldValue,float NewValue)
{
 if(Attribute==TEXT("Health")&&NewValue<=0.f)
 {
  while(!PendingSlashes.IsEmpty())CancelPending(PendingSlashes[0].Action);
  bDelayedAttackReady=false;
 }
}
bool UEGSakuraSkillBehavior::EvadeDirectAttack(AEGChessUnit* Source,FGuid Id,bool bBasicAttack,bool& bNewlyEvaded)
{
 bNewlyEvaded=false;
 if(!bBasicAttack||!IsCurrentBattle()||!Unit.IsValid()||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get())
    ||!IsValid(Source)||Source==Unit.Get()||IEGCombatant::Execute_GetFaction(Source)==EEGFaction::Neutral
    ||IEGCombatant::Execute_GetFaction(Unit.Get())==EEGFaction::Neutral
    ||IEGCombatant::Execute_GetFaction(Source)==IEGCombatant::Execute_GetFaction(Unit.Get()))return false;
 if(Id.IsValid()&&EvadedActions.Contains(Id))return true;
 if(EvadeCooldownRemaining>0.f)return false;
 const auto* D=Unit->GetDefinition()?Cast<UEGSakuraSkillDefinition>(Unit->GetDefinition()->SkillDefinition):nullptr;
 if(!D||!D->HasValidSkillRules())return false;
 const FVector Direction=(Source->GetActorLocation()-Unit->GetActorLocation()).GetSafeNormal2D();
 if(Direction.IsNearlyZero()||FVector::DotProduct(Unit->GetActorForwardVector().GetSafeNormal2D(),Direction)<D->FrontalEvadeDot)return false;
 EvadeCooldownRemaining=D->EvadeCooldownSeconds;bDelayedAttackReady=true;
 if(Id.IsValid())EvadedActions.Add(Id);
 bNewlyEvaded=true;
 return true;
}
void UEGSakuraSkillBehavior::OnEvadeCommitted(AEGChessUnit* Source,FGuid Id)
{
 if(!IsCurrentBattle()||!Unit.IsValid()||!IsValid(Source)||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const FGuid ThisRound=RoundId;TWeakObjectPtr<AEGChessUnit> Owner=Unit;
 AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraEvade,Owner.Get(),Source,Owner->GetActorLocation(),ThisRound,Id);
 if(Owner.IsValid()&&IsCurrentBattle()&&RoundId==ThisRound)OnOwnerEvaded.Broadcast(Source,Id);
}
void UEGSakuraSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle())return;
 if(!C.bUltimate){AEGChessNiagaraBurst::Swing(Skill.Get(),C.BasicAttackIndex);return;}
 ActiveUltimateAction=C.ActionId;bSlashSubmitted=false;bFanStarted=false;
 Skill->SetActionProtected(true);
 // The marked area and the later slash use one immutable board-space sector.
 UltimateOrigin=Unit->GetActorLocation();UltimateForward=Unit->GetActorForwardVector().GetSafeNormal2D();
}
void UEGSakuraSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 auto* U=Unit.Get();auto* T=C.Target.Get();
 if(!C.bUltimate)
 {
  if(Event!=TEXT("Hit")||!U->GetCombat()->IsValidEnemy(T)||!U->GetCombat()->IsInAttackRange(T))return;
  if(bDelayedAttackReady)
  {
   const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
   if(!D||!D->HasValidSkillRules())return;
   bDelayedAttackReady=false;
   FPendingSlash Pending;Pending.Round=C.RoundId;Pending.Action=C.ActionId;Pending.Damage=C.Attack*D->CounterMultiplier;Pending.AttackIndex=C.BasicAttackIndex;Pending.Remaining=D->DelayedSlashSeconds;
   Pending.Targets.Add(T);PendingSlashes.Add(MoveTemp(Pending));
   AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraDelayedMark,U,T,T->GetActorLocation(),C.RoundId,C.ActionId);
   return;
  }
  const FRotator ImpactRotation=(U->GetActorRotation().Quaternion()*FRotator(0,0,90).Quaternion()).Rotator();
  const FSakuraImpactFrame Impact=SakuraImpactFrame(U,T,ImpactRotation);
  TWeakObjectPtr<AEGChessUnit> WeakTarget(T);
  if(Event==TEXT("Hit")&&U->GetCombat()->IsValidEnemy(T)&&U->GetCombat()->IsInAttackRange(T)&&Battle->Damage(U,T,C.Attack,C.ActionId,true))
  {
   if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(TEXT("Hit"),C.ActionId,T);
   Battle->BasicHit(U,T,C.ActionId);
   if(IsCurrentBattle()&&C.RoundId==RoundId&&Skill.IsValid()&&Skill->IsActionRunning()&&Skill->ActionId==C.ActionId
      &&Unit.IsValid()&&IEGCombatant::Execute_IsAlive(U)&&WeakTarget.IsValid())
   if(const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition))
   {
    if(!PlayConfiguredEffect(D->BasicHitPresentation,Impact.Point,Impact.Rotation,C.ActionId,C.RoundId)
       &&IsCurrentBattle()&&Skill.IsValid()&&Skill->IsActionRunning()&&Skill->ActionId==C.ActionId&&WeakTarget.IsValid())
    {
     if(D->ChessFX.Contains(TEXT("Strike")))AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Strike"),WeakTarget->GetActorLocation(),U->GetActorRotation());
     else AEGChessNiagaraBurst::Spawn(Skill.Get(),Battle.Get(),D->ChessStrikeFX.Get(),WeakTarget->GetActorLocation());
    }
   }
  }
 }
 else if(Event==TEXT("FanStart"))
 {
  const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
  if(!D||!D->HasValidSkillRules()||!C.ActionId.IsValid()||C.ActionId!=ActiveUltimateAction||Skill->ActionId!=C.ActionId||!Skill->IsCasting()||bFanStarted||UltimateForward.IsNearlyZero())return;
  bFanStarted=true;
  if(FMath::IsFinite(D->UltimateRadius)&&D->UltimateRadius>0.f&&FMath::IsFinite(D->AreaPresentationReferenceRadius)&&D->AreaPresentationReferenceRadius>0.f)
   Skill->PresentSourceEffectAt(D->AreaPresentation.LoadSynchronous(),UltimateOrigin-FVector(0,0,85),UltimateForward.Rotation(),D->UltimateRadius/D->AreaPresentationReferenceRadius);
 }
 else if(Event==TEXT("Slash"))
 {
  const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
  if(!D||!D->HasValidSkillRules()||!C.ActionId.IsValid()||C.ActionId!=ActiveUltimateAction||Skill->ActionId!=C.ActionId||!Skill->IsCasting()||bSlashSubmitted)return;
  // Commit before effects/damage: callbacks may synchronously cancel the action or end the round.
  bSlashSubmitted=true;
  if(!Skill->CommitStigmataUltimate(C.ActionId))return;
  const FVector Point=UltimateOrigin,Forward=UltimateForward;const float Radius=D->UltimateRadius;
  if(!PlayConfiguredEffect(D->SlashReleasePresentation,Point-FVector(0,0,88),Forward.Rotation(),C.ActionId,C.RoundId)
     &&IsCurrentBattle()&&Skill.IsValid()&&Skill->IsCasting()&&Skill->ActionId==C.ActionId)
  {
   if(D->ChessFX.Contains(TEXT("Slash")))AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Slash"),Point-FVector(0,0,88),Forward.Rotation());
   else AEGChessNiagaraBurst::Spawn(Skill.Get(),Battle.Get(),D->ChessReleaseFX.Get(),Point);
  }
  FPendingSlash Pending;Pending.Round=C.RoundId;Pending.Action=C.ActionId;Pending.bUltimate=true;Pending.bKillMana=C.Tier>=2;
  Pending.Damage=C.Attack*C.Multiplier*U->GetUltimateBonus();Pending.Origin=Point;Pending.Forward=Forward;Pending.Remaining=D->DelayedSlashSeconds;
  const auto Targets=Battle->Units();
  for(auto* Target:Targets)
  {
   if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(U)||!Skill->IsCasting()||Skill->ActionId!=C.ActionId||Battle->GetRoundId()!=C.RoundId)break;
   if(!U->GetCombat()->IsValidEnemy(Target)||!SakuraSlashContains(Point,Forward,Target->GetActorLocation(),Radius))continue;
   Pending.Targets.Add(Target);
  }
  const auto MarkedTargets=Pending.Targets;
  PendingSlashes.Add(MoveTemp(Pending));
  for(const auto& Target:MarkedTargets)
  {
   if(!IsCurrentBattle()||!Skill.IsValid()||!Skill->IsCasting()||Skill->ActionId!=C.ActionId)break;
   if(Target.IsValid())AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraFanMark,U,Target.Get(),Target->GetActorLocation(),C.RoundId,C.ActionId);
  }
 }
}
void UEGSakuraSkillBehavior::CancelPending(FGuid Action)
{
 const bool bHadPending=PendingSlashes.ContainsByPredicate([&](const FPendingSlash& P){return P.Action==Action;});
 PendingSlashes.RemoveAll([&](const FPendingSlash& P){return P.Action==Action;});
 if(ResolvingAction==Action)bResolvingCancelled=true;
 if((bHadPending||ResolvingAction==Action)&&Unit.IsValid())AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraFanCancel,Unit.Get(),nullptr,Unit->GetActorLocation(),RoundId,Action);
}
void UEGSakuraSkillBehavior::TickBattle(float DeltaSeconds)
{
 if(!IsCurrentBattle()||!FMath::IsFinite(DeltaSeconds)||DeltaSeconds<0.f)return;
 EvadeCooldownRemaining=FMath::Max(0.f,EvadeCooldownRemaining-DeltaSeconds);
 for(auto& Pending:PendingSlashes)Pending.Remaining-=DeltaSeconds;
 while(IsCurrentBattle())
 {
  const int32 Due=PendingSlashes.IndexOfByPredicate([](const FPendingSlash& P){return P.Remaining<=0.f;});
  if(Due==INDEX_NONE)break;
  FPendingSlash Pending=MoveTemp(PendingSlashes[Due]);PendingSlashes.RemoveAt(Due);
  ResolvePending(MoveTemp(Pending));
 }
}
void UEGSakuraSkillBehavior::ResolvePending(FPendingSlash Pending)
{
 auto* U=Unit.Get();
 if(!IsCurrentBattle()||Pending.Round!=RoundId||!IsValid(U)||!IEGCombatant::Execute_IsAlive(U))return;
 const auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
 if(!D||!D->HasValidSkillRules())return;
 ResolvingAction=Pending.Action;bResolvingCancelled=false;bool bRefunded=false;
 for(const auto& WeakTarget:Pending.Targets)
 {
  if(bResolvingCancelled||!IsCurrentBattle()||Pending.Round!=RoundId||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(U))break;
  auto* Target=WeakTarget.Get();
  if(!U->GetCombat()->IsValidEnemy(Target))
  {
   if(IsValid(Target))AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraFanCancel,U,Target,Target->GetActorLocation(),Pending.Round,Pending.Action);
   continue;
  }
  const FVector Point=Target->GetActorLocation();
  const FRotator ImpactRotation=(Pending.Forward.Rotation().Quaternion()*FRotator(0,0,90).Quaternion()).Rotator();
  const FSakuraImpactFrame Impact=SakuraImpactFrame(U,Target,ImpactRotation);
  const FGuid SourceId=U->GetInstanceId(),TargetId=Target->GetInstanceId();
  bool bAccepted=false,bExecution=false;
  if(Pending.bUltimate)
  {
   const auto* Attributes=Target->GetAttributes();
   if(!Attributes||!FMath::IsFinite(Attributes->GetHealth())||!FMath::IsFinite(Attributes->GetMaxHealth())||Attributes->GetMaxHealth()<=0.f)continue;
   if(Target->GetDefinition()->HeroKind==EEGHeroKind::Generic&&!Target->GetDefinition()->bEliteEnemy
      &&!Target->FindComponentByClass<UEGCorruptionCombatComponent>()
      &&Attributes->GetHealth()<=Attributes->GetMaxHealth()*D->ExecuteHealthFraction)
   {
    bAccepted=FEGCombatExecution::Execute(U,Target,Pending.Action);
    if(bAccepted&&WeakTarget.IsValid())
    {
     const auto& Receipt=WeakTarget->LastCombatResolution;
     bExecution=Receipt.SourceId==SourceId&&Receipt.TargetId==TargetId&&Receipt.ActionId==Pending.Action&&Receipt.bExecution&&Receipt.HealthDelta<0.f;
    }
   }
   else bAccepted=Battle->Damage(U,Target,Pending.Damage,Pending.Action);
  }
  else bAccepted=Battle->Damage(U,Target,Pending.Damage,Pending.Action,true);
  if(bResolvingCancelled||!IsCurrentBattle()||Pending.Round!=RoundId||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(U))break;
  if(!bAccepted)
  {
   if(WeakTarget.IsValid())AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraFanCancel,U,WeakTarget.Get(),Point,Pending.Round,Pending.Action);
   continue;
  }
  if(Pending.bUltimate)
  {
   if(Pending.bKillMana&&!bRefunded&&WeakTarget.IsValid()&&!IEGCombatant::Execute_IsAlive(WeakTarget.Get()))
   {bRefunded=true;U->RestoreMana(D->KillMana);}
   if(bExecution)
   {
    if(!PlayConfiguredEffect(D->ExecutionImpactPresentation,Impact.Point,Impact.Rotation,Pending.Action,Pending.Round,true,true))
     if(const FEGChessFXCue* Config=D->ChessFX.Find(TEXT("Execute"));Config&&Config->System&&IsCurrentBattle())
     {
      FEGChessFXCue Cue=*Config;Cue.bActionBound=false;Cue.bActionClock=false;Cue.bAttachToOwner=false;
      AEGChessNiagaraBurst::SpawnCue(Skill.Get(),Battle.Get(),Cue,Point,Pending.Forward.Rotation(),true);
     }
   }

  }
  else if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())
   Audio->CommittedImpact(TEXT("Hit"),Pending.Round,Pending.Action,false,Pending.AttackIndex,0,Point);
  if(bResolvingCancelled||!IsCurrentBattle()||Pending.Round!=RoundId||!Unit.IsValid()||!IEGCombatant::Execute_IsAlive(U))break;
  if(!Pending.bUltimate&&WeakTarget.IsValid())Battle->BasicHit(U,WeakTarget.Get(),Pending.Action);
  if(!bResolvingCancelled&&IsCurrentBattle()&&Unit.IsValid())
  {
   if(bExecution)
    AEGFiveHeroPresentation::Emit(EEGFiveHeroCue::SakuraFanCancel,U,WeakTarget.Get(),Point,Pending.Round,Pending.Action);
   else if(!AEGFiveHeroPresentation::Emit(Pending.bUltimate?EEGFiveHeroCue::SakuraFanRelease:EEGFiveHeroCue::SakuraDelayedRelease,
    U,WeakTarget.Get(),Point,Pending.Round,Pending.Action))
    PlayConfiguredEffect(D->NormalImpactPresentation,Impact.Point,Impact.Rotation,Pending.Action,Pending.Round,false,true);
  }
 }
 ResolvingAction.Invalidate();bResolvingCancelled=false;
}
void UEGSakuraSkillBehavior::ActionEnded(bool bCompleted)
{
 if(!bCompleted){if(Skill.IsValid())CancelPending(Skill->ActionId);ClearConfiguredEffects();}
 ActiveUltimateAction.Invalidate();bSlashSubmitted=false;bFanStarted=false;UltimateOrigin=FVector::ZeroVector;UltimateForward=FVector::ForwardVector;
}
void UEGSakuraSkillBehavior::EndBattle()
{
 if(Unit.IsValid())Unit->OnAttributeChanged.RemoveDynamic(this,&ThisClass::OwnerAttributeChanged);
 PendingSlashes.Reset();EvadedActions.Reset();EvadeCooldownRemaining=0.f;bDelayedAttackReady=false;
 ClearConfiguredEffects();ActiveUltimateAction.Invalidate();bSlashSubmitted=false;bFanStarted=false;UltimateOrigin=FVector::ZeroVector;UltimateForward=FVector::ForwardVector;Super::EndBattle();
}


void UEGVillVSkillBehavior::Initialize(UEGHeroSkillComponent* S,UEGBattleContextComponent* B)
{
 Super::Initialize(S,B);
 if(!IsCurrentBattle())return;
 Mechanism=Unit->FindComponentByClass<UEGVillVMechanismComponent>();
 if(!Mechanism){Mechanism=NewObject<UEGVillVMechanismComponent>(Unit.Get());Unit->AddInstanceComponent(Mechanism);Mechanism->RegisterComponent();}
 Mechanism->Start(B);
}
bool UEGVillVSkillBehavior::IsCannon() const{return Mechanism&&Mechanism->Mode()==1;}
bool UEGVillVSkillBehavior::HasEnhancedShot() const{return Mechanism&&Mechanism->HasEnhancedShot();}
bool UEGVillVSkillBehavior::BlocksCombat() const{return IsCurrentBattle()&&Mechanism&&!Mechanism->IsOperating();}
bool UEGVillVSkillBehavior::AllowsAction(bool bUltimate) const{return Mechanism&&Mechanism->IsOperating()&&(bUltimate||Mechanism->IsAimReady());}
int32 UEGVillVSkillBehavior::PresentationIndex(bool bUltimate,int32 Default) const
{return Mechanism?(bUltimate?Mechanism->UltimatePresentation():Mechanism->Mode()):Default;}
void UEGVillVSkillBehavior::BeforeActionPresentation(){if(Mechanism)Mechanism->ClearSeatedBody();}
void UEGVillVSkillBehavior::TickAction(float Position){if(Mechanism)Mechanism->SampleAction(Position);FlushFire();}
void UEGVillVSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{Fired.Reset();PendingFires.Reset();FiringContext=C;if(Mechanism)Mechanism->BeginAction(C);}
void UEGVillVSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId||!Mechanism)return;
 if(Event==TEXT("Depart")||Event==TEXT("Dock")){Mechanism->ActionEvent(Event,C);return;}
 if(!Mechanism->IsOperating()||(Event!=TEXT("Fire1")&&Event!=TEXT("Fire2")&&Event!=TEXT("Fire3"))||Fired.Contains(Event)||PendingFires.Contains(Event))return;
 if(IsCannon()&&Event!=TEXT("Fire1"))return;
 FiringContext=C;PendingFires.Add(Event);FlushFire();
}
void UEGVillVSkillBehavior::FlushFire()
{
 if(!IsCurrentBattle()||!Mechanism||!Mechanism->IsOperating()||PendingFires.IsEmpty()||FiringContext.ActionId!=Skill->ActionId)return;
 auto* U=Unit.Get();auto* T=Mechanism->FiringTarget();
 // A fire notify authorizes this shot. If the old target dies or the barrel is
 // turning, retain that authorization until aimed or until this action ends.
 if(!T||!Mechanism->IsAimReady(T))return;
 const auto Event=PendingFires[0];PendingFires.RemoveAt(0);Fired.Add(Event);
 const auto& C=FiringContext;const bool Cannon=IsCannon();
 if(auto* Stigmata=Battle->GetStigmata())Stigmata->UpdateActionTarget(U,C.ActionId,T);
 const auto* D=Cast<UEGVillVSkillDefinition>(U->GetDefinition()->SkillDefinition);if(!D)return;
 if(C.bUltimate&&!Skill->CommitStigmataUltimate(C.ActionId))return;
 const float Damage=C.Attack*(Cannon?D->CannonMultiplier:1.f/3.f)*(Mechanism->ActionEnhanced()?C.Multiplier*U->GetUltimateBonus():1.f);
 const FVector Point=T->GetActorLocation();
 // Damage callbacks may invalidate the target or end the action. Capture only
 // presentation coordinates now; dispatch after damage under the original token.
 FTransform VisualMuzzle;
 const FGuid VisualAction=C.ActionId,VisualRound=C.RoundId;
 bool bHaveVisualMuzzle=Mechanism->CaptureV2Muzzle(Event,VisualMuzzle);
 if(bHaveVisualMuzzle)
 {
  // Existing coordinator samples the current montage position on the device:
  // notifies can arrive before this frame's normal skill-component tick.
  FVector Ignored;
  Skill->GetAssemblyBoneLocation(Cannon?TEXT("Cannon"):TEXT("Gatling"),
      Cannon?TEXT("CannonFire"):Event==TEXT("Fire1")?TEXT("FirePoint1"):Event==TEXT("Fire2")?TEXT("FirePoint2"):TEXT("FirePoint3"),Ignored);
  // A visual actor rebuild can notify external observers. Do not continue a
  // shot if that callback ended its action, round, target or mechanism.
  if(!IsCurrentBattle()||!IsValid(Mechanism)||!IsValid(U)||!IsValid(T)
     ||!Skill->IsActionRunning()||Skill->ActionId!=VisualAction||FiringContext.ActionId!=VisualAction
     ||Battle->GetRoundId()!=VisualRound)return;
  bHaveVisualMuzzle=Mechanism->CaptureV2Muzzle(Event,VisualMuzzle);
 }
 const FVector VisualPoint=bHaveVisualMuzzle?VillVVisualImpactPoint(T,Point):Point;
 const TWeakObjectPtr<AEGVillVEmplacement> VisualDevice=Mechanism->GetDevice(Mechanism->Mode());
 const bool bVisualEnhanced=Mechanism->ActionEnhanced();
 bool Hit=false;
 if(Cannon)
 {
  for(auto* Enemy:U->GetCombat()->FindEnemiesInRange(Point,EGSupport::Radius(U,D->CannonRadius/500.f)))
  {
   if(!IsValid(Enemy))continue;
   const bool Accepted=Battle->Damage(U,Enemy,Damage,C.ActionId,!C.bUltimate&&Enemy==T);
   Hit=Accepted||Hit;
  }
 }
 else Hit=FEGCombatExecution::Damage(U,T,Damage,0,1,FGameplayEffectContextHandle(),C.ActionId,false,1.f/3.f,!C.bUltimate);
 Mechanism->NotifyShot();
 if(Mechanism->ActionEnhanced()&&C.Tier>=2)Mechanism->BeginAutoFire(C.ActionId);
 if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent(Cannon?TEXT("CannonFire"):Event==TEXT("Fire1")?TEXT("GunFire1"):Event==TEXT("Fire2")?TEXT("GunFire2"):TEXT("GunFire3"),Point);
 // Armor may absorb all damage, but the authorized shot still animates/sounds.
 // Accepted-hit rewards retain the existing damage-resolution gate.
 if(Hit){Mechanism->ConsumeEnhancement();if(!C.bUltimate)Battle->BasicHit(U,T,C.ActionId);}
 if(bHaveVisualMuzzle&&VisualDevice.IsValid()&&IsCurrentBattle()&&IsValid(Mechanism))
  Mechanism->PresentV2Shot(Event,VisualMuzzle,VisualPoint,VisualAction,VisualRound,VisualDevice.Get(),bVisualEnhanced);
}
void UEGVillVSkillBehavior::ActionEnded(bool bCompleted){Fired.Reset();PendingFires.Reset();if(Mechanism)Mechanism->ActionEnded(bCompleted);}
void UEGVillVSkillBehavior::EndBattle(){if(Mechanism)Mechanism->Stop();Mechanism=nullptr;Fired.Reset();PendingFires.Reset();Super::EndBattle();}
