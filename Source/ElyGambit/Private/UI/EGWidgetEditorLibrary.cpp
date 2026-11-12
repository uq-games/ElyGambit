#include "UI/EGWidgetEditorLibrary.h"
#include "UI/EGWidgetAuthoringService.h"
#include "Features/IModularFeatures.h"
#include "Game/EGCardManager.h"
#include "Actor/EGArtPiece.h"
#include "Data/EGHeroDefinition.h"
#include "Game/EGChessGameMode.h"
#include "Components/EGBattleContextComponent.h"
#include "Blueprint/UserWidget.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"
#include "UnrealClient.h"
#include "Slate/SceneViewport.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#if WITH_EDITOR
#include "Editor.h"
#include "PlayInEditorDataTypes.h"
#endif
namespace
{
 IEGWidgetAuthoringService* FindWidgetAuthoringService()
 {
  auto& Features=IModularFeatures::Get();
  if(!Features.IsModularFeatureAvailable(IEGWidgetAuthoringService::FeatureName()))
  { UE_LOG(LogTemp,Warning,TEXT("EG widget authoring service is unavailable")); return nullptr; }
  return &Features.GetModularFeature<IEGWidgetAuthoringService>(IEGWidgetAuthoringService::FeatureName());
 }
}
bool UEGWidgetEditorLibrary::RenamePlannedAssets(const TArray<UObject*>& Assets,const TArray<FString>& Destinations)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->RenamePlannedAssets(Assets,Destinations); }
bool UEGWidgetEditorLibrary::MigrateWidgetNames(UObject* Object,const TMap<FName,FString>& Names)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->MigrateWidgetNames(Object,Names); }
bool UEGWidgetEditorLibrary::BuildBoardUIAssets()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->BuildBoardUIAssets(); }
bool UEGWidgetEditorLibrary::BuildWidgetAssets(bool bRebuild,bool bSaveAssets)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->BuildWidgetAssets(bRebuild,bSaveAssets); }
bool UEGWidgetEditorLibrary::ApplyBattleUIAssets(bool bSaveAssets)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyBattleUIAssets(bSaveAssets); }
bool UEGWidgetEditorLibrary::ApplyUnitVitalsAssets()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyUnitVitalsAssets(); }
bool UEGWidgetEditorLibrary::ApplyShopMotionAssets()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyShopMotionAssets(); }
bool UEGWidgetEditorLibrary::ApplyBoardCompositionAssets()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyBoardCompositionAssets(); }
bool UEGWidgetEditorLibrary::BuildRunPanelAsset(bool bSaveAssets)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->BuildRunPanelAsset(bSaveAssets); }
bool UEGWidgetEditorLibrary::ApplyCardInformationViews()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyCardInformationViews(); }
bool UEGWidgetEditorLibrary::RebuildPinnedUnitInformation()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->RebuildPinnedUnitInformation(); }
bool UEGWidgetEditorLibrary::ApplyGeneratedBoardControls()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyGeneratedBoardControls(); }
bool UEGWidgetEditorLibrary::RenderGeneratedControlSheet(const FString& Filename,FVector2D Size,bool bShopOpen)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->RenderGeneratedControlSheet(Filename,Size,bShopOpen); }
bool UEGWidgetEditorLibrary::RemoveCompanionCatalogue()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->RemoveCompanionCatalogue(); }
bool UEGWidgetEditorLibrary::ApplyOfficialCardAssets(const TMap<FName,UTexture2D*>& Artwork,const TArray<UEGHeroDefinition*>& Companions)
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyOfficialCardAssets(Artwork,Companions); }
bool UEGWidgetEditorLibrary::ApplyReferenceShopUI()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyReferenceShopUI(); }
bool UEGWidgetEditorLibrary::ApplyShopDeploymentAssets()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyShopDeploymentAssets(); }
bool UEGWidgetEditorLibrary::ApplyShopPriceBadge()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyShopPriceBadge(); }
bool UEGWidgetEditorLibrary::ApplyUIExperienceAssets()
{ auto* Service=FindWidgetAuthoringService(); return Service && Service->ApplyUIExperienceAssets(); }

