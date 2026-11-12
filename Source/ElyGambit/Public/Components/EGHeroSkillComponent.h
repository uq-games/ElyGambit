#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/EGChessTypes.h"
#include "GameplayEffectTypes.h"
#include "EGHeroSkillComponent.generated.h"
class AEGChessUnit;
class UEGHeroSkillBehavior;
class UEGSkillPresentationCoordinator;
class UEGBattleContextComponent;
class UAnimSequence;
class UAnimMontage;
class UAnimSequenceBase;
class USkeletalMeshComponent;
class UEGSourceEffectDefinition;
class AEGSourceEffect;
class AEGCombatVisual;
class UEGSourcePreviewDefinition;
struct FEGAttackPresentation;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEGSkillStatusChanged);

/** Per-incarnation execution state. Persistent growth belongs exclusively to the Roster. */
UCLASS(ClassGroup=(AutoChess))
class ELYGAMBIT_API UEGHeroSkillComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 void ClearTauntIfSource(AEGChessUnit* Source);
 UEGHeroSkillComponent();
 UFUNCTION(BlueprintPure,Category="AutoChess|Skill") UEGHeroSkillBehavior* GetBehavior() const {return Behavior;}
 bool ConfigureCorruptionBoss();
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
 void Configure(UEGBattleContextComponent* Context,int32 Level,bool Awakened);
 bool BeginAction(bool Ultimate);
 UAnimSequence* GetActiveAnimation() const {return ActiveAnimation;}
 UAnimSequenceBase* GetActivePresentationAsset() const;
 UAnimMontage* GetActiveMontage() const {return ActiveMontage;}
 bool GetAssemblyBoneLocation(FName Part,FName Bone,FVector& Location);
 bool IsHumanBowChargePresentationActive() const;
 /** Presentation only: no hit submission, costs, attributes or replicated state. */
 void PresentSourceEffect(UEGSourceEffectDefinition* Effect,USkeletalMeshComponent* Mesh);
 /** A fixed-world, action-owned cosmetic area effect. */
 AEGSourceEffect* PresentSourceEffectAt(UEGSourceEffectDefinition* Effect,const FVector& Point,const FRotator& Rotation,float ScaleMultiplier);
 void PresentSummonImpact(FGuid Token,FGuid Round,FVector Ground);
 FVector GetPresentationTargetGround(float HeightFraction=0.f) const;
 UFUNCTION(BlueprintCallable,Category="AutoChess|Skill") void AnimationEvent(FName Event);
 /** Capture only cosmetic form before death cancels the authoritative action. */
 void CaptureDeathPresentation();
 int32 GetDeathPresentationIndex() const {return DeathPresentationIndex;}
 bool HasDeathPresentationSnapshot() const {return bHasDeathPresentationSnapshot;}
 void ResolveDeathPresentation(UAnimSequence*& OutAnimation,UEGSourcePreviewDefinition*& OutAssembly) const;
 void CancelAction();
 void EndBattle();
 void RefreshSpeed();
 /** Cosmetic encounter transition; the caller owns its round/token and clock. */
 bool BeginBarrierTransition(UEGSourcePreviewDefinition* Definition,FGuid Round,FGuid Token,float& OutDuration);
 bool SampleBarrierTransition(FGuid Round,FGuid Token,float Position);
 void EndBarrierTransition(FGuid Round,FGuid Token);
 bool IsBarrierTransitionActive(FGuid Round,FGuid Token) const;
 void ApplyPersistentLevel(int32 NewLevel);
 bool IsBusy() const;
 bool IsActionRunning() const {return bActing;}
 bool PlayTraversalStage(const FEGAttackPresentation& Stage,UEGSourcePreviewDefinition* Assembly,float Rate);
 void SuspendForTraversal();
 void PresentIdleHit();
 bool IsCasting() const {return bActing&&bUltimate;}
 bool BlocksManaGain() const;
 void RemoveSourceShield(FName Source);
 bool IsControlImmune() const;
 UFUNCTION(BlueprintPure,Category="AutoChess|Control") bool CanReceiveHostileControl() const {return !IsControlImmune();}
 void ClearControlForImmunity();
 /** An internal authored ultimate stage owns a fresh action token and costs no mana. */
 bool BeginInternalUltimateStage(int32 SequenceIndex,AEGChessUnit* Target);
 bool IsTargetable() const;
 bool IsRecoverable() const {return bReviving;}
 bool TryPreventDeath();
 bool BeginRevive(float Delay,float HealthRatio,float AttackBonus);
 void SetActionProtected(bool Value);
 bool IsActionProtected() const {return bActionProtected;}
 UPROPERTY(BlueprintAssignable,Category="AutoChess|Skill") FEGSkillStatusChanged OnStatusChanged;
 void SetShield(float Value);
 /** Source identity controls replacement/expiry; ShieldSource supplies outgoing strength (self by default). */
 bool GrantSourceShield(FName Source,float Value,float Duration,AEGChessUnit* ShieldSource=nullptr,bool bKeepStronger=false);
 float GetSourceShield(FName Source) const;
 void RefundUncommittedUltimate();
 /** Refunds spent mana even during a form that suppresses ordinary mana gain. */
 void RestoreStigmataMana(float Amount); bool CommitStigmataUltimate(FGuid Id);
 void DiscardDeferredStigmataMana();
 float AbsorbShield(float Amount);
 void GrantPrismaticBubble(float Duration,float BreakReduction,float BreakDuration,UEGSourceEffectDefinition* Effect=nullptr,UEGSourceEffectDefinition* Burst=nullptr);
 UFUNCTION(BlueprintPure,Category="AutoChess|Protection") bool HasPrismaticBubble() const;
 float ResolveProtection(float Amount,FEGCombatResolution& Receipt);
 void ApplyProtection(FGuid Token,AEGChessUnit* Source,float Reduction,float Duration);
 void RemoveProtection(FGuid Token);
 bool HasProtection(FGuid Token) const;
 UFUNCTION(BlueprintPure,Category="AutoChess|Protection") float GetProtectionReduction() const;
 void ClearProtection();
 void SetTaunt(AEGChessUnit* Source,float Duration);
 /** The pausable timer is the authority; TauntEnd is a legacy display mirror. */
 UFUNCTION(BlueprintPure,Category="AutoChess|Skill") bool IsTauntActive() const;
 UFUNCTION(BlueprintPure,Category="AutoChess|Skill") int32 GetLevel() const {return Level;}
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") bool bSummon=false;
 /** Only explicitly configured enemy combat summons may cast and gain basic-hit mana. */
 bool bAllowSummonUltimate=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") FGuid SummonerId;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") float Shield=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") bool bInFlight=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") bool bAwakened=false;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") int32 ActionCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") int32 CommitCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="AutoChess|Skill") FGuid ActionId;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> ForcedTarget;
 double TauntEnd=0;
