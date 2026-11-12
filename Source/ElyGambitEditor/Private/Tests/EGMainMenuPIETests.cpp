#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/QualifiedFrameTime.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "Async/Future.h"
#include "HAL/FileManager.h"
#include "RHI.h"
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
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Animation/SkeletalMeshActor.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Button.h"
#include "Game/EGMainMenuGameMode.h"
#include "Game/EGCardManager.h"
#include "Character/EGChessUnit.h"
#include "Presentation/EGOpeningCinematic.h"
#include "UI/Widget/EGMainMenuWidget.h"
#include "UObject/UnrealType.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"

namespace
{
constexpr const TCHAR* MenuMap = TEXT("/Game/Assets/Maps/Gameplay/L_ElyMainMenu");
// These checks cover the retained sequence preview, not the new normal entry.
// Limit the pre-spawn override to the temporary PIE menu world. No class
// defaults or content assets are modified.
FDelegateHandle LegacyWorldInitHandle,LegacyWorldCleanupHandle,LegacyPreSpawnHandle;
TWeakObjectPtr<UWorld> LegacyPIEWorld;
int32 LegacyPreparedControllers=0;

void RemoveLegacyFixtureHooks()
{
 if(LegacyPIEWorld.IsValid()&&LegacyPreSpawnHandle.IsValid())
  LegacyPIEWorld->RemoveOnActorPreSpawnInitialization(LegacyPreSpawnHandle);
 LegacyPreSpawnHandle.Reset();LegacyPIEWorld.Reset();
 if(LegacyWorldInitHandle.IsValid())FWorldDelegates::OnPostWorldInitialization.Remove(LegacyWorldInitHandle);
 if(LegacyWorldCleanupHandle.IsValid())FWorldDelegates::OnWorldCleanup.Remove(LegacyWorldCleanupHandle);
 LegacyWorldInitHandle.Reset();LegacyWorldCleanupHandle.Reset();
}

void InstallLegacyFixtureHooks()
{
 RemoveLegacyFixtureHooks();LegacyPreparedControllers=0;
 LegacyWorldInitHandle=FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* World,const UWorld::InitializationValues)
 {
  if(!World||World->WorldType!=EWorldType::PIE||!World->GetName().Contains(TEXT("L_ElyMainMenu"))||LegacyPIEWorld.IsValid())return;
  LegacyPIEWorld=World;
  LegacyPreSpawnHandle=World->AddOnActorPreSpawnInitialization(FOnActorSpawned::FDelegate::CreateLambda([](AActor* Actor)
  {
   if(auto* Controller=Cast<AEGMainMenuController>(Actor))
   {
    Controller->bUseIntegratedOpening=false;
    ++LegacyPreparedControllers;
    UE_LOG(LogTemp,Display,TEXT("Legacy menu PIE fixture configured controller before initialization: %s"),*GetNameSafe(Controller));
   }
  }));
 });
 LegacyWorldCleanupHandle=FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World,bool,bool)
 {
  if(LegacyPIEWorld.Get()==World)RemoveLegacyFixtureHooks();
 });
}

bool VisualCompilationPending()
{
 return (GShaderCompilingManager && GShaderCompilingManager->IsCompiling()) ||
  FAssetCompilingManager::Get().GetNumRemainingAssets() > 0;
}

struct FMenuCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 int32 Draws = 0;
 bool bRequested = false;
 ~FMenuCapture(){ if(Client.IsValid() && Handle.IsValid()) Client->OnDrawn().Remove(Handle); }
};

TSharedPtr<FMenuCapture> BeginMenuCapture(UWorld* World, const FString& Label)
{
 UGameViewportClient* Client = World ? World->GetGameViewport() : nullptr;
 if(!Client || !Client->Viewport || !Client->GetWindow().IsValid() || !Client->GetGameViewportWidget().IsValid()) return nullptr;
 TSharedPtr<FMenuCapture> Capture = MakeShared<FMenuCapture>();
 Capture->Client = Client;
 Capture->Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("MainMenu20261104")/
  (Label+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path), true);
 TWeakPtr<FMenuCapture> Weak = Capture;
 Capture->Handle = Client->OnDrawn().AddLambda([Weak]()
 {
  TSharedPtr<FMenuCapture> State = Weak.Pin();
  if(!State.IsValid()) return;
  ++State->Draws;
  if(VisualCompilationPending()){ State->Draws=0; return; }
  if(State->Draws >= 3 && !State->bRequested && State->Client.IsValid() &&
     State->Client->GetGameViewportWidget().IsValid() && !FScreenshotRequest::IsScreenshotRequested())
  {
   State->bRequested = true;
   FScreenshotRequest::RequestScreenshot(State->Path, true, false, false, FIntRect(), true);
  }
 });
 return Capture;
}

bool CheckMenuCapture(const TSharedPtr<FMenuCapture>& Capture, FIntPoint Expected, FAutomationTestBase* Test)
{
 if(!Capture.IsValid() || !Capture->bRequested || Capture->Draws < 5 || FScreenshotRequest::IsScreenshotRequested()) return false;
 TArray<uint8> Bytes;
 if(!FFileHelper::LoadFileToArray(Bytes, *Capture->Path) || Bytes.Num() < 36) return false;
 const int32 End=Bytes.Num()-12;
 if(Bytes[End] || Bytes[End+1] || Bytes[End+2] || Bytes[End+3] ||
    Bytes[End+4]!='I' || Bytes[End+5]!='E' || Bytes[End+6]!='N' || Bytes[End+7]!='D') return false;
 const uint8 Signature[]={137,80,78,71,13,10,26,10};
 for(int32 I=0;I<8;++I) if(Bytes[I]!=Signature[I]){Test->AddError(TEXT("Menu screenshot is not a PNG"));return true;}
 const auto ReadBE=[&](int32 Offset)->uint32
 {return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|Bytes[Offset+3];};
 Test->TestEqual(TEXT("PIE screenshot actual dimensions"),
  FIntPoint(static_cast<int32>(ReadBE(16)),static_cast<int32>(ReadBE(20))), Expected);
 Test->AddInfo(FString::Printf(TEXT("PIE game viewport with UI: %s. Inspect image content separately."),*Capture->Path));
 return true;
}

class FMenuStartSizedPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
 FAutomationTestBase* Test;
