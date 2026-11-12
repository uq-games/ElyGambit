#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "EGElysiaNativeAuthoringLibrary.generated.h"

/** Editor bridges for source-fidelity settings absent from Unreal's Python wrappers. */
UCLASS()
class ELYGAMBITEDITOR_API UEGElysiaNativeAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Save the exact formal package identity into Saved, without replacing a file held by another editor. */
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static FString StageFormalPresentationPackage(class UEGHeroDefinition* Hero,class UEGHeroPresentationDefinition* Presentation);
    /** Only private native C1 codec subobjects may be edited; no engine defaults are changed. */
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static bool SetPrivateCompressionPrecision(UObject* Codec, float ErrorThreshold);
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static FString AuditPrivateRendererFrames(class USkeletalMesh* Mesh);
    /** Apply source geometry and per-renderer bind pose to a private, morph-free candidate copy. */
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static bool ApplyPrivateSourceBindDocument(class USkeletalMesh* Mesh, const FString& Document);
    /** Explicit compressed sampling rejects UE's editor raw-data fallback. */
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static FString SamplePrivateSourcePose(class UAnimSequence* Animation, class USkeletalMesh* Mesh, double Time, bool bCompressed);
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static UObject* ConfigurePrivateSafeCompression(class UAnimBoneCompressionSettings* Settings, float ErrorThreshold);
    /** Build an empty private static candidate from original source vertex streams. */
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static FString BuildPrivateStaticSourceMesh(class UStaticMesh* Mesh, const FString& Document);
    UFUNCTION(BlueprintCallable, Category="Elysia Native|Authoring")
    static class UStaticMesh* CreatePrivateStaticSourceMesh(const FString& PackageName,const FString& Document);
};
