#pragma once
#include "CoreMinimal.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "EGCorruptionBossBehavior.generated.h"
enum class EEGCorruptionFirstPulseEvent : uint8 { Started, Resolved, Cancelled };
DECLARE_MULTICAST_DELEGATE_SixParams(FEGCorruptionFirstPulse,EEGCorruptionFirstPulseEvent,FGuid,FGuid,FVector,float,float);
/** Boss retains basic bow animation support, but owns its phase-one attack and never uses player tiers. */
UCLASS()
class ELYGAMBIT_API UEGCorruptionBossBehavior : public UEGElysiaSkillBehavior
{
 GENERATED_BODY()
public:
 FEGCorruptionFirstPulse OnFirstPulse;
 virtual bool AllowsAction(bool bUltimate) const override;
 virtual int32 PresentationIndex(bool bUltimate,int32 DefaultIndex) const override;
 virtual void ActionStarted(const FEGSkillActionContext& Context) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void ActionEnded(bool bCompleted) override;
 virtual void TickBattle(float DeltaSeconds) override;
 virtual void EndBattle() override;
private:
 void CancelPulse();
 bool bCurrentUltimate=false,bPulsePending=false,bPulseResolving=false,bPulseCommitted=false,bHasLockedPoint=false;
 FGuid CurrentUltimateAction,PulseAction;
 FVector LockedPoint=FVector::ZeroVector,PulsePoint=FVector::ZeroVector;
 float PulseRadius=0.f,PulseDuration=0.f,PulseRemaining=0.f,PulseDamage=0.f;
};
