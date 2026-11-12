#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Character/EGDemoCombatUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "EngineUtils.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGAnimBPVisibleBody,
 "ElyGambit.AnimationBlueprints.VisibleBodySourcePoseAndOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGAnimBPVisibleBody::RunTest(const FString&)
{
 auto* D=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Summons/Knight/Data/DA_Knight"));
 if(!TestNotNull(TEXT("Production Knight definition"),D))return false;
 auto* P=D->PresentationDefinition.Get();if(!TestTrue(TEXT("Runtime graph migration enabled"),P&&P->bUseAnimationBlueprintVisuals&&!P->IdleAssemblies.IsEmpty()))return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 W->InitializeActorsForPlay(FURL());W->BeginPlay();
 auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();Unit->InitializeUnit(D,FGuid::NewGuid(),EEGFaction::Player,1);
 auto* Body=Unit->GetVisiblePresentationMesh();auto* Instance=Cast<UEGCombatAnimInstance>(Body->GetAnimInstance());
 TestNotNull(TEXT("Displayed main body has a real graph instance"),Instance);
 auto* Def=P->IdleAssemblies[0].LoadSynchronous();
 auto* Rig=W->SpawnActorDeferred<AEGCombatVisual>(AEGCombatVisual::StaticClass(),FTransform::Identity,Unit);
 Rig->Definition=Def;Rig->bAutoPlay=false;Rig->FinishSpawning(FTransform::Identity);Rig->BindBody(Body,true);
 const float Time=.8f;Rig->SetSourceTime(Time);Body->TickAnimation(0.f,false);Body->RefreshBoneTransforms();
 TestTrue(TEXT("Runtime rig reuses the actual visible mesh"),Rig->BodyMesh==Body&&Body->IsVisible());
 TArray<USkeletalMeshComponent*> OwnedParts;Rig->GetComponents(OwnedParts);TestEqual(TEXT("No second main body actor component"),OwnedParts.Num(),0);
 TestEqual(TEXT("No single-node instance on displayed body"),Body->GetAnimationMode(),EAnimationMode::AnimationBlueprint);
 auto* Clip=Def->Motions[0].Parts[0].Animation.Get();const auto& Ref=Clip->GetSkeleton()->GetReferenceSkeleton();
 int32 Compared=0;
 for(const FName Name:{FName(TEXT("Bip002")),FName(TEXT("Bip002 Spine")),FName(TEXT("Bip002-Prop1"))})
 {
  int32 Index=Ref.FindBoneIndex(Name);if(Index==INDEX_NONE||Body->GetBoneIndex(Name)==INDEX_NONE)continue;
  FTransform Expected=FTransform::Identity;
  while(Index!=INDEX_NONE){FTransform Local;Clip->GetBoneTransform(Local,FSkeletonPoseBoneIndex(Index),FAnimExtractContext(Time,false),false);Expected=Expected*Local;Index=Ref.GetParentIndex(Index);}
  const auto Actual=Body->GetSocketTransform(Name,RTS_Component);
  TestTrue(*FString::Printf(TEXT("AnimGraph preserves original %s pose"),*Name.ToString()),Actual.Equals(Expected,.05f));++Compared;
 }
 TestTrue(TEXT("Source geometry comparisons actually executed"),Compared>=1);
 int32 Previews=0;for(TActorIterator<AEGSourceCharacterPreview> It(W);It;++It)++Previews;
 TestEqual(TEXT("Runtime never creates a source preview"),Previews,0);
 Rig->Destroy();TestNotNull(TEXT("Rig teardown leaves master AnimBP owned by body"),Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()));
 TestTrue(TEXT("Rig teardown restores the original displayed mesh"),Body->GetSkeletalMeshAsset()==D->GetMesh().Get());
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);return true;
}
#endif
