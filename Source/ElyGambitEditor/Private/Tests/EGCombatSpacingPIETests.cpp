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
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Character/EGDemoCombatUnit.h"
#include "Actor/EGArtPiece.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGCombatTextLayer.h"
#include "UI/HUD/EGHUD.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "AbilitySystem/EGAttributeSet.h"

namespace
{
// The screenshot request is global. Issue it from the PIE viewport's draw
// callback so an editor viewport cannot consume a preparation capture.
struct FSpacingCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FSpacingCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

TSharedPtr<FSpacingCapture> StartSpacingCapture(UWorld* World,const FString& Label)
{
 auto* Client=World?World->GetGameViewport():nullptr;
 if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return nullptr;
 auto Capture=MakeShared<FSpacingCapture>();
 Capture->Client=Client;
 Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("BoardViewCorrection20261104")/
  (Label+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
 const TWeakPtr<FSpacingCapture> Weak=Capture;
 Capture->Handle=Client->OnDrawn().AddLambda([Weak]()
 {
  auto State=Weak.Pin();if(!State.IsValid())return;
  ++State->Draws;
  if(State->Draws>=3&&!State->bRequested&&State->Client.IsValid()&&
     State->Client->GetGameViewportWidget().IsValid()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   State->bRequested=true;
   FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  }
 });
 return Capture;
}

bool SpacingCaptureReady(const TSharedPtr<FSpacingCapture>& Capture,FIntPoint Expected,FAutomationTestBase* Test)
{
 if(!Capture.IsValid()||!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
 TArray<uint8> Bytes;
 if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
 const int32 End=Bytes.Num()-12;
 if(Bytes[End]!=0||Bytes[End+1]!=0||Bytes[End+2]!=0||Bytes[End+3]!=0||
    Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
 const uint8 Signature[]={137,80,78,71,13,10,26,10};
 for(int32 I=0;I<8;++I)if(Bytes[I]!=Signature[I]){Test->AddError(TEXT("Capture is not a PNG"));return true;}
 const auto ReadBE=[&](int32 Offset)->uint32
 {return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
 Test->TestEqual(TEXT("Game viewport PNG matches requested dimensions"),
  FIntPoint(static_cast<int32>(ReadBE(16)),static_cast<int32>(ReadBE(20))),Expected);
 Test->AddInfo(FString::Printf(TEXT("PIE viewport capture: %s; visual contents must be inspected separately."),*Capture->Path));
 return true;
}

void LogActualBoardView(FAutomationTestBase* Test,UWorld* World,AEGDemoGameMode* Mode,FIntPoint Size,const TCHAR* Phase)
{
 auto* PC=World?World->GetFirstPlayerController():nullptr;
 auto* Grid=Mode&&Mode->GetBoard()?Mode->GetBoard()->LayoutSource.Get():nullptr;
 if(!Test->TestNotNull(TEXT("Board-view player controller"),PC)||!Test->TestNotNull(TEXT("Board-view production grid"),Grid)||
    !Test->TestNotNull(TEXT("Board-view active camera"),PC?PC->PlayerCameraManager.Get():nullptr))return;
 const auto* Camera=PC->PlayerCameraManager.Get();
 const FVector Focus=Grid->GetActorLocation();
 Test->AddInfo(FString::Printf(TEXT("%s camera: location=%s rotation=%s FOV=%.2f distance-to-board-centre=%.1f cm viewport=%dx%d"),
  Phase,*Camera->GetCameraLocation().ToString(),*Camera->GetCameraRotation().ToString(),Camera->GetFOVAngle(),
  FVector::Dist(Camera->GetCameraLocation(),Focus),Size.X,Size.Y));
 double MinX=DBL_MAX,MinY=DBL_MAX,MaxX=-DBL_MAX,MaxY=-DBL_MAX;
 for(const float X:{-1.f,1.f})for(const float Y:{-1.f,1.f})
 {
  const FVector Corner=Grid->GetActorTransform().TransformPosition(FVector(X*Grid->Columns*Grid->CellWidth*.5f,Y*Grid->Rows*Grid->CellDepth*.5f,0));
  FVector2D Pixel;
  if(!Test->TestTrue(TEXT("Actual playable corner projects"),PC->ProjectWorldLocationToScreen(Corner,Pixel,true)))return;
  Test->AddInfo(FString::Printf(TEXT("%s board corner local-sign=(%.0f,%.0f) projected=(%.1f,%.1f) px"),Phase,X,Y,Pixel.X,Pixel.Y));
  MinX=FMath::Min(MinX,Pixel.X);MaxX=FMath::Max(MaxX,Pixel.X);MinY=FMath::Min(MinY,Pixel.Y);MaxY=FMath::Max(MaxY,Pixel.Y);
 }
 Test->TestTrue(TEXT("All four playable corners stay in the game viewport"),MinX>=0.&&MinY>=0.&&MaxX<=Size.X&&MaxY<=Size.Y);
 Test->AddInfo(FString::Printf(TEXT("%s projected playable board: width %.1f%%, height %.1f%%; bounds (%.1f,%.1f)-(%.1f,%.1f) px; HUD overlap requires image review."),
  Phase,100.*(MaxX-MinX)/Size.X,100.*(MaxY-MinY)/Size.Y,MinX,MinY,MaxX,MaxY));
}

void CheckMasterModelRatio(FAutomationTestBase* Test,UWorld* World,AEGDemoGameMode* Mode)
{
 int32 VisibleMasters=0;
 for(TActorIterator<AEGArtPiece> It(World);It;++It)
 {
  auto* Master=*It;
  if(!IsValid(Master)||Master->GetClass()!=AEGArtPiece::StaticClass()||Master->GetOwner()!=Mode||Master->IsHidden()||!Master->HeroVisualDefinition)continue;
  ++VisibleMasters;
  const float Normal=FEGBoardModelSizing::Resolve(Master->HeroVisualDefinition,Master);
  const float MasterScale=Master->GetBoardModelScale();
  if(!Test->TestTrue(TEXT("Normal model sizing resolves to a positive value"),Normal>KINDA_SMALL_NUMBER))continue;
  const float Ratio=MasterScale/Normal;
  Test->AddInfo(FString::Printf(TEXT("Visible master %s: source MeshScale=%s, actor scale=%s, actual model multiplier %.4f, same-definition normal multiplier %.4f, ratio %.4f. Normal multiplier is resolved configuration, not a second live actor."),
   *Master->HeroVisualDefinition->HeroId.ToString(),*Master->HeroVisualDefinition->GetMeshScale().ToString(),
   *Master->GetActorScale3D().ToString(),MasterScale,Normal,Ratio));
  if(const auto* Mesh=Master->Body.Get())
  {
   auto* PC=World->GetFirstPlayerController();FVector2D Feet,Head;
   for(int32 I=0;PC&&I<Mesh->GetNumBones();++I)
   {
    const FName Bone=Mesh->GetBoneName(I);if(!Bone.ToString().EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))continue;
    if(PC->ProjectWorldLocationToScreen(Master->GetActorLocation(),Feet,true)&&PC->ProjectWorldLocationToScreen(Mesh->GetBoneLocation(Bone),Head,true))
     Test->AddInfo(FString::Printf(TEXT("Visible master %s: world body component scale=%s projected feet=(%.1f,%.1f) head=(%.1f,%.1f), head-feet %.1f px; visual clipping is reviewed in the image."),
      *Master->HeroVisualDefinition->HeroId.ToString(),*Mesh->GetComponentScale().ToString(),Feet.X,Feet.Y,Head.X,Head.Y,FVector2D::Distance(Feet,Head)));
    break;
   }
  }
  Test->TestTrue(TEXT("Master model multiplier is 1.5 times same-definition normal model sizing"),FMath::IsNearlyEqual(Ratio,1.5f,.02f));
  Test->TestTrue(TEXT("Master actor root remains unit scale"),Master->GetActorScale3D().Equals(FVector::OneVector,.001f));
 }
 Test->TestTrue(TEXT("At least one visible production board master was checked"),VisibleMasters>0);
}

void LogCombatUnitHeads(FAutomationTestBase* Test,UWorld* World,AEGDemoGameMode* Mode)
{
 auto* PC=World?World->GetFirstPlayerController():nullptr;if(!PC)return;
 int32 Count=0;
 for(auto* Unit:Mode->GetCombatUnits())
 {
  if(!IsValid(Unit)||!Unit->GetDefinition()||Unit->IsAuxiliaryEmplacement())continue;
  const auto* Mesh=Unit->GetVisiblePresentationMesh();if(!Mesh)continue;
  for(int32 I=0;I<Mesh->GetNumBones();++I)
  {
   const FName Bone=Mesh->GetBoneName(I);if(!Bone.ToString().EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))continue;
   FVector2D Feet,Head;
   const FVector Ground=Unit->GetActorLocation()-FVector(0,0,88);
   if(PC->ProjectWorldLocationToScreen(Ground,Feet,true)&&PC->ProjectWorldLocationToScreen(Mesh->GetBoneLocation(Bone),Head,true))
   {
    Test->AddInfo(FString::Printf(TEXT("Combat unit %s faction=%d head=(%.1f,%.1f) feet=(%.1f,%.1f) projected head-feet %.1f px; pose may differ from masters."),
     *Unit->GetDefinition()->HeroId.ToString(),int32(Unit->GetFaction_Implementation()),Head.X,Head.Y,Feet.X,Feet.Y,FVector2D::Distance(Feet,Head)));
    ++Count;
   }
   break;
  }
 }
 Test->AddInfo(FString::Printf(TEXT("Combat head/feet projections recorded for %d live units; screenshots determine clipping and readability."),Count));
}

class FEGSpacingStartSizedPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FEGSpacingStartSizedPIE(FIntPoint InSize):Size(InSize){}
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0))
   .ClientSize(FVector2D(Size.X,Size.Y)).AutoCenter(EAutoCenter::None)
   .CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};
