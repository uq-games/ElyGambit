#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/EGCardPresentation.h"
#include "Data/EGHeroDefinition.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDynamicCardLayersTest,"ElyGambit.UI.DefinitionCardLayers",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDynamicCardLayersTest::RunTest(const FString&)
{
 TStrongObjectPtr<UWidgetTree> Tree(NewObject<UWidgetTree>());
 TStrongObjectPtr<UEGHeroDefinition> Hero(NewObject<UEGHeroDefinition>());
 TStrongObjectPtr<UTexture2D> Texture(NewObject<UTexture2D>());
 Hero->DisplayName=FText::FromString(TEXT("真我·人之律者"));Hero->CardFrameTexture=Texture.Get();
 auto* Root=Tree->ConstructWidget<UCanvasPanel>();Tree->RootWidget=Root;
 auto* Portrait=Tree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("CardImage"));
 auto* Original=Root->AddChildToCanvas(Portrait);Original->SetPosition({12,40});Original->SetSize({300,420});
 FEGCardPresentation::ApplyDefinitionLayers(Tree.Get(),Portrait,Hero.Get(),7);
 auto* Frame=Cast<UImage>(Tree->FindWidget(TEXT("CardImage_DefinitionFrame")));
 auto* Name=Cast<UTextBlock>(Tree->FindWidget(TEXT("CardImage_DefinitionName")));
 auto* Level=Cast<UTextBlock>(Tree->FindWidget(TEXT("CardImage_DefinitionLevel")));
 if(!TestNotNull(TEXT("Separate frame is created"),Frame)||!TestNotNull(TEXT("Dynamic name is created"),Name)||!TestNotNull(TEXT("Dynamic level is created"),Level))return false;
 TestTrue(TEXT("Frame references definition asset"),Frame->GetBrush().GetResourceObject()==Texture.Get());
 TestEqual(TEXT("Name is live definition text"),Name->GetText().ToString(),FString(TEXT("真我·人之律者")));
 TestEqual(TEXT("Level is live instance value"),Level->GetText().ToString(),FString(TEXT("Lv.7")));
 auto* Layer=Cast<UCanvasPanel>(Portrait->GetParent());
 auto* LayerSlot=Layer?Cast<UCanvasPanelSlot>(Layer->Slot):nullptr;
 if(TestNotNull(TEXT("Layer preserves outer canvas slot"),LayerSlot))
 {TestEqual(TEXT("Original position retained"),LayerSlot->GetPosition(),FVector2D(12,40));TestEqual(TEXT("Original size retained"),LayerSlot->GetSize(),FVector2D(300,420));}
 FEGCardPresentation::ApplyDefinitionLayers(Tree.Get(),Portrait,nullptr,0);
 TestEqual(TEXT("Unrelated card hides frame"),Frame->GetVisibility(),ESlateVisibility::Collapsed);
 TestEqual(TEXT("Unrelated card hides dynamic name"),Name->GetParent()->GetVisibility(),ESlateVisibility::Collapsed);
 FEGCardPresentation::ApplyDefinitionLayers(Tree.Get(),Portrait,Hero.Get(),9);
 TestTrue(TEXT("Reusing a widget reuses its frame"),Tree->FindWidget(TEXT("CardImage_DefinitionFrame"))==Frame);
 TestEqual(TEXT("Updated level is not baked into artwork"),Level->GetText().ToString(),FString(TEXT("Lv.9")));
 return true;
}
#endif
