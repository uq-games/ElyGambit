#include "Skills/EGGriseoSkillBehaviors.h"
#include "Skills/EGSupportSkillUtils.h"
#include "AbilitySystem/Effects/EGSupportEffects.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGChessNiagaraBurst.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Character/EGChessUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/CapsuleComponent.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/App.h"
#include "Engine/World.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "HAL/PlatformTime.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_EDITOR
#include "TextureCompiler.h"
#endif

namespace
{
struct FGriseoWarmMaterial
{
 TArray<TWeakObjectPtr<UTexture2D>> Textures;
 double ResidentUntil=0;
};
// Weak world/material/texture references ensure this optimization never keeps a
// level or a source asset alive. Expired worlds and materials are pruned when a
// new Griseo instance is prepared.
TMap<TWeakObjectPtr<UWorld>,TMap<TWeakObjectPtr<UMaterialInterface>,FGriseoWarmMaterial>> GriseoWarmMaterials;
void PrepareGriseoMaterials(AEGChessUnit* Unit)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EG_GriseoMaterialPrewarm);
 if(!IsValid(Unit)||!Unit->GetWorld()||!FApp::CanEverRender())return;
 const auto* Definition=Unit->GetDefinition();
 const auto* Data=Definition?Cast<UEGGriseoSkillDefinition>(Definition->SkillDefinition):nullptr;
 const auto* Presentation=Definition?Definition->PresentationDefinition.Get():nullptr;
 if(!Data||!Presentation||Data->PrewarmMaterialSlots.IsEmpty())return;
 for(auto It=GriseoWarmMaterials.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
 auto& WorldCache=GriseoWarmMaterials.FindOrAdd(Unit->GetWorld());
 for(auto It=WorldCache.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
 TSet<TWeakObjectPtr<UMaterialInterface>> Prepared;
 const auto PrepareMaterial=[&Prepared,&WorldCache](UMaterialInterface* Material)
 {
  if(!IsValid(Material)||Prepared.Contains(Material))return;
  TStrongObjectPtr<UMaterialInterface> Guard(Material);
  Prepared.Add(Material);
  const double Now=FPlatformTime::Seconds();
  const TWeakObjectPtr<UMaterialInterface> Key(Material);
  bool bCacheValid=!Material->IsCompiling();
#if WITH_EDITOR
  // Authoring can change the texture graph between two PIE spawns. Do not
  // assume a material's editor-time dependency list is immutable.
  TArray<UTexture*> CurrentTextures;Material->GetUsedTextures(CurrentTextures);
  if(const auto* Existing=WorldCache.Find(Key))
  {
   int32 Current2D=0;
   for(auto* Texture:CurrentTextures)if(auto* Texture2D=Cast<UTexture2D>(Texture))
   {++Current2D;if(Texture2D->IsCompiling()||!Existing->Textures.Contains(Texture2D))bCacheValid=false;}
   bCacheValid&=Current2D==Existing->Textures.Num();
  }
#endif
  if(auto* Warm=WorldCache.Find(Key);Warm&&Warm->ResidentUntil>Now
     &&bCacheValid&&!Warm->Textures.ContainsByPredicate([](const auto& Texture){return !Texture.IsValid();}))
  {
   // Refresh the six-second residency promise for this new instance without
   // re-blocking on textures already known resident from the previous instance.
   for(const auto& Texture:Warm->Textures)Texture->SetForceMipLevelsToBeResident(6.f);
   Warm->ResidentUntil=Now+6.;return;
  }
  Material->EnsureIsComplete();
  TArray<UTexture*> Textures;Material->GetUsedTextures(Textures);
#if WITH_EDITOR
  FTextureCompilingManager::Get().FinishCompilation(Textures);
#endif
  FGriseoWarmMaterial Warm;
  for(auto* Texture:Textures)if(auto* Texture2D=Cast<UTexture2D>(Texture))
  {Texture2D->SetForceMipLevelsToBeResident(6.f);Texture2D->WaitForPendingInitOrStreaming();Warm.Textures.Add(Texture2D);}
  Warm.ResidentUntil=FPlatformTime::Seconds()+6.;WorldCache.Add(Key,MoveTemp(Warm));
 };
 const auto PrepareEffect=[&PrepareMaterial](UEGSourceEffectDefinition* Definition)
 {
  TStrongObjectPtr<UEGSourceEffectDefinition> Effect(Definition);
  if(!Effect.IsValid())return;
  for(const auto& Track:Effect->Tracks)PrepareMaterial(Track.Material.LoadSynchronous());
  PrepareMaterial(Effect->SourceDistortionComposite.LoadSynchronous());
 };
 const auto PrepareAssembly=[&](const TSoftObjectPtr<UEGSourcePreviewDefinition>& Asset)
 {
  TStrongObjectPtr<UEGSourcePreviewDefinition> Assembly(Asset.LoadSynchronous());
  if(!Assembly.IsValid())return;
  for(const auto& Motion:Assembly->Motions)
  {
   for(const auto& Part:Motion.Parts)if(Part.PartId==TEXT("Body")&&Part.Mesh)
    for(const int32 Slot:Data->PrewarmMaterialSlots)if(Part.Mesh->GetMaterials().IsValidIndex(Slot))
     PrepareMaterial(Part.Mesh->GetMaterials()[Slot].MaterialInterface.Get());
   for(const auto& Effect:Motion.Effects)PrepareEffect(Effect.Effect.Get());
  }
 };
 for(const auto& Asset:Presentation->AttackAssemblies)PrepareAssembly(Asset);
 PrepareAssembly(Presentation->SkillAssembly);
 PrepareEffect(Data->BubbleEffect.LoadSynchronous());PrepareEffect(Data->BubbleBurstEffect.LoadSynchronous());PrepareEffect(Data->UltimateField.LoadSynchronous());
}
}