public:
 FMenuStartSizedPIE(FAutomationTestBase* InTest,FIntPoint InSize):Size(InSize),Test(InTest){}
 bool Update() override
 {
  InstallLegacyFixtureHooks();
  Test->AddInfo(TEXT("Legacy menu fixture is scoped to the temporary PIE menu controller before initialization; normal integrated entry is tested separately."));
  ULevelEditorPlaySettings* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(Size.X,Size.Y))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr) FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);
  return true;
 }
};

UObject* ObjectProperty(UObject* Object, const TCHAR* Name)
{
 const FObjectPropertyBase* Property=Object?FindFProperty<FObjectPropertyBase>(Object->GetClass(),Name):nullptr;
 return Property?Property->GetObjectPropertyValue_InContainer(Object):nullptr;
}

bool ReadPlayerTime(UObject* Player, const TCHAR* FunctionName, double& OutSeconds)
{
 UFunction* Function=Player?Player->FindFunction(FName(FunctionName)):nullptr;
 if(!Function || Function->ParmsSize!=sizeof(FQualifiedFrameTime)) return false;
 struct FParams { FQualifiedFrameTime ReturnValue; } Params;
 Player->ProcessEvent(Function,&Params);
 OutSeconds=Params.ReturnValue.AsSeconds();
 return FMath::IsFinite(OutSeconds);
}

bool ReadPlayerPlaying(UObject* Player, bool& bOutPlaying)
{
 UFunction* Function=Player?Player->FindFunction(TEXT("IsPlaying")):nullptr;
 if(!Function || Function->ParmsSize!=sizeof(bool)) return false;
 struct FParams { bool ReturnValue=false; } Params;
 Player->ProcessEvent(Function,&Params);
 bOutPlaying=Params.ReturnValue;
 return true;
}

bool ReadPlayerPaused(UObject* Player, bool& bOutPaused)
{
 UFunction* Function=Player?Player->FindFunction(TEXT("IsPaused")):nullptr;
 if(!Function || Function->ParmsSize!=sizeof(bool)) return false;
 struct FParams { bool ReturnValue=false; } Params;
 Player->ProcessEvent(Function,&Params);
 bOutPaused=Params.ReturnValue;
 return true;
}

bool InvokePlayer(UObject* Player, const TCHAR* Name)
{
 UFunction* Function=Player?Player->FindFunction(FName(Name)):nullptr;
 if(!Function || Function->ParmsSize!=0) return false;
 Player->ProcessEvent(Function,nullptr);
 return true;
}

bool ReadDelegateBinding(UObject* Player, const TCHAR* PropertyName, UObject* Target, const TCHAR* FunctionName, bool& OutBound)
{
 const FMulticastDelegateProperty* Property=Player?FindFProperty<FMulticastDelegateProperty>(Player->GetClass(),PropertyName):nullptr;
 const FMulticastScriptDelegate* Delegate=Property?Property->GetMulticastDelegate(Property->ContainerPtrToValuePtr<void>(Player)):nullptr;
 if(!Delegate || !Target) return false;
 OutBound=Delegate->Contains(Target,FName(FunctionName));
 return true;
}

AActor* FindSequenceActor(UWorld* World, FName Tag, int32& Count)
{
 Count=0;AActor* Found=nullptr;
 UClass* Type=FindObject<UClass>(nullptr,TEXT("/Script/LevelSequence.LevelSequenceActor"));
 if(!World || !Type) return nullptr;
 for(TActorIterator<AActor> It(World);It;++It)
  if(It->IsA(Type) && It->ActorHasTag(Tag)){Found=*It;++Count;}
 return Found;
}

bool SequenceAutoplay(AActor* Actor, bool& OutValue)
{
 const FStructProperty* Settings=Actor?FindFProperty<FStructProperty>(Actor->GetClass(),TEXT("PlaybackSettings")):nullptr;
 const FBoolProperty* Auto=Settings?FindFProperty<FBoolProperty>(Settings->Struct,TEXT("bAutoPlay")):nullptr;
 if(!Auto) return false;
 OutValue=Auto->GetPropertyValue_InContainer(Settings->ContainerPtrToValuePtr<void>(Actor));
 return true;
}

int32 CountSkeletalTrackBindings(UObject* SequenceAsset)
{
 UObject* Scene=ObjectProperty(SequenceAsset,TEXT("MovieScene"));
 const FArrayProperty* Bindings=Scene?FindFProperty<FArrayProperty>(Scene->GetClass(),TEXT("ObjectBindings")):nullptr;
 const FStructProperty* BindingStruct=Bindings?CastField<FStructProperty>(Bindings->Inner):nullptr;
 const FArrayProperty* Tracks=BindingStruct?FindFProperty<FArrayProperty>(BindingStruct->Struct,TEXT("Tracks")):nullptr;
 const FObjectPropertyBase* TrackObject=Tracks?CastField<FObjectPropertyBase>(Tracks->Inner):nullptr;
 if(!TrackObject) return -1;
 FScriptArrayHelper BindingArray(Bindings,Bindings->ContainerPtrToValuePtr<void>(Scene));
 int32 Result=0;
 for(int32 I=0;I<BindingArray.Num();++I)
 {
  void* Binding=BindingArray.GetRawPtr(I);
  FScriptArrayHelper TrackArray(Tracks,Tracks->ContainerPtrToValuePtr<void>(Binding));
  for(int32 J=0;J<TrackArray.Num();++J)
  {
   UObject* Track=TrackObject->GetObjectPropertyValue(TrackArray.GetRawPtr(J));
   if(Track && Track->GetClass()->GetName().Contains(TEXT("MovieSceneSkeletalAnimationTrack"))){++Result;break;}
  }
 }
 return Result;
}

int32 CountActors(UWorld* World, int32& OutComponents)
{
 int32 Actors=0;OutComponents=0;
 for(TActorIterator<AActor> It(World);It;++It)
 {
  if(!IsValid(*It)) continue;
  ++Actors;
  OutComponents+=It->GetComponents().Num();
 }
 return Actors;
}

