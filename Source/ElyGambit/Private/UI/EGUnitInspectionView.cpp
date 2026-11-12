#include "UI/EGUnitInspectionView.h"
#include "UI/EGUnitInformation.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGHeroRoster.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Skills/EGConfirmedSkillBehaviors.h"

namespace
{
/** Keep only effects unlocked for this copy, without presenting player level milestones. */
FText CopySkillText(const FText& Source,int32 Level)
{
 TArray<FString> Lines;Source.ToString().ParseIntoArrayLines(Lines,false);
 FString Result;bool bInclude=true;
 for(const FString& Line:Lines)
 {
  if(Line==TEXT("6级")){bInclude=Level>=6;continue;}
  if(Line==TEXT("11级")){bInclude=Level>=11;continue;}
  if(bInclude){if(!Result.IsEmpty())Result+=TEXT("\n");Result+=Line;}
 }
 return FText::FromString(Result.TrimStartAndEnd());
}
}

FEGUnitInspectionView FEGUnitInspectionView::Capture(const AEGChessUnit* Unit,const UEGHeroRoster* Roster,FGuid HeroId)
{
 FEGUnitInspectionView View;
 if(IsValid(Unit)&&Unit->GetDefinition()&&Unit->GetHeroSkill()&&Unit->GetAttributes())
 {
  View.Definition=Unit->GetDefinition();View.Level=Unit->GetHeroSkill()->GetLevel();
  const auto* BossAttackRules=Cast<UEGElysiaSkillDefinition>(View.Definition->SkillDefinition);
  const FString BossBasicText=BossAttackRules?FString::Printf(TEXT("射击目标，造成%.0f%%攻击力伤害。"),BossAttackRules->NormalAttackMultiplier*100.f):FString(TEXT("射击目标，造成攻击伤害。"));
  View.bEnemy=Unit->GetFaction_Implementation()==EEGFaction::Enemy;
  View.bAwakened=Unit->GetHeroSkill()->bAwakened;View.bCombat=true;
  const auto* Attributes=Unit->GetAttributes();
  View.Stats.MaxHealth=Attributes->GetMaxHealth();View.Stats.MaxMana=Attributes->GetMaxMana();
  View.Stats.AttackPower=Attributes->GetAttackPower();View.Stats.Defense=Attributes->GetDefense();
  View.Stats.AttackSpeed=Attributes->GetAttackSpeed();View.Health=Attributes->GetHealth();
  View.Mana=Attributes->GetMana();View.Shield=Unit->GetHeroSkill()->Shield;
  View.bSummonUltimateUnavailable=Unit->GetHeroSkill()->bSummon&&!Unit->GetHeroSkill()->bAllowSummonUltimate;
  View.Status=EGUnitInformation::Status(Unit);
  if(View.bEnemy&&View.Definition->HeroKind==EEGHeroKind::Pardofelis&&View.Status.StartsWith(TEXT("崩坏能 ")))
  {
   int32 Newline=INDEX_NONE;
   View.Status=View.Status.FindChar(TEXT('\n'),Newline)?View.Status.Mid(Newline+1):FString();
  }
  if(const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Unit->GetHeroSkill()->GetBehavior()))
  {
   if(const auto* Rules=Cast<UEGElysiaHerrscherSkillDefinition>(View.Definition->SkillDefinition);Rules&&Human->IsOriginActive())
   {
    View.bHasSkillOverride=true;
    View.SkillBasicOverride=FText::FromString(FString::Printf(TEXT("挥动权杖，分三段对目标共造成%.0f%%攻击力伤害，并对其周围%d格内的其他敌人共造成%.0f%%攻击力伤害。"),Rules->GetScepterMain(View.Level)*100.f,Rules->ScepterSplashRadiusCells,Rules->GetScepterSplash(View.Level)*100.f));
    View.SkillPassiveOverride=FText::FromString(TEXT("始源期间免疫控制，无法获取或储存法力。结束后返回弓形态，法力与普攻计数清零。"));
    View.SkillUltimateOverride=View.Level>=11?FText::FromString(FString::Printf(TEXT("始源自然结束时自动释放：召唤鳐鱼，对所有敌人造成%.0f%%攻击力伤害，随后返回弓形态。"),Rules->FinisherMain*100.f)):FText::FromString(TEXT("始源结束后返回弓形态。\n\n11级\n解锁鳐鱼终结，始源自然结束时自动释放。"));
   }
  }
  if(const auto* Boss=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();Boss&&Boss->IsBoss())
  {
   View.bHasSkillOverride=true;
   const auto Phase=Boss->GetBossPhase();
   switch(Phase)
   {
   case EEGCorruptionBossPhase::First:View.Status=TEXT("侵蚀首阶段");break;
   case EEGCorruptionBossPhase::Barrier:View.Status=TEXT("侵蚀壁垒阶段");break;
   case EEGCorruptionBossPhase::Second:View.Status=TEXT("侵蚀第二阶段");break;
   default:View.Status=TEXT("侵蚀战斗已结束");break;
   }
   if(Phase==EEGCorruptionBossPhase::Barrier)
   {
    View.SkillBasicOverride=FText::FromString(TEXT("壁垒期间暂停攻击，进入第二阶段后改变。"));
    View.SkillUltimateOverride=FText::FromString(TEXT("壁垒期间暂停施放技能，进入第二阶段后改变。"));
   }
   else if(Phase==EEGCorruptionBossPhase::Second)
   {
    View.SkillBasicOverride=FText::FromString(FString::Printf(TEXT("远程射击叠加印记；同一目标累计%d次有效普攻后引爆周围区域，造成%.0f%%攻击力伤害。"),Boss->MarkHitThreshold,Boss->MarkDamageMultiplier*100.f));
    View.SkillUltimateOverride=FText::FromString(TEXT("定期施展侵蚀扩散并召唤残影，造成范围伤害或获得支援效果。"));
   }
   else if(Phase==EEGCorruptionBossPhase::First)
   {
    View.SkillBasicOverride=FText::FromString(BossBasicText);
    View.SkillUltimateOverride=FText::FromString(FString::Printf(TEXT("锁定目标位置，预警%.1f秒后对%.1f格范围造成%.0f%%攻击力伤害。"),Boss->FirstPulseWarningSeconds,Boss->FirstPulseRadiusCells,Boss->FirstPulseDamageMultiplier*100.f));
   }
   else
   {View.SkillBasicOverride=FText::FromString(TEXT("当前战斗已结束。"));View.SkillUltimateOverride=View.SkillBasicOverride;}
   View.SkillPassiveOverride=FText::FromString(FString::Printf(TEXT("第一条生命耗尽后展开壁垒，持续%.1f秒；打破后%.1f秒内受到伤害提高%.0f%%。超时对敌方队伍造成伤害。"),Boss->BarrierDurationSeconds,Boss->BreakWeaknessSeconds,(Boss->BreakDamageMultiplier-1.f)*100.f));
  }
  if(View.bEnemy&&View.Definition->HeroId==FName(TEXT("ElysiaCorruption")))
  {
   const auto* Attached=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
   const auto* Boss=Attached&&Attached->IsBoss()?Attached:GetDefault<UEGCorruptionCombatComponent>();
   const auto Phase=Attached&&Attached->IsBoss()?Boss->GetBossPhase():EEGCorruptionBossPhase::First;
   View.BossArtworkPhase=(Phase==EEGCorruptionBossPhase::Second||(Phase==EEGCorruptionBossPhase::Finished&&Boss->IsPhaseTwo()))?2:1;
   auto Add=[&View](const TCHAR* Id,const TCHAR* Title,const FString& Description,const TCHAR* Icon)
   {View.EnemySkills.Add({FName(Id),FText::FromString(Title),FText::FromString(Description),FString(TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/"))+Icon});};
   Add(TEXT("arrow"),TEXT("射击"),BossBasicText,TEXT("T_Boss_Arrow"));
   Add(TEXT("pulse"),TEXT("侵蚀脉冲"),FString::Printf(TEXT("锁定目标，预警%.1f秒后对目标周围%.1f格内的对手造成%.0f%%攻击力伤害。"),Boss->FirstPulseWarningSeconds,Boss->FirstPulseRadiusCells,Boss->FirstPulseDamageMultiplier*100.f),TEXT("T_Boss_Pulse"));
   Add(TEXT("barrier"),TEXT("侵蚀壁垒"),FString::Printf(TEXT("第一条生命耗尽时展开壁垒，耐久为该生命的%.0f%%，持续%.1f秒。打破后%.1f秒内受到伤害提高%.0f%%；超时对敌方队伍造成各自最大生命%.0f%%的伤害。"),Boss->BarrierHealthRatio*100.f,Boss->BarrierDurationSeconds,Boss->BreakWeaknessSeconds,(Boss->BreakDamageMultiplier-1.f)*100.f,Boss->BarrierFailureHealthRatio*100.f),TEXT("T_Boss_Barrier"));
   Add(TEXT("mark"),TEXT("侵蚀印记"),FString::Printf(TEXT("普攻命中同一目标%d次后引爆，对其周围的对手造成%.0f%%攻击力伤害。"),Boss->MarkHitThreshold,Boss->MarkDamageMultiplier*100.f),TEXT("T_Boss_Mark"));
   Add(TEXT("spread"),TEXT("侵蚀扩散"),FString::Printf(TEXT("每%.1f秒预警并造成%.0f%%攻击力的范围伤害。"),Boss->SpreadIntervalSeconds,Boss->SpreadDamageMultiplier*100.f),TEXT("T_Boss_Spread"));
   Add(TEXT("mimic"),TEXT("侵蚀残影"),TEXT("从敌方队伍选取角色召唤残影。格蕾修提供护盾，伊甸造成范围伤害并回复法力，阿波尼亚攻击敌方全队；其他残影造成范围伤害。"),TEXT("T_Boss_Mimic"));
   Add(TEXT("enrage"),TEXT("狂暴"),TEXT("60秒后伤害提高10%，此后每5秒再提高10%。"),TEXT("T_Boss_Enrage"));
  }
  else if(View.bEnemy&&View.Definition->MonsterAttack)
  {
   const auto* Monster=View.Definition->MonsterAttack.Get();
   auto Add=[&View](const TCHAR* Id,EEGSkillInfoKind Kind,const FString& Description,const TCHAR* Icon)
   {View.EnemySkills.Add({FName(Id),View.Definition->GetPlayerFacingSkillSectionName(Kind),FText::FromString(Description),FString(TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/"))+Icon});};
   FString Basic;
   if(Monster->AttackType==EEGMonsterAttackType::Heal)Basic=FString::Printf(TEXT("治疗同阵营目标，恢复其最大生命的%.0f%%。"),Monster->HealHealthRatio*100.f);
   else Basic=Monster->AttackType==EEGMonsterAttackType::Melee?TEXT("近战攻击目标，造成攻击力等量的伤害。"):TEXT("向目标发射投射物，造成攻击力等量的伤害。");
   Add(TEXT("basic"),EEGSkillInfoKind::Basic,Basic,Monster->AttackType==EEGMonsterAttackType::Melee?TEXT("T_Enemy_Melee"):Monster->AttackType==EEGMonsterAttackType::Heal?TEXT("T_Enemy_Heal"):TEXT("T_Enemy_Shot"));
   if(Monster->Ultimate!=EEGMonsterUltimate::None)
   {
    FString Ultimate;
    switch(Monster->Ultimate)
    {
     case EEGMonsterUltimate::Shield:Ultimate=FString::Printf(TEXT("获得自身最大生命%.0f%%的护盾，持续%.1f秒。"),Monster->ShieldHealthRatio*100.f,Monster->ShieldSeconds);break;
     case EEGMonsterUltimate::EnhancedShot:Ultimate=FString::Printf(TEXT("发射强化弹，造成%.0f%%攻击力伤害。"),Monster->UltimateMultiplier*100.f);break;
     case EEGMonsterUltimate::TeamSpeed:Ultimate=FString::Printf(TEXT("使在场的同阵营单位（包括自身）攻速提高%.0f%%，持续%.1f秒。"),Monster->TeamSpeedBonus*100.f,Monster->TeamSpeedSeconds);break;
     case EEGMonsterUltimate::CrystalSeed:Ultimate=FString::Printf(TEXT("在目标位置生成水晶种，%.1f秒后对%.1f格内造成%.0f%%攻击力伤害。"),Monster->SeedDelay,Monster->SeedRadiusCells,Monster->UltimateMultiplier*100.f);break;
     default:break;
    }
    Add(TEXT("ultimate"),EEGSkillInfoKind::Ultimate,Ultimate,Monster->Ultimate==EEGMonsterUltimate::Shield?TEXT("T_Enemy_Shield"):Monster->Ultimate==EEGMonsterUltimate::EnhancedShot?TEXT("T_Enemy_EnhancedShot"):Monster->Ultimate==EEGMonsterUltimate::TeamSpeed?TEXT("T_Enemy_Speed"):TEXT("T_Enemy_Seed"));
   }
   if(View.Definition->bEliteEnemy)Add(TEXT("elite"),EEGSkillInfoKind::Passive,View.Definition->GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString(),TEXT("T_Enemy_Shield"));
  }
  else if(View.bEnemy)
  {
   const auto* Copy=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
   if(!Copy||!Copy->IsBoss())
   {
    for(const auto Kind:{EEGSkillInfoKind::Basic,EEGSkillInfoKind::Ultimate,EEGSkillInfoKind::Passive})
    {
     if(Kind==EEGSkillInfoKind::Ultimate&&View.bSummonUltimateUnavailable)continue;
     if(Kind==EEGSkillInfoKind::Passive&&View.Definition->HeroKind==EEGHeroKind::Pardofelis&&
        View.Definition->GetBehaviorClass()&&View.Definition->GetBehaviorClass()->IsChildOf(UEGPardoSkillBehavior::StaticClass()))continue;
     FText Description=CopySkillText(View.Definition->GetPlayerFacingSkillSection(Kind),View.Level);
     if(Kind==EEGSkillInfoKind::Passive&&View.Definition->HeroKind==EEGHeroKind::VillV)
      Description=FText::FromString(Description.ToString().Replace(TEXT("共占1人口。"),TEXT("")));
     if(Description.IsEmpty())continue;
     const TCHAR* Id=Kind==EEGSkillInfoKind::Basic?TEXT("copy_basic"):Kind==EEGSkillInfoKind::Ultimate?TEXT("copy_ultimate"):TEXT("copy_passive");
     const TCHAR* Icon=Kind==EEGSkillInfoKind::Basic?TEXT("T_Enemy_Shot"):Kind==EEGSkillInfoKind::Ultimate?TEXT("T_Boss_Pulse"):TEXT("T_Enemy_Shield");
     View.EnemySkills.Add({FName(Id),View.Definition->GetPlayerFacingSkillSectionName(Kind),Description,FString(TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/"))+Icon});
    }
    FString RoleText;
    if(Copy)switch(Copy->GetCopyRole())
    {
     case EEGCorruptionRole::Melee:RoleText=TEXT("造成有效伤害后回复自身生命，回复量为有效伤害的15%。");break;
     case EEGCorruptionRole::Ranged:RoleText=TEXT("大招造成伤害时，对目标周围其他对手追加相当于该伤害40%的伤害。");break;
     case EEGCorruptionRole::Support:RoleText=FString::Printf(TEXT("大招释放后，为周围其他同阵营单位回复%.0f点法力。"),Copy->SupportMana);break;
    }
    if(!RoleText.IsEmpty())View.EnemySkills.Add({TEXT("copy_role"),FText::FromString(TEXT("侵蚀特性")),FText::FromString(RoleText),TEXT("/Game/Assets/UI/SkillIcons/Enemies20261110/T_Boss_Mark")});
   }
  }
 }
 FEGHeroInstance Hero;
 if(IsValid(Roster)&&Roster->GetHero(HeroId,Hero))
 {
  View.CardsUsed=Hero.SameNameCards;
  if(!View.bCombat)
  {
   View.Definition=Roster->ResolveDefinition(HeroId);View.Level=Hero.Level;View.bAwakened=Hero.bAwakened;
   if(View.Definition){View.Stats=View.Definition->EvaluateStats(View.Level);View.Health=View.Stats.MaxHealth;}
  }
 }
 return View;
}
