#include "Skills/EGHeroSkillBehavior.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "NiagaraSystem.h"
#include "UObject/UnrealType.h"

void UEGHeroSkillBehavior::Initialize(UEGHeroSkillComponent* InSkill, UEGBattleContextComponent* InBattle)
{
 Skill=InSkill;Unit=InSkill?Cast<AEGChessUnit>(InSkill->GetOwner()):nullptr;Battle=InBattle;
 RoundId=InBattle?InBattle->GetRoundId():FGuid();
 ResidentNiagara.Reset();
 if(Unit.IsValid()&&Unit->GetNetMode()!=NM_DedicatedServer&&Unit->GetDefinition())
  if(const auto* Data=Unit->GetDefinition()->SkillDefinition.Get())
   for(TFieldIterator<FSoftObjectProperty> It(Data->GetClass());It;++It)
    if(It->PropertyClass&&It->PropertyClass->IsChildOf(UNiagaraSystem::StaticClass()))
    {
     const FSoftObjectPtr Ref=It->GetPropertyValue_InContainer(Data);
     if(auto* System=Cast<UNiagaraSystem>(Ref.ToSoftObjectPath().TryLoad()))ResidentNiagara.AddUnique(System);
    }
}
bool UEGHeroSkillBehavior::IsCurrentBattle() const
{
 return Skill.IsValid()&&Unit.IsValid()&&Battle.IsValid()&&Battle->IsBattleActive()&&RoundId.IsValid()&&Battle->GetRoundId()==RoundId;
}
void UEGHeroSkillBehavior::EndBattle()
{
 RoundId.Invalidate();Skill.Reset();Unit.Reset();Battle.Reset();
 ResidentNiagara.Reset();
}