private:
 friend class UEGSkillPresentationCoordinator;
 UPROPERTY(Transient) TObjectPtr<UEGSkillPresentationCoordinator> Presentation;
 int32 DeathPresentationIndex=0;
 bool bHasDeathPresentationSnapshot=false;
 struct FSourceShield {float Value=0,Remaining=0;};
 TMap<FName,FSourceShield> SourceShields;
 float LegacyShield=0;
 void RefreshShieldTotal();
 void UpdateBubblePresentation();
 double BubbleEnd=0;
 float BubbleBreakReduction=0,BubbleBreakDuration=0;
 FGuid BubbleProtectionToken;
 TMap<FGuid,FActiveGameplayEffectHandle> ProtectionEffects;
 UPROPERTY(Transient) TWeakObjectPtr<AEGSourceEffect> BubbleVisual;
 UPROPERTY(Transient) TWeakObjectPtr<AEGSourceEffect> BubbleBurstVisual;
 UPROPERTY(Transient) TObjectPtr<UEGSourceEffectDefinition> BubbleBurstDefinition;
 UPROPERTY(Transient) TObjectPtr<UEGHeroSkillBehavior> Behavior;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Unit;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> ActiveAnimation;
 UPROPERTY(Transient) TObjectPtr<UAnimMontage> ActiveMontage;
 UPROPERTY(Transient) TObjectPtr<UEGSourcePreviewDefinition> StageAssembly;
 int32 ActiveUltimateIndex=INDEX_NONE;
 void RefreshIdleAssembly(float DeltaTime);
 void ClearIdleAssembly();
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> ActionEffects;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGSourceEffect>> ImpactEffects;
 TSet<FGuid> LandedImpactIds;
 TSet<TWeakObjectPtr<UEGSourceEffectDefinition>> PlayedEffects;
 int32 AttackSequenceCursor=0;
 int32 ActiveBasicAttackIndex=INDEX_NONE;
 int32 ActiveVisualAttackIndex=INDEX_NONE;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> ActionTarget;
 FGuid ActionRound;
 TSet<FName> Submitted;
 int32 Level=1;
 int32 ActionTier=0;
 bool bActing=false,bUltimate=false;
 bool bStoryHoldPaused=false;
 TArray<FName> HeldAnimationEvents;
 FGuid HeldAnimationAction,HeldAnimationRound;
 bool bRestoringStigmataMana=false;
 bool bActionProtected=false,bReviving=false,bReviveSpent=false;
 FGuid RevivalRound;
 FTimerHandle ReviveTimer;
 FActiveGameplayEffectHandle ReviveAttackEffect;
 float AttackSnapshot=0,HealthSnapshot=0,Multiplier=0,Secondary=0,SpentMana=0;
 // Human bow only: preserve its existing hit/finish budget while the final
 // aiming segment runs at the source speed scaled by attack speed.
 enum class EHumanBowRetime : uint8 { None, Preparing, Aiming, Recovery };
 EHumanBowRetime HumanBowRetime=EHumanBowRetime::None;
 FGuid HumanBowRetimeAction;
 float HumanBowSwitchPosition=0.f,HumanBowHitPosition=0.f;
 float HumanBowPrepareRate=0.f,HumanBowAimRate=0.f,HumanBowRecoveryRate=0.f;
 void UpdateHumanBowRetime();
 void BeginHumanBowRecovery(FGuid ExpectedAction);
 // Only the owner of the human entry pause may defer its Finish for media.
 bool bDeferredHumanEntryFinish=false;
 FGuid DeferredHumanEntryRound,DeferredHumanEntryAction;
 FVector LastPoint=FVector::ZeroVector;
 FTimerHandle Watchdog,TauntTimer;
 void Submit(FName Event);
 void Finish();
 void FinishAction(bool bCompleted);
 void ClearRevival();
 void ClearTaunt();
 void UpdateTauntMirror();
 void StartActionAssembly();
 void ClearActionAssembly();
 void ClearImpactEffects();
 UFUNCTION() void ActionAttributeChanged(FName Attribute,float OldValue,float NewValue);
};
