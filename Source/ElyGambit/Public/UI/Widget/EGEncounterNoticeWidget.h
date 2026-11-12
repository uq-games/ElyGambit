#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Types/EGChessTypes.h"
#include "EGEncounterNoticeWidget.generated.h"

class UBorder;
class UCanvasPanel;
class UProgressBar;
class UTextBlock;
class UEGCorruptionCombatComponent;
class AEGCorruptionBarrier;

/** Presentation-only edge detector; repeat broadcasts within one preparation do not restart a notice. */
struct FEGEncounterNoticeState
{
    bool Update(bool bSevenStages, int32 Stage, EEGMatchPhase Phase)
    {
        const bool bPreparation=Phase==EEGMatchPhase::Preparation;
        const bool bShow=bSevenStages && bPreparation && (Stage==3 || Stage==6)
            && (!bInitialized || !bPreviousSevenStages || !bPreviousPreparation || PreviousStage!=Stage);
        bInitialized=true;
        bPreviousSevenStages=bSevenStages;
        bPreviousPreparation=bPreparation;
        PreviousStage=Stage;
        return bShow;
    }
    bool bInitialized=false;
    bool bPreviousSevenStages=false;
    bool bPreviousPreparation=false;
    int32 PreviousStage=INDEX_NONE;
};

/** Local, non-interactive encounter information; gameplay remains in its owning components. */
UCLASS()
class ELYGAMBIT_API UEGEncounterNoticeWidget : public UUserWidget
{
    GENERATED_BODY()
public:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    void ApplyMatchView(bool bSevenStages,int32 Stage,EEGMatchPhase Phase);
private:
    void RefreshBoss(const FGeometry& Geometry);
    UPROPERTY(Transient) TObjectPtr<UBorder> NoticePanel;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> BossPanel;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> BarrierPanel;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BossTitle;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> BarrierTime;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> BossBar;
    UPROPERTY(Transient) TObjectPtr<UProgressBar> BarrierBar;
    TWeakObjectPtr<UEGCorruptionCombatComponent> Boss;
    TWeakObjectPtr<AEGCorruptionBarrier> Barrier;
    FEGEncounterNoticeState NoticeState;
    EEGMatchPhase CurrentPhase=EEGMatchPhase::Finished;
    double NoticeEnd=0.;
};
