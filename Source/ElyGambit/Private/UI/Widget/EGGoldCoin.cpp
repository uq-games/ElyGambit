#include "UI/Widget/EGGoldCoin.h"
#include "Engine/Texture2D.h"

TSharedRef<SWidget> UEGGoldCoin::RebuildWidget()
{
 // Keep existing UImage bindings. The brush owns the texture for GC and the
 // saved Widget Blueprint brush reference includes the source icon in cooking.
 if(auto* Texture=LoadObject<UTexture2D>(nullptr,TEXT("/Game/Assets/UI/Textures/UIExperience20261026/T_HonkaiEnergy")))
 {
  SetBrushFromTexture(Texture,false);
  SetBrushSize(FVector2D(24,24));
  SetColorAndOpacity(FLinearColor::White);
 }
 return Super::RebuildWidget();
}
