#include "Presentation/EGUltimateSignetVisual.h"
#include "Presentation/EGVillVShotVisual.h"
#include "Components/EGCombatAudioComponent.h"
#include "AudioMixerBlueprintLibrary.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Materials/MaterialInterface.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGFiveHeroPresentation.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGMobiusHeadProjection.h"
#include "Presentation/EGMobiusLinkVisual.h"
#include "Presentation/EGMobiusNiagaraEffect.h"
#include "Presentation/EGMobiusSourceAttackVisual.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"

namespace
{
struct FFiveHeroCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 bool bRequested=false;
 double FirstEligibleTime=-1.;
 ~FFiveHeroCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FFiveHeroStartPIE final : public IAutomationLatentCommand
{
 int32 Width,Height;
public:
 FFiveHeroStartPIE(int32 InWidth,int32 InHeight):Width(InWidth),Height(InHeight){}
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Width;Settings->NewWindowHeight=Height;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(Width,Height))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

struct FFiveHeroCueCounts
{
 int32 Evade=0,DelayedMark=0,DelayedRelease=0,FanMark=0,FanRelease=0,Actors=0;
};
FFiveHeroCueCounts ReadCues(UWorld* World,AEGChessUnit* Sakura)
{
 FFiveHeroCueCounts Out;if(!World||!Sakura)return Out;
 for(TActorIterator<AEGFiveHeroPresentation> It(World);It;++It)
  if(It->GetOwner()==Sakura&&!It->IsActorBeingDestroyed())
  {
   ++Out.Actors;Out.Evade+=It->GetActiveCueCount(EEGFiveHeroCue::SakuraEvade);
   Out.DelayedMark+=It->GetActiveCueCount(EEGFiveHeroCue::SakuraDelayedMark);
   Out.DelayedRelease+=It->GetActiveCueCount(EEGFiveHeroCue::SakuraDelayedRelease);
   Out.FanMark+=It->GetActiveCueCount(EEGFiveHeroCue::SakuraFanMark);
   Out.FanRelease+=It->GetActiveCueCount(EEGFiveHeroCue::SakuraFanRelease);
  }
 return Out;
}

class FFiveHeroSakuraPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 int32 Width,Height;
 bool bNatural,bStarted=false,bControlled=false,bMarkCaptured=false,bReleaseCaptured=false;
 bool bConfigOverridden=false;
 double Deadline=0.,BattleStart=-1.,MarkTime=-1.;
 FString Directory,RunId;
 TWeakObjectPtr<AEGChessUnit> Sakura;
 TWeakObjectPtr<UEGSakuraSkillDefinition> MutableDefinition;
 TSoftObjectPtr<UEGSakuraReworkPresentationConfig> OriginalConfig;
 TSharedPtr<FFiveHeroCapture> Capture;
 int32 CaptureKind=INDEX_NONE;

