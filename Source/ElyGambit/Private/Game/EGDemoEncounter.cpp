#include "Game/EGDemoGameMode.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Data/EGStigmataBalance.h"
#include "Data/EGHeroGrowthSettings.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGSettlementPresentation.h"
#include "TimerManager.h"

namespace
{
EEGHeroKind BaseKind(EEGHeroKind Kind)
{
    switch (Kind)
    {
    case EEGHeroKind::CollapsedHua: return EEGHeroKind::Hua;
    case EEGHeroKind::GriseoAdult: return EEGHeroKind::Griseo;
    case EEGHeroKind::ElysiaHerrscher: return EEGHeroKind::Elysia;
    default: return Kind;
    }
}

UEGHeroDefinition* BaseHero(const UEGRunDefinition& Run, EEGHeroKind Kind)
{
    for (const auto& Card : Run.CardPool)
        if (Card && Card->HeroDefinition && Card->HeroDefinition->HeroKind == BaseKind(Kind))
            return Card->HeroDefinition;
    return nullptr;
}

EEGCorruptionRole CopyRole(const UEGHeroDefinition& Hero)
{
    switch (Hero.GetGrowthType())
    {
    case EEGGrowthType::Support: return EEGCorruptionRole::Support;
    case EEGGrowthType::Backline: return EEGCorruptionRole::Ranged;
    default: return EEGCorruptionRole::Melee;
    }
}

UEGHeroDefinition* Monster(const UEGRunDefinition& Run, EEGMonsterUltimate Ultimate,int32 StageNumber)
{
    const int32 Index=StageNumber-1;
    if(Run.Stages.IsValidIndex(Index))
        for(const auto& Definition:Run.Stages[Index].OrdinaryMonsterDefinitions)
            if(Definition&&Definition->MonsterAttack&&Definition->MonsterAttack->Ultimate==Ultimate)return Definition;
    for (const auto& Stage : Run.Stages)
        for (const auto& Slot : Stage.Enemies)
            if (Slot.Definition && Slot.Definition->MonsterAttack && Slot.Definition->MonsterAttack->Ultimate == Ultimate)
                return Slot.Definition;
    return nullptr;
}

FIntPoint CopyCell(AEGBoard& Board, const UEGHeroDefinition& Hero, int32 PreferredX)
{
    const int32 Row = Hero.GetGrowthType() == EEGGrowthType::Frontline ? 4 : 6;
    FIntPoint Best(INDEX_NONE,INDEX_NONE);
    int32 Lowest = MAX_int32;
    for (int32 Y = 4; Y <= 6; ++Y)
        for (int32 X = 0; X < 7; ++X)
        {
            const FIntPoint Cell(X,Y);
            const int32 Score = 8 * FMath::Abs(Y-Row) + FMath::Abs(X-PreferredX);
            if (Score >= Lowest) continue;
            TArray<FIntPoint> Footprint;
            if (!AEGDeploymentBoard::MakeFootprintCells(Cell, Hero.GetPlacementFootprint(), Footprint)) continue;
            if (Footprint.ContainsByPredicate([&](FIntPoint C){return !Board.IsDeploymentCell(C,EEGFaction::Enemy) || Board.GetOccupant(C);})) continue;
            Lowest = Score;
            Best = Cell;
        }
    return Best;
}
}

void AEGDemoGameMode::ResetEncounterHistory()
{
    ClearBossWave();
    BossWaveStartedRound.Invalidate();
    ClearEncounterDisguises();
    EncounterDeviceCells.Reset();
    EncounterHistory.Reset();
    EncounterBoss.Reset();
    EncounterCopies.Reset();

}

void AEGDemoGameMode::ClearEncounterDisguises()
{
    for (const auto& Entry:EncounterCopies)if(Entry.IsValid())Entry->SetActorHiddenInGame(false);
    for (const auto& Entry:EncounterDisguises)if(IsValid(Entry.Value.Get()))Entry.Value->Destroy();
    EncounterDisguises.Reset();
}

