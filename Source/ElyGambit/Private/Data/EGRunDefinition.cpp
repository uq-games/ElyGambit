#include "Data/EGRunDefinition.h"
#include "Data/EGEnvironmentTrapDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "NiagaraSystem.h"
#include "Placement/EGPlacementVisuals.h"

#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGDeploymentBoard.h"
#include "EGNativeMeshPassComponent.h"
#include "Materials/MaterialInterface.h"
#include "Curves/CurveFloat.h"

bool UEGRunDefinition::ValidateStartingCards(FString& Error) const
{
	if (!bUseExplicitStartingCards)return true;
	if (!bRoundShopRules || ExplicitStartingCards.IsEmpty() || ExplicitStartingCards[0].Card != StartingCard ||
	    (CardRules.HandLimit>0 && ExplicitStartingCards.Num()>CardRules.HandLimit))
	{Error=TEXT("Explicit opening cards require a round shop, available hand capacity and StartingCard first.");return false;}
	TSet<FName> OpeningCardIds,OpeningHeroIds;
	for(const auto& Grant:ExplicitStartingCards)
	{
		const auto* Card=Grant.Card.Get();
		if(!Card||!Card->HeroDefinition||!Card->PieceClass||Card->CardId.IsNone()||
		   Card->HeroDefinition->HeroId.IsNone()||Grant.InitialLevel<1||
		   OpeningCardIds.Contains(Card->CardId)||OpeningHeroIds.Contains(Card->HeroDefinition->HeroId))
		{Error=TEXT("Explicit opening grants require unique valid cards, heroes and positive levels.");return false;}
		OpeningCardIds.Add(Card->CardId);OpeningHeroIds.Add(Card->HeroDefinition->HeroId);
	}
	return true;
}

bool UEGRunDefinition::ValidateConfiguration(const AEGDeploymentBoard* Board, FString& Error) const
{
	if (!IsValid(Board) || !StartingCard || !StartingCard->HeroDefinition || !StartingCard->PieceClass ||
		StartingCards < 1 || StartingGold < 0 || !FMath::IsFinite(BattleTimeLimit) || BattleTimeLimit <= 0.f ||
		Stages.IsEmpty() || Stages.Num() > 7 || (bSevenStageRules && Stages.Num()!=7) || CardRules.PurchasePrice < 0 || CardRules.HandLimit < 0 ||
		CardRules.DeploymentLimit < 0 || CardRules.SameNameLimit < 0 ||
		(CardRules.HandLimit > 0 && (bUseExplicitStartingCards ? ExplicitStartingCards.Num() : bSevenStageRules ? 2 : StartingCards) > CardRules.HandLimit) ||
		(bUseFixedPopulation && FixedPopulation < 1))
	{
		Error = TEXT("Invalid starting card, economy, battle limit or stage count.");
		return false;
	}
	if (bRoundShopRules && (CardPool.IsEmpty() || RoundIncome < 0))
	{
		Error = TEXT("Round shop requires a nonempty card pool and nonnegative round income.");
		return false;
	}
	if(!ValidateStartingCards(Error))return false;
	TSet<FName> StageIds;
	TSet<FName> CardIds;
	for(const auto& Card:CardPool)
	{if(!Card||Card->CardId.IsNone()||CardIds.Contains(Card->CardId)||!Card->HeroDefinition||!Card->PieceClass){Error=TEXT("CardPool requires unique valid card IDs, heroes and piece classes.");return false;}CardIds.Add(Card->CardId);}
	if(bRoundShopRules&&!CardPool.ContainsByPredicate([](const TObjectPtr<UEGCardDefinition>& Card){return Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind!=EEGHeroKind::Elysia&&Card->HeroDefinition->HeroId!=FName(TEXT("Elysia"))&&Card->HeroDefinition->HeroId!=FName(TEXT("ElysiaHerrscherOfHuman"))&&Card->HeroDefinition->HeroId!=FName(TEXT("ElysiaCorruption"));}))
	{Error=TEXT("Round shop requires at least one non-Elysia candidate.");return false;}
	if(!ValidateDefinitionGraph(Error,false))return false;
	for (int32 StageIndex=0;StageIndex<Stages.Num();++StageIndex)
	{
		const auto& Stage=Stages[StageIndex];
        const FName StageId=GetStageId(StageIndex);
        if(StageIds.Contains(StageId)){Error=TEXT("Duplicate stage ID.");return false;}
        StageIds.Add(StageId);
		TSet<FIntPoint> Used;
  TSet<FIntPoint> TrapCells;
  for(const auto& Trap:Stage.Traps)
  {
   if(!Trap.Definition||!Trap.Definition->IsValidConfiguration()||!Trap.Definition->Material||!Board->IsCellValid(Trap.Cell)||Board->IsPlayerCell(Trap.Cell)||TrapCells.Contains(Trap.Cell))
   {Error=TEXT("Invalid trap data, duplicate position or trap inside player deployment.");return false;}
   TrapCells.Add(Trap.Cell);
  }
		if (Stage.Enemies.IsEmpty() || Stage.VictoryGold < 0 || Stage.CompletionGold < 0)
		{
			Error = TEXT("Each stage requires enemies and nonnegative victory gold.");
			return false;
		}
		for (const auto& Enemy : Stage.Enemies)
		{
			if (!Enemy.Definition || Enemy.Level < 1 || !Board->IsCellValid(Enemy.Cell) ||
				!Board->IsEnemyCell(Enemy.Cell) || Used.Contains(Enemy.Cell))
			{
				Error = TEXT("Enemy spawn is missing, duplicated, outside the grid, or inside the player/neutral rows.");
				return false;
			}
			Used.Add(Enemy.Cell);
		}
		if(bSevenStageRules && (!FMath::IsFinite(Stage.StageBattleTimeLimit) || Stage.StageBattleTimeLimit<0.f ||
			(StageIndex<6 && (Stage.StageBattleTimeLimit<=0.f || !Stage.Enemies.IsValidIndex(Stage.LowCorruptionRemoveIndex) ||
			Stage.Enemies[Stage.LowCorruptionRemoveIndex].bCorruptedCopy || !Stage.bHasHighCorruptionEnemy))))
		{Error=TEXT("Invalid campaign time limit or ordinary enemy adjustment.");return false;}
		if(bSevenStageRules && Stage.bHasHighCorruptionEnemy)
		{
			const auto& Extra=Stage.HighCorruptionEnemy;
			if(!Extra.Definition||Extra.Level<1||Extra.bCorruptedCopy||!Board->IsEnemyCell(Extra.Cell)||Used.Contains(Extra.Cell))
			{Error=TEXT("Invalid high corruption enemy or reserved position.");return false;}
		}
	}

	return true;
}

