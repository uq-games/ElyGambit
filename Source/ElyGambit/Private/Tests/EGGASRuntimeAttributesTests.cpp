#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Character/EGChessUnit.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGPieceTestGameMode.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Tests/EGProductionTestAssets.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Character/EGDemoCombatUnit.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Placement/EGBoardScene.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGASRuntimeAttributes,
 "ElyGambit.GAS.RuntimeAttributes.EffectsAndLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGASRuntimeAttributes::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();
 FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());
 Definition->BaseStats.MaxHealth=1000.f;Definition->BaseStats.MaxMana=100.f;
 Definition->BaseStats.Defense=0.f;Definition->AttackRange=250.f;Definition->MoveSpeed=300.f;
 Definition->ManaOnHit=12.f;Definition->ManaOnDamage=7.f;
 auto Spawn=[&](EEGFaction Faction,FVector Location)->AEGChessUnit*
 {
  FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
  auto* U=W->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),Location,FRotator::ZeroRotator,Params);
  if(!U||!U->InitializeUnit(Definition,FGuid::NewGuid(),Faction,1))return nullptr;
  U->GetCombat()->SetComponentTickEnabled(false);U->BeginCombat_Implementation();return U;
 };
 auto* Source=Spawn(EEGFaction::Player,FVector(0,0,88));
 auto* Target=Spawn(EEGFaction::Enemy,FVector(400,0,88));
 if(!TestNotNull(TEXT("source"),Source)||!TestNotNull(TEXT("target"),Target))return false;
 auto* ASC=Source->GetAbilitySystemComponent();auto* A=Source->GetAttributes();
 TestEqual(TEXT("range comes from definition"),A->GetAttackRange(),250.f);
 TestEqual(TEXT("movement comes from definition"),A->GetMoveSpeed(),300.f);
 TestEqual(TEXT("hit mana comes from definition"),A->GetManaOnHit(),12.f);
 TestEqual(TEXT("damage mana comes from definition"),A->GetManaOnDamage(),7.f);
 TestEqual(TEXT("default damage multiplier is neutral"),A->GetDamageMultiplier(),1.f);
 TestEqual(TEXT("default healing multiplier is neutral"),A->GetHealingMultiplier(),1.f);
 TestEqual(TEXT("default shield multiplier is neutral"),A->GetShieldMultiplier(),1.f);
 TestEqual(TEXT("default skill multiplier is neutral"),A->GetSkillMultiplier(),1.f);
 TestFalse(TEXT("target starts outside configured range"),Source->GetCombat()->IsInAttackRange(Target));
 TestEqual(TEXT("movement component starts at configured speed"),Source->GetCharacterMovement()->MaxWalkSpeed,300.f);

 TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());
 Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
 auto Add=[&](FGameplayAttribute Attribute,float Amount)
 {
  FGameplayModifierInfo Mod;Mod.Attribute=Attribute;Mod.ModifierOp=EGameplayModOp::AddBase;
  Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(Amount));Effect->Modifiers.Add(Mod);
 };
 Add(UEGAttributeSet::GetAttackRangeAttribute(),200.f);
 Add(UEGAttributeSet::GetMoveSpeedAttribute(),150.f);
 Add(UEGAttributeSet::GetManaOnHitAttribute(),8.f);
 Add(UEGAttributeSet::GetManaOnDamageAttribute(),4.f);
 Add(UEGAttributeSet::GetDamageMultiplierAttribute(),.5f);
 Add(UEGAttributeSet::GetHealingMultiplierAttribute(),.5f);
 Add(UEGAttributeSet::GetSkillMultiplierAttribute(),.5f);
 FGameplayEffectSpec Spec(Effect.Get(),ASC->MakeEffectContext(),1.f);
 const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);
 if(!TestTrue(TEXT("real duration effect applied"),Handle.IsValid()))return false;
 TestTrue(TEXT("live range affects attack target validation"),Source->GetCombat()->IsInAttackRange(Target));
 TestEqual(TEXT("live movement reaches character movement"),Source->GetCharacterMovement()->MaxWalkSpeed,450.f);
 TestEqual(TEXT("hit mana is modified"),A->GetManaOnHit(),20.f);
 Source->NotifyDamageReceived(1.f);
 TestEqual(TEXT("damage mana uses current attribute"),A->GetMana(),11.f);
 Source->NotifyDamageReceived(1.f);
 TestEqual(TEXT("damage mana cooldown still deduplicates"),A->GetMana(),11.f);
 auto* BattleOwner=W->SpawnActor<AActor>();
 auto* Battle=NewObject<UEGBattleContextComponent>(BattleOwner);BattleOwner->AddInstanceComponent(Battle);Battle->RegisterComponent();Battle->Start({});
 const FGuid HitId=FGuid::NewGuid();Battle->BasicHit(Source,Target,HitId);Battle->BasicHit(Source,Target,HitId);
 TestEqual(TEXT("one basic action uses modified mana exactly once"),A->GetMana(),31.f);
 Battle->EffectiveBasicHeal(Source,FGuid::NewGuid(),10.f);
 TestEqual(TEXT("effective basic heal uses same modified hit mana"),A->GetMana(),51.f);
 Source->SetUltimateBonus(1.25f);
 TestTrue(TEXT("skill and existing awakening multiply once"),FMath::IsNearlyEqual(Source->GetUltimateBonus(),1.875f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(),100.f);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),125.f);
 const float BeforeBasic=Target->GetAttributes()->GetHealth();
 Source->GetCombat()->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("generic attack uses proportional defense after source damage bonus"),FMath::IsNearlyEqual(BeforeBasic-Target->GetAttributes()->GetHealth(),150.f*100.f/225.f,.01f));
 TestEqual(TEXT("successful generic attack grants modified hit mana"),A->GetMana(),71.f);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000.f);
 const float InitialHealth=Target->GetAttributes()->GetHealth();
 TestTrue(TEXT("damage accepted"),FEGCombatExecution::Damage(Source,Target,100.f));
 TestTrue(TEXT("source damage modifier applies once"),FMath::IsNearlyEqual(InitialHealth-Target->GetAttributes()->GetHealth(),150.f,.01f));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealingMultiplierAttribute(),2.f);
 TestTrue(TEXT("heal accepted"),FEGCombatExecution::Heal(Source,Target,40.f));
 TestTrue(TEXT("heal uses healer modifier"),FMath::IsNearlyEqual(Target->GetAttributes()->GetHealth(),InitialHealth-90.f,.01f));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),125.f);
 const float BeforePiercing=Target->GetAttributes()->GetHealth();
 FEGCombatExecution::Damage(Source,Target,100.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),false,.5f);
 TestTrue(TEXT("defense scale applies before proportional mitigation"),FMath::IsNearlyEqual(BeforePiercing-Target->GetAttributes()->GetHealth(),150.f*100.f/162.5f,.01f));
 const float BeforeLinked=Target->GetAttributes()->GetHealth();
 FEGCombatExecution::Damage(Source,Target,100.f,0.f,1.f,FGameplayEffectContextHandle(),FGuid::NewGuid(),true);
 TestTrue(TEXT("already resolved linked damage bypasses defense and outgoing multiplier"),FMath::IsNearlyEqual(BeforeLinked-Target->GetAttributes()->GetHealth(),100.f,.01f));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 const float BeforeEnvironment=Target->GetAttributes()->GetHealth();
 FEGCombatExecution::EnvironmentDamage(Target,100.f,FGuid::NewGuid());
 TestTrue(TEXT("environment damage has no outgoing source multiplier"),FMath::IsNearlyEqual(BeforeEnvironment-Target->GetAttributes()->GetHealth(),100.f,.01f));
 ASC->RemoveActiveGameplayEffect(Handle);
 TestFalse(TEXT("range effect removal restores targeting"),Source->GetCombat()->IsInAttackRange(Target));
 TestEqual(TEXT("movement effect removal restores speed"),Source->GetCharacterMovement()->MaxWalkSpeed,300.f);
 TestEqual(TEXT("hit mana effect removal restores base"),A->GetManaOnHit(),12.f);
 TestTrue(TEXT("skill effect removal preserves awakening"),FMath::IsNearlyEqual(Source->GetUltimateBonus(),1.25f));
 Target->SetActorLocation(FVector(200,0,88));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),125.f);
 Source->GetCombat()->ResetCombat();
 const float ArmoredHealth=Target->GetAttributes()->GetHealth(),ArmoredMana=A->GetMana();
 Source->GetCombat()->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("ordinary damage remains positive against higher defense"),FMath::IsNearlyEqual(ArmoredHealth-Target->GetAttributes()->GetHealth(),100.f*100.f/225.f,.01f));
 TestTrue(TEXT("successful armored hit grants hit mana"),FMath::IsNearlyEqual(A->GetMana(),ArmoredMana+12.f,.01f));
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),0.f);
 const float BeforeUnbuffed=Target->GetAttributes()->GetHealth();
 FEGCombatExecution::Damage(Source,Target,100.f);
 TestTrue(TEXT("damage reverts without double application"),FMath::IsNearlyEqual(BeforeUnbuffed-Target->GetAttributes()->GetHealth(),100.f,.01f));
 TestEqual(TEXT("definition range never changes"),Definition->GetAttackRange(),250.f);
 TestEqual(TEXT("definition movement never changes"),Definition->GetMoveSpeed(),300.f);
 auto* Fresh=Spawn(EEGFaction::Player,FVector(1000,0,88));
 if(TestNotNull(TEXT("fresh battle incarnation"),Fresh))
 {
  TestEqual(TEXT("fresh incarnation restores configured range"),Fresh->GetAttributes()->GetAttackRange(),250.f);
  TestEqual(TEXT("fresh incarnation has no old damage effect"),Fresh->GetAttributes()->GetDamageMultiplier(),1.f);
  TestEqual(TEXT("fresh incarnation has no old skill effect"),Fresh->GetAttributes()->GetSkillMultiplier(),1.f);
 }
 auto* ExecutionTarget=Spawn(EEGFaction::Enemy,FVector(1200,0,88));
 if(TestNotNull(TEXT("execution target"),ExecutionTarget))
 {
  ExecutionTarget->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(),100000.f);
  ExecutionTarget->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),200.f);
  TestTrue(TEXT("execution is accepted despite high defense"),FEGCombatExecution::Execute(Source,ExecutionTarget,FGuid::NewGuid()));
  TestFalse(TEXT("execution bypasses armor and is lethal"),IEGCombatant::Execute_IsAlive(ExecutionTarget));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGASMonsterRange,
 "ElyGambit.GAS.RuntimeAttributes.MonsterCellRange",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGASMonsterRange::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* Mode=W->GetAuthGameMode<AEGChessGameMode>();if(!TestNotNull(TEXT("game mode"),Mode))return false;
 Mode->bAutoStart=false;W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestNotNull(TEXT("board"),Mode->GetBoard()))return false;
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());Definition->AttackRange=250.f;
 auto* Attack=NewObject<UEGMonsterAttackDefinition>(Definition);Attack->AttackType=EEGMonsterAttackType::Projectile;Attack->AttackRangeCells=2.f;Definition->MonsterAttack=Attack;
 auto* U=W->SpawnActor<AEGChessUnit>();
 if(!TestTrue(TEXT("ranged monster initialized"),U&&U->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 U->GetCombat()->SetComponentTickEnabled(false);
 const float Spacing=FVector::Dist2D(Mode->GetBoard()->CellToWorld({0,0}),Mode->GetBoard()->CellToWorld({0,1}));
 const float Baseline=Spacing*2.f;
 TestTrue(TEXT("baseline follows authored board cell range"),FMath::IsNearlyEqual(U->GetCombat()->GetEffectiveAttackRange(nullptr),Baseline,.01f));
 TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo Flat;Flat.Attribute=UEGAttributeSet::GetAttackRangeAttribute();Flat.ModifierOp=EGameplayModOp::AddBase;
 Flat.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(100.f));Effect->Modifiers.Add(Flat);
 FGameplayModifierInfo Mult;Mult.Attribute=UEGAttributeSet::GetAttackRangeAttribute();Mult.ModifierOp=EGameplayModOp::Multiplicitive;
 Mult.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(1.5f));Effect->Modifiers.Add(Mult);
 auto* ASC=U->GetAbilitySystemComponent();FGameplayEffectSpec Spec(Effect.Get(),ASC->MakeEffectContext(),1.f);
 const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);
 if(!TestTrue(TEXT("range effect applied"),Handle.IsValid()))return false;
 TestTrue(TEXT("flat and multiplier modify cell-based reach"),FMath::IsNearlyEqual(U->GetCombat()->GetEffectiveAttackRange(nullptr),(Baseline+100.f)*1.5f,.05f));
 ASC->RemoveActiveGameplayEffect(Handle);
 TestTrue(TEXT("removal restores authored cell reach"),FMath::IsNearlyEqual(U->GetCombat()->GetEffectiveAttackRange(nullptr),Baseline,.01f));
 Mode->BattleRules->Start({});
 auto* SummonDefinition=NewObject<UEGHeroDefinition>(Mode);SummonDefinition->AttackRange=250.f;SummonDefinition->ManaOnHit=9.f;
 auto* Summon=Mode->SpawnBattleSummon(SummonDefinition,1,FVector(1500,0,88),EEGFaction::Player);
 if(!TestNotNull(TEXT("battle summon"),Summon))return false;
 Summon->GetCombat()->SetComponentTickEnabled(false);Summon->GetHeroSkill()->bSummon=true;
 TestEqual(TEXT("summon initializes range from its own definition"),Summon->GetAttributes()->GetAttackRange(),250.f);
 TestEqual(TEXT("summon initializes hit mana from its own definition"),Summon->GetAttributes()->GetManaOnHit(),9.f);
 auto* SummonASC=Summon->GetAbilitySystemComponent();FGameplayEffectSpec SummonSpec(Effect.Get(),SummonASC->MakeEffectContext(),1.f);
 const auto SummonHandle=SummonASC->ApplyGameplayEffectSpecToSelf(SummonSpec);
 TestTrue(TEXT("summon accepts range effect"),SummonHandle.IsValid());
 TestTrue(TEXT("summon resolves live range"),FMath::IsNearlyEqual(Summon->GetRuntimeAttackRange(),525.f,.05f));
 SummonASC->RemoveActiveGameplayEffect(SummonHandle);
 TestEqual(TEXT("summon range restores after removal"),Summon->GetRuntimeAttackRange(),250.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGASBoardMovement,
 "ElyGambit.GAS.RuntimeAttributes.BoardMovement",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGASBoardMovement::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 // Pursuit now requires a layout-backed board. Use one large enough for the
 // controlled positions instead of asking a generic GameMode to navigate.
 W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* Mode=W->GetAuthGameMode<AEGChessGameMode>();
 if(!TestNotNull(TEXT("Board movement game mode"),Mode))return false;
 Mode->bAutoStart=false;
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 if(!TestNotNull(TEXT("Movement layout"),Grid))return false;
 Grid->Columns=20;Grid->Rows=6;Grid->PlayerRows=3;
 W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestNotNull(TEXT("Movement combat board"),Mode->GetBoard()))return false;
 Mode->GetBoard()->UseLayout(Grid);
 auto* Definition=NewObject<UEGHeroDefinition>(GI.Get());Definition->bBoardMovement=true;Definition->MoveSpeed=300.f;Definition->AttackRange=150.f;
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* U=W->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),FVector(0,0,88),FRotator::ZeroRotator,Params);
 auto* Enemy=W->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),FVector(1200,0,88),FRotator::ZeroRotator,Params);
 if(!TestTrue(TEXT("board units initialized"),U&&Enemy&&U->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Player,1)&&Enemy->InitializeUnit(Definition,FGuid::NewGuid(),EEGFaction::Enemy,1)))return false;
 U->GetCombat()->SetComponentTickEnabled(false);Enemy->GetCombat()->SetComponentTickEnabled(false);
 U->BeginCombat_Implementation();Enemy->BeginCombat_Implementation();
 TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo Mod;Mod.Attribute=UEGAttributeSet::GetMoveSpeedAttribute();Mod.ModifierOp=EGameplayModOp::AddBase;
 Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(150.f));Effect->Modifiers.Add(Mod);
 auto* ASC=U->GetAbilitySystemComponent();FGameplayEffectSpec Spec(Effect.Get(),ASC->MakeEffectContext(),1.f);
 const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);
 if(!TestTrue(TEXT("movement effect applied"),Handle.IsValid()))return false;
 const FVector Before=U->GetActorLocation();U->GetCombat()->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("board pursuit uses live 450 cm/s"),FMath::IsNearlyEqual(FVector::Dist2D(Before,U->GetActorLocation()),45.f,.05f));
 ASC->RemoveActiveGameplayEffect(Handle);
 const FVector After=U->GetActorLocation();U->GetCombat()->TickComponent(.1f,LEVELTICK_All,nullptr);
 TestTrue(TEXT("board pursuit restores 300 cm/s after removal"),FMath::IsNearlyEqual(FVector::Dist2D(After,U->GetActorLocation()),30.f,.05f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGASShieldSources,
 "ElyGambit.GAS.RuntimeAttributes.ShieldSources",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGASShieldSources::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* U=W->SpawnActor<AEGChessUnit>();auto* D=NewObject<UEGHeroDefinition>(U);
 if(!TestTrue(TEXT("initialized"),U&&U->InitializeUnit(D,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 U->GetCombat()->SetComponentTickEnabled(false);auto* S=U->GetHeroSkill();auto* A=U->GetAttributes();
 FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Provider=W->SpawnActor<AEGChessUnit>(AEGChessUnit::StaticClass(),FVector(1000,0,88),FRotator::ZeroRotator,Params);
 if(!TestTrue(TEXT("shield provider initialized"),Provider&&Provider->InitializeUnit(D,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 Provider->GetCombat()->SetComponentTickEnabled(false);
 TStrongObjectPtr<UGameplayEffect> Buff(NewObject<UGameplayEffect>());Buff->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo Mod;Mod.Attribute=UEGAttributeSet::GetShieldMultiplierAttribute();Mod.ModifierOp=EGameplayModOp::AddBase;
 Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(.5f));Buff->Modifiers.Add(Mod);
 auto* ProviderASC=Provider->GetAbilitySystemComponent();FGameplayEffectSpec Spec(Buff.Get(),ProviderASC->MakeEffectContext(),1.f);
 const auto BuffHandle=ProviderASC->ApplyGameplayEffectSpecToSelf(Spec);
 if(!TestTrue(TEXT("shield strength effect applied"),BuffHandle.IsValid()))return false;
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetShieldMultiplierAttribute(),2.f);
 S->GrantSourceShield(TEXT("Provider"),40.f,10.f,Provider);
 TestEqual(TEXT("shield uses provider multiplier, not recipient"),S->GetSourceShield(TEXT("Provider")),60.f);
 ProviderASC->RemoveActiveGameplayEffect(BuffHandle);
 TestEqual(TEXT("ending a strength effect does not retroactively resize a shield"),S->GetSourceShield(TEXT("Provider")),60.f);
 S->EndBattle();
 U->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetShieldMultiplierAttribute(),1.f);
 S->SetShield(20.f);S->GrantSourceShield(TEXT("Short"),40.f,1.f);S->GrantSourceShield(TEXT("Long"),60.f,10.f);
 TestEqual(TEXT("total includes independent sources"),A->GetShield(),120.f);
 TStrongObjectPtr<UGameplayEffect> InvalidShieldEdit(NewObject<UGameplayEffect>());InvalidShieldEdit->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo ShieldMod;ShieldMod.Attribute=UEGAttributeSet::GetShieldAttribute();ShieldMod.ModifierOp=EGameplayModOp::AddBase;
 ShieldMod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(100.f));InvalidShieldEdit->Modifiers.Add(ShieldMod);
 auto* TargetASC=U->GetAbilitySystemComponent();FGameplayEffectSpec InvalidSpec(InvalidShieldEdit.Get(),TargetASC->MakeEffectContext(),1.f);
 TestFalse(TEXT("direct effect on derived total is rejected"),TargetASC->ApplyGameplayEffectSpecToSelf(InvalidSpec).IsValid());
 TestEqual(TEXT("rejected effect leaves source total intact"),A->GetShield(),120.f);
 S->GrantSourceShield(TEXT("Short"),50.f,1.f);
 TestEqual(TEXT("same source refresh replaces amount"),A->GetShield(),130.f);
 TestEqual(TEXT("absorption uses expiring source first"),S->AbsorbShield(55.f),55.f);
 TestEqual(TEXT("short source consumed first"),S->GetSourceShield(TEXT("Short")),0.f);
 TestEqual(TEXT("long source keeps remainder"),S->GetSourceShield(TEXT("Long")),55.f);
 TestEqual(TEXT("total tracks consumption"),A->GetShield(),75.f);
 for(int32 I=0;I<102;++I){++GFrameCounter;W->Tick(LEVELTICK_All,.1f);}
 TestEqual(TEXT("source expiry removes remaining shield"),S->GetSourceShield(TEXT("Long")),0.f);
 TestEqual(TEXT("legacy shield survives independent expiry"),A->GetShield(),20.f);
 S->EndBattle();
 TestEqual(TEXT("battle end clears total"),A->GetShield(),0.f);
 bool bReentered=false;
 const auto Listener=TargetASC->GetGameplayAttributeValueChangeDelegate(UEGAttributeSet::GetShieldAttribute()).AddLambda([&](const FOnAttributeChangeData& Change)
 {
  if(!bReentered&&Change.NewValue>0.f){bReentered=true;S->AbsorbShield(30.f);}
 });
 S->GrantSourceShield(TEXT("Reentrant"),100.f,5.f);
 TargetASC->GetGameplayAttributeValueChangeDelegate(UEGAttributeSet::GetShieldAttribute()).Remove(Listener);
 TestTrue(TEXT("shield attribute listener reentered"),bReentered);
 TestEqual(TEXT("reentrant absorption updates source ledger"),S->GetSourceShield(TEXT("Reentrant")),70.f);
 TestEqual(TEXT("reentrant absorption updates GAS total"),A->GetShield(),70.f);
 TestEqual(TEXT("reentrant absorption keeps legacy mirror coherent"),S->Shield,70.f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGASMonsterSkillShield,
 "ElyGambit.GAS.RuntimeAttributes.MonsterSkillShield",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGASMonsterSkillShield::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* Grid=W->SpawnActor<AEGDeploymentBoard>();Grid->Columns=6;Grid->Rows=6;Grid->PlayerRows=3;
 W->InitializeActorsForPlay(URL);W->BeginPlay();auto* Mode=W->GetAuthGameMode<AEGPieceTestGameMode>();
 if(!TestNotNull(TEXT("piece test mode"),Mode)||!TestTrue(TEXT("combat starts"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();AEGChessUnit* Monster=nullptr;
 for(const auto& Entry:Mode->GetBattleUnits())if(auto* U=Entry.Get())
 {
  U->GetCombat()->SetComponentTickEnabled(false);
  if(U->GetDefinition()->MonsterAttack&&U->GetDefinition()->MonsterAttack->Ultimate==EEGMonsterUltimate::Shield)Monster=U;
 }
 if(!TestNotNull(TEXT("shield monster"),Monster))return false;
 auto* ASC=Monster->GetAbilitySystemComponent();auto* Skill=Monster->GetHeroSkill();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000.f);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000.f);
 TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
 for(const auto& Entry:{TPair<FGameplayAttribute,float>(UEGAttributeSet::GetSkillMultiplierAttribute(),.5f),TPair<FGameplayAttribute,float>(UEGAttributeSet::GetShieldMultiplierAttribute(),.2f)})
 {
  FGameplayModifierInfo Mod;Mod.Attribute=Entry.Key;Mod.ModifierOp=EGameplayModOp::AddBase;
  Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(Entry.Value));Effect->Modifiers.Add(Mod);
 }
 FGameplayEffectSpec Spec(Effect.Get(),ASC->MakeEffectContext(),1.f);const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);
 if(!TestTrue(TEXT("skill and shield effect applied"),Handle.IsValid()))return false;
 Monster->GetCombat()->SetCombatants(TArray<TWeakObjectPtr<AEGChessUnit>>{});
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Monster->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("targetless shield skill starts"),Monster->RequestUltimate()))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->CancelAction();
 const float Base=10000.f*Monster->GetDefinition()->MonsterAttack->ShieldHealthRatio;
 TestTrue(TEXT("skill and shield strength each apply once"),FMath::IsNearlyEqual(Skill->GetSourceShield(TEXT("MonsterShield")),Base*1.5f*1.2f,.05f));
 ASC->RemoveActiveGameplayEffect(Handle);
 TestTrue(TEXT("existing shield keeps its release snapshot"),FMath::IsNearlyEqual(Skill->GetSourceShield(TEXT("MonsterShield")),Base*1.5f*1.2f,.05f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Monster->GetAttributes()->GetMaxMana());
 if(!TestTrue(TEXT("shield recast starts"),Monster->RequestUltimate()))return false;
 Skill->AnimationEvent(TEXT("Release"));Skill->CancelAction();
 TestTrue(TEXT("recast refresh uses restored neutral strengths"),FMath::IsNearlyEqual(Skill->GetSourceShield(TEXT("MonsterShield")),Base,.05f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGGASHumanBowMana,
 "ElyGambit.GAS.RuntimeAttributes.HumanBowMana",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGGASHumanBowMana::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();UWorld* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AEGDemoGameMode::StaticClass();FURL URL;W->SetGameMode(URL);
 auto* Mode=W->GetAuthGameMode<AEGDemoGameMode>();if(!TestNotNull(TEXT("game mode"),Mode))return false;
 TSoftObjectPtr<UEGCardDefinition> FormalHumanCard;
 Mode->UnitClass=AEGDemoCombatUnit::StaticClass();Mode->RunDefinition=EGProductionTestAssets::ResolveRun(*this,&FormalHumanCard);
 auto* Scene=W->SpawnActor<AEGBoardSceneSettings>();auto* Grid=W->SpawnActor<AEGDeploymentBoard>();
 Grid->Columns=Grid->Rows=7;Grid->PlayerRows=3;Grid->NeutralRows=1;Grid->CellWidth=Grid->CellDepth=500;
 auto* Cards=W->SpawnActor<AEGCardManager>();Cards->Board=Grid;Scene->Manager=Cards;Scene->Board=Grid;
 Scene->CombatBoard=W->SpawnActor<AEGBoard>();Scene->CombatBoard->Dimensions={7,7};W->InitializeActorsForPlay(URL);W->BeginPlay();
 // Inject the production endgame reward only in this controlled fixture.
 UEGCardDefinition* Card=FormalHumanCard.LoadSynchronous();
 if(!TestNotNull(TEXT("human card"),Card)||!TestTrue(TEXT("grant human card"),Cards->GrantCard(Card))||
    !TestTrue(TEXT("deploy human card"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,{2,1}))||
    !TestTrue(TEXT("enter combat"),Mode->EnterCombat()))return false;
 Mode->BattleRules->CompleteOpeningForTests();AEGChessUnit* Human=nullptr;TArray<AEGChessUnit*> Enemies;
 for(auto* U:Mode->GetCombatUnits())
 {
  U->GetCombat()->SetComponentTickEnabled(false);
  if(U->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=U;
  else if(IEGCombatant::Execute_GetFaction(U)==EEGFaction::Enemy)Enemies.Add(U);
 }
 if(!TestNotNull(TEXT("human"),Human)||!TestTrue(TEXT("enemy available"),Enemies.Num()>0))return false;
 for(int32 I=0;I<Enemies.Num();++I)Enemies[I]->SetActorLocation(Grid->CellToWorld({2+I,4})+FVector(0,0,88));
 auto* Definition=Cast<UEGElysiaHerrscherSkillDefinition>(Human->GetDefinition()->SkillDefinition);
 if(!TestNotNull(TEXT("human rules"),Definition))return false;
 auto* ASC=Human->GetAbilitySystemComponent();auto* S=Human->GetHeroSkill();
 TestEqual(TEXT("human bow mana comes from its own skill definition"),Human->GetAttributes()->GetManaOnHit(),Definition->ManaPerFinishedBowAttack);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
 if(!TestTrue(TEXT("first bow starts"),S->BeginAction(false)))return false;
 S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
 TestEqual(TEXT("released bow restores base mana"),Human->GetAttributes()->GetMana(),Definition->ManaPerFinishedBowAttack);
 TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());Effect->DurationPolicy=EGameplayEffectDurationType::Infinite;
 FGameplayModifierInfo Mod;Mod.Attribute=UEGAttributeSet::GetManaOnHitAttribute();Mod.ModifierOp=EGameplayModOp::AddBase;
 Mod.ModifierMagnitude=FGameplayEffectModifierMagnitude(FScalableFloat(7.f));Effect->Modifiers.Add(Mod);
 FGameplayEffectSpec Spec(Effect.Get(),ASC->MakeEffectContext(),1.f);const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(Spec);
 if(!TestTrue(TEXT("bow mana effect applied"),Handle.IsValid()))return false;
 if(!TestTrue(TEXT("second bow starts"),S->BeginAction(false)))return false;
 S->AnimationEvent(TEXT("Hit"));S->AnimationEvent(TEXT("Finish"));
 TestEqual(TEXT("second released bow uses modified mana"),Human->GetAttributes()->GetMana(),Definition->ManaPerFinishedBowAttack*2.f+7.f);
 const float BeforeCancel=Human->GetAttributes()->GetMana();
 if(!TestTrue(TEXT("third bow starts"),S->BeginAction(false)))return false;
 S->CancelAction();S->AnimationEvent(TEXT("Finish"));
 TestEqual(TEXT("cancelled bow grants no mana"),Human->GetAttributes()->GetMana(),BeforeCancel);
 ASC->RemoveActiveGameplayEffect(Handle);
 TestEqual(TEXT("removing effect restores authored bow mana"),Human->GetAttributes()->GetManaOnHit(),Definition->ManaPerFinishedBowAttack);
 return true;
}
#endif
