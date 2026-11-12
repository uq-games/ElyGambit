#include "Presentation/EGCombatAuthoringLibrary.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGSkillNotify.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Misc/PackageName.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_SequenceEvaluator.h"
#include "AnimGraphNode_BlendSpacePlayer.h"
#include "AnimGraphNode_LocalRefPose.h"
#include "AnimGraphNode_LocalToComponentSpace.h"
#include "AnimGraphNode_ComponentToLocalSpace.h"
#include "AnimGraphNode_ModifyBone.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimationGraphSchema.h"
#include "K2Node_VariableGet.h"
#include "Animation/BlendSpace1D.h"

namespace
{
 template<class T> T* Place(UEdGraph* G,int32 X=0,int32 Y=0)
 {
  auto* N=NewObject<T>(G,NAME_None,RF_Transactional);G->AddNode(N,false,false);
  N->CreateNewGuid();N->NodePosX=X;N->NodePosY=Y;N->PostPlacedNewNode();N->AllocateDefaultPins();return N;
 }
 UEdGraphPin* PosePin(UEdGraphNode* N,EEdGraphPinDirection D)
 {
  for(auto* P:N->Pins)if(P->Direction==D&&UAnimationGraphSchema::IsPosePin(P->PinType))return P;
  return nullptr;
 }
 void LinkPose(UEdGraphNode* A,UEdGraphNode* B)
 {check(PosePin(A,EGPD_Output)&&PosePin(B,EGPD_Input));PosePin(A,EGPD_Output)->MakeLinkTo(PosePin(B,EGPD_Input));}
 UEdGraph* Graph(UAnimBlueprint* BP)
 {
  TArray<UEdGraph*> All;BP->GetAllGraphs(All);
  for(auto* G:All)if(G->GetFName()==TEXT("AnimGraph"))return G;
  return nullptr;
 }
 UAnimGraphNode_Root* Root(UEdGraph* G)
 {for(UEdGraphNode* N:G->Nodes)if(auto* R=Cast<UAnimGraphNode_Root>(N))return R;return nullptr;}
 void Variable(UEdGraph* G,UClass* Owner,FName Name,UEdGraphPin* Input)
 {
  check(Input);auto* N=NewObject<UK2Node_VariableGet>(G,NAME_None,RF_Transactional);
  N->VariableReference.SetSelfMember(Name);G->AddNode(N,false,false);N->CreateNewGuid();
  N->NodePosX=-400;N->AllocateDefaultPins();N->GetValuePin()->MakeLinkTo(Input);
 }
 UAnimBlueprint* Blueprint(const FString& Path,USkeletalMesh* Mesh,UClass* Parent)
 {
  if(!Mesh||!FPackageName::IsValidLongPackageName(Path)||FindPackage(nullptr,*Path))return nullptr;
  auto* BP=Cast<UAnimBlueprint>(FKismetEditorUtilities::CreateBlueprint(Parent,CreatePackage(*Path),
    FName(*FPackageName::GetLongPackageAssetName(Path)),BPTYPE_Normal,UAnimBlueprint::StaticClass(),UAnimBlueprintGeneratedClass::StaticClass()));
  if(BP){BP->TargetSkeleton=Mesh->GetSkeleton();BP->SetPreviewMesh(Mesh);FAssetRegistryModule::AssetCreated(BP);}
  return BP;
 }
 bool Compile(UAnimBlueprint* BP)
 {FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);BP->MarkPackageDirty();return BP->Status!=BS_Error;}
}
#endif

