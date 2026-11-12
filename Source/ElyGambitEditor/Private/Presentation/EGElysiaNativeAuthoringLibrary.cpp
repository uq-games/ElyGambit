#include "Presentation/EGElysiaNativeAuthoringLibrary.h"
#include "Animation/AnimBoneCompressionCodec.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "UObject/UnrealType.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "ReferenceSkeleton.h"
#include "SkeletalMeshAttributes.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimationPoseData.h"
#include "Animation/AttributesRuntime.h"
#include "BoneContainer.h"
#include "StaticMeshAttributes.h"
#include "Data/EGHeroDefinition.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/PackageName.h"
#include "AssetRegistry/AssetRegistryModule.h"
#endif

FString UEGElysiaNativeAuthoringLibrary::StageFormalPresentationPackage(UEGHeroDefinition* Hero,UEGHeroPresentationDefinition* Presentation)
{
#if WITH_EDITOR
    if(!IsRunningCommandlet()||!Hero||!Presentation
        ||Hero->GetPathName()!=TEXT("/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Elysia.DA_Elysia")
        ||Presentation->GetPathName()!=TEXT("/Game/Blueprints/Characters/Elysia/Data/Presentation/DA_C1_Presentation.DA_C1_Presentation")
        ||Hero->GetOutermost()->IsDirty())return FString();
    const FString Folder=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("ElysiaNative20261025/PendingFormal"))
        /FString::Printf(TEXT("%s_%u"),*FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")),FPlatformProcess::GetCurrentProcessId());
    const FString Filename=Folder/TEXT("DA_Elysia.uasset");
    if(!IFileManager::Get().MakeDirectory(*Folder,true)||IFileManager::Get().FileExists(*Filename))return FString();
    // The original package/name is retained. Only this commandlet's in-memory
    // presentation pointer changes temporarily; the original disk file is never saved.
    TGuardValue<TObjectPtr<UEGHeroPresentationDefinition>> Binding(Hero->PresentationDefinition,Presentation);
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if(!UPackage::SavePackage(Hero->GetOutermost(),Hero,*Filename,Args))return FString();
    Hero->GetOutermost()->SetDirtyFlag(false);
    return Filename;
#else
    return FString();
#endif
}

UStaticMesh* UEGElysiaNativeAuthoringLibrary::CreatePrivateStaticSourceMesh(const FString& PackageName,const FString& Document)
{
#if WITH_EDITOR
    if (!PackageName.StartsWith(TEXT("/Game/Assets/Characters/Elysia/Effects/C1/Meshes/NativeDome/"))
        || !FPackageName::IsValidLongPackageName(PackageName) || FPackageName::DoesPackageExist(PackageName)
        || FindPackage(nullptr,*PackageName)) return nullptr;
    UPackage* Package=CreatePackage(*PackageName);
    UStaticMesh* Mesh=NewObject<UStaticMesh>(Package,*FPackageName::GetLongPackageAssetName(PackageName),RF_Public|RF_Standalone);
    const FString Result=BuildPrivateStaticSourceMesh(Mesh,Document);
    if (!Result.StartsWith(TEXT("BUILT_SOURCE_MESH"))) {Mesh->ClearFlags(RF_Public|RF_Standalone);Mesh->MarkAsGarbage();return nullptr;}
    FAssetRegistryModule::AssetCreated(Mesh);return Mesh;
#else
    return nullptr;
#endif
}