FName UEGRunDefinition::GetStageId(int32 Index) const
{return Stages.IsValidIndex(Index)&&!Stages[Index].StageId.IsNone()?Stages[Index].StageId:FName(*FString::Printf(TEXT("Stage_%d"),Index+1));}

TArray<FSoftObjectPath> UEGRunDefinition::GatherRuntimeResources() const
{
 TArray<FSoftObjectPath> Paths;TSet<const UEGHeroDefinition*> Visited;TArray<const UEGHeroDefinition*> Pending;
 if(StartingCard)Pending.Add(StartingCard->HeroDefinition);
 if(bUseExplicitStartingCards)for(const auto& Grant:ExplicitStartingCards)if(Grant.Card)Pending.Add(Grant.Card->HeroDefinition);
 for(const auto& Card:CardPool)if(Card)Pending.Add(Card->HeroDefinition);
 for(const auto& Stage:Stages){for(const auto& E:Stage.Enemies)Pending.Add(E.Definition);if(Stage.bHasHighCorruptionEnemy)Pending.Add(Stage.HighCorruptionEnemy.Definition);}
 auto Add=[&Paths](const FSoftObjectPath& P){if(P.IsValid())Paths.AddUnique(P);};
 while(!Pending.IsEmpty())
 {
  const auto* D=Pending.Pop(EAllowShrinking::No);if(!D||Visited.Contains(D))continue;Visited.Add(D);
  auto AddNative=[&](UEGNativeMeshPassDefinition* Definition)
  {
   if(!Definition)return;
   Add(FSoftObjectPath(Definition));
   for(const auto& Pass:Definition->Passes)if(Pass.Material)Add(FSoftObjectPath(Pass.Material.Get()));
  };
  auto AddSourceFX=[&](const TSoftObjectPtr<UEGSourceEffectDefinition>& Ref)
  {
   Add(Ref.ToSoftObjectPath());
   if(const auto* FX=Ref.LoadSynchronous())
   {
    Add(FX->SourceDistortionComposite.ToSoftObjectPath());
    for(const auto& T:FX->Tracks)
    {
     Add(T.Mesh.ToSoftObjectPath());Add(T.Material.ToSoftObjectPath());Add(T.NativePassDefinition.ToSoftObjectPath());
     AddNative(T.NativePassDefinition.LoadSynchronous());
     if(T.VisibilityCurve)Add(FSoftObjectPath(T.VisibilityCurve.Get()));
     for(const auto& Curve:T.MaterialCurves)if(Curve.Value)Add(FSoftObjectPath(Curve.Value.Get()));
    }
   }
  };
  auto AddAssembly=[&](const TSoftObjectPtr<UEGSourcePreviewDefinition>& Ref)
  {
   Add(Ref.ToSoftObjectPath());
   if(const auto* Assembly=Ref.LoadSynchronous())for(const auto& Motion:Assembly->Motions)
   {
    for(const auto& Part:Motion.Parts)
    {
     if(Part.Mesh)Add(FSoftObjectPath(Part.Mesh.Get()));
     if(Part.Animation)Add(FSoftObjectPath(Part.Animation.Get()));
     if(Part.AnimationBlueprintClass)Add(FSoftObjectPath(Part.AnimationBlueprintClass.Get()));
     AddNative(Part.NativePassDefinition);
    }
    for(const auto& Binding:Motion.Effects)if(Binding.Effect)AddSourceFX(TSoftObjectPtr<UEGSourceEffectDefinition>(Binding.Effect.Get()));
   }
  };
  if(const auto* E=Cast<UEGElysiaSkillDefinition>(D->SkillDefinition))
  {
   AddAssembly(E->ArrowAssembly);AddAssembly(E->ImpactAssembly);AddAssembly(E->ChargeAssembly);AddAssembly(E->BurstAssembly);for(const auto& Seed:E->SeedAssemblies)AddAssembly(Seed);
   for(const auto* Cue:{&E->ArrowCue,&E->ChargedArrowCue,&E->RicochetCue,&E->ImpactCue,&E->ChargeCue,&E->DomeEntryCue,&E->DomeLoopCue,&E->DomeExitCue,&E->DomePetalCue,&E->DomeExitPetalCue,&E->RainCue,&E->RainPetalCue,&E->RainImpactCue,&E->RainAreaCue})Add(Cue->System.ToSoftObjectPath());
   Add(E->DomeBoundaryMaterial.ToSoftObjectPath());Add(E->DomeShellMaterial.ToSoftObjectPath());
  }
  if(const auto* E=Cast<UEGEdenSkillDefinition>(D->SkillDefinition)){AddSourceFX(E->BasicImpact);AddSourceFX(E->NoteEffect);AddSourceFX(E->UltimateImpact);}
  if(const auto* A=Cast<UEGAponiaSkillDefinition>(D->SkillDefinition)){AddSourceFX(A->BasicImpact);AddSourceFX(A->LightningEffect);AddSourceFX(A->VerdictEffect);Add(A->DisciplineNode.ToSoftObjectPath());}
  if(const auto* G=Cast<UEGGriseoSkillDefinition>(D->SkillDefinition))AddSourceFX(G->UltimateField);
  if((D->HeroKind==EEGHeroKind::Eden||D->HeroKind==EEGHeroKind::Aponia||D->HeroKind==EEGHeroKind::Griseo||D->HeroKind==EEGHeroKind::Knight)&&D->PresentationDefinition)
  {
   auto AddMusicalAssembly=[&](const TSoftObjectPtr<UEGSourcePreviewDefinition>& Ref)
   {
    Add(Ref.ToSoftObjectPath());
    if(const auto* Assembly=Ref.LoadSynchronous())for(const auto& Motion:Assembly->Motions)
     for(const auto& Binding:Motion.Effects)if(Binding.Effect)AddSourceFX(TSoftObjectPtr<UEGSourceEffectDefinition>(Binding.Effect.Get()));
   };
   const auto* P=D->PresentationDefinition.Get();
   for(const auto& Ref:P->AttackAssemblies)AddMusicalAssembly(Ref);
   for(const auto& Ref:P->IdleAssemblies)AddMusicalAssembly(Ref);
   AddMusicalAssembly(P->SkillAssembly);AddMusicalAssembly(P->WalkAssembly);AddMusicalAssembly(P->RunAssembly);AddMusicalAssembly(P->HitAssembly);AddMusicalAssembly(P->DeathAssembly);
  }
  Pending.Add(D->EnhancedDefinition);Pending.Add(D->SummonDefinition);
  if(const auto* V=Cast<UEGVillVSkillDefinition>(D->SkillDefinition))
  {
   for(const auto& FX:V->ShotImpacts)AddSourceFX(FX);
   for(const auto& FX:V->EnhancedShotImpacts)AddSourceFX(FX);
   for(const auto& A:V->EnhancedDeviceAttacks)AddAssembly(A);
   for(const auto& Def:{V->GatlingDefinition,V->CannonDefinition}){Add(Def.ToSoftObjectPath());if(auto* Device=Def.LoadSynchronous())Pending.Add(Device);}
   for(const auto& Entry:V->EntryActions){Add(Entry.Animation.ToSoftObjectPath());Add(Entry.Montage.ToSoftObjectPath());}
   for(const auto& Entry:V->JumpActions){Add(Entry.Animation.ToSoftObjectPath());Add(Entry.Montage.ToSoftObjectPath());}
   for(const auto& Body:V->JumpBodies)AddAssembly(Body);
   for(const auto* List:{&V->EntryBodies,&V->SeatedBodies,&V->DeviceIdles,&V->DeviceSpawns,&V->DeviceAttacks})for(const auto& A:*List)AddAssembly(A);
  }
  if(const auto* M=Cast<UEGMobiusSkillDefinition>(D->SkillDefinition))
  {
   for(const auto& Stage:M->BasicSourceStages)for(const auto& Cue:Stage.Cues)AddAssembly(Cue.Assembly);
   for(const auto& A:M->BasicProjectileAssemblies)AddAssembly(A);AddAssembly(M->BasicImpactAssembly);AddAssembly(M->UltimateSourceAssembly);AddAssembly(M->SerpentBody);AddAssembly(M->SerpentRecovery);AddAssembly(M->UltimateRecoveryEffect);AddAssembly(M->StormImpact);Add(M->StormBeamMaterial.ToSoftObjectPath());
  }
  Add(D->GetMesh().ToSoftObjectPath());Add(D->GetAnimationClass().ToSoftObjectPath());Add(D->GetFormArtwork().ToSoftObjectPath());Add(D->CardFrameTexture.ToSoftObjectPath());
  Add(D->GetIdleAnimation().ToSoftObjectPath());Add(D->GetWalkAnimation().ToSoftObjectPath());Add(D->GetRunAnimation().ToSoftObjectPath());
  Add(D->GetAttackAnimation().ToSoftObjectPath());Add(D->GetSkillAnimation().ToSoftObjectPath());Add(D->GetDeathAnimation().ToSoftObjectPath());Add(D->GetHitAnimation().ToSoftObjectPath());
  Add(D->GetAttackMontage().ToSoftObjectPath());Add(D->GetSkillMontage().ToSoftObjectPath());Add(D->GetDeathMontage().ToSoftObjectPath());Add(D->GetHitMontage().ToSoftObjectPath());
  Add(D->GetAttackMaterial().ToSoftObjectPath());Add(D->GetSkillMaterial().ToSoftObjectPath());Add(D->GetProjectileMesh().ToSoftObjectPath());
  if(D->MonsterAttack)Add(D->MonsterAttack->SkillMaterial.ToSoftObjectPath());
  Add(D->GetAttackSound().ToSoftObjectPath());Add(D->GetSkillSound().ToSoftObjectPath());
  if(const auto* P=D->GetCombatAudioPresentation();P&&P->bUseEventAudio){Add(P->AudioAttenuation.ToSoftObjectPath());Add(P->AudioConcurrency.ToSoftObjectPath());for(const auto& Event:P->AudioEvents){Add(Event.ConcurrencyOverride.ToSoftObjectPath());for(const auto& Sound:Event.Sounds)Add(Sound.ToSoftObjectPath());}}
  if(D->bUseSplitConfiguration&&D->PresentationDefinition)
  {
   const auto* P=D->PresentationDefinition.Get();
   Add(P->SettlementIdleAnimation.ToSoftObjectPath());AddAssembly(P->SettlementIdleAssembly);
   for(const auto& Variant:P->SettlementIdleVariants){Add(Variant.Animation.ToSoftObjectPath());AddAssembly(Variant.Assembly);}
   AddAssembly(P->SkillAssembly);AddAssembly(P->SkillTargetAssembly);AddAssembly(P->PreparationIdleAssembly);AddAssembly(P->PreparationEntranceAssembly);
   Add(P->PreparationEntranceVoice.ToSoftObjectPath());
   for(const auto& Assembly:{P->WalkAssembly,P->RunAssembly,P->DeathAssembly,P->HitAssembly})AddAssembly(Assembly);
   for(const auto& Variant:P->DeathVariants){Add(Variant.Animation.ToSoftObjectPath());AddAssembly(Variant.Assembly);}
   for(const auto& Assembly:P->IdleAssemblies)AddAssembly(Assembly);
   for(const auto& Assembly:P->UltimateAssemblies)AddAssembly(Assembly);
   for(const auto& Entry:P->UltimateSequence){Add(Entry.Animation.ToSoftObjectPath());Add(Entry.Montage.ToSoftObjectPath());}
   for(const auto& Assembly:P->AttackAssemblies)AddAssembly(Assembly);
   for(const auto& Entry:P->AttackSequence){Add(Entry.Animation.ToSoftObjectPath());Add(Entry.Montage.ToSoftObjectPath());}
  }
    for(const auto& W:D->GetWeapons()){Add(W.Mesh.ToSoftObjectPath());Add(W.DeformableMesh.ToSoftObjectPath());Add(W.AnimationClass.ToSoftObjectPath());Add(W.NockedArrow.ToSoftObjectPath());}
 }
 Paths.Sort([](const FSoftObjectPath& A,const FSoftObjectPath& B){return A.ToString()<B.ToString();});return Paths;
}

