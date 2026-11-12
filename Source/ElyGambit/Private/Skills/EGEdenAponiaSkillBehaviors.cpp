#include "Skills/EGEdenAponiaSkillBehaviors.h"
#include "Skills/EGSupportSkillUtils.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGAponiaDisciplineVisual.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGSkillNotify.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "NiagaraSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

void UEGMusicalJudgmentBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 // An enemy copy must remain a valid target during its ordinary cast. When it
 // is the last enemy, hiding it from targeting stalls every opposing attacker.
 if(C.bUltimate&&IsCurrentBattle()&&IEGCombatant::Execute_GetFaction(Unit.Get())!=EEGFaction::Enemy)
  Skill->SetActionProtected(true);
}
void UEGMusicalJudgmentBehavior::ActionEnded(bool bCompleted)
{if(Skill.IsValid())Skill->SetActionProtected(false);}
void UEGMusicalJudgmentBehavior::EndBattle()
{
 for(const auto& E:Effects)if(E.IsValid())E->Destroy();Effects.Reset();
 if(Skill.IsValid())Skill->SetActionProtected(false);Super::EndBattle();
}
bool UEGMusicalJudgmentBehavior::IsOtherHero(AEGChessUnit* Source) const
{
 return IsCurrentBattle()&&IsValid(Source)&&Source!=Unit.Get()&&Source->GetDefinition()&&
  IEGCombatant::Execute_IsAlive(Source)&&!Source->IsAuxiliaryEmplacement()&&!Source->GetHeroSkill()->bSummon&&
  Source->GetDefinition()->HeroKind!=EEGHeroKind::Generic&&
  IEGCombatant::Execute_GetFaction(Source)==IEGCombatant::Execute_GetFaction(Unit.Get());
}
void UEGMusicalJudgmentBehavior::PresentAt(const TSoftObjectPtr<UEGSourceEffectDefinition>& Ref,AEGChessUnit* Target)
{
 if(!IsCurrentBattle()||!IsValid(Target))return;
 // Config assets and their dependencies are held by the run preload handle.
 auto* FX=Ref.Get();if(!FX||!FX->bTargetGround)return;
 if(auto* E=AEGSourceEffect::Spawn(Skill.Get(),Unit->GetVisiblePresentationMesh(),FX,1.f,true))
 {
  const float Height=Target->GetDefinition()?Target->GetDefinition()->GetPresentationHeight()*FX->TargetHeightFraction:0.f;
  E->SetActorLocation(Target->GetActorLocation()-FVector(0,0,88)+FVector(0,0,Height));
  Effects.RemoveAll([](const auto& P){return !P.IsValid();});Effects.Add(E);
 }
}
void UEGMusicalJudgmentBehavior::BasicHit(const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle())return;
 auto* U=Unit.Get();auto* T=C.Target.Get();auto* B=Battle.Get();
 if(!U->GetCombat()->IsValidEnemy(T)||!U->GetCombat()->IsInAttackRange(T))return;
 if(const auto* D=Cast<UEGEdenSkillDefinition>(U->GetDefinition()->SkillDefinition))PresentAt(D->BasicImpact,T);
 if(const auto* D=Cast<UEGAponiaSkillDefinition>(U->GetDefinition()->SkillDefinition))PresentAt(D->BasicImpact,T);
 if(B->Damage(U,T,C.Attack,C.ActionId,true))
 {
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("EnhancedHit"),T->GetActorLocation(),U->GetActorRotation());
  if(auto* Audio=U->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(TEXT("Hit"),C.ActionId,T);
  B->BasicHit(U,T,C.ActionId);
 }
}
void UEGEdenSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 Super::ActionStarted(C);ShotHitTime=0.f;ShotTarget=C.Target;
 if(!IsCurrentBattle())return;
 if(C.bUltimate){AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("Overture"),Unit->GetActorLocation()-FVector(0,0,88));return;}
 ShotStart=Unit->GetActorLocation()+FVector(0,0,25);
 const UAnimSequenceBase* Clip=Skill->GetActiveMontage();if(!Clip)Clip=Skill->GetActiveAnimation();
 if(Clip)for(const auto& N:Clip->Notifies)
  if(const auto* Event=Cast<UEGSkillNotify>(N.Notify);Event&&Event->Event==TEXT("Hit")&&N.GetTriggerTime()>0.f)
   if(ShotHitTime<=0.f||N.GetTriggerTime()<ShotHitTime)ShotHitTime=N.GetTriggerTime();
}
void UEGEdenSkillBehavior::TickAction(float Position)
{
 if(!IsCurrentBattle()||ShotHitTime<=0.f||!ShotTarget.IsValid()||!Unit->GetCombat()->IsValidEnemy(ShotTarget.Get()))return;
 const float Flight=FMath::Min(.32f,ShotHitTime),Start=ShotHitTime-Flight;
 if(Position<Start||Position>=ShotHitTime)return;
 const FVector End=ShotTarget->GetActorLocation()+FVector(0,0,15);const FRotator Facing=(End-ShotStart).Rotation();
 if(!ShotVisual.IsValid())ShotVisual=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("GoldenShot"),ShotStart,Facing);
 if(ShotVisual.IsValid())ShotVisual->SetActorLocationAndRotation(FMath::Lerp(ShotStart,End,FMath::Clamp((Position-Start)/Flight,0.f,1.f)),Facing);
}
void UEGEdenSkillBehavior::ActionEnded(bool bCompleted)
{
 if(ShotVisual.IsValid())ShotVisual->Destroy();ShotVisual.Reset();ShotTarget.Reset();ShotHitTime=0.f;
 Super::ActionEnded(bCompleted);
}
void UEGEdenSkillBehavior::OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId)
{
 if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(Unit.Get())||Skill->bSummon||Skill->IsCasting())return;
 const auto* D=Cast<UEGEdenSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 Notes=FMath::Min(FMath::Max(1,D->AttacksPerNote),Notes+1);PayNote();
}
void UEGEdenSkillBehavior::TickBattle(float DeltaSeconds){PayNote();}
void UEGEdenSkillBehavior::PayNote()
{
 if(!IsCurrentBattle()||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const auto* D=Cast<UEGEdenSkillDefinition>(Unit->GetDefinition()->SkillDefinition);
 if(!D||Notes<FMath::Max(1,D->AttacksPerNote)||D->NoteMana<=0)return;
 TArray<AEGChessUnit*> Candidates;
 for(auto* T:Battle->Units())if(IsOtherHero(T)&&!T->GetHeroSkill()->BlocksManaGain())
 {const auto* A=T->GetAttributes();if(A->GetMaxMana()>0&&A->GetMana()<A->GetMaxMana()-.01f)Candidates.Add(T);}
 Candidates.Sort([](const AEGChessUnit& A,const AEGChessUnit& B){return A.GetAttributes()->GetMana()/A.GetAttributes()->GetMaxMana()<B.GetAttributes()->GetMana()/B.GetAttributes()->GetMaxMana();});
 if(Candidates.IsEmpty())return;
 Notes=0;++NotesPaid;const float Mana=D->NoteMana*Unit->GetAttributes()->GetSkillMultiplier();
 const int32 Count=FMath::Min(Candidates.Num(),Skill->GetLevel()>=11&&GetWorld()->GetTimeSeconds()<EncoreEnd?2:1);
 for(int32 I=0;I<Count;++I)
 {
  if(FMath::IsFinite(Mana))Candidates[I]->RestoreMana(Mana);
  PresentAt(D->NoteEffect,Candidates[I]);
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("NoteReceive"),Candidates[I]->GetActorLocation()-FVector(0,0,65));
 }
}
void UEGEdenSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 if(!C.bUltimate){if(Event==TEXT("Hit")){if(ShotVisual.IsValid())ShotVisual->Destroy();ShotVisual.Reset();ShotHitTime=0.f;BasicHit(C);}return;}
 if(Event!=TEXT("Release"))return;
 auto* U=Unit.Get();auto* B=Battle.Get();const auto* D=Cast<UEGEdenSkillDefinition>(U->GetDefinition()->SkillDefinition);if(!D)return;
 if(!Skill->CommitStigmataUltimate(C.ActionId))return;
 if(!U->HasAuthority())return;
 const FVector Center=U->GetActorLocation();
 const float RadiusSq=FMath::Square(EGSupport::Radius(U,D->HealingRadiusCells));
 AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("GoldenFinale"),Center-FVector(0,0,80));
 const auto Snapshot=B->Units();
 for(auto* T:Snapshot)
 {
  if(!IsCurrentBattle())break;
  if(!EGSupport::Ally(U,T)||FVector::DistSquared2D(Center,T->GetActorLocation())>RadiusSq)continue;
  B->Heal(U,T,C.Attack*D->HealingMultiplier*U->GetAttributes()->GetSkillMultiplier());
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("SupportHeal"),T->GetActorLocation()-FVector(0,0,65));
 }
 if(IsCurrentBattle())
 {
  B->ApplyEdenSong(U,(C.Tier>=1?D->AdvancedSongManaPerSecond:D->SongManaPerSecond)*U->GetAttributes()->GetSkillMultiplier(),D->SongSeconds);
  EncoreEnd=GetWorld()->GetTimeSeconds()+D->SongSeconds;
 }
}
void UEGEdenSkillBehavior::EndBattle(){ActionEnded(false);Notes=NotesPaid=0;EncoreEnd=0;Super::EndBattle();}

