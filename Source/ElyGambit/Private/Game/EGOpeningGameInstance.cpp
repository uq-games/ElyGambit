#include "Game/EGOpeningGameInstance.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/PackageName.h"
#include "MoviePlayer.h"
#include "RHI.h"
#include "Styling/CoreStyle.h"
#include "UObject/UObjectGlobals.h"
#include "Game/EGDemoGameMode.h"
#include "GameFramework/PlayerController.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGOpeningWidget.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
constexpr TCHAR MenuMapName[]=TEXT("L_ElyMainMenu");
constexpr TCHAR BoardMapName[]=TEXT("L_ElysianBoard_7x7");

TSharedRef<SWidget> MakeOpeningTravelCover()
{
 FSlateFontInfo BrandFont=FEGVisualStyle::SerifFont(54);
 BrandFont.LetterSpacing=100;
 return SNew(SOverlay)
  + SOverlay::Slot()
  [
   SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
   .BorderBackgroundColor(FLinearColor(.025f,.012f,.055f,1.f))
  ]
  + SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(96.f,86.f,0.f,0.f)
  [
   SNew(SVerticalBox)
   + SVerticalBox::Slot().AutoHeight()
   [SNew(STextBlock).Text(FText::FromString(TEXT("ELYGAMBIT"))).Font(BrandFont)
    .ColorAndOpacity(FLinearColor(.98f,.91f,.97f,1.f))]
   + SVerticalBox::Slot().AutoHeight().Padding(6.f,2.f,0.f,0.f)
   [SNew(STextBlock).Text(FText::FromString(TEXT("往 世 之 弈"))).Font(FEGVisualStyle::Font(20))
    .ColorAndOpacity(FLinearColor(.91f,.69f,.84f,1.f))]
   + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Left).Padding(6.f,18.f,0.f,0.f)
   [SNew(SBox).WidthOverride(200.f).HeightOverride(1.f)
    [SNew(SBorder).BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
     .BorderBackgroundColor(FLinearColor(.92f,.64f,.83f,.72f))]]
  ]
  + SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.f,0.f,72.f,60.f)
  [
   SNew(STextBlock).Text(FText::FromString(TEXT("正在准备")))
   .Font(FEGVisualStyle::Font(22))
   .ColorAndOpacity(FLinearColor(.98f,.91f,.97f,1.f))
  ];
}
}

void UEGOpeningGameInstance::Init()
{
 Super::Init();
 if(IsRunningDedicatedServer()||IsRunningCommandlet()||GUsingNullRHI||!FApp::CanEverRender())return;
 // The engine broadcasts this contextual delegate before its own PreLoadMap
 // listener. It also lets us reject another world's or PIE instance's travel.
 PreLoadHandle=FCoreUObjectDelegates::PreLoadMapWithContext.AddUObject(this,&ThisClass::BeforeMapLoad);
 PostLoadHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this,&ThisClass::AfterMapLoad);
}

