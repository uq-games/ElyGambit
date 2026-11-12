#include "Presentation/EGVillVShotVisual.h"
#include "Presentation/EGVillVCombatFX.h"
#include "Presentation/EGSourceEffect.h"
#include "Character/EGVillVEmplacement.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "EGNativeRendererLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Character/EGChessUnit.h"
#include "Interaction/EGCombatant.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

AEGVillVShotVisual::AEGVillVShotVisual()
{
	PrimaryActorTick.bCanEverTick=true;
	FlightRoot=CreateDefaultSubobject<USceneComponent>(TEXT("FlightRoot"));
	SetRootComponent(FlightRoot);
	Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(FlightRoot);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Body->SetGenerateOverlapEvents(false);
	Body->SetCanEverAffectNavigation(false);
	Body->SetCastShadow(false);
	Trail=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Trail"));
	Trail->SetupAttachment(FlightRoot);
	Trail->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Trail->SetGenerateOverlapEvents(false);
	Trail->SetCanEverAffectNavigation(false);
	Trail->SetCastShadow(false);
	Trail->SetVisibility(false);
	SetActorEnableCollision(false);
	bReplicates=false;
}
AEGVillVShotVisual* AEGVillVShotVisual::Spawn(UEGHeroSkillComponent* InSkill,AEGChessUnit* InTarget,const FVector& Start,const FVector& End,
	UStaticMesh* Mesh,UMaterialInterface* Material,UStaticMesh* TrailMesh,UMaterialInterface* TrailMaterial,
	const FVector& MeshScale,const FRotator& MeshRotation,const FVector& TrailScale,float Duration,float ArcHeight)
{
	(void)InTarget;
	if(!IsValid(InSkill)||!IsValid(Mesh)||!IsValid(Material)||!FMath::IsFinite(Duration)||Duration<=0.f
		||Start.ContainsNaN()||End.ContainsNaN()||MeshScale.ContainsNaN()||!FMath::IsFinite(TrailScale.X)||TrailScale.X<=0.f)return nullptr;
	auto* Unit=Cast<AEGChessUnit>(InSkill->GetOwner());
	if(!Unit||Unit->GetNetMode()==NM_DedicatedServer||!InSkill->Battle.IsValid()||!InSkill->Battle->IsBattleActive())return nullptr;
	FActorSpawnParameters Params;Params.Owner=Unit;
	auto* Shot=Unit->GetWorld()->SpawnActor<AEGVillVShotVisual>(Params);
	if(!Shot)return nullptr;
	Shot->Skill=InSkill;Shot->OwnerUnit=Unit;Shot->RoundId=InSkill->Battle->GetRoundId();
	Shot->StartPoint=Start;Shot->Destination=End;
	Shot->Lifetime=FMath::Clamp(Duration,.01f,2.f);Shot->Arc=FMath::Max(0.f,ArcHeight);
	const FVector Delta=End-Start;
	Shot->Direction=Delta.GetSafeNormal();
	if(Shot->Direction.IsNearlyZero())Shot->Direction=Unit->GetActorForwardVector();
	Shot->Body->SetStaticMesh(Mesh);
	Shot->Body->SetMaterial(0,Material);
	Shot->Body->SetRelativeScale3D(MeshScale);
	Shot->Body->SetRelativeRotation(MeshRotation);
	// Source FX materials read the same custom parameters as AEGSourceEffect. A
	// plain material assignment would leave SourceColor at its default and hide
	// the mesh, so drive the converted source parameters explicitly.
	Shot->BodyMaterial=Shot->Body->CreateDynamicMaterialInstance(0,Material);
	if(Shot->BodyMaterial)Shot->BodyMaterial->SetVectorParameterValue(TEXT("SourceColor"),FLinearColor::White);
	if(IsValid(TrailMesh)&&IsValid(TrailMaterial))
	{
		Shot->Trail->SetStaticMesh(TrailMesh);
		Shot->Trail->SetMaterial(0,TrailMaterial);
		Shot->TrailMaterialDynamic=Shot->Trail->CreateDynamicMaterialInstance(0,TrailMaterial);
		if(Shot->TrailMaterialDynamic)Shot->TrailMaterialDynamic->SetVectorParameterValue(TEXT("SourceColor"),FLinearColor::White);
		// Plane geometry is 100x100cm. Anchor its rear edge at the projectile centre
		// so the trail stretches behind the travelling body.
		Shot->Trail->SetRelativeLocation(FVector(-TrailScale.X*.5f,0.f,0.f));
		Shot->Trail->SetRelativeScale3D(FVector(TrailScale.X/100.f,FMath::Max(TrailScale.Y,1.f)/100.f,FMath::Max(TrailScale.Z,1.f)/100.f));
		Shot->Trail->SetVisibility(true);
	}
	Shot->SetActorLocationAndRotation(Start,Shot->Direction.Rotation());
	return Shot;
}
void AEGVillVShotVisual::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
    if(bV2){TickV2(DeltaSeconds);return;}
	auto* S=Skill.Get();auto* U=OwnerUnit.Get();
	if(!S||!U||!S->Battle.IsValid()||!S->Battle->IsBattleActive()||S->Battle->GetRoundId()!=RoundId||!IEGCombatant::Execute_IsAlive(U))
	{Destroy();return;}
	Elapsed+=DeltaSeconds;
	if(BodyMaterial)
	{
		BodyMaterial->SetScalarParameterValue(TEXT("SourceAge"),Elapsed);
		BodyMaterial->SetScalarParameterValue(TEXT("SourceTimeline"),Elapsed);
	}
	if(TrailMaterialDynamic)
	{
		TrailMaterialDynamic->SetScalarParameterValue(TEXT("SourceAge"),Elapsed);
		TrailMaterialDynamic->SetScalarParameterValue(TEXT("SourceTimeline"),Elapsed);
	}
	const float Alpha=Lifetime>SMALL_NUMBER?FMath::Clamp(Elapsed/Lifetime,0.f,1.f):1.f;
	FVector Position=FMath::Lerp(StartPoint,Destination,Alpha);
	if(Arc>0.f)Position.Z+=Arc*FMath::Sin(PI*Alpha);
	SetActorLocationAndRotation(Position,Direction.Rotation());
	if(Alpha>=1.f)Destroy();
}
void AEGVillVShotVisual::EndPlay(const EEndPlayReason::Type Reason)
{
    ClearV2();
	Body=nullptr;Trail=nullptr;BodyMaterial=nullptr;TrailMaterialDynamic=nullptr;Skill.Reset();OwnerUnit.Reset();
	Super::EndPlay(Reason);
}


