#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Actor/EGArtPiece.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSourceMeshLighting,
 "ElyGambit.CharacterCompletion.SourceMeshSwapPreservesBoardLighting",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSourceMeshLighting::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* Hero=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua.DA_Hua"));
 if(!TestNotNull(TEXT("Formal Hua"),Hero))return false;
 auto* Piece=W->SpawnActor<AEGArtPiece>();Piece->ApplyHeroDefinition(Hero);
 auto* Before=Cast<UMaterialInstanceDynamic>(Piece->Body->GetMaterial(0));
 if(!TestNotNull(TEXT("Appearance lighting MID"),Before))return false;
 const FLinearColor Direction(.21f,.45f,.87f,1.f);Before->SetVectorParameterValue(TEXT("DirectionToKeyLight"),Direction);
 auto* OriginalMesh=Piece->Body->GetSkeletalMeshAsset();
 auto* Source=NewObject<UEGSourcePreviewDefinition>(Piece);FEGSourcePreviewMotion Motion;Motion.Duration=1;
 FEGSourcePreviewPart Part;Part.PartId=TEXT("Body");
 Part.Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assets/Characters/FuHua/Weapons/Signature/Meshes/SK_FuHua_NoDefaultGauntlets.SK_FuHua_NoDefaultGauntlets"));
 Part.AnimationBlueprintClass=Hero->GetAnimationClass().LoadSynchronous();Motion.Parts.Add(Part);Source->Motions.Add(Motion);
 auto* Visual=W->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity);
 Visual->Definition=Source;Visual->bAutoPlay=false;Visual->FinishSpawning(FTransform::Identity);Visual->BindBody(Piece->Body,false,0);
 TestTrue(TEXT("Fixture actually swaps the displayed mesh"),Part.Mesh&&Part.Mesh!=OriginalMesh&&Piece->Body->GetSkeletalMeshAsset()==Part.Mesh);
 FLinearColor Actual;const bool bRead=Piece->Body->GetMaterial(0)->GetVectorParameterValue(FMaterialParameterInfo(TEXT("DirectionToKeyLight")),Actual);
 TestTrue(TEXT("New source MID keeps the scene light direction"),bRead&&Actual.Equals(Direction,.001f));
 Visual->Destroy();TestTrue(TEXT("EndPlay restores original body"),Piece->Body->GetSkeletalMeshAsset()==OriginalMesh);
 return true;
}
#endif
