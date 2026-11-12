#include "EGNativeMeshPassComponent.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "RenderingThread.h"
#include "EGNativeRendererInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNativeRendererLifecycleTest,"ElyGambit.ElysiaNative.NativePassOwnershipAndRestore",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNativeRendererLifecycleTest::RunTest(const FString&)
{
    // The renderer module cannot depend on the game module's hero types. Use
    // the fresh production body directly as a fixture for this generic pass.
    auto* Asset=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assets/Characters/Elysia/Reimport20261101/Meshes/SK_C1_Body.SK_C1_Body"));
    if (!TestNotNull(TEXT("Reimported production body"),Asset)) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone(); UWorld* World=Instance->GetWorld();
    ON_SCOPE_EXIT { Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    World->InitializeActorsForPlay(FURL()); World->BeginPlay();
    auto* Owner=World->SpawnActor<AActor>();
    auto* Other=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<USkeletalMeshComponent>(Owner);
    Owner->AddInstanceComponent(Mesh);Owner->SetRootComponent(Mesh);Mesh->SetSkeletalMesh(Asset);Mesh->RegisterComponent();
    Mesh->SetRenderInMainPass(true);Mesh->SetRenderInDepthPass(false);
    auto* Material=Mesh->GetMaterial(0);
    auto* Native=NewObject<UEGNativeMeshPassComponent>(Owner);
    Owner->AddInstanceComponent(Native);Native->RegisterComponent();
    FEGNativeMeshPass Pass;Pass.Material=Material;
    FlushRenderingCommands();const uint64 Before=GEGNativeRegistrationCount.load();
    TestTrue(TEXT("Native pass accepts same-owner actual skeletal component"),Native->Configure(Mesh,{Pass},true));
    FlushRenderingCommands();
    TestEqual(TEXT("Registration exists without retaining a render-thread UObject"),GEGNativeRegistrationCount.load(),Before+1);
    TestFalse(TEXT("Native replacement removes ordinary main draw"),Mesh->bRenderInMainPass);
    TestFalse(TEXT("Native replacement removes ordinary depth draw"),Mesh->bRenderInDepthPass);
    TestTrue(TEXT("No duplicated body or changed material"),Mesh->GetOwner()==Owner && Mesh->GetMaterial(0)==Material);
    auto* Foreign=NewObject<USkeletalMeshComponent>(Other);Other->AddInstanceComponent(Foreign);
    Foreign->SetSkeletalMesh(Asset);Foreign->RegisterComponent();
    TestFalse(TEXT("Foreign actor ownership rejected without releasing valid registration"),Native->Configure(Foreign,{Pass},true));
    TestFalse(TEXT("Invalid fixed-function state rejected without mutating body"),[&]
        { FEGNativeMeshPass Invalid=Pass;Invalid.State.Cull=19;return Native->Configure(Mesh,{Invalid},true); }());
    Native->DestroyComponent();FlushRenderingCommands();
    TestEqual(TEXT("Unregister removes queued render registration"),GEGNativeRegistrationCount.load(),Before);
    TestTrue(TEXT("Unregister restores original main flag"),Mesh->bRenderInMainPass);
    TestFalse(TEXT("Unregister restores original depth flag"),Mesh->bRenderInDepthPass);
    TestTrue(TEXT("Existing posed component survives native attachment removal"),IsValid(Mesh) && Mesh->GetOwner()==Owner);
    Owner->Destroy();Other->Destroy();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNativeStaticLifecycleTest,"ElyGambit.ElysiaNative.StaticPassOwnershipAndRestore",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNativeStaticLifecycleTest::RunTest(const FString&)
{
    // An engine plane is a lifecycle fixture only; it is never character art.
    auto* Asset=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!TestNotNull(TEXT("Static component test fixture"),Asset)) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
    ON_SCOPE_EXIT {Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    World->InitializeActorsForPlay(FURL());World->BeginPlay();
    auto* Owner=World->SpawnActor<AActor>();auto* Other=World->SpawnActor<AActor>();
    auto* Mesh=NewObject<UStaticMeshComponent>(Owner);Owner->AddInstanceComponent(Mesh);Owner->SetRootComponent(Mesh);
    Mesh->SetStaticMesh(Asset);Mesh->RegisterComponent();Mesh->SetRenderInDepthPass(false);
    auto* Native=NewObject<UEGNativeMeshPassComponent>(Owner);Owner->AddInstanceComponent(Native);Native->RegisterComponent();
    auto* Definition=NewObject<UEGNativeMeshPassDefinition>(Owner);FEGNativeMeshPass Pass;Pass.Material=Mesh->GetMaterial(0);Definition->Passes.Add(Pass);
    FlushRenderingCommands();const uint64 Before=GEGNativeRegistrationCount.load();
    TestTrue(TEXT("Existing static component accepts source pass definition"),Native->ApplyDefinition(Mesh,Definition,Owner));
    FlushRenderingCommands();TestEqual(TEXT("One static render registration"),GEGNativeRegistrationCount.load(),Before+1);
    TestTrue(TEXT("Static target keeps actual Actor ownership"),Native->GetTarget()==Mesh && Mesh->GetOwner()==Owner);
    TestFalse(TEXT("Ordinary static main draw replaced"),Mesh->bRenderInMainPass);
    TestTrue(TEXT("Repeated sample reuses one registration"),Native->ApplyDefinition(Mesh,Definition,Owner));
    FlushRenderingCommands();TestEqual(TEXT("No sample registration accumulation"),GEGNativeRegistrationCount.load(),Before+1);
    auto* Foreign=NewObject<UStaticMeshComponent>(Other);Other->AddInstanceComponent(Foreign);Foreign->SetStaticMesh(Asset);Foreign->RegisterComponent();
    TestFalse(TEXT("Cross Actor static borrowing rejected"),Native->Configure(Foreign,{Pass},true));
    Native->DestroyComponent();FlushRenderingCommands();
    TestEqual(TEXT("Static cleanup removes render registration"),GEGNativeRegistrationCount.load(),Before);
    TestTrue(TEXT("Original static main flag restored"),Mesh->bRenderInMainPass);
    TestFalse(TEXT("Original static depth flag restored"),Mesh->bRenderInDepthPass);
    Owner->Destroy();Other->Destroy();return true;
}
#endif
