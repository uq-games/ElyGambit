#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceDistortionPass.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusNativeBillboardTest,
 "ElyGambit.Mobius.NativeFX.CameraAndParticleRotation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusNativeBillboardTest::RunTest(const FString&)
{
 // Reference: Unity View-aligned particle axes follow camera roll; particle
 // rotation of +90 degrees around source Z turns its X axis into screen -Y.
 // The imported C basis encodes that source Z rotation as UE Y rotation.
 for(const FRotator Camera:{FRotator(0,0,0),FRotator(-35,70,42)})
 {
  const FQuat CameraQ=Camera.Quaternion();
  const FVector Facing=-CameraQ.GetForwardVector(),Right=CameraQ.GetRightVector();
  const FVector Up=CameraQ.GetUpVector();
  const FQuat Zero=EGSourceBillboardRotation(FQuat::Identity,Facing,Right);
  TestTrue(TEXT("View-aligned normal faces opposite camera forward"),Zero.GetAxisZ().Equals(Facing,1.e-4));
  TestTrue(TEXT("Camera roll is retained by the particle X axis"),Zero.GetAxisX().Equals(Right,1.e-4));
  const FQuat QuarterTurn=EGSourceBillboardRotation(FQuat(FVector::YAxisVector,PI*.5),Facing,Right);
  TestTrue(TEXT("Original particle quarter-turn remains visible on screen"),QuarterTurn.GetAxisX().Equals(-Up,1.e-4));
  TestTrue(TEXT("Rotating a source quad does not tilt its view-plane normal"),QuarterTurn.GetAxisZ().Equals(Facing,1.e-4));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusDistortionOwnershipTest,
 "ElyGambit.Mobius.NativeFX.SharedDistortionOwnershipAndCleanup",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusDistortionOwnershipTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
 Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
 ON_SCOPE_EXIT{Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 TStrongObjectPtr<UMaterial> Composite(NewObject<UMaterial>());
 Composite->MaterialDomain=MD_PostProcess;
 auto MakeSource=[&]()
 {
  auto* Owner=World->SpawnActor<AActor>();
  auto* Part=NewObject<UStaticMeshComponent>(Owner);
  Owner->AddInstanceComponent(Part);Owner->SetRootComponent(Part);
  Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->RegisterComponent();
  return Part;
 };
 auto* First=MakeSource();auto* Second=MakeSource();
 TestTrue(TEXT("First caster registers its source primitive"),AEGSourceDistortionPass::RegisterSource(First,Composite.Get()));
 TestTrue(TEXT("Second caster shares source accumulation"),AEGSourceDistortionPass::RegisterSource(Second,Composite.Get()));
 AEGSourceDistortionPass* Pass=nullptr;int32 Count=0;
 for(TActorIterator<AEGSourceDistortionPass> It(World);It;++It){Pass=*It;++Count;}
 if(!TestEqual(TEXT("Exactly one compositor for two source actors"),Count,1)||!Pass)return false;
 TestFalse(TEXT("Cosmetic compositor never replicates"),Pass->GetIsReplicated());
 TestTrue(TEXT("Registering does not take primitive ownership"),First->GetOwner()!=Pass&&Second->GetOwner()!=Pass);
 TestEqual(TEXT("Both weak source references are retained"),Pass->GetSourceCount(),2);
 TestTrue(TEXT("Re-registering cannot stack the same primitive"),AEGSourceDistortionPass::RegisterSource(First,Composite.Get()));
 TestEqual(TEXT("Source reference count remains stable"),Pass->GetSourceCount(),2);
 First->GetOwner()->Destroy();Pass->Tick(0.f);
 TestEqual(TEXT("Dead source is removed without removing the other caster"),Pass->GetSourceCount(),1);
 TestFalse(TEXT("No camera cannot activate an unrelated full-screen effect"),Pass->IsCompositeActive());
 Second->GetOwner()->Destroy();Pass->Tick(0.f);
 TestTrue(TEXT("Final source removal destroys the compositor"),Pass->IsActorBeingDestroyed());
 return true;
}
#endif
