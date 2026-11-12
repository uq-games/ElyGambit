#pragma once
#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
class AEGChessUnit;

/** Stateless GE application boundary. Battle ownership remains in the existing managers. */
struct ELYGAMBIT_API FEGCombatExecution
{
 /** DefenseScale apportions one hit's defense across sub-hits; normal attacks keep 1. */
 static bool Damage(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,float Minimum=0.f,float Level=1.f,FGameplayEffectContextHandle Context=FGameplayEffectContextHandle(),FGuid ActionId=FGuid(),bool bLinkedDamage=false,float DefenseScale=1.f,bool bBasicAttack=false);
 static bool StigmataDamage(AEGChessUnit* Source,AEGChessUnit* Target,float Raw,FGuid ActionId,bool bPeriodic=false,FName StigmataId=NAME_None);
 static bool Heal(AEGChessUnit* Target,float Amount);
 static bool Heal(AEGChessUnit* Source,AEGChessUnit* Target,float Amount);
 static bool EnvironmentDamage(AEGChessUnit* Target,float Raw,FGuid ActionId);
 /** Threshold selection belongs to the skill. Uses lethal GE damage, preserving death/revival. */
 static bool Execute(AEGChessUnit* Source,AEGChessUnit* Target,FGuid ActionId);
};