class FMenuCheck final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 bool bCheckCycle;
 FString Label;
 double Deadline=0., ReadyAt=0., LastWall=0., LastSequenceTime=-1.;
 int32 InitialActors=0,InitialComponents=0,InitialWidgets=0, Wraps=0;
 int32 MinActors=MAX_int32, MaxActors=0, MinComponents=MAX_int32, MaxComponents=0, MinWidgets=MAX_int32, MaxWidgets=0;
 double NextCountSample=0.;
 FIntPoint LastViewport=FIntPoint::ZeroValue;
 bool bStarted=false,bCaptured=false;
#if CSV_PROFILER
 bool bOwnCsv=false,bCsvEnding=false,bCsvAttempted=false;
 TSharedFuture<FString> CsvResult;
#endif
 TWeakObjectPtr<AActor> Camera;
 TWeakObjectPtr<UObject> SequencePlayer;
 TArray<TWeakObjectPtr<USkeletalMeshComponent>> DisplayMeshes;
 TArray<FQuat> InitialSpineRotations;
 TArray<double> MaxSpineRotationDegrees;
 FVector LastCameraLocation=FVector::ZeroVector;
 FQuat LastCameraRotation=FQuat::Identity;
 TSharedPtr<FMenuCapture> Capture;

 bool InspectInitial(UWorld* World, APlayerController* PC, const TArray<UUserWidget*>& Menus)
 {
  auto* Mode=World->GetAuthGameMode<AEGMainMenuGameMode>();
  if(!Test->TestNotNull(TEXT("Independent menu GameMode"),Mode) ||
     !Test->TestNotNull(TEXT("Menu player controller"),Cast<AEGMainMenuController>(PC))) return false;
  Test->TestTrue(TEXT("PIE controller received legacy fixture before initialization"),
   LegacyPreparedControllers>0&&!CastChecked<AEGMainMenuController>(PC)->bUseIntegratedOpening);
  Test->TestTrue(TEXT("Map uses menu GameMode blueprint"),Mode->GetClass()->GetPathName().Contains(TEXT("BP_ElyMenuGameMode")));
  Test->TestTrue(TEXT("Menu controller is blueprint instance"),PC->GetClass()->GetPathName().Contains(TEXT("BP_ElyMenuController")));
  Test->TestNull(TEXT("No player pawn in menu"),PC->GetPawn());
  int32 CombatManagers=0,CombatUnits=0,OldDirectors=0,Performers=0;
  bool bSawElysia=false,bSawCorruption=false;
  for(TActorIterator<AEGCardManager> It(World);It;++It) ++CombatManagers;
  for(TActorIterator<AEGChessUnit> It(World);It;++It) ++CombatUnits;
  for(TActorIterator<AEGOpeningCinematic> It(World);It;++It) ++OldDirectors;
  for(TActorIterator<ASkeletalMeshActor> It(World);It;++It)
  {
   USkeletalMeshComponent* Mesh=It->GetSkeletalMeshComponent();
   if(Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->IsVisible() && !Mesh->bHiddenInGame)
   {
    ++Performers;
    const FString Name=Mesh->GetSkeletalMeshAsset()->GetName();
    bSawElysia |= Name==TEXT("SK_C1_Body");
    bSawCorruption |= Name==TEXT("SK_Corruption_Boss312_Runtime");
    Test->TestNotNull(*FString::Printf(TEXT("Display actor %s has an animation instance"),*Name),Mesh->GetAnimInstance());
    Test->TestTrue(*FString::Printf(TEXT("Display actor %s has spine bone"),*Name),Mesh->GetBoneIndex(TEXT("Bip001-Spine2"))!=INDEX_NONE);
    DisplayMeshes.Add(Mesh);
    InitialSpineRotations.Add(Mesh->GetSocketTransform(TEXT("Bip001-Spine2"),RTS_Component).GetRotation());
    MaxSpineRotationDegrees.Add(0.);
   }
  }
  Test->TestEqual(TEXT("No board card manager in menu"),CombatManagers,0);
  Test->TestEqual(TEXT("No combat units in menu"),CombatUnits,0);
  Test->TestEqual(TEXT("No old opening director creates duplicate UI"),OldDirectors,0);
  Test->TestEqual(TEXT("Exactly two visible skeletal display actors"),Performers,2);
  Test->TestTrue(TEXT("Elysia display mesh exists"),bSawElysia);
  Test->TestTrue(TEXT("Corruption display mesh exists"),bSawCorruption);
  Test->TestEqual(TEXT("Exactly one top-level main menu"),Menus.Num(),1);
  if(Menus.Num()==1)
  {
   for(const FName Name : {FName(TEXT("StartGameButton")),FName(TEXT("SettingsButton")),FName(TEXT("QuitButton"))})
   {
    UButton* Button=Cast<UButton>(Menus[0]->GetWidgetFromName(Name));
    if(Test->TestNotNull(*FString::Printf(TEXT("Menu button %s exists"),*Name.ToString()),Button))
     Test->TestTrue(*FString::Printf(TEXT("Menu button %s visible and enabled"),*Name.ToString()),Button->IsVisible() && Button->GetIsEnabled());
   }
  }
  Camera=PC->GetViewTarget();
  if(!Test->TestTrue(TEXT("View target is tagged menu camera"),Camera.IsValid() && Camera->ActorHasTag(TEXT("ElyMenuCamera")))) return false;

  int32 IdleCount=0,OpeningCount=0;
  AActor* Found=FindSequenceActor(World,TEXT("ElyMenuIdle"),IdleCount);
  AActor* Opening=FindSequenceActor(World,TEXT("ElyMenuOpening"),OpeningCount);
  if(!Test->TestEqual(TEXT("Exactly one tagged idle sequence"),IdleCount,1) ||
     !Test->TestEqual(TEXT("Exactly one tagged opening sequence"),OpeningCount,1) ||
     !Test->TestNotNull(TEXT("Idle sequence actor exists"),Found) ||
     !Test->TestNotNull(TEXT("Opening sequence actor exists"),Opening) ||
     !Test->TestTrue(TEXT("Idle and opening use different actors"),Found!=Opening)) return false;
  UObject* Asset=ObjectProperty(Found,TEXT("LevelSequenceAsset"));
  Test->TestTrue(TEXT("Idle sequence is expected asset"),Asset && Asset->GetPathName().Contains(TEXT("LS_ElyMenuIdle")));
  UObject* OpeningAsset=ObjectProperty(Opening,TEXT("LevelSequenceAsset"));
  Test->TestTrue(TEXT("Opening sequence is expected asset"),OpeningAsset && OpeningAsset->GetPathName().Contains(TEXT("LS_ElyMenuOpening")));
  Test->TestTrue(TEXT("Idle has at least two skeletal animation bindings"),CountSkeletalTrackBindings(Asset)>=2);
  UObject* Scene=ObjectProperty(Asset,TEXT("MovieScene"));
  if(Test->TestNotNull(TEXT("Sequence MovieScene exists"),Scene))
  {
   const FObjectPropertyBase* Cut=FindFProperty<FObjectPropertyBase>(Scene->GetClass(),TEXT("CameraCutTrack"));
   if(Test->TestNotNull(TEXT("Sequence CameraCutTrack property exists"),Cut))
    Test->TestNull(TEXT("Menu sequence has no camera cuts"),Cut->GetObjectPropertyValue_InContainer(Scene));
  }
  bool bIdleAuto=true,bOpeningAuto=true;
  Test->TestTrue(TEXT("Can read idle autoplay"),SequenceAutoplay(Found,bIdleAuto));
  Test->TestTrue(TEXT("Can read opening autoplay"),SequenceAutoplay(Opening,bOpeningAuto));
  Test->TestFalse(TEXT("Controller starts idle, actor autoplay disabled"),bIdleAuto);
  Test->TestFalse(TEXT("Opening is not autoplaying"),bOpeningAuto);
  UObject* OpeningPlayer=ObjectProperty(Opening,TEXT("SequencePlayer"));
  bool bOpeningPlaying=false,bOpeningPaused=false;
  if(Test->TestNotNull(TEXT("Opening player exists"),OpeningPlayer))
  {
   Test->TestTrue(TEXT("Can read opening playing state"),ReadPlayerPlaying(OpeningPlayer,bOpeningPlaying));
   Test->TestTrue(TEXT("Can read opening paused state"),ReadPlayerPaused(OpeningPlayer,bOpeningPaused));
   Test->TestFalse(TEXT("Opening player initially stopped"),bOpeningPlaying || bOpeningPaused);
   double OpeningDuration=0.;
   if(Test->TestTrue(TEXT("Can read opening duration"),ReadPlayerTime(OpeningPlayer,TEXT("GetDuration"),OpeningDuration)))
    Test->TestTrue(TEXT("Opening duration is 11.5 seconds"),FMath::IsNearlyEqual(OpeningDuration,11.5,.05));
  }
  SequencePlayer=ObjectProperty(Found,TEXT("SequencePlayer"));
  if(!Test->TestTrue(TEXT("Sequence player exists"),SequencePlayer.IsValid())) return false;
  bool bPlaying=false;
  if(!Test->TestTrue(TEXT("Read real sequence playback state"),ReadPlayerPlaying(SequencePlayer.Get(),bPlaying))) return false;
  Test->TestTrue(TEXT("Idle sequence is playing"),bPlaying);
  double Duration=0.;
  if(Test->TestTrue(TEXT("Read actual sequence duration"),ReadPlayerTime(SequencePlayer.Get(),TEXT("GetDuration"),Duration)))
  {
   Test->TestTrue(TEXT("Idle duration between 20 and 30 seconds"),Duration>=20. && Duration<=30.);
   Test->AddInfo(FString::Printf(TEXT("Observed sequence duration %.3f seconds"),Duration));
  }
  return true;
 }
