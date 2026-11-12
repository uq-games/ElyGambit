#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Presentation/EGSkillNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"

namespace
{
class FHumanEntryNaturalPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TWeakObjectPtr<AEGChessUnit> Human, Enemy;
 TWeakObjectPtr<APlayerController> Controller;
 double Deadline=0., PausedAt=0., ReleasedAt=0.;
 double FrozenWorldTime=0.;
 FVector FrozenEnemy=FVector::ZeroVector;
 float FirstPosition=-1.f, MaxPosition=-1.f, FinishNotifyTime=-1.f;
 float EffectiveEntryRate=0.f;
 bool bDeployed=false,bInjected=false,bObservedPause=false;
 bool bMovieConfigured=false;
 bool bMeshTick=false,bSkillTick=false,bBattleTick=false;
 FGuid Action;
public:
 explicit FHumanEntryNaturalPIE(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+90.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Human entry PIE timed out before natural completion"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* GM=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!GM||!GM->GetCards()||!GM->BattleRules)return false;
  if(!bDeployed)
  {
   if(!GM->CanPrepare()||!GM->AreStageResourcesReady())return false;
   auto* PC=W->GetFirstPlayerController();
   if(!Test->TestNotNull(TEXT("Formal local controller"),PC)||!Test->TestNotNull(TEXT("Controller player state"),PC->PlayerState.Get()))return true;
   Controller=PC;
   auto* Cards=GM->GetCards();FGuid CardId;
   for(const auto& C:Cards->GetHand())if(C.Definition&&C.Definition->HeroDefinition&&C.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher)CardId=C.InstanceId;
   if(!CardId.IsValid())
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"));
    if(!Test->TestNotNull(TEXT("Formal human card"),Card)||!Test->TestTrue(TEXT("Grant fixture card"),Cards->GrantCard(Card)))return true;
    CardId=Cards->GetHand().Last().InstanceId;
   }
   if(!Test->TestTrue(TEXT("Deploy human through card manager"),Cards->TryDeployHandCard(CardId,{2,1}))||!Test->TestTrue(TEXT("Enter combat"),GM->EnterCombat()))return true;
   for(auto* U:GM->GetCombatUnits())if(U&&U->GetDefinition()&&U->GetAbilitySystemComponent())
   {
    if(U->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=U;
    else if(U->GetFaction_Implementation()==EEGFaction::Enemy&&!Enemy.IsValid())Enemy=U;
    auto* ASC=U->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000000.f);
   }
   if(!Test->TestTrue(TEXT("Human and enemy spawned"),Human.IsValid()&&Enemy.IsValid()))return true;
   Test->AddInfo(TEXT("Controlled PIE: formal board and card deployment, fixture health raised to 10,000,000 and human mana filled once. AI starts the skill; no animation events are injected. This is not real input, natural balance, or visual acceptance."));
   bDeployed=true;return false;
  }
  if(!Human.IsValid()||!Enemy.IsValid()||!Controller.IsValid()){Test->AddError(TEXT("PIE fixture actor disappeared"));return true;}
  auto* Skill=Human->GetHeroSkill();auto* Mesh=Human->GetVisiblePresentationMesh();auto* Battle=GM->BattleRules.Get();
  if(!Skill||!Mesh||!Battle){Test->AddError(TEXT("Missing production skill, visible mesh, or battle context"));return true;}
  if(!bInjected)
  {
   if(!Battle->IsCombatRunning())return false;
   bMeshTick=Mesh->PrimaryComponentTick.bTickEvenWhenPaused;
   bSkillTick=Skill->PrimaryComponentTick.bTickEvenWhenPaused;
   bBattleTick=Battle->PrimaryComponentTick.bTickEvenWhenPaused;
   Human->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Human->GetAttributes()->GetMaxMana());
   bInjected=true;return false;
  }
  if(W->IsPaused()&&Battle->IsHumanEntryPauseOwner(Human.Get(),Skill->ActionId))
  {
   auto* Montage=Skill->GetActiveMontage();auto* Anim=Mesh->GetAnimInstance();
   if(!Test->TestNotNull(TEXT("Visible animation instance"),Anim))return true;
   if(!bObservedPause)
   {
    if(!Test->TestNotNull(TEXT("Entry montage at pause acquisition"),Montage))return true;
    const float Position=Anim->Montage_GetPosition(Montage);
    bObservedPause=true;Action=Skill->ActionId;PausedAt=FPlatformTime::Seconds();
    FirstPosition=MaxPosition=Position;FrozenWorldTime=W->GetTimeSeconds();FrozenEnemy=Enemy->GetActorLocation();
    const auto* Presentation=Human->FindComponentByClass<UEGElysiaHerrscherPresentationComponent>();
    bMovieConfigured=Presentation&&Presentation->bEntryMovieEnabled&&!Presentation->EntryMovieSource.IsNull();
    Test->TestTrue(TEXT("Formal entry montage uses authored presentation rate"),FMath::IsNearlyEqual(Montage->RateScale,1.f,.001f));
    int32 FinishCount=0;
    for(const FAnimNotifyEvent& Event:Montage->Notifies)
     if(const auto* Notify=Cast<UEGSkillNotify>(Event.Notify);Notify&&Notify->Event==TEXT("Finish"))
     {FinishNotifyTime=Event.GetTriggerTime();++FinishCount;}
    Test->TestEqual(TEXT("Entry montage has exactly one gameplay Finish notify"),FinishCount,1);
    if(FinishCount==1)
    {
     EffectiveEntryRate=FMath::Abs(Montage->RateScale*Anim->Montage_GetPlayRate(Montage));
     const float Remaining=EffectiveEntryRate>SMALL_NUMBER?(Montage->GetPlayLength()-FinishNotifyTime)/EffectiveEntryRate:0.f;
     const float BlendTrigger=Montage->BlendOutTriggerTime>=0.f?Montage->BlendOutTriggerTime:Montage->BlendOut.GetBlendTime();
     // The natural blend-out delegate invalidates the montage's notify token.
     // Finish must arrive before that delegate, with one 60 Hz frame of margin.
     Test->TestTrue(TEXT("Entry Finish precedes natural blend-out by a frame"),Remaining>BlendTrigger+1.f/60.f);
     Test->AddInfo(FString::Printf(TEXT("Entry finish %.3f, length %.3f, effective rate %.3f, remaining %.3f real seconds, blend trigger %.3f real seconds"),
      FinishNotifyTime,Montage->GetPlayLength(),EffectiveEntryRate,Remaining,BlendTrigger));
    }
   }
   else
   {
    Test->TestTrue(TEXT("Paused action identity stays stable"),Skill->ActionId==Action);
    Test->TestTrue(TEXT("World clock remains frozen"),FMath::Abs(W->GetTimeSeconds()-FrozenWorldTime)<.01);
    Test->TestTrue(TEXT("Enemy stays frozen"),Enemy->GetActorLocation().Equals(FrozenEnemy,.1));
    if(Montage&&Anim->Montage_IsPlaying(Montage))MaxPosition=FMath::Max(MaxPosition,Anim->Montage_GetPosition(Montage));
   }
   return false;
  }
  if(!bObservedPause)return false;
  if(Battle->HasHumanEntryPause())return false;
  // Allow the released action and paused-tick state to settle.
  if(ReleasedAt==0.)ReleasedAt=FPlatformTime::Seconds();
  if(FPlatformTime::Seconds()-ReleasedAt<.25)return false;
  auto* Behavior=Cast<UEGElysiaHerrscherSkillBehavior>(Skill->GetBehavior());
  const double ObservedPause=ReleasedAt-PausedAt;
  // The media branch is held until the 4.9 second source ends; the fallback
  // follows the montage's authored notify and rate. Both use wall-clock time.
  if(bMovieConfigured)
   Test->TestTrue(TEXT("Entry pause spans the configured 4.9 second movie"),ObservedPause>=4.2&&ObservedPause<=8.0);
  else
  {
   const double Expected=EffectiveEntryRate>SMALL_NUMBER?FinishNotifyTime/EffectiveEntryRate:0.;
   Test->TestTrue(TEXT("Entry pause follows the authored Finish without a movie"),Expected>0.&&ObservedPause>=Expected-.7&&ObservedPause<=Expected+1.0);
  }
  Test->TestTrue(TEXT("Montage advanced naturally during pause"),MaxPosition>FirstPosition+.05f);
  Test->TestTrue(TEXT("Natural playback sampled the final 0.2 seconds before Finish"),FinishNotifyTime>0.f&&MaxPosition>=FinishNotifyTime-.2f);
  Test->TestTrue(TEXT("Entry entered origin through natural notifies"),Behavior&&Behavior->IsOriginActive());
  Test->TestTrue(TEXT("Mesh paused tick restored"),Mesh->PrimaryComponentTick.bTickEvenWhenPaused==bMeshTick);
  Test->TestTrue(TEXT("Skill paused tick restored"),Skill->PrimaryComponentTick.bTickEvenWhenPaused==bSkillTick);
  Test->TestTrue(TEXT("Battle paused tick restored"),Battle->PrimaryComponentTick.bTickEvenWhenPaused==bBattleTick);
  Test->AddInfo(FString::Printf(TEXT("Entry pause observed %.3fs real time; montage %.3f -> %.3f"),ObservedPause,FirstPosition,MaxPosition));
  return true;
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanEntryNaturalPIE,"ElyGambit.Integration.HumanEntryNaturalPause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanEntryNaturalPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Requires idle editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FHumanEntryNaturalPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
#endif
