#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGHitFeedbackComponent.h"
#include "Components/EGUltimateRingComponent.h"
#include "Components/CapsuleComponent.h"

#include "AbilitySystem/EGAttributeSet.h"
#include "Components/EGCombatComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "AbilitySystem/Abilities/EGBasicAttackAbility.h"
#include "AbilitySystem/Effects/EGManaEffect.h"
#include "EGGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "AIController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "UI/HUD/EGHUD.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Presentation/EGKillBeamVisual.h"

AEGChessUnit::AEGChessUnit()
{
	PrimaryActorTick.bCanEverTick = false;
	// Standalone authoritative units. Multiplayer requires an explicit ownership/replication design.
	bReplicates = false;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
	AbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystem->SetIsReplicated(false);
	GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Attributes = CreateDefaultSubobject<UEGAttributeSet>(TEXT("Attributes"));
	Combat = CreateDefaultSubobject<UEGCombatComponent>(TEXT("Combat"));
	HeroSkill=CreateDefaultSubobject<UEGHeroSkillComponent>(TEXT("HeroSkill"));
 CreateDefaultSubobject<UEGCombatAudioComponent>(TEXT("CombatAudio"));
 HitFeedback=CreateDefaultSubobject<UEGHitFeedbackComponent>(TEXT("HitFeedback"));
 UltimateRing=CreateDefaultSubobject<UEGUltimateRingComponent>(TEXT("UltimateRing"));UltimateRing->SetupAttachment(GetRootComponent());
}

void AEGChessUnit::PostInitializeComponents()
{
	Super::PostInitializeComponents();
    OnCombatResolved.AddUniqueDynamic(this,&ThisClass::PresentCombatResolution);
	AbilitySystem->InitAbilityActorInfo(this, this);
	AbilitySystem->AddAttributeSetSubobject(Attributes.Get());
	AbilitySystem->GameplayEffectApplicationQueries.Add(FGameplayEffectApplicationQuery::CreateUObject(this,&ThisClass::AcceptGameplayEffect));
	const FGameplayAttribute Watched[] = {
		UEGAttributeSet::GetHealthAttribute(),		UEGAttributeSet::GetMaxHealthAttribute(),
		UEGAttributeSet::GetManaAttribute(),		UEGAttributeSet::GetMaxManaAttribute(),
		UEGAttributeSet::GetAttackPowerAttribute(), UEGAttributeSet::GetAttackSpeedAttribute(),
		UEGAttributeSet::GetDefenseAttribute(), UEGAttributeSet::GetAttackRangeAttribute(),
		UEGAttributeSet::GetMoveSpeedAttribute(), UEGAttributeSet::GetManaOnHitAttribute(),
		UEGAttributeSet::GetManaOnDamageAttribute(), UEGAttributeSet::GetShieldAttribute(),
		UEGAttributeSet::GetDamageMultiplierAttribute(), UEGAttributeSet::GetHealingMultiplierAttribute(),
		UEGAttributeSet::GetShieldMultiplierAttribute(), UEGAttributeSet::GetSkillMultiplierAttribute()};
	for (const FGameplayAttribute& Attribute : Watched)
	{
		AbilitySystem->GetGameplayAttributeValueChangeDelegate(Attribute).AddUObject(this,
																					 &ThisClass::AttributeChanged);
	}
}

