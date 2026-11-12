#include "UI/Widget/EGVitalsGraphics.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Brushes/SlateRoundedBoxBrush.h"

namespace
{
constexpr float VitPI = 3.14159265358979323846f;
constexpr float InvSqrt2 = 0.70710678118654752440f;

FLinearColor WithAlpha(const FLinearColor& C, float A)
{
	return FLinearColor(C.R, C.G, C.B, C.A * A);
}

FLinearColor Shade(const FLinearColor& C, float K)
{
	return FLinearColor(C.R * K, C.G * K, C.B * K, C.A);
}

/**
 * Filled capsule (rounded box) in local space.
 *
 * UE 5.8 draws ESlateBrushDrawType::RoundedBox fill from the draw element tint,
 * not from the brush tint, so the wanted colour must be passed as the element
 * tint while the brush itself stays white.
 */
void Capsule(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G,
	const FVector2D& Position, const FVector2D& Size, float Radius,
	const FLinearColor& Color, const FLinearColor& Tint)
{
	if (Size.X <= 0.f || Size.Y <= 0.f) return;
	const float R = FMath::Clamp(Radius, 0.f, FMath::Min(Size.X, Size.Y) * .5f);
	const FSlateRoundedBoxBrush Brush(FLinearColor::White, R);
	FSlateDrawElement::MakeBox(Out, Layer, G.ToPaintGeometry(Size, FSlateLayoutTransform(Position)), &Brush,
		ESlateDrawEffect::None, Color * Tint);
}

void Line(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G,
	const FVector2D& A, const FVector2D& B, float Thickness, const FLinearColor& Color, const FLinearColor& Tint)
{
	TArray<FVector2D> Points;
	Points.Reserve(2);
	Points.Add(A);
	Points.Add(B);
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color * Tint, true, Thickness);
}

/** Circle as a capsule whose radius is half its size. */
void Circle(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G,
	const FVector2D& Centre, float Radius, const FLinearColor& Color, const FLinearColor& Tint)
{
	Capsule(Out, Layer, G, Centre - FVector2D(Radius), FVector2D(Radius * 2.f), Radius, Color, Tint);
}

/** Arc in canvas space (angle 0 = 3 o'clock, clockwise on screen). */
void Arc(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G,
	const FVector2D& Centre, float Radius, float Start, float End, float Thickness,
	const FLinearColor& Color, const FLinearColor& Tint, bool bClosed = false)
{
	if (End <= Start || Radius <= 0.f) return;
	TArray<FVector2D> Points;
	const int32 N = FMath::Max(3, FMath::CeilToInt((End - Start) * Radius / 2.5f));
	Points.Reserve(N + 1);
	for (int32 I = 0; I <= N; ++I)
	{
		float A = FMath::Lerp(Start, End, float(I) / float(N));
		if (!bClosed) A -= VitPI * .5f;
		Points.Add(Centre + FVector2D(FMath::Cos(A), FMath::Sin(A)) * Radius);
	}
	FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color * Tint, true, Thickness);
}

/** 45-degree rotated rounded square = WXQ diamond badge face. */
void Diamond(FSlateWindowElementList& Out, int32 Layer, const FGeometry& G,
	const FVector2D& Centre, float HalfDiagonal, float CornerRadius,
	const FLinearColor& Color, const FLinearColor& Tint)
{
	if (HalfDiagonal <= 0.5f) return;
	const float Side = HalfDiagonal * 2.f * InvSqrt2;
	const FVector2D Offset = Centre - FVector2D(Side * .5f);
	const FSlateRoundedBoxBrush Brush(FLinearColor::White, FMath::Clamp(CornerRadius, 0.f, Side * .25f));
	FSlateDrawElement::MakeRotatedBox(Out, Layer, G.ToPaintGeometry(FVector2D(Side), FSlateLayoutTransform(Offset)),
		&Brush, ESlateDrawEffect::None, VitPI * .25f, UE::Slate::FDeprecateOptionalVector2DParameter(),
		FSlateDrawElement::RelativeToElement, Color * Tint);
}

