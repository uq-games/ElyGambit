#include "Data/EGStigmataBalance.h"
#include "UObject/UObjectGlobals.h"

const UEGStigmataBalanceSettings& UEGStigmataBalanceSettings::Get()
{
 return *GetDefault<UEGStigmataBalanceSettings>();
}
