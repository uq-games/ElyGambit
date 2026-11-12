#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"
#include "Components/EGRunResourceComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGSourceEffect.h"
#include "Materials/MaterialInterface.h"
#include "Tests/EGProductionTestAssets.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGResourceGraphTest,"ElyGambit.Resources.NestedSoftGraph",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGResourceGraphTest::RunTest(const FString&)
{
 TStrongObjectPtr<UEGHeroDefinition> Root(NewObject<UEGHeroDefinition>());
 auto* Form=NewObject<UEGHeroDefinition>(Root.Get());Root->EnhancedDefinition=Form;
 auto* Summon=NewObject<UEGHeroDefinition>(Root.Get());Form->SummonDefinition=Summon;
 const FSoftObjectPath Future(TEXT("/Game/ResourceTest/Future.Future"));
 Summon->Mesh=TSoftObjectPtr<USkeletalMesh>(Future);
 auto* P=NewObject<UEGHeroPresentationDefinition>(Root.Get());Root->PresentationDefinition=P;
 auto* Assembly=NewObject<UEGSourcePreviewDefinition>(Root.Get());P->SkillAssembly=Assembly;
 auto* FX=NewObject<UEGSourceEffectDefinition>(Root.Get());
 FEGSourcePreviewMotion Motion;FEGSourcePreviewEffect Binding;Binding.Effect=FX;Motion.Effects.Add(Binding);Assembly->Motions.Add(Motion);
 FEGSourceEffectTrack Track;const FSoftObjectPath EffectMesh(TEXT("/Game/ResourceTest/Effect.Effect"));
 Track.Mesh=TSoftObjectPtr<UStaticMesh>(EffectMesh);FX->Tracks.Add(Track);
 TSet<FSoftObjectPath> Paths;TSet<const UEGHeroDefinition*> Definitions;
 UEGRunResourceComponent::GatherLoadedGraph(Root.Get(),Paths,&Definitions);
 TestTrue(TEXT("Enhanced summon mesh discovered"),Paths.Contains(Future));
 TestTrue(TEXT("Nested assembly effect mesh discovered"),Paths.Contains(EffectMesh));
 TestEqual(TEXT("All three hero forms traversed"),Definitions.Num(),3);
 TestNull(TEXT("Discovery does not load missing assets"),Future.ResolveObject());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGResourceOwnershipTest,"ElyGambit.Resources.SharedResidencyRelease",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGResourceOwnershipTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 auto* A=NewObject<UEGHeroDefinition>(Owner);A->HeroId=TEXT("A");
 auto* B=NewObject<UEGHeroDefinition>(Owner);B->HeroId=TEXT("B");
 const FSoftObjectPath Shared(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 A->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(Shared);B->AttackMaterial=A->AttackMaterial;
 Loader->SetRequiredHeroes({A,B});Loader->Poll();
 TestTrue(TEXT("Headless scoped load ready"),Loader->IsReady());
 TestEqual(TEXT("Shared dependency has one handle"),Loader->GetHeldResourcePaths().Num(),1);
 Loader->SetRequiredHeroes({B});Loader->Poll();
 TestTrue(TEXT("Removing one owner retains shared dependency"),Loader->IsReady()&&Loader->GetHeldResourcePaths().Contains(Shared.ToString()));
 TestFalse(TEXT("Removed hero no longer ready"),Loader->IsHeroReady(A));
 Loader->SetRequiredHeroes({});Loader->Poll();
 TestTrue(TEXT("Zero owners release all holds"),Loader->IsReady()&&Loader->GetHeldResourcePaths().IsEmpty());
 Loader->SetRequiredHeroes({A});Loader->Poll();
 TestTrue(TEXT("Cached path graph can be reacquired"),Loader->IsHeroReady(A));
 Loader->ResetResources();TestTrue(TEXT("Exit cancels and clears requests"),Loader->GetHeldResourcePaths().IsEmpty()&&Loader->GetRequiredHeroPaths().IsEmpty());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGResourceRosterTest,"ElyGambit.Resources.ProductionRosterDependencies",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGResourceRosterTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 TStrongObjectPtr<UEGRunDefinition> ProductionRun(EGProductionTestAssets::ResolveRun(*this));
 auto* Run=ProductionRun.Get();
 if(!TestNotNull(TEXT("Production run"),Run))return false;
 TArray<UEGHeroDefinition*> Heroes;
 for(const auto& Card:Run->CardPool)if(Card)Heroes.AddUnique(Card->HeroDefinition);
 for(const auto& Stage:Run->Stages)for(const auto& Enemy:Stage.Enemies)Heroes.AddUnique(Enemy.Definition);
 for(auto* Hero:Heroes)
 {
  Loader->SetRequiredHeroes({Hero});Loader->Poll();
  TestTrue(Hero->GetName()+TEXT(" full dependency graph: ")+Loader->GetError(),Loader->IsHeroReady(Hero));
  const auto Held=Loader->GetHeldResourcePaths();TSet<FString> Unique(Held);
  TestEqual(Hero->GetName()+TEXT(" requests are deduplicated"),Held.Num(),Unique.Num());
 }
 Loader->ResetResources();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGRunPinnedResidencyTest,"ElyGambit.Resources.PinnedRunSurvivesIntentAndGC",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGRunPinnedResidencyTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 auto* A=NewObject<UEGHeroDefinition>(Owner);A->HeroId=TEXT("A");
 auto* B=NewObject<UEGHeroDefinition>(Owner);B->HeroId=TEXT("B");
 const FSoftObjectPath Shared(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 A->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(Shared);B->AttackMaterial=A->AttackMaterial;
 Loader->SetRunHeroes({A,B});Loader->Poll();
 TestTrue(TEXT("All run heroes ready in headless residency test"),Loader->IsHeroReady(A)&&Loader->IsHeroReady(B));
 TestTrue(TEXT("Scene initialization does not load full skill/effect graphs"),Loader->GetHeldResourcePaths().IsEmpty());
 LoadObject<UMaterialInterface>(nullptr,*Shared.ToString());
 Loader->RequestHeroResources(A);Loader->RequestHeroResources(B);Loader->Poll();
 TestTrue(TEXT("Background requests cannot disable presentation readiness"),Loader->IsReady());
 const auto Held=Loader->GetHeldResourcePaths();
 for(const TArray<UEGHeroDefinition*>& Intent:TArray<TArray<UEGHeroDefinition*>>{{A},{B},{}})
 {Loader->SetRequiredHeroes(Intent);Loader->Poll();TestTrue(TEXT("Intent changes cannot evict run dependencies"),Held==Loader->GetHeldResourcePaths());}
 CollectGarbage(RF_NoFlags);
 TestTrue(TEXT("World owner and handles retain both heroes through GC"),Loader->IsHeroReady(A)&&Loader->IsHeroReady(B)&&Shared.ResolveObject());
 Loader->ResetResources();
 TestTrue(TEXT("Explicit reset clears run state"),!Loader->IsReady()&&Loader->GetHeldResourcePaths().IsEmpty()&&Loader->GetPreviewCount()==0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDeferredCosmeticsTest,"ElyGambit.Resources.BackgroundDoesNotGateEntry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDeferredCosmeticsTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 auto* Hero=NewObject<UEGHeroDefinition>(Owner);Hero->HeroId=TEXT("DeferredCosmeticFixture");
 Hero->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/DeferredFixture/NotRequested.NotRequested")));
 Loader->SetRunHeroes({Hero});
 TestTrue(TEXT("Scene usable without resolving a cosmetic dependency"),Loader->IsReady()&&Loader->IsHeroReady(Hero));
 TestTrue(TEXT("No whole-run loading handles during scene setup"),Loader->GetHeldResourcePaths().IsEmpty());
 TestEqual(TEXT("Compatibility state is already ready"),Loader->GetStartupPhase(),EEGRunStartupPhase::Ready);
 Loader->ResetResources();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFormalRunGraphTest,"ElyGambit.Resources.FormalRunGraphAndResidency",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFormalRunGraphTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 auto* Run=NewObject<UEGRunDefinition>(Owner);
 auto* Card=NewObject<UEGCardDefinition>(Run);auto* Hero=NewObject<UEGHeroDefinition>(Card);Hero->HeroId=TEXT("LaterStage");Card->HeroDefinition=Hero;Run->CardPool.Add(Card);
 auto* Form=NewObject<UEGHeroDefinition>(Hero);Hero->EnhancedDefinition=Form;
 const FSoftObjectPath Material(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
 Form->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(Material);
 Loader->SetRunHeroesDeferred({},Run);
 TestFalse(TEXT("Formal entry cannot be ready before discovery"),Loader->HasCompletedStartupPreload());
 for(int32 I=0;I<8&&!Loader->HasCompletedStartupPreload();++I){Loader->Poll();FlushAsyncLoading();}
 TestTrue(TEXT("Run root discovers off-lineup form resources"),Loader->GetHeldResourcePaths().Contains(Material.ToString()));
 TestTrue(TEXT("Resolved run enters ready state"),Loader->HasCompletedStartupPreload()&&Loader->GetError().IsEmpty());
 CollectGarbage(RF_NoFlags);
 TestNotNull(TEXT("Resources survive GC until world release"),Material.ResolveObject());
 Loader->ResetResources();
 TestTrue(TEXT("Reset removes holds and readiness"),Loader->GetHeldResourcePaths().IsEmpty()&&!Loader->HasCompletedStartupPreload());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFormalRunFailureTest,"ElyGambit.Resources.FormalFailureAndRetry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFormalRunFailureTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 auto* Owner=W->SpawnActor<AActor>();auto* Loader=NewObject<UEGRunResourceComponent>(Owner);Owner->AddInstanceComponent(Loader);Loader->RegisterComponent();
 auto* Hero=NewObject<UEGHeroDefinition>(Owner);Hero->HeroId=TEXT("RequiredEffectFixture");
 const FSoftObjectPath Missing(TEXT("/Game/ResourceLoadingTest/MissingEffect.MissingEffect"));
 Hero->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(Missing);
 // Expected missing-package diagnostics are confined to this deliberately nonexistent fixture.
 AddExpectedError(TEXT("/Game/ResourceLoadingTest/MissingEffect"),EAutomationExpectedErrorFlags::Contains,0);
 Loader->SetRunHeroesDeferred({Hero});
 for(int32 I=0;I<8;++I){Loader->Poll();FlushAsyncLoading();}
 TestTrue(TEXT("A missing configured effect is not optional during opening"),Loader->GetError().Contains(Missing.ToString()));
 TestFalse(TEXT("Failed preload cannot enter gameplay"),Loader->HasCompletedStartupPreload());
 TestEqual(TEXT("Failure exposed to UI"),Loader->GetStartupPhase(),EEGRunStartupPhase::Failed);
 Hero->AttackMaterial=TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
 Loader->SetRunHeroesDeferred({Hero});
 for(int32 I=0;I<8&&!Loader->HasCompletedStartupPreload();++I){Loader->Poll();FlushAsyncLoading();}
 TestTrue(TEXT("Retry clears failure and reacquires resources"),Loader->GetError().IsEmpty()&&Loader->HasCompletedStartupPreload());
 return true;
}
#endif
