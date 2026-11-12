#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Game/EGCardManager.h"
#include "Placement/EGBoardScene.h"
#include "Engine/Font.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/HUD/EGHUD.h"
#include "Character/EGChessUnit.h"
#include "Game/EGHeroRoster.h"
#include "Data/EGHeroDefinition.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGMatchManager.h"
#include "Components/EGGameAudioComponent.h"
#include "Engine/GameInstance.h"
#include "Components/VerticalBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"

#include "UI/EGCardDragOperation.h"
#include "UI/Widget/EGShopCard.h"
#include "UI/Widget/EGDragFeedback.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGTypography.h"
#include "UI/Widget/EGRichText.h"
#include "UI/EGCardPresentation.h"
#include "UI/Widget/EGGradientSurface.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/EGUnitInformation.h"
#include "Data/EGCardDefinition.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/ButtonSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Templates/Function.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"

namespace
{
/** Depth-first image walk behind the shop/sell tint ramps. */
void ForEachImageUnder(UWidget* Root,const TFunction<void(UImage*)>& Visitor)
{
    TArray<UWidget*> Stack;
    TSet<UWidget*> Visited;
    if(Root)Stack.Add(Root);
    while(!Stack.IsEmpty())
    {
        UWidget* Current=Stack.Pop();
        if(!Current||Visited.Contains(Current))continue;
        Visited.Add(Current);
        if(auto* Image=Cast<UImage>(Current))Visitor(Image);
        // DrawPanel/SellZone roots are user widgets: their content lives in the
        // WidgetTree, not in UPanelWidget/UContentWidget children, so descend it
        // explicitly (visited set keeps the tree/panel paths from double-walking).
        if(auto* UserWidget=Cast<UUserWidget>(Current))
            if(UserWidget->WidgetTree)Stack.Add(UserWidget->WidgetTree->RootWidget);
        if(auto* Panel=Cast<UPanelWidget>(Current))
            for(int32 I=0;I<Panel->GetChildrenCount();++I)Stack.Add(Panel->GetChildAt(I));
        if(auto* Content=Cast<UContentWidget>(Current))Stack.Add(Content->GetContent());
    }
}
/** Font fitting for the sale label: the authored 16px is only reduced when needed. */
FSlateFontInfo FitSellLabelFont(const FString& Text,float MaxWidth,float MaxHeight)
{
    const int32 Sizes[]={16,15,14,13,12,11,10};
    if(!FSlateApplication::IsInitialized())return FEGVisualStyle::Font(16);
    const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    for(const int32 Size:Sizes)
    {
        const FSlateFontInfo Font=FEGVisualStyle::Font(Size);
        float Width=0.f,Height=0.f;
        TArray<FString> Lines;
        Text.ParseIntoArray(Lines,TEXT("\n"),false);
        for(const FString& Line:Lines)Width=FMath::Max(Width,float(Measure->Measure(Line,Font).X));
        Height=float(Measure->GetMaxCharacterHeight(Font))*Lines.Num()*1.15f;
        if(Width<=MaxWidth&&Height<=MaxHeight)return Font;
    }
    return FEGVisualStyle::Font(10);
}

// Sale targets start collapsed, so their own cached geometry may describe an
// earlier frame or have zero size on the first drag frame. The live parent
// canvas and its slot already define the target in absolute Slate coordinates.
bool GetSaleZoneBounds(const UWidget* Zone,FSlateRect& Bounds)
{
    const auto* Slot=Zone?Cast<UCanvasPanelSlot>(Zone->Slot):nullptr;
    const auto* Parent=Zone?Zone->GetParent():nullptr;
    if(!Slot||!Parent||Slot->GetAutoSize())return false;
    const FGeometry& Geometry=Parent->GetCachedGeometry();
    const FVector2D ParentSize=Geometry.GetLocalSize(),Size=Slot->GetSize();
    const FAnchors Anchors=Slot->GetAnchors();
    if(ParentSize.X<=0.f||ParentSize.Y<=0.f||Size.X<=0.f||Size.Y<=0.f||
        !Anchors.Minimum.Equals(Anchors.Maximum))return false;
    const FVector2D Origin=ParentSize*Anchors.Minimum+Slot->GetPosition()-Size*Slot->GetAlignment();
    const FVector2D Corners[]={Geometry.LocalToAbsolute(Origin),Geometry.LocalToAbsolute(Origin+FVector2D(Size.X,0)),
        Geometry.LocalToAbsolute(Origin+Size),Geometry.LocalToAbsolute(Origin+FVector2D(0,Size.Y))};
    Bounds=FSlateRect(Corners[0].X,Corners[0].Y,Corners[0].X,Corners[0].Y);
    for(const FVector2D& Corner:Corners)
    {
        Bounds.Left=FMath::Min(Bounds.Left,float(Corner.X));Bounds.Top=FMath::Min(Bounds.Top,float(Corner.Y));
        Bounds.Right=FMath::Max(Bounds.Right,float(Corner.X));Bounds.Bottom=FMath::Max(Bounds.Bottom,float(Corner.Y));
    }
    return Bounds.Right>Bounds.Left&&Bounds.Bottom>Bounds.Top;
}
}

