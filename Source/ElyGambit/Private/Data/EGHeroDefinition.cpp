#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroGrowthSettings.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Data/EGGriseoAdultSkillDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Data/EGHeroCardInfoDefinition.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Character/EGChessUnit.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGChessAnimInstance.h"
#include "Skills/EGConfirmedSkillBehaviors.h"
#include "Skills/EGGriseoSkillBehaviors.h"
#include "Skills/EGGriseoAdultSkillBehavior.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "Skills/EGSakuraVillVSkillBehaviors.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Skills/EGMonsterSkillBehavior.h"

UAnimMontage* UEGHeroDefinition::GetPresentationMontage(EGPresentationAction Action) const
{
 switch(Action)
 {
 case EGPresentationAction::Attack:return GetAttackMontage().LoadSynchronous();
 case EGPresentationAction::Skill:return GetSkillMontage().LoadSynchronous();
 case EGPresentationAction::Hit:return GetHitMontage().LoadSynchronous();
 case EGPresentationAction::Death:return GetDeathMontage().LoadSynchronous();
 default:return nullptr;
 }
}

#include "Animation/AnimSequence.h"
#include "Presentation/EGSkillNotify.h"
#include "Abilities/GameplayAbility.h"

FEGPresentationClips UEGHeroDefinition::BuildPresentationClips() const
{
	FEGPresentationClips Result;
	Result.Idle = GetIdleAnimation().LoadSynchronous();
	Result.Walk = GetWalkAnimation().LoadSynchronous();
	Result.Run = GetRunAnimation().LoadSynchronous();
	Result.Attack = GetAttackAnimation().LoadSynchronous();
	Result.Skill = GetSkillAnimation().LoadSynchronous();
	Result.Hit = GetHitAnimation().LoadSynchronous();
	Result.Death = GetDeathAnimation().LoadSynchronous();
	// Board speeds are cm/s; clip stride references follow the hero' configured movement speed so a
	// fast hero does not look like it slides while a slow one does not sprint on the spot.
	const float Speed = FMath::Max(GetMoveSpeed(), 60.f);
	Result.WalkReferenceSpeed = FMath::Clamp(Speed * 0.45f, 90.f, 220.f);
	Result.RunReferenceSpeed = FMath::Clamp(Speed, 240.f, 520.f);
	Result.RunMinimumSpeed = FMath::Clamp(Speed * 0.72f, 200.f, 420.f);
 if(GetWalkReferenceSpeed()>0)Result.WalkReferenceSpeed=GetWalkReferenceSpeed();
 if(GetRunReferenceSpeed()>0)Result.RunReferenceSpeed=GetRunReferenceSpeed();
	return Result;
}

