#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/EGShopMotionStyle.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGWidgets.h"
#include "Animation/WidgetAnimation.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
const TCHAR* const BoardFolder = TEXT("/Game/Blueprints/UI/Widgets/Board/");
// "Minimal charcoal translucent plates, subtle light-gray outlines, no blue, no
// gold": the plate art carries the fill, the live text stays neutral off-white.
const FLinearColor PlateText(0.88f,0.88f,0.91f,0.97f);
const TCHAR* const SliceSuffixes[9] = {TEXT("TL"),TEXT("T"),TEXT("TR"),TEXT("L"),TEXT("C"),TEXT("R"),TEXT("BL"),TEXT("B"),TEXT("BR")};
const FLinearColor BaseTint(EGPearlGold::ShopPlateTintBase,EGPearlGold::ShopPlateTintBase,EGPearlGold::ShopPlateTintBase,1.f);
const FLinearColor SellTint(.72f,.12f,.16f,1.f);


TArray<TSharedPtr<FJsonValue>> JsonVec2(const FVector2D& Value)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    Out.Add(MakeShared<FJsonValueNumber>(Value.X));
    Out.Add(MakeShared<FJsonValueNumber>(Value.Y));
    return Out;
}

TArray<TSharedPtr<FJsonValue>> JsonQuad(double A,double B,double C,double D)
{
    TArray<TSharedPtr<FJsonValue>> Out;
    Out.Add(MakeShared<FJsonValueNumber>(A));Out.Add(MakeShared<FJsonValueNumber>(B));
    Out.Add(MakeShared<FJsonValueNumber>(C));Out.Add(MakeShared<FJsonValueNumber>(D));
    return Out;
}

/** Editor-side structural facts for the delivery evidence, read from the live
 *  authored widgets so no Python reflection gaps can turn into fake failures. */
TSharedPtr<FJsonObject> WidgetFacts(UWidget* Widget)
{
    auto Facts=MakeShared<FJsonObject>();
    if(!Widget)return Facts;
    Facts->SetStringField(TEXT("class"),Widget->GetClass()->GetName());
    Facts->SetNumberField(TEXT("visibility"),static_cast<int32>(Widget->GetVisibility()));
    if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Widget->Slot))
    {
        auto SlotFacts=MakeShared<FJsonObject>();
        SlotFacts->SetArrayField(TEXT("position"),JsonVec2(CanvasSlot->GetPosition()));
        SlotFacts->SetArrayField(TEXT("size"),JsonVec2(CanvasSlot->GetSize()));
        SlotFacts->SetNumberField(TEXT("z"),CanvasSlot->GetZOrder());
        Facts->SetObjectField(TEXT("slot"),SlotFacts);
    }
    if(auto* Image=Cast<UImage>(Widget))
    {
        auto ImageFacts=MakeShared<FJsonObject>();
        const FSlateBrush& Brush=Image->GetBrush();
        if(const UObject* Resource=Brush.GetResourceObject())ImageFacts->SetStringField(TEXT("texture"),Resource->GetPathName());
        const FBox2f Uv=Brush.GetUVRegion();
        if(Uv.bIsValid)ImageFacts->SetArrayField(TEXT("uv"),JsonQuad(Uv.Min.X,Uv.Min.Y,Uv.Max.X,Uv.Max.Y));
        const FVector2D ImageSize=(FVector2D)Brush.ImageSize;
        ImageFacts->SetArrayField(TEXT("image_size"),JsonVec2(ImageSize));
        const FLinearColor Tint=Image->GetColorAndOpacity();
        ImageFacts->SetArrayField(TEXT("tint"),JsonQuad(Tint.R,Tint.G,Tint.B,Tint.A));
        Facts->SetObjectField(TEXT("image"),ImageFacts);
    }
    if(auto* Text=Cast<UTextBlock>(Widget))
    {
        auto TextFacts=MakeShared<FJsonObject>();
        TextFacts->SetStringField(TEXT("text"),Text->GetText().ToString());
        TextFacts->SetNumberField(TEXT("font_size"),Text->GetFont().Size);
        TextFacts->SetBoolField(TEXT("auto_wrap"),Text->GetAutoWrapText());
        TextFacts->SetNumberField(TEXT("wrap_text_at"),Text->GetWrapTextAt());
        Facts->SetObjectField(TEXT("text"),TextFacts);
    }
    if(auto* Button=Cast<UButton>(Widget))
    {
        auto ButtonFacts=MakeShared<FJsonObject>();
        const FButtonStyle& Style=Button->GetStyle();
        ButtonFacts->SetArrayField(TEXT("normal_padding"),JsonQuad(Style.NormalPadding.Left,Style.NormalPadding.Top,Style.NormalPadding.Right,Style.NormalPadding.Bottom));
        ButtonFacts->SetArrayField(TEXT("pressed_padding"),JsonQuad(Style.PressedPadding.Left,Style.PressedPadding.Top,Style.PressedPadding.Right,Style.PressedPadding.Bottom));
        ButtonFacts->SetBoolField(TEXT("enabled"),Button->GetIsEnabled());
        Facts->SetObjectField(TEXT("button"),ButtonFacts);
    }
    if(auto* SizeBox=Cast<USizeBox>(Widget))
    {
        auto SizeFacts=MakeShared<FJsonObject>();
        SizeFacts->SetNumberField(TEXT("width_override"),SizeBox->GetWidthOverride());
        SizeFacts->SetNumberField(TEXT("height_override"),SizeBox->GetHeightOverride());
        Facts->SetObjectField(TEXT("size_box"),SizeFacts);
    }
    return Facts;
}

