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
#include "Data/EGRunDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"
#include "Actor/EGBoard.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGRunResourceComponent.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGMobiusSourceAttackVisual.h"
#include "Presentation/EGMobiusLinkVisual.h"
#include "Presentation/EGSourceEffect.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/GarbageCollection.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSkillNotify.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Skills/EGFlameChaserSkillBehaviors.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "EngineUtils.h"
#include "Presentation/EGMobiusNiagaraEffect.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
namespace
{
struct FAudioFXWorld
{
 TStrongObjectPtr<UGameInstance> GI;TStrongObjectPtr<UEGRunResourceComponent> Residency;UWorld* W;AEGDemoGameMode* GM;AEGChessUnit* Hero=nullptr;AEGChessUnit* Enemy=nullptr;
 FAudioFXWorld(FAutomationTestBase& Test,bool bNativeReview=false):GI(NewObject<UGameInstance>(GEngine))
 {
  GI->InitializeStandalone();W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);GM=W->GetAuthGameMode<AEGDemoGameMode>();GM->UnitClass=AEGDemoCombatUnit::StaticClass();
  auto* Base=bNativeReview?LoadObject<UEGRunDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/NativeFX20261025/Review/DA_Mobius_NativeReviewRun")):EGProductionTestAssets::ResolveRun(Test);
  if(!Base)return;
  GM->RunDefinition=DuplicateObject<UEGRunDefinition>(Base,GM);
  if(!bNativeReview&&!EGProductionTestAssets::ConfigureBehaviorEnemies(GM->RunDefinition.Get(),Test))return;
  auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
  auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
  auto* Card=LoadObject<UEGCardDefinition>(nullptr,bNativeReview?TEXT("/Game/Blueprints/Characters/Mobius/Data/NativeFX20261025/Review/DA_Mobius_NativeReviewCard"):TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius"));
  // Production now starts cosmetic graph loading in the background. These
  // headless source-lifetime tests explicitly load and pin the same graph before
  // inspecting source Actors; scene readiness alone no longer proves residency.
  Residency.Reset(NewObject<UEGRunResourceComponent>(GM));Residency->SetRequiredHeroes({Card->HeroDefinition});Residency->Poll();
  Cards->GrantCard(Card);Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,2});const FGuid Id=GM->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);GM->EnterCombat();
  for(auto* U:GM->GetCombatUnits())
  {
   U->GetCombat()->SetComponentTickEnabled(false);auto* A=U->GetAbilitySystemComponent();A->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000);A->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000);A->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0);
   if(U->GetInstanceId()==Id)Hero=U;else if(!Enemy&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemy=U;
  }
  if(Hero&&Enemy)
  {
   Enemy->SetActorLocation(Hero->GetActorLocation()+FVector(900,0,0));
   for(auto* U:GM->GetCombatUnits())if(U!=Enemy&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)U->SetActorLocation(Hero->GetActorLocation()+FVector(6000,0,0));
  }
 }
 ~FAudioFXWorld(){W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);if(Residency.Get())Residency->ResetResources();Residency.Reset();GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}
 void Tick(float Seconds){for(int32 I=0;I<FMath::CeilToInt(Seconds/.025f);++I){++GFrameCounter;W->Tick(LEVELTICK_All,.025f);}}
 int32 Visuals(){int32 N=0;for(TActorIterator<AEGMobiusSourceAttackVisual> I(W);I;++I)if(!I->IsActorBeingDestroyed())++N;return N;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusSourceContact,"ElyGambit.MobiusSakuraAudioFX.SourceContactAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusSourceContact::RunTest(const FString&)
{
 FAudioFXWorld E(*this,true);if(!TestNotNull(TEXT("Mobius deployed"),E.Hero)||!TestNotNull(TEXT("Enemy exists"),E.Enemy))return false;
 auto* S=E.Hero->GetHeroSkill();auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Four native source stages"),D&&D->BasicSourceStages.Num()==4))return false;
 for(const auto& Stage:D->BasicSourceStages)
 {
  if(!TestTrue(TEXT("Each active stage has source cues"),!Stage.Cues.IsEmpty()))return false;
  for(const auto& Cue:Stage.Cues)if(!TestTrue(TEXT("Active source cue is resident"),Cue.Assembly.IsValid()))return false;
 }
 const float HP=E.Enemy->GetAttributes()->GetHealth(),Attack=E.Hero->GetAttributes()->GetAttackPower();const int32 Before=S->CommitCount;
 if(!TestTrue(TEXT("Begin source attack"),S->BeginAction(false)))return false;
 TestEqual(TEXT("Exactly one cosmetic carrier"),E.Visuals(),1);E.Tick(.2f);
 TestEqual(TEXT("No damage from moving source visuals"),E.Enemy->GetAttributes()->GetHealth(),HP);
 E.Tick(.4f);TestEqual(TEXT("One gameplay commit"),S->CommitCount,Before+1);TestTrue(TEXT("Damage remains one original attack"),FMath::IsNearlyEqual(HP-E.Enemy->GetAttributes()->GetHealth(),Attack,.01f));
 S->CancelAction();E.Tick(.6f);if(!S->BeginAction(false))return false;S->CancelAction();E.Tick(.05f);
 TestEqual(TEXT("Cancellation removes source carrier and preview"),E.Visuals(),0);
 E.GM->BattleRules->Stop();E.Tick(.05f);TestEqual(TEXT("Round cleanup removes all source attack actors"),E.Visuals(),0);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCNVoiceBindings,"ElyGambit.MobiusSakuraAudioFX.ChineseVoiceAndCosmeticIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCNVoiceBindings::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();auto* A=E.Hero->FindComponentByClass<UEGCombatAudioComponent>();if(!S->BeginAction(false)||!A)return false;
 const int32 Before=A->EventCount,Commits=S->CommitCount;const float HP=E.Enemy->GetAttributes()->GetHealth();
 S->AnimationEvent(TEXT("Audio_AttackRelease"));S->AnimationEvent(TEXT("Audio_AttackRelease"));
 TestEqual(TEXT("Release SFX plays once per action"),A->EventCount,Before+1);TestEqual(TEXT("SFX cannot submit damage"),S->CommitCount,Commits);TestEqual(TEXT("SFX does not alter health"),E.Enemy->GetAttributes()->GetHealth(),HP);
 TestFalse(TEXT("No speech during a basic attack"),A->LastSoundPath.Contains(TEXT("VO_L_")));
 S->CancelAction();S->AnimationEvent(TEXT("Audio_AttackRelease"));TestEqual(TEXT("Late cancelled SFX rejected"),A->EventCount,Before+1);
 for(const TCHAR* Key:{TEXT("Mobius"),TEXT("Sakura")})
 {
  const auto* D=LoadObject<UEGHeroDefinition>(nullptr,*FString::Printf(TEXT("/Game/Blueprints/Characters/%s/Data/DA_%s"),Key,Key));
  if(!D||!D->GetCombatAudioPresentation()){AddError(TEXT("Missing voice presentation"));return false;}
  bool Ultimate=false,Release=false,Hit=false;int32 Variants=0;
  for(const auto& N:D->GetCombatAudioPresentation()->AudioEvents)
  {
   if(N.Node==TEXT("VoiceUltimate")){Ultimate|=N.bUltimate;Variants=N.Sounds.Num();}
   if(!N.bUltimate)
   {
    Release|=N.Node==TEXT("AttackRelease");Hit|=N.Node==TEXT("Hit");
    TestFalse(TEXT("Basic attack has no voice node"),N.Node==TEXT("VoiceAttack"));
    for(const auto& Sound:N.Sounds)TestFalse(TEXT("Basic sounds are SFX only"),Sound.ToSoftObjectPath().ToString().Contains(TEXT("VO_L_")));
   }
  }
  TestTrue(TEXT("Release and confirmed hit SFX exist"),Release&&Hit);
  TestTrue(TEXT("Chinese ultimate node exists"),Ultimate);TestEqual(TEXT("Costume-correct ultimate variants"),Variants,FString(Key)==TEXT("Mobius")?5:1);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusBoardWindows,"ElyGambit.MobiusBoardFX.SourceWindowsAndLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusBoardWindows::RunTest(const FString&)
{
 FAudioFXWorld E(*this,true);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();const auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Four independently anchored stages"),D&&D->BasicSourceStages.Num()==4))return false;
 for(int32 Index=0;Index<4;++Index)
 {
  E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(Index%2?1100:350,0,0));
  const float HP=E.Enemy->GetAttributes()->GetHealth();const int32 Commits=S->CommitCount,Attacks=S->Battle->GetAttackCount();
  if(!S->BeginAction(false))return false;
  const auto* Montage=S->GetActiveMontage();const float Rate=E.Hero->GetVisiblePresentationMesh()->GetAnimInstance()->Montage_GetPlayRate(Montage);
  float HitTime=0;for(const auto& N:Montage->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify);Notify&&Notify->Event==TEXT("Hit")){HitTime=N.GetTime();break;}
  if(!TestTrue(TEXT("Original source stage has a cue"),!D->BasicSourceStages[Index].Cues.IsEmpty()))return false;
  const float CueStart=D->BasicSourceStages[Index].Cues[0].StartTime;
  if(!TestTrue(TEXT("Authored native growth precedes gameplay contact"),CueStart<HitTime))return false;
  // Inspect inside the authored growth interval. A fixed 0.1-second lead was
  // outside the recovered native first/third-stage window (about 0.067 sec).
  const float ProbeTime=CueStart+(HitTime-CueStart)*.5f;
  E.Tick(ProbeTime/FMath::Max(.1f,Rate));
  AEGMobiusSourceAttackVisual* Carrier=nullptr;
  for(TActorIterator<AEGMobiusSourceAttackVisual> It(E.W);It;++It)if(!It->IsActorBeingDestroyed()&&It->BasicStageIndex==Index){Carrier=*It;break;}
  if(!TestNotNull(TEXT("Source stage before contact"),Carrier))return false;
  TestTrue(TEXT("Source forms before the existing Hit"),Carrier->ActiveCueCount>0);
  TestEqual(TEXT("Source appearance cannot advance damage"),E.Enemy->GetAttributes()->GetHealth(),HP);
  const FVector Anchor=Carrier->GetActorLocation();const FRotator Direction=Carrier->GetActorRotation();
  E.Enemy->AddActorWorldOffset(FVector(0,100,0));E.Tick(.025f);
  TestTrue(TEXT("Moving target cannot drag formed spikes"),Carrier->GetActorLocation().Equals(Anchor));
  TestTrue(TEXT("Moving target cannot steer formed spikes"),Carrier->GetActorRotation().Equals(Direction));
  auto* Instance=E.Hero->GetVisiblePresentationMesh()->GetAnimInstance();auto* Active=S->GetActiveMontage();
  if(!TestNotNull(TEXT("Active source montage"),Active)||!TestNotNull(TEXT("Active source animation instance"),Instance))return false;
  const float Before=Instance->Montage_GetPosition(Active);E.Tick(.025f);
  TestTrue(TEXT("Source montage progresses or completes naturally"),!S->IsActionRunning()||Instance->Montage_GetPosition(Active)>Before);
  E.Tick((HitTime-ProbeTime+.1f)/FMath::Max(.1f,Rate));
  TestEqual(TEXT("Continuous visual hits commit one gameplay attack"),S->CommitCount,Commits+1);
  TestEqual(TEXT("Continuous visual hits reward one basic attack"),S->Battle->GetAttackCount(),Attacks+1);
  const float Once=E.Enemy->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Hit"));
  TestEqual(TEXT("Duplicate contact cannot add damage"),E.Enemy->GetAttributes()->GetHealth(),Once);
  S->CancelAction();E.Tick(.05f);TestEqual(TEXT("Cancellation removes all source cues"),E.Visuals(),0);
 }
 // A target dying before contact must cancel its pending source cues too.
 if(!S->BeginAction(false))return false;E.Enemy->Die();E.Tick(.05f);
 TestEqual(TEXT("Dead target leaves no pending source stage"),E.Visuals(),0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusNativeTailLifetime,
 "ElyGambit.Mobius.NativeFX.ConfirmedTailFinishesAndFreezes",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusNativeTailLifetime::RunTest(const FString&)
{
 FAudioFXWorld E(*this,true);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();const auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!D||D->BasicSourceStages.IsEmpty()||!S->BeginAction(false))return false;
 auto Stage=D->BasicSourceStages[0];
 // Fixture removes the obsolete source-window cap; visual data and damage
 // behavior remain the same. This exercises tail lifetime rather than pixels.
 for(auto& Cue:Stage.Cues){Cue.StartTime=0;Cue.Duration=0;}
 auto* Visual=AEGMobiusSourceAttackVisual::SpawnBasic(S,E.Enemy,Stage,0);
 if(!TestNotNull(TEXT("Independent source fixture starts on the active attack"),Visual))return false;
 E.Tick(.05f);Visual->ConfirmHit();S->AnimationEvent(TEXT("Hit"));
 const int32 Commits=S->CommitCount;
 if(!TestTrue(TEXT("Confirmed source cue can release its finite tail"),Visual->ReleaseNaturalTail()))return false;
 TWeakObjectPtr<AEGMobiusSourceAttackVisual> Tail=Visual;
 S->AnimationEvent(TEXT("Finish"));E.Tick(.05f);
 if(!TestTrue(TEXT("Completing the attack preserves already triggered source curves"),Tail.IsValid()))return false;
  const float Before=Tail->SourceSampleTime;E.Tick(.05f);
  if(!TestTrue(TEXT("Released source tail remains valid within its duration"),Tail.IsValid()))return false;
  TestTrue(TEXT("Released source curves keep advancing"),Tail->SourceSampleTime>Before);
 E.Tick(3.f);
 TestFalse(TEXT("Native source tails finish without another attack or round transition"),Tail.IsValid());
 TestEqual(TEXT("Continuing source tails cannot submit extra damage or rewards"),S->CommitCount,Commits);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusBoardUltimate,"ElyGambit.MobiusBoardFX.LockedUltimateAndCleanup",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusBoardUltimate::RunTest(const FString&)
{
 FAudioFXWorld E(*this,true);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();const auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Ultimate original source layers preloaded"),D&&D->UltimateSourceAssembly.IsValid()))return false;
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 if(!S->BeginAction(true))return false;
 AEGCombatVisual* Layers=nullptr;for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)if(It->Definition==D->UltimateSourceAssembly.Get()){Layers=*It;break;}
 if(!TestNotNull(TEXT("One ground source assembly"),Layers))return false;
 const FVector Point=Layers->GetActorLocation();const int32 Commits=S->CommitCount;E.Enemy->SetActorLocation(Point+FVector(2000,0,88));E.Tick(.3f);
 TestTrue(TEXT("Moving target cannot drag source strike away from locked point"),Layers->GetActorLocation().Equals(Point));TestEqual(TEXT("Pre-release source particles cannot commit damage"),S->CommitCount,Commits);
 TWeakObjectPtr<AEGCombatVisual> Old=Layers;S->CancelAction();E.Tick(.05f);TestFalse(TEXT("Cancelling removes ultimate source assembly"),Old.IsValid());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusPersistentSerpent,"ElyGambit.MobiusBoardFX.HumanEmpowermentAndSingleCast",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusPersistentSerpent::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();S->ApplyPersistentLevel(11);auto* Policy=Cast<UEGMobiusSkillBehavior>(S->GetBehavior());const auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Original cast and storm data"),Policy&&D&&D->SerpentBody.IsValid()&&D->SerpentRecovery.IsValid()&&D->StormImpact.IsValid()))return false;
 AEGChessUnit* Partner=nullptr;for(auto* U:E.GM->GetCombatUnits())if(U!=E.Enemy&&IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy){Partner=U;break;}
 if(!Partner)return false;
 E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(600,0,0));Partner->SetActorLocation(E.Hero->GetActorLocation()+FVector(700,0,0));
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 if(!S->BeginAction(true))return false;TestFalse(TEXT("Original entry is protected"),S->IsTargetable());
 AEGCombatVisual* Body=nullptr;for(TActorIterator<AEGCombatVisual> I(E.W);I;++I)if(I->Definition==D->SerpentBody.Get()){Body=*I;break;}
 if(!TestNotNull(TEXT("One self-attached source serpent"),Body))return false;
 TestTrue(TEXT("Serpent belongs to the caster rather than target"),Body->GetAttachParentActor()==E.Hero);
 TWeakObjectPtr<AEGCombatVisual> OneShot=Body;
 Policy->TickAction(D->SerpentBody.Get()->Motions[0].Duration);
 TestFalse(TEXT("Full source clip ends before human recovery; no clamped final snake pose"),OneShot.IsValid()&&!OneShot->IsActorBeingDestroyed());
 TestTrue(TEXT("Ending the cosmetic clip does not finish the gameplay cast"),S->ActionId.IsValid()&&!Policy->EmpoweredActive);

 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));E.Tick(.05f);
 TestTrue(TEXT("Level eleven enhancement survives recovery without restarting"),Policy->EmpoweredActive&&Policy->EmpoweredRemaining>5.f&&Policy->EmpoweredRemaining<=6.f&&!Policy->OverridesBodyPresentation());
 TestFalse(TEXT("No persistent serpent state"),Policy->SerpentActive);
 TestTrue(TEXT("Sustained state remains targetable"),S->IsTargetable());
 const float OwnerHP=E.Hero->GetAttributes()->GetHealth();FEGCombatExecution::Damage(E.Enemy,E.Hero,50);
 TestTrue(TEXT("Serpent uses its existing damageable health pool"),E.Hero->GetAttributes()->GetHealth()<OwnerHP);
 TestTrue(TEXT("Enhancement permits a later paid recast"),Policy->AllowsAction(true));
 const float A=E.Enemy->GetAttributes()->GetHealth(),B=Partner->GetAttributes()->GetHealth();const int32 Attacks=S->Battle->GetAttackCount(),Storms=Policy->StormCount;
 if(!S->BeginAction(false))return false;S->AnimationEvent(TEXT("Hit"));
 TestTrue(TEXT("Form basic is an enemy-centred range storm"),E.Enemy->GetAttributes()->GetHealth()<A&&Partner->GetAttributes()->GetHealth()<B);
 TestEqual(TEXT("Range storm still rewards one basic attack"),S->Battle->GetAttackCount(),Attacks+1);TestEqual(TEXT("One storm per real Hit"),Policy->StormCount,Storms+1);
 const float Once=Partner->GetAttributes()->GetHealth();S->AnimationEvent(TEXT("Hit"));TestEqual(TEXT("Duplicate notify cannot add another storm"),Partner->GetAttributes()->GetHealth(),Once);S->AnimationEvent(TEXT("Finish"));
  const float Remaining=Policy->EmpoweredRemaining;E.Tick(.5f);
  TestTrue(TEXT("Form duration keeps decreasing on battle time"),Policy->EmpoweredRemaining<Remaining);
 E.Tick(D->EmpoweredDuration+.3f);
 TestFalse(TEXT("Enhancement expires"),Policy->EmpoweredActive);TestFalse(TEXT("Human presentation remains active"),Policy->OverridesBodyPresentation());TestTrue(TEXT("Recovery cannot leave immunity"),S->IsTargetable());
 for(TActorIterator<AEGCombatVisual> I(E.W);I;++I)if(I->Definition==D->SerpentBody.Get()||I->Definition==D->SerpentRecovery.Get())TestTrue(TEXT("Old form actors cleaned"),I->IsActorBeingDestroyed());
 // Cancellation before release never grants a new enhancement.
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 TestTrue(TEXT("Fresh cast starts"),S->BeginAction(true));S->CancelAction();
 TestFalse(TEXT("Pre-release cancel gives no enhancement"),Policy->EmpoweredActive);
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 TestTrue(TEXT("Post-release cancellation cast starts"),S->BeginAction(true));S->AnimationEvent(TEXT("Release"));
 TestTrue(TEXT("Release starts six second enhancement"),Policy->EmpoweredActive);
 S->CancelAction();TestTrue(TEXT("Post-release cancellation restores empowered human"),Policy->EmpoweredActive&&S->IsTargetable());
 Policy->EmpoweredRemaining=.01f;
 TestTrue(TEXT("Final enhanced attack starts"),S->BeginAction(false));E.Tick(.02f);
 TestFalse(TEXT("Timer expires during attack"),Policy->EmpoweredActive);
 const int32 LastStorm=Policy->StormCount;S->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Attack snapshots enhancement at start"),Policy->StormCount,LastStorm+1);S->AnimationEvent(TEXT("Finish"));
 TestTrue(TEXT("Next ordinary attack starts"),S->BeginAction(false));S->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Next attack no longer creates a storm"),Policy->StormCount,LastStorm+1);S->AnimationEvent(TEXT("Finish"));
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 TestTrue(TEXT("Repeated cast starts"),S->BeginAction(true));S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));
 TestEqual(TEXT("Recast refreshes rather than stacks duration"),Policy->EmpoweredRemaining,D->EmpoweredDuration);
 Policy->EndBattle();TestFalse(TEXT("Round cleanup removes enhancement"),Policy->EmpoweredActive);

 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusNativeRecoveryLifetime,
 "ElyGambit.Mobius.NativeFX.RecoveryEffectCompletesOnce",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusNativeRecoveryLifetime::RunTest(const FString&)
{
 FAudioFXWorld E(*this,true);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();auto* Policy=Cast<UEGMobiusSkillBehavior>(S->GetBehavior());
 const auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestTrue(TEXT("Native recovery effect is preloaded independently of the human body"),Policy&&D&&D->UltimateRecoveryEffect.IsValid()&&D->UltimateRecoveryStartTime>0.f))return false;
 auto Recovery=[&]()
 {
  TArray<AEGMobiusSourceAttackVisual*> Found;
  for(TActorIterator<AEGMobiusSourceAttackVisual> It(E.W);It;++It)
   if(!It->IsActorBeingDestroyed()&&It->SourceDefinition==D->UltimateRecoveryEffect.Get())Found.Add(*It);
  return Found;
 };
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 if(!S->BeginAction(true))return false;
 Policy->TickAction(D->UltimateRecoveryStartTime-.01f);TestEqual(TEXT("No recovery source before its authored phase"),Recovery().Num(),0);
 Policy->TickAction(D->UltimateRecoveryStartTime);if(!TestEqual(TEXT("One native recovery source at the recovery body transition"),Recovery().Num(),1))return false;
 Policy->TickAction(D->UltimateRecoveryStartTime+.2f);TestEqual(TEXT("Repeated ticks cannot replay the recovery source"),Recovery().Num(),1);
 TWeakObjectPtr<AEGMobiusSourceAttackVisual> Effect=Recovery()[0];
 const float SourceRate=E.Hero->GetVisiblePresentationMesh()->GetAnimInstance()->Montage_GetPlayRate(S->GetActiveMontage());
 S->AnimationEvent(TEXT("Release"));S->AnimationEvent(TEXT("Finish"));E.Tick(.05f);
 if(!TestTrue(TEXT("Completed human recovery keeps finite source tails alive"),Effect.IsValid()))return false;
 TestTrue(TEXT("Recovery source follows the current cast source-seconds rate"),FMath::IsNearlyEqual(Effect->SourceSampleTime,.05f*SourceRate,.01f));
 const float Before=Effect->SourceSampleTime;E.Tick(.05f);
 if(!TestTrue(TEXT("Recovery source remains valid within its duration"),Effect.IsValid()))return false;
 TestTrue(TEXT("Recovery source keeps its original forward timeline"),Effect->SourceSampleTime>Before);
 const int32 Commits=S->CommitCount;E.Tick(D->UltimateRecoveryEffect.Get()->Motions[0].Duration+.1f);
 TestFalse(TEXT("Original recovery finishes naturally without a second action"),Effect.IsValid());
 TestEqual(TEXT("Recovery particles never deal damage or grant rewards"),S->CommitCount,Commits);
 E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);
 if(!S->BeginAction(true))return false;Policy->TickAction(D->UltimateRecoveryStartTime);TestEqual(TEXT("Later cast creates one new recovery source"),Recovery().Num(),1);
 S->CancelAction();E.Tick(.05f);TestEqual(TEXT("Forced cancellation clears native recovery source"),Recovery().Num(),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusSourceLinkLifetime,
 "ElyGambit.MobiusSakuraAudioFX.SourceLinkOwnershipAndCleanup",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusSourceLinkLifetime::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* Definition=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);if(!Definition)return false;
 TGuardValue<bool> SourceMode(Definition->bUseNiagaraPresentation,false);
 const auto* Library=LoadObject<UEGSourceEffectDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/NativeFX20261025/Effects/FX_Avatar_Effect_Mobius_C1_Attack_01_Bullet"));
 if(!TestNotNull(TEXT("Original Mobius effect library"),Library))return false;
 const auto* Track=Library->Tracks.FindByPredicate([](const auto& T){return T.Material.GetAssetName().Contains(TEXT("Electric"));});
 if(!TestNotNull(TEXT("Original electricity track"),Track))return false;
 auto* Source=NewObject<UEGSourceEffectDefinition>();Source->Tracks.Add(*Track);
 TGuardValue<TSoftObjectPtr<UEGSourceEffectDefinition>> Restore(Definition->LinkSourceEffect,TSoftObjectPtr<UEGSourceEffectDefinition>(Source));
 TArray<AEGChessUnit*> Members;
 for(auto* Unit:E.GM->GetCombatUnits())if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Members.Add(Unit);
 if(!TestTrue(TEXT("Two linked enemies available"),Members.Num()>=2))return false;
 auto* V=AEGMobiusLinkVisual::Spawn(E.Hero,Members,6.f,true);
 if(!TestNotNull(TEXT("Source link actor"),V))return false;
 TestTrue(TEXT("Caster owns the cosmetic actor"),V->GetOwner()==E.Hero);
 TestFalse(TEXT("Source link does not replicate"),V->GetIsReplicated());
 TArray<UStaticMeshComponent*> Parts;V->GetComponents(Parts);
 if(!TestEqual(TEXT("Source ribbons replace procedural cylinders and placement marks"),Parts.Num(),Members.Num()))return false;
 const float HP=Members[0]->GetAttributes()->GetHealth();const int32 Commits=E.Hero->GetHeroSkill()->CommitCount;
 CollectGarbage(RF_NoFlags);V->Tick(.1f);
 TestFalse(TEXT("GC keeps the actor-owned source definition and materials alive"),V->IsActorBeingDestroyed());
 TestEqual(TEXT("Source animation cannot commit combat damage"),E.Hero->GetHeroSkill()->CommitCount,Commits);
 TestEqual(TEXT("Source animation cannot change enemy health"),Members[0]->GetAttributes()->GetHealth(),HP);
 Members[0]->SetActorLocation(Members[0]->GetActorLocation()+FVector(200,100,0));V->Tick(.1f);
 TestTrue(TEXT("Moving member produces a finite ribbon transform"),!Parts[0]->GetComponentTransform().ContainsNaN());
 TestTrue(TEXT("Headless fallback presents the original billboard front toward the sky"),Parts[0]->GetUpVector().Z>0.f);
 E.Hero->Die();V->Tick(.01f);
 TestTrue(TEXT("Caster death removes the link source"),V->IsActorBeingDestroyed());
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusMixedSourceBasic,"ElyGambit.MobiusSakuraAudioFX.MixedSourceBasicFallbackAndCancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusMixedSourceBasic::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 auto* S=E.Hero->GetHeroSkill();if(!D||!S)return false;
 TGuardValue<bool> Niagara(D->bUseNiagaraPresentation,true);
 TGuardValue<bool> UseSource(D->bUseSourceBasicPresentation,true);
 TGuardValue<TArray<FEGMobiusBasicSourceStage>> Stages(D->BasicSourceStages,D->BasicSourceStages);
 E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(350,0,0));
 const float HP=E.Enemy->GetAttributes()->GetHealth();const float Attack=E.Hero->GetAttributes()->GetAttackPower();
 const int32 Before=S->CommitCount;
 if(!TestTrue(TEXT("Ordinary source-enabled attack begins"),S->BeginAction(false)))return false;
 if(!TestEqual(TEXT("Exactly one source carrier"),E.Visuals(),1))return false;
 TestEqual(TEXT("Cosmetic spawn does not alter health"),E.Enemy->GetAttributes()->GetHealth(),HP);
 S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Hit"));
 TestEqual(TEXT("Repeated hit notify is accepted once"),S->CommitCount,Before+1);
 TestTrue(TEXT("One normal attack damage only"),FMath::IsNearlyEqual(HP-E.Enemy->GetAttributes()->GetHealth(),Attack,.01f));
 S->CancelAction();E.Tick(.05f);TestEqual(TEXT("Cancel destroys source carrier"),E.Visuals(),0);
 D->BasicSourceStages.Reset();
 if(!TestTrue(TEXT("Missing source stage still starts"),S->BeginAction(false)))return false;
 int32 Fallback=0;for(TActorIterator<AEGMobiusNiagaraEffect> It(E.W);It;++It)if(It->GetOwner()==E.Hero&&!It->IsActorBeingDestroyed())++Fallback;
 TestTrue(TEXT("Missing stage falls back to Niagara"),Fallback>0);
 TestEqual(TEXT("Fallback visual does not cause damage before hit"),S->CommitCount,Before+1);
 S->CancelAction();E.Tick(.05f);
 for(TActorIterator<AEGMobiusNiagaraEffect> It(E.W);It;++It)if(It->GetOwner()==E.Hero)TestTrue(TEXT("Cancel removes Niagara fallback"),It->IsActorBeingDestroyed());
 return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusGASSpeedCycles,"ElyGambit.MobiusSakuraAudioFX.GASSpeedAcrossCycles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusGASSpeedCycles::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* U=E.Hero;auto* S=U->GetHeroSkill();auto* ASC=U->GetAbilitySystemComponent();
 auto* Anim=U->GetVisiblePresentationMesh()->GetAnimInstance();
 auto* D=Cast<UEGMobiusSkillDefinition>(U->GetDefinition()->SkillDefinition);
 if(!S||!ASC||!Anim||!D)return false;
 TGuardValue<bool> Niagara(D->bUseNiagaraPresentation,true);
 TGuardValue<bool> SourceBasic(D->bUseSourceBasicPresentation,true);
 E.Enemy->SetActorLocation(U->GetActorLocation()+FVector(350,0,0));
 auto Speed=[&](float Value){ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(),Value);};
 Speed(1.f);
 for(int32 Cycle=0;Cycle<3;++Cycle)
 {
  const float StartSpeed=Cycle==0?1.f:Cycle==1?.5f:2.f;
  Speed(StartSpeed);const int32 Commits=S->CommitCount,Rewards=S->Battle->GetAttackCount();
  const float Health=E.Enemy->GetAttributes()->GetHealth(),Power=U->GetAttributes()->GetAttackPower();
  if(!TestTrue(TEXT("GAS-controlled snake attack begins"),S->BeginAction(false)))return false;
  auto* Montage=S->GetActiveMontage();const auto* Clip=S->GetActiveAnimation();if(!Montage||!Clip)return false;
  const float Rate=Anim->Montage_GetPlayRate(Montage);
  TestTrue(TEXT("Start rate uses current GAS speed"),FMath::IsNearlyEqual(Rate,StartSpeed*Clip->GetPlayLength(),.001f));
  E.Tick(.025f);Speed(2.f);
  TestTrue(TEXT("Mid-action speed change leaves current snake rate unchanged"),FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage),Rate,.001f));
  if(Cycle==0){Speed(.5f);TestTrue(TEXT("Second mid-action speed change leaves snake rate unchanged"),FMath::IsNearlyEqual(Anim->Montage_GetPlayRate(Montage),Rate,.001f));}
  for(int32 I=0;I<240&&S->IsActionRunning();++I)E.Tick(.025f);
  if(!TestFalse(TEXT("Snake action completes after speed changes"),S->IsActionRunning()))return false;
  TestEqual(TEXT("One hit notify committed"),S->CommitCount,Commits+1);
  TestEqual(TEXT("One reward per attack"),S->Battle->GetAttackCount(),Rewards+1);
  TestTrue(TEXT("One normal attack worth of damage"),FMath::IsNearlyEqual(Health-E.Enemy->GetAttributes()->GetHealth(),Power,.05f));
 }
 Speed(.5f);if(!TestTrue(TEXT("Start snake action for cancellation"),S->BeginAction(false)))return false;
 E.Tick(.025f);Speed(2.f);S->CancelAction();E.Tick(.05f);
 TestFalse(TEXT("Cancelled snake action stops"),S->IsActionRunning());
 TestEqual(TEXT("Cancellation clears source attack carriers"),E.Visuals(),0);
 S->Battle->Stop();E.Tick(.1f);
 TestEqual(TEXT("Round stop clears source attack carriers"),E.Visuals(),0);
 for(TActorIterator<AEGMobiusLinkVisual> It(E.W);It;++It)if(It->GetOwner()==U)TestTrue(TEXT("Round stop clears links"),It->IsActorBeingDestroyed());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusNiagaraLifetime,"ElyGambit.ChessFX.MobiusNiagaraTargetAndCancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusNiagaraLifetime::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!D)return false;
 TGuardValue<bool> NiagaraMode(D->bUseSourceBasicPresentation,false);
 if(!TestTrue(TEXT("Formal Mobius uses resident Niagara assets"),D&&D->bUseNiagaraPresentation&&D->NiagaraBasicStrike.IsValid()&&D->NiagaraBasicImpact.IsValid()))return false;
 E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(350,0,0));
 const float HP=E.Enemy->GetAttributes()->GetHealth();const int32 Commits=S->CommitCount;
 if(!TestTrue(TEXT("Begin formal Niagara attack"),S->BeginAction(false)))return false;
 TWeakObjectPtr<AEGMobiusNiagaraEffect> Visual;
 for(TActorIterator<AEGMobiusNiagaraEffect> It(E.W);It;++It)if(It->GetOwner()==E.Hero){Visual=*It;break;}
 if(!TestTrue(TEXT("Formal attack creates a cosmetic player"),Visual.IsValid()))return false;
 TestEqual(TEXT("Appearance does not advance damage"),E.Enemy->GetAttributes()->GetHealth(),HP);
 E.Enemy->Die();E.Tick(.025f);
 TestFalse(TEXT("Target death retires pending attack visuals"),Visual.IsValid());
 TestEqual(TEXT("Target invalidation cannot commit a delayed hit"),S->CommitCount,Commits);
 S->CancelAction();
 auto* Independent=AEGMobiusNiagaraEffect::Spawn(S,D->NiagaraBasicStrike.Get(),E.Hero->GetActorLocation(),FRotator::ZeroRotator,5.f);
 TWeakObjectPtr<AEGMobiusNiagaraEffect> Tail=Independent;if(!Tail.IsValid())return false;
 S->Battle->Stop();E.Tick(.05f);TestFalse(TEXT("Round stop clears independent Niagara tails"),Tail.IsValid());
 return true;
}


