#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "Skills/EGSupportSkillUtils.h"
#include "Presentation/EGMobiusHeadProjection.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGMobiusLinkVisual.h"
#include "Presentation/EGMobiusSourceAttackVisual.h"
#include "Presentation/EGMobiusNiagaraEffect.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSkillNotify.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
 // Source impact tails use the same source-seconds clock as the montage.
 // Do not leave lightning/recovery running at real-time 1x under a faster cast.
 float MobiusSourcePlaybackRate(const UEGHeroSkillComponent* Skill)
 {
  const auto* Unit=Skill?Cast<AEGChessUnit>(Skill->GetOwner()):nullptr;
  const auto* Mesh=Unit?Unit->GetVisiblePresentationMesh():nullptr;
  const auto* Instance=Mesh?Mesh->GetAnimInstance():nullptr;
  const auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
  const float Rate=Instance&&Montage?Instance->Montage_GetPlayRate(Montage):1.f;
  return FMath::IsFinite(Rate)&&Rate>0.f?Rate:1.f;
 }
 template<class T> const T* Config(const AEGChessUnit* U)
 {return U&&U->GetDefinition()?Cast<T>(U->GetDefinition()->SkillDefinition):nullptr;}
 bool Basic(AEGChessUnit* U,UEGBattleContextComponent* B,FName Event,const FEGSkillActionContext& C,bool Ranged=false)
 {
  auto* Target=C.Target.Get();
  if(Event!=TEXT("Hit")||C.bUltimate||!U->GetCombat()->IsValidEnemy(Target)||!U->GetCombat()->IsInAttackRange(Target))return false;
  if(Ranged){B->Launch(U,Target,Target->GetActorLocation(),C.Attack,EEGProjectileKind::BasicAttack,C.ActionId);return true;}
  if(B->Damage(U,Target,C.Attack,C.ActionId,true)){B->BasicHit(U,Target,C.ActionId);return true;}
  return false;
 }
}

void UEGHuaSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
}
bool UEGHuaSkillBehavior::TryPreventDeath()
{
 const auto* D=Config<UEGHuaSkillDefinition>(Unit.Get());
 return IsCurrentBattle()&&D&&Skill->BeginRevive(D->ReviveDelay,D->ReviveHealthRatio,0);
}
void UEGHuaSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 auto* U=Unit.Get();const auto* D=Config<UEGHuaSkillDefinition>(U);if(!D)return;
 if(!C.bUltimate)
 {
  if(Event==TEXT("Hit"))AEGChessNiagaraBurst::Swing(Skill.Get(),C.BasicAttackIndex);
  if(Basic(U,Battle.Get(),Event,C)&&C.Target.IsValid())
  {
   if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(TEXT("Hit"),C.ActionId,C.Target.Get());
   if(D->ChessFX.Contains(TEXT("Strike")))AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Strike"),C.Target->GetActorLocation(),U->GetActorRotation());
   else AEGChessNiagaraBurst::Spawn(Skill.Get(),Battle.Get(),D->ChessStrikeFX.Get(),C.Target->GetActorLocation());
  }
  return;
 }
 if(Event==TEXT("Takeoff")&&!bAirborne)
 {
  LeapStart=U->GetActorLocation();FVector Destination=C.Target.IsValid()?C.Target->GetActorLocation():LeapStart;
  const FVector Direction=(Destination-LeapStart).GetSafeNormal2D();
  Destination-=Direction*(UEGCombatComponent::BlockingRadius(U)+(C.Target.IsValid()?UEGCombatComponent::BlockingRadius(C.Target.Get()):0)+4);
  if(!U->GetCombat()->ReserveBoardLanding(Destination,Landing))return;
  bAirborne=true;Skill->SetActionProtected(true);
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Takeoff"),LeapStart-FVector(0,0,88),U->GetActorRotation());
  if(!Direction.IsNearlyZero())U->SetActorRotation(Direction.Rotation());
 }
 else if(Event==TEXT("Release")&&bAirborne)
 {
  if(!U->GetCombat()->IsBoardLocationFree(Landing)&&!U->GetCombat()->ReserveBoardLanding(Landing,Landing))return;
  if(!Skill->CommitStigmataUltimate(C.ActionId))return;
  U->SetActorLocation(Landing,false,nullptr,ETeleportType::TeleportPhysics);bAirborne=false;U->GetCombat()->ReleaseBoardLanding(RoundId);
  if(IsValid(LandingRing))LandingRing->Destroy();LandingRing=nullptr;
  if(IsValid(LandingFX))LandingFX->Destroy();LandingFX=nullptr;
  if(D->ChessFX.Contains(TEXT("Landing")))AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Landing"),U->GetActorLocation()-FVector(0,0,88),U->GetActorRotation());
  else
  {
   AEGChessNiagaraBurst::Spawn(Skill.Get(),Battle.Get(),D->ChessLandingFX.Get(),U->GetActorLocation());
  }
  // Landing reserves four centimetres of collision clearance. Include that
  // clearance so an adjacent one-cell enemy is not outside a one-cell slam.
  const auto Targets=U->GetCombat()->FindEnemiesInRange(U->GetActorLocation(),EGSupport::Radius(U,D->UltimateRadius/500.f)+4.f+UE_KINDA_SMALL_NUMBER);
  for(auto* Target:Targets)
  {
   if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(U))break;
   if(!Battle->Damage(U,Target,C.Attack*C.Multiplier*U->GetUltimateBonus(),C.ActionId)||C.Tier<2||!IsCurrentBattle()||!IsValid(Target)||!Target->IsAlive_Implementation())continue;
   auto* ASC=Target->GetAbilitySystemComponent();auto Context=ASC->MakeEffectContext();Context.AddInstigator(U,U);
   auto Spec=ASC->MakeOutgoingSpec(UEGHuaArmorEffect::StaticClass(),1,Context);
   if(Spec.IsValid()){Spec.Data->SetDuration(D->ArmorSeconds,true);Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Support.Multiplier")),1.f-D->ArmorReduction);ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
   AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("ArmorBreak"),Target->GetActorLocation());
  }
  Skill->SetActionProtected(false);U->GetCombat()->ResolveBoardOverlap();
 }
}
void UEGHuaSkillBehavior::TickAction(float Position)
{
 if(!bAirborne||!IsCurrentBattle())return;
 const auto* D=Config<UEGHuaSkillDefinition>(Unit.Get());if(!D)return;
 const float Alpha=FMath::Clamp((Position-D->TakeoffTime)/FMath::Max(.01f,D->LandingTime-D->TakeoffTime),0.f,1.f);
 // The clip already lifts and lowers the body; ease only board-plane travel
 // so takeoff and landing do not start or stop at full horizontal speed.
 const float Travel=Alpha*Alpha*(3.f-2.f*Alpha);
 Unit->SetActorLocation(FMath::Lerp(LeapStart,Landing,Travel));
}
void UEGHuaSkillBehavior::ActionEnded(bool bCompleted)
{
 if(IsValid(LandingRing))LandingRing->Destroy();LandingRing=nullptr;
 if(IsValid(LandingFX))LandingFX->Destroy();LandingFX=nullptr;
 if(bAirborne&&Unit.IsValid())
 {
  Unit->GetCombat()->CompleteOrDeferBoardLanding(Landing,RoundId);
 }
 bAirborne=false;
}

