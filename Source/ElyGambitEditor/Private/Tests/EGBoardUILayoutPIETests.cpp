#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Actor/EGBoard.h"
#include "Actor/EGArtPiece.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGBoardModelSizing.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGGradientSurface.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/StaticMeshComponent.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "GameFramework/PlayerController.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "EngineUtils.h"

namespace
{
bool HasPendingVisualCompilation()
{
 return (GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||
     FAssetCompilingManager::Get().GetNumRemainingAssets()>0;
}

int32 CheckVisibleBoardPerimeter(FAutomationTestBase* Test,const AEGDeploymentBoard* Board)
{
 if(!Test->TestNotNull(TEXT("Perimeter board exists"),Board))return 0;
 TInlineComponentArray<UStaticMeshComponent*> Meshes;Board->GetComponents(Meshes);
 int32 Count=0;
 for(auto* Mesh:Meshes)
 {
  if(!Mesh)continue;
  const FString Name=Mesh->GetName();
  const bool bEdge=Name.StartsWith(TEXT("BoardPerimeterDetail"))||Name.StartsWith(TEXT("BoardMasterPad"))||Name==TEXT("BoardBoundary0")||Name==TEXT("BoardBoundary1")||Name==TEXT("BoardBoundary2")||Name==TEXT("BoardBoundary3");
  if(!bEdge)continue;
  ++Count;
  Test->TestTrue(TEXT("Perimeter component visible in current phase"),Mesh->IsVisible()&&!Mesh->bHiddenInGame);
  Test->TestEqual(TEXT("Perimeter component has no collision"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
 }
 Test->TestTrue(TEXT("Perimeter has visible components"),Count>=8);
 return Count;
}

bool GrantVariedCards(AEGCardManager* Cards,const UEGRunDefinition* Run,const TArray<FEGCardInstance>& Opening)
{
 if(!Cards||!Run||Opening.Num()!=2)return false;
 TArray<UEGCardDefinition*> Choices;
 for(const auto& Card:Run->CardPool)
 {
  if(!Card||!Card->HeroDefinition||Card->HeroDefinition->HeroKind==EEGHeroKind::Elysia||Card==Opening[1].Definition)continue;
  Choices.AddUnique(Card.Get());
 }
 // The normal opening has one random ordinary hero. Prefer every other
 // available base hero first, then cycle the same candidates if necessary.
 if(Opening[1].Definition)Choices.AddUnique(Opening[1].Definition.Get());
 if(Choices.IsEmpty())return false;
 for(int32 I=0;I<8;++I)if(!Cards->GrantCard(Choices[I%Choices.Num()]))return false;
 return true;
}
struct FBoardLayoutCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FBoardLayoutCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

TSharedPtr<FBoardLayoutCapture> BeginBoardCapture(UWorld* World,const FString& Label)
{
 auto* Client=World?World->GetGameViewport():nullptr;
 if(!Client||!Client->Viewport)return nullptr;
 auto Capture=MakeShared<FBoardLayoutCapture>();
 Capture->Client=Client;
 Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/
     (Label+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
 const TWeakPtr<FBoardLayoutCapture> Weak=Capture;
 Capture->Handle=Client->OnDrawn().AddLambda([Weak]()
 {
  auto State=Weak.Pin();if(!State.IsValid())return;
  ++State->Draws;
  if(HasPendingVisualCompilation()){State->Draws=0;return;}
  if(State->Draws>=3&&!State->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
  {
   State->bRequested=true;
   FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  }
 });
 return Capture;
}

bool CapturedBoardFrame(const TSharedPtr<FBoardLayoutCapture>& Capture,FIntPoint Expected,FAutomationTestBase* Test)
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
 Test->TestEqual(TEXT("Saved game viewport PNG has requested dimensions"),FIntPoint(ReadBE(16),ReadBE(20)),Expected);
 Test->AddInfo(FString::Printf(TEXT("Controlled viewport capture: %s"),*Capture->Path));
 return true;
}

class FBoardLayoutSizedPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FBoardLayoutSizedPIE(FIntPoint InSize):Size(InSize){}
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(Size.X,Size.Y))
    .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
    .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

class FBoardLayoutCheck final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 FString Prefix;
 double Deadline=0.,StepStarted=0.;
 int32 Step=0;
 FGuid Protected,Excess;
 TWeakObjectPtr<AEGArtPiece> FriendlyMaster,EnemyMaster;
 TWeakObjectPtr<AEGDeploymentBoard> PhaseGrid;
 int32 PreparationEdgeCount=0;
 bool bCardZonesChecked=false;
 bool bPausedForShaders=false;
 double CompilationClearAfter=0.;
 TSharedPtr<FBoardLayoutCapture> Capture;

 bool WaitForShaders(UWorld* World)
 {
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  if(HasPendingVisualCompilation())
  {
   // Compilation can exceed the 45-second preparation period. Pause only our
   // own PIE session while waiting so the unattended test does not lose the
   // very phase it intends to capture.
   CompilationClearAfter=0.;
   if(World&&PC&&!World->IsPaused()&&PC->SetPause(true))bPausedForShaders=true;
   return true;
  }
  if(bPausedForShaders&&PC){PC->SetPause(false);bPausedForShaders=false;CompilationClearAfter=FPlatformTime::Seconds()+1.5;}
  return CompilationClearAfter>FPlatformTime::Seconds();
 }

 bool CaptureFrame(UWorld* World,const TCHAR* Suffix)
 {
  if(WaitForShaders(World))return false;
  if(!Capture.IsValid())
  {
   Capture=BeginBoardCapture(World,Prefix+TEXT("_")+Suffix);
   if(!Capture.IsValid()){Test->AddError(TEXT("Game viewport unavailable for capture"));return true;}
   return false;
  }
  if(!CapturedBoardFrame(Capture,Size,Test))return false;
  Capture.Reset();return true;
 }
 void Next(int32 NewStep){Step=NewStep;StepStarted=FPlatformTime::Seconds();}
 bool Settled(double Seconds=.5) const{return FPlatformTime::Seconds()-StepStarted>=Seconds;}

public:
 FBoardLayoutCheck(FAutomationTestBase* InTest,FIntPoint InSize,const FString& InPrefix):Test(InTest),Size(InSize),Prefix(InPrefix){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+300.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Board layout PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Cards=Mode?Mode->GetCards():nullptr;
  auto* Controller=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=Controller?Cast<AEGHUD>(Controller->GetHUD()):nullptr;
  auto* View=HUD?HUD->GetMainWidget():nullptr;
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Mode||!Cards||!Mode->GetMatch()||!View||!View->HandScrollBox||!View->HandPanel||!Client||!Client->Viewport)return false;
  if(Client->Viewport->GetSizeXY()!=Size)return false;
  if(Step==0)
  {
   if(WaitForShaders(World))return false;
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Opening=Cards->GetHand();
   if(!Test->TestEqual(TEXT("Production campaign starts with two cards"),Opening.Num(),2))return true;
   Protected=Opening[0].InstanceId;
   if(!Test->TestTrue(TEXT("Opening Elysia is protected"),Cards->IsHandOverflowProtected(Protected))||
      !Test->TestTrue(TEXT("Grant eight varied ordinary cards through public authority"),GrantVariedCards(Cards,Mode->RunDefinition.Get(),Opening)))return true;
   Test->TestEqual(TEXT("Ten cards owned"),Cards->GetOrderedHandIds().Num(),10);
   Next(10);return false;
  }
  if(Step==10)
  {
   if(!Settled())return false;
   auto* RunPanel=HUD->GetRunPanel();
   if(!Test->TestNotNull(TEXT("Formal HUD has a run panel"),RunPanel))return true;
   Test->TestNull(TEXT("Friendly identity status panel removed"),View->GetWidgetFromName(TEXT("FriendlyMasterIdentity")));
   Test->TestNull(TEXT("Enemy identity status panel removed"),View->GetWidgetFromName(TEXT("EnemyMasterIdentity")));
   auto* Left=Cast<UImage>(RunPanel->GetWidgetFromName(TEXT("PhaseClockStagePlate")));
   auto* Right=Cast<UImage>(RunPanel->GetWidgetFromName(TEXT("PhaseClockCorruptionPlate")));
   auto* Number=Cast<UTextBlock>(RunPanel->GetWidgetFromName(TEXT("PhaseClockNumber")));
   auto* PhaseName=Cast<UTextBlock>(RunPanel->GetWidgetFromName(TEXT("PhaseClockStage")));
   if(Test->TestNotNull(TEXT("Phase name exists"),PhaseName))
    Test->TestEqual(TEXT("Phase plate only displays preparation phase without stage number"),PhaseName->GetText().ToString(),FString(TEXT("准备阶段")));
   if(!Test->TestNotNull(TEXT("Stage plate exists"),Left)||!Test->TestNotNull(TEXT("Corruption plate exists"),Right)||
      !Test->TestNotNull(TEXT("Independent countdown exists"),Number)||!Test->TestNotNull(TEXT("Stage button exists"),View->Button_Stage.Get()))return true;
   Test->TestNotNull(TEXT("Stage plate texture loaded"),Left->GetBrush().GetResourceObject());
   Test->TestNotNull(TEXT("Corruption plate texture loaded"),Right->GetBrush().GetResourceObject());
   const FSlateRect L=Left->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect N=Number->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect R=Right->GetCachedGeometry().GetRenderBoundingRect();
   Test->TestTrue(TEXT("Clock plates and countdown occupy distinct ordered areas"),L.Right<=N.Left+1.f&&N.Right<=R.Left+1.f&&L.Right>L.Left&&N.Right>N.Left&&R.Right>R.Left);
   const FSlateRect B=View->Button_Stage->GetCachedGeometry().GetRenderBoundingRect();
   Test->TestTrue(TEXT("Stage button overlays the left plate"),FMath::Abs(B.Left-L.Left)<3.f&&FMath::Abs(B.Right-L.Right)<3.f&&FMath::Abs(B.Top-L.Top)<3.f&&FMath::Abs(B.Bottom-L.Bottom)<3.f);
   View->Button_Stage->OnClicked.Broadcast();Next(11);return false;
  }
  if(Step==11)
  {
   if(!Settled(.3))return false;
   auto* Panel=View->GetWidgetFromName(TEXT("StagePreviewPanel"));
   auto* Scroll=Cast<UScrollBox>(View->GetWidgetFromName(TEXT("StagePreviewScroll")));
   if(!Test->TestTrue(TEXT("Stage button opens preview"),View->IsStageExpanded()&&Panel&&Panel->IsVisible())||
      !Test->TestNotNull(TEXT("Stage preview has columns"),Scroll))return true;
   // Measure the open panel only after the same viewport has actually drawn it.
   if(!CaptureFrame(World,TEXT("StageDetails")))return false;
   if(!Test->TestEqual(TEXT("Seven campaign columns"),Scroll->GetChildrenCount(),7)||
      !Test->TestTrue(TEXT("Formal run has seven stages"),Mode->RunDefinition&&Mode->RunDefinition->Stages.Num()==7))return true;
   auto* Gradient=Cast<UEGGradientSurface>(View->GetWidgetFromName(TEXT("StagePreviewGradient")));
   if(Test->TestNotNull(TEXT("Stage preview uses a shop-style gradient"),Gradient))
   {
    const FLinearColor Top=Gradient->TopColor,Bottom=Gradient->BottomColor;
    Test->TestTrue(TEXT("Stage preview gradient starts opaque black"),
        Top.R<=.01f&&Top.G<=.01f&&Top.B<=.01f&&Top.A>=.8f);
    Test->TestTrue(TEXT("Stage preview gradient ends transparent"),Bottom.A<=.01f&&!Gradient->bHorizontalGradient);
    const FSlateRect ViewRect=View->GetCachedGeometry().GetRenderBoundingRect();
    const FSlateRect PanelBounds=Panel->GetCachedGeometry().GetRenderBoundingRect();
    const FSlateRect GradientBounds=Gradient->GetCachedGeometry().GetRenderBoundingRect();
    const auto CoversTopWidth=[&](const FSlateRect& Rect)
    {
     return Rect.Right>Rect.Left&&FMath::Abs(Rect.Left-ViewRect.Left)<=1.f&&
         FMath::Abs(Rect.Right-ViewRect.Right)<=1.f&&FMath::Abs(Rect.Top-ViewRect.Top)<=1.f;
    };
    Test->TestTrue(TEXT("Stage panel spans viewport width and starts at top edge"),CoversTopWidth(PanelBounds));
    Test->TestTrue(TEXT("Stage gradient spans viewport width and starts at top edge"),CoversTopWidth(GradientBounds));
   }
   // Inspect only visible descendants of this panel, leaving shop and other
   // independently styled UI alone. Hidden legacy widgets do not paint blur.
   if(View->WidgetTree)View->WidgetTree->ForEachWidget([&](UWidget* Widget)
   {
    auto* Blur=Cast<UBackgroundBlur>(Widget);if(!Blur)return;
    bool bVisible=Blur->IsVisible(),bDescendant=false;
    for(UWidget* Ancestor=Blur->GetParent();Ancestor;Ancestor=Ancestor->GetParent())
    {
     bVisible&=Ancestor->IsVisible();
     if(Ancestor==Panel){bDescendant=true;break;}
    }
    if(bDescendant&&bVisible)
     Test->TestTrue(*FString::Printf(TEXT("Visible stage blur %s has zero strength"),*Blur->GetName()),Blur->GetBlurStrength()<=0.f);
   });
   const FSlateRect ScrollRect=Scroll->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect PanelRect=Panel->GetCachedGeometry().GetRenderBoundingRect();
   const auto IsInside=[](const FSlateRect& Inner,const FSlateRect& Outer)
   {return Inner.Right>Inner.Left&&Inner.Bottom>Inner.Top&&Inner.Left>=Outer.Left-1.f&&Inner.Right<=Outer.Right+1.f&&
       Inner.Top>=Outer.Top-1.f&&Inner.Bottom<=Outer.Bottom+1.f;};
   const int32 CorruptionMagnitude[]={5,5,10,10,15,15,10};
   float CurrentOpacity=-1.f;
   TArray<float> OtherOpacities;
   for(int32 I=0;I<7;++I)
   {
    auto Find=[&](const TCHAR* Base)->UWidget*
    {return View->GetWidgetFromName(FName(*FString::Printf(TEXT("%s_%d"),Base,I+1)));};
    auto* Column=Cast<UCanvasPanel>(Find(TEXT("StageSimpleColumn")));
    auto* Title=Cast<UTextBlock>(Find(TEXT("StageSimpleTitle")));
    auto* Corruption=Cast<UTextBlock>(Find(TEXT("StageSimpleCorruptionText")));
    auto* Gold=Cast<UTextBlock>(Find(TEXT("StageSimpleGoldText")));
    auto* CorruptionIcon=Cast<UImage>(Find(TEXT("StageSimpleCorruptionIcon")));
    auto* BattleIcon=Cast<UImage>(Find(TEXT("StageSimpleBattleIcon")));
    auto* GoldIcon=Cast<UImage>(Find(TEXT("StageSimpleGoldIcon")));
    auto* CorruptionGroup=Find(TEXT("StageSimpleCorruptionGroup"));
    auto* GoldGroup=Find(TEXT("StageSimpleGoldGroup"));
    if(!Test->TestNotNull(TEXT("Simple stage column"),Column)||!Test->TestNotNull(TEXT("Stage title"),Title)||
       !Test->TestNotNull(TEXT("Stage corruption text"),Corruption)||!Test->TestNotNull(TEXT("Stage income text"),Gold)||
       !Test->TestNotNull(TEXT("Stage corruption icon"),CorruptionIcon)||!Test->TestNotNull(TEXT("Stage battle icon"),BattleIcon)||
       !Test->TestNotNull(TEXT("Stage income icon"),GoldIcon)||!Test->TestNotNull(TEXT("Stage corruption tooltip group"),CorruptionGroup)||
       !Test->TestNotNull(TEXT("Stage income tooltip group"),GoldGroup))continue;
    Test->TestNotNull(TEXT("Corruption icon texture loaded"),CorruptionIcon->GetBrush().GetResourceObject());
    Test->TestNotNull(TEXT("Battle icon texture loaded"),BattleIcon->GetBrush().GetResourceObject());
    Test->TestNotNull(TEXT("Income icon texture loaded"),GoldIcon->GetBrush().GetResourceObject());
    const FSlateRect ColumnRect=Column->GetCachedGeometry().GetRenderBoundingRect();
    Test->TestTrue(*FString::Printf(TEXT("Stage %d column is entirely inside preview scroll and panel"),I+1),
        IsInside(ColumnRect,ScrollRect)&&IsInside(ColumnRect,PanelRect));
    for(UWidget* Item:{static_cast<UWidget*>(Title),static_cast<UWidget*>(Corruption),static_cast<UWidget*>(Gold),
        static_cast<UWidget*>(CorruptionIcon),static_cast<UWidget*>(BattleIcon),static_cast<UWidget*>(GoldIcon)})
    {
     const FSlateRect ItemRect=Item->GetCachedGeometry().GetRenderBoundingRect();
     Test->TestTrue(*FString::Printf(TEXT("Stage %d item %s is visible and uncut"),I+1,*Item->GetName()),
         Item->IsVisible()&&IsInside(ItemRect,ColumnRect)&&IsInside(ItemRect,ScrollRect)&&IsInside(ItemRect,PanelRect));
     if(auto* Text=Cast<UTextBlock>(Item))
     {
      const FVector2D Desired=Text->GetDesiredSize(),Local=Text->GetCachedGeometry().GetLocalSize();
      Test->AddInfo(FString::Printf(TEXT("Stage %d text %s desired=(%.2f,%.2f) local=(%.2f,%.2f)"),I+1,*Item->GetName(),Desired.X,Desired.Y,Local.X,Local.Y));
      Test->TestTrue(*FString::Printf(TEXT("Stage %d text %s fits its allocated area"),I+1,*Item->GetName()),
          Desired.X<=Local.X+1.f&&Desired.Y<=Local.Y+1.f);
     }
    }
    Test->TestEqual(*FString::Printf(TEXT("Stage %d round label"),I+1),Title->GetText().ToString(),FString::Printf(TEXT("第%d回合"),I+1));
    const FString ExpectedCorruption=I<6?FString::Printf(TEXT("+%d"),CorruptionMagnitude[I]):TEXT("—");
    Test->TestEqual(*FString::Printf(TEXT("Stage %d corruption magnitude"),I+1),Corruption->GetText().ToString(),ExpectedCorruption);
    const int32 ExpectedIncome[]={5,5,6,6,7,8,0};
    const int32 ExpectedGold=ExpectedIncome[I];
    Test->TestEqual(TEXT("Campaign completion income follows the difficulty curve"),Mode->RunDefinition->Stages[I].CompletionGold,ExpectedGold);
    Test->TestEqual(*FString::Printf(TEXT("Stage %d displayed income"),I+1),Gold->GetText().ToString(),FString::Printf(TEXT("+%d"),ExpectedGold));
    const FString CorruptionTip=CorruptionGroup->GetToolTipText().ToString();
    Test->TestFalse(*FString::Printf(TEXT("Stage %d explains corruption"),I+1),CorruptionTip.IsEmpty());
    if(I<6)Test->TestTrue(*FString::Printf(TEXT("Stage %d corruption tooltip explains both outcomes"),I+1),
        CorruptionTip.Contains(TEXT("胜利减少"))&&CorruptionTip.Contains(TEXT("战败增加"))&&CorruptionTip.Contains(FString::FromInt(CorruptionMagnitude[I])));
    else Test->TestTrue(TEXT("Final stage explains the two lives, barrier and enrage"),
        CorruptionTip.Contains(TEXT("两条独立生命"))&&CorruptionTip.Contains(TEXT("壁垒"))&&CorruptionTip.Contains(TEXT("8 秒"))&&
        CorruptionTip.Contains(TEXT("60 秒"))&&CorruptionTip.Contains(TEXT("每 5 秒"))&&CorruptionTip.Contains(TEXT("10%")));
    Test->TestFalse(*FString::Printf(TEXT("Stage %d explains income"),I+1),GoldGroup->GetToolTipText().IsEmpty());
    if(I+1==Mode->GetMatch()->GetStageNumber())CurrentOpacity=Column->GetRenderOpacity();
    else OtherOpacities.Add(Column->GetRenderOpacity());
   }
   Test->TestTrue(TEXT("Current stage has a brighter column"),CurrentOpacity>=0.f&&OtherOpacities.Num()==6&&!OtherOpacities.ContainsByPredicate([CurrentOpacity](float Opacity){return Opacity>=CurrentOpacity;}));
   Test->TestFalse(TEXT("No separate expanded stage details"),View->GetWidgetFromName(TEXT("Text_StagePreviewDetail"))&&View->GetWidgetFromName(TEXT("Text_StagePreviewDetail"))->IsVisible());
   Next(12);return false;
  }
  if(Step==12)
  {
   if(!Settled(.1))return false;
   Test->TestTrue(TEXT("Programmatic dismiss closes stage preview"),View->DismissPanels());
   Test->TestFalse(TEXT("Stage preview is closed"),View->IsStageExpanded());
   if(!Test->TestNotNull(TEXT("Shop toggle exists"),View->Button_ToggleDrawPanel.Get()))return true;
   View->Button_Stage->OnClicked.Broadcast();
   View->Button_ToggleDrawPanel->OnClicked.Broadcast();
   Test->TestTrue(TEXT("Opening shop closes stage preview"),View->IsDrawExpanded()&&!View->IsStageExpanded());
   View->Button_ToggleDrawPanel->OnClicked.Broadcast();
   Test->AddInfo(TEXT("Stage buttons and dismiss were invoked programmatically, not by OS mouse or Escape input."));
   Next(1);return false;
  }
  if(Step==1)
  {
   if(!Settled())return false;
   const auto Order=Cards->GetOrderedHandIds();
   if(!Test->TestEqual(TEXT("All ten cards have widgets"),View->HandScrollBox->GetChildrenCount(),10))return true;
   auto* BoardPC=Cast<AEGBoardPlayerController>(Controller);
   const auto* Scene=BoardPC?BoardPC->GetSceneSettings():nullptr;
   auto* Grid=Scene?Scene->Board.Get():nullptr;
   if(!Test->TestNotNull(TEXT("Formal scene has a deployment grid"),Grid))return true;
   PhaseGrid=Grid;PreparationEdgeCount=CheckVisibleBoardPerimeter(Test,Grid);
   const float HalfWidth=Grid->Columns*Grid->CellWidth*.5f;
   const float HalfDepth=Grid->Rows*Grid->CellDepth*.5f;
   float BoardBottomPixels=0.f;
   for(const float X:{-1.f,1.f})for(const float Y:{-1.f,1.f})
   {
    const FVector Corner=Grid->GetActorTransform().TransformPosition(FVector(X*HalfWidth,Y*HalfDepth,0.f));
    FVector2D Pixel;
    if(Test->TestTrue(TEXT("Grid corner projects into the game viewport"),Controller->ProjectWorldLocationToScreen(Corner,Pixel,true)))
    {
     const FVector2D UV(Pixel.X/Size.X,Pixel.Y/Size.Y);
     BoardBottomPixels=FMath::Max(BoardBottomPixels,static_cast<float>(Pixel.Y));
     Test->TestTrue(TEXT("Playable perimeter fits horizontal safe region"),UV.X>=.149f&&UV.X<=.851f);
     Test->TestTrue(TEXT("Playable perimeter fits vertical safe region"),UV.Y>=.139f&&UV.Y<=.801f);
    }
   }
   FVector FriendlyAnchor,EnemyAnchor;
   if(Test->TestTrue(TEXT("Board master anchors resolve"),UEGBoardMasterPresentation::GetMasterAnchors(Mode->GetBoard(),FriendlyAnchor,EnemyAnchor)))
   {
    FVector2D CenterPixel=FVector2D::ZeroVector,FriendlyPixel=FVector2D::ZeroVector,EnemyPixel=FVector2D::ZeroVector;
    const bool bProjected=Controller->ProjectWorldLocationToScreen(Grid->GetActorLocation(),CenterPixel,true)&&
       Controller->ProjectWorldLocationToScreen(FriendlyAnchor,FriendlyPixel,true)&&
       Controller->ProjectWorldLocationToScreen(EnemyAnchor,EnemyPixel,true);
    Test->TestTrue(TEXT("Master locations and board center project"),bProjected);
    if(bProjected)
     Test->TestTrue(TEXT("Friendly station is lower left and enemy station is upper right"),
        FriendlyPixel.X<CenterPixel.X&&FriendlyPixel.Y>CenterPixel.Y&&EnemyPixel.X>CenterPixel.X&&EnemyPixel.Y<CenterPixel.Y);
    for(const FVector Anchor:{FriendlyAnchor,EnemyAnchor})
    {
     FVector2D Pixel;
     if(Test->TestTrue(TEXT("Board master anchor projects"),Controller->ProjectWorldLocationToScreen(Anchor,Pixel,true)))
      Test->TestTrue(TEXT("Board master anchor is inside viewport"),Pixel.X>=0&&Pixel.X<=Size.X&&Pixel.Y>=0&&Pixel.Y<=Size.Y);
    }
    for(TActorIterator<AEGArtPiece> It(World);It;++It)
    {
     AEGArtPiece* Piece=*It;if(Piece->GetOwner()!=Mode)continue;
     if(FVector::DistSquared2D(Piece->GetActorLocation(),FriendlyAnchor)<1.f)FriendlyMaster=Piece;
     if(FVector::DistSquared2D(Piece->GetActorLocation(),EnemyAnchor)<1.f)EnemyMaster=Piece;
    }
    if(Test->TestTrue(TEXT("Friendly master actor exists"),FriendlyMaster.IsValid()))
    {
     Test->TestFalse(TEXT("Friendly master shown before deployment"),FriendlyMaster->IsHidden());
     Test->TestTrue(TEXT("Friendly master defaults to ordinary Elysia"),FriendlyMaster->HeroVisualDefinition&&FriendlyMaster->HeroVisualDefinition->HeroKind==EEGHeroKind::Elysia);
    }
    if(Test->TestTrue(TEXT("Enemy master actor exists"),EnemyMaster.IsValid()))
     Test->TestFalse(TEXT("Enemy master shown in ordinary stage"),EnemyMaster->IsHidden());
    for(const auto& WeakMaster:{FriendlyMaster,EnemyMaster})
    {
     auto* Master=WeakMaster.Get();
     if(!Test->TestNotNull(TEXT("Master has a hero definition"),Master?Master->HeroVisualDefinition.Get():nullptr))continue;
     const float Normal=FEGBoardModelSizing::Resolve(Master->HeroVisualDefinition,Master);
     if(!Test->TestTrue(TEXT("Same-definition normal board multiplier is positive"),Normal>KINDA_SMALL_NUMBER))continue;
     const float Ratio=Master->GetBoardModelScale()/Normal;
     Test->AddInfo(FString::Printf(TEXT("Visible master %s model multiplier %.4f / resolved same-definition board model %.4f = %.4f; actor scale %s."),
      *Master->HeroVisualDefinition->HeroId.ToString(),Master->GetBoardModelScale(),Normal,Ratio,*Master->GetActorScale3D().ToString()));
     Test->TestTrue(TEXT("Visible master has 1.5 times the same-definition normal model multiplier"),FMath::IsNearlyEqual(Ratio,1.5f,.02f));
     Test->TestTrue(TEXT("Master actor root remains unit scale"),Master->GetActorScale3D().Equals(FVector::OneVector,.001f));
    }
   }
   const FGeometry& Scroll=View->HandScrollBox->GetCachedGeometry();
   const FVector2D ScrollSize=Scroll.GetLocalSize();
   const FGeometry& ViewGeometry=View->GetCachedGeometry();
   const FVector2D ViewSize=ViewGeometry.GetLocalSize();
   if(ScrollSize.X<=0||ScrollSize.Y<=0)return false;
   TArray<FSlateRect> CardRects,CardLayoutRects;
   for(int32 I=0;I<Order.Num();++I)
   {
    auto* Card=View->FindCardWidget(Order[I]);
    if(!Test->TestNotNull(TEXT("Each ordered card is represented"),Card))continue;
    Test->TestTrue(TEXT("Rendered row follows authoritative order"),View->HandScrollBox->GetChildAt(I)==Card);
    const FGeometry& G=Card->GetCachedGeometry();
    CardRects.Add(G.GetRenderBoundingRect());
    auto* Face=Cast<USizeBox>(Card->GetWidgetFromName(TEXT("CardSize")));
    if(Test->TestNotNull(TEXT("Prepared card has a face size box"),Face))
     CardLayoutRects.Add(Face->GetCachedGeometry().GetLayoutBoundingRect());
    const FVector2D A=Scroll.AbsoluteToLocal(G.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D B=Scroll.AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()));
    Test->TestTrue(TEXT("Ten-card layout is inside horizontal scroll clip"),A.X>=-1.f&&B.X<=ScrollSize.X+1.f&&B.X>A.X);
    const FVector2D Top=ViewGeometry.AbsoluteToLocal(G.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D Bottom=ViewGeometry.AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()));
    Test->TestTrue(TEXT("Prepared card has full 140 by 196 logical size"),
        FMath::IsNearlyEqual(Bottom.X-Top.X,140.f,2.f)&&FMath::IsNearlyEqual(Bottom.Y-Top.Y,196.f,2.f));
    Test->TestTrue(TEXT("Prepared card is fully inside viewport"),Top.X>=-1.f&&Top.Y>=-1.f&&Bottom.X<=ViewSize.X+1.f&&Bottom.Y<=ViewSize.Y+1.f);
    Test->TestTrue(TEXT("Prepared card bottom is near viewport bottom"),FMath::IsNearlyEqual(ViewSize.Y-Bottom.Y,6.f,2.f));
    Test->TestTrue(TEXT("Playable board clears the complete hand"),Top.Y>=BoardBottomPixels*ViewSize.Y/Size.Y-1.f);
    const auto* Definition=Cards->FindHandCardDefinition(Order[I]);
    if(Definition&&Definition->HeroDefinition&&Card->WidgetTree)
    {
     const FText ExpectedName=Definition->HeroDefinition->DisplayName;
     auto* Label=Cast<UTextBlock>(Card->GetWidgetFromName(TEXT("HandCardNameText")));
     auto* Plate=Card->GetWidgetFromName(TEXT("HandCardNamePlate"));
     if(Test->TestNotNull(TEXT("Full card name label exists"),Label)&&Test->TestNotNull(TEXT("Full card name plate exists"),Plate))
     {
      const FSlateRect Bounds=Label->GetCachedGeometry().GetRenderBoundingRect();
      const FSlateRect Screen=ViewGeometry.GetRenderBoundingRect();
      const FSlateRect PlateBounds=Plate->GetCachedGeometry().GetRenderBoundingRect();
      const bool bVisible=Label->IsVisible()&&Plate->IsVisible()&&Label->GetText().EqualTo(ExpectedName)&&
          Bounds.Right>Bounds.Left&&Bounds.Bottom>Bounds.Top&&Bounds.Left>=Screen.Left-1.f&&
          Bounds.Right<=Screen.Right+1.f&&Bounds.Top>=Screen.Top-1.f&&Bounds.Bottom<=Screen.Bottom+1.f&&
          Bounds.Left>=PlateBounds.Left-1.f&&Bounds.Right<=PlateBounds.Right+1.f&&
          Bounds.Top>=PlateBounds.Top-1.f&&Bounds.Bottom<=PlateBounds.Bottom+1.f;
      Test->TestTrue(TEXT("Complete card has its readable name on screen"),bVisible);
      Test->TestTrue(TEXT("Card name fits its allocated label"),Label->GetDesiredSize().X<=Label->GetCachedGeometry().GetLocalSize().X+1.f&&
          Label->GetDesiredSize().Y<=Label->GetCachedGeometry().GetLocalSize().Y+1.f);
     }
    }
   }
   if(!bCardZonesChecked&&CardRects.Num()==10&&CardLayoutRects.Num()==10)
   {
    bCardZonesChecked=true;
    const auto Overlaps=[](const FSlateRect& A,const FSlateRect& B)
    {return FMath::Min(A.Right,B.Right)-FMath::Max(A.Left,B.Left)>.75f&&
        FMath::Min(A.Bottom,B.Bottom)-FMath::Max(A.Top,B.Top)>.75f;};
    for(int32 I=0;I<CardRects.Num();++I)
    {
     const auto& L=CardLayoutRects[I];const auto& R=CardRects[I];
     Test->AddInfo(FString::Printf(TEXT("Card %d layout (%.1f,%.1f)-(%.1f,%.1f); rendered root (%.1f,%.1f)-(%.1f,%.1f)"),
         I+1,L.Left,L.Top,L.Right,L.Bottom,R.Left,R.Top,R.Right,R.Bottom));
     if(I>0)Test->TestTrue(*FString::Printf(TEXT("Prepared card faces %d and %d are ordered without layout overlap"),I,I+1),
         CardLayoutRects[I-1].Right<=L.Left+.75f&&!Overlaps(CardLayoutRects[I-1],L));
    }
    for(UWidget* Control:{static_cast<UWidget*>(View->Button_Battle.Get()),static_cast<UWidget*>(View->Button_ToggleDrawPanel.Get()),View->GetWidgetFromName(TEXT("BoardThemePopulationPill"))})
    {
     if(!Test->TestNotNull(TEXT("Bottom control exists for overlap check"),Control))continue;
     const FSlateRect Bounds=Control->GetCachedGeometry().GetRenderBoundingRect();
     if(!Test->TestTrue(*FString::Printf(TEXT("Bottom control %s is visible with geometry"),*Control->GetName()),
        Control->IsVisible()&&Bounds.Right>Bounds.Left&&Bounds.Bottom>Bounds.Top))continue;
     for(const FSlateRect& CardRect:CardRects)
      Test->TestFalse(*FString::Printf(TEXT("Hand cards do not cover bottom control %s"),*Control->GetName()),Overlaps(CardRect,Bounds));
    }
    if(Test->TestTrue(TEXT("Both sale targets have hit geometry"),View->HasSellZoneHitGeometry()))
    {
     // Exercise the public sale hit API with an owned-hand drag state. This
     // tests both targets' real clipped hit areas without creating a sale.
     View->SetDragActive(true);
     for(const FSlateRect& CardRect:CardRects)
      Test->TestFalse(TEXT("Hand card does not overlap either sale target"),View->IsDragCardTouchingSellZone(
          FVector2D((CardRect.Left+CardRect.Right)*.5f,(CardRect.Top+CardRect.Bottom)*.5f),
          FVector2D(CardRect.Right-CardRect.Left,CardRect.Bottom-CardRect.Top)));
     View->SetDragActive(false);
    }
   }
   Test->TestTrue(TEXT("No normal card marked excess"),Cards->GetOverflowHandIds().IsEmpty());
   if(!CaptureFrame(World,TEXT("Preparation10")))return false;
   auto* Card=Cards->GetHand()[1].Definition.Get();
   if(!Test->TestTrue(TEXT("Grant eleventh card"),Cards->GrantCard(Card)))return true;
   Excess=Cards->GetOrderedHandIds().Last();Next(2);return false;
  }
  if(Step==2)
  {
   if(!Settled())return false;
   if(!Test->TestEqual(TEXT("Eleventh card is excess"),Cards->GetOverflowHandIds().Num(),1))return true;
   Test->TestEqual(TEXT("Newest card marked excess"),Cards->GetOverflowHandIds()[0],Excess);
   Test->TestEqual(TEXT("Displayed proceeds are authoritative sale value"),Cards->GetOverflowSaleValue(),Cards->GetSaleValue(Excess));
   auto* Card=View->FindCardWidget(Excess);
   if(!Test->TestNotNull(TEXT("Excess card widget"),Card))return true;
   Test->TestTrue(TEXT("Excess card has sale warning"),Card->OverflowText&&Card->OverflowText->GetText().ToString().Contains(TEXT("出售")));
   Test->TestTrue(TEXT("Hand counter displays excess and proceeds"),View->Text_HandCount&&View->Text_HandCount->GetText().ToString().Contains(TEXT("超额"))&&
       View->Text_HandCount->GetText().ToString().Contains(FString::FromInt(static_cast<int32>(Cards->GetOverflowSaleValue()))));
   // Keep the eleventh card within the viewport for this controlled capture.
   View->HandScrollBox->ScrollToEnd();
   Next(3);return false;
  }
  if(Step==3)
  {
   if(!Settled())return false;
   if(!CaptureFrame(World,TEXT("Overflow11")))return false;
   auto Order=Cards->GetOrderedHandIds();
   if(!Test->TestTrue(TEXT("Public reorder keeps excess card"),Cards->TryReorderHandCard(Excess,Order[1],Cards->GetHandOrderRevision())))return true;
   Test->TestFalse(TEXT("Reordered card is no longer excess"),Cards->GetOverflowHandIds().Contains(Excess));
   Test->TestEqual(TEXT("Reordered card reaches second position"),Cards->GetOrderedHandIds()[1],Excess);
   Next(4);return false;
  }
  if(Step==4)
  {
   if(!Settled())return false;
   const auto Order=Cards->GetOrderedHandIds();
   for(int32 I=0;I<Order.Num();++I)
    Test->TestTrue(TEXT("Visible row follows reorder"),View->HandScrollBox->GetChildAt(I)==View->FindCardWidget(Order[I]));
   if(!Test->TestTrue(TEXT("Deploy ordinary Elysia"),Cards->TryDeployHandCard(Protected,FIntPoint(3,1))))return true;
   if(FriendlyMaster.IsValid())Test->TestFalse(TEXT("Deployment leaves independent friendly master visible"),FriendlyMaster->IsHidden());
   if(!Test->TestTrue(TEXT("Enter actual combat"),Mode->EnterCombat()))return true;
   Next(5);return false;
  }
  if(Step==5)
  {
   if(!Settled(.35))return false;
   if(!Test->TestEqual(TEXT("Combat remains active for controlled check"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Combat))return true;
   if(auto* RunPanel=HUD->GetRunPanel())if(auto* PhaseName=Cast<UTextBlock>(RunPanel->GetWidgetFromName(TEXT("PhaseClockStage"))))
    Test->TestEqual(TEXT("Phase plate only displays combat phase without stage number"),PhaseName->GetText().ToString(),FString(TEXT("战斗阶段")));
   if(FriendlyMaster.IsValid())Test->TestFalse(TEXT("Independent friendly master stays visible in combat"),FriendlyMaster->IsHidden());
   if(EnemyMaster.IsValid())Test->TestFalse(TEXT("Enemy master remains visible during ordinary combat"),EnemyMaster->IsHidden());
   Test->TestEqual(TEXT("Perimeter persists in combat without duplicating components"),CheckVisibleBoardPerimeter(Test,PhaseGrid.Get()),PreparationEdgeCount);
   const auto Order=Cards->GetOrderedHandIds();
   if(!Test->TestTrue(TEXT("A card remains to inspect"),!Order.IsEmpty()))return true;
   Test->TestTrue(TEXT("Combat hand uses approximately two-thirds scale"),FMath::IsNearlyEqual(View->HandPanel->GetRenderTransform().Scale.X,.67f,.03f));
   Test->TestFalse(TEXT("Hand editing disabled in combat"),View->CanEditHand());
   Test->TestFalse(TEXT("Authority rejects combat reorder"),Cards->TryReorderHandCard(Order[0],FGuid(),Cards->GetHandOrderRevision()));
   auto* Card=View->FindCardWidget(Order[0]);
   if(!Test->TestNotNull(TEXT("Combat hand source card"),Card))return true;
   const FSlateRect Screen=View->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect CombatCard=Card->GetCachedGeometry().GetRenderBoundingRect();
   Test->TestTrue(TEXT("Scaled combat card is fully inside viewport"),CombatCard.Left>=Screen.Left-1.f&&CombatCard.Right<=Screen.Right+1.f&&
       CombatCard.Top>=Screen.Top-1.f&&CombatCard.Bottom<=Screen.Bottom+1.f&&CombatCard.Right>CombatCard.Left&&CombatCard.Bottom>CombatCard.Top);
   View->ShowHandHover(Card);
   auto* Root=Cast<UCanvasPanel>(View->WidgetTree->RootWidget);
   UEGHandCard* Hover=nullptr;
   if(Root)for(int32 I=0;I<Root->GetChildrenCount();++I)
    if(auto* Candidate=Cast<UEGHandCard>(Root->GetChildAt(I));Candidate&&Candidate!=Card&&Candidate->InstanceId==Order[0]&&Candidate->IsVisible()){Hover=Candidate;break;}
   if(Test->TestNotNull(TEXT("Programmatic hover shows separate full card"),Hover))
   {
    auto* CardSize=Cast<USizeBox>(Hover->GetWidgetFromName(TEXT("CardSize")));
    if(Test->TestNotNull(TEXT("Hover has a card size widget"),CardSize))
     Test->TestTrue(TEXT("Hover retains full preparation dimensions"),CardSize->GetWidthOverride()>=139.f&&CardSize->GetHeightOverride()>=195.f);
   }
   Next(6);return false;
  }
  if(Step==6)
  {
   if(!Settled(.15))return false;
   auto* Root=Cast<UCanvasPanel>(View->WidgetTree->RootWidget);
   UEGHandCard* Hover=nullptr;
   const auto Order=Cards->GetOrderedHandIds();
   if(Root&&!Order.IsEmpty())for(int32 I=0;I<Root->GetChildrenCount();++I)
    if(auto* Candidate=Cast<UEGHandCard>(Root->GetChildAt(I));Candidate&&Order.Contains(Candidate->InstanceId)&&
       Candidate!=View->FindCardWidget(Candidate->InstanceId)&&Candidate->IsVisible()){Hover=Candidate;break;}
   if(Hover)Test->AddInfo(FString::Printf(TEXT("Visible hover copy %s corresponds to hand instance %s"),*Hover->GetName(),*Hover->InstanceId.ToString()));
   else if(Root)for(int32 I=0;I<Root->GetChildrenCount();++I)
    if(auto* Candidate=Cast<UEGHandCard>(Root->GetChildAt(I)))
     Test->AddInfo(FString::Printf(TEXT("Root card %s visible=%d instance=%s"),*Candidate->GetName(),Candidate->IsVisible()?1:0,*Candidate->InstanceId.ToString()));
   if(Test->TestNotNull(TEXT("Full hover card remains visible"),Hover))
   {
    const auto& G=View->GetCachedGeometry();const auto& HG=Hover->GetCachedGeometry();
    const FVector2D A=G.AbsoluteToLocal(HG.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D B=G.AbsoluteToLocal(HG.LocalToAbsolute(HG.GetLocalSize()));
    const FVector2D Bounds=G.GetLocalSize();
    Test->TestTrue(TEXT("Hover card uses full size and fits inside viewport"),
        FMath::IsNearlyEqual(B.X-A.X,140.f,2.f)&&FMath::IsNearlyEqual(B.Y-A.Y,196.f,2.f)&&
        A.X>=-1.f&&A.Y>=-1.f&&B.X<=Bounds.X+1.f&&B.Y<=Bounds.Y+1.f);
   }
   Next(7);return false;
  }
  if(!Settled(.15))return false;
  if(!CaptureFrame(World,TEXT("CombatHover")))return false;
  Test->AddInfo(TEXT("PIE uses controlled public transactions and programmatic hover; this does not test physical mouse, natural play, or visual acceptance."));
  return true;
 }
};

class FBoardLayoutCleanup final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Started=0.;
public:
 explicit FBoardLayoutCleanup(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(!Started)Started=FPlatformTime::Seconds();
  if(GEditor&&GEditor->PlayWorld&&FPlatformTime::Seconds()-Started<15.)return false;
  Test->TestTrue(TEXT("PIE world closed after layout capture"),GEditor&&!GEditor->PlayWorld);
  return true;
 }
};

/** Manual probe: only setup is scripted; the two observed gestures must be real input. */
class FBoardUIPhysicalInputProbe final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.,ReadyAt=0.;
 int32 Step=0;
 FGuid First,Last;
 TArray<FGuid> InitialOrder;
 bool bOwnPause=false;
public:
 explicit FBoardUIPhysicalInputProbe(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+300.;
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  AEGDemoGameMode* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  AEGCardManager* Cards=Mode?Mode->GetCards():nullptr;
  APlayerController* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* View=HUD?HUD->GetMainWidget():nullptr;
  if(FPlatformTime::Seconds()>Deadline)
  {
   if(bOwnPause&&PC)PC->SetPause(false);
   Test->AddError(FString::Printf(TEXT("Physical input probe timed out at step %d"),Step));return true;
  }
  if(!Mode||!Mode->GetMatch()||!Cards||!PC||!View||!View->HandScrollBox)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Opening=Cards->GetHand();
   if(!Test->TestEqual(TEXT("Input probe starts with two cards"),Opening.Num(),2))return true;
   if(!Test->TestTrue(TEXT("Input probe grants eight varied ordinary cards"),GrantVariedCards(Cards,Mode->RunDefinition.Get(),Opening)))return true;
   InitialOrder=Cards->GetOrderedHandIds();
   if(!Test->TestEqual(TEXT("Input probe has ten cards"),InitialOrder.Num(),10))return true;
   First=InitialOrder[0];Last=InitialOrder.Last();
   if(!Test->TestTrue(TEXT("Pause physical input fixture"),PC->SetPause(true)))return true;
   bOwnPause=true;ReadyAt=FPlatformTime::Seconds();Step=1;return false;
  }
  if(Step==1)
  {
   if(HasPendingVisualCompilation()||FPlatformTime::Seconds()-ReadyAt<1.)return false;
   auto* FirstCard=View->FindCardWidget(First);
   auto* LastCard=View->FindCardWidget(Last);
   auto* SecondCard=View->FindCardWidget(InitialOrder[1]);
   if(!FirstCard||!LastCard||!SecondCard)return false;
   const FGeometry& FirstGeo=FirstCard->GetCachedGeometry();
   const FGeometry& LastGeo=LastCard->GetCachedGeometry();
   const FGeometry& SecondGeo=SecondCard->GetCachedGeometry();
   if(FirstGeo.GetLocalSize().IsNearlyZero()||LastGeo.GetLocalSize().IsNearlyZero()||SecondGeo.GetLocalSize().IsNearlyZero())return false;
   const FSlateRect Row=View->HandScrollBox->GetCachedGeometry().GetRenderBoundingRect();
   const FSlateRect Screen=View->GetCachedGeometry().GetRenderBoundingRect();
   auto VisibleCentre=[&](const FGeometry& G,FVector2D& Out)
   {
    const FSlateRect Card=G.GetRenderBoundingRect();
    const float L=FMath::Max3(Card.Left,Row.Left,Screen.Left),R=FMath::Min3(Card.Right,Row.Right,Screen.Right);
    const float T=FMath::Max3(Card.Top,Row.Top,Screen.Top),B=FMath::Min3(Card.Bottom,Row.Bottom,Screen.Bottom);
    if(R<=L||B<=T)return false;
    Out=FVector2D((L+R)*.5f,(T+B)*.5f);return true;
   };
   FVector2D FirstCentre,LastCentre,SecondCentre;
   if(!VisibleCentre(FirstGeo,FirstCentre)||!VisibleCentre(LastGeo,LastCentre)||!VisibleCentre(SecondGeo,SecondCentre))return false;
   const FVector2D ReorderTarget((FirstCentre.X+SecondCentre.X)*.5f,SecondCentre.Y);
   auto* BoardPC=Cast<AEGBoardPlayerController>(PC);
   const auto* Scene=BoardPC?BoardPC->GetSceneSettings():nullptr;
   const auto* Grid=Scene?Scene->Board.Get():nullptr;
   if(!Test->TestNotNull(TEXT("Input probe board"),Grid))return true;
   FVector2D BoardPixel;
   if(!Test->TestTrue(TEXT("Input probe target projects"),PC->ProjectWorldLocationToScreen(Grid->CellToWorld({3,1}),BoardPixel,true)))return true;
   const float ViewportScale=UWidgetLayoutLibrary::GetViewportScale(World);
   const FVector2D BoardAbsolute=View->GetCachedGeometry().LocalToAbsolute(BoardPixel/FMath::Max(.01f,ViewportScale));
   const auto* Client=World->GetGameViewport();
   const FIntPoint ActualSize=Client&&Client->Viewport?Client->Viewport->GetSizeXY():FIntPoint::ZeroValue;
   UE_LOG(LogTemp,Display,TEXT("INPUT_PROBE_READY viewport=(%d,%d) uiScale=%.3f last=(%.0f,%.0f) reorderTarget=(%.0f,%.0f) first=(%.0f,%.0f) board=(%.0f,%.0f)"),
       ActualSize.X,ActualSize.Y,ViewportScale,LastCentre.X,LastCentre.Y,ReorderTarget.X,ReorderTarget.Y,FirstCentre.X,FirstCentre.Y,BoardAbsolute.X,BoardAbsolute.Y);
   Test->AddInfo(FString::Printf(TEXT("INPUT_PROBE_READY viewport=(%d,%d) uiScale=%.3f last=(%.0f,%.0f) reorderTarget=(%.0f,%.0f) first=(%.0f,%.0f) board=(%.0f,%.0f)"),
       ActualSize.X,ActualSize.Y,ViewportScale,LastCentre.X,LastCentre.Y,ReorderTarget.X,ReorderTarget.Y,FirstCentre.X,FirstCentre.Y,BoardAbsolute.X,BoardAbsolute.Y));
   Deadline=FPlatformTime::Seconds()+600.;Step=2;return false;
  }
  if(Step==2)
  {
   const auto Order=Cards->GetOrderedHandIds();
   if(Order.Num()<2||Order[1]!=Last)return false;
   Test->TestEqual(TEXT("Actual mouse reorder moves last card to position two"),Order[1],Last);
   UE_LOG(LogTemp,Display,TEXT("INPUT_PROBE_REORDER_OK"));
   Step=3;return false;
  }
  if(Cards->GetPieces().IsEmpty()||Cards->HasHandCard(First))return false;
  Test->TestFalse(TEXT("Actual mouse deployment consumes the original Elysia hand card"),Cards->HasHandCard(First));
  Test->TestEqual(TEXT("Actual mouse deployment produces one deployed piece"),Cards->GetPieces().Num(),1);
  UE_LOG(LogTemp,Display,TEXT("INPUT_PROBE_DEPLOY_OK"));
  if(bOwnPause){PC->SetPause(false);bOwnPause=false;}
  Test->AddInfo(TEXT("User-controlled OS mouse gestures were observed; scripted code only granted fixture cards and paused the preparation clock."));
  return true;
 }
};

