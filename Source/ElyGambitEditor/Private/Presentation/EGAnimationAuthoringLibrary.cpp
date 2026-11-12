#include "Presentation/EGAnimationAuthoringLibrary.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimMontage.h"
#include "Presentation/EGSkillNotify.h"
#include "Curves/CurveFloat.h"
#if WITH_EDITOR
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimCurveTypes.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/Linker.h"
#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"
#include "Components/SkeletalMeshComponent.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/UnrealType.h"
#endif

bool UEGAnimationAuthoringLibrary::RetimeSkillMontageNotifies(UAnimMontage* Montage,const TArray<FName>& EventNames,float OldTime,float NewTime)
{
#if WITH_EDITOR
 if(!IsValid(Montage)||!Montage->GetOutermost()->GetName().StartsWith(TEXT("/Game/"))||EventNames.IsEmpty()||
  !FMath::IsFinite(OldTime)||!FMath::IsFinite(NewTime)||OldTime<=0.f||NewTime<=0.f||
  OldTime>=Montage->GetPlayLength()||NewTime>=Montage->GetPlayLength())
 {UE_LOG(LogTemp,Display,TEXT("Origin notify retime rejected: invalid montage, names or time"));return false;}
 TSet<FName> Names;
 for(const FName Name:EventNames){if(Name.IsNone()||Names.Contains(Name)){UE_LOG(LogTemp,Display,TEXT("Origin notify retime rejected: empty or duplicate requested name %s"),*Name.ToString());return false;}Names.Add(Name);}
 const TArray<FAnimNotifyEvent> Before=Montage->Notifies;
 TSet<FGuid> AllGuids;TSet<FName> Found;
 for(const auto& Item:Before)
 {
  if(!Item.Guid.IsValid()||AllGuids.Contains(Item.Guid))
  {UE_LOG(LogTemp,Display,TEXT("Origin notify retime rejected: invalid or duplicate GUID=%s notify=%s time=%.6f"),*Item.Guid.ToString(),*GetNameSafe(Item.Notify),Item.GetTriggerTime());return false;}
  AllGuids.Add(Item.Guid);
  const auto* Notify=Cast<UEGSkillNotify>(Item.Notify);
  if(Notify&&Names.Contains(Notify->Event))
  {
   if(Item.NotifyStateClass){UE_LOG(LogTemp,Display,TEXT("Origin notify retime rejected: state notify event=%s"),*Notify->Event.ToString());return false;}
   if(Found.Contains(Notify->Event)||!FMath::IsNearlyEqual(Item.GetTriggerTime(),OldTime,.001f))
   {UE_LOG(LogTemp,Display,TEXT("Origin notify retime rejected: duplicate or unexpected time event=%s actual=%.6f expected=%.6f"),*Notify->Event.ToString(),Item.GetTriggerTime(),OldTime);return false;}
   Found.Add(Notify->Event);
  }
 }
 if(Found.Num()!=Names.Num()){UE_LOG(LogTemp,Display,TEXT("Origin notify retime rejected: matched %d of %d requested names"),Found.Num(),Names.Num());return false;}
 UE_LOG(LogTemp,Display,TEXT("Origin notify retime: validated %d unique notifies, editing %d from %.6f to %.6f"),Before.Num(),Found.Num(),OldTime,NewTime);
 Montage->Modify();
 for(auto& Item:Montage->Notifies)
  if(const auto* Notify=Cast<UEGSkillNotify>(Item.Notify);Notify&&Names.Contains(Notify->Event))
   Item.FAnimLinkableElement::SetTime(NewTime-Item.TriggerTimeOffset);
 Montage->RefreshCacheData();
 bool bValid=Montage->Notifies.Num()==Before.Num();
 if(!bValid)UE_LOG(LogTemp,Display,TEXT("Origin notify retime failed: notify count changed %d -> %d"),Before.Num(),Montage->Notifies.Num());
 for(const auto& Original:Before)
 {
  const auto* Current=Montage->Notifies.FindByPredicate([&Original](const FAnimNotifyEvent& Item){return Item.Guid==Original.Guid;});
  if(!Current){UE_LOG(LogTemp,Display,TEXT("Origin notify retime failed: missing GUID %s"),*Original.Guid.ToString());bValid=false;break;}
  const auto* Notify=Cast<UEGSkillNotify>(Original.Notify);
  const bool bMoved=Notify&&Names.Contains(Notify->Event);
  if(!FMath::IsNearlyEqual(Current->GetTriggerTime(),bMoved?NewTime:Original.GetTriggerTime(),.0001f))
  {UE_LOG(LogTemp,Display,TEXT("Origin notify retime failed: time GUID=%s actual=%.9f expected=%.9f"),*Original.Guid.ToString(),Current->GetTriggerTime(),bMoved?NewTime:Original.GetTriggerTime());bValid=false;break;}
  FAnimNotifyEvent Normalized=*Current;
  if(bMoved)Normalized.FAnimLinkableElement::SetTime(Original.GetTime());
  if(!FAnimNotifyEvent::StaticStruct()->CompareScriptStruct(&Normalized,&Original,0))
  {
   UE_LOG(LogTemp,Display,TEXT("Origin notify retime failed: structure differs GUID=%s notify=%s originalBase=%.9f currentBase=%.9f normalizedBase=%.9f"),
    *Original.Guid.ToString(),*GetNameSafe(Original.Notify),Original.GetTime(),Current->GetTime(),Normalized.GetTime());
   for(TFieldIterator<FProperty> It(FAnimNotifyEvent::StaticStruct());It;++It)
    if(!It->Identical_InContainer(&Normalized,&Original,0))UE_LOG(LogTemp,Display,TEXT("Origin notify retime differing property: %s"),*It->GetName());
   bValid=false;break;
  }
 }
 if(!bValid){UE_LOG(LogTemp,Display,TEXT("Origin notify retime rolled back"));Montage->Notifies=Before;Montage->RefreshCacheData();return false;}
 UE_LOG(LogTemp,Display,TEXT("Origin notify retime verified all metadata"));
 Montage->MarkPackageDirty();return true;
#else
 return false;
#endif
}