FText UEGHeroDefinition::GetSkillDetails(int32 Level,bool Awakened) const
{
 const FString Heading=FString::Printf(TEXT("%s  Lv.%d\n"),*DisplayName.ToString(),Level);
 return FText::FromString(Heading+GetPlayerFacingSkillName().ToString()+TEXT("\n")+GetPlayerFacingSkillEffect().ToString());

}
FText UEGHeroDefinition::GetPlayerFacingSkillSectionName(EEGSkillInfoKind Kind) const
{
 if(const auto* Monster=MonsterAttack.Get())
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(Monster->AttackType==EEGMonsterAttackType::Heal?TEXT("治疗"):TEXT("普攻"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(TEXT("被动"));
  switch(Monster->Ultimate)
  {
   case EEGMonsterUltimate::Shield:return FText::FromString(TEXT("护盾"));
   case EEGMonsterUltimate::EnhancedShot:return FText::FromString(TEXT("强化射击"));
   case EEGMonsterUltimate::TeamSpeed:return FText::FromString(TEXT("团队加速"));
   case EEGMonsterUltimate::CrystalSeed:return FText::FromString(TEXT("水晶种"));
   default:return FText::FromString(TEXT("无独立大招"));
  }
 }
 if(HeroKind==EEGHeroKind::ElysiaHerrscher)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("落花刹那，光翳明灭"):Kind==EEGSkillInfoKind::Passive?TEXT("天眷始终，爱佑世人"):TEXT("命定此身，璀耀永恒"));
 if(HeroKind==EEGHeroKind::Elysia)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("花与鸟之矢"):Kind==EEGSkillInfoKind::Passive?TEXT("始与终之史"):TEXT("你与我之始"));
 if(HeroKind==EEGHeroKind::Aponia)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("爱憎，分明无束"):Kind==EEGSkillInfoKind::Passive?TEXT("此识，诸念禁绝"):TEXT("此罪，永世难辞"));
 if(HeroKind==EEGHeroKind::Hua)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("荒虎"):Kind==EEGSkillInfoKind::Passive?TEXT("无心"):TEXT("狂龙"));
 if(HeroKind==EEGHeroKind::CollapsedHua)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("回首浮生若等闲"):Kind==EEGSkillInfoKind::Passive?TEXT("犹忆迅羽过洪波"):TEXT("沧浪翻涛云翻墨"));
 if(HeroKind==EEGHeroKind::GriseoAdult)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("登程的行杖"):Kind==EEGSkillInfoKind::Passive?TEXT("恒照的示灯"):TEXT("澄澈的决意"));
 if(HeroKind==EEGHeroKind::Sakura)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("寒樱斩"):Kind==EEGSkillInfoKind::Passive?TEXT("虚影步"):TEXT("寒天狂舞"));
 if(HeroKind==EEGHeroKind::VillV)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("万敌专对的轮齿"):Kind==EEGSkillInfoKind::Passive?TEXT("倾覆矩法的群集"):TEXT("余众俱寂的终演"));
 if(HeroKind==EEGHeroKind::Mobius)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("侵吞诸界的郁暗"):Kind==EEGSkillInfoKind::Passive?TEXT("永生不死的恶咒"):TEXT("崩落无存的人心"));
 if(HeroKind==EEGHeroKind::Griseo)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("画家与骑士"):Kind==EEGSkillInfoKind::Passive?TEXT("星空·印象"):TEXT("创世：绘梦境"));
 if(HeroKind==EEGHeroKind::Pardofelis)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("能打就打"):Kind==EEGSkillInfoKind::Passive?TEXT("「老板」"):TEXT("去吧，罐头！"));
 if(HeroKind==EEGHeroKind::Eden)return FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("随想曲：星光"):Kind==EEGSkillInfoKind::Passive?TEXT("安魂曲：追忆"):TEXT("幻想曲：颂世"));
 return Kind==EEGSkillInfoKind::Ultimate?GetPlayerFacingSkillName():FText::FromString(Kind==EEGSkillInfoKind::Basic?TEXT("普攻"):TEXT("被动"));
}
FText UEGHeroDefinition::GetPlayerFacingSkillName() const
{
 if(MonsterAttack)return GetPlayerFacingSkillSectionName(EEGSkillInfoKind::Ultimate);
 if(HeroKind==EEGHeroKind::Griseo||HeroKind==EEGHeroKind::Pardofelis||HeroKind==EEGHeroKind::Eden)return GetPlayerFacingSkillSectionName(EEGSkillInfoKind::Ultimate);
 if(HeroKind==EEGHeroKind::Elysia)return FText::FromString(TEXT("你与我之始"));
 if(HeroKind==EEGHeroKind::ElysiaHerrscher)return FText::FromString(TEXT("命定此身，璀耀永恒"));
 return CardInformation&&CardInformation->bHasUltimate?CardInformation->UltimateName:FText::FromString(TEXT("无独立大招"));
}
FText UEGHeroDefinition::GetPlayerFacingSkillEffect() const
{
 if(MonsterAttack)
  return FText::FromString(FString::Printf(TEXT("%s\n%s\n\n%s\n%s"),
   *GetPlayerFacingSkillSectionName(EEGSkillInfoKind::Basic).ToString(),*GetPlayerFacingSkillSection(EEGSkillInfoKind::Basic).ToString(),
   *GetPlayerFacingSkillSectionName(EEGSkillInfoKind::Ultimate).ToString(),*GetPlayerFacingSkillSection(EEGSkillInfoKind::Ultimate).ToString()));
 const UClass* Behavior=GetBehaviorClass().Get();
 const bool bStructured=Behavior&&(
  (Cast<UEGEdenSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGEdenSkillBehavior::StaticClass()))||
  (Cast<UEGAponiaSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGAponiaSkillBehavior::StaticClass()))||
  (Cast<UEGGriseoSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGGriseoSkillBehavior::StaticClass()))||
  (Cast<UEGGriseoAdultSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGGriseoAdultSkillBehavior::StaticClass()))||
  (Cast<UEGCollapsedHuaSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGCollapsedHuaSkillBehavior::StaticClass()))||
  (Cast<UEGHuaSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGHuaSkillBehavior::StaticClass()))||
  (Cast<UEGMobiusSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGMobiusSkillBehavior::StaticClass()))||
  (Cast<UEGVillVSkillDefinition>(SkillDefinition)&&Behavior->IsChildOf(UEGVillVSkillBehavior::StaticClass())));
 if((HeroKind==EEGHeroKind::Elysia&&Cast<UEGElysiaSkillDefinition>(SkillDefinition))||
    (HeroKind==EEGHeroKind::ElysiaHerrscher&&Cast<UEGElysiaHerrscherSkillDefinition>(SkillDefinition))||
    (HeroKind==EEGHeroKind::Sakura&&Cast<UEGSakuraSkillDefinition>(SkillDefinition)&&Behavior&&Behavior->IsChildOf(UEGSakuraSkillBehavior::StaticClass()))||
    (HeroKind==EEGHeroKind::Pardofelis&&GetBehaviorClass()&&GetBehaviorClass()->IsChildOf(UEGPardoSkillBehavior::StaticClass()))||bStructured)
 {
  return FText::FromString(FString::Printf(TEXT("普攻\n%s\n\n被动\n%s\n\n大招\n%s"),
   *GetPlayerFacingSkillSection(EEGSkillInfoKind::Basic).ToString(),
   *GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString(),
   *GetPlayerFacingSkillSection(EEGSkillInfoKind::Ultimate).ToString()));
 }
 return CardInformation?CardInformation->Effect:FText::GetEmpty();
}

