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
#include "AssetCompilingManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/Button.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UObject/UnrealType.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
namespace {
class FSupportStartPIE final:public IAutomationLatentCommand
{
public:bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ClientSize(FVector2D(1920,1080)).AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};
class FSupportCapture final:public IAutomationLatentCommand
{
 FAutomationTestBase* Test;int32 Step=0,Index=0,LevelPreview=0;double Deadline=0,Since=0,Released=0;
 TArray<TWeakObjectPtr<AEGChessUnit>> Heroes;TWeakObjectPtr<UGameViewportClient> Client;FDelegateHandle Draw;
 FString Directory,Pending;bool Requested=false;
public:
 FSupportCapture(FAutomationTestBase* T):Test(T){Heroes.SetNum(3);Directory=FPaths::ProjectSavedDir()/TEXT("SupportRework20261109/Graphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 ~FSupportCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 bool Update() override
 {
  if(!Deadline)Deadline=FPlatformTime::Seconds()+180;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(FString::Printf(TEXT("Support capture timeout step %d index %d"),Step,Index));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=W?W->GetFirstPlayerController():nullptr;auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;auto* Main=HUD?HUD->GetMainWidget():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->BattleRules||!Main)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo"),TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"),TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden")};
   for(int32 I=0;I<3;++I)
   {auto* Card=LoadObject<UEGCardDefinition>(nullptr,Paths[I]);if(!Test->TestNotNull(TEXT("Formal support card"),Card)||!Mode->GetCards()->GrantCard(Card)||!Test->TestTrue(TEXT("Deploy support"),Mode->GetCards()->TryDeployHandCard(Mode->GetCards()->GetHand().Last().InstanceId,{I+2,1})))return true;}
   if(!Test->TestTrue(TEXT("Enter production battle"),Mode->EnterCombat()))return true;Step=1;return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   for(auto* U:Mode->GetCombatUnits())
   {
    U->GetCombat()->SetComponentTickEnabled(false);auto* ASC=U->GetAbilitySystemComponent();
    if(U->GetFaction_Implementation()==EEGFaction::Player)
    {
     const auto Kind=U->GetDefinition()->HeroKind;
     int32 I=Kind==EEGHeroKind::Griseo?0:Kind==EEGHeroKind::Pardofelis?1:Kind==EEGHeroKind::Eden?2:INDEX_NONE;
     if(I!=INDEX_NONE){Heroes[I]=U;U->GetHeroSkill()->ApplyPersistentLevel(11);}
     ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),2000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000);
    }
    else {ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);}
   }
   for(const auto& H:Heroes)if(!Test->TestTrue(TEXT("Formal hero spawned"),H.IsValid()))return true;
   Client=W->GetGameViewport();if(!Client.IsValid())return true;
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this](){if(!Pending.IsEmpty()&&!Requested&&!FScreenshotRequest::IsScreenshotRequested()){Requested=true;FScreenshotRequest::RequestScreenshot(Pending,true,false,false,FIntRect(),true);}});
   Test->AddInfo(TEXT("Controlled level 11 showcase: real montage notifications, frozen enemy/hero AI, reduced allied health for healing; not a natural battle or performance run."));Step=2;
  }
  auto* Hero=Heroes[Index].Get();if(!Hero)return true;
  auto* P=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));auto* Info=P?Cast<UEGHeroInfoWidget>(P->GetObjectPropertyValue_InContainer(Main)):nullptr;
  if(Step==2){Main->InspectUnit(Hero);Since=W->GetTimeSeconds();Step=3;return false;}
  if(Step==3)
  {
   if(W->GetTimeSeconds()-Since<.35||!Info)return false;
   auto* Button=Cast<UButton>(Info->GetWidgetFromName(TEXT("Button_UltimateInfo")));if(!Test->TestNotNull(TEXT("Ultimate description button"),Button))return true;Button->OnClicked.Broadcast();
   Hero->GetHeroSkill()->CancelAction();Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   if(!Test->TestTrue(TEXT("Natural montage ultimate starts"),Hero->GetHeroSkill()->BeginAction(true)))return true;
   Released=0;Step=4;return false;
  }
  if(Step==4)
  {
   bool Applied=false;
   if(Index==0){for(auto* U:Mode->BattleRules->Units())if(U->GetHeroSkill()->GetSourceShield(TEXT("Griseo.DreamShield"))>0)Applied=true;}
   if(Index==1){auto* Cat=Mode->BattleRules->FindCan(Hero->GetInstanceId());Applied=Cat&&!Cat->GetHeroSkill()->bInFlight;}
   if(Index==2)Applied=Hero->GetAttributes()->GetHealth()>1000;
   if(!Applied)return false;
   if(!Released)Released=W->GetTimeSeconds();if(W->GetTimeSeconds()-Released<.15)return false;
   Test->TestTrue(TEXT("Ultimate panel remains open"),Info&&Info->HasSkillDetailsOpen());
   Pending=FPaths::ConvertRelativePathToFull(Directory/(FString::Printf(TEXT("%d_%s.png"),Index,*Hero->GetDefinition()->HeroId.ToString())));Requested=false;Step=5;return false;
  }
  if(Step==5)
  {
   if(!Requested||FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*Pending)<1000)return false;
   Test->AddInfo(Pending);Pending.Empty();Requested=false;
   if(++Index==3){Index=0;Step=6;}else Step=2;
   return false;
  }
  if(Step==6)
  {
   Hero->GetHeroSkill()->ApplyPersistentLevel(LevelPreview==0?1:6);
   Main->InspectUnit(Hero);Since=W->GetTimeSeconds();Step=7;return false;
  }
  if(Step==7)
  {
   if(!Info||W->GetTimeSeconds()-Since<.5)return false;
   if(!Info->HasSkillDetailsOpen())
   {CastChecked<UButton>(Info->GetWidgetFromName(TEXT("Button_UltimateInfo")))->OnClicked.Broadcast();Since=W->GetTimeSeconds();return false;}
   Pending=FPaths::ConvertRelativePathToFull(Directory/FString::Printf(TEXT("Griseo_Level%d.png"),LevelPreview==0?1:6));Requested=false;Step=8;return false;
  }
  if(Step==8)
  {
   if(!Requested||FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*Pending)<1000)return false;
   Test->AddInfo(Pending);Pending.Empty();Requested=false;
   if(++LevelPreview==2)return true;Step=6;
  }
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSupportReworkPIE,"ElyGambit.Integration.SupportRework.ThreeHeroSkillsAndText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSupportReworkPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FSupportStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FSupportCapture(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
