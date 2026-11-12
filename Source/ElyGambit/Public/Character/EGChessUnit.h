#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "GameplayAbilitySpec.h"
#include "Interaction/EGCombatant.h"
#include "EGChessUnit.generated.h"

class UAbilitySystemComponent;
class UEGAttributeSet;
class UEGCombatComponent;
class UEGHeroSkillComponent;
class UEGHeroDefinition;
class UEGHitFeedbackComponent;
class UEGUltimateRingComponent;
class AEGDeployedPiece;
class AEGChessUnit;
struct FOnAttributeChangeData;
struct FEGPresentationClips;
struct FActiveGameplayEffectsContainer;
struct FGameplayEffectSpec;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGUnitDied, AEGChessUnit*, Unit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGUnitStateChanged, EEGUnitState, State);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FEGAttributeChanged, FName, Attribute, float, OldValue, float, NewValue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGDamageReceived, float, Amount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEGCombatResolved,const FEGCombatResolution&,Result);
/** Native presentation consumers release temporary display state before death or a body swap. */
DECLARE_MULTICAST_DELEGATE_OneParam(FEGPresentationWillRelease, AEGChessUnit*);
DECLARE_DELEGATE_OneParam(FEGPresentationReleaseLease, AEGChessUnit*);

UCLASS(Blueprintable)
class ELYGAMBIT_API AEGChessUnit : public ACharacter, public IAbilitySystemInterface, public IEGCombatant
{
	GENERATED_BODY()

public:
	AEGChessUnit();
 void RefreshBoardCollision();
 bool IsDeathPending() const {return bDeathPending;}
 UFUNCTION(BlueprintPure,Category="Presentation") UEGHitFeedbackComponent* GetHitFeedback() const {return HitFeedback;}
	UFUNCTION(BlueprintPure,Category="AutoChess|Unit") UEGHeroSkillComponent* GetHeroSkill() const {return HeroSkill;}
	void SetRuntimeState(EEGUnitState Value){SetState(Value);}
	virtual void SetPresentationRate(float Rate) {}
 virtual void CancelPresentationAction() {}
	virtual void PresentRevive() {}
	virtual FVector GetActionOrigin() const {return GetActorLocation()+FVector(0,0,35);}
 virtual bool IsAuxiliaryEmplacement() const {return false;}
 virtual USkeletalMeshComponent* GetVisiblePresentationMesh() const {return GetMesh();}
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override { return AbilitySystem; }
	virtual EEGFaction GetFaction_Implementation() const override { return Faction; }
	virtual bool IsAlive_Implementation() const override;
	virtual EEGUnitState GetCombatState_Implementation() const override { return State; }
	virtual void BeginCombat_Implementation() override;
	virtual void EndCombat_Implementation() override;

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Unit")
	virtual bool InitializeUnit(UEGHeroDefinition* Definition, FGuid HeroInstanceId, EEGFaction NewFaction, int32 Level = 1);

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Unit")
	void Die();

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Unit")
	bool RequestBasicAttack();

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Unit")
	bool RequestUltimate();

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Unit")
	void RestoreMana(float Amount);
	// Preparation synchronization; persistent level remains in HeroRoster.
	bool RefreshPreparationLevel(int32 Level);
	virtual void NotifyDamageReceived(float Amount);
	virtual float GetUltimateBonus() const;
	/** Resolved live range, including a ranged monster's current board cell size. */
	float GetRuntimeAttackRange() const;
    void SetUltimateBonus(float Value) {UltimateBonus=FMath::Max(1.f,Value);}
    virtual void AttachPresentation(AEGDeployedPiece* Piece) {}
    virtual void RestorePresentation() {}
 FEGPresentationWillRelease OnPresentationWillRelease;
 bool TryAcquirePresentationReleaseLease(const FEGPresentationReleaseLease& Lease,FDelegateHandle& OutHandle);
 void ReleasePresentationLease(FDelegateHandle Handle);
 void NotifyPresentationWillRelease();
	virtual void PresentAttack(AEGChessUnit* Target, bool bUltimate) {}
	/** Native transaction boundary; legacy presentation hooks remain callable. */
	virtual bool TryPresentAction(AEGChessUnit* Target, bool bUltimate) { PresentAttack(Target, bUltimate); return true; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Unit")
	FGuid GetInstanceId() const { return InstanceId; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Unit")
	UEGHeroDefinition* GetDefinition() const { return Configuration; }

	/** Presentation clips for the crossfading anim instance; false when no definition is assigned. */
	virtual bool GetPresentationClips(FEGPresentationClips& Out) const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Unit")
	UEGCombatComponent* GetCombat() const { return Combat; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Unit")
	const UEGAttributeSet* GetAttributes() const { return Attributes; }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Unit")
	bool IsInitialized() const { return bInitialized; }

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Unit")
	FEGUnitDied OnDied;

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Unit")
	FEGUnitStateChanged OnCombatStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Unit")
	FEGAttributeChanged OnAttributeChanged;

	UPROPERTY(BlueprintAssignable, Category = "AutoChess|Unit")
	FEGDamageReceived OnDamageReceived;
	UPROPERTY(BlueprintAssignable,Category="AutoChess|Unit") FEGCombatResolved OnCombatResolved;
	UPROPERTY(Transient,BlueprintReadOnly,Category="AutoChess|Unit") FEGCombatResolution LastCombatResolution;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Unit")
	TObjectPtr<UAbilitySystemComponent> AbilitySystem;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Unit")
	TObjectPtr<UEGAttributeSet> Attributes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Unit")
	TObjectPtr<UEGCombatComponent> Combat;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Presentation") TObjectPtr<UEGHitFeedbackComponent> HitFeedback;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Presentation") TObjectPtr<UEGUltimateRingComponent> UltimateRing;
	UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AutoChess|Unit") TObjectPtr<UEGHeroSkillComponent> HeroSkill;
	double LastDamageManaTime=-100;

private:
    UFUNCTION() void PresentCombatResolution(const FEGCombatResolution& Result);
    bool bKillBeamPresented = false;
	UPROPERTY(Transient)
	TObjectPtr<UEGHeroDefinition> Configuration;

	UPROPERTY(VisibleInstanceOnly)
	FGuid InstanceId;

	UPROPERTY(VisibleInstanceOnly)
	EEGFaction Faction = EEGFaction::Neutral;

	UPROPERTY(VisibleInstanceOnly)
	EEGUnitState State = EEGUnitState::Idle;

	UPROPERTY(Transient)
	bool bInitialized = false;
	bool bDeathPending = false;
	bool bNotifyingPresentationWillRelease = false;
	FEGPresentationReleaseLease PresentationReleaseLease;
	float UltimateBonus=1.f;
	float InitialAttackRange=0.f;
	FGameplayAbilitySpecHandle AttackHandle;
	FGameplayAbilitySpecHandle UltimateHandle;
	void ApplyLevelAttributes(int32 Level);
	void GrantStartupAbilities(int32 Level);
 // Only ignores installed by the board-team policy are owned and removed here.
 TArray<TWeakObjectPtr<AEGChessUnit>> BoardFriendlyMoveIgnores;
 void RemoveBoardFriendlyMoveIgnore(AEGChessUnit* Other);
 void ClearBoardFriendlyMoveIgnores();
 void RefreshBoardFriendlyMoveIgnores();
	void SetState(EEGUnitState NewState);
	void AttributeChanged(const FOnAttributeChangeData& Data);
	bool AcceptGameplayEffect(const FActiveGameplayEffectsContainer& Container,const FGameplayEffectSpec& Spec) const;
};
