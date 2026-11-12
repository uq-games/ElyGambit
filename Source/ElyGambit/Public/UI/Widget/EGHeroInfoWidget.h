#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/EGUnitInspectionView.h"
#include "EGHeroInfoWidget.generated.h"
class UEGHeroDefinition;
class UTexture2D;
class UWidgetTree;
class UEGRichTextBlock;
class UTextBlock;
class UButton;
class USizeBox;
class UEGHeroInfoWidget;
/** A stable per-button delegate target; owned by the inspection widget. */
UCLASS(Transient)
class UEGInspectionSkillButtonHandler : public UObject
{
 GENERATED_BODY()
public:
 TWeakObjectPtr<UEGHeroInfoWidget> Owner;
 int32 Index=INDEX_NONE;
 UFUNCTION() void Click();
};
UENUM(BlueprintType)
enum class EEGHeroInfoPresentation : uint8 { Tooltip, DragPreview, CardPreview, UnitHover, UnitDetails };
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FEGHeroInfoClose);
UCLASS(Blueprintable)
class ELYGAMBIT_API UEGHeroInfoWidget : public UUserWidget
{
 GENERATED_BODY()
public:
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Information") bool bDragLayout=false;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Information") EEGHeroInfoPresentation Presentation=EEGHeroInfoPresentation::Tooltip;
 UPROPERTY(BlueprintAssignable,Category="Information") FEGHeroInfoClose OnCloseRequested;
 UPROPERTY(BlueprintAssignable,Category="Information") FEGHeroInfoClose OnSellRequested;
 UFUNCTION(BlueprintCallable,Category="Information") void Present(const UEGHeroDefinition* Definition,const FText& Extra);
 void PresentUnit(const UEGHeroDefinition* Definition,const FText& Summary,float Health,float MaxHealth,float Mana,float MaxMana);
 void PresentAttributes(const FEGUnitInspectionView& View);
 void OpenEnemySkill(int32 Index);
 void CollapseSkill();
 bool HasSkillDetailsOpen() const { return OpenSkillIndex!=INDEX_NONE; }
 UFUNCTION() void CloseSkillDetails();
 float GetSkillDetailsWidth(float ViewWidth) const;
 void SetSkillLayoutWidth(float ViewWidth);
 void SetSaleAvailable(bool bAvailable);
 void SetReturnAvailable(bool bAvailable);
 static void BuildLayout(UWidgetTree* Tree,bool bDrag);
 static void BuildPresentation(UWidgetTree* Tree,EEGHeroInfoPresentation Mode);
protected:
 virtual void NativeOnInitialized() override;
 virtual void NativeConstruct() override;
 virtual void NativeDestruct() override;
 virtual void NativeTick(const FGeometry& MyGeometry,float InDeltaTime) override;
private:
 TWeakObjectPtr<const UEGHeroDefinition> PresentedDefinition;
 bool bSkillExpanded=false;
 int32 OpenSkillIndex=INDEX_NONE;
 bool bSkillDetailsInline=false;
 float LastSkillLayoutViewport=-1.f;
 float NormalInfoScrollOffset=0.f;
 float SkillDetailsReveal=0.f;
 float SkillOpeningDuration=.12f;
 int32 PendingSkillIndex=INDEX_NONE;
 bool bClosingSkillDetails=false;
 bool bSwitchingSkillDetails=false;
 bool bHasSkillOverride=false;
 FText SkillBasicOverride,SkillUltimateOverride,SkillPassiveOverride;
 bool bInspectedEnemy=false;
 int32 DisplayedBossArtworkPhase=0;
 int32 PendingBossScrollPhase=0;
 FString EnemySkillsLayoutKey;
 TArray<FEGInspectionSkill> EnemySkills;
 UPROPERTY(Transient) TArray<TObjectPtr<UEGInspectionSkillButtonHandler>> EnemySkillHandlers;
 UPROPERTY(Transient) TArray<TObjectPtr<UButton>> EnemySkillButtons;
 UPROPERTY(Transient) TArray<TObjectPtr<USizeBox>> EnemySkillGroupHeaders;
 UPROPERTY(Transient) TObjectPtr<UTexture2D> BossPhaseArtwork;
 void UpdateEnemySkills(const FEGUnitInspectionView& View);
 void ApplyPendingBossScroll();
 int32 InspectedLevel=1;
 void OpenSkillDetails(int32 Index);
 void RefreshSkillDetails();
 void FinishCloseSkillDetails();
 void UpdateSkillSelection();
 void SetActionHover(int32 Index,bool bHovered);
 UFUNCTION() void OpenBasicDetails();
 UFUNCTION() void OpenUltimateDetails();
 UFUNCTION() void OpenPassiveDetails();
 UFUNCTION() void HoverBasic();
 UFUNCTION() void UnhoverBasic();
 UFUNCTION() void HoverUltimate();
 UFUNCTION() void UnhoverUltimate();
 UFUNCTION() void HoverPassive();
 UFUNCTION() void UnhoverPassive();
 /** Saved-tree text blocks are converted to rich text once, then written through. */
 void EnsureRichTextTexts();
 void SetSkillText(UEGRichTextBlock* Rich,UTextBlock* Plain,const FText& Value);
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> SkillEffectRichText;
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> SkillExtraRichText;
 UPROPERTY(Transient) TObjectPtr<UEGRichTextBlock> SkillHintRichText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> SkillEffectPlainText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> SkillExtraPlainText;
 UPROPERTY(Transient) TObjectPtr<UTextBlock> SkillHintPlainText;
 bool bRichTextEnsured=false;
 UFUNCTION() void ToggleSkill();
 UFUNCTION() void RequestClose();
 UFUNCTION() void RequestSell();
};