// Candidate-only lifecycle regression. All mutable data asset fields are scoped
// to this test and restored automatically, including on early return.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGMobiusSourceUltimateOverlay,
 "ElyGambit.MobiusBoardFX.SourceUltimateOverlayLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGMobiusSourceUltimateOverlay::RunTest(const FString&)
{
 FAudioFXWorld E(*this);if(!E.Hero||!E.Enemy)return false;
 auto* S=E.Hero->GetHeroSkill();
 auto* D=Cast<UEGMobiusSkillDefinition>(E.Hero->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("Mobius skill"),D)||!TestNotNull(TEXT("Skill component"),S))return false;
 auto* Energy=LoadObject<UEGSourcePreviewDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/AutoChess20261025/Assemblies/DA_AutoChess_Energy"));
 auto* Burst=LoadObject<UEGSourcePreviewDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Mobius/Data/AutoChess20261025/Assemblies/DA_AutoChess_Storm"));
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/Assets/FXs/Characters/Mobius/NS_Electric"));
 if(!TestNotNull(TEXT("Board source energy"),Energy)||!TestNotNull(TEXT("Board source storm"),Burst)||!TestNotNull(TEXT("Resident Niagara"),System))return false;
 TGuardValue<bool> NiagaraGuard(D->bUseNiagaraPresentation,true);
 TGuardValue<bool> OverlayGuard(D->bUseSourceUltimateOverlay,true);
 TGuardValue<TSoftObjectPtr<UEGSourcePreviewDefinition>> EnergyGuard(D->UltimateSourceAssembly,D->UltimateSourceAssembly);
 TGuardValue<TSoftObjectPtr<UEGSourcePreviewDefinition>> BurstGuard(D->StormImpact,D->StormImpact);
 TGuardValue<TSoftObjectPtr<UNiagaraSystem>> CastGuard(D->NiagaraCast,D->NiagaraCast);
 TGuardValue<TSoftObjectPtr<UNiagaraSystem>> StormGuard(D->NiagaraUltimateStorm,D->NiagaraUltimateStorm);
 D->UltimateSourceAssembly=Energy;D->StormImpact=Burst;D->NiagaraCast=System;D->NiagaraUltimateStorm=System;
 auto Mana=[&](){E.Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),100);};
 auto EnemyHP=[&](){return E.Enemy->GetAttributes()->GetHealth();};
 auto SourceEnergy=[&]()->AEGCombatVisual*
 {
  for(TActorIterator<AEGCombatVisual> It(E.W);It;++It)
   if(!It->IsActorBeingDestroyed()&&It->GetOwner()==E.Hero&&It->Definition==Energy)return *It;
  return nullptr;
 };
 auto SourceBurst=[&]()->AEGMobiusSourceAttackVisual*
 {
  for(TActorIterator<AEGMobiusSourceAttackVisual> It(E.W);It;++It)
   if(!It->IsActorBeingDestroyed()&&It->GetOwner()==E.Hero&&It->SourceDefinition==Burst)return *It;
  return nullptr;
 };
 auto NiagaraCount=[&]()->int32
 {
  int32 Count=0;
  for(TActorIterator<AEGMobiusNiagaraEffect> It(E.W);It;++It)
   if(!It->IsActorBeingDestroyed()&&It->GetOwner()==E.Hero)
    if(const auto* Component=It->FindComponentByClass<UNiagaraComponent>();Component&&Component->GetAsset()==System)++Count;
  return Count;
 };
 E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(600,0,0));
 Mana();const float HP0=EnemyHP();const int32 StartCommits=S->CommitCount;
 if(!TestTrue(TEXT("Mixed cast starts"),S->BeginAction(true)))return false;
 auto* EnergyActor=SourceEnergy();
 if(!TestNotNull(TEXT("Source atmosphere starts with Niagara"),EnergyActor))return false;
 TestTrue(TEXT("Niagara cast exists beside source atmosphere"),NiagaraCount()>0);
 E.Tick(.1f);TestEqual(TEXT("Atmosphere cannot submit damage"),EnemyHP(),HP0);
 TestEqual(TEXT("Atmosphere cannot submit a gameplay event"),S->CommitCount,StartCommits);
 TWeakObjectPtr<AEGCombatVisual> OldEnergy=EnergyActor;S->CancelAction();E.Tick(.05f);
 TestFalse(TEXT("Cancel destroys source atmosphere"),OldEnergy.IsValid()&&!OldEnergy->IsActorBeingDestroyed());
 TestEqual(TEXT("Cancel destroys Niagara cast"),NiagaraCount(),0);

 // Compare the normal Release with and without the cosmetic source overlay.
 D->bUseSourceUltimateOverlay=false;
 E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(600,0,0));Mana();
 if(!TestTrue(TEXT("Control cast starts"),S->BeginAction(true)))return false;
 const float ControlBefore=EnemyHP();S->AnimationEvent(TEXT("Release"));
 const float ControlDamage=ControlBefore-EnemyHP();
 if(!TestTrue(TEXT("Control Release damages locked target"),ControlDamage>0.f))return false;
 S->CancelAction();E.Tick(.05f);
 E.Enemy->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),ControlBefore);
 D->bUseSourceUltimateOverlay=true;
 E.Enemy->SetActorLocation(E.Hero->GetActorLocation()+FVector(600,0,0));Mana();
 const FVector Locked=E.Enemy->GetActorLocation()-FVector(0,0,88);
 if(!TestTrue(TEXT("Overlay cast starts"),S->BeginAction(true)))return false;
 EnergyActor=SourceEnergy();if(!TestNotNull(TEXT("Source atmosphere recreated"),EnergyActor))return false;
 E.Enemy->AddActorWorldOffset(FVector(0,30,0));
 const float OverlayBefore=EnemyHP();const int32 Commits=S->CommitCount;
 S->AnimationEvent(TEXT("Release"));
 const float OverlayDamage=OverlayBefore-EnemyHP();
 TestTrue(TEXT("Source overlay adds no damage"),FMath::IsNearlyEqual(OverlayDamage,ControlDamage,.01f));
 TestEqual(TEXT("Release commits once"),S->CommitCount,Commits+1);
 auto* Ground=SourceBurst();if(!TestNotNull(TEXT("Source storm starts on release"),Ground))return false;
 TestTrue(TEXT("Source storm stays at locked target ground"),Ground->GetActorLocation().Equals(Locked,.1f));
 TestTrue(TEXT("Niagara storm is spawned alongside source"),NiagaraCount()>=2);
 const float HPAfterOverlay=EnemyHP();S->AnimationEvent(TEXT("Release"));
 TestEqual(TEXT("Repeated Release does not add damage"),EnemyHP(),HPAfterOverlay);
 TestEqual(TEXT("Repeated Release does not add a commit"),S->CommitCount,Commits+1);
 TWeakObjectPtr<AEGMobiusSourceAttackVisual> OldGround=Ground;OldEnergy=EnergyActor;
 E.GM->BattleRules->Stop();E.Tick(.05f);
 TestFalse(TEXT("Round stop destroys source ground burst"),OldGround.IsValid()&&!OldGround->IsActorBeingDestroyed());
 TestFalse(TEXT("Round stop destroys source atmosphere"),OldEnergy.IsValid()&&!OldEnergy->IsActorBeingDestroyed());
 TestEqual(TEXT("Round stop destroys Niagara visuals"),NiagaraCount(),0);
 return true;
}

#endif