FString UEGElysiaNativeAuthoringLibrary::BuildPrivateStaticSourceMesh(UStaticMesh* Mesh,const FString& Document)
{
#if WITH_EDITOR
    // Existing extracted libraries and previously built candidates are immutable.
    if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Assets/Characters/Elysia/Effects/C1/Meshes/NativeDome/"))
        || Mesh->GetNumSourceModels()!=0) return TEXT("REJECTED_NONEMPTY_OR_NONPRIVATE_MESH");
    TSharedPtr<FJsonObject> Json;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Document),Json) || !Json) return TEXT("INVALID_JSON");
    TArray<double> P,N,T,C,Indices;TArray<TArray<double>> UV;
    auto Numbers=[](const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,TArray<double>& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if (!Object->TryGetArrayField(Key,Values)) return false;
        for (const auto& Value:*Values) { double V; if (!Value->TryGetNumber(V) || !FMath::IsFinite(V)) return false; Out.Add(V); }
        return true;
    };
    if (!Numbers(Json,TEXT("positions"),P) || !Numbers(Json,TEXT("normals"),N) || !Numbers(Json,TEXT("tangents"),T)
        || !Numbers(Json,TEXT("colors"),C) || !Numbers(Json,TEXT("indices"),Indices)) return TEXT("INVALID_STREAM");
    const int32 Count=P.Num()/3;
    if (!Count || P.Num()!=Count*3 || N.Num()!=Count*3 || (!T.IsEmpty() && T.Num()!=Count*4)
        || (!C.IsEmpty() && C.Num()!=Count*4) || Indices.IsEmpty() || Indices.Num()%3) return TEXT("INVALID_STREAM_COUNTS");
    const TArray<TSharedPtr<FJsonValue>>* Channels=nullptr;
    if (!Json->TryGetArrayField(TEXT("uv"),Channels) || Channels->IsEmpty() || Channels->Num()>8) return TEXT("INVALID_UV_CHANNELS");
    for (const auto& Channel:*Channels)
    {
        const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
        if (!Channel->TryGetArray(Values) || Values->Num()!=Count*2) return TEXT("INVALID_UV_COUNT");
        auto& ValuesOut=UV.AddDefaulted_GetRef();
        for (const auto& Value:*Values) { double V; if (!Value->TryGetNumber(V) || !FMath::IsFinite(V)) return TEXT("INVALID_UV");ValuesOut.Add(V); }
    }
    for (double I:Indices) if (I<0 || I>=Count || FMath::FloorToDouble(I)!=I) return TEXT("INVALID_SOURCE_INDEX");
    FMeshDescription Description;FStaticMeshAttributes Attributes(Description);Attributes.Register();
    auto Positions=Attributes.GetVertexPositions();auto Normals=Attributes.GetVertexInstanceNormals();
    auto Tangents=Attributes.GetVertexInstanceTangents();auto Signs=Attributes.GetVertexInstanceBinormalSigns();
    auto Colors=Attributes.GetVertexInstanceColors();auto UVs=Attributes.GetVertexInstanceUVs();UVs.SetNumChannels(UV.Num());
    TArray<FVertexID> Vertices;
    for (int32 I=0;I<Count;++I)
    {
        const auto V=Description.CreateVertex();Vertices.Add(V);
        Positions[V]=FVector3f(-P[I*3]*100,P[I*3+2]*100,P[I*3+1]*100);
    }
    const auto Group=Description.CreatePolygonGroup();
    Attributes.GetPolygonGroupMaterialSlotNames()[Group]=TEXT("SourceSlot0");
    for (int32 Triangle=0;Triangle<Indices.Num();Triangle+=3)
    {
        TArray<FVertexInstanceID,TInlineAllocator<3>> Instances;
        for (int32 Corner=0;Corner<3;++Corner)
        {
            const int32 I=static_cast<int32>(Indices[Triangle+Corner]);
            const auto VI=Description.CreateVertexInstance(Vertices[I]);Instances.Add(VI);
            Normals[VI]=FVector3f(-N[I*3],N[I*3+2],N[I*3+1]);
            if (!T.IsEmpty()) Tangents[VI]=FVector3f(-T[I*4],T[I*4+2],T[I*4+1]);
            Signs[VI]=T.IsEmpty()?1.f:T[I*4+3];
            Colors[VI]=C.IsEmpty()?FVector4f(1,1,1,1):FVector4f(C[I*4],C[I*4+1],C[I*4+2],C[I*4+3]);
            for (int32 Channel=0;Channel<UV.Num();++Channel) UVs.Set(VI,Channel,FVector2f(UV[Channel][I*2],1-UV[Channel][I*2+1]));
        }
        Description.CreatePolygon(Group,Instances);
    }
    Mesh->Modify();Mesh->AddSourceModel();
    auto& Settings=Mesh->GetSourceModel(0).BuildSettings;
    Settings.bRecomputeNormals=false;Settings.bRecomputeTangents=T.IsEmpty();
    Settings.bGenerateLightmapUVs=false;Settings.bUseFullPrecisionUVs=true;Settings.bUseHighPrecisionTangentBasis=true;
    Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr,TEXT("SourceSlot0")));
    Mesh->CreateMeshDescription(0,MoveTemp(Description));Mesh->CommitMeshDescription(0);Mesh->Build(true);Mesh->MarkPackageDirty();
    return FString::Printf(TEXT("BUILT_SOURCE_MESH vertices=%d indices=%d uv_channels=%d colors=%d tangents=%d"),Count,Indices.Num(),UV.Num(),C.Num(),T.Num());