AEGVillVShotVisual* AEGVillVShotVisual::SpawnV2(UEGHeroSkillComponent* InSkill,AEGVillVEmplacement* InDevice,
    UEGVillVCombatFXDefinition* Config,int32 PresetIndex,int32 BarrelIndex,FName Bone,
    const FTransform& Muzzle,const FVector& Target,const FGuid& InAction,bool bIndependent)
{
    if(!IsValid(InSkill)||!IsValid(InDevice)||!IsValid(Config)||!Config->ShotPresetsV2.IsValidIndex(PresetIndex)
       ||!InAction.IsValid()||(!bIndependent&&InSkill->ActionId!=InAction)||Bone.IsNone()||Muzzle.ContainsNaN()||Target.ContainsNaN()
       ||!InSkill->Battle.IsValid()||!InSkill->Battle->IsBattleActive())return nullptr;
    auto* Unit=Cast<AEGChessUnit>(InSkill->GetOwner());
    if(!Unit||!Unit->GetWorld()||Unit->GetNetMode()==NM_DedicatedServer||!IEGCombatant::Execute_IsAlive(Unit)
       ||!IEGCombatant::Execute_IsAlive(InDevice))return nullptr;
    const FEGVillVShotV2Preset& Preset=Config->ShotPresetsV2[PresetIndex];
    if(!FMath::IsFinite(Preset.FlightSpeed)||Preset.FlightSpeed<=SMALL_NUMBER)return nullptr;
    const double Distance=FVector::Distance(Muzzle.GetLocation(),Target);
    if(!FMath::IsFinite(Distance))return nullptr;
    const FGuid ExpectedRound=InSkill->Battle->GetRoundId();
    FActorSpawnParameters Params;Params.Owner=Unit;Params.ObjectFlags|=RF_Transient;
    auto* Shot=Unit->GetWorld()->SpawnActor<AEGVillVShotVisual>(Params);
    if(!IsValid(Shot))return nullptr;
    // Actor-spawn observers can synchronously tear down a round or action.
    if(!IsValid(Unit)||!IsValid(InSkill)||!IsValid(InDevice)||!IsValid(Config)
       ||!IEGCombatant::Execute_IsAlive(Unit)||!IEGCombatant::Execute_IsAlive(InDevice)
       ||!InSkill->Battle.IsValid()||!InSkill->Battle->IsBattleActive()
       ||InSkill->Battle->GetRoundId()!=ExpectedRound||(!bIndependent&&InSkill->ActionId!=InAction))
    {Shot->Destroy();return nullptr;}
    Shot->bIndependentShot=bIndependent;Shot->bV2=true;Shot->Skill=InSkill;Shot->OwnerUnit=Unit;Shot->Device=InDevice;
    Shot->V2Config=Config;Shot->ActionId=InAction;Shot->MuzzleBone=Bone;
    Shot->RoundId=InSkill->Battle->GetRoundId();Shot->FrozenMuzzle=Muzzle;
    Shot->StartPoint=Muzzle.GetLocation();Shot->Destination=Target;
    Shot->Direction=(Target-Shot->StartPoint).GetSafeNormal();
    if(Shot->Direction.IsNearlyZero())Shot->Direction=Muzzle.GetRotation().GetForwardVector();
    Shot->V2FlightTime=static_cast<float>(FMath::Clamp(Distance/Preset.FlightSpeed,0.,2.));
    Shot->Body->SetVisibility(false);Shot->Trail->SetVisibility(false);
    Shot->AddTickPrerequisiteComponent(InSkill);Shot->AddTickPrerequisiteActor(InDevice);
    if(!Shot->InitializeV2(Config,PresetIndex,BarrelIndex)){Shot->Destroy();return nullptr;}
    Shot->TickV2(0.f);
    return IsValid(Shot)?Shot:nullptr;
}

