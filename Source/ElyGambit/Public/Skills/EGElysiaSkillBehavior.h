#pragma once
#include "CoreMinimal.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Skills/EGElysiaPassiveState.h"
#include "EGElysiaSkillBehavior.generated.h"
class AEGElysiaCombatVisual;
/** Four ordinary shots followed by one charged shot; the ultimate rains on a locked area. */
UCLASS()
class ELYGAMBIT_API UEGElysiaSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual bool AllowsAction(bool Ultimate) const override {return !Ultimate||(!IsArrowRainActive()&&!IsEmpowered());}
 virtual int32 PresentationIndex(bool Ultimate,int32 DefaultIndex) const override;
 virtual float BasicCycleScale() const override {return bLastCharged?1.2f:1.f;}
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool Completed) override;
 virtual void TickAction(float Position) override;
 virtual void TickBattle(float DeltaSeconds) override;
 void SuspendBattleClock(float DeltaSeconds);
 virtual void EndBattle() override;
 UFUNCTION(BlueprintPure,Category="Elysia|Status") FEGElysiaPassiveState GetPassiveState() const;
 /** The actual world-space field radius used by damage resolution. */
 UFUNCTION(BlueprintPure,Category="Elysia|Status") float GetEmpoweredRadius() const;
 /** Cosmetic consumers use the same locked ground point, world radius and lifetime as damage. */
 UFUNCTION(BlueprintPure,Category="Elysia|Arrow Rain") bool IsArrowRainActive() const {return RainActionId.IsValid();}
 UFUNCTION(BlueprintPure,Category="Elysia|Arrow Rain") FVector GetArrowRainCenter() const {return RainCenter;}
 UFUNCTION(BlueprintPure,Category="Elysia|Arrow Rain") float GetArrowRainRadius() const {return RainRadius;}
 UFUNCTION(BlueprintPure,Category="Elysia|Arrow Rain") float GetArrowRainRemaining() const;
 UFUNCTION(BlueprintPure,Category="Elysia|Arrow Rain") int32 GetArrowRainPulseCount() const {return RainPulseIndex;}
 UFUNCTION(BlueprintPure) int32 GetSeedStacks(AEGChessUnit* Target) const;
 UFUNCTION(BlueprintPure) int32 GetPendingHitCount() const {return Pending.Num();}
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 PrimaryHitCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 ChargedHitCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 RicochetHitCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 SeedsApplied=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 NormalCursor=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) float DomeRemaining=0;
private:
 struct FHit {TWeakObjectPtr<AEGChessUnit> Target;TWeakObjectPtr<AEGElysiaCombatVisual> Visual;FGuid Id;double DueTime=0;float Attack=0;bool Charged=false;bool Ricochet=false;int32 Tier=0;int32 AttackIndex=INDEX_NONE;};
 TArray<FHit> Pending;
 TMap<TWeakObjectPtr<AEGChessUnit>,int32> Seeds;
 TMap<TWeakObjectPtr<AEGChessUnit>,TWeakObjectPtr<AEGElysiaCombatVisual>> SeedVisuals;
 UPROPERTY(Transient) TObjectPtr<AEGElysiaCombatVisual> DomeVisual;
 int32 EmpoweredCursor=0;
 bool bActionEmpowered=false,bStatusDirty=false;
 struct FDomeWindow {double Start=0,End=0;FVector Center=FVector::ZeroVector;};
 TArray<FDomeWindow> DomeWindows;
 double DomeStartTime=0,DomeEndTime=0;
 bool IsEmpowered() const;
 bool UsesArrowRain() const;
 void ExpireEmpowerment();
 FVector DomeCenter=FVector::ZeroVector;
 FGuid RainActionId;
 double RainStartTime=0.,RainEndTime=0.,RainInterval=0.;
 int32 RainPulseIndex=0,RainPulseLimit=0;
 float RainRadius=0.f,RainDamage=0.f;
 FVector RainCenter=FVector::ZeroVector;
 void EndArrowRain(bool bNatural);
 void ResolveRainPulse();
 UPROPERTY(Transient) TArray<TObjectPtr<AEGElysiaCombatVisual>> Transients;
 TMap<TWeakObjectPtr<AEGElysiaCombatVisual>,FGuid> VisualActions;
 UPROPERTY(Transient) TObjectPtr<AEGElysiaCombatVisual> ChargeVisual;
 TSet<FGuid> Fired;
 FGuid ActiveAction;
 FVector UltimatePoint=FVector::ZeroVector;
 FVector UltimateGroundPoint=FVector::ZeroVector;
 bool bLastUltimate=false,bLastCharged=false;
 // Opt-in montage event; legacy/formal montages without BoardBackstep stay stationary.
 bool bBoardBackstep=false;
 FVector BackstepStart=FVector::ZeroVector,BackstepEnd=FVector::ZeroVector;
 float BackstepPosition=0;
 void Resolve(const FHit& Hit);
 void TrackVisual(AEGElysiaCombatVisual* Visual,FGuid Action);
 void ClearVisuals();
};
