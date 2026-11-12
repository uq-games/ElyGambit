#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Game/EGHeroRoster.h"
#include "EGSevenStageSaleProbe.generated.h"

/** Attempts the public roster API from a synchronous transaction notification. */
UCLASS(Transient, NotBlueprintable)
class UEGSevenStageSaleProbe : public UObject
{
 GENERATED_BODY()
public:
 UPROPERTY(Transient) TObjectPtr<UEGHeroRoster> Roster;
 FGuid Target;
 bool bAttempted=false;
 bool bSucceeded=false;
 UFUNCTION() void OnSaleNotification()
 {
  bAttempted=true;
  bSucceeded=Roster&&Roster->SellHero(Target);
 }
};
