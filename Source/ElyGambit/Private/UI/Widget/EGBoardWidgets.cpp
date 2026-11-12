#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGShopCard.h"
#include "UI/Widget/EGDragFeedback.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/HUD/EGHUD.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGRichText.h"
#include "UI/EGCardPresentation.h"
#include "UI/EGPearlGoldLayout.h"
#include "Blueprint/WidgetTree.h"
#include "Components/PanelWidget.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/ScaleBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/Font.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGStigmataBattleComponent.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGStigmataDefinition.h"
#include "Placement/EGPlacementVisuals.h"
#include "Placement/EGBoardScene.h"

namespace
{
// Small, source-controlled shapes are rendered by UImage, so the
// function buttons do not depend on a system glyph or an external icon font.
UWidget* MakeRightFunctionContent(UWidgetTree* Tree,const TCHAR* Text,const TCHAR* IconKind)
{
    const FName RowName(*FString::Printf(TEXT("RightFunctionContent_%s"),IconKind));
    auto* Row=Tree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),RowName);
    auto* IconSize=Tree->ConstructWidget<USizeBox>();
    IconSize->SetWidthOverride(24.f);IconSize->SetHeightOverride(24.f);
    auto* Icon=Tree->ConstructWidget<UCanvasPanel>();IconSize->SetContent(Icon);
    Row->AddChildToHorizontalBox(IconSize)->SetVerticalAlignment(VAlign_Center);
    auto AddMark=[Tree,Icon](FVector2D Position,FVector2D Size,float Radius=1.f)
    {
        auto* Mark=Tree->ConstructWidget<UImage>();
        Mark->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Plum,Radius));
        Mark->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* Slot=Icon->AddChildToCanvas(Mark);Slot->SetPosition(Position);Slot->SetSize(Size);
    };
    if(FCString::Strcmp(IconKind,TEXT("Statistics"))==0)
    {
        AddMark(FVector2D(2,19),FVector2D(20,2));
        AddMark(FVector2D(4,13),FVector2D(4,6));
        AddMark(FVector2D(10,8),FVector2D(4,11));
        AddMark(FVector2D(16,3),FVector2D(4,16));
    }
    else if(FCString::Strcmp(IconKind,TEXT("Menu"))==0)
    {
        AddMark(FVector2D(3,5),FVector2D(18,2));AddMark(FVector2D(3,11),FVector2D(18,2));AddMark(FVector2D(3,17),FVector2D(18,2));
    }
    else
    {
        // Speaker body and two sound bars.
        AddMark(FVector2D(2,9),FVector2D(5,7));
        AddMark(FVector2D(7,6),FVector2D(5,13));
        AddMark(FVector2D(15,8),FVector2D(2,9));
        AddMark(FVector2D(19,5),FVector2D(2,15));
    }
    auto* Label=Tree->ConstructWidget<UTextBlock>();
    Label->SetText(FText::FromString(Text));Label->SetFont(FEGVisualStyle::BodyFont(17));
    Label->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* LabelSlot=Row->AddChildToHorizontalBox(Label);
    LabelSlot->SetPadding(FMargin(5,0,0,0));LabelSlot->SetVerticalAlignment(VAlign_Center);
    Row->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Row;
}

// Existing WBP assets can already contain the original three metric buttons.
// Search the actual drawer rather than assuming it was built by the C++ fallback.
void FindStatisticsMetricRow(UWidget* Widget,UHorizontalBox*& OutRow,UEGBoardButton*& OutSignet)
{
    if(!Widget)return;
    if(auto* Button=Cast<UEGBoardButton>(Widget);Button&&Button->Action==23)
    {
        if(auto* Row=Cast<UHorizontalBox>(Button->GetParent()))
        {
            if(Button->Index>=0&&Button->Index<=2&&!OutRow)OutRow=Row;
            if(Button->Index==3)OutSignet=Button;
        }
    }
    if(auto* Panel=Cast<UPanelWidget>(Widget))
        for(int32 I=0;I<Panel->GetChildrenCount();++I)FindStatisticsMetricRow(Panel->GetChildAt(I),OutRow,OutSignet);
}
}