void AEGChessUnit::EndPlay(const EEndPlayReason::Type Reason)
{
 ClearBoardFriendlyMoveIgnores();
	Combat->ResetCombat();
    OnCombatResolved.RemoveDynamic(this,&ThisClass::PresentCombatResolution);
	AbilitySystem->CancelAllAbilities();
	AbilitySystem->GameplayEffectApplicationQueries.RemoveAll([this](const FGameplayEffectApplicationQuery& Query){return Query.IsBoundToObject(this);});
	AbilitySystem->ClearActorInfo();
	Super::EndPlay(Reason);
}
bool AEGChessUnit::AcceptGameplayEffect(const FActiveGameplayEffectsContainer& Container,const FGameplayEffectSpec& Spec) const
{
 // Shield is a derived source-ledger total, not an independent GE pool.
 // Reject a whole effect rather than apply its other modifiers partially.
 if(Spec.Def)for(const FGameplayModifierInfo& Mod:Spec.Def->Modifiers)
  if(Mod.Attribute==UEGAttributeSet::GetShieldAttribute())return false;
 if(!HeroSkill||!HeroSkill->IsControlImmune())return true;
 FGameplayTagContainer AssetTags;Spec.GetAllAssetTags(AssetTags);
 return !AssetTags.HasTag(EGTags::Control);
}
void AEGChessUnit::PresentCombatResolution(const FEGCombatResolution& Result)
{
 if(!bKillBeamPresented && GetNetMode()!=NM_DedicatedServer && Result.bFatal && !Result.bHealing &&
    !Result.bEnvironmentDamage && Result.HealthDelta<0.f && Result.SourceId.IsValid() &&
    Result.TargetId==InstanceId && !IsAlive_Implementation() && !HeroSkill->IsRecoverable() &&
    GetWorld() && !GetWorld()->bIsTearingDown)
 {
  bKillBeamPresented=true;
  FVector Foot=GetActorLocation()-FVector(0.f,0.f,GetSimpleCollisionHalfHeight());
  if(const auto* Mode=GetWorld()->GetAuthGameMode<AEGChessGameMode>();Mode&&Mode->GetBoard())
   Foot.Z=Mode->GetBoard()->CellToWorld(FIntPoint::ZeroValue).Z-Mode->GetBoard()->UnitCenterHeight+5.f;
  GetWorld()->SpawnActor<AEGKillBeamVisual>(Foot,FRotator::ZeroRotator);
 }
 if(!Result.bHealing&&Result.HealthDelta<0&&IsAlive_Implementation())HeroSkill->PresentIdleHit();
 if(auto* PC=UGameplayStatics::GetPlayerController(this,0))
  if(PC->IsLocalController())if(auto* HUD=Cast<AEGHUD>(PC->GetHUD()))HUD->PresentCombatText(this,Result);
}

bool AEGChessUnit::InitializeUnit(UEGHeroDefinition* Definition, FGuid HeroInstanceId, EEGFaction NewFaction,
								  int32 Level)
{
	// One combat incarnation per Actor; round recreation avoids stale GE/ability state.
	if (!HasAuthority() || bInitialized || !IsValid(Definition) || Level < 1 || NewFaction == EEGFaction::Neutral)
	{
		return false;
	}
	Configuration = Definition;
 if(Definition->MonsterAttack)Combat->SetComponentTickInterval(0.f);
	HeroSkill->Configure(nullptr,Level,false);
	InstanceId = HeroInstanceId.IsValid() ? HeroInstanceId : FGuid::NewGuid();
	Faction = NewFaction;
	InitialAttackRange = Definition->GetAttackRange();
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(), InitialAttackRange);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetMoveSpeedAttribute(), Definition->GetMoveSpeed());
	float AttackMana = Definition->GetManaOnHit();
	// Herrscher bow mana is awarded at Finish, not on contact. Keep its
	// authored amount and event rule while exposing the same runtime resource.
	if (Definition->HeroKind == EEGHeroKind::ElysiaHerrscher)
		if (const auto* Rules = Cast<UEGElysiaHerrscherSkillDefinition>(Definition->SkillDefinition)) AttackMana = Rules->ManaPerFinishedBowAttack;
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetManaOnHitAttribute(), AttackMana);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetManaOnDamageAttribute(), Definition->GetManaOnDamage());
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetDamageMultiplierAttribute(), 1.f);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetHealingMultiplierAttribute(), 1.f);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetShieldMultiplierAttribute(), 1.f);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetSkillMultiplierAttribute(), 1.f);
	ApplyLevelAttributes(Level);
	GetCharacterMovement()->MaxWalkSpeed = Attributes->GetMoveSpeed();
	const auto* GameMode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
	if(Definition->GetBoardMovement()||(GameMode&&GameMode->GetBoard()&&GameMode->GetBoard()->LayoutSource.Get()))
 {
  GetCharacterMovement()->SetMovementMode(MOVE_Flying);
  // Combat owns board translation. Do not run a second depenetration/movement driver.
  GetCharacterMovement()->SetComponentTickEnabled(false);
  GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
 }
	if (USkeletalMesh* CharacterMesh = Definition->GetMesh().LoadSynchronous())
	{
		GetMesh()->SetSkeletalMeshAsset(CharacterMesh);
	}
	if (UClass* AnimClass = Definition->GetAnimationClass().LoadSynchronous())
	{
		GetMesh()->SetAnimInstanceClass(AnimClass);
	}
	GrantStartupAbilities(Level);
	bInitialized = true;
 RefreshBoardCollision();
	return true;
}

