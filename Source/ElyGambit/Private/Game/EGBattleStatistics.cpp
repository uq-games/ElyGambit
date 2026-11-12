#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Data/EGHeroDefinition.h"
#include "Components/EGHeroSkillComponent.h"

TArray<FEGHeroCombatStatistics> AEGDemoGameMode::GetCombatStatistics() const
{
 TArray<FEGHeroCombatStatistics> Rows;CombatStatistics.GenerateValueArray(Rows);
 Rows.Sort([](const FEGHeroCombatStatistics& A,const FEGHeroCombatStatistics& B){return A.Damage>B.Damage;});return Rows;
}
void AEGDemoGameMode::TrackCombatResolution(const FEGCombatResolution& Result)
{
 if(!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Combat)return;
 auto Resolve=[this](FGuid Id)->AEGChessUnit*{for(const auto& U:GetManagedUnits())if(U.IsValid()&&U->GetInstanceId()==Id)return U.Get();return nullptr;};
 auto Row=[this](AEGChessUnit* Unit)->FEGHeroCombatStatistics*
 {
  if(!Unit||IEGCombatant::Execute_GetFaction(Unit)!=EEGFaction::Player)return nullptr;
  if(auto* Device=Cast<AEGVillVEmplacement>(Unit);Device&&Device->Pilot.IsValid())Unit=Device->Pilot.Get();
  const FGuid Owner=Unit->GetHeroSkill()->SummonerId.IsValid()?Unit->GetHeroSkill()->SummonerId:Unit->GetInstanceId();
  auto& R=CombatStatistics.FindOrAdd(Owner);R.HeroId=Owner;
  FEGHeroInstance H;R.Name=GetRoster()&&GetRoster()->GetHero(Owner,H)&&H.Definition?H.Definition->DisplayName:Unit->GetDefinition()->DisplayName;
  return &R;
 };
 if(auto* R=Row(Resolve(Result.SourceId)))
 {if(Result.bHealing)R->Healing+=FMath::Max(0.f,Result.HealthDelta);else R->Damage+=FMath::Max(0.f,-Result.HealthDelta);}
 if(!Result.bHealing)if(auto* R=Row(Resolve(Result.TargetId))){R->Taken+=FMath::Max(0.f,-Result.HealthDelta);R->Shield+=Result.ShieldAbsorbed;}
 OnCombatStatisticsChanged.Broadcast();
}
