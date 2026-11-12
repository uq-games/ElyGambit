#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
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
#include "GameFramework/PlayerController.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGPlacementVisuals.h"

namespace
{
class FElysiaSizingStartPIE final : public IAutomationLatentCommand
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
  if(!GUnrealEd->CheckForPlayerStart())FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

struct FElysiaSizingCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 bool bRequested=false;
 ~FElysiaSizingCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FElysiaSizingPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.,DeployedAt=0.;
 TWeakObjectPtr<AEGDeployedPiece> Ordinary,Human,Eden;
 TSharedPtr<FElysiaSizingCapture> Capture;
 bool bMeasured=false;

 bool Measure(AEGDeployedPiece* Piece,const TCHAR* Name,float& OutHeight,APlayerController* PC)
 {
  auto* Body=Piece?Piece->Body.Get():nullptr;
  auto* Vitals=Piece?Piece->PreparationVitals.Get():nullptr;
  auto* Widget=Vitals?Vitals->GetUserWidgetObject():nullptr;
  auto* Badge=Widget?Widget->GetWidgetFromName(TEXT("PreparationBadge")):nullptr;
  if(!Test->TestNotNull(FString::Printf(TEXT("%s body"),Name),Body)||
     !Test->TestNotNull(FString::Printf(TEXT("%s badge"),Name),Badge)||
     !Test->TestTrue(FString::Printf(TEXT("%s body visible"),Name),Body->IsVisible()&&!Body->bHiddenInGame))return false;
  FName Head=NAME_None;float LowestFoot=TNumericLimits<float>::Max();int32 Feet=0;
  for(int32 I=0;I<Body->GetNumBones();++I)
  {
   const FName Bone=Body->GetBoneName(I);const FString BoneName=Bone.ToString();
   if(BoneName.EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))Head=Bone;
   if(BoneName.EndsWith(TEXT("Foot"),ESearchCase::IgnoreCase)&&!BoneName.Contains(TEXT("IK")))
   {LowestFoot=FMath::Min(LowestFoot,float(Body->GetBoneLocation(Bone).Z));++Feet;}
  }
  if(!Test->TestTrue(FString::Printf(TEXT("%s has head and foot bones"),Name),!Head.IsNone()&&Feet>0))return false;
  const FVector HeadPosition=Body->GetBoneLocation(Head);
  OutHeight=float(HeadPosition.Z)-LowestFoot;
  if(!Test->TestTrue(FString::Printf(TEXT("%s has measurable body height"),Name),FMath::IsFinite(OutHeight)&&OutHeight>30.f))return false;
  FVector2D Crown;
  if(!Test->TestTrue(FString::Printf(TEXT("%s head projects into viewport"),Name),
   UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,HeadPosition+FVector(0,0,15.f),Crown,true)))return false;
  const FGeometry Viewport=UWidgetLayoutLibrary::GetPlayerScreenWidgetGeometry(PC);
  const FVector2D CrownAbsolute=Viewport.LocalToAbsolute(Crown);
  const FSlateRect BadgeRect=Badge->GetCachedGeometry().GetRenderBoundingRect();
  Test->AddInfo(FString::Printf(TEXT("%s body height %.2f cm; head %.1f %.1f; badge [%.1f %.1f %.1f %.1f]; projected crown %.1f %.1f"),
   Name,OutHeight,HeadPosition.X,HeadPosition.Z,BadgeRect.Left,BadgeRect.Top,BadgeRect.Right,BadgeRect.Bottom,CrownAbsolute.X,CrownAbsolute.Y));
  Test->TestTrue(FString::Printf(TEXT("%s badge is laid out and visible"),Name),Badge->IsVisible()&&BadgeRect.Right>BadgeRect.Left&&BadgeRect.Bottom>BadgeRect.Top);
  Test->TestTrue(FString::Printf(TEXT("%s badge leaves a visible gap above the projected crown"),Name),BadgeRect.Bottom+2.f<CrownAbsolute.Y);
  return true;
 }

