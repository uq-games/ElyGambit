#include "Components/EGRunResourceComponent.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGHeroConfig.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "Placement/EGPlacementVisuals.h"

UEGRunResourceComponent::UEGRunResourceComponent()
{ PrimaryComponentTick.bCanEverTick=false; }

void UEGRunResourceComponent::GatherLoadedGraph(const UObject* Root,TSet<FSoftObjectPath>& Paths,
 TSet<const UEGHeroDefinition*>* Definitions,TSet<FSoftObjectPath>* Optional)
{
 TSet<const UObject*> Seen;
 TFunction<void(const UObject*)> Visit;
 TFunction<void(const FProperty*,const void*)> Property;
 Property=[&](const FProperty* P,const void* Value)
 {
  if(const auto* Soft=CastField<FSoftObjectProperty>(P))
  {
   const auto Ref=Soft->GetPropertyValue(Value);const auto Path=Ref.ToSoftObjectPath();
   if(Path.IsValid())
   {
    Paths.Add(Path);
    if(Optional&&(P->GetFName()==TEXT("AttackSound")||P->GetFName()==TEXT("SkillSound")))Optional->Add(Path);
    Visit(Ref.Get());
   }
  }
  else if(const auto* Object=CastField<FObjectPropertyBase>(P))Visit(Object->GetObjectPropertyValue(Value));
  else if(const auto* Array=CastField<FArrayProperty>(P))
  { FScriptArrayHelper H(Array,Value);for(int32 I=0;I<H.Num();++I)Property(Array->Inner,H.GetRawPtr(I)); }
  else if(const auto* Map=CastField<FMapProperty>(P))
  { FScriptMapHelper H(Map,Value);for(int32 I=0;I<H.GetMaxIndex();++I)if(H.IsValidIndex(I)){Property(Map->KeyProp,H.GetKeyPtr(I));Property(Map->ValueProp,H.GetValuePtr(I));} }
  else if(const auto* Set=CastField<FSetProperty>(P))
  { FScriptSetHelper H(Set,Value);for(int32 I=0;I<H.GetMaxIndex();++I)if(H.IsValidIndex(I))Property(Set->ElementProp,H.GetElementPtr(I)); }
  else if(const auto* Struct=CastField<FStructProperty>(P))
   for(TFieldIterator<FProperty> It(Struct->Struct);It;++It)
    for(int32 I=0;I<It->ArrayDim;++I)Property(*It,It->ContainerPtrToValuePtr<void>(Value,I));
 };
 Visit=[&](const UObject* Object)
 {
  // Engine meshes, animation classes and materials load their hard dependencies themselves.
  // Restrict reflection traversal to project data assets, avoiding world/outer/engine graphs.
  if(!Object||Seen.Contains(Object)||!Object->IsA<UDataAsset>())return;
  const bool bProjectAsset=Object->GetOutermost()->GetName().StartsWith(TEXT("/Game/"));
  const bool bProjectFixture=Object->GetClass()->GetPathName().StartsWith(TEXT("/Script/ElyGambit."));
  if(!bProjectAsset&&!bProjectFixture)return;
  Seen.Add(Object);
  if(Definitions)if(const auto* D=Cast<UEGHeroDefinition>(Object))Definitions->Add(D);
  for(TFieldIterator<FProperty> It(Object->GetClass());It;++It)
   if(!It->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated))
    for(int32 I=0;I<It->ArrayDim;++I)Property(*It,It->ContainerPtrToValuePtr<void>(Object,I));
 };
 Visit(Root);
}

void UEGRunResourceComponent::SetRequiredHeroes(const TArray<UEGHeroDefinition*>& Heroes)
{
 // A pinned run is immutable during play. Stage/drag changes cannot evict assets.
 if(bRunConfigured)return;
 TArray<TObjectPtr<UEGHeroDefinition>> Next;
 for(auto* Hero:Heroes)if(IsValid(Hero))Next.AddUnique(Hero);
 bool bSame=Next.Num()==RequiredHeroes.Num();
 for(const auto& Hero:Next)bSame&=RequiredHeroes.Contains(Hero);
 if(bSame)return;
 RequiredHeroes=MoveTemp(Next);bDirty=true;bReady=false;Error.Reset();
}
void UEGRunResourceComponent::SetRunHeroes(const TArray<UEGHeroDefinition*>& Heroes,UDataAsset* Run)
{
 ResetResources();RunRoot=Run;bRunConfigured=true;
 for(auto* Hero:Heroes)if(IsValid(Hero))ModelPreloadHeroes.AddUnique(Hero);
 InitializePresentations();
 // Presentation setup is ordinary scene initialization. Full combat graphs never gate entry.
 bReady=true;bStartupComplete=true;StartupPhase=EEGRunStartupPhase::Ready;bDirty=false;
}
void UEGRunResourceComponent::SetRunHeroesDeferred(const TArray<UEGHeroDefinition*>& Heroes,UDataAsset* Run)
{
 ResetResources();RunRoot=Run;bRunConfigured=true;
 for(auto* Hero:Heroes)if(IsValid(Hero))ModelPreloadHeroes.AddUnique(Hero);
 RunDefinitions=ModelPreloadHeroes;
 bFormalOpeningPreload=true;bDeferredPresentations=true;DeferredPresentationIndex=0;
 // The run root includes cards, traps and later stages, not only the first lineup.
 if(RunRoot){PendingBackgroundRoots.Add(RunRoot.Get());bBackgroundPending=true;}
 for(const auto& Hero:ModelPreloadHeroes)RequestHeroResources(Hero);
 StartupPhase=EEGRunStartupPhase::LoadingResources;
}
void UEGRunResourceComponent::RequireOpeningHero(const UEGHeroDefinition* Hero)
{
 if(!bFormalOpeningPreload||!IsValid(Hero))return;
 auto Add=[this](const FSoftObjectPath& Path){if(Path.IsValid())OpeningRequiredPaths.Add(Path);};
 Add(Hero->GetMesh().ToSoftObjectPath());
 Add(Hero->GetIdleAnimation().ToSoftObjectPath());
 Add(Hero->GetAnimationClass().ToSoftObjectPath());
 Add(Hero->CombatActorClass.ToSoftObjectPath());
}
void UEGRunResourceComponent::FinishOpeningPreload()
{bFormalOpeningPreload=false;OpeningRequiredPaths.Reset();}
void UEGRunResourceComponent::SetModelPreloadHeroes(const TArray<UEGHeroDefinition*>& Heroes){SetRunHeroes(Heroes);}
TArray<FString> UEGRunResourceComponent::GetPreloadedModelHeroPaths() const
{TArray<FString> Result;for(const auto& Hero:ModelPreloadHeroes)Result.Add(Hero->GetPathName());Result.Sort();return Result;}

