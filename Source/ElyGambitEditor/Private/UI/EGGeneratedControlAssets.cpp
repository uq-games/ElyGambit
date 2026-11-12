#include "UI/EGWidgetAuthoringServiceImpl.h"
#include "UI/EGWidgetEditorLibrary.h"
#if WITH_EDITOR
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGGoldCoin.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Serialization/BufferArchive.h"
#include "RenderingThread.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
constexpr const TCHAR* BoardFolder=TEXT("/Game/Blueprints/UI/Widgets/Board/");
constexpr const TCHAR* ArtFolder=TEXT("/Game/Assets/UI/Textures/GeneratedControls20261024/");

template<class T>T* Control(UWidgetBlueprint* BP,const TCHAR* Name)
{
    // Reuse named objects even while their parent is being rebuilt, when a
    // hierarchy-only FindWidget cannot see a temporarily detached child.
    if(auto* Existing=FindObject<T>(BP->WidgetTree,Name))return Existing;
    return BP->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name);
}
UCanvasPanelSlot* PlaceControl(UCanvasPanel* Root,UWidget* Widget,FAnchors Anchors,FVector2D Position,FVector2D Size,FVector2D Alignment=FVector2D::ZeroVector)
{
    Widget->RemoveFromParent();auto* Slot=Root->AddChildToCanvas(Widget);
    Slot->SetAnchors(Anchors);Slot->SetPosition(Position);Slot->SetSize(Size);Slot->SetAlignment(Alignment);return Slot;
}
FSlateBrush ControlArt(const TCHAR* Name,FVector2D Size,FBox2f UV)
{
    FSlateBrush Brush;Brush.SetResourceObject(LoadObject<UTexture2D>(nullptr,*(FString(ArtFolder)+Name)));
    Brush.DrawAs=ESlateBrushDrawType::Image;Brush.ImageSize=Size;Brush.SetUVRegion(UV);return Brush;
}
void StyleLabel(UTextBlock* Label,int32 Size)
{
    Label->SetFont(FEGVisualStyle::Font(Size,true));
    Label->SetJustification(ETextJustify::Center);Label->SetColorAndOpacity(FLinearColor(.97f,.93f,.84f));
    Label->SetVisibility(ESlateVisibility::HitTestInvisible);Label->SetAutoWrapText(false);
}
void StyleControl(UButton* Button,FSlateBrush Brush)
{
    FButtonStyle Style;Style.Normal=Brush;Style.Hovered=Brush;Style.Pressed=Brush;Style.Disabled=Brush;
    Style.Hovered.TintColor=FLinearColor(1.18f,1.18f,1.18f,1);
    Style.Pressed.TintColor=FLinearColor(.78f,.78f,.78f,1);
    Style.Disabled.TintColor=FLinearColor(.65f,.65f,.65f,.7f);
    Style.NormalPadding=Style.PressedPadding=FMargin(0);
    Button->SetStyle(Style);Button->SetBackgroundColor(FLinearColor(.55f,.55f,.55f,1));
}
void FillContent(UContentWidget* Parent,UWidget* Child)
{
    Parent->SetContent(Child);
    if(auto* Slot=Cast<UButtonSlot>(Child->Slot))
    {Slot->SetPadding(FMargin(0));Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
}
bool SaveControlBP(UWidgetBlueprint* BP)
{
    // Reparenting leaves the old content objects under WidgetTree until compilation.
    // Move only unreachable widgets out before registering GUIDs, so the compiler
    // does not first register, then abandon those old control subtrees.
    TSet<UWidget*> Reachable;
    BP->WidgetTree->ForEachWidget([&](UWidget* W){Reachable.Add(W);});
    TArray<UWidget*> Abandoned;
    BP->ForEachSourceWidget([&](UWidget* W){if(W&&!Reachable.Contains(W))Abandoned.Add(W);});
    for(auto* W:Abandoned)W->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
    TSet<FName> Names;
    BP->ForEachSourceWidget([&](UWidget* W){if(!W)return;Names.Add(W->GetFName());if(!BP->WidgetVariableNameToGuidMap.Contains(W->GetFName()))BP->WidgetVariableNameToGuidMap.Add(W->GetFName(),FGuid::NewDeterministicGuid(W->GetPathName()));});
    for(const auto& Animation:BP->Animations)if(Animation){Names.Add(Animation->GetFName());if(!BP->WidgetVariableNameToGuidMap.Contains(Animation->GetFName()))BP->WidgetVariableNameToGuidMap.Add(Animation->GetFName(),FGuid::NewDeterministicGuid(Animation->GetPathName()));}
    for(auto It=BP->WidgetVariableNameToGuidMap.CreateIterator();It;++It)if(!Names.Contains(It.Key()))It.RemoveCurrent();
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
    if(BP->Status==BS_Error)return false;
    if(auto* Class=Cast<UBlueprintGeneratedClass>(BP->GeneratedClass))Class->UpdateCustomPropertyListForPostConstruction();
    BP->MarkPackageDirty();return GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
}
}
#endif

