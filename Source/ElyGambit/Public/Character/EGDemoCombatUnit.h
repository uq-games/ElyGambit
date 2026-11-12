#pragma once
#include "CoreMinimal.h"
#include "Character/EGChessUnit.h"
#include "EGDemoCombatUnit.generated.h"

class AEGDeployedPiece;
class UAnimSequence;
class UEGChessAnimInstance;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UWidgetComponent;
class UStaticMeshComponent;
struct FEGBoardLocomotion;

/** Presentation-only attack "pounce": moves the visible mesh forward/up on a parabolic arc and
 *  adds a short squash/stretch. Neutral defaults (everything zero) keep the legacy motion. The
 *  actor transform, collision, range, targeting, damage and attack-speed rules are never touched. */
USTRUCT(BlueprintType)
struct ELYGAMBIT_API FEGAttackPouncePresentation
{
 GENERATED_BODY()
 /** Forward distance in board centimetres toward the current target, reached at contact. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce") float Distance=0.f;
 /** Peak arc height in centimetres above the authored mesh transform. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce") float Height=0.f;
 /** Explicit launch-to-contact seconds. Zero derives the window from LaunchFraction/ContactFraction. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce") float Seconds=0.f;
 /** Contact squash amount (0.18 = 18% vertical compression). Zero disables squash and stretch. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce") float Squash=0.f;
 /** Action fraction where the launch begins. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce",meta=(ClampMin="0",ClampMax="1")) float LaunchFraction=0.28f;
 /** Action fraction where the pounce reaches contact; must match the Hit notify fraction. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce",meta=(ClampMin="0",ClampMax="1")) float ContactFraction=0.5f;
 /** Action fraction where the mesh has returned to its authored transform. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce",meta=(ClampMin="0",ClampMax="1")) float ReturnFraction=0.9f;
 /** Squash/stretch recovery length in seconds, measured from contact. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Pounce") float SquashSeconds=0.13f;
 /** Any non-zero component opts the unit in; the default struct is a hard no-op. */
 bool IsConfigured() const {return Distance>KINDA_SMALL_NUMBER||Height>KINDA_SMALL_NUMBER||Squash>KINDA_SMALL_NUMBER;}
};

UCLASS()
class ELYGAMBIT_API AEGDemoCombatUnit : public AEGChessUnit
{
 GENERATED_BODY()
public:
 AEGDemoCombatUnit();
 virtual void SetPresentationRate(float Rate) override;
 virtual void CancelPresentationAction() override;
 virtual void PresentRevive() override;
 virtual FVector GetActionOrigin() const override;
 virtual USkeletalMeshComponent* GetVisiblePresentationMesh() const override {return PresentationMesh();}
 virtual void Tick(float DeltaSeconds) override;
 virtual bool InitializeUnit(UEGHeroDefinition* Definition,FGuid Id,EEGFaction NewFaction,int32 Level=1) override;
 virtual void BeginPlay() override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void AttachPresentation(AEGDeployedPiece* Piece) override {AttachPiece(Piece);}
 virtual void RestorePresentation() override {RestorePiece();}
 virtual void PresentAttack(AEGChessUnit* Target,bool bUltimate) override;
 virtual bool TryPresentAction(AEGChessUnit* Target,bool bUltimate) override;
 void ConfigureMonster(int32 Kind);
 void AttachPiece(AEGDeployedPiece* Piece);
 void RestorePiece();
 UPROPERTY(VisibleAnywhere) TObjectPtr<UWidgetComponent> Vitals;
 /** Presentation-only attack pounce/squash. Set only on units that should visibly lunge. */
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Presentation") FEGAttackPouncePresentation AttackPounce;
 UFUNCTION(BlueprintPure,Category="Demo") int32 GetUltimateCount() const {return UltimateCount;}
 UFUNCTION(BlueprintPure,Category="Demo") int32 GetAttackCount() const {return AttackCount;}
private:
 UFUNCTION() void HandleMonsterDamage(float Amount);
 float MonsterDeathElapsed=0.f;
 float DeathPresentationElapsed=0.f;
 UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> DeathMaterials;
 void TickMonsterDeath(float DeltaSeconds);
 UPROPERTY(Transient) TWeakObjectPtr<AEGDeployedPiece> ArtPiece;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> IdleAnimation;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> WalkAnimation;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> RunAnimation;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> AttackAnimation;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> DeathAnimation;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> SkillAnimation;
 USkeletalMeshComponent* PresentationMesh() const;
 void DriveLocomotion(float Speed);
 bool PlayPresentation(UAnimSequence* Animation,bool bLoop);
 UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Bolt;
 FVector BoltStart;
 FVector BoltEnd;
 float BoltTime=0.f;
 bool bSkillPulse=false;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AttackMaterial;
 UPROPERTY(Transient) TObjectPtr<UMaterialInterface> SkillMaterial;
 void ConfigurePresentation();
 // Presentation-only pounce state; the authored mesh transform is restored exactly on every exit.
 void BeginAttackPounce();
 void TickAttackPounce(float DeltaSeconds);
 void EndAttackPounce();
 bool bPounceActive=false;
 bool bPounceAwaitingRate=false;
 float PounceElapsed=0.f;
 float PounceDuration=0.f;
 FTransform PounceRestRelative=FTransform::Identity;
 FTransform PreparationTransform;
 FVector PreviousLocation=FVector::ZeroVector;
 float AnimationTime=0.f;
 TSharedPtr<FEGBoardLocomotion> Locomotion;
 bool bDeathShown=false,bReviveShown=false,bFlightShown=false;
 int32 AttackCount=0;
 int32 UltimateCount=0;
};
