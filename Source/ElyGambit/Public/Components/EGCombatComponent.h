#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGCombatComponent.generated.h"

class AEGChessUnit;
using FEGCombatantList=TArray<TWeakObjectPtr<AEGChessUnit>>;

UCLASS(ClassGroup = (AutoChess), meta = (BlueprintSpawnableComponent))
class ELYGAMBIT_API UEGCombatComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEGCombatComponent();
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
							   FActorComponentTickFunction* ThisTickFunction) override;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Combat")
	bool CanAct() const;
	/** Independent owners may suppress new actions without clearing one another. */
	void AddExternalActionBlock(FGuid Token) {if(Token.IsValid()) ExternalActionBlocks.Add(Token);}
	void RemoveExternalActionBlock(FGuid Token) {ExternalActionBlocks.Remove(Token);}
	bool HasExternalActionBlock() const {return !ExternalActionBlocks.IsEmpty();}
	bool HasExternalActionBlockOtherThan(FGuid Token) const {return ExternalActionBlocks.Num()>(ExternalActionBlocks.Contains(Token)?1:0);}

	UFUNCTION(BlueprintPure, Category = "AutoChess|Combat")
	AEGChessUnit* GetTarget() const { return Target.Get(); }

	UFUNCTION(BlueprintPure, Category = "AutoChess|Combat")
	bool IsValidEnemy(const AEGChessUnit* Other) const;

	UFUNCTION(BlueprintPure, Category = "AutoChess|Combat")
	bool IsInAttackRange(const AEGChessUnit* Other) const;
 bool IsInAttackReach(const AEGChessUnit* Other,float Extra=0.f) const;
 UFUNCTION(BlueprintPure,Category="AutoChess|Combat") float GetEffectiveAttackRange(const AEGChessUnit* Other) const;
 static float BodyRadius(const AEGChessUnit* Other);
 static float BlockingRadius(const AEGChessUnit* Other);
 static bool IsSpaceOccupant(const AEGChessUnit* Other);
 UFUNCTION(BlueprintPure,Category="AutoChess|Combat") bool IsValidHealingTarget(const AEGChessUnit* Other) const;
 UFUNCTION(BlueprintPure,Category="AutoChess|Combat") AEGChessUnit* FindHealingTarget(bool bRequireRange=true) const;
 bool IsInHealingRange(const AEGChessUnit* Other) const;
 /** Recovery for spawn/teleport/landing overlap; normal movement always sweeps. */
 bool ResolveBoardOverlap();
 FVector FindFreeBoardLocation(FVector Desired,bool* bOutFound=nullptr) const;
 /** Same body/footprint sweep used by chase; no movement or gameplay effect. */
 bool IsBoardPathClear(FVector Start,FVector End,const AEGChessUnit* IgnoreA=nullptr,const AEGChessUnit* IgnoreB=nullptr,const AEGChessUnit** OutBlocker=nullptr,const TCHAR** OutReason=nullptr,const TArray<AEGChessUnit*>* IgnoreGroup=nullptr,bool bIgnoreBarrier=false,bool bRequireBoardBounds=true) const;
 /** Shared temporary-grid search; output waypoints are followed continuously. */
 bool PlanBoardRoute(const TArray<FVector>& Goals,TArray<FVector>& OutRoute,FVector& OutGoal,const AEGChessUnit* IgnoreA=nullptr,const AEGChessUnit* IgnoreB=nullptr,bool bPreferGoalOrder=false) const;
 bool AdvanceBoardRoute(TArray<FVector>& Route,float Travel,const AEGChessUnit* IgnoreA=nullptr,const AEGChessUnit* IgnoreB=nullptr,float* OutRemainingTravel=nullptr,bool bRequireBoardBounds=true);
 bool IsBoardPursuitStalled(FVector Goal,float Delta);
 void ResetBoardPursuitStall();
#if WITH_DEV_AUTOMATION_TESTS
 bool GetChaseGoalForTests(FVector& OutGoal) const {if(!bHasContact)return false;OutGoal=ReservedContact;return true;}
 FString GetChaseDebugForTests() const {return FString::Printf(TEXT("contact=%d route=%d retry=%.2f reason=%s block=%s blocker=%s"),bHasContact?1:0,ChaseRoute.Num(),ReplanWait,LastPursuitReason,LastMovementBlockReason,*LastMovementBlocker.ToString());}
