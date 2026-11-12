#pragma once
#include "Skills/EGHeroSkillBehavior.h"
#include "EGGriseoSkillBehaviors.generated.h"
class AEGSourceEffect;

UCLASS()
class ELYGAMBIT_API UEGGriseoSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void Initialize(UEGHeroSkillComponent* InSkill,UEGBattleContextComponent* InBattle) override;
 virtual void TickBattle(float DeltaSeconds) override;
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId) override;
 virtual void RefreshProtectionField() override;
 virtual void EndBattle() override;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Griseo") int32 BubbleHits=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Griseo") FVector FieldCenter=FVector::ZeroVector;
 bool HasField() const;
private:
 void GrantBubble();
 void ClearField();
 double FieldEnd=0;
 FGuid FieldToken;
 UPROPERTY(Transient) TSet<TWeakObjectPtr<AEGChessUnit>> FieldRecipients;
 UPROPERTY(Transient) TWeakObjectPtr<AEGSourceEffect> FieldVisual;
};

/** The knight is a summon: native sword poses, regular GAS damage, no hero rewards or mana. */
UCLASS()
class ELYGAMBIT_API UEGKnightSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void TickBattle(float DeltaSeconds) override;
private:
 bool bGuardUsed=false;
};
