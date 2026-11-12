#include "UI/Widget/EGMainMenuWidget.h"

#include "AudioDevice.h"
#include "AudioDeviceHandle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/ComboBoxString.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Components/SizeBox.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/EGMainMenuGameMode.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/ConfigCacheIni.h"
#include "UI/EGTypography.h"
#include "UI/EGVisualStyle.h"

namespace
{
constexpr float MenuFadeDuration = .65f;
constexpr float MenuOpeningFadeDuration = .30f;
const TCHAR* AudioSection = TEXT("/Script/ElyGambit.MainMenuAudio");
const TCHAR* VolumeKey = TEXT("PrimaryVolume");
const FLinearColor Pearl(.98f, .91f, .98f, 1.f);
const FLinearColor SoftPink(.91f, .69f, .84f, 1.f);
const FLinearColor MutedPink(.67f, .49f, .64f, 1.f);

float ReadSavedVolume()
{
    float Result = 1.f;
    if (GConfig) GConfig->GetFloat(AudioSection, VolumeKey, Result, GGameUserSettingsIni);
    return FMath::IsFinite(Result) ? FMath::Clamp(Result, 0.f, 1.f) : 1.f;
}

void ApplyVolume(UWorld* World, float Volume)
{
    if (World)
    {
        FAudioDeviceHandle Audio = World->GetAudioDevice();
        if (Audio.IsValid())
            Audio->SetTransientPrimaryVolume(FMath::Clamp(Volume, 0.f, 1.f));
    }
}

UTextBlock* MakeText(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* Text, int32 Size,
                     const FLinearColor& Colour, bool bTitle = false)
{
    UTextBlock* Label = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), FName(Name));
    Label->SetText(FText::FromString(Text));
    Label->SetFont(bTitle ? FEGVisualStyle::TitleFont(Size) : FEGVisualStyle::Font(Size));
    Label->SetColorAndOpacity(FSlateColor(Colour));
    Label->SetShadowOffset(FVector2D(1.f, 2.f));
    Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, .7f));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Label;
}

FButtonStyle MakeButtonStyle()
{
    FButtonStyle Style;
    Style.SetNormal(FEGVisualStyle::Rounded(FLinearColor(.045f, .025f, .085f, .64f), 4.f,
                                             FLinearColor(.60f, .36f, .57f, .55f), 1.f));
    Style.SetHovered(FEGVisualStyle::Rounded(FLinearColor(.20f, .07f, .17f, .84f), 4.f,
                                              FLinearColor(1.f, .58f, .82f, .95f), 2.f));
    Style.SetPressed(FEGVisualStyle::Rounded(FLinearColor(.34f, .12f, .25f, .95f), 4.f,
                                              FLinearColor(1.f, .77f, .91f, 1.f), 2.f));
    Style.SetDisabled(FEGVisualStyle::Rounded(FLinearColor(.035f, .025f, .05f, .35f), 4.f));
    Style.NormalPadding = FMargin(0.f);
    Style.PressedPadding = FMargin(2.f, 2.f, 0.f, 0.f);
    return Style;
}

