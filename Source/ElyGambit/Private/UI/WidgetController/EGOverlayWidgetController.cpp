#include "UI/WidgetController/EGOverlayWidgetController.h"
#include "UI/EGCardDragOperation.h"
#include "UI/EGCardPresentation.h"
#include "Game/EGCardManager.h"
#include "Placement/EGBoardScene.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Placement/EGPlacementVisuals.h"
#include "Engine/World.h"

void UEGOverlayWidgetController::BindCallbacksToDependencies()
{
    // Repeated binding and switching sources must never multiply subscriptions.
    UnbindCallbacksFromDependencies();
    Super::BindCallbacksToDependencies();
    if(PlayerController.IsValid()&&PlayerController->GetWorld())RunSource=PlayerController->GetWorld()->GetAuthGameMode<AEGDemoGameMode>();
    if(RunSource.IsValid()){RunSource->OnRunViewChanged.AddUniqueDynamic(this,&ThisClass::RunChanged);RunSource->OnBattleClockChanged.AddUniqueDynamic(this,&ThisClass::ClockChanged);if(auto* Roster=RunSource->GetRoster())Roster->OnHeroChanged.AddUniqueDynamic(this,&ThisClass::RosterChanged);}
    if (HandSource.IsValid())
    {
        HandSource->OnHandChanged.AddUniqueDynamic(this, &ThisClass::HandChanged);
        HandSource->OnEconomyChanged.AddUniqueDynamic(this, &ThisClass::EconomyChanged);
        HandSource->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandSourceDestroyed);
    }
    if (MatchSource.IsValid())
    {
        MatchSource->OnPhaseChanged.AddUniqueDynamic(this, &ThisClass::PhaseChanged);
        MatchSource->OnStagePrepared.AddUniqueDynamic(this, &ThisClass::StagePrepared);
        MatchSource->OnCorruptionChanged.AddUniqueDynamic(this, &ThisClass::CorruptionChanged);
    }
}
void UEGOverlayWidgetController::UnbindCallbacksFromDependencies()
{
    if (HandSource.IsValid())
    {
        HandSource->OnHandChanged.RemoveDynamic(this, &ThisClass::HandChanged);
        HandSource->OnEconomyChanged.RemoveDynamic(this, &ThisClass::EconomyChanged);
        HandSource->OnDestroyed.RemoveDynamic(this, &ThisClass::HandSourceDestroyed);
    }
    if (MatchSource.IsValid())
    {
        MatchSource->OnPhaseChanged.RemoveDynamic(this, &ThisClass::PhaseChanged);
        MatchSource->OnStagePrepared.RemoveDynamic(this, &ThisClass::StagePrepared);
        MatchSource->OnCorruptionChanged.RemoveDynamic(this, &ThisClass::CorruptionChanged);
    }
    if(RunSource.IsValid()){RunSource->OnRunViewChanged.RemoveDynamic(this,&ThisClass::RunChanged);RunSource->OnBattleClockChanged.RemoveDynamic(this,&ThisClass::ClockChanged);if(auto* Roster=RunSource->GetRoster())Roster->OnHeroChanged.RemoveDynamic(this,&ThisClass::RosterChanged);}
    RunSource.Reset();
    LastHandView.Reset();LastPlacementHint=FText::GetEmpty();bHasHandView=false;
    Super::UnbindCallbacksFromDependencies();
}
TArray<FEGHandCardView> UEGOverlayWidgetController::GetHandView() const
{
    TArray<FEGHandCardView> View;
    if (HandSource.IsValid())
        for (const auto& Card : HandSource->GetHand())
        {
            FEGHandCardView& Item = View.AddDefaulted_GetRef();
            Item.InstanceId = Card.InstanceId; Item.CardId = Card.CardId;Item.Level=FMath::Max(1,Card.InitialLevel);
            if(Card.Definition)
            {Item.Identity=Card.Definition->DisplayName;Item.DisplayName=Card.Definition->DisplayName;Item.Artwork=Card.Definition->Artwork;
             if(auto* D=Card.Definition->HeroDefinition.Get()){Item.HeroDefinition=D;Item.DisplayName=D->GetSkillDetails(Item.Level,false);if(auto* Art=GetHeroArtwork(D))Item.Artwork=Art;}}
        }
    if(RunSource.IsValid()&&HandSource.IsValid())
    {
     for(const auto& H:HandSource->GetReserveHeroes())
     {
      auto& Item=View.AddDefaulted_GetRef();Item.InstanceId=H.InstanceId;Item.bReserve=true;Item.Level=H.Level;
      auto* D=RunSource->GetRoster()->ResolveDefinition(H.InstanceId);auto* Card=HandSource->FindHeroCardDefinition(H.InstanceId);
      Item.Identity=FText::FromString(FString::Printf(TEXT("备战 Lv.%d %s%s"),H.Level,H.bAwakened?TEXT("觉醒 "):TEXT(""),H.Form==EEGHeroForm::Enhanced?TEXT("升格"):TEXT("")));
      if(Card)Item.Artwork=Card->Artwork;
      if(auto* Art=GetHeroArtwork(D))Item.Artwork=Art;
      Item.Identity=FText::FromString(FString::Printf(TEXT("%d%s%s"),H.Level,H.bAwakened?TEXT(" 觉醒"):TEXT(""),H.Form==EEGHeroForm::Enhanced?TEXT(" 升格"):TEXT("")));
      Item.HeroDefinition=D;Item.DisplayName=D?D->GetSkillDetails(H.Level,H.bAwakened):Item.Identity;
     }
    }
    if(HandSource.IsValid())
    {
        const auto Ordered=HandSource->GetOrderedHandIds();
        const auto Overflow=HandSource->GetOverflowHandIds();
        for(auto& Item:View)
        {
            Item.bOverflow=Overflow.Contains(Item.InstanceId);
            Item.bOverflowProtected=HandSource->IsHandOverflowProtected(Item.InstanceId);
            if(!Item.bReserve)Item.Identity=FText::FromString(Item.Identity.ToString()+TEXT("\n未使用卡牌"));
        }
        View.StableSort([&Ordered](const FEGHandCardView& A,const FEGHandCardView& B)
        {
            const int32 IA=Ordered.IndexOfByKey(A.InstanceId),IB=Ordered.IndexOfByKey(B.InstanceId);
            return (IA==INDEX_NONE?MAX_int32:IA)<(IB==INDEX_NONE?MAX_int32:IB);
        });
    }
    return View;
}
FEGMatchView UEGOverlayWidgetController::GetMatchView() const
{
    FEGMatchView View;
    if(HandSource.IsValid())
    {View.RoundNumber=HandSource->GetRoundNumber();View.Gold=HandSource->GetGold();
        const auto& Rules=HandSource->GetRules();View.PurchasePrice=Rules.PurchasePrice;
        View.bCanPurchase=HandSource->CanEditPlacement()&&View.Gold>=Rules.PurchasePrice&&
            ((RunSource.IsValid()&&RunSource->RunDefinition&&RunSource->RunDefinition->bRoundShopRules)||
             Rules.HandLimit<=0||HandSource->GetHand().Num()<Rules.HandLimit);}
    if (MatchSource.IsValid())
    {
        View.bHasMatch = MatchSource->HasStarted();
        View.StageNumber = MatchSource->GetStageNumber();
        View.Phase = MatchSource->GetPhase();
        const auto* PC=PlayerController.Get();
        const auto* Mode=PC&&PC->GetWorld()?PC->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
        View.bCanRequestCombat=bDependenciesBound&&Mode&&Mode->GetMatch()==MatchSource.Get()&&
            View.bHasMatch&&View.Phase==EEGMatchPhase::Preparation;
    }
    if(RunSource.IsValid()&&RunSource->RunDefinition)
    {
        const auto* Run=RunSource->RunDefinition.Get();View.bIsRun=true;View.StageCount=Run->Stages.Num();View.RunName=Run->DisplayName;
        if(const auto* Card=GetAvailableCardDefinition();Card&&Card->HeroDefinition)View.SameNameLevelGain=Card->HeroDefinition->GetSameNameLevelGain();
        View.bCanRequestCombat=View.bCanRequestCombat&&RunSource->CanPrepare()&&HandSource.IsValid()&&
            (Run->bSevenStageRules||!HandSource->GetPieces().IsEmpty());
        View.bCanAdvance=RunSource->AreResourcesReady()&&View.Phase!=EEGMatchPhase::Combat&&
            View.Phase!=EEGMatchPhase::Settlement&&View.Phase!=EEGMatchPhase::Reward;
        View.bCanRetryStage=RunSource->CanRetryCurrentStage();
        int32 Deployed=0,Reserve=0,Awakened=0;
        if(const auto* Roster=RunSource->GetRoster())for(const auto& Hero:Roster->GetHeroes())
        {if(Hero.bDeployed)++Deployed;else ++Reserve;if(Hero.bAwakened)++Awakened;}
        const FString HeroText=FString::Printf(TEXT("出战 %d  备战 %d  觉醒 %d"),Deployed,Reserve,Awakened);
        View.HeroSummary=FText::FromString(HeroText);
        const TCHAR* PhaseText=View.Phase==EEGMatchPhase::Preparation?TEXT("准备布阵"):View.Phase==EEGMatchPhase::Combat?TEXT("自动作战"):RunSource->WasVictory()?TEXT("胜利"):TEXT("失败");
        View.RunStatus=FText::FromString(FString::Printf(TEXT("第 %d / %d 关  %s"),View.StageNumber,Run->Stages.Num(),PhaseText));
        const int32 StageIndex=View.StageNumber-1;
        const float Limit=Run->bSevenStageRules&&Run->Stages.IsValidIndex(StageIndex)?Run->Stages[StageIndex].StageBattleTimeLimit:Run->BattleTimeLimit;
        View.RunHint=FText::FromString(Run->bSevenStageRules&&View.Phase==EEGMatchPhase::Combat?FString():View.Phase==EEGMatchPhase::Combat?
            (Limit>0.f?FString::Printf(TEXT("%.0f / %.0f 秒  满蓝自动释放技能"),RunSource->GetBattleSeconds(),Limit):
             FString::Printf(TEXT("%.0f 秒  满蓝自动释放技能"),RunSource->GetBattleSeconds())):RunSource->GetHint());
        View.RunAction=FText::FromString(View.Phase==EEGMatchPhase::Preparation?TEXT("开始作战"):View.Phase==EEGMatchPhase::Combat?TEXT("作战中…"):View.Phase==EEGMatchPhase::Resolution?TEXT("继续  下一关"):TEXT("重新开始"));
        View.bRoundShopRun=Run->bRoundShopRules;
        View.bLastVictory=RunSource->WasVictory();
        if(View.bRoundShopRun)
        {
            if(View.Phase==EEGMatchPhase::Finished)View.RunAction=FText::FromString(TEXT("重新开始"));
        }
        for(int32 I=0;I<Run->Stages.Num();++I)View.StageTypes.Add(MatchSource.IsValid()?MatchSource->GetStageTypeAt(I+1):Run->Stages[I].StageType);
    }
    return View;
}
UTexture2D* UEGOverlayWidgetController::GetHeroArtwork(const UEGHeroDefinition* Definition) const
{
 if(!Definition)return nullptr;
 if(auto* Official=FEGCardPresentation::ArtworkForHero(Definition))return Official;
 if(RunSource.IsValid()&&RunSource->RunDefinition&&RunSource->RunDefinition->StartingCard&&RunSource->RunDefinition->StartingCard->HeroDefinition==Definition)return RunSource->RunDefinition->StartingCard->Artwork;
 if(Definition->GetFormArtwork().IsValid())return Definition->GetFormArtwork().Get();
 if(HandSource.IsValid())for(const auto& Card:HandSource->CardPool)
  if(Card&&Card->HeroDefinition&&(Card->HeroDefinition==Definition||Card->HeroDefinition->EnhancedDefinition==Definition))return Card->Artwork;
 return nullptr;
}
FEGUnitInspectionView UEGOverlayWidgetController::GetInspectionView(const AEGChessUnit* Unit,const UEGHeroRoster* Roster,FGuid HeroId) const
{return FEGUnitInspectionView::Capture(Unit,Roster,HeroId);}
AEGChessUnit* UEGOverlayWidgetController::FindInspectedCombatUnit(FGuid HeroId) const
{
 const auto* PC=PlayerController.Get();
 const auto* Mode=PC&&PC->GetWorld()?PC->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 if(!Mode||!Mode->GetMatch()||Mode->GetMatch()->GetPhase()!=EEGMatchPhase::Combat)return nullptr;
 for(const auto& Ref:Mode->GetBattleUnits())if(Ref.IsValid()&&!Ref->IsActorBeingDestroyed()&&Ref->GetInstanceId()==HeroId)return Ref.Get();
 return nullptr;
}
bool UEGOverlayWidgetController::CanSellInspectedHero(FGuid HeroId) const
{return false;}
bool UEGOverlayWidgetController::CanWithdrawInspectedHero(FGuid HeroId) const
{
 if(!HeroId.IsValid()||!HandSource.IsValid())return false;
 for(auto* Piece:HandSource->GetPieces())if(Piece&&Piece->RosterId==HeroId)return HandSource->CanWithdraw(Piece);
 return false;
}
bool UEGOverlayWidgetController::RequestWithdrawInspectedHero(FGuid HeroId)
{
 if(!bDependenciesBound||!CanWithdrawInspectedHero(HeroId))return false;
 for(auto* Piece:HandSource->GetPieces())if(Piece&&Piece->RosterId==HeroId)return HandSource->TryWithdraw(Piece);
 return false;
}
bool UEGOverlayWidgetController::RequestSellInspectedHero(FGuid HeroId)
{
 if(!bDependenciesBound||!CanSellInspectedHero(HeroId))return false;
 for(auto* Piece:HandSource->GetPieces())if(Piece&&Piece->RosterId==HeroId)return HandSource->TrySellPlacedPiece(Piece);
 return false;
}
TArray<FEGStagePreviewView> UEGOverlayWidgetController::GetStagePreviews() const
{
 TArray<FEGStagePreviewView> Views;
 if(!bDependenciesBound||!RunSource.IsValid()||!MatchSource.IsValid()||!RunSource->RunDefinition)return Views;
 const auto* Run=RunSource->RunDefinition.Get();const int32 Current=MatchSource->GetStageNumber();
 for(int32 I=0;I<FMath::Min(Run->Stages.Num(),MatchSource->GetStageCount());++I)
 {
  const auto& Stage=Run->Stages[I];auto& View=Views.AddDefaulted_GetRef();View.StageNumber=I+1;View.StageId=Run->GetStageId(I);
  View.Title=Stage.DisplayName.IsEmpty()?FText::FromString(FString::Printf(TEXT("第 %d 关"),I+1)):Stage.DisplayName;
  View.StageType=MatchSource->GetStageTypeAt(I+1);View.bPast=I+1<Current;View.bCurrent=I+1==Current;
  View.Status=FText::FromString(View.bPast?TEXT("已结束"):View.bCurrent?TEXT("当前关卡"):TEXT("待进行"));
  const bool bCopy=Run->bSevenStageRules&&(I==2||I==5)&&Stage.Enemies.ContainsByPredicate([](const FEGEnemySpawn& Spawn){return Spawn.bCorruptedCopy;});
  View.bCorruptedCopyEncounter=bCopy;
  View.bCorruptionBossEncounter=Run->bSevenStageRules&&I==6;
  const TCHAR* Type=bCopy?TEXT("侵蚀复制"):Run->bSevenStageRules&&I==6?TEXT("首领关卡"):View.StageType==EEGStageType::Boss?TEXT("首领关卡"):View.StageType==EEGStageType::Corruption?TEXT("侵蚀关卡"):TEXT("常规关卡");
  View.TypeLabel=FText::FromString(Type);
  const int32 BaseLimit=Run->bUseFixedPopulation?FMath::Max(1,Run->FixedPopulation):AEGCardManager::PopulationForStage(I+1);
  View.PopulationLimit=View.bCurrent&&HandSource.IsValid()?HandSource->GetRules().DeploymentLimit:BaseLimit;
  View.bHasCompletionGold=!Run->bSevenStageRules||I<6;
  View.CompletionGold=Run->bSevenStageRules?Stage.CompletionGold:Stage.VictoryGold;
  View.bStigmataChoice=Run->bSevenStageRules&&(I==1||I==3||I==5);
  View.bHasCorruptionGain=Run->bSevenStageRules&&I<6;
  View.bPeriodicCorruption=false;
  View.CorruptionGain=View.bHasCorruptionGain?(I<2?5:I<4?10:15):0;
  FString Details=FString::Printf(TEXT("第 %d 关  %s  %s\n%s %d"),I+1,Type,*View.Status.ToString(),View.bCurrent?TEXT("上阵上限"):TEXT("基础上限"),View.PopulationLimit);
  if(View.bHasCompletionGold)Details+=FString::Printf(TEXT("    结算收入 +%d"),View.CompletionGold);
  else Details+=TEXT("    无结算收入");
  if(View.bStigmataChoice)Details+=I==5?TEXT("    终局刻印三选一"):TEXT("    刻印三选一");
  Details+=FString::Printf(TEXT("\n关卡效果：%s"),Stage.EffectPreview.IsEmpty()?TEXT("暂无说明"):*Stage.EffectPreview.ToString());
  if(bCopy)Details+=TEXT("\n侵蚀之律者以我方上阵英桀为蓝本制造侵蚀复制体；我方英桀仍然保留。敌人将在开战时揭示。");
  if(View.bCorruptionBossEncounter)Details+=TEXT("\n首领拥有两条独立生命。击空第一条生命后，失色壁垒持续 8 秒；有效战斗时间达到 60 秒后进入狂暴，每 5 秒伤害提高 10%。");
  Details+=TEXT("\n敌方阵容\n");
  for(const auto& Enemy:Stage.Enemies)if(Enemy.bCorruptedCopy)Details+=TEXT("侵蚀伪装单位  开战揭示\n");else if(Enemy.Definition)Details+=FString::Printf(TEXT("%s  等级 %d\n"),*Enemy.Definition->DisplayName.ToString(),Enemy.Level);
  if(Stage.Enemies.IsEmpty())Details+=TEXT("暂无敌方信息\n");
  View.Details=FText::FromString(Details);
 }
 return Views;
}
void UEGOverlayWidgetController::BroadcastInitialValues()
{
    PublishHand(true);
    OnMatchViewChanged.Broadcast(GetMatchView());
    PresentPlacementFeedback(static_cast<uint8>(EEGPlacementDragState::Idle));
}
void UEGOverlayWidgetController::PublishHand(bool bForce)
{
 auto View=GetHandView();bool bSame=bHasHandView&&View.Num()==LastHandView.Num();
 if(bSame)for(int32 I=0;I<View.Num();++I)
 {
  const auto& A=View[I];const auto& B=LastHandView[I];
   if(A.InstanceId!=B.InstanceId||A.CardId!=B.CardId||A.bReserve!=B.bReserve||A.bOverflow!=B.bOverflow||A.bOverflowProtected!=B.bOverflowProtected||A.Level!=B.Level||A.HeroDefinition!=B.HeroDefinition||A.Artwork!=B.Artwork||!A.Identity.EqualTo(B.Identity)||!A.DisplayName.EqualTo(B.DisplayName)){bSame=false;break;}
 }
 if(!bForce&&bSame)return;
 LastHandView=MoveTemp(View);bHasHandView=true;++HandBroadcastCount;OnHandViewChanged.Broadcast(LastHandView);
}
void UEGOverlayWidgetController::HandChanged() { PublishHand();OnMatchViewChanged.Broadcast(GetMatchView()); }
void UEGOverlayWidgetController::EconomyChanged(){OnMatchViewChanged.Broadcast(GetMatchView());}
void UEGOverlayWidgetController::ClockChanged()
{
 if(!RunSource.IsValid()||!RunSource->RunDefinition)return;
 ++ClockBroadcastCount;
 const auto* Run=RunSource->RunDefinition.Get();
 if(Run->bSevenStageRules)return;
 const int32 StageIndex=MatchSource.IsValid()?MatchSource->GetStageNumber()-1:INDEX_NONE;
 const float Limit=Run->bSevenStageRules&&Run->Stages.IsValidIndex(StageIndex)?Run->Stages[StageIndex].StageBattleTimeLimit:Run->BattleTimeLimit;
 const FString Hint=Limit>0.f?FString::Printf(TEXT("%.0f / %.0f 秒  满蓝自动释放技能"),RunSource->GetBattleSeconds(),Limit):
  FString::Printf(TEXT("%.0f 秒  满蓝自动释放技能"),RunSource->GetBattleSeconds());
 OnBattleClockViewChanged.Broadcast(FText::FromString(Hint));
}
void UEGOverlayWidgetController::HandSourceDestroyed(AActor* Actor)
{
    if (HandSource.Get(true) != Actor) return;
    CancelActiveDrag();
    HandSource.Reset();
    HandChanged();
}
void UEGOverlayWidgetController::PhaseChanged(EEGMatchPhase Phase)
{
 OnMatchViewChanged.Broadcast(GetMatchView());
 PresentRuleFeedback(FText::FromString(Phase==EEGMatchPhase::Combat?TEXT("自动作战中  满蓝自动释放技能"):
  Phase==EEGMatchPhase::Preparation?TEXT("拖动手牌至我方部署区  松手提交  Esc 取消"):
  Phase==EEGMatchPhase::Settlement?TEXT("本场结算中"):
  Phase==EEGMatchPhase::Resolution?TEXT("本场作战已结算  正在进入下一关"):TEXT("本局已结束")));
}
void UEGOverlayWidgetController::StagePrepared(int32 StageNumber, EEGStageType StageType) { OnMatchViewChanged.Broadcast(GetMatchView()); }
void UEGOverlayWidgetController::CorruptionChanged(int32 Current, int32 Delta)
{
    OnMatchViewChanged.Broadcast(GetMatchView());
    OnCorruptionViewChanged.Broadcast(Current, Delta);
}
bool UEGOverlayWidgetController::RequestObtainElysia()
{
    return RequestObtainCard(GetAvailableCardDefinition());
}
UEGCardDefinition* UEGOverlayWidgetController::GetAvailableCardDefinition() const
{return HandSource.IsValid()?HandSource->AvailableCardDefinition.Get():nullptr;}
bool UEGOverlayWidgetController::RequestObtainCard(UEGCardDefinition* Definition)
{return bDependenciesBound&&HandSource.IsValid()&&HandSource->RequestObtainCard(Definition);}
bool UEGOverlayWidgetController::RequestCombat()
{
    auto* PC=PlayerController.Get();
    auto* Mode=PC&&PC->GetWorld()?PC->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
    if(!GetMatchView().bCanRequestCombat||!Mode)return false;
    CancelActiveDrag();return Mode->EnterCombat();
}
void UEGOverlayWidgetController::PresentPlacementFeedback(uint8 State)
{
    const TCHAR* Message=TEXT("拖动手牌至近端三行部署  Esc 取消");
    switch(static_cast<EEGPlacementDragState>(State))
    {
    case EEGPlacementDragState::DraggingHandCard2D: Message=TEXT("拖向棋盘预览  松开无效位置将保留手牌");break;
    case EEGPlacementDragState::PreviewValid: Message=TEXT("可以部署  松开鼠标放置棋子");break;
    case EEGPlacementDragState::PreviewInvalid: Message=TEXT("此位置不可部署  松开保留手牌或棋子");break;
    case EEGPlacementDragState::HoveringSellZone: Message=TEXT("松开鼠标出售");break;
    case EEGPlacementDragState::HoveringShopReturn: Message=TEXT("松开返回商店");break;
    case EEGPlacementDragState::DraggingPlacedPiece: Message=TEXT("拖回卡牌区撤回备战  Esc 取消");break;
    default:break;
    }
    PresentRuleFeedback(FText::FromString(Message));
}
void UEGOverlayWidgetController::CancelActiveDrag()
{
    if (auto* PC = Cast<AEGBoardPlayerController>(PlayerController.Get())) PC->CancelActiveDrag();
}
bool UEGOverlayWidgetController::BeginHandCardDrag(FGuid Id,UEGCardDragOperation* ExpectedOperation)
{
    auto* PC = Cast<AEGBoardPlayerController>(PlayerController.Get());
    // Never start input against a hand source different from the actual scene.
    return bDependenciesBound && PC && PC->GetSceneSettings() &&
        PC->GetSceneSettings()->Manager == HandSource.Get() && PC->BeginHandCardDragForOperation(Id,ExpectedOperation);
}
int32 UEGOverlayWidgetController::GetHandOrderRevision() const
{return HandSource.IsValid()?HandSource->GetHandOrderRevision():INDEX_NONE;}
bool UEGOverlayWidgetController::ReorderHandCard(FGuid Id,FGuid BeforeId,int32 Revision)
{return bDependenciesBound&&HandSource.IsValid()&&HandSource->TryReorderHandCard(Id,BeforeId,Revision);}
int64 UEGOverlayWidgetController::GetOverflowSaleValue() const
{return HandSource.IsValid()?HandSource->GetOverflowSaleValue():0;}
bool UEGOverlayWidgetController::AttachHandCardDrag(UEGCardDragOperation* Operation, FVector2D ScreenPosition)
{
    auto* PC = Cast<AEGBoardPlayerController>(PlayerController.Get());
    if (!Operation || !PC || !bDependenciesBound) return false;
    Operation->Controller = PC;
    PC->AttachCardOperation(Operation);
    PC->UpdateDragAtScreenPosition(ScreenPosition);
    return !Operation->HasFinished()&&PC->GetDragState()!=EEGPlacementDragState::Idle;
}
void UEGOverlayWidgetController::Shutdown()
{
    Super::Shutdown();
    OnHandViewChanged.Clear(); OnMatchViewChanged.Clear();OnPlacementHintChanged.Clear();OnBattleClockViewChanged.Clear();OnCorruptionViewChanged.Clear();
}

void UEGOverlayWidgetController::RunChanged(){OnMatchViewChanged.Broadcast(GetMatchView());}
bool UEGOverlayWidgetController::RequestNextShopCard()
{
 if(!bDependenciesBound||!HandSource.IsValid()||HandSource->CardPool.IsEmpty())return false;
 return HandSource->SelectShopCard((HandSource->CardPool.IndexOfByKey(HandSource->AvailableCardDefinition)+1)%HandSource->CardPool.Num());
}
bool UEGOverlayWidgetController::RequestAdvanceRun()
{
 if(!bDependenciesBound||!RunSource.IsValid())return false;
 CancelActiveDrag();return RunSource->AdvanceDemo();
}
bool UEGOverlayWidgetController::RequestRetryStage()
{
 if(!bDependenciesBound||!RunSource.IsValid()||!RunSource->CanRetryCurrentStage())return false;
 CancelActiveDrag();return RunSource->RetryCurrentStage();
}

void UEGOverlayWidgetController::PresentRuleFeedback(const FText& Message)
{if(!LastPlacementHint.EqualTo(Message)){LastPlacementHint=Message;OnPlacementHintChanged.Broadcast(Message);}}

void UEGOverlayWidgetController::RosterChanged(FGuid Id){HandChanged();}