// A short production-map encounter: transactions and the optional Vill-V card
// are programmatic, but health, time, AI, movement, abilities and hits are not injected.
class FEGSpacingProductionScenario final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint CaptureSize;
 bool bVillV;
 bool bPrepared=false,bStarted=false,bRunning=false,bFinished=false,bSawDamage=false,bSawDevice=false,bSawVillVHit=false;
 double Deadline=0.;
 double CombatStart=0.;
 double LastFrameWall=0.;
 double PreparationReadyTime=0.;
 TArray<double> CombatFrameIntervalsMs;
 int32 Samples=0,Overlaps=0;
 int32 VisibleVitalsSamples=0,NonzeroVitalsSamples=0,MaxCombatNumbers=0,PresentedCombatNumbers=0;
 TSharedPtr<FSpacingCapture> PreparationCapture,CombatCapture,PhaseCapture;
 bool bCombatCaptured=false;
 TSet<FString> CapturedPhases;
 struct FMotionObservation { FVector Position=FVector::ZeroVector; double Time=0.; double CurrentStop=0.; };
 TMap<TWeakObjectPtr<AEGChessUnit>,FMotionObservation> Motion;
 double PursuitSeconds=0.,PursuitDistance=0.,ExpectedDistance=0.,StillSeconds=0.,LongestStop=0.;
 int32 PursuitSamples=0;

 TSharedPtr<FSpacingCapture> Screenshot(UWorld* World,const TCHAR* Phase)
 {
  if(!FApp::CanEverRender())return nullptr;
  return StartSpacingCapture(World,FString::Printf(TEXT("%s_%dx%d_%s"),
   bVillV?TEXT("VillVInjected"):TEXT("Opening"),CaptureSize.X,CaptureSize.Y,Phase));
 }
