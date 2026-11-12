#include "Game/EGMatchManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGStigmataDefinition.h"

bool UEGMatchManager::ConfigureRun(int32 InStageCount, bool bInEndOnDefeat)
{
 if(bTransitioning || bStigmataCommitting || (bStarted && Phase != EEGMatchPhase::Finished)) return false;
 StageCount=FMath::Clamp(InStageCount,1,7); bEndOnDefeat=bInEndOnDefeat;RunDefinition=nullptr; return true;
}
bool UEGMatchManager::ConfigureRunDefinition(UEGRunDefinition* Definition)
{if(!Definition||!ConfigureRun(Definition->Stages.Num(),!Definition->bRoundShopRules))return false;RunDefinition=Definition;return true;}

EEGStageType UEGMatchManager::GetStageType() const
{
    return GetStageTypeAt(StageIndex+1);
}
EEGStageType UEGMatchManager::GetStageTypeAt(int32 StageNumber) const
{
    const int32 Index=StageNumber-1;
    if(Index<0||Index>=StageCount)return EEGStageType::Normal;
	if(RunDefinition&&RunDefinition->Stages.IsValidIndex(Index)&&RunDefinition->Stages[Index].bUseConfiguredStageType)return RunDefinition->Stages[Index].StageType;
	return EEGStageType::Normal;
}

void UEGMatchManager::SetPhase(EEGMatchPhase NewPhase)
{
	Phase = NewPhase;
	OnPhaseChanged.Broadcast(Phase);
}

void UEGMatchManager::PrepareStage()
{
	SetPhase(EEGMatchPhase::Preparation);
	OnStagePrepared.Broadcast(GetStageNumber(), GetStageType());

}

bool UEGMatchManager::StartRun()
{
	if (bTransitioning || bStigmataCommitting || (bStarted && Phase != EEGMatchPhase::Finished))
	{
		return false;
	}
	TGuardValue<bool> Guard(bTransitioning, true);
	StageIndex = 0;
	Corruption = RunDefinition && RunDefinition->bSevenStageRules ? FMath::Clamp(RunDefinition->StartingCorruption,0,200) : 0;
	PendingCorruptionDelta = 0;
	bSettlementImpactApplied = false;
	StigmataOwned.Reset();
	EndgameRoute=NAME_None;
	StigmataGoldEarned.Reset();
	StigmataRefreshSavings.Reset();
	bStarted = true;
	bWonRun = false;
	PrepareStage();
	return true;
}

void UEGMatchManager::RecordStigmataGoldEarned(FName Id,int32 Amount)
{
 if(Id.IsNone()||Amount<=0||!bStarted)return;
 int64& Total=StigmataGoldEarned.FindOrAdd(Id);
 Total=Total>MAX_int64-Amount?MAX_int64:Total+Amount;
}
void UEGMatchManager::RecordStigmataRefreshSavings(FName Id,int32 Amount)
{
 if(Id.IsNone()||Amount<=0||!bStarted)return;
 int64& Total=StigmataRefreshSavings.FindOrAdd(Id);
 Total=Total>MAX_int64-Amount?MAX_int64:Total+Amount;
}
#if WITH_DEV_AUTOMATION_TESTS
bool UEGMatchManager::SetStigmataForBalanceTests(const TArray<FName>& Ids)
{
 if(!bStarted||bTransitioning||bStigmataCommitting||Phase!=EEGMatchPhase::Preparation)return false;
 TSet<FName> Seen;
 for(const FName Id:Ids)
 {
  if(Id.IsNone()||Seen.Contains(Id)||Id==TEXT("OriginFlower")||Id==TEXT("Story")||Id==TEXT("Unity")||!UEGStigmataCatalog::Find(Id))return false;
  Seen.Add(Id);
 }
 StigmataOwned=Ids;
 return true;
}
#endif

bool UEGMatchManager::EnterCombat()
{
	if (bTransitioning || bStigmataCommitting || !bStarted || Phase != EEGMatchPhase::Preparation)
	{
		return false;
	}
	TGuardValue<bool> Guard(bTransitioning, true);
	SetPhase(EEGMatchPhase::Combat);
	return true;
}

bool UEGMatchManager::ResolveCombat(bool bVictory)
{
	if (bTransitioning || bStigmataCommitting || Phase != EEGMatchPhase::Combat)
	{
		return false;
	}
	TGuardValue<bool> Guard(bTransitioning, true);
	if (RunDefinition && RunDefinition->bSevenStageRules)
	{
		if (StageIndex == StageCount-1)
		{
			bWonRun = bVictory;
			SetPhase(EEGMatchPhase::Finished);
			OnBattleResolved.Broadcast(bVictory, Corruption);
			return true;
		}
		const int32 Magnitude = StageIndex < 2 ? 5 : StageIndex < 4 ? 10 : 15;
		PendingCorruptionDelta = bVictory ? -Magnitude : Magnitude;
		bSettlementImpactApplied = false;
		SetPhase(EEGMatchPhase::Settlement);
		OnBattleResolved.Broadcast(bVictory, Corruption);
		return true;
	}
	if (!bVictory && StageIndex < StageCount-1)
	{
		++Corruption;
	}
	SetPhase(EEGMatchPhase::Resolution);
	OnBattleResolved.Broadcast(bVictory, Corruption);
	if (StageIndex == StageCount - 1 || (!bVictory && bEndOnDefeat))
	{
		bWonRun = bVictory;
		SetPhase(EEGMatchPhase::Finished);
	}
	return true;
}

