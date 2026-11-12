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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHuaBoardEntrance,
 "ElyGambit.Presentation.HuaFormalBoardEntranceLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHuaBoardEntrance::RunTest(const FString&)
{
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua.DA_Card_Hua"));
 if(!TestNotNull(TEXT("Formal Hua card"),Card)||!TestNotNull(TEXT("Card hero"),Card->HeroDefinition.Get()))return false;
 auto* Hero=Card->HeroDefinition.Get();auto* P=Hero->PresentationDefinition.Get();
 if(!TestTrue(TEXT("Hero uses split presentation"),Hero->bUseSplitConfiguration&&P))return false;
 TestEqual(TEXT("Formal Hua hero"),Hero->GetPathName(),FString(TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua.DA_Hua")));
 TestEqual(TEXT("Formal presentation"),P->GetPathName(),FString(TEXT("/Game/Blueprints/Characters/FuHua/Data/CombatPresentation20261030/DA_Hua_Presentation.DA_Hua_Presentation")));
 auto* Entrance=P->PreparationEntranceAssembly.LoadSynchronous();auto* Voice=P->PreparationEntranceVoice.LoadSynchronous();
 if(!TestNotNull(TEXT("Entry assembly bound and loadable"),Entrance)||!TestNotNull(TEXT("Entry voice bound and loadable"),Voice))return false;
 TestEqual(TEXT("Formal entry asset"),Entrance->GetPathName(),FString(TEXT("/Game/Blueprints/Characters/FuHua/Data/Entrance20261103/DA_Hua_BoardEntrance.DA_Hua_BoardEntrance")));
 TestEqual(TEXT("Formal entry voice"),Voice->GetPathName(),FString(TEXT("/Game/Assets/Characters/FuHua/Audio/Entrance20261103/VO_Hua_SwitchIn.VO_Hua_SwitchIn")));
 if(!TestTrue(TEXT("One positive-duration entry motion"),Entrance->Motions.Num()==1&&Entrance->Motions[0].Duration>.05f))return false;
 const auto& Motion=Entrance->Motions[0];
 auto* Body=Motion.Parts.FindByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Body");});
 if(!TestNotNull(TEXT("Entry body part"),Body)||!TestNotNull(TEXT("Entry body mesh"),Body->Mesh.Get())||!TestNotNull(TEXT("Entry body animation"),Body->Animation.Get()))return false;
 auto* BaseMesh=P->Mesh.LoadSynchronous();if(!TestNotNull(TEXT("Formal base mesh"),BaseMesh))return false;
 TestEqual(TEXT("Entry uses the formal body mesh"),Body->Mesh.Get(),BaseMesh);
 TestEqual(TEXT("Formal body mesh path"),BaseMesh->GetPathName(),FString(TEXT("/Game/Assets/Characters/FuHua/Meshes/SK_FuHua_Source.SK_FuHua_Source")));
 TestEqual(TEXT("Private entry animation"),Body->Animation->GetPathName(),FString(TEXT("/Game/Assets/Characters/FuHua/Entrance20261103/Animations/A_Hua_Appear_BoardIdleBlend.A_Hua_Appear_BoardIdleBlend")));
 TestTrue(TEXT("Body skeleton matches entry animation"),BaseMesh->GetSkeleton()&&BaseMesh->GetSkeleton()==Body->Animation->GetSkeleton());
 TestTrue(TEXT("Formal body includes Hair bone"),BaseMesh->GetRefSkeleton().FindBoneIndex(TEXT("Hair"))!=INDEX_NONE);
 TestTrue(TEXT("Entry body runs once for the animation duration"),Body->bVisible&&!Body->bLoop&&FMath::IsNearlyZero(Body->StartTime)&&FMath::IsNearlyEqual(Motion.Duration,Body->Animation->GetPlayLength(),.001f)&&FMath::IsNearlyEqual(Body->Duration,Motion.Duration,.001f));
 TestTrue(TEXT("Entry does not add separate gauntlet parts"),Motion.Parts.Num()==1&&Hero->GetWeapons().IsEmpty());

 if(!GEngine){AddError(TEXT("Engine unavailable"));return false;}
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 if(!TestNotNull(TEXT("Fixture world"),W))return false;
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Fixture game mode"),GM))return false;
 GM->UnitClass=AEGDemoCombatUnit::StaticClass();
 auto* Base=EGProductionTestAssets::ResolveRun(*this);if(!Base)return false;
 auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,*this))return false;
 // Controlled non-shop fixture allows withdrawal, while using the actual hero,
 // presentation and production deployment code.
 Run->bRoundShopRules=false;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 auto* Cards=W->SpawnActor<AEGCardManager>();auto* CombatBoard=W->SpawnActor<AEGBoard>();
 if(!Scene||!Grid||!Cards||!CombatBoard){AddError(TEXT("Fixture actors could not spawn"));return false;}
 Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
 Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=CombatBoard;CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto Tick=[&](float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);}};
 auto Rig=[&](AActor* Owner)->AEGCombatVisual*{for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Owner&&!It->IsActorBeingDestroyed())return *It;return nullptr;};
 auto VoiceComponent=[&](AActor* Owner)->UAudioComponent*
 {
  TInlineComponentArray<UAudioComponent*> Components(Owner);
  for(auto* Audio:Components)if(IsValid(Audio)&&Audio->IsRegistered()&&Audio->GetSound()==Voice)return Audio;
  return nullptr;
 };
 if(!TestTrue(TEXT("Grant Hua card"),Cards->GrantCard(Card))||!TestTrue(TEXT("Deploy Hua card"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2})))return false;
 auto Pieces=Cards->GetPieces();if(!TestEqual(TEXT("One deployed hero"),Pieces.Num(),1))return false;
 auto* Piece=Pieces[0];const FGuid HeroId=Piece->RosterId;auto* First=Rig(Piece);
 if(!TestNotNull(TEXT("Deployment creates source rig"),First))return false;
 TestTrue(TEXT("Deployment plays formal entry once"),First->Definition==Entrance&&!First->bLoop&&First->bPlaying);
 TestTrue(TEXT("Entry drives deployed body"),First->BodyMesh==Piece->Body&&First->bDriveBodyPose);
 TestNotNull(TEXT("Entry voice component uses configured sound"),VoiceComponent(Piece));
 const float Start=First->SourceTime;Tick(FMath::Min(.1f,Motion.Duration*.25f));
 TestTrue(TEXT("Entry source clock advances"),Rig(Piece)==First&&First->SourceTime>Start);
 Cards->RefreshHeroPresentations();TestTrue(TEXT("Roster refresh does not restart entry"),Rig(Piece)==First&&First->SourceTime>Start);
 Tick(Motion.Duration+.1f);
 auto* Idle=Rig(Piece);auto* ExpectedIdle=!P->PreparationIdleAssembly.IsNull()?P->PreparationIdleAssembly.LoadSynchronous():!P->IdleAssemblies.IsEmpty()?P->IdleAssemblies[0].LoadSynchronous():nullptr;
 TestTrue(TEXT("Entry returns to configured idle or base body"),ExpectedIdle?(Idle&&Idle->Definition==ExpectedIdle&&Idle->bLoop):(!Idle&&Piece->Body->GetSkeletalMeshAsset()==BaseMesh));
 if(!TestTrue(TEXT("Withdraw deployed Hua"),Cards->TryWithdraw(Piece)))return false;
 TestNull(TEXT("Withdrawal destroys entry voice component"),VoiceComponent(Piece));
 if(!TestTrue(TEXT("Redeploy Hua"),Cards->TryDeployReserve(HeroId,{2,2})))return false;
 Pieces=Cards->GetPieces();if(!TestEqual(TEXT("Redeploy produces one hero"),Pieces.Num(),1))return false;
 Piece=Pieces[0];auto* Replay=Rig(Piece);
 TestTrue(TEXT("Redeploy plays entry anew"),Replay&&Replay->Definition==Entrance&&!Replay->bLoop);
 TestNotNull(TEXT("Redeploy binds voice again"),VoiceComponent(Piece));
 if(!TestTrue(TEXT("Start combat during entry"),GM->EnterCombat()))return false;
 TestNull(TEXT("Combat interrupts entry"),Rig(Piece));
 TestNull(TEXT("Combat stops and destroys entry voice"),VoiceComponent(Piece));
 Tick(Motion.Duration+.1f);
 TestNull(TEXT("Cancelled timer does not recreate preparation entry"),Rig(Piece));
 AddInfo(TEXT("Controlled game-world test: audio binding and component lifetime only; no claim of audible playback, visual acceptance or natural battle."));
 return true;
}
#endif
