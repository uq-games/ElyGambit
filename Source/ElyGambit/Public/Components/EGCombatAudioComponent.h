#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EGCombatAudioComponent.generated.h"
class AEGChessUnit;
class UEGBattleContextComponent;
class UAudioComponent;

/** Cosmetic, per-incarnation audio. No attributes, damage or attack accounting. */
UCLASS(ClassGroup=(Presentation))
class ELYGAMBIT_API UEGCombatAudioComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGCombatAudioComponent();
 virtual void BeginPlay() override;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
 void Configure(UEGBattleContextComponent* Context);
 void BeginAction(FGuid Id,bool Ultimate,int32 Index);
 void ActionEvent(FName Node,const FVector& TargetLocation);
 void ConfirmHit(FName Node,FGuid Id,AEGChessUnit* Target);
 /** Confirmed gameplay impact can outlive its casting action (projectiles/rain).
  * Round and pulse identity prevent duplicates without borrowing a later action. */
 void CommittedImpact(FName Node,FGuid Round,FGuid Id,bool Ultimate,int32 Index,int32 Pulse,const FVector& Point);
 /** Repeated confirmed pulses within one cast (e.g. Aponia lightning). */
 void ActionPulse(FName Node,FGuid ExpectedAction,FGuid PulseToken,const FVector& Point);
 void RegisterLaunch(FGuid Id);
 void Land(FGuid Id,FGuid Round,const FVector& Point);
 void EndAction(bool Completed);
 /** End Vill-V's continuous gun audio when firing actually stops. */
 void StopVillVGunLoop(bool bPlayTail=true);
 UAudioComponent* GetVillVGunLoop() const { return VillVGunLoop.Get(); }
 void StopVillVCannonCharge();
 UAudioComponent* GetVillVCannonCharge() const { return VillVCannonCharge.Get(); }
 void Clear();
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Audio") int32 EventCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Audio") int32 SpawnedVoiceCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Audio") FName LastNode;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Audio") FString LastSoundPath;
 UFUNCTION(BlueprintPure,Category="Audio") int32 GetActiveVoiceCount() const;
private:
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> Voices;
 UPROPERTY(Transient) TWeakObjectPtr<UAudioComponent> VillVGunLoop;
 UPROPERTY(Transient) TWeakObjectPtr<UAudioComponent> VillVGunStart;
 UPROPERTY(Transient) TWeakObjectPtr<UAudioComponent> VillVGunTail;
 UPROPERTY(Transient) TWeakObjectPtr<UAudioComponent> VillVCannonCharge;
 TMap<TWeakObjectPtr<UAudioComponent>,FGuid> ActionVoices;
 TSet<FName> Submitted;
 TSet<FGuid> SubmittedPulses;
 TSet<FGuid> Launched;
 TSet<FString> CommittedImpacts;
 TMap<FName,FVector> PendingEntryAudio;
 bool bFlushingEntryAudio=false;
 TMap<int32,int32> PreviousVariants;
 FGuid ActionId,RoundId;
 int32 AttackIndex=INDEX_NONE;
 bool bUltimate=false;
 bool IsCurrent() const;
 void UpdateVillVGunLoopRate();
 UFUNCTION() void OwnerDied(AEGChessUnit* Unit);
 void Play(FName Node,FGuid Id,bool Ultimate,int32 Index,const FVector& Point,bool TrackAction);
};