void DumpBlueprint(const TSharedPtr<FJsonObject>& Root,const TCHAR* AssetName,UWidgetBlueprint* BP,const TArray<FName>& Names)
{
    auto Entry=MakeShared<FJsonObject>();
    for(const FName& Name:Names)
    {
        UWidget* Widget=BP&&BP->WidgetTree?BP->WidgetTree->FindWidget(Name):nullptr;
        if(Widget)Entry->SetObjectField(Name.ToString(),WidgetFacts(Widget));
    }
    Root->SetObjectField(AssetName,Entry);
}

bool WriteStructureReport(UWidgetBlueprint* Left,UWidgetBlueprint* Right,UWidgetBlueprint* Shop,UWidgetBlueprint* HUD)
{
    if(!Left||!Right||!Shop||!HUD)return false;
    auto Root=MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("generated_by"),TEXT("FEGWidgetAuthoringService::ApplyShopMotionAssets"));
    TArray<FName> SellNames;
    SellNames.Add(TEXT("SellLabelCenter"));SellNames.Add(TEXT("Text_Sell"));
    for(const TCHAR* Suffix:SliceSuffixes)SellNames.Add(FName(*(FString(TEXT("SellPlateArt"))+Suffix)));
    DumpBlueprint(Root,TEXT("WBP_SellLeft"),Left,SellNames);
    DumpBlueprint(Root,TEXT("WBP_SellRight"),Right,SellNames);
    TArray<FName> ShopNames;
    ShopNames.Add(TEXT("Button_ShopLock"));ShopNames.Add(TEXT("Button_RefreshShop"));
    ShopNames.Add(TEXT("Text_ShopLock"));ShopNames.Add(TEXT("BoardThemeRefreshText"));ShopNames.Add(TEXT("ShopRefreshCost"));
    ShopNames.Add(TEXT("ShopLockGlyphBox"));ShopNames.Add(TEXT("ShopLockGlyphClosed"));ShopNames.Add(TEXT("ShopLockGlyphOpen"));ShopNames.Add(TEXT("ShopRefreshGlyph"));
    for(const TCHAR* Suffix:SliceSuffixes)
    {
        ShopNames.Add(FName(*(FString(TEXT("ShopLockPlate"))+Suffix)));
        ShopNames.Add(FName(*(FString(TEXT("ShopRefreshPlate"))+Suffix)));
    }
    DumpBlueprint(Root,TEXT("WBP_Shop"),Shop,ShopNames);
    TArray<FName> HudNames;HudNames.Add(TEXT("SellZone_Left"));HudNames.Add(TEXT("SellZone_Right"));
    DumpBlueprint(Root,TEXT("WBP_BoardHUD"),HUD,HudNames);
    FString Json;
    if(!FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Json)))return false;
    const FString Path=FPaths::ProjectSavedDir()/TEXT("ShopMotion20261024/authored-structure.json");
    return FFileHelper::SaveStringToFile(Json,*Path);
}