FVector2D UEGWidgetEditorLibrary::WidgetScreenPosition(UWidget* Widget,FVector2D Normalized)
{
    if(!IsValid(Widget))return FVector2D::ZeroVector;
    const auto& Geometry=Widget->GetCachedGeometry();
    return Geometry.LocalToAbsolute(Geometry.GetLocalSize()*Normalized);
}
FVector2D UEGWidgetEditorLibrary::WidgetLocalSize(UWidget* Widget)
{return IsValid(Widget)?Widget->GetCachedGeometry().GetLocalSize():FVector2D::ZeroVector;}
FVector2D UEGWidgetEditorLibrary::WorldScreenPosition(APlayerController* PC,FVector Position)
{
    if(!IsValid(PC)||!PC->GetWorld()->GetGameViewport())return FVector2D::ZeroVector;
    const auto View=PC->GetWorld()->GetGameViewport()->GetGameViewportWidget();
    if(!View.IsValid())return FVector2D::ZeroVector;
    FVector2D Pixels;int32 W,H;PC->GetViewportSize(W,H);
    if(W<=0||H<=0||!PC->ProjectWorldLocationToScreen(Position,Pixels))return FVector2D::ZeroVector;
    const auto& Geometry=View->GetCachedGeometry();
    return Geometry.LocalToAbsolute(Pixels*Geometry.GetLocalSize()/FVector2D(W,H));
}

bool UEGWidgetEditorLibrary::CapturePIE(const FString& Filename)
{
#if WITH_EDITOR
    if(!GEditor||!GEditor->PlayWorld||!GEditor->PlayWorld->GetGameViewport())return false;
    // A global FScreenshotRequest can be consumed by an editor viewport first.
    // Read only this PIE viewport widget, including its UMG overlay, synchronously.
    const TSharedPtr<SViewport> Viewport = GEditor->PlayWorld->GetGameViewport()->GetGameViewportWidget();
    if (!Viewport.IsValid()) return false;
    TArray<FColor> Pixels;
    FIntVector Size;
    if (!FSlateApplication::Get().TakeScreenshot(Viewport.ToSharedRef(), Pixels, Size)
        || Size.X <= 0 || Size.Y <= 0 || Pixels.Num() != static_cast<int64>(Size.X) * Size.Y) return false;
    for (FColor& Pixel : Pixels) Pixel.A = 255;
    TArray64<uint8> Compressed;
    FImageUtils::PNGCompressImageArray(Size.X, Size.Y, MakeArrayView(Pixels), Compressed);
    return !Compressed.IsEmpty() && FFileHelper::SaveArrayToFile(Compressed, *Filename);
#else
    return false;
#endif
}
bool UEGWidgetEditorLibrary::StartPIEWindow()
{
#if WITH_EDITOR
    if(!GEditor||GEditor->PlayWorld)return false;
    FRequestPlaySessionParams Params;Params.WorldType=EPlaySessionWorldType::PlayInEditor;
    GEditor->RequestPlaySession(Params);return true;
#else
    return false;
#endif
}
AEGArtPiece* UEGWidgetEditorLibrary::SpawnPresentationProbe(UEGHeroDefinition* Definition,FVector Position,FRotator Rotation)
{
#if WITH_EDITOR
    if(!GEditor||!GEditor->PlayWorld||!IsValid(Definition)||Position.ContainsNaN()||Rotation.ContainsNaN())return nullptr;
    FActorSpawnParameters Params;Params.Owner=GEditor->PlayWorld->GetFirstPlayerController();Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    auto* Actor=GEditor->PlayWorld->SpawnActor<AEGArtPiece>(AEGArtPiece::StaticClass(),Position,Rotation,Params);
    if(Actor){Actor->Tags.Add(TEXT("BoardVisualProbe"));Actor->ApplyHeroDefinition(Definition);}
    return Actor;
#else
    return nullptr;
#endif
}
bool UEGWidgetEditorLibrary::TestPointer(FVector2D Position,FName Action)
{
#if WITH_EDITOR
    if(!GEditor||!GEditor->PlayWorld||!FSlateApplication::IsInitialized())return false;
    auto& App=FSlateApplication::Get();static TSet<FKey> Buttons;
    const FVector2D Last=App.GetCursorPos();
    App.SetCursorPos(Position);
    const bool bDown=Action==TEXT("Down")||Action==TEXT("RightDown");
    const bool bUp=Action==TEXT("Up")||Action==TEXT("RightUp");
    const FKey ChangedButton=Action==TEXT("RightDown")||Action==TEXT("RightUp")?EKeys::RightMouseButton:EKeys::LeftMouseButton;
    if(bDown)Buttons.Add(ChangedButton);
    if(bUp)Buttons.Remove(ChangedButton);
    const float Wheel=Action==TEXT("WheelUp")?1.f:Action==TEXT("WheelDown")?-1.f:0.f;
    const FPointerEvent Event(0,Position,Last,Buttons,Action==TEXT("Move")||Wheel!=0?FKey():ChangedButton,Wheel,FModifierKeysState());
    if(Wheel!=0)return App.ProcessMouseWheelOrGestureEvent(Event,nullptr);
    if(bDown)
    {
        return App.ProcessMouseButtonDownEvent(nullptr,Event);
    }
    if(bUp)return App.ProcessMouseButtonUpEvent(Event);
    return App.ProcessMouseMoveEvent(Event);
#else
    return false;
#endif
}
bool UEGWidgetEditorLibrary::TestApplicationActivation(bool bActive)
{
#if WITH_EDITOR
    if(!GEditor||!GEditor->PlayWorld||!FSlateApplication::IsInitialized())return false;
    if(bActive)
    {
        const auto Window=GEditor->PlayWorld->GetGameViewport()->GetWindow();
        if(Window.IsValid())Window->BringToFront(true);
    }
    return FSlateApplication::Get().OnApplicationActivationChanged(bActive);
#else
    return false;
#endif
}
bool UEGWidgetEditorLibrary::TestKey(FName KeyName)
{
#if WITH_EDITOR
    if(!GEditor||!GEditor->PlayWorld||!FSlateApplication::IsInitialized())return false;
    return FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(FKey(KeyName),FModifierKeysState(),0,false,0,0));
