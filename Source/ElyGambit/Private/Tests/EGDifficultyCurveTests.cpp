#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGChessGameMode.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDifficultyCorruptionDamageTest,
 "ElyGambit.DifficultyCurve.CorruptionDamage",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDifficultyCorruptionDamageTest::RunTest(const FString&)
{
 if(!GEngine)return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();UWorld* World=GI->GetWorld();
 if(!TestNotNull(TEXT("Fixture world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGChessGameMode>();
 if(!TestNotNull(TEXT("Fixture game mode"),Mode))return false;
 Mode->bAutoStart=false;World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());
 Definition->BaseStats.MaxHealth=1000.f;Definition->BaseStats.MaxMana=100.f;Definition->BaseStats.Defense=0.f;
 auto Spawn=[&](EEGFaction Faction,FVector Location)->AEGChessUnit*
 {
  FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* Unit=World->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),Location,FRotator::ZeroRotator,Params);
  if(!Unit||!Unit->InitializeUnit(Definition,FGuid::NewGuid(),Faction,1))return nullptr;
  Unit->GetCombat()->SetComponentTickEnabled(false);Unit->BeginCombat_Implementation();return Unit;
 };
 auto* Player=Spawn(EEGFaction::Player,FVector(0,0,88));
 auto* Enemy=Spawn(EEGFaction::Enemy,FVector(400,0,88));
 if(!TestNotNull(TEXT("Player unit"),Player)||!TestNotNull(TEXT("Enemy unit"),Enemy)||
    !TestNotNull(TEXT("Battle context"),Mode->BattleRules.Get()))return false;
 TestNull(TEXT("Combat units reject neutral faction"),Spawn(EEGFaction::Neutral,FVector(800,0,88)));
 auto* Battle=Mode->BattleRules.Get();
 auto StartCase=[&](int32 Corruption,bool bEnabled)
 {
  Battle->Start({});const bool bAccepted=Battle->SetEnemyDamageCorruptionSnapshot(Corruption,bEnabled);
  Battle->CompleteOpeningForTests();
  return bAccepted&&Battle->IsCombatRunning();
 };
 auto ResetHealth=[](AEGChessUnit* Unit,float Value)
 {Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Value);};
 auto Damage=[&](AEGChessUnit* Source,AEGChessUnit* Target,bool bLinked=false)
 {
  ResetHealth(Target,1000.f);
  if(!FEGCombatExecution::Damage(Source,Target,100.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),bLinked))
  {AddError(TEXT("Damage transaction rejected"));return -1.f;}
  return 1000.f-Target->GetAttributes()->GetHealth();
 };
 for(const auto& Case : {TPair<int32,float>(39,.95f),TPair<int32,float>(40,1.f),
                         TPair<int32,float>(70,1.f),TPair<int32,float>(71,1.05f)})
 {
  if(!TestTrue(TEXT("Combat is running"),StartCase(Case.Key,true)))return false;
  TestTrue(*FString::Printf(TEXT("Corruption %d enemy damage"),Case.Key),
   FMath::IsNearlyEqual(Damage(Enemy,Player),100.f*Case.Value,.01f));
 }
 TestFalse(TEXT("A running battle rejects snapshot changes"),Battle->SetEnemyDamageCorruptionSnapshot(39,true));
 TestTrue(TEXT("A running battle cannot change its corruption snapshot"),FMath::IsNearlyEqual(Battle->GetEnemyDamageMultiplier(),1.05f));
 TestTrue(TEXT("Player damage does not receive enemy bonus"),FMath::IsNearlyEqual(Damage(Player,Enemy),100.f,.01f));
 TestTrue(TEXT("Already-resolved linked damage is not multiplied again"),FMath::IsNearlyEqual(Damage(Enemy,Player,true),100.f,.01f));
 ResetHealth(Player,1000.f);
 TestTrue(TEXT("Environmental damage transaction succeeds"),FEGCombatExecution::EnvironmentDamage(Player,100.f,FGuid::NewGuid()));
 TestTrue(TEXT("Environmental damage has no enemy bonus"),FMath::IsNearlyEqual(1000.f-Player->GetAttributes()->GetHealth(),100.f,.01f));
 ResetHealth(Player,500.f);
 TestTrue(TEXT("Enemy-sourced healing transaction succeeds"),FEGCombatExecution::Heal(Enemy,Player,100.f));
 TestTrue(TEXT("Healing has no enemy bonus"),FMath::IsNearlyEqual(Player->GetAttributes()->GetHealth(),600.f,.01f));
 Battle->Stop();
 TestTrue(TEXT("Stopping clears the corruption multiplier"),FMath::IsNearlyEqual(Battle->GetEnemyDamageMultiplier(),1.f));
 if(!TestTrue(TEXT("New battle starts"),StartCase(71,false)))return false;
 TestTrue(TEXT("Disabled snapshot remains neutral after new battle"),FMath::IsNearlyEqual(Damage(Enemy,Player),100.f,.01f));
 Battle->Stop();
 return true;
}
#endif
