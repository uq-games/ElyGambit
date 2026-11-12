#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/StrongObjectPtr.h"
#include "Placement/EGPlacementVisuals.h"
#include "Data/EGHeroDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGCombatVisual.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPreviewIdentity,
 "ElyGambit.Placement.PreviewIdentityAcrossHeroes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPreviewIdentity::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* Pardo=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"));
 auto* Griseo=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Griseo"));
 if(!TestNotNull(TEXT("Pardo definition"),Pardo)||!TestNotNull(TEXT("Griseo definition"),Griseo))return false;
 auto* Preview=W->SpawnActor<AEGPlacementPreview>();
 for(auto* D:TArray<UEGHeroDefinition*>{Griseo,Pardo,Griseo,Pardo})
 {
  Preview->ApplyHeroDefinition(D);
  // The source standby may intentionally use another mesh of the same hero
  // (Griseo's resident knight). A fresh preview is the correct visual oracle.
  auto* Fresh=W->SpawnActor<AEGPlacementPreview>();Fresh->ApplyHeroDefinition(D);
  auto* Expected=Fresh->Body->GetSkeletalMeshAsset();Fresh->Destroy();
  TestTrue(TEXT("Visual definition follows requested hero"),Preview->HeroVisualDefinition==D);
  TestTrue(TEXT("Reused preview matches a fresh preview of the requested hero"),Preview->Body->GetSkeletalMeshAsset()==Expected);
  const int32 Revision=Preview->GetAppearanceRevision();
  auto* Material=Preview->Body->GetMaterial(0);
  Preview->ApplyHeroDefinition(D);
  TestEqual(TEXT("Repeated definition does not rebuild presentation"),Preview->GetAppearanceRevision(),Revision);
  TestTrue(TEXT("Repeated definition retains instance material"),Material==Preview->Body->GetMaterial(0));
  int32 Children=0;
  for(TActorIterator<AEGCombatVisual> I(W);I;++I)if(I->GetOwner()==Preview&&!I->IsActorBeingDestroyed())++Children;
  TestTrue(TEXT("At most one live preparation owner after switching"),Children<=1);
  Preview->HidePreview();Preview->ShowAt(FVector::ZeroVector,true);
  TestTrue(TEXT("Hide and show cannot restore an earlier hero"),Preview->Body->GetSkeletalMeshAsset()==Expected);
  Preview->SetBattlePresentation(true);
  TestTrue(TEXT("Releasing standby restores current hero's base mesh"),Preview->Body->GetSkeletalMeshAsset()==D->GetMesh().LoadSynchronous());
  Preview->SetBattlePresentation(false);
 }
 Preview->Destroy();
 for(TActorIterator<AEGCombatVisual> I(W);I;++I)if(I->GetOwner()==Preview)TestTrue(TEXT("Destroy preview releases assembly"),I->IsActorBeingDestroyed());
 return true;
}
#endif