bool UEGAnimationAuthoringLibrary::SaveSourceEditorAsset(UObject* Asset)
{
#if WITH_EDITOR
 if(!IsValid(Asset) || !GEditor) return false;
 UPackage* Package=Asset->GetOutermost();
 if(!Package || Package==GetTransientPackage() || !Package->GetName().StartsWith(TEXT("/Game/"))) return false;
 Package->FullyLoad();
 ResetLoaders(Package);
 Package->MarkPackageDirty();
 auto* Assets=GEditor->GetEditorSubsystem<UEditorAssetSubsystem>();
 return Assets && Assets->SaveLoadedAsset(Asset,false);
#else
 return false;
#endif
}
TArray<FName> UEGAnimationAuthoringLibrary::ReferenceBoneNames(USkeletalMesh* Mesh)
{
 TArray<FName> Names;
 if(Mesh)for(const auto& Bone:Mesh->GetRefSkeleton().GetRefBoneInfo())Names.Add(Bone.Name);
 return Names;
}
TArray<FTransform> UEGAnimationAuthoringLibrary::ReferenceBonePoses(USkeletalMesh* Mesh)
{
 return Mesh?Mesh->GetRefSkeleton().GetRefBonePose():TArray<FTransform>();
}
TArray<int32> UEGAnimationAuthoringLibrary::ReferenceBoneParents(USkeletalMesh* Mesh)
{
 TArray<int32> Parents;
 if(Mesh)for(const auto& Bone:Mesh->GetRefSkeleton().GetRefBoneInfo())Parents.Add(Bone.ParentIndex);
 return Parents;
}

UAnimSequence* UEGAnimationAuthoringLibrary::CreateSourceAnimation(const FString& PackageName, USkeletalMesh* Mesh)
{
#if WITH_EDITOR
 if (!Mesh || !Mesh->GetSkeleton() || !PackageName.StartsWith(TEXT("/Game/Assets/Characters/"))
     || !FPackageName::IsValidLongPackageName(PackageName) || FPackageName::DoesPackageExist(PackageName)) return nullptr;
 const FString Name = FPackageName::GetLongPackageAssetName(PackageName);
 UPackage* Package = CreatePackage(*PackageName);
 if (FindObject<UObject>(Package, *Name)) return nullptr;
 UAnimSequence* Animation = NewObject<UAnimSequence>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
 Animation->SetSkeleton(Mesh->GetSkeleton());
 Animation->GetController().InitializeModel();
 Animation->SetPreviewMesh(Mesh);
 // AnimSequenceFactory populates one default-rate frame too early. Keeping this
 // model unpopulated lets the source importer set its actual rate without a
 // fractional-frame resample of the factory's placeholder key.
 FAssetRegistryModule::AssetCreated(Animation);
 Animation->MarkPackageDirty();
 return Animation;
#else
 return nullptr;
#endif
}