UAnimBlueprint* UEGCombatAuthoringLibrary::CreateRuntimeBlueprint(const FString& Path,USkeletalMesh* Mesh,UAnimSequence* Idle,bool bSecondary)
{
#if WITH_EDITOR
 if(!Mesh||!Idle||Mesh->GetSkeleton()!=Idle->GetSkeleton())return nullptr;
 UClass* Parent=bSecondary?UEGCombatPartAnimInstance::StaticClass():UEGCombatAnimInstance::StaticClass();
 auto* BP=Blueprint(Path,Mesh,Parent);if(!BP)return nullptr;
 auto* G=Graph(BP);if(!G||!Root(G))return nullptr;
 auto Evaluator=[&](UEdGraph* Graph,int32 State)
 {
  auto* N=Place<UAnimGraphNode_SequenceEvaluator>(Graph,-200,0);
  N->Node.SetSequence(Idle);N->Node.SetTeleportToExplicitTime(true);N->Node.SetShouldLoop(false);
  for(auto& P:N->ShowPinForProperties)if(P.PropertyName==TEXT("Sequence")||P.PropertyName==TEXT("ExplicitTime"))P.bShowPin=true;
  N->ReconstructNode();
  const TCHAR* Names[]={TEXT("Idle"),TEXT("Walk"),TEXT("Run"),TEXT("Death"),TEXT("Hit")};
  Variable(Graph,Parent,bSecondary?FName(TEXT("SourceClip")):FName(FString(Names[State])+TEXT("Clip")),N->FindPin(TEXT("Sequence")));
  Variable(Graph,Parent,bSecondary?FName(TEXT("SourceTime")):FName(FString(Names[State])+TEXT("Time")),N->FindPin(TEXT("ExplicitTime")));
  return N;
 };
 if(bSecondary)LinkPose(Evaluator(G,0),Root(G));
 else
 {
  auto* SM=Place<UAnimGraphNode_StateMachine>(G,-500,0);
  SM->EditorStateMachineGraph->Rename(TEXT("ChessPresentation"),nullptr,REN_DontCreateRedirectors);
  auto* Slot=Place<UAnimGraphNode_Slot>(G,-100,0);Slot->Node.SlotName=TEXT("FullBody");
  Mesh->GetSkeleton()->RegisterSlotNode(TEXT("FullBody"));LinkPose(SM,Slot);LinkPose(Slot,Root(G));
  auto* Machine=SM->EditorStateMachineGraph.Get();TArray<UAnimStateNode*> States;
  const TCHAR* Names[]={TEXT("Idle"),TEXT("Walk"),TEXT("Run"),TEXT("Death"),TEXT("Hit")};
  const FName Flags[]={TEXT("bIdle"),TEXT("bWalk"),TEXT("bRun"),TEXT("bDead"),TEXT("bHit")};
  for(int32 I=0;I<5;++I)
  {
   auto* State=Place<UAnimStateNode>(Machine,I*250,80);State->BoundGraph->Rename(Names[I],nullptr,REN_DontCreateRedirectors);
   PosePin(Evaluator(State->BoundGraph,I),EGPD_Output)->MakeLinkTo(State->GetPoseSinkPinInsideState());States.Add(State);
  }
  for(UEdGraphNode* N:Machine->Nodes)if(auto* Entry=Cast<UAnimStateEntryNode>(N))Entry->GetOutputPin()->MakeLinkTo(States[0]->GetInputPin());
  for(int32 A=0;A<5;++A)for(int32 B=0;B<5;++B)if(A!=B)
  {
   auto* Edge=Place<UAnimStateTransitionNode>(Machine);Edge->CreateConnections(States[A],States[B]);Edge->CrossfadeDuration=B==3?0.f:.08f;
   auto* TG=CastChecked<UAnimationTransitionGraph>(Edge->BoundGraph);Variable(TG,Parent,Flags[B],TG->GetResultNode()->FindPin(TEXT("bCanEnterTransition")));
  }
 }
 return Compile(BP)?BP:nullptr;
#else
 return nullptr;
#endif
}

