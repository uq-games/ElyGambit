#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Features/IModularFeatures.h"
#include "UObject/SoftObjectPath.h"
#include "UI/EGWidgetAuthoringService.h"
#include "UI/EGWidgetEditorLibrary.h"
#include "Presentation/EGAnimationAuthoringLibrary.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEditorServicesTest,"ElyGambit.Editor.AuthoringBridge",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEditorServicesTest::RunTest(const FString&)
{
 TestTrue(TEXT("Editor service registered"),IModularFeatures::Get().IsModularFeatureAvailable(IEGWidgetAuthoringService::FeatureName()));
 TestEqual(TEXT("Original UI reflection path retained"),UEGWidgetEditorLibrary::StaticClass()->GetPathName(),FString(TEXT("/Script/ElyGambit.EGWidgetEditorLibrary")));
 TestEqual(TEXT("Authoring class moved into editor module"),UEGAnimationAuthoringLibrary::StaticClass()->GetPathName(),FString(TEXT("/Script/ElyGambitEditor.EGAnimationAuthoringLibrary")));
 for(const TCHAR* Name:{TEXT("EGAnimationAuthoringLibrary"),TEXT("EGAnimationEditLibrary"),TEXT("EGAnimationRetargetLibrary"),TEXT("EGCombatAuthoringLibrary"),TEXT("EGContentMaintenanceLibrary"),TEXT("EGElysiaNativeAuthoringLibrary"),TEXT("EGSourceMeshColorLibrary")})
 {
  const FString CurrentPath=FString(TEXT("/Script/ElyGambitEditor."))+Name;
  FSoftObjectPath EditorPath(CurrentPath);
  auto* EditorClass=Cast<UClass>(EditorPath.TryLoad());
  if(TestNotNull(FString(TEXT("Current editor authoring class loads: "))+Name,EditorClass))
   TestEqual(TEXT("Authoring class uses its current module"),EditorClass->GetPathName(),CurrentPath);
 }
 TestNotNull(TEXT("Stable UI authoring reflection entry"),UEGWidgetEditorLibrary::StaticClass()->FindFunctionByName(TEXT("BuildWidgetAssets")));
 TestNotNull(TEXT("Stable animation authoring reflection entry"),UEGAnimationAuthoringLibrary::StaticClass()->FindFunctionByName(TEXT("CreateSourceAnimation")));
 TestFalse(TEXT("Empty migration rejected by editor service"),UEGWidgetEditorLibrary::RenamePlannedAssets({},{}));
 return true;
}
#endif
