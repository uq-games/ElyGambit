#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/EGChessTypes.h"
#include "EGCombatTextLayer.generated.h"

class AEGChessUnit;
class UCanvasPanel;
class UTextBlock;

/** One reusable, non-interactive combat number. */
UCLASS()
class ELYGAMBIT_API UEGCombatNumber : public UUserWidget
{
 GENERATED_BODY()
public:
 void Configure(double Amount,bool bHealing,bool bAbsorbed,bool bEnemyDamage);
 void SetAmount(double Amount,bool bHealing);
 static FString FormatAmount(double Amount);
 virtual void NativeOnInitialized() override;
 virtual int32 NativePaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& CullingRect,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override;
private:
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Number;
 bool bShield=false;
};

USTRUCT()
struct FEGActiveCombatNumber
{
 GENERATED_BODY()
 UPROPERTY(Transient) TWeakObjectPtr<AEGChessUnit> Target;
 UPROPERTY(Transient) TObjectPtr<UEGCombatNumber> Widget;
 FGuid TargetId;
 FVector LastWorld=FVector::ZeroVector;
 float StartOffsetY=0.f;
 float Age=0,Duration=.8f;
 double StartedAt=0.,Amount=0.;
 bool bHealing=false,bShield=false;
 bool bEnemyDamage=false;
 int8 Direction=1;

 /** Aggregate only the same target and display kind within the first hit's fixed window. */
 bool TryAccumulate(const FGuid& CandidateId,bool bCandidateHealing,bool bCandidateShield,double Now,float Delta);

 /** Choose a stable vertical lane for a new number over the overlap of both lifetimes. */
 static float ChooseStartOffsetY(const TArray<FEGActiveCombatNumber>& Entries,const FGuid& CandidateId,double Now,float NewDuration,float PreferredOffset);
 /** Compact stagger for enemy damage; opposite directions occupy different lanes. */
 static float ChooseEnemyStartOffsetY(const TArray<FEGActiveCombatNumber>& Entries,const FGuid& CandidateId,double Now,int8 Direction);
 static void SampleEnemyDamage(float T,int8 Direction,FVector2D& Offset,float& Scale,float& Opacity);
};

/** HUD-owned screen layer. A timer exists only while numbers are active. */
UCLASS()
class ELYGAMBIT_API UEGCombatTextLayer : public UUserWidget
{
 GENERATED_BODY()
public:
 virtual void NativeOnInitialized() override;
 virtual void NativeDestruct() override;
 void Present(AEGChessUnit* Target,const FEGCombatResolution& Result);
 UFUNCTION(BlueprintCallable,Category="UI") void ClearNumbers();
 UFUNCTION(BlueprintPure,Category="UI|Diagnostics") int32 GetActiveCount() const {return Active.Num();}
 UFUNCTION(BlueprintPure,Category="UI|Diagnostics") int32 GetPoolSize() const {return Pool.Num();}
 UPROPERTY(Transient,BlueprintReadOnly,Category="UI|Diagnostics") int32 PresentedCount=0;
private:
 void AddNumber(AEGChessUnit* Target,float Amount,bool bHealing,bool bShield);
 void Release(int32 Index);
 void Animate();
 UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Canvas;
 UPROPERTY(Transient) TArray<FEGActiveCombatNumber> Active;
 UPROPERTY(Transient) TArray<TObjectPtr<UEGCombatNumber>> Pool;
 FTimerHandle AnimationTimer;
 bool bNextEnemyDamageRight=true;
};