/**
 * One WXQ vitals capsule: ink frame, champagne hairline, navy track,
 * glossy camp-coloured fill, shield overlay and segment separators.
 * Insets are proportional so the same routine serves 14px and 7px bars.
 */
void PaintVitalsCapsule(const UEGVitalsBar& W, FSlateWindowElementList& Out, int32& Layer,
	const FGeometry& G, const FWidgetStyle& PaintStyle)
{
	const FVector2D Size = G.GetLocalSize();
	if (Size.X < 3.f || Size.Y < 2.f) return;
	const FLinearColor Tint = PaintStyle.GetColorAndOpacityTint();
	const float H = Size.Y;
	const float R = H * .5f;
	const float Fraction = FMath::Clamp(W.GetPercent(), 0.f, 1.f);
	// Low health keeps the camp fill colour (camp identification) and swaps the
	// metal frame + halo to the WXQ danger orange instead.
	const bool bLow = W.bDangerGlow && Fraction <= .28f;
	const FLinearColor Fill = W.GetFillColorAndOpacity();

	// Soft drop shadow keeps the tag readable over the bright board.
	Capsule(Out, Layer, G, FVector2D(-.4f, 1.1f), FVector2D(Size.X + .8f, Size.Y), R + .4f,
		FLinearColor(0.f, 0.f, 0.f, .26f), Tint);
	++Layer;

	// Warm danger halo when the bar is nearly empty.
	if (bLow && Fraction > 0.f)
	{
		Capsule(Out, Layer, G, FVector2D(-1.2f, -1.2f), FVector2D(Size.X + 2.4f, Size.Y + 2.4f), R + 1.2f,
			FLinearColor(.98f, .28f, .20f, .30f), Tint);
		++Layer;
	}

	// Ink frame.
	Capsule(Out, Layer, G, FVector2D::ZeroVector, Size, R, WithAlpha(W.FrameColor, .96f), Tint);
	++Layer;

	// Metal hairline rim: champagne normally, hot orange under 28% health.
	const FLinearColor RimColor = bLow
		? FMath::Lerp(W.RimColor, FLinearColor::FromSRGBColor(FColor(255, 146, 112)), .85f)
		: W.RimColor;
	const float RimIn = FMath::Max(.8f, FMath::Min(1.3f, H * .095f));
	const float RimBand = FMath::Max(.95f, H * .085f);
	Capsule(Out, Layer, G, FVector2D(RimIn), FVector2D(Size.X - RimIn * 2.f, Size.Y - RimIn * 2.f),
		FMath::Max(0.f, R - RimIn), WithAlpha(RimColor, .95f), Tint);
	++Layer;
	Line(Out, Layer, G, FVector2D(RimIn + R * .9f, RimIn + RimBand * .52f),
		FVector2D(Size.X - RimIn - R * .9f, RimIn + RimBand * .52f), FMath::Max(.8f, H * .075f),
		FLinearColor(1.f, .98f, .92f, .42f), Tint);
	++Layer;
	Line(Out, Layer, G, FVector2D(RimIn + R * .9f, Size.Y - RimIn - RimBand * .60f),
		FVector2D(Size.X - RimIn - R * .9f, Size.Y - RimIn - RimBand * .60f), FMath::Max(.8f, H * .07f),
		WithAlpha(RimColor, .35f), Tint);
	++Layer;

	// Deep navy track.
	const float TrackIn = RimIn + RimBand;
	const FVector2D TrackPos(TrackIn, TrackIn);
	const FVector2D TrackSize(FMath::Max(0.f, Size.X - TrackIn * 2.f), FMath::Max(0.f, Size.Y - TrackIn * 2.f));
	if (TrackSize.X <= 0.f || TrackSize.Y <= 0.f) return;
	const float TrackR = FMath::Min(TrackSize.X, TrackSize.Y) * .5f;
	Capsule(Out, Layer, G, TrackPos, TrackSize, TrackR, W.TrackColor, Tint);
	++Layer;
	// Inner top shadow and a faint lower sheen give the track depth without textures.
	Line(Out, Layer, G, FVector2D(TrackPos.X + TrackR * .8f, TrackPos.Y + .55f),
		FVector2D(TrackPos.X + TrackSize.X - TrackR * .8f, TrackPos.Y + .55f), .9f,
		FLinearColor(0.f, 0.f, 0.f, .55f), Tint);
	++Layer;
	Line(Out, Layer, G, FVector2D(TrackPos.X + TrackR * .8f, TrackPos.Y + TrackSize.Y - .7f),
		FVector2D(TrackPos.X + TrackSize.X - TrackR * .8f, TrackPos.Y + TrackSize.Y - .7f), .8f,
		WithAlpha(W.TrackTopColor, .35f), Tint);
	++Layer;

	if (Fraction > 0.f)
	{
		const FVector2D FillPos(TrackPos.X + .5f, TrackPos.Y + .5f);
		const FVector2D FillSize(FMath::Max(0.f, TrackSize.X - 1.f), FMath::Max(0.f, TrackSize.Y - 1.f));
		const float FillRadius = FMath::Min(FillSize.X, FillSize.Y) * .5f;
		const float FillWidth = FMath::Max(FillSize.Y * 1.2f, FillSize.X * Fraction);
		const float EffectiveWidth = FMath::Min(FillSize.X, FillWidth);
		const float CapR = FMath::Min(FillRadius, EffectiveWidth * .5f);

		// Solid camp colour base: painted metal, not glass.
		Capsule(Out, Layer, G, FillPos, FVector2D(EffectiveWidth, FillSize.Y), CapR, Fill, Tint);
		++Layer;

		// WXQ vertical ramp: bright top band + darker bottom band.
		const FVector2D GlassSize(FMath::Max(0.f, EffectiveWidth - 1.f), FMath::Max(0.f, FillSize.Y * .50f));
		Capsule(Out, Layer, G, FVector2D(FillPos.X + .5f, FillPos.Y + .45f), GlassSize,
			FMath::Min(GlassSize.Y * .5f, CapR * .9f),
			WithAlpha(FMath::Lerp(Fill, W.HighlightColor, .58f), .52f), Tint);
		++Layer;
		const FVector2D ShadeSize(FMath::Max(0.f, EffectiveWidth - 1.f), FMath::Max(0.f, FillSize.Y * .26f));
		Capsule(Out, Layer, G, FVector2D(FillPos.X + .5f, FillPos.Y + FillSize.Y - ShadeSize.Y - .35f), ShadeSize,
			FMath::Min(ShadeSize.Y * .5f, CapR * .7f), FLinearColor(0.f, 0.f, 0.f, .16f), Tint);
		++Layer;

		// Specular top rim.
		Line(Out, Layer, G, FVector2D(FillPos.X + CapR * .85f, FillPos.Y + .65f),
			FVector2D(FillPos.X + FMath::Max(CapR * .85f, EffectiveWidth - CapR * .85f), FillPos.Y + .65f),
			.95f, FLinearColor(1.f, 1.f, 1.f, .40f), Tint);
		++Layer;

		// Low-health leading edge glows so a short bar still reads.
		if (bLow && EffectiveWidth > FillSize.Y)
		{
			Line(Out, Layer, G, FVector2D(FillPos.X + EffectiveWidth - 1.1f, FillPos.Y + 1.f),
				FVector2D(FillPos.X + EffectiveWidth - 1.1f, FillPos.Y + FillSize.Y - 1.f), 1.6f,
				FLinearColor(1.f, .89f, .85f, .92f), Tint);
			++Layer;
		}

		if (W.ShieldFraction > 0.f)
		{
			const float ShieldWidth = FMath::Min(FillSize.X, FMath::Max(FillSize.Y * 1.2f, FillSize.X * FMath::Clamp(W.ShieldFraction, 0.f, 1.f)));
			Capsule(Out, Layer, G, FillPos, FVector2D(ShieldWidth, FillSize.Y),
				FMath::Min(FillRadius, ShieldWidth * .5f), WithAlpha(W.ShieldColor, .58f), Tint);
			++Layer;
			Line(Out, Layer, G, FVector2D(FillPos.X + ShieldWidth - 1.1f, FillPos.Y + 1.f),
				FVector2D(FillPos.X + ShieldWidth - 1.1f, FillPos.Y + FillSize.Y - 1.f), 1.5f,
				WithAlpha(W.ShieldColor, .95f), Tint);
			++Layer;
		}

		// Segment separators: dark gouges with a faint bright bevel on the right.
		const int32 Segments = FMath::Clamp(W.Segments, 0, 16);
		for (int32 I = 1; I < Segments; ++I)
		{
			const float X = TrackPos.X + TrackSize.X * (float(I) / float(Segments));
			Line(Out, Layer, G, FVector2D(X, TrackPos.Y + .35f), FVector2D(X, TrackPos.Y + TrackSize.Y - .35f),
				FMath::Max(1.1f, H * .12f), FLinearColor(0.f, 0.f, 0.f, .70f), Tint);
			++Layer;
			Line(Out, Layer, G, FVector2D(X + 1.f, TrackPos.Y + .6f), FVector2D(X + 1.f, TrackPos.Y + TrackSize.Y - .6f),
				.8f, FLinearColor(1.f, 1.f, 1.f, .10f), Tint);
			++Layer;
		}
	}
}

