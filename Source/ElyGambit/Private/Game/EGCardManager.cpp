#include "Game/EGCardManager.h"
#include "Game/Deployment/EGVillVDeploymentPolicy.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Data/EGCardDefinition.h"
#include "Game/EGHeroRoster.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGHumanEntranceComponent.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Data/EGStigmataBalance.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
    bool IsElysia(const UEGHeroDefinition* Hero)
    {return Hero && Hero->HeroId==FName(TEXT("Elysia"));}
    bool IsHerrscherOfHuman(const UEGHeroDefinition* Hero)
    {return Hero && Hero->HeroId==FName(TEXT("ElysiaHerrscherOfHuman"));}
    bool IsExcludedFromShop(const UEGHeroDefinition* Hero)
    {return IsElysia(Hero)||IsHerrscherOfHuman(Hero)||(Hero&&(Hero->HeroKind==EEGHeroKind::Elysia||Hero->HeroId==FName(TEXT("ElysiaCorruption"))));}
    FIntPoint PlacementSize(const UEGCardDefinition* Card)
    {return Card&&Card->HeroDefinition?Card->HeroDefinition->GetPlacementFootprint():FIntPoint(1,1);}
    FRotator PlayerPieceRotation(const AEGDeploymentBoard* Board,const UEGHeroDefinition* Hero)
    {
        if(!Board)return FRotator::ZeroRotator;
        const FVector Forward=(Board->CellToWorld(FIntPoint(0,1))-Board->CellToWorld(FIntPoint(0,0))).GetSafeNormal2D();
        const FQuat Facing=Forward.IsNearlyZero()?Board->GetActorQuat():Forward.Rotation().Quaternion();
        const FQuat Model=Hero&&(!Hero->bUseSplitConfiguration||Hero->PresentationDefinition)?
            Hero->GetPresentationRotation().Quaternion():FQuat::Identity;
        return (Facing*Model).Rotator();
    }
    bool SameHero(const UEGHeroDefinition* A,const UEGHeroDefinition* B)
    {return A&&B&&(A==B||(!A->HeroId.IsNone()&&A->HeroId==B->HeroId)||A->EnhancedDefinition==B||B->EnhancedDefinition==A);}
    const TCHAR* EvolvedCardPath(const UEGHeroDefinition* Hero)
    {
        if(!Hero)return nullptr;
        if(Hero->HeroKind==EEGHeroKind::Hua)return TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua.DA_Card_CollapsedHua");
        if(Hero->HeroKind==EEGHeroKind::Griseo)return TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult.DA_Card_GriseoAdult");
        return nullptr;
    }
    UEGCardDefinition* LoadEvolvedCard(const UEGHeroDefinition* Hero)
    {
        const TCHAR* Path=EvolvedCardPath(Hero);if(!Path)return nullptr;
        auto* Enhanced=LoadObject<UEGCardDefinition>(nullptr,Path);
        return Enhanced&&Enhanced->HeroDefinition==Hero->EnhancedDefinition&&!Enhanced->CardId.IsNone()&&Enhanced->PieceClass&&
          Enhanced->HeroDefinition->GetPlacementFootprint()==Hero->GetPlacementFootprint()?Enhanced:nullptr;
    }
    FEGPlacementEvaluation Evaluation(EEGPlacementAction Action,const TCHAR* Message)
    { FEGPlacementEvaluation Result;Result.Action=Action;Result.Message=FText::FromString(Message);return Result; }
    bool PlayDeploymentEntrance(AEGDeployedPiece* Piece)
    {
        if(!IsValid(Piece))return false;
        const auto* Hero=Piece->HeroVisualDefinition.Get();
        const auto* Presentation=Hero&&Hero->bUseSplitConfiguration?Hero->PresentationDefinition.Get():nullptr;
        const bool bHuman=Hero&&Hero->HeroKind==EEGHeroKind::ElysiaHerrscher;
        if(Presentation&&!Presentation->PreparationEntranceAssembly.IsNull())
            Piece->PlayPreparationEntrance(Presentation->PreparationEntranceAssembly.LoadSynchronous(),
                Presentation->PreparationEntranceVoice.LoadSynchronous(),bHuman);
        if(!bHuman)return false;
        const bool bPlayingHumanCard=UEGHumanEntranceComponent::Play(Piece);
        if(!bPlayingHumanCard&&IsValid(Piece))Piece->StartDeferredPreparationEntranceVoice();
        return bPlayingHumanCard;
    }
}
AEGCardManager::AEGCardManager()
{
    PrimaryActorTick.bCanEverTick=false;
    SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
    PieceClass=AEGDeployedPiece::StaticClass();
}
void AEGCardManager::BeginPlay(){Super::BeginPlay();InitializeCards();}
void AEGCardManager::EndPlay(const EEndPlayReason::Type Reason)
{
    bEnding=true;
    const auto Snapshot=GetDeploymentParts();Pieces.Reset();DevicePieces.Reset();Hand.Reset();
    for(auto* Piece:Snapshot)
    {
        Piece->OnDestroyed.RemoveDynamic(this,&ThisClass::PieceDestroyed);
        if(IsValid(Board))Board->ReleaseCell(Piece->Cell,Piece);
        Piece->Destroy();
    }
    OnHandChanged.Clear();OnBoardChanged.Clear();OnEconomyChanged.Clear();OnFeedbackChanged.Clear();
    if(Roster.IsValid())Roster->OnHeroChanged.RemoveDynamic(this,&ThisClass::HandleRosterChanged);
    Roster.Reset();Super::EndPlay(Reason);
}
int32 AEGCardManager::FindCard(FGuid Id) const
{return Id.IsValid()?Hand.IndexOfByPredicate([Id](const FEGCardInstance& Card){return Card.InstanceId==Id;}):INDEX_NONE;}
bool AEGCardManager::HasHandCard(FGuid Id) const{return FindCard(Id)!=INDEX_NONE;}
bool AEGCardManager::InitializeCards()
{
    if(bInitialized||bCommitting||bEnding||!HasAuthority()||!IsValid(Board)||Board->GetWorld()!=GetWorld())return false;
    bInitialized=true;return ResetCards(0,1,AvailableCardDefinition);
}
void AEGCardManager::ConfigureRules(const FEGCardRules& InRules,UEGHeroRoster* InRoster)
{
    if(!HasAuthority()||bCommitting||bEnding||IsStigmataTransactionActive())return;
    if(Roster.IsValid())Roster->OnHeroChanged.RemoveDynamic(this,&ThisClass::HandleRosterChanged);
    Rules=InRules;Roster=InRoster;
    if(Roster.IsValid())Roster->OnHeroChanged.AddUniqueDynamic(this,&ThisClass::HandleRosterChanged);
    SynchronizeHandOrder();
    if(RoundShopDefinition&&RoundShopDefinition->bUseFixedPopulation)
        Rules.DeploymentLimit=FMath::Max(1,RoundShopDefinition->FixedPopulation);
}
bool AEGCardManager::CanEditPlacement() const
{return bInitialized&&bPreparationEnabled&&!bEnding;}
bool AEGCardManager::SynchronizeHandOrder()
{
    TSet<FGuid> Current;
    for(const auto& Card:Hand)if(Card.InstanceId.IsValid())Current.Add(Card.InstanceId);
    for(const auto& Hero:GetReserveHeroes())if(Hero.InstanceId.IsValid())Current.Add(Hero.InstanceId);
    TArray<FGuid> Updated;
    Updated.Reserve(Current.Num());
    for(const FGuid Id:OrderedHandIds)if(Current.Remove(Id)>0)Updated.Add(Id);
    // New entries are appended in their source order, never by TSet iteration.
    for(const auto& Card:Hand)if(Current.Remove(Card.InstanceId)>0)Updated.Add(Card.InstanceId);
    for(const auto& Hero:GetReserveHeroes())if(Current.Remove(Hero.InstanceId)>0)Updated.Add(Hero.InstanceId);
    if(Updated==OrderedHandIds)return false;
    OrderedHandIds=MoveTemp(Updated);
    HandOrderRevision=HandOrderRevision==MAX_int32?1:HandOrderRevision+1;
    return true;
}
void AEGCardManager::NotifyHandChanged()
{
    SynchronizeHandOrder();
    OnHandChanged.Broadcast();
}
void AEGCardManager::HandleRosterChanged(FGuid Id)
{
    if(bEnding)return;
    const bool bCardChanged=SynchronizeUnlockedCards();
    const bool bOrderChanged=SynchronizeHandOrder();
    if((bCardChanged||bOrderChanged)&&!bCommitting)OnHandChanged.Broadcast();
    if(bCardChanged&&!bCommitting)OnEconomyChanged.Broadcast();
}

UEGCardDefinition* AEGCardManager::ResolveUnlockedCard(UEGCardDefinition* Card) const
{
    const auto* Hero=Card?Card->HeroDefinition.Get():nullptr;
    if(!Hero||!Roster.IsValid()||!Roster->IsFormUnlocked(Hero))return Card;
    const TCHAR* Path=EvolvedCardPath(Hero);
    if(!Path)return Card;
    auto* Enhanced=LoadEvolvedCard(Hero);
    if(!Enhanced)
    {
        UE_LOG(LogTemp,Error,TEXT("Unlocked hero card unavailable or mismatched: %s"),Path);
        return Card;
    }
    return Enhanced;
}

bool AEGCardManager::CanUnlockFormCard(const UEGHeroDefinition* Definition)
{return Definition&&(!EvolvedCardPath(Definition)||LoadEvolvedCard(Definition)!=nullptr);}

