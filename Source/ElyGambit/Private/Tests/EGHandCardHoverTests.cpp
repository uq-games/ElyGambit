#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "UI/Widget/EGWidgets.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHandCardHoverPaintTest,"ElyGambit.UI.HandCard.HoverPaintAndInput",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHandCardHoverPaintTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* World=GI->GetWorld();
 ON_SCOPE_EXIT{World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
 auto* HUDClass=LoadClass<UEGMainHUD>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_BoardHUD.WBP_BoardHUD_C"));
 if(!TestNotNull(TEXT("Production HUD class"),HUDClass))return false;
 const auto CardClass=HUDClass->GetDefaultObject<UEGMainHUD>()->CardWidgetClass;
 if(!TestNotNull(TEXT("Production hand card class"),CardClass.Get()))return false;
 auto* Card=CreateWidget<UEGHandCard>(GI.Get(),CardClass);
 if(!TestNotNull(TEXT("Hand card"),Card))return false;
 TStrongObjectPtr<UEGHandCard> KeepCard(Card);
 const TSharedRef<SWidget> KeepSlate=Card->TakeWidget();
 auto* PaintRoot=Card->WidgetTree?Cast<USizeBox>(Card->WidgetTree->RootWidget):nullptr;
 if(!TestNotNull(TEXT("Runtime card paint root"),PaintRoot))return false;
 Card->SetVisibility(ESlateVisibility::Visible);Card->SetRenderOpacity(.47f);
 PaintRoot->SetRenderOpacity(.63f);
 Card->SetHoverSourceHidden(true);
 TestEqual(TEXT("Original card paint is hidden"),PaintRoot->GetRenderOpacity(),0.f);
 TestEqual(TEXT("Card remains hit-test visible"),Card->GetVisibility(),ESlateVisibility::Visible);
 TestEqual(TEXT("Outer animation opacity is preserved"),Card->GetRenderOpacity(),.47f);
 Card->SetHoverSourceHidden(true);
 Card->SetHoverSourceHidden(false);
 TestEqual(TEXT("Original paint opacity is restored after repeated updates"),PaintRoot->GetRenderOpacity(),.63f);
 TestEqual(TEXT("Restoring paint leaves outer opacity unchanged"),Card->GetRenderOpacity(),.47f);
 Card->SetHoverSourceHidden(true);Card->SetHoverSourceHidden(false);
 TestEqual(TEXT("Another hover restores the same paint opacity"),PaintRoot->GetRenderOpacity(),.63f);
 return true;
}
#endif