template<class T>T* Reclaim(UWidgetBlueprint* BP,FName Name)
{
    if(auto* Existing=FindObjectFast<T>(BP->WidgetTree,Name)){Existing->RemoveFromParent();return Existing;}
    auto* Widget=BP->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name);
    BP->WidgetVariableNameToGuidMap.Add(Widget->GetFName(),FGuid::NewDeterministicGuid(Widget->GetPathName()));
    return Widget;
}

UCanvasPanelSlot* Place(UCanvasPanel* Canvas,UWidget* Widget,FAnchors Anchors,FVector2D Position,FVector2D Size,int32 ZOrder=0)
{
    Widget->RemoveFromParent();
    auto* Slot=Canvas->AddChildToCanvas(Widget);
    Slot->SetAnchors(Anchors);Slot->SetPosition(Position);Slot->SetSize(Size);
    Slot->SetAlignment(FVector2D(0,0));Slot->SetZOrder(ZOrder);
    return Slot;
}

UCanvasPanelSlot* FillCanvas(UCanvasPanel* Canvas,UWidget* Widget,int32 ZOrder=0)
{
    Widget->RemoveFromParent();
    auto* Slot=Canvas->AddChildToCanvas(Widget);
    Slot->SetAnchors(FAnchors(0,0,1,1));Slot->SetOffsets(FMargin(0));Slot->SetAlignment(FVector2D(0,0));Slot->SetZOrder(ZOrder);
    return Slot;
}

UOverlaySlot* AddOverlayFill(UOverlay* Overlay,UWidget* Widget)
{
    Widget->RemoveFromParent();
    auto* Slot=Overlay->AddChildToOverlay(Widget);
    Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);
    return Slot;
}

UCanvasPanel* ReclaimRootCanvas(UWidgetBlueprint* BP,const TCHAR* Name)
{
    auto* Root=FindObjectFast<UCanvasPanel>(BP->WidgetTree,Name);
    if(!Root){Root=BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Name);}
    BP->WidgetTree->RootWidget=Root;
    BP->WidgetVariableNameToGuidMap.Add(Root->GetFName(),FGuid::NewDeterministicGuid(Root->GetPathName()));
    Root->ClearChildren();
    return Root;
}

UTextBlock* ReclaimLabel(UWidgetBlueprint* BP,const TCHAR* Name,const FString& Text,int32 FontSize)
{
    auto* Label=Reclaim<UTextBlock>(BP,FName(Name));
    Label->SetFont(FEGVisualStyle::Font(FontSize));
    Label->SetColorAndOpacity(FSlateColor(PlateText));
    Label->SetJustification(ETextJustify::Center);
    Label->SetAutoWrapText(false);Label->SetWrapTextAt(0.f);
    Label->SetText(FText::FromString(Text));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Label;
}

bool SaveWidgetBP(UWidgetBlueprint* BP)
{
    // Reparenting/replacing content leaves stale widgets under WidgetTree until a
    // compile. Move only unreachable ones out before GUID registration, exactly
    // like the existing control authoring helper, so the compiler never first
    // registers then abandons those subtrees.
    TSet<UWidget*> Reachable;
    BP->WidgetTree->ForEachWidget([&](UWidget* W){Reachable.Add(W);});
    TArray<UWidget*> Abandoned;
    BP->ForEachSourceWidget([&](UWidget* W){if(W&&!Reachable.Contains(W))Abandoned.Add(W);});
    for(auto* W:Abandoned)W->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    TSet<FName> Names;
    BP->ForEachSourceWidget([&](UWidget* W){if(!W)return;Names.Add(W->GetFName());if(!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));});
    // Animations are source entries too; dropping their GUIDs would break clips.
    for(const auto& Animation:BP->Animations)if(Animation){Names.Add(Animation->GetFName());if(!BP->WidgetVariableNameToGuidMap.Contains(Animation->GetFName()))BP->WidgetVariableNameToGuidMap.Add(Animation->GetFName(),FGuid::NewDeterministicGuid(Animation->GetPathName()));}
    for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    if(BP->Status==BS_Error)return false;
    if(auto* Class=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
    BP->MarkPackageDirty();
    return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
}

