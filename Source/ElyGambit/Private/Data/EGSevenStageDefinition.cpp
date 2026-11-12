#include "Data/EGSevenStageDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Data/EGStigmataBalance.h"

namespace
{
UEGHeroDefinition* MakeMonster(UEGRunDefinition* Run, const TCHAR* Family, const TCHAR* RuntimeId,
    EEGMonsterUltimate Ultimate, float HealthRatio, float AttackRatio)
{
    const FString Path = FString::Printf(TEXT("/Game/Blueprints/Characters/Enemies/%s/Data/DA_Monster_%s.DA_Monster_%s"),Family,Family,Family);
    auto* Source = LoadObject<UEGHeroDefinition>(nullptr,*Path);
    if (!Source || !Source->StatsDefinition || !Source->MonsterAttack) return nullptr;
    auto* Hero = DuplicateObject<UEGHeroDefinition>(Source,Run,FName(RuntimeId));
    Hero->HeroId = FName(RuntimeId);
    Hero->StatsDefinition = DuplicateObject<UEGHeroStatsDefinition>(Source->StatsDefinition,Hero);
    Hero->MonsterAttack = DuplicateObject<UEGMonsterAttackDefinition>(Source->MonsterAttack,Hero);
    Hero->MonsterAttack->Ultimate = Ultimate;
    // Freeze the pre-rebalance formal starting Elysia's authored values (MCP
    // readback, 2026-11-07). Monster tuning must not silently follow later
    // player hero changes. Other authored monster attributes remain untouched.
    Hero->StatsDefinition->BaseStats.MaxHealth = 950.f * HealthRatio;
    Hero->StatsDefinition->BaseStats.AttackPower = 100.f * AttackRatio;
    Hero->StatsDefinition->GrowthPerLevel.MaxHealth = 65.f * HealthRatio;
    Hero->StatsDefinition->GrowthPerLevel.AttackPower = 10.f * AttackRatio;
    if (Ultimate==EEGMonsterUltimate::Shield) Hero->MonsterAttack->ShieldHealthRatio=.30f;
    if (Ultimate==EEGMonsterUltimate::EnhancedShot) Hero->MonsterAttack->UltimateMultiplier=2.2f;
    if (Ultimate==EEGMonsterUltimate::TeamSpeed) Hero->MonsterAttack->TeamSpeedBonus=.25f;
    Hero->MonsterAttack->ShieldSeconds=5.f;
    Hero->MonsterAttack->TeamSpeedSeconds=5.f;
    if (Ultimate==EEGMonsterUltimate::CrystalSeed)
    {
        Hero->DisplayName=FText::FromString(TEXT("侵蚀花妖"));
        Hero->MonsterAttack->AttackType=EEGMonsterAttackType::Projectile;
        Hero->MonsterAttack->UltimateMultiplier=1.3f;
    }
    return Hero;
}
}

