#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "GameFramework/PlayerController.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Interaction/EGCombatant.h"
#include "UI/EGUnitInspectionView.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGRichText.h"
#include "Components/Image.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/ScrollBox.h"
#include "UObject/UnrealType.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"

namespace
{
class FEnemyReworkStartPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FEnemyReworkStartPIE(FIntPoint InSize):Size(InSize){}
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(Size.X,Size.Y))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(!GUnrealEd->CheckForPlayerStart())FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};
struct FEnemyReworkCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FEnemyReworkCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};
class FEnemyReworkRun final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 FString Directory;
 double Deadline=0.,At=0.;
 int32 Step=0,SkillIndex=0;
 bool bPositionChecked=false;
 TWeakObjectPtr<AEGChessUnit> Boss,Hero;
 TWeakObjectPtr<UEGCorruptionCombatComponent> Rules;
 TArray<TWeakObjectPtr<AEGChessUnit>> OldEnemies;
 TSharedPtr<FEnemyReworkCapture> Capture;
 void Next(int32 NewStep){Step=NewStep;At=FPlatformTime::Seconds();}
 bool Ready()const{return FPlatformTime::Seconds()-At>.4;}
 bool Fail(const FString& Text){Capture.Reset();Test->AddError(Text);return true;}
 bool Screenshot(UWorld* World,const FString& Name)
 {
  if(!Capture.IsValid())
  {
   auto* Client=World?World->GetGameViewport():nullptr;if(!Client||!Client->Viewport)return false;
   Capture=MakeShared<FEnemyReworkCapture>();Capture->Client=Client;Capture->Path=Directory/Name;
   const TWeakPtr<FEnemyReworkCapture> Weak=Capture;
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
  Test->TestEqual(TEXT("Screenshot uses the actual requested PIE viewport"),FIntPoint(BE(16),BE(20)),Size);
  Test->AddInfo(FString::Printf(TEXT("Controlled enemy inspection screenshot: %s"),*Capture->Path));Capture.Reset();return true;
 }
 UEGHeroInfoWidget* Info(UEGMainHUD* Main)const
 {
  auto* Property=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));
  return Property&&Main?Cast<UEGHeroInfoWidget>(Property->GetObjectPropertyValue_InContainer(Main)):nullptr;
 }
 bool CheckCard(UEGHeroInfoWidget* Widget,int32 Phase)
 {
  auto* Card=Widget?Cast<UImage>(Widget->GetWidgetFromName(TEXT("InfoCard"))):nullptr;
  auto* Expected=LoadObject<UTexture2D>(nullptr,Phase==1?TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase1.T_Card_Corruption_Phase1"):TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase2.T_Card_Corruption_Phase2"));
  if(!Test->TestNotNull(TEXT("Phase card widget"),Card)||!Test->TestNotNull(TEXT("Phase card asset"),Expected))return false;
  Test->TestEqual(TEXT("Boss image follows its live combat phase"),Card->GetBrush().GetResourceObject(),static_cast<UObject*>(Expected));
  const auto* Box=Cast<USizeBox>(Widget->GetWidgetFromName(TEXT("InfoCardSize")));
  if(Test->TestNotNull(TEXT("Boss card size"),Box))Test->TestTrue(TEXT("5:7 card slot preserves 5:7 source"),FMath::IsNearlyEqual(Box->GetWidthOverride()/Box->GetHeightOverride(),5.f/7.f,.001f));
  const FBox2f UV=Card->GetBrush().GetUVRegion();
  if(Test->TestTrue(TEXT("Phase artwork uses a bounded crop of the original image"),UV.bIsValid))
  {
   Test->TestTrue(TEXT("Crop stays inside the source image"),UV.Min.X>=0.f&&UV.Min.Y>=0.f&&UV.Max.X<=1.f&&UV.Max.Y<=1.f&&UV.Max.X>UV.Min.X&&UV.Max.Y>UV.Min.Y);
#if WITH_EDITOR
   const float SourceWidth=float(Expected->Source.GetSizeX()),SourceHeight=float(Expected->Source.GetSizeY());
   auto* Slot=Cast<UCanvasPanelSlot>(Card->Slot);
   if(Test->TestTrue(TEXT("Original image has nonzero dimensions"),SourceWidth>0.f&&SourceHeight>0.f)&&
      Test->TestNotNull(TEXT("Portrait image uses a canvas slot"),Slot)&&Box)
   {
    const FAnchors Anchors=Slot->GetAnchors();
    const float VisibleAspect=((UV.Max.X-UV.Min.X)*SourceWidth)/((UV.Max.Y-UV.Min.Y)*SourceHeight);
    const float DisplayAspect=(Box->GetWidthOverride()*(Anchors.Maximum.X-Anchors.Minimum.X))/(Box->GetHeightOverride()*(Anchors.Maximum.Y-Anchors.Minimum.Y));
    Test->TestTrue(TEXT("Source crop matches the displayed portrait area without stretching"),FMath::IsNearlyEqual(VisibleAspect,DisplayAspect,.005f));
   }
#endif
  }
  for(const TCHAR* Name:{TEXT("InfoCard_CorruptionPhaseBar"),TEXT("InfoCard_CorruptionPhaseTitleFit"),TEXT("InfoCard_CorruptionPhaseBadge"),TEXT("InfoCard_CorruptionPhaseMark")})
  {
   auto* Decoration=Widget->GetWidgetFromName(Name);
   if(Test->TestNotNull(TEXT("Phase card decoration exists"),Decoration))Test->TestTrue(TEXT("Phase card decoration visible"),Decoration->GetVisibility()!=ESlateVisibility::Collapsed);
  }
  Test->TestTrue(TEXT("Card has no horizontal or vertical render stretch"),Card->GetRenderTransform().Scale.Equals(FVector2D(1.f,1.f),.001f));
  return true;
 }
