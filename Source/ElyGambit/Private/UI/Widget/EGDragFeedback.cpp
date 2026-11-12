#include "UI/Widget/EGDragFeedback.h"
#include "UI/EGVisualStyle.h"
#include "UI/EGCardPresentation.h"
#include "UI/EGPearlGoldLayout.h"
#include "UI/Widget/EGRichText.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

namespace
{
// White-brush triangles keep the overlay crisp at any viewport/DPI scale.
void FillDragPolygon(FSlateWindowElementList& Out,int32 Layer,const FGeometry& Geometry,
    const TArray<FVector2D>& Points,const FLinearColor& Color)
{
    if(Points.Num()<3||!FSlateApplication::IsInitialized())return;
    TArray<FSlateVertex> Vertices;
    TArray<SlateIndex> Indices;
    const FColor Tint=Color.ToFColor(true);
    for(const FVector2D& Point:Points)
        Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(),
            FVector2f(Point),FVector2f(.5f,.5f),Tint));
    for(int32 Index=1;Index+1<Points.Num();++Index)
    {
        Indices.Add(0);Indices.Add(static_cast<SlateIndex>(Index));Indices.Add(static_cast<SlateIndex>(Index+1));
    }
    const auto Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")));
    FSlateDrawElement::MakeCustomVerts(Out,Layer,Resource,Vertices,Indices,nullptr,0,0);
}
}

void UEGDragFeedback::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    ForceVolatile(true);
    auto* Root = WidgetTree->ConstructWidget<UCanvasPanel>();
    WidgetTree->RootWidget = Root;
    auto* Font = LoadObject<UFont>(nullptr, TEXT("/Game/Assets/UI/Fonts/F_UI_CJK"));

    InfoPanel = WidgetTree->ConstructWidget<UBorder>();
    InfoPanel->SetBrush(FEGVisualStyle::PearlPanel());
    InfoPanel->SetPadding(FMargin(16));
    InfoPanel->SetClipping(EWidgetClipping::ClipToBounds);
    auto* PanelSlot = Root->AddChildToCanvas(InfoPanel);
    PanelSlot->SetPosition(FVector2D(172, 124));
    PanelSlot->SetSize(FVector2D(336, 548));
    auto* Column = WidgetTree->ConstructWidget<UVerticalBox>();
    InfoPanel->SetContent(Column);

    Portrait = WidgetTree->ConstructWidget<UImage>();
    Portrait->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* ArtSize = WidgetTree->ConstructWidget<USizeBox>();
    ArtSize->SetWidthOverride(156);
    ArtSize->SetHeightOverride(218.4f);
    ArtSize->SetContent(Portrait);
    Column->AddChildToVerticalBox(ArtSize)->SetHorizontalAlignment(HAlign_Center);

    auto* Line = WidgetTree->ConstructWidget<UImage>();
    Line->SetBrush(FEGVisualStyle::GoldLine());
    Line->SetVisibility(ESlateVisibility::HitTestInvisible);
    auto* LineSize = WidgetTree->ConstructWidget<USizeBox>();
    LineSize->SetWidthOverride(250); LineSize->SetHeightOverride(8);
    LineSize->SetContent(Line);
    Column->AddChildToVerticalBox(LineSize)->SetHorizontalAlignment(HAlign_Center);

    Heading = WidgetTree->ConstructWidget<UTextBlock>();
    Heading->SetFont(FEGVisualStyle::SerifFont(22));
    Heading->SetColorAndOpacity(FSlateColor(EGPearlGold::Plum));
    Heading->SetJustification(ETextJustify::Center);
    Column->AddChildToVerticalBox(Heading)->SetPadding(FMargin(0, 10, 0, 6));
    Description = WidgetTree->ConstructWidget<UEGRichTextBlock>(UEGRichTextBlock::StaticClass());
    Description->ApplyTextStyle(FEGVisualStyle::BodyFont(18),EGPearlGold::PlumSoft);
    Description->SetAutoWrapText(true);
    Description->SetWrapTextAt(300);
    Description->SetJustification(ETextJustify::Left);
    Column->AddChildToVerticalBox(Description);

    DragCard = FEGCardPresentation::Build(WidgetTree);
    DragCard->SetVisibility(ESlateVisibility::Hidden);
    auto* CardSlot = Root->AddChildToCanvas(DragCard);
    CardSlot->SetAnchors(FAnchors(0, 0));
    CardSlot->SetSize(FVector2D(EGPearlGold::HandCardWidth, EGPearlGold::HandCardHeight));
    CardSlot->SetZOrder(20);

    Hint = WidgetTree->ConstructWidget<UTextBlock>();
    FSlateFontInfo HintFont = FEGVisualStyle::TitleFont(17);
    Hint->SetFont(HintFont);
    Hint->SetColorAndOpacity(FSlateColor(EGPearlGold::ChampagneDeep));
    Hint->SetJustification(ETextJustify::Center);
    auto* HintSlot = Root->AddChildToCanvas(Hint);
    HintSlot->SetAnchors(FAnchors(.5f, 0));
    HintSlot->SetAlignment(FVector2D(.5f, 0));
    HintSlot->SetPosition(FVector2D(0, 500));
    HintSlot->SetSize(FVector2D(850, 44));
}

