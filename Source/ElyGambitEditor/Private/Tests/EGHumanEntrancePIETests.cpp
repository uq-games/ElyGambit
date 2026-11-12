#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
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
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGCardDefinition.h"
#include "Placement/EGPlacementVisuals.h"
#include "Presentation/EGHumanEntranceComponent.h"

namespace
{
class FHumanEntranceStartPIE final : public IAutomationLatentCommand
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

class FHumanEntranceCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGDeployedPiece> Piece;
 double Deadline=0.,Started=-1.;
 FString CaptureRunId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 int32 NextCapture=0;
 bool bGranted=false;
public:
 explicit FHumanEntranceCapture(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+120.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Human entrance capture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
  auto* Cards=Mode->GetCards();
  if(!bGranted)
  {
   auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"));
   if(!Test->TestNotNull(TEXT("Formal human card loaded"),Card)||!Test->TestTrue(TEXT("Grant human fixture card"),Cards->GrantCard(Card)))return true;
   const FGuid Id=Cards->GetHand().Last().InstanceId;
   if(!Test->TestTrue(TEXT("Deploy human on formal board"),Cards->TryDeployHandCard(Id,FIntPoint(3,1))))return true;
   for(auto* Candidate:Cards->GetPieces())if(Candidate&&Candidate->InstanceId==Id){Piece=Candidate;break;}
   if(!Test->TestTrue(TEXT("Human deployed and effect started"),Piece.IsValid()&&Piece->FindComponentByClass<UEGHumanEntranceComponent>()!=nullptr))return true;
   Started=W->GetTimeSeconds();bGranted=true;
   Test->AddInfo(TEXT("Controlled fixture: a formal human card is granted and deployed programmatically; screenshots are not real mouse input or visual acceptance."));
   return false;
  }
  if(!Piece.IsValid()){Test->AddError(TEXT("Human piece disappeared during capture"));return true;}
  static constexpr float Times[]={.25f,.7f,1.1f,1.45f,1.8f,2.4f,3.f,3.7f};
  static const TCHAR* Names[]={TEXT("HumanEntrance_025"),TEXT("HumanEntrance_070"),TEXT("HumanEntrance_110"),TEXT("HumanEntrance_145"),TEXT("HumanEntrance_180"),TEXT("HumanEntrance_240"),TEXT("HumanEntrance_300"),TEXT("HumanEntrance_370")};
  if(NextCapture<UE_ARRAY_COUNT(Times)&&W->GetTimeSeconds()-Started>=Times[NextCapture]&&!FScreenshotRequest::IsScreenshotRequested())
  {
   const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(FString(Names[NextCapture])+TEXT("_")+CaptureRunId+TEXT(".png")));
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
   FScreenshotRequest::RequestScreenshot(Path,true,false,false,FIntRect(),true);
   const auto* Viewport=W->GetGameViewport()?W->GetGameViewport()->Viewport:nullptr;
   const FIntPoint Size=Viewport?Viewport->GetSizeXY():FIntPoint::ZeroValue;
   Test->AddInfo(FString::Printf(TEXT("Capture requested at %.3fs, viewport %dx%d: %s"),W->GetTimeSeconds()-Started,Size.X,Size.Y,*Path));
   ++NextCapture;return false;
  }
  if(NextCapture==UE_ARRAY_COUNT(Times)&&!FScreenshotRequest::IsScreenshotRequested())
  {
   Test->TestNull(TEXT("Entrance effect cleaned after its duration"),Piece->FindComponentByClass<UEGHumanEntranceComponent>());
   return true;
  }
  return false;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanEntranceGraphical,
 "ElyGambit.Integration.HumanEntranceGraphical",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanEntranceGraphical::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Human entrance capture requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Human entrance capture requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FHumanEntranceStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FHumanEntranceCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
