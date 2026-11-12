#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "EGDragFeedback.generated.h"

class UBorder;
class UImage;
class UEGRichTextBlock;
class UTextBlock;
class UTexture2D;
class UEGHeroDefinition;
class UCanvasPanel;

/** Read-only drag presentation; placement and sale remain manager transactions. */
UCLASS()
class ELYGAMBIT_API UEGDragFeedback : public UUserWidget
{
    GENERATED_BODY()
public:
    void Present(UEGHeroDefinition* Definition, UTexture2D* Artwork, FVector2D Origin,
                 FVector2D Pointer, bool bAllowed, bool bShowCardAndArrow);
    void SetHint(const FText& Text);
    /** Absolute Slate coordinates. Four consecutive corners describe each legal cell. */
    void PresentBoardMove(const TArray<FVector2D>& CellCorners, FVector2D Origin,
                          FVector2D Pointer, bool bAllowed, bool bShowArrow);
    void ClearBoardMove();
protected:
    virtual void NativeOnInitialized() override;
    virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
    virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
        int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
private:
    UPROPERTY(Transient) TObjectPtr<UBorder> InfoPanel;
    UPROPERTY(Transient) TObjectPtr<UImage> Portrait;
    UPROPERTY(Transient) TObjectPtr<UCanvasPanel> DragCard;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Heading;
    /** Drag summary: plain-text input rendered with numeric emphasis. */
    UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> Description;
    UPROPERTY(Transient) TObjectPtr<UTextBlock> Hint;
    UPROPERTY(Transient) TWeakObjectPtr<UEGHeroDefinition> ShownDefinition;
    UPROPERTY(Transient) TWeakObjectPtr<UTexture2D> ShownArtwork;
    FVector2D ArrowOrigin = FVector2D::ZeroVector;
    FVector2D ArrowPointer = FVector2D::ZeroVector;
    bool bDrawArrow = false;
    bool bPlacementAllowed = false;
    TArray<FVector2D> LegalCellCorners;
};