bool FEGWidgetAuthoringService::ApplyGeneratedBoardControls()
{
#if WITH_EDITOR
    if(!GEditor||GEditor->PlayWorld)return false;
    auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_BoardHUD")));
    if(!HUD||!HUD->WidgetTree)return false;
    auto* Root=Cast<UCanvasPanel>(HUD->WidgetTree->RootWidget);
    auto* Shop=Cast<UButton>(HUD->WidgetTree->FindWidget(TEXT("Button_ToggleDrawPanel")));
    auto* Battle=Cast<UButton>(HUD->WidgetTree->FindWidget(TEXT("Button_Battle")));
    auto* Gold=Cast<UTextBlock>(HUD->WidgetTree->FindWidget(TEXT("Text_Gold")));
    auto* BattleText=Cast<UTextBlock>(HUD->WidgetTree->FindWidget(TEXT("Text_Battle")));
    if(!Root||!Shop||!Battle||!Gold||!BattleText||!FEGVisualStyle::UiFont())return false;
    const auto ShopBrush=ControlArt(TEXT("T_UI_GPT_Shop"),FVector2D(124),FBox2f(FVector2f(28.f/1254,22.f/1254),FVector2f(1225.f/1254,1215.f/1254)));
    const auto BattleBrush=ControlArt(TEXT("T_UI_GPT_Battle"),FVector2D(184,52),FBox2f(FVector2f(0,128.f/724),FVector2f(1,566.f/724)));
    if(!ShopBrush.GetResourceObject()||!BattleBrush.GetResourceObject())return false;
    StyleControl(Shop,ShopBrush);StyleControl(Battle,BattleBrush);
    // Center the live text itself, with identical normal/pressed padding.
    Gold->RemoveFromParent();BattleText->RemoveFromParent();
    auto* ShopCanvas=Control<UCanvasPanel>(HUD,TEXT("GeneratedShopContent"));ShopCanvas->ClearChildren();
    FillContent(Shop,ShopCanvas);
    auto* ShopCenter=Control<UBorder>(HUD,TEXT("GeneratedShopCenter"));ShopCenter->SetBrushColor(FLinearColor::Transparent);
    ShopCenter->SetPadding(FMargin(0));ShopCenter->SetHorizontalAlignment(HAlign_Center);ShopCenter->SetVerticalAlignment(VAlign_Center);
    PlaceControl(ShopCanvas,ShopCenter,FAnchors(0,0,1,1),FVector2D::ZeroVector,FVector2D::ZeroVector)->SetOffsets(FMargin(18));
    auto* ShopColumn=Control<UVerticalBox>(HUD,TEXT("GeneratedShopColumn"));ShopColumn->ClearChildren();ShopCenter->SetContent(ShopColumn);
    ShopColumn->SetRenderTranslation(FVector2D(0,-6)); // Optical centering of the font's visible glyphs.
    StyleLabel(Gold,30);Gold->SetText(FText::FromString(TEXT("3")));
    auto* GoldSlot=ShopColumn->AddChildToVerticalBox(Gold);GoldSlot->SetHorizontalAlignment(HAlign_Center);GoldSlot->SetPadding(FMargin(0,0,0,2));
    auto* ShopLabel=Control<UTextBlock>(HUD,TEXT("GeneratedShopLabel"));StyleLabel(ShopLabel,17);ShopLabel->SetText(FText::FromString(TEXT("商店")));
    ShopColumn->AddChildToVerticalBox(ShopLabel)->SetHorizontalAlignment(HAlign_Center);
    auto* BattleCenter=Control<UBorder>(HUD,TEXT("GeneratedBattleCenter"));BattleCenter->SetBrushColor(FLinearColor::Transparent);
    BattleCenter->SetPadding(FMargin(28,0));BattleCenter->SetHorizontalAlignment(HAlign_Center);BattleCenter->SetVerticalAlignment(VAlign_Center);
    FillContent(Battle,BattleCenter);StyleLabel(BattleText,22);BattleText->SetText(FText::FromString(TEXT("开战")));BattleCenter->SetContent(BattleText);
    // Keep the existing right-hand stack and its bindings; only give the CTA balanced width.
    PlaceControl(Root,Battle,FAnchors(1,1),FVector2D(-206,-246),FVector2D(184,52))->SetZOrder(32);

    for(int32 Side=0;Side<2;++Side)
    {
        const FString Name=Side?TEXT("WBP_SellRight"):TEXT("WBP_SellLeft");
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+Name));if(!BP||!BP->WidgetTree)return false;
        auto* Label=Control<UTextBlock>(BP,TEXT("Text_Sell"));Label->RemoveFromParent();
        auto* Canvas=Control<UCanvasPanel>(BP,TEXT("GeneratedSellRoot"));Canvas->ClearChildren();BP->WidgetTree->RootWidget=Canvas;
        auto* Art=Control<UImage>(BP,TEXT("GeneratedSellArtwork"));
        Art->SetBrush(ControlArt(TEXT("T_UI_GPT_SellEdge"),FVector2D(120,400),FBox2f(FVector2f(0,0),FVector2f(535.f/887,1))));
        Art->SetVisibility(ESlateVisibility::HitTestInvisible);Art->SetRenderTransformPivot(FVector2D(.5f));Art->SetRenderScale(FVector2D(Side?-1.f:1.f,1.f));
        PlaceControl(Canvas,Art,FAnchors(0,0,1,1),FVector2D::ZeroVector,FVector2D::ZeroVector)->SetOffsets(FMargin(0));
        auto* Center=Control<UBorder>(BP,TEXT("GeneratedSellCenter"));Center->SetBrushColor(FLinearColor::Transparent);
        Center->SetPadding(FMargin(0));Center->SetHorizontalAlignment(HAlign_Center);Center->SetVerticalAlignment(VAlign_Center);
        PlaceControl(Canvas,Center,FAnchors(0,0,1,1),FVector2D::ZeroVector,FVector2D::ZeroVector)->SetOffsets(FMargin(8,0,8,0));
        auto* Column=Control<UVerticalBox>(BP,TEXT("GeneratedSellColumn"));Column->ClearChildren();Center->SetContent(Column);
        StyleLabel(Label,16);Label->SetText(FText::FromString(TEXT("出售")));Label->SetLineHeightPercentage(1.15f);
        // Keep cancellation and the currency word intact; sale value has its own explicit line.
        Label->SetAutoWrapText(false);Label->SetWrapTextAt(0);
        Column->AddChildToVerticalBox(Label)->SetHorizontalAlignment(HAlign_Center);
        if(!SaveControlBP(BP))return false;
    }
    // Compiling nested widgets may reinstance their HUD hosts. Reacquire the live tree.
    Root=Cast<UCanvasPanel>(HUD->WidgetTree->RootWidget);
    for(int32 Side=0;Side<2;++Side)
    {
        auto* Zone=HUD->WidgetTree->FindWidget(Side?TEXT("SellZone_Right"):TEXT("SellZone_Left"));if(!Zone)return false;
        PlaceControl(Root,Zone,FAnchors(Side?1.f:0.f,.5f),FVector2D(0,24),FVector2D(120,400),FVector2D(Side?1.f:0.f,.5f))->SetZOrder(45);
        Zone->SetRenderTransformPivot(FVector2D(Side?1.f:0.f,.5f));Zone->SetRenderScale(FVector2D(1));Zone->SetVisibility(ESlateVisibility::Collapsed);
    }
    return SaveControlBP(HUD);