bool AuthorSellZone(UWidgetBlueprint* BP,bool bRight)
{
    if(!BP||!BP->WidgetTree)return false;
    auto* Root=ReclaimRootCanvas(BP,TEXT("SellRoot"));
    Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
    // Full 192x320 plate authored as a 9-slice; corners scale uniformly to the
    // specified radius 28 (a direct stretch would only reach ~12 design pixels).
    for(int32 Index=0;Index<9;++Index)
    {
        const auto Slice=static_cast<EGShopMotion::EPlateSlice>(Index);
        const FName ImageName(*(FString(TEXT("SellPlateArt"))+SliceSuffixes[Index]));
        auto* Image=Reclaim<UImage>(BP,ImageName);
        Image->SetBrush(EGShopMotion::SellPlateBrush(Slice));
        Image->SetColorAndOpacity(SellTint);
        Image->SetVisibility(ESlateVisibility::HitTestInvisible);
        FVector2D Position=EGShopMotion::SellPlateSlicePosition(Slice);
        const FVector2D Size=EGShopMotion::SellPlateSliceSize(Slice);
        // Mirror each slice as well as its placement. Moving an unflipped corner
        // to the opposite side reverses the rounded silhouette and its border.
        // Only background images are transformed; the live text stays upright.
        if(bRight)Position.X=EGPearlGold::SellPlateWidth-(Position.X+Size.X);
        Image->SetRenderTransformPivot(FVector2D(.5f,.5f));
        Image->SetRenderScale(FVector2D(bRight?-1.f:1.f,1.f));
        Place(Root,Image,FAnchors(0,0,0,0),Position,Size);
    }
    // Border centres the TextBlock at its desired size, so the two live lines are
    // really centred in the visible 96-unit half instead of drawing from the top.
    auto* Center=Reclaim<UBorder>(BP,TEXT("SellLabelCenter"));
    Center->SetBrushColor(FLinearColor::Transparent);Center->SetPadding(FMargin(0));
    Center->SetHorizontalAlignment(HAlign_Center);Center->SetVerticalAlignment(VAlign_Center);
    Center->SetVisibility(ESlateVisibility::HitTestInvisible);
    Place(Root,Center,FAnchors(0,0,0,0),FVector2D(bRight?0.f:EGPearlGold::SellPlateVisibleWidth,0.f),
        FVector2D(EGPearlGold::SellPlateVisibleWidth,EGPearlGold::SellPlateHeight));
    auto* Label=ReclaimLabel(BP,TEXT("Text_Sell"),FString(TEXT("出售\n+1 崩坏能")),static_cast<int32>(EGPearlGold::SellPlateFontSize));
    Label->SetLineHeightPercentage(1.15f);
    Center->SetContent(Label);
    return true;
}

struct FShopControlSpec
{
    const TCHAR* ButtonName;
    const TCHAR* LabelName;
    const TCHAR* LabelText;
    EGPearlGold::FLayoutRect Rect;
    bool bLock;
};

