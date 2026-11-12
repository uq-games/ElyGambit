#include "Game/EGHeroRoster.h"

#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGMatchManager.h"
#include "Engine/World.h"

bool UEGHeroRoster::CanEdit() const
{
	const AEGChessGameMode* GM = GetWorld() ? GetWorld()->GetAuthGameMode<AEGChessGameMode>() : nullptr;
	return !bHandSettlementLocked&&(!GM || (GM->GetMatch() && GM->GetMatch()->GetPhase() == EEGMatchPhase::Preparation));
}

FEGHeroInstance* UEGHeroRoster::FindMutable(FGuid Id)
{
	return Heroes.FindByPredicate([Id](const FEGHeroInstance& H) { return H.InstanceId == Id; });
}

FGuid UEGHeroRoster::AddHero(UEGHeroDefinition* Definition)
{
	if (!IsValid(Definition) || !CanEdit() || FindHeroByDefinition(Definition).IsValid())
	{
		return FGuid();
	}
	FEGHeroInstance& Hero = Heroes.AddDefaulted_GetRef();
	Hero.InstanceId = FGuid::NewGuid();
	Hero.Definition = Definition;
	if(FormPermissions.Contains(Definition->HeroId)&&Definition->EnhancedDefinition)Hero.Form=EEGHeroForm::Enhanced;
	const FGuid Id = Hero.InstanceId;
	NotifyHeroChanged(Id);
	return Id;
}

bool UEGHeroRoster::GetHero(FGuid Id, FEGHeroInstance& OutHero) const
{
	const FEGHeroInstance* Hero = Heroes.FindByPredicate([Id](const FEGHeroInstance& H) { return H.InstanceId == Id; });
	if (!Hero)
	{
		return false;
	}
	OutHero = *Hero;
	return true;
}

bool UEGHeroRoster::AddLevels(FGuid Id, int32 Amount)
{
	const auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
	if(GM&&GM->GetMatch()&&GM->GetMatch()->IsStigmataCommitting())return false;
	FEGHeroInstance* Hero = FindMutable(Id);
	if (!Hero || Amount <= 0 || Hero->Level > MAX_int32 - Amount)
	{
		return false;
	}
	Hero->Level += Amount;
	NotifyHeroChanged(Id);
	return true;
}

bool UEGHeroRoster::ApplySameNameCard(FGuid Id)
{
	FEGHeroInstance* Hero = FindMutable(Id);
	const int32 Gain=Hero&&Hero->Definition?(MergeLevelOverride>0?MergeLevelOverride:Hero->Definition->GetSameNameLevelGain()):0;
	if (!CanEdit() || !Hero || !Hero->Definition || Gain < 1 ||
        Hero->Level > MAX_int32 - Gain || Hero->SameNameCards == MAX_int32)
	{
		return false;
	}
	Hero->Level += Gain;
	++Hero->SameNameCards;
	Hero->bAwakened = Hero->SameNameCards >= FMath::Max(1,Hero->Definition->GetAwakeningCardCount());
	NotifyHeroChanged(Id);
	return true;
}

bool UEGHeroRoster::SellHero(FGuid Id)
{
 // Seven-stage sales must go through CardManager so the roster, board and
 // economy change together, even during reentrant delegate callbacks.
 const auto* Demo=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 if(Demo&&Demo->RunDefinition&&Demo->RunDefinition->bSevenStageRules)return false;
 return SellHeroFromCardManager(Id);
}
bool UEGHeroRoster::SellHeroFromCardManager(FGuid Id)
{
 const auto* Hero=FindMutable(Id);
 if(!CanEdit()||!Hero||!Hero->Definition||Hero->Definition->HeroId==FName(TEXT("Elysia"))||!RemoveUncommittedHero(Id))return false;
 NotifyHeroChanged(Id);return true;
}

bool UEGHeroRoster::SetDeployment(FGuid Id, FIntPoint Cell)
{
	FEGHeroInstance* Hero = FindMutable(Id);
	if (!Hero || !CanEdit())
	{
		return false;
	}
	Hero->bDeployed = true;
	Hero->DeploymentCell = Cell;
	NotifyHeroChanged(Id);
	return true;
}

void UEGHeroRoster::ResetRun()
{
	Heroes.Reset();
	FormPermissions.Reset();MergeLevelOverride=0;
	bStartingCardsIssued=false;
	NotifyHeroChanged(FGuid());
}

