#pragma once
#include "CoreMinimal.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "Skills/EGElysiaPassiveState.h"
#include "ActiveGameplayEffectHandle.h"
#include "EGElysiaHerrscherSkillBehavior.generated.h"

// Presentation consumes these authoritative transitions; they never advance combat.
enum class EEGElysiaHerrscherPresentationPhase : uint8
{ Bow, Entering, EntryReleased, Origin, FinisherPending, FinisherBegin, FinisherReleased, ReturnBow, BattleEnd };
DECLARE_MULTICAST_DELEGATE_ThreeParams(FEGElysiaHerrscherPresentationPhaseChanged,EEGElysiaHerrscherPresentationPhase,FGuid,FGuid);

struct FActiveGameplayEffect;
class UEGElysiaOriginKnockupComponent;

/** One combat incarnation owns the bow/origin phases and all their action tokens. */
UCLASS()
class ELYGAMBIT_API UEGElysiaHerrscherSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle) override;
 virtual void OnBattleConfigured() override;
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void TickBattle(float DeltaSeconds) override;
 void SuspendBattleClock(float DeltaSeconds);
 virtual void EndBattle() override;
 virtual bool AllowsAction(bool bUltimate) const override;
 virtual bool BlocksCombat() const override;
 virtual bool BlocksManaGain() const override;
 bool SuppressManaModifier(FActiveGameplayEffectHandle Handle);
 virtual bool IsControlImmune() const override {return true;}
 virtual int32 PresentationIndex(bool bUltimate,int32 DefaultIndex) const override;
 virtual int32 IdlePresentationIndex() const override;
 virtual int32 DeathPresentationIndex() const override;
 // Subscribers must remove their delegate when changing Behavior or ending play.
 FEGElysiaHerrscherPresentationPhaseChanged OnPresentationPhaseChanged;
 EEGElysiaHerrscherPresentationPhase GetPresentationPhase() const {return PresentationPhase;}
 FGuid GetPresentationRoundId() const {return RoundId;}
 FGuid GetPresentationActionId() const {return PresentationAction;}
 UFUNCTION(BlueprintPure,Category="Elysia|Herrscher") bool IsOriginActive() const;
 UFUNCTION(BlueprintPure,Category="Elysia|Herrscher") FEGElysiaPassiveState GetPassiveState() const;
 UFUNCTION(BlueprintPure,Category="Elysia|Herrscher") int32 GetBowAttackCount() const {return BowCursor;}
 UFUNCTION(BlueprintPure,Category="Elysia|Herrscher") int32 GetOriginAttackCount() const {return OriginCursor;}
private:
 enum class EPhase:uint8 {Bow,Entering,Origin,FinisherPending,Finishing};
 EPhase Phase=EPhase::Bow;
 EEGElysiaHerrscherPresentationPhase PresentationPhase=EEGElysiaHerrscherPresentationPhase::Bow;
 FGuid PresentationAction;
 void PublishPresentationPhase(EEGElysiaHerrscherPresentationPhase NewPhase,FGuid Action);
 int32 BowCursor=0,OriginCursor=0;
 double OriginEndTime=0;
 FGuid ActiveAction;
 bool bActionUltimate=false,bActionOrigin=false,bEntryReleased=false,bFinisherStarting=false,bActionFinisher=false,bFinisherReleased=false;
 bool bPassiveStatusDirty=false;
 TSet<FGuid> SubmittedHits;
 // One committed staff swing retains the targets and attack snapshot of its
 // first contact. Weak references never extend any combatant's lifetime.
 struct FOriginStrikeTarget {TWeakObjectPtr<AEGChessUnit> Unit;float Multiplier=0.f;bool bMain=false;};
 struct FPendingOriginStrike
 {
  FEGSkillActionContext Context;
  TArray<FOriginStrikeTarget> Targets;
  FVector ImpactPoint=FVector::ZeroVector;
  double NextHitTime=0.;
  double ContactInterval=.12;
  int32 AttackIndex=0;
  int32 Remaining=2;
  bool bActionCompleted=false;
 };
 TArray<FPendingOriginStrike> PendingOriginStrikes;
 TSet<FActiveGameplayEffectHandle> SuppressedManaEffects;
 TArray<TWeakObjectPtr<UEGElysiaOriginKnockupComponent>> FinisherReactions;
 FDelegateHandle ManaEffectRemovedDelegate;
 void ManaEffectRemoved(const FActiveGameplayEffect& Effect);
 void ReturnToBow();
 bool IsActionCurrent(const FEGSkillActionContext& Context) const;
 bool IsOriginStrikeCurrent(FGuid Action,double HitTime) const;
 void ApplyOriginStrike(const FPendingOriginStrike& Strike,float Fraction,int32 ContactIndex,double HitTime);
 void TickOriginStrikes();
};
