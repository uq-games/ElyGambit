#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Data/EGCardDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Presentation/EGKillBeamVisual.h"
#include "Presentation/EGTeleportVisual.h"
#include "Presentation/EGSettlementPresentation.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardPolishLifecycle,
 "ElyGambit.Presentation.BoardPolish.FatalReceiptAndLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardPolishLifecycle::RunTest(const FString&)
{
 TestNotNull(TEXT("kill pack variant loads"),AEGKillBeamVisual::GetPreloadPath().TryLoad());
 TestNotNull(TEXT("arrival pack variant loads"),AEGTeleportVisual::GetPreloadPath().TryLoad());
 for(const auto& Path:AEGSettlementPresentation::GetPreloadPaths())TestNotNull(*Path.ToString(),Path.TryLoad());
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* World=GI->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL Url;World->SetGameMode(Url);
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 World->InitializeActorsForPlay(Url);World->BeginPlay();auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("mode"),Mode)||!TestTrue(TEXT("combat starts"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();
 for(auto* Unit:Mode->BattleRules->Units())if(Unit&&Unit->GetCombat())Unit->GetCombat()->SetComponentTickEnabled(false);
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"));
 if(!TestNotNull(TEXT("formal Elysia card"),Card))return false;
 auto Spawn=[&](EEGFaction Faction,FIntPoint Cell)
 {
  auto* Unit=Mode->SpawnBattleSummon(Card->HeroDefinition,1,Grid->CellToWorld(Cell)+FVector(0,0,88),Faction);
  if(Unit){Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->Configure(Mode->BattleRules,1,false);Mode->BattleRules->EnterUnit(Unit);}
  return Unit;
 };
 auto* Source=Spawn(EEGFaction::Player,{0,1});auto* Target=Spawn(EEGFaction::Enemy,{6,5});auto* EnvironmentTarget=Spawn(EEGFaction::Enemy,{5,5});
 if(!Source||!Target||!EnvironmentTarget)return false;
 auto Count=[&](){int32 N=0;for(TActorIterator<AEGKillBeamVisual> It(World);It;++It)if(IsValid(*It)&&!It->IsActorBeingDestroyed())++N;return N;};
 const int32 Before=Count();
 FEGCombatExecution::Damage(Source,Target,1.f);
 TestEqual(TEXT("nonfatal damage has no beam"),Count(),Before);
 FEGCombatExecution::Damage(Source,Target,100000.f);
 TestTrue(TEXT("real lethal receipt"),Target->LastCombatResolution.bFatal);
 TestEqual(TEXT("one attributed kill beam"),Count(),Before+1);
 Target->OnCombatResolved.Broadcast(Target->LastCombatResolution);
 TestEqual(TEXT("duplicate receipt does not repeat"),Count(),Before+1);
 FEGCombatExecution::EnvironmentDamage(EnvironmentTarget,100000.f,FGuid::NewGuid());
 TestEqual(TEXT("environment death is not an attributed kill"),Count(),Before+1);
 const FVector ArrivalLocation(450.f,650.f,100.f);
 auto* Arrival=World->SpawnActor<AEGTeleportVisual>(ArrivalLocation,FRotator::ZeroRotator);Arrival->Initialize(true,80.f,1.1f);
 TestTrue(TEXT("arrival retains its spawn position"),Arrival->GetActorLocation().Equals(ArrivalLocation));
 TWeakObjectPtr<AEGTeleportVisual> WeakArrival=Arrival;
 for(int32 I=0;I<52;++I){++GFrameCounter;World->Tick(LEVELTICK_All,.025f);}
 TestEqual(TEXT("beam expires without a surviving victim"),Count(),Before);
 TestFalse(TEXT("arrival expires"),WeakArrival.IsValid()&&!WeakArrival->IsActorBeingDestroyed());
 return true;
}
#endif