class SEGVitalsBar final : public SProgressBar
{
public:
	TWeakObjectPtr<UEGVitalsBar> Owner;
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
		int32 L, const FWidgetStyle& PaintStyle, bool) const override
	{
		const auto* W = Owner.Get();
		if (!W) return L;
		int32 Layer = L;
		PaintVitalsCapsule(*W, Out, Layer, G, PaintStyle);
		return Layer;
	}
};

/**
 * WXQ level badge: a 45-degree diamond with an ink bezel, champagne bevel rim,
 * dark navy face and glossy edge facets. The number itself is a sibling
 * TextBlock so it stays designer-editable.
 */
class SEGLevelMedal final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEGLevelMedal) {} SLATE_END_ARGS()
	TWeakObjectPtr<UEGLevelMedal> Owner;
	void Construct(const FArguments&) {}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(24.f); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
		int32 L, const FWidgetStyle& PaintStyle, bool) const override
	{
		const auto* W = Owner.Get();
		if (!W) return L;
		const FVector2D Size = G.GetLocalSize();
		const float S = FMath::Min(Size.X, Size.Y);
		if (S < 8.f) return L;
		const FVector2D C = Size * .5f;
		const float A = S * .5f;
		const FLinearColor Tint = PaintStyle.GetColorAndOpacityTint();
		const bool bA = W->bAwakened;
		const FLinearColor Rim = bA ? W->AwakenedRim : W->Rim;
		const FLinearColor RimBright = bA ? FMath::Lerp(W->AwakenedRim, FLinearColor::White, .45f) : W->RimBright;
		const FLinearColor RimShadow = bA ? Shade(W->AwakenedRim, .62f) : W->RimShadow;
		const FLinearColor Face = bA ? W->AwakenedFace : W->Face;
		const FLinearColor FaceGloss = bA ? FMath::Lerp(W->AwakenedFace, FLinearColor::White, .40f) : W->FaceGloss;
		int32 Layer = L;

		if (bA)
		{
			// Soft golden halo behind the diamond.
			Diamond(Out, Layer, G, C + FVector2D(0.f, .8f), A + 1.6f, 3.f, WithAlpha(W->AwakenedGlow, .30f), Tint);
			++Layer;
		}

		// Drop shadow + ink bezel.
		Diamond(Out, Layer, G, C + FVector2D(0.f, 1.1f), A, 2.6f, FLinearColor(0.f, 0.f, 0.f, .38f), Tint);
		++Layer;
		Diamond(Out, Layer, G, C, A, 2.6f, FLinearColor::FromSRGBColor(FColor(4, 6, 10)), Tint);
		++Layer;

		// Champagne bevel rim.
		const float Bevel = FMath::Max(1.1f, S * .058f);
		const float RimHalf = A - Bevel * 1.414f;
		Diamond(Out, Layer, G, C, RimHalf, 2.1f, Rim, Tint);
		++Layer;

		// Bevel facets: bright upper edge, shadowed lower edge.
		const FVector2D T = C + FVector2D(0.f, -RimHalf * .92f);
		const FVector2D RT = C + FVector2D(RimHalf * .92f, 0.f);
		const FVector2D LT = C + FVector2D(-RimHalf * .92f, 0.f);
		const FVector2D B = C + FVector2D(0.f, RimHalf * .92f);
		Line(Out, Layer, G, FMath::Lerp(T, LT, .12f), FMath::Lerp(T, LT, .88f), 1.35f, WithAlpha(RimBright, .95f), Tint);
		++Layer;
		Line(Out, Layer, G, FMath::Lerp(T, RT, .12f), FMath::Lerp(T, RT, .88f), 1.25f, WithAlpha(RimBright, .55f), Tint);
		++Layer;
		Line(Out, Layer, G, FMath::Lerp(B, LT, .14f), FMath::Lerp(B, RT, .86f), 1.2f, WithAlpha(RimShadow, .85f), Tint);
		++Layer;

		// Face.
		const float FaceHalf = A - Bevel * 2.7f;
		Diamond(Out, Layer, G, C, FaceHalf, 1.4f, Face, Tint);
		++Layer;

		// Inner hairline + top gloss facet.
		const float InnerHalf = FaceHalf - .9f;
		const FVector2D IT = C + FVector2D(0.f, -InnerHalf);
		const FVector2D IR = C + FVector2D(InnerHalf, 0.f);
		const FVector2D IL = C + FVector2D(-InnerHalf, 0.f);
		Line(Out, Layer, G, FMath::Lerp(IT, IL, .08f), FMath::Lerp(IT, IR, .92f), .8f, WithAlpha(Rim, .40f), Tint);
		++Layer;
		Line(Out, Layer, G, FMath::Lerp(IT, IL, .16f), FMath::Lerp(IT, IL, .58f), 1.5f, WithAlpha(FaceGloss, .55f), Tint);
		++Layer;
		Line(Out, Layer, G, FMath::Lerp(IT, IR, .16f), FMath::Lerp(IT, IR, .50f), 1.2f, WithAlpha(FaceGloss, .35f), Tint);
		++Layer;

		if (bA)
		{
			// Small gold stud at the top vertex.
			Circle(Out, Layer, G, C + FVector2D(0.f, -(A - Bevel * .6f)), FMath::Max(1.f, S * .045f),
				FLinearColor(1.f, .96f, .82f, .95f), Tint);
			++Layer;
		}
		return Layer;
	}
};

