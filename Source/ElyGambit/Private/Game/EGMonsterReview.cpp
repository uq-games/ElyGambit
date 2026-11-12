#include "Game/EGPieceTestGameMode.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGSkillProjectile.h"
#include "EngineUtils.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

// Opt-in Development package fixture. Normal play never calls this path.
void AEGPieceTestGameMode::TickMonsterReview()
{
#if !UE_BUILD_SHIPPING
 AEGChessUnit *Hero=nullptr,*Melee=nullptr,*Ranged=nullptr;
 for(const auto& Entry:GetBattleUnits())if(auto* U=Entry.Get())
 {
  if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Player)Hero=U;
  else if(U->GetDefinition()->HeroId==FName(TEXT("Monster_Melee")))Melee=U;
  else if(U->GetDefinition()->HeroId==FName(TEXT("Monster_Ranged")))Ranged=U;
 }
 if(!Hero||!Melee||!Ranged)return;
 if(MonsterReviewStarted<0)
 {
  FParse::Value(FCommandLine::Get(),TEXT("EGMonsterReviewOutput="),MonsterReviewFolder);
  if(MonsterReviewFolder.IsEmpty())MonsterReviewFolder=FPaths::ProjectSavedDir()/TEXT("MonsterReview");
  IFileManager::Get().MakeDirectory(*MonsterReviewFolder,true);
  if(!EnterCombat()){FPlatformMisc::RequestExitWithStatus(false,1);return;}
  for(auto* U:{Hero,Melee,Ranged})
  {
   auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);
  }
  Hero->GetCombat()->SetComponentTickEnabled(false);
  MonsterReviewStarted=GetWorld()->GetTimeSeconds();
  MonsterReviewRows=TEXT("scope,controlled package: raised health and player AI disabled; actual enemy movement attacks and GAS damage; enemy attack speed set to 3 after 9 seconds\nseconds,melee_actions,ranged_actions,hero_health,projectiles,melee_distance\n");
 }
 const double Seconds=GetWorld()->GetTimeSeconds()-MonsterReviewStarted;
 if(Seconds>9&&!bMonsterReviewFast)
 {
  for(auto* U:{Melee,Ranged})U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),3.f);
  bMonsterReviewFast=true;
 }
 int32 Projectiles=0;for(TActorIterator<AEGSkillProjectile> It(GetWorld());It;++It)if(!It->IsActorBeingDestroyed())++Projectiles;
 bMonsterReviewSawProjectile|=Projectiles>0;
 static const float Captures[]={2,5,8,10,13,17,20};
 if(MonsterReviewCapture<UE_ARRAY_COUNT(Captures)&&Seconds>=Captures[MonsterReviewCapture])
 {
  MonsterReviewRows+=FString::Printf(TEXT("%.2f,%d,%d,%.2f,%d,%.2f\n"),Seconds,Melee->GetHeroSkill()->ActionCount,Ranged->GetHeroSkill()->ActionCount,
   Hero->GetAttributes()->GetHealth(),Projectiles,FVector::Dist2D(Melee->GetActorLocation(),Hero->GetActorLocation()));
  FScreenshotRequest::RequestScreenshot(MonsterReviewFolder/FString::Printf(TEXT("monsters-%d.png"),MonsterReviewCapture++),true,false);
  FFileHelper::SaveStringToFile(MonsterReviewRows,*(MonsterReviewFolder/TEXT("review.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
 }
 if(Seconds>22)
 {
  const bool Passed=Melee->GetHeroSkill()->ActionCount>3&&Ranged->GetHeroSkill()->ActionCount>3&&bMonsterReviewSawProjectile&&
   Hero->GetAttributes()->GetHealth()>0&&Hero->GetAttributes()->GetHealth()<100000&&MonsterReviewCapture==UE_ARRAY_COUNT(Captures);
  FFileHelper::SaveStringToFile(Passed?TEXT("PASS real melee/ranged actions, projectile flight, damage and fast attacks\n"):TEXT("FAIL missing monster attacks/projectile/damage/captures\n"),
   *(MonsterReviewFolder/TEXT("result.txt")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
  FPlatformMisc::RequestExitWithStatus(false,Passed?0:1);
 }
#endif
}