public:
 explicit FElysiaSizingPIE(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+90.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Sizing PIE timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* GM=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Cards=GM?GM->GetCards():nullptr;
  if(!GM||!Cards||!GM->CanPrepare()||!GM->AreStageResourcesReady())return false;
  if(DeployedAt==0.)
  {
   struct FTarget {EEGHeroKind Kind;const TCHAR* Card;FIntPoint Cell;};
   const FTarget Targets[]={
    {EEGHeroKind::Elysia,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia.DA_Card_Elysia"),{1,1}},
    {EEGHeroKind::ElysiaHerrscher,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"),{3,1}},
    {EEGHeroKind::Eden,TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden.DA_Card_Eden"),{5,1}}};
   for(const auto& Target:Targets)
   {
    FGuid Id;
    for(const auto& Card:Cards->GetHand())if(Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==Target.Kind){Id=Card.InstanceId;break;}
    if(!Id.IsValid())
    {
     auto* Card=LoadObject<UEGCardDefinition>(nullptr,Target.Card);
     if(!Test->TestNotNull(TEXT("Controlled reference card"),Card)||!Test->TestTrue(TEXT("Grant controlled reference card"),Cards->GrantCard(Card)))return true;
     Id=Cards->GetHand().Last().InstanceId;
    }
    if(!Test->TestTrue(TEXT("Deploy through formal card manager"),Cards->TryDeployHandCard(Id,Target.Cell)))return true;
   }
   for(auto* Piece:Cards->GetPieces())if(Piece&&Piece->HeroVisualDefinition)
   {
    const auto Kind=Piece->HeroVisualDefinition->HeroKind;
    if(Kind==EEGHeroKind::Elysia)Ordinary=Piece;
    else if(Kind==EEGHeroKind::ElysiaHerrscher)Human=Piece;
    else if(Kind==EEGHeroKind::Eden)Eden=Piece;
   }
   if(!Test->TestTrue(TEXT("All three preparation units spawned"),Ordinary.IsValid()&&Human.IsValid()&&Eden.IsValid()))return true;
   Test->AddInfo(TEXT("Controlled preparation: two opening cards and a granted Eden reference, deployed via CardManager; waits for entrance animations. This is not real mouse input or user visual acceptance."));
   DeployedAt=FPlatformTime::Seconds();return false;
  }
  if(FPlatformTime::Seconds()-DeployedAt<3.5)return false;
  auto* PC=W->GetFirstPlayerController();
  if(!PC||!Ordinary.IsValid()||!Human.IsValid()||!Eden.IsValid()){Test->AddError(TEXT("Preparation fixture disappeared"));return true;}
  if(!bMeasured)
  {
   float A=0.f,B=0.f,E=0.f;
   if(!Measure(Ordinary.Get(),TEXT("Ordinary Elysia"),A,PC)||!Measure(Human.Get(),TEXT("Human Elysia"),B,PC)||!Measure(Eden.Get(),TEXT("Eden"),E,PC))return true;
   Test->TestTrue(TEXT("Ordinary adult height remains comparable to Eden"),A/E>=.80f&&A/E<=1.15f);
   Test->TestTrue(TEXT("Human adult height remains comparable to Eden"),B/E>=.80f&&B/E<=1.15f);
   Test->TestTrue(TEXT("Two forms of Elysia have comparable body heights"),A/B>=.90f&&A/B<=1.10f);
   Test->AddInfo(FString::Printf(TEXT("Measured head-to-foot ratios ordinary/Eden=%.3f human/Eden=%.3f ordinary/human=%.3f"),A/E,B/E,A/B));
   auto* Client=W->GetGameViewport();
   if(!Test->TestNotNull(TEXT("PIE viewport"),Client))return true;
   Capture=MakeShared<FElysiaSizingCapture>();Capture->Client=Client;
   Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ElysiaNiagaraRevision20261107/Validation/Preparation_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png"));
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
   const TWeakPtr<FElysiaSizingCapture> Weak=Capture;
   Capture->Handle=Client->OnDrawn().AddLambda([Weak]()
   {auto State=Weak.Pin();if(!State.IsValid()||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
    State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);});
   bMeasured=true;return false;
  }
  if(!Capture.IsValid()||!Capture->bRequested||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  Test->AddInfo(FString::Printf(TEXT("Preparation screenshot: %s"),*Capture->Path));Capture.Reset();return true;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaRevisionSizingPIE,"ElyGambit.Integration.ElysiaRevisionPreparationSizing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaRevisionSizingPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender()){AddError(TEXT("Requires idle editor and rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FElysiaSizingStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FElysiaSizingPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
