#include "Modules/ModuleManager.h"
#include "Features/IModularFeatures.h"
#include "Presentation/EGSourceAnimationDataModel.h"
#include "UI/EGWidgetAuthoringServiceImpl.h"

class FElyGambitEditorModule final : public IModuleInterface
{
 FEGWidgetAuthoringService WidgetAuthoring;
public:
 virtual void StartupModule() override
 {
  EGSourceAnimationDataModel::Register();
  IModularFeatures::Get().RegisterModularFeature(IEGWidgetAuthoringService::FeatureName(),&WidgetAuthoring);
 }
 virtual void ShutdownModule() override
 {
  IModularFeatures::Get().UnregisterModularFeature(IEGWidgetAuthoringService::FeatureName(),&WidgetAuthoring);
  EGSourceAnimationDataModel::Unregister();
 }
};
IMPLEMENT_MODULE(FElyGambitEditorModule,ElyGambitEditor);
