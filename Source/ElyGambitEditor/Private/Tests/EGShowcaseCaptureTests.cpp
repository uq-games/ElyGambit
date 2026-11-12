#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "CoreGlobals.h"
#include "Misc/App.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Actor/EGBoard.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGCombatAudioComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystem/EGCombatExecution.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGUltimateSignetVisual.h"
#include "Presentation/EGVillVShotVisual.h"
#include "UI/HUD/EGHUD.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/Widget/EGCombatTextLayer.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/UserWidget.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
class FShowcaseStartPIE final : public IAutomationLatentCommand
{
public:
 bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1280;Settings->NewWindowHeight=720;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).Title(FText::FromString(TEXT("ElyGambit Showcase")))
   .ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1280,720)).AutoCenter(EAutoCenter::None)
   .CreateTitleBar(false).UseOSWindowBorder(false).SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(!GUnrealEd->CheckForPlayerStart())FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

class FShowcaseHero final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FString Clip,CardPath,RunId;
 EEGHeroKind Kind;
 TWeakObjectPtr<AEGChessUnit> Hero,Target;
 TWeakObjectPtr<ACameraActor> Camera;
 TWeakObjectPtr<AActor> PreviousView;
 TWeakObjectPtr<UWorld> CaptureWorld;
 TWeakObjectPtr<AEGDemoGameMode> FrozenStageClock;
 TArray<TWeakObjectPtr<UUserWidget>> SuppressedHUD;
 FVector FramingCenter=FVector::ZeroVector;
 FRotator FramingRotation=FRotator::ZeroRotator;
 float FramingDistance=0.f;
 double LastFramingAt=0.;
 double Deadline=0.,ReadyAt=0.,StartAt=0.,LastSignalAt=0.;
 int32 Step=0;bool bMana=false,bRevive=false,bBasicSeen=false,bUltimateSeen=false,bSignetSeen=false;
 bool bVillVShot=false,bVillVDamage=false,bVillVGun=false,bVillVCannon=false,bStageClockWasTicking=false;
 bool bScreenMessagesWereEnabled=false,bScreenMessagesSuppressed=false;
 double Duration() const {return Kind==EEGHeroKind::ElysiaHerrscher?45.:Kind==EEGHeroKind::VillV?40.:30.;}
 bool Signal(const TCHAR* State,UWorld* World)
 {
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("Showcase20261111/signals");
  IFileManager::Get().MakeDirectory(*Directory,true);
  const FString Path=Directory/TEXT("current.json"),Temporary=Directory/TEXT("current.tmp.json");
  TSharedRef<FJsonObject> Object=MakeShared<FJsonObject>();
  Object->SetStringField(TEXT("clip"),Clip);Object->SetStringField(TEXT("state"),State);
  Object->SetStringField(TEXT("run_id"),RunId);Object->SetStringField(TEXT("utc"),FDateTime::UtcNow().ToIso8601());
  Object->SetNumberField(TEXT("world_seconds"),World?World->GetTimeSeconds():0.);
  Object->SetNumberField(TEXT("wall_seconds"),FPlatformTime::Seconds());
  Object->SetNumberField(TEXT("duration_seconds"),Duration());
  FString Text;const auto Writer=TJsonWriterFactory<>::Create(&Text);
  if(!FJsonSerializer::Serialize(Object,Writer)||!FFileHelper::SaveStringToFile(Text,*Temporary))return false;
  return IFileManager::Get().Move(*Path,*Temporary,true,true,false,true);
 }
 void UpdateFraming(AEGDemoGameMode* Mode,bool bSnap=false)
 {
  if(!Camera.IsValid()||!Mode||!Mode->BattleRules||!Hero.IsValid())return;
  FBox Bounds(ForceInit);
  const auto AddUnit=[&Bounds](AEGChessUnit* Unit)
  {
   if(!IsValid(Unit)||Unit->IsActorBeingDestroyed()||Unit->IsHidden())return;
   const FVector Point=Unit->GetActorLocation();
   if(Point.ContainsNaN())return;
   Bounds+=Point+FVector(-230,-230,-90);Bounds+=Point+FVector(230,230,450);
   if(const auto* Mesh=Unit->GetVisiblePresentationMesh();IsValid(Mesh)&&Mesh->IsVisible())
   {
    const FVector Center=Mesh->Bounds.Origin,Extent=Mesh->Bounds.BoxExtent;
    if(!Center.ContainsNaN()&&!Extent.ContainsNaN()&&FVector::Dist(Center,Point)<1000.f)
    {
     const FVector E(FMath::Clamp(Extent.X,0.,500.),FMath::Clamp(Extent.Y,0.,500.),FMath::Clamp(Extent.Z,0.,500.));
     Bounds+=Center-E;Bounds+=Center+E+FVector(0,0,100);
    }
   }
  };
  auto* ActiveTarget=Hero->GetCombat()?Hero->GetCombat()->GetTarget():nullptr;
  if(!IsValid(ActiveTarget))ActiveTarget=Target.Get();
  for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
  {
   if(Kind==EEGHeroKind::GriseoAdult)
   {
    const bool bKnight=Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetDefinition()&&Unit->GetDefinition()->HeroKind==EEGHeroKind::Knight;
    if(Unit==Hero.Get()||Unit==ActiveTarget||bKnight)AddUnit(Unit);
   }
   else if(Unit->GetFaction_Implementation()==EEGFaction::Player||Unit->GetFaction_Implementation()==EEGFaction::Enemy)AddUnit(Unit);
  }
  if(Kind==EEGHeroKind::VillV)if(auto* Mechanism=Hero->FindComponentByClass<UEGVillVMechanismComponent>())
   for(int32 I=0;I<2;++I)AddUnit(Mechanism->GetDevice(I));
  if(!Bounds.IsValid)return;
  const double Now=FPlatformTime::Seconds();
  const float Dt=LastFramingAt>0.?FMath::Clamp(float(Now-LastFramingAt),0.f,.1f):0.f;LastFramingAt=Now;
  const FVector DesiredCenter=Bounds.GetCenter();
  FramingCenter=bSnap?DesiredCenter:FMath::VInterpTo(FramingCenter,DesiredCenter,Dt,1.5f);
  const FRotationMatrix Basis(FramingRotation);
  const FVector Right=Basis.GetScaledAxis(EAxis::Y),Up=Basis.GetScaledAxis(EAxis::Z),Forward=Basis.GetScaledAxis(EAxis::X);
  float HalfWidth=0.f,HalfHeight=0.f,Depth=0.f;
  for(int32 I=0;I<8;++I)
  {
   const FVector Corner((I&1)?Bounds.Max.X:Bounds.Min.X,(I&2)?Bounds.Max.Y:Bounds.Min.Y,(I&4)?Bounds.Max.Z:Bounds.Min.Z);
   const FVector Offset=Corner-FramingCenter;
   HalfWidth=FMath::Max(HalfWidth,float(FMath::Abs(FVector::DotProduct(Offset,Right))));
   HalfHeight=FMath::Max(HalfHeight,float(FMath::Abs(FVector::DotProduct(Offset,Up))));
   Depth=FMath::Max(Depth,float(FMath::Abs(FVector::DotProduct(Offset,Forward))));
  }
  const float HorizontalTangent=FMath::Tan(FMath::DegreesToRadians(26.f));
  const float VerticalTangent=HorizontalTangent/(16.f/9.f);
  const float Required=FMath::Max((HalfWidth+160.f)/HorizontalTangent,(HalfHeight+160.f)/VerticalTangent)+Depth;
  const float DesiredDistance=FMath::Max(Kind==EEGHeroKind::GriseoAdult?1800.f:2300.f,Required);
  FramingDistance=bSnap?DesiredDistance:DesiredDistance>FramingDistance?DesiredDistance:FMath::FInterpTo(FramingDistance,DesiredDistance,Dt,.6f);
  Camera->SetActorLocationAndRotation(FramingCenter-Forward*FramingDistance,FramingRotation);
 }
 void HideUI(UWorld* World,AEGDemoGameMode* Mode)
 {
  // Remove the initial top-level HUD instead of toggling visibility: production
  // refresh may restore visibility. Later Human transformation movie is new and
  // therefore is deliberately not in this list.
  if(Step==1)
  {
   TArray<UUserWidget*> Widgets;
   UWidgetBlueprintLibrary::GetAllWidgetsOfClass(World,Widgets,UUserWidget::StaticClass(),true);
   for(auto* Widget:Widgets)if(IsValid(Widget))SuppressedHUD.Add(Widget);
  }
  for(const auto& Widget:SuppressedHUD)if(Widget.IsValid()&&Widget->IsInViewport())Widget->RemoveFromParent();
  if(Mode&&Mode->BattleRules)for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit))
  {
   TArray<UWidgetComponent*> Components;Unit->GetComponents(Components);
   for(auto* Component:Components)if(IsValid(Component))Component->SetVisibility(false,true);
  }
 }
 bool Fail(const FString& Reason,UWorld* World)
 {
  Test->AddError(Reason);Signal(TEXT("error"),World);return true;
 }
