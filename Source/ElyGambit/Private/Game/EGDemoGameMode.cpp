#include "Game/EGDemoGameMode.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroGrowthSettings.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGStigmataBalance.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGDeploymentComponent.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGOpeningWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/HUD/EGHUD.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Effects/EGDamageEffect.h"
#include "EGGameplayTags.h"
#include "EngineUtils.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Components/EGRunPerformanceProbe.h"
#include "Components/EGRunResourceComponent.h"
#include "HAL/IConsoleManager.h"
#include "Actor/EGCorruptionMine.h"
#include "Data/EGEnvironmentTrapDefinition.h"
#include "Presentation/EGSettlementPresentation.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGOpeningPresentation.h"
#include "Presentation/EGKillBeamVisual.h"
#include "Presentation/EGTeleportVisual.h"
#include "Data/EGSevenStageDefinition.h"
#include "Data/EGStigmataDefinition.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

AEGDemoGameMode::AEGDemoGameMode()
{
    bAutoStart=false;PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.TickInterval=.1f;
    UnitClass=AEGDemoCombatUnit::StaticClass();PlayerControllerClass=AEGBoardPlayerController::StaticClass();
    HUDClass=AEGHUD::StaticClass();Deployment=CreateDefaultSubobject<UEGDeploymentComponent>(TEXT("Deployment"));
    Resources=CreateDefaultSubobject<UEGRunResourceComponent>(TEXT("RunResources"));
    HumanFusionCard=TSoftObjectPtr<UEGCardDefinition>(FSoftObjectPath(TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human")));
}
void AEGDemoGameMode::StartPlay()
{
    // Decide the entry policy before any configuration check can return.
    bPresentationFlowActive=HasAuthority()&&GetNetMode()!=NM_DedicatedServer&&
        UGameplayStatics::ParseOption(OptionsString,TEXT("EGOpening"))==TEXT("1");
    int32 Count=0;
    for(TActorIterator<AEGBoardSceneSettings> It(GetWorld());It;++It){Scene=*It;++Count;}
    if(Scene&&Scene->CombatBoard)Board=Scene->CombatBoard;
    Super::StartPlay();
    if(BattleRules)BattleRules->OnCombatRunning.AddUniqueDynamic(this,&ThisClass::EncounterCombatRunning);
    if(!IsRunningCommandlet()&&GetWorld()->GetGameViewport())
    {
     if(auto* Cap=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))
     {
      PreviousFrameCap=Cap->GetFloat();bAppliedFrameCap=true;
      const bool bUncapped=FParse::Param(FCommandLine::Get(),TEXT("EGUncapped"))||FParse::Param(FCommandLine::Get(),TEXT("EGProfile"));
      Cap->SetWithCurrentPriority(bUncapped?0.f:60.f);
     }
    }
    Cards=Scene?Scene->Manager.Get():nullptr;
    if(RunDefinition&&RunDefinition->GetPathName()==TEXT("/Game/Blueprints/Game/Board7x7/MonsterSkills20261026/DA_TwoStageRun_Monsters.DA_TwoStageRun_Monsters")&&
       UGameplayStatics::GetCurrentLevelName(this,true)==TEXT("L_ElysianBoard_7x7"))
        RunDefinition=EGSevenStage::Build(this,RunDefinition);
    FString Error;
    if(Count!=1||!Cards||!Scene->Board||!Board||!GetRoster()||!RunDefinition||!RunDefinition->ValidateConfiguration(Scene->Board,Error))
    {
        if(bPresentationFlowActive)
        {
            bOpeningPending=true;bResourcesReady=false;
            if(Cards)Cards->SetPreparationEnabled(false);
            OpeningStartupError=TEXT("战斗场景配置不完整，请重新进入游戏。");
            bOpeningErrorWidgetPending=true;
            TryInstallOpeningErrorWidget();
        }
        SetHint(TEXT("战斗场景配置不完整，请重新打开正式 7×7 棋盘。"));
        UE_LOG(LogTemp,Error,TEXT("Basic demo configuration: %s"),*Error);return;
    }
    Board->UseLayout(Scene->Board);
    Cards->ConfigureRules(RunDefinition->CardRules,GetRoster());Cards->CardPool=RunDefinition->CardPool;
    Deployment->Configure(this,Cards,Board);
    GetMatch()->ConfigureRunDefinition(RunDefinition);
    GetMatch()->OnBattleResolved.AddDynamic(this,&ThisClass::BattleResolved);
    GetMatch()->OnPhaseChanged.AddDynamic(this,&ThisClass::RunPhaseChanged);
    Cards->OnFeedbackChanged.AddDynamic(this,&ThisClass::CardFeedback);
    TArray<UEGHeroDefinition*> ModelHeroes;
    if(RunDefinition->StartingCard)ModelHeroes.AddUnique(RunDefinition->StartingCard->HeroDefinition);
    if(RunDefinition->bUseExplicitStartingCards)for(const auto& Grant:RunDefinition->ExplicitStartingCards)
     if(Grant.Card&&Grant.Card->HeroDefinition)ModelHeroes.AddUnique(Grant.Card->HeroDefinition);
    for(const auto& Card:RunDefinition->CardPool)if(Card&&Card->HeroDefinition)ModelHeroes.AddUnique(Card->HeroDefinition);
    for(const auto& Stage:RunDefinition->Stages)
    {
        for(const auto& Enemy:Stage.Enemies)if(Enemy.Definition)ModelHeroes.AddUnique(Enemy.Definition);
        if(Stage.bHasHighCorruptionEnemy&&Stage.HighCorruptionEnemy.Definition)ModelHeroes.AddUnique(Stage.HighCorruptionEnemy.Definition);
        // Runtime-generated reinforcement definitions are transient and deliberately excluded from generic reflection.
        for(const auto& Hero:Stage.OrdinaryMonsterDefinitions)if(Hero)ModelHeroes.AddUnique(Hero);
    }
    bConfigured=true;
    // The regular board and automation entry remain immediate. Only the
    // explicitly requested formal entry owns an opening presentation gate.
    if(bPresentationFlowActive)
    {
        if(RunDefinition->StartingCard&&RunDefinition->StartingCard->HeroDefinition)
            OpeningPreloadHeroes.AddUnique(RunDefinition->StartingCard->HeroDefinition);
        if(RunDefinition->bUseExplicitStartingCards)for(const auto& Grant:RunDefinition->ExplicitStartingCards)
            if(Grant.Card&&Grant.Card->HeroDefinition)OpeningPreloadHeroes.AddUnique(Grant.Card->HeroDefinition);
        // The shop and random opening draw can select any card in this pool.
        for(const auto& Card:RunDefinition->CardPool)if(Card&&Card->HeroDefinition)
            OpeningPreloadHeroes.AddUnique(Card->HeroDefinition);
        if(RunDefinition->Stages.IsValidIndex(0))
        {
            const auto& Stage=RunDefinition->Stages[0];
            for(const auto& Enemy:Stage.Enemies)if(Enemy.Definition)OpeningPreloadHeroes.AddUnique(Enemy.Definition);
            if(Stage.bHasHighCorruptionEnemy&&Stage.HighCorruptionEnemy.Definition)
                OpeningPreloadHeroes.AddUnique(Stage.HighCorruptionEnemy.Definition);
        }
        OpeningModelHeroes=OpeningPreloadHeroes;
        for(auto* Hero:ModelHeroes)OpeningPreloadHeroes.AddUnique(Hero);
        bOpeningPending=true;OpeningToken=FGuid::NewGuid();bResourcesReady=false;
        Cards->SetPreparationEnabled(false);
        // The board's standalone card manager seeds a sandbox card at BeginPlay.
        // The formal opening starts with an empty hand until both masters arrive.
        if(!Cards->ResetCards(0,0,RunDefinition->StartingCard))
            OpeningStartupError=TEXT("无法初始化开场手牌。");
        OpeningPresentation=UEGOpeningPresentation::Install(this);
        if(!OpeningPresentation)
        {
            OpeningStartupError=TEXT("无法创建开场演出。");
            UE_LOG(LogTemp,Error,TEXT("Unable to install opening presentation"));
            bOpeningErrorWidgetPending=true;
            TryInstallOpeningErrorWidget();
        }
    }
    else
    {
        Resources->SetRunHeroes(ModelHeroes,RunDefinition);
        bResourcesReady=true;StartGame();
        if(RunDefinition->bSevenStageRules)UEGBoardMasterPresentation::Install(this);
    }
#if !UE_BUILD_SHIPPING
    if(FParse::Param(FCommandLine::Get(),TEXT("EGProfile")))
    {auto* Probe=NewObject<UEGRunPerformanceProbe>(this,TEXT("PerformanceProbe"));AddInstanceComponent(Probe);Probe->RegisterComponent();}
#endif
    OnRunViewChanged.Broadcast();
}
void AEGDemoGameMode::TryInstallOpeningErrorWidget()
{
 if(!bOpeningErrorWidgetPending||!GetWorld()||GetWorld()->bIsTearingDown)return;
 if(auto* PC=GetWorld()->GetFirstPlayerController())
  if(PC->IsLocalController()&&IsValid(UEGOpeningWidget::Install(PC)))bOpeningErrorWidgetPending=false;
}
UEGDemoPanel* AEGDemoGameMode::GetDemoPanel() const
{
 if(auto* PC=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)
  if(auto* Host=Cast<AEGHUD>(PC->GetHUD()))return Host->GetRunPanel();
 return nullptr;
}
bool AEGDemoGameMode::StartGame(bool bResetRoster)
{
    if(!bConfigured||!bResetRoster||bCoordinating||(IsPresentationInputBlocked()&&!bOpeningCommitInProgress)||
        (GetMatch()->HasStarted()&&GetMatch()->GetPhase()!=EEGMatchPhase::Finished))return false;
    RestoreOvertimeDilation();
    ResetPhaseClock();
    ClearEncounterDisguises();
    ClearSettlementPresentation();
    if(EncounterBoss.IsValid())EncounterBoss->OnDied.RemoveDynamic(this,&ThisClass::RecordBossDeath);
    ResetEncounterHistory();
    BalanceSeed=RunDefinition->EncounterSeed>=0?RunDefinition->EncounterSeed:FMath::Rand();
#if !UE_BUILD_SHIPPING
    FParse::Value(FCommandLine::Get(),TEXT("EGBalanceSeed="),BalanceSeed);
#endif
    EncounterRandom.Initialize(BalanceSeed ^ 0x51E1A7);
    StigmataRandom.Initialize(BalanceSeed ^ 0x1C57A6);
    UE_LOG(LogTemp,Log,TEXT("ElyGambit balance run seed: %d"),BalanceSeed);
    StigmataOffer.Reset();
    bChoosingEndgameRoute=false;
    bEndgameCheckpointValid=false;bEndgameCheckpointRetryNeeded=false;bFinalBossCombatEntered=false;
    EndgameCardsCheckpoint=FEGCardRunCheckpoint();EndgameRosterCheckpoint=FEGHeroRosterRunSnapshot();
    EndgameEncounterHistory.Reset();EndgameCombatStatistics.Reset();EndgameContributions.Reset();
    ++StigmaSkipRevision;
    BossCorruptionTicks=0;BossDeathFrame=MAX_uint64;
    SetActorTickInterval(.1f);
    Deployment->RestorePresentations();Cards->SetPreparationEnabled(false);
    if(!Cards->ResetCards(0,0,RunDefinition->StartingCard))return false;
    StageSnapshot.Reset();
    
    CombatStatistics.Reset();OnCombatStatisticsChanged.Broadcast();
    bLastVictory=false;bDuskStreetSettlementEligible=false;BattleSeconds=0;LastClockSecond=INDEX_NONE;
    bStageReady=false;
    if(!Super::StartGame(true))return false;
    if(BattleRules&&BattleRules->GetStigmata())BattleRules->GetStigmata()->ClearContributions();
    Cards->SetPreparationEnabled(CanPrepare());
    if(RunDefinition->bRoundShopRules)Cards->ConfigureRoundShop(RunDefinition);
    else Cards->ResetCards(RunDefinition->StartingGold,RunDefinition->StartingCards,RunDefinition->StartingCard);
    Cards->SetPreparationEnabled(CanPrepare());
    if(bStageReady)SetHint(TEXT("拖动新英雄到我方棋盘部署；同名卡拖入棋盘任意位置合成，发光格指示目标。"));
    return true; // The run has started; bStageReady separately reports preparation failure.
}
bool AEGDemoGameMode::CanPrepare() const
{return bConfigured&&!bCoordinating&&!IsPresentationInputBlocked()&&GetMatch()&&GetMatch()->HasStarted()&&GetMatch()->GetPhase()==EEGMatchPhase::Preparation;}
bool AEGDemoGameMode::IsPresentationInputBlocked() const
{return bOpeningPending||bStageTransitionPending||bStageEntrancePending;}
bool AEGDemoGameMode::IsOpeningPreloadReady() const
{
 return bOpeningPending&&bOpeningLoadRequested&&bOpeningPreloadStarted&&Resources&&OpeningStartupError.IsEmpty()&&
     Resources->HasCompletedStartupPreload()&&Resources->IsBackgroundReady()&&Resources->GetError().IsEmpty();
}
FString AEGDemoGameMode::GetOpeningPreloadError() const
{
 if(!OpeningStartupError.IsEmpty())return OpeningStartupError;
 return Resources?Resources->GetError():TEXT("加载组件不可用。");
}
bool AEGDemoGameMode::RetryOpeningPreload()
{
 if(!HasAuthority()||!bConfigured||!bOpeningPending||!bOpeningLoadRequested||!bOpeningPreloadStarted||
    !OpeningStartupError.IsEmpty()||!IsValid(OpeningPresentation)||!Resources||
    Resources->GetError().IsEmpty()||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 StartOpeningPreload();
 OnRunViewChanged.Broadcast();
 return true;
}
void AEGDemoGameMode::StartOpeningPreload()
{
 if(!bOpeningPending||!bOpeningLoadRequested||!Resources||!GetWorld()||GetWorld()->bIsTearingDown)return;
 TArray<UEGHeroDefinition*> ModelHeroes;
 for(const auto& Hero:OpeningModelHeroes)if(IsValid(Hero))ModelHeroes.Add(Hero);
 bOpeningPreloadStarted=false;
 Resources->SetRunHeroesDeferred(ModelHeroes,RunDefinition);
 for(const auto& Hero:OpeningPreloadHeroes)if(IsValid(Hero))
 {Resources->RequireOpeningHero(Hero);Resources->RequestHeroResources(Hero);}
 Resources->RequestRuntimeAsset(HumanFusionCard.ToSoftObjectPath());
 for(const auto& Path:AEGSettlementPresentation::GetPreloadPaths())Resources->RequestRuntimeAsset(Path);
 Resources->RequestRuntimeAsset(AEGKillBeamVisual::GetPreloadPath());
 Resources->RequestRuntimeAsset(AEGTeleportVisual::GetPreloadPath());
 bOpeningPreloadStarted=true;
}
bool AEGDemoGameMode::RequestOpeningStart()
{
 if(!HasAuthority()||!bConfigured||!bOpeningPending||bOpeningLoadRequested||!OpeningStartupError.IsEmpty()||
    !IsValid(OpeningPresentation)||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 if(!OpeningPresentation->RequestBegin())return false;
 bOpeningLoadRequested=true;
 OnRunViewChanged.Broadcast();
 return true;
}
void AEGDemoGameMode::CompleteOpeningPresentation(FGuid Token)
{
 if(!HasAuthority()||!bConfigured||!bOpeningPending||!Token.IsValid()||Token!=OpeningToken||
    !IsOpeningPreloadReady()||!IsValid(OpeningPresentation)||!OpeningPresentation->HasCompletedDeparture()||
    !GetWorld()||GetWorld()->bIsTearingDown)return;
 // Consume the opening token before any phase or card delegate can re-enter.
 OpeningToken.Invalidate();bOpeningPending=false;bStageEntrancePending=true;
 Resources->FinishOpeningPreload();
 EntranceToken=FGuid::NewGuid();
 bool bStarted=false;
 {
  TGuardValue<bool> Guard(bOpeningCommitInProgress,true);
  bStarted=StartGame();
 }
 if(!bStarted)
 {
  bStageEntrancePending=false;EntranceToken.Invalidate();bOpeningPending=true;
  OpeningStartupError=TEXT("开局初始化失败，请重新进入游戏。");
  OpeningPresentation->FailOpening(OpeningStartupError);
  if(Cards)Cards->SetPreparationEnabled(false);
  OnRunViewChanged.Broadcast();return;
 }
 if(RunDefinition&&RunDefinition->bSevenStageRules)UEGBoardMasterPresentation::Install(this);
 BeginStageEntranceOrWait();
}
void AEGDemoGameMode::CompleteStageTransition(FGuid Token)
{
 if(!HasAuthority()||!bConfigured||!bStageTransitionPending||!Token.IsValid()||Token!=TransitionToken||
    !GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Resolution||
    GetMatch()->GetStageNumber()!=TransitionFromStage||!IsValid(OpeningPresentation)||
    !OpeningPresentation->HasCompletedStageTransition(Token)||
    !GetWorld()||GetWorld()->bIsTearingDown)return;
 TransitionToken.Invalidate();bStageTransitionPending=false;TransitionFromStage=INDEX_NONE;
 bStageEntrancePending=true;EntranceToken=FGuid::NewGuid();
 if(!CommitNextStage())
 {
  bStageEntrancePending=false;EntranceToken.Invalidate();bResourcesReady=true;
  if(IsValid(OpeningPresentation))OpeningPresentation->CancelTransition();
  if(Cards)Cards->SetPreparationEnabled(CanPrepare());
  OnRunViewChanged.Broadcast();return;
 }
 BeginStageEntranceOrWait();
}
void AEGDemoGameMode::BeginStageEntranceOrWait()
{
 if(!bStageEntrancePending||!EntranceToken.IsValid()||!GetWorld()||GetWorld()->bIsTearingDown)return;
 if(!bStageReady||!IsValid(OpeningPresentation))
 {
  StageEntranceError=IsValid(OpeningPresentation)?TEXT("敌人入场准备失败，请重试。"):TEXT("入场演出中断，请重试。");
  if(IsValid(OpeningPresentation))OpeningPresentation->FailStageEntrance(StageEntranceError);
  if(auto* PC=GetWorld()->GetFirstPlayerController())UEGOpeningWidget::Install(PC);
  OnRunViewChanged.Broadcast();return;
 }
 StageEntranceError.Reset();
 if(IsValid(OpeningPresentation))OpeningPresentation->BeginStageEntrance(EntranceToken);
}
bool AEGDemoGameMode::RetryStageEntrance()
{
 if(!HasAuthority()||!bConfigured||!IsStageEntranceRetryNeeded()||bRetryingStageEntrance||bPreparingStage||
    !EntranceToken.IsValid()||!GetMatch()||
    GetMatch()->GetPhase()!=EEGMatchPhase::Preparation||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 TGuardValue<bool> Guard(bRetryingStageEntrance,true);
 if(!IsValid(OpeningPresentation))OpeningPresentation=UEGOpeningPresentation::Install(this);
 if(!IsValid(OpeningPresentation)){BeginStageEntranceOrWait();return false;}
 // When only the presentation was interrupted, reuse the existing batch.
 if(!bStageReady)PrepareStageEnemies();
 const bool bPrepared=bStageReady;
 BeginStageEntranceOrWait();
 return bPrepared;
}
void AEGDemoGameMode::CompleteStageEntrance(FGuid Token)
{
 if(!HasAuthority()||!bConfigured||!bStageEntrancePending||!bStageReady||!StageEntranceError.IsEmpty()||!Token.IsValid()||Token!=EntranceToken||
    !GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Preparation||!IsValid(OpeningPresentation)||
    !OpeningPresentation->HasCompletedStageEntrance(Token)||!GetWorld()||GetWorld()->bIsTearingDown)return;
 EntranceToken.Invalidate();bStageEntrancePending=false;bResourcesReady=true;StageEntranceError.Reset();
 // Do not charge the player for time spent in the cinematic.
 LastAudioClockSeconds=GetWorld()->GetAudioTimeSeconds();PreparationRealSeconds=0.f;
 if(Cards)Cards->SetPreparationEnabled(CanPrepare());
 // NextStage's normal synchronization ran while placement was deliberately
 // disabled; finish it now so deployed presentation and combat units agree.
 if(Deployment&&!Deployment->TrySynchronize())
     SetHint(TEXT("部署同步未完成，请检查棋盘配置。"));
 OnRunViewChanged.Broadcast();OnPhaseClockChanged.Broadcast();
}
void AEGDemoGameMode::RefreshBackgroundResourceRequests()
{
 // Only heroes currently visible/used request their full presentation graphs. No whole-run barrier.
 if(!Cards||!RunDefinition||!GetMatch())return;
 for(auto* Card:Cards->GetShop())if(Card)Resources->RequestHeroResources(Card->HeroDefinition);
 for(const auto& Card:Cards->GetHand())if(Card.Definition)Resources->RequestHeroResources(Card.Definition->HeroDefinition);
 if(GetRoster())for(const auto& Hero:GetRoster()->GetHeroes())if(Hero.bDeployed)Resources->RequestHeroResources(Hero.Definition);
 const int32 Stage=GetMatch()->GetStageNumber()-1;
 if(RunDefinition->Stages.IsValidIndex(Stage))
  for(const auto& Enemy:RunDefinition->Stages[Stage].Enemies)Resources->RequestHeroResources(Enemy.Definition);
}
bool AEGDemoGameMode::EnsureHeroResources(UEGHeroDefinition* Definition)
{
 if(!bConfigured||!IsValid(Definition)||!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Preparation)return false;
 Resources->RequestHeroResources(Definition);
 return true; // Cosmetic resource readiness must never reject a valid purchase/deployment.
}
void AEGDemoGameMode::SetHint(const FString& Message)
{Hint=Message;OnRunViewChanged.Broadcast();}
void AEGDemoGameMode::CardFeedback(){if(Cards)SetHint(Cards->GetLastMessage().ToString());}
void AEGDemoGameMode::RunPhaseChanged(EEGMatchPhase Phase)
{
    if(Phase==EEGMatchPhase::Combat&&RunDefinition&&RunDefinition->bSevenStageRules&&GetMatch()&&GetMatch()->GetStageNumber()==7)
        bFinalBossCombatEntered=true;
    if(Phase!=EEGMatchPhase::Combat)ClearBossWave();
    LastAudioClockSeconds=GetWorld()?GetWorld()->GetAudioTimeSeconds():-1.0;
    if(Phase==EEGMatchPhase::Settlement&&RunDefinition&&RunDefinition->bSevenStageRules)
    {
        if(!bSettlementClockActive&&GetWorld())
        {
            // Audio time is undilated and freezes when the world is paused.
            // Start before BattleResolved spawns the optional presentation.
            bSettlementClockActive=true;
            SettlementStartAudioSeconds=GetWorld()->GetAudioTimeSeconds();
            PreviousSettlementTickInterval=GetActorTickInterval();
            PrimaryActorTick.UpdateTickIntervalAndCoolDown(0.f);
        }
    }
    else ResetSettlementClock();
    if(Phase==EEGMatchPhase::Preparation)
    {
        ClockStageNumber=GetMatch()?GetMatch()->GetStageNumber():INDEX_NONE;
        PreparationRealSeconds=0.f;
        bPreparationAutoStartAttempted=false;
    }
    if(Phase!=EEGMatchPhase::Combat)
    {
        RestoreOvertimeDilation();
        ClearEncounterDisguises();
    }
    if(Phase!=EEGMatchPhase::Preparation&&Phase!=EEGMatchPhase::Combat)ClearStageTraps();
    if(Cards)Cards->SetPreparationEnabled(CanPrepare());
    OnRunViewChanged.Broadcast();
    OnPhaseClockChanged.Broadcast();
}
void AEGDemoGameMode::PrepareStageEnemies()
{
    if(bPreparingStage)return;
    TGuardValue<bool> Guard(bPreparingStage,true);
    ClearStageTraps();
    bStageReady=false;
    SpawnPreparedStageEnemies();
}
void AEGDemoGameMode::SpawnPreparedStageEnemies()
{
    if(!RunDefinition||!GetMatch()||!RunDefinition->Stages.IsValidIndex(GetMatch()->GetStageNumber()-1))return;
    const auto& Stage=RunDefinition->Stages[GetMatch()->GetStageNumber()-1];
    TArray<TWeakObjectPtr<AEGChessUnit>> CreatedEnemies;
    TArray<FEGEnemySpawn> Spawns=Stage.Enemies;
    // Encounter size is fixed across corruption tiers; the legacy add/remove
    // fields remain serialized for existing run assets and validation.
    for(const auto& Spawn:Spawns)
    {
        auto* Enemy=SpawnEnemy(Spawn.Definition,Spawn.Level,Spawn.Cell);
        if(!IsValid(Enemy)||Enemy->IsActorBeingDestroyed())break;
        CreatedEnemies.Add(Enemy);
        Enemy->OnCombatResolved.AddUniqueDynamic(this,&ThisClass::TrackCombatResolution);
    }
    bStageReady=CreatedEnemies.Num()==Spawns.Num()&&!CreatedEnemies.IsEmpty();
    if(bStageReady)for(const auto& Enemy:CreatedEnemies)
     if(!Enemy.IsValid()||Enemy->IsActorBeingDestroyed()){bStageReady=false;break;}
    if(bStageReady&&StageTraps.IsEmpty())for(const auto& Trap:Stage.Traps)
    {
     auto* Mine=GetWorld()->SpawnActor<AEGCorruptionMine>(Board->CellToWorld(Trap.Cell)-FVector(0,0,88),FRotator::ZeroRotator);
     if(!IsValid(Mine)||Mine->IsActorBeingDestroyed()){bStageReady=false;break;}
     Mine->SetOwner(this);Mine->Configure(Trap.Definition,FVector::Dist2D(Board->CellToWorld({0,0}),Board->CellToWorld({0,1})));
     if(!IsValid(Mine)||Mine->IsActorBeingDestroyed()){bStageReady=false;break;}
     StageTraps.Add(Mine);
    }
    if(bStageReady)for(const auto& Mine:StageTraps)
     if(!IsValid(Mine)||Mine->IsActorBeingDestroyed()){bStageReady=false;break;}
    if(!bStageReady)
    {
     ClearStageTraps();
     for(const auto& Entry:CreatedEnemies)if(Entry.IsValid())
     {Entry->OnCombatResolved.RemoveDynamic(this,&ThisClass::TrackCombatResolution);DiscardPreparedUnit(Entry.Get());}
     SetHint(TEXT("敌人生成失败，请检查关卡配置和棋盘落点；再次点击开始可重试。"));
    }
    else
    {
        LastAudioClockSeconds=GetWorld()?GetWorld()->GetAudioTimeSeconds():-1.0;
        ClockStageNumber=GetMatch()->GetStageNumber();
        PreparationRealSeconds=0.f;
        bPreparationAutoStartAttempted=false;
        OnPhaseClockChanged.Broadcast();
    }
}
void AEGDemoGameMode::PrepareUnitForCombat(AEGChessUnit* Unit,const FEGHeroInstance& Hero)
{
    Super::PrepareUnitForCombat(Unit,Hero);
    Unit->OnCombatResolved.AddUniqueDynamic(this,&ThisClass::TrackCombatResolution);
    Deployment->AttachPresentation(Unit);
}
void AEGDemoGameMode::PrepareSummonForCombat(AEGChessUnit* Unit)
{if(Unit)Unit->OnCombatResolved.AddUniqueDynamic(this,&ThisClass::TrackCombatResolution);}
bool AEGDemoGameMode::EnterCombat()
{
    if(!CanPrepare()||bCoordinating||bPreparingStage||!Cards)return false;
    if(!bStageReady)PrepareStageEnemies();
    if(!bStageReady)return false;
    bDuskStreetSettlementEligible=false;
    if(auto* PC=Cast<AEGBoardPlayerController>(GetWorld()->GetFirstPlayerController()))
    {PC->CancelActiveDrag();if(auto* View=PC->GetMainHUD())View->CloseContextPanels();}
    if(Cards->GetPieces().IsEmpty())
    {
        if(RunDefinition->bSevenStageRules)
        {
            if(RunDefinition->bRoundShopRules&&!Cards->BeginHandSettlement())return false;
            const bool bEntered=GetMatch()->EnterCombat();
            if(RunDefinition->bRoundShopRules)Cards->EndHandSettlement(bEntered);
            if(bEntered)return ResolveCombat(false);
        }
        SetHint(TEXT("请先将手牌拖到我方棋盘。"));return false;
    }
    if(!Deployment->TrySynchronize()){Deployment->RestorePresentations();SetHint(TEXT("部署同步未完成，请检查棋子和占格后再开始。"));return false;}
    if(RunDefinition->bRoundShopRules&&!Cards->BeginHandSettlement())
    {Deployment->RestorePresentations();SetHint(TEXT("受保护的手牌数量超过上限，请先整理手牌。"));return false;}
    StageSnapshot=GetRoster()->GetHeroes();Cards->SetPreparationEnabled(false);
    if(RunDefinition->bSevenStageRules&&!TransformEncounterCopies())
    {if(RunDefinition->bRoundShopRules)Cards->EndHandSettlement(false);Deployment->RestorePresentations();Cards->SetPreparationEnabled(true);SetHint(TEXT("侵蚀阵容准备失败，请检查关卡配置。"));return false;}
    if(EncounterBoss.IsValid())
    {
        BossDeathFrame=MAX_uint64;
        EncounterBoss->OnDied.AddUniqueDynamic(this,&ThisClass::RecordBossDeath);
    }
    CombatRealSeconds=0.f;CorruptionPeriodStartRealSeconds=0.f;BattleSeconds=0.f;LastClockSecond=INDEX_NONE;BossCorruptionTicks=0;
    LastAudioClockSeconds=GetWorld()->GetAudioTimeSeconds();
    RunningRoundId.Invalidate();
    if(!Super::EnterCombat())
    {if(EncounterBoss.IsValid())EncounterBoss->OnDied.RemoveDynamic(this,&ThisClass::RecordBossDeath);if(RunDefinition->bRoundShopRules)Cards->EndHandSettlement(false);RestoreEncounterAfterFailedStart();Deployment->RestorePresentations();Cards->SetPreparationEnabled(true);SetHint(TEXT("双方阵容尚未准备完成。"));return false;}
    if(GetMatch()->HasStigmata(TEXT("DuskStreet")))
    {
        TSet<EEGHeroKind> Deployed;
        for(const auto& Hero:StageSnapshot)if(Hero.bDeployed&&Hero.Definition)Deployed.Add(Hero.Definition->HeroKind);
        bDuskStreetSettlementEligible=Deployed.Contains(EEGHeroKind::Pardofelis)&&
            Deployed.Contains(EEGHeroKind::Sakura)&&Deployed.Contains(EEGHeroKind::Aponia);
    }
    if(RunDefinition->bRoundShopRules)Cards->EndHandSettlement(true);
    if(EncounterBoss.IsValid())SetActorTickInterval(0.f);
    CombatStatistics.Reset();OnCombatStatisticsChanged.Broadcast();
    SetHint(TEXT("自动作战中：攻击与受击回蓝，满蓝自动释放技能。"));return true;
}
void AEGDemoGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if(!GetWorld()||GetWorld()->IsPaused())return;
    const float EffectiveDilation=GetWorld()->GetWorldSettings()?GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation():1.f;
    const double Now=GetWorld()->GetAudioTimeSeconds();
    const float RealDelta=LastAudioClockSeconds>=0.0?static_cast<float>(FMath::Max(0.0,Now-LastAudioClockSeconds)):0.f;
    LastAudioClockSeconds=Now;
    TryInstallOpeningErrorWidget();
    if(bConfigured)
    {
     if(bStageEntrancePending&&!IsValid(OpeningPresentation)&&StageEntranceError.IsEmpty())
         BeginStageEntranceOrWait();
     if(bStageTransitionPending&&!IsValid(OpeningPresentation))
     {
         bStageTransitionPending=false;TransitionToken.Invalidate();TransitionFromStage=INDEX_NONE;
         bResourcesReady=true;SetHint(TEXT("关卡过渡中断，请再次进入下一关。"));
     }
     // The menu is usable before loading the run. Start actual dependency
     // requests only after the presentation accepted the player's click.
     if(bOpeningPending&&bOpeningLoadRequested&&!bOpeningPreloadStarted&&OpeningStartupError.IsEmpty()&&
        IsValid(OpeningPresentation)&&OpeningPresentation->IsPreparingAfterStart())StartOpeningPreload();
     RefreshBackgroundResourceRequests();Resources->Poll();
     SyncStigmaPanel();
    }
    if(RunDefinition&&RunDefinition->bSevenStageRules&&GetMatch()&&GetMatch()->GetPhase()==EEGMatchPhase::Settlement)
    {
        OnPhaseClockChanged.Broadcast();
        if(bSettlementClockActive&&!bCoordinating&&GetSettlementElapsedSeconds()>=SettlementDurationSeconds)SettlementFinished();
        return;
    }
    if(RunDefinition&&RunDefinition->bRoundShopRules&&GetMatch()&&GetMatch()->GetPhase()==EEGMatchPhase::Resolution&&!bCoordinating)
    {AdvanceDemo();return;}
    if(RunDefinition&&RunDefinition->bSevenStageRules&&GetMatch()&&GetMatch()->GetPhase()==EEGMatchPhase::Preparation)
    {
        const int32 Stage=GetMatch()->GetStageNumber();
        if(ClockStageNumber!=Stage)
        {
            ClockStageNumber=Stage;
            PreparationRealSeconds=0.f;
            bPreparationAutoStartAttempted=false;
        }
        if(bStageReady&&Cards&&Cards->IsInitialized()&&!bCoordinating&&!IsPresentationInputBlocked())
        {
            PreparationRealSeconds+=RealDelta;
            const float Limit=FMath::Max(.01f,GetDefault<UEGSevenStageDefinition>()->PreparationRealSeconds);
            OnPhaseClockChanged.Broadcast();
            if(PreparationRealSeconds>=Limit&&!bPreparationAutoStartAttempted)
            {
                bPreparationAutoStartAttempted=true;
                EnterCombat();
            }
        }
        return;
    }
    if(!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Combat)return;
    const bool bCampaign=RunDefinition&&RunDefinition->bSevenStageRules;
    const bool bBossStage=bCampaign&&GetMatch()->GetStageNumber()==7;
    if(bBossStage&&(BossDeathFrame!=MAX_uint64||!EncounterBoss.IsValid()||!IEGCombatant::Execute_IsAlive(EncounterBoss.Get())))
    {
        if(BossDeathFrame==MAX_uint64)BossDeathFrame=GFrameCounter;
        // Wait until every damage callback in the death frame has completed.
        if(BossDeathFrame>=GFrameCounter)return;
    }
    bool bOvertimeExpired=false;
    if(bCampaign)
    {
        if(!BattleRules||!BattleRules->IsCombatRunning())
        {
            OnPhaseClockChanged.Broadcast();
            return;
        }
        CombatRealSeconds+=RealDelta;
        const auto* Config=GetDefault<UEGSevenStageDefinition>();
        const int32 Index=GetMatch()->GetStageNumber()-1;
        const float Normal=bBossStage?FMath::Max(.01f,Config->BossNormalRealSeconds):
            RunDefinition->Stages.IsValidIndex(Index)?FMath::Max(.01f,RunDefinition->Stages[Index].StageBattleTimeLimit):25.f;
        if(!bBossStage&&CombatRealSeconds>=Normal&&!bOwnsOvertimeDilation)
        {
            OriginalGlobalTimeDilation=UGameplayStatics::GetGlobalTimeDilation(GetWorld());
            UGameplayStatics::SetGlobalTimeDilation(GetWorld(),OriginalGlobalTimeDilation*FMath::Max(.01f,Config->OvertimeSpeed));
            AppliedGlobalTimeDilation=UGameplayStatics::GetGlobalTimeDilation(GetWorld());
            bOwnsOvertimeDilation=true;
        }
        bOvertimeExpired=!bBossStage&&CombatRealSeconds>=Normal+FMath::Max(.01f,Config->OvertimeRealSeconds);
        if(!BattleRules||!BattleRules->IsCombatRunning())
        {
            OnPhaseClockChanged.Broadcast();
            if(bOvertimeExpired)ResolveCombat(false);
            return;
        }
        if(RealDelta<=0.f)
        {
            OnPhaseClockChanged.Broadcast();
            if(bOvertimeExpired)ResolveCombat(false);
            return;
        }
    }
    BattleSeconds+=DeltaSeconds;
    if(bBossStage)
    {
        if(EncounterBoss.IsValid())
            if(auto* Boss=EncounterBoss->FindComponentByClass<UEGCorruptionCombatComponent>())
                Boss->SetEffectiveBattleSeconds(BattleSeconds);
    }
    if(bCampaign)OnPhaseClockChanged.Broadcast();
    int32 Players=0,Enemies=0;
    for(const auto& Unit:GetManagedUnits())
    {
        if(!Unit.IsValid()||(!IEGCombatant::Execute_IsAlive(Unit.Get())&&!Unit->GetHeroSkill()->IsRecoverable()))continue;
        const auto Team=IEGCombatant::Execute_GetFaction(Unit.Get());
        Players+=Team==EEGFaction::Player?1:0;Enemies+=Team==EEGFaction::Enemy?1:0;
    }
    // Deferred from damage callbacks; destruction is also detected. Simultaneous wipe is a loss.
    const int32 StageIndex=GetMatch()->GetStageNumber()-1;
    const float TimeLimit=bCampaign&&RunDefinition->Stages.IsValidIndex(StageIndex)?RunDefinition->Stages[StageIndex].StageBattleTimeLimit:RunDefinition->BattleTimeLimit;
    if(Players==0||Enemies==0||bOvertimeExpired||(!bCampaign&&TimeLimit>0.f&&BattleSeconds>=TimeLimit))
    {ResolveCombat(Players>0&&Enemies==0);return;}
    const int32 Second=FMath::FloorToInt(BattleSeconds);
    if(Second!=LastClockSecond){LastClockSecond=Second;OnBattleClockChanged.Broadcast();}
}
void AEGDemoGameMode::BattleResolved(bool bVictory,int32 Corruption)
{
    ClearBossWave();
    bLastVictory=bVictory;
    if(!RunDefinition->bSevenStageRules)Deployment->RestorePresentations();
    if(RunDefinition->bSevenStageRules)
    {
        if(GetMatch()->GetPhase()==EEGMatchPhase::Finished)
        {
            if(Cards&&bDuskStreetSettlementEligible){bDuskStreetSettlementEligible=false;const int32 Gold=FMath::Max(0,UEGStigmataBalanceSettings::Get().DuskGold);Cards->AwardGold(Gold);GetMatch()->RecordStigmataGoldEarned(TEXT("DuskStreet"),Gold);if(BattleRules&&BattleRules->GetStigmata())BattleRules->GetStigmata()->RecordGold(TEXT("DuskStreet"),Gold);OnRunViewChanged.Broadcast();}
            ClearSettlementPresentation();
        }
        SetHint(bVictory?TEXT("本回合胜利"):TEXT("本回合失利"));
        if(GetMatch()->GetPhase()==EEGMatchPhase::Settlement||GetMatch()->GetPhase()==EEGMatchPhase::Finished)
            BeginSettlementPresentation(bVictory);
        return;
    }
    if(RunDefinition->bRoundShopRules)
    {
        SetHint(bVictory?TEXT("本回合胜利"):GetMatch()->GetStageNumber()<GetMatch()->GetStageCount()?FString::Printf(TEXT("本回合失利  侵蚀度 +1（当前 %d）"),Corruption):TEXT("本局失败"));
        return;
    }
    const int32 Index=GetMatch()->GetStageNumber()-1;
    if(bVictory&&RunDefinition->Stages.IsValidIndex(Index))Cards->AwardGold(RunDefinition->Stages[Index].VictoryGold);
    if(!bVictory)SetHint(BattleSeconds>=RunDefinition->BattleTimeLimit?TEXT("作战超时，可重试本关。"):TEXT("战斗失败，可重新布阵、合成后重试本关。"));
    else if(Index==RunDefinition->Stages.Num()-1)SetHint(TEXT("两关已完成，可以重新开始。"));
    else SetHint(TEXT("本关胜利，奖励已发放。继续进入下一关。"));
}
bool AEGDemoGameMode::NextStage()
{
    if(!HasAuthority()||!bConfigured||bCoordinating||IsPresentationInputBlocked()||bChoosingStigmata||bEndgameCheckpointRetryNeeded||
       (!bLastVictory&&!RunDefinition->bRoundShopRules)||GetMatch()->GetPhase()!=EEGMatchPhase::Resolution)return false;
    if(bPresentationFlowActive&&!IsValid(OpeningPresentation))
    {
        OpeningPresentation=UEGOpeningPresentation::Install(this);
        if(!IsValid(OpeningPresentation)){SetHint(TEXT("关卡过渡不可用，请再次尝试。"));return false;}
    }
    if(bPresentationFlowActive&&GetMatch()->GetStageNumber()<RunDefinition->Stages.Num())
    {
        bStageTransitionPending=true;TransitionFromStage=GetMatch()->GetStageNumber();
        TransitionToken=FGuid::NewGuid();bResourcesReady=false;
        if(Cards)Cards->SetPreparationEnabled(false);
        OnRunViewChanged.Broadcast();
        OpeningPresentation->BeginStageTransition(TransitionToken);
        return true;
    }
    return CommitNextStage();
}
bool AEGDemoGameMode::CommitNextStage()
{
    if(GetMatch()->GetStageNumber()==RunDefinition->Stages.Num())
    {const bool bCompleted=GetMatch()->CompleteRun();if(bCompleted)SetHint(TEXT("两关已完成，可以重新开始。"));return bCompleted;}
    ResetPhaseClock();
    bStageReady=false;
    if(!Super::NextStage())return false;
    if(RunDefinition->bRoundShopRules)Cards->BeginRoundShopRound(GetMatch()->GetStageNumber());
    Cards->SetPreparationEnabled(CanPrepare());
    Deployment->Synchronize();if(bStageReady)SetHint(TEXT("下一关已准备好，可布阵、合成或购买卡牌。"));return true;
}
bool AEGDemoGameMode::CanRetryCurrentStage() const
{
    if(RunDefinition&&RunDefinition->bSevenStageRules)return false;
    return bConfigured&&!bCoordinating&&Cards&&GetRoster()&&GetMatch()&&GetMatch()->HasStarted()&&
        GetMatch()->GetPhase()==EEGMatchPhase::Finished&&!GetMatch()->DidWinRun()&&!bLastVictory&&!StageSnapshot.IsEmpty();
}
bool AEGDemoGameMode::RetryCurrentStage()
{
    if(!HasAuthority()||!CanRetryCurrentStage())return false;
    {
        TGuardValue<bool> Guard(bCoordinating,true);
        Cards->SetPreparationEnabled(false);
        // Transition first: a rejected or reentrant retry cannot destroy the current battle.
        if(!GetMatch()->RetryStage())return false;
        Cards->RollbackSkillGold(BattleRules->ConsumeAwardedGold());
        Deployment->RestorePresentations();ClearUnits();ClearStageTraps();
        GetRoster()->RestoreSnapshot(StageSnapshot);bStageReady=false;
        for(const auto& Hero:GetRoster()->GetHeroes())if(Hero.bDeployed)SpawnRosterHero(Hero.InstanceId);
        PrepareStageEnemies();BattleSeconds=0.f;LastClockSecond=INDEX_NONE;
        CombatStatistics.Reset();OnCombatStatisticsChanged.Broadcast();
    }
    Cards->SetPreparationEnabled(CanPrepare());
    const bool bSynchronized=bStageReady&&Deployment->TrySynchronize();
    if(bSynchronized)SetHint(TEXT("本关已重置，英雄成长与手牌保留。准备后再次作战。"));
    else SetHint(TEXT("本关重试准备未完成，请检查部署与敌人配置。"));
    return bSynchronized;
}

bool AEGDemoGameMode::CaptureEndgameCheckpoint()
{
    bEndgameCheckpointValid=false;
    if(!HasAuthority()||!Cards||!GetRoster()||!GetMatch()||!BattleRules||
       GetMatch()->GetStageNumber()!=6||GetMatch()->GetPhase()!=EEGMatchPhase::Resolution||BattleRules->IsBattleActive())return false;
    FEGCardRunCheckpoint CardState;
    if(!Cards->CaptureRunCheckpoint(CardState))return false;
    EndgameCardsCheckpoint=MoveTemp(CardState);EndgameRosterCheckpoint=GetRoster()->CaptureRunState();
    EndgameEncounterHistory=EncounterHistory;EndgameCombatStatistics=CombatStatistics;
    EndgameOrdinaryStigmata=GetMatch()->StigmataOwned;
    EndgameStigmataGold=GetMatch()->StigmataGoldEarned;EndgameStigmataSavings=GetMatch()->StigmataRefreshSavings;
    EndgameCorruption=GetMatch()->Corruption;
    EndgameContributions=BattleRules->GetStigmata()?BattleRules->GetStigmata()->GetContributions():TArray<FEGStigmataContribution>();
    EndgameEncounterRandom=EncounterRandom;EndgameStigmataRandom=StigmataRandom;
    bEndgameLastVictory=bLastVictory;bEndgameCheckpointValid=true;return true;
}

bool AEGDemoGameMode::CanRestoreEndgameCheckpoint() const
{
    return HasAuthority()&&bConfigured&&!bCoordinating&&!bChoosingStigmata&&bEndgameCheckpointValid&&bFinalBossCombatEntered&&
        RunDefinition&&RunDefinition->bSevenStageRules&&Cards&&Deployment&&GetRoster()&&GetMatch()&&BattleRules&&
        !BattleRules->IsBattleActive()&&GetMatch()->HasStarted()&&
        GetMatch()->GetStageNumber()==7&&GetMatch()->GetStageCount()==7&&
        GetMatch()->GetPhase()==EEGMatchPhase::Finished&&!GetMatch()->DidWinRun();
}

bool AEGDemoGameMode::RestoreEndgameCheckpoint()
{
    if(!CanRestoreEndgameCheckpoint())return false;
    {
    TGuardValue<bool> Guard(bCoordinating,true);
    // Move attached visuals back before staging replacement pieces; gameplay state remains
    // unchanged if staging or occupancy validation rejects the checkpoint.
    Deployment->RestorePresentations();
    if(!Cards->RestoreRunCheckpoint(EndgameCardsCheckpoint))return false;
    ClearSettlementPresentation();ClearBossWave();RestoreOvertimeDilation();ResetPhaseClock();
    ClearEncounterDisguises();ClearStageTraps();ClearUnits();
    if(EncounterBoss.IsValid())EncounterBoss->OnDied.RemoveDynamic(this,&ThisClass::RecordBossDeath);
    EncounterBoss.Reset();EncounterCopies.Reset();EncounterDeviceCells.Reset();BossDeathFrame=MAX_uint64;BossCorruptionTicks=0;
    FEGScopedRosterMutation Mutation(GetRoster());
    GetRoster()->RestoreRunState(EndgameRosterCheckpoint);
    Cards->RefreshHeroPresentations();
    GetMatch()->StageIndex=5;GetMatch()->Corruption=EndgameCorruption;GetMatch()->bWonRun=false;
    GetMatch()->PendingCorruptionDelta=0;GetMatch()->bSettlementImpactApplied=false;
    GetMatch()->StigmataOwned=EndgameOrdinaryStigmata;GetMatch()->EndgameRoute=NAME_None;
    GetMatch()->StigmataGoldEarned=EndgameStigmataGold;GetMatch()->StigmataRefreshSavings=EndgameStigmataSavings;
    GetMatch()->PendingStigmataId=NAME_None;GetMatch()->bStigmataCommitting=false;
    if(UEGStigmataBattleComponent* Stigmata=BattleRules->GetStigmata())
    {
        const bool bStatisticsRestored=Stigmata->RestoreContributions(EndgameContributions);
        ensureAlways(bStatisticsRestored);
    }
    EncounterHistory=EndgameEncounterHistory;CombatStatistics=EndgameCombatStatistics;
    EncounterRandom=EndgameEncounterRandom;StigmataRandom=EndgameStigmataRandom;
    StageSnapshot.Reset();bStageReady=false;bLastVictory=bEndgameLastVictory;bDuskStreetSettlementEligible=false;
    BattleSeconds=0.f;LastClockSecond=INDEX_NONE;bFinalBossCombatEntered=false;
    bChoosingEndgameRoute=true;StigmataOffer.Reset();PresentedStigmaRevision=INDEX_NONE;
    StigmaSkipRevision=StigmaSkipRevision==MAX_int32?1:StigmaSkipRevision+1;
    // Publish the route and all restored state before observers see the phase.
    GetMatch()->SetPhase(EEGMatchPhase::Reward);
    }
    Cards->NotifyRunCheckpointRestored();OnCombatStatisticsChanged.Broadcast();OnRunViewChanged.Broadcast();
    return true;
}
void AEGDemoGameMode::RequestEncounterRecovery(FGuid ExpectedRound)
{
    if(!HasAuthority()||!GetWorld()||!ExpectedRound.IsValid())return;
    GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this,[this,ExpectedRound]()
    {
        if(bCoordinating||!BattleRules||BattleRules->GetRoundId()!=ExpectedRound||!GetMatch()||
           GetMatch()->GetPhase()!=EEGMatchPhase::Combat||!Cards||!Cards->HasEncounterHandSnapshot()||StageSnapshot.IsEmpty())return;
        {
            TGuardValue<bool> Guard(bCoordinating,true);
            const int32 SkillGold=BattleRules->ConsumeAwardedGold();
            Cards->RollbackSkillGold(SkillGold);Cards->EndHandSettlement(false);Cards->SetPreparationEnabled(false);
            GetMatch()->SetPhase(EEGMatchPhase::Preparation);
            Deployment->RestorePresentations();ClearUnits();ClearStageTraps();ClearBossWave();ClearEncounterDisguises();
            EncounterCopies.Reset();EncounterBoss.Reset();BossDeathFrame=MAX_uint64;
            {
                FEGScopedRosterMutation Mutation(GetRoster());
                GetRoster()->RestoreSnapshot(StageSnapshot);Cards->RestoreEncounterHandSnapshot();
            }
            bStageReady=false;ResetPhaseClock();
            for(const auto& Hero:GetRoster()->GetHeroes())if(Hero.bDeployed)SpawnRosterHero(Hero.InstanceId);
            PrepareStageEnemies();BattleSeconds=0.f;LastClockSecond=INDEX_NONE;
            CombatStatistics.Reset();OnCombatStatisticsChanged.Broadcast();
        }
        Cards->SetPreparationEnabled(CanPrepare());Deployment->TrySynchronize();
        SetHint(TEXT("壁垒部署空间不足，本场已返回准备阶段。未结算胜败或侵蚀，请重新布阵后再试。"));
        OnRunViewChanged.Broadcast();
    }));
}
bool AEGDemoGameMode::AdvanceDemo()
{
    if(!bConfigured||bCoordinating||IsPresentationInputBlocked())return false;
    const auto Phase=GetMatch()->GetPhase();
    if(Phase==EEGMatchPhase::Preparation)return EnterCombat();
    if(Phase==EEGMatchPhase::Resolution&&(bLastVictory||RunDefinition->bRoundShopRules))
    {
        return NextStage();
    }
    if(Phase==EEGMatchPhase::Finished)
    {
        if(CanRestoreEndgameCheckpoint())return false;
        if(RunDefinition->bRoundShopRules)return StartGame();
        if(bLastVictory)return StartGame();
        return RetryCurrentStage();
    }
    return false;
}
TArray<AEGChessUnit*> AEGDemoGameMode::GetCombatUnits() const
{
    TArray<AEGChessUnit*> Result;for(const auto& Unit:GetManagedUnits())if(Unit.IsValid())Result.Add(Unit.Get());return Result;
}
void AEGDemoGameMode::RecordBossDeath(AEGChessUnit* Unit)
{
    if(Unit&&Unit==EncounterBoss.Get()&&GetMatch()&&GetMatch()->GetPhase()==EEGMatchPhase::Combat)
        BossDeathFrame=GFrameCounter;
}

void AEGDemoGameMode::EncounterCombatRunning(FGuid RoundId)
{
    if(!RoundId.IsValid()||RunningRoundId==RoundId||!BattleRules||!BattleRules->IsCombatRunning()||
       BattleRules->GetRoundId()!=RoundId||!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Combat)return;
    const double Now=GetWorld()?GetWorld()->GetAudioTimeSeconds():-1.0;
    LastAudioClockSeconds=Now;
    RunningRoundId=RoundId;
    CorruptionPeriodStartRealSeconds=CombatRealSeconds;
    ClearEncounterDisguises();
    if(RunDefinition&&RunDefinition->bSevenStageRules)ActivateEncounterCombat();
    for(const auto& Trap:StageTraps)if(IsValid(Trap.Get()))Trap->Arm(BattleRules);
    OnPhaseClockChanged.Broadcast();
}

void AEGDemoGameMode::BeginSettlementPresentation(bool bVictory)
{
    ClearSettlementPresentation();
    if(!GetWorld()||!Board||!Scene||!Scene->Board)return;
    AEGChessUnit* Elysia=nullptr;
    const auto CombatUnits=GetCombatUnits();
    for(AEGChessUnit* Unit:CombatUnits)
    {
        const auto* Definition=IsValid(Unit)?Unit->GetDefinition():nullptr;
        if(Definition&&IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player&&
           (Definition->HeroId==FName(TEXT("Elysia"))||Definition->HeroKind==EEGHeroKind::Elysia))
        {Elysia=Unit;break;}
    }
    const int32 Center=Scene->Board->Columns/2;
    const FVector PlayerFallback=Board->CellToWorld(FIntPoint(Center,-1));
    const FVector EnemyLocation=Board->CellToWorld(FIntPoint(Center,Scene->Board->Rows));
    SettlementPresentation=GetWorld()->SpawnActor<AEGSettlementPresentation>();
    if(!SettlementPresentation)return;
    SettlementPresentation->SetOwner(this);
    // Finished has already committed the final result. Its interaction is
    // cosmetic, so it must never reapply an impact transaction.
    if(GetMatch()&&GetMatch()->GetPhase()==EEGMatchPhase::Settlement)
        SettlementPresentation->OnImpact.AddUObject(this,&ThisClass::SettlementImpact);
    // A final-result presentation keeps its idle/frozen leases through the
    // result screen. StartGame/EndPlay clear it before unit reuse/destruction.
    SettlementPresentation->StartPresentation(CombatUnits,Elysia,PlayerFallback,EnemyLocation,bVictory);
    OnPhaseClockChanged.Broadcast();
}
void AEGDemoGameMode::SyncStigmaPanel()
{
    UEGDemoPanel* Current=GetDemoPanel();
    if(Panel!=Current)
    {
        if(Panel)Panel->OnStigmaSkipRequested.RemoveDynamic(this,&ThisClass::StigmaSkipRequested);
        Panel=Current;PresentedStigmaRevision=INDEX_NONE;
        if(Panel)Panel->OnStigmaSkipRequested.AddUniqueDynamic(this,&ThisClass::StigmaSkipRequested);
    }
    if(!Panel)return;
    if(!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Reward)
    {
        if(PresentedStigmaRevision!=INDEX_NONE)Panel->HideStigmaSkip();
        PresentedStigmaRevision=INDEX_NONE;return;
    }
    if(PresentedStigmaRevision!=StigmaSkipRevision)
    {
        Panel->PresentStigmaSkip(StigmaSkipRevision);
        PresentedStigmaRevision=StigmaSkipRevision;
    }
}
void AEGDemoGameMode::StigmaSkipRequested(int32 Revision)
{
    const bool bSkipped=SkipStigma(Revision);
    if(!bSkipped&&GetMatch()&&GetMatch()->GetPhase()==EEGMatchPhase::Reward)PresentedStigmaRevision=INDEX_NONE;
    SyncStigmaPanel();
}
void AEGDemoGameMode::BeginStigmaChoice()
{
    if(!GetMatch())return;
    // The sixth reward is the final route itself: no third ordinary engraving.
    // Publish the route state and revision before EnterReward broadcasts the phase.
    if(GetMatch()->GetStageNumber()==6)
    {
        if(!CaptureEndgameCheckpoint())
        {
            bEndgameCheckpointRetryNeeded=true;
            UE_LOG(LogTemp,Error,TEXT("Unable to capture the endgame retry checkpoint"));
            SetHint(TEXT("终局准备失败，请重试。"));
            if(GetWorld())if(auto* PC=GetWorld()->GetFirstPlayerController())UEGOpeningWidget::Install(PC);
            return;
        }
        bEndgameCheckpointRetryNeeded=false;
        StigmataOffer.Reset();
        bChoosingEndgameRoute=true;
        ++StigmaSkipRevision;
        if(GetMatch()->EnterReward())OnRunViewChanged.Broadcast();
        else
        {
            bChoosingEndgameRoute=false;bEndgameCheckpointRetryNeeded=true;
            SetHint(TEXT("终局准备失败，请重试。"));
            if(GetWorld())if(auto* PC=GetWorld()->GetFirstPlayerController())UEGOpeningWidget::Install(PC);
        }
        return;
    }
    GenerateStigmataOffer(GetMatch()->GetStageNumber());
    if(StigmataOffer.Num()!=3)
    {
        UE_LOG(LogTemp,Error,TEXT("Unable to generate three valid stigmata candidates"));
        ++StigmaSkipRevision;
        GetMatch()->EnterReward();
        SetHint(TEXT("往世刻印候选配置不完整，请检查运行配置。"));
        return;
    }
    ++StigmaSkipRevision;
    if(GetMatch()->EnterReward())OnRunViewChanged.Broadcast();
}
bool AEGDemoGameMode::RetryEndgameCheckpoint()
{
    if(!HasAuthority()||!bConfigured||bCoordinating||!bEndgameCheckpointRetryNeeded||!GetMatch()||
       GetMatch()->GetStageNumber()!=6||GetMatch()->GetPhase()!=EEGMatchPhase::Resolution||
       !GetWorld()||GetWorld()->bIsTearingDown)return false;
    TGuardValue<bool> Guard(bCoordinating,true);
    BeginStigmaChoice();
    return !bEndgameCheckpointRetryNeeded&&bChoosingEndgameRoute&&GetMatch()->GetPhase()==EEGMatchPhase::Reward;
}
bool AEGDemoGameMode::SkipStigma(int32 ExpectedRevision)
{
    if(RunDefinition&&RunDefinition->bSevenStageRules)return false;
    if(!HasAuthority()||!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Reward||
       ExpectedRevision!=StigmaSkipRevision)return false;
    if(!GetMatch()->CompleteReward())return false;
    ++StigmaSkipRevision;
    OnRunViewChanged.Broadcast();return true;
}

void AEGDemoGameMode::SettlementImpact()
{
    if(!GetMatch()||!GetMatch()->ApplySettlementImpact())return;
    // If this impact ends the run, the outcome is already committed. Keep the
    // existing cosmetic interaction alive until its completion callback.
    if(GetMatch()->GetCorruption()>=200)bLastVictory=false;
}
void AEGDemoGameMode::SettlementFinished()
{
    // A stale presentation callback or other premature caller cannot bypass
    // the authoritative phase deadline. FinishSettlement remains the one
    // transaction that applies any outstanding impact and moves the run on.
    if(bCoordinating||!bSettlementClockActive||!GetMatch()||
       GetMatch()->GetPhase()!=EEGMatchPhase::Settlement||GetSettlementElapsedSeconds()<SettlementDurationSeconds)return;
    // Keep phase-change observers from advancing before the reward/checkpoint transaction is ready.
    TGuardValue<bool> Guard(bCoordinating,true);
    const int32 Stage=GetMatch()?GetMatch()->GetStageNumber():0;
    const bool bCompleted=GetMatch()&&GetMatch()->FinishSettlement();
    ClearSettlementPresentation();
    if(!bCompleted||GetMatch()->GetPhase()!=EEGMatchPhase::Resolution)return;
    if(Cards&&RunDefinition->Stages.IsValidIndex(Stage-1))Cards->AwardGold(RunDefinition->Stages[Stage-1].CompletionGold);
    if(Cards&&bDuskStreetSettlementEligible){bDuskStreetSettlementEligible=false;const int32 Gold=FMath::Max(0,UEGStigmataBalanceSettings::Get().DuskGold);Cards->AwardGold(Gold);GetMatch()->RecordStigmataGoldEarned(TEXT("DuskStreet"),Gold);if(BattleRules&&BattleRules->GetStigmata())BattleRules->GetStigmata()->RecordGold(TEXT("DuskStreet"),Gold);OnRunViewChanged.Broadcast();}
    if(Stage==2||Stage==4||Stage==6)BeginStigmaChoice();
}
void AEGDemoGameMode::ClearSettlementPresentation()
{
    if(IsValid(SettlementPresentation))
    {
        SettlementPresentation->OnImpact.RemoveAll(this);
        SettlementPresentation->OnFinished.RemoveAll(this);
        SettlementPresentation->Destroy();
    }
    SettlementPresentation=nullptr;
}

void AEGDemoGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
    // Invalidate queued presentation callbacks before destroying run state.
    bOpeningPending=false;bStageTransitionPending=false;bStageEntrancePending=false;bEndgameCheckpointRetryNeeded=false;
    OpeningToken.Invalidate();TransitionToken.Invalidate();EntranceToken.Invalidate();
    bPresentationFlowActive=false;bOpeningErrorWidgetPending=false;bOpeningLoadRequested=false;bOpeningPreloadStarted=false;StageEntranceError.Reset();
    ClearBossWave();
    ResetSettlementClock();
    RestoreOvertimeDilation();
    ClearEncounterDisguises();
    if(BattleRules)BattleRules->OnCombatRunning.RemoveDynamic(this,&ThisClass::EncounterCombatRunning);
    ClearSettlementPresentation();
    if(EncounterBoss.IsValid())EncounterBoss->OnDied.RemoveDynamic(this,&ThisClass::RecordBossDeath);
    ResetEncounterHistory();
    ClearStageTraps();
    bConfigured=false;
    if(bAppliedFrameCap)
     if(auto* Cap=IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS")))Cap->SetWithCurrentPriority(PreviousFrameCap);
    bResourcesReady=false;Resources->ResetResources();
    Deployment->RestorePresentations();
    if(Cards){Cards->SetPreparationEnabled(false);Cards->OnFeedbackChanged.RemoveDynamic(this,&ThisClass::CardFeedback);}
    if(GetMatch())
    {
        GetMatch()->OnBattleResolved.RemoveDynamic(this,&ThisClass::BattleResolved);
        GetMatch()->OnPhaseChanged.RemoveDynamic(this,&ThisClass::RunPhaseChanged);
    }
    if(Panel){Panel->OnStigmaSkipRequested.RemoveDynamic(this,&ThisClass::StigmaSkipRequested);Panel->RemoveFromParent();Panel=nullptr;}
    for(const auto& U:GetManagedUnits())if(U.IsValid())U->OnCombatResolved.RemoveDynamic(this,&ThisClass::TrackCombatResolution);
    OnRunViewChanged.Clear();OnBattleClockChanged.Clear();OnPhaseClockChanged.Clear();OnCombatStatisticsChanged.Clear();Super::EndPlay(Reason);
}
void AEGDemoGameMode::ClearStageTraps()
{for(const auto& Trap:StageTraps)if(IsValid(Trap.Get()))Trap->Destroy();StageTraps.Reset();}

void AEGDemoGameMode::GenerateStigmataOffer(int32 Stage)
{
    StigmataOffer.Reset();
    if(!GetMatch()||!GetRoster()||!Cards||(Stage!=2&&Stage!=4&&Stage!=6))return;
    TSet<EEGHeroKind> Held,Deployed;
    for(const auto& Hero:GetRoster()->GetHeroes())if(Hero.Definition)
    {
        Held.Add(Hero.Definition->HeroKind);
        if(Hero.bDeployed)Deployed.Add(Hero.Definition->HeroKind);
    }
    for(const auto& Card:Cards->GetHand())if(Card.Definition&&Card.Definition->HeroDefinition)
        Held.Add(Card.Definition->HeroDefinition->HeroKind);
    auto CountHeld=[&](std::initializer_list<EEGHeroKind> Kinds)
    {int32 Count=0;for(auto Kind:Kinds)if(Held.Contains(Kind))++Count;return Count;};
    auto HasForm=[&](EEGHeroKind Kind)
    {return RunDefinition&&RunDefinition->CardPool.ContainsByPredicate([&](const TObjectPtr<UEGCardDefinition>& Card)
       {return Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==Kind&&Card->HeroDefinition->EnhancedDefinition;});};
    auto Effective=[&](FName Id)
    {
        if(Id==TEXT("Reverie"))return true;
        if(Id==TEXT("Stars"))return Deployed.Contains(EEGHeroKind::Griseo);
        if(Id==TEXT("Vicissitude"))return Deployed.Contains(EEGHeroKind::Hua);
        if(Id==TEXT("DuskStreet"))return Deployed.Contains(EEGHeroKind::Pardofelis)&&Deployed.Contains(EEGHeroKind::Sakura)&&Deployed.Contains(EEGHeroKind::Aponia);
        if(Id==TEXT("Kindred"))return Deployed.Contains(EEGHeroKind::VillV)&&Deployed.Contains(EEGHeroKind::Mobius);
        if(Id==TEXT("LastBanquet"))return Deployed.Contains(EEGHeroKind::Aponia)&&Deployed.Contains(EEGHeroKind::Eden)&&Deployed.Contains(EEGHeroKind::Elysia);
        // Team-wide effects remain useful for the next battle even after an empty-board defeat.
        return true;
    };
    TArray<FName> Pool,EffectivePool;
    for(const auto& Entry:UEGStigmataCatalog::GetDefinitions())
    {
        const FName Id=Entry.Id;
        if(Id==TEXT("OriginFlower")||Id==TEXT("Story")||Id==TEXT("Unity")||GetMatch()->HasStigmata(Id))continue;
        if(Id==TEXT("Reverie")&&Stage!=2)continue;
        if(Id==TEXT("Stars")&&(!Held.Contains(EEGHeroKind::Griseo)||!HasForm(EEGHeroKind::Griseo)))continue;
        if(Id==TEXT("Vicissitude")&&(!Held.Contains(EEGHeroKind::Hua)||!HasForm(EEGHeroKind::Hua)))continue;
        if(Id==TEXT("DuskStreet")&&CountHeld({EEGHeroKind::Pardofelis,EEGHeroKind::Sakura,EEGHeroKind::Aponia})<2)continue;
        if(Id==TEXT("Kindred")&&CountHeld({EEGHeroKind::VillV,EEGHeroKind::Mobius})<2)continue;
        if(Id==TEXT("LastBanquet")&&CountHeld({EEGHeroKind::Aponia,EEGHeroKind::Eden,EEGHeroKind::Elysia})<2)continue;
        Pool.Add(Id);if(Effective(Id))EffectivePool.Add(Id);
    }
    const int32 Required=Stage==6?3:2;
    while(StigmataOffer.Num()<Required&&!EffectivePool.IsEmpty())
    {
        const int32 Index=StigmataRandom.RandRange(0,EffectivePool.Num()-1);
        const FName Id=EffectivePool[Index];EffectivePool.RemoveAt(Index);Pool.RemoveSingle(Id);StigmataOffer.Add(Id);
    }
    while(StigmataOffer.Num()<3&&!Pool.IsEmpty()&&Stage!=6)
    {
        const int32 Index=StigmataRandom.RandRange(0,Pool.Num()-1);
        StigmataOffer.Add(Pool[Index]);Pool.RemoveAt(Index);
    }
    // Shuffle so the guaranteed slots do not disclose which candidates were forced.
    for(int32 I=StigmataOffer.Num()-1;I>0;--I)StigmataOffer.Swap(I,StigmataRandom.RandRange(0,I));
}

bool AEGDemoGameMode::ChooseStigmata(FName Id,int32 ExpectedRevision)
{
    if(!HasAuthority()||bChoosingStigmata||bChoosingEndgameRoute||!GetMatch()||!Cards||!GetRoster()||
       GetMatch()->GetPhase()!=EEGMatchPhase::Reward||ExpectedRevision!=StigmaSkipRevision||
       !StigmataOffer.Contains(Id)||GetMatch()->HasStigmata(Id)||!UEGStigmataCatalog::Find(Id))return false;
    TGuardValue<bool> Guard(bChoosingStigmata,true);
    if(!GetMatch()->BeginStigmataCommit(Id))return false;
    FEGScopedRosterMutation RosterMutation(GetRoster());
    bool bApplied=true;int32 GoldAwarded=0;
    if(Id==TEXT("Reverie"))
    {
        const int32 Gold=FMath::Max(1,UEGStigmataBalanceSettings::Get().ReverieGold);
        bApplied=Cards->AwardStigmataGold(Gold);
        if(bApplied)GoldAwarded=Gold;
    }
    else if(Id==TEXT("Stars")||Id==TEXT("Vicissitude"))
    {
        const EEGHeroKind Kind=Id==TEXT("Stars")?EEGHeroKind::Griseo:EEGHeroKind::Hua;
        UEGHeroDefinition* Definition=nullptr;
        if(RunDefinition)for(const auto& Card:RunDefinition->CardPool)
            if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==Kind){Definition=Card->HeroDefinition;break;}
        bApplied=GetRoster()->UnlockForm(Definition);
        if(bApplied)Cards->RefreshHeroPresentations();
    }
    if(!bApplied){GetMatch()->AbortStigmataCommit();SetHint(TEXT("领取失败，请检查资源后选择其他刻印。"));return false;}
    if(!GetMatch()->CommitStigmata(false))return false;
    if(GoldAwarded>0)GetMatch()->RecordStigmataGoldEarned(Id,GoldAwarded);
    StigmataOffer.Reset();
    bChoosingEndgameRoute=false;
    ++StigmaSkipRevision;
    Cards->NotifyStigmataChanged();OnRunViewChanged.Broadcast();
    return true;
}