void UEGOpeningGameInstance::BeforeMapLoad(const FWorldContext& Context,const FString& MapName)
{
 if(bOwnsLoadingMovie||Context.OwningGameInstance.Get()!=this||Context.WorldType!=EWorldType::Game||
    IsRunningDedicatedServer()||IsRunningCommandlet()||GUsingNullRHI||!FApp::CanEverRender()||!IsMoviePlayerEnabled())return;
 UWorld* SourceWorld=Context.World();
 if(!IsValid(SourceWorld)||SourceWorld->GetNetMode()==NM_DedicatedServer||
    FPackageName::GetShortName(MapName)!=BoardMapName)return;
 const FString SourceMap=FPackageName::GetShortName(SourceWorld->GetOutermost()->GetName());
 if(SourceMap!=MenuMapName&&SourceMap!=BoardMapName)return;

 IGameMoviePlayer* Player=GetMoviePlayer();
 if(!Player||!Player->IsInitialized()||Player->IsMovieCurrentlyPlaying()||Player->LoadingScreenIsPrepared())return;
 FLoadingScreenAttributes Attributes;
 Attributes.bAutoCompleteWhenLoadingCompletes=true;
 Attributes.bMoviesAreSkippable=false;
 Attributes.bWaitForManualStop=false;
 Attributes.bAllowInEarlyStartup=false;
 Attributes.bAllowEngineTick=false;
 Attributes.WidgetLoadingScreen=MakeOpeningTravelCover();
 Player->SetupLoadingScreen(Attributes);
 // Starting here handles both standard PreLoadMap and the optional blocking
 // proxy mode. The engine's later listener sees an already-running movie;
 // this instance therefore also owns the matching finish call below.
 bOwnsLoadingMovie=Player->PlayMovie();
 if(bOwnsLoadingMovie)UE_LOG(LogTemp,Display,TEXT("OpeningTravel Game cover started source=%s target=%s"),*SourceMap,*MapName);
 if(!bOwnsLoadingMovie)
 {
  if(!Player->IsMovieCurrentlyPlaying())Player->SetupLoadingScreen(FLoadingScreenAttributes());
  UE_LOG(LogTemp,Warning,TEXT("Opening loading movie could not start; continuing map load."));
 }
}

void UEGOpeningGameInstance::AfterMapLoad(UWorld* LoadedWorld)
{
 if(LoadedWorld&&LoadedWorld->GetGameInstance()!=this)return;
 if(bOwnsLoadingMovie)ReleaseOwnedMovie(false);
 if(PIECover.IsValid()&&LoadedWorld&&LoadedWorld!=PIECoverSource.Get()&&LoadedWorld->WorldType==EWorldType::PIE)
 {
  PIECoverTarget=LoadedWorld;PIECoverDeadline=FPlatformTime::Seconds()+15.;
  UE_LOG(LogTemp,Display,TEXT("OpeningTravel PIE target loaded world=%s"),*GetNameSafe(LoadedWorld));
 }
}

bool UEGOpeningGameInstance::BeginPIETravelCover(UWorld* SourceWorld)
{
 if(!IsValid(SourceWorld)||SourceWorld->GetGameInstance()!=this||SourceWorld->WorldType!=EWorldType::PIE||
    SourceWorld->bIsTearingDown||PIECover.IsValid())return false;
 auto* Viewport=SourceWorld->GetGameViewport();if(!IsValid(Viewport))return false;
 PIECover=MakeOpeningTravelCover();PIECoverSource=SourceWorld;PIECoverViewport=Viewport;
 PIECoverDeadline=FPlatformTime::Seconds()+15.;bPIETargetDrawn=false;
 Viewport->AddViewportWidgetContent(PIECover.ToSharedRef(),10000);
 PIECoverTicker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&ThisClass::TickPIECover));
 UE_LOG(LogTemp,Display,TEXT("OpeningTravel PIE cover installed source=%s viewport=%s"),*GetNameSafe(SourceWorld),*GetNameSafe(Viewport));
 return true;
}

void UEGOpeningGameInstance::AbortPIETravelCover(UWorld* ExpectedSource)
{
 if(PIECover.IsValid()&&PIECoverSource.Get()==ExpectedSource&&!PIECoverTarget.IsValid())ReleasePIECover();
}

void UEGOpeningGameInstance::OnPIETargetDrawn()
{
 if(!bPIETargetDrawn&&PIECoverTarget.IsValid()&&PIECoverViewport.IsValid()&&PIECoverViewport->GetWorld()==PIECoverTarget.Get())
 {
  bPIETargetDrawn=true;
  UE_LOG(LogTemp,Display,TEXT("OpeningTravel PIE target UI draw observed world=%s"),*GetNameSafe(PIECoverTarget.Get()));
 }
}