public:
 FShowcaseHero(FAutomationTestBase* In,const TCHAR* InClip,const TCHAR* InCard,EEGHeroKind InKind)
  :Test(In),Clip(InClip),CardPath(InCard),RunId(FGuid::NewGuid().ToString(EGuidFormats::Digits)),Kind(InKind){}
 ~FShowcaseHero()
 {
  if(bScreenMessagesSuppressed)GAreScreenMessagesEnabled=bScreenMessagesWereEnabled;
  if(FrozenStageClock.IsValid())FrozenStageClock->SetActorTickEnabled(bStageClockWasTicking);
  if(auto* World=CaptureWorld.Get())if(auto* PC=World->GetFirstPlayerController())
   if(Camera.IsValid()&&PreviousView.IsValid())PC->SetViewTarget(PreviousView.Get());
  if(Camera.IsValid())Camera->Destroy();
 }
 bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  UWorld* World=GEditor?GEditor->PlayWorld.Get():nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(FPlatformTime::Seconds()>Deadline)return Fail(TEXT("Hero showcase timed out"),World);
  if(!World||!Mode||!Mode->GetCards()||!Mode->BattleRules)return false;
  CaptureWorld=World;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Card=LoadObject<UEGCardDefinition>(nullptr,*CardPath);
   if(!Card||!Mode->GetCards()->GrantCard(Card))return Fail(TEXT("Cannot grant formal showcase card ")+Clip,World);
   const auto Hand=Mode->GetCards()->GetHand();
   if(Hand.IsEmpty()||!Mode->GetCards()->TryDeployHandCard(Hand.Last().InstanceId,{3,1})||!Mode->EnterCombat())
    return Fail(TEXT("Cannot deploy formal showcase hero ")+Clip,World);
   Step=1;return false;
  }
  if(Step==1)
  {
   if(!Mode->BattleRules->IsCombatRunning())return false;
   FAssetCompilingManager::Get().FinishAllCompilation();
   if(GShaderCompilingManager&&GShaderCompilingManager->IsCompiling())GShaderCompilingManager->FinishAllCompilation();
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit)&&Unit->GetDefinition()&&Unit->GetAbilitySystemComponent())
   {
    Unit->GetCombat()->SetComponentTickEnabled(false);
    auto* ASC=Unit->GetAbilitySystemComponent();
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),1000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),1000000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
    if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetDefinition()->HeroKind==Kind)Hero=Unit;
    else if(Unit->GetFaction_Implementation()==EEGFaction::Enemy&&!Target.IsValid())Target=Unit;
   }
   if(!Hero.IsValid()||!Target.IsValid())return Fail(TEXT("Formal hero or target missing for ")+Clip,World);
   if(Kind==EEGHeroKind::GriseoAdult)
   {bScreenMessagesWereEnabled=GAreScreenMessagesEnabled;GAreScreenMessagesEnabled=false;bScreenMessagesSuppressed=true;}
   Hero->GetHeroSkill()->ApplyPersistentLevel(11);
   const FVector Origin=Hero->GetActorLocation();int32 EnemyIndex=0;
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
   {
    const FVector EnemyLocation=Kind==EEGHeroKind::VillV&&Mode->GetBoard()
     ?Mode->GetBoard()->CellToWorld(FIntPoint(1+2*EnemyIndex,4))+FVector(0,0,88)
     :Origin+FVector((EnemyIndex-1)*200.f,650.f,0.f);
    Unit->SetActorLocation(EnemyLocation,false,nullptr,ETeleportType::TeleportPhysics);++EnemyIndex;
   }
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetAttackRangeAttribute(),Kind==EEGHeroKind::VillV?3000.f:1800.f);
   if(Kind==EEGHeroKind::VillV)
   {
    auto* Mechanism=Hero->FindComponentByClass<UEGVillVMechanismComponent>();
    if(!Mechanism||!Mechanism->GetDevice(0)||!Mechanism->GetDevice(1))return Fail(TEXT("Vill-V showcase requires both formal devices"),World);
    for(int32 I=0;I<2;++I)
    {
     auto* Device=Mechanism->GetDevice(I);Device->GetCombat()->SetComponentTickEnabled(true);
     for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
      if(FVector::Dist2D(Device->GetActorLocation(),Unit->GetActorLocation())<600.f)
       return Fail(TEXT("Vill-V showcase target overlaps a device cell"),World);
    }
    // Preserve the actual component/animation ticks while preventing this
    // controlled 40-second clip from hitting the campaign overtime cutoff.
    FrozenStageClock=Mode;bStageClockWasTicking=Mode->IsActorTickEnabled();Mode->SetActorTickEnabled(false);
   }
   auto* PC=World->GetFirstPlayerController();if(!PC)return Fail(TEXT("Missing showcase player controller"),World);
   PreviousView=PC->GetViewTarget();Camera=World->SpawnActor<ACameraActor>();
   if(!Camera.IsValid())return Fail(TEXT("Could not create test-only showcase camera"),World);
   const FVector Mid=Origin+FVector(0,300,160),Position=Mid+(Kind==EEGHeroKind::VillV?FVector(1800,-1300,1450):FVector(1350,1450,1250));
   Camera->GetCameraComponent()->SetFieldOfView(52.f);Camera->SetActorLocationAndRotation(Position,(Mid-Position).Rotation());
   FramingCenter=Mid;FramingRotation=(Mid-Position).Rotation();FramingDistance=FVector::Dist(Position,Mid);
   if(Kind==EEGHeroKind::GriseoAdult)FramingRotation.Pitch=-65.f;
   if(Kind==EEGHeroKind::VillV||Kind==EEGHeroKind::Pardofelis||Kind==EEGHeroKind::GriseoAdult)UpdateFraming(Mode,true);
   PC->SetViewTarget(Camera.Get());HideUI(World,Mode);
   if(!Signal(TEXT("ready"),World))return Fail(TEXT("Could not write ready signal"),World);
   ReadyAt=FPlatformTime::Seconds();Step=2;
   Test->AddInfo(TEXT("Controlled showcase: production card and combat, enemy AI frozen, temporary health/range/level; actual montage notifies, not natural battle."));
   return false;
  }
  HideUI(World,Mode);
  if(Kind==EEGHeroKind::VillV||Kind==EEGHeroKind::Pardofelis||Kind==EEGHeroKind::GriseoAdult)UpdateFraming(Mode);
  if(!Hero.IsValid()||!Target.IsValid())return Fail(TEXT("Showcase subject or target lost"),World);
  if(Step==2)
  {
   if(FPlatformTime::Seconds()-ReadyAt<3.)return false;
   if(!Signal(TEXT("start"),World))return Fail(TEXT("Could not write start signal"),World);
   StartAt=FPlatformTime::Seconds();Step=3;return false;
  }
  const double Elapsed=FPlatformTime::Seconds()-StartAt;
  if(Kind==EEGHeroKind::ElysiaHerrscher&&Elapsed>=16.&&PreviousView.IsValid())
   if(auto* PC=World->GetFirstPlayerController();PC&&PC->GetViewTarget()!=PreviousView.Get())PC->SetViewTarget(PreviousView.Get());
  auto* Skill=Hero->GetHeroSkill();
  if(Step==3&&Elapsed>=2.&&!Hero->GetCombat()->IsComponentTickEnabled())Hero->GetCombat()->SetComponentTickEnabled(true);
  if(Elapsed>=10.&&!bMana)
  {
   Hero->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Hero->GetAttributes()->GetMaxMana());bMana=true;
  }
  if(Skill->IsActionRunning())
  {
   if(Skill->IsCasting())bUltimateSeen=true;else bBasicSeen=true;
  }
  for(TActorIterator<AEGUltimateSignetVisual> It(World);It;++It)if(It->GetOwner()==Hero.Get()){bSignetSeen=true;break;}
  if(Elapsed>=21.&&!bRevive&&(Kind==EEGHeroKind::Hua||Kind==EEGHeroKind::CollapsedHua)&&!Skill->IsCasting())
  {
   bRevive=true;FEGCombatExecution::Damage(Target.Get(),Hero.Get(),Hero->GetAttributes()->GetHealth()*3.f);
  }
  if(Kind==EEGHeroKind::VillV)
  {
   if(const auto* Audio=Hero->FindComponentByClass<UEGCombatAudioComponent>())
   {
    bVillVGun|=Audio->GetVillVGunLoop()!=nullptr||Audio->LastNode==TEXT("GunFire1")||Audio->LastNode==TEXT("GunFire2")||Audio->LastNode==TEXT("GunFire3");
    bVillVCannon|=Audio->LastNode==TEXT("CannonFire")||Audio->LastSoundPath.Contains(TEXT("CannonFire"));
   }
   for(TActorIterator<AEGVillVShotVisual> It(World);It;++It)if(It->GetOwner()==Hero.Get()){bVillVShot=true;break;}
   for(auto* Unit:Mode->BattleRules->Units())if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Enemy)
    bVillVDamage|=Unit->LastCombatResolution.SourceId==Hero->GetInstanceId()&&Unit->LastCombatResolution.HealthDelta<0.f;
  }
  if(Elapsed<Duration()-2.)return false;
  if(Step==3)
  {
   Hero->GetCombat()->SetComponentTickEnabled(false);LastSignalAt=FPlatformTime::Seconds();Step=4;return false;
  }
  if(FPlatformTime::Seconds()-LastSignalAt<2.)return false;
  if(!Signal(TEXT("end"),World))return Fail(TEXT("Could not write end signal"),World);
  Test->TestTrue(TEXT("Observed a real basic action"),bBasicSeen);
  Test->TestTrue(TEXT("Observed a real ultimate action"),bUltimateSeen);
  Test->TestTrue(TEXT("Observed the hero ultimate signet visual"),bSignetSeen);
  if(Kind==EEGHeroKind::VillV)
  {
   const auto* Mechanism=Hero->FindComponentByClass<UEGVillVMechanismComponent>();
   Test->TestTrue(TEXT("Observed a real device transfer"),Mechanism&&Mechanism->JumpTransferCount>0);
   Test->TestTrue(TEXT("Observed machine-gun fire audio event or loop"),bVillVGun);
   Test->TestTrue(TEXT("Observed cannon fire audio event"),bVillVCannon);
   Test->TestTrue(TEXT("Observed a real shot visual"),bVillVShot);
   Test->TestTrue(TEXT("Observed authoritative Vill-V damage"),bVillVDamage);
  }
  return true;
 }
};
}

