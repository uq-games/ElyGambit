#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "AudioDeviceManager.h"
#include "AudioMixerBlueprintLibrary.h"
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
#include "AbilitySystemComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Game/EGCardManager.h"
#include "Game/EGDemoGameMode.h"
#include "Interaction/EGCombatant.h"
#include "Presentation/EGMonsterSkillVisual.h"
#include "Presentation/EGCorruptionBossVisual.h"
#include "Presentation/EGCorruptionPhantomVisual.h"
#include "Skills/EGCorruptionBossBehavior.h"

namespace
{
class FEnemyEffectsStartPIE final : public IAutomationLatentCommand
{
public:
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1280;Settings->NewWindowHeight=720;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1280,720))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(!GUnrealEd->CheckForPlayerStart())FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

template<typename T> int32 CountEffects(UWorld* World)
{
 int32 Count=0;for(TActorIterator<T> It(World);It;++It)if(IsValid(*It))++Count;return Count;
}

/** Formal board and definitions; scripted actions, injected actors and frozen AI. */
class FEnemyEffectsCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Hero,Chest,Spore,Boss,Candidate;
 TWeakObjectPtr<UEGCorruptionCombatComponent> Rules;
 TWeakObjectPtr<UEGCorruptionBossBehavior> PulseBehavior;
 FDelegateHandle PulseHandle;
 TWeakObjectPtr<UWorld> RecordingWorld;
 FString Directory,PendingPath,AudioPath;
 TArray<FString> SeriesPaths;
 double Deadline=0.,NextAt=0.,ExportDeadline=0.,CueAt=0.;
 int32 Step=0,MimicIndex=0,SampleIndex=0;
 bool bRecording=false,bSeenMimic=false,bPulseResolved=false;
 void StopAudio()
 {
  if(bRecording&&RecordingWorld.IsValid())
   UAudioMixerBlueprintLibrary::StopRecordingOutput(RecordingWorld.Get(),EAudioRecordingExportType::WavFile,TEXT("EnemyEffectsMaster"),Directory);
  bRecording=false;RecordingWorld.Reset();
 }
 bool Fail(const FString& Message){Test->AddError(Message);StopAudio();return true;}
 void Next(int32 InStep,double Delay=0.){Step=InStep;NextAt=FPlatformTime::Seconds()+Delay;}
 bool Capture(const TCHAR* Name)
 {
  if(PendingPath.IsEmpty())
  {
   if(FScreenshotRequest::IsScreenshotRequested())return false;
   PendingPath=Directory/Name;FScreenshotRequest::RequestScreenshot(PendingPath,false,false,false,FIntRect(),true);return false;
  }
  if(FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Data;if(!FFileHelper::LoadFileToArray(Data,*PendingPath)||Data.Num()<36)return false;
  const int32 End=Data.Num()-12;
  if(Data[End+4]!='I'||Data[End+5]!='E'||Data[End+6]!='N'||Data[End+7]!='D')return false;
  Test->AddInfo(TEXT("Controlled PIE screenshot: ")+PendingPath);PendingPath.Empty();return true;
 }
 bool CaptureSeries(const TCHAR* Name)
 {
  // Two explicit presentation ages, independent of action completion.
  const double SampleTimes[]={.12,.28};
  if(SampleIndex<2)
  {
   if(FPlatformTime::Seconds()<CueAt+SampleTimes[SampleIndex]||FScreenshotRequest::IsScreenshotRequested())return false;
   const double Age=FPlatformTime::Seconds()-CueAt;
   Test->AddInfo(FString::Printf(TEXT("%s screenshot requested %.3f s after cue"),Name,Age));
   Test->TestTrue(TEXT("Screenshot requested while short effect is active"),Age<.65);
   UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
   if(Step==3||Step==5||Step==7)Test->TestTrue(TEXT("Target cue actor still active for screenshot"),World&&CountEffects<AEGMonsterEffectCueVisual>(World)>0);
   if(Step==9||Step==11)Test->TestTrue(TEXT("Pulse effect actor still active for screenshot"),World&&CountEffects<AEGCorruptionBossVisual>(World)>1);
   if(Step==15)Test->TestTrue(TEXT("Mimic actor still active for screenshot"),World&&CountEffects<AEGCorruptionPhantomVisual>(World)>0);
   const FString File=FString::Printf(TEXT("%s_%s.png"),Name,SampleIndex==0?TEXT("012s"):TEXT("028s"));
   SeriesPaths.Add(Directory/File);
   FScreenshotRequest::RequestScreenshot(SeriesPaths.Last(),false,false,false,FIntRect(),true);
   ++SampleIndex;return false;
  }
  if(FScreenshotRequest::IsScreenshotRequested())return false;
  for(const FString& Path:SeriesPaths)
  {
   TArray<uint8> Data;if(!FFileHelper::LoadFileToArray(Data,*Path)||Data.Num()<36)return false;
   const int32 End=Data.Num()-12;
   if(Data[End+4]!='I'||Data[End+5]!='E'||Data[End+6]!='N'||Data[End+7]!='D')return false;
  }
  for(const FString& Path:SeriesPaths)Test->AddInfo(TEXT("Controlled PIE screenshot: ")+Path);
  SeriesPaths.Reset();SampleIndex=0;return true;
 }
 AEGChessUnit* Spawn(AEGDemoGameMode* Mode,UEGHeroDefinition* Definition,FIntPoint Cell,EEGFaction Faction)
 {
  auto* Unit=Mode->SpawnBattleSummon(Definition,1,Mode->GetBoard()->CellToWorld(Cell),Faction);
  if(!Unit)return nullptr;
  Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->Configure(Mode->BattleRules,1,false);
  Mode->BattleRules->EnterUnit(Unit);return Unit;
 }
 bool Begin(AEGChessUnit* Unit,bool Ultimate,FName Event)
 {
  if(!IsValid(Unit))return false;
  auto* Skill=Unit->GetHeroSkill();Skill->CancelAction();Unit->GetCombat()->ResetCombat();
  if(Ultimate)Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Unit->GetAttributes()->GetMaxMana());
  if(!Skill->BeginAction(Ultimate))return false;
  Skill->AnimationEvent(Event);return true;
 }
