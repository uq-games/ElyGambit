#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/EGChessFXCue.h"
#include "EGChessNiagaraBurst.generated.h"
class UNiagaraComponent;
class UNiagaraSystem;
class UEGHeroSkillComponent;
class UEGBattleContextComponent;

/** Short cosmetic effect owned by a battle round, never by damage resolution. */
UCLASS(NotBlueprintable)
class ELYGAMBIT_API AEGChessNiagaraBurst : public AActor
{
 GENERATED_BODY()
public:
 AEGChessNiagaraBurst();
 static void Spawn(UEGHeroSkillComponent* Skill, UEGBattleContextComponent* Battle, UNiagaraSystem* System, const FVector& Location);
 /** Optional confirmed-hit tail survives a stopped round for at most 0.5s; never an action-bound cue. */
 static AEGChessNiagaraBurst* SpawnCue(UEGHeroSkillComponent* Skill,UEGBattleContextComponent* Battle,const FEGChessFXCue& Cue,const FVector& Location,const FRotator& Rotation=FRotator::ZeroRotator,bool bAllowRoundEndTail=false);
 static AEGChessNiagaraBurst* Play(UEGHeroSkillComponent* Skill,FName Cue,const FVector& Location,const FRotator& Rotation=FRotator::ZeroRotator);
 static void Swing(UEGHeroSkillComponent* Skill,int32 Index);
 /** Editor-only inspection; refuses public library systems. Never modifies an emitter. */
 UFUNCTION(BlueprintCallable,Category="Chess FX|Authoring") static FString AuditPrivateSystem(UNiagaraSystem* System);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UFUNCTION() void Finished(UNiagaraComponent* Component);
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Effect;
 TWeakObjectPtr<UEGHeroSkillComponent> SkillOwner;
 TWeakObjectPtr<UEGBattleContextComponent> BattleOwner;
 FGuid Round,Action;
 float Age=0.f,Duration=3.f;
 bool bActionBound=false,bActionClock=false,bAllowRoundEndTail=false;
};