public:
 FMenuCheck(FAutomationTestBase* InTest,FIntPoint InSize,bool bInCheckCycle,const TCHAR* InLabel)
  :Test(InTest),Size(InSize),bCheckCycle(bInCheckCycle),Label(InLabel){}
 ~FMenuCheck()
 {
#if CSV_PROFILER
  if(bOwnCsv && !bCsvEnding) FCsvProfiler::Get()->EndCapture();
#endif
 }
 bool Update() override
 {
  const double Now=FPlatformTime::Seconds();
  if(!Deadline) Deadline=Now+115.;
  if(Now>Deadline)
  {
   Test->AddError(FString::Printf(TEXT("Menu graphical PIE timed out or screenshot could not be written; requested %dx%d, last viewport %dx%d"),
    Size.X,Size.Y,LastViewport.X,LastViewport.Y));
   return true;
  }
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  APlayerController* PC=World?World->GetFirstPlayerController():nullptr;
  UGameViewportClient* Client=World?World->GetGameViewport():nullptr;
  if(!World || !PC || !Client || !Client->Viewport || World->GetTimeSeconds()<1.f || VisualCompilationPending()) return false;
  if(!Client->GetWindow().IsValid() || !Client->GetGameViewportWidget().IsValid()) return false;
  const FIntPoint Actual=Client->Viewport->GetSizeXY();
  LastViewport=Actual;
  if(Actual!=Size) return false;
  TArray<UUserWidget*> Menus;
  UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Menus,UEGMainMenuWidget::StaticClass(),true);
  if(Menus.Num()==0) return false;
  if(!bStarted)
  {
   if(!InspectInitial(World,PC,Menus)) return true;
   InitialActors=CountActors(World,InitialComponents);InitialWidgets=Menus.Num();
   MinActors=MaxActors=InitialActors;MinComponents=MaxComponents=InitialComponents;
   MinWidgets=MaxWidgets=InitialWidgets;NextCountSample=Now+1.;
   ReadyAt=Now;bStarted=true;
   Test->AddInfo(FString::Printf(TEXT("Actual game viewport %dx%d; baseline actors=%d components=%d menus=%d"),Actual.X,Actual.Y,InitialActors,InitialComponents,InitialWidgets));
   return false;
  }
  if(!Camera.IsValid() || PC->GetViewTarget()!=Camera.Get() || !SequencePlayer.IsValid())
  {Test->AddError(TEXT("Menu camera or sequence player was replaced during idle"));return true;}
  bool bPlaying=false;double Current=0.;
  if(!ReadPlayerPlaying(SequencePlayer.Get(),bPlaying) || !ReadPlayerTime(SequencePlayer.Get(),TEXT("GetCurrentTime"),Current))
  {Test->AddError(TEXT("Could not observe live sequence playback"));return true;}
  if(!bPlaying){Test->AddError(TEXT("Idle sequence stopped during observation"));return true;}
  if(LastSequenceTime>=0. && Current < LastSequenceTime-1.)
  {
   ++Wraps;
   const double Gap=Now-LastWall;
   const double Distance=FVector::Distance(Camera->GetActorLocation(),LastCameraLocation);
   const double Angle=FMath::RadiansToDegrees(Camera->GetActorQuat().AngularDistance(LastCameraRotation));
   Test->AddInfo(FString::Printf(TEXT("Sequence wrap: previous %.3f s, now %.3f s; frame gap %.3f s, camera step %.3f cm / %.3f deg"),LastSequenceTime,Current,Gap,Distance,Angle));
   Test->TestTrue(TEXT("Sequence wrap observed without long frame gap"),Gap<.25);
   Test->TestTrue(TEXT("Camera position continuous across observed wrap"),Distance<5.);
   Test->TestTrue(TEXT("Camera rotation continuous across observed wrap"),Angle<1.);
  }
  LastSequenceTime=Current;LastWall=Now;LastCameraLocation=Camera->GetActorLocation();LastCameraRotation=Camera->GetActorQuat();
  for(int32 I=0;I<DisplayMeshes.Num();++I)
  {
   if(!DisplayMeshes[I].IsValid()){Test->AddError(TEXT("A display skeletal component was destroyed"));return true;}
   const FQuat Rotation=DisplayMeshes[I]->GetSocketTransform(TEXT("Bip001-Spine2"),RTS_Component).GetRotation();
   MaxSpineRotationDegrees[I]=FMath::Max(MaxSpineRotationDegrees[I],
    FMath::RadiansToDegrees(Rotation.AngularDistance(InitialSpineRotations[I])));
  }
  if(Now>=NextCountSample)
  {
   int32 Components=0;const int32 Actors=CountActors(World,Components);
   MinActors=FMath::Min(MinActors,Actors);MaxActors=FMath::Max(MaxActors,Actors);
   MinComponents=FMath::Min(MinComponents,Components);MaxComponents=FMath::Max(MaxComponents,Components);
   MinWidgets=FMath::Min(MinWidgets,Menus.Num());MaxWidgets=FMath::Max(MaxWidgets,Menus.Num());
   NextCountSample=Now+1.;
  }
  if(!bCaptured && Now-ReadyAt>=2.)
  {
   if(!Capture.IsValid())
   {
    Capture=BeginMenuCapture(World,Label);
    if(!Capture.IsValid()){Test->AddError(TEXT("Could not access actual PIE game viewport for screenshot"));return true;}
   }
   if(CheckMenuCapture(Capture,Size,Test)){bCaptured=true;Capture.Reset();}
  }
