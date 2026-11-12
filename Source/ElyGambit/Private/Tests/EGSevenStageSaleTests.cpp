#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "EGSevenStageSaleProbe.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Actor/EGBoard.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSevenStageSaleTransactions,
 "ElyGambit.SevenStage.SaleTransactions",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGSevenStageSaleTransactions::RunTest(const FString&)
{
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);
 if(!Seed||!GEngine)return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();
 if(!TestNotNull(TEXT("Fixture world"),World))return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
 FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
 if(!TestNotNull(TEXT("Fixture game mode"),Mode))return false;
 Mode->RunDefinition=EGSevenStage::Build(Mode,Seed);
 if(!TestNotNull(TEXT("Seven-stage run"),Mode->RunDefinition.Get()))return false;
 auto* Scene=World->SpawnActor<AEGBoardSceneSettings>();
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();
 auto* Cards=World->SpawnActor<AEGCardManager>();
 auto* CombatBoard=World->SpawnActor<AEGBoard>();
 if(!Scene||!Grid||!Cards||!CombatBoard){AddError(TEXT("Fixture actors could not spawn"));return false;}
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;
 Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=CombatBoard;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 if(!TestTrue(TEXT("Preparation is enabled"),Cards->CanEditPlacement())||
    !TestTrue(TEXT("Opening hand includes protected Elysia"),Cards->GetHand().Num()>0))return false;
 const FGuid Elysia=Cards->GetHand()[0].InstanceId;
 const int64 OpeningGold=Cards->GetGold();
 TestTrue(TEXT("Opening Elysia is protected"),Cards->IsSaleProtected(Elysia));
 TestFalse(TEXT("Protected card cannot be sold"),Cards->TrySellHandCard(Elysia));
 TestEqual(TEXT("Rejected protected sale gives no gold"),Cards->GetGold(),OpeningGold);

 const auto Shop=Cards->GetShop();
 const int32 ShopSlot=Shop.IndexOfByPredicate([](const UEGCardDefinition* Card){return Card!=nullptr;});
 if(!TestTrue(TEXT("Shop has a paid card"),ShopSlot!=INDEX_NONE)||
    !TestTrue(TEXT("Paid purchase succeeds"),Cards->BuyShopSlotExpected(ShopSlot,Cards->GetShopSlotRevision(ShopSlot))))return false;
 const FGuid PaidCard=Cards->GetHand().Last().InstanceId;
 TestEqual(TEXT("Paid card records actual purchase cost"),Cards->GetHand().Last().PaidCost,int64(3));
 TestEqual(TEXT("Paid card sells for two thirds of actual payment"),Cards->GetSaleValue(PaidCard),int64(2));
 TestTrue(TEXT("Paid hand sale succeeds"),Cards->TrySellHandCard(PaidCard));
 TestEqual(TEXT("Paid sale pays only once"),Cards->GetGold(),OpeningGold-1);
 const int64 BaselineGold=Cards->GetGold();

 auto* Pardo=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));
 if(!TestNotNull(TEXT("Ordinary hero card"),Pardo)||!TestTrue(TEXT("Grant card"),Cards->GrantCard(Pardo)))return false;
 const FGuid HandId=Cards->GetHand().Last().InstanceId;
 TestEqual(TEXT("Granted card has zero sale value"),Cards->GetSaleValue(HandId),int64(0));
 Cards->SetPreparationEnabled(false);
 TestFalse(TEXT("Sale outside preparation rejected"),Cards->TrySellHandCard(HandId));
 TestTrue(TEXT("Rejected card remains"),Cards->HasHandCard(HandId));
 TestEqual(TEXT("Rejected sale leaves gold unchanged"),Cards->GetGold(),BaselineGold);
 Cards->SetPreparationEnabled(true);
 TestTrue(TEXT("Hand card sale succeeds"),Cards->TrySellHandCard(HandId));
 TestFalse(TEXT("Repeated hand sale rejected"),Cards->TrySellHandCard(HandId));
 TestEqual(TEXT("Free hand sale pays zero"),Cards->GetGold(),BaselineGold);

 if(!TestTrue(TEXT("Grant reserve fixture"),Cards->GrantCard(Pardo)))return false;
 const FGuid FirstCard=Cards->GetHand().Last().InstanceId;
 if(!TestTrue(TEXT("Deploy ordinary hero"),Cards->TryDeployHandCard(FirstCard,{1,1})))return false;
 AEGDeployedPiece* Piece=Cast<AEGDeployedPiece>(Grid->GetOccupant({1,1}));
 if(!TestNotNull(TEXT("Deployed piece"),Piece))return false;
 const FGuid FirstHero=Piece->RosterId;
 TestTrue(TEXT("Grant persistent growth"),Mode->GetRoster()->AddLevels(FirstHero,4));
 if(!TestTrue(TEXT("Withdraw for reserve sale"),Cards->TryWithdraw(Piece)))return false;
 TestTrue(TEXT("Reserve identity retained"),Cards->HasReserveHero(FirstHero));
 TestFalse(TEXT("Direct roster sale cannot bypass the economy transaction"),Mode->GetRoster()->SellHero(FirstHero));
 TestTrue(TEXT("Rejected direct sale retains reserve"),Cards->HasReserveHero(FirstHero));
 TestTrue(TEXT("Reserve sale succeeds"),Cards->TrySellReserveHero(FirstHero));
 TestFalse(TEXT("Repeated reserve sale rejected"),Cards->TrySellReserveHero(FirstHero));
 FEGHeroInstance SoldHero;
 TestFalse(TEXT("Sold hero and its growth removed"),Mode->GetRoster()->GetHero(FirstHero,SoldHero));
 TestEqual(TEXT("Free reserve sale pays zero"),Cards->GetGold(),BaselineGold);

 if(!TestTrue(TEXT("Grant same hero again"),Cards->GrantCard(Pardo)))return false;
 const FGuid SecondCard=Cards->GetHand().Last().InstanceId;
 if(!TestTrue(TEXT("Deploy newly acquired hero"),Cards->TryDeployHandCard(SecondCard,{1,1})))return false;
 Piece=Cast<AEGDeployedPiece>(Grid->GetOccupant({1,1}));
 if(!TestNotNull(TEXT("Reacquired piece"),Piece))return false;
 const FGuid SecondHero=Piece->RosterId;
 FEGHeroInstance Reacquired;
 TestTrue(TEXT("Reacquired hero has fresh identity"),SecondHero!=FirstHero);
 TestTrue(TEXT("Reacquired hero is in roster"),Mode->GetRoster()->GetHero(SecondHero,Reacquired));
 TestEqual(TEXT("Reacquired hero starts without sold growth"),Reacquired.Level,1);
 UEGCardDefinition* OtherCard=nullptr;
 for(const auto& Candidate:Mode->RunDefinition->CardPool)
  if(Candidate&&Candidate->HeroDefinition&&Candidate!=Pardo&&
     Candidate->HeroDefinition->HeroKind!=EEGHeroKind::Elysia&&
     Candidate->HeroDefinition->HeroKind!=EEGHeroKind::VillV)
  {OtherCard=Candidate.Get();break;}
 if(!TestNotNull(TEXT("Different ordinary hero"),OtherCard)||
    !TestTrue(TEXT("Grant reentry target"),Cards->GrantCard(OtherCard)))return false;
 const FGuid OtherCardId=Cards->GetHand().Last().InstanceId;
 if(!TestTrue(TEXT("Deploy reentry target"),Cards->TryDeployHandCard(OtherCardId,{3,1})))return false;
 auto* OtherPiece=Cast<AEGDeployedPiece>(Grid->GetOccupant({3,1}));
 if(!TestNotNull(TEXT("Reentry target piece"),OtherPiece))return false;
 const FGuid OtherHero=OtherPiece->RosterId;
 TStrongObjectPtr<UEGSevenStageSaleProbe> Probe(NewObject<UEGSevenStageSaleProbe>());
 Probe->Roster=Mode->GetRoster();Probe->Target=OtherHero;
 Cards->OnEconomyChanged.AddDynamic(Probe.Get(),&UEGSevenStageSaleProbe::OnSaleNotification);
 TestTrue(TEXT("Placed hero sale succeeds"),Cards->TrySellPlacedPiece(Piece));
 Cards->OnEconomyChanged.RemoveDynamic(Probe.Get(),&UEGSevenStageSaleProbe::OnSaleNotification);
 TestTrue(TEXT("Sale notification attempted reentrant public roster sale"),Probe->bAttempted);
 TestFalse(TEXT("Reentrant public roster sale rejected"),Probe->bSucceeded);
 TestTrue(TEXT("Reentry target retains roster identity"),Mode->GetRoster()->GetHero(OtherHero,Reacquired));
 TestTrue(TEXT("Reentry target retains its board cell"),Grid->GetOccupant({3,1})==OtherPiece);
 TestFalse(TEXT("Repeated placed sale rejected"),Cards->TrySellPlacedPiece(Piece));
 TestNull(TEXT("Sale releases occupied cell"),Grid->GetOccupant({1,1}));
 TestFalse(TEXT("Placed sale removes roster identity"),Mode->GetRoster()->GetHero(SecondHero,Reacquired));
 TestEqual(TEXT("Free placed sale pays zero"),Cards->GetGold(),BaselineGold);

 if(!TestTrue(TEXT("Deploy protected Elysia"),Cards->TryDeployHandCard(Elysia,{1,1})))return false;
 Piece=Cast<AEGDeployedPiece>(Grid->GetOccupant({1,1}));
 if(!TestNotNull(TEXT("Protected piece"),Piece))return false;
 const FGuid ElysiaHero=Piece->RosterId;
 TestFalse(TEXT("Protected deployed hero cannot be sold"),Cards->TrySellPlacedPiece(Piece));
 TestFalse(TEXT("Roster cannot sell protected hero directly"),Mode->GetRoster()->SellHero(ElysiaHero));
 TestTrue(TEXT("Protected hero can withdraw"),Cards->TryWithdraw(Piece));
 TestFalse(TEXT("Protected reserve hero cannot be sold"),Cards->TrySellReserveHero(ElysiaHero));
 TestTrue(TEXT("Protected hero remains in roster"),Mode->GetRoster()->GetHero(ElysiaHero,Reacquired));
 TestEqual(TEXT("Protected sale attempts do not pay"),Cards->GetGold(),BaselineGold);
 return true;
}
#endif
