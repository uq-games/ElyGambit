#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "EGFoundationProbe.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGMatchManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/Abilities/EGUltimateAbility.h"
#include "AbilitySystem/Effects/EGDamageEffect.h"
#include "EGGameplayTags.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "AbilitySystem/Abilities/EGBasicAttackAbility.h"
#include "AbilitySystem/Effects/EGManaEffect.h"
#include "Actor/EGArtPiece.h"

namespace
{
	struct FEGTestWorld
	{
		TStrongObjectPtr<UGameInstance> Instance;
		UWorld* World;
		FEGTestWorld() : Instance(NewObject<UGameInstance>(GEngine))
		{
			Instance->InitializeStandalone();
			World = Instance->GetWorld();
			World->GetWorldSettings()->DefaultGameMode = AEGChessGameMode::StaticClass();
			FURL URL;
			World->SetGameMode(URL);
			World->InitializeActorsForPlay(URL);
			World->BeginPlay();
		}
		~FEGTestWorld()
		{
			World->BeginTearingDown();
			World->EndPlay(EEndPlayReason::Quit);
			Instance->Shutdown();
			World->DestroyWorld(false);
			GEngine->DestroyWorldContext(World);
		}
	};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMatchTest, "ElyGambit.Foundation.MatchFlow",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGMatchTest::RunTest(const FString& Parameters)
{
 TStrongObjectPtr<UEGMatchManager> Match(NewObject<UEGMatchManager>());
 TestTrue(TEXT("Configure two stages"),Match->ConfigureRun(2,false));
 TestTrue(TEXT("Start"),Match->StartRun());
 for(int32 I=1;I<=2;++I)
 {
  TestEqual(TEXT("Stage number"),Match->GetStageNumber(),I);
  TestEqual(TEXT("No removed boss/corruption stages"),Match->GetStageType(),EEGStageType::Normal);
  TestTrue(TEXT("Enter"),Match->EnterCombat());TestFalse(TEXT("Duplicate enter"),Match->EnterCombat());
  TestTrue(TEXT("Resolve"),Match->ResolveCombat(false));TestFalse(TEXT("Duplicate resolution"),Match->ResolveCombat(true));
  if(I==1){TestEqual(TEXT("First loss still advances"),Match->GetPhase(),EEGMatchPhase::Resolution);TestTrue(TEXT("Next"),Match->NextStage());}
 }
 TestEqual(TEXT("Second stage finishes"),Match->GetPhase(),EEGMatchPhase::Finished);
 TestFalse(TEXT("Third stage unreachable"),Match->NextStage());
 TestTrue(TEXT("Restart"),Match->StartRun());TestEqual(TEXT("Restart at one"),Match->GetStageNumber(),1);
 for(int32 I=1;I<=2;++I){Match->EnterCombat();Match->ResolveCombat(true);if(I==1)Match->NextStage();}
 TestTrue(TEXT("Second win completes"),Match->DidWinRun());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGIntegrationTest, "ElyGambit.Foundation.WorldAndGAS",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGIntegrationTest::RunTest(const FString& Parameters)
{
	FEGTestWorld Env;
	AEGChessGameMode* GM = Env.World->GetAuthGameMode<AEGChessGameMode>();
	if (!TestNotNull(TEXT("GameMode initializes"), GM))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Match owned by GM"), GM->GetMatch()) ||
		!TestNotNull(TEXT("Roster subsystem"), GM->GetRoster()))
	{
		return false;
	}
	TestEqual(TEXT("Auto preparation"), GM->GetMatch()->GetPhase(), EEGMatchPhase::Preparation);
	TestFalse(TEXT("Empty combat rejected"), GM->EnterCombat());
	TStrongObjectPtr<UEGHeroDefinition> Definition(NewObject<UEGHeroDefinition>());
	Definition->HeroId = TEXT("TestHero");
	Definition->GrowthPerLevel.MaxHealth = 10.f;
	Definition->UltimateAbility = UEGUltimateAbility::StaticClass();
	const FGuid Id = GM->GetRoster()->AddHero(Definition.Get());
	TestTrue(TEXT("Merge one"), GM->GetRoster()->ApplySameNameCard(Id));
	TestTrue(TEXT("Merge two"), GM->GetRoster()->ApplySameNameCard(Id));
	TestTrue(TEXT("Other card level gain"), GM->GetRoster()->AddLevels(Id, 2));
	FEGHeroInstance Hero;
	GM->GetRoster()->GetHero(Id, Hero);
	TestEqual(TEXT("Linear persistent levels"), Hero.Level, 13);
	TestTrue(TEXT("Three cards awaken"), Hero.bAwakened);
	AEGChessUnit* Player = GM->SpawnRosterHero(Id);
	AEGChessUnit* Enemy = GM->SpawnEnemy(Definition.Get(), 1, FIntPoint(4, 0));
	AEGChessUnit* OtherEnemy = GM->SpawnEnemy(Definition.Get(), 1, FIntPoint(4, 1));
	if (!TestNotNull(TEXT("Spawn player"), Player) || !TestNotNull(TEXT("Spawn enemy"), Enemy))
	{
		return false;
	}
	if (!TestNotNull(TEXT("Spawn second enemy"), OtherEnemy))
	{
		return false;
	}
	TestEqual(TEXT("Query faction via interface"), IEGCombatant::Execute_GetFaction(Player), EEGFaction::Player);
	TestEqual(TEXT("Growth applied"), Player->GetAttributes()->GetMaxHealth(), 220.f);
	TestNotNull(TEXT("ASC"), Player->GetAbilitySystemComponent());
	TestTrue(TEXT("Move during preparation"), GM->GetBoard()->PlaceUnit(Player, FIntPoint(1, 0)));
	TestFalse(TEXT("Wrong deployment region"), GM->GetBoard()->PlaceUnit(Player, FIntPoint(4, 1)));
	TestFalse(TEXT("Occupied cell rejected"), GM->SpawnEnemy(Definition.Get(), 1, FIntPoint(4, 0)) != nullptr);
	TStrongObjectPtr<UEGFoundationProbe> Probe(NewObject<UEGFoundationProbe>());
	Enemy->OnDied.AddDynamic(Probe.Get(), &UEGFoundationProbe::Died);
	Enemy->OnCombatStateChanged.AddDynamic(Probe.Get(), &UEGFoundationProbe::StateChanged);
	Enemy->OnAttributeChanged.AddDynamic(Probe.Get(), &UEGFoundationProbe::AttributeChanged);
	Enemy->OnDamageReceived.AddDynamic(Probe.Get(), &UEGFoundationProbe::DamageReceived);
	GM->GetRoster()->AddLevels(Id, 1);
	TestTrue(TEXT("Enter managed combat"), GM->EnterCombat());
	TestEqual(TEXT("Latest roster stats synchronized"), Player->GetAttributes()->GetMaxHealth(), 230.f);
	TestTrue(TEXT("Combat interface started"), Player->GetCombat()->CanAct());
	TestFalse(TEXT("Cannot merge in combat"), GM->GetRoster()->ApplySameNameCard(Id));
	TestFalse(TEXT("Cannot move grid in combat"), GM->GetBoard()->PlaceUnit(Player, FIntPoint(2, 0)));
	// No NavMesh or authored level is needed to test selection and a hit at close range.
	Enemy->SetActorLocation(Player->GetActorLocation() + FVector(100.f, 0.f, 0.f));
	OtherEnemy->SetActorLocation(Player->GetActorLocation() + FVector(120.f, 0.f, 0.f));
	TestEqual(TEXT("Find closest enemy"), Player->GetCombat()->FindNearestEnemy(), Enemy);
	Player->GetCombat()->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Basic attack -> GE health"), Enemy->GetAttributes()->GetHealth(), 90.f);
	TestEqual(TEXT("Attacker mana on hit"), Player->GetAttributes()->GetMana(), 10.f);
	TestEqual(TEXT("Defender mana on damage"), Enemy->GetAttributes()->GetMana(), 5.f);
	TestEqual(TEXT("Damage delegate"), Probe->DamageEvents, 1);
	TestTrue(TEXT("Attribute delegates"), Probe->Attributes >= 2);
	OtherEnemy->SetActorLocation(Player->GetActorLocation() + FVector(50.f, 0.f, 0.f));
	Player->GetCombat()->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Free melee selects newly nearest target"), Player->GetCombat()->GetTarget(), OtherEnemy);
	FGameplayEffectSpecHandle Lethal = Player->GetAbilitySystemComponent()->MakeOutgoingSpec(
		UEGDamageEffect::StaticClass(), 1.f, Player->GetAbilitySystemComponent()->MakeEffectContext());
	Lethal.Data->SetSetByCallerMagnitude(EGTags::Damage, -10000.f);
	Player->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(*Lethal.Data,
																		 Enemy->GetAbilitySystemComponent());
	Enemy->Die();
	TestEqual(TEXT("Health clamp"), Enemy->GetAttributes()->GetHealth(), 0.f);
	TestEqual(TEXT("Death event once"), Probe->Deaths, 1);
	TestEqual(TEXT("Combat and death state events"), Probe->States, 2);
	TestFalse(TEXT("Dead cannot act"), Enemy->GetCombat()->CanAct());
	Player->GetCombat()->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestEqual(TEXT("Reacquire when target dies"), Player->GetCombat()->GetTarget(), OtherEnemy);
	Player->GetAbilitySystemComponent()->ApplyGameplayEffectSpecToTarget(*Lethal.Data,
																		 OtherEnemy->GetAbilitySystemComponent());
	Player->GetCombat()->TickComponent(0.1f, LEVELTICK_All, nullptr);
	TestNull(TEXT("Invalid target cleared"), Player->GetCombat()->GetTarget());
	Player->RestoreMana(10000.f);
	TestEqual(TEXT("Mana clamp"), Player->GetAttributes()->GetMana(), 100.f);
	TestTrue(TEXT("Ultimate without enemy target"), Player->RequestUltimate());
	TestEqual(TEXT("Ultimate consumes through GE"), Player->GetAttributes()->GetMana(), 0.f);
	TestTrue(TEXT("Resolve through GM"), GM->ResolveCombat(false));
	TestFalse(TEXT("Combat ends"), Player->GetCombat()->CanAct());
	Player->Destroy();
	TestTrue(TEXT("Roster survives actor destruction"), GM->GetRoster()->GetHero(Id, Hero));
	TestEqual(TEXT("Retained level"), Hero.Level, 14);
	TestTrue(TEXT("Retained awakening"), Hero.bAwakened);
	TestTrue(TEXT("Retained deployed status"), Hero.bDeployed);
	TestEqual(TEXT("Cell independent of combat location"), Hero.DeploymentCell, FIntPoint(1, 0));
	TestTrue(TEXT("Next round"), GM->NextStage());
	AEGChessUnit* Recreated = GM->SpawnRosterHero(Id);
	TestNotNull(TEXT("Recreated from roster"), Recreated);

	TestTrue(TEXT("Hero retained"), GM->GetRoster()->GetHero(Id, Hero));
	TestTrue(TEXT("Withdraw persistent hero"),GM->GetRoster()->Withdraw(Id));
	GM->RemoveReserveIncarnations();
	TestNull(TEXT("Reserve has no combat incarnation"),GM->GetBoard()->GetOccupant(FIntPoint(1,0)));
	TestTrue(TEXT("Sell reserve"),GM->GetRoster()->SellHero(Id));
	TestFalse(TEXT("Sale removes identity"),GM->GetRoster()->GetHero(Id,Hero));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGReflectionTest, "ElyGambit.Foundation.ReflectionCompatibility",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGReflectionTest::RunTest(const FString& Parameters)
{
	// Moving a header or splitting a translation unit must preserve serialized /Script identities.
	const TPair<UClass*, const TCHAR*> Classes[] = {
		{AEGArtPiece::StaticClass(), TEXT("EGArtPiece")},
		{AEGBoard::StaticClass(), TEXT("EGBoard")},
		{AEGChessUnit::StaticClass(), TEXT("EGChessUnit")},
		{AEGChessGameMode::StaticClass(), TEXT("EGChessGameMode")},
		{UEGMatchManager::StaticClass(), TEXT("EGMatchManager")},
		{UEGHeroRoster::StaticClass(), TEXT("EGHeroRoster")},
		{UEGHeroDefinition::StaticClass(), TEXT("EGHeroDefinition")},
		{UEGCombatComponent::StaticClass(), TEXT("EGCombatComponent")},
		{UEGCombatant::StaticClass(), TEXT("EGCombatant")},
		{UEGAttributeSet::StaticClass(), TEXT("EGAttributeSet")},
		{UEGChessAbility::StaticClass(), TEXT("EGChessAbility")},
		{UEGBasicAttackAbility::StaticClass(), TEXT("EGBasicAttackAbility")},
		{UEGUltimateAbility::StaticClass(), TEXT("EGUltimateAbility")},
		{UEGDamageEffect::StaticClass(), TEXT("EGDamageEffect")},
		{UEGManaEffect::StaticClass(), TEXT("EGManaEffect")}};
	for (const TPair<UClass*, const TCHAR*>& Entry : Classes)
	{
		TestEqual(TEXT("Stable reflected class path"), Entry.Key->GetPathName(),
				  FString(TEXT("/Script/ElyGambit.")) + Entry.Value);
	}
	TestEqual(TEXT("Stable hero struct"), FEGHeroInstance::StaticStruct()->GetPathName(),
			  FString(TEXT("/Script/ElyGambit.EGHeroInstance")));
	TestEqual(TEXT("Stable attribute struct"), FEGStatBlock::StaticStruct()->GetPathName(),
			  FString(TEXT("/Script/ElyGambit.EGStatBlock")));
	TestEqual(TEXT("Stable phase enum"), StaticEnum<EEGMatchPhase>()->GetPathName(),
			  FString(TEXT("/Script/ElyGambit.EEGMatchPhase")));
	TestEqual(TEXT("Stable damage tag"), EGTags::Damage.GetTag().ToString(), FString(TEXT("AutoChess.Data.Damage")));
	TestEqual(TEXT("Stable mana tag"), EGTags::Mana.GetTag().ToString(), FString(TEXT("AutoChess.Data.Mana")));
	TestTrue(TEXT("Basic ability inherits shared gate"),
			 UEGBasicAttackAbility::StaticClass()->IsChildOf(UEGChessAbility::StaticClass()));
	TestTrue(TEXT("Ultimate inherits shared gate"),
			 UEGUltimateAbility::StaticClass()->IsChildOf(UEGChessAbility::StaticClass()));
	TestEqual(TEXT("Damage effect remains instant"), GetDefault<UEGDamageEffect>()->DurationPolicy,
			  EGameplayEffectDurationType::Instant);
	TestEqual(TEXT("Mana effect remains instant"), GetDefault<UEGManaEffect>()->DurationPolicy,
			  EGameplayEffectDurationType::Instant);
	return true;
}
#endif