int32 UEGMatchManager::AddCorruption(int32 Delta)
{
 if(!bStarted || bStigmataCommitting || Phase==EEGMatchPhase::Finished)return 0;
 const int32 Previous=Corruption;
 Corruption=static_cast<int32>(FMath::Clamp<int64>(int64(Corruption)+int64(Delta),0,200));
 const int32 Applied=Corruption-Previous;
 const bool bCorruptionDefeat=RunDefinition&&RunDefinition->bSevenStageRules&&Corruption>=200;
 if(bCorruptionDefeat){bWonRun=false;SetPhase(EEGMatchPhase::Finished);}
 if(Applied!=0)OnCorruptionChanged.Broadcast(Corruption,Applied);
 if(bCorruptionDefeat)OnBattleResolved.Broadcast(false,Corruption);
 return Applied;
}

bool UEGMatchManager::ApplySettlementImpact()
{
 if(bStigmataCommitting || Phase!=EEGMatchPhase::Settlement || bSettlementImpactApplied)return false;
 bSettlementImpactApplied=true;
 AddCorruption(PendingCorruptionDelta);
 return true;
}

bool UEGMatchManager::FinishSettlement()
{
 if(bTransitioning || bStigmataCommitting || Phase!=EEGMatchPhase::Settlement)return false;
 TGuardValue<bool> Guard(bTransitioning,true);
 ApplySettlementImpact();
 if(Phase!=EEGMatchPhase::Finished)SetPhase(EEGMatchPhase::Resolution);
 return true;
}

bool UEGMatchManager::EnterReward()
{
 if(bTransitioning || bStigmataCommitting || Phase!=EEGMatchPhase::Resolution)return false;
 TGuardValue<bool> Guard(bTransitioning,true);SetPhase(EEGMatchPhase::Reward);return true;
}

bool UEGMatchManager::BeginStigmataCommit(FName Id)
{
 if(bTransitioning||bStigmataCommitting||Phase!=EEGMatchPhase::Reward||Id.IsNone()||StigmataOwned.Contains(Id))return false;
 bStigmataCommitting=true;
 PendingStigmataId=Id;
 return true;
}
bool UEGMatchManager::CommitStigmata(bool bStayInReward)
{
 if(!bStigmataCommitting||PendingStigmataId.IsNone()||Phase!=EEGMatchPhase::Reward)return false;
 StigmataOwned.Add(PendingStigmataId);
 PendingStigmataId=NAME_None;
 if(!bStayInReward)SetPhase(EEGMatchPhase::Resolution);
 bStigmataCommitting=false;
 return true;
}
bool UEGMatchManager::CommitEndgameRoute(FName Id)
{
 if(!bStigmataCommitting||PendingStigmataId!=Id||Phase!=EEGMatchPhase::Reward||!EndgameRoute.IsNone()||
    (Id!=TEXT("OriginFlower")&&Id!=TEXT("Story")&&Id!=TEXT("Unity")))return false;
 EndgameRoute=Id;
 PendingStigmataId=NAME_None;
 SetPhase(EEGMatchPhase::Resolution);
 bStigmataCommitting=false;
 return true;
}
void UEGMatchManager::AbortStigmataCommit()
{
 PendingStigmataId=NAME_None;
 bStigmataCommitting=false;
}
bool UEGMatchManager::CompleteReward()
{
 if(bTransitioning || bStigmataCommitting || Phase!=EEGMatchPhase::Reward ||
    (RunDefinition&&RunDefinition->bSevenStageRules))return false;
 TGuardValue<bool> Guard(bTransitioning,true);SetPhase(EEGMatchPhase::Resolution);return true;
}

bool UEGMatchManager::CompleteRun()
{
 if(bTransitioning||bStigmataCommitting||Phase!=EEGMatchPhase::Resolution||StageIndex!=StageCount-1)return false;
 TGuardValue<bool> Guard(bTransitioning,true);bWonRun=true;SetPhase(EEGMatchPhase::Finished);return true;
}

bool UEGMatchManager::NextStage()
{
	if (bTransitioning || bStigmataCommitting || Phase != EEGMatchPhase::Resolution || StageIndex >= StageCount - 1)
	{
		return false;
	}
	TGuardValue<bool> Guard(bTransitioning, true);
	++StageIndex;
	PrepareStage();
	return true;
}

bool UEGMatchManager::RetryStage()
{
 if(bTransitioning||bStigmataCommitting||!bStarted||Phase!=EEGMatchPhase::Finished||bWonRun||
    (RunDefinition&&RunDefinition->bSevenStageRules))return false;
 TGuardValue<bool> Guard(bTransitioning,true);PrepareStage();return true;
}
