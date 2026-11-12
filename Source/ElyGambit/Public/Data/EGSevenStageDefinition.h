#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "EGSevenStageDefinition.generated.h"

class UEGRunDefinition;
UENUM()
enum class EEGEncounterPawn : uint8 { Chest, Eye, Spore, Flower, Copy, Boss };

USTRUCT()
struct FEGEncounterPosition
{
    GENERATED_BODY()
    UPROPERTY() EEGEncounterPawn Pawn = EEGEncounterPawn::Chest;
    UPROPERTY() FIntPoint Cell = FIntPoint::ZeroValue;
};

USTRUCT()
struct FEGEncounterRow
{
    GENERATED_BODY()
    UPROPERTY() int32 Level = 1;
    UPROPERTY() int32 Seconds = 60;
    UPROPERTY() int32 Income = 0;
    UPROPERTY() TArray<FEGEncounterPosition> Enemies;
    UPROPERTY() int32 RemoveIndex = INDEX_NONE;
    UPROPERTY() FEGEncounterPosition Extra;
};

UCLASS(Config=Game)
class UEGSevenStageDefinition : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(Config) TArray<FEGEncounterRow> Stages;
    UPROPERTY(Config) float PreparationRealSeconds=45.f;
    UPROPERTY(Config) float BossNormalRealSeconds=25.f;
    UPROPERTY(Config) float OvertimeRealSeconds=15.f;
    UPROPERTY(Config) float OvertimeSpeed=1.5f;
};

namespace EGSevenStage
{
    ELYGAMBIT_API UEGRunDefinition* Build(UObject* Outer, UEGRunDefinition* Seed);
}
