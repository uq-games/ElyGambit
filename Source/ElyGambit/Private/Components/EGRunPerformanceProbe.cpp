#include "Components/EGRunPerformanceProbe.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Character/EGChessUnit.h"
#include "Data/EGCardDefinition.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "HAL/PlatformMemory.h"
#include "HAL/FileManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UnrealClient.h"
#include "Diagnostics/EGResourceAuditLibrary.h"
#include "Placement/EGDeploymentBoard.h"

UEGRunPerformanceProbe::UEGRunPerformanceProbe(){PrimaryComponentTick.bCanEverTick=true;}
void UEGRunPerformanceProbe::TickComponent(float Delta,ELevelTick Type,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Type,Function);
#if !UE_BUILD_SHIPPING
 auto* Mode=Cast<AEGDemoGameMode>(GetOwner());if(!Mode)return;const double Now=FPlatformTime::Seconds();
 if(!bPrepared)
 {
  if(!bScenarioSetup && !Mode->CanPrepare())return;
  if(!bScenarioSetup)
  {
  FParse::Value(FCommandLine::Get(),TEXT("EGProfileOutput="),Folder);
  if(Folder.IsEmpty())Folder=FPaths::ProjectSavedDir()/TEXT("Optimization/StandaloneProfile");
  IFileManager::Get().MakeDirectory(*Folder,true);
  FParse::Value(FCommandLine::Get(),TEXT("EGProfileScenario="),Scenario);
  FParse::Value(FCommandLine::Get(),TEXT("EGProfileQuality="),Quality);
  FParse::Value(FCommandLine::Get(),TEXT("EGProfileSeconds="),Seconds);Seconds=FMath::Clamp(Seconds,5.f,600.f);
  auto* PC=GetWorld()->GetFirstPlayerController();
  if(PC){PC->ConsoleCommand(TEXT("r.VSync 0"));PC->ConsoleCommand(TEXT("t.MaxFPS 0"));}
  if(PC&&Quality==TEXT("performance"))
   for(const TCHAR* Command:{TEXT("sg.ShadowQuality 2"),TEXT("sg.GlobalIlluminationQuality 2"),TEXT("sg.ReflectionQuality 2"),TEXT("sg.PostProcessQuality 2"),TEXT("sg.EffectsQuality 2")})PC->ConsoleCommand(Command);
   bScenarioSetup=true;
  }
  if(Scenario==TEXT("reward") && Mode->GetMatch())
  {
   // The formal empty-board defeat plays settlement and opens the first skip page.
   if(Mode->CanPrepare())Mode->EnterCombat();
   if(Mode->GetMatch()->GetPhase()!=EEGMatchPhase::Reward)return;
  }
  if(Scenario==TEXT("combat"))
  {
   // Profile the opening hand through ordinary deployment. Async loading may span multiple ticks.
   auto* Cards=Mode->GetCards();const auto Hand=Cards->GetHand();
   if(!Hand.IsEmpty())
   {
    const auto Card=Hand[0];
    if(!Card.Definition||!Mode->EnsureHeroResources(Card.Definition->HeroDefinition))return;
    FIntPoint Cell(0,0);bool bFound=false;
    for(int32 Y=0;Y<Cards->Board->PlayerRows&&!bFound;++Y)for(int32 X=0;X<Cards->Board->Columns&&!bFound;++X)
     if(Cards->Board->IsCellAvailable({X,Y})){Cell={X,Y};bFound=true;}
    if(!Cards->TryDeployHandCard(Card.InstanceId,Cell))
    {UE_LOG(LogTemp,Error,TEXT("Performance scenario deployment failed"));SetComponentTickEnabled(false);if(FParse::Param(FCommandLine::Get(),TEXT("EGProfileExit")))FPlatformMisc::RequestExitWithStatus(false,1);}
    return;
   }
   if(!Mode->EnterCombat()){UE_LOG(LogTemp,Error,TEXT("Performance scenario combat failed"));SetComponentTickEnabled(false);if(FParse::Param(FCommandLine::Get(),TEXT("EGProfileExit")))FPlatformMisc::RequestExitWithStatus(false,1);return;}
  }
  StartTime=Now;bPrepared=true;return;
 }
 // Controlled long fight. Changes are transient and do not define natural combat performance.
 if(Scenario==TEXT("combat")&&!bCompleted)
  for(auto* U:Mode->GetCombatUnits())if(U->GetAttributes()->GetMaxHealth()<1000000.f)
  {auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),1000000.f);ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000000.f);}
 if(!bCapturing&&!bCompleted&&Now-StartTime>=8)
 {
  CaptureStart=Now;bCapturing=true;Rows=TEXT("frame,frame_ms,used_physical_bytes,combat_units\n");
  FScreenshotRequest::RequestScreenshot(Folder/TEXT("scene.png"),true,false);
#if CSV_PROFILER
  FCsvProfiler::SetMetadata(TEXT("EGScenario"),*Scenario);FCsvProfiler::SetMetadata(TEXT("EGQuality"),*Quality);
  FCsvProfiler::Get()->BeginCapture(-1,Folder,TEXT("engine.csv"));
#endif
 }
 if(bCapturing)
 {
  if(Frame==0||Now-LastMemorySample>=1.0){LastMemorySample=Now;Memory=FPlatformMemory::GetStats().UsedPhysical;UEGResourceAuditLibrary::CaptureMemorySample(Scenario,Frame==0);}
  Rows+=FString::Printf(TEXT("%d,%.6f,%llu,%d\n"),Frame++,FApp::GetDeltaTime()*1000,Memory,Mode->GetCombatUnits().Num());
  if(Now-CaptureStart>=Seconds)
  {
   FFileHelper::SaveStringToFile(Rows,*(Folder/TEXT("frames.csv")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
#if CSV_PROFILER
   FCsvProfiler::Get()->EndCapture();
#endif
   bCapturing=false;bCompleted=true;CompletedTime=Now;
  }
 }
 if(bCompleted&&Now-CompletedTime>5)
 {SetComponentTickEnabled(false);if(FParse::Param(FCommandLine::Get(),TEXT("EGProfileExit")))FPlatformMisc::RequestExit(false);}
#endif
}
void UEGRunPerformanceProbe::EndPlay(const EEndPlayReason::Type Reason)
{
#if CSV_PROFILER
 if(bCapturing)FCsvProfiler::Get()->EndCapture();
#endif
 Super::EndPlay(Reason);
}