void AEGDemoGameMode::RestoreEncounterAfterFailedStart()
{
    if(!RunDefinition||!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Preparation)return;
    for(const auto& Entry:EncounterCopies)if(Entry.IsValid())DiscardPreparedUnit(Entry.Get());
    EncounterCopies.Reset();
    ClearEncounterDisguises();
    const int32 Index=GetMatch()->GetStageNumber()-1;
    if(!RunDefinition->Stages.IsValidIndex(Index))return;
    for(const auto& Slot:RunDefinition->Stages[Index].Enemies)
        if(Slot.bCorruptedCopy&&!Board->GetOccupant(Slot.Cell))SpawnEnemy(Slot.Definition,Slot.Level,Slot.Cell);
}

bool AEGDemoGameMode::TransformEncounterCopies()
{
    if (!RunDefinition || !RunDefinition->bSevenStageRules) return true;
    ClearEncounterDisguises();
    EncounterDeviceCells.Reset();
    const auto& Stage = RunDefinition->Stages[GetMatch()->GetStageNumber()-1];
    TArray<FEGEnemySpawn> Slots = Stage.Enemies.FilterByPredicate([](const FEGEnemySpawn& Slot){return Slot.bCorruptedCopy;});
    EncounterBoss.Reset();
    if (Slots.IsEmpty())
    {
        EncounterCopies.Reset();
        if (GetMatch()->GetStageNumber()==7 && !Stage.Enemies.IsEmpty())
            EncounterBoss = Board->GetOccupant(Stage.Enemies[0].Cell);
        return GetMatch()->GetStageNumber()!=7 || EncounterBoss.IsValid();
    }
    // A preparation retry after the core transition failed must re-read the current board.
    for (auto& Previous : EncounterCopies)
        if (Previous.IsValid()) DiscardPreparedUnit(Previous.Get());
    EncounterCopies.Reset();

    TArray<FEGHeroInstance> Candidates;
    for (const auto& Hero : GetRoster()->GetHeroes())
    {
        auto* Actual = Board->GetOccupant(Hero.DeploymentCell);
        if (Hero.bDeployed && Actual && Actual->GetInstanceId()==Hero.InstanceId && Hero.Definition &&
            BaseHero(*RunDefinition,Hero.Definition->HeroKind))
            Candidates.Add(Hero);
    }
    TArray<AEGChessUnit*> Disguises;
    for (const auto& Slot : Slots)
    {
        auto* Disguise=Board->GetOccupant(Slot.Cell);
        if (!Disguise || !DetachPreparedUnitForOpening(Disguise))
        {
            for (auto* Detached:Disguises)if(IsValid(Detached))Detached->Destroy();
            for (const auto& Original:Slots)
                if(!Board->GetOccupant(Original.Cell))SpawnEnemy(Original.Definition,Original.Level,Original.Cell);
            return false;
        }
        Disguises.Add(Disguise);
    }
    TSet<EEGHeroKind> Picked;
    const int32 E = GetMatch()->GetCorruption();
    const FRandomStream Before = EncounterRandom;
    for (int32 SlotIndex=0;SlotIndex<Slots.Num();++SlotIndex)
    {
        const auto& Slot=Slots[SlotIndex];
        TArray<int32> Pool;
        for (int32 I=0; I<Candidates.Num(); ++I)
            if (!Picked.Contains(BaseKind(Candidates[I].Definition->HeroKind))) Pool.Add(I);
        if (Pool.IsEmpty()) for (int32 I=0; I<Candidates.Num(); ++I) Pool.Add(I);
        auto* Definition = Monster(*RunDefinition,EEGMonsterUltimate::Shield,GetMatch()->GetStageNumber());
        int32 Level = Slot.Level;
        const bool bCopy = !Pool.IsEmpty();
        if (bCopy)
        {
            const int32 Index = Pool[EncounterRandom.RandRange(0,Pool.Num()-1)];
            const auto Hero = Candidates[Index];
            Definition = BaseHero(*RunDefinition,Hero.Definition->HeroKind);
            Picked.Add(Definition->HeroKind);
            const auto& Tuning=UEGHeroGrowthSettings::Get();
            Level=Tuning.ResolveEliteLevel(GetMatch()->GetStageNumber(),Hero.Level,E);
            Definition=Tuning.MakeEliteDefinition(this,Definition,GetMatch()->GetStageNumber());
            Candidates.RemoveAt(Index);
        }
        if(!bCopy&&Definition){Definition=DuplicateObject<UEGHeroDefinition>(Definition,this);Definition->bEliteEnemy=true;}
        auto* Unit = Definition ? SpawnEnemy(Definition,Level,CopyCell(*Board,*Definition,Slot.Cell.X)) : nullptr;
        if (!Unit)
        {
            for (auto& Created : EncounterCopies) if (Created.IsValid()) DiscardPreparedUnit(Created.Get());
            EncounterCopies.Reset();
            for (auto* Disguise:Disguises)if(IsValid(Disguise))Disguise->Destroy();
            EncounterDisguises.Reset();
            for (const auto& Original : Slots) SpawnEnemy(Original.Definition,Original.Level,Original.Cell);
            EncounterRandom = Before;
            return false;
        }
        EncounterCopies.Add(Unit);
        Unit->SetActorHiddenInGame(true);
        EncounterDisguises.Add(Unit->GetInstanceId(),Disguises[SlotIndex]);
        Unit->OnCombatResolved.AddUniqueDynamic(this,&ThisClass::TrackCombatResolution);
    }
    return true;
}