void UEGMainHUD::SetupBoardView()
{
    auto Bind=[this](UWidgetTree* Tree)
    {
        if(!Tree)return;
        Tree->ForEachWidget([this](UWidget* Widget)
        {
            if(auto* Button=Cast<UEGBoardButton>(Widget))
            {
                Button->OnClicked.AddUniqueDynamic(Button,&UEGBoardButton::Invoke);
                Button->OnRequested.AddUniqueDynamic(this,&ThisClass::OnBoardAction);
            }
        });
    };
    Bind(WidgetTree);if(DrawPanel)Bind(DrawPanel->WidgetTree);
    SetupAudioControls();
    SetupRightSideControls();

    if(auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget))
    {
        auto AddInfo=[&](TObjectPtr<UEGHeroInfoWidget>& W,TSubclassOf<UEGHeroInfoWidget> Class,FVector2D Position,FVector2D Size,int32 Layer)
        {
            if(W)return;
            W=CreateWidget<UEGHeroInfoWidget>(GetOwningPlayer(),Class?Class.Get():UEGHeroInfoWidget::StaticClass());
            auto* InfoSlot=Root->AddChildToCanvas(W);InfoSlot->SetPosition(Position);InfoSlot->SetSize(Size);InfoSlot->SetZOrder(Layer);W->SetVisibility(ESlateVisibility::Collapsed);
        };
        AddInfo(SkillInformation,SkillInformationClass,FVector2D(32,100),FVector2D(380,300),62);
        AddInfo(ShopDragInformation,ShopDragInformationClass,FVector2D(24,80),FVector2D(616,420),61);
        // Saved native-backed WBP classes keep the three interaction layouts editable.
        if(CardPreviewInformationClass)AddInfo(CardPreviewInformation,CardPreviewInformationClass,FVector2D::ZeroVector,FVector2D(820,560),66);
        if(UnitHoverInformationClass)AddInfo(UnitHoverInformation,UnitHoverInformationClass,FVector2D::ZeroVector,FVector2D(440,780),63);
        if(UnitPinnedInformationClass)AddInfo(UnitPinnedInformation,UnitPinnedInformationClass,FVector2D::ZeroVector,FVector2D(460,1080),64);
        for(auto* Info:{CardPreviewInformation.Get(),UnitPinnedInformation.Get()})if(Info)Info->OnCloseRequested.AddUniqueDynamic(this,&ThisClass::CloseInformationViews);
        if(UnitPinnedInformation)UnitPinnedInformation->OnSellRequested.AddUniqueDynamic(this,&ThisClass::SellInspectedInformation);
    }
    if(auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);Root&&!DragFeedback)
    {
        DragFeedback=CreateWidget<UEGDragFeedback>(GetOwningPlayer());
        if(DragFeedback)
        {
            auto* FeedbackSlot=Root->AddChildToCanvas(DragFeedback);FeedbackSlot->SetAnchors(FAnchors(0,0,1,1));FeedbackSlot->SetOffsets(FMargin(0));FeedbackSlot->SetZOrder(55);
            DragFeedback->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if(auto* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);Root&&!GetWidgetFromName(TEXT("BoardInfoDock")))
    {
        auto* Dock=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BoardInfoDock"));Dock->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Srgb(36,27,56,.80f),12.f,EGPearlGold::GoldEdge,1.f));Dock->SetPadding(FMargin(16,16,16,22));Dock->SetVisibility(ESlateVisibility::Collapsed);
        Dock->SetClipping(EWidgetClipping::ClipToBounds);
        auto* DS=Root->AddChildToCanvas(Dock);DS->SetPosition(FVector2D(32,116));DS->SetSize(FVector2D(EGPearlGold::HoverMaxWidth,680));DS->SetZOrder(60);
        Dock->SetHorizontalAlignment(HAlign_Fill);Dock->SetVerticalAlignment(VAlign_Fill);
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();Dock->SetContent(Column);
        auto* Art=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BoardInfoArt"));
        Art->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* ArtSize=WidgetTree->ConstructWidget<USizeBox>();ArtSize->SetWidthOverride(220);ArtSize->SetHeightOverride(308);ArtSize->SetContent(Art);Column->AddChildToVerticalBox(ArtSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Line=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BoardInfoLine"));Line->SetBrush(FEGVisualStyle::GoldLine());Line->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* LineSize=WidgetTree->ConstructWidget<USizeBox>();LineSize->SetWidthOverride(272);LineSize->SetHeightOverride(8);LineSize->SetContent(Line);Column->AddChildToVerticalBox(LineSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoardInfoTitle"));Title->SetFont(FEGVisualStyle::SerifFont(24));Title->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Title->SetJustification(ETextJustify::Center);Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(4,14,4,8));
        auto* Scroll=WidgetTree->ConstructWidget<UScrollBox>();Column->AddChildToVerticalBox(Scroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* Body=WidgetTree->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass(),TEXT("BoardInfoBody"));Body->ApplyTextStyle(FEGVisualStyle::BodyFont(17),EGPearlGold::Plum);Body->SetAutoWrapText(true);Body->SetWrapTextAt(300);Body->SetClipping(EWidgetClipping::ClipToBounds);Scroll->SetClipping(EWidgetClipping::ClipToBounds);Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);Scroll->AddChild(Body);
    }

    BoardRun=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    if(DrawPanel&&BoardRun.IsValid())for(int32 I=0;I<4;++I)
    {
        auto* Host=Cast<UPanelWidget>(DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopSlot_%d"),I))));
        if(!Host||Host->GetChildrenCount()>0)continue;
        auto* ShopClass=LoadClass<UEGShopCard>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_ShopCard.WBP_ShopCard_C"));
        auto* Card=CreateWidget<UEGShopCard>(GetOwningPlayer(),ShopClass?ShopClass:UEGShopCard::StaticClass());
        if(!Card)continue;
        Host->AddChild(Card);
        if(auto* OfferSlot=Cast<UCanvasPanelSlot>(Card->Slot)){OfferSlot->SetAnchors(FAnchors(0,0,1,1));OfferSlot->SetOffsets(FMargin(0));}
        Card->Configure(I,BoardRun->GetCards(),this);
    }
    if(BoardRun.IsValid())BoardRun->OnCombatStatisticsChanged.AddUniqueDynamic(this,&ThisClass::MarkBattleStatisticsDirty);
    auto AddStatisticsMetric=[this](UHorizontalBox* Metrics,int32 Index,const TCHAR* Caption)
    {
        auto* B=Index==3?WidgetTree->ConstructWidget<UEGBoardButton>(UEGBoardButton::StaticClass(),TEXT("StatisticsMetric_SignetDamage")):
            WidgetTree->ConstructWidget<UEGBoardButton>();
        B->Action=23;B->Index=Index;B->SetStyle(FEGVisualStyle::PearlButton());
        if(Index==3)B->SetToolTipText(FText::FromString(TEXT("按刻印统计可确认的实际伤害，包含护盾损失")));
        auto* L=WidgetTree->ConstructWidget<UTextBlock>();L->SetFont(FEGVisualStyle::PearlFont(15));L->SetText(FText::FromString(Caption));L->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));B->SetContent(L);
        B->OnClicked.AddUniqueDynamic(B,&UEGBoardButton::Invoke);B->OnRequested.AddUniqueDynamic(this,&ThisClass::OnBoardAction);
        Metrics->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    };
    if(auto* Dock=Cast<UBorder>(GetWidgetFromName(TEXT("BoardStatistics")));Dock&&!GetWidgetFromName(TEXT("BoardStatisticsTitle")))
    {
        Dock->SetPadding(FMargin(18));
        auto* Box=WidgetTree->ConstructWidget<UVerticalBox>();Dock->SetContent(Box);
        auto* Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoardStatisticsTitle"));Title->SetText(FText::FromString(TEXT("本场统计")));Title->SetFont(FEGVisualStyle::PearlFont(20));Title->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(2,0,2,8));
        auto* Line=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BoardStatisticsLine"));Line->SetBrush(FEGVisualStyle::GoldLine());Line->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* LineSize=WidgetTree->ConstructWidget<USizeBox>();LineSize->SetWidthOverride(320);LineSize->SetHeightOverride(8);LineSize->SetContent(Line);Box->AddChildToVerticalBox(LineSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Metrics=WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(Metrics)->SetPadding(FMargin(0,8,0,4));
        int32 Metric=0;
        for(const TCHAR* Caption:{TEXT("伤害"),TEXT("承伤"),TEXT("治疗"),TEXT("刻印伤害")})
            AddStatisticsMetric(Metrics,Metric++,Caption);
        for(int32 I=0;I<9;++I)
        {
            auto* L=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("StatisticsLabel_%d"),I)));L->SetFont(FEGVisualStyle::PearlFont(18));L->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Box->AddChildToVerticalBox(L)->SetPadding(FMargin(2,7,2,2));
            auto* Bar=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),FName(*FString::Printf(TEXT("StatisticsBar_%d"),I)));
            Bar->SetWidgetStyle(FEGVisualStyle::Progress());Bar->SetFillColorAndOpacity(EGPearlGold::Champagne);
            auto* BarHeight=WidgetTree->ConstructWidget<USizeBox>();BarHeight->SetHeightOverride(8);BarHeight->SetContent(Bar);Box->AddChildToVerticalBox(BarHeight);
        }
    }
    if(auto* Dock=GetWidgetFromName(TEXT("BoardStatistics")))
    {
        UHorizontalBox* Metrics=nullptr;UEGBoardButton* Signet=nullptr;
        FindStatisticsMetricRow(Dock,Metrics,Signet);
        if(Metrics&&!Signet)AddStatisticsMetric(Metrics,3,TEXT("刻印伤害"));
        if(Metrics)
            for(int32 I=0;I<Metrics->GetChildrenCount();++I)
                if(auto* Button=Cast<UEGBoardButton>(Metrics->GetChildAt(I));Button&&Button->Action==23)
                    if(auto* Label=Cast<UTextBlock>(Button->GetContent()))Label->SetFont(FEGVisualStyle::PearlFont(15));
    }
    RefreshBoardView();
}

