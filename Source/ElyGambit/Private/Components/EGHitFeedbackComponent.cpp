#include "Components/EGHitFeedbackComponent.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"

UEGHitFeedbackComponent::UEGHitFeedbackComponent()
{PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;}
void UEGHitFeedbackComponent::BeginPlay()
{
 Super::BeginPlay();Unit=Cast<AEGChessUnit>(GetOwner());
 if(Unit.IsValid()){Unit->OnCombatResolved.AddUniqueDynamic(this,&ThisClass::OnResolved);Unit->OnCombatStateChanged.AddUniqueDynamic(this,&ThisClass::OnState);}
}
void UEGHitFeedbackComponent::EndPlay(const EEndPlayReason::Type Reason)
{
 if(Unit.IsValid()){Unit->OnCombatResolved.RemoveDynamic(this,&ThisClass::OnResolved);Unit->OnCombatStateChanged.RemoveDynamic(this,&ThisClass::OnState);}
 ClearFeedback();for(const auto& Part:Parts)if(IsValid(Part))Part->DestroyComponent();
 Parts.Reset();Materials.Reset();Data=nullptr;Unit.Reset();Super::EndPlay(Reason);
}
bool UEGHitFeedbackComponent::BuildPool()
{
 if(!Unit.IsValid()||Profile.IsNull())return false;
 // These characters and Vill-V's devices do not show incoming-hit visuals.
 if(Unit->IsA<AEGVillVEmplacement>())return false;
 if(const auto* Definition=Unit->GetDefinition();Definition&&(Definition->HeroKind==EEGHeroKind::Hua||Definition->HeroKind==EEGHeroKind::Sakura||Definition->HeroKind==EEGHeroKind::VillV))return false;
 if(!Data)Data=Profile.LoadSynchronous();
 if(!Data||Data->Layers.IsEmpty()||Data->Layers.Num()>4||!FMath::IsFinite(Data->Duration)||Data->Duration<=0)return false;
 if(!Parts.IsEmpty())return true;
 auto* Plane=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane"));if(!Plane)return false;
 for(const auto& Layer:Data->Layers)if(!Layer.LoadSynchronous())return false;
 Ages.Init(-1.f,4);Points.Init(FVector::ZeroVector,4);Directions.Init(FVector::ForwardVector,4);Variants.Init(0,4);
 ImpactScales.Init(1.f,4);ImpactDurations.Init(Data->Duration,4);ImpactTints.Init(Data->Tint,4);
 for(int32 Pool=0;Pool<4;++Pool)for(const auto& Layer:Data->Layers)
 {
  auto* Part=NewObject<UStaticMeshComponent>(GetOwner());GetOwner()->AddInstanceComponent(Part);
  Part->SetStaticMesh(Plane);Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetCastShadow(false);
  Part->SetMaterial(0,Layer.LoadSynchronous());Part->SetVisibility(false);Part->RegisterComponent();
  Part->AttachToComponent(GetOwner()->GetRootComponent(),FAttachmentTransformRules::KeepWorldTransform);
  Part->SetAbsolute(true,true,true);Parts.Add(Part);Materials.Add(Part->CreateDynamicMaterialInstance(0));
 }
 return true;
}
void UEGHitFeedbackComponent::OnResolved(const FEGCombatResolution& Receipt)
{
 if(!Unit.IsValid()||GetWorld()->GetNetMode()==NM_DedicatedServer||Receipt.TargetId!=Unit->GetInstanceId()||Receipt.bHealing
    ||(Receipt.HealthDelta>=0&&Receipt.ShieldAbsorbed<=0))return;
 if(const auto* Battle=Unit->GetHeroSkill()->Battle.Get())for(const auto* Source:Battle->Units())
  if(IsValid(Source)&&Source->GetInstanceId()==Receipt.SourceId&&Source->GetDefinition()&&Source->GetDefinition()->HeroKind==EEGHeroKind::VillV)return;
 if(!BuildPool())return;
 // Do not deduplicate by ActionId: a single combo action can have two real hits.
 const int32 Slot=Cursor++%4;Ages[Slot]=0;
 ImpactScales[Slot]=1.f;ImpactDurations[Slot]=Data->Duration;ImpactTints[Slot]=Data->Tint;
 const float Height=Unit->GetDefinition()?Unit->GetDefinition()->GetPresentationHeight():160.f;
 FVector Incoming=Unit->GetActorForwardVector();int32 Variant=0;
 if(const auto* Battle=Unit->GetHeroSkill()->Battle.Get())for(auto* Source:Battle->Units())
  if(IsValid(Source)&&Source->GetInstanceId()==Receipt.SourceId)
  {
   Incoming=(Unit->GetActorLocation()-Source->GetActorLocation()).GetSafeNormal2D();
   const auto* Definition=Source->GetDefinition();
   if(Definition&&(Definition->HeroKind==EEGHeroKind::Elysia||Definition->HeroKind==EEGHeroKind::ElysiaHerrscher))
   {
    // Sharper contact accent, driven by this receipt rather than the launch animation.
    ImpactScales[Slot]=1.4f;ImpactDurations[Slot]=FMath::Min(Data->Duration,.18f);
    ImpactTints[Slot]=FLinearColor(1.6f,.85f,1.35f,1.f);
   }
   if(Definition&&(Definition->HeroKind==EEGHeroKind::Can||(Definition->MonsterAttack&&Definition->MonsterAttack->AttackType==EEGMonsterAttackType::Melee)))Variant=1;
   break;
  }
 if(Incoming.IsNearlyZero())Incoming=FVector::ForwardVector;
 const FVector Torso=Unit->GetActorLocation()+FVector(0,0,FMath::Max(60.f,Height*.48f)-Unit->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
 auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
 const FVector View=Camera?(Camera->GetCameraLocation()-Torso).GetSafeNormal():FVector(0,-.7f,.7f).GetSafeNormal();
 // Surface-biased contact, also lifted towards the view so it is not buried inside the mesh.
 Points[Slot]=Data->bAnimatedAtlas?Torso+(View-Incoming)*UEGCombatComponent::BodyRadius(Unit.Get())*.55f:Torso;
 Directions[Slot]=Incoming;Variants[Slot]=FMath::Min(Variant,Data->Layers.Num()-1);
 LastImpactPoint=Points[Slot];LastVariant=Variants[Slot];
 ++PlayedCount;SetComponentTickEnabled(true);TickComponent(0,LEVELTICK_All,nullptr);
}
void UEGHitFeedbackComponent::OnState(EEGUnitState State)
{if(State==EEGUnitState::Idle)ClearFeedback();}
void UEGHitFeedbackComponent::ClearFeedback()
{for(const auto& Part:Parts)if(IsValid(Part))Part->SetVisibility(false);for(float& Age:Ages)Age=-1;SetComponentTickEnabled(false);}
int32 UEGHitFeedbackComponent::GetActiveBurstCount() const
{int32 Count=0;for(float Age:Ages)if(Age>=0)++Count;return Count;}
int32 UEGHitFeedbackComponent::GetVisiblePartCount() const
{int32 Count=0;for(const auto& Part:Parts)if(IsValid(Part)&&Part->IsVisible())++Count;return Count;}
void UEGHitFeedbackComponent::TickComponent(float Delta,ELevelTick Tick,FActorComponentTickFunction* Function)
{
 Super::TickComponent(Delta,Tick,Function);if(!Data||!Unit.IsValid()){ClearFeedback();return;}
 if(const auto* Battle=Unit->GetHeroSkill()->Battle.Get();Battle&&Battle->IsBattleActive()&&!Battle->IsCombatRunning())Delta=0.f;
 const float Height=Unit->GetDefinition()?Unit->GetDefinition()->GetPresentationHeight():160.f;
 const float Diameter=FMath::Clamp(Height*Data->SizeInBodyHeights,35.f,190.f);
 auto* Camera=UGameplayStatics::GetPlayerCameraManager(this,0);
 for(int32 Slot=0;Slot<Ages.Num();++Slot)
 {
  if(Ages[Slot]>=0){Ages[Slot]+=Delta;if(Ages[Slot]>=ImpactDurations[Slot])Ages[Slot]=-1;}
  for(int32 Layer=0;Layer<Data->Layers.Num();++Layer)
  {
   const int32 I=Slot*Data->Layers.Num()+Layer;const bool Active=Ages[Slot]>=0&&(!Data->bAnimatedAtlas||Layer==Variants[Slot]);
   Parts[I]->SetVisibility(Active);if(!Active)continue;
   const float T=Ages[Slot]/ImpactDurations[Slot];
   const FVector Normal=Camera?(Camera->GetCameraLocation()-Points[Slot]).GetSafeNormal():FVector(0,-.7f,.7f).GetSafeNormal();
   const FQuat Facing=FRotationMatrix::MakeFromZ(Normal).ToQuat();
   const FVector Right=Facing.GetAxisX(),Up=Facing.GetAxisY();
   const float DirectionAngle=FMath::Atan2(FVector::DotProduct(Directions[Slot],Up),FVector::DotProduct(Directions[Slot],Right));
   const FQuat Roll(Normal,Data->bAnimatedAtlas?DirectionAngle+FMath::DegreesToRadians((Slot%2==0?1.f:-1.f)*12.f):FMath::DegreesToRadians(Layer==1?45.f+(Slot%2)*60.f:0.f));
   // Source flipbooks own the expansion and breakup. Do not scale that motion twice.
   const float Scale=Diameter/100.f*ImpactScales[Slot]*(Data->bAnimatedAtlas?(Layer==1?1.08f:1.f):(Layer==2?.45f+T:.8f+.35f*T));
   Parts[I]->SetWorldTransform(FTransform(Roll*Facing,Points[Slot]+Normal*Layer*.5f,FVector(Scale)));
   if(Materials[I])
   {
    Materials[I]->SetVectorParameterValue(TEXT("Tint"),ImpactTints[Slot]);
    Materials[I]->SetScalarParameterValue(TEXT("FrameProgress"),T);
    Materials[I]->SetScalarParameterValue(TEXT("Opacity"),Data->bAnimatedAtlas?1.f-FMath::SmoothStep(.75f,1.f,T):FMath::Square(1-T));
   }
  }
 }
 if(GetActiveBurstCount()==0)SetComponentTickEnabled(false);
}
