#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextBlock.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Input/HittestGrid.h"
#include "Placement/EGPlacementVisuals.h"
#include "Rendering/DrawElements.h"
#include "Types/PaintArgs.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "UI/Widget/EGWidgets.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SWindow.h"

namespace
{
bool IsInside(const UWidget* Child, const UWidget* Ancestor)
{
 for (const UWidget* Parent = Child ? Child->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
  if (Parent == Ancestor) return true;
 return false;
}

bool CheckDetails(FAutomationTestBase& Test, UWidgetTree* Tree, const TCHAR* Context)
{
 if (!Test.TestNotNull(FString::Printf(TEXT("%s widget tree"), Context), Tree)) return false;
 auto* Scroll = Cast<UScrollBox>(Tree->FindWidget(TEXT("UnitInfoScroll")));
 auto* Heading = Tree->FindWidget(TEXT("CharacterName"));
 auto* Card = Cast<USizeBox>(Tree->FindWidget(TEXT("InfoCardSize")));
 auto* Sell = Tree->FindWidget(TEXT("Button_SellInfo"));
 auto* Close = Tree->FindWidget(TEXT("Button_CloseInfo"));
 bool bValid = Test.TestNotNull(FString::Printf(TEXT("%s scroll"), Context), Scroll);
 bValid = Test.TestNotNull(FString::Printf(TEXT("%s heading"), Context), Heading) && bValid;
 bValid = Test.TestNotNull(FString::Printf(TEXT("%s card size"), Context), Card) && bValid;
 bValid = Test.TestNotNull(FString::Printf(TEXT("%s sell button"), Context), Sell) && bValid;
 bValid = Test.TestNotNull(FString::Printf(TEXT("%s close button"), Context), Close) && bValid;
 if (!bValid) return false;

 Test.TestTrue(FString::Printf(TEXT("%s large card is scrollable"), Context), IsInside(Card, Scroll));
 Test.TestFalse(FString::Printf(TEXT("%s heading remains outside scroll"), Context), IsInside(Heading, Scroll));
 Test.TestFalse(FString::Printf(TEXT("%s sell remains outside scroll"), Context), IsInside(Sell, Scroll));
 Test.TestFalse(FString::Printf(TEXT("%s close remains outside scroll"), Context), IsInside(Close, Scroll));
 const float Width = Card->GetWidthOverride(), Height = Card->GetHeightOverride();
 Test.TestTrue(FString::Printf(TEXT("%s retains a large card"), Context), Width >= 200.f && Height >= 280.f);
 Test.TestTrue(FString::Printf(TEXT("%s retains card aspect ratio"), Context), Height > 0.f && FMath::IsNearlyEqual(Width / Height, 5.f / 7.f, .02f));
 return true;
}

bool CheckHover(FAutomationTestBase& Test, UWidgetTree* Tree, const TCHAR* Context)
{
 if (!Test.TestNotNull(FString::Printf(TEXT("%s widget tree"), Context), Tree)) return false;
 auto* Card = Tree->FindWidget(TEXT("InfoCardSize"));
 Test.TestTrue(FString::Printf(TEXT("%s does not show the large card"), Context),
  !Card || Card->GetVisibility() == ESlateVisibility::Collapsed);
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUIRemakePreview,
 "ElyGambit.UI.Remake.PreviewShowHideReuse", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGUIRemakePreview::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();
 UWorld* W = GI->GetWorld();
 ON_SCOPE_EXIT { W->BeginTearingDown(); W->EndPlay(EEndPlayReason::Quit); GI->Shutdown(); W->DestroyWorld(false); GEngine->DestroyWorldContext(W); };
 W->GetWorldSettings()->DefaultGameMode = AGameModeBase::StaticClass();
 FURL URL; W->SetGameMode(URL); W->InitializeActorsForPlay(URL); W->BeginPlay();
 auto* Hero = LoadObject<UEGHeroDefinition>(nullptr, TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"));
 auto* Preview = W->SpawnActor<AEGPlacementPreview>();
 if (!TestNotNull(TEXT("Representative hero definition"), Hero) ||
     !TestNotNull(TEXT("Placement preview"), Preview) ||
     !TestNotNull(TEXT("Preview body"), Preview->Body.Get()) ||
     !TestNotNull(TEXT("Legacy placement component"), Preview->Ring.Get())) return false;
 Preview->ApplyHeroDefinition(Hero);
 auto* Mesh = Preview->Body->GetSkeletalMeshAsset();
 if (!TestNotNull(TEXT("Representative hero mesh is loaded"), Mesh)) return false;

 // Simulate an old asset that authored the component as visible.
 Preview->Ring->SetVisibility(true); Preview->Ring->SetHiddenInGame(false);
 Preview->ShowAt(FVector(100, 200, 0), true);
 TestFalse(TEXT("Legal preview actor is shown"), Preview->IsHidden());
 TestTrue(TEXT("Legal preview body remains visible"), Preview->Body->IsVisible() && !Preview->Body->bHiddenInGame);
 TestTrue(TEXT("Legal preview does not draw the plane"), !Preview->Ring->IsVisible() || Preview->Ring->bHiddenInGame);
 TestTrue(TEXT("Preview retains its hero mesh"), Preview->Body->GetSkeletalMeshAsset() == Mesh);
 Preview->HidePreview();
 TestTrue(TEXT("Preview is hidden between drags"), Preview->IsHidden());
 Preview->Ring->SetVisibility(true); Preview->Ring->SetHiddenInGame(false);
 Preview->ShowAt(FVector(200, 300, 0), false);
 TestFalse(TEXT("Reused invalid preview actor is shown"), Preview->IsHidden());
 TestTrue(TEXT("Reused preview body remains visible"), Preview->Body->IsVisible() && !Preview->Body->bHiddenInGame);
 TestTrue(TEXT("Reused invalid preview does not draw the plane"), !Preview->Ring->IsVisible() || Preview->Ring->bHiddenInGame);
 TestTrue(TEXT("Reuse retains the selected hero mesh"), Preview->Body->GetSkeletalMeshAsset() == Mesh);
 Preview->Destroy();
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUIRemakeNativeDetails,
 "ElyGambit.UI.Remake.NativeDetailsLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGUIRemakeNativeDetails::RunTest(const FString&)
{
 TStrongObjectPtr<UWidgetTree> Details(NewObject<UWidgetTree>());
 UEGHeroInfoWidget::BuildPresentation(Details.Get(), EEGHeroInfoPresentation::UnitDetails);
 CheckDetails(*this, Details.Get(), TEXT("Native details"));
 TStrongObjectPtr<UWidgetTree> Hover(NewObject<UWidgetTree>());
 UEGHeroInfoWidget::BuildPresentation(Hover.Get(), EEGHeroInfoPresentation::UnitHover);
 CheckHover(*this, Hover.Get(), TEXT("Native hover"));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUIRemakeSavedDetails,
 "ElyGambit.UI.Remake.SavedDetailsLayout", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGUIRemakeSavedDetails::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();
 UWorld* W = GI->GetWorld();
 ON_SCOPE_EXIT { W->BeginTearingDown(); W->EndPlay(EEndPlayReason::Quit); GI->Shutdown(); W->DestroyWorld(false); GEngine->DestroyWorldContext(W); };
 auto* DetailsClass = LoadClass<UEGHeroInfoWidget>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_UnitPinnedInfo.WBP_UnitPinnedInfo_C"));
 auto* HoverClass = LoadClass<UEGHeroInfoWidget>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_UnitHoverInfo.WBP_UnitHoverInfo_C"));
 if (!TestNotNull(TEXT("Saved details class"), DetailsClass) || !TestNotNull(TEXT("Saved hover class"), HoverClass)) return false;
 TestEqual(TEXT("Saved pinned presentation mode"), DetailsClass->GetDefaultObject<UEGHeroInfoWidget>()->Presentation, EEGHeroInfoPresentation::UnitDetails);
 TestEqual(TEXT("Saved hover presentation mode"), HoverClass->GetDefaultObject<UEGHeroInfoWidget>()->Presentation, EEGHeroInfoPresentation::UnitHover);
 TStrongObjectPtr<UEGHeroInfoWidget> Details(CreateWidget<UEGHeroInfoWidget>(GI.Get(), DetailsClass));
 TStrongObjectPtr<UEGHeroInfoWidget> Hover(CreateWidget<UEGHeroInfoWidget>(GI.Get(), HoverClass));
 if (!TestNotNull(TEXT("Saved details instance"), Details.Get()) || !TestNotNull(TEXT("Saved hover instance"), Hover.Get())) return false;
 CheckDetails(*this, Details->WidgetTree, TEXT("Saved details"));
 CheckHover(*this, Hover->WidgetTree, TEXT("Saved hover"));
 auto* Hero = LoadObject<UEGHeroDefinition>(nullptr, TEXT("/Game/Blueprints/Characters/Pardofelis/Data/DA_Pardo"));
 if (TestNotNull(TEXT("Representative hero for hover"), Hero))
 {
  Hover->Present(Hero, FText::GetEmpty());
  CheckHover(*this, Hover->WidgetTree, TEXT("Saved hover after presenting a hero"));
 }
 auto* Sell = Details->GetWidgetFromName(TEXT("Button_SellInfo"));
 if (TestNotNull(TEXT("Saved details sell button"), Sell))
 {
  Details->SetSaleAvailable(true);
  TestEqual(TEXT("Available sale is visible"), Sell->GetVisibility(), ESlateVisibility::Visible);
  Details->SetSaleAvailable(false);
  TestEqual(TEXT("Unavailable sale is collapsed"), Sell->GetVisibility(), ESlateVisibility::Collapsed);
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGUIRemakeSavedHUD,
 "ElyGambit.UI.Remake.SavedHUDAndSellZones", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGUIRemakeSavedHUD::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
 GI->InitializeStandalone();
 UWorld* W = GI->GetWorld();
 ON_SCOPE_EXIT { W->BeginTearingDown(); W->EndPlay(EEndPlayReason::Quit); GI->Shutdown(); W->DestroyWorld(false); GEngine->DestroyWorldContext(W); };
 auto* HUDClass = LoadClass<UEGMainHUD>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_BoardHUD.WBP_BoardHUD_C"));
 auto* DetailsClass = LoadClass<UEGHeroInfoWidget>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_UnitPinnedInfo.WBP_UnitPinnedInfo_C"));
 auto* HoverClass = LoadClass<UEGHeroInfoWidget>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_UnitHoverInfo.WBP_UnitHoverInfo_C"));
 auto* LeftClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_SellLeft.WBP_SellLeft_C"));
 auto* RightClass = LoadClass<UUserWidget>(nullptr, TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_SellRight.WBP_SellRight_C"));
 if (!TestNotNull(TEXT("Saved board HUD class"), HUDClass) || !TestNotNull(TEXT("Saved pinned info class"), DetailsClass) ||
     !TestNotNull(TEXT("Saved hover info class"), HoverClass) || !TestNotNull(TEXT("Saved left sale class"), LeftClass) ||
     !TestNotNull(TEXT("Saved right sale class"), RightClass)) return false;
 TStrongObjectPtr<UEGMainHUD> HUD(CreateWidget<UEGMainHUD>(GI.Get(), HUDClass));
 if (!TestNotNull(TEXT("Saved board HUD instance"), HUD.Get())) return false;
 TestTrue(TEXT("Board HUD uses board presentation"), HUD->bBoardPresentation);
 if (TestNotNull(TEXT("Shop panel is bound"), HUD->DrawPanel.Get()))
 {
  HUD->ToggleDrawPanel();
  auto* ShopRoot=HUD->DrawPanel->GetWidgetFromName(TEXT("ShopRoot"));
  if(TestNotNull(TEXT("Shop content root exists"),ShopRoot))
   TestEqual(TEXT("Open shop canvas leaves empty space available to the hand"),ShopRoot->GetVisibility(),ESlateVisibility::SelfHitTestInvisible);
  HUD->ToggleDrawPanel();
 }
 TestTrue(TEXT("Board HUD references saved pinned info"), HUD->UnitPinnedInformationClass.Get() == DetailsClass);
 TestTrue(TEXT("Board HUD references saved hover info"), HUD->UnitHoverInformationClass.Get() == HoverClass);
 UUserWidget* Zones[] = { HUD->SellZone_Left.Get(), HUD->SellZone_Right.Get() };
 UClass* ZoneClasses[] = { LeftClass, RightClass };
 for (int32 I = 0; I < 2; ++I)
 {
  const TCHAR* Side = I == 0 ? TEXT("Left") : TEXT("Right");
  auto* Zone = Zones[I];
  if (!TestNotNull(FString::Printf(TEXT("%s sale zone bound in HUD"), Side), Zone)) continue;
  TestTrue(FString::Printf(TEXT("%s sale zone uses saved class"), Side), Zone->GetClass() == ZoneClasses[I]);
  auto* Slot = Cast<UCanvasPanelSlot>(Zone->Slot);
  if (TestNotNull(FString::Printf(TEXT("%s sale zone has canvas slot"), Side), Slot))
  {
   const FVector2D Size = Slot->GetSize();
   TestTrue(FString::Printf(TEXT("%s sale hit area is 96 by 320"), Side), Size.Equals(FVector2D(96.f, 320.f), .1f));
   const FAnchors Anchors = Slot->GetAnchors();
   TestTrue(FString::Printf(TEXT("%s sale zone is anchored to its screen edge"), Side),
    FMath::IsNearlyEqual(Anchors.Minimum.X, float(I)) && FMath::IsNearlyEqual(Anchors.Maximum.X, float(I)));
   TestTrue(FString::Printf(TEXT("%s full sale zone stays on screen"), Side),
    FMath::IsNearlyZero(Slot->GetPosition().X) && FMath::IsNearlyEqual(Slot->GetAlignment().X, float(I)));
  }
  auto* Surface = Cast<UBorder>(Zone->GetWidgetFromName(TEXT("SellPlateSurface")));
  auto* Label = Cast<UTextBlock>(Zone->GetWidgetFromName(TEXT("Text_Sell")));
  if (TestNotNull(FString::Printf(TEXT("%s sale zone has new background"), Side), Surface))
   TestTrue(FString::Printf(TEXT("%s sale background is visible"), Side),
    Surface->GetVisibility() != ESlateVisibility::Collapsed && Surface->GetVisibility() != ESlateVisibility::Hidden);
  TestNotNull(FString::Printf(TEXT("%s sale zone has price label"), Side), Label);
 }
 if (Zones[0] && Zones[1])
 {
  // A detached canvas gives the targets their real slot layout without an OS
  // window. Paint while both targets are collapsed to reproduce their first drag.
  TStrongObjectPtr<UCanvasPanel> Canvas(NewObject<UCanvasPanel>(HUD.Get()));
  for(int32 I=0;I<2;++I)
  {
   Zones[I]->RemoveFromParent();
   auto* Slot=Canvas->AddChildToCanvas(Zones[I]);
   Slot->SetAnchors(FAnchors(float(I),1.f));Slot->SetAlignment(FVector2D(float(I),1.f));
   Slot->SetPosition(FVector2D::ZeroVector);Slot->SetSize(FVector2D(96.f,320.f));
   Zones[I]->SetVisibility(ESlateVisibility::Collapsed);
  }
  HUD->SetDragActive(true);
  TestFalse(TEXT("An unlaid-out canvas cannot supply sale geometry"), HUD->HasSellZoneHitGeometry());
  HUD->SetDragActive(false);
  const TSharedRef<SWidget> CanvasSlate=Canvas->TakeWidget();
  const auto Window = SNew(SWindow).ClientSize(FVector2D(1500.f, 1000.f));
  FHittestGrid Grid;
  Grid.SetHittestArea(FVector2D::ZeroVector, FVector2D(1500.f, 1000.f));
  FSlateWindowElementList Elements(Window);
  const FPaintArgs PaintArgs(nullptr, Grid, FVector2D::ZeroVector, 0.0, .016f);
  CanvasSlate->SlatePrepass();
  CanvasSlate->Paint(PaintArgs, FGeometry::MakeRoot(FVector2D(900.f,600.f),FSlateLayoutTransform(1.5f,FVector2D(20.f,30.f))),
   FSlateRect(0.f,0.f,1500.f,1000.f),Elements,0,FWidgetStyle(),true);
  TestTrue(TEXT("Collapsed left target has no cached layout on the first drag"), Zones[0]->GetCachedGeometry().GetLocalSize().IsNearlyZero());
  TestTrue(TEXT("Collapsed right target has no cached layout on the first drag"), Zones[1]->GetCachedGeometry().GetLocalSize().IsNearlyZero());
  HUD->SetDragActive(true);
  TestTrue(TEXT("Owned-card drag exposes both sale targets"), Zones[0]->IsVisible() && Zones[1]->IsVisible());
  TestEqual(TEXT("Owned drag enables left target visibility"), Zones[0]->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
  TestEqual(TEXT("Owned drag enables right target visibility"), Zones[1]->GetVisibility(), ESlateVisibility::SelfHitTestInvisible);
  TestTrue(TEXT("Parent canvas provides first-frame target geometry"), HUD->HasSellZoneHitGeometry());
  const FVector2D LeftPoint(92.f,690.f), RightPoint(1298.f,690.f), BoardPoint(695.f,330.f);
  TestEqual(TEXT("First-frame left target matches scaled absolute pointer"), HUD->GetSellZoneAt(LeftPoint), 1);
  TestEqual(TEXT("First-frame right target matches scaled absolute pointer"), HUD->GetSellZoneAt(RightPoint), 2);
  TestEqual(TEXT("First-frame board position is outside both targets"), HUD->GetSellZoneAt(BoardPoint), 0);
  TestFalse(TEXT("Sale targets do not block a board position"), HUD->IsPointBlocked(BoardPoint));
  TestTrue(TEXT("Active left target blocks board input"), HUD->IsPointBlocked(LeftPoint));
  TestTrue(TEXT("Active right target blocks board input"), HUD->IsPointBlocked(RightPoint));
  HUD->SetDragActive(false);
  HUD->ToggleDrawPanel();
  HUD->SetDragActive(true);
  HUD->SetShopContextDragPresentation(nullptr);
  TestTrue(TEXT("Owned-card drag with shop open exposes both sale targets"),Zones[0]->IsVisible()&&Zones[1]->IsVisible());
  TestEqual(TEXT("Shop context retains left sale hit"),HUD->GetSellZoneAt(LeftPoint),1);
  TestEqual(TEXT("Shop context retains right sale hit"),HUD->GetSellZoneAt(RightPoint),2);
  HUD->SetDragActive(false);
  TestTrue(TEXT("Shop remains open after owned drag"),HUD->IsShopExpanded());
  HUD->ToggleDrawPanel();
  // HitTestInvisible is the actual state during the drag-end fade-out.
  Zones[0]->SetVisibility(ESlateVisibility::HitTestInvisible);
  Zones[1]->SetVisibility(ESlateVisibility::HitTestInvisible);
  TestTrue(TEXT("Fade-out targets remain visible"), Zones[0]->IsVisible() && Zones[1]->IsVisible());
  TestFalse(TEXT("Fading left target does not block board input"), HUD->IsPointBlocked(LeftPoint));
  TestFalse(TEXT("Fading right target does not block board input"), HUD->IsPointBlocked(RightPoint));
  HUD->SetDragActive(true);
  HUD->SetShopDragPresentation(0, nullptr);
  TestFalse(TEXT("Shop-offer drag hides the left sale target"), Zones[0]->IsVisible());
  TestFalse(TEXT("Shop-offer drag hides the right sale target"), Zones[1]->IsVisible());
  TestEqual(TEXT("Shop-offer drag cannot hit a sale target"), HUD->GetSellZoneAt(FVector2D::ZeroVector), 0);
  TestFalse(TEXT("Shop-offer card cannot overlap a sale target"), HUD->IsDragCardTouchingSellZone(FVector2D::ZeroVector, FVector2D(280.f, 392.f)));
  HUD->SetDragActive(false);
  TestEqual(TEXT("Finished drag cannot hit a sale target"), HUD->GetSellZoneAt(FVector2D::ZeroVector), 0);
  HUD->SetDragActive(true);
  TestTrue(TEXT("A new owned drag restores both sale targets"), Zones[0]->IsVisible() && Zones[1]->IsVisible());
  HUD->SetDragActive(false);
  auto* LeftLabel = Cast<UTextBlock>(Zones[0]->GetWidgetFromName(TEXT("Text_Sell")));
  auto* RightLabel = Cast<UTextBlock>(Zones[1]->GetWidgetFromName(TEXT("Text_Sell")));
  if (LeftLabel && RightLabel)
  {
   HUD->SetSellZoneSaleValue(7);
   HUD->SetSellZoneFeedback(1);
   TestTrue(TEXT("Left target shows release and actual price"), LeftLabel->GetText().ToString().Contains(TEXT("松开出售")) && LeftLabel->GetText().ToString().Contains(TEXT("+7")));
   TestFalse(TEXT("Right target does not show left hover"), RightLabel->GetText().ToString().Contains(TEXT("松开出售")));
   HUD->SetSellZoneFeedback(2);
   TestTrue(TEXT("Right target shows release and actual price"), RightLabel->GetText().ToString().Contains(TEXT("松开出售")) && RightLabel->GetText().ToString().Contains(TEXT("+7")));
   TestFalse(TEXT("Left target clears hover when pointer leaves"), LeftLabel->GetText().ToString().Contains(TEXT("松开出售")));
   HUD->ResetSellZoneSaleLabels();
  }
  if(TestNotNull(TEXT("Hand panel exists"),HUD->HandPanel.Get()))
  {
   HUD->HandPanel->RemoveFromParent();
   auto* HandSlot=Canvas->AddChildToCanvas(HUD->HandPanel);
   HandSlot->SetPosition(FVector2D(200.f,360.f));HandSlot->SetSize(FVector2D(500.f,200.f));
   HUD->HandPanel->SetVisibility(ESlateVisibility::Visible);
   CanvasSlate->SlatePrepass();
   CanvasSlate->Paint(PaintArgs,FGeometry::MakeRoot(FVector2D(900.f,600.f),FSlateLayoutTransform(1.5f,FVector2D(20.f,30.f))),
    FSlateRect(0.f,0.f,1500.f,1000.f),Elements,0,FWidgetStyle(),true);
   const auto& HandGeometry=HUD->HandPanel->GetCachedGeometry();
   TestFalse(TEXT("Sorting stays sorting within the hand"),HUD->IsOutsideHandReorderArea(HandGeometry.LocalToAbsolute(FVector2D(20.f,100.f))));
   TestFalse(TEXT("Small edge movement keeps sorting"),HUD->IsOutsideHandReorderArea(HandGeometry.LocalToAbsolute(FVector2D(-12.f,100.f))));
   TestTrue(TEXT("Dragging left out of hand can transition to sale"),HUD->IsOutsideHandReorderArea(HandGeometry.LocalToAbsolute(FVector2D(-25.f,100.f))));
   TestTrue(TEXT("Dragging right out of hand can transition to sale"),HUD->IsOutsideHandReorderArea(HandGeometry.LocalToAbsolute(FVector2D(525.f,100.f))));
   TestTrue(TEXT("Dragging above hand can transition to deployment"),HUD->IsOutsideHandReorderArea(HandGeometry.LocalToAbsolute(FVector2D(250.f,-25.f))));
  }
 }
 return true;
}
#endif