void StyleQualityCombo(UComboBoxString* Combo)
{
    if (!Combo) return;

    FButtonStyle ButtonStyle;
    ButtonStyle.SetNormal(FEGVisualStyle::Rounded(FLinearColor(.075f, .035f, .105f, .92f), 3.f,
                                                  FLinearColor(.69f, .42f, .62f, .75f), 1.f));
    ButtonStyle.SetHovered(FEGVisualStyle::Rounded(FLinearColor(.15f, .06f, .15f, .96f), 3.f,
                                                   FLinearColor(.99f, .61f, .82f, .95f), 1.f));
    ButtonStyle.SetPressed(FEGVisualStyle::Rounded(FLinearColor(.24f, .09f, .20f, .98f), 3.f,
                                                   FLinearColor(1.f, .76f, .90f, 1.f), 1.f));
    ButtonStyle.SetDisabled(FEGVisualStyle::Rounded(FLinearColor(.04f, .025f, .06f, .6f), 3.f));
    ButtonStyle.NormalPadding = FMargin(0.f);
    ButtonStyle.PressedPadding = FMargin(0.f, 1.f, 0.f, 0.f);

    FComboBoxStyle ComboStyle = Combo->GetWidgetStyle();
    ComboStyle.ComboButtonStyle.SetButtonStyle(ButtonStyle);
    ComboStyle.ComboButtonStyle.SetMenuBorderBrush(FEGVisualStyle::Rounded(
        FLinearColor(.035f, .018f, .065f, .98f), 3.f, FLinearColor(.69f, .42f, .62f, .75f), 1.f));
    ComboStyle.ComboButtonStyle.SetMenuBorderPadding(FMargin(3.f));
    ComboStyle.ComboButtonStyle.DownArrowImage.TintColor = FSlateColor(SoftPink);
    ComboStyle.SetContentPadding(FMargin(10.f, 6.f));
    ComboStyle.SetMenuRowPadding(FMargin(8.f, 5.f));
    Combo->SetWidgetStyle(ComboStyle);
    Combo->SetContentPadding(FMargin(10.f, 6.f));

    const FSlateBrush Row = FEGVisualStyle::Rounded(FLinearColor(.06f, .025f, .09f, .97f), 2.f);
    const FSlateBrush Hover = FEGVisualStyle::Rounded(FLinearColor(.24f, .08f, .20f, .98f), 2.f);
    const FSlateBrush Selected = FEGVisualStyle::Rounded(FLinearColor(.31f, .10f, .25f, .98f), 2.f,
                                                         FLinearColor(.93f, .58f, .78f, .6f), 1.f);
    FTableRowStyle RowStyle = Combo->GetItemStyle();
    RowStyle.SetEvenRowBackgroundBrush(Row).SetOddRowBackgroundBrush(Row);
    RowStyle.SetEvenRowBackgroundHoveredBrush(Hover).SetOddRowBackgroundHoveredBrush(Hover);
    RowStyle.SetActiveBrush(Selected).SetInactiveBrush(Selected);
    RowStyle.SetActiveHoveredBrush(Hover).SetInactiveHoveredBrush(Hover);
    RowStyle.SetSelectorFocusedBrush(Selected);
    RowStyle.SetTextColor(FSlateColor(Pearl)).SetSelectedTextColor(FSlateColor(Pearl));
    Combo->SetItemStyle(RowStyle);
}

UButton* MakeButton(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* Text, int32 Size = 27)
{
    UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
    Button->SetStyle(MakeButtonStyle());
    UTextBlock* Label = MakeText(Tree, *(FString(Name) + TEXT("Label")), Text, Size, Pearl);
    Label->SetJustification(ETextJustify::Center);
    Button->AddChild(Label);
    return Button;
}

UImage* MakeDiamond(UWidgetTree* Tree, const FLinearColor& Colour)
{
    UImage* Diamond = Tree->ConstructWidget<UImage>();
    Diamond->SetBrush(FEGVisualStyle::Rounded(Colour, 0.f));
    Diamond->SetRenderTransformAngle(45.f);
    Diamond->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Diamond;
}

