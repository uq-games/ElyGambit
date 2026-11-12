#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Presentation/EGBoardLocomotion.h"
#include "EGSkillPresentationCoordinator.generated.h"
class UEGHeroSkillComponent;
class UEGCombatComponent;
class AEGCombatVisual;
class AEGChessUnit;
class USkeletalMeshComponent;
class UEGSourcePreviewDefinition;
/** Owns source-rig presentation only. Action IDs, costs and gameplay stay in the skill component. */
UCLASS(Transient)
class UEGSkillPresentationCoordinator : public UObject
{
 GENERATED_BODY()
public:
 void Initialize(UEGHeroSkillComponent* InSkill){Skill=InSkill;}
 void StartActionAssembly();
 /** Select a cosmetic clip without changing the combat attack index. */
 int32 SelectAttackPresentationIndex(int32 BasicIndex) const;
 void FinishActionPresentation(bool bCompleted);
 bool IsHumanBowChargeStarted() const {return bHumanBowChargeStarted;}
 void ClearActionAssembly();
 void ClearIdleAssembly();
 void ResetTickOrdering();
 void PresentIdleHit();
 void RefreshIdleAssembly(float DeltaTime);
 bool GetAssemblyBoneLocation(FName Part,FName Bone,FVector& Location);
 void TickAction(float Position,bool bHide);
 /** Exclusive cosmetic body/weapon assembly for an encounter-owned transition. */
 bool BeginBarrierTransition(UEGSourcePreviewDefinition* Definition,FGuid Round,FGuid Token,float& OutDuration);
 bool SampleBarrierTransition(FGuid Round,FGuid Token,float Position);
 void EndBarrierTransition(FGuid Round,FGuid Token);
 bool IsBarrierTransitionActive(FGuid Round,FGuid Token) const;
private:
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> Skill;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> ActionAssembly;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> IdleAssembly;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> BarrierTransitionAssembly;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> BarrierTransitionBody;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> BarrierTransitionUnit;
 FDelegateHandle BarrierTransitionLease;
 FGuid BarrierTransitionRound,BarrierTransitionToken;
 float BarrierTransitionLastPosition=0.f;
 void ClearBarrierTransition();
 void OnBarrierPresentationWillRelease(AEGChessUnit* Unit);
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> AssemblyHiddenMesh;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> PassiveTickBody;
 UPROPERTY(Transient) TWeakObjectPtr<UEGCombatComponent> MovementTickCombat;
 bool bOwnsPassiveTickPrerequisite=false;
 bool bOwnsMovementTickPrerequisite=false;
 bool bAssemblyMeshWasVisible=false;
 bool bHumanBowChargeStarted=false;
 int32 IdleAssemblyIndex=INDEX_NONE;
 float PassiveClock=0,PassiveHitRemaining=0;
 FEGBoardLocomotion PassiveLocomotion;
};