bool AEGCardManager::SynchronizeUnlockedCards()
{
    bool bChanged=false;
    for(auto& Card:Hand)
        if(auto* Resolved=ResolveUnlockedCard(Card.Definition.Get());Resolved&&Resolved!=Card.Definition)
        {Card.Definition=Resolved;Card.CardId=Resolved->CardId;bChanged=true;}
    for(int32 Index=0;Index<Shop.Num();++Index)
        if(auto* Resolved=ResolveUnlockedCard(Shop[Index].Get());Resolved&&Resolved!=Shop[Index])
        {
            Shop[Index]=Resolved;
            ShopRevision=ShopRevision==MAX_int32?1:ShopRevision+1;
            if(ShopSlotRevisions.IsValidIndex(Index))ShopSlotRevisions[Index]=ShopRevision;
            bChanged=true;
        }
    // A deployed piece is also the source for its drag-card appearance. The
    // preflight above requires identical footprints, so occupancy is unchanged.
    for(auto* Piece:GetPieces())if(IsValid(Piece))
        if(auto* Resolved=ResolveUnlockedCard(Piece->CardDefinition.Get());Resolved&&Resolved!=Piece->CardDefinition)
        {Piece->CardDefinition=Resolved;bChanged=true;}
    return bChanged;
}
bool AEGCardManager::IsHandOverflowProtected(FGuid Id) const
{
    if(!OrderedHandIds.Contains(Id))return false;
    const int32 Index=FindCard(Id);
    if(Index!=INDEX_NONE&&!Hand[Index].Definition)return true;
    FEGHeroInstance Hero;
    return (Roster.IsValid()&&Roster->GetHero(Id,Hero)&&!Hero.Definition)||IsOverflowProtected(Id);
}
TArray<FGuid> AEGCardManager::GetOverflowHandIds() const
{
    TArray<FGuid> Result;
    if(!RoundShopDefinition||!RoundShopDefinition->bSevenStageRules)return Result;
    constexpr int32 Limit=10;
    int32 Protected=0;
    for(const FGuid Id:OrderedHandIds)if(IsHandOverflowProtected(Id))++Protected;
    int32 FreeSlots=FMath::Max(0,Limit-Protected);
    for(const FGuid Id:OrderedHandIds)
    {
        if(IsHandOverflowProtected(Id))continue;
        if(FreeSlots>0)--FreeSlots;
        else Result.Add(Id);
    }
    return Result;
}
int64 AEGCardManager::GetOverflowSaleValue() const
{
    int64 Value=0;
    for(const FGuid Id:GetOverflowHandIds())Value+=GetSaleValue(Id);
    return Value;
}
bool AEGCardManager::TryReorderHandCard(FGuid Id,FGuid BeforeId,int32 ExpectedRevision)
{
    if(!HasAuthority()||bCommitting||bEnding||IsStigmataTransactionActive()||
       !CanEditPlacement()||ExpectedRevision!=HandOrderRevision)return false;
    const int32 From=OrderedHandIds.IndexOfByKey(Id);
    if(From==INDEX_NONE||(BeforeId.IsValid()&&!OrderedHandIds.Contains(BeforeId)))return false;
    if(Id==BeforeId)return true;
    TArray<FGuid> Updated=OrderedHandIds;
    Updated.RemoveAt(From);
    const int32 To=BeforeId.IsValid()?Updated.IndexOfByKey(BeforeId):Updated.Num();
    if(To==INDEX_NONE)return false;
    Updated.Insert(Id,To);
    if(Updated==OrderedHandIds)return true;
    OrderedHandIds=MoveTemp(Updated);
    HandOrderRevision=HandOrderRevision==MAX_int32?1:HandOrderRevision+1;
    OnHandChanged.Broadcast();
    return true;
}
void AEGCardManager::SetPreparationEnabled(bool bEnabled)
{bPreparationEnabled=bEnabled;if(bEnabled)PlayPendingDeviceEntrances();}
void AEGCardManager::PlayPendingDeviceEntrances()
{ FEGVillVDeploymentPolicy::PlayEntrances(*this); }
void AEGCardManager::Feedback(const FText& Message)
{LastMessage=Message;OnFeedbackChanged.Broadcast();}
bool AEGCardManager::ObtainDebugElysiaCard(){return RequestObtainCard(AvailableCardDefinition);}
void AEGCardManager::ConfigureRoundShop(UEGRunDefinition* Definition)
{
    if(!HasAuthority()||!bInitialized||bCommitting||bEnding||IsStigmataTransactionActive())return;
    if(Definition&&Definition->bUseExplicitStartingCards&&(!Roster.IsValid()||Definition->ExplicitStartingCards.IsEmpty()))return;
    FString OpeningError;
    if(Definition&&!Definition->ValidateStartingCards(OpeningError))
    {UE_LOG(LogTemp,Warning,TEXT("Invalid opening card configuration: %s"),*OpeningError);return;}
    // Reconfiguration or restoration of the same run must never reissue the opening hand.
    if(Definition&&Definition->bUseExplicitStartingCards&&Roster.IsValid()&&
       Roster->bStartingCardsIssued&&RoundShopDefinition==Definition)return;
    RoundShopDefinition=Definition&&Definition->bRoundShopRules&&!Definition->CardPool.IsEmpty()&&Definition->RoundIncome>=0?Definition:nullptr;
    if(!RoundShopDefinition)return;
    TGuardValue<bool> Guard(bCommitting,true);
    int32 ShopSeed=FMath::Rand();
    if(Definition->bSevenStageRules)
        if(const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr)
            ShopSeed=Mode->GetBalanceSeed() ^ 0x47A991;
#if !UE_BUILD_SHIPPING
    // Reproducible opt-in profiling only; normal runs retain their random pool.
    if(!Definition->bSevenStageRules&&FParse::Param(FCommandLine::Get(),TEXT("EGProfile")))
        FParse::Value(FCommandLine::Get(),TEXT("EGProfileSeed="),ShopSeed);
#endif
    ShopRandom.Initialize(ShopSeed);Shop.Reset();bShopLocked=false;
    LastIncomeStage=1;InitialRound=1;PopulationFloor=0;FreeRefreshesUsed=0;
    Rules=RoundShopDefinition->CardRules;
    Rules.DeploymentLimit=Definition->bUseFixedPopulation?FMath::Max(1,Definition->FixedPopulation):3;
    // Keep the complete registry separate from the ordinary shop candidates.
    RegisteredCards.Reset();
    if(Definition->StartingCard)RegisteredCards.AddUnique(Definition->StartingCard);
    if(Definition->bUseExplicitStartingCards)for(const auto& Grant:Definition->ExplicitStartingCards)
        if(Grant.Card)RegisteredCards.AddUnique(Grant.Card);
    for(const auto& Card:Definition->CardPool)if(Card)RegisteredCards.AddUnique(Card);
    CardPool.Reset();
    for(const auto& Card:Definition->CardPool)if(Card&&(!IsExcludedFromShop(Card->HeroDefinition)||(Definition->bSevenStageRules&&IsElysia(Card->HeroDefinition))))CardPool.AddUnique(Card);
    if(Definition->bSevenStageRules&&Definition->StartingCard)CardPool.AddUnique(Definition->StartingCard);
    const bool bAlreadyIssued=Definition->bUseExplicitStartingCards&&Roster.IsValid()&&Roster->bStartingCardsIssued;
    auto Add=[this](UEGCardDefinition* D,int32 Level=1){D=ResolveUnlockedCard(D);if(!D)return;FEGCardInstance C;C.InstanceId=FGuid::NewGuid();C.CardId=D->CardId;C.Definition=D;C.InitialLevel=FMath::Max(1,Level);Hand.Add(C);};
    if(!bAlreadyIssued)
    {
        Gold=Definition->StartingGold;Hand.Reset();
        EncounterRecoveryHand.Reset();EncounterRecoveryOrder.Reset();bEncounterHandSnapshot=false;
        if(Definition->bUseExplicitStartingCards)
        {
            for(const auto& Grant:Definition->ExplicitStartingCards)Add(Grant.Card,Grant.InitialLevel);
            Roster->bStartingCardsIssued=true;
        }
        else
        {
            if(Definition->StartingCards>0)Add(Definition->StartingCard);
            if(Definition->bSevenStageRules)
            {
                TArray<UEGCardDefinition*> Candidates;
                for(const auto& Card:CardPool)if(Card&&Card->HeroDefinition&&!IsExcludedFromShop(Card->HeroDefinition))Candidates.Add(Card);
                if(!Candidates.IsEmpty())Add(Candidates[ShopRandom.RandRange(0,Candidates.Num()-1)]);
            }
            else for(int32 I=1;I<Definition->StartingCards;++I)Add(DrawShopCard());
        }
    }
    bPreparationEnabled=true;
    GenerateShop();
    if(Definition->bSevenStageRules)
    {
        TArray<UEGCardDefinition*> Unowned;
        for(const auto& Candidate:CardPool)
        {
            if(!Candidate||!Candidate->HeroDefinition)continue;
            const bool bHeld=Hand.ContainsByPredicate([&](const FEGCardInstance& C){return C.Definition&&SameHero(C.Definition->HeroDefinition,Candidate->HeroDefinition);})||
                (Roster.IsValid()&&Roster->FindHeroByDefinition(Candidate->HeroDefinition).IsValid());
            if(!bHeld)Unowned.Add(Candidate);
        }
        const bool bHasUnowned=Shop.ContainsByPredicate([&](const TObjectPtr<UEGCardDefinition>& C)
        {return C&&Unowned.ContainsByPredicate([&](const UEGCardDefinition* U){return U&&SameHero(U->HeroDefinition,C->HeroDefinition);});});
        if(!bHasUnowned&&!Unowned.IsEmpty())Shop[0]=ResolveUnlockedCard(Unowned[ShopRandom.RandRange(0,Unowned.Num()-1)]);
    }
    NotifyHandChanged();OnEconomyChanged.Broadcast();OnBoardChanged.Broadcast();
}
bool AEGCardManager::CanTrade() const
{
    if(!RoundShopDefinition||!bInitialized||bEnding)return false;
    const auto* GM=GetWorld()->GetAuthGameMode<AEGDemoGameMode>();
    return GM&&GM->AreStageResourcesReady()&&GM->GetMatch()&&
        (GM->GetMatch()->GetPhase()==EEGMatchPhase::Preparation||GM->GetMatch()->GetPhase()==EEGMatchPhase::Combat);
}
bool AEGCardManager::IsStigmataTransactionActive() const
{
    const auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    return GM&&GM->GetMatch()&&GM->GetMatch()->IsStigmataCommitting();
}
UEGCardDefinition* AEGCardManager::DrawShopCard()
{
    if(!RoundShopDefinition||CardPool.IsEmpty())return nullptr;
    TArray<UEGCardDefinition*> Candidates;
    for(const auto& Card:CardPool)
        if(Card&&Card->HeroDefinition&&(!IsExcludedFromShop(Card->HeroDefinition)||(RoundShopDefinition->bSevenStageRules&&IsElysia(Card->HeroDefinition)))&&!Candidates.ContainsByPredicate([&](const UEGCardDefinition* Existing){return SameHero(Existing->HeroDefinition,Card->HeroDefinition);}))Candidates.Add(Card.Get());
    return Candidates.IsEmpty()?nullptr:ResolveUnlockedCard(Candidates[ShopRandom.RandRange(0,Candidates.Num()-1)]);
}
void AEGCardManager::GenerateShop()
{
    Shop.Reset();
    for(int32 Index=0;Index<4;++Index)Shop.Add(DrawShopCard());
    CorruptionShopSlot=INDEX_NONE;
    ShopSlotRevisions.Init(++ShopRevision,4);
}
TArray<UEGCardDefinition*> AEGCardManager::GetShop() const
{TArray<UEGCardDefinition*> Result;for(const auto& Card:Shop)Result.Add(Card.Get());return Result;}
bool AEGCardManager::BuyShopSlot(int32 Index)
{
    if(!HasAuthority()||bCommitting||!CanTrade()||!Shop.IsValidIndex(Index)||!Shop[Index])return false;
    if(Gold<Rules.PurchasePrice){Feedback(FText::FromString(TEXT("崩坏能不足")));return false;}
    TGuardValue<bool> Guard(bCommitting,true);
    auto* D=Shop[Index].Get();FEGCardInstance C;C.InstanceId=FGuid::NewGuid();C.CardId=D->CardId;C.Definition=D;C.PaidCost=FMath::Max(0,Rules.PurchasePrice);
    Gold-=Rules.PurchasePrice;Hand.Add(C);Shop[Index]=DrawShopCard();
    ShopSlotRevisions[Index]=++ShopRevision;
    NotifyHandChanged();OnEconomyChanged.Broadcast();return true;
}
bool AEGCardManager::BuyShopSlotExpected(int32 Index,int32 Revision)
{return Revision!=INDEX_NONE&&GetShopSlotRevision(Index)==Revision&&BuyShopSlot(Index);}
FEGPlacementEvaluation AEGCardManager::EvaluateShopPlacement(int32 Index,int32 Revision,FIntPoint Cell) const
{
    if(!CanTrade()||Revision==INDEX_NONE||GetShopSlotRevision(Index)!=Revision||!Shop.IsValidIndex(Index)||!Shop[Index])
        return Evaluation(EEGPlacementAction::Invalid,TEXT("商店商品已变化，请重新拖动。"));
    if(Gold<Rules.PurchasePrice)return Evaluation(EEGPlacementAction::Invalid,TEXT("崩坏能不足"));
    return EvaluateDefinitionPlacement(Shop[Index],Cell);
}
bool AEGCardManager::TryBuyShopAndDeploy(int32 Index,int32 Revision,FIntPoint Cell)
{
    if(!HasAuthority()||bCommitting||bEnding)return false;
    const auto Result=EvaluateShopPlacement(Index,Revision,Cell);
    if(!Result.IsAllowed()){Feedback(Result.Message);return false;}
    TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
    auto* Definition=Shop[Index].Get();
    // All fallible placement work precedes economy/random changes and notifications.
    // No temporary hand card is published and an invalid spawn releases its cell/roster.
    if(!CommitDefinitionPlacement(Definition,FGuid::NewGuid(),Cell,Result,1,FMath::Max(0,Rules.PurchasePrice)))return false;
    Gold-=Rules.PurchasePrice;Shop[Index]=DrawShopCard();ShopSlotRevisions[Index]=++ShopRevision;
    NotifyHandChanged();OnEconomyChanged.Broadcast();OnBoardChanged.Broadcast();return true;
}
int32 AEGCardManager::GetRefreshCost() const
{
    const auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    if(RoundShopDefinition&&RoundShopDefinition->bSevenStageRules&&GM&&GM->GetMatch()&&GM->GetMatch()->GetPhase()==EEGMatchPhase::Preparation)
    {
        const auto& Balance=UEGStigmataBalanceSettings::Get();
        const int64 Free=FMath::Max(0,Balance.BaseFreeRefreshesPerPreparation)+int64(GM->GetMatch()->HasStigmata(TEXT("Reverie"))?FMath::Max(0,Balance.ReverieExtraFreeRefreshes):0);
        if(FreeRefreshesUsed<Free)return 0;
    }
    // One gold is the established refresh fee; the UI reads it from here instead
    // of hard-coding a static label.
    return 1;
}
bool AEGCardManager::RefreshShop()
{
    if(!HasAuthority()||bCommitting||!CanTrade()||CardPool.IsEmpty())return false;
    const int32 Cost=GetRefreshCost();
    if(Gold<Cost){Feedback(FText::FromString(FString::Printf(TEXT("刷新需要 %d 崩坏能"),Cost)));return false;}
    auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    auto* Match=GM?GM->GetMatch():nullptr;
    const bool bReverieFree=Cost==0&&RoundShopDefinition&&RoundShopDefinition->bSevenStageRules&&Match&&
        Match->GetPhase()==EEGMatchPhase::Preparation&&Match->HasStigmata(TEXT("Reverie"))&&
        FreeRefreshesUsed>=FMath::Max(0,UEGStigmataBalanceSettings::Get().BaseFreeRefreshesPerPreparation);
    TGuardValue<bool> Guard(bCommitting,true);Gold-=Cost;
    if(Cost==0)
    {
        ++FreeRefreshesUsed;
    }
    GenerateShop();if(bReverieFree)Match->RecordStigmataRefreshSavings(TEXT("Reverie"),1);
    OnEconomyChanged.Broadcast();return true;
}
bool AEGCardManager::ToggleShopLock()
{if(!HasAuthority()||bCommitting||!CanTrade())return false;bShopLocked=!bShopLocked;OnEconomyChanged.Broadcast();return true;}
bool AEGCardManager::GrantCard(UEGCardDefinition* Definition,int32 Count)
{
    if(!HasAuthority()||bCommitting||bEnding||IsStigmataTransactionActive()||!Definition||Count<1||Count>128)return false;
    TGuardValue<bool> Guard(bCommitting,true);
    Definition=ResolveUnlockedCard(Definition);
    RegisteredCards.AddUnique(Definition);
    for(int32 I=0;I<Count;++I){FEGCardInstance C;C.InstanceId=FGuid::NewGuid();C.Definition=Definition;C.CardId=Definition->CardId;Hand.Add(C);}
    NotifyHandChanged();return true;
}
bool AEGCardManager::GrantCardsBatch(const TArray<UEGCardDefinition*>& Definitions,bool bNotify,int32 InitialLevel)
{
    if(!HasAuthority()||bCommitting||bEnding||(bNotify&&IsStigmataTransactionActive())||Definitions.IsEmpty())return false;
    for(const auto* Definition:Definitions)
        if(!IsValid(Definition)||Definition->CardId.IsNone()||!Definition->HeroDefinition||!Definition->PieceClass)return false;
    TGuardValue<bool> Guard(bCommitting,true);
    for(auto* Definition:Definitions)
    {
        Definition=ResolveUnlockedCard(Definition);
        RegisteredCards.AddUnique(Definition);
        FEGCardInstance Card;Card.InstanceId=FGuid::NewGuid();Card.Definition=Definition;Card.CardId=Definition->CardId;Card.InitialLevel=FMath::Max(1,InitialLevel);
        Hand.Add(Card);
    }
    SynchronizeHandOrder();
    if(bNotify)NotifyHandChanged();return true;
}
void AEGCardManager::PromoteHeldHeroesToLevel(int32 MinimumLevel)
{
    if(!HasAuthority()||!Roster.IsValid()||MinimumLevel<1)return;
    for(auto& Hero:Roster->Heroes)if(Hero.Level<MinimumLevel){Hero.Level=MinimumLevel;Roster->NotifyHeroChanged(Hero.InstanceId);}
    for(auto& Card:Hand)Card.InitialLevel=FMath::Max(Card.InitialLevel,MinimumLevel);
}
void AEGCardManager::BeginRoundShopRound(int32 Stage,bool bFirst)
{
    if(!HasAuthority()||!RoundShopDefinition||RoundShopDefinition->RoundIncome<0||bCommitting||IsStigmataTransactionActive()||Stage<=LastIncomeStage)return;
    TGuardValue<bool> Guard(bCommitting,true);
    LastIncomeStage=Stage;InitialRound=Stage;FreeRefreshesUsed=0;
    Rules.DeploymentLimit=RoundShopDefinition->bUseFixedPopulation?FMath::Max(1,RoundShopDefinition->FixedPopulation):FMath::Max(PopulationFloor,PopulationForStage(Stage));
    if(!bFirst&&!RoundShopDefinition->bSevenStageRules)Gold+=RoundShopDefinition->RoundIncome;
    if(!bShopLocked)GenerateShop();
    OnEconomyChanged.Broadcast();OnBoardChanged.Broadcast();
}
void AEGCardManager::SetPopulationFloor(int32 Count,bool bNotify)
{
    if(bNotify&&IsStigmataTransactionActive())return;
    if(RoundShopDefinition&&RoundShopDefinition->bUseFixedPopulation)
    {
        Rules.DeploymentLimit=FMath::Max(1,RoundShopDefinition->FixedPopulation);
        if(bNotify)OnBoardChanged.Broadcast();return;
    }
    PopulationFloor=FMath::Max(PopulationFloor,Count);
    Rules.DeploymentLimit=FMath::Max(Rules.DeploymentLimit,PopulationFloor);
    if(bNotify)OnBoardChanged.Broadcast();
}
void AEGCardManager::SellExcessHand()
{
    if(BeginHandSettlement())EndHandSettlement(true);
}
bool AEGCardManager::CanResolveExcessHand() const
{
    if(!RoundShopDefinition||RoundShopDefinition->bSevenStageRules)return true;
    int32 Protected=0;
    for(const auto& Hero:GetReserveHeroes())if(!Hero.Definition||IsOverflowProtected(Hero.InstanceId))++Protected;
    for(const auto& Card:Hand)if(IsOverflowProtected(Card.InstanceId))++Protected;
    return Protected<=FMath::Max(1,Rules.HandLimit);
}
bool AEGCardManager::BeginHandSettlement()
{
    if(!HasAuthority()||bCommitting||bEnding||!RoundShopDefinition||!Roster.IsValid()||!CanResolveExcessHand())return false;
    EncounterRecoveryHand=Hand;EncounterRecoveryOrder=OrderedHandIds;EncounterRecoveryGold=Gold;bEncounterHandSnapshot=true;
    bCommitting=true;bHandSettlement=true;Roster->bHandSettlementLocked=true;return true;
}
bool AEGCardManager::RestoreEncounterHandSnapshot()
{
    if(!HasAuthority()||!bEncounterHandSnapshot||bCommitting||bEnding||!Roster.IsValid())return false;
    TGuardValue<bool> Guard(bCommitting,true);
    Hand=EncounterRecoveryHand;OrderedHandIds=EncounterRecoveryOrder;Gold=EncounterRecoveryGold;
    SynchronizeUnlockedCards();
    bEncounterHandSnapshot=false;EncounterRecoveryHand.Reset();EncounterRecoveryOrder.Reset();
    NotifyHandChanged();OnEconomyChanged.Broadcast();return true;
}

