#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EGVictoryEndingDefinition.generated.h"

class ULevelSequence;
class UMediaSource;
class USoundBase;
class UAnimSequence;

UENUM(BlueprintType)
enum class EEGVictoryEndingNodeType : uint8 { Dialogue, Sequence, Video };

/** A shot can be authored in world coordinates or relative to an existing master. */
UENUM(BlueprintType)
enum class EEGVictoryCameraAnchor : uint8 { World, ElysiaMaster, CorruptionMaster };

/** Authored media are separate so subtitles and original sound are verifiable. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGVictoryEndingNode
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere, BlueprintReadOnly) EEGVictoryEndingNodeType Type = EEGVictoryEndingNodeType::Dialogue;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) FText Speaker;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, meta=(MultiLine=true)) FText Subtitle;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> Voice;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<ULevelSequence> Sequence;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<UMediaSource> MediaSource;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> MediaEffect;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) bool bMuteMediaAudio = true;
 /** Private animations must match each master, have no notifies and use no root motion. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Characters") TSoftObjectPtr<UAnimSequence> ElysiaAnimation;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Characters") TSoftObjectPtr<UAnimSequence> CorruptionAnimation;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Characters") bool bWaitForAnimations = true;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera") bool bUseCamera = false;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(EditCondition="bUseCamera")) EEGVictoryCameraAnchor CameraAnchor = EEGVictoryCameraAnchor::World;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(EditCondition="bUseCamera")) FTransform CameraTransform = FTransform::Identity;
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(EditCondition="bUseCamera", ClampMin="5", ClampMax="170")) float CameraFOV = 45.f;
 /** Frame the selected master's head and upper body; CameraTransform XY gives the viewing direction in actor space. */
 UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Camera", meta=(EditCondition="bUseCamera")) bool bFrameMasterUpperBody = false;
};

/** One shared victory ending for all routes. No draft text is supplied by code. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGVictoryEndingDefinition : public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TSoftObjectPtr<USoundBase> BackgroundMusic;
 UPROPERTY(EditAnywhere, BlueprintReadOnly) TArray<FEGVictoryEndingNode> Nodes;
};
