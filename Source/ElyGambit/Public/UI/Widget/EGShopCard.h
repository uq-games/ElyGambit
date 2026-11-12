#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/EGCardDragOperation.h"
#include "UI/EGCardPresentation.h"
#include "EGShopCard.generated.h"

class AEGCardManager;
class UEGMainHUD;
class UEGRichTextBlock;
class UImage;
class UTextBlock;
class UTexture2D;
class UCanvasPanel;

/** A shop drag records an offer identity; the manager validates it again at release. */
UCLASS()
class ELYGAMBIT_API UEGShopDragOperation : public UEGCardDragOperation
{
 GENERATED_BODY()
public:
 UPROPERTY(Transient) TWeakObjectPtr<AEGCardManager> Manager;
 UPROPERTY(Transient) TWeakObjectPtr<UEGMainHUD> HUD;
 int32 SlotIndex=INDEX_NONE;
 int32 Revision=INDEX_NONE;
 bool bConsumed=false;
 virtual void Drop_Implementation(const FPointerEvent& Event) override;
 virtual void DragCancelled_Implementation(const FPointerEvent& Event) override;
private:
 void Complete(const FPointerEvent& Event);
};

/** BoardTheme-style shop offer card: full-bleed card art, cost badge, description, hover/flip feedback. */
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGShopCard : public UUserWidget
{
 GENERATED_BODY()
public:
 void Configure(int32 Index,AEGCardManager* Source,UEGMainHUD* MainHUD);
 void RefreshOffer();
 static void BuildLayout(UWidgetTree* Tree);
 static void BuildPriceBadge(UWidgetTree* Tree);
 UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Card Appearance") FEGCardAppearance Appearance;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Card Appearance") bool bAuthoredAppearance=false;
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativePreConstruct() override;
 virtual void NativeTick(const FGeometry& Geometry,float DeltaSeconds) override;
 virtual void NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseWheel(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry,const FPointerEvent& Event) override;
 virtual void NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& Operation) override;
private:
 UPROPERTY(Transient) TWeakObjectPtr<AEGCardManager> Manager;
 UPROPERTY(Transient) TWeakObjectPtr<UEGMainHUD> HUD;
 UPROPERTY(Transient) TObjectPtr<UImage> Image;
 UPROPERTY(Transient) TObjectPtr<UImage> CostBadge;
 UPROPERTY(Transient) TObjectPtr<UImage> HoverGlow;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Name;
 /** Card summary uses numeric emphasis; measurement stays on plain text (FitShopSummary). */
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> Description;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> DescriptionPlainText;
 int32 DescriptionFontSize=17;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> Price;
 UPROPERTY(Transient) TObjectPtr<UTexture2D> Artwork;
 UPROPERTY(Transient) TObjectPtr<UTexture2D> Back;
 UPROPERTY(Transient) TObjectPtr<UCanvasPanel> Face;
 UPROPERTY(Transient) TObjectPtr<UImage> BackImage;
 int32 SlotIndex=INDEX_NONE;
 int32 SeenRevision=INDEX_NONE;
 int32 PressedRevision=INDEX_NONE;
 float FlipTime=0.f;
 float HoverAlpha=0.f;
 float HoverSeconds=0.f;
 bool bHoverInformationShown=false;
};