void AEGDemoGameMode::ActivateEncounterCopy(AEGChessUnit* Unit)
{
    if (!RunDefinition || !RunDefinition->bSevenStageRules || !IsValid(Unit) ||
        !Unit->GetDefinition() || Unit->GetDefinition()->MonsterAttack) return;
    auto* Hero=Unit->GetDefinition();
    const int32 Level=Unit->GetHeroSkill()->GetLevel();
    UEGCorruptionCombatComponent::AttachCopy(Unit,BattleRules,CopyRole(*Hero));
    if (Hero->HeroKind == EEGHeroKind::VillV && Board && Board->LayoutSource)
    {
        TArray<FIntPoint> Available;
        TSet<FIntPoint> LiveUnitCells;
        for (auto* Other:BattleRules->Units())
        {
            FIntPoint Cell;
            if (IsValid(Other) && IEGCombatant::Execute_IsAlive(Other) &&
                Board->LayoutSource->TryWorldToCell(Other->GetActorLocation(),Cell))
                LiveUnitCells.Add(Cell);
        }
        for (int32 Y=0;Y<Board->Dimensions.Y;++Y)
            for (int32 X=0;X<Board->Dimensions.X;++X)
            {
                const FIntPoint Cell(X,Y);
                if (Board->IsDeploymentCell(Cell,EEGFaction::Enemy) && !Board->GetOccupant(Cell) &&
                    !LiveUnitCells.Contains(Cell) && !EncounterDeviceCells.Contains(Cell))
                    Available.Add(Cell);
            }
        Available.Sort([&](const FIntPoint& A,const FIntPoint& B)
        {
            return FVector::DistSquared2D(Board->CellToWorld(A),Unit->GetActorLocation()) <
                FVector::DistSquared2D(Board->CellToWorld(B),Unit->GetActorLocation());
        });
        if (Available.Num()>=2)
            if (auto* Mechanism=Unit->FindComponentByClass<UEGVillVMechanismComponent>())
                if (Mechanism->StartEnemyLayout(BattleRules,Available[0],Available[1]))
                {
                    EncounterDeviceCells.Add(Available[0]);
                    EncounterDeviceCells.Add(Available[1]);
                }
    }
    FEGHeroInstance& History=EncounterHistory.AddDefaulted_GetRef();
    History.Definition=Hero;
    History.Level=Level;
    History.InstanceId=Unit->GetInstanceId();
}

void AEGDemoGameMode::ActivateEncounterCombat()
{
    if (!RunDefinition || !RunDefinition->bSevenStageRules) return;
    if (auto* Boss=EncounterBoss.Get())
    {
        auto* Reference=BaseHero(*RunDefinition,EEGHeroKind::Elysia);
        if (!Reference) return;
        const auto Stats=Reference->EvaluateStats(6);
        UEGCorruptionCombatComponent::AttachBoss(Boss,BattleRules,Stats.MaxHealth*12.f,Stats.AttackPower*1.1f,GetMatch()->GetCorruption());
    }
}