void UEGRunResourceComponent::Poll()
{
 if(bPolling||!GetWorld()||GetWorld()->bIsTearingDown)return;
 TGuardValue<bool> Guard(bPolling,true);
 if(bRunConfigured){PollBackgroundResources();PollDeferredPresentations();return;}
 if(!bDirty&&(bReady||!Error.IsEmpty()))return;
 // Headless automation retains its synchronous contract. Real play always uses async requests.
 const bool bSync=IsRunningCommandlet()||!GetWorld()->GetGameViewport();
 for(int32 Pass=0;Pass<32;++Pass)
 {
  TSet<FSoftObjectPath> Desired,Optional;TSet<const UEGHeroDefinition*> Definitions;
  for(const auto& Hero:RequiredHeroes)
  {
   auto& Cached=GraphCache.FindOrAdd(FSoftObjectPath(Hero.Get()));
   GatherLoadedGraph(Hero,Cached,&Definitions,&Optional);Desired.Append(Cached);
  }
  int32 Released=0,Added=0;
  for(auto It=Handles.CreateIterator();It;++It)if(!Desired.Contains(It.Key()))
  { if(It.Value())It.Value()->CancelHandle();It.RemoveCurrent();++Released; }
  bool bWaiting=false;
  for(const auto& Path:Desired)
  {
   auto* Existing=Handles.Find(Path);
   if(!Existing)
   {
    auto Handle=bSync?UAssetManager::GetStreamableManager().RequestSyncLoad(Path):
     UAssetManager::GetStreamableManager().RequestAsyncLoad(Path);
    Handles.Add(Path,MoveTemp(Handle));Existing=Handles.Find(Path);++Added;
   }
   if(*Existing&&!(*Existing)->HasLoadCompleted())bWaiting=true;
  }
  if(Added||Released)UE_LOG(LogTemp,Display,TEXT("EGResidency roots=%d paths=%d added=%d released=%d"),RequiredHeroes.Num(),Handles.Num(),Added,Released);
  bDirty=false;bReady=false;
  if(bWaiting)return;
  // A loaded assembly/device may reveal another layer of soft mesh/effect references.
  if(Added)continue;
  Error.Reset();
  for(const auto& Path:Desired)if(!Path.ResolveObject()&&!Optional.Contains(Path))
  { Error=TEXT("Resource unavailable: ")+Path.ToString();break; }
  if(Error.IsEmpty())for(const auto* D:Definitions)
   if(!Validated.Contains(FSoftObjectPath(D)))
   {
    if(!D->ValidateDefinition(Error,true))break;
    Validated.Add(FSoftObjectPath(D));
   }
  if(!Error.IsEmpty()){FailStartup(Error);return;}
  bReady=true;bStartupComplete=true;StartupPhase=EEGRunStartupPhase::Ready;
  return;
 }
 FailStartup(TEXT("Resource dependency graph exceeded 32 discovery layers."));
}

bool UEGRunResourceComponent::IsHeroReady(const UEGHeroDefinition* Hero) const
{ return bReady&&(bRunConfigured?RunDefinitions.Contains(Hero):RequiredHeroes.Contains(Hero)); }

TArray<FString> UEGRunResourceComponent::GetHeldResourcePaths() const
{ TArray<FString> Result;for(const auto& Entry:Handles)Result.Add(Entry.Key.ToString());Result.Sort();return Result; }
TArray<FString> UEGRunResourceComponent::GetRequiredHeroPaths() const
{ TArray<FString> Result;for(const auto& Hero:RequiredHeroes)Result.Add(Hero->GetPathName());Result.Sort();return Result; }

void UEGRunResourceComponent::ResetResources()
{
 for(const auto& Entry:Previews)if(IsValid(Entry.Value))Entry.Value->Destroy();
 Previews.Reset();RunDefinitions.Reset();RunRoot=nullptr;
 bRunConfigured=false;bBackgroundPending=false;
 bDeferredPresentations=false;bFormalOpeningPreload=false;DeferredPresentationIndex=0;OpeningRequiredPaths.Reset();
 StartupPhase=EEGRunStartupPhase::LoadingResources;
 for(auto& Entry:Handles)if(Entry.Value)Entry.Value->CancelHandle();
 Handles.Reset();RequiredHeroes.Reset();PendingBackgroundRoots.Reset();PendingRuntimeAssets.Reset();BackgroundGraphTraversalCount=0;ModelPreloadHeroes.Reset();GraphCache.Reset();Validated.Reset();
 bReady=false;bStartupComplete=false;bDirty=true;CompletedResourceCount=0;Error.Reset();
}
void UEGRunResourceComponent::EndPlay(const EEndPlayReason::Type Reason)
{ ResetResources();Super::EndPlay(Reason); }
