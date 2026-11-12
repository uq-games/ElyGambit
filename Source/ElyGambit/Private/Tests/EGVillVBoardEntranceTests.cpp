#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
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
#include "Presentation/EGSourceCharacterPreview.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Sound/SoundBase.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVBoardEntrance,
 "ElyGambit.Presentation.VillVFormalBoardEntranceLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVBoardEntrance::RunTest(const FString&)
{
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV.DA_Card_VillV"));
 if(!TestNotNull(TEXT("Formal VillV card"),Card)||!TestNotNull(TEXT("Card hero"),Card->HeroDefinition.Get()))return false;
 auto* Hero=Card->HeroDefinition.Get();auto* P=Hero->PresentationDefinition.Get();
 if(!TestTrue(TEXT("Hero uses split presentation"),Hero->bUseSplitConfiguration&&P))return false;
 TestEqual(TEXT("Formal presentation asset"),P->GetPathName(),FString(TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV_Presentation.DA_VillV_Presentation")));
 auto* Entrance=P->PreparationEntranceAssembly.LoadSynchronous();auto* Voice=P->PreparationEntranceVoice.LoadSynchronous();
 if(!TestNotNull(TEXT("Entry assembly bound and loadable"),Entrance)||!TestNotNull(TEXT("Entry voice bound and loadable"),Voice))return false;
 if(!TestTrue(TEXT("One positive-duration entry motion"),Entrance->Motions.Num()==1&&Entrance->Motions[0].Duration>.05f))return false;
 auto* Body=Entrance->Motions[0].Parts.FindByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Body");});
 if(!TestNotNull(TEXT("Entry body part"),Body)||!TestNotNull(TEXT("Entry body mesh"),Body->Mesh.Get())||!TestNotNull(TEXT("Entry body animation"),Body->Animation.Get()))return false;
 auto* BaseMesh=P->Mesh.LoadSynchronous();
 if(!TestNotNull(TEXT("Formal base mesh"),BaseMesh))return false;
 TestTrue(TEXT("Entry body skeleton matches animation and formal body"),Body->Mesh->GetSkeleton()&&Body->Mesh->GetSkeleton()==Body->Animation->GetSkeleton()&&Body->Mesh->GetSkeleton()==BaseMesh->GetSkeleton());
 TestTrue(TEXT("Entry body animates once"),!Body->bLoop&&Body->bVisible);

 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);if(!Base)return false;
 auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,*this))return false;
 // Non-shop fixture allows withdrawal for all heroes; the formal hero and
 // deployment/presentation implementations remain unchanged.
 Run->bRoundShopRules=false;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto Tick=[&](float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);}};
 auto Rig=[&](AActor* Owner)->AEGCombatVisual*{for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Owner&&!It->IsActorBeingDestroyed())return *It;return nullptr;};
 auto VoiceComponent=[&](AActor* Owner)->UAudioComponent*
 {
  TInlineComponentArray<UAudioComponent*> Components(Owner);
  for(auto* Audio:Components)if(IsValid(Audio)&&Audio->IsRegistered()&&Audio->GetSound()==Voice)return Audio;
  return nullptr;
 };
 if(!TestTrue(TEXT("Grant VillV card"),Cards->GrantCard(Card))||!TestTrue(TEXT("Deploy VillV card"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2})))return false;
 auto Pieces=Cards->GetPieces();if(!TestEqual(TEXT("One deployed hero"),Pieces.Num(),1))return false;
 auto* Piece=Pieces[0];const FGuid HeroId=Piece->RosterId;
 auto* First=Rig(Piece);
 if(!TestNotNull(TEXT("Deployment creates source rig"),First))return false;
 TestTrue(TEXT("Deployment plays formal entry once"),First->Definition==Entrance&&!First->bLoop&&First->bPlaying);
 TestTrue(TEXT("Voice component owns configured sound"),VoiceComponent(Piece)!=nullptr);
 const float Start=First->SourceTime;Tick(FMath::Min(.1f,Entrance->Motions[0].Duration*.25f));
 TestTrue(TEXT("Entry source clock advances"),Rig(Piece)==First&&First->SourceTime>Start);
 Cards->RefreshHeroPresentations();TestTrue(TEXT("Roster refresh does not restart entry"),Rig(Piece)==First&&First->SourceTime>Start);
 Tick(Entrance->Motions[0].Duration+.1f);
 auto* Idle=Rig(Piece);auto* ExpectedIdle=!P->PreparationIdleAssembly.IsNull()?P->PreparationIdleAssembly.LoadSynchronous():!P->IdleAssemblies.IsEmpty()?P->IdleAssemblies[0].LoadSynchronous():nullptr;
 TestTrue(TEXT("Entry completes to configured idle or base mesh"),ExpectedIdle?(Idle&&Idle->Definition==ExpectedIdle&&Idle->bLoop):(!Idle&&Piece->Body->GetSkeletalMeshAsset()==BaseMesh));
 if(!TestTrue(TEXT("Withdraw deployed VillV"),Cards->TryWithdraw(Piece)))return false;
 TestNull(TEXT("Withdrawal destroys entry voice component"),VoiceComponent(Piece));
 if(!TestTrue(TEXT("Redeploy same VillV"),Cards->TryDeployReserve(HeroId,{2,2})))return false;
 Pieces=Cards->GetPieces();if(!TestEqual(TEXT("Redeploy produces one hero"),Pieces.Num(),1))return false;
 Piece=Pieces[0];auto* Replay=Rig(Piece);
 TestTrue(TEXT("Redeploy plays entry anew"),Replay&&Replay->Definition==Entrance&&!Replay->bLoop);
 TestNotNull(TEXT("Redeploy binds voice again"),VoiceComponent(Piece));
 if(!TestTrue(TEXT("Start combat during entry"),GM->EnterCombat()))return false;
 TestNull(TEXT("Combat interrupts preparation entry"),Rig(Piece));
 TestNull(TEXT("Combat stops and destroys entry voice component"),VoiceComponent(Piece));
 Tick(Entrance->Motions[0].Duration+.1f);
 TestNull(TEXT("Cancelled timer does not recreate preparation entry"),Rig(Piece));
 AddInfo(TEXT("Controlled game-world test: sound binding/component lifetime only; no claim of audible playback, visual acceptance or natural battle."));
 return true;
}
#endif