void UEGAponiaSkillBehavior::OnBattleConfigured()
{
 if(!IsCurrentBattle())return;
 const auto* D=Cast<UEGAponiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);
 if(D&&!DisciplineVisual.IsValid())DisciplineVisual=AEGAponiaDisciplineVisual::Spawn(Unit.Get(),this,D->DisciplineNode.Get());
}
void UEGAponiaSkillBehavior::ActionStarted(const FEGSkillActionContext& C)
{
 Super::ActionStarted(C);
 BasicHitTime=0.f;BasicTarget=C.Target;
 if(!C.bUltimate&&IsCurrentBattle())
 {
  BasicStart=Unit->GetActorLocation()+FVector(0,0,25);
  const UAnimSequenceBase* Clip=Skill->GetActiveMontage();
  if(!Clip)Clip=Skill->GetActiveAnimation();
  if(Clip)for(const auto& Notify:Clip->Notifies)
   if(const auto* Event=Cast<UEGSkillNotify>(Notify.Notify);Event&&Event->Event==TEXT("Hit"))
    if(Notify.GetTriggerTime()>0.f&&(BasicHitTime<=0.f||Notify.GetTriggerTime()<BasicHitTime))BasicHitTime=Notify.GetTriggerTime();
 }
 if(C.bUltimate){CastId=C.ActionId;CastAttack=C.Attack;CastBudget=C.Attack*C.Multiplier;
 const auto* D=Cast<UEGAponiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);CastPenalty=C.Attack*(C.Tier>=1?D->AdvancedPenaltyMultiplier:D->PenaltyMultiplier);
 VerdictPoint=Unit->GetCombat()->IsValidEnemy(C.Target.Get())?C.Target->GetActorLocation():Unit->GetActorLocation();bDisciplineConsumed=false;bVerdictReleased=false;DisciplineSnapshot=PenaltiesReleased=0;PenaltyRemaining=0.f;bJudging=false;NextLightning=0;JudgedAttacks.Reset();}
 if(C.bUltimate&&IsCurrentBattle())
 {
  if(WingsVisual.IsValid())WingsVisual->Destroy();
  WingsVisual=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("PrayerWings"),Unit->GetActorLocation()+FVector(0,0,40),Unit->GetActorRotation());
 }
}
void UEGAponiaSkillBehavior::TickAction(float Position)
{
 if(WingsVisual.IsValid()&&Unit.IsValid())
  if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(Unit.Get(),0))
  {
   WingsVisual->SetActorRotation((Camera->GetCameraLocation()-WingsVisual->GetActorLocation()).Rotation());
  }
 if(!IsCurrentBattle()||Skill->IsCasting()||BasicHitTime<=0.f||!BasicTarget.IsValid()||!Unit->GetCombat()->IsValidEnemy(BasicTarget.Get()))return;
 const float Flight=FMath::Min(.28f,BasicHitTime);
 const float StartTime=BasicHitTime-Flight;
 if(Position<StartTime||Position>=BasicHitTime)return;
 const FVector End=BasicTarget->GetActorLocation()+FVector(0,0,15);
 const FRotator Facing=(End-BasicStart).Rotation();
 if(!BasicVisual.IsValid())BasicVisual=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("ButterflyBolt"),BasicStart,Facing);
 if(BasicVisual.IsValid())BasicVisual->SetActorLocationAndRotation(FMath::Lerp(BasicStart,End,FMath::Clamp((Position-StartTime)/Flight,0.f,1.f)),Facing);
}
void UEGAponiaSkillBehavior::AllyBasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid Id)
{
 if(!IsOtherHero(Source)||!IEGCombatant::Execute_IsAlive(Unit.Get()))return;
 const auto* D=Cast<UEGAponiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 if(!Skill->IsCasting())
 {
  if(!Id.IsValid()||GainedAttacks.Contains(Id))return;
  GainedAttacks.Add(Id);DisciplineStacks=FMath::Min(D->DisciplineLimit,DisciplineStacks+1);
  const float Mana=D->AllyAttackMana*Unit->GetAttributes()->GetSkillMultiplier();if(FMath::IsFinite(Mana))Unit->RestoreMana(Mana);
  return;
 }
 if(Skill->GetLevel()<11||!bJudging||!CastId.IsValid()||Skill->ActionId!=CastId||!Id.IsValid()||JudgedAttacks.Contains(Id))return;
 JudgedAttacks.Add(Id);
 const double Now=Unit->GetWorld()->GetTimeSeconds();
 if(Now+UE_KINDA_SMALL_NUMBER<NextLightning||!Unit->GetCombat()->IsValidEnemy(Target))return;
 // Reserve the shared interval before damage; callbacks may end this round.
 NextLightning=Now+FMath::Max(.05f,D->LightningInterval);++LightningCount;
 PresentAt(D->LightningEffect,Target);
 if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->ActionPulse(TEXT("Lightning"),CastId,Id,Target->GetActorLocation());
 // A fresh damage receipt is not a basic hit and cannot generate mana/notes.
 Battle->Damage(Unit.Get(),Target,CastAttack*D->LightningMultiplier*Unit->GetUltimateBonus(),FGuid::NewGuid());
}
bool UEGAponiaSkillBehavior::ConsumeDiscipline(const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.ActionId!=CastId||C.RoundId!=RoundId||!IEGCombatant::Execute_IsAlive(Unit.Get()))return false;
 if(bDisciplineConsumed)return true;
 if(!Skill->CommitStigmataUltimate(C.ActionId)||!IsCurrentBattle()||C.ActionId!=CastId)return false;
 const auto* D=Cast<UEGAponiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return false;
 DisciplineSnapshot=FMath::Clamp(DisciplineStacks,0,D->DisciplineLimit);DisciplineStacks=0;
 PenaltiesReleased=0;PenaltyRemaining=.08f;bDisciplineConsumed=true;
 CastBudget*=Unit->GetUltimateBonus();CastPenalty*=Unit->GetUltimateBonus();
 return true;
}
void UEGAponiaSkillBehavior::EmitPenalty()
{
 if(!IsCurrentBattle()||!bDisciplineConsumed||PenaltiesReleased>=DisciplineSnapshot||Skill->ActionId!=CastId)return;
 const auto* D=Cast<UEGAponiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 ++PenaltiesReleased; // Reserve before damage callbacks can cancel the action.
 const FGuid Expected=CastId;const auto Targets=Battle->Units();
 AEGChessUnit* Best=nullptr;double BestDistance=TNumericLimits<double>::Max();
 for(auto* Target:Targets)
 {
  if(!Unit->GetCombat()->IsValidEnemy(Target))continue;
  const double Distance=FVector::DistSquared2D(VerdictPoint,Target->GetActorLocation());
  if(Distance<BestDistance){Best=Target;BestDistance=Distance;}
 }
 if(Best&&Battle->Damage(Unit.Get(),Best,CastPenalty,Expected)&&IsCurrentBattle()&&Expected==CastId)
 {
  PresentAt(D->LightningEffect,Best);AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("EnhancedLightning"),Best->GetActorLocation());
  if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())
   Audio->ActionPulse(TEXT("Lightning"),Expected,FGuid::NewGuid(),Best->GetActorLocation());
 }
}
void UEGAponiaSkillBehavior::TickBattle(float DeltaSeconds)
{
 if(!IsCurrentBattle()||!bJudging||!bDisciplineConsumed||PenaltiesReleased>=DisciplineSnapshot)return;
 const auto* D=Cast<UEGAponiaSkillDefinition>(Unit->GetDefinition()->SkillDefinition);if(!D)return;
 PenaltyRemaining-=FMath::Max(0.f,DeltaSeconds);
 while(PenaltyRemaining<=0.f&&bJudging&&IsCurrentBattle()&&Skill->ActionId==CastId&&PenaltiesReleased<DisciplineSnapshot)
 {PenaltyRemaining+=D->JudgmentSeconds/FMath::Max(1,DisciplineSnapshot);EmitPenalty();}
}
void UEGAponiaSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 if(!C.bUltimate){if(Event==TEXT("Hit")){if(BasicVisual.IsValid())BasicVisual->Destroy();BasicVisual.Reset();BasicHitTime=0.f;BasicHit(C);}return;}
 if(Event==TEXT("Judge")){if(ConsumeDiscipline(C))bJudging=true;return;}
 if(Event!=TEXT("Release")||bVerdictReleased)return;
 bJudging=false;
 auto* U=Unit.Get();auto* B=Battle.Get();const auto* D=Cast<UEGAponiaSkillDefinition>(U->GetDefinition()->SkillDefinition);if(!D)return;
 if(!ConsumeDiscipline(C))return;
 while(IsCurrentBattle()&&C.ActionId==CastId&&Skill->ActionId==CastId&&PenaltiesReleased<DisciplineSnapshot)EmitPenalty();
 if(!IsCurrentBattle()||C.ActionId!=CastId)return;
 bVerdictReleased=true;const float FinalDamage=CastBudget;
 if(WingsVisual.IsValid())WingsVisual->Destroy();
 WingsVisual=AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("VerdictWings"),U->GetActorLocation()+FVector(0,0,40),U->GetActorRotation());
 const auto Snapshot=B->Units();
 for(auto* T:Snapshot)
 {
  if(!IsCurrentBattle()||C.ActionId!=CastId)break;
  if(!U->GetCombat()->IsValidEnemy(T)||FVector::DistSquared2D(VerdictPoint,T->GetActorLocation())>FMath::Square(EGSupport::Radius(U,D->UltimateRadius/500.f)))continue;
  PresentAt(D->VerdictEffect,T);B->Damage(U,T,FinalDamage,C.ActionId);
  AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("EnhancedVerdict"),T->GetActorLocation());
 }
}
void UEGAponiaSkillBehavior::ActionEnded(bool bCompleted)
{
 if(BasicVisual.IsValid())BasicVisual->Destroy();BasicVisual.Reset();BasicTarget.Reset();BasicHitTime=0.f;
 if(WingsVisual.IsValid())WingsVisual->Destroy();WingsVisual.Reset();
 bJudging=false;bDisciplineConsumed=false;DisciplineSnapshot=PenaltiesReleased=0;PenaltyRemaining=CastBudget=0.f;CastId.Invalidate();JudgedAttacks.Reset();NextLightning=0;Super::ActionEnded(bCompleted);
}
void UEGAponiaSkillBehavior::EndBattle()
{
 if(DisciplineVisual.IsValid())DisciplineVisual->Destroy();DisciplineVisual.Reset();
 ActionEnded(false);LightningCount=DisciplineStacks=0;GainedAttacks.Reset();Super::EndBattle();
}
