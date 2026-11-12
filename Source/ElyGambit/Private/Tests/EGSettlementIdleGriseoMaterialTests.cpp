#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Character/EGDemoCombatUnit.h"
#include "Components/SkeletalMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSettlementPresentation.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSettlementIdleGriseoMaterial,
 "ElyGambit.Presentation.SettlementIdleGriseoMaterial",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSettlementIdleGriseoMaterial::RunTest(const FString&)
{
 auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo.DA_Card_Griseo"));
 if(!TestNotNull(TEXT("Formal Griseo card"),Card)||!TestNotNull(TEXT("Formal hero"),Card->HeroDefinition.Get())||
    !TestNotNull(TEXT("Formal presentation"),Card->HeroDefinition->PresentationDefinition.Get()))return false;
 auto* P=Card->HeroDefinition->PresentationDefinition.Get();auto* Source=P->SettlementIdleAssembly.LoadSynchronous();
 if(!TestNotNull(TEXT("Formal settlement assembly"),Source)||!TestTrue(TEXT("Assembly has a motion"),!Source->Motions.IsEmpty()))return false;
 const auto& Parts=Source->Motions[0].Parts;
 const int32 BodyIndex=Parts.IndexOfByPredicate([](const FEGSourcePreviewPart& Part){return Part.PartId==TEXT("Body");});
 if(!TestTrue(TEXT("Assembly has a body"),Parts.IsValidIndex(BodyIndex)))return false;
 const auto& BodyPart=Parts[BodyIndex];
 const auto* CurvePtr=BodyPart.MaterialCurves.Find(FName(TEXT("inkAlpha")));
 if(!TestTrue(TEXT("Body has the inkAlpha source curve"),CurvePtr&&IsValid(CurvePtr->Get()))||
    !TestTrue(TEXT("Section visibility guard remains applicable"),BodyPart.SectionVisibility.IsEmpty()))return false;
 const UCurveFloat* Curve=CurvePtr->Get();
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();auto* W=GI->GetWorld();
 ON_SCOPE_EXIT{W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->GetWorldSettings()->DefaultGameMode=AGameModeBase::StaticClass();FURL URL;W->SetGameMode(URL);W->InitializeActorsForPlay(URL);W->BeginPlay();
 if(!TestTrue(TEXT("Fixture world has begun play"),W->HasBegunPlay()))return false;
 // This fixture deliberately selects the authored source idle to exercise its
 // material curves; the production selector now prefers ordinary standing idle.
 auto* FixtureHero=DuplicateObject<UEGHeroDefinition>(Card->HeroDefinition,W);
 FixtureHero->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(P,FixtureHero);
 FixtureHero->PresentationDefinition->IdleAnimation=P->SettlementIdleAnimation;
 auto* Unit=W->SpawnActor<AEGDemoCombatUnit>();
 if(!TestTrue(TEXT("Initialize Griseo fixture"),Unit->InitializeUnit(FixtureHero,FGuid::NewGuid(),EEGFaction::Player,1)))return false;
 auto* Body=Unit->GetVisiblePresentationMesh();
 if(!TestNotNull(TEXT("Body material"),Body->GetMaterial(0)))return false;
 auto* LiveMaterial=UMaterialInstanceDynamic::Create(Body->GetMaterial(0),Unit);
 if(!TestNotNull(TEXT("Original body MID"),LiveMaterial))return false;
 LiveMaterial->SetScalarParameterValue(TEXT("inkAlpha"),.37f);
 LiveMaterial->SetScalarParameterValue(TEXT("SettlementProbeScalar"),.63f);
 const FLinearColor ProbeColor(.13f,.27f,.51f,.79f);
 LiveMaterial->SetVectorParameterValue(TEXT("SettlementProbeColor"),ProbeColor);
 Body->SetMaterial(0,LiveMaterial);
 auto* Settlement=W->SpawnActor<AEGSettlementPresentation>();
 Settlement->StartPresentation({Unit},nullptr,FVector::ZeroVector,FVector(500,0,0),true);
 AEGCombatVisual* Rig=nullptr;
 for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==Settlement&&It->Definition==Source&&!It->IsActorBeingDestroyed())Rig=*It;
 if(!TestNotNull(TEXT("Griseo settlement proxy is created"),Rig))return false;
 TArray<USkeletalMeshComponent*> Components;Rig->GetComponents(Components);USkeletalMeshComponent* ProxyBody=nullptr;
 const FName BodyTag(*FString::Printf(TEXT("EGSourcePartIndex_%d"),BodyIndex));
 for(auto* Component:Components)if(IsValid(Component)&&Component->ComponentHasTag(BodyTag)){ProxyBody=Component;break;}
 if(!TestNotNull(TEXT("Proxy body"),ProxyBody))return false;
 auto* ProxyMaterial=Cast<UMaterialInstanceDynamic>(ProxyBody->GetMaterial(0));
 if(!TestNotNull(TEXT("Proxy uses an independent MID"),ProxyMaterial))return false;
 TestTrue(TEXT("Proxy MID does not alias live MID"),ProxyMaterial!=LiveMaterial);
 TestTrue(TEXT("Proxy uses the live MID's valid material parent"),LiveMaterial->Parent&&ProxyMaterial->Parent==LiveMaterial->Parent);
 const FMaterialParameterInfo ScalarProbe(TEXT("SettlementProbeScalar")),ColorProbe(TEXT("SettlementProbeColor"));
 float ProxyProbe=0.f;FLinearColor ProxyColor;
 TestTrue(TEXT("Proxy copies unrelated scalar override"),ProxyMaterial->GetScalarParameterValue(ScalarProbe,ProxyProbe)&&FMath::IsNearlyEqual(ProxyProbe,.63f,.001f));
 TestTrue(TEXT("Proxy copies unrelated vector override"),ProxyMaterial->GetVectorParameterValue(ColorProbe,ProxyColor)&&ProxyColor.Equals(ProbeColor,.001f));
 LiveMaterial->SetScalarParameterValue(TEXT("SettlementProbeScalar"),.91f);
 TestTrue(TEXT("Later live MID changes do not mutate proxy"),ProxyMaterial->GetScalarParameterValue(ScalarProbe,ProxyProbe)&&FMath::IsNearlyEqual(ProxyProbe,.63f,.001f));
 const FMaterialParameterInfo Parameter(TEXT("inkAlpha"));float LiveValue=0.f,ProxyValue=0.f;
 TestTrue(TEXT("Live scalar is readable"),LiveMaterial->GetScalarParameterValue(Parameter,LiveValue));
 TestTrue(TEXT("Proxy scalar is readable"),ProxyMaterial->GetScalarParameterValue(Parameter,ProxyValue));
 TestTrue(TEXT("Frame zero was reapplied to private MID"),FMath::IsNearlyEqual(ProxyValue,Curve->GetFloatValue(0.f),.001f));
 TestTrue(TEXT("Live scalar remains untouched at frame zero"),FMath::IsNearlyEqual(LiveValue,.37f,.001f));
 Settlement->Tick(.25f);
 LiveMaterial->GetScalarParameterValue(Parameter,LiveValue);ProxyMaterial->GetScalarParameterValue(Parameter,ProxyValue);
 TestTrue(TEXT("Proxy follows source scalar curve"),FMath::IsNearlyEqual(ProxyValue,Curve->GetFloatValue(.25f),.001f));
 TestTrue(TEXT("Live scalar remains unchanged during sampling"),FMath::IsNearlyEqual(LiveValue,.37f,.001f));
 Settlement->Destroy();
 TestTrue(TEXT("Interrupt restores original body and material"),Body->IsVisible()&&Body->GetMaterial(0)==LiveMaterial);
 LiveMaterial->GetScalarParameterValue(Parameter,LiveValue);
 TestTrue(TEXT("Interrupt retains original scalar"),FMath::IsNearlyEqual(LiveValue,.37f,.001f));
 TestTrue(TEXT("Interrupt destroys proxy"),!IsValid(Rig)||Rig->IsPendingKillPending());
 Unit->Destroy();
 AddInfo(TEXT("Controlled formal-asset and material isolation test; no rendered, audio, natural-play, Cook or package acceptance."));
 return true;
}
#endif
