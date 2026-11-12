#include "Components/EGRunResourceComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGPlacementVisuals.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

void UEGRunResourceComponent::FailStartup(const FString& Message)
{
 Error=Message;StartupPhase=EEGRunStartupPhase::Failed;bReady=false;
 UE_LOG(LogTemp,Error,TEXT("EGResidency %s"),*Error);
 // Shipping omits normal logs; retain the exact failing resource for support.
 const FString DiagnosticDirectory=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Diagnostics"));
 IFileManager::Get().MakeDirectory(*DiagnosticDirectory,true);
 FFileHelper::SaveStringToFile(FDateTime::UtcNow().ToIso8601()+TEXT("\n")+Error+TEXT("\n"),
  *FPaths::Combine(DiagnosticDirectory,TEXT("LastResourceFailure.txt")));
}
int32 UEGRunResourceComponent::GetCompletedWork() const
{
 return CompletedResourceCount+DeferredPresentationIndex;
}
int32 UEGRunResourceComponent::GetTotalWork() const { return Handles.Num()+RunDefinitions.Num(); }
FString UEGRunResourceComponent::GetStartupDetail() const
{
 if(!Error.IsEmpty())return TEXT("部分资源无法读取，请重试加载");
 if(bBackgroundPending)return FString::Printf(TEXT("正在加载角色、动作与特效 · 已完成 %d 项"),GetCompletedWork());
 if(bDeferredPresentations)return FString::Printf(TEXT("正在准备角色显示 · %d / %d"),DeferredPresentationIndex,RunDefinitions.Num());
 return bStartupComplete?TEXT("资源已就绪"):TEXT("正在整理本局资源");
}

