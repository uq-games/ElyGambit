#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Presentation/EGBoardFraming.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBoardFramingTest,"ElyGambit.Presentation.BoardFramingAcrossWindows",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBoardFramingTest::RunTest(const FString&)
{
 const FRotator Rotation(-48,90,0);const FRotationMatrix Basis(Rotation);
 const FVector Forward=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
 const double TanX=FMath::Tan(FMath::DegreesToRadians(30.));
 TArray<FVector> Points;
 for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})
 {
  Points.Add(FVector(X*1499.882,Y*1502.036,0));
  Points.Add(FVector(X*1249.902,Y*1251.697,530));
 }
 for(FVector2D Size:{FVector2D(1920,1080),FVector2D(2560,1440),FVector2D(1280,720),FVector2D(1252,959)})
 {
  FVector Location;
  TestTrue(TEXT("Fit succeeds"),FEGBoardFraming::Solve(Points,FVector::ZeroVector,Rotation,60,Size,Location));
  const double TanY=TanX*Size.Y/Size.X;
  auto Project=[&](FVector Point)
  {
   const FVector Delta=Point-Location;const double Depth=FVector::DotProduct(Delta,Forward);
   TestTrue(TEXT("Ground and heads in front of camera"),Depth>0);
   return FVector2D(.5+FVector::DotProduct(Delta,Right)/(2*Depth*TanX),.5-FVector::DotProduct(Delta,Up)/(2*Depth*TanY));
  };
  for(FVector P:Points)
  {
   const FVector2D UV=Project(P);
   TestTrue(TEXT("Playable edges and outer-row heads remain visible"),UV.X>=.0599&&UV.X<=.9401&&UV.Y>=.0999&&UV.Y<=.8001);
  }
  const double HeroHeight=(Project(FVector::ZeroVector).Y-Project(FVector(0,0,466.89)).Y)*Size.Y;
  if(FMath::IsNearlyEqual(Size.X/Size.Y,16./9.,.001))
   TestTrue(TEXT("Pardo centre height is readable at the 1080 design scale"),HeroHeight*1080/Size.Y>=100&&HeroHeight*1080/Size.Y<=130);
 }
 FVector Unchanged(1,2,3);
 TestFalse(TEXT("Zero viewport is deferred"),FEGBoardFraming::Solve(Points,FVector::ZeroVector,Rotation,60,FVector2D::ZeroVector,Unchanged));
 TestEqual(TEXT("Invalid input does not move camera"),Unchanged,FVector(1,2,3));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPlayableBoardSafeFrameTest,"ElyGambit.Presentation.PlayableBoardCornerAndHandClearance",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPlayableBoardSafeFrameTest::RunTest(const FString&)
{
 const FRotator CameraRotation(-48,90,0);
 const FRotationMatrix CameraBasis(CameraRotation);
 const FVector Forward=CameraBasis.GetUnitAxis(EAxis::X),Right=CameraBasis.GetUnitAxis(EAxis::Y),Up=CameraBasis.GetUnitAxis(EAxis::Z);
 const double TanX=FMath::Tan(FMath::DegreesToRadians(30.));
 for(const FVector2D Size:{FVector2D(1920,1080),FVector2D(1600,900),FVector2D(2560,1080),FVector2D(1252,959)})
 for(const double Yaw:{0.,37.})
 {
  const FTransform BoardTransform(FRotator(0,Yaw,0),FVector(100,-250,30));
  const FVector Focus=BoardTransform.GetLocation();
  TArray<FVector> Ground,Heads;
  for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})
  {
   Ground.Add(BoardTransform.TransformPosition(FVector(X*1749.862,Y*1752.375,0)));
   Heads.Add(BoardTransform.TransformPosition(FVector(X*1499.882,Y*1502.036,0)));
   Heads.Add(BoardTransform.TransformPosition(FVector(X*1499.882,Y*1502.036,590)));
  }
  FVector Location;
  TestTrue(TEXT("Seven-by-seven framing succeeds"),FEGBoardFraming::SolvePlayableGrid(Ground,Heads,Focus,CameraRotation,60,Size,Location));
  const double TanY=TanX*Size.Y/Size.X;
  auto Project=[&](const FVector& P)
  {
   const FVector Delta=P-Location;const double Depth=FVector::DotProduct(Delta,Forward);
   TestTrue(TEXT("Framed point remains in front"),Depth>0);
   return FVector2D(.5+FVector::DotProduct(Delta,Right)/(2*Depth*TanX),.5-FVector::DotProduct(Delta,Up)/(2*Depth*TanY));
  };
  for(const FVector& P:Ground)
  {
   const FVector2D UV=Project(P);
   TestTrue(TEXT("Closer perimeter stays above the complete hand"),UV.X>=.1499&&UV.X<=.8501&&UV.Y>=.1399&&UV.Y<=.8001);
  }
  for(const FVector& P:Heads)
  {
   const FVector2D UV=Project(P);
   TestTrue(TEXT("Outer-row bodies and heads clear HUD"),UV.X>=.0599&&UV.X<=.9401&&UV.Y>=.0899&&UV.Y<=.7901);
  }
 }
 FVector Unchanged(1,2,3);
 TestFalse(TEXT("Unready viewport cannot move camera"),FEGBoardFraming::SolvePlayableGrid({}, {},FVector::ZeroVector,CameraRotation,60,FVector2D::ZeroVector,Unchanged));
 TestEqual(TEXT("Deferred framing preserves transform"),Unchanged,FVector(1,2,3));
 return true;
}
#endif
