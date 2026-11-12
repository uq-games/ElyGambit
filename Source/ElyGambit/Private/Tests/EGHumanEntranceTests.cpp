#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGHumanEntranceComponent.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/AudioComponent.h"
#include "Blueprint/UserWidget.h"
#include "UI/EGVitalsLayoutSubsystem.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanEntranceLifecycle,
 "ElyGambit.Presentation.HumanFormalBoardEntranceLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanEntranceLifecycle::RunTest(const FString&)
{
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"));
 if(!TestNotNull(TEXT("Formal human card"),Card)||!TestNotNull(TEXT("Formal human hero"),Card->HeroDefinition.Get()))return false;
 auto* Hero=Card->HeroDefinition.Get();
 if(!TestEqual(TEXT("Card identifies the human"),Hero->HeroKind,EEGHeroKind::ElysiaHerrscher)||
    !TestNotNull(TEXT("Human presentation"),Hero->PresentationDefinition.Get()))return false;
 auto* Entrance=Hero->PresentationDefinition->PreparationEntranceAssembly.LoadSynchronous();
 if(!TestNotNull(TEXT("Human entry animation asset is bound"),Entrance)||
    !TestTrue(TEXT("Human entry has a motion"),!Entrance->Motions.IsEmpty()))return false;
 auto* Voice=Hero->PresentationDefinition->PreparationEntranceVoice.LoadSynchronous();
 if(!TestNotNull(TEXT("Human entrance voice asset is bound"),Voice))return false;
 auto* RevealSound=LoadObject<USoundBase>(nullptr,TEXT("/Game/Assets/Characters/ElysiaHerrscherOfHuman/Entrance20261106/Audio/SW_HumanEntrance_Reveal.SW_HumanEntrance_Reveal"));
 if(!TestNotNull(TEXT("Reveal sound is available"),RevealSound))return false;

 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);if(!Base)return false;
 auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,*this))return false;
 Run->bRoundShopRules=false;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
 Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto Tick=[&](float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);}};
 auto Rig=[&](AActor* Owner)->AEGCombatVisual*{for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Owner&&!It->IsActorBeingDestroyed())return *It;return nullptr;};
 auto VoiceComponent=[&](AActor* Owner)->UAudioComponent*
 {
  TInlineComponentArray<UAudioComponent*> Components(Owner);
  for(auto* Audio:Components)if(IsValid(Audio)&&Audio->IsRegistered()&&Audio->GetSound()==Voice)return Audio;
  return nullptr;
 };
 auto EntranceSound=[](AActor* Owner,const TCHAR* Prefix)->UAudioComponent*
 {
  TInlineComponentArray<UAudioComponent*> Components(Owner);
  for(auto* Audio:Components)if(IsValid(Audio)&&Audio->IsRegistered()&&Audio->GetName().StartsWith(Prefix))return Audio;
  return nullptr;
 };
 auto ShardCount=[](AEGArtPiece* Piece)
 {
  int32 Count=0;TInlineComponentArray<UWidgetComponent*> Widgets(Piece);
  for(const auto* Widget:Widgets)if(IsValid(Widget)&&Widget->IsRegistered()&&Widget->GetName().StartsWith(TEXT("HumanEntranceShard"))&&Widget->GetSlateWidget().IsValid())++Count;
  return Count;
 };
 auto HumanCard=[](AEGArtPiece* Piece)->UWidgetComponent*
 {
  TInlineComponentArray<UWidgetComponent*> Widgets(Piece);
  for(auto* Widget:Widgets)if(IsValid(Widget)&&Widget->IsRegistered()&&Widget->GetName().StartsWith(TEXT("HumanEntrancePortraitCard")))return Widget;
  return nullptr;
 };
 auto BurstCard=[](AEGArtPiece* Piece)->UStaticMeshComponent*
 {
  TInlineComponentArray<UStaticMeshComponent*> Meshes(Piece);
  for(auto* Mesh:Meshes)if(IsValid(Mesh)&&Mesh->IsRegistered()&&Mesh->GetName().StartsWith(TEXT("HumanEntranceBurst")))return Mesh;
  return nullptr;
 };
 if(!TestTrue(TEXT("Grant formal human card"),Cards->GrantCard(Card))||
    !TestTrue(TEXT("Deploy human through the hand transaction"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2})))return false;
 auto Pieces=Cards->GetPieces();if(!TestEqual(TEXT("One deployed human"),Pieces.Num(),1))return false;
 auto* Piece=Pieces[0];const FGuid HeroId=Piece->RosterId;
 auto* Effect=Piece->FindComponentByClass<UEGHumanEntranceComponent>();auto* InitialRig=Rig(Piece);
 if(!TestNotNull(TEXT("Deployment starts human effect"),Effect)||!TestNotNull(TEXT("Formal appearance animation plays"),InitialRig))return false;
 auto* RevealAudio=EntranceSound(Piece,TEXT("HumanEntranceRevealAudio"));
 if(TestNotNull(TEXT("Reveal sound belongs to the deployed piece"),RevealAudio))
  TestEqual(TEXT("Reveal component uses its configured sound"),RevealAudio->GetSound(),RevealSound);
 TestTrue(TEXT("Entry uses the configured animation"),InitialRig->Definition==Entrance&&!InitialRig->bLoop);
 TestEqual(TEXT("No flying card widgets are created"),ShardCount(Piece),0);
 TestNull(TEXT("No merged portrait card is created"),HumanCard(Piece));
 if(!BurstCard(Piece))AddInfo(TEXT("Optional entrance light material was unavailable."));
 TestFalse(TEXT("Entrance animation remains visible"),InitialRig->IsHidden());
 if(!TestNotNull(TEXT("Configured entry voice component exists"),VoiceComponent(Piece)))return false;
 TestFalse(TEXT("Voice no longer waits for cards"),Piece->bPreparationEntranceVoicePending);
 TestEqual(TEXT("Voice starts exactly once"),Piece->PreparationVoicePlayCallsForTests,1);
 auto* PrepWidget=Piece->PreparationVitals?Piece->PreparationVitals->GetUserWidgetObject():nullptr;
 if(!TestNotNull(TEXT("Preparation vitals widget"),PrepWidget))return false;
 // Hold the Slate wrapper through GC; without a viewport the transient wrapper
 // can otherwise destruct/reconstruct while the test samples the layout.
 const TSharedRef<SWidget> PrepSlate=PrepWidget->TakeWidget();
 const float InitialTime=InitialRig->SourceTime;
 Tick(.2f);CollectGarbage(RF_NoFlags);
 TestTrue(TEXT("Effect survives GC"),IsValid(Effect)&&Piece->FindComponentByClass<UEGHumanEntranceComponent>()==Effect);
 if(TestNotNull(TEXT("Preparation badge component"),Piece->PreparationVitals.Get()))
 {
  // The source entrance rig can replace and hide Piece->Body with its own
  // visible skeletal component, so sample the actually rendered body.
  USkeletalMeshComponent* VisibleBody=nullptr;
  FName Head=NAME_None;
  TArray<AActor*> Visuals;Piece->GetAttachedActors(Visuals,true,true);Visuals.Insert(Piece,0);
  for(auto* Visual:Visuals)
  {
   if(!IsValid(Visual)||Visual->IsHidden())continue;
   TInlineComponentArray<USkeletalMeshComponent*> Meshes(Visual);
   for(auto* Mesh:Meshes)
   {
    if(!IsValid(Mesh)||!Mesh->IsVisible()||Mesh->bHiddenInGame||!Mesh->GetSkeletalMeshAsset()||Mesh==Piece->Outline)continue;
    for(int32 I=0;I<Mesh->GetNumBones();++I)
     if(Mesh->GetBoneName(I).ToString().EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))
     {Head=Mesh->GetBoneName(I);VisibleBody=Mesh;break;}
    if(VisibleBody)break;
   }
   if(VisibleBody)break;
  }
  if(TestNotNull(TEXT("Visible preparation body exposes a head bone"),VisibleBody))
  {
   TestTrue(TEXT("Preparation widget is visible during layout"),PrepWidget->IsVisible());
   if(auto* Layout=W->GetSubsystem<UEGVitalsLayoutSubsystem>())Layout->Tick(0.f);
   const FVector Expected=VisibleBody->GetBoneLocation(Head)+FVector(0,0,45.f);
   AddInfo(FString::Printf(TEXT("Visible preparation head %s on %s, expected anchor %s, actual %s"),
    *Head.ToString(),*VisibleBody->GetName(),*Expected.ToString(),*Piece->PreparationVitals->GetComponentLocation().ToString()));
   TestTrue(TEXT("Preparation badge anchor stays above visible head"),
    Piece->PreparationVitals->GetComponentLocation().Equals(Expected,.2f));
   TestTrue(TEXT("Preparation pivot provides additional screen-space clearance"),
    FMath::IsNearlyEqual(Piece->PreparationVitals->GetPivot().Y,1.45,.001));
  }
 }
 TestNotNull(TEXT("Reveal audio survives GC"),EntranceSound(Piece,TEXT("HumanEntranceRevealAudio")));
 TestTrue(TEXT("Entry animation advances while light plays"),InitialRig->SourceTime>InitialTime+.03f);
 TestTrue(TEXT("Reveal sound crosses its cue"),Effect->bRevealSoundTriggered);
 Cards->RefreshHeroPresentations();
 TestTrue(TEXT("Roster refresh does not restart entrance"),Piece->FindComponentByClass<UEGHumanEntranceComponent>()==Effect);
 TestEqual(TEXT("Roster refresh does not replay voice"),Piece->PreparationVoicePlayCallsForTests,1);
 Tick(1.3f);
 TestNull(TEXT("Effect cleans itself up after entrance"),Piece->FindComponentByClass<UEGHumanEntranceComponent>());
 TestEqual(TEXT("No shards after completion"),ShardCount(Piece),0);
 TestNull(TEXT("Light is removed on completion"),BurstCard(Piece));
 TestNull(TEXT("Reveal sound is removed on completion"),EntranceSound(Piece,TEXT("HumanEntranceRevealAudio")));
 TestEqual(TEXT("Voice still started only once"),Piece->PreparationVoicePlayCallsForTests,1);
 Tick(1.4f);
 TestTrue(TEXT("Entry timer naturally returns to idle"),Rig(Piece)!=InitialRig);
 if(!TestTrue(TEXT("Withdraw human"),Cards->TryWithdraw(Piece))||!TestTrue(TEXT("Redeploy human"),Cards->TryDeployReserve(HeroId,{2,2})))return false;
 Pieces=Cards->GetPieces();if(!TestEqual(TEXT("Redeployment produces one human"),Pieces.Num(),1))return false;
 Piece=Pieces[0];auto* ReplayedEffect=Piece->FindComponentByClass<UEGHumanEntranceComponent>();
 if(!TestNotNull(TEXT("Redeployment replays effect"),ReplayedEffect))return false;
 TestEqual(TEXT("Redeployment creates no flying cards"),ShardCount(Piece),0);
 TestEqual(TEXT("Redeployment starts voice once"),Piece->PreparationVoicePlayCallsForTests,1);
 Piece->SetBattlePresentation(true);
 TestNull(TEXT("Entering combat cancels effect"),Piece->FindComponentByClass<UEGHumanEntranceComponent>());
 TestNull(TEXT("Entering combat removes light"),BurstCard(Piece));
 TestNull(TEXT("Entering combat removes entrance voice"),VoiceComponent(Piece));
 TestNull(TEXT("Entering combat removes reveal sound"),EntranceSound(Piece,TEXT("HumanEntranceRevealAudio")));
 Tick(1.5f);TestNull(TEXT("Cancelled effect does not restart"),Piece->FindComponentByClass<UEGHumanEntranceComponent>());
 TestNull(TEXT("Cancelled entry timer does not recreate source rig"),Rig(Piece));
 Piece->SetBattlePresentation(false);
 if(!TestTrue(TEXT("Withdraw before fallback"),Cards->TryWithdraw(Piece)))return false;
 UEGHumanEntranceComponent::bForceInitializationFailureForTests=true;
 const bool bFallbackDeployed=Cards->TryDeployReserve(HeroId,{2,2});
 UEGHumanEntranceComponent::bForceInitializationFailureForTests=false;
 if(!TestTrue(TEXT("Deploy despite forced effect failure"),bFallbackDeployed))return false;
 Pieces=Cards->GetPieces();if(!TestEqual(TEXT("Fallback produces one human"),Pieces.Num(),1))return false;
 Piece=Pieces[0];
 TestNull(TEXT("Failed effect cleans its component"),Piece->FindComponentByClass<UEGHumanEntranceComponent>());
 if(auto* FallbackRig=Rig(Piece))TestTrue(TEXT("Fallback retains playing visible entrance"),!FallbackRig->IsHidden()&&FallbackRig->bPlaying);
 else AddError(TEXT("Fallback entrance rig is missing"));
 TestNotNull(TEXT("Fallback retains voice"),VoiceComponent(Piece));
 TestFalse(TEXT("Fallback does not leave voice pending"),Piece->bPreparationEntranceVoicePending);
 TestEqual(TEXT("Fallback starts voice exactly once"),Piece->PreparationVoicePlayCallsForTests,1);
 Piece->PlayPreparationEntrance(Entrance,Voice);
 TestFalse(TEXT("Default entrance voice is not deferred"),Piece->bPreparationEntranceVoicePending);
 TestEqual(TEXT("Default entrance voice still starts immediately"),Piece->PreparationVoicePlayCallsForTests,1);
 if(!TestTrue(TEXT("Withdraw before hide"),Cards->TryWithdraw(Piece))||!TestTrue(TEXT("Redeploy before hide"),Cards->TryDeployReserve(HeroId,{2,2})))return false;
 Pieces=Cards->GetPieces();if(!TestEqual(TEXT("Hide test produces one human"),Pieces.Num(),1))return false;
 Piece=Pieces[0];
 TestEqual(TEXT("Hide test starts voice once"),Piece->PreparationVoicePlayCallsForTests,1);
 Piece->SetActorHiddenInGame(true);
 TestNull(TEXT("Hiding removes voice component"),VoiceComponent(Piece));
 TestNull(TEXT("Hiding removes entrance effect"),Piece->FindComponentByClass<UEGHumanEntranceComponent>());
 Tick(1.5f);TestEqual(TEXT("Hidden voice never starts again"),Piece->PreparationVoicePlayCallsForTests,1);
 Piece->SetActorHiddenInGame(false);
 if(!TestTrue(TEXT("Withdraw before return"),Cards->TryWithdraw(Piece))||!TestTrue(TEXT("Redeploy before return"),Cards->TryDeployReserve(HeroId,{2,2})))return false;
 Pieces=Cards->GetPieces();if(!TestEqual(TEXT("Return test produces one human"),Pieces.Num(),1))return false;
 Piece=Pieces[0];TWeakObjectPtr<UAudioComponent> ReturnedVoice=VoiceComponent(Piece);
 if(!TestTrue(TEXT("Return human while entrance plays"),Cards->TryWithdraw(Piece)))return false;
 TestTrue(TEXT("Returned piece no longer owns a registered voice"),!ReturnedVoice.IsValid()||!ReturnedVoice->IsRegistered());
 Tick(1.5f);TestTrue(TEXT("Returned voice does not restart"),!ReturnedVoice.IsValid()||!ReturnedVoice->IsPlaying());
 AddInfo(TEXT("Controlled game-world deployment, audio scheduling and lifecycle test; no audible-output, visual, real-input, packaging, or multiplayer claim."));
 return true;
}
#endif
