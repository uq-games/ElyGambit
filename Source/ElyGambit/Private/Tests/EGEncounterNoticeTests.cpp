#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGPieceTestGameMode.h"
#include "Placement/EGDeploymentBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "UI/Widget/EGEncounterNoticeWidget.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGVitalsGraphics.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEncounterPreparationNoticeTest,
    "ElyGambit.UI.EncounterPreparationNotice", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEGEncounterPreparationNoticeTest::RunTest(const FString& Parameters)
{
    FEGEncounterNoticeState State;
    TestFalse(TEXT("ordinary stage does not show warning"),State.Update(true,2,EEGMatchPhase::Preparation));
    TestTrue(TEXT("third stage shows warning"),State.Update(true,3,EEGMatchPhase::Preparation));
    TestFalse(TEXT("repeated preparation broadcasts do not restart warning"),State.Update(true,3,EEGMatchPhase::Preparation));
    TestFalse(TEXT("combat does not show preparation warning"),State.Update(true,3,EEGMatchPhase::Combat));
    TestFalse(TEXT("settlement does not show preparation warning"),State.Update(true,3,EEGMatchPhase::Settlement));
    TestTrue(TEXT("retrying same stage shows warning again"),State.Update(true,3,EEGMatchPhase::Preparation));
    TestFalse(TEXT("fourth stage does not show warning"),State.Update(true,4,EEGMatchPhase::Preparation));
    TestTrue(TEXT("sixth stage shows warning"),State.Update(true,6,EEGMatchPhase::Preparation));
    TestFalse(TEXT("boss stage does not show elite warning"),State.Update(true,7,EEGMatchPhase::Preparation));
    FEGEncounterNoticeState OtherRun;
    TestFalse(TEXT("non-seven-stage run never displays the elite warning"),OtherRun.Update(false,3,EEGMatchPhase::Preparation));
    TestTrue(TEXT("enabling seven-stage run while preparing stage three displays warning"),OtherRun.Update(true,3,EEGMatchPhase::Preparation));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEncounterBorderlessBossUITest,
    "ElyGambit.UI.EncounterBorderlessBoss", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGEncounterBorderlessBossUITest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
    UWorld* World=GI->GetWorld();
    if(!TestNotNull(TEXT("Standalone UI world"),World))return false;
    ON_SCOPE_EXIT {World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    // Controllers join World->PlayerControllerList in PostInitializeComponents,
    // which only runs for a spawned actor after the world initializes actors.
    // FLocalPlayerContext resolves its owner through that world list.
    World->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
    FURL URL;World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();
    if(!TestTrue(TEXT("UI world has initialized its actors"),World->AreActorsInitialized()))return false;
    auto* Controller=World->SpawnActor<APlayerController>();
    if(!TestNotNull(TEXT("Local owning controller"),Controller))return false;
    TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));Controller->SetPlayer(LocalPlayer.Get());
    if(!TestTrue(TEXT("Widget owner is a local controller"),Controller->IsLocalController()))return false;
    if(!TestTrue(TEXT("World resolves the owning controller"),LocalPlayer->GetPlayerController(World)==Controller))return false;
    // The native widget builds its tree in NativeOnInitialized. A game
    // instance without a local player skips that UE lifecycle callback.
    TStrongObjectPtr<UEGEncounterNoticeWidget> View(CreateWidget<UEGEncounterNoticeWidget>(Controller));
    if(!TestNotNull(TEXT("Encounter view"),View.Get()))return false;
    if(!TestTrue(TEXT("Encounter view has a valid local-player context"),View->GetPlayerContext().IsValid()))return false;
    const TSharedRef<SWidget> KeepSlate=View->TakeWidget();
    auto* Notice=Cast<UBorder>(View->GetWidgetFromName(TEXT("ElitePreparationNotice")));
    auto* BossPanel=Cast<UCanvasPanel>(View->GetWidgetFromName(TEXT("CorruptionBossPanel")));
    auto* WallPanel=Cast<UCanvasPanel>(View->GetWidgetFromName(TEXT("CorruptionBarrierPanel")));
    auto* BossHealth=Cast<UEGVitalsBar>(View->GetWidgetFromName(TEXT("CorruptionBossHealth")));
    auto* BossTitle=Cast<UTextBlock>(View->GetWidgetFromName(TEXT("CorruptionBossTitle")));
    auto* WallHealth=Cast<UEGVitalsBar>(View->GetWidgetFromName(TEXT("CorruptionBarrierHealth")));
    auto* Clock=Cast<UTextBlock>(View->GetWidgetFromName(TEXT("CorruptionBarrierTime")));
    if(!TestNotNull(TEXT("Elite notice"),Notice)||!TestNotNull(TEXT("Borderless boss view"),BossPanel)||
       !TestNotNull(TEXT("Separate wall view"),WallPanel)||!TestNotNull(TEXT("Boss health"),BossHealth)||!TestNotNull(TEXT("Boss title"),BossTitle)||
       !TestNotNull(TEXT("Wall durability"),WallHealth)||!TestNotNull(TEXT("Wall countdown"),Clock))return false;
    TestTrue(TEXT("Elite notice draws no background or border"),Notice->Background.DrawAs==ESlateBrushDrawType::NoDrawType);
    TestTrue(TEXT("Whole encounter layer passes input through"),View->GetVisibility()==ESlateVisibility::HitTestInvisible);
    TestTrue(TEXT("Wall view starts hidden outside barrier"),WallPanel->GetVisibility()==ESlateVisibility::Collapsed);
    UClass* VitalsClass=UEGUnitVitals::ResolveWidgetClass();
    const auto* VitalsDefaults=VitalsClass?VitalsClass->GetDefaultObject<UEGUnitVitals>():nullptr;
    const FEGUnitVitalsStyle Style=VitalsDefaults?VitalsDefaults->VitalsStyle:FEGUnitVitalsStyle();
    TestTrue(TEXT("Boss uses the same enemy health fill as units"),BossHealth->GetFillColorAndOpacity().Equals(Style.Enemy));
    TestTrue(TEXT("Wall uses the same enemy health fill as units"),WallHealth->GetFillColorAndOpacity().Equals(Style.Enemy));
    TestTrue(TEXT("Both bars use the unit health highlight"),BossHealth->HighlightColor.Equals(Style.EnemyHighlight)&&WallHealth->HighlightColor.Equals(Style.EnemyHighlight));
    TestTrue(TEXT("Both bars use the unit health track and intrinsic rim"),BossHealth->TrackColor.Equals(Style.TrackBottom)&&WallHealth->TrackColor.Equals(Style.TrackBottom)&&BossHealth->RimColor.Equals(Style.Rim)&&WallHealth->RimColor.Equals(Style.Rim));
    auto* BossSlot=Cast<UCanvasPanelSlot>(BossPanel->Slot);
    auto* WallSlot=Cast<UCanvasPanelSlot>(WallPanel->Slot);
    if(TestNotNull(TEXT("Boss placement"),BossSlot)&&TestNotNull(TEXT("Wall placement"),WallSlot))
    {
        TestTrue(TEXT("Boss bar panel is fixed at top centre"),BossSlot->GetAnchors().Minimum.Equals(FVector2D(.5f,0.f))&&BossSlot->GetAnchors().Maximum.Equals(FVector2D(.5f,0.f)));
        TestTrue(TEXT("Wall bar is anchored above projected wall"),WallSlot->GetAlignment().Equals(FVector2D(.5f,1.f)));
    }
    TestTrue(TEXT("Boss and wall bars pass input through"),BossHealth->GetVisibility()==ESlateVisibility::HitTestInvisible&&WallHealth->GetVisibility()==ESlateVisibility::HitTestInvisible);
    const auto* TitleSlot=Cast<UCanvasPanelSlot>(BossTitle->Slot);
    const auto* HealthSlot=Cast<UCanvasPanelSlot>(BossHealth->Slot);
    if(TestNotNull(TEXT("Boss title placement"),TitleSlot)&&TestNotNull(TEXT("Boss health placement"),HealthSlot))
        TestTrue(TEXT("Boss title and health retain vertical clearance"),TitleSlot->GetPosition().Y+TitleSlot->GetSize().Y+8.f<=HealthSlot->GetPosition().Y);
    TestNull(TEXT("No old phase-one explanation"),View->GetWidgetFromName(TEXT("CorruptionFirstHealthLabel")));
    TestNull(TEXT("No old phase-two explanation"),View->GetWidgetFromName(TEXT("CorruptionSecondHealthLabel")));
    TestNull(TEXT("No old barrier explanation"),View->GetWidgetFromName(TEXT("CorruptionBarrierCaption")));
    View->ApplyMatchView(true,3,EEGMatchPhase::Preparation);
    TestTrue(TEXT("Elite notice remains noninteractive and visible"),Notice->GetVisibility()==ESlateVisibility::HitTestInvisible);
    View->ApplyMatchView(true,3,EEGMatchPhase::Combat);
    TestTrue(TEXT("Notice hides when combat begins"),Notice->GetVisibility()==ESlateVisibility::Collapsed);
    View->ApplyMatchView(true,3,EEGMatchPhase::Settlement);
    TestTrue(TEXT("Wall UI hides outside combat"),WallPanel->GetVisibility()==ESlateVisibility::Collapsed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEncounterBossHeadupTest,
    "ElyGambit.UI.EncounterBossHeadup", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGEncounterBossHeadupTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine)); GI->InitializeStandalone();
    UWorld* World=GI->GetWorld();
    if(!TestNotNull(TEXT("Standalone combat world"),World))return false;
    ON_SCOPE_EXIT {World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    World->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
    FURL URL;World->SetGameMode(URL);
    auto* Board=World->SpawnActor<AEGDeploymentBoard>(); Board->Columns=7;Board->Rows=7;Board->PlayerRows=3;
    World->InitializeActorsForPlay(URL);World->BeginPlay();
    auto* Mode=World->GetAuthGameMode<AEGPieceTestGameMode>();
    if(!TestNotNull(TEXT("Combat mode"),Mode)||!TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return false;
    Mode->BattleRules->CompleteOpeningForTests();
    AEGChessUnit* Enemy=nullptr;AEGChessUnit* Ally=nullptr;
    for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
    {
        if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Ally=Unit;
        else if(!Enemy)Enemy=Unit;
    }
    if(!TestNotNull(TEXT("Enemy fixture"),Enemy)||!TestNotNull(TEXT("Ally fixture"),Ally))return false;
    auto* Controller=World->SpawnActor<APlayerController>();
    if(!TestNotNull(TEXT("Local owning controller"),Controller))return false;
    TStrongObjectPtr<ULocalPlayer> LocalPlayer(NewObject<ULocalPlayer>(GEngine));Controller->SetPlayer(LocalPlayer.Get());
    if(!TestTrue(TEXT("Widget owner is a local controller"),Controller->IsLocalController()))return false;
    TStrongObjectPtr<UEGUnitVitals> View(CreateWidget<UEGUnitVitals>(Controller));
    if(!TestNotNull(TEXT("Unit vitals"),View.Get()))return false;
    const TSharedRef<SWidget> KeepSlate=View->TakeWidget();View->SetUnit(Enemy);
    auto* Combat=Cast<UCanvasPanel>(View->GetWidgetFromName(TEXT("CombatGroup")));
    if(!TestNotNull(TEXT("Head-up combat group"),Combat))return false;
    TestTrue(TEXT("Ordinary enemy keeps head-up vitals"),Combat->GetVisibility()==ESlateVisibility::HitTestInvisible);
    auto* Rules=UEGCorruptionCombatComponent::AttachCopy(Enemy,Mode->BattleRules,EEGCorruptionRole::Ranged);
    if(!TestNotNull(TEXT("Elite copy rules"),Rules))return false;
    View->SynchronizePresentationState();
    TestTrue(TEXT("Corrupted elite keeps head-up vitals"),Combat->GetVisibility()==ESlateVisibility::HitTestInvisible);
    Rules=UEGCorruptionCombatComponent::AttachBoss(Enemy,Mode->BattleRules,1000.f,100.f,50);
    if(!TestNotNull(TEXT("Boss rules attached after view binds"),Rules))return false;
    View->SynchronizePresentationState();
    TestTrue(TEXT("Boss hides only duplicate head-up vitals"),Combat->GetVisibility()==ESlateVisibility::Collapsed);
    TestTrue(TEXT("Outer view stays ticking and noninteractive"),View->GetVisibility()==ESlateVisibility::HitTestInvisible);
    View->SetUnit(Ally);View->SynchronizePresentationState();
    TestTrue(TEXT("Reused view restores normal unit vitals"),Combat->GetVisibility()==ESlateVisibility::HitTestInvisible);
    return true;
}
#endif
