#pragma once
#include "Skills/EGHeroSkillBehavior.h"
#include "EGGriseoAdultSkillBehavior.generated.h"
class AEGChessNiagaraBurst;

UCLASS()
class ELYGAMBIT_API UEGGriseoAdultSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle) override;
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void TickAction(float Position) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void EndBattle() override;
private:
 FGuid ActiveAction;
 FVector UltimatePoint=FVector::ZeroVector;
 bool bSubmitted=false;
 float MeteorReleaseTime=0.f;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessNiagaraBurst> MeteorVisual;
};
