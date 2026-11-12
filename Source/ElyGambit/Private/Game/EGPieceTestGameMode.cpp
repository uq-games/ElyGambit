#include "Game/EGPieceTestGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGDeploymentBoard.h"
#include "UI/Widget/EGPieceTestPanel.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

AEGPieceTestGameMode::AEGPieceTestGameMode()
{
 bAutoStart=false;
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.1f;
 UnitClass=AEGDemoCombatUnit::StaticClass();
 PlayerControllerClass=AEGPieceTestPlayerController::StaticClass();
 static ConstructorHelpers::FObjectFinder<UEGHeroDefinition> Slime(TEXT("/Game/Blueprints/Characters/Enemies/Melee/Data/DA_Monster_Melee"));
 static ConstructorHelpers::FObjectFinder<UEGHeroDefinition> Mushroom(TEXT("/Game/Blueprints/Characters/Enemies/Ranged/Data/DA_Monster_Ranged"));
 static ConstructorHelpers::FObjectFinder<UEGHeroDefinition> Pardo(TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"));
 AvailablePieces={Pardo.Object};
 FirstMonster=Slime.Object;SecondMonster=Mushroom.Object;
}

void AEGPieceTestGameMode::StartPlay()
{
 if(GetRoster())PreviousRoster=GetRoster()->CaptureRunState();
 Super::StartPlay();
 for(TActorIterator<AEGDeploymentBoard> It(GetWorld());It;++It){Board->UseLayout(*It);break;}
 GetMatch()->ConfigureRun(1,true);
 if(!StartGame())UE_LOG(LogTemp,Error,TEXT("PieceTest: failed to create one hero and two monsters"));
}

bool AEGPieceTestGameMode::PopulatePlayer()
{
 if(!AvailablePieces.IsValidIndex(SelectedIndex)||!AvailablePieces[SelectedIndex])return false;
 const FGuid Id=GetRoster()->AddHero(AvailablePieces[SelectedIndex]);
 if(!Id.IsValid()||!GetRoster()->SetDeployment(Id,FIntPoint(2,1)))return false;
 return SpawnRosterHero(Id)!=nullptr;
}

bool AEGPieceTestGameMode::StartGame(bool bResetRoster)
{
 // Validate before destroying the current test. Never modify the shared data assets.
 FString Error;
 if(!AvailablePieces.IsValidIndex(SelectedIndex)||!AvailablePieces[SelectedIndex]||!FirstMonster||!SecondMonster||
    !AvailablePieces[SelectedIndex]->ValidateDefinition(Error)||!FirstMonster->ValidateDefinition(Error)||!SecondMonster->ValidateDefinition(Error))return false;
 if(!Super::StartGame(true))return false;
 const bool bReady=PopulatePlayer()&&GetBattleUnits().Num()==3;
 if(!bReady)ClearUnits();
 return bReady;
}

void AEGPieceTestGameMode::PrepareStageEnemies()
{
 SpawnEnemy(FirstMonster,1,FIntPoint(2,4));
 SpawnEnemy(SecondMonster,1,FIntPoint(3,4));
}

bool AEGPieceTestGameMode::ResetTest()
{
 if(bResetting||bCoordinating||!GetMatch()||!GetRoster())return false;
 TGuardValue<bool> Guard(bResetting,true);
 if(GetMatch()->GetPhase()==EEGMatchPhase::Combat&&!ResolveCombat(false))return false;
 if(GetMatch()->GetPhase()==EEGMatchPhase::Finished)return StartGame();
 if(GetMatch()->GetPhase()!=EEGMatchPhase::Preparation)return false;
 ClearUnits();GetRoster()->ResetRun();PrepareStageEnemies();
 const bool bReady=PopulatePlayer()&&GetBattleUnits().Num()==3;
 if(!bReady)ClearUnits();
 return bReady;
}

bool AEGPieceTestGameMode::SelectPiece(int32 Index)
{
 if(!AvailablePieces.IsValidIndex(Index)||!AvailablePieces[Index]||bResetting||bCoordinating)return false;
 const int32 Previous=SelectedIndex;SelectedIndex=Index;
 if(ResetTest())return true;
 SelectedIndex=Previous;return false;
}

void AEGPieceTestGameMode::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
 if(FParse::Param(FCommandLine::Get(),TEXT("EGMonsterReview")))TickMonsterReview();
#endif
 if(!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Combat)return;
 bool bPlayer=false,bEnemy=false;
 for(const auto& Entry:GetBattleUnits())if(Entry.IsValid()&&IEGCombatant::Execute_IsAlive(Entry.Get()))
 {if(IEGCombatant::Execute_GetFaction(Entry.Get())==EEGFaction::Player)bPlayer=true;else bEnemy=true;}
 if(!bPlayer||!bEnemy)ResolveCombat(bPlayer);
}

FString AEGPieceTestGameMode::GetTestStatus() const
{
 if(!GetMatch()||GetBattleUnits().Num()<3)return TEXT("场景配置失败，请检查角色与怪物配置");
 const auto Phase=GetMatch()->GetPhase();
 if(Phase==EEGMatchPhase::Preparation)return TEXT("已就绪  1 名棋子 / 2 个怪物");
 if(Phase==EEGMatchPhase::Finished)return GetMatch()->DidWinRun()?TEXT("战斗胜利  可重置再测"):TEXT("战斗失败  可重置再测");
 return FString::Printf(TEXT("战斗中  普攻命中 %d"),BattleRules->GetAttackCount());
}

void AEGPieceTestGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
 ClearUnits();
 // The sandbox must not leave its test roster in the persistent game instance.
 if(GetRoster())GetRoster()->RestoreRunState(PreviousRoster);
 PreviousRoster=FEGHeroRosterRunSnapshot();Super::EndPlay(Reason);
}
