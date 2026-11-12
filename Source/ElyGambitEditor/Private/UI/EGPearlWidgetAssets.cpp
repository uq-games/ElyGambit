#include "UI/EGWidgetAuthoringServiceImpl.h"
// Editor-only authoring: saved Widget Blueprint trees remain fully editable in UMG.
#if WITH_EDITOR
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGGoldCoin.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGCardPresentation.h"
#include "UI/EGPearlGoldLayout.h"
#include "AssetToolsModule.h"
#include "Blueprint/WidgetTree.h"
#include "Animation/WidgetAnimation.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/Border.h"
#include "Components/BackgroundBlur.h"
#include "UI/Widget/EGShopCard.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SizeBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Editor.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Modules/ModuleManager.h"

namespace
{
const FString Folder=TEXT("/Game/Blueprints/UI/Widgets");
const FLinearColor Ink(.055f,.025f,.09f,1),Gold(.25f,.12f,.035f,1),Pearl(.87f,.8f,.96f,1);
const FLinearColor Night(.018f,.022f,.055f,.94f),Champagne(.66f,.48f,.25f,1),Muted(.56f,.54f,.68f,1);
TMap<FString,FBox2f> Regions;
UFont* Font=nullptr;

template<class T>T* Make(UWidgetBlueprint* BP,FName Name)
{return BP->WidgetTree->ConstructWidget<T>(T::StaticClass(),Name);}
UWidgetBlueprint* Blueprint(const TCHAR* Name,UClass* Parent,bool bRebuild)
{
    auto* BP=LoadObject<UWidgetBlueprint>(nullptr,*(Folder+TEXT("/")+Name));
    if(!BP)
    {
        auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=Parent;
        BP=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateAsset(Name,Folder,UWidgetBlueprint::StaticClass(),Factory));
    }
    if(BP&&bRebuild&&BP->WidgetTree)
    {
        BP->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
        BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
    }
    return BP;
}
void RegisterWidgetGuids(UWidgetBlueprint* BP)
{
    // Register every source widget (including direct outer objects, not just the
    // root traversal) before skeleton compilation. Preserve IDs of stable names
    // so Blueprint references survive authoring; remove only stale tree names.
    TMap<FName,FGuid> Current;
    auto Track=[BP,&Current](UObject* Object)
    {
        if(!Object)return;
        const FName Name=Object->GetFName();
        const FGuid* Existing=BP->WidgetVariableNameToGuidMap.Find(Name);
        Current.Add(Name,Existing&&Existing->IsValid()?*Existing:FGuid::NewDeterministicGuid(Object->GetPathName()));
    };
    BP->ForEachSourceWidget([&Track](UWidget* Widget){Track(Widget);});
    for(const auto& Animation:BP->Animations)Track(Animation.Get());
    BP->WidgetVariableNameToGuidMap=MoveTemp(Current);
}
bool Save(UWidgetBlueprint* BP,bool bPersist)
{
    RegisterWidgetGuids(BP);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
    RegisterWidgetGuids(BP);
    FKismetEditorUtilities::CompileBlueprint(BP);BP->MarkPackageDirty();
    if(BP->Status==BS_Error)return false;
    return !bPersist||GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(BP,false);
}
FSlateBrush Brush(const TCHAR* Name,bool bBox=false)
{
    FSlateBrush B;
    auto* Texture=LoadObject<UTexture2D>(nullptr,*(FString(TEXT("/Game/Assets/UI/Textures/PearlLilac/"))+Name));
    B.SetResourceObject(Texture);B.ImageSize=FVector2D(128,128);
    // Image UV cropping is exact; nine-slice margins otherwise include source alpha padding.
    B.DrawAs=ESlateBrushDrawType::Image;
    if(const auto* Region=Regions.Find(Name))B.SetUVRegion(*Region);
    // UV region excludes generated empty canvas; margins protect the visible corner details.
    if(bBox)B.Margin=FMargin(.12f,.35f);
    return B;
}
UImage* Image(UWidgetBlueprint* BP,FName Name,const TCHAR* Sprite)
{
    auto* W=Make<UImage>(BP,Name);W->SetBrush(Brush(Sprite));W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
UTextBlock* Text(UWidgetBlueprint* BP,FName Name,const FString& Label,int32 Size=18,FLinearColor Color=Ink)
{
    auto* W=Make<UTextBlock>(BP,Name);W->SetText(FText::FromString(Label));
    W->SetFont(FSlateFontInfo(Font,Size,TEXT("Regular")));W->SetColorAndOpacity(FSlateColor(Color));
    W->SetJustification(ETextJustify::Center);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
UCanvasPanelSlot* Place(UCanvasPanel* Canvas,UWidget* W,FAnchors A,FMargin O,FVector2D Align=FVector2D::ZeroVector,int32 Z=0)
{auto* S=Canvas->AddChildToCanvas(W);S->SetAnchors(A);S->SetOffsets(O);S->SetAlignment(Align);S->SetZOrder(Z);return S;}
UBorder* Plate(UWidgetBlueprint* BP,FName Name,const TCHAR* Sprite,bool bBox=true)
{
    auto* W=Make<UBorder>(BP,Name);W->SetBrush(Brush(Sprite,bBox));W->SetPadding(FMargin(0));
    W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);return W;
}
UBorder* Dock(UWidgetBlueprint* BP,FName Name)
{
    auto* W=Make<UBorder>(BP,Name);
    W->SetBrush(FSlateRoundedBoxBrush(Night,8.f, FLinearColor(.38f,.28f,.45f,.65f),1.f));
    W->SetPadding(FMargin(0));W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
}
UButton* Button(UWidgetBlueprint* BP,FName Name,const TCHAR* Sprite)
{
    auto* W=Make<UButton>(BP,Name);FButtonStyle Style;
    const bool bCapsule=FString(Sprite).Contains(TEXT("Battle"))||FString(Sprite).Contains(TEXT("Header"));
    Style.SetNormal(Brush(Sprite,bCapsule));
    auto Hover=Brush(Sprite,bCapsule);Hover.TintColor=FSlateColor(FLinearColor(1.12f,1.08f,1.18f,1));Style.SetHovered(Hover);
    auto Press=Brush(Sprite,bCapsule);Press.TintColor=FSlateColor(FLinearColor(.87f,.8f,.95f,1));Style.SetPressed(Press);
    auto Disabled=Brush(Sprite,bCapsule);Disabled.TintColor=FSlateColor(FLinearColor(.7f,.66f,.77f,.65f));Style.SetDisabled(Disabled);
    Style.NormalPadding=FMargin(0);Style.PressedPadding=FMargin(0,1,0,-1);
    W->SetStyle(Style);W->SetBackgroundColor(FLinearColor::White);return W;
}
void Content(UButton* Button,UWidget* Child)
{
    Button->SetContent(Child);
    if(auto* ContentSlot=Cast<UButtonSlot>(Child->Slot))
    {ContentSlot->SetPadding(FMargin(0));ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);}
}
void ReadRegions()
{
    Regions.Reset();FString Json;
    if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectDir()/TEXT("SourceArt/UI/Components/20261016_PearlLilac_V1/UI_Components_Manifest.json"))))return;
    TSharedPtr<FJsonObject> Root;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root))return;
    for(const auto& Item:Root->GetArrayField(TEXT("assets")))
    {
        const auto Object=Item->AsObject();const auto& Size=Object->GetArrayField(TEXT("source_size"));
        const auto& Bound=Object->GetArrayField(TEXT("content_bounds"));
        const double W=Size[0]->AsNumber(),H=Size[1]->AsNumber();
        Regions.Add(Object->GetStringField(TEXT("source_id")),FBox2f(
            FVector2f(FMath::Max(0.,Bound[0]->AsNumber()-4)/W,FMath::Max(0.,Bound[1]->AsNumber()-4)/H),
            FVector2f(FMath::Min(W,Bound[0]->AsNumber()+Bound[2]->AsNumber()+4)/W,FMath::Min(H,Bound[1]->AsNumber()+Bound[3]->AsNumber()+4)/H)));
    }
}
}