#define EG_SHOWCASE_HERO(TestClass,Name,Card,Kind) \
 IMPLEMENT_SIMPLE_AUTOMATION_TEST(TestClass,"ElyGambit.Showcase." Name,EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter) \
 bool TestClass::RunTest(const FString&) { \
  if(!GEditor||GEditor->PlayWorld||!FApp::CanEverRender()){AddError(TEXT("Showcase requires idle graphics editor"));return false;} \
  ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7"))); \
  ADD_LATENT_AUTOMATION_COMMAND(FShowcaseStartPIE()); \
  ADD_LATENT_AUTOMATION_COMMAND(FShowcaseHero(this,TEXT(Name),TEXT(Card),EEGHeroKind::Kind)); \
  ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true; }

EG_SHOWCASE_HERO(FShowcasePardo,"Pardo","/Game/Blueprints/Characters/Pardofelis/Data/DA_Card_Pardo",Pardofelis)
EG_SHOWCASE_HERO(FShowcaseHua,"Hua","/Game/Blueprints/Characters/FuHua/Data/DA_Card_Hua",Hua)
EG_SHOWCASE_HERO(FShowcaseMobius,"Mobius","/Game/Blueprints/Characters/Mobius/Data/DA_Card_Mobius",Mobius)
EG_SHOWCASE_HERO(FShowcaseSakura,"Sakura","/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura",Sakura)
EG_SHOWCASE_HERO(FShowcaseVillV,"VillV","/Game/Blueprints/Characters/VillV/Data/DA_Card_VillV",VillV)
EG_SHOWCASE_HERO(FShowcaseEden,"Eden","/Game/Blueprints/Characters/Eden/Data/DA_Card_Eden",Eden)
EG_SHOWCASE_HERO(FShowcaseAponia,"Aponia","/Game/Blueprints/Characters/Aponia/Data/DA_Card_Aponia",Aponia)
EG_SHOWCASE_HERO(FShowcaseGriseo,"Griseo","/Game/Blueprints/Characters/Griseo/Data/DA_Card_Griseo",Griseo)
EG_SHOWCASE_HERO(FShowcaseElysia,"Elysia","/Game/Blueprints/Characters/Elysia/Data/Combat20261025/DA_Card_Elysia",Elysia)
EG_SHOWCASE_HERO(FShowcaseHuman,"Human","/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human",ElysiaHerrscher)
EG_SHOWCASE_HERO(FShowcaseCollapsedHua,"CollapsedHua","/Game/Blueprints/Characters/FuHua/FuSheng/Data/DA_Card_CollapsedHua",CollapsedHua)
EG_SHOWCASE_HERO(FShowcaseGriseoAdult,"GriseoAdult","/Game/Blueprints/Characters/Griseo/Adult/Data/DA_Card_GriseoAdult",GriseoAdult)
#undef EG_SHOWCASE_HERO
#endif
