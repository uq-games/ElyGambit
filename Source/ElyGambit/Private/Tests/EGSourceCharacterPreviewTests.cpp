#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Presentation/EGSourceCharacterPreview.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Presentation/EGSourceEffect.h"
#include "Curves/CurveFloat.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSourcePreviewLifetimeTest,
    "ElyGambit.SourcePreview.SeekSwitchAndCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGSourcePreviewLifetimeTest::RunTest(const FString&)
{
    auto* Definition=LoadObject<UEGSourcePreviewDefinition>(nullptr,
        TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Mobius_SourcePreview.DA_Mobius_SourcePreview"));
    if(!TestNotNull(TEXT("Mobius source preview definition"),Definition)) return false;
    if(!TestTrue(TEXT("Original idle, normals and ultimate phases available"),Definition->Motions.Num()>=10)) return false;
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
    ON_SCOPE_EXIT{Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Viewer=World->SpawnActor<AEGSourceCharacterPreview>();
    Viewer->Definition=Definition;Viewer->bAutoPlay=false;Viewer->bLoop=false;
    auto Parts=[&](){TArray<USkeletalMeshComponent*> Result;Viewer->GetComponents(Result);return Result;};
    for(int32 Pass=0;Pass<3;++Pass) for(int32 Index=0;Index<10;++Index)
    {
        Viewer->MotionIndex=Index;Viewer->SetPreviewTime(Definition->Motions[Index].Duration*.5f);
        auto Components=Parts();
        if(!TestEqual(TEXT("Switching owns exactly one component per part"),Components.Num(),Definition->Motions[Index].Parts.Num()))return false;
        TestFalse(TEXT("Viewer never replicates"),Viewer->GetIsReplicated());
        for(auto* Component:Components)
        {
            TestTrue(TEXT("Preview parts cannot be duplicated into PIE"),Component->HasAnyFlags(RF_DuplicateTransient));
            TestEqual(TEXT("No collision"),Component->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
            TestTrue(TEXT("Component remains owned by viewer"),Component->GetOwner()==Viewer);
        }
        Viewer->Pause();const float Time=Viewer->PreviewTime;Viewer->Tick(.25f);
        TestEqual(TEXT("Pause holds exact source time"),Viewer->PreviewTime,Time);
    }
    Viewer->MotionIndex=0;Viewer->SetPreviewTime(0);Viewer->Play();Viewer->Tick(.1f);
    TestTrue(TEXT("Playing advances source time"),Viewer->PreviewTime>.09f);
    Viewer->SetPreviewTime(Definition->Motions[0].Duration-.01f);Viewer->Play();Viewer->Tick(.1f);
    TestFalse(TEXT("Nonlooping playback stops at final sample"),Viewer->bPlaying);
    Viewer->Definition=nullptr;Viewer->SetPreviewTime(0);
    TestEqual(TEXT("Clearing definition removes owned components"),Parts().Num(),0);
    auto* VillV=LoadObject<UEGSourcePreviewDefinition>(nullptr,
        TEXT("/Game/Blueprints/Characters/VillV/Data/DA_VillV_SourcePreview.DA_VillV_SourcePreview"));
    if(TestNotNull(TEXT("Vill-V source equipment definition"),VillV))
    {
        Viewer->Definition=VillV;int32 CheckedDevices=0;
        for(int32 Motion=0;Motion<VillV->Motions.Num();++Motion)
        {
            Viewer->MotionIndex=Motion;
            for(float Fraction:{0.f,.5f,1.f})
            {
                Viewer->SetPreviewTime(VillV->Motions[Motion].Duration*Fraction);
                auto Components=Parts();
                if(!TestEqual(TEXT("Each separate source surface owns its own component"),Components.Num(),VillV->Motions[Motion].Parts.Num()))return false;
                for(int32 Index=0;Index<Components.Num();++Index)
                {
                    const auto& Source=VillV->Motions[Motion].Parts[Index];
                    const FName Tag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),Index));
                    auto* const* Match=Components.FindByPredicate([&](const auto* Component){return Component->ComponentHasTag(Tag);});
                    if(!TestNotNull(TEXT("Source part has a uniquely identified component"),Match))return false;
                    auto* Component=*Match;
                    TestTrue(TEXT("Component uses its own mesh asset"),Component->GetSkeletalMeshAsset()==Source.Mesh);
                    TestTrue(TEXT("Floating plates are excluded from final compositions"),Source.PartId!=TEXT("Add001") && Source.PartId!=TEXT("Add002"));
                    TestTrue(TEXT("Animation belongs to this part's skeleton"),Source.Animation && Source.Animation->GetSkeleton()==Source.Mesh->GetSkeleton());
                    if(Source.Visibility && Viewer->PreviewTime>=Source.StartTime && Viewer->PreviewTime<Source.StartTime+Source.Duration)
                    {
                        const bool Expected=Source.bVisible && Source.Visibility->GetFloatValue(Viewer->PreviewTime-Source.StartTime)>.5f;
                        TestEqual(TEXT("Independent source device obeys its visibility curve"),Component->IsVisible(),Expected);
                        ++CheckedDevices;
                    }
                }
            }
        }
        TestTrue(TEXT("Independent source devices were checked throughout the action timeline"),CheckedDevices>50);
        for(const auto& Expected:TArray<TPair<FString,FString>>{
            {TEXT("Gatlin_Attack_Loop"),TEXT("Gatling")},
            {TEXT("Cannon_Attack"),TEXT("Cannon")},
            {TEXT("SKL02_ChariotMove_Loop"),TEXT("Chariot")}})
        {
            const int32 Index=VillV->Motions.IndexOfByPredicate([&](const auto& M){return M.Label.ToString().EndsWith(Expected.Key);});
            if(!TestTrue(TEXT("Source device action exists"),Index!=INDEX_NONE))return false;
            Viewer->MotionIndex=Index;Viewer->SetPreviewTime(VillV->Motions[Index].Duration*.5f);
            int32 Visible=0;
            for(auto* Component:Parts())if(Component->IsVisible() && Component->GetSkeletalMeshAsset()->GetName()==TEXT("SK_VillV_")+Expected.Value)++Visible;
            TestTrue(TEXT("The source-enabled device is actually shown, overriding the inactive prefab default"),Visible>0);
        }
    }
    // A looping device may repeat its clip only inside its explicit time window.
    TStrongObjectPtr<UEGSourcePreviewDefinition> WindowDefinition(NewObject<UEGSourcePreviewDefinition>());
    FEGSourcePreviewMotion WindowMotion;
    auto WindowPart=Definition->Motions[0].Parts[0];
    WindowPart.StartTime=.25f;WindowPart.Duration=.5f;WindowPart.bLoop=true;
    WindowMotion.Duration=2.f;WindowMotion.Parts.Add(WindowPart);
    WindowDefinition->Motions.Add(WindowMotion);
    Viewer->Definition=WindowDefinition.Get();Viewer->MotionIndex=0;
    Viewer->SetPreviewTime(.5f);TestTrue(TEXT("Looping device shown inside source window"),Parts()[0]->IsVisible());
    Viewer->SetPreviewTime(1.f);TestFalse(TEXT("Looping device hidden after source window"),Parts()[0]->IsVisible());
    World->DestroyActor(Viewer);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSourcePreviewEffectSeekTest,
    "ElyGambit.SourcePreview.EffectSeekScaleAndCleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FEGSourcePreviewEffectSeekTest::RunTest(const FString&)
{
    TStrongObjectPtr<UGameInstance> Instance(NewObject<UGameInstance>(GEngine));
    Instance->InitializeStandalone();UWorld* World=Instance->GetWorld();
    ON_SCOPE_EXIT{Instance->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Viewer=World->SpawnActor<AEGSourceCharacterPreview>();
    Viewer->SetActorLocation(FVector(100,0,0));Viewer->SetActorScale3D(FVector(2));
    TStrongObjectPtr<UEGSourceEffectDefinition> Data(NewObject<UEGSourceEffectDefinition>());
    auto* Mesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    if(!TestNotNull(TEXT("Source effect fixture mesh"),Mesh))return false;
    FEGSourceEffectTrack Track;Track.Mesh=Mesh;Track.Material=Mesh->GetMaterial(0);Track.Duration=1;
    FEGSourceEffectSample Start,End;End.Transform.SetTranslation(FVector(100,0,0));
    Track.Samples={Start,End};Data->Tracks.Add(Track);
    TStrongObjectPtr<UEGSourcePreviewDefinition> Definition(NewObject<UEGSourcePreviewDefinition>());
    FEGSourcePreviewEffect Binding;Binding.Effect=Data.Get();Binding.StartTime=.25f;Binding.Duration=.5f;
    Binding.Transform.SetTranslation(FVector(10,0,0));
    FEGSourcePreviewMotion Motion;Motion.Duration=2;Motion.Effects.Add(Binding);
    Definition->Motions.Add(Motion);Definition->Motions.Add(FEGSourcePreviewMotion());
    Viewer->Definition=Definition.Get();Viewer->SetPreviewTime(.5f);
    auto Components=[&](){TArray<UStaticMeshComponent*> Result;Viewer->GetComponents(Result);return Result;};
    if(!TestEqual(TEXT("Seeking directly into an effect allocates one owned component"),Components().Num(),1))return false;
    TestTrue(TEXT("Source centimetres and viewer scale are applied once"),FMath::IsNearlyEqual(Components()[0]->GetComponentLocation().X,170.f,.01f));
    TestEqual(TEXT("Cosmetic effects have no collision"),Components()[0]->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Effect is not duplicated into PIE"),Components()[0]->HasAnyFlags(RF_DuplicateTransient));
    Viewer->SetPreviewTime(.1f);TestFalse(TEXT("Backward seek hides an effect before its start"),Components()[0]->IsVisible());
    Viewer->SetPreviewTime(1.f);TestFalse(TEXT("Explicit effect window hides remaining particle lifetime"),Components()[0]->IsVisible());
    Viewer->SetPreviewTime(.5f);TestEqual(TEXT("Rewinding reuses the component"),Components().Num(),1);
    Viewer->MotionIndex=1;Viewer->SetPreviewTime(0);TestEqual(TEXT("Switching releases all effect components"),Components().Num(),0);
    Viewer->Definition=nullptr;Viewer->SetPreviewTime(0);TestEqual(TEXT("Clearing remains idempotent"),Components().Num(),0);
    auto* Character=LoadObject<UEGSourcePreviewDefinition>(nullptr,
        TEXT("/Game/Blueprints/Characters/Mobius/Data/DA_Mobius_SourcePreview.DA_Mobius_SourcePreview"));
    if(TestNotNull(TEXT("Animated source anchor fixture"),Character) && !Character->Motions.IsEmpty())
    {
        Definition->Motions[0].Parts={Character->Motions[0].Parts[0]};
        auto& Source=Definition->Motions[0].Parts[0];
        Definition->Motions[0].Duration=Source.Animation->GetPlayLength();
        Definition->Motions[0].Effects[0].StartTime=0;
        Definition->Motions[0].Effects[0].Duration=0;
        Definition->Motions[0].Effects[0].AnchorPartId=Source.PartId;
        const auto& Bones=Source.Mesh->GetRefSkeleton();
        FName Bone;
        for(int32 I=0;I<Bones.GetNum();++I)if(Bones.GetBoneName(I).ToString().Contains(TEXT("Hand"))){Bone=Bones.GetBoneName(I);break;}
        TestFalse(TEXT("Animated anchor bone exists"),Bone.IsNone());
        Definition->Motions[0].Effects[0].AnchorBone=Bone;
        Data->Tracks[0].bWorldSimulation=true;Data->Tracks[0].Samples={Start,Start};
        Viewer->Definition=Definition.Get();Viewer->MotionIndex=0;Viewer->SetPreviewTime(.2f);
        if(TestEqual(TEXT("World-space fixture owns one effect"),Components().Num(),1))
        {
            const FVector Birth=Components()[0]->GetComponentLocation();
            Viewer->SetPreviewTime(.7f);
            TestTrue(TEXT("World-simulated particle holds its birth anchor while the hand animates"),Components()[0]->GetComponentLocation().Equals(Birth,.01f));
            Viewer->SetPreviewTime(.1f);
            TestTrue(TEXT("Backward seek retains deterministic birth anchor"),Components()[0]->GetComponentLocation().Equals(Birth,.01f));
        }
    }
    World->DestroyActor(Viewer);return true;
}
#endif
