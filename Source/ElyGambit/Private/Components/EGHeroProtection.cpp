#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Character/EGChessUnit.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Effects/EGProtectionEffect.h"
#include "Presentation/EGSourceEffect.h"
#include "EGGameplayTags.h"

bool UEGHeroSkillComponent::HasPrismaticBubble() const
{
 return Unit.IsValid()&&IEGCombatant::Execute_IsAlive(Unit.Get())&&Battle.IsValid()&&Battle->IsBattleActive()&&GetWorld()&&BubbleEnd>GetWorld()->GetTimeSeconds();
}
void UEGHeroSkillComponent::UpdateBubblePresentation()
{
 if(!HasPrismaticBubble()&&(BubbleEnd>0||BubbleVisual.IsValid()))
 {BubbleEnd=0;if(BubbleVisual.IsValid())BubbleVisual->Destroy();BubbleVisual.Reset();OnStatusChanged.Broadcast();}
}
void UEGHeroSkillComponent::GrantPrismaticBubble(float Duration,float Reduction,float BreakDuration,UEGSourceEffectDefinition* Effect,UEGSourceEffectDefinition* Burst)
{
 if(!Unit.IsValid()||!Unit->HasAuthority()||!Battle.IsValid()||!Battle->IsBattleActive()||!IEGCombatant::Execute_IsAlive(Unit.Get())
    ||!FMath::IsFinite(Duration)||Duration<=0||!FMath::IsFinite(Reduction)||!FMath::IsFinite(BreakDuration))return;
 BubbleEnd=GetWorld()->GetTimeSeconds()+Duration;BubbleBreakReduction=FMath::Clamp(Reduction,0.f,1.f);BubbleBreakDuration=FMath::Max(0.f,BreakDuration);
 BubbleBurstDefinition=Burst;
 if(!BubbleProtectionToken.IsValid())BubbleProtectionToken=FGuid::NewGuid();
 if(BubbleVisual.IsValid())BubbleVisual->Destroy();
 BubbleVisual=Effect?AEGSourceEffect::Spawn(this,Unit->GetVisiblePresentationMesh(),Effect,1.f,true):nullptr;
 OnStatusChanged.Broadcast();
}
void UEGHeroSkillComponent::ApplyProtection(FGuid Token,AEGChessUnit* Source,float Reduction,float Duration)
{
 if(!Token.IsValid()||!Unit.IsValid()||!Unit->HasAuthority()||!IsValid(Source)||!Source->HasAuthority()||Source->GetWorld()!=GetWorld()
    ||!Battle.IsValid()||!Battle->IsBattleActive()||!IEGCombatant::Execute_IsAlive(Unit.Get())||!FMath::IsFinite(Reduction)||Reduction<=0||Reduction>1||!FMath::IsFinite(Duration)||Duration<=0)return;
 auto* ASC=Unit->GetAbilitySystemComponent();auto Context=ASC->MakeEffectContext();Context.AddSourceObject(Source);
 auto Spec=ASC->MakeOutgoingSpec(UEGProtectionEffect::StaticClass(),1,Context);if(!Spec.IsValid())return;
 Spec.Data->SetDuration(Duration,true);Spec.Data->SetSetByCallerMagnitude(EGTags::ProtectionReduction,Reduction);
 RemoveProtection(Token);
 const auto Handle=ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data);
 if(Handle.IsValid())ProtectionEffects.Add(Token,Handle);
 OnStatusChanged.Broadcast();
}
void UEGHeroSkillComponent::RemoveProtection(FGuid Token)
{
 if(const auto* Handle=ProtectionEffects.Find(Token))
 {
  const auto Copy=*Handle;ProtectionEffects.Remove(Token);
  if(Unit.IsValid())Unit->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(Copy);
  OnStatusChanged.Broadcast();
 }
}
bool UEGHeroSkillComponent::HasProtection(FGuid Token) const
{
 const auto* Handle=ProtectionEffects.Find(Token);
 if(!Handle||!Unit.IsValid()||!GetWorld())return false;
 const auto* Active=Unit->GetAbilitySystemComponent()->GetActiveGameplayEffect(*Handle);
 return Active&&Active->GetTimeRemaining(GetWorld()->GetTimeSeconds())>0;
}
float UEGHeroSkillComponent::GetProtectionReduction() const
{
 if(!Unit.IsValid()||!Battle.IsValid()||!Battle->IsBattleActive())return 0;
 float Value=0;const auto* ASC=Unit->GetAbilitySystemComponent();
 for(const auto& Pair:ProtectionEffects)if(const auto* Active=ASC->GetActiveGameplayEffect(Pair.Value))
  if(Active->GetTimeRemaining(GetWorld()->GetTimeSeconds())>0)
   Value=FMath::Max(Value,Active->Spec.GetSetByCallerMagnitude(EGTags::ProtectionReduction,false,0));
 return FMath::Clamp(Value,0.f,1.f);
}
float UEGHeroSkillComponent::ResolveProtection(float Amount,FEGCombatResolution& Receipt)
{
 if(!FMath::IsFinite(Amount)||Amount<=0)return 0;
 if(Battle.IsValid())Battle->RefreshProtectionFields();
 if(HasPrismaticBubble())
 {
  // Consume before callbacks/GE application so reentrant and same-frame hits cannot reuse this charge.
  BubbleEnd=0;Receipt.BubbleAbsorbed=Amount;
  UpdateBubblePresentation();
  if(BubbleBurstVisual.IsValid())BubbleBurstVisual->Destroy();
  if(BubbleBurstDefinition)BubbleBurstVisual=AEGSourceEffect::Spawn(this,Unit->GetVisiblePresentationMesh(),BubbleBurstDefinition,1.f,true);
  ApplyProtection(BubbleProtectionToken,Unit.Get(),BubbleBreakReduction,BubbleBreakDuration);
  OnStatusChanged.Broadcast();return 0;
 }
 Receipt.ProtectionPrevented=Amount*GetProtectionReduction();return Amount-Receipt.ProtectionPrevented;
}
void UEGHeroSkillComponent::ClearProtection()
{
 BubbleEnd=0;BubbleProtectionToken.Invalidate();
 if(BubbleVisual.IsValid())BubbleVisual->Destroy();BubbleVisual.Reset();
 if(BubbleBurstVisual.IsValid())BubbleBurstVisual->Destroy();BubbleBurstVisual.Reset();BubbleBurstDefinition=nullptr;
 const auto Previous=MoveTemp(ProtectionEffects);ProtectionEffects.Reset();
 if(Unit.IsValid())for(const auto& Pair:Previous)Unit->GetAbilitySystemComponent()->RemoveActiveGameplayEffect(Pair.Value);
 OnStatusChanged.Broadcast();
}