#else
    return TEXT("EDITOR_ONLY");
#endif
}

bool UEGElysiaNativeAuthoringLibrary::SetPrivateCompressionPrecision(UObject* Codec, float ErrorThreshold)
{
#if WITH_EDITOR
    if (!Cast<UAnimBoneCompressionCodec>(Codec) || !FMath::IsFinite(ErrorThreshold)
        || ErrorThreshold <= 0.f || ErrorThreshold > .001f
        || !Codec->GetOutermost()->GetName().StartsWith(TEXT("/Game/Assets/Characters/Elysia/Animations/C1/"))) return false;
    // These codec classes expose editor properties to C++, but Python wraps them as
    // their base class and cannot resolve the subclass's ErrorThreshold property.
    FFloatProperty* Property = FindFProperty<FFloatProperty>(Codec->GetClass(), TEXT("ErrorThreshold"));
    if (!Property) return false;
    Codec->Modify();
    Property->SetPropertyValue_InContainer(Codec, ErrorThreshold);
    Codec->MarkPackageDirty();
    return Property->GetPropertyValue_InContainer(Codec) == ErrorThreshold;
#else
    return false;
#endif
}

FString UEGElysiaNativeAuthoringLibrary::AuditPrivateRendererFrames(USkeletalMesh* Mesh)
{
#if WITH_EDITOR
    if (!Mesh || !Mesh->GetPathName().StartsWith(TEXT("/Game/Assets/Characters/Elysia/Meshes/C1/"))) return TEXT("{}");
    const FMeshDescription* MD = Mesh->GetMeshDescription(0);
    if (!MD) return TEXT("{}");
    FSkeletalMeshConstAttributes Attr(*MD);
    const auto Normals=Attr.GetVertexInstanceNormals(), Tangents=Attr.GetVertexInstanceTangents();
    const auto Signs=Attr.GetVertexInstanceBinormalSigns();
    const auto UVs=Attr.GetVertexInstanceUVs();
    const auto Colors=Attr.GetVertexInstanceColors();
    TArray<TSharedPtr<FJsonValue>> Rows;
    auto Vec=[](const FVector3f& V) {return TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z)};};
    for (FVertexInstanceID VI : MD->VertexInstances().GetElementIDs())
    {
        auto Row=MakeShared<FJsonObject>(); Row->SetNumberField(TEXT("vertex"),MD->GetVertexInstanceVertex(VI).GetValue());
        Row->SetArrayField(TEXT("normal"),Vec(Normals[VI])); Row->SetArrayField(TEXT("tangent"),Vec(Tangents[VI]));
        TArray<TSharedPtr<FJsonValue>> Channels;
        for(int32 Channel=0;Channel<UVs.GetNumChannels();++Channel)
        {
            const FVector2f UV=UVs.Get(VI,Channel);
            Channels.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(UV.X),MakeShared<FJsonValueNumber>(UV.Y)}));
        }
        Row->SetArrayField(TEXT("uv"),Channels);
        const FVector4f Color=Colors[VI];Row->SetArrayField(TEXT("color"),TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(Color.X),MakeShared<FJsonValueNumber>(Color.Y),MakeShared<FJsonValueNumber>(Color.Z),MakeShared<FJsonValueNumber>(Color.W)});
        Row->SetNumberField(TEXT("binormal_sign"),Signs[VI]); Rows.Add(MakeShared<FJsonValueObject>(Row));
    }
    auto Result=MakeShared<FJsonObject>(); Result->SetArrayField(TEXT("instances"),Rows);
    FString Json; auto Writer=TJsonWriterFactory<>::Create(&Json); FJsonSerializer::Serialize(Result,Writer); return Json;
