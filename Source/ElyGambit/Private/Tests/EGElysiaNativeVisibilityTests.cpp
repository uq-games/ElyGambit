#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "EGProductionTestAssets.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Presentation/EGCombatVisual.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimMontage.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Data/EGCardDefinition.h"
#include "Game/EGCardManager.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGPlacementVisuals.h"
#include "Actor/EGBoard.h"
#include "GameFramework/WorldSettings.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/StrongObjectPtr.h"
#include "EGNativeRendererLibrary.h"
#include "RenderingThread.h"

namespace ElysiaNativeVisibility
{
// Resolve the production card instead of coupling these renderer lifecycle
// checks to the retired Native20261025 mesh, graph, animation and material.
UEGHeroDefinition* ProductionHero(FAutomationTestBase& Test)
{
 auto* Run=EGProductionTestAssets::ResolveRun(Test);
 if(!Test.TestNotNull(TEXT("Production run"),Run)||!Test.TestNotNull(TEXT("Starting card"),Run->StartingCard.Get()))return nullptr;
 auto* Hero=Run->StartingCard->HeroDefinition.Get();
 if(!Test.TestNotNull(TEXT("Starting hero"),Hero)||!Test.TestTrue(TEXT("Starting hero is Elysia"),Hero->HeroKind==EEGHeroKind::Elysia))return nullptr;
 if(!Test.TestNotNull(TEXT("Elysia presentation"),Hero->PresentationDefinition.Get()))return nullptr;
 return Hero;
}

TSharedPtr<FJsonValue> Read(const TCHAR* Relative)
{
 FString Text;
 if (!FFileHelper::LoadFileToString(Text, *(FPaths::ProjectSavedDir()/TEXT("ElysiaNative20261025")/Relative))) return nullptr;
 TSharedPtr<FJsonValue> Value;
 return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Value) ? Value : nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaNativeBooleanCurveTest,
 "ElyGambit.ElysiaNative.VisibilityMatchesUnitySamples",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaNativeBooleanCurveTest::RunTest(const FString&)
{
 auto Manifest=ElysiaNativeVisibility::Read(TEXT("Reports/source-weapon-clips.json"));
 auto Index=ElysiaNativeVisibility::Read(TEXT("Reports/unity-skin-prefab-probes.json"));
 if(!TestTrue(TEXT("Source and native Unity evidence available"),Manifest.IsValid()&&Index.IsValid()))return false;
 FString ProbePath=Index->AsArray()[0]->AsObject()->GetStringField(TEXT("file")),Text;
 if(!TestTrue(TEXT("Read native result"),FFileHelper::LoadFileToString(Text,*ProbePath)))return false;
 TSharedPtr<FJsonObject> Probe;
 if(!TestTrue(TEXT("Parse native result"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Probe)))return false;
 FEGSourcePreviewPart Source;Source.bNonzeroSourceVisibility=true;int32 Checked=0;
 for(const auto& ClipValue:Probe->GetArrayField(TEXT("weapon_visibility_checks")))
 {
  const auto Clip=ClipValue->AsObject();const FString Name=Clip->GetStringField(TEXT("clip"));
  const auto* Match=Manifest->AsArray().FindByPredicate([&](const auto& V){return V->AsObject()->GetObjectField(TEXT("source"))->GetStringField(TEXT("name"))==Name;});
  if(!TestNotNull(TEXT("Clip provenance present"),Match))return false;
  const auto Row=(*Match)->AsObject();
  TestEqual(TEXT("Native result sampled the source-identified clip"),Clip->GetStringField(TEXT("source_sha256")),Row->GetObjectField(TEXT("source"))->GetStringField(TEXT("sha256")));
  for(const auto& CheckValue:Clip->GetArrayField(TEXT("checks")))
  {
   const auto Check=CheckValue->AsObject();const FString Path=Check->GetStringField(TEXT("path")),Attribute=Check->GetStringField(TEXT("attribute"));
   const auto* CurveValue=Row->GetArrayField(TEXT("curves")).FindByPredicate([&](const auto& V){auto C=V->AsObject();return C->GetStringField(TEXT("path"))==Path&&C->GetStringField(TEXT("attribute"))==Attribute;});
   if(!TestNotNull(TEXT("Native binding exists in original source"),CurveValue))return false;
   TStrongObjectPtr<UCurveFloat> Curve(NewObject<UCurveFloat>());
   for(const auto& KeyValue:(*CurveValue)->AsObject()->GetObjectField(TEXT("curve"))->GetArrayField(TEXT("m_Curve")))
   {
    auto K=KeyValue->AsObject();auto H=Curve->FloatCurve.AddKey(K->GetNumberField(TEXT("time")),K->GetNumberField(TEXT("value")));
    auto& Key=Curve->FloatCurve.GetKey(H);Key.InterpMode=RCIM_Cubic;Key.TangentMode=RCTM_User;Key.ArriveTangent=K->GetNumberField(TEXT("inSlope"));Key.LeaveTangent=K->GetNumberField(TEXT("outSlope"));
   }
   TestEqual(*FString::Printf(TEXT("Original Unity visibility %s %s.%s"),*Name,*Path,*Attribute),Source.EvaluateVisibility(Curve.Get(),Check->GetNumberField(TEXT("time"))),Check->GetBoolField(TEXT("actual")));++Checked;
  }
 }
 TestEqual(TEXT("All existing-target source visibility samples covered"),Checked,205);
 FEGSourcePreviewPart Legacy;
 TStrongObjectPtr<UCurveFloat> Constant(NewObject<UCurveFloat>());Constant->FloatCurve.AddKey(0,.25f);
 TestFalse(TEXT("Existing assets retain the half threshold"),Legacy.EvaluateVisibility(Constant.Get(),0));
 TestTrue(TEXT("Native binding shows a nonzero value below half"),Source.EvaluateVisibility(Constant.Get(),0));
 TestTrue(TEXT("An absent curve retains normal visibility"),Source.EvaluateVisibility(nullptr,0));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaNativeVisibilityActorsTest,
 "ElyGambit.ElysiaNative.VisibilityViewerAndCombat",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaNativeVisibilityActorsTest::RunTest(const FString&)
{
 auto* Hero=ElysiaNativeVisibility::ProductionHero(*this);if(!Hero)return false;
 auto* Mesh=Hero->GetMesh().LoadSynchronous();
 auto* Animation=Hero->GetAttackAnimation().LoadSynchronous();
 auto* AnimationClass=Hero->GetAnimationClass().LoadSynchronous();
 if(!TestNotNull(TEXT("Production body"),Mesh)||!TestNotNull(TEXT("Production attack"),Animation)||!TestNotNull(TEXT("Production animation graph"),AnimationClass))return false;
 if(!TestTrue(TEXT("Body and attack use the same skeleton"),Mesh->GetSkeleton()==Animation->GetSkeleton()))return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();auto* World=Instance->GetWorld();
 ON_SCOPE_EXIT{Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 // Exercise real destruction callbacks, rather than an uninitialized editor
 // world where RouteEndPlay deliberately skips actor EndPlay.
 World->InitializeActorsForPlay(FURL());World->BeginPlay();
 TStrongObjectPtr<UEGSourcePreviewDefinition> Definition(NewObject<UEGSourcePreviewDefinition>());
 TStrongObjectPtr<UCurveFloat> Curve(NewObject<UCurveFloat>());Curve->FloatCurve.AddKey(0,.25f);
 FEGSourcePreviewPart Part;Part.PartId=TEXT("ProductionBody");Part.Mesh=Mesh;Part.Duration=1;Part.Visibility=Curve.Get();Part.bNonzeroSourceVisibility=true;
 Part.SectionVisibility.Add(0,Curve.Get());
 Part.AnimationBlueprintClass=AnimationClass;
 Part.Animation=Animation;
 FEGSourcePreviewMotion Motion;Motion.Duration=1;Motion.Parts.Add(Part);Definition->Motions.Add(Motion);
 auto* Viewer=World->SpawnActor<AEGSourceCharacterPreview>();Viewer->bAutoPlay=false;Viewer->bLoop=false;Viewer->Definition=Definition.Get();Viewer->SetPreviewTime(.25f);
 auto* Combat=World->SpawnActor<AEGCombatVisual>();Combat->bAutoPlay=false;Combat->bLoop=false;Combat->Definition=Definition.Get();Combat->SetSourceTime(.25f);
 auto Check=[&](AActor* Actor,bool Visible){TArray<USkeletalMeshComponent*> Parts;Actor->GetComponents(Parts);if(!TestEqual(TEXT("One owned presentation part"),Parts.Num(),1))return;TestEqual(TEXT("Source boolean reaches displayed component"),Parts[0]->IsVisible(),Visible);TestEqual(TEXT("Source renderer section uses the same boolean interpretation"),Parts[0]->IsMaterialSectionShown(0,0),Visible);TestTrue(TEXT("Actor owns presentation component"),Parts[0]->GetOwner()==Actor);TestFalse(TEXT("Presentation component does not replicate"),Parts[0]->GetIsReplicated());};
 Check(Viewer,true);Check(Combat,true);
 Definition->Motions[0].Parts[0].bNonzeroSourceVisibility=false;Viewer->SetPreviewTime(.25f);Combat->SetSourceTime(.25f);Check(Viewer,false);Check(Combat,false);
 Definition->Motions[0].Parts[0].bNonzeroSourceVisibility=true;Curve->FloatCurve.Reset();Curve->FloatCurve.AddKey(0,0);Viewer->SetPreviewTime(.25f);Combat->SetSourceTime(.25f);Check(Viewer,false);Check(Combat,false);
 auto* SourceMaterial=Mesh->GetMaterials().IsEmpty()?nullptr:Mesh->GetMaterials()[0].MaterialInterface.Get();
 if(!TestNotNull(TEXT("Production body material"),SourceMaterial))return false;
 auto& BoundPart=Definition->Motions[0].Parts[0];BoundPart.bUseSourceRenderBindings=true;BoundPart.bSourceDepthOnly=true;BoundPart.SourceTranslucencySortPriority=17;BoundPart.MaterialOverrides.Add(0,SourceMaterial);
 Curve->FloatCurve.Reset();Curve->FloatCurve.AddKey(0,1);Viewer->SetPreviewTime(.25f);Combat->SetSourceTime(.25f);
 auto CheckPass=[&](AActor* Actor){TArray<USkeletalMeshComponent*> Parts;Actor->GetComponents(Parts);if(!TestEqual(TEXT("Native pass reuses one source component"),Parts.Num(),1))return;
  auto* Component=Parts[0];TestFalse(TEXT("Depth-only source draw does not write main color"),Component->bRenderInMainPass);TestTrue(TEXT("Depth-only source draw participates in depth"),Component->bRenderInDepthPass);TestEqual(TEXT("Source sorting order preserved"),Component->TranslucencySortPriority,17);
  auto* Material=Cast<UMaterialInstanceDynamic>(Component->GetMaterial(0));if(TestNotNull(TEXT("Per-component dynamic material is strongly owned by component"),Material)){TestTrue(TEXT("Material override uses the requested source program"),Material->Parent==SourceMaterial);auto* Before=Material;
   if(auto* Preview=Cast<AEGSourceCharacterPreview>(Actor))Preview->SetPreviewTime(.5f);if(auto* Visual=Cast<AEGCombatVisual>(Actor))Visual->SetSourceTime(.5f);TestTrue(TEXT("Repeated evaluation does not allocate another MID"),Component->GetMaterial(0)==Before);}
 };
 CheckPass(Viewer);CheckPass(Combat);
 Viewer->Destroy();Combat->Destroy();
 // The action actor borrows the displayed chess body; it must restore it and
 // must never destroy or take ownership of that component when the action ends.
 auto* BodyOwner=World->SpawnActor<AActor>();
 auto* Body=NewObject<USkeletalMeshComponent>(BodyOwner);BodyOwner->AddInstanceComponent(Body);BodyOwner->SetRootComponent(Body);Body->SetSkeletalMesh(Mesh);Body->RegisterComponent();
 Body->SetRenderInMainPass(true);Body->SetRenderInDepthPass(false);Body->SetTranslucentSortPriority(3);
 auto* OriginalMaterial=Body->GetMaterial(0);
 auto* Action=World->SpawnActor<AEGCombatVisual>();Action->bAutoPlay=false;Action->Definition=Definition.Get();Action->BindBody(Body,false);
 if(!Action->HasActorBegunPlay())Action->DispatchBeginPlay();Action->SetSourceTime(.25f);
 TestTrue(TEXT("Lifecycle probe Actor is initialized"),Action->IsActorInitialized());TestTrue(TEXT("Lifecycle probe entered BeginPlay"),Action->HasActorBegunPlay());
 TestTrue(TEXT("Action references the displayed body"),Action->BodyMesh==Body);TestTrue(TEXT("Borrowed body keeps original Actor ownership"),Body->GetOwner()==BodyOwner);
 TestFalse(TEXT("Source depth pass applies to borrowed body"),Body->bRenderInMainPass);TestTrue(TEXT("Source depth pass enables borrowed depth participation"),Body->bRenderInDepthPass);
 TestTrue(TEXT("Action destroy succeeds through the world"),Action->Destroy());
 TestTrue(TEXT("Ending source action preserves the chess component"),IsValid(Body)&&Body->GetOwner()==BodyOwner);
 TestTrue(TEXT("Ending source action restores main color participation"),Body->bRenderInMainPass);TestFalse(TEXT("Ending source action restores original depth flag"),Body->bRenderInDepthPass);
 TestEqual(TEXT("Ending source action restores original sorting"),Body->TranslucencySortPriority,3);TestTrue(TEXT("Ending source action restores original material reference"),Body->GetMaterial(0)==OriginalMaterial);
 BodyOwner->Destroy();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaNativePersistentPassTest,
 "ElyGambit.ElysiaNative.PersistentPassFollowsActionLifetime",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaNativePersistentPassTest::RunTest(const FString&)
{
 auto* Hero=ElysiaNativeVisibility::ProductionHero(*this);if(!Hero)return false;
 auto* Mesh=Hero->GetMesh().LoadSynchronous();
 if(!TestNotNull(TEXT("Production body"),Mesh))return false;
 TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));Instance->InitializeStandalone();auto* World=Instance->GetWorld();
 ON_SCOPE_EXIT{Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 World->InitializeActorsForPlay(FURL());World->BeginPlay();
 auto* Owner=World->SpawnActor<AActor>();
 auto* Body=NewObject<USkeletalMeshComponent>(Owner);Owner->AddInstanceComponent(Body);Owner->SetRootComponent(Body);Body->SetSkeletalMesh(Mesh);Body->RegisterComponent();
 Body->SetRenderInMainPass(true);Body->SetRenderInDepthPass(false);
 auto* Material=Body->GetMaterial(0);
 TStrongObjectPtr<UEGNativeMeshPassDefinition> Native(NewObject<UEGNativeMeshPassDefinition>());
 FEGNativeMeshPass Pass;Pass.Material=Material;Native->Passes.Add(Pass);
 TStrongObjectPtr<UEGSourcePreviewDefinition> Definition(NewObject<UEGSourcePreviewDefinition>());
 FEGSourcePreviewMotion Motion;Motion.Duration=1.f;
 FEGSourcePreviewPart Part;Part.PartId=TEXT("Body");Part.Mesh=Mesh;Part.Duration=1.f;Part.NativePassDefinition=Native.Get();
 Motion.Parts.Add(Part);Definition->Motions.Add(Motion);
 auto* Action=World->SpawnActor<AEGCombatVisual>();Action->bAutoPlay=false;Action->Definition=Definition.Get();Action->BindBody(Body,false);
 if(!Action->HasActorBegunPlay())Action->DispatchBeginPlay();
 TestTrue(TEXT("Native lifetime probe actor is initialized"),Action->IsActorInitialized());
 TestTrue(TEXT("Native lifetime probe entered BeginPlay"),Action->HasActorBegunPlay());
 Action->SetSourceTime(.1f);
 TInlineComponentArray<UEGNativeMeshPassComponent*> Components(Owner);
 if(!TestEqual(TEXT("Data-driven action creates exactly one native attachment"),Components.Num(),1))return false;
 auto* Binding=Components[0];
 TestTrue(TEXT("Action is lifetime owner while chess Actor still owns the component"),Binding->IsBindingOwnedBy(Action)&&Binding->GetOwner()==Owner);
 for(int32 Sample=0;Sample<5;++Sample)Action->SetSourceTime(.1f*Sample);
 TInlineComponentArray<UEGNativeMeshPassComponent*> AfterSamples(Owner);
 TestTrue(TEXT("Repeated samples reuse the same attachment and mesh"),AfterSamples.Num()==1&&AfterSamples[0]==Binding&&Action->BodyMesh==Body);
 TestFalse(TEXT("Native body suppresses ordinary color"),Body->bRenderInMainPass);
 UEGNativeRendererLibrary::ReleaseSourceDefinition(Body,Owner);
 TestTrue(TEXT("Foreign lifetime cannot release action rendering"),IsValid(Binding));
 Action->Destroy();FlushRenderingCommands();
 TInlineComponentArray<UEGNativeMeshPassComponent*> AfterEnd(Owner);
 TestEqual(TEXT("Ending action releases borrowed-body native attachment"),AfterEnd.Num(),0);
 TestTrue(TEXT("Ending action restores chess main draw"),Body->bRenderInMainPass);
 TestFalse(TEXT("Ending action restores original depth flag after native cleanup"),Body->bRenderInDepthPass);
 TestTrue(TEXT("Ending action preserves actual chess component and material"),IsValid(Body)&&Body->GetMaterial(0)==Material&&Body->GetOwner()==Owner);
 Owner->Destroy();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaReimportBodyCancellationTest,
 "ElyGambit.ElysiaReimport.ProductionBodySurvivesAttackCancel",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaReimportBodyCancellationTest::RunTest(const FString&)
{
 auto* HeroDefinition=ElysiaNativeVisibility::ProductionHero(*this);if(!HeroDefinition)return false;
 auto* P=HeroDefinition->PresentationDefinition.Get();
 if(!TestEqual(TEXT("Five gameplay attack assemblies"),P->AttackAssemblies.Num(),5))return false;
 auto* NormalBow=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assets/Characters/Elysia/Reimport20261101/Meshes/SK_C1_Bow.SK_C1_Bow"));
 if(!TestNotNull(TEXT("Normal bow mesh"),NormalBow))return false;
 for(int32 I=0;I<P->AttackAssemblies.Num();++I)
 {
  auto* Assembly=P->AttackAssemblies[I].LoadSynchronous();
  if(!TestNotNull(TEXT("Attack assembly"),Assembly)||!TestEqual(TEXT("One motion per attack"),Assembly->Motions.Num(),1))return false;
  const auto& Parts=Assembly->Motions[0].Parts;
  const auto* Bow=Parts.FindByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Bow");});
  if(!TestNotNull(TEXT("Every attack contains the normal bow"),Bow))return false;
  TestTrue(TEXT("Attack uses normal bow mesh"),Bow->Mesh==NormalBow);
  const auto* BodyPart=Parts.FindByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Body");});
  if(!TestNotNull(TEXT("Attack assembly contains the body"),BodyPart))return false;
  TestTrue(TEXT("Body uses the corresponding formal attack animation"),P->AttackSequence.IsValidIndex(I)&&BodyPart->Animation==P->AttackSequence[I].Animation.LoadSynchronous());
  TestTrue(TEXT("Normal bow is the static left-hand attachment"),!Bow->Animation&&Bow->ParentPartId==TEXT("Body")&&Bow->AttachBone==TEXT("Bip001-L-Hand")&&Bow->bVisible);
  TestTrue(TEXT("Bow attachment has a valid unscaled grip"),Bow->Transform.GetTranslation().Equals(FVector::ZeroVector,.01f)&&Bow->Transform.GetScale3D().Equals(FVector::OneVector,.01f));
  TestTrue(TEXT("Bow attachment targets a real body bone"),BodyPart->Mesh&&BodyPart->Mesh->GetRefSkeleton().FindBoneIndex(Bow->AttachBone)!=INDEX_NONE);
  TestFalse(TEXT("Attack excludes the unsupported F4 bow"),Parts.ContainsByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Bow_F4")||(Part.Mesh&&Part.Mesh->GetName()==TEXT("SK_C1_Bow_F4"));}));
 }
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* GM=W->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("Fixture game mode"),GM))return false;
 GM->UnitClass=AEGDemoCombatUnit::StaticClass();GM->RunDefinition=EGProductionTestAssets::ResolveRun(*this);
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
 Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestNotNull(TEXT("Starting card"),GM->RunDefinition?GM->RunDefinition->StartingCard.Get():nullptr))return false;
 Cards->GrantCard(GM->RunDefinition->StartingCard.Get());
 if(!TestTrue(TEXT("Starting card granted"),!Cards->GetHand().IsEmpty()))return false;
 Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2});GM->EnterCombat();
 AEGChessUnit* Hero=nullptr;AEGChessUnit* Enemy=nullptr;
 for(auto* Unit:GM->GetCombatUnits())if(Unit&&Unit->GetDefinition())
 {
  if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia)Hero=Unit;
  else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy&&!Enemy)Enemy=Unit;
 }
 if(!TestNotNull(TEXT("Deployed production Elysia"),Hero)||!TestNotNull(TEXT("Live enemy"),Enemy))return false;
 Hero->GetCombat()->SetComponentTickEnabled(false);Enemy->SetActorLocation(Hero->GetActorLocation()+FVector(500,0,0));
 auto* Body=Hero->GetVisiblePresentationMesh();auto* Skill=Hero->GetHeroSkill();
 if(!TestNotNull(TEXT("Visible body"),Body)||!TestNotNull(TEXT("Skill component"),Skill))return false;
 auto* Graph=Cast<UEGCombatAnimInstance>(Body->GetAnimInstance());
 if(!TestNotNull(TEXT("Actual combat animation graph"),Graph))return false;
 auto* ExpectedMesh=HeroDefinition->GetMesh().LoadSynchronous();
 if(!TestNotNull(TEXT("Production mesh"),ExpectedMesh))return false;
 TestTrue(TEXT("Deployed body uses production mesh"),Body->GetSkeletalMeshAsset()==ExpectedMesh);
 // Combat borrows the deployed piece's body; AttachPiece attaches that actor
 // to the combat unit while preserving the piece's component ownership.
 auto* Piece=Cast<AEGDeployedPiece>(Body->GetOwner());
 if(!TestNotNull(TEXT("Displayed body's deployed piece owner"),Piece))return false;
 TestTrue(TEXT("Piece owns the visible body and uses the production hero"),Piece->Body==Body&&Piece->HeroVisualDefinition==HeroDefinition);
 TestTrue(TEXT("Displayed piece is attached to the active combat unit"),Piece->GetAttachParentActor()==Hero);
 auto* Material=Body->GetMaterial(0);const bool bInitiallyVisible=Body->IsVisible();
 if(!TestTrue(TEXT("First attack starts"),Skill->BeginAction(false)))return false;
 if(!TestNotNull(TEXT("Attack plays a montage"),Skill->GetActiveMontage()))return false;
 TestTrue(TEXT("Combat graph owns the active action"),Graph->IsActionActive());
 const FName UpperArm(TEXT("Bip001-L-UpperArm"));
 if(!TestTrue(TEXT("Visible body really contains the sampled upper-arm bone"),Body->GetBoneIndex(UpperArm)!=INDEX_NONE))return false;
 const FTransform BeforePose=Body->GetSocketTransform(UpperArm,RTS_Component);
 // Advance the actual world/AnimGraph before the authored Hit notification;
 // the source clip has pronounced upper-arm movement within these 0.2 s.
 for(int32 Frame=0;Frame<2;++Frame){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}
 AEGCombatVisual* AttackRig=nullptr;
 for(TActorIterator<AEGCombatVisual> It(W);It;++It)
  if(It->GetOwner()==Hero&&!It->IsActorBeingDestroyed()&&It->Definition==P->AttackAssemblies[0].LoadSynchronous()){AttackRig=*It;break;}
 if(!TestNotNull(TEXT("First attack uses its formal runtime assembly"),AttackRig))return false;
 TArray<USkeletalMeshComponent*> Components;AttackRig->GetComponents(Components);
 USkeletalMeshComponent* RuntimeBow=nullptr;
 for(auto* Component:Components)if(Component&&Component->GetSkeletalMeshAsset()==NormalBow){RuntimeBow=Component;break;}
 if(!TestNotNull(TEXT("Runtime attack has the normal bow component"),RuntimeBow))return false;
 TestTrue(TEXT("Runtime bow is visible and attached to the borrowed body at the left hand"),RuntimeBow->IsVisible()&&RuntimeBow->GetAttachParent()==Body&&RuntimeBow->GetAttachSocketName()==TEXT("Bip001-L-Hand"));
 const FTransform FirstBow=RuntimeBow->GetComponentTransform();
 const FVector FirstHand=Body->GetSocketLocation(TEXT("Bip001-L-Hand"));
 TestTrue(TEXT("Runtime bow grip matches the moving hand"),FVector::Dist(FirstBow.GetLocation(),FirstHand)<1.f);
 for(int32 Frame=0;Frame<2;++Frame){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}
 const FTransform AfterPose=Body->GetSocketTransform(UpperArm,RTS_Component);
 const FTransform LastBow=RuntimeBow->GetComponentTransform();
 const FVector LastHand=Body->GetSocketLocation(TEXT("Bip001-L-Hand"));
 TestTrue(TEXT("Runtime bow continues following the hand"),FVector::Dist(LastBow.GetLocation(),LastHand)<1.f);
 TestTrue(TEXT("Runtime bow moves with the attack pose"),FVector::Dist(FirstBow.GetLocation(),LastBow.GetLocation())>.05f||FirstBow.GetRotation().AngularDistance(LastBow.GetRotation())>.01f);
 TWeakObjectPtr<USkeletalMeshComponent> ReleasedBow(RuntimeBow);
 TestTrue(TEXT("A running production attack changes the visible body's bone pose"),
  BeforePose.GetRotation().AngularDistance(AfterPose.GetRotation())>.01f||
  FVector::Dist(BeforePose.GetTranslation(),AfterPose.GetTranslation())>.05f);
 Skill->CancelAction();
 TestTrue(TEXT("Cancel removes the action's bow component"),!ReleasedBow.IsValid()||!ReleasedBow->IsRegistered());
 TestFalse(TEXT("Cancel releases the action"),Skill->IsBusy());
 TestFalse(TEXT("Cancel clears the graph's active action"),Graph->IsActionActive());
 TestTrue(TEXT("Cancel retains the borrowed body, piece ownership and graph"),IsValid(Body)&&IsValid(Piece)&&Body->GetOwner()==Piece&&Piece->Body==Body&&Piece->GetAttachParentActor()==Hero&&Hero->GetVisiblePresentationMesh()==Body&&Body->GetAnimInstance()==Graph);
 TestTrue(TEXT("Cancel restores the production mesh"),Body->GetSkeletalMeshAsset()==ExpectedMesh);
 TestTrue(TEXT("Cancel restores body material"),Body->GetMaterial(0)==Material);
 TestEqual(TEXT("Cancel restores body visibility"),Body->IsVisible(),bInitiallyVisible);
 if(TestTrue(TEXT("A new attack starts after cancellation"),Skill->BeginAction(false)))
 {
  TestTrue(TEXT("Replay uses the same body and graph"),Hero->GetVisiblePresentationMesh()==Body&&Body->GetAnimInstance()==Graph&&Graph->IsActionActive());
  Skill->CancelAction();
  TestFalse(TEXT("Replay cancellation releases the action"),Skill->IsBusy());
 }
 Hero->RestorePresentation();
 TestTrue(TEXT("Returning to preparation detaches the same deployed piece"),IsValid(Piece)&&Piece->GetAttachParentActor()==nullptr);
 TestTrue(TEXT("Restored piece retains its body and production mesh"),IsValid(Body)&&Body->GetOwner()==Piece&&Piece->Body==Body&&Body->GetSkeletalMeshAsset()==ExpectedMesh);
 TestTrue(TEXT("Combat unit no longer borrows the piece body"),Hero->GetVisiblePresentationMesh()==Hero->GetMesh());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaSourcePreviewRegistrationTest,
 "ElyGambit.ElysiaReimport.SourcePreviewReregistrationPreservesAnimation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaSourcePreviewRegistrationTest::RunTest(const FString&)
{
 auto* Definition=LoadObject<UEGSourcePreviewDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Reimport20261101/DA_C1_FourAttack_Preview.DA_C1_FourAttack_Preview"));
 if(!TestNotNull(TEXT("Four-attack preview definition"),Definition)||!TestEqual(TEXT("Four preview actions"),Definition->Motions.Num(),4))return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->InitializeActorsForPlay(FURL());W->BeginPlay();
 auto* Viewer=W->SpawnActorDeferred<AEGSourceCharacterPreview>(AEGSourceCharacterPreview::StaticClass(),FTransform::Identity);
 if(!TestNotNull(TEXT("Preview actor"),Viewer))return false;
 Viewer->Definition=Definition;Viewer->bAutoPlay=false;Viewer->FinishSpawning(FTransform::Identity);
 const FName Bone(TEXT("Bip001-L-UpperArm"));
 TArray<TWeakObjectPtr<USkeletalMeshComponent>> PreviousParts;
 for(int32 I=0;I<4;++I)
 {
  Viewer->MotionIndex=I;Viewer->SetPreviewTime(.2f);
  for(const auto& Previous:PreviousParts)TestTrue(TEXT("Changing motion releases previous part components"),!Previous.IsValid()||!Previous->IsRegistered());
  PreviousParts.Reset();
  TArray<USkeletalMeshComponent*> Components;Viewer->GetComponents(Components);
  const auto& Parts=Definition->Motions[I].Parts;
  if(!TestEqual(TEXT("Preview owns one component per source part"),Components.Num(),Parts.Num()))return false;
  const auto CountPart=[&Parts](FName Name){return Parts.FilterByPredicate([Name](const FEGSourcePreviewPart& Part){return Part.PartId==Name;}).Num();};
  if(!TestEqual(TEXT("One body source part"),CountPart(TEXT("Body")),1)||!TestEqual(TEXT("One normal bow source part"),CountPart(TEXT("Bow")),1))return false;
  if(!TestTrue(TEXT("At most one optional outline"),CountPart(TEXT("Outline"))<=1))return false;
  const auto* BodyPart=Parts.FindByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Body");});
  const auto* OutlinePart=Parts.FindByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Outline");});
  if(OutlinePart)TestTrue(TEXT("Outline uses the body mesh and animation"),OutlinePart->Mesh==BodyPart->Mesh&&OutlinePart->Animation==BodyPart->Animation);
  TArray<USkeletalMeshComponent*> Ordered;
  for(int32 PartIndex=0;PartIndex<Parts.Num();++PartIndex)
  {
   const FName Tag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),PartIndex));
   auto** Match=Components.FindByPredicate([Tag](const USkeletalMeshComponent* Component){return IsValid(Component)&&Component->ComponentHasTag(Tag);});
   if(!TestNotNull(TEXT("Source-indexed preview component"),Match))return false;
   auto* Component=*Match;const auto& Source=Parts[PartIndex];
   if(!TestNotNull(TEXT("Source animation"),Source.Animation.Get()))return false;
   TestTrue(TEXT("Indexed component uses the expected source mesh"),Component->GetSkeletalMeshAsset()==Source.Mesh.Get());
   TestTrue(TEXT("No duplicate component tags"),Components.FilterByPredicate([Tag](const USkeletalMeshComponent* C){return IsValid(C)&&C->ComponentHasTag(Tag);}).Num()==1);
   if(!TestTrue(TEXT("Real sampled bone exists"),Component->GetBoneIndex(Bone)!=INDEX_NONE))return false;
   Ordered.Add(Component);
   PreviousParts.Add(Component);
   // SIE property edits unregister and later register instance components.
   // Exercise the same lifecycle boundary without writing the editor map.
   Component->UnregisterComponent();Component->RegisterComponent();
  }
  Viewer->SetPreviewTime(.2f);
  TArray<FTransform> Before;
  for(int32 PartIndex=0;PartIndex<Ordered.Num();++PartIndex)
  {
   auto* Component=Ordered[PartIndex];const auto& Source=Parts[PartIndex];
   auto* Instance=Component->GetSingleNodeInstance();
   if(!TestNotNull(TEXT("Registered preview has a single-node animation instance"),Instance))return false;
   TestTrue(TEXT("Reregistered instance retains this motion's animation"),Instance->GetCurrentAsset()==Source.Animation.Get());
   TestTrue(TEXT("Reregistered body is still owned by preview actor"),Component->GetOwner()==Viewer);
   Before.Add(Component->GetSocketTransform(Bone,RTS_Component));
  }
  Viewer->SetPreviewTime(.7f);
  for(int32 J=0;J<Ordered.Num();++J)
  {
   const auto After=Ordered[J]->GetSocketTransform(Bone,RTS_Component);
   TestTrue(TEXT("Reregistered body/bow follows motion time"),Before[J].GetRotation().AngularDistance(After.GetRotation())>.01f||FVector::Dist(Before[J].GetTranslation(),After.GetTranslation())>.05f);
  }
 }
 Viewer->Destroy();
 for(const auto& Previous:PreviousParts)TestTrue(TEXT("Destroying viewer unregisters body, bow and optional outline"),!Previous.IsValid()||!Previous->IsRegistered());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaPreviewAttachedPartsTest,
 "ElyGambit.ElysiaReimport.PreviewAttachedPartsLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaPreviewAttachedPartsTest::RunTest(const FString&)
{
 auto* Hero=ElysiaNativeVisibility::ProductionHero(*this);if(!Hero)return false;
 auto* BodyMesh=Hero->GetMesh().LoadSynchronous();auto* Attack=Hero->GetAttackAnimation().LoadSynchronous();
 auto* BowMesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Assets/Characters/Elysia/Reimport20261101/Meshes/SK_C1_Bow.SK_C1_Bow"));
 if(!TestNotNull(TEXT("Source body"),BodyMesh)||!TestNotNull(TEXT("Source attack"),Attack)||!TestNotNull(TEXT("Source bow"),BowMesh))return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->InitializeActorsForPlay(FURL());W->BeginPlay();
 TStrongObjectPtr<UEGSourcePreviewDefinition> Definition(NewObject<UEGSourcePreviewDefinition>());
 FEGSourcePreviewPart Body;Body.PartId=TEXT("Body");Body.Mesh=BodyMesh;Body.Animation=Attack;Body.Duration=1.f;
 FEGSourcePreviewPart Weapon;Weapon.PartId=TEXT("Weapon");Weapon.Mesh=BowMesh;Weapon.Duration=1.f;
 Weapon.ParentPartId=TEXT("Body");Weapon.AttachBone=TEXT("Bip001-L-UpperArm");
 Weapon.Transform=FTransform(FRotator(0,15,0),FVector(5,3,-1),FVector::OneVector);
 FEGSourcePreviewPart RootPart;RootPart.PartId=TEXT("LegacyRootPart");RootPart.Mesh=BowMesh;RootPart.Duration=1.f;
 RootPart.Transform=FTransform(FVector(11,7,2));
 FEGSourcePreviewMotion Valid;Valid.Duration=1.f;Valid.Parts={Body,Weapon,RootPart};
 Definition->Motions.Add(Valid);Definition->Motions.Add(Valid);
 FEGSourcePreviewMotion Invalid;Invalid.Duration=1.f;Invalid.Parts.Add(Body);
 auto AddInvalid=[&](FName Id,FName Parent,FName Bone,bool bAnimated=false)
 {
  auto P=Weapon;P.PartId=Id;P.ParentPartId=Parent;P.AttachBone=Bone;if(bAnimated){P.Mesh=BodyMesh;P.Animation=Attack;}Invalid.Parts.Add(P);
 };
 AddInvalid(TEXT("MissingParent"),TEXT("NoSuchPart"),Weapon.AttachBone);
 AddInvalid(TEXT("MissingBone"),TEXT("Body"),TEXT("NoSuchBone"));
 AddInvalid(TEXT("Self"),TEXT("Self"),Weapon.AttachBone);
 AddInvalid(TEXT("CycleA"),TEXT("CycleB"),Weapon.AttachBone);
 AddInvalid(TEXT("CycleB"),TEXT("CycleA"),Weapon.AttachBone);
 AddInvalid(TEXT("AnimatedChild"),TEXT("Body"),Weapon.AttachBone,true);
 auto HiddenBody=Body;HiddenBody.PartId=TEXT("HiddenBody");HiddenBody.bVisible=false;Invalid.Parts.Add(HiddenBody);
 AddInvalid(TEXT("HiddenChild"),TEXT("HiddenBody"),Weapon.AttachBone);
 auto DuplicateBody=Body;DuplicateBody.PartId=TEXT("DuplicateBody");Invalid.Parts.Add(DuplicateBody);Invalid.Parts.Add(DuplicateBody);
 AddInvalid(TEXT("AmbiguousChild"),TEXT("DuplicateBody"),Weapon.AttachBone);
 AddInvalid(TEXT("BoneWithoutParent"),NAME_None,Weapon.AttachBone);
 Definition->Motions.Add(Invalid);
 auto* Viewer=W->SpawnActorDeferred<AEGSourceCharacterPreview>(AEGSourceCharacterPreview::StaticClass(),FTransform::Identity);
 if(!TestNotNull(TEXT("Source viewer"),Viewer))return false;
 Viewer->Definition=Definition.Get();Viewer->bAutoPlay=false;Viewer->FinishSpawning(FTransform::Identity);
 auto FindPart=[Viewer](int32 Index)->USkeletalMeshComponent*
 {
  TArray<USkeletalMeshComponent*> Components;Viewer->GetComponents(Components);
  const FName Tag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),Index));
  auto** P=Components.FindByPredicate([Tag](const USkeletalMeshComponent* C){return IsValid(C)&&C->ComponentHasTag(Tag);});return P?*P:nullptr;
 };
 Viewer->SetPreviewTime(.2f);
 auto* Parent=FindPart(0);auto* Child=FindPart(1);auto* Legacy=FindPart(2);
 if(!TestNotNull(TEXT("Attached parent"),Parent)||!TestNotNull(TEXT("Attached weapon"),Child)||!TestNotNull(TEXT("Unattached legacy part"),Legacy))return false;
 TestTrue(TEXT("Default parent remains the viewer root"),Legacy->GetAttachParent()==Viewer->GetRootComponent());
 TestTrue(TEXT("Default relative transform is unchanged"),Legacy->GetRelativeTransform().Equals(RootPart.Transform,.001f));
 if(!TestTrue(TEXT("Parent bone exists"),Parent->GetBoneIndex(Weapon.AttachBone)!=INDEX_NONE))return false;
 auto CheckFollow=[&]()
 {
  const FTransform Socket=Parent->GetSocketTransform(Weapon.AttachBone,RTS_World);
  const FVector Expected=Socket.GetLocation()+Socket.GetRotation().RotateVector(Weapon.Transform.GetLocation()*Viewer->GetActorScale3D());
  TestTrue(TEXT("Weapon uses socket-local centimetre offset without bone scale"),Child->GetComponentLocation().Equals(Expected,.1f));
  TestTrue(TEXT("Weapon uses socket-local rotation"),Child->GetComponentQuat().AngularDistance(Socket.GetRotation()*Weapon.Transform.GetRotation())<.001f);
  TestTrue(TEXT("Weapon scale is relative to viewer, not socket bone scale"),Child->GetComponentScale().Equals(Viewer->GetActorScale3D()*Weapon.Transform.GetScale3D(),.001f));
  TestTrue(TEXT("Valid attachment remains visible"),Child->IsVisible());
 };
 CheckFollow();const FVector Before=Child->GetComponentLocation();Viewer->SetPreviewTime(.7f);CheckFollow();
 TestTrue(TEXT("Weapon follows the animated parent bone"),FVector::Dist(Before,Child->GetComponentLocation())>.05f);
 Viewer->SetActorScale3D(FVector(1.5f));Viewer->SetPreviewTime(.7f);CheckFollow();
 Viewer->SetActorScale3D(FVector::OneVector);
 TWeakObjectPtr<USkeletalMeshComponent> OldParent=Parent,OldChild=Child;
 Viewer->NextMotion();Viewer->Play();Viewer->SetPreviewTime(.7f);
 TestTrue(TEXT("Rebuild unregisters old parent"),!OldParent.IsValid()||!OldParent->IsRegistered());
 TestTrue(TEXT("Rebuild unregisters old child"),!OldChild.IsValid()||!OldChild->IsRegistered());
 Parent=FindPart(0);Child=FindPart(1);
 if(!TestNotNull(TEXT("Rebuilt parent"),Parent)||!TestNotNull(TEXT("Rebuilt child"),Child))return false;
 CheckFollow();Viewer->Stop();Viewer->Play();Viewer->SetPreviewTime(.7f);CheckFollow();
 Viewer->NextMotion();
 if(!TestNotNull(TEXT("Valid parent survives invalid attachments"),FindPart(0)))return false;
 for(int32 Index : {1,2,3,4,5,6,8,11,12})
 {
  const auto* P=FindPart(Index);
  TestTrue(TEXT("Invalid, cyclic and animated attachments fail hidden"),!IsValid(P)||!P->IsVisible());
 }
 TArray<USkeletalMeshComponent*> Last;Viewer->GetComponents(Last);TArray<TWeakObjectPtr<USkeletalMeshComponent>> LastWeak;
 for(auto* P:Last)LastWeak.Add(P);
 Viewer->Destroy();
 for(const auto& P:LastWeak)TestTrue(TEXT("EndPlay unregisters preview-owned parts"),!P.IsValid()||!P->IsRegistered());
 return true;
}
#endif
