#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGRichText.h"
#include "Components/Button.h"
#include "UObject/UnrealType.h"

namespace
{
class FPardoStartPIE final : public IAutomationLatentCommand
{
public:
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ClientSize(FVector2D(1920,1080)).AutoCenter(EAutoCenter::None)
   .CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

class FPardoPreparationCheck final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 int32 Step=0,Index=0;
 double Deadline=0,ChangedAt=0;
 FGuid HeroId;
 TWeakObjectPtr<UEGCardDefinition> Card;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 FString Directory,Pending;
 bool bRequested=false;
 const int32 Levels[4]={5,6,10,11};
public:
 explicit FPardoPreparationCheck(FAutomationTestBase* InTest):Test(InTest)
 {Directory=FPaths::ProjectSavedDir()/TEXT("UpgradePardo20261110")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 ~FPardoPreparationCheck(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 bool Update() override
 {
  if(!Deadline)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(FString::Printf(TEXT("Pardo preparation timeout at step %d, level index %d"),Step,Index));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Cards=Mode?Mode->GetCards():nullptr;
  auto* Roster=Mode?Mode->GetRoster():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Main=HUD?HUD->GetMainWidget():nullptr;
  if(!Mode||!Cards||!Roster||!Main)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"));
   if(!Test->TestTrue(TEXT("Formal Pardo card and definition load"),Card.IsValid()&&Card->HeroDefinition))return true;
   const auto* D=Card->HeroDefinition.Get();
   Test->TestEqual(TEXT("Pardo skill tiers step at 6 and 11"),D->GetSkillTierStep(),5);
   Test->TestTrue(TEXT("Pardo attack multipliers match displayed 300/450 percent"),
    D->GetSkillMultipliers().Equals(FVector(3.f,4.5f,4.5f),.001f));
   Test->TestTrue(TEXT("Living Can speed bonus matches displayed 30 percent"),FMath::IsNearlyEqual(D->GetSkillRules().LivingCanAttackSpeed,.3f));
   if(!Test->TestTrue(TEXT("Grant controlled Pardo card"),Cards->GrantCard(Card.Get()))||
      !Test->TestTrue(TEXT("Deploy Pardo through production card transaction"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   HeroId=Roster->FindHeroByDefinition(Card->HeroDefinition);
   if(!Test->TestTrue(TEXT("Deployed Pardo has roster identity"),HeroId.IsValid())||
      !Test->TestTrue(TEXT("Set controlled starting level to 5"),Roster->AddLevels(HeroId,4)))return true;
   // One-card merges make both exact threshold crossings observable in this fixture.
   Roster->SetMergeLevelOverride(1);
   Main->InspectHero(Roster,HeroId);
   Client=World->GetGameViewport();if(!Test->TestTrue(TEXT("Game viewport"),Client.IsValid()))return true;
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this](){if(!Pending.IsEmpty()&&!bRequested&&!FScreenshotRequest::IsScreenshotRequested())
    {bRequested=true;FScreenshotRequest::RequestScreenshot(Pending,true,false,false,FIntRect(),true);}});
   Test->AddInfo(TEXT("Controlled fixture: grant/deploy Pardo, set level 5 and 10, and merge one granted duplicate at each threshold. UI remains open. This is not physical mouse input or a natural match."));
   ChangedAt=FPlatformTime::Seconds();Step=1;return false;
  }
  auto* Property=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));
  auto* Info=Property?Cast<UEGHeroInfoWidget>(Property->GetObjectPropertyValue_InContainer(Main)):nullptr;
  if(!Info||!Info->IsVisible())return false;
  if(Step==1)
  {
   if(FPlatformTime::Seconds()-ChangedAt<.5)return false;
   auto* Button=Cast<UButton>(Info->GetWidgetFromName(TEXT("Button_UltimateInfo")));
   if(!Test->TestNotNull(TEXT("Ultimate detail button"),Button))return true;
   Button->OnClicked.Broadcast();ChangedAt=FPlatformTime::Seconds();Step=2;return false;
  }
  if(Step==2)
  {
   if(FPlatformTime::Seconds()-ChangedAt<.4)return false;
   if(!Test->TestTrue(TEXT("Skill detail stays open across upgrades"),Info->HasSkillDetailsOpen()))return true;
   FEGHeroInstance Hero;
   if(!Test->TestTrue(TEXT("Roster still contains Pardo"),Roster->GetHero(HeroId,Hero)))return true;
   Test->TestEqual(TEXT("Roster level"),Hero.Level,Levels[Index]);
   auto* Body=Cast<UEGRichTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsBody")));
   if(!Test->TestNotNull(TEXT("Actual rich skill detail"),Body))return true;
   const FString Plain=Body->GetPlainText().ToString(),Markup=Body->GetText().ToString();
   Test->TestTrue(TEXT("Displays baseline 300 percent and upgrades 450 percent and 30 percent"),
    Plain.Contains(TEXT("300%"))&&Plain.Contains(TEXT("450%"))&&Plain.Contains(TEXT("30%")));
   const FString Six=Levels[Index]>=6?TEXT("<skillready>6级</>"):TEXT("<skilllocked>6级</>");
   const FString Eleven=Levels[Index]>=11?TEXT("<skillready>11级</>"):TEXT("<skilllocked>11级</>");
   Test->TestTrue(TEXT("Level 6 unlock style follows current roster level"),Markup.Contains(Six));
   Test->TestTrue(TEXT("Level 11 unlock style follows current roster level"),Markup.Contains(Eleven));
   Pending=FPaths::ConvertRelativePathToFull(Directory/FString::Printf(TEXT("Pardo_Level%d.png"),Levels[Index]));
   bRequested=false;Step=3;return false;
  }
  if(Step==3)
  {
   if(!bRequested||FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*Pending)<1000)return false;
   Test->AddInfo(FString::Printf(TEXT("Controlled preparation screenshot: %s"),*Pending));Pending.Empty();bRequested=false;
   if(++Index==4)return true;
   if(Levels[Index]==10)
   {
    if(!Test->TestTrue(TEXT("Set controlled level to 10"),Roster->AddLevels(HeroId,4)))return true;
   }
   else
   {
    if(!Test->TestTrue(TEXT("Grant same-name card for threshold merge"),Cards->GrantCard(Card.Get()))||
       !Test->TestTrue(TEXT("Merge card through production transaction"),Cards->TryMergeIntoHero(Cards->GetHand().Last().InstanceId,HeroId)))return true;
   }
   ChangedAt=FPlatformTime::Seconds();Step=2;return false;
  }
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUpgradePardoPreparation,"ElyGambit.Integration.UpgradePardo.Preparation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGUpgradePardoPreparation::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FPardoStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FPardoPreparationCheck(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