FGuid UEGHeroRoster::FindHeroByDefinition(UEGHeroDefinition* Definition) const
{
 for(const auto& H:Heroes)if(H.Definition==Definition || (H.Definition&&Definition&&((!Definition->HeroId.IsNone()&&H.Definition->HeroId==Definition->HeroId)||H.Definition->EnhancedDefinition==Definition||Definition->EnhancedDefinition==H.Definition)))return H.InstanceId;
 return FGuid();
}
UEGHeroDefinition* UEGHeroRoster::ResolveDefinition(FGuid Id) const
{
 FEGHeroInstance H;if(!GetHero(Id,H)||!H.Definition)return nullptr;
 return H.Form==EEGHeroForm::Enhanced&&H.Definition->EnhancedDefinition?H.Definition->EnhancedDefinition.Get():H.Definition.Get();
}
bool UEGHeroRoster::Withdraw(FGuid Id)
{
 auto* H=FindMutable(Id);if(!CanEdit()||!H)return false;H->bDeployed=false;NotifyHeroChanged(Id);return true;
}
bool UEGHeroRoster::SetForm(FGuid Id,EEGHeroForm Form)
{
 auto* H=FindMutable(Id);if(!H||!CanEdit()||(Form==EEGHeroForm::Enhanced&&!H->Definition->EnhancedDefinition))return false;
 H->Form=Form;NotifyHeroChanged(Id);return true;
}
bool UEGHeroRoster::RemoveUncommittedHero(FGuid Id)
{
 if(!CanEdit())return false;return Heroes.RemoveAll([Id](const FEGHeroInstance& H){return H.InstanceId==Id;})>0;
}
bool UEGHeroRoster::RemoveOverflowReserve(FGuid Id)
{
 const auto* Hero=FindMutable(Id);
 if(!bHandSettlementLocked||!Hero||Hero->bDeployed||!Hero->Definition||Hero->Definition->HeroId==FName(TEXT("Elysia")))return false;
 Heroes.RemoveAll([Id](const FEGHeroInstance& H){return H.InstanceId==Id;});
 NotifyHeroChanged(Id);return true;
}
void UEGHeroRoster::RestoreSnapshot(const TArray<FEGHeroInstance>& Snapshot)
{ Heroes=Snapshot;NotifyHeroChanged(FGuid()); }

FEGHeroRosterRunSnapshot UEGHeroRoster::CaptureRunState() const
{
 FEGHeroRosterRunSnapshot Snapshot;Snapshot.Heroes=Heroes;Snapshot.FormPermissions=FormPermissions;
 Snapshot.MergeLevelOverride=MergeLevelOverride;
 Snapshot.bStartingCardsIssued=bStartingCardsIssued;
 return Snapshot;
}
void UEGHeroRoster::RestoreRunState(const FEGHeroRosterRunSnapshot& Snapshot)
{
 Heroes=Snapshot.Heroes;FormPermissions=Snapshot.FormPermissions;
 MergeLevelOverride=Snapshot.MergeLevelOverride;
 bStartingCardsIssued=Snapshot.bStartingCardsIssued;
 NotifyHeroChanged(FGuid());
}

void UEGHeroRoster::NotifyHeroChanged(FGuid Id)
{if(MutationDepth>0)PendingChanges.AddUnique(Id);else OnHeroChanged.Broadcast(Id);}
void UEGHeroRoster::EndMutation()
{
 check(MutationDepth>0);if(--MutationDepth>0)return;
 const auto Completed=MoveTemp(PendingChanges);PendingChanges.Reset();
 for(const FGuid Id:Completed)OnHeroChanged.Broadcast(Id);
}
bool UEGHeroRoster::UnlockForm(UEGHeroDefinition* Definition)
{
 auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 if(!Definition||!Definition->EnhancedDefinition||!AEGCardManager::CanUnlockFormCard(Definition)||!GM||!GM->GetMatch()||
    (GM->GetMatch()->GetPhase()!=EEGMatchPhase::Resolution&&GM->GetMatch()->GetPhase()!=EEGMatchPhase::Reward))return false;
 FormPermissions.Add(Definition->HeroId);
 if(auto* Hero=FindMutable(FindHeroByDefinition(Definition))){Hero->Form=EEGHeroForm::Enhanced;NotifyHeroChanged(Hero->InstanceId);}
 else NotifyHeroChanged(FGuid());
 return true;
}

bool UEGHeroRoster::IsFormUnlocked(const UEGHeroDefinition* Definition) const
{return Definition&&Definition->EnhancedDefinition&&FormPermissions.Contains(Definition->HeroId);}

bool UEGHeroRoster::SetVillVLayout(FGuid Id,FIntPoint Person,FIntPoint Gatling,FIntPoint Cannon)
{
 auto* H=FindMutable(Id);
 if(!CanEdit()||!H||!H->Definition||H->Definition->HeroKind!=EEGHeroKind::VillV||Person==Gatling||Person==Cannon||Gatling==Cannon)return false;
 H->bVillVLayout=true;H->bDeployed=true;H->DeploymentCell=Person;H->GatlingCell=Gatling;H->CannonCell=Cannon;NotifyHeroChanged(Id);return true;
}