public:
 FEnemyReworkRun(FAutomationTestBase* InTest,FIntPoint InSize):Test(InTest),Size(InSize){}
 bool Update() override
 {
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Main=HUD?HUD->GetMainWidget():nullptr;
  auto* Client=World?World->GetGameViewport():nullptr;
  if(Deadline==0.)
  {
   Deadline=FPlatformTime::Seconds()+240.;Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("EnemyRework20261110/Graphical")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
   IFileManager::Get().MakeDirectory(*Directory,true);
  }
  if(FPlatformTime::Seconds()>Deadline)return Fail(FString::Printf(TEXT("Enemy inspection timed out at step %d skill %d"),Step,SkillIndex));
  if(!Mode||!Main||!Mode->BattleRules||!Mode->GetBoard()||!Client||!Client->Viewport||Client->Viewport->GetSizeXY()!=Size)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Hand=Mode->GetCards()->GetHand();if(Hand.IsEmpty())return false;
   if(!Test->TestTrue(TEXT("Deploy opening hero"),Mode->GetCards()->TryDeployHandCard(Hand[0].InstanceId,{3,1}))||
      !Test->TestTrue(TEXT("Begin controlled battle"),Mode->EnterCombat()))return true;
   Next(1);return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->CancelAction();
    if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;
    else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)OldEnemies.Add(Unit);
   }
   auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
   if(!Test->TestNotNull(TEXT("Formal boss definition"),Definition)||!Test->TestNotNull(TEXT("Attacking hero"),Hero.Get()))return true;
   Boss=Mode->SpawnBattleSummon(Definition,1,Mode->GetBoard()->CellToWorld({3,5}),EEGFaction::Enemy);
   if(!Test->TestNotNull(TEXT("Injected formal boss"),Boss.Get()))return true;
   Boss->GetHeroSkill()->Configure(Mode->BattleRules,1,false);Mode->BattleRules->EnterUnit(Boss.Get());
   Rules=UEGCorruptionCombatComponent::AttachBoss(Boss.Get(),Mode->BattleRules,10000.f,20.f,50);
   if(!Test->TestNotNull(TEXT("Real boss rules component"),Rules.Get()))return true;
   Boss->GetCombat()->SetComponentTickEnabled(false);Boss->GetHeroSkill()->CancelAction();
   for(const auto& Enemy:OldEnemies)if(Enemy.IsValid()&&Enemy.Get()!=Boss.Get())Enemy->Destroy();OldEnemies.Reset();
   Main->InspectUnit(Boss.Get());Next(2);return false;
  }
  if(!Boss.IsValid()||!Hero.IsValid()||!Rules.IsValid())return Fail(TEXT("Controlled battle actors were unexpectedly destroyed"));
  auto* Widget=Info(Main);if(!Widget||!Widget->IsVisible()||!Ready())return false;
  if(Step==2)
  {
   const auto View=FEGUnitInspectionView::Capture(Boss.Get(),nullptr,FGuid());
   Test->TestEqual(TEXT("First-phase view"),View.BossArtworkPhase,1);
   Test->TestEqual(TEXT("All seven boss skills"),View.EnemySkills.Num(),7);
   for(int32 I=0;I<View.EnemySkills.Num();++I)
   {
    const FString Text=View.EnemySkills[I].Title.ToString()+View.EnemySkills[I].Description.ToString();
    Test->TestFalse(TEXT("Enemy skills do not display hero level upgrades"),Text.Contains(TEXT("6级"))||Text.Contains(TEXT("11级")));
    auto* Icon=Cast<UImage>(Widget->GetWidgetFromName(FName(*FString::Printf(TEXT("EnemyInspectionIcon_%d"),I))));
    if(Test->TestNotNull(TEXT("Enemy skill icon"),Icon))
    {
     Test->TestNotNull(TEXT("Enemy skill atlas reference"),Icon->GetBrush().GetResourceObject());
     const FBox2f UV=Icon->GetBrush().GetUVRegion();
     Test->TestTrue(TEXT("Enemy skill has a dedicated atlas cell"),UV.bIsValid);
    }
   }
   for(int32 I=0;I<3;++I)Test->TestNotNull(TEXT("Distinct phase group heading"),Widget->GetWidgetFromName(FName(*FString::Printf(TEXT("EnemySkillGroup_%d"),I))));
   if(!CheckCard(Widget,1))return true;
   Next(3);return false;
  }
  if(Step==3)
  {
   if(!Screenshot(World,TEXT("BossPhase1.png")))return false;
   SkillIndex=0;Next(4);return false;
  }
  if(Step==4)
  {
   // At 720p keep the last description open across the real phase change;
   // at 1080p close it first to exercise immediate positioning separately.
   if(SkillIndex>=7){if(Size.X>1280)Widget->CloseSkillDetails();Next(6);return false;}
   Widget->OpenEnemySkill(SkillIndex);Next(5);return false;
  }
  if(Step==5)
  {
   const auto View=FEGUnitInspectionView::Capture(Boss.Get(),nullptr,FGuid());
   auto* Body=Cast<UEGRichTextBlock>(Widget->GetWidgetFromName(TEXT("ActionDetailsBody")));
   if(!Test->TestNotNull(TEXT("Selected skill description"),Body)||!View.EnemySkills.IsValidIndex(SkillIndex))return true;
   Test->TestEqual(TEXT("Selected skill displays its own description"),Body->GetPlainText().ToString(),View.EnemySkills[SkillIndex].Description.ToString());
   ++SkillIndex;Next(4);return false;
  }
  if(Step==6)
  {
   if(!Test->TestTrue(TEXT("Exhaust first life through combat damage"),FEGCombatExecution::Damage(Hero.Get(),Boss.Get(),1000000.f)))return true;
   Test->TestEqual(TEXT("First life enters the barrier"),int32(Rules->GetBossPhase()),int32(EEGCorruptionBossPhase::Barrier));
   Rules->CompleteBarrierIntroForTests();
   if(!Test->TestTrue(TEXT("Break the actual barrier"),FEGCombatExecution::Damage(Hero.Get(),Boss.Get(),1000000.f)))return true;
   Next(7);return false;
  }
  if(Step==7)
  {
   if(Rules->GetBossPhase()!=EEGCorruptionBossPhase::Second)return false;
   const auto View=FEGUnitInspectionView::Capture(Boss.Get(),nullptr,FGuid());
   Test->TestEqual(TEXT("Live second-phase view"),View.BossArtworkPhase,2);
   if(!CheckCard(Widget,2))return true;
   if(Size.X<=1280)
   {
    Test->TestTrue(TEXT("Phase transition preserves an open skill description"),Widget->HasSkillDetailsOpen());
    auto* Body=Cast<UEGRichTextBlock>(Widget->GetWidgetFromName(TEXT("ActionDetailsBody")));
    if(Test->TestNotNull(TEXT("Open description after phase transition"),Body)&&View.EnemySkills.IsValidIndex(6))
     Test->TestEqual(TEXT("Selected skill remains unchanged across phase transition"),Body->GetPlainText().ToString(),View.EnemySkills[6].Description.ToString());
    Next(11);return false;
   }
   Next(8);return false;
  }
  if(Step==11)
  {
   if(!Screenshot(World,TEXT("BossPhase2Reading.png")))return false;
   Widget->CloseSkillDetails();Next(12);return false;
  }
  if(Step==12)
  {
   Test->TestFalse(TEXT("Description closes before deferred stage positioning"),Widget->HasSkillDetailsOpen());
   Next(8);return false;
  }
  if(Step==8)
  {
   if(!bPositionChecked)
   {
    auto* Scroll=Cast<UScrollBox>(Widget->GetWidgetFromName(TEXT("UnitInfoScroll")));
    auto* Group=Widget->GetWidgetFromName(TEXT("EnemySkillGroup_1"));
    if(Test->TestNotNull(TEXT("Skill scroll"),Scroll)&&Test->TestNotNull(TEXT("Second-phase skill group"),Group))
    {
     const FGeometry& ScrollGeometry=Scroll->GetCachedGeometry();
     const FGeometry& GroupGeometry=Group->GetCachedGeometry();
     const FVector2D ScrollTop=ScrollGeometry.LocalToAbsolute(FVector2D::ZeroVector);
     const FVector2D ScrollBottom=ScrollGeometry.LocalToAbsolute(ScrollGeometry.GetLocalSize());
     const FVector2D GroupTop=GroupGeometry.LocalToAbsolute(FVector2D::ZeroVector);
     const FVector2D GroupBottom=GroupGeometry.LocalToAbsolute(GroupGeometry.GetLocalSize());
     Test->TestTrue(TEXT("Current phase heading is visible after positioning"),
      GroupGeometry.GetLocalSize().Y>0.f&&GroupTop.Y>=ScrollTop.Y-2.f&&GroupBottom.Y<=ScrollBottom.Y+2.f);
    }
    bPositionChecked=true;
   }
   if(!Screenshot(World,TEXT("BossPhase2Skills.png")))return false;
   if(auto* Scroll=Cast<UScrollBox>(Widget->GetWidgetFromName(TEXT("UnitInfoScroll"))))Scroll->ScrollToStart();
   // Repeated status refreshes must not steal a player's deliberate scroll.
   Main->InspectUnit(Boss.Get());Main->InspectUnit(Boss.Get());
   Next(10);return false;
  }
  if(Step==10)
  {
   if(auto* Scroll=Cast<UScrollBox>(Widget->GetWidgetFromName(TEXT("UnitInfoScroll"))))
    Test->TestTrue(TEXT("Repeated updates preserve scrolling back to the card"),FMath::IsNearlyZero(Scroll->GetScrollOffset(),1.f));
   if(!Screenshot(World,TEXT("BossPhase2Card.png")))return false;
   Main->InspectUnit(Hero.Get());Next(9);return false;
  }
  if(Step==9)
  {
   auto* Basic=Widget->GetWidgetFromName(TEXT("Button_BasicInfo"));
   if(Test->TestNotNull(TEXT("Player basic skill button restored"),Basic))Test->TestTrue(TEXT("Player buttons restored after inspecting boss"),Basic->GetParent()&&Basic->GetParent()->GetVisibility()==ESlateVisibility::Visible);
   if(auto* Decoration=Widget->GetWidgetFromName(TEXT("InfoCard_CorruptionPhaseTitleFit")))
    Test->TestEqual(TEXT("Boss title overlay removed on returning to a player"),Decoration->GetVisibility(),ESlateVisibility::Collapsed);
   if(!Screenshot(World,TEXT("PlayerAfterBoss.png")))return false;
   Main->InspectUnit(nullptr);
   Test->AddInfo(TEXT("Controlled PIE: injected stage-7 boss, fixed health, scripted damage and barrier advancement; button selection is programmatic. Not natural play, balance, real input, performance or packaging evidence."));
   return true;
  }
  return true;
 }
};
bool QueueEnemyRework(FAutomationTestBase* Test,FIntPoint Size)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEnemyReworkStartPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FEnemyReworkRun(Test,Size));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEnemyRework1080,"ElyGambit.Integration.EnemyRework1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEnemyRework1080::RunTest(const FString&){return QueueEnemyRework(this,FIntPoint(1920,1080));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEnemyRework720,"ElyGambit.Integration.EnemyRework720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEnemyRework720::RunTest(const FString&){return QueueEnemyRework(this,FIntPoint(1280,720));}
#endif
