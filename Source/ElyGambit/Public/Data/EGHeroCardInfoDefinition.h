#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "EGHeroCardInfoDefinition.generated.h"
class UTexture2D;

/** Player-facing explanation only. Combat rules and numbers remain in SkillDefinition. */
UCLASS(BlueprintType)
class ELYGAMBIT_API UEGHeroCardInfoDefinition : public UPrimaryDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information") FText UltimateName;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information",meta=(MultiLine=true)) FText Effect;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information") FText Roles;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information") TObjectPtr<UTexture2D> Icon;
 /** Optional per-action artwork. The legacy Icon remains the ultimate fallback. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information|Skill Icons") TSoftObjectPtr<UTexture2D> BasicIcon;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information|Skill Icons") TSoftObjectPtr<UTexture2D> UltimateIcon;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information|Skill Icons") TSoftObjectPtr<UTexture2D> PassiveIcon;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Information") bool bHasUltimate=true;
 FText ToText() const;
};
