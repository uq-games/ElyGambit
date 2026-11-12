#pragma once
#include "CoreMinimal.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "EGMonsterSkillBehavior.generated.h"

enum class EEGMonsterEffectCue : uint8 { Shield, Heal, TeamSpeed };
DECLARE_MULTICAST_DELEGATE_SixParams(FEGMonsterEffectEvent,EEGMonsterEffectCue,AEGChessUnit*,AEGChessUnit*,FGuid,FGuid,float);

/** Shares the existing notify transaction, round guards, cancellation and GAS damage pipeline. */
UCLASS()
class ELYGAMBIT_API UEGMonsterSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 FEGMonsterEffectEvent OnMonsterEffect;
 virtual void Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle) override;
 virtual void EndBattle() override;
 virtual bool AllowsAction(bool bUltimate) const override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
private:
 bool bPendingUltimate=false;
};
