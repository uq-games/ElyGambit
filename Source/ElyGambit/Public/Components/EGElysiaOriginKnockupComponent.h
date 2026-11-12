#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Types/EGChessTypes.h"
#include "EGElysiaOriginKnockupComponent.generated.h"

class AEGChessUnit;
class UEGBattleContextComponent;

/** One target-owned, round-scoped vertical reaction. No damage is applied here. */
UCLASS(Transient)
class ELYGAMBIT_API UEGElysiaOriginKnockupComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UEGElysiaOriginKnockupComponent();
 static UEGElysiaOriginKnockupComponent* Start(AEGChessUnit* Source,AEGChessUnit* Target,UEGBattleContextComponent* Battle,
  FGuid Round,FGuid Action,float Height,float Duration);
 void FinishReaction();
 virtual void TickComponent(float Delta,ELevelTick TickType,FActorComponentTickFunction* Function) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 TWeakObjectPtr<AEGChessUnit> SourceUnit;
 TWeakObjectPtr<UEGBattleContextComponent> BattleContext;
 FGuid RoundId,ActionId,BlockToken;
 float GroundZ=0.f,Elapsed=0.f,PeakHeight=0.f,ReactionDuration=0.f;
 EMovementMode PreviousMode=MOVE_None;
 uint8 PreviousCustomMode=0;
 bool bOwnsReaction=false,bOwnsMovement=false,bWaitingLanding=false,bAwaitingMovementRelease=false,bEndingPlay=false;
 void CompleteLanding(bool bForced);
 UFUNCTION() void OnTargetStateChanged(EEGUnitState State);
};
