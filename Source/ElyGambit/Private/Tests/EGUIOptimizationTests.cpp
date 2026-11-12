#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroCardInfoDefinition.h"
#include "UI/EGUnitInspectionView.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGRichText.h"
#include "UI/Widget/EGWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUIVitalsDetails,"ElyGambit.UI.Optimization.VitalsDetailsAndInspection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGUIVitalsDetails::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* GM=W->GetAuthGameMode<AEGChessGameMode>();auto* Roster=GM?GM->GetRoster():nullptr;
 if(!TestNotNull(TEXT("Roster"),Roster))return false;
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());Definition->HeroId=TEXT("UIDetailsFixture");Definition->DisplayName=FText::FromString(TEXT("Fixture"));
 Definition->BaseStats.MaxHealth=100;Definition->BaseStats.MaxMana=50;
 auto* Info=NewObject<UEGHeroCardInfoDefinition>(Definition);Info->Effect=FText::FromString(TEXT("Static skill effect"));Definition->CardInformation=Info;
 const FGuid HeroId=Roster->AddHero(Definition);
 if(!TestTrue(TEXT("Fixture roster identity"),HeroId.IsValid()))return false;
 auto* Vitals=CreateWidget<UEGUnitVitals>(GI.Get(),UEGUnitVitals::ResolveWidgetClass());
 if(!TestNotNull(TEXT("Vitals widget"),Vitals))return false;
 TStrongObjectPtr<UEGUnitVitals> KeepVitals(Vitals);Vitals->TakeWidget();Vitals->SetPreparationHero(Roster,HeroId);
 auto* Tooltip=Cast<UEGHeroInfoWidget>(Vitals->GetToolTip());
 if(!TestNotNull(TEXT("Live skill tooltip"),Tooltip))return false;
 auto* Effect=Cast<UEGRichTextBlock>(Tooltip->GetWidgetFromName(TEXT("SkillEffect")));
 auto* Extra=Cast<UEGRichTextBlock>(Tooltip->GetWidgetFromName(TEXT("SkillExtra")));
 if(!TestNotNull(TEXT("Tooltip effect"),Effect)||!TestNotNull(TEXT("Tooltip dynamic details"),Extra))return false;
 TestEqual(TEXT("Static skill text remains once in effect"),Effect->GetPlainText().ToString(),FString(TEXT("Static skill effect")));
 TestTrue(TEXT("Preparation reports level"),Extra->GetPlainText().ToString().Contains(TEXT("Lv.1")));
 TestFalse(TEXT("Dynamic detail does not repeat static effect"),Extra->GetPlainText().ToString().Contains(TEXT("Static skill effect")));
 Roster->ApplySameNameCard(HeroId);Roster->ApplySameNameCard(HeroId);
 TestTrue(TEXT("Awakening updates tooltip"),Extra->GetPlainText().ToString().Contains(TEXT("已觉醒")));
 const auto Preparation=FEGUnitInspectionView::Capture(nullptr,Roster,HeroId);
 TestTrue(TEXT("Inspection reflects authoritative roster"),Preparation.Definition==Definition&&Preparation.CardsUsed==3&&Preparation.bAwakened&&!Preparation.bCombat);
 auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestNotNull(TEXT("Combat unit"),Unit)||!TestTrue(TEXT("Initialize combat unit"),Unit->InitializeUnit(Definition,HeroId,EEGFaction::Player,2)))return false;
 Vitals->SetUnit(Unit);
 Tooltip=Cast<UEGHeroInfoWidget>(Vitals->GetToolTip());Extra=Tooltip?Cast<UEGRichTextBlock>(Tooltip->GetWidgetFromName(TEXT("SkillExtra"))):nullptr;
 TestTrue(TEXT("Combat identity is shown in tooltip"),Extra&&Extra->GetPlainText().ToString().Contains(TEXT("Lv.2")));
 const auto Combat=FEGUnitInspectionView::Capture(Unit,Roster,HeroId);
 TestTrue(TEXT("Combat snapshot uses unit identity and roster card count"),Combat.Definition==Definition&&Combat.bCombat&&Combat.Level==2&&Combat.CardsUsed==3);
 Vitals->SetUnit(nullptr);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUIStageNodes,"ElyGambit.UI.Optimization.StageNodeGrowthAndShrink",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGUIStageNodes::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* HUDClass=LoadClass<UEGMainHUD>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_BoardHUD.WBP_BoardHUD_C"));
 if(!TestNotNull(TEXT("Actual board HUD class"),HUDClass))return false;
 if(!TestFalse(TEXT("Board HUD class is concrete"),HUDClass->HasAnyClassFlags(CLASS_Abstract)))return false;
 auto* HUD=CreateWidget<UEGMainHUD>(GI.Get(),HUDClass);
 if(!TestNotNull(TEXT("HUD"),HUD)||!TestNotNull(TEXT("HUD tree"),HUD->WidgetTree.Get()))return false;
 TStrongObjectPtr<UEGMainHUD> KeepHUD(HUD);
 auto* Nodes=Cast<UCanvasPanel>(HUD->GetWidgetFromName(TEXT("StageNodes")));
 if(!Nodes)
 {
  // The current board HUD can omit the future stage panel; construct the
  // seven-node authored shape on this widget instance without saving an asset.
  AddInfo(TEXT("Board HUD has no stage panel; using an instance-only seven-node fixture."));
  Nodes=HUD->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("StageNodes"));HUD->WidgetTree->RootWidget=Nodes;
  for(int32 I=1;I<=7;++I)
  {
   for(const TCHAR* Prefix:{TEXT("StageRing_"),TEXT("StageLabel_"),TEXT("StageCurrent_")})
   {
    const FName Name(*FString::Printf(TEXT("%s%d"),Prefix,I));
    UWidget* Node=FCString::Strcmp(Prefix,TEXT("StageLabel_"))==0
     ?static_cast<UWidget*>(HUD->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name))
     :static_cast<UWidget*>(HUD->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Name));
    Nodes->AddChildToCanvas(Node);
   }
   if(I<7)Nodes->AddChildToCanvas(HUD->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("StageLink_%d"),I-1))));
  }
 }
 FEGMatchView Match;Match.bHasMatch=true;Match.StageCount=2;HUD->ApplyMatchView(Match);
 Match.StageCount=9;Match.StageNumber=9;HUD->ApplyMatchView(Match);
 for(const TCHAR* Name:{TEXT("StageRing_8"),TEXT("StageRing_9"),TEXT("StageLink_6"),TEXT("StageLink_7")})
 {
  auto* Node=HUD->GetWidgetFromName(Name);
  // This instance-only fixture does not construct a Slate viewport. UE 5.8
  // IsVisible() requires a cached SWidget, so assert the authored UMG state.
  TestTrue(FString(TEXT("Growing creates shown UMG node "))+Name,Node&&Node->GetVisibility()==ESlateVisibility::HitTestInvisible);
 }
 Match.StageCount=2;Match.StageNumber=1;HUD->ApplyMatchView(Match);
 for(const TCHAR* Name:{TEXT("StageRing_8"),TEXT("StageRing_9"),TEXT("StageLink_6"),TEXT("StageLink_7")})
 {auto* Node=HUD->GetWidgetFromName(Name);TestTrue(FString(TEXT("Shrinking hides "))+Name,Node&&Node->GetVisibility()==ESlateVisibility::Collapsed);}
 return true;
}
#endif