public:
 explicit FEnemyEffectsCapture(FAutomationTestBase* InTest):Test(InTest){}
 ~FEnemyEffectsCapture(){StopAudio();if(PulseBehavior.IsValid())PulseBehavior->OnFirstPulse.Remove(PulseHandle);}
 bool Update() override
 {
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  auto* Client=World?World->GetGameViewport():nullptr;
  if(Deadline==0.)
  {
   Deadline=FPlatformTime::Seconds()+240.;
   Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("EnemyRework20261110/EffectsGraphical")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
   IFileManager::Get().MakeDirectory(*Directory,true);AudioPath=Directory/TEXT("EnemyEffectsMaster.wav");
  }
  if(FPlatformTime::Seconds()>Deadline)return Fail(FString::Printf(TEXT("Enemy effects timed out at step %d, mimic %d"),Step,MimicIndex));
  if(!World||!Mode||!Mode->BattleRules||!Mode->GetBoard()||!Mode->RunDefinition||!Client||!Client->Viewport||Client->Viewport->GetSizeXY()!=FIntPoint(1280,720))return false;
  if(FPlatformTime::Seconds()<NextAt)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   const auto Hand=Mode->GetCards()->GetHand();if(Hand.IsEmpty())return false;
   if(!Test->TestTrue(TEXT("Deploy opening hero"),Mode->GetCards()->TryDeployHandCard(Hand[0].InstanceId,{3,1}))||
      !Test->TestTrue(TEXT("Enter combat"),Mode->EnterCombat()))return true;
   Next(1);return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);Unit->GetHeroSkill()->CancelAction();
    if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player)Hero=Unit;
    else Unit->Destroy();
   }
   if(!Hero.IsValid()||!Mode->RunDefinition->Stages.IsValidIndex(3))return Fail(TEXT("Missing opening hero or production monster definitions"));
   UEGHeroDefinition *ChestDef=nullptr,*SporeDef=nullptr;
   for(const auto& Entry:Mode->RunDefinition->Stages[3].OrdinaryMonsterDefinitions)
    if(auto* D=Entry.Get();D&&D->MonsterAttack)
    {
     if(D->MonsterAttack->Ultimate==EEGMonsterUltimate::Shield)ChestDef=D;
     if(D->MonsterAttack->Ultimate==EEGMonsterUltimate::TeamSpeed)SporeDef=D;
    }
   if(!ChestDef||!SporeDef)return Fail(TEXT("Production chest/spore definitions missing"));
   Chest=Spawn(Mode,ChestDef,{3,4},EEGFaction::Enemy);Spore=Spawn(Mode,SporeDef,{4,4},EEGFaction::Enemy);
   if(!Chest.IsValid()||!Spore.IsValid())return Fail(TEXT("Could not spawn production monster fixtures"));
   if(!FAudioDeviceManager::GetAudioMixerDeviceFromWorldContext(World))return Fail(TEXT("PIE has no audio mixer device"));
   UAudioMixerBlueprintLibrary::StartRecordingOutput(World,180.f);RecordingWorld=World;bRecording=true;
   Next(101,.3);return false;
  }
  if(!Hero.IsValid()||!Mode->BattleRules->IsBattleActive())return Fail(TEXT("Battle or hero lost during controlled effects test"));
  if(!Mode->BattleRules->IsCombatRunning())return false;
  if(Step==101){if(!Capture(TEXT("CaptureWarmup.png")))return false;Next(2,.1);return false;}
  if(Step==2)
  {
   if(!Test->TestTrue(TEXT("Chest starts shield through skill action"),Begin(Chest.Get(),true,TEXT("Release"))))return true;
   if(!Test->TestTrue(TEXT("Shield produces target visual"),CountEffects<AEGMonsterEffectCueVisual>(World)>0))return true;
   CueAt=FPlatformTime::Seconds();
   Next(3);return false;
  }
  if(Step==3){if(!CaptureSeries(TEXT("ChestShield")))return false;Chest->GetHeroSkill()->AnimationEvent(TEXT("Finish"));Next(4,1.);return false;}
  if(Step==4)
  {
   if(!Test->TestEqual(TEXT("Shield cue expires"),CountEffects<AEGMonsterEffectCueVisual>(World),0))return true;
   Chest->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),Chest->GetAttributes()->GetMaxHealth()*.5f);
   if(!Test->TestTrue(TEXT("Spore starts heal through skill action"),Begin(Spore.Get(),false,TEXT("Hit"))))return true;
   if(!Test->TestTrue(TEXT("Effective heal produces target visual"),CountEffects<AEGMonsterEffectCueVisual>(World)>0))return true;
   CueAt=FPlatformTime::Seconds();
   Next(5);return false;
  }
  if(Step==5){if(!CaptureSeries(TEXT("SporeHeal")))return false;Spore->GetHeroSkill()->AnimationEvent(TEXT("Finish"));Next(6,1.);return false;}
  if(Step==6)
  {
   if(!Test->TestEqual(TEXT("Heal cue expires"),CountEffects<AEGMonsterEffectCueVisual>(World),0))return true;
   if(!Test->TestTrue(TEXT("Spore starts team speed through skill action"),Begin(Spore.Get(),true,TEXT("Release"))))return true;
   if(!Test->TestTrue(TEXT("Applied team speed produces target visual"),CountEffects<AEGMonsterEffectCueVisual>(World)>0))return true;
   CueAt=FPlatformTime::Seconds();
   Next(7);return false;
  }
  if(Step==7){if(!CaptureSeries(TEXT("SporeTeamSpeed")))return false;Spore->GetHeroSkill()->AnimationEvent(TEXT("Finish"));Next(8,1.);return false;}
  if(Step==8)
  {
   if(!Test->TestEqual(TEXT("Team speed cues expire"),CountEffects<AEGMonsterEffectCueVisual>(World),0))return true;
   Chest->Destroy();Spore->Destroy();
   auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption.DA_Corruption"));
   Boss=Spawn(Mode,Definition,{3,5},EEGFaction::Enemy);
   if(!Boss.IsValid())return Fail(TEXT("Cannot spawn production boss"));
   Rules=UEGCorruptionCombatComponent::AttachBoss(Boss.Get(),Mode->BattleRules,10000.f,20.f,50);
   if(!Rules.IsValid())return Fail(TEXT("Cannot attach boss rules"));
   auto* BossSkill=Boss->GetHeroSkill();
   if(!Test->TestTrue(TEXT("Valid source shield returns success"),BossSkill->GrantSourceShield(TEXT("CorruptionMimic"),25.f,4.f)))return true;
   const float PriorShield=BossSkill->GetSourceShield(TEXT("CorruptionMimic"));
   if(!Test->TestTrue(TEXT("Successful source shield is positive"),PriorShield>0.f))return true;
   if(!Test->TestFalse(TEXT("Invalid source shield reports failure even when previous shield exists"),BossSkill->GrantSourceShield(TEXT("CorruptionMimic"),0.f,4.f)))return true;
   if(!Test->TestEqual(TEXT("Invalid shield leaves previous source unchanged"),BossSkill->GetSourceShield(TEXT("CorruptionMimic")),PriorShield))return true;
   BossSkill->RemoveSourceShield(TEXT("CorruptionMimic"));
   PulseBehavior=Cast<UEGCorruptionBossBehavior>(Boss->GetHeroSkill()->GetBehavior());
   if(!PulseBehavior.IsValid())return Fail(TEXT("Missing boss pulse behavior"));
   PulseHandle=PulseBehavior->OnFirstPulse.AddLambda([this](EEGCorruptionFirstPulseEvent Event,FGuid,FGuid,FVector,float,float)
    {if(Event==EEGCorruptionFirstPulseEvent::Resolved){bPulseResolved=true;CueAt=FPlatformTime::Seconds();}});
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   if(!Test->TestTrue(TEXT("First pulse starts through boss skill action"),Begin(Boss.Get(),true,TEXT("Release"))))return true;
   if(!Test->TestTrue(TEXT("Pulse warning visual exists"),CountEffects<AEGCorruptionBossVisual>(World)>1))return true;
   CueAt=FPlatformTime::Seconds();
   Next(9);return false;
  }
  if(Step==9){if(!CaptureSeries(TEXT("BossPulseWarning")))return false;Boss->GetHeroSkill()->AnimationEvent(TEXT("Finish"));Next(10);return false;}
  if(Step==10)
  {
   if(!bPulseResolved)return false;
   if(!Test->TestTrue(TEXT("Pulse resolution produces burst visual"),CountEffects<AEGCorruptionBossVisual>(World)>1))return true;
   Next(11);return false;
  }
  if(Step==11){if(!CaptureSeries(TEXT("BossPulseBurst")))return false;Next(12,1.);return false;}
  if(Step==12)
  {
   if(!Test->TestEqual(TEXT("Pulse visuals expire"),CountEffects<AEGCorruptionBossVisual>(World),1))return true;
   Rules->FirstSpreadDelaySeconds=.1f;Rules->SpreadIntervalSeconds=1000.f;Rules->MimicIntervalSeconds=.1f;
   if(!Test->TestTrue(TEXT("Exhaust first boss life"),FEGCombatExecution::Damage(Hero.Get(),Boss.Get(),1000000.f)))return true;
   Rules->CompleteBarrierIntroForTests();
   if(!Test->TestTrue(TEXT("Break actual boss barrier"),FEGCombatExecution::Damage(Hero.Get(),Boss.Get(),1000000.f)))return true;
   // Freeze only the boss clock between candidates. Otherwise the deliberately
   // accelerated interval can start a Generic cast while no candidate exists.
   Rules->SetComponentTickEnabled(false);
   Hero->GetHeroSkill()->bSummon=true;Next(13,.2);return false;
  }
  if(Step==13)
  {
   if(Rules->GetBossPhase()!=EEGCorruptionBossPhase::Second)return false;
   if(MimicIndex>=3){StopAudio();ExportDeadline=FPlatformTime::Seconds()+30.;Next(16);return false;}
   const EEGHeroKind Kinds[]={EEGHeroKind::Griseo,EEGHeroKind::Aponia,EEGHeroKind::Sakura};
   UEGHeroDefinition* Selected=nullptr;
   for(const auto& Card:Mode->RunDefinition->CardPool)if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==Kinds[MimicIndex]){Selected=Card->HeroDefinition;break;}
   if(!Selected)return Fail(TEXT("Required production mimic hero is missing from card pool"));
   Candidate=Spawn(Mode,Selected,{4,1},EEGFaction::Player);if(!Candidate.IsValid())return Fail(TEXT("Cannot spawn mimic candidate"));
   Candidate->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
   Candidate->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
   bSeenMimic=false;Rules->SetComponentTickEnabled(true);Next(14);return false;
  }
  if(Step==14)
  {
   if(Rules->GetBossCastKind()==EEGCorruptionBossCast::Mimic)
   {
    bSeenMimic=true;
    if(!Test->TestTrue(TEXT("Real mimic spawned phantom"),CountEffects<AEGCorruptionPhantomVisual>(World)>0))return true;
   }
   if(!bSeenMimic||Rules->GetBossCastKind()==EEGCorruptionBossCast::Mimic)return false;
   if(!Test->TestTrue(TEXT("Resolved mimic retains short phantom visual"),CountEffects<AEGCorruptionPhantomVisual>(World)>0))return true;
   if(MimicIndex==0&&(!Test->TestTrue(TEXT("Mimic applied its source shield"),Boss->GetHeroSkill()->GetSourceShield(TEXT("CorruptionMimic"))>0.f)||
     !Test->TestTrue(TEXT("Successful mimic shield has a target marker"),CountEffects<AEGMonsterEffectCueVisual>(World)>0)))return true;
   Rules->SetComponentTickEnabled(false);
   CueAt=FPlatformTime::Seconds();
   Next(15);return false;
  }
  if(Step==15)
  {
   const TCHAR* Names[]={TEXT("MimicSelfShield"),TEXT("MimicAllTargets"),TEXT("MimicArea")};
   if(!CaptureSeries(Names[MimicIndex]))return false;
   Candidate->Destroy();Candidate.Reset();++MimicIndex;Next(13,1.2);return false;
  }
  if(Step==16)
  {
   if(!Test->TestEqual(TEXT("Mimic phantoms clean up"),CountEffects<AEGCorruptionPhantomVisual>(World),0))return true;
   const int64 Size=IFileManager::Get().FileSize(*AudioPath);
   if(Size>=44)
   {
    TArray<uint8> Bytes;if(FFileHelper::LoadFileToArray(Bytes,*AudioPath)&&Bytes.Num()>=44&&
     Bytes[0]=='R'&&Bytes[1]=='I'&&Bytes[2]=='F'&&Bytes[3]=='F'&&Bytes[8]=='W'&&Bytes[9]=='A'&&Bytes[10]=='V'&&Bytes[11]=='E')
    {
     const uint32 RiffSize=uint32(Bytes[4])|(uint32(Bytes[5])<<8)|(uint32(Bytes[6])<<16)|(uint32(Bytes[7])<<24);
     if(RiffSize>=36&&int64(RiffSize)+8<=Size)
     {Test->AddInfo(TEXT("PIE master mix for listening: ")+AudioPath);Test->AddInfo(TEXT("Frozen AI and boss clock between candidate changes, injected actors, explicit animation events, accelerated boss intervals and scripted damage; not natural play, audio acceptance, performance or packaged validation."));return true;}
    }
   }
   if(FPlatformTime::Seconds()>ExportDeadline)return Fail(TEXT("Master mix WAV export did not complete"));
   return false;
  }
  return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEnemyEffectsGraphical,"ElyGambit.Monsters.Release.ControlledEffectsGraphical",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEnemyEffectsGraphical::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEnemyEffectsStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FEnemyEffectsCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