AEGPlacementPreview* UEGRunResourceComponent::GetPooledPreview(const UEGHeroDefinition* Hero) const
{
 const auto* Found=Previews.Find(const_cast<UEGHeroDefinition*>(Hero));
 return Found&&IsValid(*Found)?Found->Get():nullptr;
}
AEGPlacementPreview* UEGRunResourceComponent::AcquirePreview(UEGHeroDefinition* Hero)
{
 if(!IsValid(Hero)||!GetWorld()||GetWorld()->bIsTearingDown)return nullptr;
 if(auto* Existing=GetPooledPreview(Hero))return Existing;
 if(Hero->GetMesh().IsNull())return nullptr;
 auto* PC=Cast<AEGBoardPlayerController>(GetWorld()->GetFirstPlayerController());
 const auto Class=PC&&PC->PreviewClass?PC->PreviewClass.Get():AEGPlacementPreview::StaticClass();
 auto* Preview=GetWorld()->SpawnActorDeferred<AEGPlacementPreview>(Class,FTransform::Identity,GetOwner(),nullptr,
  ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
 if(!Preview)return nullptr;
 Preview->HeroVisualDefinition=Hero;Preview->SetReplicates(false);
 Preview->FinishSpawning(FTransform::Identity);Preview->HidePreview();
 RunDefinitions.AddUnique(Hero);Previews.Add(Hero,Preview);
 return Preview;
}
void UEGRunResourceComponent::InitializePresentations()
{
 RunDefinitions=ModelPreloadHeroes;
 // Forms/summons are definitions, not whole skill/FX graphs. Do not walk every soft reference here.
 for(int32 I=0;I<RunDefinitions.Num();++I)
 {
  auto* Hero=RunDefinitions[I].Get();
  if(Hero->EnhancedDefinition)RunDefinitions.AddUnique(Hero->EnhancedDefinition);
  if(Hero->SummonDefinition)RunDefinitions.AddUnique(Hero->SummonDefinition);
 }
 if(IsRunningCommandlet()||!GetWorld()->GetGameViewport())return;
 // The scene prepares its usable models once. No visible rendering cycle or PSO wait is added.
 const auto Definitions=RunDefinitions;
 for(const auto& Hero:Definitions)AcquirePreview(Hero);
 UE_LOG(LogTemp,Display,TEXT("EGPresentationCache scene previews=%d; no startup resource gate"),Previews.Num());
}
void UEGRunResourceComponent::PollDeferredPresentations()
{
 if(!bDeferredPresentations||bBackgroundPending||!Error.IsEmpty())return;
 StartupPhase=EEGRunStartupPhase::PreparingPreviews;
 if(IsRunningCommandlet()||!GetWorld()->GetGameViewport())DeferredPresentationIndex=RunDefinitions.Num();
 else if(RunDefinitions.IsValidIndex(DeferredPresentationIndex))
 {
  // A single preview can synchronously instantiate its mesh; never create the
  // entire roster in one frame while the opening animation is visible.
  auto* Hero=RunDefinitions[DeferredPresentationIndex].Get();
  if(IsValid(Hero)&&!Hero->GetMesh().IsNull())
  {
   if(!Hero->GetMesh().Get()||!AcquirePreview(Hero))
   {FailStartup(TEXT("Opening preview unavailable: ")+Hero->GetPathName());return;}
  }
  ++DeferredPresentationIndex;
 }
 if(DeferredPresentationIndex>=RunDefinitions.Num())
 {
  bDeferredPresentations=false;bReady=true;bStartupComplete=true;
  StartupPhase=EEGRunStartupPhase::Ready;
 }
}
void UEGRunResourceComponent::RequestHeroResources(UEGHeroDefinition* Hero)
{
 if(!bRunConfigured||!IsValid(Hero)||RequiredHeroes.Contains(Hero))return;
 RequiredHeroes.Add(Hero);PendingBackgroundRoots.Add(Hero);bDirty=true;bBackgroundPending=true;
}
void UEGRunResourceComponent::RequestRuntimeAsset(const FSoftObjectPath& Path)
{
 if(!bRunConfigured||!Path.IsValid()||Handles.Contains(Path))return;
 Handles.Add(Path,UAssetManager::GetStreamableManager().RequestAsyncLoad(Path));
 PendingRuntimeAssets.Add(Path);bDirty=true;bBackgroundPending=true;
}
void UEGRunResourceComponent::PollBackgroundResources()
{
 TRACE_CPUPROFILER_EVENT_SCOPE(EG_BackgroundResourceGraph);
 // Never flush async loading here. Completion/failure is consumed by the GameMode's formal opening gate.
 if(!bDirty&&!bBackgroundPending)return;
 // Cache progress at the resource poll rate, not once per Slate frame.
 CompletedResourceCount=0;bool bWaiting=false;
 for(const auto& H:Handles)
 {
  if(H.Value&&!H.Value->HasLoadCompleted())bWaiting=true;
  else if(H.Key.ResolveObject())++CompletedResourceCount;
 }
 if(bWaiting)return;
 for(const auto& Path:PendingRuntimeAssets)
 {
  UObject* Object=Path.ResolveObject();
  if(!Object){if(Error.IsEmpty())FailStartup(TEXT("Resource unavailable: ")+Path.ToString());}
  else if(auto* Asset=Cast<UDataAsset>(Object))PendingBackgroundRoots.Add(Asset);
 }
 PendingRuntimeAssets.Reset();
 bool bAdded=false;
 for(auto Root=PendingBackgroundRoots.CreateIterator();Root;++Root)
 {
  if(!Root->IsValid()){Root.RemoveCurrent();continue;}
  TSet<FSoftObjectPath> Paths,Optional;
  ++BackgroundGraphTraversalCount;
  GatherLoadedGraph(Root->Get(),Paths,nullptr,&Optional);
  bool bRootPending=false;
  for(const auto& Path:Paths)
  {
   auto* Existing=Handles.Find(Path);
   if(!Existing)
   {
    Handles.Add(Path,UAssetManager::GetStreamableManager().RequestAsyncLoad(Path));
    bAdded=true;bRootPending=true;
   }
   else if(*Existing&&!(*Existing)->HasLoadCompleted())bRootPending=true;
  }
  // A newly loaded data asset may reveal more edges on the next traversal.
  if(bRootPending)continue;
  for(const auto& Path:Paths)if(!Path.ResolveObject()&&!Optional.Contains(Path))
  {
   // A configured mesh/animation/effect is required even if it is not a title-screen model.
   // Do not silently enter a run whose skill graphs failed to load.
   if(Error.IsEmpty())FailStartup(TEXT("Resource unavailable: ")+Path.ToString());
  }
  Root.RemoveCurrent();
 }
 bDirty=false;bBackgroundPending=!PendingBackgroundRoots.IsEmpty();
 if(bAdded)
 {UE_LOG(LogTemp,Display,TEXT("EGBackground roots=%d held=%d formalGate=%d"),RequiredHeroes.Num(),Handles.Num(),bFormalOpeningPreload);return;}
 // Strong handles keep loaded resources for reuse until the world ends; switching cards cannot evict them.
}