TArray<FName> AEGDemoGameMode::GetEndgameRouteOffer() const
{
    if(!bChoosingEndgameRoute||!GetMatch()||GetMatch()->GetPhase()!=EEGMatchPhase::Reward)return {};
    return {TEXT("OriginFlower"),TEXT("Story"),TEXT("Unity")};
}

TArray<FEGRouteStigmataPreview> AEGDemoGameMode::GetEndgameRoutePreview(FName RouteId) const
{
    TArray<FEGRouteStigmataPreview> Result;
    if(!GetEndgameRouteOffer().Contains(RouteId)||!GetMatch()||!GetRoster())return Result;
    // Origin now grants the human card to the hand. No inheritance prediction is
    // presented for this route; keep the reflected function safe for old callers.
    if(RouteId==TEXT("OriginFlower"))return Result;
    TSet<EEGHeroKind> Deployed;
    for(const auto& Hero:GetRoster()->GetHeroes())if(Hero.bDeployed&&Hero.Definition)Deployed.Add(Hero.Definition->HeroKind);
    for(const FName Id:GetMatch()->GetStigmataOwned())
    {
        auto& Entry=Result.AddDefaulted_GetRef();Entry.StigmataId=Id;
        bool bConditional=false,bActive=true;
        if(Id==TEXT("Stars")){bConditional=true;bActive=Deployed.Contains(EEGHeroKind::Griseo);}
        else if(Id==TEXT("Vicissitude")){bConditional=true;bActive=Deployed.Contains(EEGHeroKind::Hua);}
        else if(Id==TEXT("DuskStreet")){bConditional=true;bActive=Deployed.Contains(EEGHeroKind::Pardofelis)&&Deployed.Contains(EEGHeroKind::Sakura)&&Deployed.Contains(EEGHeroKind::Aponia);}
        else if(Id==TEXT("Kindred")){bConditional=true;bActive=Deployed.Contains(EEGHeroKind::VillV)&&Deployed.Contains(EEGHeroKind::Mobius);}
        else if(Id==TEXT("LastBanquet")){bConditional=true;bActive=Deployed.Contains(EEGHeroKind::Aponia)&&Deployed.Contains(EEGHeroKind::Eden)&&Deployed.Contains(EEGHeroKind::Elysia);}
        if(bConditional&&!bActive)
        {
            Entry.Status=FText::FromString(TEXT("失效"));
            Entry.Explanation=FText::FromString(TEXT("当前上阵阵容不满足角色条件；最终准备阶段重新满足条件后可恢复。"));
        }
        else if((Id==TEXT("LoveBeginning")||Id==TEXT("Flawless"))&&!Deployed.Contains(EEGHeroKind::Elysia)&&!Deployed.Contains(EEGHeroKind::ElysiaHerrscher))
        {
            Entry.Status=FText::FromString(TEXT("部分生效"));
            Entry.Explanation=FText::FromString(TEXT("全队效果保留，爱莉希雅专属效果需要对应角色上阵。"));
        }
        else if(Deployed.IsEmpty()&&Id!=TEXT("Reverie"))
        {
            Entry.Status=FText::FromString(TEXT("部分生效"));
            Entry.Explanation=FText::FromString(TEXT("需要在最终准备阶段上阵英桀后才能在战斗中生效。"));
        }
        else
        {
            Entry.Status=FText::FromString(TEXT("继续生效"));
            Entry.Explanation=FText::FromString(Id==TEXT("Reverie")?TEXT("最终准备阶段仍可使用免费刷新。"):TEXT("按当前阵容与刻印各自的战斗条件生效。"));
        }
    }
    return Result;
}

