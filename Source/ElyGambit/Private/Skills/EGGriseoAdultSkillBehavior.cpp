#include "Skills/EGGriseoAdultSkillBehavior.h"
#include "Skills/EGSupportSkillUtils.h"
#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "AbilitySystemComponent.h"
#include "EGGameplayTags.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Data/EGGriseoAdultSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGSkillNotify.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
void UEGGriseoAdultSkillBehavior::Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle)
{
 Super::Initialize(InSkill,InBattle);
 if(!IsCurrentBattle()||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const auto* D=Cast<UEGGriseoAdultSkillDefinition>(Unit->GetDefinition()->SkillDefinition);
 if(!D||!D->HasValidSkillRules())return;
 if(auto* Knight=Battle->SpawnKnight(Unit.Get(),D->KnightHealthRatio,D->KnightAttackRatio))
  Battle->RegisterAdultKnightAura(Knight,D->PassiveSpeedBonus);
}
void UEGGriseoAdultSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 ActiveAction=C.ActionId;bSubmitted=false;MeteorReleaseTime=0.f;
 if(!IsCurrentBattle())return;
 if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())
 {
  Audio->ActionEvent(C.bUltimate?TEXT("UltimateStart"):TEXT("AttackStart"),Unit->GetActorLocation());
  if(C.bUltimate)Audio->ActionEvent(TEXT("VoiceUltimate"),Unit->GetActorLocation());
 }
 if(!C.bUltimate){AEGChessNiagaraBurst::Swing(Skill.Get(),C.BasicAttackIndex);return;}
 UltimatePoint=Unit->GetActorLocation();
 if(Unit->GetCombat()->IsValidEnemy(C.Target.Get()))UltimatePoint=C.Target->GetActorLocation();
 AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("StarGather"),UltimatePoint-FVector(0,0,80));
 const UAnimSequenceBase* Clip=Skill->GetActiveMontage();if(!Clip)Clip=Skill->GetActiveAnimation();
 if(Clip)for(const auto& N:Clip->Notifies)
  if(const auto* Event=Cast<UEGSkillNotify>(N.Notify);Event&&Event->Event==TEXT("Release")&&N.GetTriggerTime()>0.f)
   if(MeteorReleaseTime<=0.f||N.GetTriggerTime()<MeteorReleaseTime)MeteorReleaseTime=N.GetTriggerTime();
 Skill->SetActionProtected(true);
}
void UEGGriseoAdultSkillBehavior::TickAction(float Position)
{
 if(!IsCurrentBattle()||bSubmitted||MeteorReleaseTime<=0.f||Skill->ActionId!=ActiveAction)return;
 const float Flight=FMath::Min(.7f,MeteorReleaseTime),Start=MeteorReleaseTime-Flight;
 if(Position<Start||Position>=MeteorReleaseTime)return;
 const FVector Top=UltimatePoint+FVector(120,0,900),Bottom=UltimatePoint+FVector(0,0,20);
 const FRotator Rotation=(Bottom-Top).Rotation();
 if(!MeteorVisual.IsValid())MeteorVisual=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("MeteorTrail"),Top,Rotation);
 if(MeteorVisual.IsValid())MeteorVisual->SetActorLocationAndRotation(FMath::Lerp(Top,Bottom,FMath::Clamp((Position-Start)/Flight,0.f,1.f)),Rotation);
}
void UEGGriseoAdultSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId||!C.ActionId.IsValid()||C.ActionId!=ActiveAction||bSubmitted||!Unit->HasAuthority()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 auto* U=Unit.Get();auto* B=Battle.Get();
 if(!C.bUltimate)
 {
  auto* Target=C.Target.Get();
  if(Event!=TEXT("Hit")||!U->GetCombat()->IsValidEnemy(Target)||!U->GetCombat()->IsInAttackRange(Target))return;
  bSubmitted=true;
  if(B->Damage(U,Target,C.Attack,C.ActionId,true))
  {
   AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("StarStrike"),Target->GetActorLocation(),U->GetActorRotation());
   if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(Event,C.ActionId,Target);
   if(IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(U))B->BasicHit(U,Target,C.ActionId);
  }
  return;
 }
 if(Event!=TEXT("Release"))return;
 const auto* D=Cast<UEGGriseoAdultSkillDefinition>(U->GetDefinition()->SkillDefinition);if(!D)return;
 bSubmitted=true;
 if(!Skill->CommitStigmataUltimate(C.ActionId))return;
 if(MeteorVisual.IsValid())MeteorVisual->Destroy();MeteorVisual.Reset();MeteorReleaseTime=0.f;
 AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("MeteorBurst"),UltimatePoint-FVector(0,0,80));
 if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionEvent(TEXT("MeteorImpact"),UltimatePoint);
 if(auto* Impact=D->MeteorImpact.LoadSynchronous())Skill->PresentSourceEffect(Impact,U->GetVisiblePresentationMesh());
 // Pure cosmetics share the locked action point. Damage remains exclusively
 // in the existing BattleContext/GAS authority chain.
 const auto Targets=B->Units();
 for(auto* Target:Targets)
 {
  if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(U))break;
  if(U->GetCombat()->IsValidEnemy(Target)&&FVector::DistSquared2D(UltimatePoint,Target->GetActorLocation())<=FMath::Square(EGSupport::Radius(U,D->UltimateRadius/500.f)))
  {
   const bool Hit=B->Damage(U,Target,C.Attack*C.Multiplier*U->GetUltimateBonus(),C.ActionId);
   if(Hit&&C.Tier>=2&&IsCurrentBattle()&&IsValid(Target)&&Target->IsAlive_Implementation()&&!Target->FindComponentByClass<UEGCorruptionCombatComponent>()&&Target->GetHeroSkill()->CanReceiveHostileControl())
   {
    auto* ASC=Target->GetAbilitySystemComponent();auto Context=ASC->MakeEffectContext();Context.AddInstigator(U,U);
    auto Spec=ASC->MakeOutgoingSpec(UEGMeteorStunEffect::StaticClass(),1,Context);
    if(Spec.IsValid())
    {
     Spec.Data->SetDuration(D->MeteorStunSeconds,true);Spec.Data->AddDynamicAssetTag(EGTags::ControlStun);
     if(ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data).IsValid())
     {Target->GetHeroSkill()->CancelAction();ASC->CancelAllAbilities();AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("MeteorStun"),Target->GetActorLocation());}
    }
   }
  }
 }
 if(IsCurrentBattle()&&IEGCombatant::Execute_IsAlive(U))
 {
  B->ApplyTeamSpeed(U,(C.Tier>=1?D->AdvancedTeamSpeedBonus:D->TeamSpeedBonus)*U->GetAttributes()->GetSkillMultiplier(),D->TeamSpeedSeconds,false,true);
  for(auto* Ally:B->Units())if(IsValid(Ally)&&!Ally->GetHeroSkill()->bSummon&&!Ally->IsAuxiliaryEmplacement()&&Ally->GetDefinition()->HeroKind!=EEGHeroKind::Generic&&IEGCombatant::Execute_IsAlive(Ally)&&Ally->GetFaction_Implementation()==U->GetFaction_Implementation())
   AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("StarBlessing"),Ally->GetActorLocation()-FVector(0,0,80));
 }
}
void UEGGriseoAdultSkillBehavior::ActionEnded(bool bCompleted)
{if(MeteorVisual.IsValid())MeteorVisual->Destroy();MeteorVisual.Reset();MeteorReleaseTime=0.f;ActiveAction.Invalidate();bSubmitted=false;}
void UEGGriseoAdultSkillBehavior::EndBattle()
{ActionEnded(false);Super::EndBattle();}