#else
    return false;
#endif
}

#include "Character/EGChessUnit.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "Components/EGHeroSkillComponent.h"
UWidget* UEGWidgetEditorLibrary::FindNamedWidget(UUserWidget* View,FName Name)
{return IsValid(View)?View->GetWidgetFromName(Name):nullptr;}
bool UEGWidgetEditorLibrary::ProbeCombatFeedback(AEGChessUnit* Source,AEGChessUnit* Target,float Damage,float Healing,float Shield)
{
#if WITH_EDITOR
 if(!GEditor||!GEditor->PlayWorld||!IsValid(Source)||!IsValid(Target)||Source->GetWorld()!=GEditor->PlayWorld||Target->GetWorld()!=GEditor->PlayWorld||!FMath::IsFinite(Damage)||!FMath::IsFinite(Healing)||!FMath::IsFinite(Shield)||Damage<0||Healing<0||Shield<0)return false;
 Target->GetHeroSkill()->SetShield(Shield);
 bool Result=true;
 if(Damage>0)Result=FEGCombatExecution::Damage(Source,Target,Damage+Target->GetAttributes()->GetDefense(),0,1,FGameplayEffectContextHandle());
 if(Healing>0)Result=FEGCombatExecution::Heal(Target,Healing)&&Result;
 return Result;
#else
 return false;
#endif
}
bool UEGWidgetEditorLibrary::SetCombatProbeAttributes(AEGChessUnit* Unit,float MaxHealth,float Health,float Mana)
{
#if WITH_EDITOR
 if(!IsValid(Unit)||!Unit->HasAuthority()||!Unit->GetWorld()->IsGameWorld()||!FMath::IsFinite(MaxHealth)||!FMath::IsFinite(Health)||!FMath::IsFinite(Mana))return false;
 auto* ASC=Unit->GetAbilitySystemComponent();
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),FMath::Max(1.f,MaxHealth));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),FMath::Clamp(Health,0.f,MaxHealth));
 ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),FMath::Clamp(Mana,0.f,Unit->GetAttributes()->GetMaxMana()));return true;