bool AEGVillVShotVisual::InitializeV2(UEGVillVCombatFXDefinition* Config,int32 PresetIndex,int32 BarrelIndex)
{
    if(!IsValid(Config)||!Config->ShotPresetsV2.IsValidIndex(PresetIndex))return false;
    const FEGVillVShotV2Preset& Preset=Config->ShotPresetsV2[PresetIndex];
    ReleaseAnchor=FrozenMuzzle;
    if(Preset.ReleaseLocalUps.IsValidIndex(BarrelIndex))
    {
        const FVector LocalUp=Preset.ReleaseLocalUps[BarrelIndex];
        const FVector Forward=FrozenMuzzle.GetRotation().GetForwardVector().GetSafeNormal();
        if(!LocalUp.ContainsNaN()&&!Forward.IsNearlyZero())
        {
            const FVector CurrentUp=FVector::VectorPlaneProject(FrozenMuzzle.GetRotation().RotateVector(LocalUp),Forward);
            const FVector WorldUp=FVector::VectorPlaneProject(FVector::UpVector,Forward);
            // Near vertical there is no stable projected world-up axis: retain
            // the authored rotation rather than amplifying a tiny direction.
            if(CurrentUp.SizeSquared()>1.e-4&&WorldUp.SizeSquared()>1.e-4)
            {
                const FVector A=CurrentUp.GetSafeNormal(),B=WorldUp.GetSafeNormal();
                const double Angle=FMath::Atan2(FVector::DotProduct(Forward,FVector::CrossProduct(A,B)),FVector::DotProduct(A,B));
                if(FMath::IsFinite(Angle))ReleaseAnchor.SetRotation(FQuat(Forward,Angle)*FrozenMuzzle.GetRotation());
            }
        }
    }
    auto AddTracks=[this](UEGSourceEffectDefinition* Data,int32 Layer)
    {
        float End=0.f;
        if(!IsValid(Data)||!FMath::IsFinite(Data->PresentationScale)||Data->PresentationScale<=0.f)return End;
        for(int32 Index=0;Index<Data->Tracks.Num();++Index)
        {
            const auto& Track=Data->Tracks[Index];
            if(!FMath::IsFinite(Track.StartTime)||!FMath::IsFinite(Track.Duration)||Track.StartTime<0.f
               ||Track.Duration<=0.f||Track.StartTime+Track.Duration>4.f||Track.Samples.IsEmpty()||Track.bSourceDistortion)
            {
                UE_LOG(LogTemp,Warning,TEXT("VillV V2 unsupported sampled track %d in %s; skipped"),Index,*GetPathNameSafe(Data));
                continue;
            }
            FEGVillVShotV2Track Slot;Slot.Data=Data;Slot.Index=Index;Slot.Layer=Layer;V2Tracks.Add(Slot);
            End=FMath::Max(End,Track.StartTime+Track.Duration);
        }
        return End;
    };
    float MaxLife=0.f;
    MaxLife=FMath::Max(MaxLife,AddTracks(Preset.MuzzleEffects.IsValidIndex(BarrelIndex)?Preset.MuzzleEffects[BarrelIndex].Get():nullptr,0));
    MaxLife=FMath::Max(MaxLife,AddTracks(Preset.ReleaseEffects.IsValidIndex(BarrelIndex)?Preset.ReleaseEffects[BarrelIndex].Get():nullptr,1));
    const float ProjectileLife=AddTracks(Preset.ProjectileEffect,2);
    if(ProjectileLife>0.f)MaxLife=FMath::Max(MaxLife,V2FlightTime);
    const float ImpactLife=AddTracks(Preset.ImpactEffect,3);
    if(ImpactLife>0.f)MaxLife=FMath::Max(MaxLife,V2FlightTime+ImpactLife);
    if(IsValid(Preset.SparkSystem)&&FMath::IsFinite(Preset.SparkLifetime)&&Preset.SparkLifetime>0.f)
    {
        bool bSupported=true;
        for(const FNiagaraEmitterHandle& Handle:Preset.SparkSystem->GetEmitterHandles())
        {
            if(!Handle.GetIsEnabled())continue;
            const FVersionedNiagaraEmitterData* Emitter=Handle.GetEmitterData();
            if(!Emitter||Emitter->SimTarget!=ENiagaraSimTarget::CPUSim||!Emitter->bDeterminism){bSupported=false;break;}
        }
        if(bSupported)
        {
            V2SparkLifetime=FMath::Clamp(Preset.SparkLifetime,0.f,.5f);
            V2Spark=NewObject<UNiagaraComponent>(this,NAME_None,RF_Transient);
            if(V2Spark)
            {
                AddInstanceComponent(V2Spark);V2Spark->ComponentTags.Add(TEXT("VillVShotNiagara"));
                V2Spark->SetupAttachment(GetRootComponent());V2Spark->SetAbsolute(true,true,true);
                V2Spark->SetAutoActivate(false);V2Spark->SetAutoDestroy(false);V2Spark->SetAsset(Preset.SparkSystem);
                const uint32 Seed=HashCombineFast(GetTypeHash(ActionId),GetTypeHash(BarrelIndex));
                V2Spark->SetRandomSeedOffset(static_cast<int32>(Seed));
                V2Spark->RegisterComponent();V2Spark->SetWorldTransform(FrozenMuzzle);V2Spark->Activate(true);
                MaxLife=FMath::Max(MaxLife,V2SparkLifetime);
            }
        }
        else UE_LOG(LogTemp,Warning,TEXT("VillV V2 Niagara must use deterministic CPU emitters; skipped"));
    }
    Lifetime=FMath::Clamp(MaxLife,0.f,4.f);
    return Lifetime>SMALL_NUMBER;
}