#if CSV_PROFILER
  if(bCheckCycle && !bCsvAttempted && bCaptured && Now-ReadyAt>=3.)
  {
   bCsvAttempted=true;
   if(!FCsvProfiler::IsCapturing())
   {
    const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("MainMenu20261104"));
    IFileManager::Get().MakeDirectory(*Directory,true);
    const FString Name=TEXT("PIE_Menu1080_")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
    FCsvProfiler::SetNonPersistentMetadata(TEXT("MenuViewportPixels"),*FString::Printf(TEXT("%dx%d"),Actual.X,Actual.Y));
    FCsvProfiler::SetNonPersistentMetadata(TEXT("MenuCaptureScope"),TEXT("Editor PIE including editor overhead"));
    FCsvProfiler::Get()->BeginCapture(-1,Directory,Name);
    bOwnCsv=true;
   }
   else Test->AddWarning(TEXT("CSV profiler already in use; this test will not take over another capture"));
  }
#endif
  if(!bCaptured || (bCheckCycle && (Now-ReadyAt<30. || Wraps<1))) return false;
#if CSV_PROFILER
  if(bOwnCsv && !bCsvEnding)
  {
   if(!FCsvProfiler::IsCapturing())
   {
    Test->AddError(TEXT("PIE CSV capture did not start or was stopped externally"));
    bOwnCsv=false;
    return true;
   }
   bCsvEnding=true;
   CsvResult=FCsvProfiler::Get()->EndCapture();
   if(!CsvResult.IsValid())
   {
    Test->AddError(TEXT("PIE CSV profiler did not provide a completion result"));
    return true;
   }
   return false;
  }
  if(bOwnCsv && !CsvResult.IsReady()) return false;
  if(bOwnCsv)
  {
   const FString CsvPath=CsvResult.Get();
   Test->TestTrue(TEXT("PIE frame-time CSV was written"),!CsvPath.IsEmpty() && IFileManager::Get().FileExists(*CsvPath));
   Test->AddInfo(FString::Printf(TEXT("Editor PIE CSV: %s; includes editor overhead and must not be presented as packaged-game performance."),*CsvPath));
  }
#endif
  int32 Components=0;const int32 Actors=CountActors(World,Components);
  Test->TestEqual(TEXT("Actor count remains stable through observation"),Actors,InitialActors);
  Test->TestEqual(TEXT("Actor component count remains stable through observation"),Components,InitialComponents);
  Test->TestEqual(TEXT("Only one top-level menu remains"),Menus.Num(),InitialWidgets);
  Test->TestEqual(TEXT("Sampled actor count never changes"),MinActors,MaxActors);
  Test->TestEqual(TEXT("Sampled component count never changes"),MinComponents,MaxComponents);
  Test->TestEqual(TEXT("Sampled menu widget count never changes"),MinWidgets,MaxWidgets);
  for(int32 I=0;I<DisplayMeshes.Num();++I)
  {
   Test->TestTrue(TEXT("Sequencer idle changes performer spine pose"),MaxSpineRotationDegrees[I]>.001);
   Test->AddInfo(FString::Printf(TEXT("Idle performer %d max sampled spine change %.4f degrees"),I,MaxSpineRotationDegrees[I]));
  }
  Test->AddInfo(FString::Printf(TEXT("Observed %.2f wall seconds; wraps=%d; actors=%d components=%d menus=%d. Input and visual acceptance not asserted."),
   Now-ReadyAt,Wraps,Actors,Components,Menus.Num()));
  return true;
 }
};

