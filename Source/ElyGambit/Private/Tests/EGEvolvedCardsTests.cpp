#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "EGProductionTestAssets.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEvolvedCardsRunTest,
 "ElyGambit.Run.EvolvedCardsAcrossHandShopAndReset",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEvolvedCardsRunTest::RunTest(const FString&)
{
 auto* Seed=EGProductionTestAssets::ResolveRun(*this);if(!Seed||!GEngine)return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* World=GI->GetWorld();if(!World)return false;
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGChessGameMode>();
 auto* Grid=World->SpawnActor<AEGDeploymentBoard>();auto* Cards=World->SpawnActor<AEGCardManager>();
 if(!Mode||!Grid||!Cards)return false;
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Cards->Board=Grid;
 World->InitializeActorsForPlay(URL);World->BeginPlay();
 auto* Roster=Mode->GetRoster();auto* Match=Mode->GetMatch();
 TStrongObjectPtr<UEGRunDefinition> Run(EGSevenStage::Build(GetTransientPackage(),Seed));
 if(!TestNotNull(TEXT("Roster"),Roster)||!TestNotNull(TEXT("Match"),Match)||!TestNotNull(TEXT("Production card pool"),Run.Get()))return false;
 Cards->ConfigureRules(Run->CardRules,Roster);Cards->ConfigureRoundShop(Run.Get());
 UEGCardDefinition *Hua=nullptr,*Griseo=nullptr;
 for(const auto& Entry:Cards->CardPool)if(auto* Card=Entry.Get();Card&&Card->HeroDefinition)
 {
  if(Card->HeroDefinition->HeroKind==EEGHeroKind::Hua)Hua=Card;
  if(Card->HeroDefinition->HeroKind==EEGHeroKind::Griseo)Griseo=Card;
 }
 if(!TestNotNull(TEXT("Production Hua card"),Hua)||!TestNotNull(TEXT("Production Griseo card"),Griseo))return false;
 const FGuid HuaHero=Roster->AddHero(Hua->HeroDefinition);const FGuid GriseoHero=Roster->AddHero(Griseo->HeroDefinition);
 if(!TestTrue(TEXT("Both base heroes in reserve"),HuaHero.IsValid()&&GriseoHero.IsValid()))return false;
 if(!TestTrue(TEXT("Deploy Hua before unlock"),Cards->TryDeployReserve(HuaHero,{2,1})))return false;
 auto* HuaPiece=Cast<AEGDeployedPiece>(Grid->GetOccupant({2,1}));if(!TestNotNull(TEXT("Hua deployed piece"),HuaPiece))return false;
 if(!TestTrue(TEXT("Grant base cards with retained level"),Cards->GrantCardsBatch({Hua,Griseo},true,4)))return false;
 const auto Before=Cards->GetHand();const FEGCardInstance HuaBefore=Before[Before.Num()-2],GriseoBefore=Before.Last();
 Cards->Shop={Hua,Griseo};Cards->ShopSlotRevisions={100,101};Cards->ShopRevision=101;
 TestTrue(TEXT("Enter combat"),Match->EnterCombat());TestTrue(TEXT("Resolve to reward-eligible phase"),Match->ResolveCombat(true));
 auto* Mismatched=DuplicateObject<UEGHeroDefinition>(Hua->HeroDefinition,GetTransientPackage());
 Mismatched->EnhancedDefinition=Griseo->HeroDefinition->EnhancedDefinition;
 TestFalse(TEXT("Mismatched enhanced card aborts unlock"),Roster->UnlockForm(Mismatched));
 TestFalse(TEXT("Failed unlock leaves permission untouched"),Roster->IsFormUnlocked(Hua->HeroDefinition));
 TestTrue(TEXT("Unlock Hua"),Roster->UnlockForm(Hua->HeroDefinition));TestTrue(TEXT("Unlock Griseo"),Roster->UnlockForm(Griseo->HeroDefinition));
 const auto After=Cards->GetHand();
 auto Check=[&](const FEGCardInstance& Old,EEGHeroKind Kind)
 {
  const auto* New=After.FindByPredicate([&](const FEGCardInstance& C){return C.InstanceId==Old.InstanceId;});
  TestTrue(TEXT("Hand instance, level, cost and evolved identity retained"),New&&New->Definition&&New->Definition->HeroDefinition&&
   New->Definition->HeroDefinition->HeroKind==Kind&&New->InitialLevel==Old.InitialLevel&&New->PaidCost==Old.PaidCost);
 };
 Check(HuaBefore,EEGHeroKind::CollapsedHua);Check(GriseoBefore,EEGHeroKind::GriseoAdult);
 TestTrue(TEXT("Current shop forms upgraded"),Cards->Shop[0]&&Cards->Shop[0]->HeroDefinition->HeroKind==EEGHeroKind::CollapsedHua&&Cards->Shop[1]&&Cards->Shop[1]->HeroDefinition->HeroKind==EEGHeroKind::GriseoAdult);
 TestTrue(TEXT("Deployed piece drag card is upgraded and retains board occupancy"),HuaPiece->CardDefinition&&HuaPiece->CardDefinition->HeroDefinition->HeroKind==EEGHeroKind::CollapsedHua&&Grid->GetOccupant({2,1})==HuaPiece);
 TestTrue(TEXT("Both stale shop revisions invalidated"),Cards->GetShopSlotRevision(0)!=100&&Cards->GetShopSlotRevision(1)!=101);
 TestTrue(TEXT("Reserve card reflects unlocked form"),Cards->FindHeroCardDefinition(HuaHero)->HeroDefinition->HeroKind==EEGHeroKind::CollapsedHua);
 for(int32 I=0;I<100;++I)if(auto* Draw=Cards->DrawShopCard())
  TestTrue(TEXT("Future draws use evolved forms"),Draw->HeroDefinition->HeroKind!=EEGHeroKind::Hua&&Draw->HeroDefinition->HeroKind!=EEGHeroKind::Griseo);
 TestTrue(TEXT("Grant after unlock"),Cards->GrantCard(Hua));
 TestEqual(TEXT("Later granted card is enhanced"),Cards->GetHand().Last().Definition->HeroDefinition->HeroKind,EEGHeroKind::CollapsedHua);
 TestTrue(TEXT("Advance to preparation"),Match->NextStage());
 TestTrue(TEXT("Evolved card merges into original hero"),Cards->TryMergeIntoHero(HuaBefore.InstanceId,HuaHero));
 FEGHeroInstance Hero;TestTrue(TEXT("Roster survives merge"),Roster->GetHero(HuaHero,Hero));
 TestTrue(TEXT("Original roster identity and upgraded form preserved"),Hero.Definition==Hua->HeroDefinition&&Hero.Form==EEGHeroForm::Enhanced&&Hero.Level>1);
 Roster->ResetRun();
 TestTrue(TEXT("New run clears form permissions"),Cards->ResolveUnlockedCard(Hua)==Hua&&Cards->ResolveUnlockedCard(Griseo)==Griseo);
 TestTrue(TEXT("New run clears prior pieces"),Cards->ResetCards(0,0,nullptr));
 Cards->ConfigureRoundShop(Run.Get());
 for(int32 I=0;I<100;++I)if(auto* Draw=Cards->DrawShopCard())
  TestTrue(TEXT("New run has no unlocked-form draws"),Draw->HeroDefinition->HeroKind!=EEGHeroKind::CollapsedHua&&Draw->HeroDefinition->HeroKind!=EEGHeroKind::GriseoAdult);
 auto* DirectHua=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua.DA_Card_CollapsedHua"));
 auto* DirectGriseo=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult.DA_Card_GriseoAdult"));
 auto CheckDirect=[&](UEGCardDefinition* Card,FIntPoint Cell,EEGHeroKind Kind)
 {
  if(!TestNotNull(TEXT("Direct enhanced card"),Card)||!TestTrue(TEXT("Grant direct enhanced card"),Cards->GrantCard(Card)))return;
  const FGuid CardId=Cards->GetHand().Last().InstanceId;
  if(!TestTrue(TEXT("Deploy direct enhanced card without form permission"),Cards->TryDeployHandCard(CardId,Cell)))return;
  const auto* Piece=Cast<AEGDeployedPiece>(Grid->GetOccupant(Cell));
  TestTrue(TEXT("Direct enhanced card retains its enhanced roster and visible form"),Piece&&Roster->ResolveDefinition(Piece->RosterId)&&
   Roster->ResolveDefinition(Piece->RosterId)->HeroKind==Kind);
 };
 CheckDirect(DirectHua,{2,1},EEGHeroKind::CollapsedHua);
 CheckDirect(DirectGriseo,{4,1},EEGHeroKind::GriseoAdult);
 return true;
}
#endif