bool QueueBoardLayoutPIE(FAutomationTestBase* Test,FIntPoint Size,const TCHAR* Prefix)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Board layout PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Board layout PIE requires a rendering RHI"));return false;}
 const FString Map=TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7");
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FBoardLayoutSizedPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FBoardLayoutCheck(Test,Size,Prefix));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FBoardLayoutCleanup(Test));
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardUILayout1080,"ElyGambit.Integration.BoardUILayout1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardUILayout1080::RunTest(const FString&){return QueueBoardLayoutPIE(this,FIntPoint(1920,1080),TEXT("BoardUILayout1080"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardUILayout900,"ElyGambit.Integration.BoardUILayout900",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardUILayout900::RunTest(const FString&){return QueueBoardLayoutPIE(this,FIntPoint(1600,900),TEXT("BoardUILayout900"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardUILayoutWide,"ElyGambit.Integration.BoardUILayoutWide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardUILayoutWide::RunTest(const FString&){return QueueBoardLayoutPIE(this,FIntPoint(2560,1080),TEXT("BoardUILayoutWide"));}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardUIInputProbe,"ElyGambit.Integration.BoardUIInputProbe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardUIInputProbe::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Physical input probe requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Physical input probe requires a rendering RHI"));return false;}
 const FString Map=TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7");
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FBoardLayoutSizedPIE(FIntPoint(1600,900)));
 ADD_LATENT_AUTOMATION_COMMAND(FBoardUIPhysicalInputProbe(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FBoardLayoutCleanup(this));
 return true;
}
#endif
