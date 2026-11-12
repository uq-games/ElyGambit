#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/App.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "NiagaraComponent.h"
#include "Materials/MaterialInterface.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGElysiaCombatVisual.h"
#include "Presentation/EGElysiaStarSeaVisual.h"
#include "Placement/EGBoardScene.h"
#include "Placement/EGDeploymentBoard.h"

namespace
{
class FElysiaPolishStartPIE final : public IAutomationLatentCommand
{
 FIntPoint Size;
public:
 explicit FElysiaPolishStartPIE(FIntPoint InSize):Size(InSize){}
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(Size.X,Size.Y))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

struct FElysiaPolishCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle Handle;
 FString Path;
 bool bRequested=false;
 bool bMatches=false;
 ~FElysiaPolishCapture(){if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);}
};

FString ActionSummary(AEGChessUnit* Unit)
{
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;
 auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 auto* Body=Unit?Unit->GetVisiblePresentationMesh():nullptr;
 auto* Anim=Body?Body->GetAnimInstance():nullptr;
 const auto* Clip=Skill?Skill->GetActiveAnimation():nullptr;
 return FString::Printf(TEXT("action=%s montage=%s position=%.3f id=%s casting=%d"),
  Clip?*Clip->GetName():TEXT("none"),Montage?*Montage->GetName():TEXT("none"),
  Anim&&Montage?Anim->Montage_GetPosition(Montage):-1.f,
  Skill?*Skill->ActionId.ToString():TEXT("none"),Skill&&Skill->IsCasting()?1:0);
}

float ActionPosition(AEGChessUnit* Unit)
{
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;
 auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 auto* Body=Unit?Unit->GetVisiblePresentationMesh():nullptr;
 auto* Anim=Body?Body->GetAnimInstance():nullptr;
 return Anim&&Montage?Anim->Montage_GetPosition(Montage):-1.f;
}

bool ChargedFrame(AEGChessUnit* Unit)
{
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;
 const auto* Clip=Skill?Skill->GetActiveAnimation():nullptr;
 const float Position=ActionPosition(Unit);
 return Clip&&Clip->GetName().Contains(TEXT("BranchLV3"))&&Position>=.6f&&Position<=1.1f;
}

AEGElysiaCombatVisual* FindArrowRain(UWorld* World,AEGChessUnit* Unit)
{
 if(!World||!Unit)return nullptr;
 for(TActorIterator<AEGElysiaCombatVisual> It(World);It;++It)
  if(!It->IsActorBeingDestroyed()&&It->GetOwner()==Unit&&It->VisualKind==2)return *It;
 return nullptr;
}

bool FinisherFrame(AEGChessUnit* Unit,EEGElysiaHerrscherPresentationPhase Phase)
{
 const float Position=ActionPosition(Unit);
 return (Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin||Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased)
  &&Position>=1.8f&&Position<=2.3f;
}