bool AEGCardManager::CaptureRunCheckpoint(FEGCardRunCheckpoint& Out) const
{
    if(!HasAuthority()||bCommitting||bEnding||!Roster.IsValid()||!IsValid(Board))return false;
    FEGCardRunCheckpoint Snapshot;
    Snapshot.Hand=Hand;Snapshot.Order=OrderedHandIds;Snapshot.Shop=Shop;Snapshot.RegisteredCards=RegisteredCards;Snapshot.Rules=Rules;
    Snapshot.Gold=Gold;Snapshot.CorruptionShopSlot=CorruptionShopSlot;
    Snapshot.LastIncomeStage=LastIncomeStage;Snapshot.InitialRound=InitialRound;Snapshot.PopulationFloor=PopulationFloor;
    Snapshot.FreeRefreshesUsed=FreeRefreshesUsed;Snapshot.bShopLocked=bShopLocked;Snapshot.Random=ShopRandom;
    for(const auto* Piece:GetDeploymentParts())
    {
        if(!IsValid(Piece)||!Piece->InstanceId.IsValid()||!Piece->RosterId.IsValid()||!Piece->CardDefinition||!Piece->HeroVisualDefinition)return false;
        auto& Entry=Snapshot.Pieces.AddDefaulted_GetRef();Entry.PieceClass=Piece->GetClass();
        Entry.Card=Piece->CardDefinition;Entry.Visual=Piece->HeroVisualDefinition;
        Entry.InstanceId=Piece->InstanceId;Entry.RosterId=Piece->RosterId;Entry.Cell=Piece->Cell;Entry.PartRole=Piece->PartRole;
    }
    Out=MoveTemp(Snapshot);return true;
}

