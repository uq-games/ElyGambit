#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/WidgetComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "UI/EGVitalsLayoutSubsystem.h"
#include "UI/Widget/EGDemoPanel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFixedOverheadVitals,
 "ElyGambit.UI.FixedOverheadVitals",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FEGFixedOverheadVitals::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();
 UWorld* World=Instance->GetWorld();
 ON_SCOPE_EXIT
 {
  World->BeginTearingDown();
  World->EndPlay(EEndPlayReason::Quit);
  Instance->Shutdown();
  World->DestroyWorld(false);
  GEngine->DestroyWorldContext(World);
 };
 World->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();
 FURL URL;
 World->SetGameMode(URL);
 auto* Mode=World->GetAuthGameMode<AEGChessGameMode>();
 if(!TestNotNull(TEXT("Fixture game mode"),Mode))return false;
 Mode->bAutoStart=false;
 World->InitializeActorsForPlay(URL);
 World->BeginPlay();

 auto* Layout=World->GetSubsystem<UEGVitalsLayoutSubsystem>();
 if(!TestNotNull(TEXT("World owns the overhead anchor subsystem"),Layout))return false;
 auto* Definition=NewObject<UEGHeroDefinition>(Instance.Get());
 Definition->HeroId=TEXT("FixedOverheadVitalsFixture");
 Definition->BaseStats.MaxHealth=100.f;
 Definition->BaseStats.MaxMana=100.f;
 Definition->VitalsAnchorHeight=250.f;

 FActorSpawnParameters Params;
 Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 const FVector Origin(100.f,200.f,88.f);
 auto* Ally=World->SpawnActor<AEGDemoCombatUnit>(AEGDemoCombatUnit::StaticClass(),Origin,FRotator::ZeroRotator,Params);
 auto* Enemy=World->SpawnActor<AEGDemoCombatUnit>(AEGDemoCombatUnit::StaticClass(),Origin,FRotator::ZeroRotator,Params);
 if(!TestTrue(TEXT("Overlapping ally and enemy initialize"),Ally&&Enemy&&
  Ally->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Player,1)&&
  Enemy->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;

 auto* BodyMesh=LoadObject<USkeletalMesh>(nullptr,
  TEXT("/Game/Assets/Characters/FuHua/Weapons/Signature/Meshes/SK_FuHua_NoDefaultGauntlets.SK_FuHua_NoDefaultGauntlets"));
 if(!TestNotNull(TEXT("Source body mesh for the head anchor"),BodyMesh))return false;
 for(auto* Unit:{Ally,Enemy})
 {
  auto* Body=Unit->GetVisiblePresentationMesh();
  Body->SetSkeletalMeshAsset(BodyMesh);
  Body->SetVisibility(true);
  Body->RefreshBoneTransforms();
 }
 const FName HeadNames[]={TEXT("Bip001 Head"),TEXT("Bip001_Head"),TEXT("Bip001-Head"),
  TEXT("Bip01 Head"),TEXT("Bip01_Head"),TEXT("Bip01-Head"),TEXT("Head"),TEXT("Bip001Head")};
 FName HeadName=NAME_None;
 for(const FName Name:HeadNames)
  if(Ally->GetVisiblePresentationMesh()->GetBoneIndex(Name)!=INDEX_NONE){HeadName=Name;break;}
 if(!TestTrue(TEXT("Source body exposes a head bone"),!HeadName.IsNone()))return false;

 auto* AllyComponent=Ally->FindComponentByClass<UWidgetComponent>();
 auto* EnemyComponent=Enemy->FindComponentByClass<UWidgetComponent>();
 if(!TestNotNull(TEXT("Ally has an overhead component"),AllyComponent)||
    !TestNotNull(TEXT("Enemy has an overhead component"),EnemyComponent))return false;
 auto* AllyVitals=Cast<UEGUnitVitals>(AllyComponent->GetUserWidgetObject());
 auto* EnemyVitals=Cast<UEGUnitVitals>(EnemyComponent->GetUserWidgetObject());
 if(!TestNotNull(TEXT("Ally vitals are constructed"),AllyVitals)||
    !TestNotNull(TEXT("Enemy vitals are constructed"),EnemyVitals))return false;
 // Keep the Slate wrappers alive for the complete test; releasing the wrappers
 // invokes NativeDestruct and intentionally unbinds the vitals.
 const TSharedRef<SWidget> AllySlate=AllyVitals->TakeWidget();
 const TSharedRef<SWidget> EnemySlate=EnemyVitals->TakeWidget();
 AllyVitals->SetRenderTranslation(FVector2D(-55.f,-35.f));
 EnemyVitals->SetRenderTranslation(FVector2D(55.f,-35.f));

 Layout->Tick(1.f/60.f);
 TestEqual(TEXT("Overlapping ally vitals retain no screen displacement"),
  AllyVitals->GetRenderTransform().Translation,FVector2D::ZeroVector);
 TestEqual(TEXT("Overlapping enemy vitals retain no screen displacement"),
  EnemyVitals->GetRenderTransform().Translation,FVector2D::ZeroVector);
 const FVector Head=Ally->GetVisiblePresentationMesh()->GetSocketLocation(HeadName);
 TestTrue(TEXT("Ally vitals follow the visible body head"),
  AllyComponent->GetComponentLocation().Equals(Head+FVector(0,0,35.f),.1f));
 TestTrue(TEXT("Overlapping units keep the same overhead anchor"),
  AllyComponent->GetComponentLocation().Equals(EnemyComponent->GetComponentLocation(),.01f));

 const FVector Before=AllyComponent->GetComponentLocation();
 const FVector Movement(175.f,-90.f,0.f);
 Ally->SetActorLocation(Origin+Movement,false);
 Layout->Tick(1.f/60.f);
 TestTrue(TEXT("The overhead anchor follows its moving owner"),
  AllyComponent->GetComponentLocation().Equals(Before+Movement,.01f));
 TestEqual(TEXT("Movement does not introduce a screen displacement"),
  AllyVitals->GetRenderTransform().Translation,FVector2D::ZeroVector);
 return true;
}
#endif