#else
    return TEXT("{}");
#endif
}

bool UEGElysiaNativeAuthoringLibrary::ApplyPrivateSourceBindDocument(USkeletalMesh* Mesh, const FString& Document)
{
#if WITH_EDITOR
    const FString Target=TEXT("/Game/Assets/Characters/Elysia/Meshes/C1/CorrectedParts/SK_C1_Body_A.SK_C1_Body_A");
    if (!Mesh || Mesh->GetPathName()!=Target || Mesh->GetMorphTargets().Num()!=0 || Mesh->GetLODNum()!=1) return false;
    TSharedPtr<FJsonObject> Doc;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Document),Doc) || !Doc.IsValid()) return false;
    FString Candidate;
    if (!Doc->TryGetStringField(TEXT("candidate_mesh"),Candidate) || Candidate!=Target) return false;
    const TArray<TSharedPtr<FJsonValue>> *Positions,*Normals,*Tangents,*Signs,*BoneNames,*Matrices;
    if (!Doc->TryGetArrayField(TEXT("vertices"),Positions) || !Doc->TryGetArrayField(TEXT("normals"),Normals)
        || !Doc->TryGetArrayField(TEXT("tangents"),Tangents) || !Doc->TryGetArrayField(TEXT("binormal_signs"),Signs)
        || !Doc->TryGetArrayField(TEXT("bone_names"),BoneNames) || !Doc->TryGetArrayField(TEXT("reference_local_matrices"),Matrices)) return false;
    FMeshDescription* MD=Mesh->GetMeshDescription(0);
    if (!MD || Positions->Num()!=MD->Vertices().Num() || Normals->Num()!=Positions->Num()
        || Tangents->Num()!=Positions->Num() || Signs->Num()!=Positions->Num()
        || BoneNames->Num()!=Mesh->GetRefSkeleton().GetRawBoneNum() || Matrices->Num()!=BoneNames->Num()) return false;
    for(FVertexID V:MD->Vertices().GetElementIDs())if(V.GetValue()<0 || V.GetValue()>=Positions->Num())return false;
    TArray<FVector3f> P,N,T; TArray<float> S; TArray<FTransform> RefPoses;
    auto ReadVec=[](const TSharedPtr<FJsonValue>& Value,FVector3f& Out)
    {
        const TArray<TSharedPtr<FJsonValue>>* A; if (!Value->TryGetArray(A) || A->Num()!=3) return false;
        for(int32 I=0;I<3;++I){double D;if(!(*A)[I]->TryGetNumber(D)||!FMath::IsFinite(D)||FMath::Abs(D)>1.e6)return false;Out[I]=D;}
        return true;
    };
    for(int32 I=0;I<Positions->Num();++I)
    {
        FVector3f A,B,C; double Sign;
        if (!ReadVec((*Positions)[I],A)||!ReadVec((*Normals)[I],B)||!ReadVec((*Tangents)[I],C)
            || !(*Signs)[I]->TryGetNumber(Sign) || FMath::Abs(FMath::Abs(Sign)-1.)>1.e-6
            || !FMath::IsNearlyEqual(B.SizeSquared(),1.f,.001f) || !FMath::IsNearlyEqual(C.SizeSquared(),1.f,.001f)) return false;
        P.Add(A);N.Add(B);T.Add(C);S.Add(Sign);
    }
    for(int32 I=0;I<BoneNames->Num();++I)
    {
        FString Name; if (!(*BoneNames)[I]->TryGetString(Name) || FName(*Name)!=Mesh->GetRefSkeleton().GetBoneName(I)) return false;
        const TArray<TSharedPtr<FJsonValue>>* Rows;
        if(!(*Matrices)[I]->TryGetArray(Rows)||Rows->Num()!=4)return false;
        FMatrix M=FMatrix::Identity;
        for(int32 R=0;R<4;++R)
        {
            const TArray<TSharedPtr<FJsonValue>>* Cols;if(!(*Rows)[R]->TryGetArray(Cols)||Cols->Num()!=4)return false;
            for(int32 C=0;C<4;++C){double D;if(!(*Cols)[C]->TryGetNumber(D)||!FMath::IsFinite(D))return false;M.M[C][R]=D;}
        }
        FTransform Pose(M); Pose.NormalizeRotation();
        if(Pose.ContainsNaN() || Pose.GetScale3D().GetAbsMin()<1.e-6 || !Pose.ToMatrixWithScale().Equals(M,.0001)) return false;
        RefPoses.Add(Pose);
    }
    // All validation precedes mutation. The shared USkeleton stays intact; each mesh
    // retains its original renderer's bind pose while source animations supply bone poses.
    Mesh->Modify(); Mesh->PreEditChange(nullptr);
    FSkeletalMeshAttributes Attr(*MD);auto Points=Attr.GetVertexPositions();auto VN=Attr.GetVertexInstanceNormals();
    auto VT=Attr.GetVertexInstanceTangents();auto VS=Attr.GetVertexInstanceBinormalSigns();
    for(FVertexID V:MD->Vertices().GetElementIDs()) Points[V]=P[V.GetValue()];
    for(FVertexInstanceID VI:MD->VertexInstances().GetElementIDs())
    {
        int32 I=MD->GetVertexInstanceVertex(VI).GetValue(); VN[VI]=N[I];VT[VI]=T[I];VS[VI]=S[I];
    }
    FReferenceSkeleton Ref=Mesh->GetRefSkeleton();
    {FReferenceSkeletonModifier Modifier(Ref,Mesh->GetSkeleton());for(int32 I=0;I<RefPoses.Num();++I)Modifier.UpdateRefPoseTransform(I,RefPoses[I]);}
    Mesh->SetRefSkeleton(Ref);Mesh->GetRefBasesInvMatrix().Empty();Mesh->CalculateInvRefMatrices();
    if(!Mesh->CommitMeshDescription(0))return false;
    Mesh->PostEditChange();Mesh->MarkPackageDirty();return true;
