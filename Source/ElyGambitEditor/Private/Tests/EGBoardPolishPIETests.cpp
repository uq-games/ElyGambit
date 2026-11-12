#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "HAL/FileManager.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "Presentation/EGTeleportVisual.h"
#include "Presentation/EGKillBeamVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Actor/EGArtPiece.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Presentation/EGBoardMasterActionComponent.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Data/EGCardDefinition.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Presentation/EGOpeningPresentation.h"
#include "Presentation/EGSettlementPresentation.h"

namespace
{
class FPolishStartPIE final : public IAutomationLatentCommand
{
public:
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  auto Window=SNew(SWindow).ScreenPosition(FVector2D::ZeroVector).ClientSize(FVector2D(1920,1080))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

class FPolishCapture final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 int32 Step=0;
 double Deadline=0;
 float Start=0;
 bool Requested=false;
 float FlightSeen=-1.f;
 bool HeardLaunch=false,HeardImpact=false;
 bool CastCaptured=false,HitCaptured=false;
 float DeathLength=0;
 FString Path;
 TWeakObjectPtr<AEGChessUnit> Source,Victim;
 TWeakObjectPtr<AEGSettlementPresentation> Settlement;
 TWeakObjectPtr<AEGArtPiece> PersistentMasters[2];
 TWeakObjectPtr<USkeletalMeshComponent> PersistentBodies[2];
 TWeakObjectPtr<UAnimInstance> PersistentInstances[2];
 TSharedRef<int32> Impacts=MakeShared<int32>(0);
 bool Capture(UWorld* W,const TCHAR* Name,float Delay)
 {
  if(W->GetTimeSeconds()-Start<Delay)return false;
  auto* View=W->GetGameViewport()?W->GetGameViewport()->Viewport:nullptr;
  if(!View)return false;
  TArray<FColor> Pixels;
  const FIntPoint Size=View->GetSizeXY();
  if(!View->ReadPixels(Pixels)||Pixels.Num()!=Size.X*Size.Y)return false;
  TArray64<uint8> Png;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,Pixels,Png);
  Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("BoardPolish20261109/BurstArrival/Graphics")/(FString(Name)+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
  if(!FFileHelper::SaveArrayToFile(Png,*Path)){Test->AddError(TEXT("PIE viewport capture could not be saved"));return false;}
  Test->AddInfo(Path);return true;
 }
public:
 explicit FPolishCapture(FAutomationTestBase* In):Test(In){}
 bool Update() override
 {
  if(!Deadline)Deadline=FPlatformTime::Seconds()+180;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Board polish graphical check timeout"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetBoard())return false;
  if(Settlement.IsValid())
  {
   TArray<UAudioComponent*> Audio;Settlement->GetComponents(Audio);
   for(auto* A:Audio)if(A->IsPlaying())
   {if(A->GetName()==TEXT("LaunchAudio"))HeardLaunch=true;if(A->GetName()==TEXT("ImpactAudio"))HeardImpact=true;}
  }
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"));
   auto* Cards=Mode->GetCards();
   if(!Test->TestTrue(TEXT("Grant and deploy"),Card&&Cards->GrantCard(Card)&&Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{3,1})))return true;
   if(!Test->TestTrue(TEXT("Enter real combat"),Mode->EnterCombat()))return true;
   Mode->BattleRules->CompleteOpeningForTests();
   for(auto* U:Mode->GetCombatUnits())if(U)
   {
    U->GetCombat()->SetComponentTickEnabled(false);
    if(U->GetFaction_Implementation()==EEGFaction::Player)Source=U;
    else Victim=U;
   }
   if(!Source.IsValid()||!Victim.IsValid()){Test->AddError(TEXT("Missing combat units"));return true;}
   // Render a warm-up instance before sampling the short portal burst, so first-use
   // Niagara/material compilation cannot consume the entire screenshot window.
   auto* Warmup=W->SpawnActor<AEGTeleportVisual>(Victim->GetActorLocation(),FRotator::ZeroRotator);
   Warmup->Initialize(true,80.f,1.1f);
   Test->AddInfo(TEXT("Controlled formal-board PIE: real lethal damage receipt; explicit stage entrance and both cosmetic settlement directions. Not natural-play acceptance."));
   Start=W->GetTimeSeconds();Step=1;
  }
  else if(Step==1&&W->GetTimeSeconds()-Start>5)
  {
   AEGTeleportVisual::GetPreloadPath().TryLoad();
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager)GShaderCompilingManager->FinishAllCompilation();
   if(auto* Clip=Victim->GetDefinition()->GetDeathAnimation().LoadSynchronous())DeathLength=Clip->GetPlayLength();
   FEGCombatExecution::Damage(Source.Get(),Victim.Get(),100000.f);Start=W->GetTimeSeconds();Step=2;
  }
  else if(Step==2&&Capture(W,TEXT("KillBurst"),.06f)){Step=21;}
  else if(Step==21&&Capture(W,TEXT("KillBeam"),.18f)){Step=22;}
  else if(Step==22&&Capture(W,TEXT("KillTail"),.45f))
  {Start=W->GetTimeSeconds();Step=20;}
  else if(Step==20&&W->GetTimeSeconds()-Start>DeathLength+.3f)
  {
   auto* Opening=UEGOpeningPresentation::Install(Mode);
   if(!Test->TestNotNull(TEXT("Stage entrance presentation"),Opening))return true;
   auto* DeathMesh=Victim.IsValid()?Victim->GetVisiblePresentationMesh():nullptr;
   auto* DeathAnim=DeathMesh?Cast<UEGCombatAnimInstance>(DeathMesh->GetAnimInstance()):nullptr;
   Test->TestTrue(TEXT("Monster death animation reaches the last frame before transition"),DeathAnim&&DeathAnim->DeathTime>=DeathLength-.05f);
   const bool WasDeadHidden=Victim.IsValid()&&Victim->IsHidden();
   Opening->BeginStageEntrance(FGuid::NewGuid());Start=W->GetTimeSeconds();Step=3;
   if(Victim.IsValid())Test->TestEqual(TEXT("Entrance never hides a dying monster"),Victim->IsHidden(),WasDeadHidden);
  }
  else if(Step==3&&W->GetTimeSeconds()-Start>.25f)
  {
   int32 Enemies=0,Arrivals=0;
   for(auto* Unit:Mode->GetCombatUnits())if(IsValid(Unit)&&Unit->IsAlive_Implementation()&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
   {
    ++Enemies;int32 AtFoot=0;
    FVector Foot=Unit->GetActorLocation();
    Foot.Z=Mode->GetBoard()->CellToWorld(FIntPoint::ZeroValue).Z-Mode->GetBoard()->UnitCenterHeight+5.f;
    Test->AddInfo(FString::Printf(TEXT("portal monster=%s actor=%s capsule_half=%.1f board_surface=%s"),*Unit->GetName(),*Unit->GetActorLocation().ToString(),Unit->GetSimpleCollisionHalfHeight(),*Foot.ToString()));
    for(TActorIterator<AEGTeleportVisual> It(W);It;++It)if(It->GetActorLocation().Equals(Foot,1.f))++AtFoot;
    Test->TestEqual(TEXT("One portal at each monster foot"),AtFoot,1);
   }
   for(TActorIterator<AEGTeleportVisual> It(W);It;++It)++Arrivals;
   Test->TestEqual(TEXT("Portal count equals monster count"),Arrivals,Enemies);
   Capture(W,TEXT("ArrivalRing"),0.f);Step=31;
  }
  else if(Step==31&&Capture(W,TEXT("ArrivalReveal"),.65f)){Step=32;}
  else if(Step==32&&Capture(W,TEXT("ArrivalTail"),1.15f)){Start=W->GetTimeSeconds();Step=4;}
  else if((Step==4||Step==7)&&W->GetTimeSeconds()-Start>2)
  {
   int32 Counts[2]={0,0};
   for(TActorIterator<AEGArtPiece> It(W);It;++It)
    for(int32 Side=0;Side<2;++Side)
     if(It->ActorHasTag(Side==0?TEXT("EGFriendlyBoardMaster"):TEXT("EGEnemyBoardMaster")))
     {
      ++Counts[Side];
      if(Step==4)
      {PersistentMasters[Side]=*It;PersistentBodies[Side]=It->Body;PersistentInstances[Side]=It->Body->GetAnimInstance();}
     }
   Test->TestEqual(TEXT("Exactly one friendly master before settlement"),Counts[0],1);
   Test->TestEqual(TEXT("Exactly one enemy master before settlement"),Counts[1],1);
   auto* P=W->SpawnActor<AEGSettlementPresentation>();Settlement=P;*Impacts=0;
   FlightSeen=-1.f;HeardLaunch=HeardImpact=false;
   CastCaptured=HitCaptured=false;
   UAudioMixerBlueprintLibrary::StartRecordingOutput(W,5.f);
   P->SetOwner(Mode);P->OnImpact.AddLambda([Count=Impacts](){++*Count;});
   P->StartPresentation({},nullptr,FVector::ZeroVector,FVector::ZeroVector,Step==7);
   for(int32 Side=0;Side<2;++Side)
    Test->TestTrue(TEXT("Settlement keeps the existing actor, body and AnimInstance"),
     P->GetMasterActor(Side==0)==PersistentMasters[Side].Get()&&PersistentMasters[Side].IsValid()&&
     PersistentMasters[Side]->Body==PersistentBodies[Side].Get()&&
     PersistentBodies[Side]->GetAnimInstance()==PersistentInstances[Side].Get());
   for(TActorIterator<AEGArtPiece> It(W);It;++It)
    if(It->ActorHasTag(TEXT("EGFriendlyBoardMaster"))||It->ActorHasTag(TEXT("EGEnemyBoardMaster")))
     Test->TestFalse(TEXT("Persistent master remains visible during settlement"),It->IsHidden());
   Start=W->GetTimeSeconds();++Step;
  }
  else if(Step==5||Step==8)
  {
   if(!CastCaptured)
   {
    if(Capture(W,Step==5?TEXT("EnemyCast"):TEXT("FriendlyDraw"),.35f))CastCaptured=true;
    return false;
   }
   bool Flying=Requested;
   if(Settlement.IsValid())
   {
    TArray<UNiagaraComponent*> FXs;Settlement->GetComponents(FXs);
    for(auto* FX:FXs)if(FX->GetName()==TEXT("MasterProjectileFX")&&FX->IsActive())Flying=true;
    for(TActorIterator<AEGCombatVisual> It(W);It;++It)
     if(It->GetOwner()==Settlement.Get()&&!It->IsHidden()&&It->GetName().Contains(TEXT("CombatVisual"))&&W->GetTimeSeconds()-Start>.9f)Flying=true;
   }
   if(Flying&&FlightSeen<0)FlightSeen=W->GetTimeSeconds();
   if(((FlightSeen>=0&&W->GetTimeSeconds()-FlightSeen>.12f)||W->GetTimeSeconds()-Start>1.2f)&&Capture(W,Step==5?TEXT("EnemyAttack"):TEXT("FriendlyAttack"),0.f))++Step;
  }
  else if((Step==6||Step==9)&&!HitCaptured&&W->GetTimeSeconds()-Start>1.65f)
  {
   if(Settlement.IsValid())
   {
    auto* Master=Settlement->GetMasterActor(Step==6);
    auto* Action=Master?Master->FindComponentByClass<UEGBoardMasterActionComponent>():nullptr;
    auto* Montage=Action?Action->GetPlayingMontage():nullptr;
    Test->TestTrue(TEXT("Persistent defender plays a source hit montage through its AnimBP"),
     Master&&Cast<UEGCombatAnimInstance>(Master->Body->GetAnimInstance())&&Montage&&
     Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference()->GetName()==(Step==6?TEXT("A_Elysia_Hit"):TEXT("A_Corruption_Hit")));

   }
   if(Capture(W,Step==6?TEXT("EnemyImpact"):TEXT("FriendlyImpact"),0.f))HitCaptured=true;
  }
  else if((Step==6||Step==9)&&W->GetTimeSeconds()-Start>4)
  {
   Test->TestEqual(TEXT("Settlement impact occurs once"),*Impacts,1);
   Test->TestTrue(TEXT("Launch sound played"),HeardLaunch);
   Test->TestTrue(TEXT("Impact sound played"),HeardImpact);
   UAudioMixerBlueprintLibrary::StopRecordingOutput(W,EAudioRecordingExportType::WavFile,
    Step==6?TEXT("EnemySettlement"):TEXT("FriendlySettlement"),FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("BoardPolish20261109/Graphics01")));
   if(Settlement.IsValid())Settlement->Destroy();
   for(int32 Side=0;Side<2;++Side)
   {
    Test->TestTrue(TEXT("Settlement cleanup preserves master actor and animation graph"),
     PersistentMasters[Side].IsValid()&&PersistentBodies[Side].IsValid()&&
     PersistentBodies[Side]->GetAnimInstance()==PersistentInstances[Side].Get());
    auto* Action=PersistentMasters[Side].IsValid()?PersistentMasters[Side]->FindComponentByClass<UEGBoardMasterActionComponent>():nullptr;
    Test->TestTrue(TEXT("Completed master returns to idle without an active montage"),Action&&!Action->GetPlayingMontage());
   }
   if(Step==9)return true;
   Start=W->GetTimeSeconds();Step=7;
  }
  return false;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBoardPolishPIE,"ElyGambit.Integration.BoardPolish.Controlled",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBoardPolishPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender())return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FPolishStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FPolishCapture(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
#endif