bool UEGAnimationAuthoringLibrary::SetSourceFloatCurve(UAnimSequence* Animation, FName CurveName, const TArray<float>& Times, const TArray<float>& Values)
{
 FEGSourceFloatCurveSamples Curve;Curve.CurveName=CurveName;Curve.Times=Times;Curve.Values=Values;
 return SetSourceFloatCurves(Animation,{Curve});
}

bool UEGAnimationAuthoringLibrary::SetSourceFloatCurves(UAnimSequence* Animation, const TArray<FEGSourceFloatCurveSamples>& Curves)
{
#if WITH_EDITOR
 if(!Animation || Curves.IsEmpty())return false;
 TSet<FName> Names;
 TArray<TArray<FRichCurveKey>> AllKeys;
 for(const auto& Curve:Curves)
 {
 const auto& Times=Curve.Times;const auto& Values=Curve.Values;const FName CurveName=Curve.CurveName;
 if (!Animation || CurveName.IsNone() || Times.IsEmpty() || Times.Num()!=Values.Num()) return false;
 if(Names.Contains(CurveName))return false;
 Names.Add(CurveName);
 TArray<FRichCurveKey> Keys;Keys.Reserve(Times.Num());
 for (int32 Index=0;Index<Times.Num();++Index)
 {
  if (!FMath::IsFinite(Times[Index]) || !FMath::IsFinite(Values[Index]) || Times[Index]<0.f
      || (Index>0 && Times[Index]<=Times[Index-1]) || Times[Index]>Animation->GetPlayLength()+KINDA_SMALL_NUMBER) return false;
  FRichCurveKey Key(Times[Index],Values[Index]);Key.InterpMode=RCIM_Linear;Keys.Add(Key);
 }
 AllKeys.Add(MoveTemp(Keys));
 }
 // Compression workers evaluate the model's curves directly. A controller
 // bracket batches notifications, but does not synchronize those readers.
 Animation->WaitOnExistingCompression();
 auto& Controller=Animation->GetController();
 Controller.OpenBracket(FText::FromString(TEXT("Replace sampled source curves")),false);
 bool bSuccess=true;
 for(int32 Index=0;Index<Curves.Num();++Index)
 {
 const FAnimationCurveIdentifier Id(Curves[Index].CurveName,ERawCurveTrackTypes::RCT_Float);
 if(Animation->GetDataModel()->FindCurve(Id))Controller.RemoveCurve(Id,false);
 bSuccess &= Controller.AddCurve(Id,AACF_DefaultCurve,false) && Controller.SetCurveKeys(Id,AllKeys[Index],false);
 }
 Controller.CloseBracket(false);
 Animation->WaitOnExistingCompression();
 if(bSuccess)Animation->MarkPackageDirty();
 return bSuccess;
#else
 return false;
#endif
}

bool UEGAnimationAuthoringLibrary::SetSourceCurveKeys(UCurveFloat* Curve, const TArray<float>& Times, const TArray<float>& Values, bool bConstant)
{
#if WITH_EDITOR
 if(!Curve || Times.IsEmpty() || Times.Num()!=Values.Num())return false;
 for(int32 Index=0;Index<Times.Num();++Index)
  if(!FMath::IsFinite(Times[Index]) || !FMath::IsFinite(Values[Index]) || Times[Index]<0.f
     || (Index>0 && Times[Index]<=Times[Index-1]))return false;
 Curve->FloatCurve.Reset();
 for(int32 Index=0;Index<Times.Num();++Index)
 {
  const auto Handle=Curve->FloatCurve.AddKey(Times[Index],Values[Index]);
  Curve->FloatCurve.SetKeyInterpMode(Handle,bConstant?RCIM_Constant:RCIM_Linear);
 }
 Curve->MarkPackageDirty();return true;
#else
 return false;
#endif
}