bool UEGCollapsedHuaSkillBehavior::TryPreventDeath()
{
 const auto* D=Config<UEGCollapsedHuaSkillDefinition>(Unit.Get());
 return IsCurrentBattle()&&D&&Skill->BeginRevive(D->ReviveDelay,D->ReviveHealthRatio,D->ReviveAttackBonus);
}
void UEGCollapsedHuaSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 bBurned=false;bReleased=false;BurnedHealth=0;
 if(!C.bUltimate&&IsCurrentBattle())AEGChessNiagaraBurst::Swing(Skill.Get(),C.BasicAttackIndex);
 if(C.bUltimate&&IsCurrentBattle())
 {
  if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent(TEXT("VoiceUltimate"),Unit->GetActorLocation());
  Skill->SetActionProtected(true);
  const auto* D=Config<UEGCollapsedHuaSkillDefinition>(Unit.Get());
  ChargeFX=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Charge"),Unit->GetActorLocation()-FVector(0,0,88),Unit->GetActorRotation());
  if(D&&!D->ChessFX.Contains(TEXT("Charge")))BurnRing=AEGDescentRing::SpawnTelegraph(Unit->GetWorld(),Unit->GetActorLocation()-FVector(0,0,88),nullptr,D->UltimateRadius,1.7f);
  if(BurnRing)BurnRing->SetTint(FLinearColor(1,.22f,.025f));
 }
}
void UEGCollapsedHuaSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 auto* U=Unit.Get();const auto* D=Config<UEGCollapsedHuaSkillDefinition>(U);if(!D)return;
 if(!C.bUltimate)
 {
  if(Basic(U,Battle.Get(),Event,C)&&C.Target.IsValid())
  {
   AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Strike"),C.Target->GetActorLocation(),U->GetActorRotation());
   if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(TEXT("Hit"),C.ActionId,C.Target.Get());
  }
  return;
 }
 if(Event==TEXT("Burn")&&!bBurned)
 {
  bBurned=true;const float Before=U->GetAttributes()->GetHealth();
  const float After=FMath::Max(1.f,Before*(1-FMath::Clamp(D->BurnCurrentHealthRatio,0.f,1.f)));
  BurnedHealth=FMath::Max(0.f,Before-After);
  if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent(TEXT("Burn"),U->GetActorLocation());
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Burn"),U->GetActorLocation()-FVector(0,0,88),U->GetActorRotation());
  U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),After);
 }
 else if(Event==TEXT("Release")&&bBurned&&!bReleased)
 {
  if(!Skill->CommitStigmataUltimate(C.ActionId))return;
  bReleased=true;
  if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent(TEXT("UltimateImpact"),U->GetActorLocation());
  if(BurnRing){BurnRing->Destroy();BurnRing=nullptr;}
  if(IsValid(ChargeFX))ChargeFX->Destroy();ChargeFX=nullptr;
  if(D->ChessFX.Contains(TEXT("Landing")))AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Landing"),U->GetActorLocation()-FVector(0,0,88),U->GetActorRotation());
  else if(auto* Impact=AEGDescentRing::SpawnShockwave(U->GetWorld(),U->GetActorLocation()-FVector(0,0,88),nullptr,D->UltimateRadius,.35f))Impact->SetTint(FLinearColor(1,.22f,.025f));
  const float Radius=EGSupport::Radius(U,D->UltimateRadius/500.f);
  // Snapshot recipients at Release; shield uses the actual paid health, not max health.
  if(C.Tier>=2)for(auto* Ally:Battle->Units())
  {
   if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(U))break;
   if(!EGSupport::Ally(U,Ally)||FVector::DistSquared2D(U->GetActorLocation(),Ally->GetActorLocation())>FMath::Square(Radius))continue;
   Ally->GetHeroSkill()->GrantSourceShield(TEXT("CollapsedHua.BurnShield"),BurnedHealth*D->BurnShieldRatio,D->BurnShieldSeconds,U,true);
   AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("BurnShield"),Ally->GetActorLocation()-FVector(0,0,65));
  }
  const auto Targets=U->GetCombat()->FindEnemiesInRange(U->GetActorLocation(),Radius);
  for(auto* T:Targets)
  {
   if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(U))break;
   Battle->Damage(U,T,(C.Attack*C.Multiplier+BurnedHealth)*U->GetUltimateBonus(),C.ActionId);
  }
 }
}
void UEGCollapsedHuaSkillBehavior::ActionEnded(bool bCompleted)
{
 if(BurnRing){BurnRing->Destroy();BurnRing=nullptr;}
 if(IsValid(ChargeFX))ChargeFX->Destroy();ChargeFX=nullptr;
 if(bCompleted&&bReleased&&IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(Unit.Get()))
 {
  auto* U=Unit.Get();const auto* D=Config<UEGCollapsedHuaSkillDefinition>(U);
  if(D){Battle->Heal(U,FMath::Max(0.f,U->GetAttributes()->GetMaxHealth()*FMath::Clamp(D->RecoveryHealthRatio,0.f,1.f)-U->GetAttributes()->GetHealth()));AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Recovery"),U->GetActorLocation()-FVector(0,0,88),U->GetActorRotation());}
 }
 bBurned=false;bReleased=false;BurnedHealth=0;
}

UEGMobiusSkillBehavior::UEGMobiusSkillBehavior()
{
 // The individual combat accents are retained by the native CDO for cooking.
 // The workshop comparison asset also contains static preview targets and is
 // deliberately not used as a runtime actor.
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Basic1(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic01_Accent_V2.DA_Mobius_Basic01_Accent_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Basic2(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic02_Accent_V2.DA_Mobius_Basic02_Accent_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Basic3(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic03_Accent_V2.DA_Mobius_Basic03_Accent_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Basic4(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic04_Accent_V2.DA_Mobius_Basic04_Accent_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Spike1(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic01_Source_V2.DA_Mobius_Basic01_Source_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Spike2(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic02_Triple_Source_V2.DA_Mobius_Basic02_Triple_Source_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Spike2Extra(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic02_Extra_Source_V2.DA_Mobius_Basic02_Extra_Source_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Spike4(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_Basic04_Source_V2.DA_Mobius_Basic04_Source_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Impact(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_UltimateImpact_Accent_V2.DA_Mobius_UltimateImpact_Accent_V2"));
 static ConstructorHelpers::FObjectFinder<UEGSourceEffectDefinition> Empowered(TEXT("/Game/Assets/Characters/Mobius/FXs/Workshop20261102/Effects/DA_Mobius_EmpoweredImpact_Accent_V2.DA_Mobius_EmpoweredImpact_Accent_V2"));
 WorkshopBasicAccents={Basic1.Object,Basic2.Object,Basic3.Object,Basic4.Object};
 WorkshopSpikes={Spike1.Object,Spike2.Object,Spike2Extra.Object,Spike4.Object};
 WorkshopUltimateImpact=Impact.Object;WorkshopEmpoweredImpact=Empowered.Object;
}
void UEGMobiusSkillBehavior::PresentWorkshopEffect(UEGSourceEffectDefinition* Definition,const FVector& Point,const FRotator& Rotation,float Scale)
{
 auto* U=Unit.Get();auto* S=Skill.Get();auto* Mesh=U?U->GetVisiblePresentationMesh():nullptr;
 if(!IsCurrentBattle()||!IsValid(U)||!IsValid(S)||!IsValid(Mesh)||!IsValid(Definition)||!WorkshopAction.IsValid()
    ||S->ActionId!=WorkshopAction||!S->IsActionRunning()||!IEGCombatant::Execute_IsAlive(U)
    ||Point.ContainsNaN()||Rotation.ContainsNaN()||!FMath::IsFinite(Scale)||Scale<=0.f)return;
 // A short confirmed tail may survive a normal action finish. Cancellation and
 // round end explicitly destroy it; the actor also checks owner death/round.
 auto* Effect=AEGSourceEffect::Spawn(S,Mesh,Definition,MobiusSourcePlaybackRate(S),true);
 if(!Effect)return;
 Effect->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
 Effect->SetActorTransform(FTransform(Rotation,Point,FVector(Definition->PresentationScale*Scale)));
 WorkshopEffects.RemoveAll([](const auto& Item){return !Item.IsValid();});WorkshopEffects.Add(Effect);
}
void UEGMobiusSkillBehavior::ClearWorkshopEffects()
{
 for(const auto& Effect:WorkshopEffects)if(Effect.IsValid())Effect->Destroy();
 WorkshopEffects.Reset();
}
bool UEGMobiusSkillBehavior::LinkActive() const
{return IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&LinkRemaining>0.f;}
void UEGMobiusSkillBehavior::PresentNiagara(UNiagaraSystem* System,const FVector& Location,const FRotator& Rotation,float Duration,bool bActionBound,AEGChessUnit* Target)
{
 NiagaraEffects.RemoveAll([](const auto& Effect){return !Effect.IsValid();});
 if(auto* Effect=AEGMobiusNiagaraEffect::Spawn(Skill.Get(),System,Location,Rotation,Duration,bActionBound,Target))NiagaraEffects.Add(Effect);
}
void UEGMobiusSkillBehavior::SetLink(const TArray<AEGChessUnit*>& Targets,bool Group)
{
 if(IsValid(LinkVisual))LinkVisual->Destroy();LinkVisual=nullptr;
 Members.Reset();bGroup=Group;
 for(auto* T:Targets)if(Unit->GetCombat()->IsValidEnemy(T))Members.AddUnique(T);
 const auto* D=Config<UEGMobiusSkillDefinition>(Unit.Get());
 LinkRemaining=(D?FMath::Max(0.f,D->LinkDuration):0.f);
 if(Members.Num()>1&&D)LinkVisual=AEGMobiusLinkVisual::Spawn(Unit.Get(),Targets,D->LinkDuration,true);
}
void UEGMobiusSkillBehavior::OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId)
{
 if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(Unit.Get())||!Unit->GetCombat()->IsValidEnemy(Target)||(LinkActive()&&bGroup))return;
 if(LinkActive()&&Members.Contains(Target))
 {
  // Rebuild when the partner has died; otherwise refresh the existing pair.
  int32 Alive=0;for(const auto& Member:Members)if(Member.IsValid()&&Unit->GetCombat()->IsValidEnemy(Member.Get()))++Alive;
  if(Alive==2){if(const auto* D=Config<UEGMobiusSkillDefinition>(Unit.Get())){LinkRemaining=D->LinkDuration;if(IsValid(LinkVisual))LinkVisual->Refresh(D->LinkDuration);}return;}
 }
 AEGChessUnit* Nearest=nullptr;double Distance=TNumericLimits<double>::Max();
 for(auto* Other:Battle->Units())if(Other!=Target&&Unit->GetCombat()->IsValidEnemy(Other))
 {
  const double Candidate=FVector::DistSquared2D(Target->GetActorLocation(),Other->GetActorLocation());
  if(Candidate<Distance){Distance=Candidate;Nearest=Other;}
 }
 TArray<AEGChessUnit*> Pair{Target};if(Nearest)Pair.Add(Nearest);SetLink(Pair,false);
}

void UEGMobiusSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle())return;
 auto* U=Unit.Get();const auto* D=Config<UEGMobiusSkillDefinition>(U);const auto* P=U->GetDefinition()->PresentationDefinition.Get();if(!D||!P)return;
 WorkshopAction=C.ActionId;bWorkshopAuraPresented=false;
 bSerpentCast=C.bUltimate;bStormAttack=!C.bUltimate&&EmpoweredActive;
 if(C.bUltimate){bReleased=false;bRecoveryEffectPresented=false;}
 if(!C.bUltimate)
 {
  const int32 I=C.BasicAttackIndex;
  // Indices correspond to the formal cue array, not the preview's mannequin.
  // Missing or changed inputs keep the existing source presentation per cue.
  TArray<UEGSourceEffectDefinition*> Spikes;
  if(I==0&&D->BasicSourceStages.IsValidIndex(I)&&D->BasicSourceStages[I].Cues.Num()==2)
  {Spikes.SetNumZeroed(2);Spikes[0]=WorkshopSpikes.IsValidIndex(0)?WorkshopSpikes[0].Get():nullptr;}
  else if(I==1&&D->BasicSourceStages.IsValidIndex(I)&&D->BasicSourceStages[I].Cues.Num()==7)
  {
   Spikes.SetNumZeroed(7);
   if(WorkshopSpikes.IsValidIndex(2)){Spikes[0]=Spikes[2]=Spikes[4]=WorkshopSpikes[1].Get();Spikes[6]=WorkshopSpikes[2].Get();}
  }
  else if(I==3&&D->BasicSourceStages.IsValidIndex(I)&&D->BasicSourceStages[I].Cues.Num()==1)
  {Spikes.SetNumZeroed(1);Spikes[0]=WorkshopSpikes.IsValidIndex(3)?WorkshopSpikes[3].Get():nullptr;}
  // Source basics are opt-in independently of the ultimate and link modes.
  // A missing or unusable stage falls back to the existing Niagara attack.
  if(D->bUseNiagaraPresentation&&D->bUseSourceBasicPresentation&&!bStormAttack&&D->BasicSourceStages.IsValidIndex(I))
  {
   BasicVisual=AEGMobiusSourceAttackVisual::SpawnBasic(Skill.Get(),C.Target.Get(),D->BasicSourceStages[I],I,&Spikes);
   if(IsValid(BasicVisual))return;
  }
  if(D->bUseNiagaraPresentation)
  {
   if(C.Target.IsValid()&&U->GetNetMode()!=NM_DedicatedServer)
   {
    const FVector Direction=(C.Target->GetActorLocation()-U->GetActorLocation()).GetSafeNormal2D();
    const FVector Position=C.Target->GetActorLocation()-Direction*C.Target->GetDefinition()->GetPresentationPickRadius()
      +FVector(0,0,-88+C.Target->GetDefinition()->GetPresentationHeight()*.4f);
    PresentNiagara(D->NiagaraBasicStrike.Get(),Position,Direction.Rotation(),1.1f,true,C.Target.Get());
   }
   return;
  }
  if(D->BasicSourceStages.IsValidIndex(I))
   BasicVisual=AEGMobiusSourceAttackVisual::SpawnBasic(Skill.Get(),C.Target.Get(),D->BasicSourceStages[I],I,&Spikes);
  return;
 }
 Skill->SetActionProtected(true);
 CastPoint=C.Target.IsValid()?C.Target->GetActorLocation():U->GetActorLocation();CastPoint.Z=U->GetActorLocation().Z;
 if(D->bUseNiagaraPresentation)CastWarning=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Telegraph"),CastPoint-FVector(0,0,88));
 if(U->GetNetMode()==NM_DedicatedServer)return;
 // Mobius uses the extracted source atmosphere and storm layers. The generic
 // procedural telegraph is not part of her source effect; target locking and
 // gameplay radius remain independent of the cosmetic presentation.
 // One incarnation and health pool. The native snake belongs to the caster,
 // while the warning and lightning belong to the selected enemy ground point.
 auto SpawnSelf=[&](UEGSourcePreviewDefinition* Definition,const FVector& Scale)
 {
  if(!Definition)return static_cast<AEGCombatVisual*>(nullptr);
  auto* V=U->GetWorld()->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,U,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!V)return V;V->Definition=Definition;V->bAutoPlay=false;V->bLoop=false;V->FinishSpawning(FTransform::Identity);V->SetActorTickEnabled(false);V->AttachToComponent(U->GetRootComponent(),FAttachmentTransformRules::KeepRelativeTransform);V->SetActorRelativeTransform(FTransform(P->PresentationRotation,FVector(0,0,-88+P->PresentationGroundOffset),Scale));V->SetSourceTime(0);return V;
 };
 SnakeVisual=SpawnSelf(D->SerpentBody.IsNull()?P->SkillTargetAssembly.LoadSynchronous():D->SerpentBody.LoadSynchronous(),P->MeshScale);
 if(D->bUseNiagaraPresentation)
 {
  // Flatten the complete local-space pack after emitter rotation, rather
  // than flattening individual meshes before their rotating orientation.
  NiagaraEffects.RemoveAll([](const auto& V){return !V.IsValid();});
  if(auto* Gather=AEGMobiusNiagaraEffect::Spawn(Skill.Get(),D->NiagaraCast.LoadSynchronous(),U->GetActorLocation()-FVector(0,0,84),U->GetActorRotation(),1.25f,true))
  {Gather->SetActorScale3D(FVector(1.f,1.f,.08f));NiagaraEffects.Add(Gather);}
 }
 if(!D->bUseNiagaraPresentation||D->bUseSourceUltimateOverlay)
  UltimateSourceVisual=SpawnSelf(D->UltimateSourceAssembly.LoadSynchronous(),P->MeshScale);
}
void UEGMobiusSkillBehavior::TickAction(float Position)
{
 if(!IsCurrentBattle())return;
 if(IsValid(UltimateSourceVisual))UltimateSourceVisual->SetSourceTime(Position);
 if(bSerpentCast&&!bRecoveryEffectPresented&&Unit->GetNetMode()!=NM_DedicatedServer)
 {
  const auto* D=Config<UEGMobiusSkillDefinition>(Unit.Get());
  if(D&&(D->bUseNiagaraPresentation?!D->NiagaraRecovery.IsNull():!D->UltimateRecoveryEffect.IsNull())&&FMath::IsFinite(D->UltimateRecoveryStartTime)
   &&D->UltimateRecoveryStartTime>=0.f&&Position>=D->UltimateRecoveryStartTime)
  {
   bRecoveryEffectPresented=true;
   if(D->bUseNiagaraPresentation)
   {
    PresentNiagara(D->NiagaraRecovery.Get(),Unit->GetActorLocation()+FVector(0,0,80),Unit->GetActorRotation(),1.f);
   }
   else
   {
   // One source-only lifetime. It keeps the original curves after the cast
   // ends, using the existing time-stop/death/round-bound presentation owner.
   BasicImpacts.RemoveAll([](const auto& V){return !V.IsValid();});
   if(auto* V=AEGMobiusSourceAttackVisual::SpawnImpact(Skill.Get(),Unit.Get(),D->UltimateRecoveryEffect.LoadSynchronous(),Unit->GetDefinition()->PresentationDefinition->MeshScale.X,MobiusSourcePlaybackRate(Skill.Get()),true))
    BasicImpacts.Add(V);
   }
  }
 }

 // The original five-second snake clip plays forward exactly once, on the
 // same source clock as the body. No repeated poses or attack-driven seeks.
 if(IsValid(SnakeVisual))
 {
  const auto* Definition=SnakeVisual->Definition.Get();
  const float Length=Definition&&Definition->Motions.IsValidIndex(SnakeVisual->MotionIndex)
   ?Definition->Motions[SnakeVisual->MotionIndex].Duration:0.f;
  // The one-shot source clip has no sustained pose. Retire it at its own
  // endpoint instead of clamping its last frame through human recovery.
  if(Length<=0.f||Position>=Length){SnakeVisual->Destroy();SnakeVisual=nullptr;}
  else SnakeVisual->SetSourceTime(Position);
 }
}

void UEGMobiusSkillBehavior::PresentDischarge(const TArray<AEGChessUnit*>& Targets,float Duration)
{
 Discharges.RemoveAll([](const auto& V){return !V.IsValid();});if(auto* V=AEGMobiusLinkVisual::Spawn(Unit.Get(),Targets,Duration,false))Discharges.Add(V);
}
void UEGMobiusSkillBehavior::PresentStorm(AEGChessUnit* Target,FVector Point)
{
 const auto* D=Config<UEGMobiusSkillDefinition>(Unit.Get());if(!D||!Target)return;
 if(Unit->GetNetMode()==NM_DedicatedServer){++StormCount;return;}
 if(D->bUseNiagaraPresentation)
 {
  // Resolve the optional ultimate-only visual even if asynchronous preloading
  // has not finished; empowered attacks retain their existing impact system.
  UNiagaraSystem* System=nullptr;
  if(bSerpentCast&&!D->NiagaraUltimateStorm.IsNull())System=D->NiagaraUltimateStorm.LoadSynchronous();
  if(!System)System=D->NiagaraStormImpact.Get();
  PresentNiagara(System,Point-FVector(0,0,88),Unit->GetActorRotation(),1.f);
  // The original ground burst is a cosmetic companion to Niagara's random
  // strikes. It uses the same locked release point and never submits damage.
  if(bSerpentCast&&D->bUseSourceUltimateOverlay&&!D->StormImpact.IsNull())
  {
   BasicImpacts.RemoveAll([](const auto& V){return !V.IsValid();});
   const auto* Presentation=Unit->GetDefinition()?Unit->GetDefinition()->PresentationDefinition.Get():nullptr;
   if(Presentation)
    if(auto* Burst=AEGMobiusSourceAttackVisual::SpawnImpact(Skill.Get(),Target,D->StormImpact.LoadSynchronous(),Presentation->MeshScale.X,MobiusSourcePlaybackRate(Skill.Get()),true))
    {
     Burst->EndPoint=Point-FVector(0,0,88);Burst->SetActorLocation(Burst->EndPoint);
     BasicImpacts.Add(Burst);
    }
  }
  FVector Origin=Unit->GetActorLocation()+FVector(0,0,650);
  if(IsValid(SnakeVisual))SnakeVisual->GetVisiblePartBoneLocation(TEXT("Mobius_SKL02_Snake"),TEXT("ThunderChainShoot"),Origin);
  if(Target!=Unit.Get())
  {
   Discharges.RemoveAll([](const auto& Beam){return !Beam.IsValid();});
   const TArray<AEGChessUnit*> Targets{Target};
   if(auto* Beam=AEGMobiusLinkVisual::Spawn(Unit.Get(),Targets,.3f/MobiusSourcePlaybackRate(Skill.Get()),false,&Origin))Discharges.Add(Beam);
  }
  ++StormCount;return;
 }
 BasicImpacts.RemoveAll([](const auto& V){return !V.IsValid();});
 const float VisualScale=Unit->GetDefinition()->PresentationDefinition->MeshScale.X
  *(bStormAttack?FMath::Clamp(D->EmpoweredStormVisualScale,.001f,1.f):1.f);
 if(auto* V=AEGMobiusSourceAttackVisual::SpawnImpact(Skill.Get(),Target,D->StormImpact.LoadSynchronous(),VisualScale,MobiusSourcePlaybackRate(Skill.Get()),true))
 {
  V->EndPoint=Point-FVector(0,0,88);V->SetActorLocation(V->EndPoint);FVector Origin=Unit->GetActorLocation()+FVector(0,0,650);
  if(IsValid(SnakeVisual))SnakeVisual->GetVisiblePartBoneLocation(TEXT("Mobius_SKL02_Snake"),TEXT("ThunderChainShoot"),Origin);
  if(!D->DischargeSourceEffect.IsNull())
  {
   Discharges.RemoveAll([](const auto& Beam){return !Beam.IsValid();});
   const TArray<AEGChessUnit*> Targets{Target};
   if(auto* Beam=AEGMobiusLinkVisual::Spawn(Unit.Get(),Targets,.3f/MobiusSourcePlaybackRate(Skill.Get()),false,&Origin))Discharges.Add(Beam);
  }
  BasicImpacts.Add(V);
 }
 ++StormCount;
}
void UEGMobiusSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 auto* U=Unit.Get();const auto* D=Config<UEGMobiusSkillDefinition>(U);if(!D)return;
 if(!C.bUltimate)
 {
  auto* T=C.Target.Get();if(Event!=TEXT("Hit")||!U->GetCombat()->IsValidEnemy(T)||!U->GetCombat()->IsInAttackRange(T))return;
  bool Hit=false;
  const auto* TargetDefinition=T->GetDefinition();
  const FVector ConfirmedImpact=T->GetActorLocation()+FVector(0,0,-88+(TargetDefinition?TargetDefinition->GetPresentationHeight():120.f)*.4f);
  if(bStormAttack)
  {
   HeadProjections.RemoveAll([](const auto& V){return !V.IsValid();});
   if(auto* Head=AEGMobiusHeadProjection::Spawn(U,T))HeadProjections.Add(Head);
   PresentStorm(T,T->GetActorLocation());const auto Targets=U->GetCombat()->FindEnemiesInRange(T->GetActorLocation(),EGSupport::Radius(U,D->EmpoweredRadius/500.f));
   for(auto* Enemy:Targets){if(!IsCurrentBattle())break;Hit=Battle->Damage(U,Enemy,C.Attack*D->EmpoweredMultiplier*U->GetAttributes()->GetSkillMultiplier(),C.ActionId,Enemy==T)||Hit;}
  }
  else if(Battle->Damage(U,T,C.Attack,C.ActionId,true))
  {
   Hit=true;
   if(D->bUseNiagaraPresentation&&!IsValid(BasicVisual)&&IsCurrentBattle()&&Unit.IsValid())PresentNiagara(D->NiagaraBasicImpact.Get(),ConfirmedImpact,Unit->GetActorRotation(),.7f);
   if(IsValid(BasicVisual))BasicVisual->ConfirmHit();
  }
  if(Hit&&IsCurrentBattle())
  {
   if(C.ActionId==WorkshopAction&&Skill.IsValid()&&Skill->ActionId==C.ActionId)
    PresentWorkshopEffect(bStormAttack?WorkshopEmpoweredImpact.Get():(WorkshopBasicAccents.IsValidIndex(C.BasicAttackIndex)?WorkshopBasicAccents[C.BasicAttackIndex].Get():nullptr),bStormAttack?FVector(ConfirmedImpact.X,ConfirmedImpact.Y,ConfirmedImpact.Z+100.f-(TargetDefinition?TargetDefinition->GetPresentationHeight():120.f)*.4f):ConfirmedImpact,U->GetActorRotation(),.5f);
   if(IsValid(BasicVisual))BasicVisual->ConfirmHit();
   if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(TEXT("Hit"),C.ActionId,T);Battle->BasicHit(U,T,C.ActionId);}
  return;
 }
 if(Event!=TEXT("Release"))return;
 bReleased=true;
 if(!Skill->CommitStigmataUltimate(C.ActionId))return;
 PresentWorkshopEffect(WorkshopUltimateImpact,CastPoint-FVector(0,0,88),U->GetActorRotation(),.7f);
 if(IsValid(CastWarning))CastWarning->Destroy();CastWarning=nullptr;
 const auto Targets=U->GetCombat()->FindEnemiesInRange(CastPoint,EGSupport::Radius(U,D->UltimateRadius/500.f));if(IsValid(CastRing))CastRing->Destroy();CastRing=nullptr;
 // The initial burst keeps its existing tier multiplier and locked warning.
 Members.Reset();LinkRemaining=0;TArray<AEGChessUnit*> Hit;
 PresentStorm(C.Target.IsValid()?C.Target.Get():U,CastPoint);
 for(auto* T:Targets)if(Battle->Damage(U,T,C.Attack*C.Multiplier*U->GetUltimateBonus(),C.ActionId)&&U->GetCombat()->IsValidEnemy(T))Hit.Add(T);
 SetLink(Hit,true);
 if(C.Tier>=2&&IsCurrentBattle()){EmpoweredActive=true;EmpoweredRemaining=D->EmpoweredDuration;}
}
void UEGMobiusSkillBehavior::ActionEnded(bool bCompleted)
{
 if(!bCompleted)ClearWorkshopEffects();WorkshopAction.Invalidate();bWorkshopAuraPresented=false;
 if(IsValid(CastWarning))CastWarning->Destroy();CastWarning=nullptr;
 if(IsValid(BasicVisual))
 {
  if(bCompleted&&BasicVisual->ReleaseNaturalTail())
  {BasicImpacts.RemoveAll([](const auto& V){return !V.IsValid();});BasicImpacts.Add(BasicVisual);}
  else BasicVisual->Destroy();
 }
 BasicVisual=nullptr;
 if(IsValid(UltimateSourceVisual))UltimateSourceVisual->Destroy();UltimateSourceVisual=nullptr;
 if(IsValid(CastRing))CastRing->Destroy();CastRing=nullptr;
 // Finishing or cancelling after Release restores the same human entity.
 // Death/round cleanup can never award or retain enhancement.
 if(bSerpentCast)
 {
  if(IsValid(SnakeVisual))SnakeVisual->Destroy();SnakeVisual=nullptr;
  // Enhancement starts at Release and must not restart when the montage ends.
 }
 if(!bCompleted){for(const auto& V:BasicImpacts)if(V.IsValid())V->Destroy();BasicImpacts.Reset();}
 if(!bCompleted){for(const auto& V:NiagaraEffects)if(V.IsValid())V->Destroy();NiagaraEffects.Reset();}
 bSerpentCast=false;bStormAttack=false;bReleased=false;bRecoveryEffectPresented=false;
}
void UEGMobiusSkillBehavior::StopSerpent()
{
 EmpoweredActive=false;EmpoweredRemaining=0;SerpentActive=false;SerpentRemaining=0;bReleased=false;
 if(IsValid(SnakeVisual))SnakeVisual->Destroy();SnakeVisual=nullptr;
 if(IsValid(RecoveryVisual))RecoveryVisual->Destroy();RecoveryVisual=nullptr;
 if(Skill.IsValid())Skill->SetActionProtected(false);
 if(Unit.IsValid()&&IEGCombatant::Execute_IsAlive(Unit.Get()))Unit->GetVisiblePresentationMesh()->SetVisibility(true);
}
void UEGMobiusSkillBehavior::TickBattle(float DeltaSeconds)
{
 if(!IsCurrentBattle())return;
 if(!IEGCombatant::Execute_IsAlive(Unit.Get())){StopSerpent();return;}
 LinkRemaining=FMath::Max(0.f,LinkRemaining-DeltaSeconds);
 if(LinkRemaining<=0.f&&IsValid(LinkVisual)){LinkVisual->Destroy();LinkVisual=nullptr;}
 if(!EmpoweredActive)return;
 EmpoweredRemaining=FMath::Max(0.f,EmpoweredRemaining-DeltaSeconds);
 // bStormAttack is snapshotted at action start; expiry cannot change a hit
 // already in flight. The next basic attack returns to the ordinary rules.
 if(EmpoweredRemaining<=0.f)EmpoweredActive=false;
}
void UEGMobiusSkillBehavior::DamageResolved(const FEGCombatResolution& R)
{
 if(R.bHealing||R.bLinkedDamage||R.bExecution||R.HealthDelta>=0||!LinkActive())return;
 auto* U=Unit.Get();const auto* D=Config<UEGMobiusSkillDefinition>(U);if(!D)return;
 bool InGroup=false;for(const auto& Member:Members)if(Member.IsValid()&&Member->GetInstanceId()==R.TargetId){InGroup=true;break;}
 if(!InGroup)return;
 bool Allied=false;for(auto* Source:Battle->Units())if(Source->GetInstanceId()==R.SourceId&&IEGCombatant::Execute_GetFaction(Source)==IEGCombatant::Execute_GetFaction(U)){Allied=true;break;}
 if(!Allied)return;
 const auto Snapshot=Members;
 AEGChessUnit* Origin=nullptr;
 for(const auto& Member:Snapshot)if(Member.IsValid()&&Member->GetInstanceId()==R.TargetId){Origin=Member.Get();break;}
 Discharges.RemoveAll([](const auto& Visual){return !Visual.IsValid();});
 for(const auto& Member:Snapshot)if(IsCurrentBattle()&&Member.IsValid()&&Member->GetInstanceId()!=R.TargetId&&U->GetCombat()->IsValidEnemy(Member.Get()))
 {
  if(FEGCombatExecution::Damage(U,Member.Get(),-R.HealthDelta*FMath::Clamp(D->LinkDamageRatio,0.f,1.f),0,1,FGameplayEffectContextHandle(),R.ActionId,true))
   if(auto* Pulse=AEGMobiusLinkVisual::SpawnPulse(U,Origin,Member.Get()))Discharges.Add(Pulse);
 }
}
bool UEGMobiusSkillBehavior::IsLinkedTarget(const AEGChessUnit* Target) const
{
 if(!IsValid(Target)||!LinkActive())return false;
 for(const auto& Member:Members)if(Member.Get()==Target)return true;
 return false;
}
AEGChessUnit* UEGMobiusSkillBehavior::LowestLinkedTarget() const
{
 if(!LinkActive()||!Unit.IsValid())return nullptr;
 AEGChessUnit* Lowest=nullptr;float LowestRatio=TNumericLimits<float>::Max();
 for(const auto& Member:Members)if(auto* Target=Member.Get();IsValid(Target)&&Unit->GetCombat()->IsValidEnemy(Target))
 {
  const auto* Stats=Target->GetAttributes();const float Ratio=Stats->GetHealth()/FMath::Max(1.f,Stats->GetMaxHealth());
  if(Ratio<LowestRatio){Lowest=Target;LowestRatio=Ratio;}
 }
 return Lowest;
}
void UEGMobiusSkillBehavior::EndBattle()
{
 for(const auto& Head:HeadProjections)if(Head.IsValid())Head->Destroy();HeadProjections.Reset();
 ClearWorkshopEffects();
 StopSerpent();ActionEnded(false);if(IsValid(LinkVisual))LinkVisual->Destroy();LinkVisual=nullptr;
 for(const auto& Visual:BasicImpacts)if(Visual.IsValid())Visual->Destroy();BasicImpacts.Reset();
 for(const auto& Visual:Discharges)if(Visual.IsValid())Visual->Destroy();Discharges.Reset();
 for(const auto& Visual:NiagaraEffects)if(Visual.IsValid())Visual->Destroy();NiagaraEffects.Reset();
 Members.Reset();LinkRemaining=0;bGroup=false;Super::EndBattle();
}
