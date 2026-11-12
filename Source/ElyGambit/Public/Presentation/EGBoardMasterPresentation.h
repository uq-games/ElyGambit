#pragma once
#include "Components/ActorComponent.h"
#include "Types/EGChessTypes.h"
#include "EGBoardMasterPresentation.generated.h"
class AEGDemoGameMode;
class AEGBoard;
class AEGArtPiece;
class UEGHeroDefinition;

UCLASS()
class ELYGAMBIT_API UEGBoardMasterPresentation : public UActorComponent
{
    GENERATED_BODY()
public:
    UEGBoardMasterPresentation();
    static void Install(AEGDemoGameMode* Mode);
    /** Capsule-center-height positions shared by board masters and settlement. */
    static bool GetMasterAnchors(const AEGBoard* Board, FVector& OutFriendly, FVector& OutEnemy);
    /** Relative size applied to a master's model, based on the same hero on the board. */
    static float GetDisplayScale(const UEGHeroDefinition* Hero, const UObject* WorldContext);
    /** Same actor/mesh persists through idle, attack, hit and return to idle. */
    AEGArtPiece* GetMaster(bool bFriendly) const {return bFriendly?Friendly.Get():Enemy.Get();}
    static AEGArtPiece* FindOrCreateMaster(UWorld* World,bool bFriendly,UEGHeroDefinition* Hero,const FVector& Position,const FVector& Facing,float CenterHeight);
    /** Keeps the existing masters visible during a settlement request. */
    void SetSettlementPresenter(AActor* Presenter, bool bActive);
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    UPROPERTY(Transient) TArray<TWeakObjectPtr<AActor>> SettlementPresenters;
    UPROPERTY(Transient) TObjectPtr<AEGArtPiece> Friendly;
    UPROPERTY(Transient) TObjectPtr<AEGArtPiece> Enemy;
    float FriendlyReveal = 1.f;
    float EnemyReveal = 1.f;
    float FriendlyBaseScale = 1.f;
    float EnemyBaseScale = 1.f;
    UFUNCTION() void Refresh();
    UFUNCTION() void PhaseChanged(EEGMatchPhase Phase);
    AEGArtPiece* CreateBody(UEGHeroDefinition* Hero,const FVector& Position,const FVector& Facing,bool bFriendly);
    void UpdateDisplayScale(AEGArtPiece* Body, float Reveal, float& BaseScale) const;
};