bool AEGCardManager::RestoreRunCheckpoint(const FEGCardRunCheckpoint& In)
{
    if(!HasAuthority()||bCommitting||bEnding||!Roster.IsValid()||!IsValid(Board)||!GetWorld()||GetWorld()->bIsTearingDown)return false;
    TGuardValue<bool> Guard(bCommitting,true);
    const auto Previous=GetDeploymentParts();
    TSet<FIntPoint> Claimed;TSet<FGuid> Ids;
    for(const auto& Entry:In.Pieces)
    {
        if(!Entry.PieceClass||Entry.PieceClass->HasAnyClassFlags(CLASS_Abstract)||!Entry.Card||!Entry.Visual||
           !Entry.InstanceId.IsValid()||!Entry.RosterId.IsValid()||Ids.Contains(Entry.InstanceId))return false;
        Ids.Add(Entry.InstanceId);
        TArray<FIntPoint> Cells;
        const auto Size=Entry.PartRole==EEGDeploymentPart::Hero?PlacementSize(Entry.Card):FIntPoint(1,1);
        if(!AEGDeploymentBoard::MakeFootprintCells(Entry.Cell,Size,Cells))return false;
        for(const auto Cell:Cells)
        {
            AActor* Occupant=Board->GetOccupant(Cell);
            if(!Board->IsPlayerCell(Cell)||Claimed.Contains(Cell)||
               (Occupant&&!Previous.ContainsByPredicate([Occupant](const AEGDeployedPiece* P){return P==Occupant;})))return false;
            Claimed.Add(Cell);
        }
    }
    // Spawn before releasing old occupancy. A spawn failure leaves the live state untouched.
    TArray<AEGDeployedPiece*> Created;
    auto DiscardCreated=[&](){for(auto* P:Created)if(IsValid(P))P->Destroy();};
    for(const auto& Entry:In.Pieces)
    {
        FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* P=GetWorld()->SpawnActor<AEGDeployedPiece>(Entry.PieceClass,Board->CellToWorld(Entry.Cell),PlayerPieceRotation(Board,Entry.Visual),Params);
        if(!IsValid(P)||P->IsActorBeingDestroyed()){DiscardCreated();return false;}
        P->SetActorHiddenInGame(true);P->PartRole=Entry.PartRole;P->RosterId=Entry.RosterId;P->CardDefinition=Entry.Card;
        P->AssignPlacement(Board,Entry.Cell,Entry.InstanceId);P->ApplyHeroDefinition(Entry.Visual);Created.Add(P);
    }
    for(auto* P:Previous)if(IsValid(P))Board->ReleaseFootprint(P->Cell,P);
    int32 Placed=0;
    for(;Placed<Created.Num();++Placed)
    {
        const auto& Entry=In.Pieces[Placed];
        const auto Size=Entry.PartRole==EEGDeploymentPart::Hero?PlacementSize(Entry.Card):FIntPoint(1,1);
        if(!Board->TryOccupyFootprint(Entry.Cell,Size,Created[Placed]))break;
    }
    if(Placed!=Created.Num())
    {
        for(int32 I=0;I<Placed;++I)Board->ReleaseFootprint(Created[I]->Cell,Created[I]);
        for(auto* P:Previous)if(IsValid(P))
        {
            const auto Size=P->PartRole==EEGDeploymentPart::Hero?PlacementSize(P->CardDefinition):FIntPoint(1,1);
            const bool bRestored=Board->TryOccupyFootprint(P->Cell,Size,P);
            ensureAlwaysMsgf(bRestored,TEXT("Checkpoint occupancy rollback failed"));
        }
        DiscardCreated();return false;
    }
    Pieces.Reset();DevicePieces.Reset();PendingDeviceEntrances.Reset();
    for(auto* P:Previous)if(IsValid(P)){P->OnDestroyed.RemoveDynamic(this,&ThisClass::PieceDestroyed);P->Destroy();}
    for(auto* P:Created)
    {
        if(P->PartRole==EEGDeploymentPart::Hero)Pieces.Add(P->InstanceId,P);else DevicePieces.Add(P->InstanceId,P);
        P->OnDestroyed.AddDynamic(this,&ThisClass::PieceDestroyed);P->SetActorHiddenInGame(false);
        P->RefreshPreparationVitals();P->SetPreparationVitalsVisible(P->PartRole==EEGDeploymentPart::Hero);
    }
    Hand=In.Hand;OrderedHandIds=In.Order;Gold=In.Gold;Rules=In.Rules;Shop=In.Shop;RegisteredCards=In.RegisteredCards;
    CorruptionShopSlot=In.CorruptionShopSlot;LastIncomeStage=In.LastIncomeStage;InitialRound=In.InitialRound;PopulationFloor=In.PopulationFloor;
    FreeRefreshesUsed=In.FreeRefreshesUsed;bShopLocked=In.bShopLocked;ShopRandom=In.Random;
    // Never resurrect stale UI revisions from the saved checkpoint.
    ShopRevision=ShopRevision==MAX_int32?1:ShopRevision+1;ShopSlotRevisions.Init(ShopRevision,Shop.Num());
    HandOrderRevision=HandOrderRevision==MAX_int32?1:HandOrderRevision+1;
    EncounterRecoveryHand.Reset();EncounterRecoveryOrder.Reset();bEncounterHandSnapshot=false;bHandSettlement=false;
    return true;
}
void AEGCardManager::NotifyRunCheckpointRestored()
{OnHandChanged.Broadcast();OnBoardChanged.Broadcast();OnEconomyChanged.Broadcast();}
void AEGCardManager::EndHandSettlement(bool bCommit)
{
    if(!bHandSettlement)return;
    if(bCommit&&Roster.IsValid()&&RoundShopDefinition->bSevenStageRules)
    {
        FEGScopedRosterMutation Mutation(Roster.Get());
        const auto Excess=GetOverflowHandIds();
        int64 Proceeds=0;
        for(const FGuid Id:Excess)
        {
            const int64 Value=GetSaleValue(Id);
            const int32 Index=FindCard(Id);
            if(Index!=INDEX_NONE)
            {
                Hand.RemoveAt(Index);
                Proceeds+=Value;
            }
            else if(Roster->RemoveOverflowReserve(Id))Proceeds+=Value;
            else ensureMsgf(false,TEXT("Locked overflow reserve could not be removed"));
        }
        if(!Excess.IsEmpty())
        {
            Gold+=Proceeds;
            NotifyHandChanged();
            OnEconomyChanged.Broadcast();
        }
    }
    else if(bCommit&&Roster.IsValid()&&!RoundShopDefinition->bSevenStageRules)
    {
        FEGScopedRosterMutation Mutation(Roster.Get());
        const auto Reserve=GetReserveHeroes();
        int32 Excess=Hand.Num()+Reserve.Num()-FMath::Max(1,Rules.HandLimit);
        int32 Sold=0;
        for(int32 I=Hand.Num()-1;I>=0&&Excess>0;--I)
            if(!IsOverflowProtected(Hand[I].InstanceId)){Hand.RemoveAt(I);++Sold;--Excess;}
        for(int32 I=Reserve.Num()-1;I>=0&&Excess>0;--I)
            if(Reserve[I].Definition&&!IsOverflowProtected(Reserve[I].InstanceId)&&Roster->RemoveOverflowReserve(Reserve[I].InstanceId)){++Sold;--Excess;}
        ensureMsgf(Excess<=0,TEXT("Hand settlement changed while locked"));
        if(Sold){Gold+=Sold;NotifyHandChanged();OnEconomyChanged.Broadcast();}
    }
    if(Roster.IsValid())Roster->bHandSettlementLocked=false;
    bHandSettlement=false;bCommitting=false;
}
bool AEGCardManager::RequestObtainCard(UEGCardDefinition* Definition)
{
    if(RoundShopDefinition)return false; // A purchase must name a current shop slot.
    if(!HasAuthority()||bCommitting||!(RoundShopDefinition?CanTrade():CanEditPlacement())||(Definition!=AvailableCardDefinition&&!CardPool.Contains(Definition))||
        (Definition&&Definition->CardId.IsNone()))return false;
    if(Gold<Rules.PurchasePrice){Feedback(FText::FromString(TEXT("崩坏能不足。")));return false;}
    if(!RoundShopDefinition&&Rules.HandLimit>0&&Hand.Num()>=Rules.HandLimit){Feedback(FText::FromString(TEXT("手牌已达上限。")));return false;}
    TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
    Gold-=Rules.PurchasePrice;
    Definition=ResolveUnlockedCard(Definition);
    FEGCardInstance Card;Card.InstanceId=FGuid::NewGuid();Card.Definition=Definition;
    Card.PaidCost=FMath::Max(0,Rules.PurchasePrice);
    if(Definition)Card.CardId=Definition->CardId;
    Hand.Add(Card);NotifyHandChanged();OnEconomyChanged.Broadcast();return true;
}
UEGCardDefinition* AEGCardManager::FindHandCardDefinition(FGuid Id) const
{const int32 Index=FindCard(Id);return Index==INDEX_NONE?nullptr:Hand[Index].Definition.Get();}
FEGPlacementEvaluation AEGCardManager::EvaluateHandPlacement(FGuid Id,FIntPoint Cell) const
{
    const int32 Index=FindCard(Id);
    if(!CanEditPlacement())return Evaluation(EEGPlacementAction::Invalid,TEXT("当前阶段不能布阵。"));
    if(Index==INDEX_NONE)return Evaluation(EEGPlacementAction::Invalid,TEXT("该手牌已失效。"));
    return EvaluateDefinitionPlacement(Hand[Index].Definition,Cell);
}
FEGPlacementEvaluation AEGCardManager::EvaluateDefinitionPlacement(const UEGCardDefinition* Definition,FIntPoint Cell) const
{
    if(!CanEditPlacement())return Evaluation(EEGPlacementAction::Invalid,TEXT("当前阶段不能布阵。"));
    if(!IsValid(Board)||!Board->IsCellValid(Cell))return Evaluation(EEGPlacementAction::Invalid,TEXT("请将手牌拖入棋盘。"));
    if(RoundShopDefinition&&RoundShopDefinition->bUseExplicitStartingCards&&Definition&&
       IsHerrscherOfHuman(Definition->HeroDefinition)&&Roster.IsValid()&&Roster->FindHeroByDefinition(Definition->HeroDefinition).IsValid())
        return Evaluation(EEGPlacementAction::Invalid,TEXT("本轮测试中人之律者不参与合成。"));
    // An existing deployed hero owns same-name growth, regardless of the pointer cell.
    if(Rules.bAllowSameNameMerge&&Definition&&Definition->HeroDefinition&&Roster.IsValid())
    {
        FEGHeroInstance Hero;
        const FGuid HeroId=Roster->FindHeroByDefinition(Definition->HeroDefinition);
        if(Roster->GetHero(HeroId,Hero)&&Hero.bDeployed)
        {
            for(auto* Target:GetPieces())
            {
                if(!OwnsPiece(Target)||Target->RosterId!=HeroId||Board->GetOccupant(Target->Cell)!=Target)continue;
                if(Rules.SameNameLimit>0&&Hero.SameNameCards>=Rules.SameNameLimit)
                    return Evaluation(EEGPlacementAction::Invalid,TEXT("已达到同名合成上限。"));
                auto Result=Evaluation(EEGPlacementAction::Merge,TEXT("松开合成同名英雄  发光格为合成目标"));
                Result.TargetHeroId=HeroId;Result.TargetCell=Target->Cell;return Result;
            }
        }
    }
    if(!Board->IsPlayerCell(Cell))return Evaluation(EEGPlacementAction::Invalid,TEXT("新英雄请放在我方部署区。"));
    if(!Board->CanOccupyFootprint(Cell,PlacementSize(Definition)))
        return Evaluation(EEGPlacementAction::Invalid,TEXT("部署范围必须完全位于我方区域且没有其他棋子。"));
    if(Definition&&Definition->HeroDefinition&&Definition->HeroDefinition->HeroKind==EEGHeroKind::VillV)
    {
     FIntPoint Gatling,Cannon;
     if(!FindVillVCells(Cell,Gatling,Cannon))return Evaluation(EEGPlacementAction::Invalid,TEXT("维尔薇需要人物和两台装置共三个空格。"));
    }
    if(Rules.DeploymentLimit>0&&GetPieces().Num()>=Rules.DeploymentLimit)
        return Evaluation(EEGPlacementAction::Invalid,TEXT("部署数量已达上限；同名卡拖入棋盘即可合成。"));
    const TSubclassOf<AEGDeployedPiece> SpawnClass=Definition?Definition->PieceClass:PieceClass;
    if(!SpawnClass||SpawnClass->HasAnyClassFlags(CLASS_Abstract))return Evaluation(EEGPlacementAction::Invalid,TEXT("棋子配置无效。"));
    if(Roster.IsValid()&&Definition&&Roster->FindHeroByDefinition(Definition->HeroDefinition).IsValid())return Evaluation(EEGPlacementAction::Invalid,TEXT("同名英雄在备战区，请拖到对应备战卡合成。"));
    if(Roster.IsValid()&&(!Definition||!Definition->HeroDefinition))return Evaluation(EEGPlacementAction::Invalid,TEXT("卡牌缺少英雄配置。"));
    return Evaluation(EEGPlacementAction::Deploy,TEXT("松开鼠标部署棋子。"));
}
FEGPlacementEvaluation AEGCardManager::EvaluatePieceMove(AEGDeployedPiece* Piece,FIntPoint Cell) const
{
    if(!CanEditPlacement())return Evaluation(EEGPlacementAction::Invalid,TEXT("当前阶段不能布阵。"));
    if(!OwnsPiece(Piece)||!IsValid(Board)||Board->GetOccupant(Piece->Cell)!=Piece)
        return Evaluation(EEGPlacementAction::Invalid,TEXT("棋子已失效。"));
    const auto* Occupant=Cast<AEGDeployedPiece>(Board->GetOccupant(Cell));
    auto IsVillVPart=[](const AEGDeployedPiece* P){return P&&P->CardDefinition&&P->CardDefinition->HeroDefinition&&P->CardDefinition->HeroDefinition->HeroKind==EEGHeroKind::VillV;};
    const FIntPoint Size=PlacementSize(Piece->CardDefinition);
    if(Size!=FIntPoint(1,1))
    {
        if(!Board->CanOccupyFootprint(Cell,Size,Piece))
            return Evaluation(EEGPlacementAction::Invalid,TEXT("部署范围必须完全位于我方区域且没有其他棋子。"));
        return Evaluation(EEGPlacementAction::Move,TEXT("松开鼠标调整部署位置。"));
    }
    if(Occupant&&Occupant!=Piece&&PlacementSize(Occupant->CardDefinition)!=FIntPoint(1,1))
        return Evaluation(EEGPlacementAction::Invalid,TEXT("大型棋子占用的格子不可交换。"));
    if(Cell!=Piece->Cell&&Occupant&&(IsVillVPart(Piece)||IsVillVPart(Occupant)))
        return Evaluation(EEGPlacementAction::Invalid,TEXT("维尔薇的人物和装置需要独立空格。"));
    if(!Board->IsPlayerCell(Cell)||(Cell!=Piece->Cell&&!Board->IsCellAvailable(Cell)&&(RoundShopDefinition||!OwnsPiece(Cast<AEGDeployedPiece>(Board->GetOccupant(Cell))))))
        return Evaluation(EEGPlacementAction::Invalid,TEXT("目标格不可移动。"));
    return Evaluation(EEGPlacementAction::Move,TEXT("松开鼠标调整部署位置。"));
}
bool AEGCardManager::TryDeployHandCard(FGuid Id,FIntPoint Cell)
{
    if(!HasAuthority()||bCommitting)return false;
    const auto Result=EvaluateHandPlacement(Id,Cell);
    if(!Result.IsAllowed()){Feedback(Result.Message);return false;}
    TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
    const int32 Index=FindCard(Id);
    auto* Definition=Hand[Index].Definition.Get();
    if(!CommitDefinitionPlacement(Definition,Id,Cell,Result,Hand[Index].InitialLevel,Hand[Index].PaidCost))return false;
    Hand.RemoveAt(Index);NotifyHandChanged();OnBoardChanged.Broadcast();
    if(Result.Action==EEGPlacementAction::Merge)Feedback(FText::FromString(TEXT("同名合成成功，英雄成长已保存。")));
    return true;
}
bool AEGCardManager::CommitDefinitionPlacement(UEGCardDefinition* Definition,FGuid Id,FIntPoint Cell,const FEGPlacementEvaluation& Result,int32 InitialLevel,int64 PaidCost)
{
    if(InitialLevel<1)return false;
    if(auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>())
        if(!Mode->EnsureHeroResources(Definition?Definition->HeroDefinition.Get():nullptr))return false;
    if(Result.Action==EEGPlacementAction::Merge)
    {
        auto* Target=Cast<AEGDeployedPiece>(Board->GetOccupant(Result.TargetCell));
        auto* Hero=Roster.IsValid()?Roster->FindMutable(Result.TargetHeroId):nullptr;
        if(!OwnsPiece(Target)||Target->RosterId!=Result.TargetHeroId||!Hero||PaidCost<0||Hero->PaidCost>MAX_int64-PaidCost||!Roster->ApplySameNameCard(Result.TargetHeroId))return false;
        Hero->PaidCost+=PaidCost;
        AEGPresentationBurst::Spawn(GetWorld(),Target->GetActorLocation(),Definition->HeroDefinition?Definition->HeroDefinition->GetSkillMaterial().LoadSynchronous():nullptr,1.4f);
        return true;
    }
    const auto SpawnClass=Definition?Definition->PieceClass:PieceClass;
    FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Piece=GetWorld()->SpawnActor<AEGDeployedPiece>(SpawnClass,Board->CellToWorld(Cell),PlayerPieceRotation(Board,Definition?Definition->HeroDefinition.Get():nullptr),Params);
    if(!IsValid(Piece))return false;
    if(!IsValid(Board)||!Board->TryOccupyFootprint(Cell,PlacementSize(Definition),Piece)){Piece->Destroy();return false;}
    if(Roster.IsValid())
    {
        UEGHeroDefinition* RosterDefinition=Definition->HeroDefinition.Get();
        for(const auto& Candidate:CardPool)
            if(Candidate&&Candidate->HeroDefinition&&Candidate->HeroDefinition->EnhancedDefinition==RosterDefinition&&
               Roster->IsFormUnlocked(Candidate->HeroDefinition))
            {RosterDefinition=Candidate->HeroDefinition;break;}
        Piece->RosterId=Roster->AddHero(RosterDefinition);
        if(!Piece->RosterId.IsValid()||(InitialLevel>1&&!Roster->AddLevels(Piece->RosterId,InitialLevel-1))||!Roster->SetDeployment(Piece->RosterId,Cell))
        {
            if(Piece->RosterId.IsValid())Roster->RemoveUncommittedHero(Piece->RosterId);
            Board->ReleaseFootprint(Cell,Piece);Piece->Destroy();return false;
        }
        if(auto* Hero=Roster->FindMutable(Piece->RosterId))Hero->PaidCost=FMath::Max<int64>(0,PaidCost);
    }
    Piece->CardDefinition=Definition;Piece->AssignPlacement(Board,Cell,Id);Pieces.Add(Id,Piece);
    Piece->OnDestroyed.AddDynamic(this,&ThisClass::PieceDestroyed);
    if(Definition&&Definition->HeroDefinition&&Definition->HeroDefinition->HeroKind==EEGHeroKind::VillV&&!EnsureVillVLayout(Piece))
    {
     const FGuid HeroId=Piece->RosterId;DestroyDeploymentGroup(HeroId);if(Roster.IsValid())Roster->RemoveUncommittedHero(HeroId);return false;
    }
    RefreshHeroPresentations();
    if(!PlayDeploymentEntrance(Piece))
        AEGPresentationBurst::Spawn(GetWorld(),Piece->GetActorLocation(),Definition&&Definition->HeroDefinition?Definition->HeroDefinition->GetSkillMaterial().LoadSynchronous():nullptr);
    return true;
}
bool AEGCardManager::TryMovePlacedPiece(AEGDeployedPiece* Piece,FIntPoint Cell)
{
    if(!HasAuthority()||bCommitting)return false;
    const auto Result=EvaluatePieceMove(Piece,Cell);
    if(!Result.IsAllowed()){Feedback(Result.Message);return false;}
    if(Cell==Piece->Cell)return true;
    TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
    const FIntPoint Previous=Piece->Cell;
    const FIntPoint Size=PlacementSize(Piece->CardDefinition);
    if(Size!=FIntPoint(1,1))
    {
        if(!Board->TryMoveFootprint(Piece,Previous,Cell,Size))return false;
        if(Roster.IsValid()&&!Roster->SetDeployment(Piece->RosterId,Cell))
        {
            const bool bRestored=Board->TryMoveFootprint(Piece,Cell,Previous,Size);
            ensureMsgf(bRestored,TEXT("Footprint rollback failed"));
            return false;
        }
        Piece->AssignPlacement(Board,Cell,Piece->InstanceId);Piece->SetActorLocation(Board->CellToWorld(Cell));
        OnBoardChanged.Broadcast();return true;
    }
    if(auto* Other=Cast<AEGDeployedPiece>(Board->GetOccupant(Cell)))
    {
        FEGHeroInstance FirstHero,SecondHero;
        if(!OwnsPiece(Other)||(Roster.IsValid()&&(!Roster->GetHero(Piece->RosterId,FirstHero)||!Roster->GetHero(Other->RosterId,SecondHero))))return false;
        Board->ReleaseCell(Previous,Piece);Board->ReleaseCell(Cell,Other);
        if(!Board->TryOccupyCell(Cell,Piece)||!Board->TryOccupyCell(Previous,Other))
        {Board->ReleaseCell(Cell,Piece);Board->ReleaseCell(Previous,Other);Board->TryOccupyCell(Previous,Piece);Board->TryOccupyCell(Cell,Other);return false;}
        if(Roster.IsValid())
        {
            const auto Snapshot=Roster->GetHeroes();
            if(!Roster->SetDeployment(Piece->RosterId,Cell)||!Roster->SetDeployment(Other->RosterId,Previous))
            {Roster->RestoreSnapshot(Snapshot);Board->ReleaseCell(Cell,Piece);Board->ReleaseCell(Previous,Other);Board->TryOccupyCell(Previous,Piece);Board->TryOccupyCell(Cell,Other);return false;}
        }
        Piece->AssignPlacement(Board,Cell,Piece->InstanceId);Other->AssignPlacement(Board,Previous,Other->InstanceId);
        Piece->SetActorLocation(Board->CellToWorld(Cell));Other->SetActorLocation(Board->CellToWorld(Previous));OnBoardChanged.Broadcast();return true;
    }
    if(!Board->ReleaseCell(Previous,Piece))return false;
    if(!Board->TryOccupyCell(Cell,Piece)){Board->TryOccupyCell(Previous,Piece);return false;}
    bool bRosterMoved=true;
    if(Roster.IsValid())
    {
     FEGHeroInstance H;
     if(Roster->GetHero(Piece->RosterId,H)&&H.bVillVLayout)
     {
      if(Piece->PartRole==EEGDeploymentPart::Hero)H.DeploymentCell=Cell;
      else if(Piece->PartRole==EEGDeploymentPart::Gatling)H.GatlingCell=Cell;
      else H.CannonCell=Cell;
      bRosterMoved=Roster->SetVillVLayout(H.InstanceId,H.DeploymentCell,H.GatlingCell,H.CannonCell);
     }
     else bRosterMoved=Roster->SetDeployment(Piece->RosterId,Cell);
    }
    if(!bRosterMoved)
    {Board->ReleaseCell(Cell,Piece);Board->TryOccupyCell(Previous,Piece);return false;}
    Piece->AssignPlacement(Board,Cell,Piece->InstanceId);Piece->SetActorLocation(Board->CellToWorld(Cell));
    const auto* Definition=Piece->CardDefinition.Get();
    AEGPresentationBurst::Spawn(GetWorld(),Piece->GetActorLocation(),Definition&&Definition->HeroDefinition?Definition->HeroDefinition->GetSkillMaterial().LoadSynchronous():nullptr,.7f);
    OnBoardChanged.Broadcast();return true;
}
int64 AEGCardManager::GetSaleValue(FGuid Id) const
{
 if(IsSaleProtected(Id))return 0;
 if(const auto* Device=DevicePieces.Find(Id);Device&&Device->IsValid())return GetSaleValue(Device->Get()->RosterId);
 if(RoundShopDefinition&&RoundShopDefinition->bSevenStageRules)
 {
  int64 Paid=0;
  const int32 Index=FindCard(Id);
  if(Index!=INDEX_NONE)Paid=Hand[Index].PaidCost;
  else {FEGHeroInstance Hero;if(Roster.IsValid()&&Roster->GetHero(Id,Hero))Paid=Hero.PaidCost;else for(const auto* P:GetPieces())if(P&&P->InstanceId==Id&&Roster.IsValid()&&Roster->GetHero(P->RosterId,Hero)){Paid=Hero.PaidCost;break;}}
  const auto& Balance=UEGStigmataBalanceSettings::Get();
  const int64 Denominator=FMath::Max(1,Balance.SaleRefundDenominator),Numerator=FMath::Clamp<int64>(Balance.SaleRefundNumerator,0,Denominator);
  Paid=FMath::Max<int64>(0,Paid);
  return (Paid/Denominator)*Numerator+((Paid%Denominator)*Numerator)/Denominator;
 }
 if(RoundShopDefinition)return HasHandCard(Id)||HasReserveHero(Id)||GetPieces().ContainsByPredicate([Id](const AEGDeployedPiece* P){return P->RosterId==Id||P->InstanceId==Id;})?1:0;
 if(HasHandCard(Id))return FMath::Max(0,Rules.PurchasePrice);
 FEGHeroInstance H;if(Roster.IsValid()&&Roster->GetHero(Id,H))return static_cast<int64>(FMath::Max(1,H.SameNameCards))*FMath::Max(0,Rules.PurchasePrice);
 for(const auto* P:GetPieces())if(P->InstanceId==Id)return Roster.IsValid()?GetSaleValue(P->RosterId):FMath::Max(0,Rules.PurchasePrice);
 return 0;
}
bool AEGCardManager::IsSaleProtected(FGuid Id) const
{
 if(const int32 Index=FindCard(Id);Index!=INDEX_NONE)
     return Hand[Index].Definition&&IsElysia(Hand[Index].Definition->HeroDefinition);
 FEGHeroInstance H;
 if(Roster.IsValid()&&Roster->GetHero(Id,H))return IsElysia(H.Definition);
 for(const auto* P:GetDeploymentParts())if(P&&(P->InstanceId==Id||P->RosterId==Id))
     return Roster.IsValid()&&Roster->GetHero(P->RosterId,H)&&IsElysia(H.Definition);
 return false;
}
bool AEGCardManager::IsOverflowProtected(FGuid Id) const
{
 if(IsSaleProtected(Id))return true;
 // The final-route reward must survive automatic excess-hand settlement even
 // when the hand was already full; voluntary selling remains unchanged.
 const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 const bool bOriginReward=RoundShopDefinition&&RoundShopDefinition->bSevenStageRules&&Mode&&Mode->GetMatch()&&
     Mode->GetMatch()->GetEndgameRoute()==TEXT("OriginFlower");
 if(!RoundShopDefinition||(!RoundShopDefinition->bUseExplicitStartingCards&&!bOriginReward))return false;
 if(const int32 Index=FindCard(Id);Index!=INDEX_NONE)
     return Hand[Index].Definition&&IsHerrscherOfHuman(Hand[Index].Definition->HeroDefinition);
 FEGHeroInstance H;
 return Roster.IsValid()&&Roster->GetHero(Id,H)&&IsHerrscherOfHuman(H.Definition);
}
bool AEGCardManager::TrySellHandCard(FGuid Id)
{
 if(!HasAuthority()||!CanEditPlacement()||bCommitting||bEnding||IsStigmataTransactionActive())return false;
 const int32 Index=FindCard(Id);if(Index==INDEX_NONE)return false;
 if(IsSaleProtected(Id)){Feedback(FText::FromString(TEXT("爱莉希雅不可出售。")));return false;}
 TGuardValue<bool> Guard(bCommitting,true);Gold+=GetSaleValue(Id);Hand.RemoveAt(Index);
 NotifyHandChanged();OnEconomyChanged.Broadcast();return true;
}
bool AEGCardManager::TrySellReserveHero(FGuid Id)
{
 if(!HasAuthority()||!CanEditPlacement()||bCommitting||bEnding||IsStigmataTransactionActive()||!HasReserveHero(Id))return false;
 if(IsSaleProtected(Id)){Feedback(FText::FromString(TEXT("爱莉希雅不可出售。")));return false;}
 TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());const int64 Value=GetSaleValue(Id);
 if(!Roster->SellHeroFromCardManager(Id))return false;Gold+=Value;NotifyHandChanged();OnEconomyChanged.Broadcast();return true;
}
bool AEGCardManager::OwnsPiece(const AEGDeployedPiece* Piece) const
{
    if(!IsValid(Piece)||Piece->GetOwner()!=this)return false;
    const auto* Entry=Piece->PartRole==EEGDeploymentPart::Hero?Pieces.Find(Piece->InstanceId):DevicePieces.Find(Piece->InstanceId);return Entry&&Entry->Get()==Piece;
}
TArray<AEGDeployedPiece*> AEGCardManager::GetPieces() const
{
    TArray<AEGDeployedPiece*> Result;for(const auto& Entry:Pieces)if(Entry.Value.IsValid())Result.Add(Entry.Value.Get());return Result;
}
bool AEGCardManager::TrySellPlacedPiece(AEGDeployedPiece* Piece)
{
 if(!HasAuthority()||!CanEditPlacement()||bCommitting||bEnding||IsStigmataTransactionActive()||!OwnsPiece(Piece)||!IsValid(Board))return false;
 if(IsSaleProtected(Piece->RosterId)){Feedback(FText::FromString(TEXT("爱莉希雅不可出售。")));return false;}
 TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());const int64 Value=GetSaleValue(Piece->InstanceId);
 if(Roster.IsValid()&&!Roster->SellHeroFromCardManager(Piece->RosterId))return false;
 DestroyDeploymentGroup(Piece->RosterId);
 Gold+=Value;NotifyHandChanged();OnBoardChanged.Broadcast();OnEconomyChanged.Broadcast();return true;
}
void AEGCardManager::PieceDestroyed(AActor* Actor)
{
 auto* Piece=Cast<AEGDeployedPiece>(Actor);if(!Piece)return;
 const auto* Entry=Piece->PartRole==EEGDeploymentPart::Hero?Pieces.Find(Piece->InstanceId):DevicePieces.Find(Piece->InstanceId);
 if(!Entry||Entry->Get(true)!=Piece)return;
 const FGuid HeroId=Piece->RosterId;Pieces.Remove(Piece->InstanceId);DevicePieces.Remove(Piece->InstanceId);PendingDeviceEntrances.Remove(Piece->InstanceId);
 if(IsValid(Board))Board->ReleaseCell(Piece->Cell,Piece);
 DestroyDeploymentGroup(HeroId);
 if(!bEnding){if(Roster.IsValid())Roster->Withdraw(HeroId);NotifyHandChanged();OnBoardChanged.Broadcast();}
}
bool AEGCardManager::ResetCards(int32 InitialGold,int32 InitialCards,UEGCardDefinition* Definition)
{
    if(!HasAuthority()||!bInitialized||bCommitting||bEnding||IsStigmataTransactionActive()||InitialCards<0||InitialGold<0)return false;
    TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
    const auto Snapshot=GetDeploymentParts();Pieces.Reset();DevicePieces.Reset();PendingDeviceEntrances.Reset();
    for(auto* Piece:Snapshot)
    {
        Piece->OnDestroyed.RemoveDynamic(this,&ThisClass::PieceDestroyed);
        if(IsValid(Board))Board->ReleaseCell(Piece->Cell,Piece);
        if(Roster.IsValid())Roster->RemoveUncommittedHero(Piece->RosterId);
        Piece->Destroy();
    }
    Hand.Reset();Gold=InitialGold;AvailableCardDefinition=Definition;LastMessage=FText::GetEmpty();
    EncounterRecoveryHand.Reset();EncounterRecoveryOrder.Reset();bEncounterHandSnapshot=false;
    for(int32 I=0;I<InitialCards;++I)
    {
        auto* Resolved=ResolveUnlockedCard(Definition);
        FEGCardInstance Card;Card.InstanceId=FGuid::NewGuid();Card.Definition=Resolved;if(Resolved)Card.CardId=Resolved->CardId;Hand.Add(Card);
    }
    NotifyHandChanged();OnBoardChanged.Broadcast();OnEconomyChanged.Broadcast();return true;
}
void AEGCardManager::ConfigureDemoHand(int32 InitialGold,int32 InitialCards)
{ResetCards(InitialGold,InitialCards,AvailableCardDefinition);}
void AEGCardManager::AwardGold(int32 Amount)
{if(HasAuthority()&&!IsStigmataTransactionActive()&&Amount>0){Gold+=Amount;OnEconomyChanged.Broadcast();}}
void AEGCardManager::RollbackSkillGold(int32 Amount)
{if(HasAuthority()&&!IsStigmataTransactionActive()&&Amount>0){Gold=FMath::Max(int64(0),Gold-int64(Amount));OnEconomyChanged.Broadcast();}}