TSharedRef<SWidget> UEGHandCard::RebuildWidget()
{
    if(!IsDesignTime()&&WidgetTree&&WidgetTree->RootWidget&&!CompactViewport)
    {
        UWidget* OriginalRoot=WidgetTree->RootWidget;
        CompactViewport=WidgetTree->ConstructWidget<USizeBox>();
        CompactViewport->SetClipping(EWidgetClipping::ClipToBounds);
        auto* Layers=WidgetTree->ConstructWidget<UCanvasPanel>();CompactViewport->SetContent(Layers);
        // Canvas auto-size preserves the complete, undistorted card artwork.
        auto* FaceSlot=Layers->AddChildToCanvas(OriginalRoot);FaceSlot->SetAutoSize(true);
        FaceSlot->SetAnchors(FAnchors(0.f,0.f));FaceSlot->SetPosition(FVector2D::ZeroVector);
        CompactNameBar=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("HandCardNamePlate"));
        // Cover only the tiny baked-in name strip; preserve the full portrait.
        CompactNameBar->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.045f,.032f,.05f,1.f),0.f));
        CompactNameBar->SetPadding(FMargin(2.f,0.f));CompactNameBar->SetHorizontalAlignment(HAlign_Center);CompactNameBar->SetVerticalAlignment(VAlign_Center);
        CompactNameBar->SetVisibility(ESlateVisibility::HitTestInvisible);
        CompactNameText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("HandCardNameText"));
        CompactNameText->SetFont(FEGVisualStyle::PearlFont(14));CompactNameText->SetJustification(ETextJustify::Center);
        CompactNameText->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));CompactNameText->SetVisibility(ESlateVisibility::HitTestInvisible);
        CompactNameText->SetShadowColorAndOpacity(FLinearColor(0.f,0.f,0.f,.95f));CompactNameText->SetShadowOffset(FVector2D(1.f,1.f));
        CompactNameBar->SetContent(CompactNameText);
        auto* NameSlot=Layers->AddChildToCanvas(CompactNameBar);
        NameSlot->SetAnchors(FAnchors(0.f,1.f,1.f,1.f));NameSlot->SetOffsets(FMargin(0.f,-28.f,0.f,28.f));NameSlot->SetZOrder(1);
        WidgetTree->RootWidget=CompactViewport;
        SetCompactPresentation(bCompactPresentation,CompactName);
    }
    return Super::RebuildWidget();
}
void UEGHandCard::SetCompactPresentation(bool bCompact,const FText& Name)
{
    bCompactPresentation=bCompact;CompactName=Name;
    if(CompactNameText)CompactNameText->SetText(CompactName);
    if(CompactNameBar)CompactNameBar->SetVisibility(Name.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    if(CompactViewport)
    {
        if(bCompact){CompactViewport->SetWidthOverride(EGPearlGold::HandCardWidth);CompactViewport->SetHeightOverride(EGPearlGold::HandCardHeight);}
        else{CompactViewport->ClearWidthOverride();CompactViewport->ClearHeightOverride();}
    }
}
void UEGHandCard::SetHoverSourceHidden(bool bHidden)
{
    if(bHidden==bHoverSourceHidden||!CompactViewport)return;
    if(bHidden)HoverSourcePreviousOpacity=CompactViewport->GetRenderOpacity();
    CompactViewport->SetRenderOpacity(bHidden?0.f:HoverSourcePreviousOpacity);
    bHoverSourceHidden=bHidden;
}
void UEGHandCard::NativePreConstruct()
{Super::NativePreConstruct();if(bAuthoredAppearance)FEGCardPresentation::ApplyAuthored(WidgetTree,Appearance);}

void UEGHandCard::PlayAcquisition(float Delay)
{
    ArrivalElapsed=-FMath::Clamp(Delay,0.f,.24f);
    SetRenderTransformPivot(FVector2D(.5f,1.f));
    SetRenderOpacity(0.f);SetRenderTranslation(FVector2D(0,24));SetRenderScale(FVector2D(.88f));
}
void UEGHandCard::SettleMotion()
{
    ArrivalElapsed=-1.f;MotionHover=0.f;bMotionHovered=false;
    SetRenderOpacity(1.f);SetRenderTranslation(FVector2D::ZeroVector);SetRenderScale(FVector2D(1));
}
void UEGHandCard::NativeDestruct(){SetHoverSourceHidden(false);SettleMotion();Super::NativeDestruct();}
void UEGHandCard::NativeTick(const FGeometry& G,float DT)
{
    Super::NativeTick(G,DT);
    const float Target=bMotionHovered?1.f:0.f;
    const bool bArrival=ArrivalElapsed>-.99f;
    if(!bArrival&&FMath::IsNearlyEqual(MotionHover,Target,.001f))return;
    // Exponential smoothing remains continuous when the pointer reverses direction.
    MotionHover=FMath::Lerp(MotionHover,Target,1.f-FMath::Exp(-DT*4.f/FMath::Max(.05f,HoverSeconds)));
    if(FMath::IsNearlyEqual(MotionHover,Target,.001f))MotionHover=Target;
    float Reveal=1.f,Scale=1.f,Lift=0.f;
    if(bArrival)
    {
        ArrivalElapsed+=DT;
        const float P=FMath::Clamp(ArrivalElapsed/FMath::Max(.05f,AcquisitionSeconds),0.f,1.f);
        const float Ease=1.f-FMath::Pow(1.f-P,3.f);
        Reveal=FMath::Clamp(P/.45f,0.f,1.f);
        Scale=FMath::Lerp(.88f,1.f,Ease)+.035f*FMath::Sin(PI*P);
        Lift=24.f*(1.f-Ease);
        if(P>=1.f)ArrivalElapsed=-1.f;
    }
    SetRenderTransformPivot(FVector2D(.5f,1.f));SetRenderOpacity(Reveal);
    const auto* PC=GetOwningPlayer();const auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;
    const bool bBoardHand=HUD&&HUD->GetMainWidget()&&HUD->GetMainWidget()->bBoardPresentation;
    // Board-hand hover uses a separate top-layer copy. Keep the source geometry
    // fixed so overlapping cards retain predictable pointer hit areas.
    SetRenderScale(FVector2D(Scale*(bBoardHand?1.f:1.f+.15f*MotionHover)));
    SetRenderTranslation(FVector2D(0,Lift-(bBoardHand?0.f:15.f*MotionHover)));
    if(CardFrame)CardFrame->SetRenderOpacity(MotionHover);
}

void UEGHandCard::Configure(FGuid Id,UTexture2D* InArtwork)
{
    InArtwork=FEGCardPresentation::ResolveArtwork(InArtwork);
    InstanceId=Id;
    if(Artwork!=InArtwork)
    {
        Artwork=InArtwork;
        FEGCardPresentation::Apply(WidgetTree,Artwork);
    }
}
void UEGHandCard::SetDefinitionAppearance(UEGHeroDefinition* Definition,int32 Level)
{
    CardHeroDefinition=Definition;DisplayLevel=Level;
    auto* Image=CardImage.Get();if(!Image&&WidgetTree)Image=Cast<UImage>(WidgetTree->FindWidget(TEXT("CardImage")));
    FEGCardPresentation::ApplyDefinitionLayers(WidgetTree,Image,Definition,Level);
}
void UEGHandCard::NativeOnMouseEnter(const FGeometry& Geometry,const FPointerEvent& Event)
{
    Super::NativeOnMouseEnter(Geometry,Event);
    bMotionHovered=true;
    if(auto* PC=GetOwningPlayer())if(auto* HUD=Cast<AEGHUD>(PC->GetHUD()))if(auto* Main=HUD->GetMainWidget();Main&&Main->bBoardPresentation)
    {
        if(auto* BoardPC=Cast<AEGBoardPlayerController>(PC);BoardPC&&BoardPC->GetDragState()==EEGPlacementDragState::Idle&&
            !UWidgetBlueprintLibrary::IsDragDropping()&&Main->FindCardWidget(InstanceId)==this)
            UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Hover);
        Main->ShowHandHover(this);Main->ShowHandInformation(InstanceId);
    }
}
void UEGHandCard::NativeOnMouseLeave(const FPointerEvent& Event)
{
    Super::NativeOnMouseLeave(Event);bMotionHovered=false;
    bInformationClick=false;
    if(auto* PC=GetOwningPlayer())if(auto* HUD=Cast<AEGHUD>(PC->GetHUD()))if(auto* Main=HUD->GetMainWidget();Main&&Main->bBoardPresentation){Main->HideHandHover(this);Main->HideCardInformation();}
}
FReply UEGHandCard::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton)
    {bInformationClick=true;bPressTracking=true;PressPosition=Event.GetScreenSpacePosition();
        auto* PC=GetOwningPlayer();auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;auto* Main=HUD?HUD->GetMainWidget():nullptr;
        if(Main&&Main->CanEditHand())return FReply::Handled().DetectDrag(TakeWidget(),EKeys::LeftMouseButton).CaptureMouse(TakeWidget());
        return FReply::Handled().CaptureMouse(TakeWidget());
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
FReply UEGHandCard::NativeOnMouseButtonUp(const FGeometry& G,const FPointerEvent& E)
{
 if(bInformationClick&&E.GetEffectingButton()==EKeys::LeftMouseButton)
 {
  bInformationClick=false;bPressTracking=false;
   if(auto* PC=GetOwningPlayer())if(auto* HUD=Cast<AEGHUD>(PC->GetHUD()))if(auto* Main=HUD->GetMainWidget();Main&&Main->bBoardPresentation){Main->HandleOutsideClick(E.GetScreenSpacePosition());Main->ShowHandInformation(InstanceId,true);return FReply::Handled().ReleaseMouseCapture();}
 }
 if(E.GetEffectingButton()==EKeys::LeftMouseButton){bPressTracking=false;bInformationClick=false;return FReply::Handled().ReleaseMouseCapture();}
 return Super::NativeOnMouseButtonUp(G,E);
}
void UEGHandCard::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event)
{bPressTracking=false;Super::NativeOnMouseCaptureLost(Event);}
void UEGHandCard::NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& Operation)
{
    bInformationClick=false;
    auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
    if(!Controller||!GetOwningPlayer())return;
    auto* Visual=CreateWidget<UEGHandCard>(GetOwningPlayer(),GetClass());
    if(!Visual)return;
    auto* PC=GetOwningPlayer();auto* HUD=PC?Cast<AEGHUD>(PC->GetHUD()):nullptr;auto* Main=HUD?HUD->GetMainWidget():nullptr;
    if(!Main||!Main->CanEditHand())return;
    const int32 Revision=Controller->GetHandOrderRevision();if(Revision==INDEX_NONE)return;
    Main->HideHandHover(this);
    SettleMotion();
    auto* Drag=NewObject<UEGCardDragOperation>(this);
    Visual->Configure(InstanceId,Artwork);Visual->SetDefinitionAppearance(CardHeroDefinition.Get(),DisplayLevel);Visual->SetVisibility(ESlateVisibility::HitTestInvisible);
    Drag->InstanceId=InstanceId;Drag->DefaultDragVisual=Visual;Drag->Pivot=EDragPivot::CenterCenter;
    Drag->ArmHandGesture(Main,Controller,PressPosition,Revision);
    Main->RegisterHandReorder(Drag);Operation=Drag;
    // A single large native movement may already exceed the direction lock.
    Drag->UpdatePendingHandGesture(Event.GetScreenSpacePosition());
}
bool UEGMainHUD::NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation)
{
    if(const auto* Shop=Cast<UEGShopDragOperation>(Operation))return Shop->HUD.Get()==this;
    const auto* Drag=Cast<UEGCardDragOperation>(Operation);
    const auto* Visual=Drag?Cast<UUserWidget>(Drag->DefaultDragVisual):nullptr;
    return Visual&&Visual->GetOwningPlayer()==GetOwningPlayer();
}
void UEGMainHUD::NativeConstruct()
{
    Super::NativeConstruct();
    if(bBoardPresentation)
    {
        SetupBoardView();CreateShopReturnHint();BindShopPlateHover();
        // Keep the authored sale widgets and their hit geometry together at the
        // bottom corners, including when the viewport or UI scale changes.
        int32 Side=0;
        for(auto* Zone:{SellZone_Left.Get(),SellZone_Right.Get()})
        {
            if(Zone)if(auto* ZoneSlot=Cast<UCanvasPanelSlot>(Zone->Slot))
            {
                const float Edge=float(Side);
                ZoneSlot->SetAnchors(FAnchors(Edge,1.f));
                ZoneSlot->SetAlignment(FVector2D(Edge,1.f));
                ZoneSlot->SetPosition(FVector2D::ZeroVector);
                ZoneSlot->SetAutoSize(false);
            }
            ++Side;
        }
    }
    bDrawExpanded=false;bStageExpanded=false;RefreshPanelVisibility();
    if(Button_ToggleDrawPanel)
    {
        Button_ToggleDrawPanel->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleDrawPanel);
        Button_ToggleDrawPanel->OnHovered.AddUniqueDynamic(this,&ThisClass::ShopHovered);
        Button_ToggleDrawPanel->OnUnhovered.AddUniqueDynamic(this,&ThisClass::ShopUnhovered);
    }
    if(Button_Stage)Button_Stage->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleStagePanel);
    if(Button_Battle)Button_Battle->OnClicked.AddUniqueDynamic(this,&ThisClass::RequestBattle);
    if(DrawPanel)
    {
        if(auto* Obtain=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_ObtainElysia"))))
            Obtain->OnClicked.AddUniqueDynamic(this,&ThisClass::ObtainElysia);
        if(auto* Close=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_Close"))))
            Close->OnClicked.AddUniqueDynamic(this,&ThisClass::ClosePanels);
    }
    if(DrawPanel)if(auto* Next=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_NextHero"))))Next->OnClicked.AddUniqueDynamic(this,&ThisClass::NextShopCard);
    for(const TCHAR* Name:{TEXT("Button_CloseStagePreview"),TEXT("Button_CloseHeroDetails")})
     if(auto* B=Cast<UButton>(GetWidgetFromName(Name)))
     {if(FString(Name)==TEXT("Button_CloseHeroDetails"))B->OnClicked.AddUniqueDynamic(this,&ThisClass::CloseHeroDetails);else B->OnClicked.AddUniqueDynamic(this,&ThisClass::ClosePanels);}
    SetDragActive(false);
    // Theme fonts are applied after the authored WBP tree is fully constructed.
    // Explicit SetFont calls made by information widgets later in the frame win over it.
    EGApplyTypography(this);
}
void UEGMainHUD::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    if(bBoardPresentation)
    {
        const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
        if(Mode&&Mode->RunDefinition&&Mode->RunDefinition->bSevenStageRules)
        {
            if(auto* OldHeader=GetWidgetFromName(TEXT("ReferenceRoundHeader")))OldHeader->SetVisibility(ESlateVisibility::Collapsed);
            else for(const TCHAR* Name:{TEXT("Text_Round"),TEXT("Text_Corruption")})
                if(auto* OldText=GetWidgetFromName(Name))OldText->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if(bBoardPresentation)UpdateShopTransition(DeltaSeconds);
    if(bBoardPresentation)UpdateStageTransition(DeltaSeconds);
    if(bBoardPresentation)UpdateFeedbackMotion(DeltaSeconds);
    if(bBoardPresentation&&HandPanel)
    {
        const auto* C=Cast<UEGOverlayWidgetController>(GetWidgetController());
        const float Target=C&&C->GetMatchView().Phase==EEGMatchPhase::Combat?1.f:0.f;
        HandCombatBlend=FMath::Clamp(HandCombatBlend+FMath::Clamp(Target-HandCombatBlend,-DeltaSeconds*5.f,DeltaSeconds*5.f),0.f,1.f);
        HandPanel->SetRenderTransformPivot(FVector2D(.5f,1.f));
        const float HandScale=FMath::Lerp(1.f,.67f,HandCombatBlend);
        HandPanel->SetRenderScale(FVector2D(HandScale));
        if(auto* PanelSlot=Cast<UCanvasPanelSlot>(HandPanel->Slot))PanelSlot->SetPosition(FVector2D(0.f,-EGPearlGold::HandBottom));
        if(Text_HandCount)if(auto* CountSlot=Cast<UCanvasPanelSlot>(Text_HandCount->Slot))CountSlot->SetPosition(FVector2D(0.f,-EGPearlGold::HandBottom-EGPearlGold::HandCardHeight*HandScale-4.f));
        if(HoverSource.IsValid())ShowHandHover(HoverSource.Get());
    }
    if(auto* Reorder=ActiveHandReorder.Get();Reorder&&bHasReorderPointer)
    {
        const auto* C=Cast<UEGOverlayWidgetController>(GetWidgetController());
        if(!CanEditHand()||!C||C->GetHandOrderRevision()!=Reorder->OrderRevision)CancelHandReorder();
        else if(HandScrollBox)
        {
            const auto& ScrollGeometry=HandScrollBox->GetCachedGeometry();
            const FVector2D Size=ScrollGeometry.GetLocalSize();
            const FVector2D Point=ScrollGeometry.AbsoluteToLocal(ReorderPointer);
            if(Size.X>0.f&&Size.Y>0.f&&Point.Y>=0.f&&Point.Y<=Size.Y&&Point.X>=0.f&&Point.X<=Size.X)
            {
                const float Direction=Point.X<32.f?-1.f:Point.X>Size.X-32.f?1.f:0.f;
                if(Direction!=0.f)
                {
                    const float Current=HandScrollBox->GetScrollOffset();
                    const float End=FMath::Max(0.f,HandScrollBox->GetScrollOffsetOfEnd());
                    const float Next=FMath::Clamp(Current+Direction*320.f*DeltaSeconds,0.f,End);
                    if(!FMath::IsNearlyEqual(Current,Next))HandScrollBox->SetScrollOffset(Next);
                }
            }
            // The next Slate layout updates cached card geometry after scrolling;
            // recompute every tick even if the pointer stays stationary.
            UpdateHandReorder(ReorderPointer);
        }
    }
    if(CorruptionDeltaText&&CorruptionDeltaRemaining>0.f)
    {
        CorruptionDeltaRemaining=FMath::Max(0.f,CorruptionDeltaRemaining-DeltaSeconds);
        CorruptionDeltaText->SetRenderTranslation(FVector2D(0.f,-24.f*(1.f-CorruptionDeltaRemaining/1.2f)));
        CorruptionDeltaText->SetRenderOpacity(FMath::Min(1.f,CorruptionDeltaRemaining/.3f));
        if(CorruptionDeltaRemaining<=0.f)CorruptionDeltaText->SetVisibility(ESlateVisibility::Collapsed);
    }
    if(bBoardPresentation)UpdateSidebarMotion(DeltaSeconds);
    if(bBoardPresentation)LayoutInformationViews();
    if(bBattleStatisticsDirty)RefreshBattleStatistics();
    if(bBoardPresentation&&!FMath::IsNearlyEqual(HandLayoutViewportWidth,float(Geometry.GetLocalSize().X),.5f))
        RefreshHandLayout();
    if(!bBoardPresentation||!HasHeroSelection()||bDragActive||bDrawExpanded||bStageExpanded){InspectionRefreshElapsed=0;return;}
    InspectionRefreshElapsed+=DeltaSeconds;
    if(InspectionRefreshElapsed>=.12f){InspectionRefreshElapsed=0;RefreshInspection();}
}
void UEGMainHUD::NativeDestruct()
{
    if(auto* Menu=Cast<UEGBoardButton>(GetWidgetFromName(TEXT("Button_PauseMenu"))))
    {Menu->OnRequested.RemoveDynamic(this,&ThisClass::OnBoardAction);Menu->OnClicked.RemoveDynamic(Menu,&UEGBoardButton::Invoke);}
    if(auto* PC=GetOwningPlayer())if(auto* HUD=Cast<AEGHUD>(PC->GetHUD()))if(auto* Run=HUD->GetRunPanel())Run->SetStigmataSidebarVisible(true);
    if(AudioButton)AudioButton->OnClicked.RemoveDynamic(this,&ThisClass::ToggleAudioControls);
    if(MusicVolumeSlider)MusicVolumeSlider->OnValueChanged.RemoveDynamic(this,&ThisClass::MusicVolumeChanged);
    if(UISoundVolumeSlider)UISoundVolumeSlider->OnValueChanged.RemoveDynamic(this,&ThisClass::UISoundVolumeChanged);
    if(AudioPanel)AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
    CancelHandReorder();HideHandHover();bReorderTargetValid=false;if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);
    for(auto* Info:{CardPreviewInformation.Get(),UnitPinnedInformation.Get()})if(Info)Info->OnCloseRequested.RemoveDynamic(this,&ThisClass::CloseInformationViews);
    if(UnitPinnedInformation)UnitPinnedInformation->OnSellRequested.RemoveDynamic(this,&ThisClass::SellInspectedInformation);
    for(auto* Info:{CardPreviewInformation.Get(),UnitHoverInformation.Get(),UnitPinnedInformation.Get()})if(Info)Info->SetVisibility(ESlateVisibility::Collapsed);
    UnbindShopPlateHover();
    if(BoardRun.IsValid())BoardRun->OnCombatStatisticsChanged.RemoveDynamic(this,&ThisClass::MarkBattleStatisticsDirty);
    BoardRun.Reset();
    bBattleStatisticsDirty=false;InformationLayoutViewport=FVector2D(-1.f,-1.f);
    bUnitSidebarOpen=bStatisticsSidebarOpen=false;UnitSidebarReveal=StatisticsSidebarReveal=0.f;
    ClearInspectionBindings();
    // Drag-only presentation must not survive the widget: no stale drag flags,
    // return hint or upper-left card may leak into the next HUD instance.
    bShopOfferDrag=false;bShopContextDragPresentationActive=false;bShopWasExpandedOnDragStart=false;
    bShopGroupRectValid=false;ShopGroupMinLocal=ShopGroupMaxLocal=FVector2D::ZeroVector;
    ShopLayoutWidth=-1.f;bShopPlateInteractive=false;bShopLockPressed=false;bShopRefreshPressed=false;
    ShopDragSlot=INDEX_NONE;ShopDragBlend=0.f;ShopReveal=0.f;
    SellReveal=0.f;SellHover[0]=SellHover[1]=0.f;SellFeedbackZone=0;EnergyPulse=0.f;PresentedEnergy=MIN_int64;
    CorruptionDeltaRemaining=0.f;if(CorruptionDeltaText)CorruptionDeltaText->SetVisibility(ESlateVisibility::Collapsed);
    if(ShopReturnHint)ShopReturnHint->SetVisibility(ESlateVisibility::Collapsed);
    if(ShopDragInformation)ShopDragInformation->SetVisibility(ESlateVisibility::Collapsed);
    for(const TCHAR* Name:{TEXT("Button_CloseStagePreview"),TEXT("Button_CloseHeroDetails")})
     if(auto* B=Cast<UButton>(GetWidgetFromName(Name)))
     {B->OnClicked.RemoveDynamic(this,&ThisClass::CloseHeroDetails);B->OnClicked.RemoveDynamic(this,&ThisClass::ClosePanels);}
    for(const auto& B:PreviewButtons)if(B)B->OnSelected.RemoveDynamic(this,&ThisClass::SelectStagePreview);
    PreviewButtons.Reset();StagePreviews.Reset();StagePreviewSignature.Empty();
    if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))Controller->CancelActiveDrag();
    if(Button_ToggleDrawPanel)
    {
        Button_ToggleDrawPanel->OnClicked.RemoveDynamic(this,&ThisClass::ToggleDrawPanel);
        Button_ToggleDrawPanel->OnHovered.RemoveDynamic(this,&ThisClass::ShopHovered);
        Button_ToggleDrawPanel->OnUnhovered.RemoveDynamic(this,&ThisClass::ShopUnhovered);
    }
    if(Button_Stage)Button_Stage->OnClicked.RemoveDynamic(this,&ThisClass::ToggleStagePanel);
    if(Button_Battle)Button_Battle->OnClicked.RemoveDynamic(this,&ThisClass::RequestBattle);
    if(DrawPanel)
    {
        if(auto* Next=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_NextHero"))))Next->OnClicked.RemoveDynamic(this,&ThisClass::NextShopCard);
        if(auto* Obtain=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_ObtainElysia"))))
            Obtain->OnClicked.RemoveDynamic(this,&ThisClass::ObtainElysia);
        if(auto* Close=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_Close"))))
            Close->OnClicked.RemoveDynamic(this,&ThisClass::ClosePanels);
    }
    bDrawExpanded=false;bStageExpanded=false;SetDragActive(false);
    Super::NativeDestruct();
}
void UEGMainHUD::BindWidgetControllerCallbacks()
{
    auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());if(!Controller)return;
    Controller->OnHandViewChanged.AddUniqueDynamic(this,&ThisClass::ApplyHandView);
    Controller->OnMatchViewChanged.AddUniqueDynamic(this,&ThisClass::ApplyMatchView);
    Controller->OnPlacementHintChanged.AddUniqueDynamic(this,&ThisClass::ApplyPlacementHint);
    Controller->OnCorruptionViewChanged.AddUniqueDynamic(this,&ThisClass::ShowCorruptionDelta);
    ApplyHandView(Controller->GetHandView());ApplyMatchView(Controller->GetMatchView());
    ApplyPlacementHint(FText::FromString(TEXT("拖动手牌至近端三行部署  Esc 取消")));
}
void UEGMainHUD::UnbindWidgetControllerCallbacks()
{
    HideHandHover();
    if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))
    {
        Controller->OnHandViewChanged.RemoveDynamic(this,&ThisClass::ApplyHandView);
        Controller->OnMatchViewChanged.RemoveDynamic(this,&ThisClass::ApplyMatchView);
        Controller->OnPlacementHintChanged.RemoveDynamic(this,&ThisClass::ApplyPlacementHint);
        Controller->OnCorruptionViewChanged.RemoveDynamic(this,&ThisClass::ShowCorruptionDelta);
    }
    for(auto& Card:Cards)if(Card.Value)Card.Value->SetWidgetController(nullptr);
    if(HandScrollBox)HandScrollBox->ClearChildren();if(ReserveScrollBox)ReserveScrollBox->ClearChildren();Cards.Reset();AppliedCards.Reset();LaidOutStageCount=INDEX_NONE;
}
void UEGMainHUD::ApplyHandView(const TArray<FEGHandCardView>& Hand)
{
    if(!HandScrollBox||!CardWidgetClass)return;
    int32 ArrivalIndex=0;
    ReserveIds.Reset();for(const auto& Card:Hand)if(Card.bReserve)ReserveIds.Add(Card.InstanceId);
    TSet<FGuid> Present;for(const auto& Card:Hand)Present.Add(Card.InstanceId);
    for(auto It=Cards.CreateIterator();It;++It)
        if(!Present.Contains(It.Key()))
        {if(It.Value()){HideHandHover(It.Value());It.Value()->SetWidgetController(nullptr);It.Value()->RemoveFromParent();}AppliedCards.Remove(It.Key());It.RemoveCurrent();}
    for(const auto& Card:Hand)
    {
        UEGHandCard* View=FindCardWidget(Card.InstanceId);
        if(!View)
        {
            const auto* CharacterClass=CharacterCardClasses.Find(FEGCardPresentation::CharacterKey(Card.Artwork));
            View=CreateWidget<UEGHandCard>(GetOwningPlayer(),CharacterClass?*CharacterClass:CardWidgetClass);if(!View)continue;
            View->SetWidgetController(GetWidgetController());Cards.Add(Card.InstanceId,View);
            if(auto* HandSlot=Cast<UScrollBoxSlot>((!bBoardPresentation&&Card.bReserve&&ReserveScrollBox?ReserveScrollBox.Get():HandScrollBox.Get())->AddChild(View)))
            {HandSlot->SetPadding(FMargin(5,2,5,2));HandSlot->SetVerticalAlignment(VAlign_Center);}
            if(bBoardPresentation)View->PlayAcquisition(FMath::Min(ArrivalIndex++*.045f,.18f));
        }
        const auto* Previous=AppliedCards.Find(Card.InstanceId);
        if(HoverSource.Get()==View&&(!Previous||Previous->CardId!=Card.CardId||Previous->bReserve!=Card.bReserve||Previous->bOverflow!=Card.bOverflow||
           Previous->bOverflowProtected!=Card.bOverflowProtected||Previous->Level!=Card.Level||Previous->HeroDefinition!=Card.HeroDefinition||
           Previous->Artwork!=Card.Artwork||!Previous->Identity.EqualTo(Card.Identity)||!Previous->DisplayName.EqualTo(Card.DisplayName)))
            bHoverCardDirty=true;
        View->Configure(Card.InstanceId,Card.Artwork?Card.Artwork.Get():CardArtwork.Get());
        View->SetDefinitionAppearance(Card.HeroDefinition.Get(),Card.Level);
        View->SetOverflow(Card.bOverflow,Card.bOverflowProtected);
        if(!Previous||!Previous->DisplayName.EqualTo(Card.DisplayName))View->SetToolTipText(Card.DisplayName);
        if(!Previous||!Previous->Identity.EqualTo(Card.Identity))View->SetIdentity(Card.Identity);
        if(auto* Size=Cast<USizeBox>(View->GetWidgetFromName(TEXT("CardSize")))){Size->SetWidthOverride(Card.bReserve?58:72);Size->SetHeightOverride(Card.bReserve?87:108);}
        AppliedCards.Add(Card.InstanceId,Card);
    }
    if(Text_HandCount)Text_HandCount->SetText(FText::FromString(FString::Printf(TEXT("手牌 %d  备战 %d"),Hand.Num()-ReserveIds.Num(),ReserveIds.Num())));
    if(HandViewport)HandViewport->SetWidthOverride(FMath::Min(960.f,(Hand.Num()-ReserveIds.Num())*82.f));
    if(bBoardPresentation)
    {
      bool bNeedsOrder=HandScrollBox->GetChildrenCount()!=Hand.Num();
      if(!bNeedsOrder)for(int32 I=0;I<Hand.Num();++I)if(HandScrollBox->GetChildAt(I)!=FindCardWidget(Hand[I].InstanceId)){bNeedsOrder=true;break;}
      if(bNeedsOrder)
      {
        // Clearing the scroll children can destruct/rebuild the hovered widget.
        HideHandHover();
        const float Offset=HandScrollBox->GetScrollOffset();
        HandScrollBox->ClearChildren();
        for(const auto& Card:Hand)if(auto* View=FindCardWidget(Card.InstanceId))HandScrollBox->AddChild(View);
        HandScrollBox->SetScrollOffset(Offset);
      }
      int32 Index=0;const int32 Count=Hand.Num();
      for(const auto& Card:Hand)if(auto* View=FindCardWidget(Card.InstanceId))
      {
        if(auto* Size=Cast<USizeBox>(View->GetWidgetFromName(TEXT("CardSize")))){Size->SetWidthOverride(EGPearlGold::HandCardWidth);Size->SetHeightOverride(EGPearlGold::HandCardHeight);Size->SetVisibility(ESlateVisibility::Visible);}
        if(auto* CardSlot=Cast<UScrollBoxSlot>(View->Slot)){CardSlot->SetVerticalAlignment(VAlign_Bottom);CardSlot->SetHorizontalAlignment(HAlign_Left);}
        // The manager selects removable cards after protecting special heroes;
        // an index alone cannot predict which card will be recycled.
        const bool bLayeredCard=Card.HeroDefinition&&!Card.HeroDefinition->CardFrameTexture.IsNull();
        View->SetIdentity(!bLayeredCard?(Card.bReserve?Card.Identity:(Card.Level>1?FText::FromString(FString::Printf(TEXT("Lv.%d"),Card.Level)):FText::GetEmpty())):FText::GetEmpty());
        if(View->IdentityText)if(auto* IdentitySlot=Cast<UOverlaySlot>(View->IdentityText->Slot))
        {IdentitySlot->SetHorizontalAlignment(HAlign_Right);IdentitySlot->SetVerticalAlignment(VAlign_Top);IdentitySlot->SetPadding(FMargin(0.f,28.f,4.f,0.f));}
        const FText CardName=Card.HeroDefinition&&!Card.HeroDefinition->DisplayName.IsEmpty()?Card.HeroDefinition->DisplayName:Card.Identity;
        View->SetCompactPresentation(false,CardName);
        View->SetToolTipText(FText::GetEmpty());++Index;
      }
      const float HandWidth=EGPearlGold::HandWidthForCount(Count>0?Count:(bDragActive?1:0));
      if(HandPanel)HandPanel->SetVisibility(Count>0||bDragActive?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
      if(HandViewport)HandViewport->SetWidthOverride(HandWidth);
      if(HandPanel)if(auto* PanelSlot=Cast<UCanvasPanelSlot>(HandPanel->Slot))PanelSlot->SetSize(FVector2D(HandWidth,EGPearlGold::HandPanelHeight));
      if(Text_HandCount)
      {
        int32 Excess=0,Protected=0;for(const auto& Card:Hand){Excess+=Card.bOverflow?1:0;Protected+=Card.bOverflowProtected?1:0;}
        const auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
        FString CountLabel=Excess>0?FString::Printf(TEXT("超额 %d  开战返还 %lld"),Excess,Controller?Controller->GetOverflowSaleValue():0):FString::Printf(TEXT("%d / 10"),Count);
        if(Protected>10)CountLabel+=TEXT("  保护例外，不会出售");
        Text_HandCount->SetText(FText::FromString(CountLabel));
        Text_HandCount->SetVisibility(Count>0||bDragActive?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* CountSlot=Cast<UCanvasPanelSlot>(Text_HandCount->Slot))
        {CountSlot->SetAnchors(FAnchors(.5f,1.f));CountSlot->SetAlignment(FVector2D(.5f,1.f));CountSlot->SetPosition(FVector2D(0.f,-EGPearlGold::HandBottom-EGPearlGold::HandCardHeight-4.f));CountSlot->SetSize(FVector2D(430.f,24.f));CountSlot->SetZOrder(23);}
      }
      RefreshHandLayout();
    }
}
void UEGMainHUD::RefreshHandLayout()
{
    if(!bBoardPresentation||!HandScrollBox)return;
    const float Width=GetCachedGeometry().GetLocalSize().X;
    if(Width<=0)return;
    HandLayoutViewportWidth=Width;
    // Ten cards fit without scrolling; additional cards retain that spacing.
    // At the 1920-wide design size, ten 140-wide cards plus gaps and hover
    // padding fit between the bottom-corner controls without overlapping.
    const float Available=FMath::Min(EGPearlGold::HandMaxWidth,FMath::Max(300.f,Width-456.f));
    const float Scale=1.f;
    const float CardW=EGPearlGold::HandCardWidth*Scale;
    const float CardH=EGPearlGold::HandCardHeight*Scale;
    const float Gap=EGPearlGold::HandCardGap*Scale;
    const float PanelH=EGPearlGold::HandPanelHeight*Scale;
    const int32 Count=HandScrollBox->GetChildrenCount();
    const int32 FitCount=FMath::Clamp(Count,1,10);
    const float Step=FitCount>1?FMath::Min(CardW+Gap,FMath::Max(1.f,(Available-2.f*EGPearlGold::HandEdgePadding*Scale-CardW)/(FitCount-1))):CardW;
    const float InterGap=Step-CardW;
    for(int32 Index=0;Index<Count;++Index)if(auto* Card=Cast<UEGHandCard>(HandScrollBox->GetChildAt(Index)))
    {
        if(auto* Size=Cast<USizeBox>(Card->GetWidgetFromName(TEXT("CardSize")))){Size->SetWidthOverride(CardW);Size->SetHeightOverride(CardH);}
        if(auto* CardSlot=Cast<UScrollBoxSlot>(Card->Slot))
        {
            // The scroll box clips its contents, so the first and last cards
            // need room to grow while the gaps between cards stay unchanged.
            const float Edge=EGPearlGold::HandEdgePadding*Scale;
            CardSlot->SetPadding(FMargin(Index==0?Edge:InterGap*.5f,EGPearlGold::HandTopPadding*Scale,Index==Count-1?Edge:InterGap*.5f,0));
            CardSlot->SetHorizontalAlignment(HAlign_Left);
            CardSlot->SetVerticalAlignment(VAlign_Bottom);
        }
    }
    const int32 LayoutCount=Count>0?Count:(bDragActive?1:0);
    const float PanelW=FMath::Min(Available,LayoutCount?CardW+(LayoutCount-1)*Step+2.f*EGPearlGold::HandEdgePadding*Scale:0.f);
    if(HandViewport){HandViewport->SetWidthOverride(PanelW);HandViewport->SetHeightOverride(PanelH);}
    if(HandPanel)if(auto* PanelSlot=Cast<UCanvasPanelSlot>(HandPanel->Slot)){PanelSlot->SetSize(FVector2D(PanelW,PanelH));PanelSlot->SetAnchors(FAnchors(.5f,1.f));PanelSlot->SetAlignment(FVector2D(.5f,1.f));PanelSlot->SetPosition(FVector2D(0.f,-EGPearlGold::HandBottom));}
}
void UEGMainHUD::ShowCorruptionDelta(int32 Current,int32 Delta)
{
    if(!bBoardPresentation||Delta==0)return;
    if(!CorruptionDeltaText)
    {
        auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
        if(!Root)return;
        CorruptionDeltaText=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CorruptionDeltaFeedback"));
        CorruptionDeltaText->SetFont(FEGVisualStyle::PearlFont(26));
        CorruptionDeltaText->SetJustification(ETextJustify::Center);
        CorruptionDeltaText->SetShadowColorAndOpacity(FLinearColor(0,0,0,.8f));
        CorruptionDeltaText->SetShadowOffset(FVector2D(1,2));
        auto* DeltaSlot=Root->AddChildToCanvas(CorruptionDeltaText);
        DeltaSlot->SetAnchors(FAnchors(.5f,0.f));DeltaSlot->SetAlignment(FVector2D(.5f,0.f));
        DeltaSlot->SetPosition(FVector2D(245.f,44.f));DeltaSlot->SetSize(FVector2D(120.f,44.f));DeltaSlot->SetZOrder(100);
    }
    CorruptionDeltaText->SetText(FText::FromString(FString::Printf(TEXT("%+d"),Delta)));
    CorruptionDeltaText->SetColorAndOpacity(FSlateColor(Delta<0?FLinearColor(.2f,1.f,.38f):FLinearColor(1.f,.2f,.25f)));
    CorruptionDeltaText->SetRenderTranslation(FVector2D::ZeroVector);
    CorruptionDeltaText->SetRenderOpacity(1.f);
    CorruptionDeltaText->SetVisibility(ESlateVisibility::HitTestInvisible);
    CorruptionDeltaRemaining=1.2f;
}
void UEGMainHUD::ApplyMatchView(const FEGMatchView& Match)
{
    if(Match.Phase!=EEGMatchPhase::Preparation)CancelHandReorder();
    if(LastMatchStage>Match.StageNumber)SelectedPreviewStage=0;LastMatchStage=Match.StageNumber;
    if(HasHeroSelection())RebindInspectionSource();
    if(bStageExpanded)RefreshStagePreviews();
    if(auto* Summary=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_HeroSummary"))))Summary->SetText(Match.HeroSummary);
    if(Button_Battle&&Match.bIsRun)Button_Battle->SetVisibility(ESlateVisibility::Collapsed);
    if(Match.bIsRun)if(auto* Location=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_Location"))))Location->SetText(Match.RunName);
    if(auto* Gold=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_HUDGold"))))
        Gold->SetText(FText::AsNumber(Match.Gold));
    if(PresentedEnergy!=Match.Gold){if(PresentedEnergy!=MIN_int64)EnergyPulse=.24f;PresentedEnergy=Match.Gold;}
    if(auto* State=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_MatchState"))))
        State->SetText(FText::FromString(!Match.bHasMatch?TEXT("自由部署"):
            Match.Phase==EEGMatchPhase::Preparation?TEXT("准备阶段"):TEXT("对局进行中")));
    static const TCHAR* Roman[]={TEXT("I"),TEXT("II"),TEXT("III"),TEXT("IV"),TEXT("V"),TEXT("VI"),TEXT("VII")};
    const FString StageName=Match.StageNumber>=1&&Match.StageNumber<=7?FString(Roman[Match.StageNumber-1]):FString::FromInt(Match.StageNumber);
    if(Text_Round)Text_Round->SetText(FText::FromString(bBoardPresentation?FString::Printf(TEXT("第 %d 关"),Match.StageNumber):Match.bHasMatch?
        FString::Printf(TEXT("第 %s 关  第 %d 回合"),*StageName,Match.RoundNumber):
        FString::Printf(TEXT("第 %d 回合"),Match.RoundNumber)));
    if(Button_Battle)
    {
        Button_Battle->SetIsEnabled(true); // B reference keeps the CTA bright; RequestCombat validates the action
        Button_Battle->SetToolTipText(FText::FromString(Match.bCanRequestCombat?TEXT("开始作战"):TEXT("当前场景尚未连接可用的战斗流程")));
    }
    const int32 Count=FMath::Max(1,Match.StageCount);
    if(auto* Nodes=Cast<UCanvasPanel>(GetWidgetFromName(TEXT("StageNodes"))))
    for(int32 I=8;I<=Count;++I)
    {
        const FString Suffix=FString::FromInt(I);
        auto Add=[&](UWidget* W,FVector2D Size,FVector2D Pos){auto* S=Nodes->AddChildToCanvas(W);S->SetSize(Size);S->SetPosition(Pos);S->SetAlignment(FVector2D(.5,.5));W->SetVisibility(ESlateVisibility::HitTestInvisible);};
        // Existing authored trees contain seven nodes and six connectors. Extend
        // both, reusing the authored brush while retaining the same slot geometry.
        const FName LinkName(*FString::Printf(TEXT("StageLink_%d"),I-2));
        if(!GetWidgetFromName(LinkName))
        {
            auto* Link=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),LinkName);
            if(auto* Template=Cast<UImage>(GetWidgetFromName(TEXT("StageLink_0"))))Link->SetBrush(Template->GetBrush());
            auto* LinkSlot=Nodes->AddChildToCanvas(Link);LinkSlot->SetSize(FVector2D(1,2));LinkSlot->SetZOrder(-1);
            Link->SetVisibility(ESlateVisibility::HitTestInvisible);
        }
        if(GetWidgetFromName(FName(*(TEXT("StageRing_")+Suffix))))continue;
        auto* Ring=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*(TEXT("StageRing_")+Suffix)));
        if(auto* Template=Cast<UImage>(GetWidgetFromName(TEXT("StageRing_1"))))Ring->SetBrush(Template->GetBrush());Add(Ring,FVector2D(32,32),FVector2D(0,39));
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*(TEXT("StageLabel_")+Suffix)));
        Label->SetText(FText::AsNumber(I));Label->SetJustification(ETextJustify::Center);Label->SetFont(FEGVisualStyle::NumeralFont(16));Add(Label,FVector2D(48,24),FVector2D(0,74));
        auto* Marker=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*(TEXT("StageCurrent_")+Suffix)));
        if(auto* Template=Cast<UImage>(GetWidgetFromName(TEXT("StageCurrent_1"))))Marker->SetBrush(Template->GetBrush());Add(Marker,FVector2D(16,20),FVector2D(0,12));
    }
    const float Spacing=Count>1?692.f/(Count-1):0.f;
    const bool bLayoutChanged=LaidOutStageCount!=Count;
    // Also visit previously created dynamic children when a run shrinks (e.g. 9 -> 2).
    for(int32 I=1;I<=FMath::Max(7,FMath::Max(Count,LaidOutStageCount));++I)
    {
        const float X=74.f+(I-1)*Spacing;
        for(const TCHAR* Prefix:{TEXT("StageRing_"),TEXT("StageLabel_"),TEXT("StageCurrent_")})
            if(auto* Node=GetWidgetFromName(FName(*FString::Printf(TEXT("%s%d"),Prefix,I))))
            {
                Node->SetVisibility(I<=Count?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
                if(bLayoutChanged)if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Node->Slot))CanvasSlot->SetPosition(FVector2D(X,CanvasSlot->GetPosition().Y));
            }
        if(auto* Link=GetWidgetFromName(FName(*FString::Printf(TEXT("StageLink_%d"),I-1))))
        {
            Link->SetVisibility(I<Count?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            if(bLayoutChanged)if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Link->Slot)){CanvasSlot->SetPosition(FVector2D(X+22,39));CanvasSlot->SetSize(FVector2D(FMath::Max(0.f,Spacing-44),2));}
        }
        if(auto* Marker=GetWidgetFromName(FName(*FString::Printf(TEXT("StageCurrent_%d"),I))))
            Marker->SetVisibility(I<=Count&&Match.bHasMatch&&Match.StageNumber==I?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        if(auto* Ring=Cast<UImage>(GetWidgetFromName(FName(*FString::Printf(TEXT("StageRing_%d"),I)))))
        {
            const bool bDanger=Match.StageTypes.IsValidIndex(I-1)?Match.StageTypes[I-1]!=EEGStageType::Normal:I==3||I==6;
            Ring->SetColorAndOpacity(bDanger?FLinearColor(1,.22f,.32f,1):
                Match.bHasMatch&&I<Match.StageNumber?FLinearColor(.65f,.92f,.83f,1):FLinearColor::White);
        }
    }
    if(bLayoutChanged)if(auto* Boss=GetWidgetFromName(TEXT("StageBoss")))if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Boss->Slot))CanvasSlot->SetPosition(FVector2D(74+(Count-1)*Spacing,10));
    LaidOutStageCount=Count;
    if(!DrawPanel)return;
    if(bBoardPresentation){RefreshBoardView();return;}
    if(auto* Gold=Cast<UTextBlock>(DrawPanel->GetWidgetFromName(TEXT("Text_Gold"))))
        Gold->SetText(FText::FromString(FString::Printf(TEXT("崩坏能  %lld"),Match.Gold)));
    auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
    const auto* Definition=Controller?Controller->GetAvailableCardDefinition():nullptr;
    if(auto* Image=Cast<UImage>(DrawPanel->GetWidgetFromName(TEXT("DrawCardArtwork"))))
        Image->SetBrushFromTexture(FEGCardPresentation::ResolveArtwork(Definition&&Definition->Artwork?Definition->Artwork.Get():CardArtwork.Get()),false);
    if(auto* Title=Cast<UTextBlock>(DrawPanel->GetWidgetFromName(TEXT("Text_DrawTitle"))))
        Title->SetText(Definition?Definition->DisplayName:FText::FromString(TEXT("往世的涟漪")));
    if(auto* Help=Cast<UTextBlock>(DrawPanel->GetWidgetFromName(TEXT("Text_DrawHelp"))))
        Help->SetText(FText::FromString(FString::Printf(TEXT("购买 %d 崩坏能  同名卡拖入棋盘合成 +%d 级"),Match.PurchasePrice,Match.SameNameLevelGain)));
    if(auto* Label=Cast<UTextBlock>(DrawPanel->GetWidgetFromName(TEXT("Text_Obtain"))))
        Label->SetText(FText::FromString(FString::Printf(TEXT("购买卡牌  %d崩坏能"),Match.PurchasePrice)));
    if(auto* Obtain=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_ObtainElysia"))))
        Obtain->SetIsEnabled(Match.bCanPurchase);

}
void UEGMainHUD::ApplyPlacementHint(const FText& Hint)
{
    if(bBoardPresentation&&bDragActive&&DragFeedback)DragFeedback->SetHint(Hint);
    if(auto* Label=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_PlacementHint"))))Label->SetText(Hint);
    if(auto* Dock=GetWidgetFromName(TEXT("HintDock")))Dock->SetRenderOpacity(bDragActive?1.f:.78f);
}
UEGHandCard* UEGMainHUD::FindCardWidget(FGuid Id) const
{const auto* Entry=Cards.Find(Id);return Entry?Entry->Get():nullptr;}
void UEGMainHUD::RefreshPanelVisibility()
{
    if(bDrawExpanded||bStageExpanded)
        if(auto* Catalogue=GetWidgetFromName(TEXT("CompanionCatalogue")))Catalogue->SetVisibility(ESlateVisibility::Collapsed);
    if(bDrawExpanded)InspectUnit(nullptr);
    if(bBoardPresentation)UpdateShopTransition(0.f);
    else if(DrawPanel){DrawPanel->SetVisibility(bDrawExpanded?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);if(!bDrawExpanded)DrawPanel->SetRenderScale(FVector2D(1.f));}
    // The shop replaces the round title in the same top-center slot; never two titles.
    if(auto* Banner=GetWidgetFromName(TEXT("BoardThemeBanner")))
        if(auto* BannerSlot=Cast<UCanvasPanelSlot>(Banner->Slot))
            BannerSlot->SetSize(bDrawExpanded?EGPearlGold::BannerShop.Size():EGPearlGold::BannerNormal.Size());
    for(const TCHAR* Name:{TEXT("Text_Round")})
        if(auto* W=GetWidgetFromName(Name))W->SetVisibility(!bBoardPresentation&&bDrawExpanded?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    for(const TCHAR* Name:{TEXT("Text_ShopTitle")})
        if(auto* W=GetWidgetFromName(Name))W->SetVisibility(!bBoardPresentation&&bDrawExpanded?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(Button_Stage)Button_Stage->SetVisibility(!bBoardPresentation&&bDrawExpanded?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    if(StagePanel)StagePanel->SetVisibility(ESlateVisibility::Collapsed);
    if(bBoardPresentation)UpdateStageTransition(0.f);
    else if(auto* Preview=GetWidgetFromName(TEXT("StagePreviewPanel")))Preview->SetVisibility(bStageExpanded?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    RefreshInspection();
    if(ModalBlocker)ModalBlocker->SetVisibility(!bBoardPresentation&&(bDrawExpanded||bStageExpanded)?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(Text_DrawToggle)Text_DrawToggle->SetText(bBoardPresentation?FText::GetEmpty():FText::FromString(TEXT("商店")));
    if(auto* Host=GetOwningPlayer()?Cast<AEGHUD>(GetOwningPlayer()->GetHUD()):nullptr)if(auto* Run=Host->GetRunPanel())Run->SetContextPanelsOpen(bDrawExpanded||bStageExpanded);
}
void UEGMainHUD::ToggleDrawPanel()
{
    if(bDragActive)return;
    HideCardInformation(true);
    if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))Controller->CancelActiveDrag();
    if(!bDrawExpanded&&StageReveal>0.f)StageReveal=0.f;
    bDrawExpanded=!bDrawExpanded;bStageExpanded=false;RefreshPanelVisibility();
}
float UEGMainHUD::ShopOpenTranslationY() const
{
    const float P=ShopReveal*ShopReveal*(3.f-2.f*ShopReveal);
    return EGPearlGold::ShopOpenOffsetY*(1.f-P);
}
void UEGMainHUD::UpdateStageTransition(float DeltaSeconds)
{
    if(!bBoardPresentation)return;
    const float ViewWidth=GetCachedGeometry().GetLocalSize().X;
    const float Width=FMath::Max(160.f,FMath::Min(1080.f,ViewWidth-48.f));
    const bool bLayoutChanged=!FMath::IsNearlyEqual(StageLayoutWidth,Width,.5f);
    if(ViewWidth>0.f&&bLayoutChanged)
    {
        const float ClockFit=FMath::Min(1.f,FMath::Max(.1f,(ViewWidth-24.f)/660.f));
        if(Button_Stage)if(auto* ButtonSlot=Cast<UCanvasPanelSlot>(Button_Stage->Slot))
        {
            ButtonSlot->SetAnchors(FAnchors(.5f,0.f));ButtonSlot->SetAlignment(FVector2D::ZeroVector);
            ButtonSlot->SetPosition(FVector2D(-184.f*ClockFit,4.f+14.f*ClockFit));ButtonSlot->SetSize(FVector2D(144.f*ClockFit,32.f*ClockFit));
        }
    }
    auto* Preview=GetWidgetFromName(TEXT("StagePreviewPanel"));if(!Preview)return;
    const float Target=bStageExpanded?1.f:0.f;
    const float Duration=Target>StageReveal?.18f:.16f;
    StageReveal=FMath::Clamp(StageReveal+FMath::Clamp(Target-StageReveal,-DeltaSeconds/Duration,DeltaSeconds/Duration),0.f,1.f);
    const float Smooth=StageReveal*StageReveal*(3.f-2.f*StageReveal);
    Preview->SetVisibility(bStageExpanded?ESlateVisibility::Visible:StageReveal>0.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    Preview->SetRenderOpacity(Smooth);Preview->SetRenderTranslation(FVector2D(0.f,-48.f*(1.f-Smooth)));
    Preview->SetIsEnabled(bStageExpanded&&StageReveal>=.999f);
    if(ViewWidth<=0.f||!bLayoutChanged)return;
    if(auto* PreviewSlot=Cast<UCanvasPanelSlot>(Preview->Slot))
    {
        PreviewSlot->SetAnchors(FAnchors(0.f,0.f,1.f,0.f));PreviewSlot->SetAlignment(FVector2D::ZeroVector);
        // Keep fixed top-bar controls and statistics above the backdrop.
        PreviewSlot->SetOffsets(FMargin(0.f,0.f,0.f,300.f));PreviewSlot->SetZOrder(28);
    }
    if(auto* PreviewBorder=Cast<UBorder>(Preview))
    {
        // The authored WBP wraps this panel's canvas in a glass blur. Remove
        // only that wrapper on this runtime instance, including its low-quality
        // fallback brush, so the gradient can truly fade to clear board pixels.
        if(auto* Glass=Cast<UBackgroundBlur>(PreviewBorder->GetContent()))
            if(auto* Canvas=Cast<UCanvasPanel>(Glass->GetContent());Canvas&&Canvas->GetFName()==TEXT("StagePreviewCanvas"))
            {Canvas->RemoveFromParent();PreviewBorder->SetContent(Canvas);}
        FSlateBrush Empty;Empty.DrawAs=ESlateBrushDrawType::NoDrawType;PreviewBorder->SetBrush(Empty);PreviewBorder->SetPadding(FMargin(0));
    }
    if(auto* PreviewCanvas=Cast<UCanvasPanel>(GetWidgetFromName(TEXT("StagePreviewCanvas"))))
    {
        auto* Gradient=Cast<UEGGradientSurface>(GetWidgetFromName(TEXT("StagePreviewGradient")));
        if(!Gradient)
        {
            Gradient=WidgetTree->ConstructWidget<UEGGradientSurface>(UEGGradientSurface::StaticClass(),TEXT("StagePreviewGradient"));
            Gradient->SetVisibility(ESlateVisibility::HitTestInvisible);auto* GradientSlot=PreviewCanvas->AddChildToCanvas(Gradient);
            GradientSlot->SetAnchors(FAnchors(0.f,0.f,1.f,1.f));GradientSlot->SetOffsets(FMargin(0));GradientSlot->SetZOrder(-1);
        }
        Gradient->SetGradient(FLinearColor(0.f,0.f,0.f,.88f),FLinearColor::Transparent);
    }
    for(const TCHAR* Name:{TEXT("Text_StagePreviewTitle"),TEXT("Button_CloseStagePreview"),TEXT("Button_CloseStagePreviewGlassBlur"),TEXT("Text_StagePreviewDetail"),TEXT("StagePreviewDetailScroll")})
        if(auto* Old=GetWidgetFromName(Name))Old->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("StagePreviewScroll"))))
    {
        Scroll->SetOrientation(Orient_Horizontal);
        Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        if(auto* ScrollSlot=Cast<UCanvasPanelSlot>(Scroll->Slot))
        {ScrollSlot->SetAnchors(FAnchors(.5f,0.f));ScrollSlot->SetAlignment(FVector2D(.5f,0.f));ScrollSlot->SetPosition(FVector2D(0.f,58.f));ScrollSlot->SetSize(FVector2D(Width-40.f,226.f));}
    }
    StageLayoutWidth=Width;if(bStageExpanded)RefreshStagePreviews();
}
void UEGMainHUD::UpdateShopTransition(float DeltaSeconds)
{
    if(!DrawPanel)return;
    const FVector2D View=GetCachedGeometry().GetLocalSize();
    const float ViewWidth=View.X;
    const float PreviousReveal=ShopReveal;
    const float PreviousCompact=ShopDragBlend;
    // Opening .18s and closing .16s share one channel; the compact channel is
    // independent. An interrupted fade keeps running from its current progress.
    const float RevealStep=DeltaSeconds/FMath::Max(.001f,bDrawExpanded?EGPearlGold::ShopOpenSeconds:EGPearlGold::ShopCloseSeconds);
    const float RevealTarget=bDrawExpanded?1.f:0.f;
    ShopReveal=FMath::Clamp(ShopReveal+FMath::Clamp(RevealTarget-ShopReveal,-RevealStep,RevealStep),0.f,1.f);
    const float CompactStep=DeltaSeconds/FMath::Max(.001f,EGPearlGold::ShopCompactSeconds);
    const float CompactTarget=bShopContextDragPresentationActive?1.f:0.f;
    ShopDragBlend=FMath::Clamp(ShopDragBlend+FMath::Clamp(CompactTarget-ShopDragBlend,-CompactStep,CompactStep),0.f,1.f);
    // A resize/DPI change can leave both channel values static while the measured
    // layout still has to move, so the viewport width is part of the early-out.
    const bool bLayoutChanged=!FMath::IsNearlyEqual(ViewWidth,ShopLayoutWidth,.5f);
    if(DeltaSeconds>0.f&&!bLayoutChanged&&
        FMath::IsNearlyEqual(PreviousReveal,ShopReveal)&&FMath::IsNearlyEqual(PreviousCompact,ShopDragBlend))return;
    ShopLayoutWidth=ViewWidth;
    const float OpenP=ShopReveal*ShopReveal*(3.f-2.f*ShopReveal);
    const float D=ShopDragBlend*ShopDragBlend*(3.f-2.f*ShopDragBlend);
    const bool bVisible=bDrawExpanded||ShopReveal>0.f;
    DrawPanel->SetVisibility(bVisible?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
    // Closing stops accepting clicks on the first frame; opening becomes
    // interactive only when fully open. Dragging keeps the content inert.
    const bool bInteractive=bDrawExpanded&&ShopReveal>=.999f&&!bDragActive;
    DrawPanel->SetIsEnabled(bInteractive);
    if(bShopPlateInteractive!=bInteractive){bShopPlateInteractive=bInteractive;RefreshShopPlateTint();}
    // Open channel: translate Y=-48 -> 0 and alpha 0 -> 1 only. No scale, no
    // midpoint pivot, no shared value applied to parent and child twice.
    DrawPanel->SetRenderTransformPivot(FVector2D(0,0));
    DrawPanel->SetRenderScale(FVector2D(1));
    DrawPanel->SetRenderTranslation(FVector2D(0,ShopOpenTranslationY()));
    DrawPanel->SetRenderOpacity(OpenP);
    bShopGroupRectValid=ViewWidth>0.f;
    if(bShopGroupRectValid)
    {
        const auto Layout=EGPearlGold::ShopCompactLayout(ViewWidth);
        auto ToLocal=[ViewWidth](const EGPearlGold::FLayoutRect& R)
        {
            return EGPearlGold::FLayoutRect{ViewWidth*.5f+(R.X-EGPearlGold::DesignWidth*.5f),R.Y,R.W,R.H};
        };
        // The return rectangle is the union of the actual animated child rects,
        // not an interpolated AABB. Every rect is viewport-local before conversion
        // so non-1920 design widths cannot receive the centring offset twice.
        EGPearlGold::FLayoutRect Union{0.f,0.f,0.f,0.f};bool bHaveUnion=false;
        auto Accumulate=[&](const EGPearlGold::FLayoutRect& R)
        {
            if(!bHaveUnion){Union=R;bHaveUnion=true;return;}
            const float MinX=FMath::Min(Union.X,R.X),MinY=FMath::Min(Union.Y,R.Y);
            const float MaxX=FMath::Max(Union.X+Union.W,R.X+R.W),MaxY=FMath::Max(Union.Y+Union.H,R.Y+R.H);
            Union=EGPearlGold::FLayoutRect{MinX,MinY,MaxX-MinX,MaxY-MinY};
        };
        for(int32 I=0;I<4;++I)
            if(auto* Card=DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopSlot_%d"),I))))
            {
                const auto Expanded=ToLocal(EGPearlGold::ShopCardRect(I));
                const EGPearlGold::FLayoutRect Compact{Layout.CardRowLeft+I*(Layout.CardWidth+Layout.CardGap),
                    Layout.CardTop,Layout.CardWidth,Layout.CardHeight};
                const auto Animated=EGPearlGold::LerpRect(Expanded,Compact,D);
                Accumulate(Animated);
                Card->SetRenderTransformPivot(FVector2D(0,0));
                Card->SetRenderScale(FVector2D(FMath::Lerp(1.f,Layout.CardScale,D)));
                Card->SetRenderTranslation(FVector2D(Animated.X-Expanded.X,Animated.Y-Expanded.Y));
            }
        // Controls keep the readable 112x44 plate at normal widths and shrink by
        // the same Fit as the cards on narrow viewports, so the rendered group and
        // the return bounds cannot diverge.
        const float ButtonScale=FMath::Lerp(1.f,Layout.Fit,D);
        const TArray<TPair<FName,EGPearlGold::FLayoutRect>> Controls={
            TPair<FName,EGPearlGold::FLayoutRect>(FName(TEXT("Button_ShopLock")),EGPearlGold::ShopLock),
            TPair<FName,EGPearlGold::FLayoutRect>(FName(TEXT("Button_RefreshShop")),EGPearlGold::ShopRefresh)};
        for(const auto& Pair:Controls)
            if(auto* Button=DrawPanel->GetWidgetFromName(Pair.Key))
            {
                const auto Expanded=ToLocal(Pair.Value);
                const float TargetX=Pair.Key==TEXT("Button_ShopLock")?Layout.LockLeft:Layout.RefreshLeft;
                const EGPearlGold::FLayoutRect Compact{TargetX,Layout.ButtonTop,Layout.ButtonWidth,Layout.ButtonHeight};
                const auto Animated=EGPearlGold::LerpRect(Expanded,Compact,D);
                Accumulate(Animated);
                Button->SetRenderTransformPivot(FVector2D(0,0));
                Button->SetRenderScale(FVector2D(ButtonScale));
                Button->SetRenderTranslation(FVector2D(Animated.X-Expanded.X,Animated.Y-Expanded.Y));
            }
        if(bHaveUnion)
        {
            ShopGroupMinLocal=FVector2D(Union.X,Union.Y);
            ShopGroupMaxLocal=FVector2D(Union.X+Union.W,Union.Y+Union.H);
        }
        if(auto* Surface=Cast<UEGGradientSurface>(DrawPanel->GetWidgetFromName(TEXT("ShopGradientSurface"))))
        {
            Surface->SetCompact(D);
            Surface->SetCompactGeometry(FVector2D(Layout.GroupWidth+2.f*EGPearlGold::ShopCompactMargin,
                Layout.HintTop+EGPearlGold::ShopCompactHintHeight+EGPearlGold::ShopCompactHintPadding));
            Surface->SetGradient(FLinearColor(0,0,0,FMath::Lerp(.88f,.32f,D)),FLinearColor::Transparent);
        }
        UpdateShopReturnHint(D,ViewWidth);
    }
    if(auto* ShopCanvas=DrawPanel->GetWidgetFromName(TEXT("ShopRoot")))
    {
        // Only the shop's actual controls may intercept pointer input. The canvas
        // can overlap the hand on short viewports while the shop is open.
        ShopCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        ShopCanvas->SetIsEnabled(!bDragActive);
    }
    for(const TCHAR* Name:{TEXT("ShopOrbGradient"),TEXT("BattleButtonGradient")})
        if(auto* ButtonSurface=Cast<UEGGradientSurface>(GetWidgetFromName(Name)))
            ButtonSurface->SetGradient(FMath::Lerp(EGPearlGold::Srgb(18,30,55,.94f),EGPearlGold::Srgb(128,150,188,.94f),OpenP),FMath::Lerp(EGPearlGold::Srgb(5,10,24,.98f),EGPearlGold::Srgb(46,67,105,.98f),OpenP));
    for(auto* Button:{Button_ToggleDrawPanel.Get(),Button_Battle.Get()})
        if(Button)if(auto* Art=Button->GetStyle().Normal.GetResourceObject();Art&&Art->GetPathName().Contains(TEXT("GeneratedControls20261024")))
            Button->SetBackgroundColor(FLinearColor(FMath::Lerp(.55f,1.f,OpenP),FMath::Lerp(.55f,1.f,OpenP),FMath::Lerp(.55f,1.f,OpenP),1));
}
void UEGMainHUD::ToggleStagePanel()
{
    if(bDragActive)return;
    if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))Controller->CancelActiveDrag();
    if(!bStageExpanded){HideCardInformation(true);CloseHeroDetails();}
    if(!bStageExpanded&&ShopReveal>0.f)ShopReveal=0.f;
    bStageExpanded=!bStageExpanded;bDrawExpanded=false;if(bStageExpanded)RefreshStagePreviews();RefreshPanelVisibility();
}
bool UEGMainHUD::DismissPanels()
{
    if(bUnitSidebarOpen&&UnitPinnedInformation&&UnitPinnedInformation->HasSkillDetailsOpen())
    {UnitPinnedInformation->CloseSkillDetails();LayoutInformationViews();return true;}
    bool bHadPanel=bDrawExpanded||bStageExpanded||HasHeroSelection()||bInformationPinned||bStatisticsSidebarOpen;
    if(AudioPanel&&AudioPanel->IsVisible()){bHadPanel=true;AudioPanel->SetVisibility(ESlateVisibility::Collapsed);}
    bStatisticsSidebarOpen=false;
    for(const TCHAR* Name:{TEXT("BoardInfoDock"),TEXT("CompanionCatalogue")})
        if(auto* Panel=GetWidgetFromName(Name);Panel&&Panel->IsVisible()){bHadPanel=true;Panel->SetVisibility(ESlateVisibility::Collapsed);}
    HideCardInformation(true);bDrawExpanded=false;bStageExpanded=false;CloseHeroDetails();RefreshPanelVisibility();return bHadPanel;
}
bool UEGMainHUD::CloseContextPanels(){const bool Had=bDrawExpanded||bStageExpanded;bDrawExpanded=bStageExpanded=false;RefreshPanelVisibility();return Had;}
void UEGMainHUD::ClosePanels(){CloseContextPanels();}
void UEGMainHUD::ShopHovered()
{if(auto* Halo=GetWidgetFromName(TEXT("ShopHoverHalo")))Halo->SetRenderOpacity(.65f);}
void UEGMainHUD::ShopUnhovered()
{if(auto* Halo=GetWidgetFromName(TEXT("ShopHoverHalo")))Halo->SetRenderOpacity(0);}
FReply UEGMainHUD::NativeOnMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(bBoardPresentation&&bStageExpanded)return FReply::Unhandled();
    if(bBoardPresentation&&bDrawExpanded)
    {
        // The input router closes on an ordinary release, after distinguishing a
        // click from a drag. A world/hand drag keeps the shop open and shrinks it.
        return FReply::Unhandled();
    }
    if(Event.GetEffectingButton()==EKeys::LeftMouseButton&&(bDrawExpanded||bStageExpanded))
    {
        const auto P=Event.GetScreenSpacePosition();
        if(!IsOver(DrawPanel,P)&&!IsOver(GetWidgetFromName(TEXT("StagePreviewPanel")),P))CloseContextPanels();
        return FReply::Handled();
    }
    return Super::NativeOnMouseButtonDown(Geometry,Event);
}
FReply UEGMainHUD::NativeOnKeyDown(const FGeometry& Geometry,const FKeyEvent& Event)
{if(Event.GetKey()==EKeys::Escape&&DismissPanels())return FReply::Handled();return Super::NativeOnKeyDown(Geometry,Event);}
void UEGMainHUD::ObtainElysia()
{if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))Controller->RequestObtainElysia();}
void UEGMainHUD::RequestBattle()
{
    if(bDragActive)return;
    if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))
        if(!Controller->RequestCombat()&&Button_Battle)Button_Battle->SetToolTipText(FText::FromString(TEXT("尚未满足作战条件")));
}
bool UEGMainHUD::IsOver(const UWidget* Widget,FVector2D Position)
{return IsValid(Widget)&&Widget->IsVisible()&&Widget->GetCachedGeometry().GetLocalSize().X>0&&Widget->GetCachedGeometry().IsUnderLocation(Position);}
bool UEGMainHUD::IsOverUnitInspectionContent(FVector2D Position) const
{
 if(!bUnitSidebarOpen||bDragActive||UnitSidebarReveal<=0.f||!UnitPinnedInformation)return false;
 for(const TCHAR* Name:{TEXT("CharacterName"),TEXT("Button_CloseInfo"),TEXT("Button_SellInfo")})
  if(IsOver(UnitPinnedInformation->GetWidgetFromName(Name),Position))return true;
 const UWidget* Scroll=UnitPinnedInformation->GetWidgetFromName(TEXT("UnitInfoScroll"));
 if(IsOver(Scroll,Position))
  for(const TCHAR* Name:{TEXT("UnitInfoBody"),TEXT("SkillActionRow")})
   if(IsOver(UnitPinnedInformation->GetWidgetFromName(Name),Position))return true;
 return UnitPinnedInformation->HasSkillDetailsOpen()&&IsOver(UnitPinnedInformation->GetWidgetFromName(TEXT("ActionDetails")),Position);
}
int32 UEGMainHUD::GetSellZoneAt(FVector2D Position) const
{
    // A shop offer cannot be sold, even while a previous plate is fading out.
    if(!bDragActive||bShopOfferDrag)return 0;
    int32 Side=0;
    for(const auto* Zone:{SellZone_Left.Get(),SellZone_Right.Get()})
    {
        ++Side;FSlateRect Bounds;
        if(GetSaleZoneBounds(Zone,Bounds)&&Position.X>=Bounds.Left&&Position.X<=Bounds.Right&&
            Position.Y>=Bounds.Top&&Position.Y<=Bounds.Bottom)return Side;
    }
    return 0;
}
bool UEGMainHUD::HasSellZoneHitGeometry() const
{
    FSlateRect Left,Right;
    return GetSaleZoneBounds(SellZone_Left,Left)&&GetSaleZoneBounds(SellZone_Right,Right);
}
bool UEGMainHUD::IsDragCardTouchingSellZone(FVector2D Center,FVector2D CardSize) const
{
    if(!bDragActive||bShopOfferDrag||CardSize.X<=0.f||CardSize.Y<=0.f)return false;
    const FVector2D Half=CardSize*.5;
    const FSlateRect Card(Center-Half,Center+Half);
    const FSlateRect View=GetCachedGeometry().GetRenderBoundingRect();
    if(View.IsEmpty())return false;
    for(const auto* Zone:{SellZone_Left.Get(),SellZone_Right.Get()})
    {
        FSlateRect Bounds;
        if(!GetSaleZoneBounds(Zone,Bounds))continue;
        const FSlateRect Visible(FMath::Max(Bounds.Left,View.Left),FMath::Max(Bounds.Top,View.Top),
            FMath::Min(Bounds.Right,View.Right),FMath::Min(Bounds.Bottom,View.Bottom));
        if(Visible.Right>Visible.Left&&Visible.Bottom>Visible.Top&&
            Card.Right>=Visible.Left&&Card.Left<=Visible.Right&&Card.Bottom>=Visible.Top&&Card.Top<=Visible.Bottom)return true;
    }
    return false;
}
bool UEGMainHUD::GetShopReturnBoundsLocal(FVector2D& Min,FVector2D& Max) const
{
    if(!bShopGroupRectValid)return false;
    Min=ShopGroupMinLocal;Max=ShopGroupMaxLocal;return true;
}
bool UEGMainHUD::IsInShopArea(FVector2D Position) const
{
    if(!DrawPanel||!bDrawExpanded||!bShopGroupRectValid)return false;
    // UWidget::GetCachedGeometry() already contains this widget's render transform
    // (the paint pass builds child geometry with MakeChild(...,RenderTransform,...)),
    // so the open translation must NOT be added a second time. Convert through the
    // shop content widget so the child-vs-host origin is the real one.
    UWidget* Content=DrawPanel->GetWidgetFromName(TEXT("ShopRoot"));
    const UWidget* Anchor=Content?Content:DrawPanel.Get();
    const FGeometry& Geometry=Anchor->GetCachedGeometry();
    if(Geometry.GetLocalSize().X<=0.f||Geometry.GetLocalSize().Y<=0.f)return false;
    const FVector2D Min=Geometry.LocalToAbsolute(ShopGroupMinLocal);
    const FVector2D Max=Geometry.LocalToAbsolute(ShopGroupMaxLocal);
    return Position.X>=FMath::Min(Min.X,Max.X)&&Position.X<=FMath::Max(Min.X,Max.X)&&
        Position.Y>=FMath::Min(Min.Y,Max.Y)&&Position.Y<=FMath::Max(Min.Y,Max.Y);
}
bool UEGMainHUD::IsInShopReturnArea(FVector2D AbsoluteScreenPosition) const
{
    // Explicit origin flag only: a compact presentation of an owned-card drag is
    // never treated as a shop-offer return.
    return bShopOfferDrag&&IsInShopArea(AbsoluteScreenPosition);
}
void UEGMainHUD::SetSellZoneSaleValue(int64 Value)
{
    // Value and hover state independently invalidate the label presentation.
    if(bSellValueApplied&&CachedSellValue==Value)return;
    CachedSellValue=Value;
    bSellValueApplied=true;
    UpdateSellZoneSaleLabels();
}
void UEGMainHUD::UpdateSellZoneSaleLabels()
{
    if(!bSellValueApplied)return;
    int32 Index=0;
    for(auto* Zone:{SellZone_Left.Get(),SellZone_Right.Get()})
    {
        ++Index;
        const FString Text=CachedSellValue<0?TEXT("不可出售"):FString::Printf(TEXT("%s\n+%lld\n崩坏能"),SellFeedbackZone==Index?TEXT("松开出售"):TEXT("出售"),CachedSellValue);
        const FSlateFontInfo Font=FitSellLabelFont(Text,
            EGPearlGold::SellPlateVisibleWidth-2.f*EGPearlGold::SellLabelPadding,EGPearlGold::SellLabelHeight);
        if(Zone)if(auto* Label=Cast<UTextBlock>(Zone->GetWidgetFromName(TEXT("Text_Sell"))))
        {
            Label->SetAutoWrapText(false);Label->SetWrapTextAt(0.f);
            Label->SetFont(Font);Label->SetText(FText::FromString(Text));
        }
    }
}
void UEGMainHUD::ResetSellZoneSaleLabels()
{
    for(auto* Zone:{SellZone_Left.Get(),SellZone_Right.Get()})
        if(Zone)if(auto* Label=Cast<UTextBlock>(Zone->GetWidgetFromName(TEXT("Text_Sell"))))
        {
            Label->SetAutoWrapText(false);Label->SetWrapTextAt(0.f);
            Label->SetFont(FEGVisualStyle::Font(EGPearlGold::SellPlateFontSize));
            Label->SetText(FText::FromString(TEXT("出售")));
        }
    CachedSellValue=MIN_int64;
    bSellValueApplied=false;
}
void UEGMainHUD::HideShopReturnHint()
{
    if(ShopReturnHint)ShopReturnHint->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGMainHUD::SetupShopReturnHintSlot()
{
    if(!ShopReturnHint||!WidgetTree)return;
    auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root)return;
    if(ShopReturnHint->GetParent()!=Root){ShopReturnHint->RemoveFromParent();Root->AddChildToCanvas(ShopReturnHint);}
    if(auto* HintSlot=Cast<UCanvasPanelSlot>(ShopReturnHint->Slot))
    {
        HintSlot->SetAnchors(FAnchors(0,0,0,0));HintSlot->SetAlignment(FVector2D(0,0));
        HintSlot->SetPosition(FVector2D(0,0));HintSlot->SetSize(FVector2D(160,EGPearlGold::ShopCompactHintHeight));HintSlot->SetZOrder(62);
    }
}
void UEGMainHUD::CreateShopReturnHint()
{
    if(!WidgetTree)return;
    // Reuse both an existing property object and a same-named widget that survived
    // a reconstruction, so repeated NativeConstruct never duplicates or leaks it.
    if(!ShopReturnHint)if(auto* Existing=Cast<UTextBlock>(GetWidgetFromName(TEXT("ShopReturnHint"))))ShopReturnHint=Existing;
    if(!ShopReturnHint)
    {
        ShopReturnHint=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("ShopReturnHint"));
        ShopReturnHint->SetText(FText::FromString(TEXT("松开返回商店")));
        ShopReturnHint->SetFont(FEGVisualStyle::Font(EGPearlGold::SellPlateFontSize));
        ShopReturnHint->SetJustification(ETextJustify::Center);
        ShopReturnHint->SetColorAndOpacity(FSlateColor(FLinearColor(.88f,.88f,.91f,.95f)));
        ShopReturnHint->SetShadowColorAndOpacity(FLinearColor(0,0,0,.55f));
        ShopReturnHint->SetShadowOffset(FVector2D(0,1));
    }
    SetupShopReturnHintSlot();
    // Hit-test invisible by contract: the hint is never part of the cancel target.
    ShopReturnHint->SetVisibility(ESlateVisibility::Collapsed);
    ShopReturnHint->SetRenderOpacity(1.f);
}
void UEGMainHUD::UpdateShopReturnHint(float CompactBlend,float ViewWidth)
{
    if(!ShopReturnHint)return;
    if(!bShopOfferDrag||!bDrawExpanded||CompactBlend<=.02f)
    {
        ShopReturnHint->SetVisibility(ESlateVisibility::Collapsed);return;
    }
    const auto Layout=EGPearlGold::ShopCompactLayout(ViewWidth);
    const float HintWidth=FMath::Max(160.f,Layout.GroupWidth);
    if(auto* HintSlot=Cast<UCanvasPanelSlot>(ShopReturnHint->Slot))
    {
        HintSlot->SetAnchors(FAnchors(0,0,0,0));HintSlot->SetAlignment(FVector2D(0,0));
        HintSlot->SetPosition(FVector2D(Layout.GroupLeft+Layout.GroupWidth*.5f-HintWidth*.5f,Layout.HintTop));
        HintSlot->SetSize(FVector2D(HintWidth,EGPearlGold::ShopCompactHintHeight));
    }
    ShopReturnHint->SetRenderOpacity(FMath::Clamp(CompactBlend,0.f,1.f));
    ShopReturnHint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UEGMainHUD::BindShopPlateHover()
{
    if(!DrawPanel)return;
    if(auto* Lock=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_ShopLock"))))
    {
        Lock->OnHovered.AddUniqueDynamic(this,&ThisClass::ShopLockHovered);
        Lock->OnUnhovered.AddUniqueDynamic(this,&ThisClass::ShopLockUnhovered);
        Lock->OnPressed.AddUniqueDynamic(this,&ThisClass::ShopLockPressed);
        Lock->OnReleased.AddUniqueDynamic(this,&ThisClass::ShopLockReleased);
    }
    if(auto* Refresh=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_RefreshShop"))))
    {
        Refresh->OnHovered.AddUniqueDynamic(this,&ThisClass::ShopRefreshHovered);
        Refresh->OnUnhovered.AddUniqueDynamic(this,&ThisClass::ShopRefreshUnhovered);
        Refresh->OnPressed.AddUniqueDynamic(this,&ThisClass::ShopRefreshPressed);
        Refresh->OnReleased.AddUniqueDynamic(this,&ThisClass::ShopRefreshReleased);
    }
    RefreshShopPlateTint();
}
void UEGMainHUD::UnbindShopPlateHover()
{
    if(!DrawPanel)return;
    if(auto* Lock=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_ShopLock"))))
    {
        Lock->OnHovered.RemoveDynamic(this,&ThisClass::ShopLockHovered);
        Lock->OnUnhovered.RemoveDynamic(this,&ThisClass::ShopLockUnhovered);
        Lock->OnPressed.RemoveDynamic(this,&ThisClass::ShopLockPressed);
        Lock->OnReleased.RemoveDynamic(this,&ThisClass::ShopLockReleased);
    }
    if(auto* Refresh=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_RefreshShop"))))
    {
        Refresh->OnHovered.RemoveDynamic(this,&ThisClass::ShopRefreshHovered);
        Refresh->OnUnhovered.RemoveDynamic(this,&ThisClass::ShopRefreshUnhovered);
        Refresh->OnPressed.RemoveDynamic(this,&ThisClass::ShopRefreshPressed);
        Refresh->OnReleased.RemoveDynamic(this,&ThisClass::ShopRefreshReleased);
    }
}
void UEGMainHUD::SetShopPlateTint(const TCHAR* Prefix,FLinearColor Tint)
{
    if(!DrawPanel)return;
    ForEachImageUnder(DrawPanel,[Prefix,Tint](UImage* Image)
    {
        if(Image&&Image->GetName().StartsWith(Prefix))Image->SetColorAndOpacity(Tint);
    });
}
void UEGMainHUD::RefreshShopPlateTint()
{
    if(!DrawPanel)return;
    auto TintFor=[this](const TCHAR* ButtonName,bool bPressed)->FLinearColor
    {
        const auto* Button=Cast<UButton>(DrawPanel->GetWidgetFromName(ButtonName));
        if(!Button||!Button->GetIsEnabled())return FLinearColor(EGPearlGold::ShopPlateTintDisabled,EGPearlGold::ShopPlateTintDisabled,EGPearlGold::ShopPlateTintDisabled,1.f);
        if(bPressed)return FLinearColor(EGPearlGold::ShopPlateTintPressed,EGPearlGold::ShopPlateTintPressed,EGPearlGold::ShopPlateTintPressed,1.f);
        return Button->IsHovered()?FLinearColor(EGPearlGold::ShopPlateTintHover,EGPearlGold::ShopPlateTintHover,EGPearlGold::ShopPlateTintHover,1.f)
            :FLinearColor(EGPearlGold::ShopPlateTintBase,EGPearlGold::ShopPlateTintBase,EGPearlGold::ShopPlateTintBase,1.f);
    };
    const FLinearColor LockTint=TintFor(TEXT("Button_ShopLock"),bShopLockPressed);
    SetShopPlateTint(TEXT("ShopLockPlate"),LockTint);
    SetShopPlateTint(TEXT("ShopLockGlyph"),LockTint);
    const FLinearColor RefreshTint=TintFor(TEXT("Button_RefreshShop"),bShopRefreshPressed);
    SetShopPlateTint(TEXT("ShopRefreshPlate"),RefreshTint);
    SetShopPlateTint(TEXT("ShopRefreshGlyph"),RefreshTint);
}
void UEGMainHUD::SetShopLockGlyphState(bool bLocked)
{
    if(!DrawPanel)return;
    // Two aspect-fitted images inside one fixed 20x20 slot: switching visibility
    // never changes the layout size, so the icon and label do not drift.
    if(auto* Closed=Cast<UImage>(DrawPanel->GetWidgetFromName(TEXT("ShopLockGlyphClosed"))))
        Closed->SetVisibility(bLocked?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* Open=Cast<UImage>(DrawPanel->GetWidgetFromName(TEXT("ShopLockGlyphOpen"))))
        Open->SetVisibility(bLocked?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
void UEGMainHUD::ShopLockHovered(){RefreshShopPlateTint();}
void UEGMainHUD::ShopLockUnhovered(){RefreshShopPlateTint();}
void UEGMainHUD::ShopRefreshHovered(){RefreshShopPlateTint();}
void UEGMainHUD::ShopRefreshUnhovered(){RefreshShopPlateTint();}
void UEGMainHUD::ShopLockPressed(){bShopLockPressed=true;RefreshShopPlateTint();}
void UEGMainHUD::ShopLockReleased(){bShopLockPressed=false;RefreshShopPlateTint();}
void UEGMainHUD::ShopRefreshPressed(){bShopRefreshPressed=true;RefreshShopPlateTint();}
void UEGMainHUD::ShopRefreshReleased(){bShopRefreshPressed=false;RefreshShopPlateTint();}
bool UEGMainHUD::IsPointBlocked(FVector2D Position) const
{
    if(bBoardPresentation&&ShopReveal>0.f)
    {
        // During a drag only the animating compact rect may block the board; the
        // full expanded DrawPanel band would be an invisible dead zone.
        if(bDragActive){if(IsInShopArea(Position))return true;}
        else if(IsOver(DrawPanel,Position))return true;
    }
    if(auto* Host=GetOwningPlayer()?Cast<AEGHUD>(GetOwningPlayer()->GetHUD()):nullptr)
        if(auto* Panel=Host->GetRunPanel();Panel&&Panel->BlocksPoint(Position))return true;
    bool bShopControl=false;
    if(bBoardPresentation&&bDrawExpanded)
    {
        bShopControl=IsOver(GetWidgetFromName(TEXT("BoardThemeBanner")),Position)||IsOver(Button_ToggleDrawPanel,Position)||
            IsOver(HandPanel,Position)||IsOver(Button_Battle,Position)||IsOver(GetWidgetFromName(TEXT("BoardThemePopulationPill")),Position);
        if(!bShopControl)bShopControl=IsInShopArea(Position);
    }
    // Fade-out is purely visual: once the owned drag ends it must not leave
    // an invisible interaction strip over the corners of the board.
    const bool bOverSaleTarget=GetSellZoneAt(Position)!=0;
    return IsOver(AudioButton,Position)||IsOver(AudioPanel,Position)||IsOver(CardPreviewInformation,Position)||IsOverUnitInspectionContent(Position)||IsOver(GetWidgetFromName(TEXT("Button_Companions")),Position)||IsOver(GetWidgetFromName(TEXT("CompanionCatalogue")),Position)||bShopControl||(bStageExpanded&&IsOver(GetWidgetFromName(TEXT("StagePreviewPanel")),Position))||(bInformationPinned&&(IsOver(GetWidgetFromName(TEXT("BoardInfoDock")),Position)||IsOver(SkillInformation,Position)))||(bStatisticsSidebarOpen&&!bDragActive&&StatisticsSidebarReveal>0.f&&IsOver(GetWidgetFromName(TEXT("BoardStatistics")),Position))||IsOver(GetWidgetFromName(TEXT("UnitDetailDock")),Position)||IsOver(HandPanel,Position)||bOverSaleTarget||
        IsOver(Button_ToggleDrawPanel,Position)||IsOver(Button_Stage,Position)||IsOver(Button_Battle,Position)||
        IsOver(GetWidgetFromName(TEXT("Button_Statistics")),Position)||IsOver(GetWidgetFromName(TEXT("Button_PauseMenu")),Position)||
        IsOver(GetWidgetFromName(TEXT("BoardThemePopulationPill")),Position)||IsOver(GetWidgetFromName(TEXT("BoardThemeAvatarDock")),Position)||
        IsOver(GetWidgetFromName(TEXT("CorruptionDock")),Position);
}
void UEGMainHUD::SetDragActive(bool bActive)
{
    if(bActive&&AudioPanel)AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
    if(DragFeedback)DragFeedback->ClearBoardMove();
    if(bActive)if(auto* Catalogue=GetWidgetFromName(TEXT("CompanionCatalogue")))Catalogue->SetVisibility(ESlateVisibility::Collapsed);
    if(bActive)bShopWasExpandedOnDragStart=bDrawExpanded;
    bDragActive=bActive;
    if(bBoardPresentation&&Cards.IsEmpty())
    {
        if(HandPanel)HandPanel->SetVisibility(bActive?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(Text_HandCount)Text_HandCount->SetVisibility(bActive?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        RefreshHandLayout();
    }
    if(bActive)
    {
        if(UnitPinnedInformation)UnitPinnedInformation->CollapseSkill();
        HideHandHover();
        // Keep the inspected identity and its subscriptions while temporarily
        // clearing the sale edges. Roster changes can still invalidate it.
        for(auto* Info:{SkillInformation.Get(),CardPreviewInformation.Get(),UnitHoverInformation.Get()})
            if(Info)Info->SetVisibility(ESlateVisibility::Collapsed);
        if(auto* Dock=GetWidgetFromName(TEXT("BoardInfoDock")))Dock->SetVisibility(ESlateVisibility::Collapsed);
        if(InformationOwner!=TEXT("Unit")){bInformationPinned=false;InformationOwner=NAME_None;}
    }
    if(!bActive)
    {
        // Success, cancellation, destruction and focus loss all converge here:
        // clear every drag-only presentation, restore the captured shop-open state
        // and the offer-card opacity, then let the transition tick animate back.
        ShopDragSlot=INDEX_NONE;
        bShopContextDragPresentationActive=false;
        bShopOfferDrag=false;
        if(ShopDragInformation)ShopDragInformation->SetVisibility(ESlateVisibility::Collapsed);
        if(DrawPanel)for(int32 I=0;I<4;++I)if(auto* W=DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopSlot_%d"),I))))W->SetRenderOpacity(1.f);
        ResetSellZoneSaleLabels();
        HideShopReturnHint();
        if(bShopWasExpandedOnDragStart&&!bDrawExpanded){bDrawExpanded=true;bStageExpanded=false;RefreshPanelVisibility();}
        bShopWasExpandedOnDragStart=false;
    }
    if(bBoardPresentation)UpdateShopTransition(0.f);
    else if(DrawPanel){DrawPanel->SetRenderTransformPivot(FVector2D(.5f,0.f));DrawPanel->SetRenderScale(FVector2D(bActive?0.9f:1.f));}
    if(DragFeedback){DragFeedback->SetVisibility(bActive?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);if(bActive)DragFeedback->SetHint(FText::FromString(TEXT("拖动至棋盘  返回手牌区、右键或 Esc 取消")));}
    if(Button_ToggleDrawPanel)Button_ToggleDrawPanel->SetIsEnabled(!bActive);
    if(Button_Stage)Button_Stage->SetIsEnabled(!bActive);
    if(bBoardPresentation)
    {
        // When the shop is open the banner belongs to the shop title; a drag ending
        // must not resurrect the round/corruption labels underneath it.
        const bool bShowRound=!bActive&&!bDrawExpanded;
        if(Button_Stage)Button_Stage->SetVisibility(bShowRound?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
        if(auto* Corruption=GetWidgetFromName(TEXT("Text_Corruption")))Corruption->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))ApplyMatchView(Controller->GetMatchView());
    if(!bActive&&HasHeroSelection())RebindInspectionSource();
    if(!bActive&&bStatisticsSidebarOpen)bBattleStatisticsDirty=true;
    UpdateSidebarMotion(0.f);
    SetSellZoneFeedback(0);
}
void UEGMainHUD::UpdateDragFeedback(UEGHeroDefinition* Definition,UTexture2D* Artwork,FVector2D Origin,FVector2D Pointer,bool bAllowed,bool bShowCardAndArrow)
{
    if(bBoardPresentation&&bDragActive&&DragFeedback)DragFeedback->Present(Definition,Artwork,Origin,Pointer,bAllowed,bShowCardAndArrow);
}
void UEGMainHUD::UpdateBoardMoveFeedback(const TArray<FVector2D>& CellCorners,FVector2D Origin,FVector2D Pointer,bool bAllowed,bool bShowArrow)
{
    if(bBoardPresentation&&bDragActive&&DragFeedback)DragFeedback->PresentBoardMove(CellCorners,Origin,Pointer,bAllowed,bShowArrow);
}
void UEGMainHUD::SetSellZoneFeedback(int32 Zone)
{
 if(SellFeedbackZone!=Zone){SellFeedbackZone=Zone;UpdateSellZoneSaleLabels();}
 UpdateFeedbackMotion(0.f);
}
void UEGMainHUD::UpdateFeedbackMotion(float DT)
{
 if(EnergyPulse>0.f)
 {
  EnergyPulse=FMath::Max(0.f,EnergyPulse-DT);
  auto* Value=GetWidgetFromName(TEXT("EnergyValueRow"));
  if(!Value)Value=GetWidgetFromName(TEXT("Text_HUDGold"));
  if(Value)
  {Value->SetRenderTransformPivot(FVector2D(.5f,.5f));Value->SetRenderScale(FVector2D(1.f+.08f*FMath::Sin(PI*(1.f-EnergyPulse/.24f))));}
 }
 int32 I=0;
 const bool bVisible=bDragActive&&!bShopOfferDrag;
 // Do not carry a fading owned-card target into a shop-offer drag.
 if(bShopOfferDrag)SellReveal=0.f;
 const float Target=bVisible?1.f:0.f;
 const float Step=DT/.14f;
 const float PreviousReveal=SellReveal;
 SellReveal=FMath::Clamp(SellReveal+FMath::Clamp(Target-SellReveal,-Step,Step),0.f,1.f);
 for(auto* W:{SellZone_Left.Get(),SellZone_Right.Get()})
 {
  ++I;if(!W)continue;
  const float HoverTarget=bVisible&&SellFeedbackZone==I?1.f:0.f;
  const float PreviousHover=SellHover[I-1];
  SellHover[I-1]=FMath::Lerp(PreviousHover,HoverTarget,1.f-FMath::Exp(-DT*24.f));
  if(FMath::IsNearlyEqual(SellHover[I-1],HoverTarget,.001f))SellHover[I-1]=HoverTarget;
  W->SetVisibility(bVisible?ESlateVisibility::SelfHitTestInvisible:SellReveal>0.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  if(DT>0.f&&PreviousReveal==SellReveal&&PreviousHover==SellHover[I-1])continue;
  W->SetRenderOpacity(SellReveal*SellReveal*(3.f-2.f*SellReveal));
  W->SetRenderTransformPivot(FVector2D(I==1?0.f:1.f,.5f));
  W->SetRenderScale(FVector2D(1.f)); // hover tint only, never a scale pop
  const FLinearColor Tint=FMath::Lerp(FLinearColor(.72f,.12f,.16f,1),FLinearColor(1.f,.30f,.34f,1),SellHover[I-1]);
  if(auto* User=Cast<UUserWidget>(W))
   if(auto* Surface=Cast<UBorder>(User->GetWidgetFromName(TEXT("SellPlateSurface"))))Surface->SetBrushColor(Tint);
  ForEachImageUnder(W,[&Tint](UImage* Image){if(Image->GetName().StartsWith(TEXT("SellPlateArt")))Image->SetColorAndOpacity(Tint);});
 }
}

bool UEGMainHUD::IsInCardArea(FVector2D P) const {return IsOver(HandPanel,P);}
bool UEGMainHUD::IsAboveHandForDeployment(FVector2D P) const
{
 if(!HandPanel)return false;
 const auto& Geometry=HandPanel->GetCachedGeometry();
 return Geometry.AbsoluteToLocal(P).Y<=-24.f;
}
bool UEGMainHUD::IsOutsideHandReorderArea(FVector2D P) const
{
 if(!HandPanel)return false;
 const auto& Geometry=HandPanel->GetCachedGeometry();
 const FVector2D Size=Geometry.GetLocalSize();
 if(Size.X<=0.f||Size.Y<=0.f)return false;
 const FVector2D Local=Geometry.AbsoluteToLocal(P);
 constexpr float ExitMargin=24.f;
 return Local.Y<=-ExitMargin||Local.X<=-ExitMargin||Local.X>=Size.X+ExitMargin;
}
FGuid UEGMainHUD::ReserveTargetAt(FVector2D P) const
{for(const auto& Id:ReserveIds)if(auto* W=FindCardWidget(Id))if(IsOver(W,P))return Id;return FGuid();}
void UEGHandCard::SetIdentity(FText Text)
{
 if(!IdentityText&&WidgetTree)
  if(auto* Layers=Cast<UOverlay>(GetWidgetFromName(TEXT("CardLayers"))))
  {
   IdentityText=WidgetTree->ConstructWidget<UTextBlock>();
   auto* S=Layers->AddChildToOverlay(IdentityText);S->SetHorizontalAlignment(HAlign_Fill);S->SetVerticalAlignment(VAlign_Bottom);
   IdentityText->SetVisibility(ESlateVisibility::HitTestInvisible);IdentityText->SetAutoWrapText(true);
   IdentityText->SetFont(FEGVisualStyle::PearlFont(14));IdentityText->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));
  }
 if(IdentityText)IdentityText->SetText(Text);
}
void UEGHandCard::SetOverflow(bool bOverflow,bool bProtected)
{
 (void)bProtected;
 if(!OverflowText&&WidgetTree)if(auto* Layers=Cast<UOverlay>(GetWidgetFromName(TEXT("CardLayers"))))
 {
  OverflowText=WidgetTree->ConstructWidget<UTextBlock>();
  auto* OverflowSlot=Layers->AddChildToOverlay(OverflowText);OverflowSlot->SetHorizontalAlignment(HAlign_Center);OverflowSlot->SetVerticalAlignment(VAlign_Top);
  OverflowText->SetVisibility(ESlateVisibility::HitTestInvisible);OverflowText->SetFont(FEGVisualStyle::PearlFont(14));
  OverflowText->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.12f,.12f,1.f)));
 }
 if(OverflowText)
 {
  OverflowText->SetText(bOverflow?FText::FromString(TEXT("开战自动出售")):FText::GetEmpty());
  OverflowText->SetColorAndOpacity(FSlateColor(bOverflow?FLinearColor(1.f,.12f,.12f,1.f):EGPearlGold::Champagne));
 }
 if(CardImage)CardImage->SetColorAndOpacity(bOverflow?FLinearColor(1.f,.48f,.48f,1.f):FLinearColor::White);
}

bool UEGMainHUD::CanEditHand() const
{
 const auto* C=Cast<UEGOverlayWidgetController>(GetWidgetController());
 return C&&C->GetMatchView().Phase==EEGMatchPhase::Preparation&&!bDragActive;
}
void UEGMainHUD::ShowHandHover(UEGHandCard* Card)
{
 if(!Card||bDragActive||!WidgetTree)return;
 const auto* Data=AppliedCards.Find(Card->InstanceId);auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
 if(!Data||!Root||!GetOwningPlayer())return;
 if(!HoverCard)
 {
  HoverCard=CreateWidget<UEGHandCard>(GetOwningPlayer(),Card->GetClass());if(!HoverCard)return;
  auto* HoverSlot=Root->AddChildToCanvas(HoverCard);HoverSlot->SetZOrder(72);HoverCard->SetVisibility(ESlateVisibility::HitTestInvisible);
 }
 if(HoverSource.Get()!=Card||bHoverCardDirty)
 {
  if(HoverSource.Get()!=Card)if(auto* Previous=HoverSource.Get())Previous->SetHoverSourceHidden(false);
  bHoverCardDirty=false;
  const FText CardName=Data->HeroDefinition&&!Data->HeroDefinition->DisplayName.IsEmpty()?Data->HeroDefinition->DisplayName:Data->Identity;
  HoverSource=Card;HoverCard->SetCompactPresentation(false,CardName);HoverCard->Configure(Data->InstanceId,Data->Artwork);
  HoverCard->SetDefinitionAppearance(Data->HeroDefinition,Data->Level);HoverCard->SetOverflow(Data->bOverflow,Data->bOverflowProtected);
  const bool bLayeredCard=Data->HeroDefinition&&!Data->HeroDefinition->CardFrameTexture.IsNull();
  HoverCard->SetIdentity(!bLayeredCard&&Data->Level>1?FText::FromString(FString::Printf(TEXT("Lv.%d"),Data->Level)):FText::GetEmpty());
 }
 if(auto* Size=Cast<USizeBox>(HoverCard->GetWidgetFromName(TEXT("CardSize")))){Size->SetWidthOverride(EGPearlGold::HandCardWidth);Size->SetHeightOverride(EGPearlGold::HandCardHeight);}
 if(auto* HoverSlot=Cast<UCanvasPanelSlot>(HoverCard->Slot))
 {
  const auto& Geometry=GetCachedGeometry();const FVector2D P=Geometry.AbsoluteToLocal(Card->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector));
  const FVector2D V=Geometry.GetLocalSize();
  HoverSlot->SetPosition(FVector2D(FMath::Clamp(P.X,0.f,FMath::Max(0.f,V.X-EGPearlGold::HandCardWidth)),FMath::Clamp(P.Y-30.f,0.f,FMath::Max(0.f,V.Y-EGPearlGold::HandCardHeight))));
  HoverSlot->SetSize(FVector2D(EGPearlGold::HandCardWidth,EGPearlGold::HandCardHeight));
 }
 HoverCard->SetVisibility(ESlateVisibility::HitTestInvisible);
 Card->SetHoverSourceHidden(true);
}
void UEGMainHUD::HideHandHover(UEGHandCard* Card)
{if(Card&&HoverSource.Get()!=Card)return;if(auto* Source=HoverSource.Get())Source->SetHoverSourceHidden(false);HoverSource.Reset();bHoverCardDirty=false;if(HoverCard)HoverCard->SetVisibility(ESlateVisibility::Collapsed);}
bool UEGMainHUD::BeginHandReorder(FGuid Id,int32& Revision)
{
 auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
 if(!CanEditHand()||!Controller||!Cards.Contains(Id))return false;
 Revision=Controller->GetHandOrderRevision();if(Revision==INDEX_NONE)return false;
 HideHandHover();ReorderBefore.Invalidate();bReorderTargetValid=false;bHasReorderPointer=false;
 if(!HandInsertionMarker&&WidgetTree)if(auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget))
 {
  HandInsertionMarker=WidgetTree->ConstructWidget<UBorder>();HandInsertionMarker->SetBrushColor(FLinearColor(1.f,.75f,.95f,1.f));
  auto* MarkerSlot=Root->AddChildToCanvas(HandInsertionMarker);MarkerSlot->SetSize(FVector2D(3.f,EGPearlGold::HandPeekHeight));MarkerSlot->SetZOrder(74);
  HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);
 }
 return true;
}
void UEGMainHUD::RegisterHandReorder(UEGCardDragOperation* Operation)
{ActiveHandReorder=Operation;}
void UEGMainHUD::UnregisterHandReorder(UEGCardDragOperation* Operation)
{
 if(ActiveHandReorder.Get()!=Operation)return;
 ActiveHandReorder.Reset();bHasReorderPointer=false;bReorderTargetValid=false;ReorderBefore.Invalidate();
 if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);
}
bool UEGMainHUD::CancelHandReorder()
{
 auto* Operation=ActiveHandReorder.Get();if(!Operation)return false;
 TSharedPtr<FDragDropOperation> SlateOperation;
 if(FSlateApplication::IsInitialized()&&UWidgetBlueprintLibrary::GetDragDroppingContent()==Operation)
  SlateOperation=FSlateApplication::Get().GetDragDroppingContent();
 ActiveHandReorder.Reset();Operation->MarkFinished();bReorderTargetValid=false;bHasReorderPointer=false;ReorderBefore.Invalidate();
 if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);
 UEGCardDragOperation::CancelSlateDragDeferred(SlateOperation);return true;
}
void UEGMainHUD::UpdateHandReorder(FVector2D Position)
{
 ReorderPointer=Position;bHasReorderPointer=true;
 bReorderTargetValid=CanEditHand()&&IsInCardArea(Position)&&HandScrollBox;
 if(!bReorderTargetValid){if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);return;}
 const auto& ViewGeometry=HandScrollBox->GetCachedGeometry();const FVector2D ViewSize=ViewGeometry.GetLocalSize();
 if(ViewSize.X<=0.f||ViewSize.Y<=0.f){bReorderTargetValid=false;if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);return;}
 const FVector2D ViewPoint=ViewGeometry.AbsoluteToLocal(Position);
 if(ViewPoint.X<0.f||ViewPoint.X>ViewSize.X||ViewPoint.Y<0.f||ViewPoint.Y>ViewSize.Y)
 {bReorderTargetValid=false;if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);return;}
 const FVector2D ViewMin=ViewGeometry.LocalToAbsolute(FVector2D::ZeroVector);
 const FVector2D ViewMax=ViewGeometry.LocalToAbsolute(ViewSize);
 ReorderBefore.Invalidate();float MarkerX=0.f;float ContentOffset=0.f;int32 LastVisible=INDEX_NONE;bool bBeforeVisible=false;
 for(int32 I=0;I<HandScrollBox->GetChildrenCount();++I)if(auto* Card=Cast<UEGHandCard>(HandScrollBox->GetChildAt(I)))
 {
  const auto* CardSlot=Cast<UScrollBoxSlot>(Card->Slot);if(!CardSlot)continue;
  const FMargin CardPadding=CardSlot->GetPadding();
  const float ExpectedLeft=ContentOffset+CardPadding.Left-HandScrollBox->GetScrollOffset();
  ContentOffset+=CardPadding.Left+EGPearlGold::HandCardWidth+CardPadding.Right;
  // Cached geometry for a previously visible, now clipped child can remain
  // stale; reject it using the current slot position as well as its geometry.
  if(ExpectedLeft+EGPearlGold::HandCardWidth<=0.f||ExpectedLeft>=ViewSize.X)continue;
  const auto& G=Card->GetCachedGeometry();const FVector2D CardSize=G.GetLocalSize();if(CardSize.X<=0.f||CardSize.Y<=0.f)continue;
  const FVector2D Left=G.LocalToAbsolute(FVector2D::ZeroVector);const FVector2D Right=G.LocalToAbsolute(CardSize);
  if(!FMath::IsFinite(Left.X)||!FMath::IsFinite(Left.Y)||!FMath::IsFinite(Right.X)||!FMath::IsFinite(Right.Y)||
     Right.X<=ViewMin.X||Left.X>=ViewMax.X||Right.Y<=ViewMin.Y||Left.Y>=ViewMax.Y)continue;
  const float VisibleLeft=FMath::Max(Left.X,ViewMin.X),VisibleRight=FMath::Min(Right.X,ViewMax.X);
  LastVisible=I;MarkerX=VisibleRight;
  if(Position.X<(VisibleLeft+VisibleRight)*.5f){ReorderBefore=Card->InstanceId;MarkerX=VisibleLeft;bBeforeVisible=true;break;}
 }
 if(LastVisible==INDEX_NONE){bReorderTargetValid=false;if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);return;}
 if(!bBeforeVisible&&LastVisible+1<HandScrollBox->GetChildrenCount())
  if(const auto* Next=Cast<UEGHandCard>(HandScrollBox->GetChildAt(LastVisible+1)))ReorderBefore=Next->InstanceId;
 if(HandInsertionMarker)
 {
  const FVector2D P=GetCachedGeometry().AbsoluteToLocal(FVector2D(MarkerX,HandScrollBox->GetCachedGeometry().LocalToAbsolute(FVector2D::ZeroVector).Y));
  if(auto* MarkerSlot=Cast<UCanvasPanelSlot>(HandInsertionMarker->Slot))MarkerSlot->SetPosition(P);
  HandInsertionMarker->SetVisibility(ESlateVisibility::HitTestInvisible);
 }
}
void UEGMainHUD::FinishHandReorder(FGuid Id,int32 Revision,FVector2D Position,bool bCommit)
{
 ActiveHandReorder.Reset();
 UpdateHandReorder(Position);
 if(bCommit&&bReorderTargetValid)if(auto* C=Cast<UEGOverlayWidgetController>(GetWidgetController()))C->ReorderHandCard(Id,ReorderBefore,Revision);
 bReorderTargetValid=false;bHasReorderPointer=false;ReorderBefore.Invalidate();if(HandInsertionMarker)HandInsertionMarker->SetVisibility(ESlateVisibility::Collapsed);
}

