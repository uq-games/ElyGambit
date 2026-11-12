#include "UI/EGUnitInformation.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Skills/EGConfirmedSkillBehaviors.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"

FString EGUnitInformation::Status(const AEGChessUnit* Unit)
{
 if(!IsValid(Unit)||!Unit->GetDefinition()||!Unit->GetHeroSkill())return FString();
 const auto* Skill=Unit->GetHeroSkill();const auto* Battle=Skill->Battle.Get();
 if(!Battle||!Battle->IsBattleActive()||Skill->bSummon)return FString();
 FEGElysiaPassiveState State;
 bool bElysia=false;
 if(const auto* P=Cast<UEGElysiaSkillBehavior>(Skill->GetBehavior())){State=P->GetPassiveState();bElysia=true;}
 else if(const auto* HumanBehavior=Cast<UEGElysiaHerrscherSkillBehavior>(Skill->GetBehavior())){State=HumanBehavior->GetPassiveState();bElysia=true;}
 if(bElysia)
 {
  switch(State.Phase)
  {
  case EEGElysiaPassivePhase::Normal:
   return FString::Printf(TEXT("蓄力进度 %d / %d%s"),State.Progress,State.Threshold,State.bNextAttackEnhanced?TEXT("\n下一击蓄力"):TEXT(""));
  case EEGElysiaPassivePhase::Empowered:
   return FString::Printf(TEXT("穹顶强化 剩余 %.1f 秒"),FMath::Max(0.f,State.RemainingSeconds));
  case EEGElysiaPassivePhase::Bow:
   return FString::Printf(TEXT("贯穿进度 %d / %d%s"),State.Progress,State.Threshold,State.bNextAttackEnhanced?TEXT("\n下一击贯穿"):TEXT(""));
  case EEGElysiaPassivePhase::Entering:return TEXT("正在进入始源");
  case EEGElysiaPassivePhase::Origin:
   return FString::Printf(TEXT("始源形态 · 剩余 %.1f 秒"),FMath::Max(0.f,State.RemainingSeconds));
  case EEGElysiaPassivePhase::FinisherPending:return TEXT("始源终结技准备中");
  case EEGElysiaPassivePhase::Finishing:return TEXT("始源终结技");
  default:return FString();
  }
 }
 if(const auto* P=Cast<UEGPardoSkillBehavior>(Skill->GetBehavior()))
  return FString::Printf(TEXT("崩坏能 %d/%d %s\n罐头%s"),P->GetCoinProgress(),Unit->GetDefinition()->GetSkillRules().CoinAttackThreshold,P->HasAwardedCoin()?TEXT("已领取"):TEXT(""),Battle->HasLivingCan(Unit->GetInstanceId())?TEXT("在场"):TEXT("未在场"));
 return FString();
}