void UEGMainHUD::SetupRightSideControls()
{
    auto* Root=WidgetTree?Cast<UCanvasPanel>(WidgetTree->RootWidget):nullptr;
    if(!Root)return;
    if(auto* Statistics=Cast<UButton>(GetWidgetFromName(TEXT("Button_Statistics"))))
    {
        if(!Statistics->GetContent()||Statistics->GetContent()->GetFName()!=TEXT("RightFunctionContent_Statistics"))
            Statistics->SetContent(MakeRightFunctionContent(WidgetTree,TEXT("统计"),TEXT("Statistics")));
        Statistics->SetToolTipText(FText::FromString(TEXT("查看战斗统计")));
    }
    if(AudioButton)
    {
        if(!AudioButton->GetContent()||AudioButton->GetContent()->GetFName()!=TEXT("RightFunctionContent_Audio"))
            AudioButton->SetContent(MakeRightFunctionContent(WidgetTree,TEXT("音量"),TEXT("Audio")));
        AudioButton->SetToolTipText(FText::FromString(TEXT("调整音量")));
    }
    auto* Menu=Cast<UEGBoardButton>(GetWidgetFromName(TEXT("Button_PauseMenu")));
    if(!Menu)
    {
        Menu=WidgetTree->ConstructWidget<UEGBoardButton>(UEGBoardButton::StaticClass(),TEXT("Button_PauseMenu"));
        Menu->SetStyle(FEGVisualStyle::PearlButton());Menu->SetContent(MakeRightFunctionContent(WidgetTree,TEXT("菜单"),TEXT("Menu")));
        auto* MenuSlot=Root->AddChildToCanvas(Menu);MenuSlot->SetAnchors(FAnchors(1.f,0.f));MenuSlot->SetPosition(FVector2D(-350.f,24.f));MenuSlot->SetSize(FVector2D(98.f,40.f));MenuSlot->SetZOrder(30);
    }
    Menu->Action=90;Menu->SetToolTipText(FText::FromString(TEXT("暂停游戏")));
    Menu->OnClicked.AddUniqueDynamic(Menu,&UEGBoardButton::Invoke);Menu->OnRequested.AddUniqueDynamic(this,&ThisClass::OnBoardAction);
    // Also hide a legacy instance if an existing widget tree contains one.
    if(auto* Signet=GetWidgetFromName(TEXT("Button_SignetSidebar")))Signet->SetVisibility(ESlateVisibility::Collapsed);
}

void UEGMainHUD::SetupAudioControls()
{
    auto* Root=WidgetTree?Cast<UCanvasPanel>(WidgetTree->RootWidget):nullptr;
    if(!Root)return;
    if(!AudioButton)
    {
        AudioButton=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("AudioVolumeButton"));
        AudioButton->SetStyle(FEGVisualStyle::PearlButton());
        auto* Label=WidgetTree->ConstructWidget<UTextBlock>();
        Label->SetText(FText::FromString(TEXT("音量")));Label->SetFont(FEGVisualStyle::PearlFont(18));
        Label->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Label->SetJustification(ETextJustify::Center);
        AudioButton->SetContent(Label);
        auto* AudioButtonSlot=Root->AddChildToCanvas(AudioButton);AudioButtonSlot->SetAnchors(FAnchors(1.f,0.f));
        AudioButtonSlot->SetPosition(FVector2D(-130.f,24.f));AudioButtonSlot->SetSize(FVector2D(98.f,40.f));AudioButtonSlot->SetZOrder(30);
    }
    if(!AudioPanel)
    {
        AudioPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("AudioVolumePanel"));
        AudioPanel->SetBrush(FEGVisualStyle::PearlPanel());AudioPanel->SetPadding(FMargin(14.f));
        AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
        auto* AudioPanelSlot=Root->AddChildToCanvas(AudioPanel);AudioPanelSlot->SetAnchors(FAnchors(1.f,0.f));
        AudioPanelSlot->SetAlignment(FVector2D(1.f,0.f));AudioPanelSlot->SetPosition(FVector2D(-150.f,72.f));
        AudioPanelSlot->SetSize(FVector2D(280.f,120.f));AudioPanelSlot->SetZOrder(70);
        auto* Column=WidgetTree->ConstructWidget<UVerticalBox>();AudioPanel->SetContent(Column);
        auto AddRow=[this,Column](const TCHAR* Caption,TObjectPtr<USlider>& Slider,TObjectPtr<UTextBlock>& Value)
        {
            auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>();
            Column->AddChildToVerticalBox(Row)->SetPadding(FMargin(0.f,4.f,0.f,8.f));
            auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetText(FText::FromString(Caption));
            Label->SetFont(FEGVisualStyle::PearlFont(16));Label->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));
            Row->AddChildToHorizontalBox(Label)->SetPadding(FMargin(0.f,0.f,10.f,0.f));
            auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(110.f);
            Slider=WidgetTree->ConstructWidget<USlider>();Slider->SetStepSize(.01f);Size->SetContent(Slider);
            Row->AddChildToHorizontalBox(Size)->SetVerticalAlignment(VAlign_Center);
            Value=WidgetTree->ConstructWidget<UTextBlock>();Value->SetFont(FEGVisualStyle::PearlFont(16));
            Value->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));
            Row->AddChildToHorizontalBox(Value)->SetPadding(FMargin(8.f,0.f,0.f,0.f));
        };
        AddRow(TEXT("音乐"),MusicVolumeSlider,MusicVolumeText);
        AddRow(TEXT("界面音效"),UISoundVolumeSlider,UISoundVolumeText);
    }
    AudioButton->OnClicked.AddUniqueDynamic(this,&ThisClass::ToggleAudioControls);
    MusicVolumeSlider->OnValueChanged.AddUniqueDynamic(this,&ThisClass::MusicVolumeChanged);
    UISoundVolumeSlider->OnValueChanged.AddUniqueDynamic(this,&ThisClass::UISoundVolumeChanged);
}