bool UEGCombatAuthoringLibrary::SetSocket(USkeletalMesh* Mesh,FName Name,FName Bone,FTransform Local)
{
#if WITH_EDITOR
 if(!Mesh||Mesh->GetRefSkeleton().FindBoneIndex(Bone)==INDEX_NONE||Local.ContainsNaN())return false;
 USkeletalMeshSocket* S=Mesh->FindSocket(Name);
 if(!S){S=NewObject<USkeletalMeshSocket>(Mesh,Name,RF_Transactional);Mesh->GetMeshOnlySocketList().Add(S);}
 S->SocketName=Name;S->BoneName=Bone;S->RelativeLocation=Local.GetTranslation();
 S->RelativeRotation=Local.Rotator();S->RelativeScale=Local.GetScale3D();Mesh->MarkPackageDirty();return true;
#else
 return false;
#endif
}
UAnimBlueprint* UEGCombatAuthoringLibrary::CreateCombatBlueprint(const FString& Path,USkeletalMesh* Mesh,UAnimSequence* Idle,UAnimSequence* Walk,UAnimSequence* Run)
{
#if WITH_EDITOR
 if(!Mesh||!Idle||!Walk||!Run)return nullptr;
 auto* BP=Blueprint(Path,Mesh,UEGCombatAnimInstance::StaticClass());if(!BP)return nullptr;
 const FString BSPath=FPackageName::GetLongPackagePath(Path)+TEXT("/BS_ElysiaCombatSpeed");
 if(FindPackage(nullptr,*BSPath))return nullptr;
 auto* BS=NewObject<UBlendSpace1D>(CreatePackage(*BSPath),TEXT("BS_ElysiaCombatSpeed"),RF_Public|RF_Standalone|RF_Transactional);
 BS->SetSkeleton(Mesh->GetSkeleton());BS->SetPreviewMesh(Mesh);
 auto& Param=const_cast<FBlendParameter&>(BS->GetBlendParameter(0));Param.DisplayName=TEXT("Speed");Param.Min=0;Param.Max=380;Param.GridNum=4;
 BS->AddSample(Idle,FVector(0,0,0));BS->AddSample(Walk,FVector(160,0,0));BS->AddSample(Run,FVector(380,0,0));
 BS->ResampleData();BS->PostEditChange();FAssetRegistryModule::AssetCreated(BS);BS->MarkPackageDirty();
 auto* G=Graph(BP);if(!G||!Root(G))return nullptr;
 auto* SM=Place<UAnimGraphNode_StateMachine>(G,-450,0);SM->EditorStateMachineGraph->Rename(TEXT("Locomotion"),nullptr,REN_DontCreateRedirectors);
 auto* Slot=Place<UAnimGraphNode_Slot>(G,-100,0);Slot->Node.SlotName=TEXT("FullBody");
 Mesh->GetSkeleton()->RegisterSlotNode(TEXT("FullBody"));LinkPose(SM,Slot);LinkPose(Slot,Root(G));
 auto* Machine=SM->EditorStateMachineGraph.Get();
 TArray<UAnimStateNode*> States;
 const TCHAR* Names[]={TEXT("Idle"),TEXT("Walk"),TEXT("Run")};
 const FName Vars[]={TEXT("bIdle"),TEXT("bWalk"),TEXT("bRun")};
 for(int32 I=0;I<3;++I)
 {
  auto* State=Place<UAnimStateNode>(Machine,I*260,80);State->BoundGraph->Rename(Names[I],nullptr,REN_DontCreateRedirectors);
  auto* Player=Place<UAnimGraphNode_BlendSpacePlayer>(State->BoundGraph,-200,0);Player->Node.SetBlendSpace(BS);
  for(auto& P:Player->ShowPinForProperties)if(P.PropertyName==TEXT("X"))P.bShowPin=true;
  Player->ReconstructNode();Variable(State->BoundGraph,UEGCombatAnimInstance::StaticClass(),TEXT("Speed"),Player->FindPin(TEXT("X")));
  PosePin(Player,EGPD_Output)->MakeLinkTo(State->GetPoseSinkPinInsideState());States.Add(State);
 }
 for(UEdGraphNode* N:Machine->Nodes)if(auto* Entry=Cast<UAnimStateEntryNode>(N))Entry->GetOutputPin()->MakeLinkTo(States[0]->GetInputPin());
 for(int32 A=0;A<3;++A)for(int32 B=0;B<3;++B)if(A!=B)
 {
  auto* Edge=Place<UAnimStateTransitionNode>(Machine);Edge->CreateConnections(States[A],States[B]);Edge->CrossfadeDuration=.12f;
  auto* TG=CastChecked<UAnimationTransitionGraph>(Edge->BoundGraph);
  Variable(TG,UEGCombatAnimInstance::StaticClass(),Vars[B],TG->GetResultNode()->FindPin(TEXT("bCanEnterTransition")));
 }
 return Compile(BP)?BP:nullptr;
#else
 return nullptr;
#endif
}
UAnimBlueprint* UEGCombatAuthoringLibrary::CreateBowBlueprint(const FString& Path,USkeletalMesh* Mesh)
{
#if WITH_EDITOR
 auto* BP=Blueprint(Path,Mesh,UEGBowAnimInstance::StaticClass());if(!BP)return nullptr;
 auto* G=Graph(BP);if(!G||!Root(G))return nullptr;
 auto* Ref=Place<UAnimGraphNode_LocalRefPose>(G,-1100,0);auto* ToComponent=Place<UAnimGraphNode_LocalToComponentSpace>(G,-900,0);LinkPose(Ref,ToComponent);
 UEdGraphNode* Previous=ToComponent;
 const FName Bones[]={TEXT("EG_StringNock"),TEXT("EG_LimbUpper"),TEXT("EG_LimbLower")};
 for(int32 I=0;I<3;++I)
 {
  auto* N=Place<UAnimGraphNode_ModifyBone>(G,-700+I*200,0);N->Node.BoneToModify.BoneName=Bones[I];
  N->Node.TranslationMode=BMM_Additive;N->Node.TranslationSpace=BCS_ComponentSpace;
  N->Node.RotationMode=BMM_Ignore;N->Node.ScaleMode=BMM_Ignore;
  N->ReconstructNode();LinkPose(Previous,N);
  Variable(G,UEGBowAnimInstance::StaticClass(),I==0?TEXT("StringOffset"):TEXT("LimbOffset"),N->FindPin(TEXT("Translation")));Previous=N;
 }
 auto* ToLocal=Place<UAnimGraphNode_ComponentToLocalSpace>(G,0,0);LinkPose(Previous,ToLocal);LinkPose(ToLocal,Root(G));
 return Compile(BP)?BP:nullptr;
#else
 return nullptr;
#endif
}
UAnimMontage* UEGCombatAuthoringLibrary::CreateActionMontage(const FString& Path,UAnimSequence* Clip,float Seconds,FName Event,bool Hold)
{
#if WITH_EDITOR
 if(!Clip||!FPackageName::IsValidLongPackageName(Path)||FindPackage(nullptr,*Path))return nullptr;
 auto* M=NewObject<UAnimMontage>(CreatePackage(*Path),FName(*FPackageName::GetLongPackageAssetName(Path)),RF_Public|RF_Standalone|RF_Transactional);
 M->SetSkeleton(Clip->GetSkeleton());M->SetPreviewMesh(Clip->GetPreviewMesh());
 FSlotAnimationTrack Track;Track.SlotName=TEXT("FullBody");FAnimSegment Segment;
 Segment.SetAnimReference(Clip);Segment.StartPos=0;Segment.AnimStartTime=0;Segment.AnimEndTime=Clip->GetPlayLength();Segment.AnimPlayRate=1;Segment.LoopingCount=1;
 Track.AnimTrack.AnimSegments.Add(Segment);M->SlotAnimTracks.Reset();M->SlotAnimTracks.Add(Track);M->SetCompositeLength(Clip->GetPlayLength());
 FCompositeSection Section;Section.SectionName=TEXT("Default");Section.Link(M,0);M->CompositeSections.Add(Section);
 M->BlendIn.SetBlendTime(.1f);M->BlendOut.SetBlendTime(.12f);M->BlendOutTriggerTime=-1.f;M->bEnableAutoBlendOut=!Hold;M->bEnableRootMotionTranslation=false;M->bEnableRootMotionRotation=false;
 M->AnimNotifyTracks.Add(FAnimNotifyTrack(TEXT("Combat"),FLinearColor::Yellow));
 if(!Event.IsNone())
 {
  auto* Notify=NewObject<UEGSkillNotify>(M);Notify->Event=Event;
  FAnimNotifyEvent N;N.Notify=Notify;N.NotifyName=Event;N.TrackIndex=0;N.Link(M,FMath::Clamp(Seconds,0.f,M->GetPlayLength()));
  N.TriggerWeightThreshold=.01f;M->Notifies.Add(N);
 }
 if(!Hold)
 {
  auto* Notify=NewObject<UEGSkillNotify>(M);Notify->Event=TEXT("Finish");FAnimNotifyEvent N;
  N.Notify=Notify;N.NotifyName=TEXT("Finish");N.Link(M,FMath::Max(0.f,M->GetPlayLength()-.14f));M->Notifies.Add(N);
 }
 M->PostEditChange();FAssetRegistryModule::AssetCreated(M);M->MarkPackageDirty();return M;
#else
 return nullptr;
#endif
}
bool UEGCombatAuthoringLibrary::RepairMontageTrack(UAnimMontage* M,UAnimSequence* Clip)
{
#if WITH_EDITOR
 if(!M||!Clip||M->GetSkeleton()!=Clip->GetSkeleton())return false;
 FSlotAnimationTrack Track;Track.SlotName=TEXT("FullBody");FAnimSegment Segment;
 Segment.SetAnimReference(Clip);Segment.StartPos=0;Segment.AnimStartTime=0;
 Segment.AnimEndTime=Clip->GetPlayLength();Segment.AnimPlayRate=1;Segment.LoopingCount=1;
 Track.AnimTrack.AnimSegments.Add(Segment);M->SlotAnimTracks.Reset();M->SlotAnimTracks.Add(Track);
 M->SetCompositeLength(Clip->GetPlayLength());M->BlendOutTriggerTime=-1.f;
 M->PostEditChange();M->MarkPackageDirty();return true;
#else
 return false;
#endif
}
bool UEGCombatAuthoringLibrary::SetActionSequence(UAnimMontage* M,const TArray<UAnimSequence*>& Clips)
{
#if WITH_EDITOR
 if(!M||Clips.IsEmpty())return false;
 for(auto* Clip:Clips)if(!Clip||Clip->GetSkeleton()!=M->GetSkeleton())return false;
 FSlotAnimationTrack Track;Track.SlotName=TEXT("FullBody");float Time=0;
 for(auto* Clip:Clips)
 {
  FAnimSegment Segment;Segment.SetAnimReference(Clip);Segment.StartPos=Time;Segment.AnimStartTime=0;
  Segment.AnimEndTime=Clip->GetPlayLength();Segment.AnimPlayRate=1;Segment.LoopingCount=1;
  Track.AnimTrack.AnimSegments.Add(Segment);Time+=Clip->GetPlayLength();
 }
 M->SlotAnimTracks.Reset();M->SlotAnimTracks.Add(Track);M->SetCompositeLength(Time);
 M->CompositeSections.Reset();FCompositeSection Section;Section.SectionName=TEXT("Default");Section.Link(M,0);M->CompositeSections.Add(Section);
 M->BlendIn.SetBlendTime(.08f);M->BlendOut.SetBlendTime(.08f);M->BlendOutTriggerTime=0;M->bEnableAutoBlendOut=true;
 M->PostEditChange();M->MarkPackageDirty();return true;
#else
 return false;
#endif
}
bool UEGCombatAuthoringLibrary::SetActionWindow(UAnimMontage* M,UAnimSequence* Clip,float Start,float End,float In,float Out,float Duration)
{
#if WITH_EDITOR
 if(!M||!Clip||M->GetSkeleton()!=Clip->GetSkeleton()||!FMath::IsFinite(Start)||!FMath::IsFinite(End)||
    !FMath::IsFinite(In)||!FMath::IsFinite(Out)||!FMath::IsFinite(Duration)||Duration<0||Start<0||End>Clip->GetPlayLength()||End<=Start||In<0||Out<0)return false;
 const float Length=Duration>0?Duration:End-Start;
 if(Out>=Length)return false;
 FSlotAnimationTrack Track;Track.SlotName=TEXT("FullBody");FAnimSegment Segment;
 Segment.SetAnimReference(Clip);Segment.StartPos=0;Segment.AnimStartTime=Start;Segment.AnimEndTime=End;Segment.AnimPlayRate=(End-Start)/Length;Segment.LoopingCount=1;
 Track.AnimTrack.AnimSegments.Add(Segment);M->SlotAnimTracks.Reset();M->SlotAnimTracks.Add(Track);M->SetCompositeLength(Length);
 M->CompositeSections.Reset();FCompositeSection Section;Section.SectionName=TEXT("Default");Section.Link(M,0);M->CompositeSections.Add(Section);
 M->BlendIn.SetBlendTime(In);M->BlendOut.SetBlendTime(Out);M->BlendOutTriggerTime=0;M->bEnableAutoBlendOut=true;
 M->PostEditChange();M->MarkPackageDirty();return true;
#else
 return false;
#endif
}

