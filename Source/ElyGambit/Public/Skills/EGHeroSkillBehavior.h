#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Types/EGChessTypes.h"
#include "EGHeroSkillBehavior.generated.h"

class AEGChessUnit;
class UEGHeroSkillComponent;
class UEGBattleContextComponent;
class UNiagaraSystem;

/** Immutable inputs captured by the shared action transaction. */
USTRUCT()
struct ELYGAMBIT_API FEGSkillActionContext
{
 GENERATED_BODY()
 FGuid RoundId, ActionId;
 TWeakObjectPtr<AEGChessUnit> Target;
 bool bUltimate=false;
 int32 Tier=0;
 int32 BasicAttackIndex=INDEX_NONE;
 float Attack=0, MaxHealth=0, Multiplier=0, Secondary=0;
};

/** Per-unit execution policy. The owning skill component supplies lifetime and action guards. */
UCLASS(Abstract)
class ELYGAMBIT_API UEGHeroSkillBehavior : public UObject
{
 GENERATED_BODY()
public:
 virtual void Initialize(UEGHeroSkillComponent* InSkill, UEGBattleContextComponent* InBattle);
 /** Called after the component resets all per-battle status containers. */
 virtual void OnBattleConfigured() {}
 virtual void ActionEvent(FName Event, const FEGSkillActionContext& Context) {}
 virtual void ActionStarted(const FEGSkillActionContext& Context) {}
 virtual void ActionEnded(bool bCompleted) {}
 virtual void TickAction(float Position) {}
 virtual void TickBattle(float DeltaSeconds) {}
 /** Called immediately before damage as well as by the field owner's tick. No damage may be submitted here. */
 virtual void RefreshProtectionField() {}
 virtual bool BlocksCombat() const {return false;}
 virtual bool AllowsAction(bool bUltimate) const {return true;}
 /** Combat status hooks: direct positive mana writes and hostile control consult the owning behavior. */
 virtual bool BlocksManaGain() const {return false;}
 virtual bool IsControlImmune() const {return false;}
 virtual bool OverridesBodyPresentation() const {return false;}
 virtual void BeforeActionPresentation() {}
 virtual int32 PresentationIndex(bool bUltimate, int32 DefaultIndex) const {return DefaultIndex;}
 virtual int32 IdlePresentationIndex() const {return 0;}
 /** Visible form at the instant death commits, before action cancellation. */
 virtual int32 DeathPresentationIndex() const {return 0;}
 virtual float BasicCycleScale() const {return 1.f;}
 virtual bool EvadeDirectAttack(AEGChessUnit* Source,FGuid ActionId,bool bBasicAttack,bool& bNewlyEvaded) {return false;}
 /** Called only after the first real evade and any selected signet benefit are committed. */
 virtual void OnEvadeCommitted(AEGChessUnit* Source,FGuid ActionId) {}
 virtual bool TryPreventDeath() {return false;}
 virtual void OwnerBasicHit(AEGChessUnit* Target, FGuid ActionId) {}
 /** Observes an already deduplicated basic hit; derived damage must never call BasicHit again. */
 virtual void AllyBasicHit(AEGChessUnit* Source,AEGChessUnit* Target,FGuid ActionId) {}
 virtual void DamageResolved(const FEGCombatResolution& Receipt) {}
 virtual void EndBattle();
protected:
 /** Resolve legacy soft Niagara fields once at battle entry, retaining them for GC. */
 UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraSystem>> ResidentNiagara;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> Skill;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Unit;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 FGuid RoundId;
 bool IsCurrentBattle() const;
};
