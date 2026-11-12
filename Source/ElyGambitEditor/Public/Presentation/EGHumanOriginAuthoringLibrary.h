#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGHumanOriginAuthoringLibrary.generated.h"

class UAnimBlueprint;
class UAnimSequenceBase;
class USkeletalMesh;
class USkeletalMeshComponent;
class UEGHeroDefinition;

/** Read-only authored notify information; times use UE's animation timeline. */
USTRUCT(BlueprintType)
struct FEGHumanOriginNotifyInfo
{
 GENERATED_BODY()

 UPROPERTY(BlueprintReadOnly, Category="Animation|Authoring|HumanOrigin") FName NotifyName;
 /** Populated for UEGSkillNotify, whose gameplay event can differ from NotifyName. */
 UPROPERTY(BlueprintReadOnly, Category="Animation|Authoring|HumanOrigin") FName SkillEvent;
 UPROPERTY(BlueprintReadOnly, Category="Animation|Authoring|HumanOrigin") FString NotifyClass;
 UPROPERTY(BlueprintReadOnly, Category="Animation|Authoring|HumanOrigin") float TimeSeconds = 0.f;
 UPROPERTY(BlueprintReadOnly, Category="Animation|Authoring|HumanOrigin") float DurationSeconds = 0.f;
};

/** Editor-only authoring of a private C2 graph. This does not save or rebind assets. */
UCLASS()
class ELYGAMBITEDITOR_API UEGHumanOriginAuthoringLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Insert isolated secondary-motion nodes after FullBody on a private copy.
  * Roots must be explicit source-derived candidates and must not overlap. */
 UFUNCTION(BlueprintCallable, Category="Animation|Authoring|HumanOrigin")
 static bool AddCandidateClothToPrivateBlueprint(UAnimBlueprint* Blueprint, USkeletalMesh* Mesh,
     const TArray<FName>& RootBones, FString& Error);

 /** Install candidate only on an idle transient PIE ArtPiece; no asset binding. */
 UFUNCTION(BlueprintCallable, Category="Animation|Authoring|HumanOrigin")
 static bool InstallOnTransientArtPiece(USkeletalMeshComponent* Body, UAnimBlueprint* Candidate,
     UEGHeroDefinition* Hero, FString& Error);

 /** Returns the authored events in array order without editing the animation. */
 UFUNCTION(BlueprintPure, Category="Animation|Authoring|HumanOrigin")
 static TArray<FEGHumanOriginNotifyInfo> GetAnimationNotifySummary(const UAnimSequenceBase* Animation);
};