void AEGChessUnit::RemoveBoardFriendlyMoveIgnore(AEGChessUnit* Other)
{
 if(!Other)return;
 if(BoardFriendlyMoveIgnores.Remove(Other)>0)GetCapsuleComponent()->IgnoreActorWhenMoving(Other,false);
 // A device may have its combat tick disabled. Remove the paired entry now,
 // rather than leaving a strong MoveIgnoreActors reference until another tick.
 if(Other->BoardFriendlyMoveIgnores.Remove(this)>0)Other->GetCapsuleComponent()->IgnoreActorWhenMoving(this,false);
}
void AEGChessUnit::ClearBoardFriendlyMoveIgnores()
{
 while(!BoardFriendlyMoveIgnores.IsEmpty())
 {
  if(auto* Other=BoardFriendlyMoveIgnores.Last().Get())RemoveBoardFriendlyMoveIgnore(Other);
  else BoardFriendlyMoveIgnores.Pop(EAllowShrinking::No);
 }
}

void AEGChessUnit::RefreshBoardFriendlyMoveIgnores()
{
 auto* Capsule=GetCapsuleComponent();
 const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 const bool Active=Mode&&State==EEGUnitState::Fighting&&IsAlive_Implementation()&&Faction!=EEGFaction::Neutral;
 const auto IsAlly=[&](const AEGChessUnit* Other)
 {
  if(!Active||!IsValid(Other)||Other==this||Other->State!=EEGUnitState::Fighting||
     !Other->IsAlive_Implementation()||Other->Faction!=Faction)return false;
  const auto* ThisDevice=Cast<AEGVillVEmplacement>(this);
  const auto* OtherDevice=Cast<AEGVillVEmplacement>(Other);
  return (ThisDevice&&ThisDevice->Pilot.Get()==Other)||(OtherDevice&&OtherDevice->Pilot.Get()==this);
 };
 for(int32 Index=BoardFriendlyMoveIgnores.Num()-1;Index>=0;--Index)
 {
  auto* Other=BoardFriendlyMoveIgnores[Index].Get();
  const bool Registered=Mode&&Mode->GetBattleUnits().ContainsByPredicate(
   [Other](const TWeakObjectPtr<AEGChessUnit>& Entry){return Entry.Get()==Other;});
  if(!IsAlly(Other)||!Registered)
  {
   if(Other)RemoveBoardFriendlyMoveIgnore(Other);
   else BoardFriendlyMoveIgnores.RemoveAtSwap(Index,1,EAllowShrinking::No);
  }
 }
 if(!Active)return;
 const auto AddOwnedIgnore=[](AEGChessUnit* IgnoreOwner,AEGChessUnit* Other)
 {
  auto* OwnerCapsule=IgnoreOwner->GetCapsuleComponent();
  if(!OwnerCapsule->GetMoveIgnoreActors().Contains(Other))
  {
   OwnerCapsule->IgnoreActorWhenMoving(Other,true);
   IgnoreOwner->BoardFriendlyMoveIgnores.Add(Other);
  }
 };
 for(const auto& Entry:Mode->GetBattleUnits())
  if(auto* Other=Entry.Get();IsAlly(Other))
  {
   // Register both directions immediately, including newly spawned devices.
   AddOwnedIgnore(this,Other);AddOwnedIgnore(Other,this);
  }
}