UButton* MakeMenuButton(UWidgetTree* Tree, const TCHAR* Name, const TCHAR* Text, bool bPrimary)
{
    UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), FName(Name));
    FButtonStyle Style;
    const float FillAlpha = bPrimary ? .44f : .31f;
    const float LineAlpha = bPrimary ? .72f : .42f;
    Style.SetNormal(FEGVisualStyle::Rounded(FLinearColor(.045f, .025f, .075f, FillAlpha), 2.f,
                                           FLinearColor(.86f, .56f, .76f, LineAlpha), 1.f));
    Style.SetHovered(FEGVisualStyle::Rounded(FLinearColor(.16f, .055f, .12f, .54f), 2.f,
                                            FLinearColor(1.f, .65f, .84f, .94f), 1.f));
    Style.SetPressed(FEGVisualStyle::Rounded(FLinearColor(.32f, .11f, .23f, .70f), 2.f,
                                            FLinearColor(1.f, .82f, .93f, 1.f), 1.f));
    Style.SetDisabled(FEGVisualStyle::Rounded(FLinearColor(.035f, .025f, .05f, .25f), 2.f));
    Style.NormalPadding = FMargin(0.f);
    Style.PressedPadding = FMargin(1.f, 1.f, 0.f, 0.f);
    Button->SetStyle(Style);

    UHorizontalBox* Row = Tree->ConstructWidget<UHorizontalBox>();
    Row->SetVisibility(ESlateVisibility::HitTestInvisible);
    UButtonSlot* ButtonSlot = Cast<UButtonSlot>(Button->AddChild(Row));
    ButtonSlot->SetHorizontalAlignment(HAlign_Fill);
    ButtonSlot->SetVerticalAlignment(VAlign_Center);
    ButtonSlot->SetPadding(FMargin(22.f, 0.f, 20.f, 0.f));

    USizeBox* IconSize = Tree->ConstructWidget<USizeBox>();
    IconSize->SetWidthOverride(bPrimary ? 8.f : 6.f);
    IconSize->SetHeightOverride(bPrimary ? 8.f : 6.f);
    IconSize->AddChild(MakeDiamond(Tree, bPrimary ? SoftPink : MutedPink));
    UHorizontalBoxSlot* IconSlot = Row->AddChildToHorizontalBox(IconSize);
    IconSlot->SetVerticalAlignment(VAlign_Center);
    IconSlot->SetPadding(FMargin(0.f, 0.f, 22.f, 0.f));

    UTextBlock* Label = MakeText(Tree, *(FString(Name) + TEXT("Label")), Text,
                                 bPrimary ? 27 : 25, Pearl);
    Label->SetShadowOffset(FVector2D(0.f, 1.f));
    Label->SetShadowColorAndOpacity(FLinearColor(.015f, .005f, .025f, .58f));
    UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(Label);
    LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
    LabelSlot->SetVerticalAlignment(VAlign_Center);

    UTextBlock* Arrow = MakeText(Tree, *(FString(Name) + TEXT("Arrow")), TEXT(">"), 22, MutedPink);
    UHorizontalBoxSlot* ArrowSlot = Row->AddChildToHorizontalBox(Arrow);
    ArrowSlot->SetVerticalAlignment(VAlign_Center);
    return Button;
}

UCanvasPanelSlot* Place(UCanvasPanel* Canvas, UWidget* Child, const FVector2D& Position,
                        const FVector2D& Size, const FAnchors& Anchors = FAnchors(0.f, 0.f),
                        const FVector2D& Alignment = FVector2D::ZeroVector)
{
    UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
    Slot->SetAnchors(Anchors);
    Slot->SetAlignment(Alignment);
    Slot->SetPosition(Position);
    Slot->SetSize(Size);
    return Slot;
}

void Stretch(UCanvasPanel* Canvas, UWidget* Child)
{
    UCanvasPanelSlot* Slot = Canvas->AddChildToCanvas(Child);
    Slot->SetAnchors(FAnchors(0.f, 0.f, 1.f, 1.f));
    Slot->SetOffsets(FMargin(0.f));
}

void AddMenuButton(UWidgetTree* Tree, UVerticalBox* List, UButton* Button)
{
    USizeBox* Size = Tree->ConstructWidget<USizeBox>();
    Size->SetWidthOverride(310.f);
    Size->SetHeightOverride(64.f);
    Size->AddChild(Button);
    UVerticalBoxSlot* Slot = List->AddChildToVerticalBox(Size);
    Slot->SetPadding(FMargin(0.f, 0.f, 0.f, 14.f));
}

void SetButtonHovered(UButton* Button, bool bHovered)
{
    if (Button)
        if (UHorizontalBox* Row = Cast<UHorizontalBox>(Button->GetContent()))
        {
            Row->SetRenderTranslation(FVector2D(bHovered ? 3.f : 0.f, 0.f));
            if (UTextBlock* Label = Cast<UTextBlock>(Row->GetChildAt(1)))
            Label->SetColorAndOpacity(FSlateColor(bHovered ? FLinearColor(1.f, .66f, .87f, 1.f) : Pearl));
            if (UTextBlock* Arrow = Cast<UTextBlock>(Row->GetChildAt(2)))
                Arrow->SetColorAndOpacity(FSlateColor(bHovered ? SoftPink : MutedPink));
        }
}
}