 void RestoreConfig()
 {
  if(bConfigOverridden&&MutableDefinition.IsValid())MutableDefinition->ReworkPresentation=OriginalConfig;
  bConfigOverridden=false;
 }
 bool ArmCapture(UWorld* World,int32 Kind)
 {
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return false;
  Capture=MakeShared<FFiveHeroCapture>();Capture->Client=Client;
  Capture->Path=FPaths::ConvertRelativePathToFull(Directory/(FString(Kind==0?TEXT("mark_"):TEXT("release_"))+RunId+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
  const TWeakPtr<FFiveHeroCapture> Weak=Capture;const TWeakObjectPtr<UWorld> WeakWorld(World);const auto Unit=Sakura;
  auto* CurrentTest=Test;const double Start=BattleStart;
  Capture->Handle=Client->OnDrawn().AddLambda([Weak,WeakWorld,Unit,CurrentTest,Kind,Start]()
  {
   auto State=Weak.Pin();auto* W=WeakWorld.Get();auto* U=Unit.Get();
   if(!State.IsValid()||!W||!U||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   const auto C=ReadCues(W,U);
   const int32 Relevant=Kind==0?C.DelayedMark+C.FanMark:C.DelayedRelease+C.FanRelease;
   if(Relevant<=0){State->FirstEligibleTime=-1.;return;}
   if(State->FirstEligibleTime<0.)State->FirstEligibleTime=W->GetTimeSeconds();
   // Capture the visible body of the cue, not its zero-opacity birth frame.
   if(W->GetTimeSeconds()-State->FirstEligibleTime<.08)return;
   const auto* Camera=UGameplayStatics::GetPlayerCameraManager(W,0);
   CurrentTest->TestNotNull(TEXT("Formal board camera"),Camera);
   CurrentTest->AddInfo(FString::Printf(TEXT("capture=%s battle_time=%.3f evade=%d delayed_mark=%d delayed_release=%d fan_mark=%d fan_release=%d actors=%d camera=%s camera_rotation=%s path=%s"),
    Kind==0?TEXT("mark"):TEXT("release"),W->GetTimeSeconds()-Start,C.Evade,C.DelayedMark,C.DelayedRelease,C.FanMark,C.FanRelease,C.Actors,
    Camera?*Camera->GetCameraLocation().ToString():TEXT("missing"),Camera?*Camera->GetCameraRotation().ToString():TEXT("missing"),*State->Path));
   State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  });
  CaptureKind=Kind;return true;
 }
 bool CompleteCapture()
 {
  if(!Capture.IsValid()||!Capture->bRequested||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
  Test->TestEqual(TEXT("Screenshot has requested pixel dimensions"),FIntPoint(int32(BE(16)),int32(BE(20))),FIntPoint(Width,Height));
  if(CaptureKind==0){bMarkCaptured=true;MarkTime=FPlatformTime::Seconds();}else bReleaseCaptured=true;
  Capture.Reset();CaptureKind=INDEX_NONE;return true;
 }
public:
 FFiveHeroSakuraPIE(FAutomationTestBase* InTest,int32 InWidth,int32 InHeight,bool bInNatural)
  :Test(InTest),Width(InWidth),Height(InHeight),bNatural(bInNatural),RunId(FGuid::NewGuid().ToString(EGuidFormats::Digits))
 {
  Directory=FPaths::ProjectSavedDir()/TEXT("FiveHeroRework20261108/Graphics")/
   FString::Printf(TEXT("%s_%dx%d"),bNatural?TEXT("Natural"):TEXT("Controlled"),Width,Height);
 }
 virtual ~FFiveHeroSakuraPIE(){RestoreConfig();}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Sakura graphic capture timed out"));RestoreConfig();return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Cards=Mode->GetCards();FGuid SakuraCard;
   for(const auto& C:Cards->GetHand())if(C.Definition&&C.Definition->HeroDefinition&&C.Definition->HeroDefinition->HeroKind==EEGHeroKind::Sakura)SakuraCard=C.InstanceId;
   if(!SakuraCard.IsValid())
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura.DA_Card_Sakura"));
    if(!Test->TestNotNull(TEXT("Formal Sakura card"),Card)||!Test->TestTrue(TEXT("Grant Sakura"),Cards->GrantCard(Card)))return true;
    SakuraCard=Cards->GetHand().Last().InstanceId;
   }
   if(!Test->TestTrue(TEXT("Deploy Sakura on the formal board"),Cards->TryDeployHandCard(SakuraCard,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Sakura){Sakura=U;break;}
   if(!Test->TestTrue(TEXT("Sakura is present"),Sakura.IsValid()))return true;
   auto* Definition=Cast<UEGSakuraSkillDefinition>(Sakura->GetDefinition()->SkillDefinition);
   if(!Test->TestNotNull(TEXT("Sakura skill"),Definition))return true;
   FString Candidate;
   if(FParse::Value(FCommandLine::Get(),TEXT("FiveHeroSakuraConfig="),Candidate)&&!Candidate.IsEmpty())
   {
    auto* Config=LoadObject<UEGSakuraReworkPresentationConfig>(nullptr,*Candidate);
    if(!Test->TestNotNull(TEXT("Requested candidate presentation config"),Config))return true;
    MutableDefinition=Definition;OriginalConfig=Definition->ReworkPresentation;
    Definition->ReworkPresentation=Config;bConfigOverridden=true;
   }
   Test->AddInfo(FString::Printf(TEXT("Presentation config: %s"),*Definition->ReworkPresentation.ToSoftObjectPath().ToString()));
   if(!Test->TestFalse(TEXT("Formal or explicit candidate config must be bound"),Definition->ReworkPresentation.IsNull()))return true;
   Test->AddInfo(bNatural?TEXT("Natural AI: granted/deployed Sakura, unchanged enemies, health, mana and timing; actual user input is not simulated."):
    TEXT("Controlled: grant/deploy Sakura, disable enemy AI, raise health, move targets and fill mana; animation and notifications advance naturally."));
   bStarted=true;return false;
  }
  if(Capture.IsValid()&&!CompleteCapture())return false;
  if(!Sakura.IsValid()){Test->AddError(TEXT("Sakura disappeared before captures"));RestoreConfig();return true;}
  if(!Mode->BattleRules->IsCombatRunning())
  {if(BattleStart>=0.){Test->AddError(TEXT("Battle ended before both captures"));RestoreConfig();return true;}return false;}
  if(BattleStart<0.)BattleStart=W->GetTimeSeconds();
  if(!bNatural&&!bControlled)
  {
   const FVector Origin=Sakura->GetActorLocation();Sakura->SetActorRotation(FRotator::ZeroRotator);int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy)
    {
     U->GetCombat()->SetComponentTickEnabled(false);
     const FVector Offset=Index==0?FVector(250,-115,0):(Index==1?FVector(250,115,0):FVector(650,0,0));
     ++Index;U->SetActorLocation(Origin+Offset);
    }
   }
   Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Sakura->GetAttributes()->GetMaxMana());
   bControlled=true;
  }
  const auto C=ReadCues(W,Sakura.Get());
  if(!bMarkCaptured&&(C.FanMark+C.DelayedMark)>0){if(!ArmCapture(W,0))Test->AddError(TEXT("Could not arm mark capture"));return false;}
  if(bMarkCaptured&&!bReleaseCaptured&&(C.FanRelease+C.DelayedRelease)>0){if(!ArmCapture(W,1))Test->AddError(TEXT("Could not arm release capture"));return false;}
  if(bMarkCaptured&&bReleaseCaptured)
  {
   Test->AddInfo(TEXT("Both selected cue frames saved; pixel contents and motion still require visual review."));
   RestoreConfig();return true;
  }
  if(W->GetTimeSeconds()-BattleStart>45. || (MarkTime>0.&&FPlatformTime::Seconds()-MarkTime>20.))
  {Test->AddError(FString::Printf(TEXT("Required frames missing: mark=%d release=%d"),bMarkCaptured,bReleaseCaptured));RestoreConfig();return true;}
  return false;
 }
};