bool AuthorShopControls(UWidgetBlueprint* BP)
{
    if(!BP||!BP->WidgetTree)return false;
    auto* ShopRoot=Cast<UCanvasPanel>(BP->WidgetTree->FindWidget(TEXT("ShopRoot")));
    if(!ShopRoot)return false;
    const FShopControlSpec Specs[]=
    {
        {TEXT("Button_ShopLock"),TEXT("Text_ShopLock"),TEXT("锁定"),EGPearlGold::ShopLock,true},
        {TEXT("Button_RefreshShop"),TEXT("BoardThemeRefreshText"),TEXT("刷新"),EGPearlGold::ShopRefresh,false},
    };
    for(const auto& Spec:Specs)
    {
        auto* Button=Cast<UEGBoardButton>(BP->WidgetTree->FindWidget(Spec.ButtonName));
        if(!Button)return false;
        const FString ButtonName(Spec.ButtonName);
        const FString PlatePrefix(Spec.bLock?TEXT("ShopLockPlate"):TEXT("ShopRefreshPlate"));
        // The plate art is authored; the button style only owns input. All states
        // are NoDraw so hover/pressed feedback comes from the visible tint ramp
        // instead of a background that the opaque plate would conceal.
        FButtonStyle Style;
        FSlateBrush None;None.DrawAs=ESlateBrushDrawType::NoDrawType;
        Style.SetNormal(None);Style.SetHovered(None);Style.SetPressed(None);Style.SetDisabled(None);
        Style.SetNormalPadding(FMargin(0));Style.SetPressedPadding(FMargin(0));
        Button->SetStyle(Style);
        Button->SetBackgroundColor(FLinearColor(1,1,1,1));
        auto* Content=Reclaim<UOverlay>(BP,FName(*(ButtonName+TEXT("Content"))));
        Content->ClearChildren();
        Content->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
        Button->SetContent(Content);
        if(auto* ContentSlot=Cast<UButtonSlot>(Content->Slot))
        {ContentSlot->SetPadding(FMargin(0));ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);}
        auto* PlateLayer=Reclaim<UCanvasPanel>(BP,FName(*(PlatePrefix+TEXT("Canvas"))));
        PlateLayer->ClearChildren();
        PlateLayer->SetVisibility(ESlateVisibility::HitTestInvisible);
        AddOverlayFill(Content,PlateLayer);
        for(int32 Index=0;Index<9;++Index)
        {
            const auto Slice=static_cast<EGShopMotion::EPlateSlice>(Index);
            auto* Image=Reclaim<UImage>(BP,FName(*(PlatePrefix+SliceSuffixes[Index])));
            Image->SetBrush(EGShopMotion::ButtonPlateBrush(Slice));
            Image->SetColorAndOpacity(BaseTint);
            Image->SetVisibility(ESlateVisibility::HitTestInvisible);
            Place(PlateLayer,Image,FAnchors(0,0,0,0),
                EGShopMotion::ButtonPlateSlicePosition(Slice),EGShopMotion::ButtonPlateSliceSize(Slice));
        }
        auto* Center=Reclaim<UBorder>(BP,FName(*(ButtonName+TEXT("Center"))));
        Center->SetBrushColor(FLinearColor::Transparent);Center->SetPadding(FMargin(0));
        Center->SetHorizontalAlignment(HAlign_Center);Center->SetVerticalAlignment(VAlign_Center);
        Center->SetVisibility(ESlateVisibility::HitTestInvisible);
        AddOverlayFill(Content,Center);
        auto* Row=Reclaim<UHorizontalBox>(BP,FName(*(ButtonName+TEXT("Row"))));
        Row->ClearChildren();Row->SetVisibility(ESlateVisibility::HitTestInvisible);
        Center->SetContent(Row);
        // Fixed 20x20 glyph slot: both lock states live here as aspect-fitted
        // images, so toggling the state never stretches the icon or shifts text.
        const FString GlyphBase=Spec.bLock?FString(TEXT("ShopLockGlyph")):FString(TEXT("ShopRefreshGlyph"));
        auto* GlyphBox=Reclaim<USizeBox>(BP,FName(*(GlyphBase+TEXT("Box"))));
        GlyphBox->SetWidthOverride(EGPearlGold::ShopPlateGlyphSize);
        GlyphBox->SetHeightOverride(EGPearlGold::ShopPlateGlyphSize);
        auto* GlyphLayers=Reclaim<UOverlay>(BP,FName(*(GlyphBase+TEXT("Layers"))));
        GlyphLayers->ClearChildren();
        GlyphBox->SetContent(GlyphLayers);
        auto AddGlyph=[&](const TCHAR* ImageName,EGShopMotion::EGlyph Glyph,bool bVisible)
        {
            const FVector2D GlyphSize=EGShopMotion::GlyphSize(Glyph);
            auto* Size=Reclaim<USizeBox>(BP,FName(*(FString(ImageName)+TEXT("Box"))));
            Size->SetWidthOverride(GlyphSize.X);Size->SetHeightOverride(GlyphSize.Y);
            auto* Image=Reclaim<UImage>(BP,FName(ImageName));
            Image->SetBrush(EGShopMotion::GlyphBrush(Glyph));
            Image->SetColorAndOpacity(BaseTint);
            Image->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            Size->SetContent(Image);
            auto* SizeSlot=GlyphLayers->AddChildToOverlay(Size);
            SizeSlot->SetHorizontalAlignment(HAlign_Center);SizeSlot->SetVerticalAlignment(VAlign_Center);
        };
        if(Spec.bLock)
        {
            AddGlyph(EGShopMotion::LockClosedImageName(),EGShopMotion::EGlyph::LockClosed,true);
            AddGlyph(EGShopMotion::LockOpenImageName(),EGShopMotion::EGlyph::LockOpen,false);
        }
        else AddGlyph(EGShopMotion::RefreshImageName(),EGShopMotion::EGlyph::Refresh,true);
        auto* GlyphSlot=Row->AddChildToHorizontalBox(GlyphBox);
        GlyphSlot->SetVerticalAlignment(VAlign_Center);
        GlyphSlot->SetPadding(FMargin(0,0,EGPearlGold::ShopPlateGlyphGap,0));
        auto* Label=ReclaimLabel(BP,Spec.LabelName,Spec.LabelText,static_cast<int32>(EGPearlGold::ShopPlateFontSize));
        Row->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
        if(!Spec.bLock)
        {
            // The real fee comes from AEGCardManager::GetRefreshCost at runtime;
            // this authored value is only the neutral design-time default.
            auto* Cost=ReclaimLabel(BP,TEXT("ShopRefreshCost"),TEXT("1"),static_cast<int32>(EGPearlGold::ShopPlateFontSize));
            if(auto* CostSlot=Row->AddChildToHorizontalBox(Cost))
            {CostSlot->SetVerticalAlignment(VAlign_Center);CostSlot->SetPadding(FMargin(EGPearlGold::ShopPlateGlyphGap,0,0,0));}
        }
        if(auto* Slot=Cast<UCanvasPanelSlot>(Button->Slot))
        {
            Slot->SetAnchors(FAnchors(.5f,0.0f));
            Slot->SetPosition(FVector2D(Spec.Rect.X-EGPearlGold::DesignWidth*.5f,Spec.Rect.Y));
            Slot->SetSize(FVector2D(EGPearlGold::ShopCompactButtonWidth,EGPearlGold::ShopCompactButtonHeight));
            Slot->SetAlignment(FVector2D(0,0));
            Slot->SetZOrder(4);
        }
    }
    return true;
}

