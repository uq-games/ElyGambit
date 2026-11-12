#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Framework/Text/RichTextMarkupProcessing.h"
#include "UI/Widget/EGRichText.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/TextBlock.h"

namespace
{
class FTestNumberDecorator : public FEGNumberHighlightDecorator
{
public:
 explicit FTestNumberDecorator(URichTextBlock* Owner):FEGNumberHighlightDecorator(Owner){}
 using FEGNumberHighlightDecorator::CreateDecoratorText;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGRichNumberTextTest,"ElyGambit.UI.RichText.NumberContent",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGRichNumberTextTest::RunTest(const FString&)
{
 const FString Source=TEXT("伤害150%/200%，持续1.5秒；<num>2</> & 3.");
 const FString Markup=UEGRichTextBlock::BuildHighlightedMarkup(Source);
 TArray<FTextLineParseResults> Lines;
 FString Parsed;
 FDefaultRichTextMarkupParser::GetStaticInstance()->Process(Lines,Markup,Parsed);
 FString Visible;
 int32 NumberRuns=0;
 auto* Owner=NewObject<UEGRichTextBlock>();
 FTestNumberDecorator Decorator(Owner);
 FTextBlockStyle Style;
 for(const FTextLineParseResults& Line:Lines)
 {
  for(const FTextRunParseResults& Run:Line.Runs)
  {
   if(Run.Name==TEXT("num"))
   {
    ++NumberRuns;
    const FString Content=Parsed.Mid(Run.ContentRange.BeginIndex,Run.ContentRange.Len());
    Decorator.CreateDecoratorText(FTextRunInfo(Run.Name,FText::FromString(Content)),Style,Visible);
   }
   // Slate's marshaller uses OriginalRange for undecorated text. Such runs have
   // no ContentRange, so reading that field would drop all ordinary text here.
   else Visible+=Parsed.Mid(Run.OriginalRange.BeginIndex,Run.OriginalRange.Len());
  }
 }
 TestEqual(TEXT("Rendered runs preserve the original text and escaped markup"),Visible,Source);
 TestEqual(TEXT("Integers, decimals and percentages are decorated separately"),NumberRuns,5);
 TestTrue(TEXT("Decimal and percent form complete numeric runs"),Markup.Contains(TEXT("<num>1.5</>"))&&Markup.Contains(TEXT("<num>150%</>")));
 TestTrue(TEXT("User markup is escaped"),Markup.Contains(TEXT("&lt;num&gt;")));
 FString Scratch;
 FTextBlockStyle DarkBody;DarkBody.ColorAndOpacity=FSlateColor(FLinearColor(.05f,.04f,.05f));
 Decorator.CreateDecoratorText(FTextRunInfo(TEXT("num"),FText::FromString(TEXT("1"))),DarkBody,Scratch);
 FTextBlockStyle LightBody;LightBody.ColorAndOpacity=FSlateColor(FLinearColor(.95f,.95f,.95f));
 Decorator.CreateDecoratorText(FTextRunInfo(TEXT("num"),FText::FromString(TEXT("1"))),LightBody,Scratch);
 TestTrue(TEXT("Dark body uses a dark accent for a light surface"),DarkBody.ColorAndOpacity.GetSpecifiedColor().GetLuminance()<.25f);
 TestTrue(TEXT("Light body uses a light accent for a dark surface"),LightBody.ColorAndOpacity.GetSpecifiedColor().GetLuminance()>.5f);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGRichReplaceOrderTest,"ElyGambit.UI.RichText.SafeReplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGRichReplaceOrderTest::RunTest(const FString&)
{
 auto* Tree=NewObject<UWidgetTree>();
 auto* Parent=Tree->ConstructWidget<UCanvasPanel>();Tree->RootWidget=Parent;
 auto* Before=Tree->ConstructWidget<UTextBlock>();Parent->AddChild(Before);
 auto* Original=Tree->ConstructWidget<UTextBlock>();Parent->AddChild(Original);
 auto* After=Tree->ConstructWidget<UTextBlock>();Parent->AddChild(After);
 const TSharedRef<SWidget> LiveParent=Parent->TakeWidget();
 TestNull(TEXT("Live Slate container is left untouched"),EGReplaceTextBlockWithRichText(Tree,Original));
 TestTrue(TEXT("Original children and order remain"),Parent->GetChildrenCount()==3&&Parent->GetChildAt(0)==Before&&Parent->GetChildAt(1)==Original&&Parent->GetChildAt(2)==After);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSkillLevelTextTest,"ElyGambit.UI.RichText.SkillLevelStates",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSkillLevelTextTest::RunTest(const FString&)
{
 const FString Text=TEXT("基础效果\n\n6级\n护盾30%\n\n11级\n攻击20%");
 for(int32 Level:{1,5,6,10,11})
 {
  const FString Markup=UEGRichTextBlock::BuildSkillLevelMarkup(Text,Level);
  TestTrue(TEXT("Six unlocks independently"),Markup.Contains(Level>=6?TEXT("<skillready>护盾30%</>"):TEXT("<skilllocked>护盾30%</>")));
  TestTrue(TEXT("Eleven unlocks independently"),Markup.Contains(Level>=11?TEXT("<skillready>攻击20%</>"):TEXT("<skilllocked>攻击20%</>")));
 }
 auto* Owner=NewObject<UEGRichTextBlock>();Owner->SetSkillLevelText(FText::FromString(Text),5);Owner->SetSkillLevelText(FText::FromString(Text),11);
 TestTrue(TEXT("Unchanged prose refreshes on level change"),Owner->GetText().ToString().Contains(TEXT("<skillready>护盾30%</>"))&&Owner->GetText().ToString().Contains(TEXT("<skillready>攻击20%</>")));
 TestEqual(TEXT("Plain text remains free of display markup"),Owner->GetPlainText().ToString(),Text);
 FTestNumberDecorator Decorator(Owner);FTextBlockStyle Locked,Ready;Locked.Font.Size=Ready.Font.Size=22;FString Rendered;
 Decorator.CreateDecoratorText(FTextRunInfo(TEXT("skilllocked"),FText::FromString(TEXT("未解锁"))),Locked,Rendered);
 Decorator.CreateDecoratorText(FTextRunInfo(TEXT("skillready"),FText::FromString(TEXT("已解锁"))),Ready,Rendered);
 TestEqual(TEXT("Locked body remains legible at 20"),Locked.Font.Size,20.f);
 TestEqual(TEXT("Unlocked body retains 22"),Ready.Font.Size,22.f);
 TestTrue(TEXT("Locked text stays opaque and readable"),Locked.ColorAndOpacity.GetSpecifiedColor().A==1.f&&Locked.ColorAndOpacity.GetSpecifiedColor().GetLuminance()>.5f);
 return true;
}
#endif