void AEGVillVShotVisual::TickV2(float DeltaSeconds)
{
    auto* S=Skill.Get();auto* U=OwnerUnit.Get();auto* D=Device.Get();
    if(!IsValid(S)||!IsValid(U)||!IsValid(D)||!IEGCombatant::Execute_IsAlive(U)||!IEGCombatant::Execute_IsAlive(D)
       ||!S->Battle.IsValid()||!S->Battle->IsBattleActive()||S->Battle->GetRoundId()!=RoundId)
    {Destroy();return;}
    Elapsed+=FMath::IsFinite(DeltaSeconds)?FMath::Max(0.f,DeltaSeconds):0.f;
    if(Elapsed>=Lifetime){Destroy();return;}
    if(IsValid(V2Spark)&&Elapsed>=V2SparkLifetime)
    {
        V2Spark->DeactivateImmediate();RemoveInstanceComponent(V2Spark);V2Spark->DestroyComponent();V2Spark=nullptr;
    }
    FTransform LiveMuzzle;
    const bool bMuzzleActive=(bIndependentShot||(S->ActionId==ActionId&&S->IsActionRunning()))&&D->MuzzleFrame(MuzzleBone,LiveMuzzle);
    if(bMuzzleActive)
    {
        const FQuat Axis=FQuat::FindBetweenNormals(FVector::ForwardVector,FVector::UpVector);
        LiveMuzzle.SetRotation(LiveMuzzle.GetRotation()*Axis);LiveMuzzle.SetScale3D(FVector::OneVector);
    }
    const FQuat FlightRotation=Direction.Rotation().Quaternion();
    const float Alpha=V2FlightTime>SMALL_NUMBER?FMath::Clamp(Elapsed/V2FlightTime,0.f,1.f):1.f;
    const FTransform Flight(FlightRotation,FMath::Lerp(StartPoint,Destination,Alpha),FVector::OneVector);
    const FTransform Impact(FlightRotation,Destination,FVector::OneVector);
    for(FEGVillVShotV2Track& Slot:V2Tracks)
    {
        UEGSourceEffectDefinition* Data=Slot.Data;
        if(!IsValid(Data)||!Data->Tracks.IsValidIndex(Slot.Index))continue;
        const FEGSourceEffectTrack& Track=Data->Tracks[Slot.Index];
        const float Local=Slot.Layer==3?Elapsed-V2FlightTime:Elapsed;
        const float Age=Local-Track.StartTime;
        const bool bPhase=Slot.Layer==0?bMuzzleActive:Slot.Layer==2?Elapsed<V2FlightTime:Slot.Layer==3?Elapsed>=V2FlightTime:true;
        if(!bPhase||Age<0.f||Age>=Track.Duration||!Track.IsSourceVisible(Age))
        {if(IsValid(Slot.Component))Slot.Component->SetVisibility(false);continue;}
        if(!IsValid(Slot.Component))
        {
            UStaticMesh* Geometry=Track.Mesh.LoadSynchronous();UMaterialInterface* Material=Track.Material.LoadSynchronous();
            if(!IsValid(Geometry)||!IsValid(Material))continue;
            auto* Part=NewObject<UStaticMeshComponent>(this,NAME_None,RF_Transient);
            AddInstanceComponent(Part);Part->SetupAttachment(GetRootComponent());Part->SetAbsolute(true,true,true);
            Part->ComponentTags.Add(Slot.Layer==0?FName(TEXT("VillVShotMuzzle")):Slot.Layer==1?FName(TEXT("VillVShotRelease"))
                :Slot.Layer==2?FName(TEXT("VillVShotProjectile")):FName(TEXT("VillVShotImpact")));
            Part->SetStaticMesh(Geometry);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetGenerateOverlapEvents(false);
            Part->SetCanEverAffectNavigation(false);Part->SetCastShadow(false);
            Part->SetRenderInMainPass(!Track.bSourceDepthOnly);Part->SetRenderInDepthPass(true);
            Part->SetMaterial(0,Material);Part->RegisterComponent();
            Slot.Component=Part;Slot.Material=Part->CreateDynamicMaterialInstance(0);
        }
        if(!Track.ApplyNativeRendering(Slot.Component,this,Age)){Slot.Component->SetVisibility(false);continue;}
        const float Frame=FMath::Clamp(Age/Track.Duration,0.f,1.f)*(Track.Samples.Num()-1);
        const int32 Low=FMath::FloorToInt(Frame),High=FMath::Min(Low+1,Track.Samples.Num()-1);
        const float Blend=Frame-Low;
        FTransform Sample;Sample.Blend(Track.Samples[Low].Transform,Track.Samples[High].Transform,Blend);
        FTransform Anchor=Slot.Layer==0?LiveMuzzle:Slot.Layer==1?ReleaseAnchor:Slot.Layer==2?Flight:Impact;
        Anchor.SetScale3D(FVector(Data->PresentationScale));
        const FTransform Result=Sample*Anchor;
        if(Result.ContainsNaN()){Slot.Component->SetVisibility(false);continue;}
        Slot.Component->SetWorldTransform(Result);Slot.Component->SetVisibility(true);
        if(Track.bCameraFacing)if(auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0))
            Slot.Component->SetWorldRotation(Track.SourceBillboardAlignment==INDEX_NONE
                ? FRotationMatrix::MakeFromZ(Camera->GetCameraLocation()-Slot.Component->GetComponentLocation()).ToQuat()
                : EGSourceBillboardRotation(Sample.GetRotation(),Track.SourceBillboardAlignment==0
                    ? -Camera->GetCameraRotation().Vector():Camera->GetCameraLocation()-Slot.Component->GetComponentLocation(),
                    Camera->GetCameraRotation().Quaternion().GetRightVector()));
        if(UMaterialInstanceDynamic* Material=Slot.Material)
        {
            Material->SetVectorParameterValue(TEXT("SourceColor"),FMath::Lerp(Track.Samples[Low].Color,Track.Samples[High].Color,Blend));
            Material->SetVectorParameterValue(TEXT("SourceParticleUV"),Track.Samples[Low].TextureUV);
            Material->SetVectorParameterValue(TEXT("SourceCustom1"),FMath::Lerp(Track.Samples[Low].CustomData1,Track.Samples[High].CustomData1,Blend));
            Material->SetVectorParameterValue(TEXT("SourceCustom2"),FMath::Lerp(Track.Samples[Low].CustomData2,Track.Samples[High].CustomData2,Blend));
            Material->SetScalarParameterValue(TEXT("SourceAge"),Age);
            Material->SetScalarParameterValue(TEXT("SourceTimeline"),Local);
        }
    }
}

void AEGVillVShotVisual::ClearV2()
{
    if(IsValid(V2Spark)){V2Spark->DeactivateImmediate();RemoveInstanceComponent(V2Spark);V2Spark->DestroyComponent();}
    V2Spark=nullptr;
    for(FEGVillVShotV2Track& Slot:V2Tracks)
    {
        if(IsValid(Slot.Component))
        {
            UEGNativeRendererLibrary::ReleaseSourceDefinition(Slot.Component,this);
            RemoveInstanceComponent(Slot.Component);Slot.Component->DestroyComponent();
        }
    }
    V2Tracks.Reset();V2Config=nullptr;Device.Reset();
}