bool PositionSellZones(UWidgetBlueprint* HUD)
{
    if(!HUD||!HUD->WidgetTree)return false;
    auto* Root=Cast<UCanvasPanel>(HUD->WidgetTree->RootWidget);
    if(!Root)return false;
    for(int32 Side=0;Side<2;++Side)
    {
        auto* Zone=HUD->WidgetTree->FindWidget(Side?TEXT("SellZone_Right"):TEXT("SellZone_Left"));
        if(!Zone)return false;
        auto* Slot=Cast<UCanvasPanelSlot>(Zone->Slot);
        if(!Slot)return false;
        // The full 192x320 plate is placed with exactly half off screen: the
        // remaining 96 units butt against the screen edge with no inset gap.
        Slot->SetAnchors(FAnchors(Side?1.f:0.f,.5f));
        Slot->SetPosition(FVector2D(Side?EGPearlGold::SellPlateVisibleWidth:-EGPearlGold::SellPlateVisibleWidth,0.f));
        Slot->SetSize(FVector2D(EGPearlGold::SellPlateWidth,EGPearlGold::SellPlateHeight));
        Slot->SetAlignment(FVector2D(Side?1.f:0.f,.5f));
        Slot->SetZOrder(45);
        Zone->SetRenderScale(FVector2D(1));
        Zone->SetRenderTransformPivot(FVector2D(.5f,.5f));
        Zone->SetVisibility(ESlateVisibility::Collapsed);
    }
    return true;
}
}
#endif

bool EGApplyShopMotionAssets()
{
#if WITH_EDITOR
    if(!GEditor||GEditor->PlayWorld)return false;
    if(!EGShopMotion::AreArtworkAssetsAvailable())return false;
    auto* Shop=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_Shop")));
    auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_BoardHUD")));
    auto* Left=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_SellLeft")));
    auto* Right=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_SellRight")));
    if(!Shop||!HUD||!Left||!Right)return false;
    if(!AuthorSellZone(Left,false)||!AuthorSellZone(Right,true))return false;
    if(!AuthorShopControls(Shop))return false;
    if(!PositionSellZones(HUD))return false;
    if(!SaveWidgetBP(Left)||!SaveWidgetBP(Right))return false;
    if(!SaveWidgetBP(Shop))return false;
    // Compiling the shop can recompile its host HUD; reacquire the current asset
    // before the final save so the sell-zone slots are written to the real tree.
    HUD=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_BoardHUD")));
    if(!HUD||!PositionSellZones(HUD))return false;
    if(!SaveWidgetBP(HUD))return false;
    // Structural evidence is written from the live authored widgets; it is
    // static asset data only (no PIE, no rendering, no visual acceptance).
    Left=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_SellLeft")));
    Right=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_SellRight")));
    Shop=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_Shop")));
    return WriteStructureReport(Left,Right,Shop,HUD);
#else
    return false;
#endif
}