#else
 return false;
#endif
}

FVector2D UEGWidgetEditorLibrary::SetProbeRenderSize(int32 Width,int32 Height)
{
#if WITH_EDITOR
 if(!GEditor||!GEditor->PlayWorld||Width<64||Height<64||Width>4096||Height>4096)return FVector2D::ZeroVector;
 if(auto* Client=GEditor->PlayWorld->GetGameViewport())if(auto* View=Client->GetGameViewport())
 {View->SetFixedViewportSize(Width,Height);return FVector2D(View->GetSizeXY());}
#endif
 return FVector2D::ZeroVector;
}
FVector2D UEGWidgetEditorLibrary::GetProbeRenderSize()
{
#if WITH_EDITOR
 if(GEditor&&GEditor->PlayWorld)if(auto* Client=GEditor->PlayWorld->GetGameViewport())if(auto* View=Client->GetGameViewport())return FVector2D(View->GetSizeXY());
#endif
 return FVector2D::ZeroVector;
}
bool UEGWidgetEditorLibrary::CaptureProbeRender(const FString& Filename)
{
#if WITH_EDITOR
 if(!GEditor||!GEditor->PlayWorld)return false;
 auto* Client=GEditor->PlayWorld->GetGameViewport();auto* View=Client?Client->GetGameViewport():nullptr;if(!View)return false;
 const FIntPoint Size=View->GetSizeXY();TArray<FColor> Pixels;
 if(Size.X<=0||Size.Y<=0||!View->ReadPixels(Pixels)||Pixels.Num()!=static_cast<int64>(Size.X)*Size.Y)return false;
 for(auto& Pixel:Pixels)Pixel.A=255;
 TArray64<uint8> Compressed;FImageUtils::PNGCompressImageArray(Size.X,Size.Y,MakeArrayView(Pixels),Compressed);
 return !Compressed.IsEmpty()&&FFileHelper::SaveArrayToFile(Compressed,*Filename);
#else
 return false;
#endif
}
bool UEGWidgetEditorLibrary::SetProbePopulationFloor(AEGCardManager* Cards,int32 Count)
{
#if WITH_EDITOR
 if(!GEditor||!GEditor->PlayWorld||!IsValid(Cards)||Cards->GetWorld()!=GEditor->PlayWorld||!Cards->HasAuthority()||Count<1||Count>21)return false;
 Cards->SetPopulationFloor(Count);return true;
#else
 return false;
#endif
}
AEGChessUnit* UEGWidgetEditorLibrary::SpawnCombatProbeDuplicate(AEGChessUnit* Source,FVector Point)
{
#if WITH_EDITOR
 if(!IsValid(Source)||!Source->HasAuthority()||!Source->GetWorld()->IsGameWorld()||Point.ContainsNaN())return nullptr;
 auto* GM=Source->GetWorld()->GetAuthGameMode<AEGChessGameMode>();auto* Battle=Source->GetHeroSkill()->Battle.Get();
 if(!GM||!Battle||!Battle->IsBattleActive())return nullptr;
 auto* Unit=GM->SpawnBattleSummon(Source->GetDefinition(),Source->GetHeroSkill()->GetLevel(),Point);
 if(Unit){Unit->GetHeroSkill()->bSummon=false;Unit->GetHeroSkill()->Configure(Battle,Source->GetHeroSkill()->GetLevel(),false);Battle->EnterUnit(Unit);}return Unit;
#else
 return nullptr;
#endif
}
