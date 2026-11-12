#pragma once
#include "CoreMinimal.h"
#include "GameplayEffectTypes.h"
#include "Skills/EGHeroSkillBehavior.h"
#include "EGConfirmedSkillBehaviors.generated.h"

class AEGSkillLinkVisual;

UCLASS()
class ELYGAMBIT_API UEGPardoSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
 virtual void OwnerBasicHit(AEGChessUnit* Target,FGuid ActionId) override;
 virtual void TickBattle(float DeltaSeconds) override;
 virtual void EndBattle() override;
 int32 GetCoinProgress() const {return CoinProgress;}
 bool HasAwardedCoin() const {return bCoinAwarded;}
private:
 FActiveGameplayEffectHandle CompanionSpeed;
 int32 CoinProgress=0;
 bool bCoinAwarded=false;
};

/** Can keeps its existing melee contact and owns no card reward or ultimate. */
UCLASS()
class ELYGAMBIT_API UEGCanSkillBehavior : public UEGHeroSkillBehavior
{
 GENERATED_BODY()
public:
 virtual void ActionEvent(FName Event,const FEGSkillActionContext& Context) override;
};
