#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "EGSettlementTestClock.h"
#include "Animation/AnimSequence.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSettlementPresentation.h"
#include "Placement/EGPlacementVisuals.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettlementIdleLifecycle,
 "ElyGambit.Presentation.SettlementIdleLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettlementIdleLifecycle::RunTest(const FString&)
{
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius.DA_Card_Mobius"));
 auto* Clip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Assets/Characters/Mobius/Animations/Idle/CombatIdle20261024/A_Mobius_NaturalIdle.A_Mobius_NaturalIdle"));
 if(!TestNotNull(TEXT("Formal Mobius card"),Card)||!TestNotNull(TEXT("Card hero"),Card->HeroDefinition.Get())||
    !TestNotNull(TEXT("Natural idle candidate"),Clip))return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
 FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestTrue(TEXT("Fixture world has actually begun play"),W->HasBegunPlay()))return false;
 auto* Hero=DuplicateObject<UEGHeroDefinition>(Card->HeroDefinition,GetTransientPackage());
 auto* Presentation=DuplicateObject<UEGHeroPresentationDefinition>(Hero->PresentationDefinition,Hero);
 Hero->PresentationDefinition=Presentation;
 Presentation->SettlementIdleAnimation=Clip;
 Presentation->IdleAnimation=Clip;
 auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Unit has begun play"),Unit&&Unit->HasActorBegunPlay()))return false;
 if(!TestTrue(TEXT("Initialize formal-body fixture"),Unit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* Body=Unit->GetVisiblePresentationMesh();
 auto* Instance=Cast<UEGCombatAnimInstance>(Body->GetAnimInstance());
 if(!TestNotNull(TEXT("Body has combat animation instance"),Instance)||
    !TestTrue(TEXT("Candidate skeleton matches body"),Body->GetSkeletalMeshAsset()->GetSkeleton()==Clip->GetSkeleton()))return false;

 // A private, temporary fixture exercises a body plus a separate source part.
 TStrongObjectPtr<UEGSourcePreviewDefinition> Source(NewObject<UEGSourcePreviewDefinition>());
 auto& Motion=Source->Motions.AddDefaulted_GetRef();Motion.Duration=Clip->GetPlayLength();
 auto& BodyPart=Motion.Parts.AddDefaulted_GetRef();BodyPart.PartId=TEXT("Body");BodyPart.Mesh=Body->GetSkeletalMeshAsset();BodyPart.Animation=Clip;BodyPart.AnimationBlueprintClass=UEGCombatPartAnimInstance::StaticClass();
 auto& Part=Motion.Parts.AddDefaulted_GetRef();Part.PartId=TEXT("FixturePart");Part.Mesh=Body->GetSkeletalMeshAsset();
 Presentation->SettlementIdleAssembly=Source.Get();
 auto* Run=NewObject<UEGRunDefinition>();
 auto* FixtureCard=DuplicateObject<UEGCardDefinition>(Card,Run);FixtureCard->HeroDefinition=Hero;Run->StartingCard=FixtureCard;
 const auto Resources=Run->GatherRuntimeResources();
 TestTrue(TEXT("Settlement clip participates in runtime preload"),Resources.Contains(FSoftObjectPath(Clip)));
 TestTrue(TEXT("Settlement assembly participates in runtime preload"),Resources.Contains(FSoftObjectPath(Source.Get())));
 auto* Weapon=NewObject<UStaticMeshComponent>(Unit);Weapon->ComponentTags.Add(TEXT("EGWeapon"));
 Unit->AddInstanceComponent(Weapon);Weapon->SetupAttachment(Body);Weapon->RegisterComponent();Weapon->SetVisibility(true);

 auto* DefaultMaster=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia"));
 if(!TestNotNull(TEXT("Default master for impact timing"),DefaultMaster))return false;
 UAnimSequence* MainAttack=nullptr;
 if(DefaultMaster->PresentationDefinition&&!DefaultMaster->PresentationDefinition->AttackSequence.IsEmpty())
  MainAttack=DefaultMaster->PresentationDefinition->AttackSequence[0].Animation.LoadSynchronous();
 if(!MainAttack)MainAttack=DefaultMaster->GetAttackAnimation().LoadSynchronous();
 if(!TestNotNull(TEXT("Default master impact-timing attack"),MainAttack))return false;
 auto* Settlement=W->SpawnActor<AEGSettlementPresentation>();
 if(!TestTrue(TEXT("Settlement actor has begun play before destruction"),Settlement->HasActorBegunPlay()))return false;
 int32 OtherObserverCalls=0;
 const FDelegateHandle OtherObserver=Unit->OnPresentationWillRelease.AddLambda([&OtherObserverCalls](AEGChessUnit*){++OtherObserverCalls;});
 int32 Impacts=0;Settlement->OnImpact.AddLambda([&Impacts](){++Impacts;});
 Settlement->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 TestTrue(TEXT("Friendly idle does not change six-second duration"),FMath::IsNearlyEqual(Settlement->GetDurationSeconds(),6.f,.001f));
 TestTrue(TEXT("Configured clip drives the real body"),Instance->PassiveClip==Clip);
 AEGCombatVisual* Rig=nullptr;
 for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Settlement&&It->Definition==Source.Get())Rig=*It;
 if(TestNotNull(TEXT("Unoccupied compatible body has a separate settlement rig"),Rig))
 {
  TestTrue(TEXT("Settlement rig has begun play before destruction"),Rig->HasActorBegunPlay());
  TestNull(TEXT("Settlement rig does not borrow the live body"),Rig->BodyMesh.Get());
  TestFalse(TEXT("Live body is hidden during proxy display"),Body->IsVisible());
  TestFalse(TEXT("Original tagged weapon is hidden during replacement"),Weapon->IsVisible());
  auto* Contender=W->SpawnActor<AEGSettlementPresentation>();
  int32 ContenderImpacts=0;Contender->OnImpact.AddLambda([&](){++ContenderImpacts;});
  Contender->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
  TestTrue(TEXT("Overlapping master request is rejected"),Contender->IsActorBeingDestroyed());
  TestEqual(TEXT("Rejected request never applies impact"),ContenderImpacts,0);
  AEGCombatVisual* ContenderRig=nullptr;
  for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Contender&&It->Definition==Source.Get())ContenderRig=*It;
  TestNull(TEXT("A second settlement cannot take an occupied body lease"),ContenderRig);
  Contender->Destroy();
  TestFalse(TEXT("Destroying a contender does not reveal the leased body"),Body->IsVisible());
  AdvanceSettlementFixture(Settlement,70);
  TestEqual(TEXT("Impact has not happened early"),Impacts,0);
  AdvanceSettlementFixture(Settlement,70);
  TestEqual(TEXT("Impact keeps main attack timing"),Impacts,1);
  TestTrue(TEXT("Parts share the settlement clock"),FMath::IsNearlyEqual(Rig->SourceTime,Settlement->GetElapsedSeconds(),.001f));
 }
 TestTrue(TEXT("Interrupt destruction is accepted"),Settlement->Destroy());
 TestTrue(TEXT("Interrupt restores original body visibility"),Body->IsVisible());
 TestTrue(TEXT("Interrupt restores the original weapon visibility"),Weapon->IsVisible());
 TestTrue(TEXT("Interrupt releases external idle pose"),Instance->PassiveClip!=Clip || Instance->PassiveTime==0.f);
 Unit->NotifyPresentationWillRelease();
 TestEqual(TEXT("Interrupt removes only its own observer"),OtherObserverCalls,1);
 Unit->OnPresentationWillRelease.Remove(OtherObserver);

 // Unknown attached visuals are not ours to suppress; the clip still plays.
 auto* Unknown=NewObject<UStaticMeshComponent>(Unit);Unit->AddInstanceComponent(Unknown);Unknown->SetupAttachment(Body);Unknown->RegisterComponent();
 auto* Fallback=W->SpawnActor<AEGSettlementPresentation>();Fallback->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 Rig=nullptr;for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Fallback&&It->Definition==Source.Get())Rig=*It;
 TestNull(TEXT("Unknown attached component prevents borrowing"),Rig);
 TestTrue(TEXT("Unknown attachment does not prevent body animation"),Instance->PassiveClip==Clip);
 Fallback->Destroy();
 Unknown->DestroyComponent();
 auto* WrongClip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Assets/Characters/Elysia/Combat20261101/Animations/A_C1_Idle_BoardInPlace.A_C1_Idle_BoardInPlace"));
 if(TestNotNull(TEXT("Unrelated skeleton candidate"),WrongClip))
 {
  TestTrue(TEXT("Candidate uses a different skeleton"),WrongClip->GetSkeleton()!=Clip->GetSkeleton());
  Presentation->SettlementIdleAnimation=WrongClip;
  auto* Incompatible=W->SpawnActor<AEGSettlementPresentation>();Incompatible->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
  TestTrue(TEXT("Incompatible configured clip falls back to the Mobius idle"),Instance->PassiveClip==Clip);
  Incompatible->Destroy();
 }
 Presentation->SettlementIdleAnimation=Clip;
 auto* Replay=W->SpawnActor<AEGSettlementPresentation>();int32 ReplayImpacts=0,Finishes=0;
 Replay->OnImpact.AddLambda([&ReplayImpacts](){++ReplayImpacts;});Replay->OnFinished.AddLambda([&Finishes](){++Finishes;});
 Replay->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 AdvanceSettlementFixture(Replay,170);
 Replay->Tick(5.98f-Replay->GetElapsedSeconds());
 TestEqual(TEXT("Idle replay has not finished before six seconds"),Finishes,0);
 TestTrue(TEXT("Idle remains active through the deadline"),Instance->PassiveClip==Clip);
 Replay->Tick(.04f);Replay->Tick(.1f);
 TestEqual(TEXT("Replay applies one impact"),ReplayImpacts,1);
 TestEqual(TEXT("Replay finishes once"),Finishes,1);
 Replay->Destroy();
 TestTrue(TEXT("Finished replay restores body and weapon"),Body->IsVisible()&&Weapon->IsVisible());

 // A dead enemy already owns its death pose/dissolve; settlement must not
 // create another skeletal component in its place or alter the existing one.
 Presentation->DeathVisualHideDelay=-1.f;
 auto* Enemy=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize enemy death fixture"),Enemy->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 auto* EnemyBody=Enemy->GetVisiblePresentationMesh();
 Enemy->Die();Enemy->Tick(.01f);
 const bool bDeathVisible=EnemyBody->IsVisible();
 TestTrue(TEXT("Enemy death mesh is still displayed by its own presentation"),bDeathVisible);
 auto* DeathMesh=EnemyBody->GetSkeletalMeshAsset();
 auto* DeathMaterial=EnemyBody->GetMaterial(0);
 auto* DeadEnemySettlement=W->SpawnActor<AEGSettlementPresentation>();
 TArray<USkeletalMeshComponent*> Before,After;
 DeadEnemySettlement->GetComponents(Before);
 DeadEnemySettlement->StartPresentation({Enemy},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 DeadEnemySettlement->Tick(.1f);
 DeadEnemySettlement->GetComponents(After);
 TestEqual(TEXT("Dead enemy creates no settlement skeletal proxy"),After.Num(),Before.Num());
 TestTrue(TEXT("Dead enemy retains its original mesh, material and visibility"),
  EnemyBody->GetSkeletalMeshAsset()==DeathMesh&&EnemyBody->GetMaterial(0)==DeathMaterial&&EnemyBody->IsVisible()==bDeathVisible);
 DeadEnemySettlement->Destroy();

 auto* Dying=W->SpawnActor<AEGSettlementPresentation>();Dying->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 Rig=nullptr;for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Dying&&It->Definition==Source.Get()&&!It->IsActorBeingDestroyed())Rig=*It;
 if(!TestNotNull(TEXT("Death regression starts with an active settlement proxy"),Rig))return false;
 Presentation->DeathVisualHideDelay=0.f;
 Unit->Die();Unit->Tick(.01f);
 TestFalse(TEXT("Death system hides body before settlement cleanup"),Body->IsVisible());
 Dying->Tick(.01f);
 TestFalse(TEXT("Settlement cleanup keeps death-hidden body hidden"),Body->IsVisible());
 TestFalse(TEXT("Settlement cleanup keeps death-hidden weapon hidden"),Weapon->IsVisible());
 Rig=nullptr;for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Dying&&It->Definition==Source.Get()&&!It->IsActorBeingDestroyed())Rig=*It;
 TestNull(TEXT("Death destroys the temporary rig"),Rig);
 Dying->Destroy();
 AddInfo(TEXT("Controlled source and lifecycle test only; no visual, audio, natural-play, performance, Cook or package acceptance."));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettlementIdleDeathAndBodyReuse,
 "ElyGambit.Presentation.SettlementIdleDeathAndBodyReuse",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettlementIdleDeathAndBodyReuse::RunTest(const FString&)
{
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius.DA_Card_Mobius"));
 auto* Clip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Assets/Characters/Mobius/Animations/Idle/CombatIdle20261024/A_Mobius_NaturalIdle.A_Mobius_NaturalIdle"));
 if(!TestNotNull(TEXT("Mobius card"),Card)||!TestNotNull(TEXT("Mobius hero"),Card->HeroDefinition.Get())||!TestNotNull(TEXT("Candidate clip"),Clip))return false;
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestTrue(TEXT("Fixture world has begun play"),W->HasBegunPlay()))return false;
 auto* Hero=DuplicateObject<UEGHeroDefinition>(Card->HeroDefinition,W);
 Hero->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(Hero->PresentationDefinition.Get(),Hero);
 auto* P=Hero->PresentationDefinition.Get();
 P->SettlementIdleAnimation=Clip;P->IdleAnimation=Clip;P->PreparationIdleAssembly.Reset();P->IdleAssemblies.Reset();
 auto* Source=NewObject<UEGSourcePreviewDefinition>(Hero);
 auto& Motion=Source->Motions.AddDefaulted_GetRef();Motion.Duration=Clip->GetPlayLength();
 auto& BodyPart=Motion.Parts.AddDefaulted_GetRef();BodyPart.PartId=TEXT("Body");BodyPart.Mesh=P->Mesh.LoadSynchronous();BodyPart.Animation=Clip;BodyPart.AnimationBlueprintClass=UEGCombatPartAnimInstance::StaticClass();
 auto& Extra=Motion.Parts.AddDefaulted_GetRef();Extra.PartId=TEXT("FixturePart");Extra.Mesh=P->Mesh.LoadSynchronous();
 P->SettlementIdleAssembly=Source;
 auto AddWeapon=[](AActor* Owner,USkeletalMeshComponent* Body)
 {
  auto* Weapon=NewObject<UStaticMeshComponent>(Owner);Weapon->ComponentTags.Add(TEXT("EGWeapon"));
  Owner->AddInstanceComponent(Weapon);Weapon->SetupAttachment(Body);Weapon->RegisterComponent();Weapon->SetVisibility(true);return Weapon;
 };
 auto FindRig=[W,Source](AEGSettlementPresentation* Owner)->AEGCombatVisual*
 {for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Owner&&It->Definition==Source&&!It->IsActorBeingDestroyed())return *It;return nullptr;};
 for(const float Delay:{-1.f,.2f,0.f})
 {
  P->DeathVisualHideDelay=Delay;
  auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
  if(!TestTrue(TEXT("Initialize player for death delay"),Unit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
  int32 OtherObserverCalls=0;
  const FDelegateHandle OtherObserver=Unit->OnPresentationWillRelease.AddLambda([&OtherObserverCalls](AEGChessUnit* Observed)
   {++OtherObserverCalls;Observed->NotifyPresentationWillRelease();});
  auto* Body=Unit->GetVisiblePresentationMesh();auto* Weapon=AddWeapon(Unit,Body);
  auto* Settlement=W->SpawnActor<AEGSettlementPresentation>();Settlement->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
  if(!TestNotNull(TEXT("Each death case starts with its own active proxy"),FindRig(Settlement)))return false;
  TestFalse(TEXT("Proxy owns body visibility before death"),Body->IsVisible());
  if(Delay>.0f)Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),0.f);
  else Unit->Die();
  TestTrue(TEXT("Unit really entered death"),Unit->IsDeathPending());
  TestEqual(TEXT("Other presentation observer receives death"),OtherObserverCalls,1);
  TestNull(TEXT("Death transition destroys the proxy synchronously"),FindRig(Settlement));
  TestTrue(TEXT("Death transition gives the body and weapon back before death presentation"),Body->IsVisible()&&Weapon->IsVisible());
  Unit->Tick(.01f);Settlement->Tick(.01f);
  TestEqual(TEXT("Initial visibility follows zero versus nonzero hide delay"),Body->IsVisible(),Delay!=0.f);
  Unit->Tick(.25f);Settlement->Tick(.01f);
  const bool bShouldStayVisible=Delay<0.f;
  TestEqual(TEXT("Death presentation honors configured hide delay"),Body->IsVisible(),bShouldStayVisible);
  Settlement->Destroy();
  TestEqual(TEXT("EndPlay does not overwrite death-system visibility"),Body->IsVisible(),bShouldStayVisible);
  if(!bShouldStayVisible)TestFalse(TEXT("Death-hidden weapon remains hidden"),Weapon->IsVisible());
  Unit->NotifyPresentationWillRelease();
  TestEqual(TEXT("Removing settlement callback preserves other observers"),OtherObserverCalls,2);
  Unit->OnPresentationWillRelease.Remove(OtherObserver);
  Unit->Destroy();
 }
 P->DeathVisualHideDelay=-1.f;
 auto* Piece=W->SpawnActor<AEGDeployedPiece>();Piece->ApplyHeroDefinition(Hero);
 auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize deployed-piece unit"),Unit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* InitialSettlement=W->SpawnActor<AEGSettlementPresentation>();InitialSettlement->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Initial unit body has a settlement proxy"),FindRig(InitialSettlement)))return false;
 Unit->AttachPiece(Piece);
 TestNull(TEXT("AttachPiece synchronously releases previous body proxy"),FindRig(InitialSettlement));
 TestTrue(TEXT("AttachPiece switches to a visible new body"),Unit->GetVisiblePresentationMesh()==Piece->Body&&Piece->Body->IsVisible());
 InitialSettlement->Tick(.01f);InitialSettlement->Destroy();
 TestTrue(TEXT("Old settlement does not touch attached piece"),Piece->Body->IsVisible());
 auto* Body=Piece->Body.Get();auto* Weapon=AddWeapon(Piece,Body);
 if(!TestTrue(TEXT("Combat uses the deployed piece body"),Unit->GetVisiblePresentationMesh()==Body))return false;
 auto* Settlement=W->SpawnActor<AEGSettlementPresentation>();Settlement->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Borrowed piece starts with an active proxy"),FindRig(Settlement)))return false;
 Unit->RestorePiece();
 TestNull(TEXT("Detaching the piece destroys its proxy immediately"),FindRig(Settlement));
 TestTrue(TEXT("Reused piece body and weapon are visible after restore"),Body->IsVisible()&&Weapon->IsVisible());
 Settlement->Tick(.01f);Settlement->Destroy();
 TestTrue(TEXT("Later settlement cleanup leaves the reused piece visible"),Body->IsVisible()&&Weapon->IsVisible());
 Unit->AttachPiece(Piece);
 auto* EndPlaySettlement=W->SpawnActor<AEGSettlementPresentation>();EndPlaySettlement->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Unit EndPlay case starts with an active proxy"),FindRig(EndPlaySettlement)))return false;
 Unit->Destroy();
 TestNull(TEXT("Unit EndPlay releases proxy before returning piece"),FindRig(EndPlaySettlement));
 TestTrue(TEXT("Unit EndPlay returns a visible reusable piece and weapon"),Body->IsVisible()&&Weapon->IsVisible());
 EndPlaySettlement->Tick(.01f);EndPlaySettlement->Destroy();
 TestTrue(TEXT("Later cleanup does not hide the piece returned during EndPlay"),Body->IsVisible()&&Weapon->IsVisible());

 // An observer registered before settlement actually restores the borrowed
 // piece inside the outer RestorePiece call, not just a recursive notification.
 auto* RestoreUnit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize nested restore unit"),RestoreUnit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* RestorePiece=W->SpawnActor<AEGDeployedPiece>();RestorePiece->ApplyHeroDefinition(Hero);RestoreUnit->AttachPiece(RestorePiece);
 auto* RestoreBody=RestorePiece->Body.Get();auto* RestoreWeapon=AddWeapon(RestorePiece,RestoreBody);
 int32 RestoreCalls=0;
 const auto RestoreObserver=RestoreUnit->OnPresentationWillRelease.AddLambda([&](AEGChessUnit*){++RestoreCalls;RestoreUnit->RestorePiece();});
 auto* RestoreSettlement=W->SpawnActor<AEGSettlementPresentation>();RestoreSettlement->StartPresentation({RestoreUnit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Nested restore starts with active proxy"),FindRig(RestoreSettlement)))return false;
 RestoreUnit->RestorePiece();
 TestEqual(TEXT("Nested restore observer is notified once"),RestoreCalls,1);
 TestNull(TEXT("Nested restore releases proxy"),FindRig(RestoreSettlement));
 TestTrue(TEXT("Nested restore returns the original body and weapon"),RestoreBody->IsVisible()&&RestoreWeapon->IsVisible()&&RestoreUnit->GetVisiblePresentationMesh()!=RestoreBody);
 RestoreUnit->OnPresentationWillRelease.Remove(RestoreObserver);RestoreSettlement->Destroy();RestoreUnit->Destroy();

 // An observer may attach a different valid target while an outer attach is
 // notifying. The outer request must not replace that completed inner choice.
 auto* SwapUnit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize nested attach unit"),SwapUnit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* OuterPiece=W->SpawnActor<AEGDeployedPiece>();OuterPiece->ApplyHeroDefinition(Hero);
 auto* InnerPiece=W->SpawnActor<AEGDeployedPiece>();InnerPiece->ApplyHeroDefinition(Hero);
 int32 SwapCalls=0;
 const auto SwapObserver=SwapUnit->OnPresentationWillRelease.AddLambda([&](AEGChessUnit*){++SwapCalls;SwapUnit->AttachPiece(InnerPiece);});
 auto* SwapSettlement=W->SpawnActor<AEGSettlementPresentation>();SwapSettlement->StartPresentation({SwapUnit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Nested attach starts with active proxy"),FindRig(SwapSettlement)))return false;
 SwapUnit->AttachPiece(OuterPiece);
 TestEqual(TEXT("Nested attach observer is notified once"),SwapCalls,1);
 TestNull(TEXT("Nested attach releases proxy"),FindRig(SwapSettlement));
 TestTrue(TEXT("Inner attach remains the active piece"),SwapUnit->GetVisiblePresentationMesh()==InnerPiece->Body&&InnerPiece->GetAttachParentActor()==SwapUnit);
 TestNull(TEXT("Outer target was not attached"),OuterPiece->GetAttachParentActor());
 SwapUnit->OnPresentationWillRelease.Remove(SwapObserver);SwapSettlement->Destroy();SwapUnit->Destroy();

 // A target can disappear inside a listener; the outer call must leave the
 // unit's original body alone instead of using the invalid target.
 auto* DestroyUnit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize destroyed target unit"),DestroyUnit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* DestroyBody=DestroyUnit->GetVisiblePresentationMesh();auto* DestroyWeapon=AddWeapon(DestroyUnit,DestroyBody);
 auto* DestroyTarget=W->SpawnActor<AEGDeployedPiece>();DestroyTarget->ApplyHeroDefinition(Hero);
 const auto DestroyObserver=DestroyUnit->OnPresentationWillRelease.AddLambda([&](AEGChessUnit*){DestroyTarget->Destroy();});
 auto* DestroySettlement=W->SpawnActor<AEGSettlementPresentation>();DestroySettlement->StartPresentation({DestroyUnit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Destroyed target case starts with active proxy"),FindRig(DestroySettlement)))return false;
 DestroyUnit->AttachPiece(DestroyTarget);
 TestNull(TEXT("Destroyed target case releases proxy"),FindRig(DestroySettlement));
 TestTrue(TEXT("Destroyed target leaves original body and weapon visible"),DestroyUnit->GetVisiblePresentationMesh()==DestroyBody&&DestroyBody->IsVisible()&&DestroyWeapon->IsVisible());
 DestroyUnit->OnPresentationWillRelease.Remove(DestroyObserver);DestroySettlement->Destroy();DestroyUnit->Destroy();

 auto* ReplaceUnit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize replaced target unit"),ReplaceUnit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* ReplaceBody=ReplaceUnit->GetVisiblePresentationMesh();
 auto* ReplaceTarget=W->SpawnActor<AEGDeployedPiece>();ReplaceTarget->ApplyHeroDefinition(Hero);
 const auto ReplaceObserver=ReplaceUnit->OnPresentationWillRelease.AddLambda([&](AEGChessUnit*){ReplaceTarget->Body->SetSkeletalMeshAsset(nullptr);});
 auto* ReplaceSettlement=W->SpawnActor<AEGSettlementPresentation>();ReplaceSettlement->StartPresentation({ReplaceUnit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 if(!TestNotNull(TEXT("Changed target case starts with active proxy"),FindRig(ReplaceSettlement)))return false;
 ReplaceUnit->AttachPiece(ReplaceTarget);
 TestTrue(TEXT("Changed target leaves original body active"),ReplaceUnit->GetVisiblePresentationMesh()==ReplaceBody&&ReplaceBody->IsVisible());
 TestNull(TEXT("Changed target was not attached"),ReplaceTarget->GetAttachParentActor());
 ReplaceUnit->OnPresentationWillRelease.Remove(ReplaceObserver);ReplaceSettlement->Destroy();ReplaceUnit->Destroy();

 // Destroying the temporary visual itself can synchronously invoke arbitrary
 // world observers before Destroy returns. The body must already be returned.
 auto* ReentrantUnit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize visual destruction callback unit"),ReentrantUnit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* ReentrantPiece=W->SpawnActor<AEGDeployedPiece>();ReentrantPiece->ApplyHeroDefinition(Hero);ReentrantUnit->AttachPiece(ReentrantPiece);
 auto* ReentrantBody=ReentrantPiece->Body.Get();auto* ReentrantWeapon=AddWeapon(ReentrantPiece,ReentrantBody);
 auto* ReentrantSettlement=W->SpawnActor<AEGSettlementPresentation>();ReentrantSettlement->StartPresentation({ReentrantUnit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 auto* ReentrantRig=FindRig(ReentrantSettlement);
 if(!TestNotNull(TEXT("Visual callback case starts with active proxy"),ReentrantRig))return false;
 int32 VisualDestroyCalls=0;
 const auto VisualObserver=W->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda([&](AActor* Actor)
  {if(Actor==ReentrantRig){++VisualDestroyCalls;ReentrantUnit->RestorePiece();}}));
 ReentrantUnit->RestorePiece();
 W->RemoveOnActorDestroyedHandler(VisualObserver);
 TestEqual(TEXT("Visual destruction reenters restore exactly once"),VisualDestroyCalls,1);
 TestTrue(TEXT("Visual callback sees a returned reusable body and weapon"),ReentrantBody->IsVisible()&&ReentrantWeapon->IsVisible()&&ReentrantUnit->GetVisiblePresentationMesh()!=ReentrantBody);
 ReentrantSettlement->Destroy();ReentrantUnit->Destroy();
 AddInfo(TEXT("Controlled game-world lifecycle tests only; no visual, audio, natural-play, Cook or packaged acceptance."));
 return true;
}
#endif
