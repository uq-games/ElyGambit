#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Engine/GameInstance.h"
#include "EGOpeningGameInstance.generated.h"

struct FWorldContext;
class UGameViewportClient;
class UEGOpeningWidget;
class SWidget;

/** Owns the opening travel cover across the synchronous board-map load. */
UCLASS()
class ELYGAMBIT_API UEGOpeningGameInstance : public UGameInstance
{
 GENERATED_BODY()
public:
 virtual void Init() override;
 virtual void Shutdown() override;
 /** True only while this instance owns the movie for its current map travel. */
 bool IsOpeningLoadingMoviePlaying() const;
 /** PIE cannot use the global game MoviePlayer; cover its own viewport first. */
 bool BeginPIETravelCover(UWorld* SourceWorld);
 void AbortPIETravelCover(UWorld* ExpectedSource);

private:
 void BeforeMapLoad(const FWorldContext& Context,const FString& MapName);
 void AfterMapLoad(UWorld* LoadedWorld);
 void ReleaseOwnedMovie(bool bFinishImmediately);
 bool TickPIECover(float DeltaSeconds);
 void OnPIETargetDrawn();
 void ReleasePIECover();

 FDelegateHandle PreLoadHandle;
 FDelegateHandle PostLoadHandle;
 bool bOwnsLoadingMovie=false;
 TWeakObjectPtr<UWorld> PIECoverSource;
 TWeakObjectPtr<UWorld> PIECoverTarget;
 TWeakObjectPtr<UGameViewportClient> PIECoverViewport;
 TWeakObjectPtr<UEGOpeningWidget> PIETargetWidget;
 TSharedPtr<SWidget> PIECover;
 FTSTicker::FDelegateHandle PIECoverTicker;
 FDelegateHandle PIETargetDrawHandle;
 double PIECoverDeadline=0.;
 bool bPIETargetDrawn=false;
};
