#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "Blueprint/UserWidget.h"
#include "UObject/UnrealType.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Sound/SoundBase.h"

namespace
{
enum class EMovieCase : uint8 { Complete, Cancel, MissingSource };

class FEntryMovieStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1920,1080))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

struct FEntryMovieCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 bool bRequested=false,bMovieAtDraw=false;
 ~FEntryMovieCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FEntryMoviePIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 EMovieCase Case;
 TWeakObjectPtr<AEGChessUnit> Human,Enemy;
 TWeakObjectPtr<UEGElysiaHerrscherPresentationComponent> Presentation;
 TWeakObjectPtr<UEGCombatAudioComponent> CombatAudio;
 TSet<TWeakObjectPtr<UAudioComponent>> ObservedEntryVoices;
 TSharedPtr<FEntryMovieCapture> Capture;
 FGuid Action,Round;
 double Deadline=0.,PlayStarted=0.,PauseStarted=0.,FinishedAt=0.,FrozenWorldTime=0.;
 FVector FrozenEnemy=FVector::ZeroVector;
 int32 Images=0;
 bool bSetup=false,bInjected=false,bSawAction=false,bSawPause=false,bSawPlaying=false,bCancelled=false,bFinished=false;
 bool bVoiceEarly=false,bVoiceLate=false;

 void ObserveEntryVoice(double Elapsed)
 {
  auto* Audio=CombatAudio.Get();
  const auto* ArrayProperty=Audio?FindFProperty<FArrayProperty>(Audio->GetClass(),TEXT("Voices")):nullptr;
  const auto* ItemProperty=ArrayProperty?CastField<FObjectPropertyBase>(ArrayProperty->Inner):nullptr;
  if(!ArrayProperty||!ItemProperty)return;
  FScriptArrayHelper Voices(ArrayProperty,ArrayProperty->ContainerPtrToValuePtr<void>(Audio));
  UAudioComponent* Current=nullptr;int32 Matches=0;
  for(int32 I=0;I<Voices.Num();++I)
  {
   auto* Voice=Cast<UAudioComponent>(ItemProperty->GetObjectPropertyValue(Voices.GetRawPtr(I)));
   auto* Sound=Voice?Voice->GetSound():nullptr;
   if(!Sound||!Sound->GetPathName().Contains(TEXT("VO_L_ELY_C2_ULT_3_4ac4e4eb9418")))continue;
   ++Matches;Current=Voice;ObservedEntryVoices.Add(Voice);
  }
  const bool bSampleEarly=!bVoiceEarly&&Elapsed>=1.2&&Elapsed<3.0;
  const bool bSampleLate=!bVoiceLate&&Elapsed>=3.5&&Elapsed<4.6;
  if(!bSampleEarly&&!bSampleLate)return;
  Test->TestEqual(TEXT("One ULT_3 voice is tracked at this sample"),Matches,1);
  Test->TestTrue(TEXT("ULT_3 voice is playing and marked UI sound during pause"),Current&&Current->IsPlaying()&&Current->bIsUISound);
  Test->AddInfo(FString::Printf(TEXT("Entry audio sample movie=%.3fs voice=%s sound=%s matches=%d playing=%d ui=%d observedInstances=%d; runtime state, not a listening test"),
   Elapsed,*GetPathNameSafe(Current),Current?*GetPathNameSafe(Current->GetSound()):TEXT("none"),Matches,
   Current&&Current->IsPlaying()?1:0,Current&&Current->bIsUISound?1:0,ObservedEntryVoices.Num()));
  if(bSampleEarly)bVoiceEarly=true;
  if(bSampleLate)bVoiceLate=true;
 }

 bool CaptureFrame(UWorld* World,const TCHAR* Label,bool bRequireMovie)
 {
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return false;
  Capture=MakeShared<FEntryMovieCapture>();Capture->Client=Client;
  Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ElysiaCombatAudio20261107/MoviePIE")/
   (FString(Label)+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
  const TWeakPtr<FEntryMovieCapture> Weak=Capture;
  const TWeakObjectPtr<UEGElysiaHerrscherPresentationComponent> WeakPresentation=Presentation;
  Capture->Handle=Client->OnDrawn().AddLambda([Weak,WeakPresentation,bRequireMovie]()
  {
   auto State=Weak.Pin();if(!State.IsValid()||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   const bool bPlaying=WeakPresentation.IsValid()&&WeakPresentation->IsEntryMoviePlaying();
   if(bRequireMovie&&!bPlaying)return;
   State->bMovieAtDraw=bPlaying;State->bRequested=true;
   FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  });
  return true;
 }

 bool CompleteCapture()
 {
  if(!Capture.IsValid()||!Capture->bRequested||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;
  if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
  Test->TestEqual(TEXT("Movie PIE screenshot dimensions"),FIntPoint(int32(BE(16)),int32(BE(20))),FIntPoint(1920,1080));
  Test->AddInfo(FString::Printf(TEXT("Movie PIE capture playing=%d path=%s"),Capture->bMovieAtDraw?1:0,*Capture->Path));
  Capture.Reset();return true;
 }

public:
 FEntryMoviePIE(FAutomationTestBase* InTest,EMovieCase InCase):Test(InTest),Case(InCase){}
 virtual bool Update() override
 {
  const double Now=FPlatformTime::Seconds();if(Deadline==0.)Deadline=Now+120.;
  if(Now>Deadline){Test->AddError(TEXT("Entry movie PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* GM=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!GM||!GM->GetCards()||!GM->BattleRules)return false;
  if(!bSetup)
  {
   if(!GM->CanPrepare()||!GM->AreStageResourcesReady())return false;
   auto* Cards=GM->GetCards();FGuid CardId;
   for(const auto& C:Cards->GetHand())if(C.Definition&&C.Definition->HeroDefinition&&C.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher)CardId=C.InstanceId;
   if(!CardId.IsValid())
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"));
    if(!Test->TestNotNull(TEXT("Formal human card"),Card)||!Test->TestTrue(TEXT("Grant human card"),Cards->GrantCard(Card)))return true;
    CardId=Cards->GetHand().Last().InstanceId;
   }
   if(!Test->TestTrue(TEXT("Deploy human"),Cards->TryDeployHandCard(CardId,{2,1}))||!Test->TestTrue(TEXT("Start combat"),GM->EnterCombat()))return true;
   for(auto* Unit:GM->GetCombatUnits())if(Unit&&Unit->GetDefinition()&&Unit->GetAbilitySystemComponent())
   {
    if(Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=Unit;
    else if(Unit->GetFaction_Implementation()==EEGFaction::Enemy&&!Enemy.IsValid())Enemy=Unit;
    auto* ASC=Unit->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000000.f);
   }
   if(!Test->TestTrue(TEXT("Human and enemy spawned"),Human.IsValid()&&Enemy.IsValid()))return true;
   auto* Component=Human->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
   if(!Test->TestNotNull(TEXT("Formal presentation component"),Component))return true;
   if(Case==EMovieCase::MissingSource)
   {
    // Isolated PIE actor only: replace its component before the action, so no
    // previously preloaded formal player can mask the missing-source path.
    const auto StarSeaClass=Component->StarSeaClass;
    auto* StarfieldSystem=Component->OriginStarfieldSystem.Get();
    const FTransform StarfieldTransform=Component->StarfieldTransform;
    Component->DestroyComponent();
    Component=NewObject<UEGElysiaHerrscherPresentationComponent>(Human.Get());
    Human->AddInstanceComponent(Component);
    Component->StarSeaClass=StarSeaClass;
    Component->OriginStarfieldSystem=StarfieldSystem;
    Component->StarfieldTransform=StarfieldTransform;
    Component->bEntryMovieEnabled=true;
    Component->EntryMovieSource=TSoftObjectPtr<UFileMediaSource>(FSoftObjectPath(TEXT("/Game/DoesNotExist/EG_EntryMovieMissing.EG_EntryMovieMissing")));
    Component->RegisterComponent();
   }
   else if(!Test->TestTrue(TEXT("Formal movie is enabled and bound"),Component->bEntryMovieEnabled&&!Component->EntryMovieSource.IsNull()))return true;
   Presentation=Component;
   if(Case==EMovieCase::Complete)
   {
    CombatAudio=Human->FindComponentByClass<UEGCombatAudioComponent>();
    if(!Test->TestTrue(TEXT("Combat audio component and reflected Voices array exist"),
     CombatAudio.IsValid()&&FindFProperty<FArrayProperty>(CombatAudio->GetClass(),TEXT("Voices"))!=nullptr))return true;
   }
   Test->AddInfo(TEXT("Controlled graphical PIE: formal card deployed, health raised and human mana filled once; missing-source case replaces only the transient PIE component. Not natural play or visual acceptance."));
   bSetup=true;return false;
  }
  if(Capture.IsValid()&&!CompleteCapture())return false;
  if(!Human.IsValid()||!Enemy.IsValid()||!Presentation.IsValid()){Test->AddError(TEXT("Entry movie fixture actor or presentation disappeared"));return true;}
  auto* Skill=Human->GetHeroSkill();auto* Battle=GM->BattleRules.Get();
  auto* Behavior=Skill?Cast<UEGElysiaHerrscherSkillBehavior>(Skill->GetBehavior()):nullptr;
  if(!Skill||!Battle||!Behavior){Test->AddError(TEXT("Missing production battle or skill"));return true;}
  if(!bInjected)
  {
   if(!Battle->IsCombatRunning())return false;
   Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Human->GetAttributes()->GetMaxMana());
   bInjected=true;return false;
  }
  const auto Phase=Behavior->GetPresentationPhase();
  const auto ReadEffect=[&](const TCHAR* Name)->UObject*
  {
   const auto* Property=FindFProperty<FObjectPropertyBase>(Presentation->GetClass(),Name);
   return Property?Property->GetObjectPropertyValue_InContainer(Presentation.Get()):nullptr;
  };
  if(Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::EntryReleased||Presentation->IsEntryMoviePlaying())
  {
   Test->TestNull(TEXT("Ground star sea waits for completed entry"),ReadEffect(TEXT("StarSea")));
   Test->TestNull(TEXT("Origin starfield waits for completed entry"),ReadEffect(TEXT("OriginStarfield")));
  }
  if(!bSawAction&&(Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::EntryReleased))
  {bSawAction=true;Action=Skill->ActionId;Round=Battle->GetRoundId();}
  if(!bSawAction)return false;
  const bool bOwned=Battle->IsHumanEntryPauseOwner(Human.Get(),Action);
  if(bOwned&&!bSawPause)
  {bSawPause=true;PauseStarted=Now;FrozenWorldTime=World->GetTimeSeconds();FrozenEnemy=Enemy->GetActorLocation();}
  if(bOwned&&bSawPause)
  {
   Test->TestTrue(TEXT("Pause freezes world time"),FMath::Abs(World->GetTimeSeconds()-FrozenWorldTime)<.01);
   Test->TestTrue(TEXT("Pause freezes enemy position"),Enemy->GetActorLocation().Equals(FrozenEnemy,.1));
  }
  if(Presentation->IsEntryMoviePlaying()&&!bSawPlaying)
  {
   bSawPlaying=true;PlayStarted=Now;
   Test->TestTrue(TEXT("Movie only begins under same action pause"),bOwned&&Presentation->IsEntryMoviePendingFor(Round,Action));
  }
  if(bSawPlaying&&Case!=EMovieCase::MissingSource&&!bFinished)
  {
   const double Elapsed=Now-PlayStarted;
   if(Presentation->IsEntryMoviePlaying())
   {
    Test->TestTrue(TEXT("Playing movie retains pause ownership"),bOwned);
    if(Case==EMovieCase::Complete)ObserveEntryVoice(Elapsed);
    if(Case==EMovieCase::Cancel&&Elapsed>=1.2)
    {Skill->CancelAction();bCancelled=true;bFinished=true;FinishedAt=Now;return false;}
    const double CaptureAt[]={.8,2.,3.5};
    if(Case==EMovieCase::Complete&&Images<3&&Elapsed>=CaptureAt[Images])
    {
     // Texture extent and widget opacity are observations, not a substitute
     // for inspecting the resulting screenshot pixels.
     const auto* TextureProp=FindFProperty<FObjectPropertyBase>(Presentation->GetClass(),TEXT("EntryMovieTexture"));
     const auto* WidgetProp=FindFProperty<FObjectPropertyBase>(Presentation->GetClass(),TEXT("EntryMovieWidget"));
     auto* Texture=TextureProp?Cast<UTexture>(TextureProp->GetObjectPropertyValue_InContainer(Presentation.Get())):nullptr;
     auto* Widget=WidgetProp?Cast<UUserWidget>(WidgetProp->GetObjectPropertyValue_InContainer(Presentation.Get())):nullptr;
     Test->TestTrue(TEXT("Movie layer has a non-placeholder texture and is visible"),Texture&&Texture->GetSurfaceWidth()>2&&Texture->GetSurfaceHeight()>2&&Widget&&Widget->GetRenderOpacity()>.9f);
     const FString Label=FString::Printf(TEXT("movie_%d"),Images);
     if(CaptureFrame(World,*Label,true))++Images;
     return false;
    }
    return false;
   }
   if(Case==EMovieCase::Complete)
   {
    Test->TestEqual(TEXT("Three movie captures were requested while playing"),Images,3);
    Test->TestTrue(TEXT("4.9 second movie played for its expected duration"),Elapsed>=4.2&&Elapsed<=7.0);
    Test->TestTrue(TEXT("ULT_3 voice sampled early and late during movie"),bVoiceEarly&&bVoiceLate);
    Test->TestEqual(TEXT("One ULT_3 voice component observed for this entry"),ObservedEntryVoices.Num(),1);
    bFinished=true;FinishedAt=Now;
   }
  }
  if(Case==EMovieCase::MissingSource&&!bFinished&&bSawPause&&!bOwned)
  {bFinished=true;FinishedAt=Now;Test->TestFalse(TEXT("Missing source never starts playback"),bSawPlaying);}
  if(!bFinished)return false;
  if(Now-FinishedAt<.35)return false;
  Test->TestFalse(TEXT("Movie pending clears"),Presentation->IsEntryMoviePending());
  Test->TestFalse(TEXT("Movie playing clears"),Presentation->IsEntryMoviePlaying());
  Test->TestFalse(TEXT("Entry pause clears"),Battle->HasHumanEntryPause());
  Test->TestFalse(TEXT("Board world resumes"),World->IsPaused());
  if(Case==EMovieCase::Cancel)Test->TestTrue(TEXT("Playback was cancelled"),bCancelled);
  else
  {
   Test->TestTrue(TEXT("Natural Finish enters origin"),Behavior->IsOriginActive());
   Test->TestTrue(TEXT("Ground star sea appears after entry finishes"),IsValid(ReadEffect(TEXT("StarSea")))||IsValid(ReadEffect(TEXT("OriginStarfield"))));
  }
  if(Images<4&&Case==EMovieCase::Complete)
  {if(CaptureFrame(World,TEXT("board_after_movie"),false))Images=4;return false;}
  if(Case==EMovieCase::Complete)Test->AddInfo(FString::Printf(TEXT("Observed entry pause %.3fs; movie play edge to end %.3fs"),FinishedAt-PauseStarted,FinishedAt-PlayStarted));
  return true;
 }
};

bool QueueEntryMovie(FAutomationTestBase* Test,EMovieCase Case)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Entry movie test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Entry movie test requires a graphical RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEntryMovieStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FEntryMoviePIE(Test,Case));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEntryMovieCompletePIE,"ElyGambit.Integration.HumanEntryMovie.Complete",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEntryMovieCompletePIE::RunTest(const FString&){return QueueEntryMovie(this,EMovieCase::Complete);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEntryMovieCancelPIE,"ElyGambit.Integration.HumanEntryMovie.Cancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEntryMovieCancelPIE::RunTest(const FString&){return QueueEntryMovie(this,EMovieCase::Cancel);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEntryMovieMissingPIE,"ElyGambit.Integration.HumanEntryMovie.MissingSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEntryMovieMissingPIE::RunTest(const FString&){return QueueEntryMovie(this,EMovieCase::MissingSource);}
#endif