bool QueueFiveHeroSakura(FAutomationTestBase* Test,int32 Width,int32 Height,bool bNatural)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Sakura graphic test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Sakura graphic test requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(Width,Height));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroSakuraPIE(Test,Width,Height,bNatural));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveHeroSakuraControlled1080,"ElyGambit.Integration.FiveHeroSakura.Controlled1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveHeroSakuraControlled1080::RunTest(const FString&){return QueueFiveHeroSakura(this,1920,1080,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveHeroSakuraNatural1080,"ElyGambit.Integration.FiveHeroSakura.Natural1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveHeroSakuraNatural1080::RunTest(const FString&){return QueueFiveHeroSakura(this,1920,1080,true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveHeroSakuraControlled1440,"ElyGambit.Integration.FiveHeroSakura.Controlled1440",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveHeroSakuraControlled1440::RunTest(const FString&){return QueueFiveHeroSakura(this,2560,1440,false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFiveHeroSakuraNatural1440,"ElyGambit.Integration.FiveHeroSakura.Natural1440",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FFiveHeroSakuraNatural1440::RunTest(const FString&){return QueueFiveHeroSakura(this,2560,1440,true);}
namespace
{
class FMobiusSetCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FMobiusSetCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroImplementation20261108/MobiusGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FMobiusSetCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Mobius whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/FXs/Characters/Mobius/FiveHeroRework20261108/Materials/M_Mobius_HeadProjection.M_Mobius_HeadProjection"));
   LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Mobius/FiveHeroRework20261108/NS_Mobius_Gather_Pack.NS_Mobius_Gather_Pack"));
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius.DA_Card_Mobius"));
   if(!Test->TestNotNull(TEXT("Formal Mobius card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Mobius"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Mobius){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Mobius present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*300-300,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic")))for(TActorIterator<AEGMobiusSourceAttackVisual> It(World);It;++It)
     if(It->GetOwner()==Hero.Get()&&It->BasicStageIndex>=0&&It->ActiveCueCount>0&&It->GetGameTimeSinceCreation()>.12f){Phase=TEXT("basic");break;}
    if(Phase.IsEmpty()&&!Captured.Contains(TEXT("gather")))for(TActorIterator<AEGMobiusNiagaraEffect> It(World);It;++It)
     if(auto* FX=It->FindComponentByClass<UNiagaraComponent>();FX&&FX->GetAsset()&&FX->GetAsset()->GetName()==TEXT("NS_Mobius_Gather_Pack")&&It->GetGameTimeSinceCreation()>.2f&&It->GetGameTimeSinceCreation()<.8f){Phase=TEXT("gather");break;}
    if(Phase.IsEmpty()&&!Captured.Contains(TEXT("giant")))for(TActorIterator<AEGCombatVisual> It(World);It;++It)
     if(It->GetOwner()==Hero.Get()&&It->Definition&&It->Definition->GetName()==TEXT("DA_CastSnake")&&It->SourceTime>3.35f&&It->SourceTime<4.4f){Phase=TEXT("giant");break;}
    if(Phase.IsEmpty()&&!Captured.Contains(TEXT("head")))for(TActorIterator<AEGMobiusHeadProjection> It(World);It;++It)
     if(It->GetOwner()==Hero.Get()&&!It->IsActorBeingDestroyed()&&It->GetGameTimeSinceCreation()>.09f){Phase=TEXT("head");break;}
    if(Phase.IsEmpty()&&!Captured.Contains(TEXT("link")))for(TActorIterator<AEGMobiusLinkVisual> It(World);It;++It)
     if(It->GetOwner()==Hero.Get()&&!It->IsActorBeingDestroyed()&&It->GetGameTimeSinceCreation()>.4f){Phase=TEXT("link");break;}
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==5){Test->AddInfo(TEXT("Five phase frames saved; visual review required."));return true;}
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobiusCompleteSet1080,"ElyGambit.Integration.SevenHeroMobius.CompleteSet1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMobiusCompleteSet1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FMobiusSetCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FAponiaSetCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FAponiaSetCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroImplementation20261108/AponiaGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FAponiaSetCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Aponia whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Aponia/FiveHeroRework20261108/NS_Aponia_PrayerWings_Pack.NS_Aponia_PrayerWings_Pack"));
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia.DA_Card_Aponia"));
   if(!Test->TestNotNull(TEXT("Formal Aponia card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Aponia"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Aponia){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Aponia present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*300-300,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   if(auto* Behavior=Cast<UEGAponiaSkillBehavior>(Hero->GetHeroSkill()->GetBehavior()))Behavior->DisciplineStacks=6; // Controlled visual setup only.
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("nodes"))&&World->GetTimeSeconds()-ObservationStart>2.5f)Phase=TEXT("nodes");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     const FString Name=FX->GetAsset()->GetName();const float Age=It->GetGameTimeSinceCreation();
     if(Name==TEXT("NS_Aponia_Bolt_Pack")&&!Captured.Contains(TEXT("basic"))&&Age>.1f&&World->GetTimeSeconds()-ObservationStart>1.5f)Phase=TEXT("basic");
     if(Name==TEXT("NS_Aponia_PrayerWings_Pack")&&!Captured.Contains(TEXT("prayer"))&&Age>.65f)Phase=TEXT("prayer");
     if(Name==TEXT("NS_Aponia_VerdictWings_Pack")&&!Captured.Contains(TEXT("verdict"))&&Age>.18f)Phase=TEXT("verdict");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==4){Test->AddInfo(TEXT("Four phase frames saved; visual review required."));return true;}
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAponiaCompleteSet1080,"ElyGambit.Integration.SevenHeroAponia.CompleteSet1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAponiaCompleteSet1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FAponiaSetCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}



namespace
{
class FOtherFlameChaserCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory,CardPath;
 EEGHeroKind Kind;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 FOtherFlameChaserCapture(FAutomationTestBase* In,EEGHeroKind InKind,FString InPath,FString Label):Test(In),CardPath(MoveTemp(InPath)),Kind(InKind)
 {Directory=FPaths::ProjectSavedDir()/TEXT("OtherFlameChaserVisibility20261108")/Label/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FOtherFlameChaserCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Hero whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,*CardPath);
   if(!Test->TestNotNull(TEXT("Formal Hero card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Hero"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==Kind){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Hero present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,(Kind==EEGHeroKind::Elysia||Kind==EEGHeroKind::ElysiaHerrscher)?1000:180,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset()||It->GetGameTimeSinceCreation()<.08f)continue;
     const FString Name=FX->GetAsset()->GetName();
     if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart<5.f)Phase=TEXT("basic");
     if(!Captured.Contains(TEXT("ultimate"))&&(Name.Contains(TEXT("SummonImpact"))||Name.Contains(TEXT("PaintBloom"))||Name==TEXT("NS_Hua_Landing")))Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty()&&!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetDisplayAge()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    if(Phase.IsEmpty()&&!Captured.Contains(TEXT("ultimate"))&&World->GetTimeSeconds()-ObservationStart>8.5f)Phase=TEXT("ultimate");
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled observation: static enemies 180cm for melee, 1000cm for archers, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Contains(TEXT("basic"))&&Captured.Contains(TEXT("ultimate"))&&Captured.Contains(TEXT("signet")))
  {Test->AddInfo(TEXT("Basic, ultimate and signet frames saved; visual review required."));return true;}
  if(bConfigured&&W->GetTimeSeconds()-ObservationStart>25.f)
  {Test->AddError(TEXT("Missing basic, ultimate or signet frame within the bounded observation window."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisibilityPardo,"ElyGambit.Integration.OtherVisibility.Pardo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVisibilityPardo::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FOtherFlameChaserCapture(this,EEGHeroKind::Pardofelis,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"),TEXT("Pardo")));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisibilityHua,"ElyGambit.Integration.OtherVisibility.Hua",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVisibilityHua::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FOtherFlameChaserCapture(this,EEGHeroKind::Hua,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"),TEXT("Hua")));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisibilityGriseo,"ElyGambit.Integration.OtherVisibility.Griseo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVisibilityGriseo::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FOtherFlameChaserCapture(this,EEGHeroKind::Griseo,TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo"),TEXT("Griseo")));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisibilityElysia,"ElyGambit.Integration.OtherVisibility.Elysia",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVisibilityElysia::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FOtherFlameChaserCapture(this,EEGHeroKind::Elysia,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"),TEXT("Elysia")));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVisibilityHuman,"ElyGambit.Integration.OtherVisibility.Human",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVisibilityHuman::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FOtherFlameChaserCapture(this,EEGHeroKind::ElysiaHerrscher,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human"),TEXT("Human")));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

#endif

namespace
{
class FEdenSetCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FEdenSetCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroImplementation20261108/EdenGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FEdenSetCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Eden whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden.DA_Card_Eden"));
   if(!Test->TestNotNull(TEXT("Formal Eden card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Eden"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Eden){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Eden present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*130-130,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;

    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     const FString Name=FX->GetAsset()->GetName();
     if(Name==TEXT("NS_Eden_GoldenShot")&&!Captured.Contains(TEXT("basic"))&&It->GetGameTimeSinceCreation()>.12f&&World->GetTimeSeconds()-ObservationStart>1.5f)Phase=TEXT("basic");
     if(Name==TEXT("NS_Eden_GoldenFinale")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.18f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==2){Test->AddInfo(TEXT("Two phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEdenFastFeedback1080,"ElyGambit.Integration.SevenHeroFeedback.Eden",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEdenFastFeedback1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FEdenSetCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FCollapsedHuaSetCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FCollapsedHuaSetCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroImplementation20261108/CollapsedHuaGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FCollapsedHuaSetCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("CollapsedHua whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua.DA_Card_CollapsedHua"));
   if(!Test->TestNotNull(TEXT("Formal CollapsedHua card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy CollapsedHua"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::CollapsedHua){Hero=U;break;}
   if(!Test->TestTrue(TEXT("CollapsedHua present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*130-130,180,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     const FString Name=FX->GetAsset()->GetName();
     if(Name==TEXT("NS_CollapsedHua_landing")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.18f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==2){Test->AddInfo(TEXT("Two phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollapsedHuaFastFeedback1080,"ElyGambit.Integration.SevenHeroFeedback.CollapsedHua",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCollapsedHuaFastFeedback1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FCollapsedHuaSetCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FGriseoAdultSetCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FGriseoAdultSetCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroImplementation20261108/GriseoAdultGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FGriseoAdultSetCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("GriseoAdult whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult.DA_Card_GriseoAdult"));
   if(!Test->TestNotNull(TEXT("Formal GriseoAdult card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy GriseoAdult"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::GriseoAdult){Hero=U;break;}
   if(!Test->TestTrue(TEXT("GriseoAdult present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*130-130,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     const FString Name=FX->GetAsset()->GetName();
     if(Name==TEXT("NS_GriseoAdult_MeteorBurst")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.18f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==2){Test->AddInfo(TEXT("Two phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGriseoAdultFastFeedback1080,"ElyGambit.Integration.SevenHeroFeedback.GriseoAdult",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGriseoAdultFastFeedback1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FGriseoAdultSetCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FVillVSetCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FVillVSetCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroImplementation20261108/VillVGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FVillVSetCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("VillV whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV.DA_Card_VillV"));
   if(!Test->TestNotNull(TEXT("Formal VillV card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy VillV"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::VillV){Hero=U;break;}
   if(!Test->TestTrue(TEXT("VillV present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*130-130,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    for(TActorIterator<AEGVillVShotVisual> Shot(World);Shot&&Phase.IsEmpty();++Shot)
     if(Shot->GetGameTimeSinceCreation()>.06f&&World->GetTimeSeconds()-ObservationStart>2.f)
     {
      if(!Captured.Contains(TEXT("basic")))Phase=TEXT("basic");
      else if(bUltimateRequested&&World->GetTimeSeconds()-ObservationStart>8.f&&!Captured.Contains(TEXT("ultimate")))Phase=TEXT("ultimate");
     }
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     const FString Name=FX->GetAsset()->GetName();
     if(Name==TEXT("")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.18f)Phase=TEXT("ultimate");
    }

    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==2){Test->AddInfo(TEXT("Two phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVillVFastFeedback1080,"ElyGambit.Integration.SevenHeroFeedback.VillV",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVillVFastFeedback1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FVillVSetCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FSakuraEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FSakuraEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/SakuraGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FSakuraEnhancedCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Sakura whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura.DA_Card_Sakura"));
   if(!Test->TestNotNull(TEXT("Formal Sakura card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Sakura"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Sakura){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Sakura present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,180,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     if(FX->GetAsset()->GetName()==TEXT("NS_Sakura_IceShatter")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.15f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==3){Test->AddInfo(TEXT("Three phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSakuraEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.Sakura",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FSakuraEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FSakuraEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FMobiusEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FMobiusEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/MobiusGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FMobiusEnhancedCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Mobius whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius.DA_Card_Mobius"));
   if(!Test->TestNotNull(TEXT("Formal Mobius card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Mobius"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Mobius){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Mobius present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    if(bUltimateRequested&&!Captured.Contains(TEXT("ultimate"))&&World->GetTimeSeconds()-ObservationStart>7.3f)Phase=TEXT("ultimate");
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==3){Test->AddInfo(TEXT("Three phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMobiusEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.Mobius",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FMobiusEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FMobiusEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FAponiaEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FAponiaEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/AponiaGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FAponiaEnhancedCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Aponia whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia.DA_Card_Aponia"));
   if(!Test->TestNotNull(TEXT("Formal Aponia card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Aponia"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Aponia){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Aponia present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   if(auto* B=Cast<UEGAponiaSkillBehavior>(Hero->GetHeroSkill()->GetBehavior()))B->DisciplineStacks=6; // Controlled visibility, not natural gain proof.
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     if(FX->GetAsset()->GetName()==TEXT("NS_Aponia_VerdictLightning")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.15f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==3){Test->AddInfo(TEXT("Three phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAponiaEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.Aponia",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FAponiaEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FAponiaEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FVillVEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
 bool Recording=false; TWeakObjectPtr<UWorld> RecordedWorld;
 bool HeardGun=false,HeardCannon=false,HeardAuto=false;
public:
 explicit FVillVEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/VillVGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FVillVEnhancedCapture(){if(Recording&&RecordedWorld.IsValid())UAudioMixerBlueprintLibrary::StopRecordingOutput(RecordedWorld.Get(),EAudioRecordingExportType::WavFile,TEXT("Interrupted"),Directory);if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("VillV whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV.DA_Card_VillV"));
   if(!Test->TestNotNull(TEXT("Formal VillV card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy VillV"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::VillV){Hero=U;break;}
   if(!Test->TestTrue(TEXT("VillV present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   Hero->GetHeroSkill()->ApplyPersistentLevel(11);
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   UAudioMixerBlueprintLibrary::StartRecordingOutput(W,24.f);Recording=true;RecordedWorld=W;
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    if(bUltimateRequested&&!Captured.Contains(TEXT("ultimate"))&&World->GetTimeSeconds()-ObservationStart>7.3f)Phase=TEXT("ultimate");
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(auto* Audio=Hero->FindComponentByClass<UEGCombatAudioComponent>()){HeardAuto|=Audio->LastNode==TEXT("AutoGunShot")||Audio->LastNode==TEXT("AutoCannonShot");HeardGun|=Audio->GetVillVGunLoop()!=nullptr;HeardCannon|=Audio->LastSoundPath.Contains(TEXT("CannonFire"));}
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==3&&W->GetTimeSeconds()-ObservationStart>14.f){
   Test->TestTrue(TEXT("Production preload and real montage create machine gun loop"),HeardGun);
   Test->TestTrue(TEXT("Production real montage creates cannon shot"),HeardCannon);
   Test->TestTrue(TEXT("Level eleven unoccupied device emits shot audio"),HeardAuto);
   UAudioMixerBlueprintLibrary::StopRecordingOutput(W,EAudioRecordingExportType::WavFile,TEXT("MachineMix"),Directory);Recording=false;
   Test->AddInfo(Directory/TEXT("MachineMix.wav"));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVillVEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.VillV",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FVillVEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FVillVEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FEdenEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FEdenEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/EdenGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FEdenEnhancedCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Eden whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden.DA_Card_Eden"));
   if(!Test->TestNotNull(TEXT("Formal Eden card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy Eden"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::Eden){Hero=U;break;}
   if(!Test->TestTrue(TEXT("Eden present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     if(FX->GetAsset()->GetName()==TEXT("NS_Eden_GoldenChord")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.15f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==3){Test->AddInfo(TEXT("Three phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEdenEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.Eden",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEdenEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FEdenEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FCollapsedHuaEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FCollapsedHuaEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/CollapsedHuaGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FCollapsedHuaEnhancedCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("CollapsedHua whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua.DA_Card_CollapsedHua"));
   if(!Test->TestNotNull(TEXT("Formal CollapsedHua card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy CollapsedHua"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::CollapsedHua){Hero=U;break;}
   if(!Test->TestTrue(TEXT("CollapsedHua present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,180,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     if(FX->GetAsset()->GetName()==TEXT("NS_Hua_landingRich")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.15f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==3){Test->AddInfo(TEXT("Three phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCollapsedHuaEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.CollapsedHua",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCollapsedHuaEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FCollapsedHuaEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}

namespace
{
class FGriseoAdultEnhancedCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero;
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Draw;
 TSet<FString> Captured;
 FString Pending,Directory;
 double Deadline=0.;
 bool bConfigured=false,bUltimateRequested=false;
 double ObservationStart=0.;
public:
 explicit FGriseoAdultEnhancedCapture(FAutomationTestBase* In):Test(In)
 {Directory=FPaths::ProjectSavedDir()/TEXT("SevenHeroEnhancement20261108/GriseoAdultGraphics")/FGuid::NewGuid().ToString(EGuidFormats::Digits);}
 virtual ~FGriseoAdultEnhancedCapture(){if(Client.IsValid()&&Draw.IsValid())Client->OnDrawn().Remove(Draw);}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("GriseoAdult whole-set capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!Hero.IsValid())
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   auto* Cards=Mode->GetCards();auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult.DA_Card_GriseoAdult"));
   if(!Test->TestNotNull(TEXT("Formal GriseoAdult card"),Card)||!Cards->GrantCard(Card))return true;
   if(!Test->TestTrue(TEXT("Deploy GriseoAdult"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetFaction_Implementation()==EEGFaction::Player&&U->GetDefinition()->HeroKind==EEGHeroKind::GriseoAdult){Hero=U;break;}
   if(!Test->TestTrue(TEXT("GriseoAdult present"),Hero.IsValid()))return true;
   return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  // New source material usages can compile only when their first component
  // appears. Finish those jobs before selecting any frame of the short cue.
  FAssetCompilingManager::Get().FinishAllCompilation();
  if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
  if(!bConfigured)
  {
   const FVector Origin=Hero->GetActorLocation();int32 Index=0;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(Index++*200-200,1000,0));}
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),1800.f);
   ObservationStart=W->GetTimeSeconds();
   Client=W->GetGameViewport();if(!Client.IsValid()){Test->AddError(TEXT("Missing game viewport"));return true;}
   IFileManager::Get().MakeDirectory(*Directory,true);
   Draw=Client->OnDrawn().AddLambda([this]()
   {
    UWorld* World=GEditor?GEditor->PlayWorld:nullptr;if(!World||!Hero.IsValid()||!Pending.IsEmpty()||FScreenshotRequest::IsScreenshotRequested())return;
    FString Phase;
    if(!Captured.Contains(TEXT("basic"))&&World->GetTimeSeconds()-ObservationStart>3.f)Phase=TEXT("basic");
    for(TActorIterator<AEGUltimateSignetVisual> It(World);It&&Phase.IsEmpty();++It)
     if(It->GetOwner()==Hero.Get()&&It->GetGameTimeSinceCreation()>.35f&&!Captured.Contains(TEXT("signet")))Phase=TEXT("signet");
    for(TActorIterator<AEGChessNiagaraBurst> It(World);It&&Phase.IsEmpty();++It)
    {
     auto* FX=It->FindComponentByClass<UNiagaraComponent>();
     if(It->GetOwner()!=Hero.Get()||!FX||!FX->GetAsset())continue;
     if(FX->GetAsset()->GetName()==TEXT("NS_Griseo_MeteorTrail")&&!Captured.Contains(TEXT("flight"))&&It->GetGameTimeSinceCreation()>.2f)Phase=TEXT("flight");
     if(FX->GetAsset()->GetName()==TEXT("NS_Griseo_MeteorImpact")&&!Captured.Contains(TEXT("ultimate"))&&It->GetGameTimeSinceCreation()>.15f)Phase=TEXT("ultimate");
    }
    if(Phase.IsEmpty())return;
    Pending=Phase;FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(Directory/(Phase+TEXT(".png"))),true,false,false,FIntRect(),true);
   });
   Test->AddInfo(TEXT("Controlled distant-target observation: static enemies 1000cm away, caster range raised to 1800cm only in PIE to prevent approach; ordinary attacks for 5 seconds then fill mana. No production stats changed."));bConfigured=true;
  }
  if(!bUltimateRequested&&W->GetTimeSeconds()-ObservationStart>=5.f)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());
   bUltimateRequested=true;
  }
  if(!Pending.IsEmpty()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString File=Directory/(Pending+TEXT(".png"));
   if(IFileManager::Get().FileSize(*File)>1000){Captured.Add(Pending);Test->AddInfo(File);Pending.Empty();}
  }
  if(Captured.Num()==4){Test->AddInfo(TEXT("Three phase frames saved; visual review required."));return true;}
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGriseoAdultEnhanced1080,"ElyGambit.Integration.SevenHeroEnhanced.GriseoAdult",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGriseoAdultEnhanced1080::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FFiveHeroStartPIE(1920,1080));
 ADD_LATENT_AUTOMATION_COMMAND(FGriseoAdultEnhancedCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