class FElysiaPolishBattle final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 FIntPoint Size;
 FString Directory,RunId;
 TWeakObjectPtr<AEGChessUnit> Elysia,Human;
 TWeakObjectPtr<UStaticMeshComponent> GroundBeforeSea;
 TWeakObjectPtr<UMaterialInterface> GroundMaterialBeforeSea;
 TSharedPtr<FElysiaPolishCapture> Capture;
 double Deadline=0.,BattleStart=-1.,StopAt=-1.;
 double FinisherReleasedAt=-1.,RainObservedAt=-1.;
 bool bStarted=false,bManaInjected=false,bStopped=false;
 bool bNatural=false,bNaturalClosing=false;
 bool bCaptured[9]={false,false,false,false,false,false,false,false,false};
 bool bReportedStarSeaGeometry=false;
 bool bReportedRainGeometry=false;
 int32 Capturing=INDEX_NONE,Periodic=0;

 void ReportStarSeaGeometry(UWorld* World)
 {
  if(bReportedStarSeaGeometry||!World)return;
  AEGElysiaStarSeaVisual* Sea=nullptr;
  for(TActorIterator<AEGElysiaStarSeaVisual> It(World);It;++It)
   if(!It->IsActorBeingDestroyed()&&It->GetOwner()==Human.Get()){Sea=*It;break;}
  if(!Sea)return;
  UStaticMeshComponent* Surface=nullptr;UStaticMeshComponent* Outer=nullptr;
  TArray<UStaticMeshComponent*> Components;Sea->GetComponents(Components);
  for(auto* Component:Components)if(Component)
  {
   if(Component->GetFName()==TEXT("DomainSurface"))Surface=Component;
   else if(Component->GetFName()==TEXT("OuterBoardSurface"))Outer=Component;
  }
  if(!Surface)return;
  AEGBoardSceneSettings* Scene=nullptr;
  for(TActorIterator<AEGBoardSceneSettings> It(World);It;++It)if(IsValid(It->Board)&&IsValid(It->FramingGround)){Scene=*It;break;}
  if(!Scene)return;
  auto* Board=Scene->Board.Get();auto* Ground=Scene->FramingGround->FindComponentByClass<UStaticMeshComponent>();
  if(!Ground)return;
  bReportedStarSeaGeometry=true;
  Test->TestTrue(TEXT("Star sea leaves the original ground component material unchanged"),
   GroundBeforeSea.Get()==Ground&&GroundMaterialBeforeSea.IsValid()&&Ground->GetMaterial(0)==GroundMaterialBeforeSea.Get());
  if(Test->TestNotNull(TEXT("Star sea owns a separate outer board overlay"),Outer))
  {
   const FVector GroundMin=Ground->Bounds.Origin-Ground->Bounds.BoxExtent,GroundMax=Ground->Bounds.Origin+Ground->Bounds.BoxExtent;
   const FVector OuterMin=Outer->Bounds.Origin-Outer->Bounds.BoxExtent,OuterMax=Outer->Bounds.Origin+Outer->Bounds.BoxExtent;
   const bool bCoversXY=OuterMin.X<=GroundMin.X+1.&&OuterMin.Y<=GroundMin.Y+1.&&OuterMax.X>=GroundMax.X-1.&&OuterMax.Y>=GroundMax.Y-1.;
   Test->TestTrue(TEXT("Outer overlay uses the actual ground mesh"),Outer->GetStaticMesh()&&Outer->GetStaticMesh()==Ground->GetStaticMesh());
   Test->TestTrue(TEXT("Outer overlay covers ground XY bounds"),bCoversXY);
   Test->TestTrue(TEXT("Outer overlay is visible without collision"),Outer->IsVisible()&&!Outer->bHiddenInGame&&Outer->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
   float OuterOpacity=-1.f;auto* OuterMaterial=Outer->GetMaterial(0);
   const bool bOuterOpacity=OuterMaterial&&OuterMaterial->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DomainOpacity")),OuterOpacity);
   Test->AddInfo(FString::Printf(TEXT("Star sea outer: mesh=%s boundsOrigin=%s boundsExtent=%s visible=%d collision=%d material=%s opacityPresent=%d opacity=%.3f groundMaterialBefore=%s groundMaterialNow=%s xyCovers=%d"),
    *GetNameSafe(Outer->GetStaticMesh()),*Outer->Bounds.Origin.ToString(),*Outer->Bounds.BoxExtent.ToString(),Outer->IsVisible()?1:0,int32(Outer->GetCollisionEnabled()),
    *GetNameSafe(OuterMaterial),bOuterOpacity?1:0,OuterOpacity,*GetNameSafe(GroundMaterialBeforeSea.Get()),*GetNameSafe(Ground->GetMaterial(0)),bCoversXY?1:0));
  }
  float Opacity=-1.f;auto* Material=Surface->GetMaterial(0);
  const bool bOpacity=Material&&Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("DomainOpacity")),Opacity);
  Test->AddInfo(FString::Printf(TEXT("Star sea geometry: board=%s grid=%dx%d cell=(%.2f,%.2f) floorOffset=%.2f surface=%s surfaceLocation=%s surfaceExtent=%s visible=%d material=%s opacityPresent=%d opacity=%.3f ground=%s boundsOrigin=%s boundsExtent=%s collision=%d"),
   *Board->GetActorTransform().ToHumanReadableString(),Board->Columns,Board->Rows,Board->CellWidth,Board->CellDepth,Sea->FloorOffset,
   *Surface->GetPathName(),*Surface->GetComponentLocation().ToString(),*Surface->Bounds.BoxExtent.ToString(),Surface->IsVisible()?1:0,
   *GetNameSafe(Material),bOpacity?1:0,Opacity,*Ground->GetPathName(),*Ground->Bounds.Origin.ToString(),*Ground->Bounds.BoxExtent.ToString(),int32(Ground->GetCollisionEnabled())));
  const FVector Normal=Board->GetActorTransform().GetUnitAxis(EAxis::Z);
  const FIntPoint Cells[]={FIntPoint(Board->Columns/2,Board->Rows/2),FIntPoint(Board->Columns/4,Board->Rows/4),
   FIntPoint(Board->Columns*3/4,Board->Rows/4),FIntPoint(Board->Columns/4,Board->Rows*3/4),FIntPoint(Board->Columns*3/4,Board->Rows*3/4)};
  FCollisionQueryParams Params(SCENE_QUERY_STAT(ElysiaStarSeaGround),true);
  for(const FIntPoint Cell:Cells)
  {
   const FVector Plane=Board->CellToWorld(Cell);FHitResult Hit;
   const bool bHit=Ground->LineTraceComponent(Hit,Plane+Normal*5000.f,Plane-Normal*5000.f,Params);
   const double SurfaceAboveHit=bHit?FVector::DotProduct(Surface->GetComponentLocation()-Hit.ImpactPoint,Normal):0.;
   Test->AddInfo(FString::Printf(TEXT("Star sea ground probe cell=(%d,%d) plane=%s collisionHit=%d hit=%s surfaceAboveHit=%.2fcm; trace failure is inconclusive for non-colliding visual ground"),
    Cell.X,Cell.Y,*Plane.ToString(),bHit?1:0,*Hit.ImpactPoint.ToString(),SurfaceAboveHit));
  }
 }

 bool BeginCapture(UWorld* World,int32 Index,const TCHAR* Label)
 {
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return false;
  Capture=MakeShared<FElysiaPolishCapture>();Capture->Client=Client;
  Capture->Path=FPaths::ConvertRelativePathToFull(Directory/(FString(Label)+TEXT("_")+RunId+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
  const TWeakPtr<FElysiaPolishCapture> Weak=Capture;
  const TWeakObjectPtr<UWorld> WeakWorld(World);const auto A=Elysia;const auto B=Human;
  FAutomationTestBase* CurrentTest=Test;const double Start=BattleStart;
  const double ReleaseAt=FinisherReleasedAt,RainAt=RainObservedAt;
  Capture->Handle=Client->OnDrawn().AddLambda([Weak,WeakWorld,A,B,CurrentTest,Start,Index,ReleaseAt,RainAt]()
  {
   auto State=Weak.Pin();auto* W=WeakWorld.Get();if(!State.IsValid()||!W||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   auto* ES=A.IsValid()?Cast<UEGElysiaSkillBehavior>(A->GetHeroSkill()->GetBehavior()):nullptr;
   auto* HS=B.IsValid()?Cast<UEGElysiaHerrscherSkillBehavior>(B->GetHeroSkill()->GetBehavior()):nullptr;
   const auto EP=ES?ES->GetPassiveState():FEGElysiaPassiveState();
   const auto HP=HS?HS->GetPassiveState():FEGElysiaPassiveState();
   const auto Phase=HS?HS->GetPresentationPhase():EEGElysiaHerrscherPresentationPhase::BattleEnd;
   const bool bRain=ES&&ES->IsArrowRainActive()&&FindArrowRain(W,A.Get());
   const auto* OrdinarySkill=A.IsValid()?A->GetHeroSkill():nullptr;
   const bool bRainAndBasic=bRain&&OrdinarySkill&&OrdinarySkill->IsActionRunning()&&!OrdinarySkill->IsCasting()&&OrdinarySkill->GetActiveMontage();
   State->bMatches=Index==INDEX_NONE||Index==0||(Index==1&&ChargedFrame(A.Get()))||
    (Index==2&&bRain&&RainAt>=0.&&W->GetTimeSeconds()-RainAt>=.5&&W->GetTimeSeconds()-RainAt<=1.5)||
    (Index==3&&(Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::EntryReleased))||
    (Index==4&&Phase==EEGElysiaHerrscherPresentationPhase::Origin)||
    (Index==5&&FinisherFrame(B.Get(),Phase))||
    (Index==6&&Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased&&ReleaseAt>=0.&&W->GetTimeSeconds()-ReleaseAt>=.15&&W->GetTimeSeconds()-ReleaseAt<=.30)||
    (Index==7&&bRain&&RainAt>=0.&&W->GetTimeSeconds()-RainAt>=2.&&W->GetTimeSeconds()-RainAt<=3.)||
    (Index==8&&bRainAndBasic);
   CurrentTest->AddInfo(FString::Printf(TEXT("Capture draw battle=%.3fs releaseSinceObserved=%.3fs rainSinceObserved=%.3fs ordinary phase=%d progress=%d/%d remaining=%.2f %s; human phase=%d progress=%d/%d remaining=%.2f %s; path=%s"),
    W->GetTimeSeconds()-Start,ReleaseAt>=0.?W->GetTimeSeconds()-ReleaseAt:-1.,RainAt>=0.?W->GetTimeSeconds()-RainAt:-1.,int32(EP.Phase),EP.Progress,EP.Threshold,EP.RemainingSeconds,*ActionSummary(A.Get()),
    int32(HP.Phase),HP.Progress,HP.Threshold,HP.RemainingSeconds,*ActionSummary(B.Get()),*State->Path));
   State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  });
  Capturing=Index;return true;
 }

 bool CompleteCapture()
 {
  if(!Capture.IsValid()||!Capture->bRequested||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;
  if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
  Test->TestEqual(TEXT("Screenshot dimensions"),FIntPoint(int32(BE(16)),int32(BE(20))),Size);
  if(Capturing>=0&&Capturing<9&&Capture->bMatches)bCaptured[Capturing]=true;
  Capture.Reset();Capturing=INDEX_NONE;return true;
 }

public:
 FElysiaPolishBattle(FAutomationTestBase* InTest,FIntPoint InSize,bool bInNatural=false):Test(InTest),Size(InSize),RunId(FGuid::NewGuid().ToString(EGuidFormats::Digits)),bNatural(bInNatural)
 {const FString Name=bNatural?FString(TEXT("Natural1080")):FString::Printf(TEXT("%dx%d"),Size.X,Size.Y);
  Directory=FPaths::ProjectSavedDir()/TEXT("ElysiaPolish20261107/Graphics")/Name;}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Controlled Elysia graphical fixture timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(!bNatural)
   {
    for(TActorIterator<AEGBoardSceneSettings> It(World);It;++It)
     if(IsValid(It->Board)&&IsValid(It->FramingGround))
     {GroundBeforeSea=It->FramingGround->FindComponentByClass<UStaticMeshComponent>();break;}
    if(!Test->TestTrue(TEXT("Ground mesh exists before any star sea"),GroundBeforeSea.IsValid()))return true;
    GroundMaterialBeforeSea=GroundBeforeSea->GetMaterial(0);
    if(!Test->TestTrue(TEXT("Ground material exists before any star sea"),GroundMaterialBeforeSea.IsValid()))return true;
   }
   auto* Cards=Mode->GetCards();FGuid Ordinary,HumanCard;
   for(const auto& C:Cards->GetHand())if(C.Definition&&C.Definition->HeroDefinition)
   {
    if(C.Definition->HeroDefinition->HeroKind==EEGHeroKind::Elysia)Ordinary=C.InstanceId;
    if(C.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher)HumanCard=C.InstanceId;
   }
   if(!Ordinary.IsValid()){Test->AddError(TEXT("Formal opening Elysia card is missing"));return true;}
   if(!HumanCard.IsValid())
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Card_Human.DA_Card_Human"));
    if(!Test->TestNotNull(TEXT("Human card"),Card)||!Test->TestTrue(TEXT("Grant fixture human card"),Cards->GrantCard(Card)))return true;
    HumanCard=Cards->GetHand().Last().InstanceId;
   }
   if(!Test->TestTrue(TEXT("Deploy ordinary Elysia"),Cards->TryDeployHandCard(Ordinary,{1,1}))||
      !Test->TestTrue(TEXT("Deploy human"),Cards->TryDeployHandCard(HumanCard,{4,1}))||
      !Test->TestTrue(TEXT("Enter formal board combat"),Mode->EnterCombat()))return true;
   int32 Enemies=0;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetDefinition()&&Unit->GetAbilitySystemComponent())
   {
    if(Unit->GetFaction_Implementation()==EEGFaction::Enemy)++Enemies;
    else if(Unit->GetDefinition()->HeroKind==EEGHeroKind::Elysia)Elysia=Unit;
    else if(Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)Human=Unit;
    if(!bNatural)
    {
     auto* ASC=Unit->GetAbilitySystemComponent();
     ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),10000000.f);
     ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),10000000.f);
     if(Unit->GetFaction_Implementation()==EEGFaction::Player)ASC->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),0.f);
    }
   }
   if(!Test->TestTrue(TEXT("Both characters and at least one enemy spawned"),Elysia.IsValid()&&Human.IsValid()&&Enemies>0))return true;
   Test->AddInfo(bNatural?TEXT("Natural AI fixture: human card supplied, both cards deployed through CardManager. No health, mana, damage or combat timing was modified; observation ends at settlement or 25 seconds. No real input or visual acceptance is claimed."):
    TEXT("Controlled PIE: human card granted, cards deployed through CardManager, all units given 10,000,000 max/current health; player mana starts at zero and is later filled once. Combat and animation advance normally. This is not natural play or visual acceptance."));
   bStarted=true;return false;
  }
  if(Capture.IsValid()&&!CompleteCapture())return false;
  if(bNatural)
  {
   if(bNaturalClosing)return true;
   const auto MatchPhase=Mode->GetMatch()->GetPhase();
   const bool bRunning=Mode->BattleRules->IsCombatRunning();
   if(BattleStart<0.)
   {
    if(bRunning)BattleStart=World->GetTimeSeconds();
    else if(MatchPhase!=EEGMatchPhase::Combat){Test->AddError(TEXT("Natural AI encounter ended before its running phase was observed"));return true;}
    else return false;
   }
   const double Elapsed=World->GetTimeSeconds()-BattleStart;
   if(MatchPhase!=EEGMatchPhase::Combat||!bRunning||Elapsed>=25.)
   {
    if(MatchPhase!=EEGMatchPhase::Combat)
     Test->AddInfo(FString::Printf(TEXT("Natural AI result: %s at %.3f battle seconds, match phase %d"),Mode->WasVictory()?TEXT("victory"):TEXT("defeat"),Elapsed,int32(MatchPhase)));
    else Test->AddInfo(FString::Printf(TEXT("Natural AI result: still in combat at %.3f seconds; outcome not observed"),Elapsed));
    if(BeginCapture(World,INDEX_NONE,TEXT("natural_final")))bNaturalClosing=true;
    return false;
   }
   if(Elapsed>=1.+Periodic*4.)
   {const FString Label=FString::Printf(TEXT("natural_%02d"),Periodic);if(BeginCapture(World,INDEX_NONE,*Label))++Periodic;}
   return false;
  }
  if(bStopped)
  {
   if(World->GetTimeSeconds()-StopAt<1.f)return false;
   int32 Remaining=0;
   for(TActorIterator<AEGElysiaCombatVisual> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&(It->GetOwner()==Elysia.Get()||It->GetOwner()==Human.Get()))++Remaining;
   for(TActorIterator<AEGElysiaStarSeaVisual> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&It->GetOwner()==Human.Get())++Remaining;
   Test->TestEqual(TEXT("Stopped battle releases Elysia field and star sea actors"),Remaining,0);
   Test->TestTrue(TEXT("Ground material remains unchanged after star sea cleanup"),
    GroundBeforeSea.IsValid()&&GroundMaterialBeforeSea.IsValid()&&GroundBeforeSea->GetMaterial(0)==GroundMaterialBeforeSea.Get());
   return true;
  }
  if(!Elysia.IsValid()||!Human.IsValid()){Test->AddError(TEXT("A controlled character disappeared during combat"));return true;}
  if(!Mode->BattleRules->IsCombatRunning())
  {if(BattleStart>=0.){Test->AddError(TEXT("Controlled battle stopped before the capture completed"));return true;}return false;}
  if(BattleStart<0.)BattleStart=World->GetTimeSeconds();
  const double Elapsed=World->GetTimeSeconds()-BattleStart;
  auto* ES=Cast<UEGElysiaSkillBehavior>(Elysia->GetHeroSkill()->GetBehavior());
  auto* HS=Cast<UEGElysiaHerrscherSkillBehavior>(Human->GetHeroSkill()->GetBehavior());
  if(!Test->TestNotNull(TEXT("Ordinary skill behavior"),ES)||!Test->TestNotNull(TEXT("Human skill behavior"),HS))return true;
  const auto Phase=HS->GetPresentationPhase();
  const auto OrdinaryState=ES->GetPassiveState();
  if(Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased)
  {if(FinisherReleasedAt<0.)FinisherReleasedAt=World->GetTimeSeconds();}
  else if(Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin)FinisherReleasedAt=-1.;
  if(ES->IsArrowRainActive())
  {if(RainObservedAt<0.)RainObservedAt=World->GetTimeSeconds();}
  else RainObservedAt=-1.;
  if(ES->IsArrowRainActive()&&!bReportedRainGeometry)
  {
   if(auto* Visual=FindArrowRain(World,Elysia.Get()))
   {
    bReportedRainGeometry=true;
    const auto* Data=Cast<UEGElysiaSkillDefinition>(Elysia->GetDefinition()->SkillDefinition);
    TArray<UNiagaraComponent*> Effects;Visual->GetComponents(Effects);
    bool bActiveRain=false,bActiveArea=false;const UNiagaraComponent* AreaEffect=nullptr;
    for(const auto* Effect:Effects)
     if(Effect&&Data&&Effect->IsActive())
     {
      if(Data->RainCue.System.Get()&&Effect->GetAsset()==Data->RainCue.System.Get())bActiveRain=true;
      if(Data->RainAreaCue.System.Get()&&Effect->GetAsset()==Data->RainAreaCue.System.Get()){bActiveArea=true;AreaEffect=Effect;}
     }
    Test->TestTrue(TEXT("Arrow rain has an active configured Niagara system"),bActiveRain);
    Test->TestTrue(TEXT("Arrow rain has an active configured area indicator"),bActiveArea);
    Test->TestTrue(TEXT("Arrow rain uses the combat-owned locked center"),Visual->GetActorLocation().Equals(ES->GetArrowRainCenter(),.1f));
    Test->TestTrue(TEXT("Arrow rain has a positive combat-owned radius"),ES->GetArrowRainRadius()>0.f);
    if(AreaEffect)
    {
     bool bHasDiameter=false;const float Diameter=AreaEffect->GetVariableFloat(TEXT("User.BoardDiameter"),bHasDiameter);
     Test->TestTrue(TEXT("Rain area receives combat diameter"),bHasDiameter&&FMath::IsNearlyEqual(Diameter,2.f*ES->GetArrowRainRadius(),.1f));
     Test->AddInfo(FString::Printf(TEXT("Rain area geometry: location=%s relative=%s worldScale=%s componentDiameterPresent=%d componentDiameter=%.2f expectedDiameter=%.2f visible=%d hidden=%d boundsOrigin=%s boundsExtent=%s"),
      *AreaEffect->GetComponentLocation().ToString(),*AreaEffect->GetRelativeLocation().ToString(),*AreaEffect->GetComponentScale().ToString(),
      bHasDiameter?1:0,Diameter,2.f*ES->GetArrowRainRadius(),AreaEffect->IsVisible()?1:0,AreaEffect->bHiddenInGame?1:0,
      *AreaEffect->Bounds.Origin.ToString(),*AreaEffect->Bounds.BoxExtent.ToString()));
    }
    Test->AddInfo(FString::Printf(TEXT("Arrow rain visual=%s center=%s radius=%.2f remaining=%.2f activeSystem=%d activeArea=%d"),
     *Visual->GetPathName(),*Visual->GetActorLocation().ToString(),ES->GetArrowRainRadius(),ES->GetArrowRainRemaining(),bActiveRain?1:0,bActiveArea?1:0));
   }
  }
  if(Phase==EEGElysiaHerrscherPresentationPhase::Origin||Phase==EEGElysiaHerrscherPresentationPhase::FinisherBegin||Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased)
   ReportStarSeaGeometry(World);
  if(!bCaptured[0]&&Elapsed>.3){BeginCapture(World,0,TEXT("bow_passive"));return false;}
  if(!bCaptured[1]&&ChargedFrame(Elysia.Get())){BeginCapture(World,1,TEXT("ordinary_charged"));return false;}
  if(!bManaInjected&&Elapsed>=8.&&(bCaptured[1]||Elapsed>=12.))
  {
   for(auto* Unit:{Elysia.Get(),Human.Get()})Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Unit->GetAttributes()->GetMaxMana());
   bManaInjected=true;Test->AddInfo(FString::Printf(TEXT("Controlled full mana injected once at battle %.3fs"),Elapsed));
  }
  if(!bCaptured[2]&&RainObservedAt>=0.&&World->GetTimeSeconds()-RainObservedAt>=.5&&World->GetTimeSeconds()-RainObservedAt<=1.5)
  {BeginCapture(World,2,TEXT("ordinary_arrow_rain_early"));return false;}
  auto* OrdinarySkill=Elysia->GetHeroSkill();
  if(!bCaptured[8]&&ES->IsArrowRainActive()&&FindArrowRain(World,Elysia.Get())&&OrdinarySkill&&
   OrdinarySkill->IsActionRunning()&&!OrdinarySkill->IsCasting()&&OrdinarySkill->GetActiveMontage())
  {BeginCapture(World,8,TEXT("ordinary_attack_during_rain"));return false;}
  if(!bCaptured[7]&&RainObservedAt>=0.&&World->GetTimeSeconds()-RainObservedAt>=2.&&World->GetTimeSeconds()-RainObservedAt<=3.)
  {BeginCapture(World,7,TEXT("ordinary_arrow_rain_sustained"));return false;}
  if(!bCaptured[3]&&(Phase==EEGElysiaHerrscherPresentationPhase::Entering||Phase==EEGElysiaHerrscherPresentationPhase::EntryReleased)){BeginCapture(World,3,TEXT("human_entry"));return false;}
  if(!bCaptured[4]&&Phase==EEGElysiaHerrscherPresentationPhase::Origin){BeginCapture(World,4,TEXT("human_origin"));return false;}
  if(!bCaptured[5]&&FinisherFrame(Human.Get(),Phase)){BeginCapture(World,5,TEXT("human_finisher"));return false;}
  if(!bCaptured[6]&&Phase==EEGElysiaHerrscherPresentationPhase::FinisherReleased&&FinisherReleasedAt>=0.&&
   World->GetTimeSeconds()-FinisherReleasedAt>=.15&&World->GetTimeSeconds()-FinisherReleasedAt<=.30)
  {BeginCapture(World,6,TEXT("human_finisher_release"));return false;}
  if(Elapsed>=3.+Periodic*4.&&Periodic<6)
  {const FString Label=FString::Printf(TEXT("periodic_%02d"),Periodic);if(BeginCapture(World,INDEX_NONE,*Label))++Periodic;return false;}
  bool bAll=true;for(bool b:bCaptured)bAll&=b;
  if(Elapsed>=25.&&bAll)
  {
   Test->TestTrue(TEXT("Controlled battle resolves"),Mode->ResolveCombat(false));
   bStopped=true;StopAt=World->GetTimeSeconds();return false;
  }
  if(Elapsed>55.){for(int32 I=0;I<9;++I)if(!bCaptured[I])Test->AddError(FString::Printf(TEXT("Required capture %d not observed"),I));return true;}
  return false;
 }
};

bool QueueElysiaPolish(FAutomationTestBase* Test,FIntPoint Size,bool bNatural=false)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Elysia graphical test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Elysia graphical test requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FElysiaPolishStartPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FElysiaPolishBattle(Test,Size,bNatural));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaPolish1080,"ElyGambit.Integration.ElysiaPolish1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaPolish1080::RunTest(const FString&){return QueueElysiaPolish(this,{1920,1080});}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaPolish1440,"ElyGambit.Integration.ElysiaPolish1440",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaPolish1440::RunTest(const FString&){return QueueElysiaPolish(this,{2560,1440});}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGElysiaPolishNatural1080,"ElyGambit.Integration.ElysiaPolishNatural1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGElysiaPolishNatural1080::RunTest(const FString&){return QueueElysiaPolish(this,{1920,1080},true);}
#endif
