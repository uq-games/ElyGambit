#pragma once
#include "Character/EGDemoCombatUnit.h"
#include "EGVillVEmplacement.generated.h"
class AEGCombatVisual;
class UEGSourcePreviewDefinition;

/** Targetable health of one stationary mechanism. The pilot owns attacks and mana. */
UCLASS()
class ELYGAMBIT_API AEGVillVEmplacement : public AEGDemoCombatUnit
{
 GENERATED_BODY()
public:
 virtual bool IsAuxiliaryEmplacement() const override {return true;}
 virtual void NotifyDamageReceived(float Amount) override;
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 void SetPilot(AEGChessUnit* Hero,int32 Index);
 void ShowDevice(UEGSourcePreviewDefinition* Motion,float Position);
 void ClearDevice();
 bool Muzzle(FVector& Location) const;
 bool MuzzleFrame(FTransform& Transform) const;
 bool MuzzleFrame(FName Bone,FTransform& Transform) const;
 /** Closest point on the one-cell base, used by melee contact presentation. */
 float DistanceToCombatSurface(FVector Point,FVector* OutClosest=nullptr) const;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 DeviceIndex=0;
 UPROPERTY(Transient,BlueprintReadOnly) TWeakObjectPtr<AEGChessUnit> Pilot;
private:
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> DeviceVisual;
 // Presentation-only anchor, retained across idle/attack clip changes.
 float GroundAnchorHeight=0.f;
 bool bHasGroundAnchor=false;
};
