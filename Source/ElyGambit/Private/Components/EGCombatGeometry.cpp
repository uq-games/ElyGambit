#include "Components/EGCombatGeometry.h"
#include "Character/EGChessUnit.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Actor/EGBoard.h"
#include "Placement/EGDeploymentBoard.h"
#include "Engine/World.h"

namespace
{
const AEGDeploymentBoard* BoardFor(const AEGChessUnit* U)
{
 const auto* GM=U&&U->GetWorld()?U->GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
 return GM&&GM->GetBoard()?GM->GetBoard()->LayoutSource.Get():nullptr;
}
FVector2D Extent(const AEGChessUnit* U,const AEGDeploymentBoard* B)
{
 if(!U||!U->GetDefinition()||!B)return FVector2D::ZeroVector;
 const FIntPoint N=U->GetDefinition()->GetPlacementFootprint();
 return FVector2D(N.X>1?N.X*.5*B->CellWidth:0.,N.Y>1?N.Y*.5*B->CellDepth:0.);
}
// Signed distance to nearest boundary: negative inside, positive outside.
double BoxDistance(FVector2D P,FVector2D E)
{
 const FVector2D D(FMath::Abs(P.X)-E.X,FMath::Abs(P.Y)-E.Y);
 return FVector2D(FMath::Max(0.,D.X),FMath::Max(0.,D.Y)).Size()+FMath::Min(0.,FMath::Max(D.X,D.Y));
}
bool CrossesBox(FVector2D A,FVector2D B,FVector2D E)
{
 double Lo=0.,Hi=1.;const FVector2D D=B-A;
 for(int32 I=0;I<2;++I)
 {
  const double P=I?A.Y:A.X,V=I?D.Y:D.X,H=I?E.Y:E.X;
  if(FMath::Abs(V)<UE_DOUBLE_SMALL_NUMBER){if(P<=-H||P>=H)return false;continue;}
  double T0=(-H-P)/V,T1=(H-P)/V;if(T0>T1)Swap(T0,T1);
  Lo=FMath::Max(Lo,T0);Hi=FMath::Min(Hi,T1);if(Lo>=Hi)return false;
 }
 return Hi>0.&&Lo<1.;
}
bool ObstacleFrame(const AEGChessUnit* U,float Clearance,FTransform& Frame,FVector2D& Center,FVector2D& Half)
{
 const auto* B=BoardFor(U);if(!B||!EGCombatGeometry::HasLargeFootprint(U))return false;
 Frame=B->GetActorTransform();const FVector C=Frame.InverseTransformPosition(U->GetActorLocation());Center=FVector2D(C.X,C.Y);
 Half=Extent(U,B);const FVector S=Frame.GetScale3D();
 Half.X+=Clearance/FMath::Max(.01,FMath::Abs(S.X));Half.Y+=Clearance/FMath::Max(.01,FMath::Abs(S.Y));return true;
}
}
bool EGCombatGeometry::HasLargeFootprint(const AEGChessUnit* U)
{if(!U||!U->GetDefinition())return false;const auto N=U->GetDefinition()->GetPlacementFootprint();return N.X>1||N.Y>1;}
FVector EGCombatGeometry::ClosestPoint(FVector Point,const AEGChessUnit* U)
{
 if(!U)return Point;const auto* B=BoardFor(U);
 if(!B||!HasLargeFootprint(U))return U->GetActorLocation();
 const FTransform F=B->GetActorTransform();const FVector C=F.InverseTransformPosition(U->GetActorLocation());FVector P=F.InverseTransformPosition(Point);const auto E=Extent(U,B);
 P.X=FMath::Clamp(P.X,C.X-E.X,C.X+E.X);P.Y=FMath::Clamp(P.Y,C.Y-E.Y,C.Y+E.Y);P.Z=C.Z;return F.TransformPosition(P);
}
double EGCombatGeometry::PointDistanceSquared(FVector Point,const AEGChessUnit* U)
{return U?FVector::DistSquared2D(Point,ClosestPoint(Point,U)):TNumericLimits<double>::Max();}
double EGCombatGeometry::DistanceSquared(const AEGChessUnit* A,const AEGChessUnit* B)
{
 if(!A||!B)return TNumericLimits<double>::Max();const auto* Board=BoardFor(A);
 if(!Board||(!HasLargeFootprint(A)&&!HasLargeFootprint(B)))return FVector::DistSquared2D(A->GetActorLocation(),B->GetActorLocation());
 const FTransform F=Board->GetActorTransform();const FVector D=F.InverseTransformPosition(B->GetActorLocation())-F.InverseTransformPosition(A->GetActorLocation());
 const auto EA=Extent(A,Board),EB=Extent(B,Board);const FVector S=F.GetScale3D();
 const double X=FMath::Max(0.,FMath::Abs(D.X)-EA.X-EB.X)*FMath::Abs(S.X),Y=FMath::Max(0.,FMath::Abs(D.Y)-EA.Y-EB.Y)*FMath::Abs(S.Y);return X*X+Y*Y;
}
bool EGCombatGeometry::SegmentBlocked(FVector Start,FVector End,const AEGChessUnit* Obstacle,float Clearance)
{
 FTransform F;FVector2D C,E;if(!ObstacleFrame(Obstacle,Clearance,F,C,E))return false;
 const FVector SA=F.InverseTransformPosition(Start),SB=F.InverseTransformPosition(End);const FVector2D A=FVector2D(SA.X,SA.Y)-C,B=FVector2D(SB.X,SB.Y)-C;
 if(BoxDistance(A,E)<0.)
 {
  // Permit only movement toward the nearest exit, never a sweep through the
  // rectangle to its opposite side when an overlap is being recovered.
  const FVector2D D=B-A;
  const bool XExit=E.X-FMath::Abs(A.X)<E.Y-FMath::Abs(A.Y);
  const double Outward=XExit?D.X*(A.X>=0.?1.:-1.):D.Y*(A.Y>=0.?1.:-1.);
  if(Outward>0.&&BoxDistance(B,E)>BoxDistance(A,E))return false;
 }
 return CrossesBox(A,B,E);
}
FVector EGCombatGeometry::ProjectOutside(FVector Point,const AEGChessUnit* Obstacle,float Clearance)
{
 FTransform F;FVector2D C,E;if(!ObstacleFrame(Obstacle,Clearance,F,C,E))return Point;
 FVector P=F.InverseTransformPosition(Point);const FVector2D D=FVector2D(P.X,P.Y)-C;
 if(FMath::Abs(D.X)>=E.X||FMath::Abs(D.Y)>=E.Y)return Point;
 if(E.X-FMath::Abs(D.X)<E.Y-FMath::Abs(D.Y))P.X=C.X+(D.X>=0?E.X:-E.X);
 else P.Y=C.Y+(D.Y>=0?E.Y:-E.Y);
 return F.TransformPosition(P);
}
