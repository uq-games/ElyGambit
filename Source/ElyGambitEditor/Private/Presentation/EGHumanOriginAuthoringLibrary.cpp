#include "Presentation/EGHumanOriginAuthoringLibrary.h"

#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGSkillNotify.h"
#include "Actor/EGArtPiece.h"
#include "Data/EGHeroDefinition.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/Skeleton.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_KawaiiPhysics.h"
#include "AnimationGraphSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace
{
 template <typename T> T* Place(UEdGraph* Graph, int32 X, int32 Y)
 {
  T* Node = NewObject<T>(Graph, NAME_None, RF_Transactional);
  Graph->AddNode(Node, false, false);
  Node->CreateNewGuid();
  Node->NodePosX = X;
  Node->NodePosY = Y;
  Node->PostPlacedNewNode();
  Node->AllocateDefaultPins();
  return Node;
 }

 UEdGraphPin* PosePin(UEdGraphNode* Node, EEdGraphPinDirection Direction)
 {
  if (!Node) return nullptr;
  for (UEdGraphPin* Pin : Node->Pins)
   if (Pin && Pin->Direction == Direction && UAnimationGraphSchema::IsPosePin(Pin->PinType)) return Pin;
  return nullptr;
 }
}

bool UEGHumanOriginAuthoringLibrary::AddCandidateClothToPrivateBlueprint(
 UAnimBlueprint* Blueprint, USkeletalMesh* Mesh, const TArray<FName>& RootBones, FString& Error)
{
 Error.Reset();
 auto Fail = [&Error](const TCHAR* Message) { Error = Message; return false; };
 constexpr const TCHAR* PrivatePrefix = TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Combat20261101/HumanOriginRebuild20261107/");
 if (!Blueprint || !Mesh || !Blueprint->GetOutermost()->GetName().StartsWith(PrivatePrefix))
  return Fail(TEXT("Expected the new private HumanOrigin AnimBlueprint and a skeletal mesh."));
 if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(UEGCombatAnimInstance::StaticClass()) ||
     !Mesh->GetSkeleton() || Blueprint->TargetSkeleton != Mesh->GetSkeleton())
  return Fail(TEXT("Blueprint parent or target skeleton does not match the actual combat body."));
 if (RootBones.IsEmpty() || RootBones.Num() > 64)
  return Fail(TEXT("Provide 1 to 64 explicitly source-audited root bones."));

 // Reject missing, duplicated and overlapping subtrees. A node simulates every
 // descendant of each root; nested roots would double-drive the same bones.
 const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
 TSet<int32> RootIndices;
 for (const FName Bone : RootBones)
 {
  const int32 Index = Ref.FindBoneIndex(Bone);
  if (Bone.IsNone() || Index == INDEX_NONE || RootIndices.Contains(Index))
   return Fail(TEXT("A requested root is missing or duplicated on the actual mesh."));
  bool bHasChild = false;
  for (int32 Child = Index + 1; Child < Ref.GetNum(); ++Child)
   if (Ref.GetParentIndex(Child) == Index) { bHasChild = true; break; }
  if (!bHasChild) return Fail(TEXT("A requested root is a leaf and cannot define a cloth chain."));
  RootIndices.Add(Index);
 }
 for (int32 Index : RootIndices)
  for (int32 Parent = Ref.GetParentIndex(Index); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
   if (RootIndices.Contains(Parent)) return Fail(TEXT("Requested root subtrees overlap."));

 TArray<UEdGraph*> Graphs;
 Blueprint->GetAllGraphs(Graphs);
 UEdGraph* Graph = nullptr;
 for (UEdGraph* Candidate : Graphs) if (Candidate && Candidate->GetFName() == TEXT("AnimGraph"))
 {
  if (Graph) return Fail(TEXT("More than one AnimGraph was found."));
  Graph = Candidate;
 }
 if (!Graph || !Graph->GetSchema()) return Fail(TEXT("AnimGraph or graph schema is unavailable."));
 UAnimGraphNode_Root* Root = nullptr;
 for (UEdGraphNode* Node : Graph->Nodes)
 {
  if (Cast<UAnimGraphNode_KawaiiPhysics>(Node)) return Fail(TEXT("Private graph already contains Kawaii physics."));
  if (auto* Candidate = Cast<UAnimGraphNode_Root>(Node))
  {
   if (Root) return Fail(TEXT("Multiple root nodes found."));
   Root = Candidate;
  }
 }
 UEdGraphPin* RootInput = PosePin(Root, EGPD_Input);
 if (!RootInput || RootInput->LinkedTo.Num() != 1) return Fail(TEXT("Expected exactly one original root pose connection."));
 UEdGraphPin* OriginalOutput = RootInput->LinkedTo[0];
 auto* Slot = OriginalOutput ? Cast<UAnimGraphNode_Slot>(OriginalOutput->GetOwningNode()) : nullptr;
 if (!Slot || Slot->Node.SlotName != TEXT("FullBody"))
  return Fail(TEXT("Original graph must connect the existing FullBody montage slot directly to root."));

 Blueprint->Modify();
 Graph->Modify();
 const int32 X = Root->NodePosX;
 const int32 Y = Root->NodePosY;
 auto* ToComponent = Place<UAnimGraphNode_LocalToComponentSpace>(Graph, X - 240 * (RootBones.Num() + 2), Y);
 TArray<UAnimGraphNode_KawaiiPhysics*> PhysicsNodes;
 for (int32 I = 0; I < RootBones.Num(); ++I)
 {
  // Separate nodes isolate missing LOD bones: Kawaii's multi-root path skips
  // the entire node if any AdditionalRootBones entry is not required at a LOD.
  auto* Physics = Place<UAnimGraphNode_KawaiiPhysics>(Graph, X - 240 * (RootBones.Num() + 1 - I), Y);
  PhysicsNodes.Add(Physics);
  Physics->Node.RootBone.BoneName = RootBones[I];
  // Conservative UE approximation; Unity DynamicBone parameters remain unknown.
  Physics->Node.PhysicsSettings.Damping = .35f;
  Physics->Node.PhysicsSettings.Stiffness = .22f;
  Physics->Node.PhysicsSettings.WorldDampingLocation = .8f;
  Physics->Node.PhysicsSettings.WorldDampingRotation = .8f;
  Physics->Node.PhysicsSettings.Radius = 1.f;
  Physics->Node.PhysicsSettings.LimitAngle = 35.f;
  Physics->Node.TeleportDistanceThreshold = 200.f;
  Physics->Node.TeleportRotationThreshold = 35.f;
  Physics->Node.bUpdatePhysicsSettingsInGame = false;
 }
 auto* ToLocal = Place<UAnimGraphNode_ComponentToLocalSpace>(Graph, X - 240, Y);

 const UEdGraphSchema* Schema = Graph->GetSchema();
 UEdGraphPin* InComponent = PosePin(ToComponent, EGPD_Input);
 UEdGraphPin* OutComponent = PosePin(ToComponent, EGPD_Output);
 UEdGraphPin* InLocal = PosePin(ToLocal, EGPD_Input);
 UEdGraphPin* OutLocal = PosePin(ToLocal, EGPD_Output);
 OriginalOutput->BreakLinkTo(RootInput);
 bool bConnected = InComponent && OutComponent && InLocal && OutLocal &&
  Schema->TryCreateConnection(OriginalOutput, InComponent);
 UEdGraphPin* Previous = OutComponent;
 for (UAnimGraphNode_KawaiiPhysics* Physics : PhysicsNodes)
 {
  UEdGraphPin* InPhysics = PosePin(Physics, EGPD_Input);
  UEdGraphPin* OutPhysics = PosePin(Physics, EGPD_Output);
  bConnected = bConnected && Previous && InPhysics && OutPhysics && Schema->TryCreateConnection(Previous, InPhysics);
  Previous = OutPhysics;
 }
 bConnected = bConnected && Previous && Schema->TryCreateConnection(Previous, InLocal) &&
  Schema->TryCreateConnection(OutLocal, RootInput);
 if (bConnected)
 {
  FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
  FKismetEditorUtilities::CompileBlueprint(Blueprint);
  if (Blueprint->Status != BS_Error)
  {
   Blueprint->MarkPackageDirty();
   return true;
  }
 }

 // Leave a failed private copy usable and unsaved; do not alter the original ABP.
 ToComponent->DestroyNode();
 for (UAnimGraphNode_KawaiiPhysics* Physics : PhysicsNodes) Physics->DestroyNode();
 ToLocal->DestroyNode();
 // Compile may reconstruct pin objects, so reacquire both surviving pins.
 UEdGraphPin* RestoreOutput = IsValid(Slot) ? PosePin(Slot, EGPD_Output) : nullptr;
 UEdGraphPin* RestoreInput = IsValid(Root) ? PosePin(Root, EGPD_Input) : nullptr;
 const bool bRestored = RestoreOutput && RestoreInput && Schema->TryCreateConnection(RestoreOutput, RestoreInput);
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
 FKismetEditorUtilities::CompileBlueprint(Blueprint);
 if (!bRestored) return Fail(TEXT("Candidate failed and private-copy restore failed; do not save this copy."));
 return Fail(bConnected ? TEXT("Candidate graph did not compile; original private-copy connection restored.") :
                          TEXT("Candidate graph connections failed; original private-copy connection restored."));
}

