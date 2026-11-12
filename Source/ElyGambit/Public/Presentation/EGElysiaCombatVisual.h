#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGElysiaCombatVisual.generated.h"
class AEGChessUnit;class AEGCombatVisual;class UEGBattleContextComponent;
class UEGElysiaSkillDefinition;class USceneComponent;class UEGSourcePreviewDefinition;
class UStaticMeshComponent;class UMaterialInstanceDynamic;
class UNiagaraComponent;struct FEGElysiaNiagaraCue;
/** Round-owned cosmetics only. The behavior submits every hit through the existing GAS path. */
UCLASS()
class ELYGAMBIT_API AEGElysiaCombatVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGElysiaCombatVisual();
 static AEGElysiaCombatVisual* Spawn(AEGChessUnit* Source,AEGChessUnit* Target,const FVector& From,const FVector& To,float Duration,int32 Kind,bool Charged=false,bool Enhanced=false,bool Ricochet=false,float AreaRadius=0.f);
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 void SetSeedStacks(int32 Count);
 /** Consume the authority's arrival clock; never submit damage from cosmetics. */
 void SetFlightDeadline(double Deadline){FlightDeadline=Deadline;}
 void CompleteFlight(const FVector& Contact);
 /** Presentation-only receipt for one authority-submitted rain pulse. */
 void PulseRain(int32 PulseIndex);
 /** Stop emission after the final pulse, leaving a short, round-owned visual tail. */
 void FinishRain();
 /** Legacy field natural expiry; returns its owned exit visual when configured. */
 AEGElysiaCombatVisual* FinishDome();
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly) int32 VisualKind=0;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> SourceRoot;
 UPROPERTY(Transient) TObjectPtr<AEGCombatVisual> SourceVisual;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> DomeBoundary;
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> DomeShell;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DomeShellMaterial;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> LoopCue;
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> RainAreaCue;
 /** Owned by this cast; the actor's end play also ends the petal layer. */
 UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> PetalCue;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Caster;
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> TargetUnit;
 UPROPERTY(Transient) TWeakObjectPtr<UEGBattleContextComponent> Battle;
 FGuid Round;
 FVector Start=FVector::ZeroVector,End=FVector::ZeroVector;
 float Age=0,Duration=1;
 double FlightDeadline=0.;
 float RainRadius=0.f;
 float SourceDuration=0;
 int32 ShownSeedStacks=0;
 int32 LastRainPulse=0;
 bool bRainFinishing=false;
 bool bRainVisual=false;
 FGuid ActionToken;
 bool BuildSource(UEGSourcePreviewDefinition* Definition,float Scale);
 UNiagaraComponent* SpawnCue(const FEGElysiaNiagaraCue& Cue,float RadiusScale,bool OneShot,float RainRadius=0.f,float BoardDiameter=0.f);
};