// Exercise the actual map and sequence players. Calls into the controller are
// programmatic; this is not a test of mouse input or visual acceptance.
class FOpeningPreviewCheck final : public IAutomationLatentCommand
{
 enum class EStep { Ready, Pause, PauseRecovery, Stop, StopRecovery, Finish, Hold };
 FAutomationTestBase* Test;
 EStep Step=EStep::Ready;
 double Deadline=0., HoldAt=0.;
 TWeakObjectPtr<AEGMainMenuController> Controller;
 TWeakObjectPtr<UObject> Idle,Opening;
 TWeakObjectPtr<AActor> Camera,Corruption;
 FVector FinalCamera=FVector::ZeroVector,FinalCorruption=FVector::ZeroVector;
 FQuat FinalCameraRotation=FQuat::Identity;

 bool StartAgain(const TCHAR* Label)
 {
  AEGMainMenuController* PC=Controller.Get();
  if(!PC || !Test->TestTrue(Label,PC->RequestStartGame())) return false;
  Test->TestFalse(TEXT("Duplicate start rejected during opening"),PC->RequestStartGame());
  return true;
 }
 bool CheckRecovery()
 {
  AEGMainMenuController* PC=Controller.Get();
  UEGMainMenuWidget* Menu=PC?PC->GetMenuWidget():nullptr;
  bool bIdlePlaying=false,bOpeningPlaying=false,bOpeningPaused=false;
  if(!Test->TestNotNull(TEXT("Menu survives interruption"),Menu) ||
     !Test->TestTrue(TEXT("Can inspect recovered players"),ReadPlayerPlaying(Idle.Get(),bIdlePlaying) &&
       ReadPlayerPlaying(Opening.Get(),bOpeningPlaying) && ReadPlayerPaused(Opening.Get(),bOpeningPaused))) return false;
  Test->TestTrue(TEXT("Idle resumed after interruption"),bIdlePlaying);
  Test->TestFalse(TEXT("Opening stopped after interruption"),bOpeningPlaying || bOpeningPaused);
  Test->TestTrue(TEXT("Menu interactive again"),Menu->IsInViewport() && Menu->GetIsEnabled());
  Test->TestTrue(TEXT("Mouse restored after interruption"),PC->bShowMouseCursor);
  Test->TestTrue(TEXT("Menu camera restored after interruption"),Camera.IsValid() && PC->GetViewTarget()==Camera.Get());
  return true;
 }
public:
 explicit FOpeningPreviewCheck(FAutomationTestBase* InTest):Test(InTest){}
 bool Update() override
 {
  const double Now=FPlatformTime::Seconds();
  if(!Deadline) Deadline=Now+90.;
  if(Now>Deadline){Test->AddError(FString::Printf(TEXT("Opening preview timed out at step %d"),int32(Step)));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  if(!World || World->GetTimeSeconds()<1.f || VisualCompilationPending()) return false;
  if(Step==EStep::Ready)
  {
   AEGMainMenuController* PC=Cast<AEGMainMenuController>(World->GetFirstPlayerController());
   if(!PC || !PC->GetMenuWidget()) return false;
   if(!Test->TestTrue(TEXT("Preview controller received legacy fixture before initialization"),
      LegacyPreparedControllers>0&&!PC->bUseIntegratedOpening))return true;
   int32 IdleCount=0,OpeningCount=0;
   AActor* IdleActor=FindSequenceActor(World,TEXT("ElyMenuIdle"),IdleCount);
   AActor* OpeningActor=FindSequenceActor(World,TEXT("ElyMenuOpening"),OpeningCount);
   if(!Test->TestEqual(TEXT("One actual idle sequence actor"),IdleCount,1) ||
      !Test->TestEqual(TEXT("One actual opening sequence actor"),OpeningCount,1) ||
      !Test->TestTrue(TEXT("Opening and idle actors differ"),IdleActor && OpeningActor && IdleActor!=OpeningActor)) return true;
   Controller=PC;Idle=ObjectProperty(IdleActor,TEXT("SequencePlayer"));Opening=ObjectProperty(OpeningActor,TEXT("SequencePlayer"));
   Camera=PC->GetViewTarget();
   for(TActorIterator<ASkeletalMeshActor> It(World);It;++It)
   {
    USkeletalMeshComponent* Mesh=It->GetSkeletalMeshComponent();
    if(Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetSkeletalMeshAsset()->GetName()==TEXT("SK_Corruption_Boss312_Runtime")) Corruption=*It;
   }
   if(!Test->TestTrue(TEXT("Real players and tagged camera exist"),Idle.IsValid() && Opening.IsValid() && Camera.IsValid() &&
       Camera->ActorHasTag(TEXT("ElyMenuCamera"))) || !Test->TestTrue(TEXT("Corruption performer exists"),Corruption.IsValid())) return true;
   Test->TestFalse(TEXT("Preview is disabled by default"),PC->bPreviewOnly);
   Test->TestTrue(TEXT("Menu initially idle"),PC->GetOpeningState()==EEGMainMenuOpeningState::Idle);
   PC->bPreviewOnly=true;
   if(!StartAgain(TEXT("Start opening accepted"))) return true;
   Step=EStep::Pause;
   return false;
  }
  AEGMainMenuController* PC=Controller.Get();
  if(!PC || !Idle.IsValid() || !Opening.IsValid() || !Camera.IsValid() || !Corruption.IsValid())
  {Test->AddError(TEXT("Menu objects disappeared during preview"));return true;}
  if(Step==EStep::PauseRecovery || Step==EStep::StopRecovery)
  {
   if(PC->GetOpeningState()!=EEGMainMenuOpeningState::Idle) return false;
   if(!CheckRecovery() || !StartAgain(TEXT("Retry accepted after interruption"))) return true;
   Step=Step==EStep::PauseRecovery?EStep::Stop:EStep::Finish;
   return false;
  }
  if(Step==EStep::Pause || Step==EStep::Stop)
  {
   bool bPlaying=false,bIdlePaused=false;double Position=0.;
   if(!ReadPlayerPlaying(Opening.Get(),bPlaying) || !ReadPlayerPaused(Idle.Get(),bIdlePaused) ||
      !ReadPlayerTime(Opening.Get(),TEXT("GetCurrentTime"),Position))
   {Test->AddError(TEXT("Cannot observe opening playback"));return true;}
   if(!bPlaying || Position<.8) return false;
   Test->TestTrue(TEXT("Controller remains in Playing"),PC->GetOpeningState()==EEGMainMenuOpeningState::Playing);
   Test->TestTrue(TEXT("Idle is paused during opening"),bIdlePaused);
   Test->TestTrue(TEXT("Menu disabled during opening"),PC->GetMenuWidget() && !PC->GetMenuWidget()->GetIsEnabled());
   Test->TestTrue(TEXT("Menu faded out during opening"),PC->GetMenuWidget() && PC->GetMenuWidget()->GetRenderOpacity()<.01f);
   const bool bPause=Step==EStep::Pause;
   if(!Test->TestTrue(TEXT("Interrupt actual opening player"),InvokePlayer(Opening.Get(),bPause?TEXT("Pause"):TEXT("Stop")))) return true;
   Step=bPause?EStep::PauseRecovery:EStep::StopRecovery;
   return false;
  }
  if(Step==EStep::Finish)
  {
   if(PC->GetOpeningState()!=EEGMainMenuOpeningState::Leaving) return false;
   bool bIdlePaused=false,bOpeningPaused=false;double Duration=0.,Current=0.;
   if(!Test->TestTrue(TEXT("Read completed opening state"),ReadPlayerPaused(Idle.Get(),bIdlePaused) &&
      ReadPlayerPaused(Opening.Get(),bOpeningPaused) && ReadPlayerTime(Opening.Get(),TEXT("GetDuration"),Duration) &&
      ReadPlayerTime(Opening.Get(),TEXT("GetCurrentTime"),Current))) return true;
   Test->TestTrue(TEXT("Opening has intended duration"),FMath::IsNearlyEqual(Duration,11.5,.05));
   Test->TestTrue(TEXT("Opening reaches its end"),Current>=Duration-.1);
   Test->TestTrue(TEXT("Both players paused at preview end"),bIdlePaused && bOpeningPaused);
   Test->TestTrue(TEXT("Menu remains disabled at preview end"),PC->GetMenuWidget() && !PC->GetMenuWidget()->GetIsEnabled());
   Test->TestTrue(TEXT("Menu remains faded out at preview end"),PC->GetMenuWidget() && PC->GetMenuWidget()->GetRenderOpacity()<.01f);
   FinalCamera=Camera->GetActorLocation();FinalCameraRotation=Camera->GetActorQuat();
   FinalCorruption=Corruption->GetActorLocation();HoldAt=Now;Step=EStep::Hold;
   return false;
  }
  if(Now-HoldAt<1.) return false;
  Test->TestTrue(TEXT("Preview remains on menu map"),GEditor->PlayWorld==World && PC->GetOpeningState()==EEGMainMenuOpeningState::Leaving);
  Test->TestTrue(TEXT("Preview retains camera position"),FVector::Dist(FinalCamera,Camera->GetActorLocation())<.1);
  Test->TestTrue(TEXT("Preview retains camera rotation"),FMath::RadiansToDegrees(FinalCameraRotation.AngularDistance(Camera->GetActorQuat()))<.1);
  Test->TestTrue(TEXT("Preview retains performer position"),FVector::Dist(FinalCorruption,Corruption->GetActorLocation())<.1);
  Test->AddInfo(TEXT("Preview, interruption and retry used the production sequence actors; visual and real-input acceptance remain separate."));
  return true;
 }
};

struct FOpeningEndPlayState
{
 TWeakObjectPtr<AEGMainMenuController> Controller;
 TWeakObjectPtr<UEGMainMenuWidget> Widget;
 TWeakObjectPtr<UObject> Player;
 bool bObservedPlaying=false;
};

class FOpeningWaitForEndPlay final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;TSharedRef<FOpeningEndPlayState> State;double Deadline=0.;
 bool bRequested=false;
public:
 FOpeningWaitForEndPlay(FAutomationTestBase* InTest,TSharedRef<FOpeningEndPlayState> InState):Test(InTest),State(InState){}
 bool Update() override
 {
  const double Now=FPlatformTime::Seconds();if(!Deadline) Deadline=Now+60.;
  if(Now>Deadline){Test->AddError(TEXT("Timed out waiting for real opening before EndPlay"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  if(!World || World->GetTimeSeconds()<1.f || VisualCompilationPending()) return false;
  if(!bRequested)
  {
   AEGMainMenuController* PC=Cast<AEGMainMenuController>(World->GetFirstPlayerController());
   if(!PC || !PC->GetMenuWidget()) return false;
   if(!Test->TestTrue(TEXT("EndPlay controller received legacy fixture before initialization"),
      LegacyPreparedControllers>0&&!PC->bUseIntegratedOpening))return true;
   int32 Count=0;AActor* Actor=FindSequenceActor(World,TEXT("ElyMenuOpening"),Count);
   UObject* Player=ObjectProperty(Actor,TEXT("SequencePlayer"));
   if(!Test->TestEqual(TEXT("One opening actor before EndPlay"),Count,1) ||
      !Test->TestNotNull(TEXT("Opening player before EndPlay"),Player)) return true;
   State->Controller=PC;State->Widget=PC->GetMenuWidget();State->Player=Player;
   PC->bPreviewOnly=true;
   if(!Test->TestTrue(TEXT("Start before EndPlay accepted"),PC->RequestStartGame())) return true;
   bRequested=true;
   return false;
  }
  bool bPlaying=false;
  if(!ReadPlayerPlaying(State->Player.Get(),bPlaying)){Test->AddError(TEXT("Cannot observe player before EndPlay"));return true;}
  if(!bPlaying) return false;
  for(const auto& Binding : {TPair<const TCHAR*,const TCHAR*>(TEXT("OnFinished"),TEXT("OpeningFinished")),
                             TPair<const TCHAR*,const TCHAR*>(TEXT("OnStop"),TEXT("OpeningPausedOrStopped")),
                             TPair<const TCHAR*,const TCHAR*>(TEXT("OnPause"),TEXT("OpeningPausedOrStopped"))})
  {
   bool bBound=false;
   if(Test->TestTrue(TEXT("Can inspect opening delegate"),ReadDelegateBinding(State->Player.Get(),Binding.Key,State->Controller.Get(),Binding.Value,bBound)))
    Test->TestTrue(TEXT("Controller bound while opening plays"),bBound);
  }
  State->bObservedPlaying=true;
  return true;
 }
};

class FOpeningAfterEndPlay final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;TSharedRef<FOpeningEndPlayState> State;double Deadline=0.;
public:
 FOpeningAfterEndPlay(FAutomationTestBase* InTest,TSharedRef<FOpeningEndPlayState> InState):Test(InTest),State(InState){}
 bool Update() override
 {
  const double Now=FPlatformTime::Seconds();if(!Deadline) Deadline=Now+15.;
  if(GEditor && GEditor->PlayWorld && Now<Deadline) return false;
  Test->TestTrue(TEXT("PIE world closed during opening"),GEditor && !GEditor->PlayWorld);
  Test->TestTrue(TEXT("Opening had actually started before EndPlay"),State->bObservedPlaying);
  if(State->Widget.IsValid()) Test->TestFalse(TEXT("Surviving widget removed from viewport"),State->Widget->IsInViewport());
  if(State->Player.IsValid())
  {
   bool bPlaying=false,bPaused=false;
   if(Test->TestTrue(TEXT("Can inspect surviving player"),ReadPlayerPlaying(State->Player.Get(),bPlaying) && ReadPlayerPaused(State->Player.Get(),bPaused)))
    Test->TestFalse(TEXT("Surviving opening player stopped on EndPlay"),bPlaying || bPaused);
  }
  if(State->Player.IsValid() && State->Controller.IsValid())
  {
   for(const auto& Binding : {TPair<const TCHAR*,const TCHAR*>(TEXT("OnFinished"),TEXT("OpeningFinished")),
                              TPair<const TCHAR*,const TCHAR*>(TEXT("OnStop"),TEXT("OpeningPausedOrStopped")),
                              TPair<const TCHAR*,const TCHAR*>(TEXT("OnPause"),TEXT("OpeningPausedOrStopped"))})
   {
    bool bBound=true;
    if(Test->TestTrue(TEXT("Can inspect surviving opening delegate"),ReadDelegateBinding(State->Player.Get(),Binding.Key,State->Controller.Get(),Binding.Value,bBound)))
     Test->TestFalse(TEXT("Surviving player unbound from controller"),bBound);
   }
  }
  else Test->AddInfo(TEXT("Player or controller expired by teardown; direct post-EndPlay delegate inspection unavailable."));
  return true;
 }
};

class FMenuPIECleanup final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;double Started=0.;
public:
 explicit FMenuPIECleanup(FAutomationTestBase* InTest):Test(InTest){}
 bool Update() override
 {
  if(!Started) Started=FPlatformTime::Seconds();
  if(GEditor && GEditor->PlayWorld && FPlatformTime::Seconds()-Started<15.) return false;
  Test->TestTrue(TEXT("Menu PIE world closed"),GEditor && !GEditor->PlayWorld);
  Test->TestTrue(TEXT("Legacy fixture observed a PIE menu controller"),LegacyPreparedControllers>0);
  RemoveLegacyFixtureHooks();
  return true;
 }
};

bool QueueMenuTest(FAutomationTestBase* Test,FIntPoint Size,bool bCycle,const TCHAR* Label)
{
 if(!GEditor || GEditor->PlayWorld){Test->AddError(TEXT("Menu graphical PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender() || GUsingNullRHI){Test->AddError(TEXT("Menu graphical PIE requires a real rendering RHI"));return false;}
 if(!FSlateApplication::IsInitialized()){Test->AddError(TEXT("Menu graphical PIE requires Slate"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(FString(MenuMap)));
 ADD_LATENT_AUTOMATION_COMMAND(FMenuStartSizedPIE(Test,Size));
 ADD_LATENT_AUTOMATION_COMMAND(FMenuCheck(Test,Size,bCycle,Label));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FMenuPIECleanup(Test));
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMainMenuSceneLifecycle1080,"ElyGambit.Integration.MainMenu.SceneLifecycle1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMainMenuSceneLifecycle1080::RunTest(const FString&){return QueueMenuTest(this,FIntPoint(1920,1080),true,TEXT("PIE_MainMenu_1080"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMainMenuScreenshot720,"ElyGambit.Integration.MainMenu.Screenshot720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMainMenuScreenshot720::RunTest(const FString&){return QueueMenuTest(this,FIntPoint(1280,720),false,TEXT("PIE_MainMenu_720"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMainMenuScreenshot1440,"ElyGambit.Integration.MainMenu.Screenshot1440",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMainMenuScreenshot1440::RunTest(const FString&){return QueueMenuTest(this,FIntPoint(2560,1440),false,TEXT("PIE_MainMenu_1440"));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMainMenuOpeningPreview,"ElyGambit.Integration.MainMenu.OpeningPreviewAndRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMainMenuOpeningPreview::RunTest(const FString&)
{
 if(!GEditor || GEditor->PlayWorld || !FApp::CanEverRender() || GUsingNullRHI || !FSlateApplication::IsInitialized())
 {AddError(TEXT("Opening preview requires an idle graphical editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(FString(MenuMap)));
 ADD_LATENT_AUTOMATION_COMMAND(FMenuStartSizedPIE(this,FIntPoint(1920,1080)));
 ADD_LATENT_AUTOMATION_COMMAND(FOpeningPreviewCheck(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FMenuPIECleanup(this));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMainMenuOpeningEndPlay,"ElyGambit.Integration.MainMenu.OpeningEndPlayCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMainMenuOpeningEndPlay::RunTest(const FString&)
{
 if(!GEditor || GEditor->PlayWorld || !FApp::CanEverRender() || GUsingNullRHI || !FSlateApplication::IsInitialized())
 {AddError(TEXT("Opening EndPlay requires an idle graphical editor"));return false;}
 TSharedRef<FOpeningEndPlayState> State=MakeShared<FOpeningEndPlayState>();
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(FString(MenuMap)));
 ADD_LATENT_AUTOMATION_COMMAND(FMenuStartSizedPIE(this,FIntPoint(1920,1080)));
 ADD_LATENT_AUTOMATION_COMMAND(FOpeningWaitForEndPlay(this,State));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FOpeningAfterEndPlay(this,State));
 ADD_LATENT_AUTOMATION_COMMAND(FMenuPIECleanup(this));
 return true;
}
#endif
