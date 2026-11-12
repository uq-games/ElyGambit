#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Actor/EGArtPiece.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/Material.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPreparationEntranceOutline,
 "ElyGambit.Presentation.PreparationEntranceOutlineLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPreparationEntranceOutline::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* Source=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua.DA_Hua"));
 if(!TestNotNull(TEXT("Hua fixture"),Source)||!TestNotNull(TEXT("Hua presentation"),Source->PresentationDefinition.Get()))return false;
 auto* Hero=DuplicateObject<UEGHeroDefinition>(Source,W);Hero->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(Source->PresentationDefinition.Get(),Hero);
 auto* P=Hero->PresentationDefinition.Get();P->PreparationIdleAssembly.Reset();P->IdleAssemblies.Reset();
 auto* Piece=W->SpawnActor<AEGArtPiece>();Piece->OutlineMaterial=UMaterial::GetDefaultMaterial(MD_Surface);Piece->ApplyHeroDefinition(Hero);
 if(!TestTrue(TEXT("Fixture has base mesh and visible outline"),Piece->CharacterMesh&&Piece->Outline->IsVisible()))return false;
 auto* Entrance=NewObject<UEGSourcePreviewDefinition>(Piece);FEGSourcePreviewMotion Motion;Motion.Duration=.1f;Entrance->Motions.Add(Motion);
 auto Tick=[&](){for(int32 I=0;I<6;++I){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}};
 Piece->PlayPreparationEntrance(Entrance);
 TestFalse(TEXT("Entry hides base outline while source rig owns body"),Piece->Outline->IsVisible());
 Tick();TestTrue(TEXT("Completed entry restores base outline with no idle rig"),Piece->Outline->IsVisible());
 Piece->PlayPreparationEntrance(Entrance);Piece->SetBattlePresentation(true);
 TestTrue(TEXT("Combat interruption restores outline"),Piece->Outline->IsVisible());
 Tick();TestTrue(TEXT("Cancelled entry timer does not hide it again"),Piece->Outline->IsVisible());
 Piece->SetBattlePresentation(false);
 auto* Idle=NewObject<UEGSourcePreviewDefinition>(Piece);Idle->Motions.Add(Motion);P->PreparationIdleAssembly=Idle;
 Piece->SetBattlePresentation(true);Piece->SetBattlePresentation(false);
 TestFalse(TEXT("Preparation idle rig keeps outline hidden"),Piece->Outline->IsVisible());
 Piece->PlayPreparationEntrance(Entrance);Tick();
 TestFalse(TEXT("Entry returns to the configured idle rig"),Piece->Outline->IsVisible());
 Piece->SetBattlePresentation(true);TestTrue(TEXT("Leaving idle rig restores outline"),Piece->Outline->IsVisible());
 P->PreparationIdleAssembly.Reset();Piece->SetBattlePresentation(false);Piece->PlayPreparationEntrance(Entrance);
 auto* Replacement=DuplicateObject<UEGHeroDefinition>(Hero,W);
 Piece->ApplyHeroDefinition(Replacement);Tick();
 TestTrue(TEXT("Changing hero during entry restores its base outline"),Piece->Outline->IsVisible());
 Piece->OutlineMaterial=nullptr;Piece->RefreshAppearance();Piece->SetBattlePresentation(false);P->PreparationIdleAssembly.Reset();
 Piece->SetBattlePresentation(true);Piece->SetBattlePresentation(false);Piece->PlayPreparationEntrance(Entrance);Tick();
 TestFalse(TEXT("No outline material never creates an outline"),Piece->Outline->IsVisible());
 Piece->Destroy();return true;
}
#endif