#else
    return false;
#endif
}

FString UEGElysiaNativeAuthoringLibrary::SamplePrivateSourcePose(UAnimSequence* Animation, USkeletalMesh* Mesh, double Time, bool bCompressed)
{
#if WITH_EDITOR
    const FString AnimationPrefix=TEXT("/Game/Assets/Characters/Elysia/Animations/C1/");
    const FString MeshPrefix=TEXT("/Game/Assets/Characters/Elysia/Meshes/C1/");
    if(!Animation || !Mesh || !Animation->GetPathName().StartsWith(AnimationPrefix) || !Mesh->GetPathName().StartsWith(MeshPrefix)
        || Animation->GetSkeleton()!=Mesh->GetSkeleton() || Animation->IsValidAdditive()
        || !FMath::IsFinite(Time) || Time<0. || Time>Animation->GetPlayLength()+.00001) return TEXT("{}");
    USkeleton* Skeleton=Animation->GetSkeleton();
    const auto& Ref=Mesh->GetRefSkeleton();
    for(int32 I=0;I<Ref.GetNum();++I)
    {
        if(Skeleton->GetReferenceSkeleton().FindBoneIndex(Ref.GetBoneName(I))!=I
            || Skeleton->GetBoneTranslationRetargetingMode(I)!=EBoneTranslationRetargetingMode::Animation) return TEXT("{}");
    }
    Animation->CacheDerivedDataForCurrentPlatform();Animation->WaitOnExistingCompression();
    FMemMark Mark(FMemStack::Get());
    TArray<FBoneIndexType> Bones;for(int32 I=0;I<Ref.GetNum();++I)Bones.Add(I);
    FBoneContainer Container;
    // The sequencer data model skips near-reference controls when evaluating onto
    // a mesh reference pose. Raw provenance samples use their owning skeleton.
    // Runtime compressed samples use the actual candidate mesh and normal runtime flags.
    UObject* Target=bCompressed?static_cast<UObject*>(Mesh):static_cast<UObject*>(Skeleton);
    Container.InitializeTo(Bones,UE::Anim::FCurveFilterSettings(UE::Anim::ECurveFilterMode::DisallowAll),*Target);
    Container.SetUseRAWData(!bCompressed);Container.SetUseSourceData(false);Container.SetDisableRetargeting(false);
    FCompactPose Pose;Pose.SetBoneContainer(&Container);Pose.ResetToRefPose();
    FBlendedCurve Curve;Curve.InitFrom(Container);UE::Anim::FStackAttributeContainer Attributes;
    FAnimationPoseData Data(Pose,Curve,Attributes);
    FAnimExtractContext Context(FMath::Min(Time,Animation->GetPlayLength()),false);
    Context.bIgnoreRootLock=true;Context.bEnforceCompressedDataSampling=bCompressed;
    Animation->GetBonePose(Data,Context,!bCompressed);
    TArray<TSharedPtr<FJsonValue>> Values;
    for(FCompactPoseBoneIndex I:Pose.ForEachBoneIndex())
    {
        const FTransform& P=Pose[I];const FVector V=P.GetTranslation(),S=P.GetScale3D();const FQuat Q=P.GetRotation();
        Values.Add(MakeShared<FJsonValueArray>(TArray<TSharedPtr<FJsonValue>>{MakeShared<FJsonValueNumber>(V.X),MakeShared<FJsonValueNumber>(V.Y),MakeShared<FJsonValueNumber>(V.Z),MakeShared<FJsonValueNumber>(Q.X),MakeShared<FJsonValueNumber>(Q.Y),MakeShared<FJsonValueNumber>(Q.Z),MakeShared<FJsonValueNumber>(Q.W),MakeShared<FJsonValueNumber>(S.X),MakeShared<FJsonValueNumber>(S.Y),MakeShared<FJsonValueNumber>(S.Z)}));
    }
    auto Result=MakeShared<FJsonObject>();Result->SetArrayField(TEXT("local_poses"),Values);Result->SetBoolField(TEXT("compressed_enforced"),bCompressed);
    Result->SetStringField(TEXT("raw_reference"),bCompressed?TEXT("runtime mesh"):TEXT("owning skeleton"));
    FString Json;auto Writer=TJsonWriterFactory<>::Create(&Json);FJsonSerializer::Serialize(Result,Writer);return Json;
#else
    return TEXT("{}");
#endif
}

UObject* UEGElysiaNativeAuthoringLibrary::ConfigurePrivateSafeCompression(UAnimBoneCompressionSettings* Settings, float ErrorThreshold)
{
#if WITH_EDITOR
    if(!Settings || !Settings->GetPathName().StartsWith(TEXT("/Game/Assets/Characters/Elysia/Animations/C1/"))
        || !FMath::IsFinite(ErrorThreshold) || ErrorThreshold<=0.f || ErrorThreshold>.001f)return nullptr;
    UClass* Safe=LoadClass<UAnimBoneCompressionCodec>(nullptr,TEXT("/Script/ACLPlugin.AnimBoneCompressionCodec_ACLSafe"));
    if(!Safe)return nullptr;
    auto* Codec=NewObject<UAnimBoneCompressionCodec>(Settings,Safe,NAME_None,RF_Transactional);
    if(!SetPrivateCompressionPrecision(Codec,ErrorThreshold))return nullptr;
    Settings->Modify();Settings->Codecs={Codec};Settings->MarkPackageDirty();return Codec;
#else
    return nullptr;
#endif
}