void AEGChessUnit::RefreshBoardCollision()
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EGCombat_RefreshBoardCollision);
 if(!Configuration)return;
 RefreshBoardFriendlyMoveIgnores();
 auto* Capsule=GetCapsuleComponent();
 const FVector Scale=Capsule->GetComponentScale();
 const float Radius=UEGCombatComponent::BlockingRadius(this)/FMath::Max(.01f,FMath::Min(FMath::Abs(Scale.X),FMath::Abs(Scale.Y)));
 const float HalfHeight=FMath::Max(88.f,Radius);
 // Check actual component state, not a stale cache: external Blueprint or engine
 // changes are repaired, while unchanged frames avoid physics state updates.
 if(!FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleRadius(),Radius)||
    !FMath::IsNearlyEqual(Capsule->GetUnscaledCapsuleHalfHeight(),HalfHeight))
  Capsule->SetCapsuleSize(Radius,HalfHeight,false);
 if(Capsule->GetCollisionObjectType()!=ECC_Pawn)Capsule->SetCollisionObjectType(ECC_Pawn);
 bool bResponsesMatch=true;
 for(int32 Channel=0;Channel<ECC_MAX;++Channel)
 {
  const auto Expected=Channel==ECC_Pawn?ECR_Block:ECR_Ignore;
  if(Capsule->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel))!=Expected){bResponsesMatch=false;break;}
 }
 if(!bResponsesMatch){Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);Capsule->SetCollisionResponseToChannel(ECC_Pawn,ECR_Block);}
 if(Capsule->GetGenerateOverlapEvents())Capsule->SetGenerateOverlapEvents(false);
 const bool Enabled=UEGCombatComponent::IsSpaceOccupant(this)&&State==EEGUnitState::Fighting;
 if(GetActorEnableCollision()!=Enabled)SetActorEnableCollision(Enabled);
 const auto Collision=Enabled?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision;
 if(Capsule->GetCollisionEnabled()!=Collision)Capsule->SetCollisionEnabled(Collision);
}

bool AEGChessUnit::GetPresentationClips(FEGPresentationClips& Out) const
{
	if (!Configuration) return false;
	Out = Configuration->BuildPresentationClips();
	return true;
}

bool AEGChessUnit::IsAlive_Implementation() const
{
	return bInitialized && State != EEGUnitState::Dead && Attributes->GetHealth() > 0.f && !IsActorBeingDestroyed();
}

void AEGChessUnit::SetState(EEGUnitState NewState)
{
	if (State == NewState)
	{
		return;
	}
	State = NewState;
 RefreshBoardCollision();
	OnCombatStateChanged.Broadcast(State);
}

void AEGChessUnit::BeginCombat_Implementation()
{
	if (!HasAuthority() || !IsAlive_Implementation() || State == EEGUnitState::Fighting)
	{
		return;
	}
	AbilitySystem->AddLooseGameplayTag(EGTags::Combat);
	SetState(EEGUnitState::Fighting);
 Combat->ResolveBoardOverlap();
}

void AEGChessUnit::EndCombat_Implementation()
{
	HeroSkill->EndBattle();
	AbilitySystem->RemoveLooseGameplayTag(EGTags::Combat);
	AbilitySystem->CancelAllAbilities();
	Combat->ResetCombat();
	if (State != EEGUnitState::Dead)
	{
		SetState(EEGUnitState::Idle);
	}
}