bool UEGHumanOriginAuthoringLibrary::InstallOnTransientArtPiece(
 USkeletalMeshComponent* Body, UAnimBlueprint* Candidate, UEGHeroDefinition* Hero, FString& Error)
{
 Error.Reset();
 auto Fail = [&Error](const TCHAR* Message) { Error = Message; return false; };
 constexpr const TCHAR* PrivatePrefix = TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Combat20261101/HumanOriginRebuild20261107/");
 UWorld* World = Body ? Body->GetWorld() : nullptr;
 if (!World || World->WorldType != EWorldType::PIE || !Body->GetOwner() ||
     !Body->GetOwner()->IsA<AEGArtPiece>() || !Body->GetOwner()->ActorHasTag(TEXT("HumanOriginClothProbe")) || !Candidate || !Hero ||
     !Candidate->GetOutermost()->GetName().StartsWith(PrivatePrefix))
  return Fail(TEXT("Expected a transient PIE ArtPiece and private candidate; no live unit or asset may be modified."));
 if (!Candidate->GeneratedClass || !Candidate->GeneratedClass->IsChildOf(UEGCombatAnimInstance::StaticClass()) ||
     !Body->GetSkeletalMeshAsset() || Candidate->TargetSkeleton != Body->GetSkeletalMeshAsset()->GetSkeleton())
  return Fail(TEXT("Candidate is not compiled for the ArtPiece body skeleton."));
 if (const auto* Current = Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()))
  if (Current->IsActionActive()) return Fail(TEXT("ArtPiece is already playing an action."));
 UClass* Previous = Body->GetAnimClass();
 Body->SetAnimInstanceClass(Candidate->GeneratedClass);
 auto* Instance = Cast<UEGCombatAnimInstance>(Body->GetAnimInstance());
 if (!Instance)
 {
  Body->SetAnimInstanceClass(Previous);
  if (auto* Restored = Cast<UEGCombatAnimInstance>(Body->GetAnimInstance())) Restored->Configure(Hero);
  return Fail(TEXT("Candidate animation instance could not be created; previous class restored."));
 }
 Instance->Configure(Hero);
 return true;
}

TArray<FEGHumanOriginNotifyInfo> UEGHumanOriginAuthoringLibrary::GetAnimationNotifySummary(
 const UAnimSequenceBase* Animation)
{
 TArray<FEGHumanOriginNotifyInfo> Result;
 if (!Animation) return Result;
 Result.Reserve(Animation->Notifies.Num());
 for (const FAnimNotifyEvent& Notify : Animation->Notifies)
 {
  FEGHumanOriginNotifyInfo& Info = Result.AddDefaulted_GetRef();
  Info.NotifyName = Notify.NotifyName;
  Info.TimeSeconds = Notify.GetTime();
  Info.DurationSeconds = Notify.GetDuration();
  if (const UAnimNotify* Event = Notify.Notify.Get())
  {
   Info.NotifyClass = Event->GetClass()->GetPathName();
   if (const auto* Skill = Cast<UEGSkillNotify>(Event)) Info.SkillEvent = Skill->Event;
  }
  else if (const UAnimNotifyState* State = Notify.NotifyStateClass.Get())
   Info.NotifyClass = State->GetClass()->GetPathName();
 }
 return Result;
}
