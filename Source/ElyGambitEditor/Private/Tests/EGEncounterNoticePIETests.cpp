#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGEncounterNoticeWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/WidgetController/EGOverlayWidgetController.h"

namespace
{
class FEncounterNoticePIE final : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    int32 Step=0;
    double Deadline=0.;
    double CheckAfter=0.;
public:
    explicit FEncounterNoticePIE(FAutomationTestBase* InTest):Test(InTest){}
    virtual bool Update() override
    {
        if(Deadline==0.)Deadline=FPlatformTime::Seconds()+90.;
        if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Encounter UI PIE timed out"));return true;}
        UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
        auto* PC=World?World->GetFirstPlayerController():nullptr;
        auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
        auto* Controller=HUD?HUD->GetOverlayWidgetController():nullptr;
        if(!Controller||!HUD->GetMainWidget())return false;
        TArray<UUserWidget*> Views;
        UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Views,UEGEncounterNoticeWidget::StaticClass(),false);
        UEGEncounterNoticeWidget* Notice=nullptr;
        for(auto* View:Views)if(View&&View->GetOwningPlayer()==PC){Notice=Cast<UEGEncounterNoticeWidget>(View);break;}
        if(!Notice)return false;
        auto* Panel=Notice->GetWidgetFromName(TEXT("ElitePreparationNotice"));
        auto* Text=Cast<UTextBlock>(Notice->GetWidgetFromName(TEXT("ElitePreparationNoticeText")));
        if(!Test->TestNotNull(TEXT("Preparation notice panel"),Panel)||!Test->TestNotNull(TEXT("Preparation notice text"),Text))return true;
        if(Step==0)
        {
            Test->TestTrue(TEXT("Notice layer never intercepts pointer input"),Notice->GetVisibility()==ESlateVisibility::HitTestInvisible);
            FEGMatchView View=Controller->GetMatchView(); View.bRoundShopRun=true; View.StageCount=7; View.StageNumber=3; View.Phase=EEGMatchPhase::Preparation;
            Controller->OnMatchViewChanged.Broadcast(View);
            Test->TestTrue(TEXT("Elite warning visible"),Panel->GetVisibility()==ESlateVisibility::HitTestInvisible);
            Test->TestEqual(TEXT("Elite warning wording"),Text->GetText().ToString(),FString(TEXT("侵蚀之律者侵蚀了我方英桀！")));
            auto* StageButton=Cast<UButton>(HUD->GetMainWidget()->GetWidgetFromName(TEXT("Button_Stage")));
            if(Test->TestNotNull(TEXT("Stage preview button"),StageButton))StageButton->OnClicked.Broadcast();
            auto* CopyLabel=HUD->GetMainWidget()->GetWidgetFromName(TEXT("StageSimpleCopyLabel_3"));
            if(Test->TestNotNull(TEXT("Stage three has a re-readable copy label"),CopyLabel))
                Test->TestTrue(TEXT("Copy tooltip explains the original remains"),CopyLabel->GetToolTipText().ToString().Contains(TEXT("我方英桀仍然保留")));
            const auto Previews=Controller->GetStagePreviews();
            if(Test->TestTrue(TEXT("Seven stage previews exist"),Previews.Num()==7))
            {
                const auto& Boss=Previews[6];
                Test->TestTrue(TEXT("Last stage is the corruption boss"),Boss.bCorruptionBossEncounter);
                Test->TestFalse(TEXT("Boss does not show a periodic corruption increase"),Boss.bPeriodicCorruption||Boss.bHasCorruptionGain);
                Test->TestEqual(TEXT("Boss corruption increase is zero"),Boss.CorruptionGain,0);
                Test->TestTrue(TEXT("Boss explanation describes independent pools and enrage"),Boss.Details.ToString().Contains(TEXT("两条独立生命"))&&Boss.Details.ToString().Contains(TEXT("60 秒")));
                Test->TestTrue(TEXT("Normal stage keeps its loss corruption rule"),Previews[2].bHasCorruptionGain&&Previews[2].CorruptionGain==10);
            }
            auto* BossLabel=HUD->GetMainWidget()->GetWidgetFromName(TEXT("StageSimpleCopyLabel_7"));
            if(Test->TestNotNull(TEXT("Boss has a readable encounter label"),BossLabel))
                Test->TestTrue(TEXT("Boss tooltip describes wall and enrage"),BossLabel->GetToolTipText().ToString().Contains(TEXT("壁垒"))&&BossLabel->GetToolTipText().ToString().Contains(TEXT("狂暴")));
            CheckAfter=FPlatformTime::Seconds()+4.3; Step=1; return false;
        }
        if(Step==1)
        {
            if(FPlatformTime::Seconds()<CheckAfter)return false;
            Test->TestTrue(TEXT("Warning auto-hides after four seconds"),Panel->GetVisibility()==ESlateVisibility::Collapsed);
            FEGMatchView View=Controller->GetMatchView(); View.bRoundShopRun=true; View.StageCount=7; View.StageNumber=6; View.Phase=EEGMatchPhase::Preparation;
            Controller->OnMatchViewChanged.Broadcast(View);
            Test->TestTrue(TEXT("Sixth stage displays warning again"),Panel->GetVisibility()==ESlateVisibility::HitTestInvisible);
            View.Phase=EEGMatchPhase::Combat; Controller->OnMatchViewChanged.Broadcast(View);
            Test->TestTrue(TEXT("Warning clears immediately on combat"),Panel->GetVisibility()==ESlateVisibility::Collapsed);
            return true;
        }
        return true;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEncounterNoticePIETest,"ElyGambit.Integration.EncounterNoticePIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEncounterNoticePIETest::RunTest(const FString&)
{
    if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Encounter UI test requires an idle editor"));return false;}
    ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
    ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
    ADD_LATENT_AUTOMATION_COMMAND(FEncounterNoticePIE(this));
    ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
    return true;
}
#endif