FText UEGHeroDefinition::GetPlayerFacingSkillSection(EEGSkillInfoKind Kind) const
{
 if(const auto* Monster=MonsterAttack.Get())
 {
  if(Kind==EEGSkillInfoKind::Basic)
  {
   if(Monster->AttackType==EEGMonsterAttackType::Heal)return FText::FromString(FString::Printf(TEXT("治疗范围内的友方目标，基础治疗量为目标最大生命的%.0f%%，不超过其损失生命。"),Monster->HealHealthRatio*100.f));
   return FText::FromString(Monster->AttackType==EEGMonsterAttackType::Melee?TEXT("接近敌人并以近战攻击造成攻击力等量的伤害。"):TEXT("向敌人发射投射物，造成攻击力等量的伤害。"));
  }
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(bEliteEnemy&&HeroKind==EEGHeroKind::Generic?TEXT("精英单位，免疫樱的斩杀效果。"):TEXT("无独立被动。"));
  switch(Monster->Ultimate)
  {
  case EEGMonsterUltimate::None:return FText::FromString(TEXT("无独立大招。"));
  case EEGMonsterUltimate::Shield:return FText::FromString(FString::Printf(TEXT("获得基础值为自身最大生命%.0f%%的护盾，持续%.1f秒。"),Monster->ShieldHealthRatio*100.f,Monster->ShieldSeconds));
  case EEGMonsterUltimate::EnhancedShot:return FText::FromString(FString::Printf(TEXT("发射强化弹，造成基础%.0f%%攻击力伤害。"),Monster->UltimateMultiplier*100.f));
  case EEGMonsterUltimate::TeamSpeed:return FText::FromString(FString::Printf(TEXT("有其他可选中友军时，为施放瞬间可选中的友方单位（包括自身）提供基础%.0f%%攻速加成，持续%.1f秒；之后入场的单位不受影响。"),Monster->TeamSpeedBonus*100.f,Monster->TeamSpeedSeconds));
  case EEGMonsterUltimate::CrystalSeed:return FText::FromString(FString::Printf(TEXT("在目标位置生成水晶种，%.1f秒后对约%.1f格半径范围造成基础%.0f%%攻击力伤害。"),Monster->SeedDelay,Monster->SeedRadiusCells,Monster->UltimateMultiplier*100.f));
  default:return FText::GetEmpty();
  }
 }
 if(HeroKind==EEGHeroKind::Can&&GetBehaviorClass()&&GetBehaviorClass()->IsChildOf(UEGCanSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("靠近敌人并攻击，造成攻击力等量的伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(TEXT("由帕朵召唤并独立战斗；其战斗属性可能受到召唤者和友方效果影响。"));
  return FText::FromString(TEXT("罐头不能释放独立大招。"));
 }
 if(HeroKind==EEGHeroKind::Knight&&GetBehaviorClass()&&GetBehaviorClass()->IsChildOf(UEGKnightSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("靠近敌人并攻击，造成攻击力等量的伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("接近敌人时可触发一次短暂守护，使范围内未被嘲讽的敌人以骑士为目标，持续%.1f秒。"),GetSkillRules().TauntDuration));
  return FText::FromString(TEXT("骑士不能释放独立大招。"));
 }
 const UClass* Behavior=GetBehaviorClass().Get();
 const FVector Multipliers=GetSkillMultipliers();
 if(const auto* D=Cast<UEGEdenSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGEdenSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("双枪射击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("每普攻命中%d次，为法力比例最低且未满的另一名友方英雄回复%.0f点法力。"),D->AttacksPerNote,D->NoteMana));
  return FText::FromString(FString::Printf(TEXT("为自身及周围%.0f格内的友军恢复相当于自身%.0f%%攻击力的生命，并使其他友方英雄每秒回复%.0f点法力，持续%d秒。\n\n6级\n每秒回复的法力提升至%.0f点。\n\n11级\n乐曲期间，被动额外为另一名友方英雄回复法力。"),D->HealingRadiusCells,D->HealingMultiplier*100.f,D->SongManaPerSecond,D->SongSeconds,D->AdvancedSongManaPerSecond));
 }
 if(const auto* D=Cast<UEGAponiaSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGAponiaSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("未施放必杀时，其他友方英雄每次普攻命中，使自身回复%.0f点法力并获得1层戒律，最多%d层。"),D->AllyAttackMana,D->DisciplineLimit));
  return FText::FromString(FString::Printf(TEXT("消耗全部戒律，在%.0f秒内逐层降下雷罚，每层对一名敌人造成%.0f%%攻击力伤害。结束时对目标及其周围2格内的敌人造成%.0f%%攻击力伤害。\n\n6级\n每层雷罚伤害提升至%.0f%%攻击力。\n\n11级\n施法期间，其他友方英雄普攻命中时，对其目标追加%.0f%%攻击力的雷击，每%.1f秒最多触发一次。"),D->JudgmentSeconds,D->PenaltyMultiplier*100.f,Multipliers.X*100.f,D->AdvancedPenaltyMultiplier*100.f,D->LightningMultiplier*100.f,D->LightningInterval));
 }
 if(const auto* D=Cast<UEGGriseoSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGGriseoSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("挥动画笔攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("战斗开始时召唤骑士先生，独立进行近战攻击。骑士拥有格蕾修%.0f%%的最大生命值和%.0f%%的攻击力。"),D->KnightHealthRatio*100.f,D->KnightAttackRatio*100.f));
  return FText::FromString(FString::Printf(TEXT("为骑士先生及其周围%.0f格内的友军提供相当于格蕾修%.0f%%最大生命值的护盾，持续%.0f秒。\n\n6级\n护盾比例提升至%.0f%%。\n\n11级\n额外使格蕾修及其周围%.0f格内的友军攻击力提高%.0f%%，持续%.0f秒。"),D->SupportRadiusCells,D->ShieldRatio*100.f,D->SupportSeconds,D->ShieldRatioAdvanced*100.f,D->SupportRadiusCells,D->AttackBonus*100.f,D->SupportSeconds));
 }
 if(const auto* D=Cast<UEGGriseoAdultSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGGriseoAdultSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("挥动光剑攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("战斗开始时召唤骑士先生，拥有格蕾修%.0f%%的最大生命值和%.0f%%的攻击力。骑士存活时，全体友方英雄攻速提高%.0f%%。"),D->KnightHealthRatio*100.f,D->KnightAttackRatio*100.f,D->PassiveSpeedBonus*100.f));
  return FText::FromString(FString::Printf(TEXT("降下流星，对目标及其周围2格内的敌人造成%.0f%%攻击力伤害，并使全体友方英雄攻速提高%.0f%%，持续%.0f秒。\n\n6级\n必杀提供的攻速加成提升至%.0f%%。\n\n11级\n流星额外使命中的敌人眩晕%.1f秒。"),Multipliers.X*100.f,D->TeamSpeedBonus*100.f,D->TeamSpeedSeconds,D->AdvancedTeamSpeedBonus*100.f,D->MeteorStunSeconds));
 }
 if(const auto* D=Cast<UEGCollapsedHuaSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGCollapsedHuaSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("以拳脚攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("首次受到致命伤时，进入%.0f秒无法行动、无法被选中的复苏状态，随后恢复%.0f%%最大生命值，攻击力提高%.0f%%至战斗结束。每场战斗限触发一次。"),D->ReviveDelay,D->ReviveHealthRatio*100.f,D->ReviveAttackBonus*100.f));
  return FText::FromString(FString::Printf(TEXT("消耗%.0f%%当前生命，对周围2格内的敌人造成%.0f%%攻击力加本次消耗生命值的伤害。释放结束后，生命不足%.0f%%时恢复至%.0f%%最大生命值。\n\n6级\n攻击力伤害倍率提升至%.0f%%。\n\n11级\n释放时，为自身及周围2格内的友军提供相当于本次消耗生命值%.0f%%的护盾，持续%.0f秒。"),D->BurnCurrentHealthRatio*100.f,Multipliers.X*100.f,D->RecoveryHealthRatio*100.f,D->RecoveryHealthRatio*100.f,Multipliers.Y*100.f,D->BurnShieldRatio*100.f,D->BurnShieldSeconds));
 }
 if(const auto* D=Cast<UEGHuaSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGHuaSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("以拳脚攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("首次受到致命伤时，进入%.0f秒无法行动、无法被选中的复苏状态，随后恢复%.0f%%最大生命值。每场战斗限触发一次。"),D->ReviveDelay,D->ReviveHealthRatio*100.f));
  return FText::FromString(FString::Printf(TEXT("跃向目标附近，落地时对周围1格内的敌人造成%.0f%%攻击力伤害。\n\n6级\n落地伤害提升至%.0f%%。\n\n11级\n命中的敌人防御降低%.0f%%，持续%.0f秒。"),Multipliers.X*100.f,Multipliers.Y*100.f,D->ArmorReduction*100.f,D->ArmorSeconds));
 }
 if(const auto* D=Cast<UEGMobiusSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGMobiusSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("释放蛇影尖刺攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("普攻连接目标与其最近的另一名敌人，持续%.0f秒。连接目标受到友方伤害时，将实际生命损失的%.0f%%传导给其他连接目标。\n\n传导不递归，斩杀不参与传导，普攻不覆盖大招群体连接。"),D->LinkDuration,D->LinkDamageRatio*100.f));
  return FText::FromString(FString::Printf(TEXT("轰击目标及其周围2格内的敌人，造成%.0f%%攻击力伤害，并将命中后存活的敌人连接%.0f秒。\n\n6级\n必杀伤害提升至%.0f%%。\n\n11级\n释放后强化普攻%.0f秒，对目标及其周围1格内的敌人造成%.0f%%攻击力伤害。"),Multipliers.X*100.f,D->LinkDuration,Multipliers.Y*100.f,D->EmpoweredDuration,D->EmpoweredMultiplier*100.f));
 }
 if(const auto* D=Cast<UEGVillVSkillDefinition>(SkillDefinition);D&&Behavior&&Behavior->IsChildOf(UEGVillVSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(FString::Printf(TEXT("操纵当前机兵攻击：机枪三连射，共造成100%%攻击力伤害；重炮轰击目标及其周围1格内的敌人，造成%.0f%%攻击力伤害。"),D->CannonMultiplier*100.f));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(TEXT("携带机枪与重炮参战，共占1人口。同一时间操纵一台机兵，两台均被摧毁后退出战斗。"));
  return FText::FromString(FString::Printf(TEXT("切换至另一台机兵，立即进行一次伤害为普通射击%.0f%%的强化射击。仅剩一台时，直接强化射击。\n\n6级\n强化射击伤害提升至普通射击的%.0f%%。\n\n11级\n释放后，另一台存活机兵自动攻击%.0f秒。"),Multipliers.X*100.f,Multipliers.Y*100.f,D->AutoFireSeconds));
 }
 // These two characters' live configurations supersede historical authored text.
 // Read the same values as the existing combined description, without parsing it.
 if(HeroKind==EEGHeroKind::Elysia)if(const auto* D=Cast<UEGElysiaSkillDefinition>(SkillDefinition))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(FString::Printf(TEXT("射出箭矢攻击目标，造成%.0f%%攻击力伤害。"),D->NormalAttackMultiplier*100.f));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("每第%d次普攻改为爱之矢，造成%.0f%%攻击力伤害。"),D->ChargedAttackInterval,D->ChargedMultiplier*100.f));
  if(D->bArrowRainUltimate)
  {
   const FVector M=GetSkillMultipliers();
  return FText::FromString(FString::Printf(TEXT("向目标所在区域降下箭雨，每%.1f秒对周围%.1f格内的敌人造成伤害，持续%.1f秒，共造成%.0f%%攻击力伤害。箭雨期间可继续普攻。\n\n6级\n箭雨总伤害提升至%.0f%%。\n\n11级\n箭雨期间，普攻额外弹射一名附近敌人，造成%.0f%%攻击力伤害。"),D->ArrowRainSeconds/FMath::Max(1,D->ArrowRainPulses),D->ArrowRainRadiusCells,D->ArrowRainSeconds,M.X*D->ArrowRainTotalMultiplier*100.f,M.Y*D->ArrowRainTotalMultiplier*100.f,D->DomeRicochetMultiplier*100.f));
  }
  return FText::FromString(FString::Printf(TEXT("你与我之始：满法力展开%.1f秒无瑕穹顶；期间攻击造成%.0f%%攻击力伤害，命中圈内敌人时向另一名圈内敌人弹射一次，造成%.0f%%攻击力伤害。无第二目标不弹射，结束重置攻击计数。"),D->EmpoweredSeconds,D->ChargedMultiplier*100.f,D->DomeRicochetMultiplier*100.f));
 }
 if(HeroKind==EEGHeroKind::ElysiaHerrscher)if(const auto* D=Cast<UEGElysiaHerrscherSkillDefinition>(SkillDefinition))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(FString::Printf(TEXT("射击目标，造成%.0f%%攻击力伤害。每第%d箭改为贯穿箭，造成%.0f%%攻击力伤害。"),D->BowAttack*100.f,D->BowAttackInterval,D->PiercingArrow*100.f));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("开战获得最大生命%.0f%%的护盾；每射出一箭回复%.0f点法力。免疫控制。"),D->ShieldHealthRatio*100.f,D->ManaPerFinishedBowAttack));
  return FText::FromString(FString::Printf(TEXT("消耗全部法力进入始源形态，持续%.0f秒，并将自身冰镜护盾补至最大生命的%.0f%%。权杖攻击分三段共造成%.0f%%伤害，对目标周围%d格内的其他敌人共造成%.0f%%伤害。期间无法获取法力。\n\n6级\n权杖伤害提升至%.0f%%，范围伤害提升至%.0f%%。\n\n11级\n始源结束时，召唤鳐鱼对所有敌人造成%.0f%%攻击力伤害。"),D->OriginSeconds,D->ShieldHealthRatio*100.f,D->ScepterMain*100.f,D->ScepterSplashRadiusCells,D->ScepterSplash*100.f,D->AdvancedScepterMain*100.f,D->AdvancedScepterSplash*100.f,D->FinisherMain*100.f));
 }
 if(HeroKind==EEGHeroKind::Sakura&&Behavior&&Behavior->IsChildOf(UEGSakuraSkillBehavior::StaticClass()))if(const auto* D=Cast<UEGSakuraSkillDefinition>(SkillDefinition))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("挥刀攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("闪避一次来自正面的普攻，冷却%.0f秒。闪避后，下次普攻伤害提升至%.0f%%。"),D->EvadeCooldownSeconds,D->CounterMultiplier*100.f));
  return FText::FromString(FString::Printf(TEXT("斩击前方扇形区域，造成%.0f%%攻击力伤害；生命比例不高于%.0f%%的普通敌人直接斩杀。\n\n6级\n斩击伤害提升至%.0f%%。\n\n11级\n必杀击败敌人时，回复%.0f点法力，每次释放限触发一次。"),Multipliers.X*100.f,D->ExecuteHealthFraction*100.f,Multipliers.Y*100.f,D->KillMana));
 }
 if(HeroKind==EEGHeroKind::Pardofelis&&GetBehaviorClass()&&GetBehaviorClass()->IsChildOf(UEGPardoSkillBehavior::StaticClass()))
 {
  if(Kind==EEGSkillInfoKind::Basic)return FText::FromString(TEXT("挥动环刃攻击目标，造成100%攻击力伤害。"));
  if(Kind==EEGSkillInfoKind::Passive)return FText::FromString(FString::Printf(TEXT("累计普攻命中%d次，获得%d点崩坏能，每场战斗限触发一次。"),FMath::Max(1,GetSkillRules().CoinAttackThreshold),GetSkillRules().CoinReward));
  return FText::FromString(FString::Printf(TEXT("投出罐头扑击目标，造成%.0f%%攻击力伤害。罐头落地后独立战斗，再次释放时指挥罐头重新扑击。\n\n6级\n扑击伤害提升至%.0f%%。\n\n11级\n罐头在场时，帕朵攻击速度提高%.0f%%。"),Multipliers.X*100.f,Multipliers.Y*100.f,GetSkillRules().LivingCanAttackSpeed*100.f));
 }
 switch(Kind)
 {
 case EEGSkillInfoKind::Basic:return GetBasicDescription();
 case EEGSkillInfoKind::Ultimate:return GetUltimateDescription();
 case EEGSkillInfoKind::Passive:return GetPassiveDescription();
 default:return FText::GetEmpty();
 }
}

FEGStatBlock UEGHeroDefinition::MakeZeroGrowth()
{
	FEGStatBlock Result;
	Result.MaxHealth = Result.MaxMana = Result.AttackPower = Result.AttackSpeed = Result.Defense = 0.f;
	return Result;
}

FEGStatBlock UEGHeroDefinition::EvaluateStats(int32 Level) const
{
	const float N = float(FMath::Max<int64>(0, int64(Level) - 1));
	const auto& GrowthSettings=UEGHeroGrowthSettings::Get();
	const FEGHeroGrowthProfile* Profile=GrowthSettings.FindProfile(HeroKind);
	const float HealthN=Profile?UEGHeroGrowthSettings::WeightedLevels(Level,Profile->HealthMiddle,Profile->HealthLate):N;
	const float AttackN=Profile?UEGHeroGrowthSettings::WeightedLevels(Level,Profile->AttackMiddle,Profile->AttackLate):N;
	const float DefenseN=Profile?UEGHeroGrowthSettings::WeightedLevels(Level,Profile->DefenseMiddle,Profile->DefenseLate):N;
	FEGStatBlock Growth=GetGrowthPerLevel();
	// The formal Human source is deliberately zero-growth; fusion historically
	// installs Elysia's growth into a private card. Match that path for direct
	// Human previews/spawns too, while retaining the independent Human curve.
	if(HeroKind==EEGHeroKind::ElysiaHerrscher&&FMath::IsNearlyZero(Growth.MaxHealth)&&FMath::IsNearlyZero(Growth.MaxMana)&&
	   FMath::IsNearlyZero(Growth.AttackPower)&&FMath::IsNearlyZero(Growth.AttackSpeed)&&FMath::IsNearlyZero(Growth.Defense))
	{
		Growth.MaxHealth=GrowthSettings.HumanFallbackHealthGrowth;
		Growth.AttackPower=GrowthSettings.HumanFallbackAttackGrowth;
		Growth.Defense=GrowthSettings.HumanFallbackDefenseGrowth;
	}
	const float BaseDefenseBonus=Profile&&FMath::IsFinite(Profile->BaseDefenseBonus)?Profile->BaseDefenseBonus:0.f;
	const float DefenseGrowthBonus=Profile&&FMath::IsFinite(Profile->DefenseGrowthBonus)?Profile->DefenseGrowthBonus:0.f;
	FEGStatBlock Result;
	Result.MaxHealth = FMath::Max(1.f, GetBaseStats().MaxHealth + HealthN * Growth.MaxHealth);
	Result.MaxMana = FMath::Max(0.f, GetBaseStats().MaxMana + N * Growth.MaxMana);
	Result.AttackPower = FMath::Max(0.f, GetBaseStats().AttackPower + AttackN * Growth.AttackPower);
	Result.AttackSpeed = FMath::Max(0.f, GetBaseStats().AttackSpeed + N * Growth.AttackSpeed);
	Result.Defense = FMath::Max(0.f, GetBaseStats().Defense + BaseDefenseBonus + DefenseN * (Growth.Defense + DefenseGrowthBonus));
	return Result;
}

bool UEGHeroDefinition::ValidateDefinition(FString& Error,bool bCheckAnimation) const
{
 const auto* ElysiaSkill=HeroKind==EEGHeroKind::Elysia?Cast<UEGElysiaSkillDefinition>(SkillDefinition):nullptr;
 const bool bElysiaBasicOnly=ElysiaSkill&&ElysiaSkill->bBasicOnlyPresentation;
 if(bUseSplitConfiguration && (!StatsDefinition || !PresentationDefinition || !SkillDefinition))
 {Error=GetPathName()+TEXT(": split configuration requires stats, presentation and skill assets.");return false;}
 if(!bUseSplitConfiguration && (StatsDefinition || PresentationDefinition || SkillDefinition))
 {Error=GetPathName()+TEXT(": configuration references require explicit split configuration mode.");return false;}
 auto Invalid=[this,&Error](const TCHAR* Field){Error=GetPathName()+TEXT(": invalid ")+Field;return false;};
 if(bCheckAnimation&&!CombatActorClass.IsNull())
 {
  const auto* Class=CombatActorClass.LoadSynchronous();
  if(!Class||!Class->IsChildOf(AEGChessUnit::StaticClass())||Class->HasAnyClassFlags(CLASS_Abstract))return Invalid(TEXT("combat actor Blueprint class"));
 }
 auto FiniteStats=[](const FEGStatBlock& S){return FMath::IsFinite(S.MaxHealth)&&FMath::IsFinite(S.MaxMana)&&FMath::IsFinite(S.AttackPower)&&FMath::IsFinite(S.AttackSpeed)&&FMath::IsFinite(S.Defense);};
 if(HeroId.IsNone())return Invalid(TEXT("HeroId"));
 if(PlacementFootprint.X<1||PlacementFootprint.Y<1||PlacementFootprint.X>63||PlacementFootprint.Y>63||PlacementFootprint.X%2==0||PlacementFootprint.Y%2==0)return Invalid(TEXT("placement footprint must have positive odd dimensions"));
 if(!FiniteStats(GetBaseStats())||!FiniteStats(GetGrowthPerLevel())||GetBaseStats().MaxHealth<1||GetBaseStats().MaxMana<0||GetBaseStats().AttackPower<0||GetBaseStats().AttackSpeed<0||GetBaseStats().Defense<0)return Invalid(TEXT("stats"));
 for(const float Value:{GetAttackRange(),GetMoveSpeed(),GetManaOnHit(),GetManaOnDamage(),GetAwakenedUltimateBonus(),GetPresentationGroundOffset(),GetPresentationHeight(),GetPresentationPickRadius(),GetWalkReferenceSpeed(),GetRunReferenceSpeed()})if(!FMath::IsFinite(Value))return Invalid(TEXT("numeric rule"));
 if(GetAttackRange()<=0||GetMoveSpeed()<0||GetManaOnHit()<0||GetManaOnDamage()<0||GetSameNameLevelGain()<1||GetAwakeningCardCount()<1||GetWalkReferenceSpeed()<0||GetRunReferenceSpeed()<0)return Invalid(TEXT("combat or growth rule"));
 if(GetSkillMultipliers().ContainsNaN()||GetSkillSecondary().ContainsNaN()||GetMeshScale().ContainsNaN()||GetMeshScale().GetMin()<=0||GetPresentationRotation().ContainsNaN()||GetPresentationHeight()<0||GetPresentationPickRadius()<=0||GetSkillTierStep()<1)return Invalid(TEXT("skill or presentation values"));
 if(MonsterAttack)
 {
  const auto* P=MonsterAttack.Get();
  if(HeroKind!=EEGHeroKind::Generic)return Invalid(TEXT("monster identity"));
  if(P->Ultimate!=EEGMonsterUltimate::None&&(!bCardSkillsEnabled||GetBaseStats().MaxMana<=0))return Invalid(TEXT("monster ultimate mana"));
  for(float V:{P->UltimateMultiplier,P->ShieldHealthRatio,P->ShieldSeconds,P->HealHealthRatio,P->TeamSpeedBonus,P->TeamSpeedSeconds,P->EnhancedProjectileScale})if(!FMath::IsFinite(V)||V<0)return Invalid(TEXT("monster skill values"));
  if(P->ShieldSeconds<=0||P->TeamSpeedSeconds<=0||P->EnhancedProjectileScale<1||P->ShieldHealthRatio>1||P->HealHealthRatio>1)return Invalid(TEXT("monster skill duration/ratio"));
  if(!FMath::IsFinite(P->SeedDelay)||P->SeedDelay<=0||!FMath::IsFinite(P->SeedRadiusCells)||P->SeedRadiusCells<=0||P->MaxActiveSeeds<1)return Invalid(TEXT("monster seed configuration"));
  if(!FMath::IsFinite(P->BodyRadius)||P->BodyRadius<=0||!FMath::IsFinite(P->ContactTolerance)||P->ContactTolerance<0||
     !FMath::IsFinite(P->AttackRangeCells)||P->AttackRangeCells<0||!FMath::IsFinite(P->DeathHoldSeconds)||P->DeathHoldSeconds<0||!FMath::IsFinite(P->DissolveSeconds)||P->DissolveSeconds<=0||
     !FMath::IsFinite(P->ProjectileSpeed)||P->ProjectileSpeed<=0||!FMath::IsFinite(P->ProjectileRadius)||P->ProjectileRadius<=0||P->LaunchOffset.ContainsNaN())return Invalid(TEXT("monster profile"));
  for(const auto& Material:P->DeathMaterials)if(Material.IsNull()||(bCheckAnimation&&!Material.LoadSynchronous()))return Invalid(TEXT("monster death material"));
 }
 if(HeroKind==EEGHeroKind::Elysia)
 {
  const auto* E=Cast<UEGElysiaSkillDefinition>(SkillDefinition);
  if(!E||!E->HasValidSkillRules())return Invalid(TEXT("Elysia skill configuration"));
  if(!PresentationDefinition||PresentationDefinition->AttackSequence.Num()!=5||(!bElysiaBasicOnly&&PresentationDefinition->AttackAssemblies.Num()!=5))return Invalid(TEXT("Elysia four normal and one charged action"));
 }
 if(HeroKind==EEGHeroKind::ElysiaHerrscher)
 {
  const auto* H=Cast<UEGElysiaHerrscherSkillDefinition>(SkillDefinition);
  if(!H||!H->HasValidSkillRules()||GetPlacementFootprint()!=FIntPoint(1,1)||!bCardSkillsEnabled||!GetUltimateAbility())return Invalid(TEXT("Human Herrscher skill configuration"));
  if(!PresentationDefinition||PresentationDefinition->AttackSequence.Num()!=6||PresentationDefinition->UltimateSequence.Num()!=2||
     (!PresentationDefinition->AttackAssemblies.IsEmpty()&&PresentationDefinition->AttackAssemblies.Num()!=6))return Invalid(TEXT("Human Herrscher authored combat actions"));
 }
 if(HeroKind==EEGHeroKind::Eden)
 {
  const auto* E=Cast<UEGEdenSkillDefinition>(SkillDefinition);if(!E||!E->HasValidSkillRules())return Invalid(TEXT("Eden musical support configuration"));
 }
 if(HeroKind==EEGHeroKind::Sakura)
 {
  const auto* S=Cast<UEGSakuraSkillDefinition>(SkillDefinition);if(!S||!S->HasValidSkillRules())return Invalid(TEXT("Sakura execution configuration"));
 }
 if(HeroKind==EEGHeroKind::Aponia)
 {const auto* A=Cast<UEGAponiaSkillDefinition>(SkillDefinition);if(!A||!A->HasValidSkillRules())return Invalid(TEXT("Aponia judgment configuration"));}
 if(HeroKind==EEGHeroKind::Griseo)
 {const auto* G=Cast<UEGGriseoSkillDefinition>(SkillDefinition);if(!G||!SummonDefinition||SummonDefinition->HeroKind!=EEGHeroKind::Knight||!G->HasValidSkillRules())return Invalid(TEXT("Griseo summoner protection configuration"));}
 if(HeroKind==EEGHeroKind::GriseoAdult)
 {
  const auto* G=Cast<UEGGriseoAdultSkillDefinition>(SkillDefinition);
  if(!G||!G->HasValidSkillRules()||!SummonDefinition||SummonDefinition->HeroKind!=EEGHeroKind::Knight)return Invalid(TEXT("Adult Griseo skill or Knight configuration"));
  if(!PresentationDefinition||PresentationDefinition->AttackSequence.Num()!=4||PresentationDefinition->AttackAssemblies.Num()!=4)return Invalid(TEXT("Adult Griseo four attacks"));
 }
 if(HeroKind==EEGHeroKind::Knight&&(GetBaseStats().MaxMana>0||GetUltimateAbility()||SummonDefinition))return Invalid(TEXT("Knight must remain a mana-free support summon"));
 if(bCheckAnimation && !GetAnimationClass().IsNull())
 {
  UClass* ResolvedAnimationClass=GetAnimationClass().LoadSynchronous();
  if(!ResolvedAnimationClass || (!ResolvedAnimationClass->IsChildOf(UEGCombatAnimInstance::StaticClass()) && !ResolvedAnimationClass->IsChildOf(UEGChessAnimInstance::StaticClass())))
   return Invalid(TEXT("unsupported animation class"));
  USkeletalMesh* ResolvedMesh=GetMesh().LoadSynchronous();
  if(!ResolvedMesh || !ResolvedMesh->GetSkeleton())return Invalid(TEXT("animation mesh or skeleton"));
  if(const auto* Generated=Cast<UAnimBlueprintGeneratedClass>(ResolvedAnimationClass))
  {
   const USkeleton* Skeleton=Generated->GetTargetSkeleton();
   if(!Skeleton || !Skeleton->IsCompatibleMesh(ResolvedMesh))return Invalid(TEXT("animation class mesh skeleton"));
  }
  if(ResolvedAnimationClass->IsChildOf(UEGCombatAnimInstance::StaticClass()))
  {
   const auto ValidSlot=[&](const TSoftObjectPtr<UAnimMontage>& Ref)
   {
    if(Ref.IsNull())return true;
    const auto* Montage=Ref.LoadSynchronous();
    return Montage && Montage->GetSkeleton() && Montage->GetSkeleton()->IsCompatibleMesh(ResolvedMesh)
      && Montage->SlotAnimTracks.ContainsByPredicate([](const FSlotAnimationTrack& Slot){return Slot.SlotName==TEXT("FullBody");});
   };
   if(!ValidSlot(GetAttackMontage())||!ValidSlot(GetSkillMontage()))return Invalid(TEXT("montage skeleton or FullBody slot"));
   if(PresentationDefinition)
   {
    for(const auto& Entry:PresentationDefinition->AttackSequence)if(!ValidSlot(Entry.Montage))return Invalid(TEXT("attack montage skeleton or FullBody slot"));
    for(const auto& Entry:PresentationDefinition->UltimateSequence)if(!ValidSlot(Entry.Montage))return Invalid(TEXT("ultimate montage skeleton or FullBody slot"));
   }
  }
 }
 if(!bCheckAnimation||(HeroKind==EEGHeroKind::Generic&&!MonsterAttack))return true;
 auto* Attack=GetAttackAnimation().LoadSynchronous();auto* Skill=GetSkillAnimation().LoadSynchronous();
 if(!Attack||(!bElysiaBasicOnly&&(!GetIdleAnimation().LoadSynchronous()||!GetDeathAnimation().LoadSynchronous())))return Invalid(TEXT("required animation"));
 auto HasEvent=[](const UAnimSequenceBase* Clip,FName Name)
 {return Clip&&Clip->Notifies.ContainsByPredicate([Name](const FAnimNotifyEvent& N){const auto* Event=Cast<UEGSkillNotify>(N.Notify);return Event&&Event->Event==Name;});};
 const UAnimSequenceBase* AttackAsset=GetAnimationClass().IsNull()?static_cast<UAnimSequenceBase*>(Attack):GetPresentationMontage(EGPresentationAction::Attack);
 if(!HasEvent(AttackAsset,HeroKind==EEGHeroKind::VillV?TEXT("Fire1"):TEXT("Hit")))return Invalid(TEXT("attack Hit notify"));
 if(MonsterAttack&&!HasEvent(AttackAsset,TEXT("Finish")))return Invalid(TEXT("monster Finish notify"));
 if((!bElysiaBasicOnly||Skill)&&(GetUltimateAbility()||(MonsterAttack&&MonsterAttack->Ultimate!=EEGMonsterUltimate::None))&&HeroKind!=EEGHeroKind::Can)
 {
  const UAnimSequenceBase* SkillAsset=GetAnimationClass().IsNull()?static_cast<UAnimSequenceBase*>(Skill):GetPresentationMontage(EGPresentationAction::Skill);
  if(HeroKind==EEGHeroKind::VillV?(!HasEvent(SkillAsset,TEXT("Switch"))&&!HasEvent(SkillAsset,TEXT("Depart"))&&!HasEvent(SkillAsset,TEXT("Fire1"))):!HasEvent(SkillAsset,TEXT("Release")))return Invalid(TEXT("skill Release notify"));
 }
 if(PresentationDefinition)
 {
  const auto* P=PresentationDefinition.Get();
  for(const auto& Entry:P->AttackSequence)
  {
   auto* Clip=Entry.Animation.LoadSynchronous();auto* M=Entry.Montage.LoadSynchronous();
   if(!Clip||!M||Clip->GetSkeleton()!=Attack->GetSkeleton()||M->GetSkeleton()!=Attack->GetSkeleton()||!HasEvent(M,TEXT("Finish"))||!HasEvent(M,HeroKind==EEGHeroKind::VillV?TEXT("Fire1"):TEXT("Hit")))return Invalid(TEXT("attack sequence"));
  }
  for(int32 Index=0;Index<P->UltimateSequence.Num();++Index)
  {
   const auto& Entry=P->UltimateSequence[Index];
   auto* Clip=Entry.Animation.LoadSynchronous();auto* M=Entry.Montage.LoadSynchronous();
   if(!Clip||!M||Clip->GetSkeleton()!=Attack->GetSkeleton()||M->GetSkeleton()!=Attack->GetSkeleton())return Invalid(TEXT("ultimate sequence skeleton"));
   if(HeroKind==EEGHeroKind::VillV)
   {if(!HasEvent(M,TEXT("Depart"))&&!(HasEvent(M,TEXT("Fire1"))&&HasEvent(M,TEXT("Finish")))&&!HasEvent(M,TEXT("Switch")))return Invalid(TEXT("Vill-V transition notifies"));}
   // Human's first ultimate enters Origin at Release; the second is the
   // finisher, whose behavior commits on Hit (or the legacy Release).
   else if(HeroKind==EEGHeroKind::ElysiaHerrscher&&Index==1)
   {if(!HasEvent(M,TEXT("Finish"))||(!HasEvent(M,TEXT("Hit"))&&!HasEvent(M,TEXT("Release"))))return Invalid(TEXT("Human finisher sequence"));}
   else if(!HasEvent(M,TEXT("Finish"))||!HasEvent(M,TEXT("Release")))return Invalid(TEXT("ultimate sequence"));
  }
  if(HeroKind==EEGHeroKind::VillV)
  {
   const auto* V=Cast<UEGVillVSkillDefinition>(SkillDefinition);
   if(!V||!V->GatlingDefinition.LoadSynchronous()||!V->CannonDefinition.LoadSynchronous()||V->EntryActions.Num()!=4||V->EntryBodies.Num()!=4||V->DeviceSpawns.Num()!=2||V->DeviceIdles.Num()!=2||V->DeviceAttacks.Num()!=2||V->SeatedBodies.Num()!=2)return Invalid(TEXT("Vill-V two persistent devices"));
   if(P->AttackSequence.Num()!=2||P->UltimateSequence.Num()!=4||P->AttackAssemblies.Num()!=2||P->UltimateAssemblies.Num()!=4||!P->IdleAssemblies.IsEmpty())return Invalid(TEXT("Vill-V pilot presentations"));
   for(const auto& E:V->EntryActions)if(!HasEvent(E.Montage.LoadSynchronous(),TEXT("Dock"))||!HasEvent(E.Montage.LoadSynchronous(),TEXT("Finish")))return Invalid(TEXT("Vill-V docking notifies"));
   if(V->JumpActions.Num()!=4||V->JumpBodies.Num()!=4||V->JumpProgressCurves.Num()!=2||!V->JumpProgressCurves[0]||!V->JumpProgressCurves[1]||!FMath::IsFinite(V->AimDegreesPerSecond)||V->AimDegreesPerSecond<=0||!FMath::IsFinite(V->AimTolerance)||V->AimTolerance<=0)return Invalid(TEXT("Vill-V source jumps and aim"));
   for(int32 I=0;I<V->JumpActions.Num();++I)
   {
    const auto& E=V->JumpActions[I];auto* Clip=E.Animation.LoadSynchronous();auto* M=E.Montage.LoadSynchronous();auto* Body=V->JumpBodies[I].LoadSynchronous();
    if(!Clip||!M||!Body||Body->Motions.IsEmpty()||Clip->GetSkeleton()!=Attack->GetSkeleton()||M->GetSkeleton()!=Attack->GetSkeleton()||!HasEvent(M,TEXT("Dock"))||!HasEvent(M,TEXT("Finish"))||(I>=2&&!HasEvent(M,TEXT("Fire1"))))return Invalid(TEXT("Vill-V jump skeleton or notifies"));
   }
   if(!HasEvent(P->AttackSequence[0].Montage.LoadSynchronous(),TEXT("Fire2"))||!HasEvent(P->AttackSequence[0].Montage.LoadSynchronous(),TEXT("Fire3")))return Invalid(TEXT("Vill-V burst notifies"));
  }
 }
 for(const auto& Clip:{GetIdleAnimation(),GetWalkAnimation(),GetRunAnimation(),GetDeathAnimation(),GetHitAnimation(),GetSkillAnimation()})
  if(!Clip.IsNull())if(auto* Asset=Clip.LoadSynchronous();!Asset||Asset->GetSkeleton()!=Attack->GetSkeleton())return Invalid(TEXT("animation skeleton"));
 return true;
}
#if WITH_EDITOR
EDataValidationResult UEGHeroDefinition::IsDataValid(FDataValidationContext& Context) const
{
 const auto Parent=Super::IsDataValid(Context);FString Error;
 if(!ValidateDefinition(Error,true)){Context.AddError(FText::FromString(Error));return EDataValidationResult::Invalid;}
 return Parent==EDataValidationResult::Invalid?Parent:EDataValidationResult::Valid;
}
#endif

TSubclassOf<UEGHeroSkillBehavior> UEGHeroDefinition::GetBehaviorClass() const
{
 if(MonsterAttack)return UEGMonsterSkillBehavior::StaticClass();
 if(!bCardSkillsEnabled)return nullptr;
 if(bUseSplitConfiguration){check(SkillDefinition);return SkillDefinition->BehaviorClass;}
 if(HeroKind==EEGHeroKind::Can)return UEGCanSkillBehavior::StaticClass();
 return HeroKind==EEGHeroKind::Pardofelis?UEGPardoSkillBehavior::StaticClass():nullptr;
}
