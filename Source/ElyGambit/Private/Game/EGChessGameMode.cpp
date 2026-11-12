#include "Game/EGChessGameMode.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"

#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGHeroDefinition.h"
#include "Components/EGCombatComponent.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Character/EGChessUnit.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "EngineUtils.h"

bool AEGChessGameMode::ReleasePauseRequest(FDelegateHandle Handle)
{
 if(!Handle.IsValid())return false;
 const int32 Removed=Pausers.RemoveAll([Handle](const FCanUnpause& Request){return Request.GetHandle()==Handle;});
 if(Removed>0&&Pausers.IsEmpty())if(auto* Settings=GetWorldSettings())Settings->SetPauserPlayerState(nullptr);
 return Removed>0;
}
bool AEGChessGameMode::IsOnlyPauseRequest(FDelegateHandle Handle) const
{
 return Handle.IsValid()&&Pausers.Num()==1&&Pausers[0].GetHandle()==Handle;
}

namespace
{
// A layout-backed board separates the teams along local Y. The legacy board
// separates them along local X, so do not assume a world-space compass axis.
FRotator PreparationFacing(const AEGBoard* Board, EEGFaction Faction)
{
 if (!IsValid(Board)) return FRotator::ZeroRotator;
 const FIntPoint Next = IsValid(Board->LayoutSource) ? FIntPoint(0, 1) : FIntPoint(1, 0);
 FVector Direction = Board->CellToWorld(Next) - Board->CellToWorld(FIntPoint::ZeroValue);
 Direction.Z = 0.f;
 if (Faction == EEGFaction::Enemy) Direction = -Direction;
 return Direction.IsNearlyZero() ? FRotator::ZeroRotator : Direction.Rotation();
}
}

AEGChessGameMode::AEGChessGameMode()
{
	BattleRules=CreateDefaultSubobject<UEGBattleContextComponent>(TEXT("BattleRules"));
	UnitClass = AEGChessUnit::StaticClass();
	BoardClass = AEGBoard::StaticClass();
	DefaultPawnClass = nullptr;
}

void AEGChessGameMode::InitGame(const FString& MapName, const FString& Options, FString& Error)
{
	Super::InitGame(MapName, Options, Error);
	Match = NewObject<UEGMatchManager>(this);
	Match->OnPhaseChanged.AddDynamic(this, &ThisClass::PhaseChanged);
	BattleRules->OnCombatRunning.AddDynamic(this,&ThisClass::CombatRunning);
}

UEGHeroRoster* AEGChessGameMode::GetRoster() const
{
	return GetGameInstance() ? GetGameInstance()->GetSubsystem<UEGHeroRoster>() : nullptr;
}

void AEGChessGameMode::StartPlay()
{
	for (TActorIterator<AEGBoard> It(GetWorld()); !Board && It; ++It)
	{
		Board = *It;
		break;
	}
	if (!Board)
	{
		Board = GetWorld()->SpawnActor<AEGBoard>(BoardClass);
	}
	if (UEGHeroRoster* Roster = GetRoster())
	{
		Roster->OnHeroChanged.AddDynamic(this, &ThisClass::HeroChanged);
	}
	Super::StartPlay();
	if (bAutoStart)
	{
		StartGame();
	}
}

void AEGChessGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UEGHeroRoster* Roster = GetRoster())
	{
		Roster->OnHeroChanged.RemoveDynamic(this, &ThisClass::HeroChanged);
	}
	if (Match)
	{
		Match->OnPhaseChanged.RemoveDynamic(this, &ThisClass::PhaseChanged);
	}
	BattleRules->OnCombatRunning.RemoveDynamic(this,&ThisClass::CombatRunning);
	Super::EndPlay(Reason);
}

bool AEGChessGameMode::StartGame(bool bResetRoster)
{
	if (bCoordinating || !Match || !Board || !GetRoster() ||
		(Match->HasStarted() && Match->GetPhase() != EEGMatchPhase::Finished))
	{
		return false;
	}
	TGuardValue<bool> Guard(bCoordinating, true);
	ClearUnits();
	if (bResetRoster)
	{
		GetRoster()->ResetRun();
	}
	if (!Match->StartRun())
	{
		return false;
	}
	for (const FEGHeroInstance& Hero : GetRoster()->GetHeroes())
	{
		if (Hero.bDeployed)
		{
			SpawnRosterHero(Hero.InstanceId);
		}
	}
	PrepareStageEnemies();
	return true;
}

