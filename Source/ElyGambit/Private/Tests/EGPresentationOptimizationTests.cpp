#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGRunResourceComponent.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceDistortionPass.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSourceEffectLifetimeTest,
 "ElyGambit.PresentationOptimization.ActionLifetimeAndDistortionFailure",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSourceEffectLifetimeTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 // UWorld::BeginPlay only dispatches actor BeginPlay through a GameMode.
 // Without one the lifespan timer advances, but spawned Actor ticks never run.
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
 FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestTrue(TEXT("Fixture world dispatched BeginPlay"),W->HasBegunPlay()))return false;
 auto* Unit=W->SpawnActor<AEGChessUnit>();
 if(!TestNotNull(TEXT("Unit"),Unit))return false;
 auto* Definition=NewObject<UEGHeroDefinition>(Unit);
 if(!TestTrue(TEXT("Generic unit initialized"),Unit->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* BattleOwner=W->SpawnActor<AActor>();
 auto* Battle=NewObject<UEGBattleContextComponent>(BattleOwner);BattleOwner->AddInstanceComponent(Battle);Battle->RegisterComponent();Battle->Start({});
 auto* Skill=Unit->GetHeroSkill();Skill->Configure(Battle,1,false);Skill->ActionId=FGuid::NewGuid();
 auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
 auto* Material=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 if(!TestNotNull(TEXT("Fixture mesh"),Mesh)||!TestNotNull(TEXT("Fixture material"),Material))return false;
 auto* FX=NewObject<UEGSourceEffectDefinition>(Unit);FEGSourceEffectTrack Track;Track.Mesh=Mesh;Track.Material=Material;
 Track.Duration=1.f;FEGSourceEffectSample Sample;Sample.Transform=FTransform::Identity;Track.Samples.Add(Sample);FX->Tracks.Add(Track);
 auto* Action=AEGSourceEffect::Spawn(Skill,Unit->GetMesh(),FX,1.f,false);
 auto* Independent=AEGSourceEffect::Spawn(Skill,Unit->GetMesh(),FX,1.f,true);
 if(!TestNotNull(TEXT("Action source effect"),Action)||!TestNotNull(TEXT("Round-bound source effect"),Independent))return false;
 if(!TestTrue(TEXT("Action source effect received BeginPlay"),Action->HasActorBegunPlay()))return false;
 auto Tick=[&](int32 Count){for(int32 I=0;I<Count;++I){++GFrameCounter;W->Tick(LEVELTICK_All,.1f);}};
 Tick(2);const float Before=Action->GetElapsedTime();
 if(!TestTrue(TEXT("Action effect advances on battle time"),Before>.1f))return false;
 Tick(2);
 TestTrue(TEXT("Action effect keeps advancing naturally"),Action->GetElapsedTime()>Before+.1f&&!Action->IsActorBeingDestroyed());
 Skill->ActionId=FGuid::NewGuid();Tick(1);
 TestTrue(TEXT("Changed action token destroys the old effect"),Action->IsActorBeingDestroyed());
 Tick(7);
 TestTrue(TEXT("Independent effect expires on world time"),Independent->IsActorBeingDestroyed());
 auto* Natural=AEGSourceEffect::Spawn(Skill,Unit->GetMesh(),FX,1.f,false);
 if(!TestNotNull(TEXT("Natural-lifetime action effect"),Natural))return false;
 Tick(12);
 TestTrue(TEXT("Action effect also expires after its own source duration"),Natural->IsActorBeingDestroyed());

 auto* Bad=NewObject<UEGSourceEffectDefinition>(Unit);Track.bSourceDistortion=true;Bad->Tracks.Add(Track);
 auto* Rejected=AEGSourceEffect::Spawn(Skill,Unit->GetMesh(),Bad,1.f,false);
 if(!TestNotNull(TEXT("Effect survives a rejected distortion track"),Rejected))return false;
 TestEqual(TEXT("Invalid composite is diagnosed and suppressed"),Rejected->GetRejectedDistortionTrackCount(),1);
 TInlineComponentArray<UStaticMeshComponent*> Parts(Rejected);
 if(!TestEqual(TEXT("One test track"),Parts.Num(),1))return false;
 Rejected->Tick(.1f);
 TestFalse(TEXT("Rejected signed distortion is never drawn as scene color"),Parts[0]->IsVisible());
 TestTrue(TEXT("Rejected distortion primitive remains capture-only"),Parts[0]->bVisibleInSceneCaptureOnly);
 Battle->Stop();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGIncrementalResourceGraphTest,
 "ElyGambit.PresentationOptimization.BackgroundGraphProcessesNewRootsOnly",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGIncrementalResourceGraphTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 auto* A=NewObject<UEGHeroDefinition>(Owner);auto* B=NewObject<UEGHeroDefinition>(Owner);
 Loader->SetRunHeroes({A,B});Loader->RequestHeroResources(A);Loader->Poll();
 TestTrue(TEXT("First root completed without a gameplay loading gate"),Loader->IsBackgroundReady()&&Loader->IsReady());
 TestEqual(TEXT("First root traversed once"),Loader->GetBackgroundGraphTraversalCount(),uint64(1));
 Loader->RequestHeroResources(B);Loader->Poll();
 TestEqual(TEXT("Completed first root is not traversed again"),Loader->GetBackgroundGraphTraversalCount(),uint64(2));
 Loader->RequestHeroResources(A);Loader->Poll();
 TestEqual(TEXT("Duplicate intent does not retraverse"),Loader->GetBackgroundGraphTraversalCount(),uint64(2));
 Loader->ResetResources();
 TestEqual(TEXT("World reset clears graph diagnostics"),Loader->GetBackgroundGraphTraversalCount(),uint64(0));
 return true;
}

namespace
{
struct FEGAsyncGraphFixture
{
 TStrongObjectPtr<UGameInstance> GameInstance;
 UWorld* World=nullptr;
 UEGRunResourceComponent* Loader=nullptr;
 UEGHeroDefinition *First=nullptr,*Second=nullptr,*Later=nullptr,*Missing=nullptr;
 // This real source effect contains soft mesh/material paths. Loading the
 // definition exposes a second layer that must itself be requested and held.
 const FSoftObjectPath EffectPath=FSoftObjectPath(TEXT("/Game/Assets/Characters/Mobius/Effects/Definitions/FX_Avatar_Effect_Mobius_C1_UltraAtmosphere_274c6d64a0eb.FX_Avatar_Effect_Mobius_C1_UltraAtmosphere_274c6d64a0eb"));
 const FSoftObjectPath MissingPath=FSoftObjectPath(TEXT("/Game/QA/IntentionallyMissingGraphAsset.IntentionallyMissingGraphAsset"));
 FEGAsyncGraphFixture():GameInstance(NewObject<UGameInstance>(GEngine))
 {
  GameInstance->InitializeStandalone();World=GameInstance->GetWorld();if(!World)return;
  auto* Owner=World->SpawnActor<AActor>();if(!Owner)return;
  Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
  auto* Presentation=NewObject<UEGHeroPresentationDefinition>(Owner);
  Presentation->SummonImpactEffect=TSoftObjectPtr<UEGSourceEffectDefinition>(EffectPath);
  First=NewObject<UEGHeroDefinition>(Owner);Second=NewObject<UEGHeroDefinition>(Owner);
  Later=NewObject<UEGHeroDefinition>(Owner);Missing=NewObject<UEGHeroDefinition>(Owner);
  First->PresentationDefinition=Presentation;Second->PresentationDefinition=Presentation;Later->PresentationDefinition=Presentation;
  Missing->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(MissingPath);
  Loader->SetRunHeroes({First,Second,Later,Missing});
 }
 ~FEGAsyncGraphFixture()
 {
  if(World){World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GameInstance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
 }
};

class FEGAsyncGraphCommand final : public IAutomationLatentCommand
{
 TSharedRef<FEGAsyncGraphFixture> Fixture;
 FAutomationTestBase* Test;
 double Deadline=FPlatformTime::Seconds()+45.;
 int32 Phase=0;
 uint64 CompletedTraversals=0;
 bool bEffectInitiallyLoaded=false;
public:
 FEGAsyncGraphCommand(TSharedRef<FEGAsyncGraphFixture> InFixture,FAutomationTestBase* InTest):Fixture(InFixture),Test(InTest){}
 virtual bool Update() override
 {
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Background resource graph did not converge within 45 seconds"));return true;}
  auto* Loader=Fixture->Loader;
  if(Phase==0)
  {
   bEffectInitiallyLoaded=Fixture->EffectPath.ResolveObject()!=nullptr;
   Test->AddInfo(bEffectInitiallyLoaded?TEXT("Shared source effect was already resident; cold loading not established."):TEXT("Shared source effect was not resident when first requested."));
   Loader->RequestHeroResources(Fixture->First);Loader->RequestHeroResources(Fixture->Second);
   Test->TestTrue(TEXT("Two requested roots do not block gameplay"),Loader->IsReady());
   Phase=1;
  }
  Loader->Poll();
  if(!Loader->IsBackgroundReady())return false;
  if(Phase==1)
  {
   TSet<FSoftObjectPath> Expected,Optional;
   UEGRunResourceComponent::GatherLoadedGraph(Fixture->First,Expected,nullptr,&Optional);
   const auto Held=Loader->GetHeldResourcePaths();
   Test->TestNotNull(TEXT("Shared source effect loaded"),Fixture->EffectPath.ResolveObject());
   Test->TestTrue(TEXT("Loaded source effect exposes nested soft dependencies"),Expected.Num()>1);
   Test->TestTrue(TEXT("Shared source effect is retained"),Held.Contains(Fixture->EffectPath.ToString()));
   if(!bEffectInitiallyLoaded)Test->TestTrue(TEXT("Cold roots were revisited after their shared soft effect loaded"),Loader->GetBackgroundGraphTraversalCount()>2);
   for(const auto& Path:Expected)
   {
    Test->TestTrue(FString::Printf(TEXT("Discovered dependency held: %s"),*Path.ToString()),Held.Contains(Path.ToString()));
    if(!Optional.Contains(Path))Test->TestNotNull(FString::Printf(TEXT("Required dependency loaded: %s"),*Path.ToString()),Path.ResolveObject());
   }
   Test->TestEqual(TEXT("Shared dependencies have unique handles"),Held.Num(),TSet<FString>(Held).Num());
   Test->TestTrue(TEXT("Successful graph has no resource error"),Loader->GetError().IsEmpty());
   CompletedTraversals=Loader->GetBackgroundGraphTraversalCount();
   Loader->RequestHeroResources(Fixture->Later);Phase=2;
   return false;
  }
  if(Phase==2)
  {
   Test->TestEqual(TEXT("Already completed roots are not revisited for a later shared root"),Loader->GetBackgroundGraphTraversalCount(),CompletedTraversals+1);
   CompletedTraversals=Loader->GetBackgroundGraphTraversalCount();
   Loader->RequestHeroResources(Fixture->First);Loader->Poll();
   Test->TestEqual(TEXT("Repeated intent does not rescan a completed root"),Loader->GetBackgroundGraphTraversalCount(),CompletedTraversals);
   Test->AddExpectedErrorPlain(TEXT("IntentionallyMissingGraphAsset"),EAutomationExpectedErrorFlags::Contains,-1);
   Loader->RequestHeroResources(Fixture->Missing);Phase=3;
   return false;
  }
  Test->TestTrue(TEXT("Missing dependency is diagnosed"),Loader->GetError().Contains(TEXT("IntentionallyMissingGraphAsset")));
  Test->TestTrue(TEXT("Missing cosmetic resource does not block gameplay"),Loader->IsReady());
  Test->TestTrue(TEXT("Existing shared resource remains held after failure"),Loader->GetHeldResourcePaths().Contains(Fixture->EffectPath.ToString()));
  Loader->ResetResources();
  Test->TestTrue(TEXT("Reset clears handles, roots and error"),Loader->GetHeldResourcePaths().IsEmpty()&&Loader->GetRequiredHeroPaths().IsEmpty()&&Loader->GetError().IsEmpty());
  Test->TestEqual(TEXT("Reset clears traversal count"),Loader->GetBackgroundGraphTraversalCount(),uint64(0));
  return true;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAsyncResourceClosureTest,
 "ElyGambit.PresentationOptimization.BackgroundGraphSharedAsyncClosure",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAsyncResourceClosureTest::RunTest(const FString&)
{
 const auto Fixture=MakeShared<FEGAsyncGraphFixture>();
 if(!TestNotNull(TEXT("Fixture world"),Fixture->World)||!TestNotNull(TEXT("Resource component"),Fixture->Loader))return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEGAsyncGraphCommand(Fixture,this));
 return true;
}
#endif