void UEGMainHUD::ToggleAudioControls()
{
    if(!AudioPanel||bDragActive)return;
    if(AudioPanel->IsVisible()){AudioPanel->SetVisibility(ESlateVisibility::Collapsed);return;}
    if(auto* Audio=UEGGameAudioComponent::FindFor(this))
    {
        const float Music=Audio->GetMusicVolume(),UI=Audio->GetUISoundVolume();
        MusicVolumeSlider->SetValue(Music);UISoundVolumeSlider->SetValue(UI);
        MusicVolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Music*100.f))));
        UISoundVolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(UI*100.f))));
        bStatisticsSidebarOpen=false;UpdateSidebarMotion(0.f);
        AudioPanel->SetVisibility(ESlateVisibility::Visible);
    }
}

void UEGMainHUD::MusicVolumeChanged(float Value)
{
    if(MusicVolumeText)MusicVolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Value*100.f))));
    if(auto* Audio=UEGGameAudioComponent::FindFor(this))Audio->SetMusicVolume(Value);
}

void UEGMainHUD::UISoundVolumeChanged(float Value)
{
    if(UISoundVolumeText)UISoundVolumeText->SetText(FText::FromString(FString::Printf(TEXT("%d%%"),FMath::RoundToInt(Value*100.f))));
    if(auto* Audio=UEGGameAudioComponent::FindFor(this))Audio->SetUISoundVolume(Value);
}

