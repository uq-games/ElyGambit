#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/EGProductionTestAssets.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UObject/StrongObjectPtr.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGHitFeedbackComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Character/EGVillVEmplacement.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "Skills/EGSakuraVillVSkillBehaviors.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGSkillProjectile.h"
#include "Presentation/EGMobiusLinkVisual.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCombatAnimInstance.h"
#include "Presentation/EGVillVCombatFX.h"
#include "Presentation/EGVillVShotVisual.h"
#include "Presentation/EGPresentationBurst.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Presentation/EGSourceEffect.h"
#include "NiagaraSystem.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGGriseoAdultSkillDefinition.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGEdenAponiaSkillBehaviors.h"
#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "EGGameplayTags.h"
#include "NiagaraComponent.h"
#include "EngineUtils.h"
#include "Tickable.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundConcurrency.h"

namespace
{
struct FFiveWorld
{
 TStrongObjectPtr<UGameInstance> GI;UWorld* W;AEGDemoGameMode* GM;AEGCardManager* Cards=nullptr;AEGDeploymentBoard* Grid;
 FFiveWorld(FAutomationTestBase& Test,bool bRoundShop=true):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();
  FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return;
  auto* Run=DuplicateObject<UEGRunDefinition>(Base,GM);if(!EGProductionTestAssets::ConfigureBehaviorEnemies(Run,Test))return;Run->bRoundShopRules=bRoundShop;Run->StartingCards=1;Run->CardRules.DeploymentLimit=7;Run->CardRules.HandLimit=20;GM->RunDefinition=Run;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();Grid=W->SpawnActor<AEGDeploymentBoard>();
  Grid->Columns=7;Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=500;Grid->CellDepth=500;
  Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};
  W->InitializeActorsForPlay(URL);W->BeginPlay();
 }
 ~FFiveWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 FGuid Deploy(const TCHAR* Folder,const TCHAR* Key,int32 X)
 {
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,*FString::Printf(TEXT("/Game/Blueprints/Characters/%s/Data/DA_Card_%s"),Folder,Key));
  if(!Card||!Cards->GrantCard(Card))return FGuid();
  if(!Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{X,2}))return FGuid();
  return GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
 }
 AEGChessUnit* Find(FGuid Id){for(auto* U:GM->GetCombatUnits())if(U->GetInstanceId()==Id)return U;return nullptr;}
 TArray<AEGChessUnit*> Enemies(){TArray<AEGChessUnit*> R;for(auto* U:GM->GetCombatUnits())if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)R.Add(U);return R;}
 void Freeze()
 {
  GM->BattleRules->CompleteOpeningForTests();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* ASC=U->GetAbilitySystemComponent();
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);
   ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
  }
 }
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.05f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.05f);}}
 bool Cast(AEGChessUnit* U){U->GetHeroSkill()->CancelAction();U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),U->GetAttributes()->GetMaxMana());return U->GetHeroSkill()->BeginAction(true);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveNewRules,"ElyGambit.FiveHeroRework.RulesAndGAS",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveNewRules::RunTest(const FString&)
{
 FFiveWorld E(*this);if(!E.Cards)return false;
 const auto Id=E.Deploy(TEXT("FuHua/FuSheng"),TEXT("CollapsedHua"),2);
 if(!TestTrue(TEXT("Deploy collapsed Hua"),Id.IsValid()&&E.GM->EnterCombat()))return false;
 E.Freeze();auto* U=E.Find(Id);auto Enemies=E.Enemies();if(!U||Enemies.IsEmpty())return false;auto* T=Enemies[0];
 auto* S=U->GetHeroSkill();auto* ASC=U->GetAbilitySystemComponent();auto* TAS=T->GetAbilitySystemComponent();
 for(int32 Level:{1,6,10,11})
 {
  S->ApplyPersistentLevel(Level);ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000);
  T->SetActorLocation(U->GetActorLocation()+FVector(100,0,0));TAS->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);
  const float Attack=U->GetAttributes()->GetAttackPower();if(!TestTrue(TEXT("Burn cast"),E.Cast(U)))return false;
  S->AnimationEvent(TEXT("Burn"));TestEqual(TEXT("Pay 30 percent current health"),U->GetAttributes()->GetHealth(),700.f);
  S->AnimationEvent(TEXT("Release"));const float HP=T->GetAttributes()->GetHealth();
  TestTrue(TEXT("Damage adds actual paid health"),FMath::IsNearlyEqual(10000-HP,Attack*(Level>=6?4.5f:3.f)+300.f,.05f));
  TestEqual(TEXT("Shield unlocks only at eleven"),S->GetSourceShield(TEXT("CollapsedHua.BurnShield")),Level>=11?150.f:0.f);
  S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Release cannot duplicate damage"),T->GetAttributes()->GetHealth(),HP);
  S->AnimationEvent(TEXT("Finish"));TestEqual(TEXT("Completed cast recovers to 30 percent"),U->GetAttributes()->GetHealth(),3000.f);
  S->RemoveSourceShield(TEXT("CollapsedHua.BurnShield"));
 }
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000);E.Cast(U);S->AnimationEvent(TEXT("Burn"));S->CancelAction();S->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("Cancelled before release neither heals nor shields"),U->GetAttributes()->GetHealth(),700.f);TestEqual(TEXT("No cancelled shield"),S->GetSourceShield(TEXT("CollapsedHua.BurnShield")),0.f);
 TAS->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100);
 auto Armor=TAS->MakeOutgoingSpec(UEGHuaArmorEffect::StaticClass(),1,TAS->MakeEffectContext());Armor.Data->SetDuration(6,true);Armor.Data->SetSetByCallerMagnitude(FName(TEXT("Support.Multiplier")),.7f);
 TAS->ApplyGameplayEffectSpecToSelf(*Armor.Data);TAS->ApplyGameplayEffectSpecToSelf(*Armor.Data);TestEqual(TEXT("Same armor effect does not stack"),T->GetAttributes()->GetDefense(),70.f);
 auto Stun=TAS->MakeOutgoingSpec(UEGMeteorStunEffect::StaticClass(),1,TAS->MakeEffectContext());Stun.Data->SetDuration(1.5f,true);Stun.Data->AddDynamicAssetTag(EGTags::ControlStun);TAS->ApplyGameplayEffectSpecToSelf(*Stun.Data);
 TestTrue(TEXT("GAS owns stun tag"),TAS->HasMatchingGameplayTag(EGTags::ControlStun));TestFalse(TEXT("Stun prevents movement/actions"),T->GetCombat()->CanAct());
 E.Tick(1.6f);TestFalse(TEXT("Stun expires"),TAS->HasMatchingGameplayTag(EGTags::ControlStun));
 E.GM->BattleRules->Stop();TestEqual(TEXT("Round cleanup restores defense"),T->GetAttributes()->GetDefense(),100.f);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveAponia,"ElyGambit.FiveHeroRework.AponiaLayers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveAponia::RunTest(const FString&)
{
 FFiveWorld E(*this);if(!E.Cards)return false;const auto Id=E.Deploy(TEXT("Aponia"),TEXT("Aponia"),2);const auto AllyId=E.Deploy(TEXT("FuHua"),TEXT("Hua"),3);
 if(!Id.IsValid()||!AllyId.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(Id);auto* Ally=E.Find(AllyId);auto Enemies=E.Enemies();if(!U||!Ally||Enemies.IsEmpty())return false;
 auto* T=Enemies[0];auto* S=U->GetHeroSkill();auto* B=E.GM->BattleRules.Get();auto* P=Cast<UEGAponiaSkillBehavior>(S->GetBehavior());if(!P)return false;
 for(int32 Level:{1,6,10,11})
 {
  S->ApplyPersistentLevel(Level);T->SetActorLocation(U->GetActorLocation()+FVector(300,0,0));
  const float Attack=U->GetAttributes()->GetAttackPower();
  for(int32 I=0;I<6;++I)B->BasicHit(Ally,T,FGuid::NewGuid());TestEqual(TEXT("Six stacks"),P->DisciplineStacks,6);
  if(!TestTrue(TEXT("Judge cast"),E.Cast(U)))return false;S->AnimationEvent(TEXT("Judge"));const int32 Old=P->LightningCount;B->BasicHit(Ally,T,FGuid::NewGuid());
  TestEqual(TEXT("Coordinated strike gated at eleven"),P->LightningCount,Old+(Level>=11?1:0));
  const float HP=T->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Release"));
  TestTrue(TEXT("Six single-target layers plus 200 percent verdict"),FMath::IsNearlyEqual(HP-T->GetAttributes()->GetHealth(),Attack*(2+6*(Level>=6?.75f:.5f)),.1f));
  TestEqual(TEXT("Exactly six layers consumed"),P->PenaltiesReleased,6);S->CancelAction();
  T->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);
 }
 if(auto* Montage=U->GetDefinition()->GetSkillMontage().LoadSynchronous())for(const auto& N:Montage->Notifies)AddInfo(FString::Printf(TEXT("Aponia notify %s at %.3f"),*N.NotifyName.ToString(),N.GetTriggerTime()));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveAdult,"ElyGambit.FiveHeroRework.AdultMeteor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveAdult::RunTest(const FString&)
{
 FFiveWorld E(*this);if(!E.Cards)return false;const auto Id=E.Deploy(TEXT("Griseo/Adult"),TEXT("GriseoAdult"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(Id);auto Enemies=E.Enemies();if(!U||Enemies.IsEmpty())return false;auto* T=Enemies[0];auto* S=U->GetHeroSkill();
 AEGChessUnit* Knight=nullptr;for(auto* A:E.GM->BattleRules->Units())if(A->GetHeroSkill()->SummonerId==Id)Knight=A;
 if(!TestNotNull(TEXT("Adult knight"),Knight))return false;const float KnightSpeed=Knight->GetAttributes()->GetAttackSpeed();
 for(int32 Level:{1,6,10,11})
 {
  S->ApplyPersistentLevel(Level);T->SetActorLocation(U->GetActorLocation()+FVector(300,0,0));T->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);
  if(!TestTrue(TEXT("Meteor cast"),E.Cast(U)))return false;S->AnimationEvent(TEXT("Release"));
  TestTrue(TEXT("Meteor remains 300 percent at all tiers"),FMath::IsNearlyEqual(10000-T->GetAttributes()->GetHealth(),U->GetAttributes()->GetAttackPower()*3,.05f));
  TestEqual(TEXT("Summon does not receive hero-only ultimate speed"),Knight->GetAttributes()->GetAttackSpeed(),KnightSpeed);
  TestEqual(TEXT("Only eleven stuns"),T->GetAbilitySystemComponent()->HasMatchingGameplayTag(EGTags::ControlStun),Level>=11);
  S->CancelAction();T->GetAbilitySystemComponent()->RemoveActiveGameplayEffectBySourceEffect(UEGMeteorStunEffect::StaticClass(),nullptr);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveRain,"ElyGambit.FiveHeroRework.RainBounce",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveRain::RunTest(const FString&)
{
 for(int32 Level:{1,6,10,11})
 {
  FFiveWorld E(*this);if(!E.Cards)return false;FGuid HeroId;
  if(!HeroId.IsValid())
  {
   auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia"));
   if(!Card||!E.Cards->GrantCard(Card)||!E.Cards->TryDeployHandCard(E.Cards->GetHand().Last().InstanceId,{2,2}))return false;
   HeroId=E.GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
  }
  if(!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(HeroId);auto Enemies=E.Enemies();if(!U||Enemies.Num()<2)return false;
  auto* S=U->GetHeroSkill();auto* P=Cast<UEGElysiaSkillBehavior>(S->GetBehavior());if(!P)return false;S->ApplyPersistentLevel(Level);
  Enemies[0]->SetActorLocation(U->GetActorLocation()+FVector(500,0,0));Enemies[1]->SetActorLocation(U->GetActorLocation()+FVector(500,250,0));
  if(!TestTrue(TEXT("Rain begins"),E.Cast(U)))return false;S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
  if(!TestTrue(TEXT("Ordinary attack can overlap rain"),S->BeginAction(false)))return false;S->AnimationEvent(TEXT("Hit"));E.Tick(.8f);
  TestEqual(TEXT("One ordinary bounce only at eleven"),P->RicochetHitCount,Level>=11?1:0);
  E.GM->BattleRules->Stop();TestFalse(TEXT("Stop clears rain"),P->IsArrowRainActive());
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveHuaLanding,"ElyGambit.FiveHeroRework.HuaLandingAndArmor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveHuaLanding::RunTest(const FString&)
{
 FFiveWorld E(*this);if(!E.Cards)return false;const auto Id=E.Deploy(TEXT("FuHua"),TEXT("Hua"),2);
 if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(Id);auto Enemies=E.Enemies();if(!U||Enemies.IsEmpty())return false;
 auto* T=Enemies[0];auto* S=U->GetHeroSkill();
 for(int32 Level:{1,6,10,11})
 {
  S->ApplyPersistentLevel(Level);U->SetActorLocation(E.Grid->CellToWorld({2,2})+FVector(0,0,88));
  T->SetActorLocation(U->GetActorLocation()+FVector(700,0,0));auto* ASC=T->GetAbilitySystemComponent();
  ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);ASC->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100);
  if(!TestTrue(TEXT("Hua leap starts"),E.Cast(U)))return false;S->AnimationEvent(TEXT("Takeoff"));S->AnimationEvent(TEXT("Release"));
  TestTrue(TEXT("One-cell slam reaches an adjacent target beyond reserved clearance"),T->GetAttributes()->GetHealth()<10000);
  TestEqual(TEXT("Armor reduction only at eleven"),T->GetAttributes()->GetDefense(),Level>=11?70.f:100.f);S->CancelAction();
  ASC->RemoveActiveGameplayEffectBySourceEffect(UEGHuaArmorEffect::StaticClass(),nullptr);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFiveAudioLifecycle,"ElyGambit.FiveHeroFXAudio.ActionLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFiveAudioLifecycle::RunTest(const FString&)
{
 for(bool Adult:{true,false})
 {
  FFiveWorld E(*this);if(!E.Cards)return false;
  const auto Id=E.Deploy(Adult?TEXT("Griseo/Adult"):TEXT("FuHua/FuSheng"),Adult?TEXT("GriseoAdult"):TEXT("CollapsedHua"),2);
  if(!Id.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(Id);auto Enemies=E.Enemies();if(!U||Enemies.IsEmpty())return false;
  auto* S=U->GetHeroSkill();auto* A=U->FindComponentByClass<UEGCombatAudioComponent>();if(!A)return false;
  const auto* P=U->GetDefinition()->GetCombatAudioPresentation();if(!TestNotNull(TEXT("Formal audio presentation"),P))return false;
  TestTrue(TEXT("Event audio enabled"),P->bUseEventAudio);
  // Explicit fixture preload: production preload remains the run's responsibility.
  for(const auto& Entry:P->AudioEvents)for(const auto& Sound:Entry.Sounds)TestNotNull(TEXT("Original clip loadable"),Sound.LoadSynchronous());
  Enemies[0]->SetActorLocation(U->GetActorLocation()+FVector(250,0,0));
  const int32 Before=A->EventCount;if(!E.Cast(U))return false;
  TestTrue(TEXT("Cast starts original audio"),A->EventCount>Before);
  const FGuid Old=S->ActionId;const int32 Start=A->EventCount;
  A->ActionEvent(TEXT("VoiceUltimate"),U->GetActorLocation());TestEqual(TEXT("Voice start deduplicates"),A->EventCount,Start);
  if(Adult)
  {
   S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Meteor sound committed once"),A->LastNode,FName(TEXT("MeteorImpact")));
   const int32 Impact=A->EventCount;S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Repeated release is silent"),A->EventCount,Impact);
  }
  if(!Adult)
  {
   S->AnimationEvent(TEXT("Burn"));TestEqual(TEXT("Burn sound is a committed source effect"),A->LastNode,FName(TEXT("Burn")));
   S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Final punch has source sound"),A->LastNode,FName(TEXT("UltimateImpact")));
   const int32 HitCount=A->EventCount;S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Final punch deduplicates"),A->EventCount,HitCount);
  }
  S->CancelAction();TestEqual(TEXT("Cancelled action voices stopped"),A->GetActiveVoiceCount(),0);
  const int32 Cancelled=A->EventCount;A->ActionPulse(TEXT("Lightning"),Old,FGuid::NewGuid(),U->GetActorLocation());TestEqual(TEXT("Stale action pulse rejected"),A->EventCount,Cancelled);
  if(!Adult)
  {
   TestTrue(TEXT("Revival begins"),S->BeginRevive(.1f,.5f,0));E.Tick(.2f);
   TestEqual(TEXT("Revival source event after resurrection"),A->LastNode,FName(TEXT("Revive")));
  }
  S->EndBattle();TestEqual(TEXT("Round cleanup stops all tails"),A->GetActiveVoiceCount(),0);
  const int32 End=A->EventCount;A->CommittedImpact(TEXT("Revive"),E.GM->BattleRules->GetRoundId(),FGuid::NewGuid(),false,INDEX_NONE,0,U->GetActorLocation());TestEqual(TEXT("Old round audio rejected"),A->EventCount,End);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGFivePenaltyAudio,"ElyGambit.FiveHeroFXAudio.PenaltyAudio",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGFivePenaltyAudio::RunTest(const FString&)
{
 FFiveWorld E(*this);if(!E.Cards)return false;const auto Id=E.Deploy(TEXT("Aponia"),TEXT("Aponia"),2);const auto AllyId=E.Deploy(TEXT("FuHua"),TEXT("Hua"),3);
 if(!Id.IsValid()||!AllyId.IsValid()||!E.GM->EnterCombat())return false;E.Freeze();auto* U=E.Find(Id);auto* Ally=E.Find(AllyId);auto Enemies=E.Enemies();if(!U||!Ally||Enemies.IsEmpty())return false;
 auto* A=U->FindComponentByClass<UEGCombatAudioComponent>();auto* S=U->GetHeroSkill();auto* B=E.GM->BattleRules.Get();if(!A)return false;
 for(const auto& Entry:U->GetDefinition()->GetCombatAudioPresentation()->AudioEvents)for(const auto& Sound:Entry.Sounds)Sound.LoadSynchronous();
 auto* T=Enemies[0];T->SetActorLocation(U->GetActorLocation()+FVector(250,0,0));S->ApplyPersistentLevel(1);
 for(int32 I=0;I<3;++I)B->BasicHit(Ally,T,FGuid::NewGuid());if(!E.Cast(U))return false;S->AnimationEvent(TEXT("Judge"));
 const int32 Before=A->EventCount;S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Three confirmed layers produce three lightning events"),A->EventCount-Before,3);
 S->AnimationEvent(TEXT("Release"));TestEqual(TEXT("Duplicate notify cannot replay layers"),A->EventCount-Before,3);S->CancelAction();TestEqual(TEXT("Lightning cancel cleanup"),A->GetActiveVoiceCount(),0);return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUltimateVoiceContract,"ElyGambit.FiveHeroFXAudio.UltimateOnlyVoiceContract",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGUltimateVoiceContract::RunTest(const FString&)
{
 const TCHAR* Paths[]={TEXT("/Game/Blueprints/Characters/ElysiaCorruption/Data/ThreeUnits20261101/DA_Corruption"),TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Human"),TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Mobius"),TEXT("/Game/Blueprints/Characters/Griseo/Adult/Data/DA_GriseoAdult"),TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Sakura"),TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV"),TEXT("/Game/Blueprints/Characters/FuHua/Data/DA_Hua"),TEXT("/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_CollapsedHua"),TEXT("/Game/Blueprints/Characters/Aponia/Data/DA_Aponia"),TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"),TEXT("/Game/Blueprints/Characters/Eden/Data/DA_Eden"),TEXT("/Game/Blueprints/Characters/Griseo/Summons/Knight/Data/DA_Knight"),TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia"),TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Griseo"),TEXT("/Game/Blueprints/Characters/Pardofelis/Summons/Can/Data/DA_Can")};

 int32 Speech=0;
 for(const TCHAR* Path:Paths)
 {
  const auto* D=LoadObject<UEGHeroDefinition>(nullptr,Path);if(!TestNotNull(Path,D))return false;
  const auto* P=D->GetCombatAudioPresentation();if(!P||!P->bUseEventAudio)continue;
  for(const auto& E:P->AudioEvents)
  {
   bool SourceVoice=E.Node.ToString().Contains(TEXT("Voice"));
   for(const auto& Sound:E.Sounds){const FString S=Sound.ToString();SourceVoice|=S.Contains(TEXT("VO_L_"))||S.Contains(TEXT("/ChineseCombat/"))||S.Contains(TEXT("/Voice/"))||S.Contains(TEXT("VO_Pardo_"));}
   TestEqual(TEXT("Original speech tagged explicitly"),E.bVoice,SourceVoice);
   TestTrue(TEXT("Speech only on ultimate"),!E.bVoice||E.bUltimate);
   const float Volume=E.Volume*P->CombatAudioVolume;
   TestTrue(TEXT("Finite nonnegative mix gain"),FMath::IsFinite(Volume)&&Volume>=0);
   const bool MachineLayer=D->HeroKind==EEGHeroKind::VillV&&(E.Node==TEXT("AutoGunShot")||E.Node==TEXT("AutoCannonShot")||E.Node==TEXT("GunLoopFull")||E.Node==TEXT("CannonFireFull")||E.Node==TEXT("CannonChargeFull")||E.Node==TEXT("GunStartFull")||E.Node==TEXT("GunEndFull"));
   const float Ceiling=E.bVoice?.3801f:MachineLayer?(E.bUltimate?.5501f:.4251f):E.bUltimate?.4201f:.2601f;
   TestTrue(TEXT("Role mix ceiling (continuous machinery calibrated separately)"),Volume<=Ceiling);
   if(E.bVoice)
   {
    ++Speech;const auto* C=E.ConcurrencyOverride.LoadSynchronous();if(!TestNotNull(TEXT("Global voice group resident"),C))return false;
    TestEqual(TEXT("At most two ultimate voices"),C->Concurrency.MaxCount,2);
    TestFalse(TEXT("Voice budget shared across heroes"),bool(C->Concurrency.bLimitToOwner));
    TestEqual(TEXT("Effective platform voice budget"),C->Concurrency.GetMaxCount(),2);
   }
  }
 }
 TestTrue(TEXT("All hero voice families audited"),Speech>=12);return true;
}

#endif

