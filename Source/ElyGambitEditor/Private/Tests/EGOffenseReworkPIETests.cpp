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
#include "Components/EGVillVMechanismComponent.h"
#include "Character/EGVillVEmplacement.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UObject/UnrealType.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
namespace {
class FOffenseStartPIE final:public IAutomationLatentCommand
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
class FOffenseCapture final:public IAutomationLatentCommand
{
 FAutomationTestBase* Test;int32 Step=0,Index=0;double Deadline=0,Since=0,Released=0;
 TArray<TWeakObjectPtr<AEGChessUnit>> Heroes;TWeakObjectPtr<UGameViewportClient> Client;FDelegateHandle Draw;
 FString Directory,Pending;bool Requested=false;
public:
 FOffenseCapture(FAutomationTestBase* T):Test(T){Heroes.SetNum(3);Directory=FPaths::ProjectSavedDir()/TEXT("OffenseRework20261109/Graphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 ~FOffenseCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
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
   const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"),TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"),TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius")};
   for(int32 I=0;I<3;++I)
   {auto* Card=LoadObject<UEGCardDefinition>(nullptr,Paths[I]);if(!Test->TestNotNull(TEXT("Formal support card"),Card)||!Mode->GetCards()->GrantCard(Card)||!Test->TestTrue(TEXT("Deploy support"),Mode->GetCards()->TryDeployHandCard(Mode->GetCards()->GetHand().Last().InstanceId,{I*3,1})))return true;}
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
     int32 I=Kind==EEGHeroKind::Sakura?0:Kind==EEGHeroKind::VillV?1:Kind==EEGHeroKind::Mobius?2:INDEX_NONE;
     if(I!=INDEX_NONE){Heroes[I]=U;U->GetHeroSkill()->ApplyPersistentLevel(11);}
     ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),2000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000);
    }
    else {ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000);}
   }
   for(const auto& H:Heroes)if(!Test->TestTrue(TEXT("Formal hero spawned"),H.IsValid()))return true;
   Client=W->GetGameViewport();if(!Client.IsValid())return true;
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this](){if(!Pending.IsEmpty()&&!Requested&&!FScreenshotRequest::IsScreenshotRequested()){Requested=true;FScreenshotRequest::RequestScreenshot(Pending,true,false,false,FIntRect(),true);}});
   Test->AddInfo(TEXT("Controlled level 11 showcase: real montage notifications, frozen enemy/hero AI, controlled health and targets; not a natural battle or performance run."));Step=2;
  }
  auto* Hero=Heroes[Index].Get();if(!Test->TestNotNull(TEXT("Showcase hero remains alive until capture"),Hero))return true;
  if(!Test->TestTrue(TEXT("Showcase battle remains active"),Mode->BattleRules->IsBattleActive()))return true;
  auto* P=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));auto* Info=P?Cast<UEGHeroInfoWidget>(P->GetObjectPropertyValue_InContainer(Main)):nullptr;
  if(Step==2)
  {
   if(Index==1){auto* M=Hero->FindComponentByClass<UEGVillVMechanismComponent>();if(!M||!M->IsOperating()||Hero->GetHeroSkill()->IsActionRunning())return false;}
   AEGChessUnit* Enemy=nullptr;for(auto* U:Mode->BattleRules->Units())if(U->GetFaction_Implementation()==EEGFaction::Enemy){Enemy=U;break;}
   if(!Test->TestNotNull(TEXT("Showcase enemy exists"),Enemy))return true;
   Enemy->SetActorLocation(Hero->GetActorLocation()+FVector(Index==0?150:Index==2?-600:600,0,0));
   Hero->SetActorRotation((Enemy->GetActorLocation()-Hero->GetActorLocation()).Rotation());Hero->GetCombat()->ResetCombat();
   Main->InspectUnit(Hero);Since=W->GetTimeSeconds();Step=3;return false;
  }
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
   if(Index==0){for(auto* U:Mode->BattleRules->Units())if(U->LastCombatResolution.SourceId==Hero->GetInstanceId()&&U->LastCombatResolution.HealthDelta<0)Applied=true;}
   if(Index==1){auto* M=Hero->FindComponentByClass<UEGVillVMechanismComponent>();Applied=M&&M->GetAutoFireRemaining()>0.f;}
   if(Index==2){auto* B=Cast<UEGMobiusSkillBehavior>(Hero->GetHeroSkill()->GetBehavior());Applied=B&&B->EmpoweredActive;}
   if(!Applied)return false;
   if(!Released)Released=W->GetTimeSeconds();if(W->GetTimeSeconds()-Released<.15)return false;
   Test->TestTrue(TEXT("Ultimate panel remains open"),Info&&Info->HasSkillDetailsOpen());
   Pending=FPaths::ConvertRelativePathToFull(Directory/(FString::Printf(TEXT("%d_%s.png"),Index,*Hero->GetDefinition()->HeroId.ToString())));Requested=false;Step=5;return false;
  }
  if(Step==5)
  {
   if(!Requested||FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*Pending)<1000)return false;
   Test->AddInfo(Pending);Pending.Empty();Requested=false;
   if(++Index==3)return true;else Step=2;
   return false;
  }
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGOffenseReworkPIE,"ElyGambit.Integration.OffenseRework.ThreeHeroSkillsAndText",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGOffenseReworkPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FOffenseStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FOffenseCapture(this));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