void UEGGriseoSkillBehavior::Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle)
{
 Super::Initialize(InSkill,InBattle);BubbleHits=0;PrepareGriseoMaterials(Unit.Get());
 if(IsCurrentBattle()&&Unit->HasAuthority())
  if(const auto* D=Cast<UEGGriseoSkillDefinition>(Unit->GetDefinition()->SkillDefinition))
   Battle->SpawnKnight(Unit.Get(),D->KnightHealthRatio,D->KnightAttackRatio);
}
void UEGGriseoSkillBehavior::GrantBubble() {}
void UEGGriseoSkillBehavior::TickBattle(float DeltaSeconds) {}
void UEGGriseoSkillBehavior::OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId) {}
bool UEGGriseoSkillBehavior::HasField() const {return false;}
void UEGGriseoSkillBehavior::ClearField()
{if(FieldVisual.IsValid())FieldVisual->Destroy();FieldVisual.Reset();FieldRecipients.Reset();FieldToken.Invalidate();FieldEnd=0;}
void UEGGriseoSkillBehavior::RefreshProtectionField() {}
void UEGGriseoSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId)return;
 if(!C.bUltimate)
 {
  auto* Target=C.Target.Get();
  if(Event==TEXT("Hit")&&Unit->GetCombat()->IsValidEnemy(Target)&&Unit->GetCombat()->IsInAttackRange(Target))
   if(Battle->Damage(Unit.Get(),Target,C.Attack,C.ActionId,true))
   {
    if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(Event,C.ActionId,Target);
    if(IsCurrentBattle()){AEGChessNiagaraBurst::Swing(Skill.Get(),C.BasicAttackIndex);
     AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("VisibleHit"),Target->GetActorLocation());
     Battle->BasicHit(Unit.Get(),Target,C.ActionId);}
   }
  return;
 }
 if(Event==TEXT("Release"))if(const auto* D=Cast<UEGGriseoSkillDefinition>(Unit->GetDefinition()->SkillDefinition))
 {
  if(!Skill->CommitStigmataUltimate(C.ActionId))return;
  auto* U=Unit.Get();if(!U->HasAuthority())return;
  AEGChessUnit* Knight=nullptr;
  for(auto* Ally:Battle->Units())if(EGSupport::Ally(U,Ally)&&Ally->GetHeroSkill()->bSummon&&Ally->GetHeroSkill()->SummonerId==U->GetInstanceId()&&Ally->GetDefinition()->HeroKind==EEGHeroKind::Knight){Knight=Ally;break;}
  const float RadiusSq=FMath::Square(EGSupport::Radius(U,D->SupportRadiusCells));
  const float Shield=C.MaxHealth*(C.Tier>=1?D->ShieldRatioAdvanced:D->ShieldRatio)*U->GetAttributes()->GetSkillMultiplier();
  if(Knight)AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("PaintBloom"),Knight->GetActorLocation()-FVector(0,0,70));
  if(C.Tier>=2)AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("PaintBloom"),U->GetActorLocation()-FVector(0,0,70));
  for(auto* Ally:Battle->Units())
  {
   if(!EGSupport::Ally(U,Ally))continue;
   if(Knight&&FVector::DistSquared2D(Knight->GetActorLocation(),Ally->GetActorLocation())<=RadiusSq)
   {
    Ally->GetHeroSkill()->GrantSourceShield(TEXT("Griseo.DreamShield"),Shield,D->SupportSeconds,U,true);
    AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("SupportShield"),Ally->GetActorLocation()-FVector(0,0,65));
   }
   if(C.Tier>=2&&FVector::DistSquared2D(U->GetActorLocation(),Ally->GetActorLocation())<=RadiusSq)
   {
    auto* ASC=Ally->GetAbilitySystemComponent();auto Context=ASC->MakeEffectContext();Context.AddInstigator(U,U);
    auto Spec=ASC->MakeOutgoingSpec(UEGPaintAttackEffect::StaticClass(),1,Context);
    if(Spec.IsValid())
    {Spec.Data->SetDuration(D->SupportSeconds,true);Spec.Data->SetSetByCallerMagnitude(FName(TEXT("Support.Multiplier")),1.f+D->AttackBonus);ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);}
    AEGChessNiagaraBurst::Play(Skill.Get(),TEXT("SupportAttack"),Ally->GetActorLocation()-FVector(0,0,65));
   }
  }
 }
}
void UEGGriseoSkillBehavior::EndBattle()
{ClearField();BubbleHits=0;Super::EndBattle();}
void UEGKnightSkillBehavior::ActionEvent(FName Event,const FEGSkillActionContext& C)
{
 if(!IsCurrentBattle()||C.RoundId!=RoundId||C.bUltimate||Event!=TEXT("Hit"))return;
 auto* Target=C.Target.Get();
 if(!Unit->GetCombat()->IsValidEnemy(Target)||!Unit->GetCombat()->IsInAttackRange(Target))return;
 if(Battle->Damage(Unit.Get(),Target,C.Attack,C.ActionId,true))
 {
  if(auto* Audio=Unit->FindComponentByClass<UEGCombatAudioComponent>())Audio->ConfirmHit(Event,C.ActionId,Target);
  // Summon hits must never enter the hero basic-hit observer/reward channel.
 }
}
void UEGKnightSkillBehavior::TickBattle(float DeltaSeconds)
{
 if(!IsCurrentBattle()||!Unit->HasAuthority()||bGuardUsed)return;
 const auto& Rules=Unit->GetDefinition()->GetSkillRules();
 bool bFound=false;
 for(auto* Enemy:Unit->GetCombat()->FindEnemiesInRange(Unit->GetActorLocation(),Rules.AreaRadius))
 {
  // One short contact guard, using the existing taunt. Never replace a live
  // forced target (another Knight or a boss mechanic), or restart an attack.
  if(Enemy->GetHeroSkill()->IsTauntActive())continue;
  Enemy->GetHeroSkill()->SetTaunt(Unit.Get(),Rules.TauntDuration);bFound=true;
 }
 bGuardUsed=bFound;
}
