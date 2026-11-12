#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/EGChessTypes.h"
#include "EGFoundationProbe.generated.h"

class AEGChessUnit;

/** Transient event observer used by native foundation automation; never placed in a map. */
UCLASS(Transient, NotBlueprintable)
class UEGFoundationProbe : public UObject
{
	GENERATED_BODY()
public:
	int32 Deaths = 0;
	int32 States = 0;
	int32 Attributes = 0;
	int32 DamageEvents = 0;
	int32 Phases = 0;
	UFUNCTION()
	void Died(AEGChessUnit* Unit) { ++Deaths; }
	UFUNCTION()
	void StateChanged(EEGUnitState State) { ++States; }
	UFUNCTION()
	void AttributeChanged(FName Attribute, float OldValue, float NewValue) { ++Attributes; }
	UFUNCTION()
	void DamageReceived(float Amount) { ++DamageEvents; }
	UFUNCTION()
	void PhaseChanged(EEGMatchPhase Phase) { ++Phases; }
};