bool UEGCombatAuthoringLibrary::SetActionPhases(UAnimMontage* M,UAnimSequence* Clip,float End,float Phase,float Total,float In,float Out)
{
#if WITH_EDITOR
 if(!Clip||!FMath::IsFinite(End)||!FMath::IsFinite(Phase)||!FMath::IsFinite(Total)
    ||End<=0||End>=Clip->GetPlayLength()||Phase<=0||Total<=Phase)return false;
 if(!SetActionWindow(M,Clip,0,Clip->GetPlayLength(),In,Out,Total))return false;
 auto& Segments=M->SlotAnimTracks[0].AnimTrack.AnimSegments;
 FAnimSegment Recovery=Segments[0];
 Segments[0].AnimEndTime=End;Segments[0].AnimPlayRate=End/Phase;
 Recovery.StartPos=Phase;Recovery.AnimStartTime=End;
 Recovery.AnimPlayRate=(Clip->GetPlayLength()-End)/(Total-Phase);
 Segments.Add(Recovery);M->SetCompositeLength(Total);
 M->PostEditChange();M->MarkPackageDirty();return true;
#else
 return false;
#endif
}

bool UEGCombatAuthoringLibrary::BindLocomotionRate(UAnimBlueprint* BP)
{
#if WITH_EDITOR
 if(!BP||!BP->ParentClass||!BP->ParentClass->IsChildOf(UEGCombatAnimInstance::StaticClass()))return false;
 TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);int32 Count=0;
 TArray<UAnimGraphNode_BlendSpacePlayer*> Players;
 for(auto* G:Graphs)for(UEdGraphNode* N:G->Nodes)if(auto* Player=Cast<UAnimGraphNode_BlendSpacePlayer>(N))Players.Add(Player);
 for(auto* Player:Players)
 {
  auto* G=Player->GetGraph();
  for(auto& P:Player->ShowPinForProperties)if(P.PropertyName==TEXT("PlayRate"))P.bShowPin=true;
  Player->ReconstructNode();auto* Input=Player->FindPin(TEXT("PlayRate"));if(!Input)return false;
  if(Input->LinkedTo.Num()==1)if(auto* Getter=Cast<UK2Node_VariableGet>(Input->LinkedTo[0]->GetOwningNode()))
   if(Getter->VariableReference.GetMemberName()==TEXT("LocomotionRate")){++Count;continue;}
  Input->BreakAllPinLinks();Variable(G,UEGCombatAnimInstance::StaticClass(),TEXT("LocomotionRate"),Input);++Count;
 }
 return Count>0&&Compile(BP);
#else
 return false;
#endif
}