void UEGMainMenuSettingsWidget::ApplySavedVolume(UWorld* World)
{
    ApplyVolume(World, ReadSavedVolume());
}

void UEGMainMenuSettingsWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    BuildLayout();
}

void UEGMainMenuSettingsWidget::BuildLayout()
{
    if (!WidgetTree || WidgetTree->RootWidget) return;
    UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("SettingsRoot"));
    WidgetTree->RootWidget = Root;
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

    UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SettingsPanel"));
    Panel->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.035f, .018f, .065f, .95f), 8.f,
                                               FLinearColor(.81f, .51f, .73f, .75f), 1.f));
    Panel->SetPadding(FMargin(32.f, 26.f));
    Place(Root, Panel, FVector2D(96.f, -10.f), FVector2D(550.f, 450.f), FAnchors(0.f, .5f), FVector2D(0.f, .5f));

    UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>();
    Panel->SetContent(Content);
    UTextBlock* Header = MakeText(WidgetTree, TEXT("SettingsTitle"), TEXT("设 置"), 34, Pearl, true);
    Content->AddChildToVerticalBox(Header)->SetPadding(FMargin(0.f, 0.f, 0.f, 25.f));

    UHorizontalBox* VolumeRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(VolumeRow)->SetPadding(FMargin(0.f, 4.f, 0.f, 18.f));
    UTextBlock* VolumeTitle = MakeText(WidgetTree, TEXT("VolumeTitle"), TEXT("总音量"), 22, Pearl);
    VolumeRow->AddChildToHorizontalBox(VolumeTitle)->SetPadding(FMargin(0.f, 0.f, 20.f, 0.f));
    USizeBox* SliderSize = WidgetTree->ConstructWidget<USizeBox>();
    SliderSize->SetWidthOverride(225.f);
    VolumeSlider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass(), TEXT("VolumeSlider"));
    VolumeSlider->SetStepSize(.01f);
    VolumeSlider->SetSliderBarColor(SoftPink);
    VolumeSlider->SetSliderHandleColor(Pearl);
    SliderSize->AddChild(VolumeSlider);
    VolumeRow->AddChildToHorizontalBox(SliderSize)->SetVerticalAlignment(VAlign_Center);
    VolumeValue = MakeText(WidgetTree, TEXT("VolumeValue"), TEXT("100%"), 21, SoftPink);
    VolumeRow->AddChildToHorizontalBox(VolumeValue)->SetPadding(FMargin(20.f, 0.f, 0.f, 0.f));
    VolumeSlider->OnValueChanged.AddDynamic(this, &ThisClass::OnVolumeChanged);

    UHorizontalBox* QualityRow = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(QualityRow)->SetPadding(FMargin(0.f, 4.f, 0.f, 26.f));
    QualityRow->AddChildToHorizontalBox(MakeText(WidgetTree, TEXT("QualityTitle"), TEXT("画质"), 22, Pearl))
        ->SetPadding(FMargin(0.f, 0.f, 44.f, 0.f));
    USizeBox* QualitySize = WidgetTree->ConstructWidget<USizeBox>();
    QualitySize->SetWidthOverride(250.f);
    QualityCombo = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("QualityCombo"));
    // UE 5.8 exposes no font/foreground setter: initialize before Slate construction.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
    QualityCombo->Font = FEGVisualStyle::Font(21);
    QualityCombo->ForegroundColor = FSlateColor(Pearl);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
    StyleQualityCombo(QualityCombo);
    for (const TCHAR* Choice : {TEXT("自定义"), TEXT("低"), TEXT("中"), TEXT("高"), TEXT("最高"), TEXT("电影级")})
        QualityCombo->AddOption(Choice);
    QualitySize->AddChild(QualityCombo);
    QualityRow->AddChildToHorizontalBox(QualitySize);

    UTextBlock* Note = MakeText(WidgetTree, TEXT("SettingsNote"), TEXT("应用后保存设置"), 17, MutedPink);
    Content->AddChildToVerticalBox(Note)->SetPadding(FMargin(0.f, 0.f, 0.f, 24.f));
    UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>();
    Content->AddChildToVerticalBox(Buttons);
    ApplyButton = MakeButton(WidgetTree, TEXT("ApplyButton"), TEXT("应 用"), 22);
    BackButton = MakeButton(WidgetTree, TEXT("BackButton"), TEXT("返 回"), 22);
    for (UButton* Button : {ApplyButton.Get(), BackButton.Get()})
    {
        USizeBox* Size = WidgetTree->ConstructWidget<USizeBox>();
        Size->SetWidthOverride(190.f);
        Size->SetHeightOverride(54.f);
        Size->AddChild(Button);
        Buttons->AddChildToHorizontalBox(Size)->SetPadding(FMargin(0.f, 0.f, 18.f, 0.f));
    }
    ApplyButton->OnClicked.AddDynamic(this, &ThisClass::ApplySettings);
    BackButton->OnClicked.AddDynamic(this, &ThisClass::CloseSettings);
}