void AEGChessUnit::Die()
{
	if (!HasAuthority() || !bInitialized || bDeathPending || State == EEGUnitState::Dead)
	{
		return;
	}
	bDeathPending=true;
 HeroSkill->DiscardDeferredStigmataMana();
 NotifyPresentationWillRelease();
 if (IsActorBeingDestroyed() || IsPendingKillPending()) return;
	HeroSkill->CaptureDeathPresentation();
	HeroSkill->CancelAction();
	SetState(EEGUnitState::Dead);
	AbilitySystem->AddLooseGameplayTag(EGTags::Dead);
	HeroSkill->EndBattle();
	AbilitySystem->RemoveLooseGameplayTag(EGTags::Combat);
	AbilitySystem->CancelAllAbilities();
	Combat->ResetCombat();
	GetCharacterMovement()->DisableMovement();
	SetActorEnableCollision(false);
	OnDied.Broadcast(this);
}

bool AEGChessUnit::TryAcquirePresentationReleaseLease(const FEGPresentationReleaseLease& Lease,FDelegateHandle& OutHandle)
{
 if (bNotifyingPresentationWillRelease || PresentationReleaseLease.IsBound() || !Lease.IsBound()) return false;
 PresentationReleaseLease=Lease;
 OutHandle=PresentationReleaseLease.GetHandle();
 return true;
}
void AEGChessUnit::ReleasePresentationLease(FDelegateHandle Handle)
{
 if (Handle.IsValid() && PresentationReleaseLease.GetHandle()==Handle) PresentationReleaseLease.Unbind();
}
void AEGChessUnit::NotifyPresentationWillRelease()
{
 if (bNotifyingPresentationWillRelease) return;
 TGuardValue<bool> Guard(bNotifyingPresentationWillRelease,true);
 // The single weak presentation lease releases its body before ordinary
 // observers can synchronously replace or detach that body.
 FEGPresentationReleaseLease Lease=MoveTemp(PresentationReleaseLease);
 PresentationReleaseLease.Unbind();
 Lease.ExecuteIfBound(this);
 if (IsPendingKillPending()) return;
 OnPresentationWillRelease.Broadcast(this);
}

void AEGChessUnit::AttributeChanged(const FOnAttributeChangeData& Data)
{
	if (Data.Attribute == UEGAttributeSet::GetMoveSpeedAttribute())
	{
		GetCharacterMovement()->MaxWalkSpeed = FMath::Max(0.f, Data.NewValue);
	}
	// Process lethal damage before broadcasting attribute callbacks.
	if (bInitialized && Data.Attribute == UEGAttributeSet::GetHealthAttribute() && Data.NewValue <= 0.f)
	{
		if(!HeroSkill->TryPreventDeath())Die();
	}
	OnAttributeChanged.Broadcast(FName(*Data.Attribute.GetName()), Data.OldValue, Data.NewValue);
}

void AEGChessUnit::NotifyDamageReceived(float Amount)
{
	OnDamageReceived.Broadcast(Amount);
	if(Configuration&&Configuration->HeroKind==EEGHeroKind::ElysiaHerrscher)return;
	if (IsAlive_Implementation() && State == EEGUnitState::Fighting && Configuration && !HeroSkill->IsCasting() && GetWorld()->GetTimeSeconds()-LastDamageManaTime>=.25)
	{
		LastDamageManaTime=GetWorld()->GetTimeSeconds();RestoreMana(Attributes->GetManaOnDamage());
	}
}

float AEGChessUnit::GetUltimateBonus() const
{
	const float Value = UltimateBonus * Attributes->GetSkillMultiplier();
	return FMath::IsFinite(Value) ? Value : 0.f;
}