#else
    return false;
#endif
}

bool FEGWidgetAuthoringService::RenderGeneratedControlSheet(const FString& Filename,FVector2D Size,bool bShopOpen)
{
#if WITH_EDITOR
    if(!GEditor||GEditor->PlayWorld||Size.X<640||Size.Y<480||Size.X>4096||Size.Y>4096)return false;
    auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+TEXT("WBP_BoardHUD")));if(!HUD)return false;
    UWorld* World=GEditor->GetEditorWorldContext().World();
    // A concrete existing plain UserWidget Blueprint supplies a valid owner/tree;
    // UUserWidget itself is abstract and must never be passed to CreateWidget.
    auto* PreviewClass=LoadClass<UUserWidget>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_SellLeft.WBP_SellLeft_C"));
    auto* Preview=CreateWidget<UUserWidget>(World,PreviewClass);if(!Preview)return false;
    auto* Canvas=Preview->WidgetTree->ConstructWidget<UCanvasPanel>();Preview->WidgetTree->RootWidget=Canvas;
    auto* Back=Preview->WidgetTree->ConstructWidget<UBorder>();Back->SetBrushColor(FLinearColor(.24f,.26f,.27f));
    PlaceControl(Canvas,Back,FAnchors(0,0,1,1),FVector2D::ZeroVector,FVector2D::ZeroVector)->SetOffsets(FMargin(0));
    for(const TCHAR* Name:{TEXT("Button_ToggleDrawPanel"),TEXT("Button_Battle")})
    {
        auto* Template=Cast<UButton>(HUD->WidgetTree->FindWidget(Name));if(!Template)return false;
        auto* Copy=DuplicateObject<UButton>(Template,Preview->WidgetTree);
        auto* SourceSlot=Cast<UCanvasPanelSlot>(Template->Slot);if(!SourceSlot)return false;
        PlaceControl(Canvas,Copy,SourceSlot->GetAnchors(),SourceSlot->GetPosition(),SourceSlot->GetSize(),SourceSlot->GetAlignment());
        Copy->SetBackgroundColor(bShopOpen?FLinearColor::White:FLinearColor(.55f,.55f,.55f,1));
    }
    for(int32 Side=0;Side<2;++Side)
    {
        auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(FString(BoardFolder)+(Side?TEXT("WBP_SellRight"):TEXT("WBP_SellLeft"))));if(!BP)return false;
        auto* Zone=CreateWidget<UUserWidget>(World,BP->GeneratedClass.Get());if(!Zone)return false;
        if(auto* Label=Cast<UTextBlock>(Zone->GetWidgetFromName(TEXT("Text_Sell"))))Label->SetText(FText::FromString(Side?TEXT("取消购买"):TEXT("出售\n+123 崩坏能")));
        PlaceControl(Canvas,Zone,FAnchors(Side?1.f:0.f,.5f),FVector2D(0,24),FVector2D(120,400),FVector2D(Side?1.f:0.f,.5f));
    }
    auto* Renderer=new FWidgetRenderer(false);
    auto* Target=Renderer->DrawWidget(Preview->TakeWidget(),Size);FlushRenderingCommands();
    FImage Captured;TArray64<uint8> Bytes;bool bExported=false;
    if(Target&&FImageUtils::GetRenderTargetImage(Target,Captured))
    {
        // Slate already gamma-corrects this BGRA8 target. Its RT metadata says
        // linear; tagging those stored bytes correctly avoids a washed-out PNG.
        if(Captured.Format==ERawImageFormat::BGRA8)Captured.GammaSpace=EGammaSpace::sRGB;
        bExported=FImageUtils::CompressImage(Bytes,TEXT("PNG"),Captured)&&FFileHelper::SaveArrayToFile(Bytes,*Filename);
    }
    // Record the actual laid-out Slate geometry, alongside the image review.
    // This is a static control render, not a gameplay or viewport-DPI test.
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Checks;
    bool bCentered=true,bGeometryAvailable=true;
    auto CheckCenter=[&](const TCHAR* Name,UWidget* Child,UWidget* Host,FVector2D Expected=FVector2D::ZeroVector)
    {
        if(!Child||!Host){bCentered=false;bGeometryAvailable=false;return;}
        const FGeometry& C=Child->GetPaintSpaceGeometry();const FGeometry& H=Host->GetPaintSpaceGeometry();
        const FVector2D Delta=C.LocalToAbsolute(C.GetLocalSize()*.5)-H.LocalToAbsolute(H.GetLocalSize()*.5);
        const bool bAvailable=C.GetLocalSize().X>0&&C.GetLocalSize().Y>0;
        const bool bValid=bAvailable&&FMath::Abs(Delta.X-Expected.X)<.5&&FMath::Abs(Delta.Y-Expected.Y)<.5;
        bGeometryAvailable&=bAvailable;
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("name"),Name);Row->SetBoolField(TEXT("geometry_available"),bAvailable);
        if(bAvailable){Row->SetNumberField(TEXT("center_dx"),Delta.X);Row->SetNumberField(TEXT("center_dy"),Delta.Y);Row->SetNumberField(TEXT("optical_offset_y"),Expected.Y);Row->SetBoolField(TEXT("centered"),bValid);}
        Checks.Add(MakeShared<FJsonValueObject>(Row));bCentered&=bValid;
    };
    CheckCenter(TEXT("battle_label"),Preview->WidgetTree->FindWidget(TEXT("Text_Battle")),Preview->WidgetTree->FindWidget(TEXT("Button_Battle")));
    CheckCenter(TEXT("shop_text_group"),Preview->WidgetTree->FindWidget(TEXT("GeneratedShopColumn")),Preview->WidgetTree->FindWidget(TEXT("Button_ToggleDrawPanel")),FVector2D(0,-6));
    for(auto* Child:Canvas->GetAllChildren())if(auto* Zone=Cast<UUserWidget>(Child))
    {
        CheckCenter(*Zone->GetName(),Zone->GetWidgetFromName(TEXT("Text_Sell")),Zone);
    }
    Report->SetArrayField(TEXT("alignment"),Checks);Report->SetBoolField(TEXT("geometry_available"),bGeometryAvailable);if(bGeometryAvailable)Report->SetBoolField(TEXT("all_centered"),bCentered);Report->SetNumberField(TEXT("width"),Size.X);Report->SetNumberField(TEXT("height"),Size.Y);
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));
    const bool bRecorded=FFileHelper::SaveStringToFile(Json,*(Filename+TEXT(".layout.json")));
    BeginCleanup(Renderer);return bExported&&bRecorded;
#else
    return false;
#endif
}