bool UEGRunDefinition::ValidateDefinitionGraph(FString& Error,bool bCheckAnimation) const
{
 TSet<const UEGHeroDefinition*> Complete,Active;
 TFunction<bool(const UEGHeroDefinition*)> Visit=[&](const UEGHeroDefinition* D)
 {
  if(!D||Complete.Contains(D))return true;
  if(Active.Contains(D)){Error=D->GetPathName()+TEXT(": cyclic form or summon definition.");return false;}
  Active.Add(D);if(!D->ValidateDefinition(Error,bCheckAnimation))return false;
  if(!Visit(D->EnhancedDefinition)||!Visit(D->SummonDefinition))return false;
  if(const auto* V=Cast<UEGVillVSkillDefinition>(D->SkillDefinition))
   if(!Visit(bCheckAnimation?V->GatlingDefinition.LoadSynchronous():V->GatlingDefinition.Get())||!Visit(bCheckAnimation?V->CannonDefinition.LoadSynchronous():V->CannonDefinition.Get()))return false;
  Active.Remove(D);Complete.Add(D);return true;
 };
 if(StartingCard&&!Visit(StartingCard->HeroDefinition))return false;
 if(bUseExplicitStartingCards)for(const auto& Grant:ExplicitStartingCards)if(Grant.Card&&!Visit(Grant.Card->HeroDefinition))return false;
 for(const auto& Card:CardPool)if(Card&&!Visit(Card->HeroDefinition))return false;
 for(const auto& S:Stages){for(const auto& E:S.Enemies)if(!Visit(E.Definition))return false;if(S.bHasHighCorruptionEnemy&&!Visit(S.HighCorruptionEnemy.Definition))return false;}
 return true;
}
bool UEGRunDefinition::IsOptionalResource(const FSoftObjectPath& Path) const
{
 TSet<const UEGHeroDefinition*> Seen;TArray<const UEGHeroDefinition*> Pending;
 if(StartingCard)Pending.Add(StartingCard->HeroDefinition);
 if(bUseExplicitStartingCards)for(const auto& Grant:ExplicitStartingCards)if(Grant.Card)Pending.Add(Grant.Card->HeroDefinition);
 for(const auto& C:CardPool)if(C)Pending.Add(C->HeroDefinition);
 for(const auto& S:Stages)for(const auto& E:S.Enemies)Pending.Add(E.Definition);
 while(!Pending.IsEmpty())
 {const auto* D=Pending.Pop(EAllowShrinking::No);if(!D||Seen.Contains(D))continue;Seen.Add(D);Pending.Add(D->EnhancedDefinition);Pending.Add(D->SummonDefinition);
  if(D->GetAttackSound().ToSoftObjectPath()==Path||D->GetSkillSound().ToSoftObjectPath()==Path)return true;}
 return false;
}

