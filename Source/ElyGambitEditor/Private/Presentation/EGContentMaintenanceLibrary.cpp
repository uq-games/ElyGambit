#include "Presentation/EGContentMaintenanceLibrary.h"
#if WITH_EDITOR
#include "Animation/Skeleton.h"
#include "Animation/AnimationAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureBase.h"
#endif

bool UEGContentMaintenanceLibrary::CleanPardoWidget(UBlueprint* Blueprint)
{
#if WITH_EDITOR
 auto* BP=Cast<UWidgetBlueprint>(Blueprint);if(!BP||!BP->WidgetTree)return false;
 BP->Modify();
 BP->ForEachSourceWidget([](UWidget* Widget)
 {
  const FString Name=Widget->GetName();
  const bool Identity=Name.Contains(TEXT("Commander"))||Name.Contains(TEXT("AvatarFace"))||Name.Contains(TEXT("AvatarRing"));
  if(Identity||Name==TEXT("IdentityDock")||Name==TEXT("BoardThemeAvatarDock"))Widget->SetVisibility(ESlateVisibility::Collapsed);
  if(auto* Image=Cast<UImage>(Widget))
  {
   const auto* Resource=Image->GetBrush().GetResourceObject();const FString Path=Resource?Resource->GetPathName():FString();
   if(Identity||Path.Contains(TEXT("Elysia"))||Path.Contains(TEXT("CorruptionTraveler"))||Path.Contains(TEXT("/Portraits/"))||Path.Contains(TEXT("/Signets/")))
    Image->SetBrush(FSlateBrush());
  }
 });
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);FKismetEditorUtilities::CompileBlueprint(BP);
 BP->MarkPackageDirty();return BP->Status!=BS_Error;
#else
 return false;
#endif
}

bool UEGContentMaintenanceLibrary::SetPreviewMesh(UObject* Asset,USkeletalMesh* Mesh)
{
#if WITH_EDITOR
 if(!Asset||!Mesh)return false;
 Asset->Modify();
 if(auto* Skeleton=Cast<USkeleton>(Asset))Skeleton->SetPreviewMesh(Mesh,false);
 else if(auto* Animation=Cast<UAnimationAsset>(Asset))Animation->SetPreviewMesh(Mesh,false);
 else
 {
  // Preview pointers are editor-only and deliberately absent from Python reflection.
  FProperty* Property=FindFProperty<FProperty>(Asset->GetClass(),TEXT("PreviewSkeletalMesh"));
  if(auto* Object=CastField<FObjectPropertyBase>(Property))Object->SetObjectPropertyValue_InContainer(Asset,Mesh);
  else if(auto* Soft=CastField<FSoftObjectProperty>(Property))Soft->SetPropertyValue_InContainer(Asset,FSoftObjectPtr(Mesh));
  else return false;
 }
 Asset->MarkPackageDirty();return true;
#else
 return false;
#endif
}

bool UEGContentMaintenanceLibrary::RepairReferences(const TArray<UObject*>& Assets,const TMap<FString,FString>& PackagePaths)
{
#if WITH_EDITOR
 TMap<UObject*,UObject*> Replacements;TMap<FSoftObjectPath,FSoftObjectPath> SoftPaths;
 for(const auto& Pair:PackagePaths)
 {
  const FString Old=Pair.Key+TEXT(".")+FPackageName::GetShortName(Pair.Key);
  const FString New=Pair.Value+TEXT(".")+FPackageName::GetShortName(Pair.Value);
  auto* Target=LoadObject<UObject>(nullptr,*New);if(!Target)return false;
  auto* Previous=LoadObject<UObject>(nullptr,*Old);
  if(Previous&&Previous!=Target)Replacements.Add(Previous,Target);
  SoftPaths.Add(FSoftObjectPath(Old),FSoftObjectPath(New));
  SoftPaths.Add(FSoftObjectPath(Old+TEXT("_C")),FSoftObjectPath(New+TEXT("_C")));
  if(auto* OldBP=Cast<UBlueprint>(Previous))if(auto* NewBP=Cast<UBlueprint>(Target))
   if(OldBP->GeneratedClass&&NewBP->GeneratedClass&&OldBP->GeneratedClass!=NewBP->GeneratedClass)
    Replacements.Add(OldBP->GeneratedClass,NewBP->GeneratedClass);
 }
 TArray<UPackage*> Packages;
 for(auto* Asset:Assets)
 {
  if(!Asset)return false;auto* Package=Asset->GetOutermost();Packages.AddUnique(Package);
  TArray<UObject*> Objects;GetObjectsWithOuter(Package,Objects,true);
  for(auto* Object:Objects)
  {FArchiveReplaceObjectRef<UObject> Replace(Object,Replacements,EArchiveReplaceObjectFlags::IgnoreOuterRef);}
  Package->MarkPackageDirty();
 }
 FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().RenameReferencingSoftObjectPaths(Packages,SoftPaths);
 return true;
#else
 return false;
#endif
}