bool AEGDemoGameMode::ChooseEndgameRoute(FName Id,int32 ExpectedRevision)
{
    if(!HasAuthority()||bChoosingStigmata||!bChoosingEndgameRoute||!GetMatch()||!Cards||!GetRoster()||
       GetMatch()->GetPhase()!=EEGMatchPhase::Reward||ExpectedRevision!=StigmaSkipRevision||!GetEndgameRouteOffer().Contains(Id)||
       !GetMatch()->GetEndgameRoute().IsNone())return false;
    TGuardValue<bool> Guard(bChoosingStigmata,true);
    if(!GetMatch()->BeginStigmataCommit(Id))return false;
    FEGScopedRosterMutation RosterMutation(GetRoster());
    bool bApplied=true;FString Error;
    if(Id==TEXT("Story"))
    {
        TArray<UEGCardDefinition*> Grants;TSet<EEGHeroKind> Kinds;
        if(RunDefinition)for(const auto& Card:RunDefinition->CardPool)
            if(Card&&Card->HeroDefinition&&!Kinds.Contains(Card->HeroDefinition->HeroKind))
            {Grants.Add(Card);Kinds.Add(Card->HeroDefinition->HeroKind);}
        const TSet<EEGHeroKind> Required{EEGHeroKind::Elysia,EEGHeroKind::Pardofelis,EEGHeroKind::Sakura,
            EEGHeroKind::Hua,EEGHeroKind::Griseo,EEGHeroKind::Aponia,EEGHeroKind::VillV,EEGHeroKind::Mobius,EEGHeroKind::Eden};
        bApplied=Kinds.Num()==Required.Num();
        for(const auto Kind:Required)bApplied=bApplied&&Kinds.Contains(Kind);
        const int32 Minimum=FMath::Max(1,UEGStigmataBalanceSettings::Get().StoryMinimumLevel);
        if(bApplied)bApplied=Cards->GrantCardsBatch(Grants,false,Minimum);
        if(bApplied){Cards->SetPopulationFloor(9,false);Cards->PromoteHeldHeroesToLevel(Minimum);}
    }
    else if(Id==TEXT("OriginFlower"))
    {
        auto* Human=HumanFusionCard.LoadSynchronous();
        auto* HumanDefinition=Human?Human->HeroDefinition.Get():nullptr;
        auto* BaseElysia=RunDefinition&&RunDefinition->StartingCard?RunDefinition->StartingCard->HeroDefinition.Get():nullptr;
        TStrongObjectPtr<UEGCardDefinition> PrivateCard;
        if(HumanDefinition&&(!HumanDefinition->bUseSplitConfiguration||HumanDefinition->StatsDefinition))
        {
            const FEGStatBlock& Growth=HumanDefinition->GetGrowthPerLevel();
            const bool bMissingGrowth=FMath::IsNearlyZero(Growth.MaxHealth)&&FMath::IsNearlyZero(Growth.MaxMana)&&
                FMath::IsNearlyZero(Growth.AttackPower)&&FMath::IsNearlyZero(Growth.AttackSpeed)&&FMath::IsNearlyZero(Growth.Defense);
            if(bMissingGrowth)
            {
                if(!BaseElysia||BaseElysia->HeroKind!=EEGHeroKind::Elysia||
                   (BaseElysia->bUseSplitConfiguration&&!BaseElysia->StatsDefinition))
                {
                    Error=TEXT("普通爱莉的等级成长配置不可用，无法完成人律汇聚。");
                    bApplied=false;
                }
                else
                {
                    FEGStatBlock ReferenceGrowth=UEGHeroDefinition::MakeZeroGrowth();
                    const auto& GrowthSettings=UEGHeroGrowthSettings::Get();
                    ReferenceGrowth.MaxHealth=GrowthSettings.HumanFallbackHealthGrowth;
                    ReferenceGrowth.AttackPower=GrowthSettings.HumanFallbackAttackGrowth;
                    ReferenceGrowth.Defense=GrowthSettings.HumanFallbackDefenseGrowth;
                    if(!FMath::IsFinite(ReferenceGrowth.MaxHealth)||!FMath::IsFinite(ReferenceGrowth.AttackPower)||
                       !FMath::IsFinite(ReferenceGrowth.Defense)||ReferenceGrowth.MaxHealth<=0.f||ReferenceGrowth.AttackPower<=0.f)
                    {
                        Error=TEXT("普通爱莉的等级成长配置无效，无法完成人律汇聚。");
                        bApplied=false;
                    }
                    else
                    {
                        PrivateCard.Reset(DuplicateObject<UEGCardDefinition>(Human,this));
                        auto* PrivateHero=PrivateCard.IsValid()?DuplicateObject<UEGHeroDefinition>(HumanDefinition,PrivateCard.Get()):nullptr;
                        if(PrivateHero)PrivateCard->HeroDefinition=PrivateHero;
                        if(PrivateHero&&HumanDefinition->bUseSplitConfiguration)
                            PrivateHero->StatsDefinition=DuplicateObject<UEGHeroStatsDefinition>(HumanDefinition->StatsDefinition,PrivateHero);
                        if(!PrivateHero||(HumanDefinition->bUseSplitConfiguration&&!PrivateHero->StatsDefinition))
                        {
                            Error=TEXT("人律私有等级成长配置创建失败。");
                            bApplied=false;
                        }
                        else
                        {
                            FEGStatBlock& PrivateGrowth=PrivateHero->bUseSplitConfiguration?
                                PrivateHero->StatsDefinition->GrowthPerLevel:PrivateHero->GrowthPerLevel;
                            PrivateGrowth.MaxHealth=ReferenceGrowth.MaxHealth;
                            PrivateGrowth.AttackPower=ReferenceGrowth.AttackPower;
                            PrivateGrowth.Defense=ReferenceGrowth.Defense;
                            Human=PrivateCard.Get();
                        }
                    }
                }
            }
        }
        if(bApplied)bApplied=Cards->FuseDeployedHeroes(Human,false,&Error);
    }
    if(!bApplied)
    {
        GetMatch()->AbortStigmataCommit();
        SetHint(Error.IsEmpty()?FString(TEXT("领取失败，请检查资源后选择其他路线。")):Error);
        return false;
    }
    if(!GetMatch()->CommitEndgameRoute(Id))
    {
        UE_LOG(LogTemp,Error,TEXT("Endgame route could not commit after validated effect"));return false;
    }
    bChoosingEndgameRoute=false;
    ++StigmaSkipRevision;
    Cards->NotifyStigmataChanged();OnRunViewChanged.Broadcast();
    return true;
}
void AEGDemoGameMode::ResetPhaseClock()
{
    ResetSettlementClock();
    PreparationRealSeconds=0.f;
    CombatRealSeconds=0.f;
    CorruptionPeriodStartRealSeconds=0.f;
    LastAudioClockSeconds=GetWorld()?GetWorld()->GetAudioTimeSeconds():-1.0;
    ClockStageNumber=INDEX_NONE;
    bPreparationAutoStartAttempted=false;
    RunningRoundId.Invalidate();
}

