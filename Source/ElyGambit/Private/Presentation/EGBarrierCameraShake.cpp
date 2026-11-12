#include "Presentation/EGBarrierCameraShake.h"

void UEGBarrierShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
 OutInfo.Duration=FCameraShakeDuration::Custom(.32f);
}
void UEGBarrierShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams&)
{
 Elapsed=0.f;Lease.Invalidate();
}
void UEGBarrierShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams&,FCameraShakePatternUpdateResult& OutResult)
{
 const float T=FMath::Clamp(Elapsed/.32f,0.f,1.f);
 const float Envelope=FMath::Sin(PI*T)*(1.f-T);
 OutResult.Location=FVector(0.f,3.f*Envelope*FMath::Sin(Elapsed*55.f),2.f*Envelope*FMath::Sin(Elapsed*41.f));
 OutResult.Rotation=FRotator(.20f*Envelope*FMath::Sin(Elapsed*45.f),.28f*Envelope*FMath::Sin(Elapsed*53.f),0.f);
}
bool UEGBarrierShakePattern::IsFinishedImpl() const{return Elapsed>=.32f;}
UEGBarrierCameraShake::UEGBarrierCameraShake(const FObjectInitializer& ObjectInitializer)
 :Super(ObjectInitializer.SetDefaultSubobjectClass<UEGBarrierShakePattern>(TEXT("RootShakePattern"))){}
void UEGBarrierCameraShake::SetVisualElapsed(float Seconds)
{
 if(auto* Pattern=Cast<UEGBarrierShakePattern>(GetRootShakePattern()))Pattern->SetVisualElapsed(Seconds);
}
void UEGBarrierCameraShake::SetLease(FGuid Token)
{
 if(auto* Pattern=Cast<UEGBarrierShakePattern>(GetRootShakePattern()))Pattern->SetLease(Token);
}
bool UEGBarrierCameraShake::HasLease(FGuid Token) const
{
 const auto* Pattern=Cast<UEGBarrierShakePattern>(GetRootShakePattern());return Pattern&&Pattern->HasLease(Token);
}