bool AEGCardManager::HasReserveHero(FGuid Id) const
{FEGHeroInstance H;return Roster.IsValid()&&Roster->GetHero(Id,H)&&!H.bDeployed;}
TArray<FEGHeroInstance> AEGCardManager::GetReserveHeroes() const
{TArray<FEGHeroInstance> Result;if(Roster.IsValid())for(const auto& H:Roster->GetHeroes())if(!H.bDeployed)Result.Add(H);return Result;}
UEGCardDefinition* AEGCardManager::FindHeroCardDefinition(FGuid Id) const
{
 FEGHeroInstance H;if(!Roster.IsValid()||!Roster->GetHero(Id,H))return nullptr;
 for(const auto& D:RegisteredCards)if(D&&D->HeroDefinition==H.Definition)return ResolveUnlockedCard(D.Get());
 for(const auto& D:RegisteredCards)if(D&&SameHero(D->HeroDefinition,H.Definition))return ResolveUnlockedCard(D.Get());
 for(const auto& D:CardPool)if(D&&SameHero(D->HeroDefinition,H.Definition))return ResolveUnlockedCard(D.Get());
 return AvailableCardDefinition&&SameHero(AvailableCardDefinition->HeroDefinition,H.Definition)?ResolveUnlockedCard(AvailableCardDefinition.Get()):nullptr;
}
bool AEGCardManager::CanWithdraw(AEGDeployedPiece* Piece) const
{
 if(!HasAuthority()||bCommitting||!CanEditPlacement()||!OwnsPiece(Piece)||!Roster.IsValid()||!IsValid(Board)||Board->GetOccupant(Piece->Cell)!=Piece)return false;
 FEGHeroInstance H;
 return !RoundShopDefinition||(Roster->GetHero(Piece->RosterId,H)&&(RoundShopDefinition->bSevenStageRules||IsElysia(H.Definition)||IsHerrscherOfHuman(H.Definition)));
}
bool AEGCardManager::TryWithdraw(AEGDeployedPiece* Piece)
{
 if(!CanWithdraw(Piece)){Feedback(FText::FromString(TEXT("当前无法返回手牌。")));return false;}
 TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
 if(!Board->ReleaseFootprint(Piece->Cell,Piece))return false;
 if(!Roster->Withdraw(Piece->RosterId)){const bool bRestored=Board->TryOccupyFootprint(Piece->Cell,PlacementSize(Piece->CardDefinition),Piece);ensureMsgf(bRestored,TEXT("Footprint withdrawal rollback failed"));return false;}
 DestroyDeploymentGroup(Piece->RosterId);
 NotifyHandChanged();OnBoardChanged.Broadcast();return true;
}
bool AEGCardManager::TryDeployReserve(FGuid Id,FIntPoint Cell)
{
 if(!HasAuthority()||bCommitting)return false;
 const auto Result=EvaluateReservePlacement(Id,Cell);if(!Result.IsAllowed()){Feedback(Result.Message);return false;}
 auto* D=FindHeroCardDefinition(Id);if(!D||!D->PieceClass)return false;
 if(auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>())if(!Mode->EnsureHeroResources(D->HeroDefinition))return false;
 TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());FActorSpawnParameters Params;Params.Owner=this;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Piece=GetWorld()->SpawnActor<AEGDeployedPiece>(D->PieceClass,Board->CellToWorld(Cell),PlayerPieceRotation(Board,D->HeroDefinition.Get()),Params);
 if(!Piece)return false;
 if(!Board->TryOccupyFootprint(Cell,PlacementSize(D),Piece)){Piece->Destroy();return false;}
 Piece->RosterId=Id;Piece->CardDefinition=D;Piece->AssignPlacement(Board,Cell,Id);
 if(!Roster->SetDeployment(Id,Cell)){Board->ReleaseFootprint(Cell,Piece);Piece->Destroy();return false;}
 Pieces.Add(Id,Piece);Piece->OnDestroyed.AddDynamic(this,&ThisClass::PieceDestroyed);
 if(D->HeroDefinition&&D->HeroDefinition->HeroKind==EEGHeroKind::VillV&&!EnsureVillVLayout(Piece))
 {DestroyDeploymentGroup(Id);Roster->Withdraw(Id);return false;}
 RefreshHeroPresentations();PlayDeploymentEntrance(Piece);NotifyHandChanged();OnBoardChanged.Broadcast();return true;
}
bool AEGCardManager::TryMergeIntoHero(FGuid CardId,FGuid HeroId)
{
 int32 Index=FindCard(CardId);FEGHeroInstance H;
 if(!HasAuthority()||bCommitting||!CanEditPlacement()||Index==INDEX_NONE||!Rules.bAllowSameNameMerge||!Roster.IsValid()||!Roster->GetHero(HeroId,H)||!Hand[Index].Definition||!Hand[Index].Definition->HeroDefinition||Roster->FindHeroByDefinition(Hand[Index].Definition->HeroDefinition)!=HeroId||
 (RoundShopDefinition&&RoundShopDefinition->bUseExplicitStartingCards&&IsHerrscherOfHuman(H.Definition))||
 (Rules.SameNameLimit>0&&H.SameNameCards>=Rules.SameNameLimit))return false;
 TGuardValue<bool> Guard(bCommitting,true);FEGScopedRosterMutation Mutation(Roster.Get());
 auto* Hero=Roster->FindMutable(HeroId);const int64 Paid=Hand[Index].PaidCost;
 if(!Hero||Paid<0||Hero->PaidCost>MAX_int64-Paid||!Roster->ApplySameNameCard(HeroId))return false;
 Hero->PaidCost+=Paid;
 Hand.RemoveAt(Index);NotifyHandChanged();OnBoardChanged.Broadcast();return true;
}
bool AEGCardManager::SelectShopCard(int32 Index)
{if(!HasAuthority()||bCommitting||!CardPool.IsValidIndex(Index)||!CanEditPlacement())return false;AvailableCardDefinition=CardPool[Index];OnEconomyChanged.Broadcast();return true;}
FEGPlacementEvaluation AEGCardManager::EvaluateReservePlacement(FGuid Id,FIntPoint Cell) const
{
 if(!CanEditPlacement())return Evaluation(EEGPlacementAction::Invalid,TEXT("当前阶段不能布阵。"));
 if(!HasReserveHero(Id))return Evaluation(EEGPlacementAction::Invalid,TEXT("备战英雄已失效。"));
 if(!IsValid(Board)||!Board->IsPlayerCell(Cell))return Evaluation(EEGPlacementAction::Invalid,TEXT("请放在我方部署区。"));
 auto* D=FindHeroCardDefinition(Id);
 if(!D||!D->PieceClass||D->PieceClass->HasAnyClassFlags(CLASS_Abstract))return Evaluation(EEGPlacementAction::Invalid,TEXT("棋子配置无效。"));
 if(!Board->CanOccupyFootprint(Cell,PlacementSize(D)))return Evaluation(EEGPlacementAction::Invalid,TEXT("部署范围必须完全位于我方区域且没有其他棋子。"));
 if(Rules.DeploymentLimit>0&&GetPieces().Num()>=Rules.DeploymentLimit)return Evaluation(EEGPlacementAction::Invalid,TEXT("部署数量已达上限。"));
 return Evaluation(EEGPlacementAction::Deploy,TEXT("松开部署备战英雄，保留成长与形态。"));
}
void AEGCardManager::RefreshHeroPresentations()
{
 if(!Roster.IsValid())return;
 for(auto* Piece:GetPieces())if(auto* D=Roster->ResolveDefinition(Piece->RosterId))
 {if(Piece->HeroVisualDefinition!=D)Piece->ApplyHeroDefinition(D);if(IsValid(Board))Piece->SetActorRotation(PlayerPieceRotation(Board,D));Piece->RefreshPreparationVitals();if(D->HeroKind==EEGHeroKind::VillV)EnsureVillVLayout(Piece);}
}

