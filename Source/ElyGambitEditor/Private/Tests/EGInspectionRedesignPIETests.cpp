#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/SViewport.h"
#include "ImageUtils.h"
#include "EngineUtils.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Character/EGChessUnit.h"
#include "Actor/EGArtPiece.h"
#include "Actor/EGBoard.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Components/EGHeroSkillComponent.h"
#include "GameFramework/PlayerController.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGCombatTextLayer.h"
#include "UI/Widget/EGRichText.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"

namespace
{
class FInspectionSizedPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FInspectionSizedPIE(FIntPoint InSize):Size(InSize){}
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

struct FInspectionCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FInspectionCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FInspectionPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 FString Prefix;
 int32 Step=0;
 double Deadline=0.,StepAt=0.;
 FGuid HeroId;
 FGuid TemporaryHeroId;
 FString LongDescription;
 TSharedPtr<FInspectionCapture> Capture;
 TWeakObjectPtr<AEGChessUnit> CombatUnit;
 TWeakObjectPtr<AEGChessUnit> EnemyTextTarget;
 TWeakObjectPtr<UWorld> SlowCaptureWorld;
 float SavedTimeDilation=1.f;
 double EnemyTextStarted=-1.;
 bool bSecondEnemyText=false;
 void RestoreCaptureTime()
 {
  if(SlowCaptureWorld.IsValid()&&SlowCaptureWorld->GetWorldSettings())
   SlowCaptureWorld->GetWorldSettings()->SetTimeDilation(SavedTimeDilation);
  SlowCaptureWorld.Reset();
 }
 void Next(int32 NewStep){Step=NewStep;StepAt=FPlatformTime::Seconds();}
 bool Settled(double Seconds=.3)const{return FPlatformTime::Seconds()-StepAt>=Seconds;}
 bool CaptureFrame(UWorld* World,const TCHAR* Name)
 {
  if(!Capture.IsValid())
  {
   auto* Client=World?World->GetGameViewport():nullptr;
   if(!Client||!Client->Viewport)return false;
   Capture=MakeShared<FInspectionCapture>();Capture->Client=Client;
   Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/
      (Prefix+TEXT("_")+Name+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
   IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
   const TWeakPtr<FInspectionCapture> Weak=Capture;
   Capture->Handle=Client->OnDrawn().AddLambda([Weak]()
   {
    auto State=Weak.Pin();if(!State.IsValid())return;
    ++State->Draws;
    if((GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0){State->Draws=0;return;}
    if(State->Draws>=3&&!State->bRequested&&!FScreenshotRequest::IsScreenshotRequested())
    {State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);}
   });
   return false;
  }
  if(!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;
  if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto BE=[&](int32 Offset)->uint32{return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
  Test->TestEqual(TEXT("Captured game viewport dimensions"),FIntPoint(BE(16),BE(20)),Size);
  Test->AddInfo(FString::Printf(TEXT("Controlled PIE screenshot: %s"),*Capture->Path));
  Capture.Reset();return true;
 }
 bool Click(UButton* Button)
 {
  if(!Button||!Button->IsVisible()||!FSlateApplication::IsInitialized())return false;
  const FGeometry& Geometry=Button->GetCachedGeometry();
  if(Geometry.GetLocalSize().IsNearlyZero())return false;
  auto& App=FSlateApplication::Get();
  const TSharedPtr<SWindow> Window=App.FindWidgetWindow(Button->TakeWidget());
  if(!Window.IsValid())return false;
  const FVector2D Previous=App.GetCursorPos(),Point=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
  App.SetCursorPos(Point);
  App.ProcessMouseMoveEvent(FPointerEvent(0,Point,Previous,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
  const bool bHandled=App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
   FPointerEvent(0,Point,Point,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
  App.ProcessMouseButtonUpEvent(FPointerEvent(0,Point,Point,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
  App.SetCursorPos(Previous);
  App.ProcessMouseMoveEvent(FPointerEvent(0,Previous,Point,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
  Test->TestTrue(TEXT("Slate pointer down is handled by the actual button"),bHandled);
  return true;
 }
public:
 FInspectionPIE(FAutomationTestBase* InTest,FIntPoint InSize,const TCHAR* InPrefix):Test(InTest),Size(InSize),Prefix(InPrefix){}
 virtual ~FInspectionPIE() override { RestoreCaptureTime(); }
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+240.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(FString::Printf(TEXT("Inspection PIE timed out at step %d"),Step));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Cards=Mode?Mode->GetCards():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Main=HUD?HUD->GetMainWidget():nullptr;
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Mode||!Cards||!PC||!Main||!Client||!Client->Viewport||Client->Viewport->GetSizeXY()!=Size)return false;
  if(Step==0)
  {
   if((GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())||FAssetCompilingManager::Get().GetNumRemainingAssets()>0)return false;
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(auto* Masters=Mode->FindComponentByClass<UEGBoardMasterPresentation>())
   {
    const auto* Property=FindFProperty<FObjectPropertyBase>(Masters->GetClass(),TEXT("Enemy"));
    auto* Enemy=Property?Cast<AEGArtPiece>(Property->GetObjectPropertyValue_InContainer(Masters)):nullptr;
    FVector FriendlyAnchor,EnemyAnchor;
    if(Enemy&&Enemy->HeroVisualDefinition&&UEGBoardMasterPresentation::GetMasterAnchors(Mode->GetBoard(),FriendlyAnchor,EnemyAnchor))
    {
     const FRotator Expected=(FriendlyAnchor-EnemyAnchor).Rotation()+Enemy->HeroVisualDefinition->GetPresentationRotation()+FRotator(0.f,90.f,0.f);
     Test->TestTrue(TEXT("Enemy board master uses corrected facing"),Enemy->GetActorRotation().Equals(Expected,.1f));
    }
   }
   const auto Opening=Cards->GetHand();
   if(!Test->TestTrue(TEXT("Opening includes Elysia"),Opening.Num()>0&&Opening[0].Definition&&Opening[0].Definition->HeroDefinition&&Opening[0].Definition->HeroDefinition->HeroKind==EEGHeroKind::Elysia))return true;
   if(!Test->TestTrue(TEXT("Deploy ordinary Elysia through production transaction"),Cards->TryDeployHandCard(Opening[0].InstanceId,FIntPoint(3,1))))return true;
   auto* Roster=Mode->GetRoster();
   if(!Test->TestNotNull(TEXT("Production roster"),Roster))return true;
   HeroId=Roster->FindHeroByDefinition(Opening[0].Definition->HeroDefinition);
   if(!Test->TestTrue(TEXT("Deployed Elysia has roster identity"),HeroId.IsValid()))return true;
   Main->InspectHero(Roster,HeroId);
   Test->AddInfo(TEXT("Controlled fixture: programmatically deploy and inspect the normal starting Elysia; skill buttons use Slate pointer events. No combat values modified."));
   Next(1);return false;
  }
  auto* Property=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));
  auto* Info=Property?Cast<UEGHeroInfoWidget>(Property->GetObjectPropertyValue_InContainer(Main)):nullptr;
  if(!Info)return false;
  if(Step==1)
  {
   if(!Settled(.6)||!Info->IsVisible())return false;
   const TCHAR* IconNames[]={TEXT("BasicInfoIcon"),TEXT("UltimateInfoIcon"),TEXT("PassiveInfoIcon")};
   const TCHAR* ButtonNames[]={TEXT("Button_BasicInfo"),TEXT("Button_UltimateInfo"),TEXT("Button_PassiveInfo")};
   for(int32 Index=0;Index<3;++Index)
   {
    const TCHAR* IconName=IconNames[Index];
    auto* Icon=Cast<UImage>(Info->GetWidgetFromName(IconName));
    if(Test->TestNotNull(TEXT("Formal skill icon widget"),Icon))
    {
     Test->TestNotNull(FString::Printf(TEXT("%s original texture resolves"),IconName),Icon->GetBrush().GetResourceObject());
     const FVector2D IconSize=Icon->GetCachedGeometry().GetLocalSize();
     Test->TestTrue(FString::Printf(TEXT("%s actual icon geometry is 80x80 (%.1fx%.1f)"),IconName,IconSize.X,IconSize.Y),
      FMath::IsNearlyEqual(IconSize.X,80.f,1.f)&&FMath::IsNearlyEqual(IconSize.Y,80.f,1.f));
    }
    auto* Button=Info->GetWidgetFromName(ButtonNames[Index]);
    if(Test->TestNotNull(TEXT("Formal skill button geometry"),Button))
    {
     const FVector2D ButtonSize=Button->GetCachedGeometry().GetLocalSize();
     Test->TestTrue(FString::Printf(TEXT("%s actual hit area at least 104x116 (%.1fx%.1f)"),ButtonNames[Index],ButtonSize.X,ButtonSize.Y),
      ButtonSize.X>=103.5f&&ButtonSize.Y>=115.5f);
    }
   }
   if(Size==FIntPoint(1920,1080))
   {
    auto* LastStat=Info->GetWidgetFromName(TEXT("InfoShieldValue"));
    auto* FirstButton=Info->GetWidgetFromName(TEXT("Button_BasicInfo"));
    auto* ContentScroll=Info->GetWidgetFromName(TEXT("UnitInfoScroll"));
    if(Test->TestNotNull(TEXT("Last stat geometry"),LastStat)&&Test->TestNotNull(TEXT("First skill geometry"),FirstButton)&&Test->TestNotNull(TEXT("Inspection scroll geometry"),ContentScroll))
    {
     const FGeometry& Root=Info->GetCachedGeometry();
     const FGeometry& Stat=LastStat->GetCachedGeometry();const FGeometry& Skill=FirstButton->GetCachedGeometry();
     const FVector2D StatBottom=Root.AbsoluteToLocal(Stat.LocalToAbsolute(FVector2D(0,Stat.GetLocalSize().Y)));
     const FVector2D SkillTop=Root.AbsoluteToLocal(Skill.LocalToAbsolute(FVector2D::ZeroVector));
     const float Gap=SkillTop.Y-StatBottom.Y;
     Test->TestTrue(FString::Printf(TEXT("1080 skill follows last stat within 32 units (gap %.1f)"),Gap),Gap>=-1.f&&Gap<=32.f);
     const FGeometry& ScrollGeometry=ContentScroll->GetCachedGeometry();
     const FVector2D SkillBottom=ScrollGeometry.AbsoluteToLocal(Skill.LocalToAbsolute(Skill.GetLocalSize()));
     Test->TestTrue(TEXT("1080 skill is not clipped below the content scroll"),SkillBottom.Y<=ScrollGeometry.GetLocalSize().Y+.5f);
    }
   }
   if(!CaptureFrame(World,TEXT("Overview")))return false;
   Next(2);return false;
  }
  const TCHAR* Buttons[]={TEXT("Button_BasicInfo"),TEXT("Button_UltimateInfo"),TEXT("Button_PassiveInfo")};
  const TCHAR* Shots[]={TEXT("Basic"),TEXT("Ultimate"),TEXT("Passive")};
  const EEGSkillInfoKind Kinds[]={EEGSkillInfoKind::Basic,EEGSkillInfoKind::Ultimate,EEGSkillInfoKind::Passive};
  if(Step>=2&&Step<=7)
  {
   const int32 Index=(Step-2)/2;
   if((Step%2)==0)
   {
    if(!Settled(.3))return false;
    auto* Button=Cast<UButton>(Info->GetWidgetFromName(Buttons[Index]));
    if(!Test->TestNotNull(TEXT("Formal skill button"),Button))return true;
    if(!Click(Button))return false;
    Next(Step+1);return false;
   }
   if(!Settled(.4))return false;
   if(!Test->TestTrue(TEXT("Pointer opens selected details"),Info->HasSkillDetailsOpen()))return true;
   auto* Title=Cast<UTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsTitle")));
   auto* Body=Cast<UEGRichTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsBody")));
   const auto* Definition=Mode->GetRoster()?Mode->GetRoster()->ResolveDefinition(HeroId):nullptr;
   if(Test->TestNotNull(TEXT("Deployed hero definition"),Definition))
   {
    const FString ExpectedTitle=Definition->GetPlayerFacingSkillSectionName(Kinds[Index]).ToString();
    if(Test->TestNotNull(TEXT("Selected skill title"),Title))Test->TestEqual(TEXT("Selected skill title matches pointer"),Title->GetText().ToString(),ExpectedTitle);
    if(Test->TestNotNull(TEXT("Selected skill body"),Body))
     Test->TestTrue(TEXT("Selected skill body contains the deployed hero configuration"),Body->GetPlainText().ToString().StartsWith(Definition->GetPlayerFacingSkillSection(Kinds[Index]).ToString()));
   }
   const bool bNarrow=808.f>Main->GetCachedGeometry().GetLocalSize().X*.65f;
   if(Size==FIntPoint(720,900))Test->TestTrue(TEXT("Portrait fixture exercises inline skill details after DPI scaling"),bNarrow);
   auto* Scroll=Info->GetWidgetFromName(TEXT("UnitInfoScroll"));
   auto* Content=Info->GetWidgetFromName(TEXT("UnitInfoBody"));
   if(bNarrow&&Test->TestNotNull(TEXT("Narrow content scroll"),Scroll)&&Test->TestNotNull(TEXT("Narrow character body"),Content))
   {
    Test->TestEqual(TEXT("Narrow details hide character body"),Content->GetVisibility(),ESlateVisibility::Collapsed);
    Test->TestEqual(TEXT("Narrow details retain skill scroll"),Scroll->GetVisibility(),ESlateVisibility::Visible);
   }
   auto* SelectedButton=Info->GetWidgetFromName(Buttons[Index]);
   if(Test->TestNotNull(TEXT("Selected button for hit test"),SelectedButton))
   {
    const FGeometry& G=SelectedButton->GetCachedGeometry();
    Test->TestTrue(TEXT("Skill button blocks board clicks"),Main->IsPointBlocked(G.LocalToAbsolute(G.GetLocalSize()*.5f)));
   }
   if(Title)
   {
    const FGeometry& G=Title->GetCachedGeometry();
    Test->TestTrue(TEXT("Skill explanation content blocks board clicks"),Main->IsPointBlocked(G.LocalToAbsolute(G.GetLocalSize()*.5f)));
   }
   if(!bNarrow)
   {
    const FVector2D Gap=Info->GetCachedGeometry().LocalToAbsolute(FVector2D(382.f,100.f));
    Test->TestFalse(TEXT("Transparent 12-unit gap does not block board clicks"),Main->IsPointBlocked(Gap));
   }
   if(!CaptureFrame(World,Shots[Index]))return false;
   Next(Step==7?(bNarrow?14:(Size==FIntPoint(1920,1080)?17:8)):Step+1);return false;
  }
  if(Step==14)
  {
   auto* Roster=Mode->GetRoster();auto* Source=Roster?Roster->ResolveDefinition(HeroId):nullptr;
   if(!Test->TestNotNull(TEXT("Source definition for transient long-text fixture"),Source))return true;
   auto* Fixture=DuplicateObject<UEGHeroDefinition>(Source,GetTransientPackage());
   if(!Test->TestNotNull(TEXT("Transient long-text definition"),Fixture))return true;
   Fixture->HeroKind=EEGHeroKind::Generic;
   Fixture->HeroId=FName(TEXT("InspectionLongTextFixture"));Fixture->EnhancedDefinition=nullptr;
   for(int32 I=0;I<80;++I)LongDescription+=TEXT("技能说明：长文本应可完整滚动查看。\n");
   if(Fixture->bUseSplitConfiguration)
   {
    if(!Test->TestNotNull(TEXT("Source skill configuration"),Fixture->SkillDefinition.Get()))return true;
    Fixture->SkillDefinition=DuplicateObject<UEGHeroSkillDefinition>(Fixture->SkillDefinition.Get(),Fixture);
    Fixture->SkillDefinition->PassiveDescription=FText::FromString(LongDescription);
   }
   else Fixture->PassiveDescription=FText::FromString(LongDescription);
   TemporaryHeroId=Roster->AddHero(Fixture);
   if(!Test->TestTrue(TEXT("Transient long-text hero added without deployment"),TemporaryHeroId.IsValid()))return true;
   Main->InspectHero(Roster,TemporaryHeroId);Next(23);return false;
  }
  if(Step==23)
  {
   if(!Settled(.4))return false;
   auto* Button=Cast<UButton>(Info->GetWidgetFromName(TEXT("Button_PassiveInfo")));
   if(!Test->TestNotNull(TEXT("Long-text fixture passive button"),Button))return true;
   if(!Click(Button))return false;
   Next(15);return false;
  }
  if(Step==15)
  {
   if(!Settled(.4))return false;
   auto* DetailScroll=Cast<UScrollBox>(Info->GetWidgetFromName(TEXT("ActionDetailsScroll")));
   auto* Body=Cast<UEGRichTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsBody")));
   if(!Test->TestNotNull(TEXT("Long skill description scroll"),DetailScroll)||!Test->TestNotNull(TEXT("Long skill description body"),Body))return true;
   Test->TestTrue(TEXT("Long description survives normal inspection refresh"),Body->GetPlainText().ToString().StartsWith(LongDescription));
   Test->TestTrue(TEXT("Long skill description exceeds the visible area"),DetailScroll->GetScrollOffsetOfEnd()>0.f);
   DetailScroll->ScrollToEnd();Next(16);return false;
  }
  if(Step==16)
  {
   if(!Settled(.4))return false;
   auto* DetailScroll=Cast<UScrollBox>(Info->GetWidgetFromName(TEXT("ActionDetailsScroll")));
   auto* Body=Cast<UEGRichTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsBody")));
   if(!Test->TestNotNull(TEXT("Long skill scroll remains"),DetailScroll)||!Test->TestNotNull(TEXT("Skill body remains"),Body))return true;
   Test->TestTrue(TEXT("Long skill description scrolls to later content"),DetailScroll->GetScrollOffset()>0.f);
   for(const TCHAR* Name:Buttons)
   {
    auto* Button=Info->GetWidgetFromName(Name);
    if(Test->TestNotNull(TEXT("Inline skill entry remains"),Button))
    {
     Test->TestTrue(TEXT("Inline skill entry remains visible"),Button->IsVisible());
     const FGeometry& G=Button->GetCachedGeometry();
     Test->TestTrue(TEXT("Inline skill entry remains hit-testable"),Main->IsPointBlocked(G.LocalToAbsolute(G.GetLocalSize()*.5f)));
    }
   }
   Main->InspectHero(Mode->GetRoster(),HeroId);
   Test->TestTrue(TEXT("Transient long-text roster entry is removed"),Mode->GetRoster()->RemoveUncommittedHero(TemporaryHeroId));
   TemporaryHeroId.Invalidate();Next(8);return false;
  }
  if(Step==17||Step==20)
  {
   const TCHAR* Path=Step==17?TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Mobius.DA_Mobius"):
    TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Griseo.DA_Griseo");
   auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,Path);
   if(!Test->TestNotNull(TEXT("Formal long-description hero"),Definition))return true;
   TemporaryHeroId=Mode->GetRoster()->AddHero(Definition);
   if(!Test->TestTrue(TEXT("Formal screenshot hero added without deployment"),TemporaryHeroId.IsValid()))return true;
   Main->InspectHero(Mode->GetRoster(),TemporaryHeroId);
   Test->AddInfo(FString::Printf(TEXT("Temporary roster-only long-description fixture: %s; no hero deployed and no combat values changed."),Path));
   Next(Step+1);return false;
  }
  if(Step==18||Step==21)
  {
   if(!Settled(.4))return false;
   auto* Button=Cast<UButton>(Info->GetWidgetFromName(TEXT("Button_PassiveInfo")));
   if(!Test->TestNotNull(TEXT("Long-description passive entry"),Button))return true;
   if(!Click(Button))return false;
   Next(Step+1);return false;
  }
  if(Step==19||Step==22)
  {
   if(!Settled(.4))return false;
   if(!Test->TestTrue(TEXT("Formal long-description details opened"),Info->HasSkillDetailsOpen()))return true;
   const TCHAR* Path=Step==19?TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Mobius.DA_Mobius"):
    TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Griseo.DA_Griseo");
   auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,Path);
   auto* Body=Cast<UEGRichTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsBody")));
   if(!Test->TestNotNull(TEXT("Formal long-description hero remains loaded"),Definition)||!Test->TestNotNull(TEXT("Formal long-description body"),Body))return true;
   const FString Expected=Definition->GetPlayerFacingSkillSection(EEGSkillInfoKind::Passive).ToString();
   Test->TestTrue(TEXT("Actual passive text is shown"),!Expected.IsEmpty()&&Body->GetPlainText().ToString().StartsWith(Expected));
   if(!CaptureFrame(World,Step==19?TEXT("MobiusPassive"):TEXT("GriseoPassive")))return false;
   Main->InspectHero(Mode->GetRoster(),HeroId);
   Test->TestTrue(TEXT("Formal screenshot roster entry is removed"),Mode->GetRoster()->RemoveUncommittedHero(TemporaryHeroId));
   TemporaryHeroId.Invalidate();
   if(Step==19){Next(20);return false;}
   Next(8);return false;
  }
  if(Step==8)
  {
   if(!Mode->EnterCombat()){Test->AddError(TEXT("Could not enter combat for controlled text fixture"));return true;}
   Test->AddInfo(TEXT("Combat text screenshot uses explicitly injected presentation receipts; this does not prove natural combat values."));
   Next(9);return false;
  }
  if(Step==9)
  {
   if(!Settled(.6))return false;
   for(TActorIterator<AEGChessUnit> It(World);It;++It)
    if(It->GetDefinition()&&It->GetDefinition()->HeroKind==EEGHeroKind::Elysia){CombatUnit=*It;break;}
   if(!CombatUnit.IsValid())return false;
   Main->InspectUnit(CombatUnit.Get());
   auto* Layer=HUD->GetCombatTextLayer();if(!Test->TestNotNull(TEXT("HUD combat text layer"),Layer))return true;
   Layer->ClearNumbers();
   FEGCombatResolution Damage;Damage.HealthDelta=-128.f;Damage.ShieldAbsorbed=42.f;
   FEGCombatResolution Heal;Heal.bHealing=true;Heal.HealthDelta=86.f;
   HUD->PresentCombatText(CombatUnit.Get(),Damage);HUD->PresentCombatText(CombatUnit.Get(),Heal);
   Test->TestEqual(TEXT("Injected damage, shield and healing remain three distinct entries"),Layer->GetActiveCount(),3);
   FEGCombatResolution SameDamage;SameDamage.HealthDelta=-2.f;HUD->PresentCombatText(CombatUnit.Get(),SameDamage);
   Test->TestEqual(TEXT("Second same-frame health-damage receipt merges before limits"),Layer->GetActiveCount(),3);
   Next(10);return false;
  }
  if(Step==10)
  {
   if(!CaptureFrame(World,TEXT("InjectedCombatText")))return false;
   Next(Size==FIntPoint(1920,1080)?30:11);return false;
  }
  if(Step==30)
  {
   auto* Layer=HUD->GetCombatTextLayer();if(!Test->TestNotNull(TEXT("Combat text layer for enemy capture"),Layer))return true;
   AEGChessUnit* Enemy=nullptr;
   const FVector2D View=UWidgetLayoutLibrary::GetViewportSize(World)/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(World));
   for(auto* Unit:Mode->GetCombatUnits())
   {
    FVector2D Screen;
    if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Enemy&&
       UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(PC,Unit->GetActorLocation()+FVector(0,0,25),Screen,true)&&
       Screen.X>=0&&Screen.Y>=0&&Screen.X<=View.X&&Screen.Y<=View.Y){Enemy=Unit;break;}
   }
   if(!Test->TestNotNull(TEXT("On-screen enemy for injected text capture"),Enemy))return true;
   auto* Settings=World->GetWorldSettings();if(!Test->TestNotNull(TEXT("World settings for slow capture"),Settings))return true;
   SlowCaptureWorld=World;SavedTimeDilation=Settings->TimeDilation;Settings->SetTimeDilation(.2f);
   Layer->ClearNumbers();const int32 Before=Layer->GetPoolSize();
   FEGCombatResolution EnemyDamage;EnemyDamage.HealthDelta=-128.f;
   HUD->PresentCombatText(Enemy,EnemyDamage);
   Test->TestEqual(TEXT("Enemy damage produces one number"),Layer->GetActiveCount(),1);
   if(Before>0)Test->TestEqual(TEXT("Enemy damage reuses a pooled number"),Layer->GetPoolSize(),Before-1);
   EnemyTextTarget=Enemy;EnemyTextStarted=World->GetTimeSeconds();bSecondEnemyText=false;
   Test->AddInfo(TEXT("Enemy combat-text screenshots use injected damage and 0.2 world time dilation; sample timings are logged, not natural combat."));
   Next(31);return false;
  }
  if(Step>=31&&Step<=33)
  {
   const double Age=World->GetTimeSeconds()-EnemyTextStarted;
   if(!bSecondEnemyText&&Age>=.16&&EnemyTextTarget.IsValid())
   {
    FEGCombatResolution SecondDamage;SecondDamage.HealthDelta=-64.f;
    HUD->PresentCombatText(EnemyTextTarget.Get(),SecondDamage);
    bSecondEnemyText=true;
   }
   const double SampleTimes[]={.08,.32,.56};
   if(Age<SampleTimes[Step-31])return false;
   if(!CaptureFrame(World,Step==31?TEXT("EnemyTextRise"):Step==32?TEXT("EnemyTextPeak"):TEXT("EnemyTextFall")))return false;
   Test->AddInfo(FString::Printf(TEXT("Enemy text screenshot %d completed at game-time age %.3f seconds; actual image request occurred earlier."),Step,World->GetTimeSeconds()-EnemyTextStarted));
   if(Step==33){RestoreCaptureTime();Next(11);return false;}
   Next(Step+1);return false;
  }
  if(Step==11)
  {
   if(!Settled(.4))return false;
   auto* Button=Cast<UButton>(Info->GetWidgetFromName(TEXT("Button_BasicInfo")));
   if(!Test->TestNotNull(TEXT("Combat skill button"),Button))return true;
   if(!Click(Button))return false;
   Next(12);return false;
  }
  if(Step==12)
  {
   if(!Settled(.2))return false;
   if(!Test->TestTrue(TEXT("Combat details are open before target destruction"),Info->HasSkillDetailsOpen()))return true;
   if(!Test->TestTrue(TEXT("Controlled target still valid before destruction"),CombatUnit.IsValid()))return true;
   CombatUnit->Destroy();
   Test->AddInfo(TEXT("After the screenshot, the selected combat actor is deliberately destroyed for lifecycle coverage. This is not a natural battle."));
   Next(13);return false;
  }
  if(Step==13)
  {
   if(!Settled(.3))return false;
   Test->TestFalse(TEXT("Target destruction closes the skill subpanel"),Info->HasSkillDetailsOpen());
   Test->AddInfo(TEXT("Skill interactions use synthetic Slate mouse events, not OS mouse or a human player."));
   return true;
  }
  return true;
 }
};

bool QueueInspectionPIE(FAutomationTestBase* Test,FIntPoint Size,const TCHAR* Prefix)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Inspection PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Inspection PIE requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FInspectionSizedPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FInspectionPIE(Test,Size,Prefix));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionPIE1080,"ElyGambit.Integration.InspectionRedesign1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGInspectionPIE1080::RunTest(const FString&){return QueueInspectionPIE(this,FIntPoint(1920,1080),TEXT("Inspection1080"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionPIE1440,"ElyGambit.Integration.InspectionRedesign1440",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGInspectionPIE1440::RunTest(const FString&){return QueueInspectionPIE(this,FIntPoint(2560,1440),TEXT("Inspection1440"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionPIE720,"ElyGambit.Integration.InspectionRedesign720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGInspectionPIE720::RunTest(const FString&){return QueueInspectionPIE(this,FIntPoint(1280,720),TEXT("Inspection720"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionPIENarrow,"ElyGambit.Integration.InspectionRedesignNarrow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGInspectionPIENarrow::RunTest(const FString&){return QueueInspectionPIE(this,FIntPoint(720,900),TEXT("InspectionNarrow"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionPIEWide,"ElyGambit.Integration.InspectionRedesignWide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGInspectionPIEWide::RunTest(const FString&){return QueueInspectionPIE(this,FIntPoint(2560,1080),TEXT("InspectionWide"));}
// Independent observer for an existing ProductionNaturalNine run. Register it
// with InspectionNaturalCapture, then run ProductionNaturalNine in the same
// editor process; it never supplies cards, money, attributes or battle results.
namespace
{
struct FInspectionNaturalFrame
{
 FString Name;
 double WorldTime=0.,WallTime=0.;
 uint64 EngineFrame=0;
 int32 Width=0,Height=0;
 bool bSidebar=false;
};

class FInspectionNaturalObserver : public TSharedFromThis<FInspectionNaturalObserver>
{
 FTSTicker::FDelegateHandle TickerHandle;
 TWeakObjectPtr<UWorld> ObservedWorld;
 FString Directory;
 TArray<FInspectionNaturalFrame> Frames;
 double Deadline=0.,CaptureStart=0.,LastRequestAt=0.;
 bool bActive=false,bSawWorld=false,bSidebar=false,bMixedSizes=false;
 int32 InitialDeployed=0,InitialAlive=0,MinimumAlive=MAX_int32;

 void Finish(const TCHAR* Status,const TCHAR* Reason)
 {
  if(!bActive)return;
  bActive=false;
  FString CSV=TEXT("file,world_seconds,wall_seconds,engine_frame,sidebar_open,width,height\n");
  for(const auto& Frame:Frames)
   CSV+=FString::Printf(TEXT("%s,%.6f,%.6f,%llu,%d,%d,%d\n"),*Frame.Name,Frame.WorldTime,Frame.WallTime,
    static_cast<unsigned long long>(Frame.EngineFrame),Frame.bSidebar?1:0,Frame.Width,Frame.Height);
  const double Span=Frames.Num()>1?Frames.Last().WallTime-Frames[0].WallTime:0.;
  const double WorldSpan=Frames.Num()>1?Frames.Last().WorldTime-Frames[0].WorldTime:0.;
  const FString Summary=FString::Printf(TEXT("{\n  \"status\": \"%s\",\n  \"reason\": \"%s\",\n  \"capture\": \"ProductionNaturalNine, stage 7, UI included; no presentation receipts injected\",\n  \"initial_deployed\": %d,\n  \"initial_alive\": %d,\n  \"minimum_alive\": %d,\n  \"frames\": %d,\n  \"wall_span_seconds\": %.6f,\n  \"world_span_seconds\": %.6f,\n  \"measured_fps\": %.3f,\n  \"sidebar_opened\": %s\n}\n"),
   Status,Reason,InitialDeployed,InitialAlive,MinimumAlive==MAX_int32?0:MinimumAlive,Frames.Num(),Span,WorldSpan,Span>0?(Frames.Num()-1)/Span:0.,bSidebar?TEXT("true"):TEXT("false"));
  FFileHelper::SaveStringToFile(CSV,*(Directory/TEXT("frames.csv")));
  FFileHelper::SaveStringToFile(Summary,*(Directory/TEXT("manifest.json")));
  UE_LOG(LogTemp,Display,TEXT("INSPECTION_NATURAL_CAPTURE status=%s reason=%s frames=%d wall=%.3f path=%s"),Status,Reason,Frames.Num(),Span,*Directory);
 }

 bool Tick(float)
 {
  if(!bActive)return false;
  const double Wall=FPlatformTime::Seconds();
  if(Wall>Deadline){Finish(TEXT("FAILED"),TEXT("observer_timeout"));return false;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  if(!World)
  {
   if(bSawWorld){Finish(TEXT("FAILED"),TEXT("pie_ended_before_capture"));return false;}
   return true;
  }
  bSawWorld=true;
  if(ObservedWorld.IsValid()&&ObservedWorld.Get()!=World){Finish(TEXT("FAILED"),TEXT("world_changed"));return false;}
  auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  auto* Match=Mode?Mode->GetMatch():nullptr;
  auto* Roster=Mode?Mode->GetRoster():nullptr;
  if(!Mode||!Match||!Roster)return true;
  if(CaptureStart==0.)
  {
   if(Match->GetStageNumber()!=7||Match->GetPhase()!=EEGMatchPhase::Combat||Mode->GetBattleSeconds()<1.f)return true;
   InitialDeployed=0;for(const auto& Hero:Roster->GetHeroes())if(Hero.bDeployed)++InitialDeployed;
   if(InitialDeployed!=9){Finish(TEXT("FAILED"),TEXT("natural_route_not_nine_deployed"));return false;}
   InitialAlive=0;
   for(const auto* Unit:Mode->GetCombatUnits())
    if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->IsAlive_Implementation()&&
       !Unit->IsAuxiliaryEmplacement()&&Unit->GetHeroSkill()&&!Unit->GetHeroSkill()->bSummon)++InitialAlive;
   if(InitialAlive!=9){Finish(TEXT("FAILED"),TEXT("initial_combat_not_nine_alive"));return false;}
   if(!World->GetGameViewport()||!World->GetGameViewport()->Viewport)return true;
   ObservedWorld=World;CaptureStart=Wall;
  }
  if(Match->GetStageNumber()!=7||Match->GetPhase()!=EEGMatchPhase::Combat)
  {Finish(TEXT("FAILED"),TEXT("combat_ended_before_capture"));return false;}
  int32 Alive=0;
  for(const auto* Unit:Mode->GetCombatUnits())
   if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->IsAlive_Implementation()&&
      !Unit->IsAuxiliaryEmplacement()&&Unit->GetHeroSkill()&&!Unit->GetHeroSkill()->bSummon)++Alive;
  MinimumAlive=FMath::Min(MinimumAlive,Alive);
  if(Wall-CaptureStart>=4.&&!bSidebar)
  {
   auto* PC=World->GetFirstPlayerController();auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
   auto* Main=HUD?HUD->GetMainWidget():nullptr;AEGChessUnit* Selected=nullptr;
   for(AEGChessUnit* Unit:Mode->GetCombatUnits())
    if(IsValid(Unit)&&!Unit->IsActorBeingDestroyed()&&Unit->IsAlive_Implementation()&&Unit->GetDefinition()&&
       Unit->GetFaction_Implementation()==EEGFaction::Player&&!Unit->IsAuxiliaryEmplacement()&&
       (!Selected||Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia))
    {Selected=Unit;if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia)break;}
   if(Main&&Selected){Main->InspectUnit(Selected);bSidebar=true;}
  }
  if(Wall-CaptureStart>=8.)
  {
   if(Frames.Num()<2||!bSidebar){Finish(TEXT("FAILED"),TEXT("insufficient_frames_or_sidebar"));return false;}
   const double Span=Frames.Last().WallTime-Frames[0].WallTime;
   const double FPS=Span>0?(Frames.Num()-1)/Span:0.;
   if(bMixedSizes)Finish(TEXT("PARTIAL"),TEXT("viewport_size_changed"));
   else if(FPS<8.)Finish(TEXT("PARTIAL"),TEXT("capture_rate_below_8fps"));
   else Finish(TEXT("COMPLETE"),TEXT("inspect_visible_frames"));
   return false;
  }
  if(Wall-LastRequestAt<.1)return true;
  auto* Client=World->GetGameViewport();
  const TSharedPtr<SViewport> Viewport=Client?Client->GetGameViewportWidget():nullptr;
  const TSharedPtr<SWindow> Window=Client?Client->GetWindow():nullptr;
  if(!Client||!Client->Viewport||!Viewport.IsValid()||!Window.IsValid()||
     FSlateApplication::Get().FindWidgetWindow(Viewport.ToSharedRef())!=Window)
  {Finish(TEXT("FAILED"),TEXT("game_viewport_window_invalid"));return false;}
  const FIntPoint ViewportSize=Client->Viewport->GetSizeXY();
  TArray<FColor> Pixels;FIntVector Size;
  const double FrameWorld=World->GetTimeSeconds(),FrameWall=FPlatformTime::Seconds();
  const uint64 FrameNumber=GFrameCounter;
  if(!FSlateApplication::Get().TakeScreenshot(Viewport.ToSharedRef(),Pixels,Size)||
     Size.X<=0||Size.Y<=0||Size.X!=ViewportSize.X||Size.Y!=ViewportSize.Y||
     int64(Pixels.Num())!=int64(Size.X)*Size.Y)
  {Finish(TEXT("FAILED"),TEXT("game_viewport_screenshot_failed_or_size_mismatch"));return false;}
  for(FColor& Pixel:Pixels)Pixel.A=255;
  TArray64<uint8> PNG;
  FImageUtils::PNGCompressImageArray(Size.X,Size.Y,TArrayView64<const FColor>(Pixels.GetData(),Pixels.Num()),PNG);
  const FString Name=FString::Printf(TEXT("frame_%06d.png"),Frames.Num());
  if(PNG.IsEmpty()||!FFileHelper::SaveArrayToFile(PNG,*(Directory/Name)))
  {Finish(TEXT("FAILED"),TEXT("screenshot_write_failed"));return false;}
  if(Frames.Num()>0&&(Frames[0].Width!=Size.X||Frames[0].Height!=Size.Y))bMixedSizes=true;
  FInspectionNaturalFrame& Frame=Frames.AddDefaulted_GetRef();
  Frame.Name=Name;Frame.WorldTime=FrameWorld;Frame.WallTime=FrameWall;Frame.EngineFrame=FrameNumber;
  Frame.bSidebar=bSidebar;Frame.Width=Size.X;Frame.Height=Size.Y;LastRequestAt=FrameWall;
  return true;
 }
public:
 explicit FInspectionNaturalObserver(const FString& InDirectory):Directory(InDirectory){}
 ~FInspectionNaturalObserver()
 {
  if(TickerHandle.IsValid())FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
 }
 bool IsActive()const{return bActive;}
 void Start()
 {
  IFileManager::Get().MakeDirectory(*Directory,true);bActive=true;Deadline=FPlatformTime::Seconds()+900.;
  const TWeakPtr<FInspectionNaturalObserver> Weak=AsShared();
  TickerHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float Delta)
  {const auto State=Weak.Pin();return State.IsValid()&&State->Tick(Delta);}));
 }
};
TSharedPtr<FInspectionNaturalObserver> GInspectionNaturalObserver;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGInspectionNaturalCaptureArm,"ElyGambit.Integration.InspectionNaturalCapture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGInspectionNaturalCaptureArm::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())
 {AddError(TEXT("Arm requires an idle graphical editor; then run ProductionNaturalNine in the same editor process."));return false;}
 if(GInspectionNaturalObserver.IsValid()&&GInspectionNaturalObserver->IsActive())
 {AddError(TEXT("A natural capture observer is already active."));return false;}
 const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("UIRedesign20261109/QA/Natural")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
 GInspectionNaturalObserver=MakeShared<FInspectionNaturalObserver>(Directory);GInspectionNaturalObserver->Start();
 AddInfo(FString::Printf(TEXT("Observer ARMED only; run ElyGambit.Integration.ProductionNaturalNine in this same process. Final result, actual timing and frames will be at %s/manifest.json"),*Directory));
 return true;
}

#endif