void AEGDemoGameMode::UpdateBossCorruption()
{
    if (auto* Boss = EncounterBoss.Get())
        if (auto* Component = Boss->FindComponentByClass<UEGCorruptionCombatComponent>())
            Component->UpdateCorruption(GetMatch()->GetCorruption());
}

void AEGDemoGameMode::SpawnBossWave()
{
    auto* Boss = EncounterBoss.Get();
    if (!IsValid(Boss)||!IEGCombatant::Execute_IsAlive(Boss)||!BattleRules||!BattleRules->IsBattleActive()||
        !RunDefinition||!Board||!GetWorld()) return;
    const FGuid Round=BattleRules->GetRoundId();
    if(!Round.IsValid()||BossWaveStartedRound==Round)return;
    ClearBossWave();
    BossWaveStartedRound=BossWaveRound=Round;
    auto Enqueue=[&](UEGHeroDefinition* Hero,int32 Level,FIntPoint Cell,bool bFlower,bool bPhantom)
    {
        if(!Hero)return;
        FBossWaveSpawn Entry;Entry.Definition=Hero;Entry.Level=Level;Entry.Cell=Cell;
        Entry.bFlower=bFlower;Entry.bPhantom=bPhantom;PendingBossWave.Add(Entry);
    };
    Enqueue(Monster(*RunDefinition,EEGMonsterUltimate::CrystalSeed,GetMatch()->GetStageNumber()),5,{1,5},true,false);
    if (!EncounterHistory.IsEmpty())
    {
        const auto& Record = EncounterHistory[EncounterRandom.RandRange(0,EncounterHistory.Num()-1)];
        if(Record.Definition)
        {
            const int32 Row=Record.Definition->GetGrowthType()==EEGGrowthType::Frontline?4:6;
            Enqueue(Record.Definition,Record.Level,{5,Row},false,true);
        }
    }
    if (GetMatch()&&GetMatch()->GetCorruption()>70)
        Enqueue(Monster(*RunDefinition,EEGMonsterUltimate::Shield,GetMatch()->GetStageNumber()),5,{3,4},false,false);
    Boss->OnDied.AddUniqueDynamic(this,&ThisClass::BossWaveBossDied);
    RetryBossWave();
    if(!PendingBossWave.IsEmpty())
        GetWorld()->GetTimerManager().SetTimer(BossWaveRetryTimer,this,&ThisClass::RetryBossWave,.25f,true);
}

void AEGDemoGameMode::ClearBossWave()
{
    if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(BossWaveRetryTimer);
    if(EncounterBoss.IsValid())EncounterBoss->OnDied.RemoveDynamic(this,&ThisClass::BossWaveBossDied);
    PendingBossWave.Reset();
    BossWaveRound.Invalidate();
}

void AEGDemoGameMode::BossWaveBossDied(AEGChessUnit* Unit)
{
    if(Unit&&Unit==EncounterBoss.Get())ClearBossWave();
}