void UEGMainHUD::NextShopCard()
{
 if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))Controller->RequestNextShopCard();
}

void UEGMainHUD::BindInspectedUnit(AEGChessUnit* Unit)
{
 if(InspectedUnit.Get()==Unit)return;
 if(auto* Old=InspectedUnit.Get())
 {Old->OnAttributeChanged.RemoveDynamic(this,&ThisClass::InspectedAttributeChanged);Old->GetHeroSkill()->OnStatusChanged.RemoveDynamic(this,&ThisClass::InspectedStatusChanged);Old->OnDestroyed.RemoveDynamic(this,&ThisClass::InspectedActorDestroyed);}
 InspectedUnit=Unit;
 if(Unit){Unit->OnAttributeChanged.AddUniqueDynamic(this,&ThisClass::InspectedAttributeChanged);Unit->GetHeroSkill()->OnStatusChanged.AddUniqueDynamic(this,&ThisClass::InspectedStatusChanged);Unit->OnDestroyed.AddUniqueDynamic(this,&ThisClass::InspectedActorDestroyed);}
}
void UEGMainHUD::ClearInspectionBindings()
{
 bUnitSidebarOpen=false;
 if(bBoardPresentation)UpdateSidebarMotion(0.f);
 if(UnitPinnedInformation)UnitPinnedInformation->CollapseSkill();
 BindInspectedUnit(nullptr);
 if(InspectedRoster.IsValid())InspectedRoster->OnHeroChanged.RemoveDynamic(this,&ThisClass::InspectedHeroChanged);
 InspectedUnit.Reset();InspectedRoster.Reset();InspectedHero.Invalidate();
}
void UEGMainHUD::CloseHeroDetails(){ClearInspectionBindings();RefreshInspection();}
void UEGMainHUD::InspectUnit(AEGChessUnit* Unit)
{
 if(!Unit){CloseHeroDetails();return;}
 if(InspectedUnit.Get()==Unit){RefreshInspection();return;}
 ClearInspectionBindings();BindInspectedUnit(Unit);
 auto* Roster=GetGameInstance()?GetGameInstance()->GetSubsystem<UEGHeroRoster>():nullptr;FEGHeroInstance H;
 if(Roster&&Roster->GetHero(Unit->GetInstanceId(),H)){InspectedRoster=Roster;InspectedHero=H.InstanceId;Roster->OnHeroChanged.AddUniqueDynamic(this,&ThisClass::InspectedHeroChanged);}
 RefreshInspection();
}
void UEGMainHUD::InspectHero(UEGHeroRoster* Roster,FGuid Id)
{
 if(InspectedRoster.Get()==Roster&&InspectedHero==Id){RebindInspectionSource();return;}
 ClearInspectionBindings();InspectedRoster=Roster;InspectedHero=Id;
 if(Roster)Roster->OnHeroChanged.AddUniqueDynamic(this,&ThisClass::InspectedHeroChanged);RebindInspectionSource();
}
void UEGMainHUD::RebindInspectionSource()
{
 if(InspectedRoster.IsValid())
 {
  FEGHeroInstance H;if(!InspectedRoster->GetHero(InspectedHero,H)){CloseHeroDetails();return;}
  auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
  AEGChessUnit* Unit=Controller?Controller->FindInspectedCombatUnit(InspectedHero):nullptr;
  BindInspectedUnit(Unit);
 }
 RefreshInspection();
}
void UEGMainHUD::InspectedActorDestroyed(AActor* Actor)
{
 (void)Actor;
 if(UnitPinnedInformation)UnitPinnedInformation->CollapseSkill();
 BindInspectedUnit(nullptr);
 if(InspectedRoster.IsValid())RebindInspectionSource();else CloseHeroDetails();
}
void UEGMainHUD::InspectedAttributeChanged(FName Attribute,float OldValue,float NewValue){RefreshInspection();}
void UEGMainHUD::InspectedStatusChanged(){RefreshInspection();}
void UEGMainHUD::InspectedHeroChanged(FGuid Id){if(!Id.IsValid()||Id==InspectedHero)RebindInspectionSource();}
void UEGMainHUD::RefreshInspection()
{
 const auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
 const FEGUnitInspectionView View=Controller?Controller->GetInspectionView(InspectedUnit.Get(),InspectedRoster.Get(),InspectedHero)
  :FEGUnitInspectionView::Capture(InspectedUnit.Get(),InspectedRoster.Get(),InspectedHero);
 const UEGHeroDefinition* Definition=View.Definition;const int32 Level=View.Level,CardsUsed=View.CardsUsed;
 const bool Awakened=View.bAwakened,Combat=View.bCombat;const FEGStatBlock& Stats=View.Stats;
 const float Health=View.Health,Mana=View.Mana,Shield=View.Shield;
 const int32 AwakeningSteps=FMath::Max(2,Definition?Definition->GetAwakeningCardCount():3)-1;
 const int32 CompletedAwakeningSteps=FMath::Clamp(FMath::Max(1,CardsUsed)-1,0,AwakeningSteps);
 const bool Visible=Definition&&!bDragActive&&!bDrawExpanded&&!bStageExpanded;
 if(bBoardPresentation&&UnitPinnedInformation)
 {
  if(auto* Old=GetWidgetFromName(TEXT("UnitDetailDock")))Old->SetVisibility(ESlateVisibility::Collapsed);
  if(!Visible)
  {
   bUnitSidebarOpen=false;
   UpdateSidebarMotion(0.f);
   if(!bDragActive&&InformationOwner==TEXT("Unit")){bInformationPinned=false;InformationOwner=NAME_None;}
   return;
  }
  for(auto* Info:{SkillInformation.Get(),CardPreviewInformation.Get(),UnitHoverInformation.Get()})if(Info)Info->SetVisibility(ESlateVisibility::Collapsed);
  bInformationPinned=true;InformationOwner=TEXT("Unit");
  FString Summary=FString::Printf(TEXT("Lv.%d"),Level);
  if(Awakened)Summary+=TEXT("  已觉醒");
  else if(CompletedAwakeningSteps>0)Summary+=FString::Printf(TEXT("  觉醒 %d/%d"),CompletedAwakeningSteps,AwakeningSteps);
  if(!View.Status.IsEmpty())Summary+=TEXT("\n")+View.Status;
  UnitPinnedInformation->PresentUnit(Definition,FText::FromString(Summary),Health,Stats.MaxHealth,Mana,Stats.MaxMana);
  UnitPinnedInformation->PresentAttributes(View);
  UnitPinnedInformation->SetSaleAvailable(Controller&&Controller->CanSellInspectedHero(InspectedHero));
  UnitPinnedInformation->SetReturnAvailable(Controller&&Controller->CanWithdrawInspectedHero(InspectedHero));
  bUnitSidebarOpen=true;LayoutInformationViews();UpdateSidebarMotion(0.f);return;
 }
 if(auto* Dock=GetWidgetFromName(TEXT("UnitDetailDock")))Dock->SetVisibility(Visible?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
 if(!Definition)return;
 auto Set=[&](const TCHAR* Name,const FString& Value){if(auto* T=Cast<UTextBlock>(GetWidgetFromName(Name)))T->SetText(FText::FromString(Value));};
 if(bBoardPresentation)
 {
  Set(TEXT("Text_UnitHeading"),Definition->DisplayName.ToString());
  FString Meta=FString::Printf(TEXT("Lv.%d%s"),Level,Awakened?TEXT("  已觉醒"):TEXT(""));
  if(!Awakened&&CompletedAwakeningSteps>0)Meta+=FString::Printf(TEXT("  觉醒 %d / %d"),CompletedAwakeningSteps,AwakeningSteps);
  if(Definition->bEnhancedForm)Meta+=TEXT("  升格形态");
  Set(TEXT("Text_UnitMeta"),Meta);
  Set(TEXT("Text_UnitVitals"),FString::Printf(TEXT("生命 %.0f / %.0f\n法力 %.0f / %.0f"),Health,Stats.MaxHealth,Mana,Stats.MaxMana));
  const FString Speed=FString::Printf(TEXT("%s%.2f"),Definition->HeroKind==EEGHeroKind::Pardofelis?TEXT("×"):TEXT(""),Stats.AttackSpeed);
  Set(TEXT("Text_UnitStats"),FString::Printf(TEXT("攻击  %.0f\n防御  %.0f\n攻速  %s\n护盾  %.0f"),Stats.AttackPower,Stats.Defense,*Speed,Shield));
  if(auto* B=Cast<UProgressBar>(GetWidgetFromName(TEXT("BoardDetailHealth"))))B->SetPercent(Health/FMath::Max(1.f,Stats.MaxHealth));
  if(auto* B=Cast<UProgressBar>(GetWidgetFromName(TEXT("BoardDetailMana"))))B->SetPercent(Mana/FMath::Max(1.f,Stats.MaxMana));
 }
 else
 {
  Set(TEXT("Text_UnitHeading"),Definition->DisplayName.ToString()+FString::Printf(TEXT("\n等级 %d"),Level));
  FString Meta=Awakened?TEXT("已觉醒"):TEXT("");
  if(!Awakened&&CompletedAwakeningSteps>0)Meta+=FString::Printf(TEXT("  觉醒 %d / %d"),CompletedAwakeningSteps,AwakeningSteps);
  Meta+=Definition->bEnhancedForm?TEXT("\n升格形态"):TEXT("\n基础形态");
  Set(TEXT("Text_UnitMeta"),Meta);
  Set(TEXT("Text_UnitVitals"),FString::Printf(TEXT("生命 %.0f / %.0f\n法力 %.0f / %.0f"),Health,Stats.MaxHealth,Mana,Stats.MaxMana));
 }
 if(!bBoardPresentation){const FString Speed=FString::Printf(TEXT("%s%.2f"),Definition->HeroKind==EEGHeroKind::Pardofelis?TEXT("×"):TEXT(""),Stats.AttackSpeed);Set(TEXT("Text_UnitStats"),FString::Printf(TEXT("攻击 %.0f    防御 %.0f\n攻速 %s    射程 %.0f\n护盾 %.0f  %s"),Stats.AttackPower,Stats.Defense,*Speed,InspectedUnit.IsValid()?InspectedUnit->GetRuntimeAttackRange():Definition->GetAttackRange(),Shield,Combat?TEXT("战斗实时属性"):TEXT("等级基础属性预览")));}
 FString Details=Definition->GetSkillDetails(Level,Awakened).ToString();int32 Newline;if(Details.FindChar(TEXT('\n'),Newline))Details=Details.Mid(Newline+1);
 if(bBoardPresentation&&!View.Status.IsEmpty())Details=View.Status+TEXT("\n\n")+Details;
 // Numeric emphasis for the detail dock; EGEnsureNamedRichText falls back to the
 // plain block when the authored parent slot cannot be migrated safely.
 if(auto* RichDetail=EGEnsureNamedRichText(this,TEXT("Text_UnitDetail")))RichDetail->SetHighlightedText(FText::FromString(Details));
 else Set(TEXT("Text_UnitDetail"),Details);
 if(auto* Info=Cast<UEGHeroInfoWidget>(GetWidgetFromName(TEXT("DetailSkillInformation")))){Info->Present(Definition,FText::FromString(View.Status));if(auto* Old=GetWidgetFromName(TEXT("Text_UnitDetail")))Old->SetVisibility(ESlateVisibility::Collapsed);}
 if(auto* Image=Cast<UImage>(GetWidgetFromName(TEXT("UnitDetailPortrait"))))
 {
  auto* C=Cast<UEGOverlayWidgetController>(GetWidgetController());auto* Art=C?C->GetHeroArtwork(Definition):nullptr;
  Image->SetBrush(FEGCardPresentation::PortraitBrush(Art,FVector2D(140,196)));Image->SetVisibility(Art?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
  FEGCardPresentation::ApplyDefinitionLayers(WidgetTree,Image,Definition,Level);
 }
}
void UEGMainHUD::RefreshStagePreviews()
{
 auto* C=Cast<UEGOverlayWidgetController>(GetWidgetController());StagePreviews=C?C->GetStagePreviews():TArray<FEGStagePreviewView>();
 const auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 const bool bSevenStage=Mode&&Mode->RunDefinition&&Mode->RunDefinition->bSevenStageRules;
 auto* Scroll=Cast<UScrollBox>(GetWidgetFromName(TEXT("StagePreviewScroll")));if(!Scroll)return;
 if(SimpleStageHosts.IsEmpty())
 {
  for(const auto& B:PreviewButtons)if(B)B->OnSelected.RemoveDynamic(this,&ThisClass::SelectStagePreview);
  PreviewButtons.Reset();Scroll->ClearChildren();
 }
 auto Named=[](const TCHAR* Prefix,int32 Number){return FName(*FString::Printf(TEXT("%s_%d"),Prefix,Number));};
 while(SimpleStageHosts.Num()<StagePreviews.Num())
 {
  const int32 Number=SimpleStageHosts.Num()+1;
  auto* Host=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),Named(TEXT("StageSimpleHost"),Number));
  auto* Column=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Named(TEXT("StageSimpleColumn"),Number));
  Column->SetVisibility(ESlateVisibility::SelfHitTestInvisible);Host->SetContent(Column);
  if(auto* HostSlot=Cast<UScrollBoxSlot>(Scroll->AddChild(Host)))HostSlot->SetPadding(FMargin(0.f));
  SimpleStageHosts.Add(Host);SimpleStageColumns.Add(Column);
  auto Place=[Column](UWidget* Widget,FAnchors Anchors,FMargin Bounds,FVector2D Alignment,int32 Z=0)
  {
   auto* CanvasSlot=Column->AddChildToCanvas(Widget);CanvasSlot->SetAnchors(Anchors);CanvasSlot->SetOffsets(Bounds);CanvasSlot->SetAlignment(Alignment);CanvasSlot->SetZOrder(Z);
  };
  auto* Divider=WidgetTree->ConstructWidget<UImage>();Divider->SetBrush(FEGVisualStyle::Rounded(FLinearColor(1.f,1.f,1.f,.28f),0.f));Divider->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Divider,FAnchors(0.f,0.f),FMargin(0.f,8.f,1.f,200.f),FVector2D::ZeroVector);
  auto* Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Named(TEXT("StageSimpleTitle"),Number));
  Title->SetFont(FEGVisualStyle::PearlFont(17));Title->SetJustification(ETextJustify::Center);Title->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Title,FAnchors(0.f,0.f,1.f,0.f),FMargin(2.f,2.f,2.f,36.f),FVector2D::ZeroVector);
  auto MakeGroup=[this,Number,&Named,&Place](const TCHAR* GroupName,const TCHAR* IconName,const TCHAR* TextName,const TCHAR* TexturePath,float Top,bool bDark)
  {
   auto* Group=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Named(GroupName,Number));
   Group->SetBrush(FEGVisualStyle::Rounded(bDark?FLinearColor(0.f,0.f,0.f,.48f):FLinearColor::Transparent,0.f));
   Group->SetPadding(FMargin(2.f));Group->SetHorizontalAlignment(HAlign_Center);Group->SetVerticalAlignment(VAlign_Center);
   Group->SetVisibility(ESlateVisibility::Visible);
   auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();Row->SetVisibility(ESlateVisibility::HitTestInvisible);Group->SetContent(Row);
   auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(24.f);IconSize->SetHeightOverride(24.f);
   auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Named(IconName,Number));Icon->SetBrushFromTexture(FEGVisualStyle::Texture(TexturePath),false);Icon->SetVisibility(ESlateVisibility::HitTestInvisible);IconSize->SetContent(Icon);
   auto* IconSlot=Row->AddChildToHorizontalBox(IconSize);IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0.f,0.f,4.f,0.f));
   auto* Value=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Named(TextName,Number));Value->SetFont(FEGVisualStyle::PearlFont(16));Value->SetVisibility(ESlateVisibility::HitTestInvisible);
   Row->AddChildToHorizontalBox(Value)->SetVerticalAlignment(VAlign_Center);
   Place(Group,FAnchors(.5f,0.f),FMargin(0.f,Top,112.f,36.f),FVector2D(.5f,0.f),1);
  };
  MakeGroup(TEXT("StageSimpleCorruptionGroup"),TEXT("StageSimpleCorruptionIcon"),TEXT("StageSimpleCorruptionText"),TEXT("/Game/Assets/UI/Glass/Icons/T_UI_CorruptionSeed"),43.f,false);
  auto* Swords=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),Named(TEXT("StageSimpleBattleIcon"),Number));
  Swords->SetBrushFromTexture(FEGVisualStyle::Texture(TEXT("/Game/Assets/UI/Textures/PearlLilac/T_UI_Icon_Swords")),false);Swords->SetColorAndOpacity(FLinearColor::White);Swords->SetVisibility(ESlateVisibility::HitTestInvisible);
  Place(Swords,FAnchors(.5f,0.f),FMargin(0.f,82.f,54.f,54.f),FVector2D(.5f,0.f),1);
  auto* CopyLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Named(TEXT("StageSimpleCopyLabel"),Number));
  CopyLabel->SetFont(FEGVisualStyle::PearlFont(13));CopyLabel->SetJustification(ETextJustify::Center);CopyLabel->SetColorAndOpacity(FSlateColor(FLinearColor(.86f,.65f,1.f,1.f)));
  Place(CopyLabel,FAnchors(0.f,0.f,1.f,0.f),FMargin(0.f,139.f,0.f,24.f),FVector2D::ZeroVector,1);
  MakeGroup(TEXT("StageSimpleGoldGroup"),TEXT("StageSimpleGoldIcon"),TEXT("StageSimpleGoldText"),TEXT("/Game/Assets/UI/Textures/BoardTheme/Icons/icon_energy"),166.f,true);
 }
 const float ColumnWidth=FMath::Max(1.f,(FMath::Max(1.f,StageLayoutWidth)-40.f)/FMath::Max(1,StagePreviews.Num())-1.f);
 for(int32 I=0;I<SimpleStageHosts.Num();++I)
 {
  auto* Host=SimpleStageHosts[I].Get();auto* Column=SimpleStageColumns[I].Get();if(!Host||!Column)continue;
  const bool bValid=StagePreviews.IsValidIndex(I);Host->SetVisibility(bValid?ESlateVisibility::Visible:ESlateVisibility::Collapsed);if(!bValid)continue;
  const auto& View=StagePreviews[I];const int32 Number=I+1;
  Host->SetWidthOverride(ColumnWidth);Host->SetHeightOverride(220.f);Column->SetRenderOpacity(View.bCurrent?1.f:.45f);
  const bool bCopy=View.bCorruptedCopyEncounter;
  const bool bBoss=View.bCorruptionBossEncounter;
  const FText EncounterDetails=bCopy?FText::FromString(TEXT("侵蚀之律者以我方上阵英桀为蓝本制造侵蚀复制体；我方英桀仍然保留。敌人将在开战时揭示。")):
   bBoss?FText::FromString(TEXT("首领拥有两条独立生命。击空第一条生命后，失色壁垒持续 8 秒；有效战斗时间达到 60 秒后进入狂暴，每 5 秒伤害提高 10%。")):FText::GetEmpty();
  Host->SetToolTipText(EncounterDetails);
  if(auto* Label=Cast<UTextBlock>(GetWidgetFromName(Named(TEXT("StageSimpleCopyLabel"),Number))))
  {
   Label->SetText(bCopy?FText::FromString(TEXT("侵蚀复制")):bBoss?FText::FromString(TEXT("侵蚀首领")):FText::GetEmpty());
   Label->SetVisibility(bCopy||bBoss?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
   Label->SetToolTipText(Host->GetToolTipText());
  }
  if(auto* T=Cast<UTextBlock>(GetWidgetFromName(Named(TEXT("StageSimpleTitle"),Number))))T->SetText(FText::FromString(FString::Printf(TEXT("第%d回合"),View.StageNumber)));
  if(auto* T=Cast<UTextBlock>(GetWidgetFromName(Named(TEXT("StageSimpleCorruptionText"),Number))))
   T->SetText(View.bHasCorruptionGain?FText::FromString(FString::Printf(TEXT("+%d"),View.CorruptionGain)):FText::FromString(TEXT("—")));
  if(auto* Group=GetWidgetFromName(Named(TEXT("StageSimpleCorruptionGroup"),Number)))
   Group->SetToolTipText(bBoss?EncounterDetails:View.bHasCorruptionGain?FText::FromString(FString::Printf(TEXT("战败增加 %d  胜利减少 %d。%s"),View.CorruptionGain,View.CorruptionGain,
    bSevenStage?TEXT("\n前六关按开战时侵蚀度：低于40，敌方伤害减少5%；40至70不变；高于70，增加5%。敌人数不随侵蚀度变化。第七关采用首领规则。"):TEXT(""))):FText::GetEmpty());
  if(auto* T=Cast<UTextBlock>(GetWidgetFromName(Named(TEXT("StageSimpleGoldText"),Number))))T->SetText(FText::FromString(FString::Printf(TEXT("+%d"),View.CompletionGold)));
  if(auto* Group=GetWidgetFromName(Named(TEXT("StageSimpleGoldGroup"),Number)))Group->SetToolTipText(FText::FromString(TEXT("基础结算收入，额外收益以实际结果为准。")));
 }
}
void UEGMainHUD::SelectStagePreview(int32 Number)
{
 if(StagePreviews.IsValidIndex(Number-1))SelectedPreviewStage=Number;
}
