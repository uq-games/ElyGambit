#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EGUltimateSignetVisual.generated.h"
class UMaterialBillboardComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UEGHeroSkillComponent;
class UEGBattleContextComponent;
class UEGHeroSkillDefinition;
class AEGChessUnit;


/** Camera-facing source signet. No damage, input, collision or replication. */
UCLASS()
class ELYGAMBIT_API AEGUltimateSignetVisual : public AActor
{
 GENERATED_BODY()
public:
 AEGUltimateSignetVisual();
 static AEGUltimateSignetVisual* Spawn(UEGHeroSkillComponent* Skill);
 float GetDisplayAge() const {return Age;}
 virtual void Tick(float DeltaSeconds) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
 UPROPERTY(VisibleAnywhere) TObjectPtr<UMaterialBillboardComponent> Surface;
 UPROPERTY() TObjectPtr<UMaterialInterface> SignetMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial;
 UPROPERTY(Transient) TObjectPtr<UEGHeroSkillDefinition> Data;
 TWeakObjectPtr<AEGChessUnit> Caster;
 TWeakObjectPtr<UEGHeroSkillComponent> SkillOwner;
 TWeakObjectPtr<UEGBattleContextComponent> Battle;
 FGuid Round,Action;
 FName HeadBone;
 float DisplayWidth=160.f;
 float ImageAspect=1.f;
 float Age=0.f;
 bool bWaitForHumanEntry=false;
 bool bPostEntryReceipt=false;
};