bool EGBuildPearlWidgetAssets(bool bRebuild,bool bPersist)
{
    Font=LoadObject<UFont>(nullptr,TEXT("/Game/Assets/UI/Fonts/F_UI_CJK"));
    auto* Art=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/Cards/Pardofelis/T_Card_Pardo"));
    if(!Font||!Art)return false;ReadRegions();
    // Refuse an empty import rather than silently create invisible controls.
    if(!LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/PearlLilac/T_UI_Shop_Base")))return false;
    auto* Card=Blueprint(TEXT("WBP_HandCard"),UEGHandCard::StaticClass(),bRebuild);
    if(!Card)return false;
    if(!Card->WidgetTree->RootWidget)
    {
        auto* Size=Make<USizeBox>(Card,TEXT("CardSize"));Size->SetWidthOverride(112);Size->SetHeightOverride(168);Card->WidgetTree->RootWidget=Size;
        auto* Layers=Make<UOverlay>(Card,TEXT("CardLayers"));Size->SetContent(Layers);
        auto* Artwork=Make<UImage>(Card,TEXT("CardImage"));Artwork->SetBrushFromTexture(Art,false);Artwork->SetVisibility(ESlateVisibility::Visible);
        auto* ArtSlot=Layers->AddChildToOverlay(Artwork);ArtSlot->SetHorizontalAlignment(HAlign_Fill);ArtSlot->SetVerticalAlignment(VAlign_Fill);
        auto* Focus=Plate(Card,TEXT("CardFrame"),TEXT("T_UI_CardFocus_Frame_Clean"),false);Focus->SetRenderOpacity(0);
        auto* FocusSlot=Layers->AddChildToOverlay(Focus);FocusSlot->SetHorizontalAlignment(HAlign_Fill);FocusSlot->SetVerticalAlignment(VAlign_Fill);
    }
    if(!Save(Card,bPersist))return false;
    auto* Left=Blueprint(TEXT("WBP_SellZone_Left"),UUserWidget::StaticClass(),bRebuild);
    auto* Right=Blueprint(TEXT("WBP_SellZone_Right"),UUserWidget::StaticClass(),bRebuild);
    if(!Left||!Right)return false;
    for(auto* BP:{Left,Right})
    {
        if(!BP->WidgetTree->RootWidget)
        {
            auto* Frame=Plate(BP,TEXT("ZoneFrame"),TEXT("T_UI_Sell_Base"));Frame->SetVisibility(ESlateVisibility::Visible);BP->WidgetTree->RootWidget=Frame;
            auto* C=Make<UCanvasPanel>(BP,TEXT("SellCanvas"));Frame->SetContent(C);
            auto* SellIcon=Image(BP,TEXT("SellIcon"),TEXT("T_UI_Icon_Trash_Clean"));SellIcon->SetColorAndOpacity(Ink);
            Place(C,SellIcon,FAnchors(.5,.5),FMargin(-35,0,22,26),FVector2D(.5,.5));
            Place(C,Text(BP,TEXT("Text_Sell"),TEXT("出售"),21),FAnchors(.5,.5),FMargin(12,0,64,30),FVector2D(.5,.5));
        }
        if(!Save(BP,bPersist))return false;
    }
    auto* Shop=Blueprint(TEXT("WBP_DrawPanel"),UUserWidget::StaticClass(),bRebuild);
    if(!Shop)return false;
    if(!Shop->WidgetTree->RootWidget)
    {
        auto* Back=Plate(Shop,TEXT("DrawBackdrop"),TEXT("T_UI_ShopPanel_Base"));Shop->WidgetTree->RootWidget=Back;
        auto* C=Make<UCanvasPanel>(Shop,TEXT("DrawCanvas"));Back->SetContent(C);
        Place(C,Text(Shop,TEXT("Text_ShopTitle"),TEXT("空梦商店"),26),FAnchors(.5,0),FMargin(0,25,260,42),FVector2D(.5,0));
        auto* Close=Button(Shop,TEXT("Button_Close"),TEXT("T_UI_Icon_Close"));Close->SetBackgroundColor(Ink);
        Place(C,Close,FAnchors(1,0),FMargin(-30,27,24,24),FVector2D(1,0));
        auto* CardArt=Make<UImage>(Shop,TEXT("DrawCardArtwork"));CardArt->SetBrushFromTexture(Art,false);CardArt->SetVisibility(ESlateVisibility::HitTestInvisible);
        Place(C,CardArt,FAnchors(0,.5),FMargin(62,12,176,264),FVector2D(0,.5));
        Place(C,Text(Shop,TEXT("Text_DrawTitle"),TEXT("往世的涟漪"),25),FAnchors(0,.5),FMargin(264,-60,380,46),FVector2D(0,.5));
        Place(C,Text(Shop,TEXT("Text_DrawHelp"),TEXT("可领取卡牌"),16),FAnchors(0,.5),FMargin(264,-13,380,36),FVector2D(0,.5));
        auto* Obtain=Button(Shop,TEXT("Button_ObtainElysia"),TEXT("T_UI_Battle_Base"));Content(Obtain,Text(Shop,TEXT("Text_Obtain"),TEXT("领取卡牌"),20));
        Place(C,Obtain,FAnchors(0,.5),FMargin(317,60,270,46),FVector2D(0,.5));
        Place(C,Text(Shop,TEXT("Text_Gold"),TEXT("崩坏能  0"),17,Gold),FAnchors(1,1),FMargin(-46,-30,240,30),FVector2D(1,1));
    }
    if(!Shop->WidgetTree->FindWidget(TEXT("Button_NextHero")))
    {
     auto* C=Cast<UCanvasPanel>(Shop->WidgetTree->FindWidget(TEXT("DrawCanvas")));
     if(C){auto* Next=Button(Shop,TEXT("Button_NextHero"),TEXT("T_UI_Battle_Base"));Content(Next,Text(Shop,TEXT("Text_NextHero"),TEXT("切换英雄 →"),18));Place(C,Next,FAnchors(0,.5),FMargin(317,119,270,40),FVector2D(0,.5));}
    }
    if(!Save(Shop,bPersist))return false;
    auto* HUD=Blueprint(TEXT("WBP_MainHUD"),UEGMainHUD::StaticClass(),bRebuild);
    if(!HUD)return false;
    if(!HUD->WidgetTree->RootWidget)
    {
        auto* C=Make<UCanvasPanel>(HUD,TEXT("MainCanvas"));C->SetVisibility(ESlateVisibility::SelfHitTestInvisible);HUD->WidgetTree->RootWidget=C;
        // Quiet screen-edge information leaves all thirty board cells available for input.
        auto* Identity=Dock(HUD,TEXT("IdentityDock"));
        Place(C,Identity,FAnchors(0,0),FMargin(28,24,290,82),FVector2D::ZeroVector,5);
        auto* IdentityC=Make<UCanvasPanel>(HUD,TEXT("IdentityContent"));Identity->SetContent(IdentityC);
        auto* Brand=Text(HUD,TEXT("Text_Brand"),TEXT("往世弈局"),22,Pearl);Brand->SetJustification(ETextJustify::Left);
        Place(IdentityC,Brand,FAnchors(0,0),FMargin(86,13,188,30));
        auto* Chapter=Text(HUD,TEXT("Text_Location"),TEXT("绘世者的梦境"),13,Muted);Chapter->SetJustification(ETextJustify::Left);
        Place(IdentityC,Chapter,FAnchors(0,0),FMargin(87,48,188,22));
        auto* Economy=Dock(HUD,TEXT("EconomyDock"));
        Place(C,Economy,FAnchors(1,0),FMargin(-28,24,214,70),FVector2D(1,0),5);
        auto* EconomyC=Make<UCanvasPanel>(HUD,TEXT("EconomyContent"));Economy->SetContent(EconomyC);
        Place(EconomyC,Text(HUD,TEXT("Text_GoldLabel"),TEXT("持有崩坏能"),12,Muted),FAnchors(0,0),FMargin(10,10,82,22));
        Place(EconomyC,Text(HUD,TEXT("Text_HUDGold"),TEXT("0"),25,FLinearColor(.94f,.77f,.48f,1)),FAnchors(1,0),FMargin(-16,17,116,38),FVector2D(1,0));
        Place(EconomyC,Text(HUD,TEXT("Text_MatchState"),TEXT("自由部署"),13,Pearl),FAnchors(0,0),FMargin(10,38,82,22));
        auto* HintDock=Dock(HUD,TEXT("HintDock"));
        Place(C,HintDock,FAnchors(.5,1),FMargin(0,-235,540,32),FVector2D(.5,1),6);
        auto* Hint=Text(HUD,TEXT("Text_PlacementHint"),TEXT("拖动手牌至近端三行部署 · Esc 取消"),13,Pearl);
        HintDock->SetContent(Hint);
        // A slim adaptive hand tray reserves the two screen-edge sell strips and action dock.
        auto* Hand=Make<UBorder>(HUD,TEXT("HandPanel"));Hand->SetBrushColor(FLinearColor::Transparent);Hand->SetPadding(FMargin(0));
        Place(C,Hand,FAnchors(.16f,1,.84f,1),FMargin(0,-16,0,208),FVector2D(0,1),10);
        auto* HandCanvas=Make<UCanvasPanel>(HUD,TEXT("HandCanvas"));Hand->SetContent(HandCanvas);
        auto* Tray=Image(HUD,TEXT("HandTray"),TEXT("T_UI_HandTray_Base"));Tray->SetRenderOpacity(.25f);
        Place(HandCanvas,Tray,FAnchors(.5,1),FMargin(0,0,960,85),FVector2D(.5,1));
        auto* Column=Make<UVerticalBox>(HUD,TEXT("HandContent"));
        Place(HandCanvas,Column,FAnchors(0,0,1,1),FMargin(0));
        auto* Count=Text(HUD,TEXT("Text_HandCount"),TEXT("手牌  1"),15,Pearl);
        Count->SetShadowColorAndOpacity(FLinearColor(0,0,0,.9f));Count->SetShadowOffset(FVector2D(0,1));
        auto* CountSlot=Column->AddChildToVerticalBox(Count);CountSlot->SetPadding(FMargin(0,1,0,0));
        auto* Scroll=Make<UScrollBox>(HUD,TEXT("HandScrollBox"));Scroll->SetOrientation(Orient_Horizontal);Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
        Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
        auto* HandViewport=Make<USizeBox>(HUD,TEXT("HandViewport"));HandViewport->SetWidthOverride(124);HandViewport->SetContent(Scroll);
        auto* ViewportSlot=Column->AddChildToVerticalBox(HandViewport);ViewportSlot->SetHorizontalAlignment(HAlign_Center);ViewportSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* L=HUD->WidgetTree->ConstructWidget<UUserWidget>(Left->GeneratedClass.Get(),TEXT("SellZone_Left"));L->SetVisibility(ESlateVisibility::Collapsed);
        auto* R=HUD->WidgetTree->ConstructWidget<UUserWidget>(Right->GeneratedClass.Get(),TEXT("SellZone_Right"));R->SetVisibility(ESlateVisibility::Collapsed);
        Place(C,L,FAnchors(0,1),FMargin(24,-18,186,72),FVector2D(0,1),15);
        Place(C,R,FAnchors(1,1),FMargin(-24,-18,186,72),FVector2D(1,1),15);
        auto* Blocker=Make<UBorder>(HUD,TEXT("ModalBlocker"));Blocker->SetBrushColor(FLinearColor(.016f,.012f,.038f,.58f));Blocker->SetVisibility(ESlateVisibility::Collapsed);
        Place(C,Blocker,FAnchors(0,0,1,1),FMargin(0),FVector2D::ZeroVector,20);
        auto* Panel=HUD->WidgetTree->ConstructWidget<UUserWidget>(Shop->GeneratedClass.Get(),TEXT("DrawPanel"));Panel->SetVisibility(ESlateVisibility::Collapsed);
        Place(C,Panel,FAnchors(.5f,.47f),FMargin(0,0,740,470),FVector2D(.5,.5),25);
        auto* Stage=Plate(HUD,TEXT("StagePanel"),TEXT("T_UI_StageStrip_Base"));Stage->SetVisibility(ESlateVisibility::Collapsed);
        Place(C,Stage,FAnchors(.5,0),FMargin(0,88,840,106),FVector2D(.5,0),25);
        auto* Nodes=Make<UCanvasPanel>(HUD,TEXT("StageNodes"));Stage->SetContent(Nodes);
        static const TCHAR* Roman[]={TEXT("I"),TEXT("II"),TEXT("III"),TEXT("IV"),TEXT("V"),TEXT("VI"),TEXT("VII")};
        for(int32 I=0;I<7;++I)
        {
            const float X=74+I*115.f;
            if(I<6)Place(Nodes,Image(HUD,FName(*FString::Printf(TEXT("StageLink_%d"),I)),TEXT("T_UI_StageConnector")),FAnchors(0,0),FMargin(X+22,39,71,2));
            auto* Ring=Image(HUD,FName(*FString::Printf(TEXT("StageRing_%d"),I+1)),TEXT("T_UI_StageNode_Ring"));
            if(I==2||I==5)Ring->SetColorAndOpacity(FLinearColor(1,.22f,.32f,1));
            Place(Nodes,Ring,FAnchors(0,0),FMargin(X,39,32,32),FVector2D(.5,.5));
            Place(Nodes,Text(HUD,FName(*FString::Printf(TEXT("StageLabel_%d"),I+1)),Roman[I],18,(I==2||I==5)?FLinearColor(.73f,.12f,.24f,1):Pearl),FAnchors(0,0),FMargin(X,74,70,28),FVector2D(.5,.5));
            auto* Marker=Image(HUD,FName(*FString::Printf(TEXT("StageCurrent_%d"),I+1)),TEXT("T_UI_StageCurrent_Marker"));Marker->SetVisibility(ESlateVisibility::Collapsed);
            Place(Nodes,Marker,FAnchors(0,0),FMargin(X,12,16,20),FVector2D(.5,.5));
            if(I==6)Place(Nodes,Image(HUD,TEXT("StageBoss"),TEXT("T_UI_Icon_Crown")),FAnchors(0,0),FMargin(X,10,22,16),FVector2D(.5,.5));
        }
        auto* Header=Button(HUD,TEXT("Button_Stage"),TEXT("T_UI_StageHeader_Base"));
        auto* HeaderC=Make<UCanvasPanel>(HUD,TEXT("StageHeaderContent"));Content(Header,HeaderC);
        Place(HeaderC,Text(HUD,TEXT("Text_Round"),TEXT("第 1 回合"),20),FAnchors(0,0,1,1),FMargin(14,6,32,6));
        auto* Chevron=Image(HUD,TEXT("StageChevron"),TEXT("T_UI_Icon_Chevron"));Chevron->SetColorAndOpacity(Ink);
        Place(HeaderC,Chevron,FAnchors(1,.5),FMargin(-17,0,12,8),FVector2D(1,.5));
        Place(C,Header,FAnchors(.5,0),FMargin(0,25,310,44),FVector2D(.5,0),30);
        auto* ShopButton=Button(HUD,TEXT("Button_ToggleDrawPanel"),TEXT("T_UI_Shop_Base"));
        auto* ShopC=Make<UCanvasPanel>(HUD,TEXT("ShopButtonContent"));Content(ShopButton,ShopC);
        auto* Halo=Image(HUD,TEXT("ShopHoverHalo"),TEXT("T_UI_Shop_Halo"));Halo->SetRenderOpacity(0);
        Place(ShopC,Halo,FAnchors(.5,.5),FMargin(0,0,124,124),FVector2D(.5,.5));
        Place(ShopC,Image(HUD,TEXT("ShopEmblem"),TEXT("T_UI_Pardofelis_Signet")),FAnchors(.5f,.42f),FMargin(0,0,40,40),FVector2D(.5,.5));
        Place(ShopC,Text(HUD,TEXT("Text_DrawToggle"),TEXT("商店"),16),FAnchors(.5f,.76f),FMargin(0,0,70,26),FVector2D(.5,.5));
        Place(C,ShopButton,FAnchors(1,1),FMargin(-54,-108,112,112),FVector2D(1,1),30);
        auto* Battle=Button(HUD,TEXT("Button_Battle"),TEXT("T_UI_Battle_Base"));
        auto* BattleC=Make<UCanvasPanel>(HUD,TEXT("BattleContent"));Content(Battle,BattleC);
        auto* Swords=Image(HUD,TEXT("BattleIcon"),TEXT("T_UI_Icon_Swords"));Swords->SetColorAndOpacity(Ink);
        Place(BattleC,Swords,FAnchors(.5,.5),FMargin(-27,0,18,18),FVector2D(.5,.5));
        Place(BattleC,Text(HUD,TEXT("Text_Battle"),TEXT("作战"),17),FAnchors(.5,.5),FMargin(13,0,50,28),FVector2D(.5,.5));
        Place(C,Battle,FAnchors(1,1),FMargin(-36,-239,156,48),FVector2D(1,1),30);
    }
    if(!Save(HUD,bPersist))return false;
    auto* Defaults=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject());
    Defaults->CardWidgetClass=Card->GeneratedClass.Get();Defaults->CardArtwork=Art;
    HUD->MarkPackageDirty();return !bPersist||GEditor->GetEditorSubsystem<UEditorAssetSubsystem>()->SaveLoadedAsset(HUD,false);
}
bool EGBuildRunPanelAsset(bool bPersist)
{
 Font=LoadObject<UFont>(nullptr,TEXT("/Game/Assets/UI/Fonts/F_UI_CJK"));if(!Font)return false;
 auto* BP=Blueprint(TEXT("WBP_RunPanel"),UEGDemoPanel::StaticClass(),false);if(!BP)return false;
 if(BP->WidgetTree->RootWidget)return true; // Designer edits are never rebuilt by this tool.
 auto* Root=Make<UCanvasPanel>(BP,TEXT("RunRoot"));BP->WidgetTree->RootWidget=Root;Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
 auto* Background=Make<UBorder>(BP,TEXT("Panel_RunBackground"));Background->SetBrush(FSlateRoundedBoxBrush(Night,8.f,FLinearColor(.38f,.28f,.45f,.65f),1.f));Background->SetPadding(FMargin(12));
 auto* Box=Make<UVerticalBox>(BP,TEXT("Box_RunStatus"));Background->SetContent(Box);
 Place(Root,Background,FAnchors(0,0),FMargin(28,122,420,242));
 auto Add=[&](UVerticalBox* Parent,UWidget* W,float Pad=6.f){auto* S=Parent->AddChildToVerticalBox(W);S->SetPadding(FMargin(Pad));};
 auto* Status=Text(BP,TEXT("Text_RunStatus"),TEXT("准备布阵"),17,Pearl);Status->SetJustification(ETextJustify::Left);Add(Box,Status);
 auto* Help=Text(BP,TEXT("Text_RunHelp"),TEXT("布阵后开始作战"),14,Muted);Help->SetJustification(ETextJustify::Left);Help->SetWrapTextAt(372);Help->SetAutoWrapText(true);Add(Box,Help);
 auto* Action=Make<UButton>(BP,TEXT("Button_RunAction"));FButtonStyle Style;
 Style.SetNormal(FSlateRoundedBoxBrush(FLinearColor(.12f,.075f,.2f),8.f,Champagne,1));
 Style.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.22f,.13f,.33f),8.f,Champagne,1));
 Style.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.08f,.045f,.13f),8.f,Champagne,1));
 Style.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.045f,.035f,.065f),8.f,Muted,1));Action->SetStyle(Style);
 Content(Action,Text(BP,TEXT("Text_RunAction"),TEXT("开始作战"),18,Pearl));
 auto* Size=Make<USizeBox>(BP,TEXT("RunActionSize"));Size->SetHeightOverride(46);Size->SetContent(Action);Add(Box,Size);
 return Save(BP,bPersist);
}
bool EGApplyBoardCompositionAssets()
{
 auto* Vitals=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/WBP_UnitVitals.WBP_UnitVitals"));
 auto* HUD=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_BoardHUD.WBP_BoardHUD"));
 if(!Vitals||!HUD||!Vitals->WidgetTree->RootWidget||!HUD->WidgetTree->RootWidget)return false;
 UEGUnitVitals::ApplyLayoutMetrics(Vitals->WidgetTree);
 auto Patch=[HUD](const TCHAR* Name,FVector2D Position,FVector2D Size)
 {
  auto* Widget=HUD->WidgetTree->FindWidget(FName(Name));auto* Slot=Widget?Cast<UCanvasPanelSlot>(Widget->Slot):nullptr;
  if(!Slot)return false;Slot->SetPosition(Position);Slot->SetSize(Size);return true;
 };
 if(!Patch(TEXT("Button_Battle"),FVector2D(EGPearlGold::Battle.X-EGPearlGold::DesignWidth,EGPearlGold::Battle.Y-EGPearlGold::DesignHeight),EGPearlGold::Battle.Size())||
    !Patch(TEXT("HandPanel"),FVector2D(0,-EGPearlGold::HandBottom),FVector2D(EGPearlGold::HandMaxWidth,EGPearlGold::HandPanelHeight)))return false;
 if(auto* Viewport=Cast<USizeBox>(HUD->WidgetTree->FindWidget(TEXT("HandViewport"))))
 {Viewport->SetWidthOverride(EGPearlGold::HandMaxWidth);Viewport->SetHeightOverride(EGPearlGold::HandPanelHeight);}
 return Save(Vitals,true)&&Save(HUD,true);
}
bool EGApplyUnitVitalsAssets()
{
 auto* Vitals=LoadObject<UWidgetBlueprint>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/WBP_UnitVitals.WBP_UnitVitals"));
 if(!Vitals||!Vitals->WidgetTree)return false;
 UEGUnitVitals::BuildLayout(Vitals->WidgetTree);
 if(Vitals->GeneratedClass)
  if(auto* Defaults=Cast<UEGUnitVitals>(Vitals->GeneratedClass->GetDefaultObject()))Defaults->VitalsStyle=FEGUnitVitalsStyle();
 ResetLoaders(Vitals->GetOutermost());
 return Save(Vitals,true);
}
bool EGApplyBattleUIAssets(bool bPersist)
{
 Font=LoadObject<UFont>(nullptr,TEXT("/Game/Assets/UI/Fonts/F_UI_CJK"));if(!Font)return false;
 const FLinearColor Edge(.018f,.024f,.025f,1),Paper(.8f,.79f,.66f,1),Accent(.68f,.47f,.19f,1);
 auto* Vitals=Blueprint(TEXT("WBP_UnitVitals"),UEGUnitVitals::StaticClass(),false);
 if(!Vitals)return false;if(!Vitals->WidgetTree->RootWidget)UEGUnitVitals::BuildLayout(Vitals->WidgetTree);
 UEGUnitVitals::ApplyLayoutMetrics(Vitals->WidgetTree);
 if(!Save(Vitals,bPersist))return false;
 auto Restyle=[&](UWidgetBlueprint* BP)
 {
  BP->WidgetTree->ForEachWidget([&](UWidget* W)
  {
   if(auto* T=Cast<UTextBlock>(W)){T->SetColorAndOpacity(Edge);auto F=T->GetFont();F.OutlineSettings.OutlineSize=0;T->SetFont(F);}
   if(auto* B=Cast<UBorder>(W))
   {
    if(W->GetName()==TEXT("ModalBlocker")){B->SetBrushColor(FLinearColor(.01f,.015f,.02f,.55f));return;}
    if(W->GetName()==TEXT("CardFrame")){B->SetBrush(FSlateRoundedBoxBrush(FLinearColor::Transparent,2.f,Accent,3.f));return;}
    if(W->GetName()==TEXT("HandPanel")){B->SetBrushColor(FLinearColor::Transparent);return;}
    B->SetBrush(FSlateRoundedBoxBrush(Paper,3.f,Edge,3.f));
   }
   if(auto* B=Cast<UButton>(W))
   {
    FButtonStyle S;S.SetNormal(FSlateRoundedBoxBrush(Paper,3.f,Edge,3.f));S.SetHovered(FSlateRoundedBoxBrush(FLinearColor(.96f,.88f,.63f),3.f,Accent,3.f));
    S.SetPressed(FSlateRoundedBoxBrush(FLinearColor(.62f,.57f,.4f),3.f,Edge,3.f));S.SetDisabled(FSlateRoundedBoxBrush(FLinearColor(.3f,.32f,.29f),3.f,Edge,2.f));
    S.NormalPadding=FMargin(0);S.PressedPadding=FMargin(0,1,0,-1);B->SetStyle(S);B->SetBackgroundColor(FLinearColor::White);
   }
  });
 };
 auto Move=[&](UWidgetBlueprint* BP,const TCHAR* Name,FAnchors A,FMargin O,FVector2D Align=FVector2D::ZeroVector)
 {auto* W=BP->WidgetTree->FindWidget(Name);auto* S=W?Cast<UCanvasPanelSlot>(W->Slot):nullptr;if(!S)return false;S->SetAnchors(A);S->SetOffsets(O);S->SetAlignment(Align);return true;};
 auto* HUD=Blueprint(TEXT("WBP_MainHUD"),UEGMainHUD::StaticClass(),false);
 auto* Shop=Blueprint(TEXT("WBP_DrawPanel"),UUserWidget::StaticClass(),false);
 auto* Run=Blueprint(TEXT("WBP_RunPanel"),UEGDemoPanel::StaticClass(),false);
 auto* Card=Blueprint(TEXT("WBP_HandCard"),UEGHandCard::StaticClass(),false);
 if(!HUD||!Shop||!Run||!Card||!HUD->WidgetTree->RootWidget||!Run->WidgetTree->RootWidget)return false;
 for(auto* BP:{HUD,Shop,Run,Card})Restyle(BP);
 if(auto* Identity=HUD->WidgetTree->FindWidget(TEXT("IdentityDock")))Identity->SetVisibility(ESlateVisibility::Collapsed);
 Move(HUD,TEXT("EconomyDock"),FAnchors(0,0),FMargin(24,24,270,100));
 Move(HUD,TEXT("Text_HUDGold"),FAnchors(1,0),FMargin(-16,9,128,40),FVector2D(1,0));
 Move(HUD,TEXT("Text_MatchState"),FAnchors(0,0),FMargin(12,39,220,23));
 if(!HUD->WidgetTree->FindWidget(TEXT("Text_HeroSummary")))
 {auto* C=Cast<UCanvasPanel>(HUD->WidgetTree->FindWidget(TEXT("EconomyContent")));if(!C)return false;Place(C,Text(HUD,TEXT("Text_HeroSummary"),TEXT("出战 0 · 备战 0 · 觉醒 0"),13,Edge),FAnchors(0,0),FMargin(12,68,246,24));}
 Move(HUD,TEXT("Button_Stage"),FAnchors(.5,0),FMargin(0,24,440,40),FVector2D(.5,0));
 Move(HUD,TEXT("StagePanel"),FAnchors(.5,0),FMargin(0,86,840,106),FVector2D(.5,0));
 if(auto* Stage=HUD->WidgetTree->FindWidget(TEXT("StagePanel")))Stage->SetRenderOpacity(.94f);
 Move(HUD,TEXT("HintDock"),FAnchors(.5,1),FMargin(0,-254,700,28),FVector2D(.5,1));
 Move(HUD,TEXT("HandPanel"),FAnchors(.14f,1,.82f,1),FMargin(0,-18,0,230),FVector2D(0,1));
 if(auto* Tray=HUD->WidgetTree->FindWidget(TEXT("HandTray")))Tray->SetVisibility(ESlateVisibility::Collapsed);
 if(auto* T=Cast<UTextBlock>(HUD->WidgetTree->FindWidget(TEXT("Text_HandCount"))))T->SetColorAndOpacity(FLinearColor(1,.94f,.77f));
 auto* Column=Cast<UVerticalBox>(HUD->WidgetTree->FindWidget(TEXT("HandContent")));if(!Column)return false;
 if(!HUD->WidgetTree->FindWidget(TEXT("ReserveScrollBox")))
 {
  auto* Scroll=Make<UScrollBox>(HUD,TEXT("ReserveScrollBox"));Scroll->SetOrientation(Orient_Horizontal);Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);
  auto* Size=Make<USizeBox>(HUD,TEXT("ReserveViewport"));Size->SetHeightOverride(92);Size->SetContent(Scroll);
  Column->InsertChildAt(1,Size);
 }
 if(auto* Size=Cast<USizeBox>(HUD->WidgetTree->FindWidget(TEXT("HandViewport")))){Size->SetHeightOverride(116);if(auto* S=Cast<UVerticalBoxSlot>(Size->Slot))S->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));}
 Move(HUD,TEXT("SellZone_Left"),FAnchors(0,.5),FMargin(24,0,150,78),FVector2D(0,.5));
 Move(HUD,TEXT("SellZone_Right"),FAnchors(1,.5),FMargin(-24,0,150,78),FVector2D(1,.5));
 Move(HUD,TEXT("Button_ToggleDrawPanel"),FAnchors(1,1),FMargin(-24,-96,280,54),FVector2D(1,1));
 Move(HUD,TEXT("Button_Battle"),FAnchors(1,1),FMargin(-24,-24,280,56),FVector2D(1,1));
 for(const TCHAR* Name:{TEXT("ShopHoverHalo"),TEXT("ShopEmblem")})if(auto* W=HUD->WidgetTree->FindWidget(Name))W->SetVisibility(ESlateVisibility::Collapsed);
 Move(HUD,TEXT("Text_DrawToggle"),FAnchors(.5,.5),FMargin(0,0,240,36),FVector2D(.5,.5));
 // Add/edit named detail controls without replacing the HUD tree or Blueprint graphs.
 auto* MainCanvas=Cast<UCanvasPanel>(HUD->WidgetTree->RootWidget);if(!MainCanvas)return false;
 auto* DetailDock=Cast<UBorder>(HUD->WidgetTree->FindWidget(TEXT("UnitDetailDock")));
 if(!DetailDock){DetailDock=Make<UBorder>(HUD,TEXT("UnitDetailDock"));Place(MainCanvas,DetailDock,FAnchors(0,0),FMargin(24,150,340,620),FVector2D::ZeroVector,12);}
 Move(HUD,TEXT("UnitDetailDock"),FAnchors(0,0),FMargin(24,150,340,620));DetailDock->SetPadding(FMargin(0));DetailDock->SetBrush(FSlateRoundedBoxBrush(Paper,3.f,Edge,3.f));DetailDock->SetVisibility(ESlateVisibility::Collapsed);
 auto* DetailCanvas=Cast<UCanvasPanel>(HUD->WidgetTree->FindWidget(TEXT("UnitDetailCanvas")));
 if(!DetailCanvas)
 {
  DetailCanvas=Make<UCanvasPanel>(HUD,TEXT("UnitDetailCanvas"));
  auto* Existing=HUD->WidgetTree->FindWidget(TEXT("Text_UnitDetail"));if(Existing)Existing->RemoveFromParent();DetailDock->SetContent(DetailCanvas);
  Place(DetailCanvas,Text(HUD,TEXT("Text_UnitPanelTitle"),TEXT("英雄详情"),16,Edge),FAnchors(0,0),FMargin(14,10,230,26));
  auto* Close=Make<UButton>(HUD,TEXT("Button_CloseHeroDetails"));Content(Close,Text(HUD,TEXT("Text_CloseHeroDetails"),TEXT("×"),20,Edge));Place(DetailCanvas,Close,FAnchors(1,0),FMargin(-10,8,28,28),FVector2D(1,0));
  auto* Portrait=Make<UImage>(HUD,TEXT("UnitDetailPortrait"));Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);Place(DetailCanvas,Portrait,FAnchors(0,0),FMargin(16,49,84,126));
  auto* Heading=Text(HUD,TEXT("Text_UnitHeading"),TEXT(""),20,Edge);Heading->SetJustification(ETextJustify::Left);Place(DetailCanvas,Heading,FAnchors(0,0),FMargin(116,48,205,62));
  auto* Meta=Text(HUD,TEXT("Text_UnitMeta"),TEXT(""),13,Accent);Meta->SetJustification(ETextJustify::Left);Place(DetailCanvas,Meta,FAnchors(0,0),FMargin(116,112,205,42));
  auto* VitalsText=Text(HUD,TEXT("Text_UnitVitals"),TEXT(""),13,Edge);VitalsText->SetJustification(ETextJustify::Left);Place(DetailCanvas,VitalsText,FAnchors(0,0),FMargin(116,158,205,42));
  auto* StatsText=Text(HUD,TEXT("Text_UnitStats"),TEXT(""),14,Edge);StatsText->SetJustification(ETextJustify::Left);Place(DetailCanvas,StatsText,FAnchors(0,0),FMargin(16,212,308,76));
  auto* Scroll=Make<UScrollBox>(HUD,TEXT("UnitDetailScroll"));Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);Place(DetailCanvas,Scroll,FAnchors(0,0,1,1),FMargin(16,298,16,16));
  auto* Body=Cast<UTextBlock>(Existing);if(!Body)Body=Text(HUD,TEXT("Text_UnitDetail"),TEXT(""),14,Edge);
  Body->SetJustification(ETextJustify::Left);Body->SetAutoWrapText(true);Body->SetWrapTextAt(292);Scroll->AddChild(Body);
 }
 auto* PreviewPanel=Cast<UBorder>(HUD->WidgetTree->FindWidget(TEXT("StagePreviewPanel")));
 if(!PreviewPanel)
 {
  PreviewPanel=Make<UBorder>(HUD,TEXT("StagePreviewPanel"));PreviewPanel->SetPadding(FMargin(0));PreviewPanel->SetBrush(FSlateRoundedBoxBrush(Paper,3.f,Edge,3.f));PreviewPanel->SetVisibility(ESlateVisibility::Collapsed);
  Place(MainCanvas,PreviewPanel,FAnchors(.5,0),FMargin(0,96,900,500),FVector2D(.5,0),26);
  auto* Canvas=Make<UCanvasPanel>(HUD,TEXT("StagePreviewCanvas"));PreviewPanel->SetContent(Canvas);
  Place(Canvas,Text(HUD,TEXT("Text_StagePreviewTitle"),TEXT("关卡效果预览"),22,Edge),FAnchors(0,0),FMargin(22,14,800,36));
  auto* Close=Make<UButton>(HUD,TEXT("Button_CloseStagePreview"));Content(Close,Text(HUD,TEXT("Text_CloseStagePreview"),TEXT("×"),20,Edge));Place(Canvas,Close,FAnchors(1,0),FMargin(-16,16,30,30),FVector2D(1,0));
  auto* Timeline=Make<UScrollBox>(HUD,TEXT("StagePreviewScroll"));Timeline->SetOrientation(Orient_Horizontal);Timeline->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);Place(Canvas,Timeline,FAnchors(0,0,1,0),FMargin(18,60,18,102));
  auto* Scroll=Make<UScrollBox>(HUD,TEXT("StagePreviewDetailScroll"));Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);Place(Canvas,Scroll,FAnchors(0,0,1,1),FMargin(26,178,26,22));
  auto* Body=Text(HUD,TEXT("Text_StagePreviewDetail"),TEXT("选择关卡查看效果"),16,Edge);Body->SetJustification(ETextJustify::Left);Body->SetAutoWrapText(true);Body->SetWrapTextAt(810);Scroll->AddChild(Body);
 }
 Move(HUD,TEXT("DrawPanel"),FAnchors(1,1),FMargin(-24,-166,300,520),FVector2D(1,1));
 Move(Shop,TEXT("Text_ShopTitle"),FAnchors(.5,0),FMargin(0,20,250,38),FVector2D(.5,0));
 Move(Shop,TEXT("Button_Close"),FAnchors(1,0),FMargin(-14,14,24,24),FVector2D(1,0));
 if(auto* B=Cast<UButton>(Shop->WidgetTree->FindWidget(TEXT("Button_Close"))))B->SetToolTipText(FText::FromString(TEXT("关闭")));
 Move(Shop,TEXT("DrawCardArtwork"),FAnchors(.5,0),FMargin(0,70,132,198),FVector2D(.5,0));
 Move(Shop,TEXT("Text_DrawTitle"),FAnchors(.5,0),FMargin(0,280,270,36),FVector2D(.5,0));
 Move(Shop,TEXT("Text_DrawHelp"),FAnchors(.5,0),FMargin(0,322,268,48),FVector2D(.5,0));
 if(auto* T=Cast<UTextBlock>(Shop->WidgetTree->FindWidget(TEXT("Text_DrawHelp")))){T->SetAutoWrapText(true);T->SetFont(FSlateFontInfo(Font,13));}
 Move(Shop,TEXT("Button_ObtainElysia"),FAnchors(.5,0),FMargin(0,382,264,46),FVector2D(.5,0));
 if(auto* T=Cast<UTextBlock>(Shop->WidgetTree->FindWidget(TEXT("Text_Obtain"))))T->SetFont(FSlateFontInfo(Font,16));
 Move(Shop,TEXT("Button_NextHero"),FAnchors(.5,0),FMargin(0,440,264,38),FVector2D(.5,0));
 Move(Shop,TEXT("Text_Gold"),FAnchors(.5,1),FMargin(0,-8,264,24),FVector2D(.5,1));
 auto* RunRoot=Cast<UCanvasPanel>(Run->WidgetTree->RootWidget);if(!RunRoot)return false;
 if(auto* W=Run->WidgetTree->FindWidget(TEXT("Panel_RunBackground")))W->SetVisibility(ESlateVisibility::Collapsed);
 for(const TCHAR* Name:{TEXT("Text_RunStatus"),TEXT("Text_RunHelp"),TEXT("RunActionSize")})
 {
  auto* W=Run->WidgetTree->FindWidget(Name);if(!W)return false;
  if(W->GetParent()!=RunRoot){W->RemoveFromParent();RunRoot->AddChild(W);}
 }
 Move(Run,TEXT("Text_RunStatus"),FAnchors(.5,0),FMargin(0,64,700,24),FVector2D(.5,0));
 Move(Run,TEXT("Text_RunHelp"),FAnchors(.5,0),FMargin(0,196,760,46),FVector2D(.5,0));
 for(const TCHAR* Name:{TEXT("Text_RunStatus"),TEXT("Text_RunHelp")})if(auto* T=Cast<UTextBlock>(Run->WidgetTree->FindWidget(Name))){T->SetColorAndOpacity(FLinearColor(1,.95f,.8f));T->SetJustification(ETextJustify::Center);auto F=T->GetFont();F.OutlineSettings.OutlineSize=1;F.OutlineSettings.OutlineColor=Edge;T->SetFont(F);T->SetWrapTextAt(740);}
 Move(Run,TEXT("RunActionSize"),FAnchors(1,1),FMargin(-24,-24,280,56),FVector2D(1,1));
 if(auto* S=Cast<USizeBox>(Card->WidgetTree->RootWidget)){S->SetWidthOverride(72);S->SetHeightOverride(108);}
 for(const TCHAR* Name:{TEXT("WBP_SellZone_Left"),TEXT("WBP_SellZone_Right")})
 {auto* BP=Blueprint(Name,UUserWidget::StaticClass(),false);if(!BP)return false;Restyle(BP);if(auto* Icon=BP->WidgetTree->FindWidget(TEXT("SellIcon")))Icon->SetVisibility(ESlateVisibility::Collapsed);Move(BP,TEXT("Text_Sell"),FAnchors(.5,.5),FMargin(0,0,144,32),FVector2D(.5,.5));if(!Save(BP,bPersist))return false;}
 for(auto* BP:{Card,Shop,HUD,Run})if(!Save(BP,bPersist))return false;
 return true;
}
bool EGBuildBoardUIAssets()
{
 Font=LoadObject<UFont>(nullptr,TEXT("/Game/Assets/UI/Fonts/F_UI_CJK"));
 auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
 const FString Destination=TEXT("/Game/Blueprints/UI/Widgets/Board");
 auto Clone=[&](const TCHAR* Source,const TCHAR* Name)->UWidgetBlueprint*
 {
  const FString Path=Destination+TEXT("/")+Name;
  if(Assets->DoesAssetExist(Path))return LoadObject<UWidgetBlueprint>(nullptr,*Path);
  return Cast<UWidgetBlueprint>(Assets->DuplicateAsset(Folder+TEXT("/")+Source,Path));
 };
 auto Rebuild=[&](UWidgetBlueprint* BP)
 {
  if(!BP)return;
  if(BP->WidgetTree)BP->WidgetTree->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
  BP->WidgetTree=NewObject<UWidgetTree>(BP,TEXT("WidgetTree"),RF_Transactional);
 };
 auto* HUD=Clone(TEXT("WBP_MainHUD"),TEXT("WBP_BoardHUD"));
 auto* Shop=Clone(TEXT("WBP_DrawPanel"),TEXT("WBP_Shop"));
 auto* Card=Clone(TEXT("WBP_HandCard"),TEXT("WBP_HandCard"));
 auto* Run=Clone(TEXT("WBP_RunPanel"),TEXT("WBP_RunPanel"));
 auto* SellLeft=Clone(TEXT("WBP_SellZone_Left"),TEXT("WBP_SellLeft"));
 auto* SellRight=Clone(TEXT("WBP_SellZone_Right"),TEXT("WBP_SellRight"));
 if(!HUD||!Shop||!Card||!Run||!SellLeft||!SellRight)return false;
 auto* ShopCard=LoadObject<UWidgetBlueprint>(nullptr,*(Destination+TEXT("/WBP_ShopCard")));
 if(!ShopCard)
 {
  auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=UEGShopCard::StaticClass();
  ShopCard=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateAsset(TEXT("WBP_ShopCard"),Destination,UWidgetBlueprint::StaticClass(),Factory));
 }
 if(!ShopCard)return false;
 for(auto* BP:{HUD,Shop,Card,Run,SellLeft,SellRight,ShopCard})Rebuild(BP);

 // ---------- local authoring helpers ----------
 auto Cv=[&](UWidgetBlueprint* BP,FName Name)->UCanvasPanel*
 {
  auto* W=BP->WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),Name);
  W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);return W;
 };
 auto Bord=[&](UWidgetBlueprint* BP,FName Name,const FSlateBrush& Brush)->UBorder*
 {
  auto* W=BP->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),Name);
  W->SetBrush(Brush);W->SetPadding(FMargin(0));W->SetVisibility(ESlateVisibility::SelfHitTestInvisible);return W;
 };
 auto Solid=[&](UWidgetBlueprint* BP,FName Name,FLinearColor Color)->UBorder*
 {
  auto* W=Bord(BP,Name,FSlateBrush());W->SetBrushColor(Color);return W;
 };
 auto Img=[&](UWidgetBlueprint* BP,FName Name,const FSlateBrush& Brush)->UImage*
 {
  const auto* Resource=Brush.GetResourceObject();
  auto* W=BP->WidgetTree->ConstructWidget<UImage>(Resource&&Resource->GetPathName().Contains(TEXT("icon_currency_mission_coin"))?UEGGoldCoin::StaticClass():UImage::StaticClass(),Name);
  W->SetBrush(Brush);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
 };
 auto Txt=[&](UWidgetBlueprint* BP,FName Name,const FString& Value,int32 Size,FLinearColor Color,ETextJustify::Type Just=ETextJustify::Center,bool bWrap=false)->UTextBlock*
 {
  auto* W=BP->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
  W->SetText(FText::FromString(Value));
  FSlateFontInfo Info=FEGVisualStyle::PearlFont(Size);
  W->SetFont(Info);W->SetColorAndOpacity(FSlateColor(Color));W->SetJustification(Just);
  W->SetAutoWrapText(bWrap);W->SetVisibility(ESlateVisibility::HitTestInvisible);return W;
 };
 auto Btn=[&](UWidgetBlueprint* BP,FName Name,const FButtonStyle& Style)->UButton*
 {
  auto* W=BP->WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),Name);
  W->SetStyle(Style);return W;
 };
 auto WB=[&](UWidgetBlueprint* BP,FName Name,int32 Action,int32 Index,const FButtonStyle& Style,const FString& Label,int32 FontSize)->UEGBoardButton*
 {
  auto* W=BP->WidgetTree->ConstructWidget<UEGBoardButton>(UEGBoardButton::StaticClass(),Name);
  W->Action=Action;W->Index=Index;W->SetStyle(Style);
  if(!Label.IsEmpty())W->SetContent(Txt(BP,FName(*(Name.ToString()+TEXT("Label"))),Label,FontSize,FEGVisualStyle::GoldHi));
  return W;
 };
 auto SetContentFill=[&](UButton* B,UWidget* Content)
 {
  if(!B||!Content)return;B->SetContent(Content);
  if(auto* Slot=Cast<UButtonSlot>(Content->Slot)){Slot->SetPadding(FMargin(0));Slot->SetHorizontalAlignment(HAlign_Fill);Slot->SetVerticalAlignment(VAlign_Fill);}
 };
 auto SetContentCenter=[&](UButton* B,UWidget* Content)
 {
  if(!B||!Content)return;B->SetContent(Content);
  if(auto* Slot=Cast<UButtonSlot>(Content->Slot)){Slot->SetPadding(FMargin(0));Slot->SetHorizontalAlignment(HAlign_Center);Slot->SetVerticalAlignment(VAlign_Center);}
 };
 auto Slot=[&](UCanvasPanel* Canvas,UWidget* W,FAnchors Anchors,FMargin Offsets,FVector2D Align=FVector2D::ZeroVector,int32 Z=0)
 {
  auto* S=Canvas->AddChildToCanvas(W);S->SetAnchors(Anchors);S->SetOffsets(Offsets);S->SetAlignment(Align);S->SetZOrder(Z);return S;
 };

 // ============================================================
 // HUD - Pearl-Gold (B) layout, 1920x1080 design units
 // ============================================================
 auto* Root=Cv(HUD,TEXT("BoardThemeHudRoot"));HUD->WidgetTree->RootWidget=Root;
 {
  auto Glass=[&](UCanvasPanel* Parent,const TCHAR* Name,FAnchors A,FMargin O,FVector2D Align,int32 Z,float Radius=14.f)
  {
   auto* Blur=HUD->WidgetTree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(),Name);
   Blur->SetBlurStrength(12.f);Blur->SetCornerRadius(FVector4(Radius,Radius,Radius,Radius));
   Blur->SetVisibility(ESlateVisibility::HitTestInvisible);Slot(Parent,Blur,A,O,Align,Z);
  };
  Glass(Root,TEXT("RoundGlass"),FAnchors(.5f,0),FMargin(0,16,340,44),FVector2D(.5f,0),29);
  // Corruption has its own small top-left glass dock; it is independent of the shop.
  Glass(Root,TEXT("CorruptionBlur"),FAnchors(0,0),FMargin(28,24,172,44),FVector2D(0,0),29);
  auto* CorruptionDock=Bord(HUD,TEXT("CorruptionDock"),FEGVisualStyle::PearlPanel());
  Slot(Root,CorruptionDock,FAnchors(0,0),FMargin(28,24,172,44),FVector2D(0,0),30);
  auto* CorruptionCanvas=Cv(HUD,TEXT("CorruptionCanvas"));CorruptionDock->SetContent(CorruptionCanvas);
  auto* CorruptionIcon=Img(HUD,TEXT("CorruptionIcon"),FEGVisualStyle::Image(TEXT("/Game/Assets/UI/Glass/Icons/T_UI_CorruptionSeed"),FVector2D(1280,1280)));
  Slot(CorruptionCanvas,CorruptionIcon,FAnchors(0,0),FMargin(10,7,30,30));
  // --- top-center banner: round state and the in-place shop state share one slot ---
  auto* Banner=Bord(HUD,TEXT("BoardThemeBanner"),FEGVisualStyle::PearlBanner());
  Slot(Root,Banner,FAnchors(.5f,0),FMargin(0,EGPearlGold::BannerNormal.Y,EGPearlGold::BannerNormal.W,EGPearlGold::BannerNormal.H),FVector2D(.5f,0),30);
  auto* BC=Cv(HUD,TEXT("BoardThemeBannerCanvas"));Banner->SetContent(BC);
  auto* Round=Txt(HUD,TEXT("Text_Round"),TEXT("第 1 回合 · 准备阶段"),EGPearlGold::BannerTitleSize,EGPearlGold::Plum);
  Slot(BC,Round,FAnchors(.5f,0),FMargin(0,EGPearlGold::BannerTitleTop,320,30),FVector2D(.5f,0));
  auto* Corruption=Txt(HUD,TEXT("Text_Corruption"),TEXT("侵蚀度 0"),EGPearlGold::BannerSecondarySize,EGPearlGold::PlumSoft);
  Slot(CorruptionCanvas,Corruption,FAnchors(0,0),FMargin(46,10,112,26));
  auto* ShopTitle=Txt(HUD,TEXT("Text_ShopTitle"),TEXT("商店"),EGPearlGold::BannerTitleSize,EGPearlGold::Plum);
  ShopTitle->SetVisibility(ESlateVisibility::Collapsed);
  Slot(BC,ShopTitle,FAnchors(.5f,0),FMargin(0,EGPearlGold::BannerTitleTop,320,30),FVector2D(.5f,0));
  auto* ShopProbability=Txt(HUD,TEXT("Text_ShopProbability"),TEXT("全部卡池 · 每名角色等概率"),16,EGPearlGold::PlumSoft);
  ShopProbability->SetAutoWrapText(false);ShopProbability->SetVisibility(ESlateVisibility::Collapsed);
  Slot(BC,ShopProbability,FAnchors(.5f,0),FMargin(0,EGPearlGold::BannerSecondaryTop,390,26),FVector2D(.5f,0));
  auto* BannerHit=Btn(HUD,TEXT("Button_Stage"),FEGVisualStyle::Ghost());
  Slot(Root,BannerHit,FAnchors(.5f,0),FMargin(0,EGPearlGold::BannerNormal.Y,340,EGPearlGold::BannerNormal.H),FVector2D(.5f,0),31);

  // --- top-right entry pills ---
  auto Entry=[&](FName Name,int32 Action,const TCHAR* Caption,const TCHAR* IconName)->UEGBoardButton*
  {
   auto* B=WB(HUD,Name,Action,0,FEGVisualStyle::PearlButton(),TEXT(""),1);
   auto* Row=HUD->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
   auto* IconSize=HUD->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
   IconSize->SetWidthOverride(21);IconSize->SetHeightOverride(21);
   if(FString(IconName).Contains(TEXT("diadem")))
   {
    auto* Star=Img(HUD,NAME_None,FEGVisualStyle::Image(TEXT("/Engine/EngineResources/WhiteSquareTexture"),FVector2D(411,402)));
    Star->SetColorAndOpacity(EGPearlGold::Plum);
    IconSize->SetContent(Star);
   }
   else
   {
    auto* Bars=HUD->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
    const int32 Heights[3]={9,14,19};
    for(int32 BarIndex=0;BarIndex<3;++BarIndex)
    {
     auto* Bar=Img(HUD,NAME_None,FEGVisualStyle::Rounded(EGPearlGold::ChampagneDeep,1.f));
     auto* BarSize=HUD->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
     BarSize->SetWidthOverride(5);BarSize->SetHeightOverride(Heights[BarIndex]);BarSize->SetContent(Bar);
     auto* BarSlot=Bars->AddChildToHorizontalBox(BarSize);
     BarSlot->SetVerticalAlignment(VAlign_Bottom);BarSlot->SetPadding(FMargin(0,0,2,0));
    }
    IconSize->SetContent(Bars);
   }
   auto* IconSlot=Row->AddChildToHorizontalBox(IconSize);
   IconSlot->SetVerticalAlignment(VAlign_Center);IconSlot->SetPadding(FMargin(0,0,7,0));
   auto* Label=Txt(HUD,NAME_None,Caption,18,EGPearlGold::Plum);
   Row->AddChildToHorizontalBox(Label)->SetVerticalAlignment(VAlign_Center);
   SetContentCenter(B,Row);
   return B;
  };
  auto* Statistics=Entry(TEXT("Button_Statistics"),20,TEXT("统计"),TEXT("icon_energy"));
  Slot(Root,Statistics,FAnchors(1,0),FMargin(EGPearlGold::StatisticsButton.X-EGPearlGold::DesignWidth,EGPearlGold::StatisticsButton.Y,EGPearlGold::StatisticsButton.W,EGPearlGold::StatisticsButton.H),FVector2D(0,0),30);

  // --- bottom-center: hand cards float directly over the board ---
  FSlateBrush NoBrush;NoBrush.DrawAs=ESlateBrushDrawType::NoDrawType;
  auto* Tray=Bord(HUD,TEXT("HandPanel"),NoBrush);Tray->SetPadding(FMargin(0));
  Slot(Root,Tray,FAnchors(.5f,1),FMargin(0,-EGPearlGold::HandBottom,EGPearlGold::HandMaxWidth,EGPearlGold::HandPanelHeight),FVector2D(.5f,1),20);
  auto* Viewport=HUD->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("HandViewport"));
  Viewport->SetWidthOverride(EGPearlGold::HandMaxWidth);Viewport->SetHeightOverride(EGPearlGold::HandPanelHeight);Tray->SetContent(Viewport);
  auto* HandScroll=HUD->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("HandScrollBox"));
  HandScroll->SetOrientation(Orient_Horizontal);HandScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
  HandScroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);HandScroll->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
  Viewport->SetContent(HandScroll);
  auto* HandCount=Txt(HUD,TEXT("Text_HandCount"),TEXT(""),12,EGPearlGold::PlumSoft);
  HandCount->SetVisibility(ESlateVisibility::Collapsed);Slot(Root,HandCount,FAnchors(0,0),FMargin(0,0,10,10));
  auto* HandLine=Img(HUD,TEXT("HandGoldRule"),NoBrush);
  Slot(Root,HandLine,FAnchors(.5f,1),FMargin(0,-30,1060,2),FVector2D(.5f,1),19);

  // --- bottom-right: population, battle, income, coin orb ---
  auto ControlSlot=[&](UWidget* W,const EGPearlGold::FLayoutRect& R,int32 Z=32)
  {return Slot(Root,W,FAnchors(1,1),FMargin(R.X-EGPearlGold::DesignWidth,R.Y-EGPearlGold::DesignHeight,R.W,R.H),FVector2D(0,0),Z);};
  auto* PopPill=Bord(HUD,TEXT("BoardThemePopulationPill"),FEGVisualStyle::PearlPill());
  ControlSlot(PopPill,EGPearlGold::Population);
  auto* PopRow=HUD->WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
  PopPill->SetContent(PopRow);
  auto* PopLabel=Txt(HUD,NAME_None,TEXT("上阵"),18,EGPearlGold::PlumSoft,ETextJustify::Left);
  PopRow->AddChildToHorizontalBox(PopLabel)->SetPadding(FMargin(10,0,4,0));
  auto* Population=Txt(HUD,TEXT("Text_Population"),TEXT("0 / 3"),18,EGPearlGold::Plum,ETextJustify::Right);
  auto* PopValue=PopRow->AddChildToHorizontalBox(Population);
  PopValue->SetSize(FSlateChildSize(ESlateSizeRule::Fill));PopValue->SetHorizontalAlignment(HAlign_Right);PopValue->SetPadding(FMargin(0,0,10,0));

  auto* Battle=Btn(HUD,TEXT("Button_Battle"),FEGVisualStyle::GoldOutlineButton());
  ControlSlot(Battle,EGPearlGold::Battle);
  auto* BattleRow=HUD->WidgetTree->ConstructWidget<UHorizontalBox>();
  auto* SwordsSize=HUD->WidgetTree->ConstructWidget<USizeBox>();SwordsSize->SetWidthOverride(24);SwordsSize->SetHeightOverride(24);
  auto* Swords=Img(HUD,TEXT("BattleSwords"),FEGVisualStyle::Image(TEXT("/Game/Assets/UI/Textures/PearlLilac/T_UI_Icon_Swords"),FVector2D(128,128),EGPearlGold::Plum));SwordsSize->SetContent(Swords);
  auto* SwordSlot=BattleRow->AddChildToHorizontalBox(SwordsSize);SwordSlot->SetPadding(FMargin(0,0,8,0));SwordSlot->SetVerticalAlignment(VAlign_Center);
  auto* BattleText=Txt(HUD,TEXT("Text_Battle"),TEXT("开战"),22,EGPearlGold::Plum);
  BattleRow->AddChildToHorizontalBox(BattleText)->SetVerticalAlignment(VAlign_Center);
  SetContentCenter(Battle,BattleRow);

  auto* Income=Txt(HUD,TEXT("Text_RoundIncome"),TEXT("+6"),EGPearlGold::IncomeTextSize,EGPearlGold::Champagne);
  Income->SetShadowColorAndOpacity(EGPearlGold::Plum);Income->SetShadowOffset(FVector2D(1,1));
  ControlSlot(Income,EGPearlGold::Income);

  auto* Orb=Btn(HUD,TEXT("Button_ToggleDrawPanel"),FEGVisualStyle::OrbButton());
  ControlSlot(Orb,EGPearlGold::Orb,33);
  auto* OrbFlowers=Img(HUD,TEXT("ResourceFloralRing"),FEGVisualStyle::PearlAvatarRing());
  auto FloralRect=EGPearlGold::Orb;FloralRect.X-=10;FloralRect.Y-=10;FloralRect.W+=20;FloralRect.H+=20;
  OrbFlowers->SetVisibility(ESlateVisibility::Collapsed);ControlSlot(OrbFlowers,FloralRect,34);
  auto* OrbColumn=HUD->WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
  SetContentCenter(Orb,OrbColumn);
  auto* GoldLabel=Txt(HUD,TEXT("Text_Gold"),TEXT("3"),EGPearlGold::FontGold,EGPearlGold::Plum);
  OrbColumn->AddChildToVerticalBox(GoldLabel)->SetHorizontalAlignment(HAlign_Center);
  auto* CoinRow=HUD->WidgetTree->ConstructWidget<UHorizontalBox>();
  auto* CoinSize=HUD->WidgetTree->ConstructWidget<USizeBox>();CoinSize->SetWidthOverride(18);CoinSize->SetHeightOverride(18);
  CoinSize->SetContent(Img(HUD,TEXT("ResourceCoinIcon"),FEGVisualStyle::Image(TEXT("/Game/Assets/UI/Textures/BoardTheme/Icons/icon_currency_mission_coin"),FVector2D(96,96))));
  auto* CoinIconSlot=CoinRow->AddChildToHorizontalBox(CoinSize);CoinIconSlot->SetPadding(FMargin(0,0,5,0));CoinIconSlot->SetVerticalAlignment(VAlign_Center);
  auto* CoinLabel=Txt(HUD,NAME_None,TEXT("商店"),16,EGPearlGold::PlumSoft);CoinRow->AddChildToHorizontalBox(CoinLabel)->SetVerticalAlignment(VAlign_Center);
  OrbColumn->AddChildToVerticalBox(CoinRow)->SetHorizontalAlignment(HAlign_Center);
  auto* Toggle=Txt(HUD,TEXT("Text_DrawToggle"),TEXT(""),12,EGPearlGold::PlumSoft);
  Toggle->SetVisibility(ESlateVisibility::Collapsed);Slot(Root,Toggle,FAnchors(0,0),FMargin(0,0,10,10));

  // --- shop widget host: full-screen transparent; the top banner is replaced in place ---
  auto* DrawPanel=HUD->WidgetTree->ConstructWidget<UUserWidget>(Shop->GeneratedClass.Get(),TEXT("DrawPanel"));
  DrawPanel->SetVisibility(ESlateVisibility::Collapsed);
  auto* DrawSlot=Root->AddChildToCanvas(DrawPanel);
  DrawSlot->SetAnchors(FAnchors(0,0,1,1));DrawSlot->SetOffsets(FMargin(0));DrawSlot->SetZOrder(26);

  // --- sell zones ---
  auto* Left=HUD->WidgetTree->ConstructWidget<UUserWidget>(SellLeft->GeneratedClass.Get(),TEXT("SellZone_Left"));
  auto* Right=HUD->WidgetTree->ConstructWidget<UUserWidget>(SellRight->GeneratedClass.Get(),TEXT("SellZone_Right"));
  for(auto* W:{Left,Right})W->SetVisibility(ESlateVisibility::Collapsed);
  Slot(Root,Left,FAnchors(0,.5f),FMargin(20,60,150,360),FVector2D(0,.5f),24);
  Slot(Root,Right,FAnchors(1,.5f),FMargin(-20,60,150,360),FVector2D(1,.5f),24);

  // --- hero detail drawer (left, 340 wide) ---
  auto* Detail=Bord(HUD,TEXT("UnitDetailDock"),FEGVisualStyle::PearlPanel());
  auto* DS=Root->AddChildToCanvas(Detail);
  DS->SetAnchors(FAnchors(0,0,0,1));DS->SetOffsets(FMargin(32,116,420,176));DS->SetAlignment(FVector2D(0,0));DS->SetZOrder(28);
  Detail->SetVisibility(ESlateVisibility::Collapsed);
  auto* DC=Cv(HUD,TEXT("UnitDetailCanvas"));Detail->SetContent(DC);Detail->SetPadding(FMargin(0));
  auto* Portrait=Img(HUD,TEXT("UnitDetailPortrait"),FEGVisualStyle::PearlCardFrame());
  Slot(DC,Portrait,FAnchors(0,0),FMargin(18,18,140,210),FVector2D(0,0));
  auto* Heading=Txt(HUD,TEXT("Text_UnitHeading"),TEXT("英雄"),24,EGPearlGold::Plum,ETextJustify::Left,true);
  Slot(DC,Heading,FAnchors(0,0),FMargin(174,20,228,52),FVector2D(0,0));
  auto* Meta=Txt(HUD,TEXT("Text_UnitMeta"),TEXT("Lv.1"),16,EGPearlGold::PlumSoft,ETextJustify::Left,true);
  Slot(DC,Meta,FAnchors(0,0),FMargin(174,82,228,62),FVector2D(0,0));
  auto* Vitals=Txt(HUD,TEXT("Text_UnitVitals"),TEXT("生命 0 / 0"),16,EGPearlGold::PlumSoft,ETextJustify::Left,true);
  Slot(DC,Vitals,FAnchors(0,0),FMargin(174,152,228,64),FVector2D(0,0));
  auto* Divider=Img(HUD,TEXT("BoardThemeDetailDivider"),FEGVisualStyle::GoldLine());
  Slot(DC,Divider,FAnchors(0,0),FMargin(18,236,384,4),FVector2D(0,0));
  auto* HealthBar=HUD->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("BoardDetailHealth"));
  HealthBar->SetWidgetStyle(FEGVisualStyle::Progress());HealthBar->SetFillColorAndOpacity(FLinearColor(.72f,.27f,.32f,1.f));HealthBar->SetPercent(1.f);
  Slot(DC,HealthBar,FAnchors(0,0),FMargin(18,250,384,10),FVector2D(0,0));
  auto* ManaBar=HUD->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("BoardDetailMana"));
  ManaBar->SetWidgetStyle(FEGVisualStyle::Progress());ManaBar->SetFillColorAndOpacity(FLinearColor(.42f,.56f,.78f,1.f));ManaBar->SetPercent(1.f);
  Slot(DC,ManaBar,FAnchors(0,0),FMargin(18,264,384,8),FVector2D(0,0));
  auto* Stats=Txt(HUD,TEXT("Text_UnitStats"),TEXT("攻击 0\n\n防御 0"),17,EGPearlGold::Plum,ETextJustify::Left,true);
  Slot(DC,Stats,FAnchors(0,0),FMargin(18,286,112,170),FVector2D(0,0));
  auto* Scroll=HUD->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("UnitDetailScroll"));
  Scroll->SetConsumeMouseWheel(EConsumeMouseWheel::WhenScrollingPossible);Scroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
  auto* ScrollSlot=DC->AddChildToCanvas(Scroll);ScrollSlot->SetAnchors(FAnchors(0,0,1,1));ScrollSlot->SetOffsets(FMargin(148,286,18,90));ScrollSlot->SetAlignment(FVector2D(0,0));
  auto* Body=Txt(HUD,TEXT("Text_UnitDetail"),TEXT(""),17,EGPearlGold::PlumSoft,ETextJustify::Left,true);
  Body->SetWrapTextAt(250);Scroll->AddChild(Body);
  auto* Close=Btn(HUD,TEXT("Button_CloseHeroDetails"),FEGVisualStyle::Ghost());
  Slot(DC,Close,FAnchors(1,0),FMargin(-14,10,28,28),FVector2D(1,0),5);
  auto* CloseText=Txt(HUD,TEXT("BoardThemeDetailCloseText"),TEXT("×"),24,EGPearlGold::Plum);
  SetContentFill(Close,CloseText);
  auto* Sell=WB(HUD,TEXT("Button_SellInspected"),30,0,FEGVisualStyle::GoldOutlineButton(),TEXT(""),16);
  Slot(DC,Sell,FAnchors(.5f,1),FMargin(0,-56,176,42),FVector2D(.5f,1),5);
  auto* SellText=Txt(HUD,TEXT("Text_SellInspected"),TEXT("出售 +1"),20,EGPearlGold::Plum);
  SetContentCenter(Sell,SellText);

  // --- statistics drawer ---
  auto* StatsDock=Bord(HUD,TEXT("BoardStatistics"),FEGVisualStyle::PearlPanel());
  StatsDock->SetVisibility(ESlateVisibility::Collapsed);
  Slot(Root,StatsDock,FAnchors(1,0),FMargin(-32,88,380,520),FVector2D(1,0),34);

    if(Root)
    {
        auto* Dock=HUD->WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("BoardInfoDock"));Dock->SetBrush(FEGVisualStyle::Rounded(EGPearlGold::Srgb(36,27,56,.80f),12.f,EGPearlGold::GoldEdge,1.f));Dock->SetPadding(FMargin(16,16,16,22));Dock->SetVisibility(ESlateVisibility::Collapsed);
        Dock->SetClipping(EWidgetClipping::ClipToBounds);
        auto* InfoDS=Root->AddChildToCanvas(Dock);InfoDS->SetPosition(FVector2D(32,116));InfoDS->SetSize(FVector2D(EGPearlGold::HoverMaxWidth,680));InfoDS->SetZOrder(60);
        Dock->SetHorizontalAlignment(HAlign_Fill);Dock->SetVerticalAlignment(VAlign_Fill);
        auto* Column=HUD->WidgetTree->ConstructWidget<UVerticalBox>();Dock->SetContent(Column);
        auto* Art=HUD->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BoardInfoArt"));
        Art->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* ArtSize=HUD->WidgetTree->ConstructWidget<USizeBox>();ArtSize->SetWidthOverride(150);ArtSize->SetHeightOverride(225);ArtSize->SetContent(Art);Column->AddChildToVerticalBox(ArtSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Line=HUD->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BoardInfoLine"));Line->SetBrush(FEGVisualStyle::GoldLine());Line->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* LineSize=HUD->WidgetTree->ConstructWidget<USizeBox>();LineSize->SetWidthOverride(272);LineSize->SetHeightOverride(8);LineSize->SetContent(Line);Column->AddChildToVerticalBox(LineSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Title=HUD->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoardInfoTitle"));Title->SetFont(FEGVisualStyle::PearlFont(24));Title->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Title->SetJustification(ETextJustify::Center);Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(4,14,4,8));
        auto* InfoScroll=HUD->WidgetTree->ConstructWidget<UScrollBox>();Column->AddChildToVerticalBox(InfoScroll)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        auto* InfoBody=HUD->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoardInfoBody"));InfoBody->SetFont(FEGVisualStyle::PearlFont(17));InfoBody->SetAutoWrapText(true);InfoBody->SetWrapTextAt(300);InfoBody->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));InfoBody->SetClipping(EWidgetClipping::ClipToBounds);InfoScroll->SetClipping(EWidgetClipping::ClipToBounds);InfoScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);InfoScroll->AddChild(InfoBody);
    }

    if(auto* Dock=Cast<UBorder>(HUD->WidgetTree->FindWidget(TEXT("BoardStatistics"))))
    {
        Dock->SetPadding(FMargin(18));
        auto* Box=HUD->WidgetTree->ConstructWidget<UVerticalBox>();Dock->SetContent(Box);
        auto* Title=HUD->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("BoardStatisticsTitle"));Title->SetText(FText::FromString(TEXT("本场统计")));Title->SetFont(FEGVisualStyle::PearlFont(20));Title->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Box->AddChildToVerticalBox(Title)->SetPadding(FMargin(2,0,2,8));
        auto* Line=HUD->WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("BoardStatisticsLine"));Line->SetBrush(FEGVisualStyle::GoldLine());Line->SetVisibility(ESlateVisibility::HitTestInvisible);
        auto* LineSize=HUD->WidgetTree->ConstructWidget<USizeBox>();LineSize->SetWidthOverride(320);LineSize->SetHeightOverride(8);LineSize->SetContent(Line);Box->AddChildToVerticalBox(LineSize)->SetHorizontalAlignment(HAlign_Center);
        auto* Metrics=HUD->WidgetTree->ConstructWidget<UHorizontalBox>();Box->AddChildToVerticalBox(Metrics)->SetPadding(FMargin(0,8,0,4));
        int32 Metric=0;
        for(const TCHAR* Caption:{TEXT("伤害"),TEXT("承伤"),TEXT("治疗")})
        {
            auto* B=HUD->WidgetTree->ConstructWidget<UEGBoardButton>();B->Action=23;B->Index=Metric++;
            B->SetStyle(FEGVisualStyle::PearlButton());
            auto* L=HUD->WidgetTree->ConstructWidget<UTextBlock>();L->SetFont(FEGVisualStyle::PearlFont(18));L->SetText(FText::FromString(Caption));L->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));B->SetContent(L);
            Metrics->AddChildToHorizontalBox(B)->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
        }
        for(int32 I=0;I<9;++I)
        {
            auto* L=HUD->WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("StatisticsLabel_%d"),I)));L->SetFont(FEGVisualStyle::PearlFont(18));L->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));Box->AddChildToVerticalBox(L)->SetPadding(FMargin(2,7,2,2));
            auto* Bar=HUD->WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),FName(*FString::Printf(TEXT("StatisticsBar_%d"),I)));
            Bar->SetWidgetStyle(FEGVisualStyle::Progress());Bar->SetFillColorAndOpacity(EGPearlGold::Champagne);
            auto* BarHeight=HUD->WidgetTree->ConstructWidget<USizeBox>();BarHeight->SetHeightOverride(8);BarHeight->SetContent(Bar);Box->AddChildToVerticalBox(BarHeight);
        }
    }

  // --- stage preview modal ---
  auto* Preview=Bord(HUD,TEXT("StagePreviewPanel"),FEGVisualStyle::PearlPanel());
  Preview->SetVisibility(ESlateVisibility::Collapsed);
  Slot(Root,Preview,FAnchors(.5f,.5f),FMargin(0,0,920,570),FVector2D(.5f,.5f),36);
  auto* PVC=Cv(HUD,TEXT("StagePreviewCanvas"));Preview->SetContent(PVC);
  auto* PVTitle=Txt(HUD,TEXT("Text_StagePreviewTitle"),TEXT("关卡预览"),24,EGPearlGold::Plum,ETextJustify::Left);
  Slot(PVC,PVTitle,FAnchors(0,0),FMargin(28,18,600,40),FVector2D(0,0));
  auto* PVClose=Btn(HUD,TEXT("Button_CloseStagePreview"),FEGVisualStyle::Ghost());
  Slot(PVC,PVClose,FAnchors(1,0),FMargin(-54,14,40,40),FVector2D(1,0),3);
  auto* PVCloseText=Txt(HUD,TEXT("BoardThemeStageCloseText"),TEXT("×"),24,EGPearlGold::Plum);SetContentFill(PVClose,PVCloseText);
  auto* PVScroll=HUD->WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(),TEXT("StagePreviewScroll"));
  PVScroll->SetOrientation(Orient_Horizontal);PVScroll->SetScrollBarVisibility(ESlateVisibility::Collapsed);
  Slot(PVC,PVScroll,FAnchors(0,0),FMargin(24,72,872,120),FVector2D(0,0));
  auto* PVDetail=Txt(HUD,TEXT("Text_StagePreviewDetail"),TEXT("选择关卡查看效果"),20,EGPearlGold::PlumSoft,ETextJustify::Left,true);
  PVDetail->SetWrapTextAt(860);
  Slot(PVC,PVDetail,FAnchors(0,0),FMargin(28,212,864,320),FVector2D(0,0));
 }

 // ============================================================
 // shop panel widget (cards + controls only; title lives in the HUD banner)
 // ============================================================
 {
  auto* ShopRoot=Cv(Shop,TEXT("ShopRoot"));Shop->WidgetTree->RootWidget=ShopRoot;
  for(int32 I=0;I<4;++I)
  {
   auto* Offer=Cv(Shop,FName(*FString::Printf(TEXT("ShopSlot_%d"),I)));
   const auto Rect=EGPearlGold::ShopCardRect(I);
   Slot(ShopRoot,Offer,FAnchors(.5f,0),FMargin(Rect.X-960,Rect.Y,Rect.W,Rect.H),FVector2D(0,0),3);
  }
  auto* Lock=WB(Shop,TEXT("Button_ShopLock"),12,0,FEGVisualStyle::GoldOutlineButton(),TEXT(""),1);
  const auto LockRect=EGPearlGold::ShopLock;
  Slot(ShopRoot,Lock,FAnchors(.5f,0),FMargin(LockRect.X-960,LockRect.Y,LockRect.W,LockRect.H),FVector2D(0,0),2);
  auto* LockText=Txt(Shop,TEXT("Text_ShopLock"),TEXT("锁定"),20,EGPearlGold::Plum);
  auto* Refresh=WB(Shop,TEXT("Button_RefreshShop"),11,0,FEGVisualStyle::GoldOutlineButton(),TEXT(""),1);
  const auto RefreshRect=EGPearlGold::ShopRefresh;
  Slot(ShopRoot,Refresh,FAnchors(.5f,0),FMargin(RefreshRect.X-960,RefreshRect.Y,RefreshRect.W,RefreshRect.H),FVector2D(0,0),2);
  auto* RefreshText=Txt(Shop,TEXT("BoardThemeRefreshText"),TEXT("刷新"),22,EGPearlGold::Plum);
  auto ShopButtonContent=[&](UEGBoardButton* B,UTextBlock* Caption,const TCHAR* Texture,bool bCost)
  {
   auto* Row=Shop->WidgetTree->ConstructWidget<UHorizontalBox>();
   auto* IconBox=Shop->WidgetTree->ConstructWidget<USizeBox>();IconBox->SetWidthOverride(48);IconBox->SetHeightOverride(48);
   IconBox->SetContent(Img(Shop,NAME_None,FEGVisualStyle::Image(Texture,FVector2D(1254,1254))));
   auto* IconSlot=Row->AddChildToHorizontalBox(IconBox);IconSlot->SetPadding(FMargin(0,0,10,0));IconSlot->SetVerticalAlignment(VAlign_Center);
   Row->AddChildToHorizontalBox(Caption)->SetVerticalAlignment(VAlign_Center);
   if(bCost)
   {
    auto* Coin=Shop->WidgetTree->ConstructWidget<USizeBox>();Coin->SetWidthOverride(24);Coin->SetHeightOverride(24);
    Coin->SetContent(Img(Shop,NAME_None,FEGVisualStyle::Image(TEXT("/Game/Assets/UI/Textures/BoardTheme/Icons/icon_currency_mission_coin"),FVector2D(96,96))));
    auto* CoinSlot=Row->AddChildToHorizontalBox(Coin);CoinSlot->SetPadding(FMargin(8,0,4,0));CoinSlot->SetVerticalAlignment(VAlign_Center);
    Row->AddChildToHorizontalBox(Txt(Shop,TEXT("ShopRefreshCost"),TEXT("1"),24,EGPearlGold::Plum))->SetVerticalAlignment(VAlign_Center);
   }
   SetContentCenter(B,Row);
  };
  ShopButtonContent(Lock,LockText,TEXT("/Game/Assets/UI/PearlReplica/Icons/T_UI_Replica_Lock"),false);
  ShopButtonContent(Refresh,RefreshText,TEXT("/Game/Assets/UI/PearlReplica/Icons/T_UI_Replica_Refresh"),true);
 }

 // ============================================================
 // Layered hand card: full portrait, authentic signet and native name text.
 // ============================================================
 {
  auto* Size=Card->WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("CardSize"));
  Card->WidgetTree->RootWidget=Size;Size->SetWidthOverride(EGPearlGold::HandCardWidth);Size->SetHeightOverride(EGPearlGold::HandCardHeight);
  auto* Layers=Card->WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("CardLayers"));
  Size->SetContent(Layers);
  auto* Face=FEGCardPresentation::Build(Card->WidgetTree);
  auto* FaceSlot=Layers->AddChildToOverlay(Face);FaceSlot->SetHorizontalAlignment(HAlign_Fill);FaceSlot->SetVerticalAlignment(VAlign_Fill);
  auto* Highest=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/Cards/Pardofelis/T_Card_Pardo"));
  FEGCardPresentation::Apply(Card->WidgetTree,Highest);
 }

 // ============================================================
 // sell zone widgets
 // ============================================================
 for(auto* Sell:{SellLeft,SellRight})
 {
  auto* SellRoot=Cv(Sell,TEXT("SellRoot"));Sell->WidgetTree->RootWidget=SellRoot;
  auto* Frame=Bord(Sell,TEXT("ZoneFrame"),FEGVisualStyle::PearlPanel());
  auto* FrameSlot=SellRoot->AddChildToCanvas(Frame);FrameSlot->SetAnchors(FAnchors(0,0,1,1));FrameSlot->SetOffsets(FMargin(0));
  auto* Label=Txt(Sell,TEXT("Text_Sell"),TEXT("出售\n+1"),24,EGPearlGold::Plum);
  Slot(SellRoot,Label,FAnchors(.5f,.5f),FMargin(0,0,144,80),FVector2D(.5f,.5f),2);
  auto* SellIcon=Img(Sell,TEXT("SellIcon"),FEGVisualStyle::Image(TEXT("/Game/Assets/UI/Textures/BoardTheme/Icons/icon_currency_mission_coin"),FVector2D(96,96)));
  SellIcon->SetVisibility(ESlateVisibility::HitTestInvisible);
  Slot(SellRoot,SellIcon,FAnchors(.5f,.5f),FMargin(0,74,58,58),FVector2D(.5f,.5f),2);
 }

 // ============================================================
 // run panel
 // ============================================================
 UEGDemoPanel::BuildBoardLayout(Run->WidgetTree);

 // Glass blur is authored in the Widget Blueprint behind every interactive surface.
 for(auto* BP:{HUD,Shop,Run,SellLeft,SellRight})
 {
  TArray<UWidget*> Surfaces;
  BP->WidgetTree->ForEachWidget([&](UWidget* W){if(Cast<UCanvasPanelSlot>(W->Slot)&&(Cast<UButton>(W)||Cast<UBorder>(W)))Surfaces.Add(W);});
  for(auto* W:Surfaces)
  {
   const FString N=W->GetName();
   if(N==TEXT("HandPanel")||N==TEXT("BoardFinalResult")||N==TEXT("Button_Stage"))continue;
   auto* Original=Cast<UCanvasPanelSlot>(W->Slot);auto* Canvas=Cast<UCanvasPanel>(W->GetParent());if(!Original||!Canvas)continue;
   if(Cast<UButton>(W))
   {
    const auto Layout=Original->GetLayout();const int32 Z=Original->GetZOrder();
    auto* Blur=BP->WidgetTree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(),FName(*(N+TEXT("GlassBlur"))));
    Blur->SetBlurStrength(10.f);const float Radius=N==TEXT("Button_ToggleDrawPanel")?62.f:14.f;
    Blur->SetCornerRadius(FVector4(Radius,Radius,Radius,Radius));Blur->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* BlurSlot=Canvas->AddChildToCanvas(Blur);BlurSlot->SetLayout(Layout);BlurSlot->SetZOrder(Z-1);
   }
   // Put blur inside a border so it follows the border's hidden/open state.
   if(auto* Border=Cast<UBorder>(W))
   {
    if(auto* Content=Border->GetContent())
    {
     Content->RemoveFromParent();auto* Blur=BP->WidgetTree->ConstructWidget<UBackgroundBlur>(UBackgroundBlur::StaticClass(),FName(*(N+TEXT("GlassBlur"))));
     Blur->SetBlurStrength(10.f);Blur->SetCornerRadius(FVector4(12,12,12,12));Border->SetContent(Blur);Blur->SetContent(Content);
    }
   }
  }
 }

 // ============================================================
 // save + defaults
 // ============================================================
 auto* Highest=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/Cards/Pardofelis/T_Card_Pardo"));
 if(auto* Defaults=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject()))
 {
  Defaults->bBoardPresentation=true;
  Defaults->CardWidgetClass=Card->GeneratedClass.Get();
  Defaults->CardArtwork=Highest;
 }
 UEGShopCard::BuildLayout(ShopCard->WidgetTree);
 bool bSaved=true;
 // A card compile can also compile its dependent HUD. Register the entire rebuilt
 // asset set before starting any compile/reinstance work.
 for(auto* BP:{Card,ShopCard,Shop,SellLeft,SellRight,Run,HUD})RegisterWidgetGuids(BP);
 for(auto* BP:{Card,ShopCard,Shop,SellLeft,SellRight,Run,HUD})bSaved=Save(BP,true)&&bSaved;
 if(!bSaved)return false;
 auto* HUDDefaults=Cast<UEGMainHUD>(HUD->GeneratedClass->GetDefaultObject());if(!HUDDefaults)return false;
 HUDDefaults->CharacterCardClasses.Reset();HUDDefaults->ShopCardClasses.Reset();
 const TCHAR* Keys[]={TEXT("Pardo")};
 for(const TCHAR* Key:Keys)
 {
  auto* Portrait=LoadObject<UTexture2D>(nullptr,*(FString(TEXT("/Game/Assets/Cards/Pardofelis/T_Card_"))+Key));
  for(bool bShop:{false,true})
  {
   auto* Parent=bShop?ShopCard:Card;
   const FString ChildName=FString(bShop?TEXT("WBP_ShopCard_"):TEXT("WBP_Card_"))+Key;
   const FString ChildFolder=Destination+TEXT("/Cards");
   auto* Child=LoadObject<UWidgetBlueprint>(nullptr,*(ChildFolder+TEXT("/")+ChildName));
   if(!Child){auto* Factory=NewObject<UWidgetBlueprintFactory>();Factory->ParentClass=Parent->GeneratedClass;
    Child=Cast<UWidgetBlueprint>(FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().CreateAsset(ChildName,ChildFolder,UWidgetBlueprint::StaticClass(),Factory));}
   if(!Child||Child->ParentClass!=Parent->GeneratedClass)return false;
   // No copied Designer tree: layout is inherited from the shared template.
   if(!Save(Child,false))return false;
   if(bShop){auto* CDO=Cast<UEGShopCard>(Child->GeneratedClass->GetDefaultObject());CDO->bAuthoredAppearance=true;CDO->Appearance=FEGCardPresentation::Appearance(Portrait,true);HUDDefaults->ShopCardClasses.Add(Key,Child->GeneratedClass.Get());}
   else{auto* CDO=Cast<UEGHandCard>(Child->GeneratedClass->GetDefaultObject());CDO->bAuthoredAppearance=true;CDO->Appearance=FEGCardPresentation::Appearance(Portrait);HUDDefaults->CharacterCardClasses.Add(Key,Child->GeneratedClass.Get());}
   Child->MarkPackageDirty();if(!Assets->SaveLoadedAsset(Child,false))return false;
  }
 }
 HUD->MarkPackageDirty();return Assets->SaveLoadedAsset(HUD,false);
}

#endif

