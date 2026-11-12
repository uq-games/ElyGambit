#include "Data/EGHeroCardInfoDefinition.h"
FText UEGHeroCardInfoDefinition::ToText() const
{
 return FText::FromString(FString::Printf(TEXT("%s\n%s"),bHasUltimate?*UltimateName.ToString():TEXT("无独立大招"),*Effect.ToString()));
}
