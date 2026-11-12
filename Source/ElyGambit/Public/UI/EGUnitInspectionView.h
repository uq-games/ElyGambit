#pragma once

#include "CoreMinimal.h"
#include "Types/EGChessTypes.h"

class AEGChessUnit;
class UEGHeroDefinition;
class UEGHeroRoster;

struct FEGInspectionSkill
{
 FName Id;
 FText Title;
 FText Description;
 FString IconPath;
};

/** Short-lived presentation snapshot. Gameplay remains owned by the unit and roster. */
struct ELYGAMBIT_API FEGUnitInspectionView
{
 const UEGHeroDefinition* Definition=nullptr;
 FEGStatBlock Stats;
 int32 Level=1;
 int32 CardsUsed=0;
 bool bAwakened=false;
 bool bCombat=false;
 float Health=0.f;
 float Mana=0.f;
 float Shield=0.f;
 FString Status;
 /** Runtime-only display overrides for an actor whose combat role replaces its definition. */
 bool bHasSkillOverride=false;
 bool bSummonUltimateUnavailable=false;
 FText SkillBasicOverride;
 FText SkillUltimateOverride;
 FText SkillPassiveOverride;
 bool bEnemy=false;
 /** 0 = ordinary, 1 = first life/barrier, 2 = second life. */
 int32 BossArtworkPhase=0;
 TArray<FEGInspectionSkill> EnemySkills;

 static FEGUnitInspectionView Capture(const AEGChessUnit* Unit,const UEGHeroRoster* Roster,FGuid HeroId);
};
