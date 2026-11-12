#pragma once

#include "CoreMinimal.h"
#include "EGChessTypes.generated.h"

class UEGHeroDefinition;

UENUM(BlueprintType)
enum class EEGMatchPhase : uint8
{
	Preparation,
	Combat,
	Resolution,
	Finished,
	Settlement,
	Reward
};
UENUM(BlueprintType)
enum class EEGStageType : uint8
{
	Normal,
	Corruption,
	Boss
};
UENUM(BlueprintType)
enum class EEGFaction : uint8
{
	Neutral,
	Player,
	Enemy
};
UENUM(BlueprintType)
enum class EEGUnitState : uint8
{
	Idle,
	Fighting,
	Dead,
	Reviving
};
UENUM(BlueprintType)
enum class EEGGrowthType : uint8
{
	Frontline,
	Backline,
	Support
};

UENUM(BlueprintType)
enum class EEGHeroKind : uint8 { Generic=0, Pardofelis=1, Hua=3, Can=6, Mobius=8, CollapsedHua=9, Sakura=10, VillV=11, Eden=12, Aponia=13, Griseo=14, Knight=15, Elysia=16, GriseoAdult=17, ElysiaHerrscher=18 };
UENUM(BlueprintType)
enum class EEGHeroForm : uint8 { Base, Enhanced };

/** A value-only receipt of the actual GE application, suitable for UI and diagnostics. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGCombatResolution
{
 GENERATED_BODY()
 UPROPERTY(BlueprintReadOnly) FGuid SourceId;
 UPROPERTY(BlueprintReadOnly) FGuid TargetId;
 UPROPERTY(BlueprintReadOnly) FGuid ActionId;
 UPROPERTY(BlueprintReadOnly) float RawAmount=0;
 UPROPERTY(BlueprintReadOnly) float AfterDefense=0;
 UPROPERTY(BlueprintReadOnly) float BubbleAbsorbed=0;
 UPROPERTY(BlueprintReadOnly) float ProtectionPrevented=0;
 UPROPERTY(BlueprintReadOnly) float ShieldAbsorbed=0;
 UPROPERTY(BlueprintReadOnly) float HealthDelta=0;
 UPROPERTY(BlueprintReadOnly) bool bHealing=false;
 UPROPERTY(BlueprintReadOnly) bool bEnvironmentDamage=false;
 UPROPERTY(BlueprintReadOnly) bool bFatal=false;
 /** Propagated damage cannot enter another connection group. */
 UPROPERTY(BlueprintReadOnly) bool bLinkedDamage=false;
   /** Additional or periodic stigmata damage does not trigger basic attacks or links. */
   UPROPERTY(BlueprintReadOnly) bool bStigmataDamage=false;
   UPROPERTY(BlueprintReadOnly) bool bPeriodicDamage=false;
 /** Threshold execution is a distinct hit type and must not propagate through damage links. */
 UPROPERTY(BlueprintReadOnly) bool bExecution=false;
};

USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGStatBlock
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AutoChess|Data")
	float MaxHealth = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AutoChess|Data")
	float MaxMana = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AutoChess|Data")
	float AttackPower = 10.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AutoChess|Data")
	float AttackSpeed = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AutoChess|Data")
	float Defense = 0.f;
};

UENUM(BlueprintType)
enum class EEGDeploymentPart : uint8 { Hero, Gatling, Cannon };

/** Per-run data. Never owned by a combat Actor. Grid is the preparation placement only. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGHeroInstance
{
	GENERATED_BODY()
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	FGuid InstanceId;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	TObjectPtr<UEGHeroDefinition> Definition;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	int32 Level = 1;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AutoChess|Data")
	EEGHeroForm Form = EEGHeroForm::Base;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	int32 SameNameCards = 1;
	/** Actual shop expenditure carried across merges, reserve and snapshots. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="AutoChess|Data")
	int64 PaidCost = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	bool bAwakened = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	bool bDeployed = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AutoChess|Data")
	FIntPoint DeploymentCell = FIntPoint::ZeroValue;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AutoChess|Data") bool bVillVLayout=false;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AutoChess|Data") FIntPoint GatlingCell=FIntPoint(INDEX_NONE,INDEX_NONE);
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="AutoChess|Data") FIntPoint CannonCell=FIntPoint(INDEX_NONE,INDEX_NONE);
};
