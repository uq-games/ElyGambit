#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Presentation/EGBoardModelSizing.h"
#include "Presentation/EGBoardLocomotion.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardModelFit,
 "ElyGambit.BoardModel.FitLimits",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardModelFit::RunTest(const FString&)
{
 TestTrue(TEXT("A wide body fits 80 percent of the narrower grid axis"),
  FMath::IsNearlyEqual(FEGBoardModelSizing::Fit(160,170,320,100,1.35f,1),.4f));
 TestTrue(TEXT("Height can be the limiting factor"),FMath::IsNearlyEqual(FEGBoardModelSizing::Fit(160,170,80,432,1.35f,1),.5f));
 TestTrue(TEXT("Small bodies are never enlarged"),FMath::IsNearlyEqual(FEGBoardModelSizing::Fit(160,170,60,100,1.35f,1),1.f));
 TestTrue(TEXT("A smaller authored scale stays effective"),FMath::IsNearlyEqual(FEGBoardModelSizing::Fit(160,170,80,100,1.35f,.7f),.7f));
 TestTrue(TEXT("Missing board leaves the authored model scale"),FMath::IsNearlyEqual(FEGBoardModelSizing::Fit(0,0,500,500,1.35f,.8f),.8f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVSourceScale,
 "ElyGambit.BoardModel.VillVSourceScale",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVSourceScale::RunTest(const FString&)
{
 const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV"),
  TEXT("/Game/Blueprints/Characters/VillV/Data/DualDevices/DA_VillV_Gatling"),
  TEXT("/Game/Blueprints/Characters/VillV/Data/DualDevices/DA_VillV_Cannon")};
 for(const TCHAR* Path:Paths)
 {
  auto* Definition=LoadObject<UEGHeroDefinition>(nullptr,Path);
  if(!TestNotNull(Path,Definition))continue;
  TestTrue(TEXT("Source MeshScale is three"),Definition->GetMeshScale().Equals(FVector(3.f),.001f));
  TestEqual(TEXT("Vill-V composition preserves original board multiplier"),FEGBoardModelSizing::Resolve(Definition,nullptr),1.f);
 }
 // Distinguish the exception from a default authored multiplier of one.
 auto* Synthetic=NewObject<UEGHeroDefinition>();
 Synthetic->bUseSplitConfiguration=true;
 Synthetic->PresentationDefinition=NewObject<UEGHeroPresentationDefinition>(Synthetic);
 Synthetic->PresentationDefinition->BoardModelScale=.4f;
 Synthetic->HeroKind=EEGHeroKind::Generic;
 Synthetic->HeroId=TEXT("VillV_Gatling");
 TestEqual(TEXT("Device exception does not run humanoid or authored fitting"),FEGBoardModelSizing::Resolve(Synthetic,nullptr),1.f);
 Synthetic->HeroId=TEXT("AnotherUnit");
 TestTrue(TEXT("Other definitions retain authored fitting"),FMath::IsNearlyEqual(FEGBoardModelSizing::Resolve(Synthetic,nullptr),.4f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardLocomotionHysteresis,
 "ElyGambit.BoardModel.LocomotionHysteresis",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardLocomotionHysteresis::RunTest(const FString&)
{
 FEGBoardLocomotion Motion;
 TestEqual(TEXT("First sample anchors without invented movement"),Motion.Sample(FVector(0,0,100),.1f,200.f),0.f);
 TestTrue(TEXT("Horizontal movement selects run"),Motion.Sample(FVector(25,0,100),.1f,200.f)>=200.f&&Motion.IsRunning());
 TestTrue(TEXT("Run remains stable just below threshold"),Motion.Sample(FVector(44,0,100),.1f,200.f)>=200.f&&Motion.IsRunning());
 TestTrue(TEXT("Slower movement selects walk"),Motion.Sample(FVector(61,0,100),.1f,200.f)<200.f&&!Motion.IsRunning());
 TestTrue(TEXT("Short stop retains the prior gait"),Motion.Sample(FVector(61,0,100),.1f,200.f)>0.f);
 TestEqual(TEXT("Sustained stop returns idle"),Motion.Sample(FVector(61,0,100),.1f,200.f),0.f);
 TestEqual(TEXT("Vertical-only movement remains idle"),Motion.Sample(FVector(61,0,400),.1f,200.f),0.f);
 Motion.Reset();
 TestEqual(TEXT("Reset ignores relocation on next sample"),Motion.Sample(FVector(900,0,0),.1f,200.f),0.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardModelInstanceStability,
 "ElyGambit.BoardModel.InstanceStability",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardModelInstanceStability::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* Mode=W->GetAuthGameMode<AEGChessGameMode>();
 if(!TestNotNull(TEXT("Fixture mode"),Mode))return false;
 Mode->bAutoStart=false;
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestNotNull(TEXT("Fixture board"),Mode->GetBoard()))return false;
 auto* A=W->SpawnActor<AActor>();auto* B=W->SpawnActor<AActor>();
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());
 Definition->PresentationPickRadius=1000.f;Definition->PresentationHeight=100.f;
 TestEqual(TEXT("Missing layout does not fit or cache"),FEGBoardModelSizing::Resolve(Definition,A),1.f);
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->CellWidth=500.f;Grid->CellDepth=500.f;
 Mode->GetBoard()->UseLayout(Grid);
 TestTrue(TEXT("First real layout resolves the model"),FMath::IsNearlyEqual(FEGBoardModelSizing::Resolve(Definition,A),.2f));
 Definition->PresentationPickRadius=100.f;
 TestTrue(TEXT("Existing instance remains stable across assembly changes"),FMath::IsNearlyEqual(FEGBoardModelSizing::Resolve(Definition,A),.2f));
 TestEqual(TEXT("Another instance resolves current configuration"),FEGBoardModelSizing::Resolve(Definition,B),1.f);
 FEGBoardModelSizing::Forget(A);
 TestEqual(TEXT("Reconfiguration invalidates the instance value"),FEGBoardModelSizing::Resolve(Definition,A),1.f);
 FEGBoardModelSizing::Forget(A);FEGBoardModelSizing::Forget(B);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardAssemblyEffectScale,
 "ElyGambit.BoardModel.AssemblyKeepsSourceEffectScale",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardAssemblyEffectScale::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assets/Characters/FuHua/Weapons/Signature/Meshes/SK_FuHua_NoDefaultGauntlets.SK_FuHua_NoDefaultGauntlets"));
 auto* Cube=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
 if(!TestNotNull(TEXT("Source body"),Mesh)||!TestNotNull(TEXT("Effect mesh"),Cube))return false;
 auto* Visual=W->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity);
 auto* Definition=NewObject<UEGSourcePreviewDefinition>(Visual);
 auto* Effect=NewObject<UEGSourceEffectDefinition>(Definition);
 FEGSourceEffectTrack Track;Track.Mesh=Cube;Track.Material=UMaterial::GetDefaultMaterial(MD_Surface);Track.Duration=1.f;
 Track.Samples.AddDefaulted();Effect->Tracks.Add(Track);
 Track.bWorldSimulation=true;Track.StartTime=.4f;Effect->Tracks.Add(Track);
 FEGSourcePreviewMotion Motion;Motion.Duration=1.f;
 FEGSourcePreviewPart Body;Body.PartId=TEXT("Body");Body.Mesh=Mesh;Motion.Parts.Add(Body);
 FEGSourcePreviewPart Manta;Manta.PartId=TEXT("Manta");Manta.Mesh=Mesh;Motion.Parts.Add(Manta);
 FEGSourcePreviewEffect Binding;Binding.Effect=Effect;Binding.AnchorPartId=TEXT("Body");Motion.Effects.Add(Binding);
 Definition->Motions.Add(Motion);Visual->Definition=Definition;Visual->bAutoPlay=false;
 Visual->FinishSpawning(FTransform::Identity);Visual->SetActorScale3D(FVector(2.f));Visual->SetSourceTime(.2f);
 TInlineComponentArray<USkeletalMeshComponent*> Models(Visual);
 TInlineComponentArray<UStaticMeshComponent*> Effects(Visual);
 if(!TestEqual(TEXT("Body and source effect model"),Models.Num(),2)||!TestEqual(TEXT("Initial attached effect"),Effects.Num(),1))return false;
 USkeletalMeshComponent* BodyModel=nullptr;
 USkeletalMeshComponent* MantaModel=nullptr;
 for(USkeletalMeshComponent* Model:Models)
 {
  if(Model->ComponentHasTag(TEXT("EGSourcePartIndex_0")))BodyModel=Model;
  if(Model->ComponentHasTag(TEXT("EGSourcePartIndex_1")))MantaModel=Model;
 }
 if(!TestNotNull(TEXT("Body component identified by source part index"),BodyModel)||
    !TestNotNull(TEXT("Manta component identified by source part index"),MantaModel)||BodyModel==MantaModel)return false;
 AddInfo(FString::Printf(TEXT("Body=%s; Manta=%s; enumeration first=%s"),*BodyModel->GetName(),*MantaModel->GetName(),*Models[0]->GetName()));
 const FVector PreviousBodyScale=BodyModel->GetComponentScale();
 const FVector PreviousMantaScale=MantaModel->GetComponentScale();
 UStaticMeshComponent* AttachedEffect=Effects[0];
 const FVector FirstEffectScale=AttachedEffect->GetComponentScale();
 Visual->SetBoardModelScale(.5f);Visual->SetSourceTime(.5f);
 TInlineComponentArray<UStaticMeshComponent*> ResizedEffects(Visual);
 if(!TestEqual(TEXT("World-simulated effect appears after the body shrinks"),ResizedEffects.Num(),2))return false;
 UStaticMeshComponent* WorldEffect=nullptr;
 for(UStaticMeshComponent* EffectComponent:ResizedEffects)if(EffectComponent!=AttachedEffect)WorldEffect=EffectComponent;
 if(!TestNotNull(TEXT("Original attached effect remains"),ResizedEffects.Contains(AttachedEffect)?AttachedEffect:nullptr)||
    !TestNotNull(TEXT("World-simulated effect identified by creation"),WorldEffect))return false;
 TestEqual(TEXT("Board body becomes half size"),BodyModel->GetComponentScale(),PreviousBodyScale*.5f,.001f);
 TestEqual(TEXT("Manta effect model retains world scale"),MantaModel->GetComponentScale(),PreviousMantaScale,.001f);
 TestEqual(TEXT("Attached effect retains world scale"),AttachedEffect->GetComponentScale(),FirstEffectScale,.001f);
 TestEqual(TEXT("New world-simulated effect retains world scale"),WorldEffect->GetComponentScale(),FirstEffectScale,.001f);
 Visual->SetSourceTime(.6f);
 TestEqual(TEXT("World-simulated birth anchor retains its world scale"),WorldEffect->GetComponentScale(),FirstEffectScale,.001f);
 Visual->SetBoardModelScale(1.5f);Visual->SetSourceTime(.6f);
 TestEqual(TEXT("Master body can be enlarged to one and a half times"),BodyModel->GetComponentScale(),PreviousBodyScale*1.5f,.001f);
 TestEqual(TEXT("Enlargement preserves the manta source effect model size"),MantaModel->GetComponentScale(),PreviousMantaScale,.001f);
 TestEqual(TEXT("Enlargement preserves attached effect size"),AttachedEffect->GetComponentScale(),FirstEffectScale,.001f);
 TestEqual(TEXT("Enlargement preserves world-simulated effect size"),WorldEffect->GetComponentScale(),FirstEffectScale,.001f);
 TestTrue(TEXT("Actor root scale is unchanged"),Visual->GetActorScale3D().Equals(FVector(2.f),.001f));
 return true;
}
#endif
