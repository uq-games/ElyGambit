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
#include "AudioMixerBlueprintLibrary.h"
#include "Components/EGCombatAudioComponent.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"
namespace {
class FFiveStartPIE final:public IAutomationLatentCommand
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
class FFiveCapture final:public IAutomationLatentCommand
{
 FAutomationTestBase* Test;int32 Variant=0,Step=0,Index=0;double Deadline=0,Since=0,Released=0;
 TArray<TWeakObjectPtr<AEGChessUnit>> Heroes;TWeakObjectPtr<UGameViewportClient> Client;FDelegateHandle Draw;
 FString Directory,Pending;bool Requested=false;FGuid ExpectedAction;bool Recording=false;TWeakObjectPtr<UWorld> RecordedWorld;
public:
 FFiveCapture(FAutomationTestBase* T,int32 V):Test(T),Variant(V){Heroes.SetNum(1);Directory=FPaths::ProjectSavedDir()/TEXT("FiveHeroFXAudio20261109/Graphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 ~FFiveCapture(){if(Recording&&RecordedWorld.IsValid())UAudioMixerBlueprintLibrary::StopRecordingOutput(RecordedWorld.Get(),EAudioRecordingExportType::WavFile,TEXT("Interrupted"),Directory);if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
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
   const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"),TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia"),TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"),TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult"),TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua")};
   auto* Card=LoadObject<UEGCardDefinition>(nullptr,Paths[Variant]);if(!Test->TestNotNull(TEXT("Formal card"),Card)||!Mode->GetCards()->GrantCard(Card)||!Test->TestTrue(TEXT("Deploy hero"),Mode->GetCards()->TryDeployHandCard(Mode->GetCards()->GetHand().Last().InstanceId,{3,1})))return true;
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
     const EEGHeroKind Kinds[]={EEGHeroKind::Elysia,EEGHeroKind::Aponia,EEGHeroKind::Hua,EEGHeroKind::GriseoAdult,EEGHeroKind::CollapsedHua};int32 I=Kind==Kinds[Variant]?0:INDEX_NONE;
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

   AEGChessUnit* Enemy=nullptr;for(auto* U:Mode->BattleRules->Units())if(U->GetFaction_Implementation()==EEGFaction::Enemy){Enemy=U;break;}
   if(!Test->TestNotNull(TEXT("Showcase enemy exists"),Enemy))return true;
   Enemy->SetActorLocation(Hero->GetActorLocation()+FVector(600,0,0));
   Hero->SetActorRotation((Enemy->GetActorLocation()-Hero->GetActorLocation()).Rotation());Hero->GetCombat()->ResetCombat();
   Main->InspectUnit(Hero);Since=W->GetTimeSeconds();Step=3;return false;
  }
  if(Step==3)
  {
   if(W->GetTimeSeconds()-Since<.35||!Info)return false;
   auto* Button=Cast<UButton>(Info->GetWidgetFromName(TEXT("Button_UltimateInfo")));if(!Test->TestNotNull(TEXT("Ultimate description button"),Button))return true;Button->OnClicked.Broadcast();
   Hero->GetHeroSkill()->CancelAction();Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   UAudioMixerBlueprintLibrary::StartRecordingOutput(W,12.f);Recording=true;RecordedWorld=W;Since=W->GetTimeSeconds();
   if(!Test->TestTrue(TEXT("Natural montage ultimate starts"),Hero->GetHeroSkill()->BeginAction(true)))return true;
   ExpectedAction=Hero->GetHeroSkill()->ActionId;Released=0;Step=4;return false;
  }
  if(Step==4)
  {
   bool Applied=false;for(auto* U:Mode->BattleRules->Units())if(U->LastCombatResolution.SourceId==Hero->GetInstanceId()&&U->LastCombatResolution.ActionId==ExpectedAction&&U->LastCombatResolution.HealthDelta<0)Applied=true;
   if(!Applied)return false;
   if(!Released)Released=W->GetTimeSeconds();if(W->GetTimeSeconds()-Released<.15)return false;
   Test->TestTrue(TEXT("Ultimate panel remains open"),Info&&Info->HasSkillDetailsOpen());
   Pending=FPaths::ConvertRelativePathToFull(Directory/(FString::Printf(TEXT("%d_%s.png"),Index,*Hero->GetDefinition()->HeroId.ToString())));Requested=false;Step=5;return false;
  }
  if(Step==5)
  {
   if(!Requested||FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*Pending)<1000)return false;
   Test->AddInfo(Pending);Pending.Empty();Requested=false;Step=6;return false;
  }
  if(Step==6)
  {
   if(W->GetTimeSeconds()-Since<8.f)return false;
   const auto* Audio=Hero->FindComponentByClass<UEGCombatAudioComponent>();
   Test->TestTrue(TEXT("Runtime audio created voices"),Audio&&Audio->SpawnedVoiceCount>0);
   UAudioMixerBlueprintLibrary::StopRecordingOutput(W,EAudioRecordingExportType::WavFile,TEXT("UltimateMix"),Directory);Recording=false;
   Test->AddInfo(Directory/TEXT("UltimateMix.wav"));
   Main->CloseHeroDetails();FAssetCompilingManager::Get().FinishAllCompilation();
   Hero->GetHeroSkill()->CancelAction();
   if(auto* Aponia=Cast<UEGAponiaSkillBehavior>(Hero->GetHeroSkill()->GetBehavior()))Aponia->DisciplineStacks=6;
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   if(!Test->TestTrue(TEXT("Unobstructed warmed-up FX cast"),Hero->GetHeroSkill()->BeginAction(true)))return true;
   ExpectedAction=Hero->GetHeroSkill()->ActionId;Released=0;Step=7;return false;
  }
  if(Step==7)
  {
   bool Applied=false;for(auto* U:Mode->BattleRules->Units())if(U->LastCombatResolution.SourceId==Hero->GetInstanceId()&&U->LastCombatResolution.ActionId==ExpectedAction&&U->LastCombatResolution.HealthDelta<0)Applied=true;
   if(!Applied)return false;
   if(!Released)Released=W->GetTimeSeconds();if(W->GetTimeSeconds()-Released<.12)return false;
   Pending=FPaths::ConvertRelativePathToFull(Directory/TEXT("UnobstructedFX.png"));Requested=false;Step=8;return false;
  }
  if(Step==8)
  {
   if(!Requested||FScreenshotRequest::IsScreenshotRequested()||IFileManager::Get().FileSize(*Pending)<1000)return false;
   Test->AddInfo(Pending);Pending.Empty();return true;
  }
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveProgressionElysia,"ElyGambit.Integration.FiveProgression.Elysia",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveProgressionElysia::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FFiveCapture(this,0));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveProgressionAponia,"ElyGambit.Integration.FiveProgression.Aponia",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveProgressionAponia::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FFiveCapture(this,1));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveProgressionHua,"ElyGambit.Integration.FiveProgression.Hua",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveProgressionHua::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FFiveCapture(this,2));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveProgressionGriseoAdult,"ElyGambit.Integration.FiveProgression.GriseoAdult",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveProgressionGriseoAdult::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FFiveCapture(this,3));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveProgressionCollapsedHua,"ElyGambit.Integration.FiveProgression.CollapsedHua",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveProgressionCollapsedHua::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveStartPIE());ADD_LATENT_AUTOMATION_COMMAND(FFiveCapture(this,4));ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