public:
 FEGSpacingProductionScenario(FAutomationTestBase* InTest,FIntPoint InSize,bool bInVillV)
  :Test(InTest),CaptureSize(InSize),bVillV(bInVillV){}

 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+150.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Production spacing scenario timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch())return false;
  auto* Cards=Mode->GetCards();
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(bPrepared)
   {
    if(FApp::CanEverRender())
    {
     // Let the real master reveal settle before requesting its comparison frame.
     if(World->GetTimeSeconds()<PreparationReadyTime)return false;
     if(!PreparationCapture.IsValid())
     {
      PreparationCapture=Screenshot(World,TEXT("Preparation"));
      if(!Test->TestTrue(TEXT("Preparation capture has a game viewport"),PreparationCapture.IsValid()))return true;
      return false;
     }
     if(!Test->TestTrue(TEXT("Preparation capture has a game viewport"),PreparationCapture.IsValid()))return true;
     if(!SpacingCaptureReady(PreparationCapture,CaptureSize,Test))return false;
     PreparationCapture.Reset();
     LogActualBoardView(Test,World,Mode,CaptureSize,TEXT("Preparation"));
     CheckMasterModelRatio(Test,World,Mode);
    }
    if(!Test->TestTrue(TEXT("Production combat started"),Mode->EnterCombat()))return true;
    bStarted=true;
    return false;
   }
   Test->AddInfo(bVillV?TEXT("Fixture injects one Vill-V card; deployment and combat start use public transactions. No health, damage, AI or time injection."):
    TEXT("Fixture uses the opening hand and programmatic deployment/start; no health, damage, AI or time injection."));
   if(FApp::CanEverRender())
   {
    auto* Viewport=World->GetGameViewport()?World->GetGameViewport()->Viewport:nullptr;
    if(!Test->TestNotNull(TEXT("Actual PIE viewport"),Viewport)||!Test->TestEqual(TEXT("Actual PIE viewport size"),Viewport->GetSizeXY(),CaptureSize))return true;
   }
   if(bVillV)
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"));
    if(!Test->TestNotNull(TEXT("Vill-V card exists"),Card)||!Test->TestTrue(TEXT("Vill-V fixture granted"),Cards->GrantCard(Card)))return true;
    const FGuid Id=Cards->GetHand().Last().InstanceId;
    bool bPlaced=false;
    for(int32 Y=0;Y<3&&!bPlaced;++Y)for(int32 X=0;X<7&&!bPlaced;++X)
     if(Cards->EvaluateHandPlacement(Id,{X,Y}).IsAllowed())bPlaced=Cards->TryDeployHandCard(Id,{X,Y});
    if(!Test->TestTrue(TEXT("Vill-V and its formation deploy"),bPlaced))return true;
   }
   const auto HandSnapshot=Cards->GetHand();
   for(const auto& Card:HandSnapshot)
   {
    bool bPlaced=false;
    for(int32 Y=2;Y>=0&&!bPlaced;--Y)for(int32 X=0;X<7&&!bPlaced;++X)
     if(Cards->EvaluateHandPlacement(Card.InstanceId,{X,Y}).IsAllowed())bPlaced=Cards->TryDeployHandCard(Card.InstanceId,{X,Y});
   }
   if(!Test->TestTrue(TEXT("At least one hero deployed"),Cards->GetPieces().Num()>0))return true;
   PreparationReadyTime=World->GetTimeSeconds()+.4;
   bPrepared=true;
   return false;
  }
  if(!bRunning)
  {
   if(!Mode->BattleRules||!Mode->BattleRules->IsCombatRunning())return false;
   bRunning=true;CombatStart=World->GetTimeSeconds();LastFrameWall=FPlatformTime::Seconds();
  }
  const double FrameWall=FPlatformTime::Seconds();
  if(LastFrameWall>0.&&FrameWall>LastFrameWall)CombatFrameIntervalsMs.Add((FrameWall-LastFrameWall)*1000.);
  LastFrameWall=FrameWall;
  const double Elapsed=World->GetTimeSeconds()-CombatStart;
  if(!bFinished&&Elapsed>=3.)
  {
   CombatCapture=Screenshot(World,TEXT("Combat"));bFinished=true;
   if(FApp::CanEverRender()){LogActualBoardView(Test,World,Mode,CaptureSize,TEXT("Combat"));LogCombatUnitHeads(Test,World,Mode);}
  }
  if(Mode->GetMatch()->GetPhase()==EEGMatchPhase::Combat)
  {
   const auto Units=Mode->GetCombatUnits();
   if(FApp::CanEverRender())
    if(const auto* PC=World->GetFirstPlayerController())
     if(const auto* HUD=Cast<AEGHUD>(PC->GetHUD()))
      if(const auto* Layer=HUD->GetCombatTextLayer())
      {MaxCombatNumbers=FMath::Max(MaxCombatNumbers,Layer->GetActiveCount());PresentedCombatNumbers=FMath::Max(PresentedCombatNumbers,Layer->PresentedCount);}
   FGuid PilotId;
   const UEGVillVMechanismComponent* PilotMechanism=nullptr;
   const AEGChessUnit* PilotUnit=nullptr;
   for(const auto* U:Units)
    if(IsValid(U)&&U->GetDefinition()&&U->GetDefinition()->HeroKind==EEGHeroKind::VillV&&
       U->GetFaction_Implementation()==EEGFaction::Player&&!U->IsAuxiliaryEmplacement())
     {PilotId=U->GetInstanceId();PilotUnit=U;PilotMechanism=U->FindComponentByClass<UEGVillVMechanismComponent>();break;}
   if(bVillV&&FApp::CanEverRender())
   {
    if(CombatCapture.IsValid()&&SpacingCaptureReady(CombatCapture,CaptureSize,Test)){bCombatCaptured=true;CombatCapture.Reset();}
    if(PhaseCapture.IsValid()&&SpacingCaptureReady(PhaseCapture,CaptureSize,Test))PhaseCapture.Reset();
    if(PilotMechanism&&!CombatCapture.IsValid()&&!PhaseCapture.IsValid())
    {
     FString Phase;
     switch(PilotMechanism->Phase)
     {
      case EEGVillVPhase::Moving: Phase=TEXT("Moving");break;
      case EEGVillVPhase::Entering: Phase=TEXT("Entering");break;
      case EEGVillVPhase::Operating:
       if(PilotUnit&&PilotUnit->GetHeroSkill()&&PilotUnit->GetHeroSkill()->IsActionRunning())
        Phase=PilotMechanism->Mode()==0?TEXT("GatlingAttack"):TEXT("CannonAttack");
       else Phase=PilotMechanism->Mode()==0?TEXT("GatlingSeat"):TEXT("CannonSeat");
       break;
      case EEGVillVPhase::Leaving: Phase=TEXT("Leaving");break;
      case EEGVillVPhase::Jumping: Phase=TEXT("Jumping");break;
      case EEGVillVPhase::Retired: Phase=TEXT("Retired");break;
      default:break;
     }
     if(!Phase.IsEmpty()&&!CapturedPhases.Contains(Phase))
     {
      CapturedPhases.Add(Phase);
      PhaseCapture=Screenshot(World,*Phase);
      Test->AddInfo(FString::Printf(TEXT("Vill-V phase %s observed at %.2f world seconds; capture occurs on a subsequent viewport draw and requires visual inspection."),*Phase,Elapsed));
     }
    }
   }
   for(int32 I=0;I<Units.Num();++I)
   {
    const auto* A=Units[I];
    if(!IsValid(A))continue;
    if(FApp::CanEverRender())
     if(const auto* Display=Cast<AEGDemoCombatUnit>(A);Display&&Display->Vitals&&Display->Vitals->IsVisible())
      if(const auto* Vitals=Cast<UEGUnitVitals>(Display->Vitals->GetUserWidgetObject());Vitals&&Vitals->IsVisible()&&Vitals->DisplayMode==EEGVitalsMode::Combat)
      {
       ++VisibleVitalsSamples;
       if(!Vitals->GetRenderTransform().Translation.IsNearlyZero(.01))++NonzeroVitalsSamples;
      }
    if(A->IsAuxiliaryEmplacement())bSawDevice=true;
    if(A->GetAttributes()&&A->GetAttributes()->GetHealth()<A->GetAttributes()->GetMaxHealth()-.1f)bSawDamage=true;
    if(PilotId.IsValid()&&A->LastCombatResolution.SourceId==PilotId&&A->LastCombatResolution.HealthDelta<-.01f)bSawVillVHit=true;
    // Observe only actionable, out-of-range enemy pursuits. A still sample can
    // include a legitimate obstruction; it is a diagnostic, not a pass/fail
    // assertion or a measurement of unobstructed speed.
    if(auto* Combat=A->GetCombat())
    {
     const double Now=World->GetTimeSeconds();
     auto& Observation=Motion.FindOrAdd(const_cast<AEGChessUnit*>(A));
     const double Delta=Now-Observation.Time;
     const auto* Target=Combat->GetTarget();
     const auto* UnitDefinition=A->GetDefinition();
     const bool bUsesOrdinaryPursuit=UnitDefinition&&UnitDefinition->HeroKind!=EEGHeroKind::VillV&&
      UnitDefinition->HeroKind!=EEGHeroKind::ElysiaHerrscher;
     if(bUsesOrdinaryPursuit&&Observation.Time>0.&&Delta>0.&&Delta<.25&&A->GetAttributes()&&Combat->CanAct()&&
        Combat->IsValidEnemy(Target)&&!Combat->IsInAttackRange(Target)&&UEGCombatComponent::IsSpaceOccupant(A))
     {
      const double Distance=FVector::Dist2D(Observation.Position,A->GetActorLocation());
      const double Speed=A->GetAttributes()->GetMoveSpeed();
      if(Speed>1.)
      {
       ++PursuitSamples;PursuitSeconds+=Delta;PursuitDistance+=Distance;ExpectedDistance+=Speed*Delta;
       if(Distance<Speed*Delta*.05){StillSeconds+=Delta;Observation.CurrentStop+=Delta;LongestStop=FMath::Max(LongestStop,Observation.CurrentStop);}
       else Observation.CurrentStop=0.;
      }
     }
     else Observation.CurrentStop=0.;
     Observation.Position=A->GetActorLocation();Observation.Time=Now;
    }
    if(Elapsed<1.||!UEGCombatComponent::IsSpaceOccupant(A))continue;
    for(int32 J=I+1;J<Units.Num();++J)
    {
     const auto* B=Units[J];
     if(!UEGCombatComponent::IsSpaceOccupant(B))continue;
     ++Samples;
     const float Required=UEGCombatComponent::BlockingRadius(A)+UEGCombatComponent::BlockingRadius(B);
     if(FVector::Dist2D(A->GetActorLocation(),B->GetActorLocation())<Required-1.f)++Overlaps;
    }
   }
   if(Elapsed<18.)return false;
  }
  if(FApp::CanEverRender())
  {
   if(CombatCapture.IsValid())
   {if(!SpacingCaptureReady(CombatCapture,CaptureSize,Test))return false;bCombatCaptured=true;CombatCapture.Reset();}
   if(!bCombatCaptured)Test->AddError(TEXT("Timed combat capture was not obtained from the game viewport."));
   if(PhaseCapture.IsValid())
   {if(!SpacingCaptureReady(PhaseCapture,CaptureSize,Test))return false;PhaseCapture.Reset();}
  }
  Test->TestTrue(TEXT("Natural combat produced a health change"),bSawDamage);
  Test->TestTrue(TEXT("Live combatant pairs were sampled"),Samples>0);
  Test->TestEqual(TEXT("No sampled living combatants penetrate blocking radii"),Overlaps,0);
  if(FApp::CanEverRender())
  {
   Test->TestTrue(TEXT("Visible combat vitals were sampled"),VisibleVitalsSamples>0);
   Test->TestEqual(TEXT("Visible combat vitals stay at their own anchor without a screen offset"),NonzeroVitalsSamples,0);
   Test->TestTrue(TEXT("Natural combat produced on-screen combat numbers"),PresentedCombatNumbers>0&&MaxCombatNumbers>0);
   Test->AddInfo(FString::Printf(TEXT("Combat UI samples: %d visible vitals, %d nonzero translations, %d presented numbers, maximum %d active; numbers were not injected."),
    VisibleVitalsSamples,NonzeroVitalsSamples,PresentedCombatNumbers,MaxCombatNumbers));
  }
  if(bVillV)
  {
   Test->TestTrue(TEXT("Vill-V created an independent emplacement"),bSawDevice);
   Test->TestTrue(TEXT("Vill-V dealt real damage during normal AI combat"),bSawVillVHit);
  }
  Test->AddInfo(FString::Printf(TEXT("Sampled %d living pairs, %d overlaps after one second, %.2f world seconds."),Samples,Overlaps,Elapsed));
  if(PursuitSamples>0&&PursuitSeconds>0.)
   Test->AddInfo(FString::Printf(TEXT("Actionable enemy pursuit diagnostics: %d samples, %.2f unit-seconds, %.1f cm travelled (%.1f cm/s); runtime move-speed integral %.1f cm; %.1f%% of integral; %.2f unit-seconds below 5%% nominal, longest consecutive %.2f s. Includes legitimate crowding and obstacles."),
    PursuitSamples,PursuitSeconds,PursuitDistance,PursuitDistance/PursuitSeconds,ExpectedDistance,
    ExpectedDistance>0.?100.*PursuitDistance/ExpectedDistance:0.,StillSeconds,LongestStop));
  else Test->AddInfo(TEXT("No eligible enemy pursuit samples were observed in this encounter."));
  if(!CombatFrameIntervalsMs.IsEmpty())
  {
   double Total=0.;for(double Ms:CombatFrameIntervalsMs)Total+=Ms;
   CombatFrameIntervalsMs.Sort();const int32 Count=CombatFrameIntervalsMs.Num();
   const double P95=CombatFrameIntervalsMs[FMath::Clamp(FMath::CeilToInt(Count*.95)-1,0,Count-1)];
   Test->AddInfo(FString::Printf(TEXT("PIE automation wall-clock update intervals: %d samples, average %.2f ms, P95 %.2f ms, maximum %.2f ms; includes screenshot overhead, not GPU frame time or a baseline comparison."),
    Count,Total/Count,P95,CombatFrameIntervalsMs.Last()));
  }
  return true;
 }
};

bool StartSpacingPIE(FAutomationTestBase* Test,FIntPoint Size,bool bVillV)
{
 const FString Map=TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7");
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 if(FApp::CanEverRender()){ADD_LATENT_AUTOMATION_COMMAND(FEGSpacingStartSizedPIE(Size));}
 else {ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));}
 ADD_LATENT_AUTOMATION_COMMAND(FEGSpacingProductionScenario(Test,Size,bVillV));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSpacingProduction1080,"ElyGambit.CombatSpacing.ProductionOpening1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSpacingProduction1080::RunTest(const FString&){return StartSpacingPIE(this,{1920,1080},false);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSpacingProduction900,"ElyGambit.CombatSpacing.ProductionVillV900",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSpacingProduction900::RunTest(const FString&){return StartSpacingPIE(this,{1600,900},true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSpacingProductionWide,"ElyGambit.CombatSpacing.ProductionOpeningWide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSpacingProductionWide::RunTest(const FString&){return StartSpacingPIE(this,{2560,1080},false);}
#endif