FBox UEGAnimationAuthoringLibrary::SourceSkinnedBounds(USkeletalMeshComponent* Component, bool bVisibleSectionsOnly)
{
 FBox Bounds(ForceInit);
#if WITH_EDITOR
 if(!Component || !Component->GetSkeletalMeshAsset())return Bounds;
 const auto* RenderData=Component->GetSkeletalMeshAsset()->GetResourceForRendering();
 if(!RenderData || RenderData->LODRenderData.IsEmpty())return Bounds;
 const auto& LOD=RenderData->LODRenderData[0];
 const FSkinWeightVertexBuffer* Weights=Component->GetSkinWeightBuffer(0);
 if(!Weights || LOD.GetNumVertices()==0)return Bounds;
 TArray<FMatrix44f> Matrices;Component->CacheRefToLocalMatrices(Matrices);
 TArray<FVector3f> Positions;
 USkinnedMeshComponent::ComputeSkinnedPositions(Component,Positions,Matrices,LOD,*Weights);
 if(bVisibleSectionsOnly)
 {
  for(const auto& Section:LOD.RenderSections)
   if(Component->IsMaterialSectionShown(Section.MaterialIndex,0))
    for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex)
     if(Positions.IsValidIndex(Vertex) && !Positions[Vertex].ContainsNaN())Bounds+=FVector(Positions[Vertex]);
 }
 else for(const FVector3f& Position:Positions)if(!Position.ContainsNaN())Bounds+=FVector(Position);
#endif
 return Bounds;
}

FBox UEGAnimationAuthoringLibrary::SourceSkinnedMaterialBounds(USkeletalMeshComponent* Component,int32 MaterialIndex)
{
 FBox Bounds(ForceInit);
#if WITH_EDITOR
 if(!Component||!Component->GetSkeletalMeshAsset()||MaterialIndex<0||MaterialIndex>=Component->GetNumMaterials())return Bounds;
 const auto* RenderData=Component->GetSkeletalMeshAsset()->GetResourceForRendering();
 if(!RenderData||RenderData->LODRenderData.IsEmpty())return Bounds;
 const auto& LOD=RenderData->LODRenderData[0];const auto* Weights=Component->GetSkinWeightBuffer(0);
 if(!Weights||LOD.GetNumVertices()==0)return Bounds;
 TArray<FMatrix44f> Matrices;Component->CacheRefToLocalMatrices(Matrices);
 TArray<FVector3f> Positions;USkinnedMeshComponent::ComputeSkinnedPositions(Component,Positions,Matrices,LOD,*Weights);
 for(const auto& Section:LOD.RenderSections)if(Section.MaterialIndex==MaterialIndex)
  for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex)
   if(Positions.IsValidIndex(Vertex)&&!Positions[Vertex].ContainsNaN())Bounds+=FVector(Positions[Vertex]);
#endif
 return Bounds;
}

TArray<FVector> UEGAnimationAuthoringLibrary::SourceReferenceMaterialVertices(USkeletalMesh* Mesh,int32 MaterialIndex)
{
 TArray<FVector> Result;
#if WITH_EDITOR
 if(!IsValid(Mesh)||MaterialIndex<0||MaterialIndex>=Mesh->GetMaterials().Num())return Result;
 const auto* RenderData=Mesh->GetResourceForRendering();
 if(!RenderData||RenderData->LODRenderData.IsEmpty())return Result;
 const auto& LOD=RenderData->LODRenderData[0];
 const auto& Positions=LOD.StaticVertexBuffers.PositionVertexBuffer;
 if(!Positions.GetVertexData())return Result;
 for(const auto& Section:LOD.RenderSections)if(Section.MaterialIndex==MaterialIndex)
  for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex)
   if(Vertex<Positions.GetNumVertices()&&!Positions.VertexPosition(Vertex).ContainsNaN())Result.Add(FVector(Positions.VertexPosition(Vertex)));
#endif
 return Result;
}

