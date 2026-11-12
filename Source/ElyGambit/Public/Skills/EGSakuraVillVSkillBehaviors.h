#pragma once
#include "Skills/EGHeroSkillBehavior.h"
#include "EGSakuraVillVSkillBehaviors.generated.h"
class AEGDescentRing;
class UEGVillVMechanismComponent;
class AEGChessNiagaraBurst;
class AEGSourceEffect;
class UEGSourceEffectDefinition;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FEGSakuraEvaded,AEGChessUnit*,Attacker,FGuid,AttackId);

UCLASS()
class ELYGAMBIT_API UEGSakuraSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle) override;
 virtual void ActionStarted(const FEGSkillActionContext& C) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& C) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void TickBattle(float DeltaSeconds) override;
 virtual bool EvadeDirectAttack(AEGChessUnit* Source,FGuid ActionId,bool bBasicAttack,bool& bNewlyEvaded) override;
 virtual void OnEvadeCommitted(AEGChessUnit* Source,FGuid ActionId) override;
 virtual void EndBattle() override;
 UPROPERTY(BlueprintAssignable,Category="Sakura") FEGSakuraEvaded OnOwnerEvaded;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Sakura") float EvadeCooldownRemaining=0.f;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Sakura") bool bDelayedAttackReady=false;
private:
 struct FPendingSlash
 {
  FGuid Action,Round;
  TArray<TWeakObjectPtr<AEGChessUnit>> Targets;
  float Remaining=0.f,Damage=0.f;
  int32 AttackIndex=INDEX_NONE;
  bool bUltimate=false,bKillMana=false;
  FVector Origin=FVector::ZeroVector,Forward=FVector::ForwardVector;
 };
 TArray<FPendingSlash> PendingSlashes;
 TSet<FGuid> EvadedActions;
 FGuid ResolvingAction;
 bool bResolvingCancelled=false;
 void CancelPending(FGuid Action);
 void ResolvePending(FPendingSlash Pending);
 UFUNCTION() void OwnerAttributeChanged(FName Attribute,float OldValue,float NewValue);
 bool PlayConfiguredEffect(const TSoftObjectPtr<UEGSourceEffectDefinition>& Effect,const FVector& Point,const FRotator& Rotation,FGuid Action,FGuid Round,bool bExecution=false,bool bAllowCompleted=false);
 void ClearConfiguredEffects();
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> ConfiguredEffects;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> ExecutionEffects;
 FGuid ActiveUltimateAction;
 FVector UltimateOrigin=FVector::ZeroVector;
 FVector UltimateForward=FVector::ForwardVector;
 bool bSlashSubmitted=false;
 bool bFanStarted=false;
};

UCLASS()
class ELYGAMBIT_API UEGVillVSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle) override;
 virtual int32 PresentationIndex(bool bUltimate,int32 DefaultIndex) const override;
 virtual bool BlocksCombat() const override;
 virtual bool AllowsAction(bool bUltimate) const override;
 virtual void BeforeActionPresentation() override;
 virtual void TickAction(float Position) override;
 virtual int32 IdlePresentationIndex() const override {return 0;}
 virtual float BasicCycleScale() const override {return IsCannon()?1.6f:1.f;}
 virtual void ActionStarted(const FEGSkillActionContext& C) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& C) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void EndBattle() override;
 bool IsCannon() const;
 bool HasEnhancedShot() const;
private:
 UPROPERTY(Transient) TObjectPtr<UEGVillVMechanismComponent> Mechanism;
 TSet<FName> Fired;
 TArray<FName> PendingFires;
 FEGSkillActionContext FiringContext;
 void FlushFire();
};