void UEGContentMaintenanceLibrary::AddPackageAliases(const TMap<FString,FString>& PackagePaths)
{
#if WITH_EDITOR
 TArray<FCoreRedirect> Redirects;
 for(const auto& Pair:PackagePaths)Redirects.Emplace(ECoreRedirectFlags::Type_Package,Pair.Key,Pair.Value);
 FCoreRedirects::AddRedirectList(Redirects,TEXT("ElyGambitContentComparison"));
#endif
}

FString UEGContentMaintenanceLibrary::MaterialGraphSignature(UMaterial* Material)
{
#if WITH_EDITOR
 if(!Material)return FString();TArray<FString> Rows;
 for(UMaterialExpression* Expression:Material->GetExpressions())
 {
  if(!Expression)continue;
  FString Row=Expression->GetName()+TEXT(":")+Expression->GetClass()->GetName();
  if(auto* Sample=Cast<UMaterialExpressionTextureSample>(Expression))
   Row+=TEXT(":Texture=")+(Sample->Texture?Sample->Texture->GetName():TEXT("NULL"));
  for(auto* Input:Expression->GetInputsView())
   Row+=TEXT(":Input=")+(Input&&Input->Expression?Input->Expression->GetName():TEXT("NULL"));
  Rows.Add(Row);
 }
 for(int32 P=0;P<MP_MAX;++P)
  if(auto* Input=Material->GetExpressionInputForProperty(EMaterialProperty(P)))
   Rows.Add(FString::Printf(TEXT("Property%d=%s"),P,Input->Expression?*Input->Expression->GetName():TEXT("NULL")));
 Rows.Sort();return FString::Join(Rows,TEXT("\n"));
#else
 return FString();
#endif
}

TArray<UMaterialExpression*> UEGContentMaintenanceLibrary::MaterialExpressions(UMaterial* Material)
{
 TArray<UMaterialExpression*> Result;
#if WITH_EDITOR
 if(Material)for(UMaterialExpression* Expression:Material->GetExpressions())Result.Add(Expression);
#endif
 return Result;
}

bool UEGContentMaintenanceLibrary::RestoreMaterialTextures(UMaterial* Material,const TMap<FString,UTexture*>& Textures)
{
#if WITH_EDITOR
 if(!Material)return false;TMap<FString,UMaterialExpressionTextureBase*> Nodes;
 for(UMaterialExpression* Expression:Material->GetExpressions())
  if(auto* Texture=Cast<UMaterialExpressionTextureBase>(Expression))Nodes.Add(Expression->GetName(),Texture);
 for(const auto& Pair:Textures)if(!Pair.Value||!Nodes.Contains(Pair.Key))return false;
 Material->Modify();
 for(const auto& Pair:Textures)
 {auto* Node=Nodes[Pair.Key];Node->Modify();Node->Texture=Pair.Value;Node->PostEditChange();}
 Material->PostEditChange();Material->MarkPackageDirty();return true;
#else
 return false;
#endif
}
