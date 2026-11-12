#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Types/EGChessTypes.h"
#include "EGCombatant.generated.h"

UINTERFACE(BlueprintType)
class ELYGAMBIT_API UEGCombatant : public UInterface
{
	GENERATED_BODY()
};

class ELYGAMBIT_API IEGCombatant
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AutoChess|Combat")
	EEGFaction GetFaction() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AutoChess|Combat")
	bool IsAlive() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AutoChess|Combat")
	EEGUnitState GetCombatState() const;

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AutoChess|Combat")
	void BeginCombat();

	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "AutoChess|Combat")
	void EndCombat();
};
