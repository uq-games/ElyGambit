#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Data/EGFlameChaserSkillDefinition.h"
#include "EGMobiusSourceAttackVisual.generated.h"
class UEGHeroSkillComponent;
class AEGChessUnit;
class UEGSourcePreviewDefinition;
class UEGSourceEffectDefinition;
class AEGCombatVisual;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** Action-owned source cues at their formation points; confirmed, completed
    attacks release finite round-bound source tails. Never deals damage. */
UCLASS()
class ELYGAMBIT_API AEGMobiusSourceAttackVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGMobiusSourceAttackVisual();
 static AEGMobiusSourceAttackVisual* SpawnBasic(UEGHeroSkillComponent* Skill,AEGChessUnit* Target,const FEGMobiusBasicSourceStage& Stage,int32 StageIndex,const TArray<UEGSourceEffectDefinition*>* WorkshopOverrides=nullptr);
 static AEGMobiusSourceAttackVisual* SpawnImpact(UEGHeroSkillComponent* Skill,AEGChessUnit* Target,UEGSourcePreviewDefinition* Definition,float Scale,float PlaybackRate=2.f,bool bGround=false);
 void ConfirmHit() {bHitConfirmed=true;}
 bool ReleaseNaturalTail();
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 void AddStormBeam(UMaterialInterface* Material,const FVector& Origin);
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") TObjectPtr<UEGSourcePreviewDefinition> SourceDefinition;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") float FlightFraction=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") int32 BasicStageIndex=INDEX_NONE;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") int32 ActiveCueCount=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") float SourceSampleTime=0;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") FVector StartPoint=FVector::ZeroVector;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="Source FX") FVector EndPoint=FVector::ZeroVector;
private:
 UPROPERTY(Transient) TWeakObjectPtr<UEGHeroSkillComponent> Skill;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Target;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> SourceVisual;
 UPROPERTY(Transient) TArray<TObjectPtr<AEGCombatVisual>> CueVisuals;
 UPROPERTY(Transient) TArray<TObjectPtr<UEGSourcePreviewDefinition>> PrivateCueDefinitions;
 UPROPERTY(Transient) FEGMobiusBasicSourceStage BasicStage;
 UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> StormBeam;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> StormBeamMaterial;
 FGuid ActionId,RoundId;
 bool bHitConfirmed=false;
 bool bImpact=false;
 bool bNaturalTail=false;
 float SourceClock=0.f,SourceClockRate=1.f;
 float ImpactAge=0;
 float ImpactRate=2.f;
 static AEGMobiusSourceAttackVisual* Create(UEGHeroSkillComponent* Skill,AEGChessUnit* Target);
 AEGCombatVisual* CreateSourceVisual(UEGSourcePreviewDefinition* Definition,float Scale);
};
