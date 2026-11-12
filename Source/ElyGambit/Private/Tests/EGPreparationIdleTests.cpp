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
#include "Actor/EGArtPiece.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Presentation/EGCombatVisual.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPreparationIdleClock,
 "ElyGambit.SourcePreview.PreparationIdleClockAndLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPreparationIdleClock::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* D=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Eden"));
 if(!TestTrue(TEXT("Original preparation battle standby configured"),D&&D->PresentationDefinition&&!D->PresentationDefinition->PreparationIdleAssembly.IsNull()))return false;
 auto* Piece=W->SpawnActor<AEGArtPiece>();Piece->ApplyHeroDefinition(D);
 auto Find=[&](){for(TActorIterator<AEGCombatVisual> I(W);I;++I)if(I->GetOwner()==Piece&&!I->IsActorBeingDestroyed())return *I;return static_cast<AEGCombatVisual*>(nullptr);};
 auto* Idle=Find();if(!TestNotNull(TEXT("Preparation assembly created"),Idle))return false;
 TestTrue(TEXT("Preparation selects its configured original battle stance"),Idle->Definition==D->PresentationDefinition->PreparationIdleAssembly.Get());
 TestTrue(TEXT("Preparation and combat use the same original StandBy"),!D->PresentationDefinition->IdleAssemblies.IsEmpty()&&Idle->Definition==D->PresentationDefinition->IdleAssemblies[0].Get());
 TestTrue(TEXT("Clock runs and loops"),Idle->bAutoPlay&&Idle->bPlaying&&Idle->bLoop&&Idle->IsActorTickEnabled());
 auto Tick=[&](float S){for(int32 I=0;I<FMath::CeilToInt(S/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);}};
 Tick(.3f);TestTrue(TEXT("Real world tick advances idle beyond zero"),Idle->SourceTime>.2f);
 const float Length=Idle->Definition->Motions[0].Duration;Tick(Length);TestTrue(TEXT("Loop keeps the clock inside the source clip"),Idle->bPlaying&&Idle->SourceTime>0&&Idle->SourceTime<Length);
 Piece->SetActorHiddenInGame(true);TestTrue(TEXT("Dragging hide propagates to idle"),Idle->IsHidden());Piece->SetActorHiddenInGame(false);TestFalse(TEXT("Idle visible after drag restore"),Idle->IsHidden());
 TWeakObjectPtr<AEGCombatVisual> Old(Idle);Piece->SetBattlePresentation(true);TestTrue(TEXT("Entering battle removes preparation actor"),!Old.IsValid()||Old->IsActorBeingDestroyed());TestNull(TEXT("No preparation actor during combat"),Find());
 Piece->SetBattlePresentation(false);auto* Restored=Find();if(!TestNotNull(TEXT("Returning to preparation recreates idle"),Restored))return false;
 TestTrue(TEXT("Restored clock is active"),Restored->bPlaying&&Restored->IsActorTickEnabled());Piece->Destroy();TestTrue(TEXT("Destroying the piece cleans the idle child"),Restored->IsActorBeingDestroyed());return true;
}
#endif
