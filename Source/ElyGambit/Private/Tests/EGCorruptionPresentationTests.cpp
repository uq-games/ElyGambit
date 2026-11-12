#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Game/EGPieceTestGameMode.h"
#include "Character/EGChessUnit.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Data/EGHeroDefinition.h"
#include "Placement/EGDeploymentBoard.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGCorruptionBarrierVisual.h"
#include "Presentation/EGBarrierCameraShake.h"
#include "Presentation/EGCorruptionPhantomVisual.h"
#include "Presentation/EGCorruptionBossVisual.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGBarrierShakePatternTest,"ElyGambit.Presentation.Corruption.BarrierShakePattern",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGBarrierShakePatternTest::RunTest(const FString&)
{
 auto* Pattern=NewObject<UEGBarrierShakePattern>();
 if(!TestNotNull(TEXT("Wall camera shake pattern"),Pattern))return false;
 FCameraShakePatternUpdateParams Params;FCameraShakePatternUpdateResult Moving;
 Pattern->SetVisualElapsed(.08f);Pattern->UpdateShakePattern(Params,Moving);
 TestTrue(TEXT("Active shake produces a finite, nonzero position and rotation offset"),
  !Moving.Location.ContainsNaN()&&!Moving.Rotation.ContainsNaN()&&Moving.Location.SizeSquared()>KINDA_SMALL_NUMBER&&
  !Moving.Rotation.IsNearlyZero(.0001f));
 TestTrue(TEXT("Camera shake displacement remains bounded"),Moving.Location.Size()<=3.7f&&
  FMath::Abs(Moving.Rotation.Pitch)<=.20f&&FMath::Abs(Moving.Rotation.Yaw)<=.28f&&FMath::IsNearlyZero(Moving.FOV));
 FCameraShakePatternUpdateResult Finished;Pattern->SetVisualElapsed(.32f);Pattern->UpdateShakePattern(Params,Finished);
 TestTrue(TEXT("Finished pattern returns to zero offset"),Finished.Location.IsNearlyZero(.001f)&&Finished.Rotation.IsNearlyZero(.001f)&&FMath::IsNearlyZero(Finished.FOV));
 TestTrue(TEXT("Pattern reports finished at its end"),Pattern->IsFinished());
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGCorruptionVisualTest,"ElyGambit.Presentation.Corruption.BarrierAndPhantom",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGCorruptionVisualTest::RunTest(const FString&)
{
 TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));GI->InitializeStandalone();
 UWorld* W=GI->GetWorld();W->GetWorldSettings()->DefaultGameMode=AEGPieceTestGameMode::StaticClass();
 FURL URL;W->SetGameMode(URL);auto* Board=W->SpawnActor<AEGDeploymentBoard>();Board->Columns=Board->Rows=6;Board->PlayerRows=3;
 W->InitializeActorsForPlay(URL);W->BeginPlay();auto* Mode=W->GetAuthGameMode<AEGPieceTestGameMode>();Mode->EnterCombat();
 auto* Wall=W->SpawnActor<AEGCorruptionBarrierVisual>();
 if(TestNotNull(TEXT("Wall visual"),Wall))
 {
  TestFalse(TEXT("Reject invalid width without consuming initialization"),Wall->InitializeVisual(FVector::ZeroVector,FVector::ForwardVector,0.f,400.f));
  TestTrue(TEXT("Accept world-space board geometry"),Wall->InitializeVisual(FVector(100,200,10),FVector::RightVector,3000.f,400.f));
  TestFalse(TEXT("Do not reinitialize active wall"),Wall->InitializeVisual(FVector::ZeroVector,FVector::ForwardVector,10.f,10.f));
  Wall->Tick(.5f);
  TArray<UStaticMeshComponent*> Meshes;Wall->GetComponents<UStaticMeshComponent>(Meshes);
  TestEqual(TEXT("Wall has bounded geometry"),Meshes.Num(),46);
  int32 Before=0,After=0;
  for(auto* Mesh:Meshes)
  {
   TestEqual(TEXT("Visual never blocks movement"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
   if(Mesh->GetName().StartsWith(TEXT("WallGrid")))TestFalse(TEXT("Heavy legacy grid bars stay hidden"),Mesh->IsVisible());
   if(Mesh->GetName().StartsWith(TEXT("DigitalFragment"))&&Mesh->IsVisible())++Before;
  }
  Wall->SetIntegrity(0.f);Wall->Tick(0.f);
  for(auto* Mesh:Meshes)if(Mesh->GetName().StartsWith(TEXT("DigitalFragment"))&&Mesh->IsVisible())++After;
  TestTrue(TEXT("Damage removes visible digital fragments"),Before>After);
  int32 VisibleCracks=0;
  for(auto* Mesh:Meshes)if(Mesh->GetName().StartsWith(TEXT("WallCrack"))&&Mesh->IsVisible())++VisibleCracks;
  TestTrue(TEXT("Damage reveals cracks"),VisibleCracks>0);
  auto* Surface=FindObject<UStaticMeshComponent>(Wall,TEXT("WallSurface"));
  if(TestNotNull(TEXT("Wall surface"),Surface))
  {
   TestTrue(TEXT("Wall surface remains visible after opening"),Surface->IsVisible());
   TestTrue(TEXT("Wall uses the private source-style material instead of a fallback"),Surface->ComponentHasTag(TEXT("EGSourceStyleCorruptionWall")));
   auto* WallMID=Cast<UMaterialInstanceDynamic>(Surface->GetMaterial(0));
   if(TestNotNull(TEXT("Wall surface uses a dynamic material"),WallMID))
   {
    TestEqual(TEXT("Wall uses the external scan material, not a fallback"),GetPathNameSafe(WallMID->Parent.Get()),FString(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionExternalScanWall.M_CorruptionExternalScanWall")));
    float Strength=0.f;
    TestTrue(TEXT("External scan strength exists and contributes"),WallMID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ExternalScanStrength")),Strength)&&Strength>0.f);
    UTexture* Atlas=nullptr;
    TestTrue(TEXT("External scan atlas is bound"),WallMID->GetTextureParameterValue(FMaterialParameterInfo(TEXT("ExternalScanAtlas")),Atlas)&&IsValid(Atlas));
    TestEqual(TEXT("External scan atlas is the private texture"),GetPathNameSafe(Atlas),FString(TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Textures/T_CorruptionExternalScanAtlas.T_CorruptionExternalScanAtlas")));
   }
   TestTrue(TEXT("Surface centered at requested world height"),Surface->GetComponentLocation().Equals(FVector(100,200,210),.1f));
   TestTrue(TEXT("Width uses board dimensions"),FMath::IsNearlyEqual(Surface->GetRelativeScale3D().X,30.f,.001f));
   TestTrue(TEXT("Height uses board dimensions"),FMath::IsNearlyEqual(Surface->GetRelativeScale3D().Y,4.f,.001f));
  }
  auto* Backplane=FindObject<UStaticMeshComponent>(Wall,TEXT("WallBackplane"));
  if(TestNotNull(TEXT("Translucent corruption backplane"),Backplane))
  {
   TestTrue(TEXT("Dark backplane spans actual wall width"),FMath::IsNearlyEqual(Backplane->GetRelativeScale3D().X,30.f,.001f));
   TestTrue(TEXT("Dark backplane spans actual wall height"),FMath::IsNearlyEqual(Backplane->GetRelativeScale3D().Y,4.f,.001f));
   TestTrue(TEXT("Dark backplane is visible during the wall challenge"),Backplane->IsVisible());
   TestEqual(TEXT("Dark backplane never blocks"),Backplane->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
   if(auto* Material=Cast<UMaterialInstanceDynamic>(Backplane->GetMaterial(0)))
    TestTrue(TEXT("Dark backplane remains translucent and readable"),Material->K2_GetScalarParameterValue(TEXT("DomainOpacity"))>0.f&&Material->K2_GetScalarParameterValue(TEXT("DomainOpacity"))<=.35f);
   else AddError(TEXT("Wall backplane needs a dynamic opacity material"));
  }
  Wall->Dissolve(true);Wall->Tick(.7f);
  for(auto* Mesh:Meshes)TestFalse(TEXT("Broken wall no longer visible after fade"),Mesh->IsVisible());
  Wall->Destroy();
  auto* TimeoutWall=W->SpawnActor<AEGCorruptionBarrierVisual>();
  if(TestNotNull(TEXT("Timeout wall visual"),TimeoutWall))
  {
   TestTrue(TEXT("Timeout wall accepts board geometry"),TimeoutWall->InitializeVisual(FVector(100,200,10),FVector::RightVector,3000.f,400.f));
   TimeoutWall->Tick(.5f);TimeoutWall->Dissolve(false);TimeoutWall->Tick(.7f);
   TArray<UStaticMeshComponent*> TimeoutMeshes;TimeoutWall->GetComponents<UStaticMeshComponent>(TimeoutMeshes);
   for(auto* Mesh:TimeoutMeshes)TestFalse(TEXT("Timeout wall no longer visible after fade"),Mesh->IsVisible());
   TimeoutWall->Destroy();
  }
  auto* DelayedWall=W->SpawnActor<AEGCorruptionBarrierVisual>();
  if(TestNotNull(TEXT("Delayed-reveal wall visual"),DelayedWall))
  {
   TestTrue(TEXT("Delayed wall accepts board geometry"),DelayedWall->InitializeVisual(FVector(100,200,10),FVector::RightVector,3000.f,400.f));
   DelayedWall->SetIntroRevealDelay(1.f);DelayedWall->Tick(.8f);
   auto* DelayedSurface=FindObject<UStaticMeshComponent>(DelayedWall,TEXT("WallSurface"));
   if(TestNotNull(TEXT("Delayed wall surface"),DelayedSurface))
   {
    TestFalse(TEXT("Wall is hidden before source-action reveal delay"),DelayedSurface->IsVisible());
    auto* DelayedMaterial=Cast<UMaterialInstanceDynamic>(DelayedSurface->GetMaterial(0));
    if(TestNotNull(TEXT("Delayed wall material"),DelayedMaterial))
    {
     TestTrue(TEXT("Wall opacity starts at zero"),FMath::IsNearlyZero(DelayedMaterial->K2_GetScalarParameterValue(TEXT("DomainOpacity")),.001f));
     DelayedWall->Tick(.3f);
     const float Intermediate=DelayedMaterial->K2_GetScalarParameterValue(TEXT("DomainOpacity"));
     TestTrue(TEXT("Wall gradually appears after delay"),DelayedSurface->IsVisible()&&Intermediate>0.f&&Intermediate<.8f);
     DelayedWall->Tick(.6f);
     TestTrue(TEXT("Source wall finishes its reveal"),FMath::IsNearlyEqual(DelayedMaterial->K2_GetScalarParameterValue(TEXT("DomainOpacity")),.8f,.01f));
    }
   }
   DelayedWall->Destroy();
  }
 }
 AEGChessUnit* Boss=nullptr;AEGChessUnit* Player=nullptr;const UEGHeroDefinition* Hero=nullptr;
 for(const auto& Entry:Mode->GetBattleUnits())if(auto* Unit=Entry.Get())
  if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Enemy)Boss=Unit;
  else if(IEGCombatant::Execute_GetFaction(Unit)==EEGFaction::Player){Player=Unit;Hero=Unit->GetDefinition();}
 if(TestNotNull(TEXT("Battle source"),Boss)&&TestNotNull(TEXT("Player appearance"),Hero))
 {
  // A wall intro may advance during its own hold, but an independent pause
  // must freeze the same material clock until that independent pause ends.
  Mode->BattleRules->CompleteOpeningForTests();
  auto* PC=W->SpawnActor<APlayerController>();
  if(TestNotNull(TEXT("Wall pause controller"),PC))
  {
   if(!PC->PlayerState)PC->SetPlayerState(W->SpawnActor<APlayerState>());
   if(TestNotNull(TEXT("Wall pause player state"),PC->PlayerState.Get()))
   {
    FActorSpawnParameters Params;Params.Owner=Boss;
    auto* PausedWall=W->SpawnActor<AEGCorruptionBarrierVisual>(Params);
    if(TestNotNull(TEXT("Battle-bound wall"),PausedWall)&&TestTrue(TEXT("Battle-bound wall initializes"),PausedWall->InitializeVisual(FVector::ZeroVector,FVector::RightVector,3000.f,400.f)))
    {
     const FGuid Hold=FGuid::NewGuid();Mode->BattleRules->SetPresentationHold(Hold,true);PausedWall->SetPresentationToken(Hold);
     PausedWall->SetIntroElapsed(.5f);
     auto* PauseSurface=FindObject<UStaticMeshComponent>(PausedWall,TEXT("WallSurface"));
     auto* PauseMID=PauseSurface?Cast<UMaterialInstanceDynamic>(PauseSurface->GetMaterial(0)):nullptr;
     if(TestNotNull(TEXT("Battle wall dynamic material"),PauseMID))
     {
      const float BeforePause=PauseMID->K2_GetScalarParameterValue(TEXT("VisualTime"));
      TestTrue(TEXT("Own hold advances wall clock"),FMath::IsNearlyEqual(BeforePause,.5f,.001f));
      if(TestTrue(TEXT("Independent pause acquired"),Mode->SetPause(PC)))
      {
       PausedWall->SetIntroElapsed(1.f);PausedWall->Tick(.5f);
       TestTrue(TEXT("Independent pause freezes material clock"),FMath::IsNearlyEqual(PauseMID->K2_GetScalarParameterValue(TEXT("VisualTime")),BeforePause,.001f));
       PC->SetPause(false);
       PausedWall->SetIntroElapsed(1.f);
       TestTrue(TEXT("Wall clock resumes after independent pause"),FMath::IsNearlyEqual(PauseMID->K2_GetScalarParameterValue(TEXT("VisualTime")),1.f,.001f));
      }
     }
     PausedWall->SetPresentationToken(FGuid());Mode->BattleRules->SetPresentationHold(Hold,false);PausedWall->Destroy();
    }
    PC->Destroy();
   }
  }
  // The old replay fixture fabricated a phantom without a Boss cast. Real
  // appearance, cast and cleanup are exercised by BossSpecialTimingAndCancellation.
  TestNull(TEXT("An unrelated action cannot fabricate a phantom"),
   AEGCorruptionPhantomVisual::Spawn(Boss,Hero,FVector(0,0,200),FRotator::ZeroRotator,1.f,FGuid::NewGuid()));
  auto CountVisuals=[W](){int32 Count=0;for(TActorIterator<AEGCorruptionBossVisual> It(W);It;++It)if(!It->IsActorBeingDestroyed())++Count;return Count;};
  auto* Controller=AEGCorruptionBossVisual::SpawnForBoss(Boss);
  if(TestNotNull(TEXT("Boss event visual"),Controller)&&TestNotNull(TEXT("Mark target"),Player))
  {
   auto* VisibleBody=Boss->GetVisiblePresentationMesh();
   auto* OriginalOverlay=VisibleBody?VisibleBody->GetOverlayMaterial():nullptr;
   TestEqual(TEXT("Controller starts alone"),CountVisuals(),1);
   Controller->SetSecondPhase(true);TestEqual(TEXT("Phase entry has an effect"),CountVisuals(),2);
   if(TestNotNull(TEXT("Boss visible body"),VisibleBody))TestTrue(TEXT("Phase overlay applied to visible body"),VisibleBody->GetOverlayMaterial()!=OriginalOverlay);
   for(TActorIterator<AEGCorruptionBossVisual> It(W);It;++It)if(*It!=Controller&&!It->IsActorBeingDestroyed())It->Tick(1.1f);
   TestEqual(TEXT("Phase entry effect expires"),CountVisuals(),1);
   Controller->ShowMark(Player,1,3);TestEqual(TEXT("One tracked mark"),CountVisuals(),2);
   for(TActorIterator<AEGCorruptionBossVisual> It(W);It;++It)if(*It!=Controller&&!It->IsActorBeingDestroyed())
   {
    auto* Lit=FindObject<UStaticMeshComponent>(*It,TEXT("CorruptionShard00"));
    auto* Dim=FindObject<UStaticMeshComponent>(*It,TEXT("CorruptionShard01"));
    if(TestNotNull(TEXT("Active mark slot"),Lit)&&TestNotNull(TEXT("Inactive mark slot"),Dim))
     TestTrue(TEXT("Mark count has distinct active and inactive slots"),Lit->IsVisible()&&Dim->IsVisible()&&Lit->GetMaterial(0)!=Dim->GetMaterial(0));
   }
   Controller->ShowMark(Player,2,3);TestEqual(TEXT("Updating mark does not duplicate"),CountVisuals(),2);
   Controller->ShowMark(Player,0,3);TestEqual(TEXT("Clearing mark removes it"),CountVisuals(),1);
   Controller->ShowSpreadWarning(Player->GetActorLocation(),200.f,2.f);TestEqual(TEXT("Spread warning created"),CountVisuals(),2);
   Controller->ClearSpreadWarning();Controller->ClearSpreadWarning();TestEqual(TEXT("Spread cancellation is idempotent"),CountVisuals(),1);
   Controller->ShowSpreadBurst(Player->GetActorLocation(),200.f);TestEqual(TEXT("Spread impact created only on call"),CountVisuals(),2);
   // Use a rotated, scaled board so an accidentally world-axis-aligned sweep
   // cannot pass. The wave is cosmetic; its gameplay impact is tested separately.
   Board->SetActorRotation(FRotator(0,33,0));Board->SetActorScale3D(FVector(1.4f,.8f,1.f));
   const FTransform Basis=Board->GetActorTransform();const FVector WallCenter=Basis.TransformPosition(FVector(35.f,100.f,0.f));
   const float HealthBeforeSweep=Player->GetAttributes()->GetHealth();
   Controller->ShowBarrierFailure(WallCenter,1200.f);
   AEGCorruptionBossVisual* Wave=nullptr;UNiagaraComponent* WaveParticles=nullptr;
   for(TActorIterator<AEGCorruptionBossVisual> It(W);It;++It)if(*It!=Controller&&!It->IsActorBeingDestroyed())
   {
    TArray<UNiagaraComponent*> Particles;It->GetComponents<UNiagaraComponent>(Particles);
    for(auto* Particle:Particles)if(Particle&&Particle->GetAsset()&&Particle->GetAsset()->GetName()==TEXT("NS_CorruptionBrokenWavefront")){Wave=*It;WaveParticles=Particle;break;}
   }
   if(TestNotNull(TEXT("Failure sweep actor"),Wave)&&TestNotNull(TEXT("Private failure wave particles"),WaveParticles))
   {
    TestTrue(TEXT("Wave sprites keep unit component scale"),WaveParticles->GetComponentScale().Equals(FVector::OneVector,.001f));
    bool bWidthValid=false;
    const float HalfWidth=WaveParticles->GetVariableFloat(TEXT("User.WaveHalfWidth"),bWidthValid);
    TestTrue(TEXT("Wave receives the actual wall half-width"),bWidthValid&&FMath::IsNearlyEqual(HalfWidth,1200.f,.001f));
    TestTrue(TEXT("Wave rotates with the actual board"),Wave->GetActorRotation().Equals(Board->GetActorRotation(),.01f));
    const FVector End=Basis.TransformPosition(FVector(35.f,-.5f*Board->Rows*Board->CellDepth,0.f));
    Wave->Tick(.425f);
    TestTrue(TEXT("Midpoint advances from wall toward player edge"),Wave->GetActorLocation().Equals(FMath::Lerp(WallCenter,End,.5f),.1f));
    auto* SweepPC=W->SpawnActor<APlayerController>();
    if(TestNotNull(TEXT("Sweep pause controller"),SweepPC))
    {
     if(!SweepPC->PlayerState)SweepPC->SetPlayerState(W->SpawnActor<APlayerState>());
     if(TestNotNull(TEXT("Sweep pause player state"),SweepPC->PlayerState.Get())&&TestTrue(TEXT("Independent sweep pause acquired"),Mode->SetPause(SweepPC)))
     {
      const FVector Frozen=Wave->GetActorLocation();Wave->Tick(.2f);
      TestTrue(TEXT("Independent pause freezes sweep position and particles"),Wave->GetActorLocation().Equals(Frozen,.01f)&&WaveParticles->IsPaused());
      SweepPC->SetPause(false);Wave->Tick(0.f);TestFalse(TEXT("Sweep particles resume"),WaveParticles->IsPaused());
     }
     SweepPC->Destroy();
    }
    Wave->Tick(.425f);TestTrue(TEXT("Sweep reaches the actual player edge"),Wave->GetActorLocation().Equals(End,.1f));
    Wave->Tick(.26f);TestTrue(TEXT("Finished sweep destroys its actor"),Wave->IsActorBeingDestroyed());
    TestTrue(TEXT("Presentation does not deal damage"),FMath::IsNearlyEqual(Player->GetAttributes()->GetHealth(),HealthBeforeSweep,.01f));
   }
   Controller->ShowBarrierFailure(WallCenter,1200.f);
   TWeakObjectPtr<AEGCorruptionBossVisual> ActiveWave;
   TWeakObjectPtr<UNiagaraComponent> ActiveParticles;
   for(TActorIterator<AEGCorruptionBossVisual> It(W);It;++It)if(*It!=Controller&&!It->IsActorBeingDestroyed())
   {
    TArray<UNiagaraComponent*> Particles;It->GetComponents<UNiagaraComponent>(Particles);
    for(auto* Particle:Particles)if(Particle&&Particle->GetAsset()&&Particle->GetAsset()->GetName()==TEXT("NS_CorruptionBrokenWavefront")){ActiveWave=*It;ActiveParticles=Particle;break;}
   }
   TestTrue(TEXT("Second wave is active before owner cleanup"),ActiveWave.IsValid()&&ActiveParticles.IsValid());
   Controller->Destroy();TestEqual(TEXT("Controller cleanup removes owned effects"),CountVisuals(),0);
   TestFalse(TEXT("Controller cleanup destroys the live wave"),ActiveWave.IsValid());
   TestFalse(TEXT("Controller cleanup destroys the wave particle component"),ActiveParticles.IsValid());
   if(VisibleBody)TestEqual(TEXT("Previous body overlay restored"),VisibleBody->GetOverlayMaterial(),OriginalOverlay);
   if(VisibleBody)
   {
    auto* Replacement=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/Assets/FXs/Characters/ElysiaCorruption/EliteBoss20261107/Materials/M_CorruptionPhantom.M_CorruptionPhantom"));
    auto* OtherController=AEGCorruptionBossVisual::SpawnForBoss(Boss);
    if(TestNotNull(TEXT("Overlay takeover fixture"),OtherController)&&TestNotNull(TEXT("Replacement overlay"),Replacement))
    {
     OtherController->SetSecondPhase(true);VisibleBody->SetOverlayMaterial(Replacement);
     OtherController->Tick(0.f);OtherController->Tick(0.f);
     TestEqual(TEXT("External overlay is not overwritten"),VisibleBody->GetOverlayMaterial(),Replacement);
     OtherController->Destroy();
     TestEqual(TEXT("External overlay survives teardown"),VisibleBody->GetOverlayMaterial(),Replacement);
     VisibleBody->SetOverlayMaterial(OriginalOverlay);
    }
   }
  }
 }
 W->BeginTearingDown();W->EndPlay(EEndPlayReason::Quit);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);
 return true;
}
#endif