class SEGAwakeningRing final : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEGAwakeningRing) {} SLATE_END_ARGS()
	TWeakObjectPtr<UEGAwakeningRing> Owner;
	void Construct(const FArguments&) {}
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(44); }
	virtual int32 OnPaint(const FPaintArgs&, const FGeometry& G, const FSlateRect&, FSlateWindowElementList& Out,
		int32 L, const FWidgetStyle& PaintStyle, bool) const override
	{
		const auto* W = Owner.Get();
		if (!W) return L;
		const FVector2D Size = G.GetLocalSize();
		const FVector2D C = Size * .5f;
		const float D = FMath::Min(Size.X, Size.Y);
		if (D < 8.f) return L;
		const FLinearColor Tint = PaintStyle.GetColorAndOpacityTint();
		const FLinearColor Accent = W->bAwakened ? W->AwakenedColor : W->ProgressColor;
		const float R = D * .5f;
		int32 Layer = L;
		if (W->bAwakened)
		{
			Arc(Out, Layer, G, C, R - .8f, 0.f, 2.f * VitPI, 2.4f, WithAlpha(W->AwakenedColor, .26f), Tint, true);
			++Layer;
			Arc(Out, Layer, G, C, R + 1.2f, 0.f, 2.f * VitPI, 1.5f, WithAlpha(W->AwakenedColor, .13f), Tint, true);
			++Layer;
		}
		Circle(Out, Layer, G, C + FVector2D(0.f, 1.f), R - .4f, FLinearColor(0.f, 0.f, 0.f, .30f), Tint);
		++Layer;
		Circle(Out, Layer, G, C, R - 1.2f, WithAlpha(W->DiskColor, .94f), Tint);
		++Layer;
		// Champagne frame hairline + inner shadow.
		Arc(Out, Layer, G, C, R - 1.55f, 0.f, 2.f * VitPI, 1.35f, WithAlpha(W->FrameColor, .80f), Tint, true);
		++Layer;
		Arc(Out, Layer, G, C, R - 2.2f, 0.f, 2.f * VitPI, .8f, FLinearColor(0.f, 0.f, 0.f, .45f), Tint, true);
		++Layer;
		const float Stroke = FMath::Clamp(W->StrokeWidth, 1.5f, R * .40f);
		const float ArcR = R - Stroke * .5f - 2.9f;
		const int32 Segments = FMath::Clamp(W->Segments, 1, 32);
		const float Sweep = 2.f * VitPI / float(Segments);
		const float Gap = Segments > 1 ? .17f : 0.f;
		const float ProgressEnd = 2.f * VitPI * FMath::Clamp(W->Progress, 0.f, 1.f);
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A = float(I) * Sweep + Gap;
			const float B = float(I + 1) * Sweep - Gap;
			Arc(Out, Layer, G, C, ArcR, A, B, Stroke, WithAlpha(W->TrackColor, .92f), Tint);
			++Layer;
			if (ProgressEnd > A + .001f)
			{
				const float E = FMath::Min(B, ProgressEnd);
				if (E > A + .001f)
				{
					Arc(Out, Layer, G, C, ArcR, A, E, Stroke, WithAlpha(Accent, .96f), Tint);
					++Layer;
					Arc(Out, Layer, G, C, ArcR - Stroke * .42f, A, E, FMath::Max(.8f, Stroke * .22f),
						WithAlpha(FMath::Lerp(Accent, FLinearColor::White, .55f), .55f), Tint);
					++Layer;
				}
			}
		}
		// Boundary ticks.
		for (int32 I = 0; I < Segments; ++I)
		{
			const float A = float(I) * Sweep;
			const float Cos = FMath::Cos(A);
			const float Sin = FMath::Sin(A);
			Line(Out, Layer, G, C + FVector2D(Cos, Sin) * (ArcR - Stroke * .5f - 1.2f),
				C + FVector2D(Cos, Sin) * (ArcR - Stroke * .5f + .2f), .8f, FLinearColor(0.f, 0.f, 0.f, .55f), Tint);
			++Layer;
		}
		// Progress head gem.
		if (W->Progress > 0.f && W->Progress < 1.f)
		{
			const float A = ProgressEnd - VitPI * .5f;
			const FVector2D Head = C + FVector2D(FMath::Cos(A), FMath::Sin(A)) * ArcR;
			Circle(Out, Layer, G, Head, Stroke * .62f, WithAlpha(Accent, .95f), Tint);
			++Layer;
			Circle(Out, Layer, G, Head, Stroke * .26f, FLinearColor(1.f, 1.f, 1.f, .92f), Tint);
			++Layer;
		}
		if (W->bAwakened)
		{
			// Awakened crown ornament above 12 o'clock: three gold spikes.
			const FVector2D CrownBase = C + FVector2D(0.f, -(R + 1.2f));
			TArray<FVector2D> Crown;
			Crown.Reserve(8);
			Crown.Add(CrownBase + FVector2D(-3.6f, 1.4f));
			Crown.Add(CrownBase + FVector2D(-3.6f, -1.0f));
			Crown.Add(CrownBase + FVector2D(-1.8f, 0.4f));
			Crown.Add(CrownBase + FVector2D(0.f, -2.2f));
			Crown.Add(CrownBase + FVector2D(1.8f, 0.4f));
			Crown.Add(CrownBase + FVector2D(3.6f, -1.0f));
			Crown.Add(CrownBase + FVector2D(3.6f, 1.4f));
			FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Crown, ESlateDrawEffect::None,
				FLinearColor(0.f, 0.f, 0.f, .65f) * Tint, true, 2.2f);
			++Layer;
			FSlateDrawElement::MakeLines(Out, Layer, G.ToPaintGeometry(), Crown, ESlateDrawEffect::None,
				FMath::Lerp(W->AwakenedColor, FLinearColor::White, .35f) * Tint, true, 1.1f);
			++Layer;
			// Gem on the crown's middle spike.
			Circle(Out, Layer, G, CrownBase + FVector2D(0.f, -2.4f), .9f, FLinearColor(1.f, .98f, .90f, .95f), Tint);
			++Layer;
		}
		return Layer;
	}
};
}

