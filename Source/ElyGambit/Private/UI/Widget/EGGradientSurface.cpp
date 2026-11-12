#include "UI/Widget/EGGradientSurface.h"
#include "Components/PanelSlot.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Rendering/DrawElements.h"

class SEGGradientSurface : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SEGGradientSurface){} SLATE_END_ARGS()
 void Construct(const FArguments&) { ChildSlot[SNullWidget::NullWidget]; }
 void SetChild(TSharedRef<SWidget> Child) { ChildSlot[Child]; }
 FLinearColor Top,Bottom;
 float Radius=0.f,Reveal=1.f,Compact=0.f;
 bool bHorizontal=false;
 bool bSmoothFade=false;
 float SolidFraction=0.f;
 FVector2D CompactSize=FVector2D::ZeroVector;
 virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const override
 {
  const FVector2D Size=Geometry.GetLocalSize();
  // Both channels are already folded into Top/Bottom and Compact by the HUD, so
  // this paints a single gradient quad. No clip, no skew, no parent/child scale.
  FVector2D PaintSize=Size;
  if(Compact>0.f&&CompactSize.X>0.f&&CompactSize.Y>0.f)
   PaintSize=FVector2D(FMath::Lerp(Size.X,CompactSize.X,double(Compact)),FMath::Lerp(Size.Y,CompactSize.Y,double(Compact)));
  // Keep the black top flush with the screen edge; only the width is centred.
  const FVector2D PaintOrigin((Size.X-PaintSize.X)*.5f,0.0);
  const FLinearColor Tint=Style.GetColorAndOpacityTint();
  TArray<FSlateGradientStop> Stops;
  Stops.Emplace(FVector2D(0,0),Top*Tint);
  const FVector2D End=bHorizontal?FVector2D(PaintSize.X,0):FVector2D(0,PaintSize.Y);
  if(SolidFraction>0.f)Stops.Emplace(End*FMath::Clamp(SolidFraction,0.f,.95f),Top*Tint);
  if(bSmoothFade)
  {
   const float Start=FMath::Clamp(SolidFraction,0.f,.95f);
   for(int32 I=1;I<=8;++I)
   {
    const float T=float(I)/8.f;const float Smooth=T*T*(3.f-2.f*T);
    Stops.Emplace(End*(Start+(1.f-Start)*T),FMath::Lerp(Top,Bottom,Smooth)*Tint);
   }
  }
  else Stops.Emplace(End,Bottom*Tint);
  // Slate names the orientation of each stop: horizontal stops blend top to bottom.
  FSlateDrawElement::MakeGradient(Out,Layer,Geometry.ToPaintGeometry(PaintSize,FSlateLayoutTransform(PaintOrigin)),MoveTemp(Stops),bHorizontal?Orient_Vertical:Orient_Horizontal,ESlateDrawEffect::None,FVector4f(Radius,Radius,Radius,Radius));
  const int32 Result=SCompoundWidget::OnPaint(Args,Geometry,Cull,Out,Layer+1,Style,bEnabled);
  return Result;
 }
};

TSharedRef<SWidget> UEGGradientSurface::RebuildWidget()
{
 SAssignNew(Surface,SEGGradientSurface);
 if(GetContent())Surface->SetChild(GetContent()->TakeWidget());
 return Surface.ToSharedRef();
}
void UEGGradientSurface::SynchronizeProperties()
{
 Super::SynchronizeProperties();
 if(Surface){Surface->Top=TopColor;Surface->Bottom=BottomColor;Surface->Radius=CornerRadius;Surface->Reveal=Reveal;Surface->bHorizontal=bHorizontalGradient;Surface->SolidFraction=SolidFraction;Surface->bSmoothFade=bSmoothFade;
  Surface->CompactSize=CompactSize;Surface->Invalidate(EInvalidateWidgetReason::Paint);}
}
void UEGGradientSurface::SetReveal(float Value)
{Reveal=FMath::Clamp(Value,0.f,1.f);if(Surface){Surface->Reveal=Reveal;Surface->Invalidate(EInvalidateWidgetReason::Paint);}}
void UEGGradientSurface::SetCompact(float Value)
{if(Surface){Surface->Compact=FMath::Clamp(Value,0.f,1.f);Surface->Invalidate(EInvalidateWidgetReason::Paint);}}
void UEGGradientSurface::SetCompactGeometry(FVector2D Size)
{
 const FVector2D Safe(FMath::Max(0.0,Size.X),FMath::Max(0.0,Size.Y));
 if(!CompactSize.Equals(Safe,.01)){CompactSize=Safe;if(Surface){Surface->CompactSize=CompactSize;Surface->Invalidate(EInvalidateWidgetReason::Paint);}}
}
void UEGGradientSurface::SetGradient(FLinearColor Top,FLinearColor Bottom)
{if(TopColor!=Top||BottomColor!=Bottom){TopColor=Top;BottomColor=Bottom;if(Surface){Surface->Top=Top;Surface->Bottom=Bottom;Surface->Invalidate(EInvalidateWidgetReason::Paint);}}}
void UEGGradientSurface::SetSolidFraction(float Value)
{
 const float Safe=FMath::Clamp(Value,0.f,.95f);
 if(!FMath::IsNearlyEqual(SolidFraction,Safe,.0001f))
 {SolidFraction=Safe;if(Surface){Surface->SolidFraction=Safe;Surface->Invalidate(EInvalidateWidgetReason::Paint);}}
}
void UEGGradientSurface::OnSlotAdded(UPanelSlot* InSlot)
{if(Surface&&InSlot->Content)Surface->SetChild(InSlot->Content->TakeWidget());}
void UEGGradientSurface::OnSlotRemoved(UPanelSlot*)
{if(Surface)Surface->SetChild(SNullWidget::NullWidget);}
void UEGGradientSurface::ReleaseSlateResources(bool bReleaseChildren)
{Super::ReleaseSlateResources(bReleaseChildren);Surface.Reset();}