bool AEGChessGameMode::OwnsUnit(const AEGChessUnit* Unit) const
{
	return Units.ContainsByPredicate([Unit](const TWeakObjectPtr<AEGChessUnit>& Entry) { return Entry.Get() == Unit; });
}

AEGChessUnit* AEGChessGameMode::SpawnUnit(UEGHeroDefinition* Definition, FGuid Id, EEGFaction Team, int32 Level,
										  FIntPoint Cell)
{
	if (!Match || Match->GetPhase() != EEGMatchPhase::Preparation || !Board || !Definition || !UnitClass ||
		!Board->IsDeploymentCell(Cell, Team) || Board->GetOccupant(Cell))
	{
		return nullptr;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEGChessUnit* Unit =
		GetWorld()->SpawnActor<AEGChessUnit>(Definition->CombatActorClass.IsNull()?UnitClass.Get():Definition->CombatActorClass.LoadSynchronous(), Board->CellToWorld(Cell), PreparationFacing(Board, Team), Params);
	if (!IsValid(Unit) || Unit->IsActorBeingDestroyed())
	{
		return nullptr;
	}
	Units.Add(Unit);
	if (!Unit->InitializeUnit(Definition, Id, Team, Level) || !Board->PlaceUnit(Unit, Cell))
	{
		Units.Remove(Unit);
		Unit->Destroy();
		return nullptr;
	}
	return Unit;
}

void AEGChessGameMode::DiscardPreparedUnit(AEGChessUnit* Unit)
{
	if (!IsValid(Unit) || !OwnsUnit(Unit)) return;
	if (Board) Board->RemoveUnit(Unit);
	Units.Remove(Unit);
	Unit->RestorePresentation();
	Unit->Destroy();
}

bool AEGChessGameMode::DetachPreparedUnitForOpening(AEGChessUnit* Unit)
{
 if(!Match||Match->GetPhase()!=EEGMatchPhase::Preparation||!IsValid(Unit)||!OwnsUnit(Unit))return false;
 if(Board)Board->RemoveUnit(Unit);
 Units.Remove(Unit);Unit->SetActorEnableCollision(false);
 Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->SetComponentTickEnabled(false);
 return true;
}

AEGChessUnit* AEGChessGameMode::SpawnRosterHero(FGuid Id)
{
	FEGHeroInstance Hero;
	if (!GetRoster() || !GetRoster()->GetHero(Id, Hero))
	{
		return nullptr;
	}
	for (const TWeakObjectPtr<AEGChessUnit>& Entry : Units)
	{
		if (Entry.IsValid() && Entry->GetInstanceId() == Id)
		{
			return Entry.Get();
		}
	}
	// First deployment chooses an empty friendly cell. Later rounds use the saved placement.
	FIntPoint Cell = Hero.DeploymentCell;
	if (!Hero.bDeployed && Board)
	{
		bool bFound = false;
		for (int32 X = 0; X < Board->Dimensions.X && !bFound; ++X)
		{
			for (int32 Y = 0; Y < Board->Dimensions.Y && !bFound; ++Y)
			{
				if (Board->IsDeploymentCell(FIntPoint(X,Y),EEGFaction::Player) && !Board->GetOccupant(FIntPoint(X, Y)))
				{
					Cell = FIntPoint(X, Y);
					bFound = true;
				}
			}
		}
		if (!bFound)
		{
			return nullptr;
		}
	}
	return SpawnUnit(GetRoster()->ResolveDefinition(Id), Id, EEGFaction::Player, Hero.Level, Cell);
}

AEGChessUnit* AEGChessGameMode::SpawnEnemy(UEGHeroDefinition* Definition, int32 Level, FIntPoint Cell)
{
	return SpawnUnit(Definition, FGuid::NewGuid(), EEGFaction::Enemy, Level, Cell);
}

bool AEGChessGameMode::EnterCombat()
{
	if (bCoordinating || !Match || Match->GetPhase() != EEGMatchPhase::Preparation)
	{
		return false;
	}
	bool bPlayer = false, bEnemy = false;
	for (const TWeakObjectPtr<AEGChessUnit>& Entry : Units)
	{
		if (!Entry.IsValid() || !IEGCombatant::Execute_IsAlive(Entry.Get()))
		{
			continue;
		}
		bPlayer |= IEGCombatant::Execute_GetFaction(Entry.Get()) == EEGFaction::Player;
		bEnemy |= IEGCombatant::Execute_GetFaction(Entry.Get()) == EEGFaction::Enemy;
	}
	if (!bPlayer || !bEnemy)
	{
		return false;
	}
	TGuardValue<bool> Guard(bCoordinating, true);
	for (const TWeakObjectPtr<AEGChessUnit>& Entry : Units)
	{
		FEGHeroInstance Hero;
		if (Entry.IsValid() && IEGCombatant::Execute_GetFaction(Entry.Get()) == EEGFaction::Player &&
			GetRoster()->GetHero(Entry->GetInstanceId(), Hero))
		{
			PrepareUnitForCombat(Entry.Get(),Hero);
		}
	}
	BattleRules->Start(GetRoster()->GetHeroes());
	const TSharedRef<const FEGCombatantList> Candidates=MakeShared<FEGCombatantList>(Units);
	for(const auto& Unit:Units)if(Unit.IsValid())Unit->GetCombat()->SetCombatants(Candidates);
	if(!Match->EnterCombat()){BattleRules->Stop();return false;}
	PrepareBattleOpening();
	BattleRules->BeginOpening();
	return true;
}

void AEGChessGameMode::CombatRunning(FGuid RoundId)
{
 if(!BattleRules->IsCombatRunning()||BattleRules->GetRoundId()!=RoundId||!Match||Match->GetPhase()!=EEGMatchPhase::Combat)return;
 const auto Snapshot=Units;
 for(const auto& Entry:Snapshot)
 {
  if(!BattleRules->IsCombatRunning()||BattleRules->GetRoundId()!=RoundId)return;
  if(Entry.IsValid())IEGCombatant::Execute_BeginCombat(Entry.Get());
 }
}

void AEGChessGameMode::PhaseChanged(EEGMatchPhase Phase)
{
	SettlementHumanOrigin.Reset();
	if((Phase==EEGMatchPhase::Settlement||Phase==EEGMatchPhase::Finished)&&BattleRules&&BattleRules->IsBattleActive())
	{
	 for(const TWeakObjectPtr<AEGChessUnit>& Entry:Units)
	  if(AEGChessUnit* Unit=Entry.Get();IsValid(Unit)&&Unit->IsAlive_Implementation())
	   if(const UEGHeroSkillComponent* Skill=Unit->GetHeroSkill())
	    if(const auto* Human=Cast<UEGElysiaHerrscherSkillBehavior>(Skill->GetBehavior()))
	     SettlementHumanOrigin.Add(Unit,Human->IsOriginActive());
	}
	if(Phase!=EEGMatchPhase::Combat)BattleRules->Stop();
	// Snapshot protects iteration if Blueprint callbacks destroy units or request other operations.
	const auto Snapshot = Units;
	for (const TWeakObjectPtr<AEGChessUnit>& Entry : Snapshot)
	{
		if (!Entry.IsValid())
		{
			continue;
		}
		if (Phase != EEGMatchPhase::Combat)
		{
			IEGCombatant::Execute_EndCombat(Entry.Get());
		}
	}
}

bool AEGChessGameMode::WasHumanOriginAtSettlement(AEGChessUnit* Unit) const
{
 const bool* Origin=SettlementHumanOrigin.Find(Unit);
 return Origin&&*Origin;
}

bool AEGChessGameMode::ResolveCombat(bool bVictory)
{
	if (bCoordinating || !Match)
	{
		return false;
	}
	TGuardValue<bool> Guard(bCoordinating, true);
	return Match->ResolveCombat(bVictory);
}

void AEGChessGameMode::ClearUnits()
{
	BattleRules->Stop();
	const auto Snapshot = Units;
	Units.Reset();
	if (Board)
	{
		Board->ClearBindings();
	}
	for (const TWeakObjectPtr<AEGChessUnit>& Entry : Snapshot)
	{
		if (Entry.IsValid())
		{
            Entry->RestorePresentation();
			Entry->Destroy();
		}
	}
}

bool AEGChessGameMode::NextStage()
{
	if (bCoordinating || !Match || Match->GetPhase() != EEGMatchPhase::Resolution)
	{
		return false;
	}
	TGuardValue<bool> Guard(bCoordinating, true);
	ClearUnits();
	if (!Match->NextStage())
	{
		return false;
	}
	for (const FEGHeroInstance& Hero : GetRoster()->GetHeroes())
	{
		if (Hero.bDeployed)
		{
			SpawnRosterHero(Hero.InstanceId);
		}
	}
	PrepareStageEnemies();
	return true;
}

void AEGChessGameMode::HeroChanged(FGuid Id)
{
	if (!Id.IsValid() || !GetRoster())
	{
		return;
	}
	FEGHeroInstance Hero;
	if (GetRoster()->GetHero(Id, Hero))
	{
		return; // Level/card changes apply when next combat incarnation is spawned.
	}
	for (int32 Index = Units.Num() - 1; Index >= 0; --Index)
	{
		AEGChessUnit* Unit = Units[Index].Get();
		if (Unit && Unit->GetInstanceId() == Id)
		{
			Board->RemoveUnit(Unit);
			Units.RemoveAt(Index);
			Unit->Destroy();
		}
	}
}

void AEGChessGameMode::PrepareStageEnemies()
{
 PrepareEnemies(Match->GetStageNumber(), Match->GetStageType(), Match->GetCorruption());
}

void AEGChessGameMode::PrepareUnitForCombat(AEGChessUnit* Unit,const FEGHeroInstance& Hero)
{
 Unit->RefreshPreparationLevel(Hero.Level);
 Unit->SetUltimateBonus(Hero.Definition&&Hero.Definition->HeroKind==EEGHeroKind::Generic&&Hero.bAwakened?Hero.Definition->GetAwakenedUltimateBonus():1.f);
}

AEGChessUnit* AEGChessGameMode::SpawnBattleSummon(UEGHeroDefinition* D,int32 Level,FVector Point,EEGFaction Faction,bool bRequireFreeSpace)
{
 if(!HasAuthority()||!BattleRules->IsBattleActive()||!D||Faction==EEGFaction::Neutral)return nullptr;
 FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* U=GetWorld()->SpawnActor<AEGChessUnit>(D->CombatActorClass.IsNull()?UnitClass.Get():D->CombatActorClass.LoadSynchronous(),Point,PreparationFacing(Board,Faction),Params);
 if(!U||!U->InitializeUnit(D,FGuid::NewGuid(),Faction,Level)){if(U)U->Destroy();return nullptr;}
 if(bRequireFreeSpace)
 {
  bool Found=false;const FVector Free=U->GetCombat()->FindFreeBoardLocation(Point,&Found);
  if(!Found){U->Destroy();return nullptr;}
  U->SetActorLocation(Free,false,nullptr,ETeleportType::TeleportPhysics);
 }
 PrepareSummonForCombat(U);
 Units.Add(U);const TSharedRef<const FEGCombatantList> Candidates=MakeShared<FEGCombatantList>(Units);
 for(const auto& E:Units)if(E.IsValid())E->GetCombat()->SetCombatants(Candidates);
 if(Match->GetPhase()==EEGMatchPhase::Combat&&BattleRules->IsCombatRunning())IEGCombatant::Execute_BeginCombat(U);return U;
}
void AEGChessGameMode::RemoveReserveIncarnations()
{
 if(!Match||Match->GetPhase()!=EEGMatchPhase::Preparation)return;
 for(int32 I=Units.Num()-1;I>=0;--I)
 {
  auto* U=Units[I].Get();if(!U||IEGCombatant::Execute_GetFaction(U)!=EEGFaction::Player)continue;
  FEGHeroInstance H;if(GetRoster()->GetHero(U->GetInstanceId(),H)&&H.bDeployed&&U->GetDefinition()==GetRoster()->ResolveDefinition(H.InstanceId))continue;
  Board->RemoveUnit(U);Units.RemoveAt(I);U->RestorePresentation();U->Destroy();
 }
}

AEGChessUnit* AEGChessGameMode::SpawnBattleEmplacement(UEGHeroDefinition* D,AEGChessUnit* Hero,int32 Index,FVector Point)
{
 if(!Hero||!Hero->HasAuthority())return nullptr;
 auto* U=Cast<AEGVillVEmplacement>(SpawnBattleSummon(D,Hero->GetHeroSkill()->GetLevel(),Point,IEGCombatant::Execute_GetFaction(Hero)));
 if(!U)return nullptr;
 U->SetPilot(Hero,Index);U->GetHeroSkill()->Configure(BattleRules,Hero->GetHeroSkill()->GetLevel(),false);
 auto* ASC=U->GetAbilitySystemComponent();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),Hero->GetAttributes()->GetMaxHealth()*.5f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Hero->GetAttributes()->GetMaxHealth()*.5f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),Hero->GetAttributes()->GetDefense());
 if(auto* Stigmata=BattleRules->GetStigmata())Stigmata->ApplyUnitBonuses(U,true,false);
 BattleRules->EnterUnit(U);return U;
}