TArray<AEGDeployedPiece*> AEGCardManager::GetDeploymentParts() const
{
 auto Result=GetPieces();for(const auto& Entry:DevicePieces)if(Entry.Value.IsValid())Result.Add(Entry.Value.Get());return Result;
}
bool AEGCardManager::FindVillVCells(FIntPoint Person,FIntPoint& Gatling,FIntPoint& Cannon) const
{ return FEGVillVDeploymentPolicy::FindCells(*this,Person,Gatling,Cannon); }
bool AEGCardManager::EnsureVillVLayout(AEGDeployedPiece* Main)
{ return FEGVillVDeploymentPolicy::EnsureLayout(*this,Main); }
bool AEGCardManager::SpawnVillVParts(AEGDeployedPiece* Main)
{ return FEGVillVDeploymentPolicy::SpawnParts(*this,Main); }
void AEGCardManager::DestroyDeploymentGroup(FGuid HeroId)
{ FEGVillVDeploymentPolicy::DestroyGroup(*this,HeroId); }
void AEGCardManager::SetDevicePreparationVisible(FGuid HeroId,bool bVisible)
{ FEGVillVDeploymentPolicy::SetVisible(*this,HeroId,bVisible); }
bool AEGCardManager::FuseDeployedHeroes(UEGCardDefinition* HumanCard,bool bNotify,FString* OutError)
{
    auto Reject=[OutError](const TCHAR* Reason){if(OutError)*OutError=Reason;return false;};
    const auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    if(!HasAuthority()||bCommitting||bEnding||(bNotify&&IsStigmataTransactionActive())||!Roster.IsValid()||!IsValid(Board)||!GM||!GM->GetMatch()||
       GM->GetMatch()->GetPhase()!=EEGMatchPhase::Reward||!IsValid(HumanCard)||HumanCard->CardId.IsNone()||!HumanCard->HeroDefinition||
       HumanCard->HeroDefinition->HeroKind!=EEGHeroKind::ElysiaHerrscher||!HumanCard->PieceClass||
       HumanCard->PieceClass->HasAnyClassFlags(CLASS_Abstract)||HumanCard->HeroDefinition->GetPlacementFootprint()!=FIntPoint(1,1))
        return Reject(TEXT("人之律者资源或当前领取状态无效。"));

    const auto Parts=GetDeploymentParts();
    const auto MainPieces=GetPieces();
    TSet<FGuid> DeployedIds;
    int64 LevelSum=0,TotalPaid=0;
    for(const FEGHeroInstance& Hero:Roster->GetHeroes())
    {
        if(!Hero.bDeployed)continue;
        if(!Hero.InstanceId.IsValid()||!Hero.Definition||Hero.Level<1||DeployedIds.Contains(Hero.InstanceId)||
           !MainPieces.ContainsByPredicate([&](const AEGDeployedPiece* Piece){return Piece&&Piece->RosterId==Hero.InstanceId;}))
            return Reject(TEXT("上阵英雄与部署棋子不一致，无法汇聚。"));
        LevelSum+=Hero.Level;
        if(LevelSum>MAX_int32/2)return Reject(TEXT("上阵英雄等级总和过高，无法计算汇聚等级。"));
        if(Hero.PaidCost<0||TotalPaid>MAX_int64-Hero.PaidCost)return Reject(TEXT("上阵英雄的投入记录无效。"));
        TotalPaid+=Hero.PaidCost;
        DeployedIds.Add(Hero.InstanceId);
    }
    if(DeployedIds.IsEmpty())return Reject(TEXT("没有上阵英桀可参与汇聚，请选择其他刻印。"));
    if(MainPieces.Num()!=DeployedIds.Num()||Roster->FindHeroByDefinition(HumanCard->HeroDefinition).IsValid()||
       Hand.ContainsByPredicate([&](const FEGCardInstance& Card){return Card.Definition&&SameHero(Card.Definition->HeroDefinition,HumanCard->HeroDefinition);}))
        return Reject(TEXT("部署棋子不一致或已持有人之律者，无法汇聚。"));
    for(const auto* Part:Parts)
        if(!IsValid(Part)||!DeployedIds.Contains(Part->RosterId)||Board->GetOccupant(Part->Cell)!=Part)
            return Reject(TEXT("棋盘占位已变化，无法汇聚。"));

    TGuardValue<bool> Guard(bCommitting,true);
    FEGScopedRosterMutation Mutation(Roster.Get());
    TArray<AEGDeployedPiece*> Released;
    auto Rollback=[&]()
    {
        for(auto* Part:Released)
        {
            const FIntPoint Size=Part->PartRole==EEGDeploymentPart::Hero?PlacementSize(Part->CardDefinition):FIntPoint(1,1);
            const bool bRestored=Board->TryOccupyFootprint(Part->Cell,Size,Part);
            ensureMsgf(bRestored,TEXT("Fusion occupancy rollback failed"));
        }
    };
    for(auto* Part:Parts)
    {
        if(!Board->ReleaseFootprint(Part->Cell,Part)){Rollback();return Reject(TEXT("释放原棋盘占位失败，已恢复阵容。"));}
        Released.Add(Part);
    }
    FEGCardInstance Human;
    Human.InstanceId=FGuid::NewGuid();
    Human.CardId=HumanCard->CardId;
    Human.Definition=HumanCard;
    Human.InitialLevel=static_cast<int32>(LevelSum*2);
    Human.PaidCost=TotalPaid;
    RegisteredCards.AddUnique(HumanCard);
    Hand.Add(Human);
    Roster->Heroes.RemoveAll([&](const FEGHeroInstance& Hero){return DeployedIds.Contains(Hero.InstanceId);});
    for(const FGuid Id:DeployedIds)Roster->NotifyHeroChanged(Id);

    for(auto* Part:Parts)
    {
        Part->OnDestroyed.RemoveDynamic(this,&ThisClass::PieceDestroyed);
        Pieces.Remove(Part->InstanceId);
        DevicePieces.Remove(Part->InstanceId);
        PendingDeviceEntrances.Remove(Part->InstanceId);
        if(bNotify)Part->Destroy();else PendingStigmataDestruction.Add(Part);
    }
    RefreshHeroPresentations();
    if(bNotify){NotifyHandChanged();OnBoardChanged.Broadcast();}
    return true;
}

bool AEGCardManager::AwardStigmataGold(int32 Amount)
{
    if(!HasAuthority()||bCommitting||bEnding||Amount<=0||Gold>MAX_int64-Amount)return false;
    TGuardValue<bool> Guard(bCommitting,true);
    Gold+=Amount;
    return true;
}

void AEGCardManager::NotifyStigmataChanged()
{
    const auto Pending=MoveTemp(PendingStigmataDestruction);
    PendingStigmataDestruction.Reset();
    for(const auto& Part:Pending)if(IsValid(Part.Get()))Part->Destroy();
    NotifyHandChanged();
    OnBoardChanged.Broadcast();
    OnEconomyChanged.Broadcast();
}
