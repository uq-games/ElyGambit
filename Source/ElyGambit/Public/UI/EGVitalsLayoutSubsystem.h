#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "EGVitalsLayoutSubsystem.generated.h"
class UWidgetComponent;
class USkeletalMeshComponent;
class USkeletalMesh;

/** Updates overhead anchors after the visible character pose has been evaluated. */
UCLASS()
class ELYGAMBIT_API UEGVitalsLayoutSubsystem : public UTickableWorldSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 // Read the final body pose after actor/component ticks, before viewport draw.
 virtual bool IsTickable() const override {return false;}
 virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
 virtual void Tick(float DeltaTime) override;
 virtual TStatId GetStatId() const override;
 virtual void Deinitialize() override;
 void Register(UWidgetComponent* Component);
private:
 void AfterActors(UWorld* World,ELevelTick TickType,float DeltaTime);
 FDelegateHandle AfterActorsHandle;
 struct FEntry
 {
  TWeakObjectPtr<UWidgetComponent> Component;
  FVector AuthoredAnchor=FVector::ZeroVector;
  FVector LastHeadAnchor=FVector::ZeroVector;
  bool bHasHeadAnchor=false;
  // Preparation rigs can swap meshes between entrance and idle. Cache the
  // visible body and its head bone so the post-pose tick does not rescan it.
  TWeakObjectPtr<USkeletalMeshComponent> PreparationBody;
  TWeakObjectPtr<USkeletalMesh> PreparationMesh;
  FName PreparationHead=NAME_None;
  double NextPreparationSearchTime=0.0;
 };
 TArray<FEntry> Entries;
};
