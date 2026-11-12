#include "Presentation/EGSourceAnimationDataModel.h"

#if WITH_EDITOR
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimSequence.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"

namespace EGSourceAnimationDataModel
{
namespace
{
// Preserve full quaternion samples and special bone names in the new source batch.
class FSourceBoneNameModel final : public UE::Anim::DataModel::IAnimationDataModels
{
public:
	virtual UClass* GetModelClass(UAnimSequenceBase* Animation) const override
	{
		if (Animation && Animation->IsA<UAnimSequence>())
		{
			const FString Path = Animation->GetPathName();
			if (Path.StartsWith(TEXT("/Game/Assets/Characters/")) && !Path.Contains(TEXT("/Dependencies/")) &&
				(Path.Contains(TEXT("/Animations/NormalAttack/")) || Path.Contains(TEXT("/Animations/Ultimate/")) ||
				 Path.Contains(TEXT("/Animations/Combined/NormalAttack/")) || Path.Contains(TEXT("/Animations/Combined/Ultimate/")) ||
				 Path.Contains(TEXT("/Animations/Idle/")) || Path.Contains(TEXT("/Animations/Boss/")) ||
				 Path.Contains(TEXT("/Animations/NativeLocomotion20261024/")) ||
				 Path.StartsWith(TEXT("/Game/Assets/Characters/Elysia/Animations/C1/")) ||
				 Path.StartsWith(TEXT("/Game/Assets/Characters/Elysia/Animations/C1/")) ||
				 Path.StartsWith(TEXT("/Game/Assets/Characters/Griseo/Animations/Combat/")) ||
				 (Path.StartsWith(TEXT("/Game/Assets/Characters/VillV/Animations/")) &&
				  (Path.Contains(TEXT("/Gatling/")) || Path.Contains(TEXT("/Cannon/"))))))
			{
				UE_LOG(LogTemp, Verbose, TEXT("HI3 original bone-name model: %s"), *Path);
				return UAnimDataModel::StaticClass();
			}
		}
		return nullptr;
	}
};

FSourceBoneNameModel SourceBoneNameModel;
bool bRegistered = false;
}

void Register()
{
	if (!bRegistered)
	{
		FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("AnimationData"));
		auto& Features = IModularFeatures::Get();
		const FName FeatureName = UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName();
		const auto Existing = Features.GetModularFeatureImplementations<UE::Anim::DataModel::IAnimationDataModels>(FeatureName);
		// The engine selects the last non-null model in reverse registration order.
		// Preserve the existing providers' relative order and put this narrow override last.
		for (auto* Model : Existing)
		{
			Features.UnregisterModularFeature(FeatureName, Model);
		}
		Features.RegisterModularFeature(FeatureName, &SourceBoneNameModel);
		for (int32 Index = Existing.Num() - 1; Index >= 0; --Index)
		{
			Features.RegisterModularFeature(FeatureName, Existing[Index]);
		}
		bRegistered = true;
		UE_LOG(LogTemp, Display, TEXT("HI3 original bone-name model provider registered"));
	}
}

void Unregister()
{
	if (bRegistered)
	{
		IModularFeatures::Get().UnregisterModularFeature(
			UE::Anim::DataModel::IAnimationDataModels::GetModularFeatureName(), &SourceBoneNameModel);
		bRegistered = false;
	}
}
}
#endif
