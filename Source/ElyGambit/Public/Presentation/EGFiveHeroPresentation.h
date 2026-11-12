#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "EGFiveHeroPresentation.generated.h"

class AEGChessUnit;
class AEGSourceEffect;
class UEGBattleContextComponent;
class UEGHeroSkillComponent;
class UEGSourceEffectDefinition;
class UNiagaraComponent;
class UNiagaraSystem;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USceneComponent;
class USkeletalMeshComponent;
class USkeletalMesh;

/** Gameplay events only describe facts. None of these cues may settle combat. */
enum class EEGFiveHeroCue : uint8
{
 SakuraEvade,
 SakuraDelayedMark,
 SakuraDelayedRelease,
 SakuraFanMark,
 SakuraFanRelease,
 SakuraFanCancel,
 AponiaStack,
 AponiaJudgmentStart,
 AponiaPenalty,
 AponiaVerdict,
 AponiaCancel
};

/** Use one backend per cue. Blank cues deliberately render nothing. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGSakuraReworkCue
{
 GENERATED_BODY()

 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation")
 TSoftObjectPtr<UEGSourceEffectDefinition> SourceEffect;

 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation")
 TSoftObjectPtr<UNiagaraSystem> NiagaraSystem;

 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation", meta=(ClampMin="0.01"))
 float Scale=1.f;

 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation")
 FVector LocalOffset=FVector::ZeroVector;

 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation")
 FRotator LocalRotation=FRotator::ZeroRotator;

 /** Safety cap only. Gameplay owns the actual mark/release timing. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation", meta=(ClampMin="0.01"))
 float LifetimeSeconds=1.f;
};

UCLASS(BlueprintType)
class ELYGAMBIT_API UEGSakuraReworkPresentationConfig : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation") FEGSakuraReworkCue Evade;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation") FEGSakuraReworkCue DelayedMark;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation") FEGSakuraReworkCue DelayedRelease;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation") FEGSakuraReworkCue FanMark;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation") FEGSakuraReworkCue FanRelease;
 /** Optional private translucent skeletal-compatible afterimage material with Opacity scalar. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation") TSoftObjectPtr<UMaterialInterface> AfterimageMaterial;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation", meta=(ClampMin="0.15",ClampMax="0.25")) float DodgeDuration=.2f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation", meta=(ClampMin="0",ClampMax="35")) float DodgeDistance=24.f;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sakura|Presentation", meta=(ClampMin="0",ClampMax="1")) float AfterimageOpacity=.4f;
};

USTRUCT()
struct FEGSakuraDodgeMovedPart
{
 GENERATED_BODY()
 UPROPERTY(Transient) TWeakObjectPtr<USceneComponent> Component;
 FTransform Base=FTransform::Identity;
 FTransform Applied=FTransform::Identity;
 bool bApplied=false;
};

USTRUCT()
struct FEGSakuraReworkActiveCue
{
 GENERATED_BODY()
 EEGFiveHeroCue Cue=EEGFiveHeroCue::SakuraEvade;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> Niagara;
 UPROPERTY(Transient) TWeakObjectPtr<AEGSourceEffect> Source;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Target;
 FGuid ActionId;
 FTransform Transform=FTransform::Identity;
 FVector Offset=FVector::ZeroVector;
 FRotator Rotation=FRotator::ZeroRotator;
 float Age=0.f;
 float Lifetime=0.f;
 bool bMark=false;
 bool bFollowTarget=false;
};

/** Cosmetic-only receiver. One receiver per caster/round; it does not drive actions. */
UCLASS(NotBlueprintable, Transient)
class ELYGAMBIT_API AEGFiveHeroPresentation : public AActor
{
 GENERATED_BODY()
public:
 AEGFiveHeroPresentation();
 static bool Emit(EEGFiveHeroCue Cue, AEGChessUnit* Caster, AEGChessUnit* Target,
  const FVector& Point, const FGuid& RoundId, const FGuid& ActionId, int32 Value=0);
 int32 GetActiveCueCount() const {return ActiveCues.Num();}
 int32 GetActiveCueCount(EEGFiveHeroCue Cue) const;
 int32 GetActiveMarkCount() const;
 bool IsDodgeActive() const {return DodgeDuration>0.f;}
 int32 GetDodgeGhostPartCount() const {return DodgeGhostParts.Num();}
 virtual void Tick(float DeltaSeconds) override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Unit;
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> Skill;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 UPROPERTY(Transient) TObjectPtr<UEGSakuraReworkPresentationConfig> Config;
 UPROPERTY(Transient) TArray<FEGSakuraReworkActiveCue> ActiveCues;
 UPROPERTY(Transient) TArray<FEGSakuraDodgeMovedPart> DodgeParts;
 UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> DodgeGhostParts;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DodgeGhostMaterials;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> DodgeBody;
 UPROPERTY(Transient) TWeakObjectPtr<USkeletalMesh> DodgeBodyAsset;
 float DodgeAge=0.f,DodgeDuration=0.f,DodgeDistance=0.f,DodgeOpacity=0.f;
 FVector DodgeDirection=FVector::ZeroVector;
 FGuid Round;
 bool Receive(EEGFiveHeroCue Cue, AEGChessUnit* Target, const FVector& Point, const FGuid& ActionId);
 bool Play(EEGFiveHeroCue CueId, const FEGSakuraReworkCue& Cue, AEGChessUnit* Target, const FVector& Point,
  const FGuid& ActionId, bool bMark);
 void RemoveMarks(const FGuid& ActionId, AEGChessUnit* Target);
 void RemoveCue(int32 Index);
 void BeginDodge(AEGChessUnit* Attacker);
 void ClearDodge();
 void RestoreDodgeOffsets();
};