FName UEGAnimationAuthoringLibrary::SourceRigidMaterialBone(USkeletalMesh* Mesh,int32 MaterialIndex)
{
#if WITH_EDITOR
 if(!IsValid(Mesh)||MaterialIndex<0||MaterialIndex>=Mesh->GetMaterials().Num())return NAME_None;
 const auto* RenderData=Mesh->GetResourceForRendering();
 if(!RenderData||RenderData->LODRenderData.IsEmpty())return NAME_None;
 const auto& LOD=RenderData->LODRenderData[0];const auto& Weights=LOD.SkinWeightVertexBuffer;
 if(!Weights.GetDataVertexBuffer()->GetWeightData()
    ||(Weights.GetVariableBonesPerVertex()&&!Weights.GetLookupVertexBuffer()->GetLookupData()))return NAME_None;
 for(const auto& Section:LOD.RenderSections)if(Section.MaterialIndex==MaterialIndex)
  if(uint64(Section.BaseVertexIndex)+Section.NumVertices>Weights.GetNumVertices())return NAME_None;
 int32 Found=INDEX_NONE;
 for(const auto& Section:LOD.RenderSections)if(Section.MaterialIndex==MaterialIndex)
  for(uint32 Vertex=Section.BaseVertexIndex;Vertex<Section.BaseVertexIndex+Section.NumVertices;++Vertex)
   for(uint32 Influence=0;Influence<Weights.GetMaxBoneInfluences();++Influence)
   {
    if(Weights.GetBoneWeight(Vertex,Influence)==0)continue;
    const int32 Local=Weights.GetBoneIndex(Vertex,Influence);
    if(!Section.BoneMap.IsValidIndex(Local))return NAME_None;
    const int32 Bone=Section.BoneMap[Local];
    if(Found!=INDEX_NONE&&Found!=Bone)return NAME_None;
    Found=Bone;
   }
 if(Found!=INDEX_NONE&&Found<Mesh->GetRefSkeleton().GetNum())return Mesh->GetRefSkeleton().GetBoneName(Found);
#endif
 return NAME_None;
}

bool UEGAnimationAuthoringLibrary::SetSourceMeshBounds(USkeletalMesh* Mesh, FBox Bounds)
{
#if WITH_EDITOR
 if(!Mesh || !Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())return false;
 const FBox Imported=Mesh->GetImportedBounds().GetBox();
 Bounds+=Imported;
 Mesh->SetPositiveBoundsExtension((Bounds.Max-Imported.Max).ComponentMax(FVector::ZeroVector)+FVector(1));
 Mesh->SetNegativeBoundsExtension((Imported.Min-Bounds.Min).ComponentMax(FVector::ZeroVector)+FVector(1));
 Mesh->MarkPackageDirty();return true;
#else
 return false;
#endif
}

bool UEGAnimationAuthoringLibrary::FixupVillVActionRedirectors(const TArray<FString>& PackageNames)
{
#if WITH_EDITOR
 if(PackageNames.IsEmpty())return false;
 TArray<UObjectRedirector*> Redirectors;
 for(const FString& Package:PackageNames)
 {
  if(!Package.StartsWith(TEXT("/Game/Assets/Characters/VillV/Animations/NormalAttack/A_VillV_")))return false;
  const FString ObjectPath=Package+TEXT(".")+FPackageName::GetLongPackageAssetName(Package);
  auto* Redirector=LoadObject<UObjectRedirector>(nullptr,*ObjectPath,nullptr,LOAD_NoRedirects);
  if(!Redirector || !Redirector->DestinationObject || !Redirector->DestinationObject->IsA<UAnimSequence>())return false;
  const FString Destination=Redirector->DestinationObject->GetPathName();
  if(!Destination.StartsWith(TEXT("/Game/Assets/Characters/VillV/Animations/Gatling/")) &&
     !Destination.StartsWith(TEXT("/Game/Assets/Characters/VillV/Animations/Cannon/")))return false;
  Redirectors.AddUnique(Redirector);
 }
 FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().FixupReferencers(
  Redirectors,false,ERedirectFixupMode::DeleteFixedUpRedirectors);
 return !PackageNames.ContainsByPredicate([](const FString& Package){return FPackageName::DoesPackageExist(Package);});
#else
 return false;
#endif
}