void AEGDemoGameMode::RetryBossWave()
{
    auto* Boss=EncounterBoss.Get();
    if(!IsValid(Boss)||!IEGCombatant::Execute_IsAlive(Boss)||!BattleRules||!BattleRules->IsBattleActive()||
       BattleRules->GetRoundId()!=BossWaveRound||!Board)
    {ClearBossWave();return;}
    for(int32 Index=0;Index<PendingBossWave.Num();)
    {
        const FBossWaveSpawn Entry=PendingBossWave[Index];
        auto* Hero=Entry.Definition.Get();
        if(!Hero){PendingBossWave.RemoveAt(Index);continue;}
        // SpawnSummon commits its registration and spawn feedback only after a
        // legal position is found; nullptr leaves this item pending.
        auto* Unit=BattleRules->SpawnSummon(Hero,Boss->GetInstanceId(),Entry.Level,Board->CellToWorld(Entry.Cell));
        if(!Unit){++Index;continue;}
        if(Entry.bFlower||Entry.bPhantom)Unit->GetHeroSkill()->bAllowSummonUltimate=true;
        if(Entry.bPhantom)
        {
            UEGCorruptionCombatComponent::AttachCopy(Unit,BattleRules,CopyRole(*Hero));
            const auto Stats=Hero->EvaluateStats(Entry.Level);
            auto* ASC=Unit->GetAbilitySystemComponent();
            ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),Stats.MaxHealth*.65f);
            ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Stats.MaxHealth*.65f);
            ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),Stats.AttackPower*.65f);
        }
        PendingBossWave.RemoveAt(Index);
    }
    if(PendingBossWave.IsEmpty())ClearBossWave();
}
void AEGDemoGameMode::PrepareBattleOpening()
{
    Super::PrepareBattleOpening();
    if(!BattleRules||!BattleRules->IsBattleActive())return;
    if(GetMatch())
    {
        const int32 StageNumber=GetMatch()->GetStageNumber();
        BattleRules->SetEnemyDamageCorruptionSnapshot(GetMatch()->GetCorruption(),
            RunDefinition&&RunDefinition->bSevenStageRules&&StageNumber>=1&&StageNumber<=6);
    }
    const FGuid Round=BattleRules->GetRoundId();
    for(const auto& Entry:EncounterCopies)
    {
        auto* Unit=Entry.Get();
        if(!IsValid(Unit))continue;
        const FGuid UnitId=Unit->GetInstanceId();
        const TWeakObjectPtr<AEGChessUnit> WeakUnit(Unit);
        const TWeakObjectPtr<AEGDemoGameMode> WeakMode(this);
        BattleRules->EnqueueOpening(Unit,[WeakMode,WeakUnit,Round,UnitId]()
        {
            auto* Mode=WeakMode.Get();
            auto* Copy=WeakUnit.Get();
            if(!Mode||!Copy||!Mode->BattleRules||!Mode->BattleRules->IsBattleActive()||
               Mode->BattleRules->GetRoundId()!=Round)return;
            if(auto* Disguise=Mode->EncounterDisguises.Find(UnitId))
                if(IsValid(Disguise->Get()))Disguise->Get()->Destroy();
            Mode->EncounterDisguises.Remove(UnitId);
            Copy->SetActorHiddenInGame(false);
            if(const auto* Hero=Copy->GetDefinition())
            {
                const int64 Threshold=1+int64(Hero->GetAwakeningCardCount()-1)*Hero->GetSameNameLevelGain();
                Copy->GetHeroSkill()->bAwakened=Copy->GetHeroSkill()->GetLevel()>=Threshold;
            }
        },true);
        BattleRules->EnqueueOpening(Unit,[WeakMode,WeakUnit,Round]()
        {
            auto* Mode=WeakMode.Get();
            auto* Copy=WeakUnit.Get();
            if(Mode&&Copy&&Mode->BattleRules&&Mode->BattleRules->IsBattleActive()&&
               Mode->BattleRules->GetRoundId()==Round&&Copy->GetDefinition()&&!Copy->GetDefinition()->MonsterAttack)
            {
                AEGSettlementPresentation::PresentCopyTransform(Copy);
                Mode->ActivateEncounterCopy(Copy);
            }
        });
    }
    // Last enemy queue item: all disguises have been revealed before the taunt.
    const int32 Stage=GetMatch()?GetMatch()->GetStageNumber():0;
    AEGChessUnit* Anchor=Stage==7?EncounterBoss.Get():EncounterCopies.IsEmpty()?nullptr:EncounterCopies.Last().Get();
    if((Stage==3||Stage==6||Stage==7)&&IsValid(Anchor))
    {
        const TWeakObjectPtr<AEGDemoGameMode> WeakMode(this);
        BattleRules->EnqueueFinalOpening([WeakMode,Round,Stage]()
        {
            auto* Mode=WeakMode.Get();
            if(Mode&&Mode->BattleRules&&Mode->BattleRules->IsBattleActive()&&Mode->BattleRules->GetRoundId()==Round)
                if(auto* Audio=UEGGameAudioComponent::FindFor(Mode))Audio->StartStoryDialogue(Stage);
        });
    }
}