void UEGMainMenuSettingsWidget::OpenSettings()
{
    if (!VolumeSlider || !QualityCombo) return;
    const float Volume = ReadSavedVolume();
    VolumeSlider->SetValue(Volume);
    RefreshVolumeLabel(Volume);
    const UGameUserSettings* UserSettings = GEngine ? GEngine->GetGameUserSettings() : nullptr;
    const int32 Level = UserSettings ? UserSettings->GetOverallScalabilityLevel() : -1;
    QualityCombo->SetSelectedIndex(Level >= 0 && Level <= 4 ? Level + 1 : 0);
    SetVisibility(ESlateVisibility::Visible);
    if (BackButton)
        if (APlayerController* Controller = GetOwningPlayer()) BackButton->SetUserFocus(Controller);
}

void UEGMainMenuSettingsWidget::OnVolumeChanged(float Value)
{
    RefreshVolumeLabel(Value);
}

void UEGMainMenuSettingsWidget::RefreshVolumeLabel(float Value)
{
    if (VolumeValue) VolumeValue->SetText(FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Value * 100.f))));
}

void UEGMainMenuSettingsWidget::ApplySettings()
{
    if (!VolumeSlider || !QualityCombo) return;
    const float Volume = FMath::Clamp(VolumeSlider->GetValue(), 0.f, 1.f);
    ApplyVolume(GetWorld(), Volume);
    if (UGameUserSettings* Settings = GEngine ? GEngine->GetGameUserSettings() : nullptr)
    {
        const int32 Index = QualityCombo->GetSelectedIndex();
        if (Index >= 1 && Index <= 5) Settings->SetOverallScalabilityLevel(Index - 1);
        // This screen has no resolution control; preserve the current window size.
        Settings->ApplyNonResolutionSettings();
        Settings->SaveSettings();
    }
    if (GConfig)
    {
        GConfig->SetFloat(AudioSection, VolumeKey, Volume, GGameUserSettingsIni);
        GConfig->Flush(false, GGameUserSettingsIni);
    }
}

void UEGMainMenuSettingsWidget::CloseSettings()
{
    SetVisibility(ESlateVisibility::Collapsed);
    OnSettingsClosed.Broadcast();
}

void UEGMainMenuWidget::Configure(AEGMainMenuController* Controller, TSubclassOf<UEGMainMenuSettingsWidget> InSettingsClass)
{
    MenuController = Controller;
    SettingsClass = InSettingsClass;
    if (!SettingsClass) SettingsClass = UEGMainMenuSettingsWidget::StaticClass();
}

void UEGMainMenuWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    BuildLayout();
}