float AEGChessUnit::GetRuntimeAttackRange() const
{
	if (!Configuration || !Attributes || !AbilitySystem) return 0.f;
	const auto* Attack = Configuration->MonsterAttack.Get();
	if (Attack && Attack->AttackType != EEGMonsterAttackType::Melee && Attack->AttackRangeCells > 0)
	{
		const auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AEGChessGameMode>() : nullptr;
		if (const auto* Board = Mode ? Mode->GetBoard() : nullptr)
		{
			const float BoardRange = Attack->AttackRangeCells * FVector::Dist2D(Board->CellToWorld({0, 0}), Board->CellToWorld({0, 1}));
			const auto Attribute = UEGAttributeSet::GetAttackRangeAttribute();
			const float Base = BoardRange + AbilitySystem->GetNumericAttributeBase(Attribute) - InitialAttackRange;
			bool bSuccess = false;
			// Match ordinary ASC attribute aggregation: ongoing tag requirements
			// gate active modifiers through the effect delegates, not attack target tags.
			const FGameplayTagContainer EvaluationTags;
			const float Value = UAbilitySystemBlueprintLibrary::EvaluateAttributeValueWithTagsAndBase(AbilitySystem, Attribute, EvaluationTags, EvaluationTags, Base, bSuccess);
			const float Result = bSuccess ? Value : Base;
			return FMath::IsFinite(Result) ? FMath::Max(0.f, Result) : 0.f;
		}
	}
	return Attributes->GetAttackRange();
}

void AEGChessUnit::RestoreMana(float Amount)
{
	if (!HasAuthority() || !IsAlive_Implementation() || Amount <= 0.f || HeroSkill->BlocksManaGain())
	{
		return;
	}
	FGameplayEffectSpecHandle Spec =
		AbilitySystem->MakeOutgoingSpec(UEGManaEffect::StaticClass(), 1.f, AbilitySystem->MakeEffectContext());
	if (Spec.IsValid())
	{
		Spec.Data->SetSetByCallerMagnitude(EGTags::Mana, Amount);
		AbilitySystem->ApplyGameplayEffectSpecToSelf(*Spec.Data);
	}
}

bool AEGChessUnit::RequestBasicAttack()
{
	return HasAuthority() && Combat->CanAct() && AbilitySystem->TryActivateAbility(AttackHandle);
}

bool AEGChessUnit::RequestUltimate()
{
	return HasAuthority() && Combat->CanAct() && UltimateHandle.IsValid() &&
		   AbilitySystem->TryActivateAbility(UltimateHandle);
}

bool AEGChessUnit::RefreshPreparationLevel(int32 Level)
{
	if (!HasAuthority() || !bInitialized || State != EEGUnitState::Idle || Level < 1 || !Configuration)
	{
		return false;
	}
	HeroSkill->Configure(nullptr,Level,false);
	ApplyLevelAttributes(Level);
	for (FGameplayAbilitySpecHandle Handle : {AttackHandle, UltimateHandle})
	{
		if (FGameplayAbilitySpec* Spec = AbilitySystem->FindAbilitySpecFromHandle(Handle))
		{
			Spec->Level = Level;
			AbilitySystem->MarkAbilitySpecDirty(*Spec);
		}
	}
	return true;
}

void AEGChessUnit::ApplyLevelAttributes(int32 Level)
{
	const FEGStatBlock Stats = Configuration->EvaluateStats(Level);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(), Stats.MaxHealth);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetMaxManaAttribute(), Stats.MaxMana);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(), Stats.MaxHealth);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(), 0.f);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute(), Stats.AttackPower);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetAttackSpeedAttribute(), Stats.AttackSpeed);
	AbilitySystem->SetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute(), Stats.Defense);
}

void AEGChessUnit::GrantStartupAbilities(int32 Level)
{
	TSubclassOf<UGameplayAbility> AttackClass = Configuration->GetBasicAttackAbility();
	if (!AttackClass)
	{
		AttackClass = UEGBasicAttackAbility::StaticClass();
	}
	AttackHandle = AbilitySystem->GiveAbility(FGameplayAbilitySpec(AttackClass, Level, INDEX_NONE, this));
	// Missing ultimate is intentional: full mana must not create an unbounded failed activation loop.
	if (Configuration->GetUltimateAbility())
	{
		UltimateHandle =
			AbilitySystem->GiveAbility(FGameplayAbilitySpec(Configuration->GetUltimateAbility(), Level, INDEX_NONE, this));
	}
}
