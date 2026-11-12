#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGMonsterSkillVisual.generated.h"

class AEGChessUnit;
class UAudioComponent;
class UEGBattleContextComponent;
class UEGMonsterSkillBehavior;
class UMaterialInstanceDynamic;
class UStaticMeshComponent;
class USoundBase;
enum class EEGMonsterEffectCue : uint8;

/** Observes committed monster effects; it owns no gameplay state. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGMonsterSkillVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGMonsterSkillVisual();
 static AEGMonsterSkillVisual* Attach(UEGMonsterSkillBehavior* Behavior,AEGChessUnit* Source,UEGBattleContextComponent* Battle,FGuid Round);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 void HandleEffect(EEGMonsterEffectCue Cue,AEGChessUnit* EventSource,AEGChessUnit* Target,FGuid EventRound,FGuid Action,float Seconds);
 UPROPERTY() TObjectPtr<USoundBase> ShieldSound;
 UPROPERTY() TObjectPtr<USoundBase> HealSound;
 UPROPERTY() TObjectPtr<USoundBase> SpeedSound;
 TWeakObjectPtr<UEGMonsterSkillBehavior> Behavior;
 TWeakObjectPtr<AEGChessUnit> Source;
 TWeakObjectPtr<UEGBattleContextComponent> Battle;
 TWeakObjectPtr<UAudioComponent> LastAudio;
 FDelegateHandle EffectDelegate;
 FGuid Round,LastSoundAction;
};

/** Small, short-lived target marker. It never represents an effect radius. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGMonsterEffectCueVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGMonsterEffectCueVisual();
 static AEGMonsterEffectCueVisual* Spawn(AEGChessUnit* Source,AEGChessUnit* Target,UEGBattleContextComponent* Battle,FGuid Round,EEGMonsterEffectCue Cue);
 virtual void Tick(float DeltaSeconds) override;
private:
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Ring;
 UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> Glyph;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> RingMaterial;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> GlyphMaterials;
 TWeakObjectPtr<AEGChessUnit> Source;
 TWeakObjectPtr<AEGChessUnit> Target;
 TWeakObjectPtr<UEGBattleContextComponent> Battle;
 FGuid Round;
 FLinearColor Color=FLinearColor::White;
 float Age=0.f;
};
