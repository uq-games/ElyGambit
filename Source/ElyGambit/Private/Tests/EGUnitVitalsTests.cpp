#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGVitalsGraphics.h"
#include "Presentation/EGBoardFraming.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/WorldSettings.h"
#include "Game/EGChessGameMode.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/EGHeroSkillComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Data/EGHeroDefinition.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVitalsLifecycle,"ElyGambit.Presentation.UnitVitals.LifecycleAndAttributes",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVitalsLifecycle::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGChessGameMode::StaticClass();
 FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 auto* GM=W->GetAuthGameMode<AEGChessGameMode>();auto* Roster=GM->GetRoster();
 auto* D=NewObject<UEGHeroDefinition>(GI.Get());D->HeroId=TEXT("VitalsFixture");D->BaseStats.MaxHealth=100;D->BaseStats.MaxMana=100;
 const FGuid Id=Roster->AddHero(D);
 auto* View=CreateWidget<UEGUnitVitals>(GI.Get(),UEGUnitVitals::ResolveWidgetClass());
 if(!TestNotNull(TEXT("Saved editable vitals class loads"),View))return false;
 TStrongObjectPtr<UEGUnitVitals> KeepView(View);View->TakeWidget();
 View->SetPreparationHero(Roster,Id);
 auto* Ring=View->GetAwakeningRing();
 if(!TestNotNull(TEXT("Saved WBP contains vector awakening ring"),Ring))return false;
 TestEqual(TEXT("Roster presentation"),View->DisplayMode,EEGVitalsMode::Preparation);
 TestEqual(TEXT("Two additional cards are represented by two ring segments"),Ring->Segments,2);
 TestTrue(TEXT("Initial hero starts with two empty segments"),Ring->Progress==0&&!Ring->bAwakened);
 Roster->ApplySameNameCard(Id);TestTrue(TEXT("One additional card fills half, without awakening"),FMath::IsNearlyEqual(Ring->Progress,.5f)&&!Ring->bAwakened);
 Roster->ApplySameNameCard(Id);TestTrue(TEXT("Second additional card fills ring and awakens"),Ring->bAwakened&&Ring->Progress==1);
 FEGHeroInstance AwakenedHero;
 TestTrue(TEXT("Awakening uses exactly three total cards"),Roster->GetHero(Id,AwakenedHero)&&AwakenedHero.SameNameCards==3&&AwakenedHero.bAwakened);
 auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
 TestTrue(TEXT("Actual authoritative unit initialized"),Unit->InitializeUnit(D,Id,EEGFaction::Player,1));
 View->SetUnit(Unit);auto* ASC=Unit->GetAbilitySystemComponent();
 TestEqual(TEXT("Combat presentation"),View->DisplayMode,EEGVitalsMode::Combat);
 TestEqual(TEXT("PC health size overrides saved Blueprint defaults"),View->VitalsStyle.HealthSize,FVector2D(80,10));
 TestEqual(TEXT("PC mana size"),View->VitalsStyle.ManaSize,FVector2D(64,5));
 TestFalse(TEXT("Restored level badge is left of the bars"),View->VitalsStyle.bBadgeAboveBars);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),35);
 TestTrue(TEXT("GAS damage updates health fraction"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.35f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),80);
 TestTrue(TEXT("GAS healing updates health fraction"),FMath::IsNearlyEqual(View->GetHealthBar()->GetPercent(),.8f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),65);
 TestTrue(TEXT("GAS mana updates blue bar"),FMath::IsNearlyEqual(View->GetManaBar()->GetPercent(),.65f));
 Unit->GetHeroSkill()->SetShield(40);
 auto* HP=Cast<UEGVitalsBar>(View->GetHealthBar());
 TestTrue(TEXT("Shield overlays the same health bar"),HP&&FMath::IsNearlyEqual(HP->ShieldFraction,.4f));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxManaAttribute(),0);
 TestEqual(TEXT("Units without mana hide blue bar"),View->GetManaBar()->GetVisibility(),ESlateVisibility::Collapsed);
 View->SetPreparationHero(Roster,Id);
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),20);
 TestEqual(TEXT("Combat delegates detached after restoration"),View->DisplayMode,EEGVitalsMode::Preparation);
 TestTrue(TEXT("Persistent awakening survives battle view"),Ring->bAwakened&&Ring->Progress==1);
 View->SetUnit(Unit);Unit->Die();
 TestEqual(TEXT("Death removes vitals"),View->DisplayMode,EEGVitalsMode::Unbound);
 View->SetPreparationHero(Roster,Id);Roster->ResetRun();
 TestEqual(TEXT("Restart removes stale roster vitals"),View->DisplayMode,EEGVitalsMode::Unbound);
 View->SetUnit(nullptr);
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGReferenceFraming,"ElyGambit.Presentation.UnitVitals.ReferenceFraming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGReferenceFraming::RunTest(const FString&)
{
 const FRotator R(-48,90,0);const FRotationMatrix Basis(R);
 const FVector F=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
 TArray<FVector> Ground,Units;
 for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})
 {Ground.Add(FVector(X*2000,Y*2000,0));Units.Add(FVector(X*1500,Y*1500,0));Units.Add(FVector(X*1250,Y*1250,650));}
 for(FVector2D Size:{FVector2D(1280,720),FVector2D(1920,1080),FVector2D(2560,1440),FVector2D(2560,1080),FVector2D(1252,959)})
 {
  FVector Camera;TestTrue(TEXT("Reference framing solves"),FEGBoardFraming::SolveReference(Ground,Units,FVector::ZeroVector,R,60,Size,Camera));
  const double TX=FMath::Tan(PI/6),TY=TX*Size.Y/Size.X;
  auto Project=[&](FVector P){const FVector D=P-Camera;const double Z=FVector::DotProduct(D,F);return FVector2D(.5+FVector::DotProduct(D,Right)/(2*Z*TX),.5-FVector::DotProduct(D,Up)/(2*Z*TY));};
  double MinX=1,MaxX=0;
  for(FVector P:Ground){const auto UV=Project(P);MinX=FMath::Min(MinX,UV.X);MaxX=FMath::Max(MaxX,UV.X);TestTrue(TEXT("Ground stays inside reference vertical bounds"),UV.Y>=.1399&&UV.Y<=.9001);}
  for(FVector P:Units){const auto UV=Project(P);TestTrue(TEXT("Playable corners and heads inside safe region"),UV.X>=.0599&&UV.X<=.9401&&UV.Y>=.0999&&UV.Y<=.8201);}
  if(FMath::IsNearlyEqual(Size.X/Size.Y,16./9.,.001))TestTrue(TEXT("Ground width is 72 to 78 percent"),MaxX-MinX>=.72&&MaxX-MinX<=.78);
 }
 FVector Unchanged(1,2,3);TestFalse(TEXT("Zero viewport deferred"),FEGBoardFraming::SolveReference(Ground,Units,FVector::ZeroVector,R,60,FVector2D::ZeroVector,Unchanged));
 TestEqual(TEXT("Invalid input preserves camera"),Unchanged,FVector(1,2,3));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPlayableGridFraming,"ElyGambit.Presentation.UnitVitals.PlayableGridFraming",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPlayableGridFraming::RunTest(const FString&)
{
 const FRotator R(-45,90,0);const FRotationMatrix Basis(R);
 const FVector F=Basis.GetUnitAxis(EAxis::X),Right=Basis.GetUnitAxis(EAxis::Y),Up=Basis.GetUnitAxis(EAxis::Z);
 TArray<FVector> Grid,Cells,DecorativeFloor;
 for(float X:{-1.f,1.f})for(float Y:{-1.f,1.f})
 {Grid.Add(FVector(X*1500,Y*1500,0));Cells.Add(FVector(X*1250,Y*1250,0));Cells.Add(FVector(X*1250,Y*1250,590));DecorativeFloor.Add(FVector(X*2000,Y*2000,0));}
 for(FVector2D Size:{FVector2D(1280,720),FVector2D(1920,1080),FVector2D(2560,1440),FVector2D(2560,1080),FVector2D(1252,959)})
 {
  FVector Camera,OldCamera;
  TestTrue(TEXT("Playable grid framing solves"),FEGBoardFraming::SolvePlayableGrid(Grid,Cells,FVector::ZeroVector,R,60,Size,Camera));
  TestTrue(TEXT("Legacy decorative framing still solves"),FEGBoardFraming::SolveReference(DecorativeFloor,Cells,FVector::ZeroVector,R,60,Size,OldCamera));
  TestTrue(TEXT("Small board camera is closer than decorative-ground fit"),FVector::DotProduct(Camera,F)>FVector::DotProduct(OldCamera,F));
  const double TX=FMath::Tan(PI/6),TY=TX*Size.Y/Size.X;
  auto Project=[&](FVector P){const FVector D=P-Camera;const double Z=FVector::DotProduct(D,F);return FVector2D(.5+FVector::DotProduct(D,Right)/(2*Z*TX),.5-FVector::DotProduct(D,Up)/(2*Z*TY));};
  double MinX=1,MaxX=0;
  for(FVector P:Grid){const auto UV=Project(P);MinX=FMath::Min(MinX,UV.X);MaxX=FMath::Max(MaxX,UV.X);TestTrue(TEXT("Whole playable surface stays on screen"),UV.X>=.1499&&UV.X<=.8501&&UV.Y>=.1399&&UV.Y<=.8001);}
  for(FVector P:Cells){const auto UV=Project(P);TestTrue(TEXT("Outer cell centres and heads remain in safe region"),UV.Y>=.0899&&UV.Y<=.7901);}
  if(FMath::IsNearlyEqual(Size.X/Size.Y,16./9.,.001))TestTrue(TEXT("Closer board occupies 60 to 67 percent width above the hand"),MaxX-MinX>=.60&&MaxX-MinX<=.67);
 }
 return true;
}
#endif