void UEGMainHUD::HandleOutsideClick(FVector2D Position)
{
    if(!bBoardPresentation)return;
    if(AudioPanel&&!IsOver(AudioPanel,Position)&&!IsOver(AudioButton,Position))AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
    if(!IsOver(GetWidgetFromName(TEXT("CompanionCatalogue")),Position)&&!IsOver(GetWidgetFromName(TEXT("Button_Companions")),Position))
        if(auto* Catalogue=GetWidgetFromName(TEXT("CompanionCatalogue")))Catalogue->SetVisibility(ESlateVisibility::Collapsed);
    if(IsOver(CardPreviewInformation,Position)||IsOverUnitInspectionContent(Position))return;
    if(!IsOver(GetWidgetFromName(TEXT("BoardInfoDock")),Position)&&!IsOver(SkillInformation,Position))HideCardInformation(true);
    auto OverShopControl=[this,Position]()
    {
        if(!IsOver(Button_ToggleDrawPanel,Position))
        {
            for(int32 I=0;I<4;++I)if(DrawPanel&&IsOver(DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopSlot_%d"),I))),Position))return true;
            if(DrawPanel&&(IsOver(DrawPanel->GetWidgetFromName(TEXT("Button_ShopLock")),Position)||IsOver(DrawPanel->GetWidgetFromName(TEXT("Button_RefreshShop")),Position)))return true;
            return false;
        }
        return true;
    };
    if(bDrawExpanded&&!OverShopControl()){bDrawExpanded=false;RefreshPanelVisibility();}
    if(bStageExpanded&&!IsOver(Button_Stage,Position)&&!IsOver(GetWidgetFromName(TEXT("StagePreviewPanel")),Position))
    {bStageExpanded=false;RefreshPanelVisibility();}
    const bool bOnFunctionButton=IsOver(GetWidgetFromName(TEXT("Button_Statistics")),Position);
    if(!bOnFunctionButton)
        if(auto* Dock=GetWidgetFromName(TEXT("BoardStatistics"));Dock&&!IsOver(Dock,Position))
        {bStatisticsSidebarOpen=false;UpdateSidebarMotion(0.f);}
}
void UEGMainHUD::OnBoardAction(int32 Action,int32 Index)
{
    if(Action==90){if(auto* PC=Cast<AEGBoardPlayerController>(GetOwningPlayer()))PC->OpenPauseMenu();return;}
    auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    auto* Manager=GM?GM->GetCards():nullptr;if(!Manager)return;
    if(Action==10){if(Manager->BuyShopSlot(Index))UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Purchase);}
    else if(Action==11){if(Manager->RefreshShop())UEGGameAudioComponent::PlayUI(this,EEGGameAudioCue::Refresh);}
    else if(Action==12){if(Manager->ToggleShopLock())UEGGameAudioComponent::PlayUI(this,Manager->IsShopLocked()?EEGGameAudioCue::Lock:EEGGameAudioCue::Unlock);}

    else if(Action==20)
    {
        if(AudioPanel)AudioPanel->SetVisibility(ESlateVisibility::Collapsed);
        bStatisticsSidebarOpen=!bStatisticsSidebarOpen;
        UpdateSidebarMotion(0.f);
        if(bStatisticsSidebarOpen)bBattleStatisticsDirty=true;
    }
    else if(Action==23){StatisticsMetric=FMath::Clamp(Index,0,3);RefreshBattleStatistics();}
    else if(Action==40)
    {
        if(bDragActive)return;
        if(auto* Catalogue=GetWidgetFromName(TEXT("CompanionCatalogue")))
        {
            const bool bOpen=!Catalogue->IsVisible();
            CloseContextPanels();CloseHeroDetails();HideCardInformation(true);
            Catalogue->SetVisibility(bOpen?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::Collapsed);
        }
    }
    else if(Action==41&&InspectableCompanions.IsValidIndex(Index))
    {
        auto* Definition=InspectableCompanions[Index].Get();
        auto* Artwork=FEGCardPresentation::ArtworkForHero(Definition);
        if(!Definition||!Artwork||bDragActive)return;
        if(auto* Catalogue=GetWidgetFromName(TEXT("CompanionCatalogue")))Catalogue->SetVisibility(ESlateVisibility::Collapsed);
        const FText Name=Definition->DisplayName;
        FString Body=Definition->GetSkillDetails(1,false).ToString();
        Body.ReplaceInline(*Definition->DisplayName.ToString(),*Name.ToString());
        ShowHeroInformation(Definition,true);
    }
    else if(Action==42)
    {if(auto* Catalogue=GetWidgetFromName(TEXT("CompanionCatalogue")))Catalogue->SetVisibility(ESlateVisibility::Collapsed);}

    else if(Action==30&&InspectedHero.IsValid())
        if(auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController()))
        {if(Controller->CanWithdrawInspectedHero(InspectedHero))Controller->RequestWithdrawInspectedHero(InspectedHero);}
    RefreshBoardView();
}
void UEGMainHUD::ShowHandInformation(FGuid Id,bool bPin)
{
    if(const auto* Card=AppliedCards.Find(Id))
    {
        const FText Level=Card->Level>1?FText::FromString(FString::Printf(TEXT("等级 %d"),Card->Level)):FText::GetEmpty();
        ShowHeroInformation(Card->HeroDefinition,bPin,TEXT("Hand"),FindCardWidget(Id),Level);
    }
}
void UEGMainHUD::ShowHeroInformation(UEGHeroDefinition* D,bool bPin,FName Owner,const UWidget* Anchor,const FText& Extra)
{
    if(!D||!SkillInformation||!bBoardPresentation||bDragActive||(!bPin&&bInformationPinned))return;
    if(auto* Old=GetWidgetFromName(TEXT("BoardInfoDock")))Old->SetVisibility(ESlateVisibility::Collapsed);
    if(bPin&&CardPreviewInformation)
    {
        ClearInspectionBindings();
        for(auto* W:{SkillInformation.Get(),UnitHoverInformation.Get(),UnitPinnedInformation.Get()})if(W)W->SetVisibility(ESlateVisibility::Collapsed);
        bInformationPinned=true;InformationOwner=Owner;
        CardPreviewInformation->Present(D,Extra);
        CardPreviewInformation->SetVisibility(ESlateVisibility::SelfHitTestInvisible);LayoutInformationViews();return;
    }
    if(Owner==TEXT("World")&&UnitHoverInformation)
    {
        SkillInformation->SetVisibility(ESlateVisibility::Collapsed);
        UnitHoverInformation->Present(D,Extra);UnitHoverInformation->SetVisibility(ESlateVisibility::HitTestInvisible);
        InformationOwner=Owner;LayoutInformationViews();return;
    }
    if(UnitHoverInformation)UnitHoverInformation->SetVisibility(ESlateVisibility::Collapsed);
    bInformationPinned=bPin;InformationOwner=Owner;
    SkillInformation->Present(D,Extra);SkillInformation->SetVisibility(ESlateVisibility::HitTestInvisible);
    if(auto* InfoSlot=Cast<UCanvasPanelSlot>(SkillInformation->Slot))
    {
        const FVector2D View=GetCachedGeometry().GetLocalSize();if(View.X<=0||View.Y<=0)return;
        const auto P=GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
        FVector2D A=P,B=P;
        if(Anchor){const auto& G=Anchor->GetCachedGeometry();A=GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(FVector2D::ZeroVector));B=GetCachedGeometry().AbsoluteToLocal(G.LocalToAbsolute(G.GetLocalSize()));}
        auto* Panel=SkillInformation->GetWidgetFromName(TEXT("SkillPanel"));
        auto* PanelSlot=Panel?Cast<UCanvasPanelSlot>(Panel->Slot):nullptr;
        const FString Effect=D->GetPlayerFacingSkillEffect().ToString();
        const float Margin=FMath::Min(16.f,FMath::Max(0.f,FMath::Min(View.X,View.Y)*.05f));
        const float AvailableW=FMath::Max(1.f,View.X-2.f*Margin),AvailableH=FMath::Max(1.f,View.Y-2.f*Margin);
        const float W=FMath::Min(Effect.Len()>200?600.f:380.f,AvailableW);
        const float CharactersPerLine=FMath::Max(1.f,(W-48.f)/19.f);
        int32 Newlines=0;for(const TCHAR Character:Effect)if(Character==TEXT('\n'))++Newlines;
        const float EstimatedHeight=160.f+(FMath::CeilToFloat(Effect.Len()/CharactersPerLine)+Newlines)*27.f+(Extra.IsEmpty()?0.f:120.f);
        const bool bOfferFullView=Owner==TEXT("Shop")||(Owner==TEXT("Hand")&&(Effect.Len()>200||EstimatedHeight>AvailableH));
        if(auto* Scroll=Cast<UScrollBox>(SkillInformation->GetWidgetFromName(TEXT("TooltipContentScroll"))))Scroll->ScrollToStart();
        if(auto* Hint=Cast<UTextBlock>(SkillInformation->GetWidgetFromName(TEXT("TooltipReadHint"))))
        {
            Hint->SetText(FText::FromString(Owner==TEXT("Shop")?TEXT("滚动鼠标滚轮查看完整说明"):TEXT("左键卡牌查看完整说明，可滚动")));
            Hint->SetVisibility(bOfferFullView?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
        }
        const float H=FMath::Min(AvailableH,FMath::Max(270.f,EstimatedHeight+(bOfferFullView?32.f:0.f)));
        if(PanelSlot)PanelSlot->SetSize(FVector2D(W,H));
        float X=B.X+Margin;
        if(X+W>View.X-Margin)X=A.X-W-Margin;
        InfoSlot->SetPosition(FVector2D(FMath::Clamp(X,Margin,FMath::Max(Margin,View.X-W-Margin)),FMath::Clamp(A.Y,Margin,FMath::Max(Margin,View.Y-H-Margin))));InfoSlot->SetSize(FVector2D(W,H));
    }
}
bool UEGMainHUD::ScrollShopInformation(float WheelDelta)
{
    if(InformationOwner!=TEXT("Shop")||!SkillInformation||!SkillInformation->IsVisible()||FMath::IsNearlyZero(WheelDelta))return false;
    if(auto* Scroll=Cast<UScrollBox>(SkillInformation->GetWidgetFromName(TEXT("TooltipContentScroll"))))
    {
        Scroll->SetScrollOffset(FMath::Clamp(Scroll->GetScrollOffset()-WheelDelta*64.f,0.f,Scroll->GetScrollOffsetOfEnd()));
        return true;
    }
    return false;
}
void UEGMainHUD::SetShopDragPresentation(int32 OfferIndex,UEGCardDefinition* D)
{
    ShopDragSlot=OfferIndex;
    bShopOfferDrag=true;
    // A shop offer is only ever purchased or returned: both sale plates collapse
    // immediately, stop hit testing and evaluate to zero for this drag source.
    SetSellZoneFeedback(0);
    if(D)SetShopContextDragPresentation(D->HeroDefinition,D->Artwork);
    if(DrawPanel)if(auto* Card=DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopSlot_%d"),OfferIndex))))Card->SetRenderOpacity(.25f);
}
void UEGMainHUD::ShowWorldInformation(UEGHeroDefinition* D,const FText& Summary,float Health,float MaxHealth,float Mana,float MaxMana)
{
    if(!D||!SkillInformation||!bBoardPresentation||bInformationPinned||bDragActive)return;
    if(!UnitHoverInformation){ShowHeroInformation(D,false,TEXT("World"),nullptr,Summary);return;}
    if(auto* Old=GetWidgetFromName(TEXT("BoardInfoDock")))Old->SetVisibility(ESlateVisibility::Collapsed);
    SkillInformation->SetVisibility(ESlateVisibility::Collapsed);
    UnitHoverInformation->PresentUnit(D,Summary,Health,MaxHealth,Mana,MaxMana);
    UnitHoverInformation->SetVisibility(ESlateVisibility::HitTestInvisible);
    InformationOwner=TEXT("World");LayoutInformationViews();
}
void UEGMainHUD::SetShopContextDragPresentation(UEGHeroDefinition* D,UTexture2D* Artwork)
{
    if(!bBoardPresentation)return;
    // This flag controls presentation only. An owned card never acquires a shop
    // slot/revision and keeps its original deployment, merge and sale authority.
    bShopContextDragPresentationActive=true;
    if(ShopDragInformation)
    {
        ShopDragInformation->Present(D,FText::GetEmpty());
        if(auto* Image=Cast<UImage>(ShopDragInformation->GetWidgetFromName(TEXT("InfoCard"))))
        {
            auto* Art=FEGCardPresentation::ArtworkForHero(D);
            if(!Art)Art=Artwork;
            if(!Art&&D)Art=D->GetFormArtwork().LoadSynchronous();
            Image->SetBrush(FEGCardPresentation::PortraitBrush(Art,FVector2D(300,420)));
        }
        ShopDragInformation->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    UpdateShopTransition(0.f);
}
void UEGMainHUD::ShowCardInformation(UTexture2D* Art,const FText& Heading,const FText& Body,bool bPin,FName Owner)
{
    if(!bBoardPresentation||bDragActive||(!bPin&&bInformationPinned))return;
    bInformationPinned=bPin;
    InformationOwner=Owner;
    if(auto* Image=Cast<UImage>(GetWidgetFromName(TEXT("BoardInfoArt"))))
    {
        Image->SetBrush(FEGCardPresentation::PortraitBrush(Art,FVector2D(220,308)));
        Image->SetVisibility(Art?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    }
    if(auto* T=Cast<UTextBlock>(GetWidgetFromName(TEXT("BoardInfoTitle"))))if(!T->GetText().EqualTo(Heading))T->SetText(Heading);
    // BoardInfoBody shows CardInformation::Effect; keep it as rich text so damage,
    // percentage and duration values are emphasised without changing data assets.
    if(auto* RichBody=EGEnsureNamedRichText(this,TEXT("BoardInfoBody")))
    {
        if(!RichBody->GetPlainText().EqualTo(Body))RichBody->SetHighlightedText(Body);
    }
    else if(auto* T=Cast<UTextBlock>(GetWidgetFromName(TEXT("BoardInfoBody"))))if(!T->GetText().EqualTo(Body))T->SetText(Body);
    if(auto* Dock=GetWidgetFromName(TEXT("BoardInfoDock")))
    {
        // Hover is transparent to input. Once pinned, children (the skill scroll)
        // receive input while the controller still blocks placement under it.
        Dock->SetVisibility(bPin?ESlateVisibility::SelfHitTestInvisible:ESlateVisibility::HitTestInvisible);
        if(auto* InfoSlot=Cast<UCanvasPanelSlot>(Dock->Slot))
        {
            const FVector2D View=GetCachedGeometry().GetLocalSize();
            if(View.X>0&&View.Y>0)
            {
                const FVector2D Pointer=GetCachedGeometry().AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
                const float W=FMath::Min(EGPearlGold::HoverMaxWidth,View.X-32.f);
                const float H=FMath::Min(680.f,View.Y-32.f);
                float X=Pointer.X+20.f;
                if(X+W>View.X-16.f)X=Pointer.X-W-20.f;
                InfoSlot->SetSize(FVector2D(W,H));
                InfoSlot->SetPosition(FVector2D(FMath::Clamp(X,16.f,View.X-W-16.f),FMath::Clamp(Pointer.Y-H*.4f,16.f,View.Y-H-16.f)));
            }
        }
    }
    if(auto* Dock=GetWidgetFromName(TEXT("UnitDetailDock")))Dock->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGMainHUD::HideCardInformation(bool bForce,FName Owner)
{
    if(!Owner.IsNone()&&InformationOwner!=Owner)return;
    if(bInformationPinned&&!bForce)return;bInformationPinned=false;
    InformationOwner=NAME_None;
    if(SkillInformation)SkillInformation->SetVisibility(ESlateVisibility::Collapsed);
    for(auto* W:{CardPreviewInformation.Get(),UnitHoverInformation.Get()})if(W)W->SetVisibility(ESlateVisibility::Collapsed);
    bUnitSidebarOpen=false;
    if(bForce)ClearInspectionBindings();
    else UpdateSidebarMotion(0.f);
    if(auto* Dock=GetWidgetFromName(TEXT("BoardInfoDock")))Dock->SetVisibility(ESlateVisibility::Collapsed);
}
void UEGMainHUD::CloseInformationViews(){HideCardInformation(true);}
void UEGMainHUD::SellInspectedInformation(){OnBoardAction(30,0);}
void UEGMainHUD::LayoutInformationViews()
{
    const FVector2D View=GetCachedGeometry().GetLocalSize();if(View.X<=0||View.Y<=0)return;
    const float PinnedWidth=UnitPinnedInformation?UnitPinnedInformation->GetSkillDetailsWidth(View.X):0.f;
    if(UnitPinnedInformation)UnitPinnedInformation->SetSkillLayoutWidth(View.X);
    const auto* PinnedSlot=UnitPinnedInformation?Cast<UCanvasPanelSlot>(UnitPinnedInformation->Slot):nullptr;
    if(View.Equals(InformationLayoutViewport,.1f)&&(!PinnedSlot||FMath::IsNearlyEqual(PinnedSlot->GetSize().X,PinnedWidth,.1f)))return;
    InformationLayoutViewport=View;
    if(CardPreviewInformation)if(auto* S=Cast<UCanvasPanelSlot>(CardPreviewInformation->Slot))
    {
        const float Scale=FMath::Min(1.f,FMath::Min((View.X-48.f)/820.f,(View.Y-80.f)/560.f));
        S->SetPosition(FVector2D((View.X-820.f*Scale)*.5f,(View.Y-560.f*Scale)*.5f));S->SetSize(FVector2D(820,560));
        CardPreviewInformation->SetRenderTransformPivot(FVector2D::ZeroVector);CardPreviewInformation->SetRenderScale(FVector2D(FMath::Max(.1f,Scale)));
    }
    if(UnitPinnedInformation)if(auto* S=Cast<UCanvasPanelSlot>(UnitPinnedInformation->Slot))
    {S->SetPosition(FVector2D(0,88));S->SetSize(FVector2D(PinnedWidth,FMath::Max(1.f,View.Y-88.f-176.f)));}
    if(UnitHoverInformation)if(auto* S=Cast<UCanvasPanelSlot>(UnitHoverInformation->Slot))
    {S->SetPosition(FVector2D(16,88));S->SetSize(FVector2D(FMath::Min(320.f,View.X),FMath::Min(300.f,FMath::Max(1.f,View.Y-104.f))));}
}
void UEGMainHUD::UpdateSidebarMotion(float DeltaSeconds)
{
    auto Animate=[DeltaSeconds](UWidget* Widget,bool bOpen,float& Reveal,float Direction)
    {
        if(!Widget)return;
        const float Target=bOpen?1.f:0.f;
        const float Step=FMath::Max(0.f,DeltaSeconds)/.16f;
        Reveal=FMath::Clamp(Reveal+FMath::Clamp(Target-Reveal,-Step,Step),0.f,1.f);
        const float Smooth=Reveal*Reveal*(3.f-2.f*Reveal);
        Widget->SetRenderOpacity(Smooth);
        Widget->SetRenderTranslation(FVector2D(Direction*24.f*(1.f-Smooth),0));
        Widget->SetVisibility(bOpen&&Reveal>0.f?ESlateVisibility::SelfHitTestInvisible:
            Reveal>0.f?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    };
    Animate(UnitPinnedInformation,bUnitSidebarOpen&&!bDragActive,UnitSidebarReveal,-1.f);
    Animate(GetWidgetFromName(TEXT("BoardStatistics")),bStatisticsSidebarOpen&&!bDragActive,StatisticsSidebarReveal,1.f);
    // The run panel lives in a separate viewport layer, so it must actively
    // relinquish both paint and input while the statistics drawer is visible.
    if(auto* PC=GetOwningPlayer())if(auto* HUD=Cast<AEGHUD>(PC->GetHUD()))
        if(auto* RunPanel=HUD->GetRunPanel())RunPanel->SetStigmataSidebarVisible(!bStatisticsSidebarOpen&&StatisticsSidebarReveal<=KINDA_SMALL_NUMBER);
}
void UEGMainHUD::RefreshBattleStatistics()
{
    bBattleStatisticsDirty=false;
    auto* Dock=GetWidgetFromName(TEXT("BoardStatistics"));if(!Dock||!Dock->IsVisible()||!BoardRun.IsValid())return;
    if(StatisticsMetric==3)
    {
        auto* Battle=BoardRun->BattleRules.Get();
        auto* Stigmata=Battle?Battle->GetStigmata():nullptr;
        TArray<FEGStigmataContribution> Rows=Stigmata?Stigmata->GetContributions():TArray<FEGStigmataContribution>();
        Rows.RemoveAll([](const FEGStigmataContribution& R){return R.AdditionalDamage<=0.f;});
        Rows.Sort([](const FEGStigmataContribution& A,const FEGStigmataContribution& B)
        {return A.AdditionalDamage==B.AdditionalDamage?A.Id.LexicalLess(B.Id):A.AdditionalDamage>B.AdditionalDamage;});
        float Maximum=1.f;for(const auto& R:Rows)Maximum=FMath::Max(Maximum,R.AdditionalDamage);
        for(int32 I=0;I<9;++I)
        {
            const bool bValid=Rows.IsValidIndex(I);
            if(auto* T=Cast<UTextBlock>(GetWidgetFromName(FName(*FString::Printf(TEXT("StatisticsLabel_%d"),I)))))
            {
                const auto* Definition=bValid?UEGStigmataCatalog::Find(Rows[I].Id):nullptr;
                const FString Name=bValid?(Definition?Definition->Name.ToString():Rows[I].Id.ToString()):FString();
                T->SetText(FText::FromString(bValid?Name+FString::Printf(TEXT("   %.0f"),Rows[I].AdditionalDamage):I==0?TEXT("本回合暂无刻印伤害"):TEXT("")));
                T->SetVisibility(bValid||I==0?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
            }
            if(auto* B=Cast<UProgressBar>(GetWidgetFromName(FName(*FString::Printf(TEXT("StatisticsBar_%d"),I)))))
            {B->SetVisibility(bValid?ESlateVisibility::Visible:ESlateVisibility::Collapsed);B->SetPercent(bValid?Rows[I].AdditionalDamage/Maximum:0.f);}
        }
        return;
    }
    auto Rows=BoardRun->GetCombatStatistics();
    auto Value=[this](const FEGHeroCombatStatistics& R){return StatisticsMetric==1?R.Taken:StatisticsMetric==2?R.Healing:R.Damage;};
    // GetCombatStatistics already sorts by damage; only other metrics need a second sort.
    if(StatisticsMetric!=0)Rows.Sort([&](const FEGHeroCombatStatistics& A,const FEGHeroCombatStatistics& B){return Value(A)>Value(B);});
    float Maximum=1;for(const auto& R:Rows)Maximum=FMath::Max(Maximum,Value(R));
    for(int32 I=0;I<9;++I)
    {
        const bool Valid=Rows.IsValidIndex(I);
        if(auto* T=Cast<UTextBlock>(GetWidgetFromName(FName(*FString::Printf(TEXT("StatisticsLabel_%d"),I)))))
        {T->SetText(FText::FromString(Valid?Rows[I].Name.ToString()+FString::Printf(TEXT("   %.0f"),Value(Rows[I])):I==0?TEXT("本回合暂无战斗记录"):TEXT("")));T->SetVisibility(Valid||I==0?ESlateVisibility::Visible:ESlateVisibility::Collapsed);}
        if(auto* B=Cast<UProgressBar>(GetWidgetFromName(FName(*FString::Printf(TEXT("StatisticsBar_%d"),I)))))
        {B->SetVisibility(Valid?ESlateVisibility::Visible:ESlateVisibility::Collapsed);B->SetPercent(Valid?Value(Rows[I])/Maximum:0);}
    }
}
void UEGMainHUD::MarkBattleStatisticsDirty(){bBattleStatisticsDirty=true;}
void UEGMainHUD::RefreshBoardView()
{
    auto* GM=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
    auto* Manager=GM?GM->GetCards():nullptr;if(!Manager)return;
    auto Set=[this](const TCHAR* Name,const FString& Value){if(auto* T=Cast<UTextBlock>(GetWidgetFromName(Name)))T->SetText(FText::FromString(Value));};
    if(auto* Summary=GetWidgetFromName(TEXT("EconomyDock")))Summary->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* Reserve=GetWidgetFromName(TEXT("ReserveViewport")))Reserve->SetVisibility(ESlateVisibility::Collapsed);
    if(auto* B=Cast<UButton>(GetWidgetFromName(TEXT("Button_Battle"))))
    {
        const auto* Match=GM->GetMatch();
        const auto* Run=GM->RunDefinition.Get();
        if(Match&&Run&&Run->bSevenStageRules)
        {
            const EEGMatchPhase Phase=Match->GetPhase();
            const bool bPreparation=Phase==EEGMatchPhase::Preparation;
            B->SetVisibility(Phase==EEGMatchPhase::Finished?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
            const auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
            B->SetIsEnabled(bPreparation&&Controller&&Controller->GetMatchView().bCanRequestCombat);
            Set(TEXT("Text_Battle"),bPreparation?TEXT("开战"):Phase==EEGMatchPhase::Combat?TEXT("战斗中"):TEXT("结算中"));
            if(auto* Income=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_RoundIncome"))))
            {
                const int32 StageNumber=Match->GetStageNumber();
                const bool bShowIncome=StageNumber>=1&&StageNumber<=6&&Run->Stages.IsValidIndex(StageNumber-1);
                Income->SetVisibility(bShowIncome?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
                if(bShowIncome)Income->SetText(FText::FromString(FString::Printf(TEXT("+%d"),Run->Stages[StageNumber-1].CompletionGold)));
            }
        }
        else
        {
            B->SetVisibility(ESlateVisibility::Visible);
            Set(TEXT("Text_Battle"),TEXT("开战"));
            if(auto* Income=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_RoundIncome"))))
            {
                Income->SetVisibility(ESlateVisibility::HitTestInvisible);
                if(Run&&Run->bRoundShopRules)Income->SetText(FText::FromString(FString::Printf(TEXT("+%d"),Run->RoundIncome)));
            }
        }
    }
    if(auto* B=Cast<UEGBoardButton>(GetWidgetFromName(TEXT("Button_SellInspected"))))
    {
        const auto* Controller=Cast<UEGOverlayWidgetController>(GetWidgetController());
        B->SetIsEnabled(Controller&&Controller->CanWithdrawInspectedHero(InspectedHero));
        B->SetToolTipText(FText::FromString(TEXT("准备阶段可将上阵角色返回备战席。")));
    }
    if(Text_Round&&GM->GetMatch())Text_Round->SetText(FText::FromString(FString::Printf(TEXT("第 %d 关"),GM->GetMatch()->GetStageNumber())));
    if(GM->GetMatch())
    {
        const int32 N=GM->GetMatch()->GetCorruption();
        Set(TEXT("Text_Corruption"),FString::Printf(TEXT("侵蚀度 %d"),N));
        if(auto* W=GetWidgetFromName(TEXT("Text_Corruption")))W->SetToolTipText(FText::FromString(TEXT("侵蚀度达到 200 时失败。")));
    }
    Set(TEXT("Text_Gold"),FString::Printf(TEXT("%lld"),Manager->GetGold()));
    Set(TEXT("Text_Population"),FString::Printf(TEXT("%d / %d"),Manager->GetPieces().Num(),Manager->GetRules().DeploymentLimit));
    if(!DrawPanel)return;
    // The plate always shows the manager's real refresh fee, never a static label.
    const int32 RefreshCost=Manager->GetRefreshCost();
    const bool bLocked=Manager->IsShopLocked();
    if(auto* Refresh=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_RefreshShop"))))Refresh->SetIsEnabled(Manager->CanTrade()&&Manager->GetGold()>=RefreshCost);
    if(auto* Lock=Cast<UButton>(DrawPanel->GetWidgetFromName(TEXT("Button_ShopLock"))))Lock->SetIsEnabled(Manager->CanTrade());
    if(auto* Cost=Cast<UTextBlock>(DrawPanel->GetWidgetFromName(TEXT("ShopRefreshCost"))))Cost->SetText(FText::FromString(FString::FromInt(RefreshCost)));
    // Lock state is a glyph swap plus a matching label; both plate tints follow
    // the legal <=1 ramp (disabled while trading is unavailable).
    SetShopLockGlyphState(bLocked);
    RefreshShopPlateTint();
    const auto Shop=Manager->GetShop();
    for(int32 I=0;I<4;++I)
    {
        auto* D=Shop.IsValidIndex(I)?Shop[I]:nullptr;
        if(auto* Image=Cast<UImage>(DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopArtwork_%d"),I)))))Image->SetBrushFromTexture(FEGCardPresentation::ResolveArtwork(D?D->Artwork.Get():nullptr),false);
        if(auto* Host=Cast<UPanelWidget>(DrawPanel->GetWidgetFromName(FName(*FString::Printf(TEXT("ShopSlot_%d"),I)))))
        {if(Host->GetChildrenCount()>0)if(auto* Card=Cast<UEGShopCard>(Host->GetChildAt(0)))Card->Configure(I,Manager,this);}
    }
    if(auto* T=Cast<UTextBlock>(GetWidgetFromName(TEXT("Text_ShopProbability"))))T->SetText(FText::FromString(TEXT("全部卡池  每名角色等概率")));
    if(auto* T=Cast<UTextBlock>(DrawPanel->GetWidgetFromName(TEXT("Text_ShopLock"))))T->SetText(FText::FromString(bLocked?TEXT("已锁定"):TEXT("锁定")));
}