bool UEGOpeningGameInstance::TickPIECover(float)
{
 if(!PIECover.IsValid()||!PIECoverViewport.IsValid()) {ReleasePIECover();return false;}
 if(auto* World=PIECoverTarget.Get())
 {
  if(World->bIsTearingDown){ReleasePIECover();return false;}
  auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  auto* Player=World->GetFirstPlayerController();
  if(!PIETargetWidget.IsValid()&&IsValid(Mode)&&IsValid(Player)&&Player->IsLocalController()&&
     (Mode->GetOpeningToken().IsValid()||Mode->IsOpeningStartupFailure()))
  {
   PIETargetWidget=UEGOpeningWidget::Install(Player);
   if(PIETargetWidget.IsValid())
   {
    UE_LOG(LogTemp,Display,TEXT("OpeningTravel PIE target UI installed world=%s"),*GetNameSafe(World));
    PIETargetDrawHandle=PIECoverViewport->OnDrawn().AddUObject(this,&ThisClass::OnPIETargetDrawn);
   }
  }
  if(PIETargetWidget.IsValid()&&bPIETargetDrawn){ReleasePIECover();return false;}
 }
 if(FPlatformTime::Seconds()>PIECoverDeadline)
 {
  UE_LOG(LogTemp,Warning,TEXT("PIE opening travel cover timed out waiting for the destination UI; releasing it."));
  ReleasePIECover();return false;
 }
 return true;
}

void UEGOpeningGameInstance::ReleasePIECover()
{
 if(PIECover.IsValid())UE_LOG(LogTemp,Display,TEXT("OpeningTravel PIE cover released source=%s target=%s targetDrawn=%d"),
  *GetNameSafe(PIECoverSource.Get()),*GetNameSafe(PIECoverTarget.Get()),bPIETargetDrawn?1:0);
 if(PIECoverViewport.IsValid())
 {
  if(PIETargetDrawHandle.IsValid())PIECoverViewport->OnDrawn().Remove(PIETargetDrawHandle);
  if(PIECover.IsValid())PIECoverViewport->RemoveViewportWidgetContent(PIECover.ToSharedRef());
 }
 if(PIECoverTicker.IsValid())FTSTicker::GetCoreTicker().RemoveTicker(PIECoverTicker);
 PIECoverTicker.Reset();PIETargetDrawHandle.Reset();PIECover.Reset();PIETargetWidget.Reset();
 PIECoverSource.Reset();PIECoverTarget.Reset();PIECoverViewport.Reset();PIECoverDeadline=0.;bPIETargetDrawn=false;
}

bool UEGOpeningGameInstance::IsOpeningLoadingMoviePlaying() const
{
 if(!bOwnsLoadingMovie||GUsingNullRHI||!IsMoviePlayerEnabled())return false;
 const IGameMoviePlayer* Player=GetMoviePlayer();
 return Player&&Player->IsInitialized()&&Player->IsMovieCurrentlyPlaying();
}

void UEGOpeningGameInstance::ReleaseOwnedMovie(bool bFinishImmediately)
{
 if(!bOwnsLoadingMovie)return;
 bOwnsLoadingMovie=false;
 UE_LOG(LogTemp,Display,TEXT("OpeningTravel Game cover released immediate=%d"),bFinishImmediately?1:0);
 IGameMoviePlayer* Player=GetMoviePlayer();
 if(!Player||!Player->IsInitialized()||!Player->IsMovieCurrentlyPlaying())return;
 if(bFinishImmediately)Player->StopMovie();
 // Auto-complete is enabled and manual wait is disabled; this cleans up the
 // loading thread without waiting for the loop to end.
 Player->WaitForMovieToFinish(false);
}

void UEGOpeningGameInstance::Shutdown()
{
 FCoreUObjectDelegates::PreLoadMapWithContext.Remove(PreLoadHandle);
 FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
 PreLoadHandle.Reset();PostLoadHandle.Reset();
 ReleasePIECover();
 ReleaseOwnedMovie(true);
 Super::Shutdown();
}
