#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/AnimSequence.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
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
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettlementProductionAssets,
 "ElyGambit.Presentation.SettlementProductionAssets",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettlementProductionAssets::RunTest(const FString&)
{
 struct FCase { const TCHAR* Name; const TCHAR* Card; EEGHeroKind Kind; bool bAssembly; };
 const FCase Cases[]={
  {TEXT("Elysia"),TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"),EEGHeroKind::Elysia,false},
  {TEXT("Human"),TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human"),EEGHeroKind::ElysiaHerrscher,false},
  {TEXT("Hua"),TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua"),EEGHeroKind::Hua,false},
  {TEXT("CollapsedHua"),TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua"),EEGHeroKind::CollapsedHua,false},
  {TEXT("Pardo"),TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo"),EEGHeroKind::Pardofelis,false},
  {TEXT("VillV"),TEXT("/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV"),EEGHeroKind::VillV,false},
  {TEXT("Sakura"),TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura"),EEGHeroKind::Sakura,true},
  {TEXT("Griseo"),TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo"),EEGHeroKind::Griseo,true},
  {TEXT("GriseoAdult"),TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult"),EEGHeroKind::GriseoAdult,true},
  {TEXT("Eden"),TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden"),EEGHeroKind::Eden,true},
  {TEXT("Aponia"),TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia"),EEGHeroKind::Aponia,true},
  {TEXT("Mobius"),TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius"),EEGHeroKind::Mobius,true}
 };
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();
 if(!TestNotNull(TEXT("Standalone fixture world"),W))return false;
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
 FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestTrue(TEXT("Fixture world has begun play"),W->HasBegunPlay()))return false;

 // The master is always a separate puppet; all units are ordinary participants.
 auto* MasterCard=LoadObject<UEGCardDefinition>(nullptr,Cases[0].Card);
 if(!TestNotNull(TEXT("Master fixture card"),MasterCard)||!TestNotNull(TEXT("Master fixture hero"),MasterCard->HeroDefinition.Get()))return false;
 auto* Master=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestNotNull(TEXT("Master fixture actor"),Master)||!TestTrue(TEXT("Initialize master fixture"),Master->InitializeUnit(MasterCard->HeroDefinition,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* Baseline=W->SpawnActor<AEGSettlementPresentation>();
 Baseline->StartPresentation({},Master,FVector::ZeroVector,FVector(500,0,0),true);
 const float BaseDuration=Baseline->GetDurationSeconds();Baseline->Destroy();
 TestTrue(TEXT("Master baseline lasts six seconds"),FMath::IsNearlyEqual(BaseDuration,6.f,.001f));

 for(int32 CaseIndex=0;CaseIndex<UE_ARRAY_COUNT(Cases);++CaseIndex)
 {
  const FCase& C=Cases[CaseIndex];
  const auto Label=[&C](const TCHAR* Text){return FString::Printf(TEXT("%s: %s"),C.Name,Text);};
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,C.Card);
  if(!TestNotNull(Label(TEXT("formal card loads")),Card))continue;
  auto* Hero=Card->HeroDefinition.Get();
  if(!TestNotNull(Label(TEXT("card's actual hero")),Hero))continue;
  auto* Presentation=Hero->PresentationDefinition.Get();
  if(!TestNotNull(Label(TEXT("hero's actual presentation")),Presentation))continue;
  TestEqual(Label(TEXT("formal card resolves the intended hero kind")),Hero->HeroKind,C.Kind);
  TestTrue(Label(TEXT("formal hero uses its split presentation")),Hero->bUseSplitConfiguration);
  auto* Clip=Presentation->SettlementIdleAnimation.LoadSynchronous();
  if(!TestNotNull(Label(TEXT("configured settlement clip loads")),Clip))continue;
  TestTrue(Label(TEXT("configured settlement reference resolves to the loaded clip")),Presentation->SettlementIdleAnimation.ToSoftObjectPath()==FSoftObjectPath(Clip));
  if(!TestTrue(Label(TEXT("positive finite clip duration")),FMath::IsFinite(Clip->GetPlayLength())&&Clip->GetPlayLength()>.01f))continue;
  auto* Source=Presentation->SettlementIdleAssembly.LoadSynchronous();
  if(C.bAssembly && !TestNotNull(Label(TEXT("configured settlement assembly loads")),Source))continue;
  UAnimSequence* RuntimeClip=Hero->GetIdleAnimation().LoadSynchronous();
  if(!RuntimeClip||RuntimeClip->GetSkeleton()!=Clip->GetSkeleton())RuntimeClip=Clip;
  bool bRuntimeAssembly=C.bAssembly&&RuntimeClip==Clip;
  if(Hero->HeroKind==EEGHeroKind::ElysiaHerrscher&&Presentation->SettlementIdleVariants.IsValidIndex(0))
  {
   RuntimeClip=Presentation->SettlementIdleVariants[0].Animation.LoadSynchronous();
   Source=Presentation->SettlementIdleVariants[0].Assembly.LoadSynchronous();
   if(!TestNotNull(Label(TEXT("human bow result idle loads")),RuntimeClip)||!TestNotNull(Label(TEXT("human bow idle assembly loads")),Source))continue;
   bRuntimeAssembly=true;
  }
  auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
  if(!TestNotNull(Label(TEXT("unit actor")),Unit))continue;
  if(!TestTrue(Label(TEXT("initialize actual hero")),Unit->InitializeUnit(Hero,FGuid::NewGuid(),EEGFaction::Player,1))) {Unit->Destroy();continue;}
  auto* Body=Unit->GetVisiblePresentationMesh();
  auto* ExpectedMesh=Hero->GetMesh().LoadSynchronous();
  auto* Instance=Body?Cast<UEGCombatAnimInstance>(Body->GetAnimInstance()):nullptr;
  if(!TestNotNull(Label(TEXT("displayed body")),Body)||!TestNotNull(Label(TEXT("hero mesh loads")),ExpectedMesh)||
     !TestNotNull(Label(TEXT("real combat anim instance")),Instance)) {Unit->Destroy();continue;}
  TestTrue(Label(TEXT("actual body uses hero mesh and compatible clip")),Body->GetSkeletalMeshAsset()==ExpectedMesh&&ExpectedMesh->GetSkeleton()&&ExpectedMesh->GetSkeleton()==RuntimeClip->GetSkeleton());
  TestTrue(Label(TEXT("unit starts alive and body visible")),Unit->IsAlive_Implementation()&&Body->IsVisible());
  TArray<USceneComponent*> Children;Body->GetChildrenComponents(true,Children);
  TArray<TPair<TWeakObjectPtr<USceneComponent>,bool>> Weapons;
  for(USceneComponent* Child:Children)if(IsValid(Child)&&Cast<UPrimitiveComponent>(Child)&&Child->ComponentHasTag(TEXT("EGWeapon")))Weapons.Emplace(Child,Child->IsVisible());

  auto* Settlement=W->SpawnActor<AEGSettlementPresentation>();
  if(!TestNotNull(Label(TEXT("settlement actor")),Settlement)){Unit->Destroy();continue;}
  Settlement->StartPresentation({Unit},Master,FVector::ZeroVector,FVector(500,0,0),true);
  const float Sample=FMath::Min(.25f,RuntimeClip->GetPlayLength()*.5f);
  Settlement->Tick(Sample);
  TestTrue(Label(TEXT("settlement samples configured clip and advances time")),Instance->PassiveClip==RuntimeClip&&FMath::IsNearlyEqual(Instance->PassiveTime,Sample,.001f));
  TestTrue(Label(TEXT("friendly idle preserves six-second duration")),FMath::IsNearlyEqual(Settlement->GetDurationSeconds(),6.f,.001f));
  AEGCombatVisual* Rig=nullptr;
  for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Settlement&&It->Definition==Source&&!It->IsActorBeingDestroyed())Rig=*It;
  if(bRuntimeAssembly)
  {
   if(TestNotNull(Label(TEXT("expected assembly actually creates proxy; see settlement warning for rejection")),Rig))
   {
    TestTrue(Label(TEXT("proxy owns its own body and advances")),!Rig->BodyMesh&&FMath::IsNearlyEqual(Rig->SourceTime,Sample,.001f));
    TestFalse(Label(TEXT("original body hidden while proxy displays")),Body->IsVisible());
    if(TestTrue(Label(TEXT("assembly has a motion")),!Source->Motions.IsEmpty()))
    {
     TArray<USkeletalMeshComponent*> Parts;Rig->GetComponents(Parts);
     const auto& Motion=Source->Motions[0];
     for(int32 Index=0;Index<Motion.Parts.Num();++Index)
     {
      const auto& Spec=Motion.Parts[Index];USkeletalMeshComponent* Found=nullptr;
      const FName Tag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),Index));
      for(auto* Part:Parts)if(IsValid(Part)&&Part->ComponentHasTag(Tag)){Found=Part;break;}
      const FString PartLabel=FString::Printf(TEXT("%s: proxy part %d (%s)"),C.Name,Index,*Spec.PartId.ToString());
      if(!TestNotNull(PartLabel,Found))continue;
      TestTrue(PartLabel+TEXT(" uses configured mesh"),Found->GetSkeletalMeshAsset()==Spec.Mesh);
      if(Spec.Animation)
      {
       auto* PartAnim=Cast<UEGCombatPartAnimInstance>(Found->GetAnimInstance());
       if(TestNotNull(PartLabel+TEXT(" anim instance"),PartAnim))TestTrue(PartLabel+TEXT(" samples configured animation"),PartAnim->SourceClip==Spec.Animation);
      }
      const float Local=Sample-Spec.StartTime;
      const float Length=Spec.Animation?Spec.Animation->GetPlayLength():Motion.Duration;
      const float Window=Spec.Duration>0.f?Spec.Duration:Length;
      bool bParentVisible=true;
      if(!Spec.ParentPartId.IsNone())
      {
       int32 ParentIndex=INDEX_NONE;
       for(int32 Previous=0;Previous<Index;++Previous)if(Motion.Parts[Previous].PartId==Spec.ParentPartId){ParentIndex=Previous;break;}
       USkeletalMeshComponent* Parent=nullptr;
       const FName ParentTag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),ParentIndex));
       for(auto* Part:Parts)if(IsValid(Part)&&Part->ComponentHasTag(ParentTag)){Parent=Part;break;}
       if(TestNotNull(PartLabel+TEXT(" parent"),Parent))
       {
        TestTrue(PartLabel+TEXT(" attached to configured parent"),Found->GetAttachParent()==Parent);
        bParentVisible=Parent->IsVisible();
       }
       else bParentVisible=false;
      }
      const bool bExpectedVisible=Spec.bVisible&&Local>=0.f&&Local<Window&&Spec.EvaluateVisibility(Spec.Visibility,Local)&&bParentVisible;
      TestEqual(PartLabel+TEXT(" visibility at sampled time"),Found->IsVisible(),bExpectedVisible);
     }
    }
   }
  }
  Settlement->Destroy();
  TestTrue(Label(TEXT("interrupt restores original body")),Body->IsVisible());
  TestTrue(Label(TEXT("interrupt releases sampled pose")),Instance->PassiveClip!=RuntimeClip||FMath::IsNearlyZero(Instance->PassiveTime));
  for(const auto& Weapon:Weapons)
   if(TestTrue(Label(TEXT("interrupt retains original weapon")),Weapon.Key.IsValid()))
    TestEqual(Label(TEXT("interrupt restores weapon visibility")),Weapon.Key->IsVisible(),Weapon.Value);
  if(Rig)TestTrue(Label(TEXT("interrupt destroys proxy")),!IsValid(Rig)||Rig->IsActorBeingDestroyed());
  AddInfo(FString::Printf(TEXT("%s: Hero=%s Presentation=%s Clip=%s duration=%.3f Assembly=%s"),C.Name,*Hero->GetPathName(),*Presentation->GetPathName(),*Clip->GetPathName(),Clip->GetPlayLength(),*GetNameSafe(Source)));
  Unit->Destroy();
 }
 Master->Destroy();
 AddInfo(TEXT("Standalone controlled world, formal card/Hero/Presentation assets, separate explicit master. Not production map, natural battle, render/visual/audio acceptance, Cook, package or networking."));
 return true;
}
#endif
