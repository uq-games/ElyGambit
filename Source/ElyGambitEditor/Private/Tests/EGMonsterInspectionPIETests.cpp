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
#include "GameFramework/CharacterMovementComponent.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGHeroRoster.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGRichText.h"
#include "UI/EGUnitInspectionView.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "UObject/UnrealType.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"

namespace
{
class FMonsterInspectionStartPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FMonsterInspectionStartPIE(FIntPoint InSize):Size(InSize){}
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

struct FMonsterInspectionCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws=0;
 bool bRequested=false;
 ~FMonsterInspectionCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

class FMonsterInspectionPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 FString Directory;
 double Deadline=0.,StepAt=0.;
 int32 Index=0,PathIndex=0,Step=0;
 bool bChecked=false;
 FGuid TemporaryId;
 TWeakObjectPtr<AEGDemoCombatUnit> Unit;
 TSharedPtr<FMonsterInspectionCapture> Capture;
 void Next(int32 Value){Step=Value;StepAt=FPlatformTime::Seconds();bChecked=false;}
 bool Settled()const{return FPlatformTime::Seconds()-StepAt>.4;}
 void Cleanup(UEGMainHUD* Main,UEGHeroRoster* Roster)
 {
  Capture.Reset();
  if(Main)Main->InspectUnit(nullptr);
  if(Unit.IsValid())Unit->Destroy();Unit.Reset();
  if(Roster&&TemporaryId.IsValid())Test->TestTrue(TEXT("Remove temporary roster inspection fixture"),Roster->RemoveUncommittedHero(TemporaryId));
  TemporaryId.Invalidate();
 }
 bool Fail(UEGMainHUD* Main,UEGHeroRoster* Roster,const FString& Reason)
 {Test->AddError(Reason);Cleanup(Main,Roster);return true;}
 bool Screenshot(UWorld* World,const FString& Name)
 {
  if(!Capture.IsValid())
  {
   auto* Client=World?World->GetGameViewport():nullptr;
   if(!Client||!Client->Viewport)return false;
   Capture=MakeShared<FMonsterInspectionCapture>();Capture->Client=Client;Capture->Path=Directory/Name;
   const TWeakPtr<FMonsterInspectionCapture> Weak=Capture;
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
  Test->TestEqual(TEXT("Captured PIE viewport size"),FIntPoint(BE(16),BE(20)),Size);
  Test->AddInfo(FString::Printf(TEXT("Controlled PIE screenshot: %s"),*Capture->Path));Capture.Reset();return true;
 }
 bool Click(UButton* Button)
 {
  if(!Button||!Button->IsVisible()||!Button->GetIsEnabled())return false;
  const FGeometry& G=Button->GetCachedGeometry();if(G.GetLocalSize().IsNearlyZero())return false;
  auto& App=FSlateApplication::Get();const TSharedPtr<SWindow> Window=App.FindWidgetWindow(Button->TakeWidget());if(!Window.IsValid())return false;
  const FVector2D Previous=App.GetCursorPos(),Point=G.LocalToAbsolute(G.GetLocalSize()*.5f);
  App.SetCursorPos(Point);
  App.ProcessMouseMoveEvent(FPointerEvent(0,Point,Previous,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
  const bool bHandled=App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),FPointerEvent(0,Point,Point,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
  App.ProcessMouseButtonUpEvent(FPointerEvent(0,Point,Point,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
  App.SetCursorPos(Previous);App.ProcessMouseMoveEvent(FPointerEvent(0,Previous,Point,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
  Test->TestTrue(TEXT("Slate pointer reaches skill button"),bHandled);return true;
 }
 void CheckMonsterText(const UEGMonsterAttackDefinition* P,bool bUltimate,const FString& Text,const FString& Label)
 {
  auto Has=[&](const FString& Part){Test->TestTrue(*(Label+TEXT(" contains ")+Part),Text.Contains(Part));};
  if(!bUltimate)
  {
   if(P->AttackType==EEGMonsterAttackType::Heal){Has(TEXT("治疗"));Has(FString::Printf(TEXT("%.0f%%"),P->HealHealthRatio*100.f));}
   else if(P->AttackType==EEGMonsterAttackType::Melee){Has(TEXT("近战"));Has(TEXT("攻击力"));}
   else {Has(TEXT("投射物"));Has(TEXT("攻击力"));}
   return;
  }
  switch(P->Ultimate)
  {
   case EEGMonsterUltimate::Shield:Has(TEXT("护盾"));Has(FString::Printf(TEXT("%.0f%%"),P->ShieldHealthRatio*100.f));Has(FString::Printf(TEXT("%.1f秒"),P->ShieldSeconds));break;
   case EEGMonsterUltimate::EnhancedShot:Has(TEXT("强化"));Has(FString::Printf(TEXT("%.0f%%"),P->UltimateMultiplier*100.f));break;
   case EEGMonsterUltimate::TeamSpeed:Has(TEXT("攻速"));Has(FString::Printf(TEXT("%.0f%%"),P->TeamSpeedBonus*100.f));Has(FString::Printf(TEXT("%.1f秒"),P->TeamSpeedSeconds));break;
   case EEGMonsterUltimate::CrystalSeed:Has(TEXT("水晶种"));Has(FString::Printf(TEXT("%.1f秒"),P->SeedDelay));Has(FString::Printf(TEXT("%.1f格"),P->SeedRadiusCells));Has(FString::Printf(TEXT("%.0f%%"),P->UltimateMultiplier*100.f));break;
   default:Test->AddError(Label+TEXT(" unexpected missing ultimate"));break;
  }
 }
public:
 FMonsterInspectionPIE(FAutomationTestBase* InTest,FIntPoint InSize):Test(InTest),Size(InSize){}
 bool Update() override
 {
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* PC=World?World->GetFirstPlayerController():nullptr;
  auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
  auto* Main=HUD?HUD->GetMainWidget():nullptr;
  auto* Roster=Mode?Mode->GetRoster():nullptr;
  if(Deadline==0.){Deadline=FPlatformTime::Seconds()+360.;Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ReleaseAudit20261110/MonsterInspection")/FGuid::NewGuid().ToString(EGuidFormats::Digits));IFileManager::Get().MakeDirectory(*Directory,true);}
  if(FPlatformTime::Seconds()>Deadline)return Fail(Main,Roster,FString::Printf(TEXT("Monster inspection timed out index=%d path=%d step=%d"),Index,PathIndex,Step));
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Mode||!PC||!Main||!Roster||!Client||!Client->Viewport||Client->Viewport->GetSizeXY()!=Size)return false;
  if(!Mode->RunDefinition||!Mode->RunDefinition->Stages.IsValidIndex(6)||!Mode->GetBoard())return false;
  const auto& Monsters=Mode->RunDefinition->Stages[3].OrdinaryMonsterDefinitions;
  if(Monsters.Num()!=4)return Fail(Main,Roster,TEXT("Expected four stage-4 production monster definitions"));
  UEGHeroDefinition* Definition=nullptr;
  if(Index<4)Definition=Monsters[Index].Get();
  else for(const auto& Entry:Mode->RunDefinition->Stages[6].Enemies)
   if(Entry.Definition&&!Entry.bCorruptedCopy&&Entry.Definition->HeroId==FName(TEXT("ElysiaCorruption"))){Definition=Entry.Definition.Get();break;}
  if(!Definition)return Fail(Main,Roster,TEXT("Formal monster or stage-7 Boss definition missing"));
  const FString Label=Definition->HeroId.ToString()+(PathIndex==0?TEXT("_Roster"):TEXT("_Unit"));
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   Mode->SetActorTickEnabled(false);
   if(Index<4&&!Definition->MonsterAttack)return Fail(Main,Roster,TEXT("Formal monster attack profile missing: ")+Label);
   if(PathIndex==0)
   {
    TemporaryId=Roster->AddHero(Definition);
    if(!TemporaryId.IsValid())return Fail(Main,Roster,TEXT("Cannot create temporary roster inspection fixture: ")+Label);
    Main->InspectHero(Roster,TemporaryId);
   }
   else
   {
    FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Unit=World->SpawnActor<AEGDemoCombatUnit>(Mode->GetBoard()->CellToWorld({3,3}),FRotator::ZeroRotator,Params);
    if(!Unit.IsValid()||!Unit->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1))return Fail(Main,Roster,TEXT("Cannot initialize temporary inspection actor: ")+Label);
    Unit->GetCharacterMovement()->DisableMovement();Unit->SetActorTickEnabled(false);Unit->SetActorHiddenInGame(true);
    Main->InspectUnit(Unit.Get());
   }
   Next(1);return false;
  }
  auto* Property=FindFProperty<FObjectPropertyBase>(UEGMainHUD::StaticClass(),TEXT("UnitPinnedInformation"));
  auto* Info=Property?Cast<UEGHeroInfoWidget>(Property->GetObjectPropertyValue_InContainer(Main)):nullptr;
  if(!Info||!Info->IsVisible()||!Settled())return false;
  if(Step==1)
  {
   if(!bChecked)
   {
    const TCHAR* ArtPath=TEXT("/Game/Assets/Cards/Enemies/T_Card_Corruption_Phase1.T_Card_Corruption_Phase1");
    if(Index<4)switch(Definition->MonsterAttack->Ultimate)
    {
     case EEGMonsterUltimate::Shield:ArtPath=TEXT("/Game/Assets/Cards/Enemies/T_Card_Chest.T_Card_Chest");break;
     case EEGMonsterUltimate::EnhancedShot:ArtPath=TEXT("/Game/Assets/Cards/Enemies/T_Card_Eye.T_Card_Eye");break;
     case EEGMonsterUltimate::TeamSpeed:ArtPath=TEXT("/Game/Assets/Cards/Enemies/T_Card_Spore.T_Card_Spore");break;
     case EEGMonsterUltimate::CrystalSeed:ArtPath=TEXT("/Game/Assets/Cards/Enemies/T_Card_Flower.T_Card_Flower");break;
     default:return Fail(Main,Roster,TEXT("Unexpected campaign monster role: ")+Label);
    }
    auto* Expected=LoadObject<UTexture2D>(nullptr,ArtPath);auto* Card=Cast<UImage>(Info->GetWidgetFromName(TEXT("InfoCard")));
    if(Test->TestNotNull(*(Label+TEXT(" expected formal artwork")),Expected)&&Test->TestNotNull(*(Label+TEXT(" card widget")),Card))
    {Test->TestTrue(*(Label+TEXT(" card is visible")),Card->IsVisible());Test->TestEqual(*(Label+TEXT(" displays the expected artwork")),Card->GetBrush().GetResourceObject(),static_cast<UObject*>(Expected));}
    auto* Name=Cast<UTextBlock>(Info->GetWidgetFromName(TEXT("CharacterName")));
    if(Test->TestNotNull(*(Label+TEXT(" name widget")),Name))Test->TestEqual(*(Label+TEXT(" shows formal name")),Name->GetText().ToString(),Definition->DisplayName.ToString());
    bChecked=true;
   }
   if(!Screenshot(World,Label+TEXT("_overview.png")))return false;
   Next(2);return false;
  }
  if(Step==2||Step==4)
  {
   UButton* Button=nullptr;
   if(PathIndex==1)
   {
    // Enemy actions use a variable-length row; find its real button through
    // the displayed icon rather than assuming the former fixed hero slots.
    UWidget* Current=Info->GetWidgetFromName(Step==2?TEXT("EnemyInspectionIcon_0"):TEXT("EnemyInspectionIcon_1"));
    while(Current&&!Button){Button=Cast<UButton>(Current);Current=Current->GetParent();}
   }
   else Button=Cast<UButton>(Info->GetWidgetFromName(Step==2?TEXT("Button_BasicInfo"):TEXT("Button_UltimateInfo")));
   if(!Test->TestNotNull(*(Label+TEXT(" skill button")),Button))return Fail(Main,Roster,TEXT("Missing skill button"));
   if(!Click(Button))return false;
   Next(Step+1);return false;
  }
  if(Step==3||Step==5)
  {
   const bool bUltimate=Step==5;
   if(!bChecked)
   {
    if(!Test->TestTrue(*(Label+TEXT(" details opened")),Info->HasSkillDetailsOpen()))return Fail(Main,Roster,TEXT("Skill details did not open"));
    auto* Title=Cast<UTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsTitle")));
    auto* Body=Cast<UEGRichTextBlock>(Info->GetWidgetFromName(TEXT("ActionDetailsBody")));
    const auto Kind=bUltimate?EEGSkillInfoKind::Ultimate:EEGSkillInfoKind::Basic;
    FText ExpectedTitle=Definition->GetPlayerFacingSkillSectionName(Kind),ExpectedDescription=Definition->GetPlayerFacingSkillSection(Kind);
    if(PathIndex==1)
    {
     const auto View=FEGUnitInspectionView::Capture(Unit.Get(),nullptr,FGuid());const int32 SkillIndex=bUltimate?1:0;
     if(!Test->TestTrue(*(Label+TEXT(" has enemy skill")),View.EnemySkills.IsValidIndex(SkillIndex)))return Fail(Main,Roster,TEXT("Missing enemy inspection skill"));
     ExpectedTitle=View.EnemySkills[SkillIndex].Title;ExpectedDescription=View.EnemySkills[SkillIndex].Description;
    }
    if(Test->TestNotNull(*(Label+TEXT(" skill title")),Title))Test->TestEqual(*(Label+TEXT(" expected skill title")),Title->GetText().ToString(),ExpectedTitle.ToString());
    if(Test->TestNotNull(*(Label+TEXT(" skill text")),Body))
    {
     const FString Actual=Body->GetPlainText().ToString();
     Test->TestFalse(*(Label+TEXT(" skill text is not empty")),Actual.IsEmpty());
     Test->TestTrue(*(Label+TEXT(" uses selected skill text")),Actual.StartsWith(ExpectedDescription.ToString()));
     if(PathIndex==1)Test->TestFalse(*(Label+TEXT(" enemy skill excludes hero growth")),Actual.Contains(TEXT("6级"))||Actual.Contains(TEXT("11级")));
     if(Index<4)CheckMonsterText(Definition->MonsterAttack.Get(),bUltimate,Actual,Label);
    }
    bChecked=true;
   }
   if(!Screenshot(World,Label+(bUltimate?TEXT("_ultimate.png"):TEXT("_basic.png"))))return false;
   if(!bUltimate){Next(4);return false;}
   Cleanup(Main,Roster);
   if(++PathIndex==2){PathIndex=0;++Index;}
   if(Index==5)
   {
    Test->AddInfo(TEXT("Controlled stage-4 runtime monster definitions and stage-7 Boss definition, via temporary roster and spawned enemy inspection. The Boss has no active boss component in this fixture: phase-specific battle override is not covered. Skill input uses synthetic Slate events; screenshots need human review."));
    return true;
   }
   Next(0);return false;
  }
  return true;
 }
};

bool QueueMonsterInspection(FAutomationTestBase* Test,FIntPoint Size)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Monster inspection requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Monster inspection requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FMonsterInspectionStartPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FMonsterInspectionPIE(Test,Size));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterInspection1080,"ElyGambit.Integration.MonsterInspection1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterInspection1080::RunTest(const FString&){return QueueMonsterInspection(this,FIntPoint(1920,1080));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMonsterInspection720,"ElyGambit.Integration.MonsterInspection720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMonsterInspection720::RunTest(const FString&){return QueueMonsterInspection(this,FIntPoint(1280,720));}
#endif
