#include "Presentation/EGBoardFraming.h"

namespace
{
bool SolveFraming(TConstArrayView<FVector> Ground,TConstArrayView<FVector> Units,
 FVector Focus,FRotator Rotation,float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation,
 double GroundTop,double GroundBottom,double UnitsTop,double UnitsBottom,double GroundSide=.12)
{
 if(Ground.IsEmpty()||Units.IsEmpty()||Focus.ContainsNaN()||Rotation.ContainsNaN()||
    !FMath::IsFinite(HorizontalFOV)||HorizontalFOV<10||HorizontalFOV>120||
    !FMath::IsFinite(Viewport.X)||!FMath::IsFinite(Viewport.Y)||Viewport.X<=0||Viewport.Y<=0)return false;
 for(auto Set:{Ground,Units})for(const FVector& P:Set)if(P.ContainsNaN())return false;
 const FRotationMatrix Basis(Rotation);
 const FVector Forward=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
 const double TanX=FMath::Tan(FMath::DegreesToRadians(HorizontalFOV*.5)),TanY=TanX*Viewport.Y/Viewport.X;
 const double Side=FMath::Clamp(GroundSide+(16./9.-Viewport.X/Viewport.Y)*.15,GroundSide,GroundSide+.06);
 auto Fits=[&](double Distance,double& Offset)
 {
  double Lower=-TNumericLimits<double>::Max(),Upper=TNumericLimits<double>::Max();
  auto Include=[&](TConstArrayView<FVector> Set,double Margin,double Top,double Bottom)
  {
   for(const FVector& P:Set)
   {
    const FVector Delta=P-Focus;const double Depth=Distance+FVector::DotProduct(Delta,Forward);
    if(Depth<=1||FMath::Abs(FVector::DotProduct(Delta,Right))>(1.-2*Margin)*Depth*TanX)return false;
    const double Y=FVector::DotProduct(Delta,Up);
    Lower=FMath::Max(Lower,Y-(1.-2*Top)*Depth*TanY);
    Upper=FMath::Min(Upper,Y+(2*Bottom-1.)*Depth*TanY);
   }
   return Lower<=Upper;
  };
  if(!Include(Ground,Side,GroundTop,GroundBottom)||!Include(Units,.06,UnitsTop,UnitsBottom))return false;
  Offset=(Lower+Upper)*.5;return true;
 };
 double Low=1,High=1000,Offset=0;
 while(!Fits(High,Offset)&&High<1.e8)High*=2;
 if(!Fits(High,Offset))return false;
 for(int32 I=0;I<48;++I){const double Mid=(Low+High)*.5;if(Fits(Mid,Offset))High=Mid;else Low=Mid;}
 Fits(High+1,Offset);CameraLocation=Focus-Forward*(High+1)+Up*Offset;
 return !CameraLocation.ContainsNaN();
}
}

bool FEGBoardFraming::SolveReference(TConstArrayView<FVector> Ground,TConstArrayView<FVector> Units,
 FVector Focus,FRotator Rotation,float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation)
{
 return SolveFraming(Ground,Units,Focus,Rotation,HorizontalFOV,Viewport,CameraLocation,.14,.90,.10,.82);
}
bool FEGBoardFraming::SolvePlayableGrid(TConstArrayView<FVector> Ground,TConstArrayView<FVector> Units,
 FVector Focus,FRotator Rotation,float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation)
{
 // Bring the playable grid closer while keeping its near edge above the full
 // hand and leaving the outer-row heads below the top HUD.
 return SolveFraming(Ground,Units,Focus,Rotation,HorizontalFOV,Viewport,CameraLocation,.14,.80,.09,.79,.15);
}

bool FEGBoardFraming::Solve(TConstArrayView<FVector> Points,FVector Focus,FRotator Rotation,
 float HorizontalFOV,FVector2D Viewport,FVector& CameraLocation)
{
 if(Points.IsEmpty()||Focus.ContainsNaN()||Rotation.ContainsNaN()||
    !FMath::IsFinite(HorizontalFOV)||HorizontalFOV<10.f||HorizontalFOV>120.f||
    !FMath::IsFinite(Viewport.X)||!FMath::IsFinite(Viewport.Y)||Viewport.X<=0||Viewport.Y<=0)return false;
 for(const FVector& P:Points)if(P.ContainsNaN())return false;
 const FRotationMatrix Basis(Rotation);
 const FVector Forward=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
 const double TanX=FMath::Tan(FMath::DegreesToRadians(HorizontalFOV*.5f)),TanY=TanX*Viewport.Y/Viewport.X;
 // At narrow aspect ratios the fixed-size bottom controls occupy more width.
 const double SideMargin=FMath::Clamp(.06+(16./9.-Viewport.X/Viewport.Y)*.2,.06,.16);
 auto Fits=[&](double Distance,double& Offset)
 {
  double Lower=-TNumericLimits<double>::Max(),Upper=TNumericLimits<double>::Max();
  for(const FVector& P:Points)
  {
   const FVector Delta=P-Focus;
   const double Depth=Distance+FVector::DotProduct(Delta,Forward);
   if(Depth<=1.||FMath::Abs(FVector::DotProduct(Delta,Right))>(1.-2.*SideMargin)*Depth*TanX)return false;
   const double Y=FVector::DotProduct(Delta,Up);
   // Screen bounds: y=.10..80. Bottom space belongs to hand/controls.
   Lower=FMath::Max(Lower,Y-.8*Depth*TanY);
   Upper=FMath::Min(Upper,Y+.6*Depth*TanY);
  }
  Offset=(Lower+Upper)*.5;
  return Lower<=Upper;
 };
 double Low=1.,High=1000.,Offset=0.;
 while(!Fits(High,Offset)&&High<1.e8)High*=2.;
 if(!Fits(High,Offset))return false;
 for(int32 I=0;I<48;++I){const double Mid=(Low+High)*.5;if(Fits(Mid,Offset))High=Mid;else Low=Mid;}
 Fits(High+1.,Offset);
 CameraLocation=Focus-Forward*(High+1.)+Up*Offset;
 return !CameraLocation.ContainsNaN();
}