TSharedRef<SWidget> UEGVitalsBar::RebuildWidget()
{
	auto Bar = SNew(SEGVitalsBar);
	Bar->Owner = this;
	MyProgressBar = Bar;
	return Bar;
}
void UEGVitalsBar::SetShieldFraction(float Value)
{
	ShieldFraction = FMath::Clamp(Value, 0.f, 1.f);
	if (MyProgressBar) MyProgressBar->Invalidate(EInvalidateWidgetReason::Paint);
}
void UEGVitalsBar::SetHighlightColor(FLinearColor Value)
{
	HighlightColor = Value;
	if (MyProgressBar) MyProgressBar->Invalidate(EInvalidateWidgetReason::Paint);
}
TSharedRef<SWidget> UEGLevelMedal::RebuildWidget()
{
	auto Medal = SNew(SEGLevelMedal);
	Medal->Owner = this;
	MedalWidget = Medal;
	return Medal;
}
void UEGLevelMedal::SetAwakened(bool bValue)
{
	if (bAwakened == bValue) return;
	bAwakened = bValue;
	if (MedalWidget) MedalWidget->Invalidate(EInvalidateWidgetReason::Paint);
}
TSharedRef<SWidget> UEGAwakeningRing::RebuildWidget()
{
	auto Ring = SNew(SEGAwakeningRing);
	Ring->Owner = this;
	RingWidget = Ring;
	return Ring;
}
void UEGAwakeningRing::SetProgress(int32 Cards, int32 Required, bool Awakened)
{
	// Roster totals include the card that created the hero. Only later copies
	// fill the ring: total counts 1 / 2 / 3 display 0 / 1 / 2 of two segments.
	Segments = FMath::Max(2, Required) - 1;
	bAwakened = Awakened;
	const int32 AdditionalCards = FMath::Max(1, Cards) - 1;
	Progress = Awakened ? 1.f : FMath::Clamp(float(AdditionalCards) / float(Segments), 0.f, 1.f);
	if (RingWidget) RingWidget->Invalidate(EInvalidateWidgetReason::Paint);
}
void UEGAwakeningRing::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (RingWidget) RingWidget->Invalidate(EInvalidateWidgetReason::Paint);
}
void UEGAwakeningRing::ReleaseSlateResources(bool Children)
{
	Super::ReleaseSlateResources(Children);
	RingWidget.Reset();
}
void UEGLevelMedal::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	if (MedalWidget) MedalWidget->Invalidate(EInvalidateWidgetReason::Paint);
}
void UEGLevelMedal::ReleaseSlateResources(bool Children)
{
	Super::ReleaseSlateResources(Children);
	MedalWidget.Reset();
}
