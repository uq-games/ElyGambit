#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGCombatAuthoringLibrary.generated.h"
class USkeletalMesh;class UAnimSequence;class UAnimBlueprint;class UAnimMontage;
UCLASS()
class ELYGAMBITEDITOR_API UEGCombatAuthoringLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static UAnimBlueprint* CreateRuntimeBlueprint(const FString& PackagePath,USkeletalMesh* Mesh,UAnimSequence* Idle,bool bSecondary=false);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static bool SetSocket(USkeletalMesh* Mesh,FName Name,FName Bone,FTransform Local);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static UAnimBlueprint* CreateCombatBlueprint(const FString& PackagePath,USkeletalMesh* Mesh,UAnimSequence* Idle,UAnimSequence* Walk,UAnimSequence* Run);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static UAnimBlueprint* CreateBowBlueprint(const FString& PackagePath,USkeletalMesh* Mesh);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static UAnimMontage* CreateActionMontage(const FString& PackagePath,UAnimSequence* Clip,float EventSeconds,FName Event,bool bHoldLastFrame=false);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static bool RepairMontageTrack(UAnimMontage* Montage,UAnimSequence* Clip);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static bool SetActionSequence(UAnimMontage* Montage,const TArray<UAnimSequence*>& Clips);
 /** Fit a source interval into Duration seconds; zero preserves source timing. Imported keys are unchanged. */
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static bool SetActionWindow(UAnimMontage* Montage,UAnimSequence* Clip,float Start,float End,float BlendIn,float BlendOut,float Duration=0.f);
 /** Keep the complete source, with separate active-stroke and recovery clocks. */
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static bool SetActionPhases(UAnimMontage* Montage,UAnimSequence* Clip,float SourcePhaseEnd,float PhaseDuration,float TotalDuration,float BlendIn,float BlendOut);
 UFUNCTION(BlueprintCallable,Category="Animation|Authoring") static bool BindLocomotionRate(UAnimBlueprint* Blueprint);
};
