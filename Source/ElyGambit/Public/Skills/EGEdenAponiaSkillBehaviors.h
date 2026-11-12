#pragma once
#include "Skills/EGHeroSkillBehavior.h"
#include "EGEdenAponiaSkillBehaviors.generated.h"
class AEGSourceEffect;
class UEGSourceEffectDefinition;
class AEGAponiaDisciplineVisual;
class AEGChessNiagaraBurst;

/** Shared presentation lifetime only; all damage still uses the battle's GAS path. */
UCLASS(Abstract)
class ELYGAMBIT_API UEGMusicalJudgmentBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionStarted(const FEGSkillActionContext& C) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void EndBattle() override;
protected:
 void PresentAt(const TSoftObjectPtr<UEGSourceEffectDefinition>& FX,AEGChessUnit* Target);
 void BasicHit(const FEGSkillActionContext& C);
 bool IsOtherHero(AEGChessUnit* Source) const;
private:
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> Effects;
};

UCLASS()
class ELYGAMBIT_API UEGEdenSkillBehavior : public UEGMusicalJudgmentBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionStarted(const FEGSkillActionContext& C) override;
 virtual void TickAction(float Position) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& C) override;
 virtual void OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId) override;
 virtual void TickBattle(float DeltaSeconds) override;
 virtual void EndBattle() override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Eden") int32 Notes=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Eden") int32 NotesPaid=0;
private:
 void PayNote();
 double EncoreEnd=0;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessNiagaraBurst> ShotVisual;
 TWeakObjectPtr<AEGChessUnit> ShotTarget;
 FVector ShotStart=FVector::ZeroVector;
 float ShotHitTime=0.f;
};

UCLASS()
class ELYGAMBIT_API UEGAponiaSkillBehavior : public UEGMusicalJudgmentBehavior
{
 GENERATED_BODY()
public:
 virtual void OnBattleConfigured() override;
 virtual void TickAction(float Position) override;
 virtual void ActionStarted(const FEGSkillActionContext& C) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& C) override;
 virtual void AllyBasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid ActionId) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void EndBattle() override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Aponia") bool bJudging=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Aponia") int32 LightningCount=0;
 virtual void TickBattle(float DeltaSeconds) override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Aponia") int32 DisciplineStacks=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Aponia") int32 DisciplineSnapshot=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Aponia") int32 PenaltiesReleased=0;
 int32 GetVisibleDiscipline() const {return bDisciplineConsumed?FMath::Max(0,DisciplineSnapshot-PenaltiesReleased):DisciplineStacks;}

private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGAponiaDisciplineVisual> DisciplineVisual;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessNiagaraBurst> WingsVisual;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessNiagaraBurst> BasicVisual;
 TWeakObjectPtr<AEGChessUnit> BasicTarget;
 FVector BasicStart=FVector::ZeroVector;
 float BasicHitTime=0.f;
 bool ConsumeDiscipline(const FEGSkillActionContext& Context);
 void EmitPenalty();
 TSet<FGuid> GainedAttacks;
 bool bDisciplineConsumed=false,bVerdictReleased=false;
 float PenaltyRemaining=0.f,CastBudget=0.f;
 double NextLightning=0;
 float CastAttack=0,CastPenalty=0;
 FVector VerdictPoint=FVector::ZeroVector;
 FGuid CastId;
 TSet<FGuid> JudgedAttacks;
};
