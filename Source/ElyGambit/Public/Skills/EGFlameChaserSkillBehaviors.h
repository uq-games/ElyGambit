#pragma once
#include "Skills/EGHeroSkillBehavior.h"
#include "EGFlameChaserSkillBehaviors.generated.h"
class AEGMobiusLinkVisual;
class AEGMobiusHeadProjection;
class AEGCombatVisual;
class AEGDescentRing;
class AEGMobiusSourceAttackVisual;
class AEGMobiusNiagaraEffect;
class AEGSourceEffect;
class UEGSourceEffectDefinition;
class UNiagaraSystem;
class AEGChessNiagaraBurst;

UCLASS()
class ELYGAMBIT_API UEGHuaSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void TickAction(float Position) override;
 virtual bool TryPreventDeath() override;
private:
 bool bAirborne=false;
 FVector LeapStart=FVector::ZeroVector,Landing=FVector::ZeroVector;
 UPROPERTY(Transient) TObjectPtr<AEGDescentRing> LandingRing;
 UPROPERTY(Transient) TObjectPtr<AEGChessNiagaraBurst> LandingFX;
};

UCLASS()
class ELYGAMBIT_API UEGCollapsedHuaSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual bool TryPreventDeath() override;
private:
 bool bBurned=false,bReleased=false;
 float BurnedHealth=0.f;
 UPROPERTY(Transient) TObjectPtr<AEGDescentRing> BurnRing;
 UPROPERTY(Transient) TObjectPtr<AEGChessNiagaraBurst> ChargeFX;
};

UCLASS()
class ELYGAMBIT_API UEGMobiusSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 UEGMobiusSkillBehavior();
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void TickAction(float Position) override;
 virtual void OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId) override;
 virtual void DamageResolved(const FEGCombatResolution& Receipt) override;
 virtual void EndBattle() override;
 bool IsLinkedTarget(const AEGChessUnit* Target) const;
 AEGChessUnit* LowestLinkedTarget() const;
private:
 void PresentWorkshopEffect(UEGSourceEffectDefinition* Definition,const FVector& Point,const FRotator& Rotation,float Scale);
 void ClearWorkshopEffects();
 UPROPERTY() TArray<TObjectPtr<UEGSourceEffectDefinition>> WorkshopBasicAccents;
 UPROPERTY() TArray<TObjectPtr<UEGSourceEffectDefinition>> WorkshopSpikes;
 UPROPERTY() TObjectPtr<UEGSourceEffectDefinition> WorkshopUltimateAura;
 UPROPERTY() TObjectPtr<UEGSourceEffectDefinition> WorkshopUltimateImpact;
 UPROPERTY() TObjectPtr<UEGSourceEffectDefinition> WorkshopEmpoweredImpact;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> WorkshopEffects;
 FGuid WorkshopAction;
 bool bWorkshopAuraPresented=false;
 TArray<TWeakObjectPtr<AEGChessUnit>> Members;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGMobiusHeadProjection>> HeadProjections;
 float LinkRemaining=0;
 bool bGroup=false;
 FVector CastPoint=FVector::ZeroVector;
 UPROPERTY(Transient) TObjectPtr<AEGMobiusLinkVisual> LinkVisual;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> RecoveryVisual;
 bool bSerpentCast=false,bStormAttack=false,bReleased=false,bRecoveryEffectPresented=false;
 void StopSerpent();
 void PresentStorm(AEGChessUnit* Target,FVector Point);
public:
 virtual void TickBattle(float DeltaSeconds) override;
 virtual bool OverridesBodyPresentation() const override {return false;}
 virtual bool BlocksCombat() const override {return false;}
 virtual bool AllowsAction(bool bUltimate) const override {return true;}
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Empowered") bool EmpoweredActive=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Empowered") float EmpoweredRemaining=0;
 /** Legacy diagnostics: no persistent serpent is created. */
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Serpent") bool SerpentActive=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Serpent") float SerpentRemaining=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Serpent") int32 StormCount=0;
private:
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> SnakeVisual;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> UltimateSourceVisual;
 UPROPERTY(Transient) TObjectPtr<AEGMobiusSourceAttackVisual> BasicVisual;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGMobiusSourceAttackVisual>> BasicImpacts;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGMobiusNiagaraEffect>> NiagaraEffects;
 void PresentNiagara(UNiagaraSystem* System,const FVector& Location,const FRotator& Rotation,float Duration,bool bActionBound=false,AEGChessUnit* Target=nullptr);
 UPROPERTY(Transient) TObjectPtr<AEGChessNiagaraBurst> CastWarning;
 UPROPERTY(Transient) TObjectPtr<AEGDescentRing> CastRing;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGMobiusLinkVisual>> Discharges;
 void PresentDischarge(const TArray<AEGChessUnit*>& Targets,float Duration);
 void SetLink(const TArray<AEGChessUnit*>& Targets,bool Group);
 bool LinkActive() const;
};