#endif
 /** True only if the body fits at this exact point, without moving another unit. */
 bool IsBoardLocationFree(FVector Point,const AEGChessUnit* Ignore=nullptr,const AEGChessUnit* IgnoreB=nullptr,bool bRequireBoardBounds=true,const TArray<AEGChessUnit*>* IgnoreGroup=nullptr) const;
 /** Reserve a safe landing until the movement action finishes or is cancelled. */
 bool ReserveBoardLanding(FVector Desired,FVector& OutLanding);
 void ReleaseBoardLanding(FGuid Round);
 /** Returns true if completed immediately; a current round may defer until space is available. */
 bool CompleteOrDeferBoardLanding(FVector Desired,FGuid Round,TFunction<void()> OnComplete={});
 bool IsBoardLandingPending() const {return bLandingPending;}
 bool IsBoardLandingReserved() const {return bLandingReserved;}
 void CancelBoardLanding();
 /** Retarget an active landing without dropping its completion callback. */
 void RelocateBoardLanding(FVector NewLocation);
 void GetBoardLandingCoordinates(FVector& OutReserved,FVector& OutPending) const {OutReserved=LandingReservation;OutPending=PendingLandingDesired;}
 void RestoreBoardLandingCoordinates(FVector Reserved,FVector Pending);

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Combat")
	AEGChessUnit* FindNearestEnemy() const;

	UFUNCTION(BlueprintCallable, Category = "AutoChess|Combat")
	void ResetCombat();
    void SetCombatants(const TArray<TWeakObjectPtr<AEGChessUnit>>& Units);
    void SetCombatants(TSharedRef<const FEGCombatantList> Units);
    TArray<AEGChessUnit*> FindEnemiesInRange(FVector Origin,float Radius) const;

	UFUNCTION(BlueprintNativeEvent, Category = "AutoChess|Combat")
	void RequestChase(AEGChessUnit* Other);

private:
	TSet<FGuid> ExternalActionBlocks;
	UPROPERTY(Transient)
	TObjectPtr<AEGChessUnit> Unit;

	UPROPERTY(Transient)
	TWeakObjectPtr<AEGChessUnit> Target;
 TWeakObjectPtr<AEGChessUnit> BlockedKnightTarget;
 TWeakObjectPtr<AEGChessUnit> KnightPursuitTarget;
 float KnightStallSeconds=0.f;
 float KnightBestDistance=MAX_flt;
 double KnightRetryTime=0;
 TWeakObjectPtr<AEGChessUnit> ReservedTarget;
 FVector ReservedContact=FVector::ZeroVector;
 FVector ReservedContactOffset=FVector::ZeroVector;
 TArray<FVector> ChaseRoute;
 float ReplanWait=0.f;
 TArray<FVector> DeviceEscapeRoute;
 FGuid DeviceEscapeRound;
 float DeviceEscapeRetry=0.f;
 bool IsOverlappingFriendlyEmplacement() const;
 void ClearDeviceEscape();
 float MovementDiagnosticTime=0.f,MovementDiagnosticDistance=0.f;
 mutable uint64 MovementSearchCount=0;
 mutable double MovementSearchMilliseconds=0.;
 double LastMovementBlockReport=0.;
 const TCHAR* LastPursuitReason=TEXT("WaitingRetry");
 const TCHAR* LastMovementBlockReason=TEXT("None");
 FName LastMovementBlocker=NAME_None;
 bool bHasContact=false;
 bool bLandingReserved=false;
 FVector LandingReservation=FVector::ZeroVector;
 FGuid LandingReservationRound;
 bool bLandingPending=false;
 FGuid PendingLandingRound;
 FVector PendingLandingDesired=FVector::ZeroVector;
 float PendingLandingRetry=0.f;
 TFunction<void()> PendingLandingComplete;
 bool TryCompleteBoardLanding();
 void ClearBoardLanding();
 FVector StallGoal=FVector::ZeroVector;
 FVector StallAnchor=FVector::ZeroVector;
 bool bHasStallGoal=false;
 bool PlanChase(AEGChessUnit* Other);
 void ClearChase();
	TSharedPtr<const FEGCombatantList> Combatants;
    float DecisionDeltaSeconds=0.f;
    float DecisionWait=0.f;
    TSharedRef<const FEGCombatantList> CandidateSnapshot() const;
    float AttackWait = 0.f;
	float UltimateRetryWait = 0.f;
};