void UEGDragFeedback::Present(UEGHeroDefinition* Definition, UTexture2D* Artwork,
    FVector2D Origin, FVector2D Pointer, bool bAllowed, bool bShowCardAndArrow)
{
    if (!InfoPanel) return;
    if (ShownDefinition.Get() != Definition || ShownArtwork.Get() != Artwork)
    {
        ShownDefinition = Definition;
        ShownArtwork = Artwork;
        Portrait->SetBrush(FEGCardPresentation::PortraitBrush(Artwork,FVector2D(156,218.4f)));
        FEGCardPresentation::Apply(WidgetTree,Artwork);
        Heading->SetText(Definition ? Definition->DisplayName : FText::GetEmpty());
        FString Brief = Definition ? (Definition->bConfirmedSkills?Definition->GetPassiveDescription():Definition->GetCardSkillDescription()).ToString() : FString();
        if (Brief.Len() > 100) Brief = Brief.Left(99) + TEXT("…");
        Description->SetHighlightedText(FText::FromString(Brief));
    }
    if (DragCard)
    {
        DragCard->SetVisibility(bShowCardAndArrow && Artwork ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    }
    InfoPanel->SetVisibility(bShowCardAndArrow && Definition ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
    // Retain local points: offscreen Slate capture has a different absolute origin.
    ArrowOrigin = GetCachedGeometry().AbsoluteToLocal(Origin);
    ArrowPointer = GetCachedGeometry().AbsoluteToLocal(Pointer);
    bDrawArrow = bShowCardAndArrow && !Origin.ContainsNaN() && !Pointer.ContainsNaN();
    bPlacementAllowed = bAllowed;
}

void UEGDragFeedback::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
    Super::NativeTick(Geometry, DeltaSeconds);
    if (!DragCard || DragCard->GetVisibility() == ESlateVisibility::Collapsed) return;
    if (auto* CardSlotRef = Cast<UCanvasPanelSlot>(DragCard->Slot))
    {
        CardSlotRef->SetPosition(ArrowPointer - FVector2D(75.f, 132.f));
    }
}

void UEGDragFeedback::SetHint(const FText& Text)
{
    if (Hint && !Hint->GetText().EqualTo(Text)) Hint->SetText(Text);
}

void UEGDragFeedback::PresentBoardMove(const TArray<FVector2D>& CellCorners,FVector2D Origin,
    FVector2D Pointer,bool bAllowed,bool bShowArrow)
{
    // Keep the moving piece and arrow readable; the existing HUD dock still
    // carries rule/sale feedback without a second instruction across the board.
    if(Hint)Hint->SetVisibility(ESlateVisibility::Collapsed);
    LegalCellCorners.Reset(CellCorners.Num());
    const FGeometry& Geometry=GetCachedGeometry();
    for(const FVector2D& Corner:CellCorners)LegalCellCorners.Add(Geometry.AbsoluteToLocal(Corner));
    ArrowOrigin=Geometry.AbsoluteToLocal(Origin);
    ArrowPointer=Geometry.AbsoluteToLocal(Pointer);
    bPlacementAllowed=bAllowed;
    bDrawArrow=bShowArrow&&!Origin.ContainsNaN()&&!Pointer.ContainsNaN();
}

void UEGDragFeedback::ClearBoardMove()
{
    LegalCellCorners.Reset();
    bDrawArrow=false;
    if(Hint)Hint->SetVisibility(ESlateVisibility::HitTestInvisible);
}

int32 UEGDragFeedback::NativePaint(const FPaintArgs& Args, const FGeometry& G,
    const FSlateRect& Clip, FSlateWindowElementList& Out, int32 Layer,
    const FWidgetStyle& Style, bool bEnabled) const
{
    int32 Result = Super::NativePaint(Args, G, Clip, Out, Layer, Style, bEnabled);
    const FLinearColor Tint=Style.GetColorAndOpacityTint();
    const int32 CellFillLayer=++Result;
    const int32 CellEdgeLayer=++Result;
    for(int32 Index=0;Index+3<LegalCellCorners.Num();Index+=4)
    {
        TArray<FVector2D> Corners{LegalCellCorners[Index],LegalCellCorners[Index+1],
            LegalCellCorners[Index+2],LegalCellCorners[Index+3]};
        FillDragPolygon(Out,CellFillLayer,G,Corners,FLinearColor(.35f,.9f,1.f,.085f)*Tint);
        const FVector2D FirstCorner=Corners[0];
        Corners.Add(FirstCorner);
        FSlateDrawElement::MakeLines(Out,CellEdgeLayer,G.ToPaintGeometry(),Corners,
            ESlateDrawEffect::None,FLinearColor(.6f,.95f,1.f,.65f)*Tint,true,1.4f);
    }
    if (!bDrawArrow) return Result;
    const FVector2D Start = ArrowOrigin;
    const FVector2D End = ArrowPointer;
    const double Distance = FVector2D::Distance(Start, End);
    if (Distance < 40) return Result;
    const FVector2D Delta = End - Start;
    const FVector2D Normal = FVector2D(-Delta.Y, Delta.X).GetSafeNormal();
    const FVector2D Bend = Normal * FMath::Clamp(Distance * .12, 8., 65.);
    const FVector2D C1 = Start + Delta * .28 + Bend;
    const FVector2D C2 = Start + Delta * .72 + Bend;
    const FLinearColor Color = (bPlacementAllowed ? FLinearColor(.65f, .96f, 1.f) : FLinearColor(1.f, .35f, .46f)) * Tint;
    FLinearColor Glow = Color;
    Glow.A *= .16f;
    FSlateDrawElement::MakeCubicBezierSpline(Out, ++Result, G.ToPaintGeometry(), Start, C1, C2, End, 18.f, ESlateDrawEffect::None, Glow);
    const FVector2D Direction = (End - C2).GetSafeNormal();
    const FVector2D Side(-Direction.Y, Direction.X);
    const FVector2D Neck=End-Direction*20.;
    auto Curve=[&](double T)
    {
        const double U=1.-T;
        return Start*(U*U*U)+C1*(3.*U*U*T)+(C2-Direction*20.)*(3.*U*T*T)+Neck*(T*T*T);
    };
    const int32 RibbonLayer=++Result;
    // A narrow tail widens toward a solid arrowhead, rather than a thin V-shaped line.
    for(int32 Segment=0;Segment<24;++Segment)
    {
        const double T0=Segment/24.,T1=(Segment+1)/24.;
        const FVector2D A=Curve(T0),B=Curve(T1);
        const FVector2D Tangent=(B-A).GetSafeNormal();
        const FVector2D Across(-Tangent.Y,Tangent.X);
        const double W0=2.+T0*4.,W1=2.+T1*4.;
        FillDragPolygon(Out,RibbonLayer,G,{A-Across*W0,B-Across*W1,B+Across*W1,A+Across*W0},Color);
    }
    FillDragPolygon(Out,++Result,G,{Neck+Side*14.,End,Neck-Side*14.},Color);
    TArray<FVector2D> OriginRing;
    for(int32 Index=0;Index<=24;++Index)
    {
        const double Angle=Index*2.*PI/24.;
        OriginRing.Add(Start+FVector2D(FMath::Cos(Angle),FMath::Sin(Angle))*7.);
    }
    FSlateDrawElement::MakeLines(Out,++Result,G.ToPaintGeometry(),OriginRing,ESlateDrawEffect::None,Color,true,2.f);
    return Result;
}
