#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Types/EGChessTypes.h"
#include "EGVillVMechanismComponent.generated.h"
class AEGChessUnit;
class AEGVillVEmplacement;
class AEGCombatVisual;
class AEGVillVCombatFX;
class AEGVillVShotVisual;
class UEGBattleContextComponent;
class UEGVillVSkillDefinition;
class UAnimSequence;
struct FEGSkillActionContext;

UENUM(BlueprintType)
enum class EEGVillVPhase : uint8 { Inactive, Moving, Entering, Operating, Leaving, Retired, Jumping };

/** One pilot transaction coordinating two GAS health-bearing, stationary devices. */
UCLASS(ClassGroup=(AutoChess))
class ELYGAMBIT_API UEGVillVMechanismComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGVillVMechanismComponent();
 void Start(UEGBattleContextComponent* Context);
 /** Creates an enemy copy's independent devices at prevalidated enemy cells. */
 bool StartEnemyLayout(UEGBattleContextComponent* Context,FIntPoint GatlingCell,FIntPoint CannonCell);
 bool HasActiveDevices() const;
 void Stop();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function) override;
 bool IsOperating() const {return Phase==EEGVillVPhase::Operating;}
 int32 Mode() const {return Current;}
 int32 UltimatePresentation() const;
 void BeginAction(const FEGSkillActionContext& Context);
 void ActionEvent(FName Event,const FEGSkillActionContext& Context);
 void ActionEnded(bool bCompleted);
 void SampleAction(float Position);
 void ClearSeatedBody();
 bool Muzzle(FVector& Location) const;
 /** Snapshots an already authorized shot; no target selection or damage. */
 bool CaptureV2Muzzle(FName Event,FTransform& Muzzle) const;
 void PresentV2Shot(FName Event,const FTransform& Muzzle,const FVector& Target,const FGuid& Action,const FGuid& ShotRound,AEGVillVEmplacement* ShotDevice,bool bEnhanced);
 AEGChessUnit* FiringTarget(AEGChessUnit* Preferred=nullptr) const;
 bool IsAimReady(AEGChessUnit* Target=nullptr) const;
 void NotifyShot(){bFiredThisAction=true;}
 void BeginAutoFire(FGuid UltimateAction);
 float GetAutoFireRemaining() const {return AutoRemaining;}
 void RestoreOperatingPresentation(){if(IsOperating())RefreshSeatedBody();}
 // Restore a valid occupied-seat base immediately before an attack montage blends in.
 void RestoreAttackBasePose(){if(IsOperating())RefreshSeatedBasePose();}
 bool HasEnhancedShot() const {return bEnhancedPending;}
 bool ActionEnhanced() const {return bActionEnhanced;}
 void ConsumeEnhancement() {if(bActionEnhanced)bEnhancedPending=false;}
 UFUNCTION(BlueprintPure) AEGVillVEmplacement* GetDevice(int32 Index) const;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) EEGVillVPhase Phase=EEGVillVPhase::Inactive;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 GroundTravelCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 JumpTransferCount=0;
private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Pilot;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TObjectPtr<UEGVillVSkillDefinition> Definition;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> HeldAttackBody;
 UPROPERTY(Transient) TArray<TObjectPtr<AEGVillVEmplacement>> Devices;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> SeatedBody;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGVillVCombatFX>> ShotEffects;
 UPROPERTY(Transient) TWeakObjectPtr<AEGVillVCombatFX> ChargeEffect;
 UPROPERTY(Transient) TArray<TWeakObjectPtr<AEGVillVShotVisual>> V2ShotEffects;
 FGuid Round;
 int32 Current=0,Destination=0;
 float AutoRemaining=0.f,AutoPosition=0.f;
 int32 AutoDevice=INDEX_NONE;
 FGuid AutoUltimate,AutoShot;
 TWeakObjectPtr<AEGChessUnit> AutoTarget;
 void TickAutoFire(float Delta);
 void FireAutomatic(FName Event);
 bool bStopping=false,bEnhancedPending=false,bActionEnhanced=false,bUltimateSwitch=false;
 float SpawnClock=0,RouteRetry=0,ActionWeaponOffset=0;
 bool bHandlingDeath=false;
 TArray<FVector> Route;
 FVector Approach=FVector::ZeroVector;
 bool bHasApproach=false;
 FVector JumpOrigin=FVector::ZeroVector;
 bool bJumpPlaybackStarted=false;
 FVector LeaveOrigin=FVector::ZeroVector;
 FRotator LeaveRotation=FRotator::ZeroRotator;
 FRotator JumpRotation=FRotator::ZeroRotator;
 // Hold the source loop end across consecutive basic-action gaps.
 bool bHoldDeviceFiringPose=false;
 float GunAimLostSeconds=0.f;
 bool bOpeningEntry=true;
 bool bEntryCarriesFire=false;
 bool bFiredThisAction=false;
 bool bRestartJumpQueued=false;
 int32 QueuedJump=0;
 void StartJump(int32 Index);
 bool StartJumpPlayback();
 void SuspendJumpForRetry();
 void UpdateJump(float Position);
 void UpdateDeparture(float Position);
 void UpdateAim(float Delta);
 bool IsCurrentBattle() const;
 bool DeviceAlive(int32 Index) const;
 bool OrientForGroundOffset(int32 Index,const FVector& Offset,const FVector& Toward,FVector& GroundPoint);
 void StartTravel(int32 Index);
 void ClearV2Shots();
 bool BuildRoute();
 void StartEntry();
 void RefreshSeatedBody();
 void RefreshSeatedBasePose();
 UAnimSequence* CurrentBasicBody() const;
 UFUNCTION() void DeviceDied(AEGChessUnit* Unit);
};
