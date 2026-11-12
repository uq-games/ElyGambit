#include "EGVillVFXsFrame.h"
#include "Presentation/EGVillVFXsPreview.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
void EGEvaluateVillVFXsFrame(const FEGVillVFXsLayer& Layer,UNiagaraComponent* Component,float Age,const FTransform& Anchor,const FTransform& Muzzle,const FVector& Target,float Scale,const FVector& CameraLocation,const FRotator& ViewRotation,TMap<int32,FTransform>& BirthAnchors,int32 KeyOffset)
{
        TArray<FVector4> Positions,Scales,Rotations,Colors,UVs,Custom1,Custom2;
        for(int32 Index=0;Index<Layer.Samples->Tracks.Num();++Index)
        {
            const auto& Track=Layer.Samples->Tracks[Index];const float T=Age-Track.StartTime;
            FTransform Sample;FLinearColor C(0,0,0,0),UV(1,1,0,0),C1(0,0,0,0),C2(0,0,0,0);
            bool Visible=T>=0&&T<=Track.Duration&&!Track.Samples.IsEmpty();
            if(Visible)
            {
                const float F=FMath::Clamp(T/FMath::Max(.001f,Track.Duration),0.f,1.f)*(Track.Samples.Num()-1);int32 A=FMath::FloorToInt(F),B=FMath::Min(A+1,Track.Samples.Num()-1);const float Alpha=F-A;
                const auto& SA=Track.Samples[A];const auto& SB=Track.Samples[B];Sample.Blend(SA.Transform,SB.Transform,Alpha);
                C=FMath::Lerp(SA.Color,SB.Color,Alpha);UV=FMath::Lerp(SA.TextureUV,SB.TextureUV,Alpha);C1=FMath::Lerp(SA.CustomData1,SB.CustomData1,Alpha);C2=FMath::Lerp(SA.CustomData2,SB.CustomData2,Alpha);
            }
            FTransform Birth=Anchor;const int32 Key=KeyOffset+Index;
            if(Track.bWorldSimulation){if(const auto* Existing=BirthAnchors.Find(Key))Birth=*Existing;else if(Visible)BirthAnchors.Add(Key,Birth);}
            FVector Position=Birth.TransformPosition(Sample.GetLocation()*Scale);
            FQuat Q=Birth.GetRotation()*Sample.GetRotation();
            if(Track.bCameraFacing)Q=EGSourceBillboardRotation(Sample.GetRotation(),Track.SourceBillboardAlignment==0?-ViewRotation.Vector():CameraLocation-Position,FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y));
            FVector S=Visible?Sample.GetScale3D()*Scale:FVector::ZeroVector;
            if(Visible)if(const auto* Native=Cast<UEGVillVFXsSampleDefinition>(Layer.Samples);Native&&Native->SourceRenderers.IsValidIndex(Index))
            {
                const auto& Renderer=Native->SourceRenderers[Index];
                if(Renderer.RenderMode==1)
                {
                    FVector Velocity=FVector::ZeroVector;
                    if(Track.Samples.Num()>1)
                    {
                        const int32 I=FMath::Clamp(FMath::FloorToInt(T/FMath::Max(.001f,Track.Duration)*(Track.Samples.Num()-1)),0,Track.Samples.Num()-2);
                        Velocity=Birth.TransformVectorNoScale((Track.Samples[I+1].Transform.GetLocation()-Track.Samples[I].Transform.GetLocation())*((Track.Samples.Num()-1)/FMath::Max(.001f,Track.Duration)))*Scale;
                    }
                    const float ParticleSpeed=Velocity.Size();
                    if(Layer.Anchor==EEGVillVFXsAnchor::Projectile&&Age<Layer.FlightDuration)Velocity+=(Target-Muzzle.GetLocation())/FMath::Max(.01f,Layer.FlightDuration);
                    // Unity stretched billboards combine the original particle-size
                    // multiplier with speed in source metres per second. Geometry
                    // is a 100 cm plane, so the velocity term converts cm back to m.
                    S.X=S.X*Renderer.LengthScale+Renderer.VelocityScale*ParticleSpeed*.01f;
                    const FVector Normal=-ViewRotation.Vector();
                    const FVector Along=FVector::VectorPlaneProject(Velocity,Normal);
                    if(!Along.IsNearlyZero())Q=FRotationMatrix::MakeFromXZ(Along.GetSafeNormal(),Normal).ToQuat();
                }
                if(Renderer.RenderMode!=4)Position-=Q.RotateVector(FVector(Renderer.Pivot.X*S.X,Renderer.Pivot.Y*S.Y,Renderer.Pivot.Z*S.Z)*100.f);
            }
            Positions.Add(FVector4(Position,Age));Scales.Add(FVector4(S,0));Rotations.Add(FVector4(Q.X,Q.Y,Q.Z,Q.W));Colors.Add(FVector4(C.R,C.G,C.B,C.A));UVs.Add(FVector4(UV.R,UV.G,UV.B,UV.A));Custom1.Add(FVector4(C1.R,C1.G,C1.B,C1.A));Custom2.Add(FVector4(C2.R,C2.G,C2.B,C2.A));
        }
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourcePositions"),Positions);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourceScales"),Scales);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourceRotations"),Rotations);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourceColors"),Colors);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourceUV"),UVs);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourceCustom1"),Custom1);
        UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector4(Component,TEXT("SourceCustom2"),Custom2);
        if(!Component->IsActive())Component->Activate(true);
        Component->SetVisibility(true);
}