UEGRunDefinition* EGSevenStage::Build(UObject* Outer,UEGRunDefinition* Seed)
{
    if (!Outer || !Seed || !Seed->StartingCard || !Seed->StartingCard->HeroDefinition) return nullptr;
    auto* Run = DuplicateObject<UEGRunDefinition>(Seed,Outer);
    Run->bSevenStageRules=true;
    Run->bRoundShopRules=true;
    Run->bUseFixedPopulation=false;
    // Preserve a configured opening while keeping the legacy random opening
    // for seeds that do not opt in to explicit grants.
    Run->StartingCards=Run->bUseExplicitStartingCards ? Run->ExplicitStartingCards.Num() : 2;
    Run->StartingGold=FMath::Max(0,UEGStigmataBalanceSettings::Get().StartingGold);
    Run->StartingCorruption=50;
    Run->CardRules.DeploymentLimit=3;
    Run->CardPool.RemoveAll([](const TObjectPtr<UEGCardDefinition>& C)
    {
        if (!C || !C->HeroDefinition) return true;
        switch (C->HeroDefinition->HeroKind)
        {
        case EEGHeroKind::Elysia: case EEGHeroKind::Hua: case EEGHeroKind::Pardofelis:
        case EEGHeroKind::Eden: case EEGHeroKind::Sakura: case EEGHeroKind::VillV:
        case EEGHeroKind::Griseo: case EEGHeroKind::Mobius: case EEGHeroKind::Aponia: return false;
        default: return true;
        }
    });
    Run->CardPool.AddUnique(Run->StartingCard);
    if (Run->CardPool.Num()!=9) return nullptr;
    Run->Stages.Reset();
    TMap<EEGEncounterPawn,UEGHeroDefinition*> Pawns;
    Pawns.Add(EEGEncounterPawn::Chest, MakeMonster(Run,TEXT("Melee"),TEXT("CampaignChest"),EEGMonsterUltimate::Shield,1.4f,.75f));
    Pawns.Add(EEGEncounterPawn::Eye, MakeMonster(Run,TEXT("Ranged"),TEXT("CampaignEye"),EEGMonsterUltimate::EnhancedShot,.8f,1.15f));
    Pawns.Add(EEGEncounterPawn::Spore, MakeMonster(Run,TEXT("Support"),TEXT("CampaignSpore"),EEGMonsterUltimate::TeamSpeed,.9f,.9f));
    Pawns.Add(EEGEncounterPawn::Flower, MakeMonster(Run,TEXT("Support"),TEXT("CampaignFlower"),EEGMonsterUltimate::CrystalSeed,.9f,.9f));
    auto* Corruption = LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
    Pawns.Add(EEGEncounterPawn::Boss,Corruption);
    Pawns.Add(EEGEncounterPawn::Copy,Corruption);
    for (const auto& Pair : Pawns) if (!Pair.Value) return nullptr;
    const auto& Rows=GetDefault<UEGSevenStageDefinition>()->Stages;
    if (Rows.Num()!=7) return nullptr;
    for (const auto& Row : Rows)
    {
        auto& Stage=Run->Stages.AddDefaulted_GetRef();
        const int32 Number=Run->Stages.Num();
        TMap<EEGEncounterPawn,UEGHeroDefinition*> StagePawns=Pawns;
        const auto& HealthMultipliers=UEGStigmataBalanceSettings::Get().OrdinaryStageHealthMultipliers;
        const float HealthScale=HealthMultipliers.IsValidIndex(Number-1)?FMath::Max(.01f,HealthMultipliers[Number-1]):1.f;
        for(const EEGEncounterPawn Kind:{EEGEncounterPawn::Chest,EEGEncounterPawn::Eye,EEGEncounterPawn::Spore,EEGEncounterPawn::Flower})
        {
            auto* Original=Pawns.FindRef(Kind);
            auto* Scoped=DuplicateObject<UEGHeroDefinition>(Original,Run);
            if(!Scoped||!Original->StatsDefinition)return nullptr;
            Scoped->StatsDefinition=DuplicateObject<UEGHeroStatsDefinition>(Original->StatsDefinition,Scoped);
            if(!Scoped->StatsDefinition)return nullptr;
            Scoped->StatsDefinition->BaseStats.MaxHealth*=HealthScale;
            Scoped->StatsDefinition->GrowthPerLevel.MaxHealth*=HealthScale;
            StagePawns.Add(Kind,Scoped);
            Stage.OrdinaryMonsterDefinitions.Add(Scoped);
        }
        Stage.StageId=FName(*FString::Printf(TEXT("Campaign_%02d"),Number));
        Stage.DisplayName=FText::FromString(FString::Printf(TEXT("第 %d 关"),Number));
        Stage.StageBattleTimeLimit=Row.Seconds;
        Stage.CompletionGold=Row.Income;
        Stage.LowCorruptionRemoveIndex=Row.RemoveIndex;
        Stage.bHasHighCorruptionEnemy=Number<7;
        auto MakeSpawn=[&](const FEGEncounterPosition& Position)
        {
            FEGEnemySpawn Spawn;
            Spawn.Definition=StagePawns.FindRef(Position.Pawn);
            Spawn.Cell=Position.Cell;
            Spawn.Level=Row.Level;
            Spawn.bCorruptedCopy=Position.Pawn==EEGEncounterPawn::Copy;
            return Spawn;
        };
        for (const auto& Position : Row.Enemies) Stage.Enemies.Add(MakeSpawn(Position));
        if (Number<7) Stage.HighCorruptionEnemy=MakeSpawn(Row.Extra);
    }
    return Run;
}