void UEGMainMenuWidget::NativeConstruct()
{
    Super::NativeConstruct();
    BuildLayout();
    FadeElapsed = 0.f;
    FadeDuration = MenuFadeDuration;
    FadeStartOpacity = 0.f;
    FadeTargetOpacity = 1.f;
    bFadingOut = false;
    SetIsEnabled(true);
    SetRenderOpacity(0.f);
    if (!Settings && MainCanvas && SettingsClass && GetOwningPlayer())
    {
        Settings = CreateWidget<UEGMainMenuSettingsWidget>(GetOwningPlayer(), SettingsClass);
        if (Settings)
        {
            Stretch(MainCanvas, Settings);
            Settings->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
    if (Settings) Settings->OnSettingsClosed.AddUObject(this, &ThisClass::RestoreMenuFocus);
    // Menu headings use the serif face; sweep every label once the tree (and its embedded
    // settings widget) exists. Only font assets change, sizes/colours/layout are untouched.
    EGApplyTypography(this);
}

void UEGMainMenuWidget::NativeDestruct()
{
    if (Settings) Settings->OnSettingsClosed.RemoveAll(this);
    Super::NativeDestruct();
}

void UEGMainMenuWidget::BuildLayout()
{
    if (!WidgetTree || WidgetTree->RootWidget) return;
    UScaleBox* Scale = WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(), TEXT("ViewportScale"));
    Scale->SetStretch(EStretch::ScaleToFit);
    Scale->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    WidgetTree->RootWidget = Scale;
    USizeBox* DesignSize = WidgetTree->ConstructWidget<USizeBox>();
    DesignSize->SetWidthOverride(1920.f);
    DesignSize->SetHeightOverride(1080.f);
    Scale->AddChild(DesignSize);
    MainCanvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("MenuCanvas"));
    MainCanvas->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    DesignSize->AddChild(MainCanvas);

    UCanvasPanel* Group = WidgetTree->ConstructWidget<UCanvasPanel>();
    Group->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    Stretch(MainCanvas, Group);
    MenuGroup = Group;
    UTextBlock* Brand = MakeText(WidgetTree, TEXT("Brand"), TEXT("ELYGAMBIT"), 54, Pearl);
    FSlateFontInfo BrandFont = FEGVisualStyle::SerifFont(54);
    BrandFont.LetterSpacing = 100;
    Brand->SetFont(BrandFont);
    Brand->SetShadowOffset(FVector2D(0.f, 2.f));
    Brand->SetShadowColorAndOpacity(FLinearColor(.02f, .01f, .04f, .52f));
    Place(Group, Brand, FVector2D(96.f, 86.f), FVector2D(620.f, 78.f));
    Place(Group, MakeText(WidgetTree, TEXT("Subtitle"), TEXT("往 世 之 弈"), 20, SoftPink),
          FVector2D(102.f, 166.f), FVector2D(350.f, 32.f));

    UImage* Rule = WidgetTree->ConstructWidget<UImage>();
    Rule->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.92f, .64f, .83f, .72f), 0.f));
    Rule->SetVisibility(ESlateVisibility::HitTestInvisible);
    Place(Group, Rule, FVector2D(102.f, 216.f), FVector2D(200.f, 1.f));
    Place(Group, MakeDiamond(WidgetTree, SoftPink), FVector2D(311.f, 213.f), FVector2D(6.f, 6.f));
    UImage* FineRule = WidgetTree->ConstructWidget<UImage>();
    FineRule->SetBrush(FEGVisualStyle::Rounded(FLinearColor(.92f, .64f, .83f, .32f), 0.f));
    FineRule->SetVisibility(ESlateVisibility::HitTestInvisible);
    Place(Group, FineRule, FVector2D(326.f, 216.f), FVector2D(52.f, 1.f));
    Place(Group, MakeText(WidgetTree, TEXT("Caption"), TEXT("THE ELYSIAN REALM"), 15, MutedPink),
          FVector2D(102.f, 225.f), FVector2D(350.f, 28.f));

    UVerticalBox* Actions = WidgetTree->ConstructWidget<UVerticalBox>();
    Place(Group, Actions, FVector2D(96.f, -78.f), FVector2D(310.f, 220.f), FAnchors(0.f, 1.f), FVector2D(0.f, 1.f));
    StartButton = MakeMenuButton(WidgetTree, TEXT("StartGameButton"), TEXT("开始游戏"), true);
    SettingsButton = MakeMenuButton(WidgetTree, TEXT("SettingsButton"), TEXT("设 置"), false);
    QuitButton = MakeMenuButton(WidgetTree, TEXT("QuitButton"), TEXT("退出游戏"), false);
    AddMenuButton(WidgetTree, Actions, StartButton);
    AddMenuButton(WidgetTree, Actions, SettingsButton);
    AddMenuButton(WidgetTree, Actions, QuitButton);
    StartButton->OnClicked.AddDynamic(this, &ThisClass::HandleStart);
    SettingsButton->OnClicked.AddDynamic(this, &ThisClass::HandleSettings);
    QuitButton->OnClicked.AddDynamic(this, &ThisClass::HandleQuit);
    StartButton->OnHovered.AddDynamic(this, &ThisClass::StartHovered);
    StartButton->OnUnhovered.AddDynamic(this, &ThisClass::StartUnhovered);
    SettingsButton->OnHovered.AddDynamic(this, &ThisClass::SettingsHovered);
    SettingsButton->OnUnhovered.AddDynamic(this, &ThisClass::SettingsUnhovered);
    QuitButton->OnHovered.AddDynamic(this, &ThisClass::QuitHovered);
    QuitButton->OnUnhovered.AddDynamic(this, &ThisClass::QuitUnhovered);
}

void UEGMainMenuWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (FadeElapsed < FadeDuration)
    {
        FadeElapsed = FMath::Min(FadeElapsed + FMath::Max(0.f, DeltaSeconds), FadeDuration);
        const float Alpha = FadeDuration > 0.f ? FadeElapsed / FadeDuration : 1.f;
        SetRenderOpacity(FMath::Lerp(FadeStartOpacity, FadeTargetOpacity, Alpha));
    }
}

void UEGMainMenuWidget::BeginStartFade()
{
    bFadingOut = true;
    FadeStartOpacity = GetRenderOpacity();
    FadeTargetOpacity = 0.f;
    FadeDuration = MenuOpeningFadeDuration;
    FadeElapsed = 0.f;
    SetIsEnabled(false);
}

void UEGMainMenuWidget::RestoreAfterStartFailure()
{
    bFadingOut = false;
    FadeStartOpacity = GetRenderOpacity();
    FadeTargetOpacity = 1.f;
    FadeDuration = MenuOpeningFadeDuration;
    FadeElapsed = 0.f;
    SetIsEnabled(true);
    if (Settings) Settings->SetVisibility(ESlateVisibility::Collapsed);
    RestoreMenuFocus();
}

void UEGMainMenuWidget::HandleStart()
{
    if (AEGMainMenuController* Controller = MenuController.Get()) Controller->RequestStartGame();
}

void UEGMainMenuWidget::HandleSettings()
{
    if (!Settings || bFadingOut) return;
    if (MenuGroup) MenuGroup->SetVisibility(ESlateVisibility::Collapsed);
    Settings->OpenSettings();
}

void UEGMainMenuWidget::RestoreMenuFocus()
{
    if (MenuGroup) MenuGroup->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    FocusPrimaryAction();
}

void UEGMainMenuWidget::FocusPrimaryAction()
{
    if (StartButton && GetOwningPlayer()) StartButton->SetUserFocus(GetOwningPlayer());
}

void UEGMainMenuWidget::HandleQuit()
{
    if (!bFadingOut)
        UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

void UEGMainMenuWidget::StartHovered() { SetButtonHovered(StartButton, true); }
void UEGMainMenuWidget::StartUnhovered() { SetButtonHovered(StartButton, false); }
void UEGMainMenuWidget::SettingsHovered() { SetButtonHovered(SettingsButton, true); }
void UEGMainMenuWidget::SettingsUnhovered() { SetButtonHovered(SettingsButton, false); }
void UEGMainMenuWidget::QuitHovered() { SetButtonHovered(QuitButton, true); }
void UEGMainMenuWidget::QuitUnhovered() { SetButtonHovered(QuitButton, false); }