float AEGDemoGameMode::GetSettlementElapsedSeconds() const
{
    if(!bSettlementClockActive||SettlementStartAudioSeconds<0.0||!GetWorld())return 0.f;
    return static_cast<float>(FMath::Max(0.0,GetWorld()->GetAudioTimeSeconds()-SettlementStartAudioSeconds));
}

void AEGDemoGameMode::ResetSettlementClock()
{
    if(bSettlementClockActive&&FMath::IsNearlyZero(GetActorTickInterval()))
        PrimaryActorTick.UpdateTickIntervalAndCoolDown(PreviousSettlementTickInterval);
    bSettlementClockActive=false;
    SettlementStartAudioSeconds=-1.0;
    PreviousSettlementTickInterval=0.f;
}

void AEGDemoGameMode::RestoreOvertimeDilation()
{
    if(!bOwnsOvertimeDilation)return;
    if(UWorld* World=GetWorld())
    {
        const float Current=UGameplayStatics::GetGlobalTimeDilation(World);
        if(FMath::IsNearlyEqual(Current,AppliedGlobalTimeDilation,.001f))
            UGameplayStatics::SetGlobalTimeDilation(World,OriginalGlobalTimeDilation);
    }
    bOwnsOvertimeDilation=false;
}

FEGPhaseClockView AEGDemoGameMode::GetPhaseClock() const
{
    FEGPhaseClockView View;
    if(!GetMatch()||!RunDefinition||!RunDefinition->bSevenStageRules)return View;
    View.Phase=GetMatch()->GetPhase();
    const auto* Config=GetDefault<UEGSevenStageDefinition>();
    const int32 Stage=GetMatch()->GetStageNumber();
    const int32 Index=Stage-1;
    if(View.Phase==EEGMatchPhase::Preparation)
    {
        const float Duration=FMath::Max(.01f,Config->PreparationRealSeconds);
        View.bHasCountdown=bStageReady&&!IsPresentationInputBlocked();
        View.RemainingRealSeconds=FMath::Max(0.f,Duration-(ClockStageNumber==Stage?PreparationRealSeconds:0.f));
        View.Normalized=View.RemainingRealSeconds/Duration;
    }
    else if(View.Phase==EEGMatchPhase::Combat)
    {
        const bool bBoss=Stage==7;
        if(bBoss)
        {
            View.Speed=1.f;View.bOvertime=BattleSeconds>=60.f;
            View.bHasCountdown=!View.bOvertime;
            View.RemainingRealSeconds=FMath::Max(0.f,60.f-BattleSeconds);
            View.Normalized=View.bOvertime?1.f:View.RemainingRealSeconds/60.f;
            return View;
        }
        const float Normal=bBoss?FMath::Max(.01f,Config->BossNormalRealSeconds):
            RunDefinition->Stages.IsValidIndex(Index)?FMath::Max(.01f,RunDefinition->Stages[Index].StageBattleTimeLimit):25.f;
        View.bOvertime=CombatRealSeconds>=Normal;
        View.Speed=View.bOvertime?FMath::Max(.01f,Config->OvertimeSpeed):1.f;
        if(bBoss&&BattleRules&&BattleRules->IsCombatRunning())
        {
            const float GameRemaining=FMath::Max(0.f,105.f+15.f*BossCorruptionTicks-BattleSeconds);
            const float Effective=GetWorld()&&GetWorld()->GetWorldSettings()?
                FMath::Max(.0001f,GetWorld()->GetWorldSettings()->GetEffectiveTimeDilation()):1.f;
            const float BeforeOvertime=FMath::Max(0.f,Normal-CombatRealSeconds);
            const float GameBeforeOvertime=BeforeOvertime*Effective;
            View.bHasCorruptionCountdown=true;
            View.NextCorruptionRemaining=GameRemaining<=GameBeforeOvertime?GameRemaining/Effective:
                BeforeOvertime+(GameRemaining-GameBeforeOvertime)/
                    (Effective*(View.bOvertime?1.f:FMath::Max(.01f,Config->OvertimeSpeed)));
            const float PeriodElapsed=FMath::Max(0.f,CombatRealSeconds-CorruptionPeriodStartRealSeconds);
            const float PeriodDuration=PeriodElapsed+View.NextCorruptionRemaining;
            View.CorruptionNormalized=PeriodDuration>SMALL_NUMBER?
                FMath::Clamp(View.NextCorruptionRemaining/PeriodDuration,0.f,1.f):0.f;
        }
        if(View.bOvertime&&bBoss)
        {
            View.bHasCountdown=false;
            View.Normalized=1.f;
        }
        else
        {
            const float Duration=View.bOvertime?FMath::Max(.01f,Config->OvertimeRealSeconds):Normal;
            const float Elapsed=View.bOvertime?CombatRealSeconds-Normal:CombatRealSeconds;
            View.bHasCountdown=true;
            View.RemainingRealSeconds=FMath::Max(0.f,Duration-Elapsed);
            View.Normalized=FMath::Clamp(View.RemainingRealSeconds/Duration,0.f,1.f);
        }
    }
    else if(View.Phase==EEGMatchPhase::Settlement)
    {
        View.bHasCountdown=bSettlementClockActive;
        View.RemainingRealSeconds=FMath::Max(0.f,SettlementDurationSeconds-GetSettlementElapsedSeconds());
        View.Normalized=FMath::Clamp(View.RemainingRealSeconds/SettlementDurationSeconds,0.f,1.f);
    }
    return View;
}
