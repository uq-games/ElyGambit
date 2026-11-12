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
#include "Engine/StaticMesh.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Camera/PlayerCameraManager.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Actor/EGBoard.h"
#include "Character/EGChessUnit.h"
#include "Components/EGBattleContextComponent.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCombatComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Presentation/EGSourceEffect.h"
#include "Presentation/EGCombatVisual.h"
#include "Presentation/EGSkillNotify.h"

namespace
{
class FSakuraColdStartPIE final : public IAutomationLatentCommand
{
public:
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=1920;Settings->NewWindowHeight=1080;
  FRequestPlaySessionParams Params;Params.EditorPlaySettings=Settings;
  TSharedRef<SWindow> Window=SNew(SWindow).ScreenPosition(FVector2D(0,0)).ClientSize(FVector2D(1920,1080))
   .AutoCenter(EAutoCenter::None).CreateTitleBar(false).UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize).SaneWindowPlacement(false).AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);return true;
 }
};

struct FSakuraColdCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;FDelegateHandle Handle;FString Path;
 bool bRequested=false,bMatches=false;double ArmedAt=0.;
 ~FSakuraColdCapture()
 {
  if(Client.IsValid()&&Handle.IsValid())Client->OnDrawn().Remove(Handle);
 }
};

float SakuraPosition(AEGChessUnit* Unit)
{
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 auto* Body=Unit?Unit->GetVisiblePresentationMesh():nullptr;auto* Anim=Body?Body->GetAnimInstance():nullptr;
 return Anim&&Montage?Anim->Montage_GetPosition(Montage):-1.f;
}
int32 SakuraFXCount(UWorld* World,AEGChessUnit* Unit,UEGSourceEffectDefinition* Data)
{
 if(!World||!Unit||!Data)return 0;
 int32 Count=0;for(TActorIterator<AEGSourceEffect> It(World);It;++It)
  if(It->GetOwner()==Unit&&It->GetSourceDefinition()==Data&&!It->IsActorBeingDestroyed())++Count;
 return Count;
}
float SakuraFXAge(UWorld* World,AEGChessUnit* Unit,UEGSourceEffectDefinition* Data)
{
 if(!World||!Unit||!Data)return -1.f;
 for(TActorIterator<AEGSourceEffect> It(World);It;++It)
  if(It->GetOwner()==Unit&&It->GetSourceDefinition()==Data&&!It->IsActorBeingDestroyed())return It->GetElapsedTime();
 return -1.f;
}
struct FSakuraPartAudit
{
 int32 ActiveParts=0;bool bActivePartsAboveBoard=true;
};

double SakuraBoardZ(UWorld* World,AEGChessUnit* Unit)
{
 const auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 const auto* Board=Mode?Mode->GetBoard():nullptr;
 return Board&&Board->LayoutSource?Board->CellToWorld({0,0}).Z-Board->UnitCenterHeight:
  Unit?Unit->GetActorLocation().Z-88.0:0.0;
}

FSakuraPartAudit SakuraAuditParts(AActor* Actor,double BoardZ,const FVector& Camera,FAutomationTestBase* Test,const FString& MeshFilter=FString())
{
 FSakuraPartAudit Result;if(!Actor)return Result;
 TArray<UStaticMeshComponent*> Parts;Actor->GetComponents<UStaticMeshComponent>(Parts);
 for(auto* Part:Parts)
 {
  if(!Part)continue;
  UStaticMesh* Mesh=Part->GetStaticMesh().Get();auto* Material=Part->GetMaterial(0);
  if(!MeshFilter.IsEmpty()&&(!Mesh||!Mesh->GetName().Contains(MeshFilter)))continue;
  auto* MID=Cast<UMaterialInstanceDynamic>(Material);
  bool bColorParameter=false,bTimeParameter=false;FLinearColor Color=FLinearColor::Transparent;float Timeline=-1.f;
  if(MID)
  {
   TArray<FMaterialParameterInfo> Infos;TArray<FGuid> Ids;
   MID->GetAllVectorParameterInfo(Infos,Ids);
   for(const auto& Info:Infos)if(Info.Name==TEXT("SourceColor")){bColorParameter=true;break;}
   Infos.Reset();Ids.Reset();MID->GetAllScalarParameterInfo(Infos,Ids);
   for(const auto& Info:Infos)if(Info.Name==TEXT("SourceTimeline")){bTimeParameter=true;break;}
   if(bColorParameter)Color=MID->K2_GetVectorParameterValue(TEXT("SourceColor"));
   if(bTimeParameter)Timeline=MID->K2_GetScalarParameterValue(TEXT("SourceTimeline"));
  }
  const FBoxSphereBounds Bounds=Part->Bounds;
  const bool bActive=Part->IsRegistered()&&Part->IsVisible()&&!Part->bHiddenInGame&&!Actor->IsHidden()&&Part->bRenderInMainPass&&Mesh&&Material;
  const bool bAbove=!Bounds.Origin.ContainsNaN()&&!Bounds.BoxExtent.ContainsNaN()&&Bounds.Origin.Z+Bounds.BoxExtent.Z>=BoardZ-2.;
  if(bActive){++Result.ActiveParts;Result.bActivePartsAboveBoard&=bAbove;}
  if(Test)
  {
   const FVector PlaneNormal=Part->GetComponentTransform().GetUnitAxis(EAxis::Z);
   const FVector ToCamera=(Camera-Bounds.Origin).GetSafeNormal();
   const FBoxSphereBounds LocalBounds=Mesh?Mesh->GetBounds():FBoxSphereBounds(ForceInit);
   Test->AddInfo(FString::Printf(TEXT("FX part=%s actor=%s actor_xf=%s component_xf=%s mesh=%s local_bounds_origin=%s local_bounds_extent=%s world_bounds_origin=%s world_bounds_extent=%s board_z=%.3f max_z=%.3f above=%d registered=%d visible=%d hidden_game=%d owner_hidden=%d main_pass=%d material=%s base_material=%s MID=%d color_present=%d SourceColor=%s timeline_present=%d SourceTimeline=%.4f plane_localZ_dot_camera=%.4f"),
    *GetNameSafe(Part),*GetNameSafe(Actor),*Actor->GetActorTransform().ToHumanReadableString(),*Part->GetComponentTransform().ToHumanReadableString(),
    *GetPathNameSafe(Mesh),*LocalBounds.Origin.ToString(),*LocalBounds.BoxExtent.ToString(),*Bounds.Origin.ToString(),*Bounds.BoxExtent.ToString(),
    BoardZ,Bounds.Origin.Z+Bounds.BoxExtent.Z,bAbove,Part->IsRegistered(),Part->IsVisible(),Part->bHiddenInGame,Actor->IsHidden(),Part->bRenderInMainPass,
    *GetPathNameSafe(Material),*GetPathNameSafe(Material?Material->GetBaseMaterial():nullptr),MID!=nullptr,bColorParameter,*Color.ToString(),bTimeParameter,Timeline,FVector::DotProduct(PlaneNormal,ToCamera)));
  }
 }
 return Result;
}

bool SakuraFXReady(UWorld* World,AEGChessUnit* Unit,UEGSourceEffectDefinition* Data,float MinimumAge,float MaximumAge,FAutomationTestBase* Test=nullptr)
{
 if(!World||!Unit||!Data)return false;
 const double BoardZ=SakuraBoardZ(World,Unit);
 const auto* Camera=UGameplayStatics::GetPlayerCameraManager(World,0);
 const FVector CameraLocation=Camera?Camera->GetCameraLocation():FVector::ZeroVector;
 bool bReady=false;
 for(TActorIterator<AEGSourceEffect> It(World);It;++It)
 {
  if(It->GetOwner()!=Unit||It->GetSourceDefinition()!=Data||It->IsActorBeingDestroyed())continue;
  const float Age=It->GetElapsedTime();const auto Audit=SakuraAuditParts(*It,BoardZ,CameraLocation,Test);
  if(Test)Test->AddInfo(FString::Printf(TEXT("FX actor=%s definition=%s world_xf=%s age=%.4f rate=%.3f active_parts=%d active_parts_above_board=%d"),
   *GetPathNameSafe(*It),*GetPathNameSafe(Data),*It->GetActorTransform().ToHumanReadableString(),Age,It->GetPlaybackRate(),Audit.ActiveParts,Audit.bActivePartsAboveBoard));
  if(Age>=MinimumAge&&Age<=MaximumAge&&Audit.ActiveParts>0)bReady=true;
  if(Test&&Age>=MinimumAge&&Age<=MaximumAge)
  {
   Test->TestTrue(TEXT("Required FX has a renderable static mesh component"),Audit.ActiveParts>0);
   if(Audit.ActiveParts>0)Test->TestTrue(TEXT("Every renderable FX mesh reaches the board surface"),Audit.bActivePartsAboveBoard);
  }
 }
 return bReady;
}
int32 SakuraAttackIndex(AEGChessUnit* Unit)
{
 auto* Skill=Unit?Unit->GetHeroSkill():nullptr;auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
 const auto* P=Unit&&Unit->GetDefinition()?Unit->GetDefinition()->PresentationDefinition.Get():nullptr;
 if(!Skill||Skill->IsCasting()||!Montage||!P)return INDEX_NONE;
 for(int32 I=0;I<P->AttackSequence.Num();++I)if(P->AttackSequence[I].Montage.Get()==Montage)return I;
 return INDEX_NONE;
}
bool SakuraBladeVisible(UWorld* World,AEGChessUnit* Unit)
{
 const int32 Index=SakuraAttackIndex(Unit);const float T=SakuraPosition(Unit);
 // 120 Hz sampled source mesh UV time windows from Saved/HuaSakuraFXRework20261030/blade-plan.json.
 const bool InWindow=Index==0?(T>=.08f&&T<=.18f):Index==1?((T>=.10f&&T<=.19f)||(T>=.29f&&T<=.39f)):
  Index==2?(T>=.25f&&T<=.38f):Index==3?((T>=.20f&&T<=.28f)||(T>=.32f&&T<=.39f)||(T>=.52f&&T<=.60f)):
  Index==4?((T>=.19f&&T<=.26f)||(T>=.49f&&T<=.60f)):false;
 if(!InWindow||!World)return false;
 for(TActorIterator<AEGCombatVisual> It(World);It;++It)if(It->GetOwner()==Unit&&!It->IsActorBeingDestroyed())
 {
  TArray<UStaticMeshComponent*> Parts;It->GetComponents<UStaticMeshComponent>(Parts);
  for(const auto* Part:Parts)if(Part&&Part->IsRegistered()&&Part->IsVisible()&&!Part->bHiddenInGame&&!It->IsHidden()&&Part->bRenderInMainPass&&Part->GetStaticMesh()&&
     Part->GetStaticMesh()->GetName().Contains(FString::Printf(TEXT("Sakura_Attack%d_BladeSweep"),Index)))return true;
 }
 return false;
}

class FSakuraColdBattle final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;bool bNatural=false,bStarted=false,bControlled=false,bLogged=false,bClosing=false;
 bool bCapturedBasic=false,bCapturedFan=false,bCapturedImpact=false,bCaptureFailed=false;
 double Deadline=0.,BattleStart=-1.;FString Directory,RunId;
 TWeakObjectPtr<AEGChessUnit> Sakura,Ordinary,Execution;
 TSharedPtr<FSakuraColdCapture> Capture;
 int32 CaptureKind=INDEX_NONE;

 bool BeginCapture(UWorld* World,int32 Kind,const TCHAR* Label)
 {
  auto* Client=World?World->GetGameViewport():nullptr;
  if(!Client||!Client->Viewport||!Client->GetWindow().IsValid()||!Client->GetGameViewportWidget().IsValid())return false;
  Capture=MakeShared<FSakuraColdCapture>();Capture->Client=Client;Capture->ArmedAt=FPlatformTime::Seconds();
  Capture->Path=FPaths::ConvertRelativePathToFull(Directory/(FString(Label)+TEXT("_")+RunId+TEXT("_")+FGuid::NewGuid().ToString(EGuidFormats::Digits)+TEXT(".png")));
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(Capture->Path),true);
  const TWeakPtr<FSakuraColdCapture> Weak=Capture;const TWeakObjectPtr<UWorld> WeakWorld(World);const auto Unit=Sakura;
  const auto NormalTarget=Ordinary;const auto ExecutionTarget=Execution;
  auto* CurrentTest=Test;
  Capture->Handle=Client->OnDrawn().AddLambda([Weak,WeakWorld,Unit,NormalTarget,ExecutionTarget,CurrentTest,Kind]()
  {
   auto State=Weak.Pin();auto* W=WeakWorld.Get();auto* U=Unit.Get();
   if(!State.IsValid()||!W||!U||State->bRequested||FScreenshotRequest::IsScreenshotRequested())return;
   auto* Skill=U->GetHeroSkill();auto* D=Cast<UEGSakuraSkillDefinition>(U->GetDefinition()->SkillDefinition);
   const int32 Fan=D?SakuraFXCount(W,U,D->AreaPresentation.Get()):0;
   const int32 Release=D?SakuraFXCount(W,U,D->SlashReleasePresentation.Get()):0;
   const int32 Normal=D?SakuraFXCount(W,U,D->NormalImpactPresentation.Get()):0;
   const int32 Execute=D?SakuraFXCount(W,U,D->ExecutionImpactPresentation.Get()):0;
   const float Position=SakuraPosition(U);
   const int32 AttackIndex=SakuraAttackIndex(U);const float FanAge=D?SakuraFXAge(W,U,D->AreaPresentation.Get()):-1.f;
   const float ReleaseAge=D?SakuraFXAge(W,U,D->SlashReleasePresentation.Get()):-1.f;
   const float NormalAge=D?SakuraFXAge(W,U,D->NormalImpactPresentation.Get()):-1.f;
   const float ExecuteAge=D?SakuraFXAge(W,U,D->ExecutionImpactPresentation.Get()):-1.f;
   const bool bFanReady=D&&SakuraFXReady(W,U,D->AreaPresentation.Get(),.12f,.25f);
   const bool bImpactReady=D&&SakuraFXReady(W,U,D->SlashReleasePresentation.Get(),.05f,.12f)&&
    SakuraFXReady(W,U,D->NormalImpactPresentation.Get(),.05f,.12f)&&SakuraFXReady(W,U,D->ExecutionImpactPresentation.Get(),.05f,.12f);
   State->bMatches=Kind==0?SakuraBladeVisible(W,U):
                   Kind==1?bFanReady:bImpactReady;
   // Wait for the real draw to reach the selected window. If the short effect
   // has already advanced past it, save one diagnostic frame and fail promptly.
   const bool bMissed=(Kind==1?FanAge>.25f:Kind==2&&FMath::Max3(ReleaseAge,NormalAge,ExecuteAge)>.12f)
    ||(Kind!=0&&FPlatformTime::Seconds()-State->ArmedAt>1.0);
   if(!State->bMatches&&!bMissed)return;
   const auto* Camera=UGameplayStatics::GetPlayerCameraManager(W,0);
   CurrentTest->TestNotNull(TEXT("Fixed board player camera"),Camera);
   const FVector CameraLocation=Camera?Camera->GetCameraLocation():FVector::ZeroVector;
   CurrentTest->AddInfo(FString::Printf(TEXT("Draw %d: montage=%.3f attack_index=%d casting=%d fan=%d fan_age=%.3f release=%d release_age=%.3f normal=%d normal_age=%.3f execution=%d execution_age=%.3f matches=%d path=%s unit_world_xf=%s normal_target_xf=%s execution_target_xf=%s board_z=%.3f camera=%s camera_rotation=%s"),
     Kind,Position,AttackIndex,Skill&&Skill->IsCasting(),Fan,FanAge,Release,ReleaseAge,Normal,NormalAge,Execute,ExecuteAge,State->bMatches,*State->Path,
     *U->GetActorTransform().ToHumanReadableString(),NormalTarget.IsValid()?*NormalTarget->GetActorTransform().ToHumanReadableString():TEXT("invalid"),
     ExecutionTarget.IsValid()?*ExecutionTarget->GetActorTransform().ToHumanReadableString():TEXT("invalid"),SakuraBoardZ(W,U),
     *CameraLocation.ToString(),Camera?*Camera->GetCameraRotation().ToString():TEXT("invalid")));
   if(Kind==0)
   {
    for(TActorIterator<AEGCombatVisual> It(W);It;++It)if(It->GetOwner()==U&&!It->IsActorBeingDestroyed())
    {
     const auto Audit=SakuraAuditParts(*It,SakuraBoardZ(W,U),CameraLocation,CurrentTest,FString::Printf(TEXT("Sakura_Attack%d_BladeSweep"),AttackIndex));
     if(Audit.ActiveParts>0)CurrentTest->TestTrue(TEXT("Visible blade sweep reaches board surface"),Audit.bActivePartsAboveBoard);
    }
   }
   else if(D&&Kind==1)SakuraFXReady(W,U,D->AreaPresentation.Get(),.12f,.25f,CurrentTest);
   else if(D&&Kind==2)
   {
    SakuraFXReady(W,U,D->SlashReleasePresentation.Get(),.05f,.12f,CurrentTest);
    SakuraFXReady(W,U,D->NormalImpactPresentation.Get(),.05f,.12f,CurrentTest);
    SakuraFXReady(W,U,D->ExecutionImpactPresentation.Get(),.05f,.12f,CurrentTest);
   }
   CurrentTest->TestTrue(TEXT("Actual draw matches the required effect time and renderable components"),State->bMatches);
   CurrentTest->AddInfo(TEXT("Screenshot requested on the checked viewport draw; the PIE world is not paused for capture."));
   State->bRequested=true;FScreenshotRequest::RequestScreenshot(State->Path,true,false,false,FIntRect(),true);
  });
  CaptureKind=Kind;return true;
 }
 bool CompleteCapture()
 {
  if(!Capture.IsValid()||!Capture->bRequested||FScreenshotRequest::IsScreenshotRequested())return false;
  TArray<uint8> Bytes;if(!FFileHelper::LoadFileToArray(Bytes,*Capture->Path)||Bytes.Num()<36)return false;
  const int32 End=Bytes.Num()-12;if(Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
  const auto BE=[&](int32 Offset){return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
  Test->TestEqual(TEXT("PIE screenshot is 1920x1080"),FIntPoint(int32(BE(16)),int32(BE(20))),FIntPoint(1920,1080));
  if(Capture->bMatches){if(CaptureKind==0)bCapturedBasic=true;else if(CaptureKind==1)bCapturedFan=true;else if(CaptureKind==2)bCapturedImpact=true;}
  else bCaptureFailed=true;
  Capture.Reset();CaptureKind=INDEX_NONE;return true;
 }
public:
 FSakuraColdBattle(FAutomationTestBase* InTest,bool bInNatural):Test(InTest),bNatural(bInNatural),RunId(FGuid::NewGuid().ToString(EGuidFormats::Digits))
 {Directory=FPaths::ProjectSavedDir()/TEXT("SakuraColdIai20261107/Graphics")/(bNatural?TEXT("Natural1080"):TEXT("Controlled1080"));}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Sakura graphical fixture timed out"));return true;}
  UWorld* W=GEditor?GEditor->PlayWorld:nullptr;auto* Mode=W?W->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetCards()||!Mode->GetMatch()||!Mode->BattleRules)return false;
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Cards=Mode->GetCards();FGuid SakuraCard;
   for(const auto& C:Cards->GetHand())if(C.Definition&&C.Definition->HeroDefinition&&C.Definition->HeroDefinition->HeroKind==EEGHeroKind::Sakura)SakuraCard=C.InstanceId;
   if(!SakuraCard.IsValid())
   {
    auto* Card=LoadObject<UEGCardDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/Sakura/Data/DA_Card_Sakura.DA_Card_Sakura"));
    if(!Test->TestNotNull(TEXT("Formal Sakura card"),Card)||!Test->TestTrue(TEXT("Grant Sakura card"),Cards->GrantCard(Card)))return true;
    SakuraCard=Cards->GetHand().Last().InstanceId;
   }
   if(!Test->TestTrue(TEXT("Deploy Sakura on formal board"),Cards->TryDeployHandCard(SakuraCard,{3,1})))return true;
   // Guarantee at least two opponents before combat. Existing stage opponents remain intact.
   if(!bNatural&&Mode->RunDefinition&&!Mode->RunDefinition->Stages.IsEmpty()&&!Mode->RunDefinition->Stages[0].Enemies.IsEmpty())
   {
    auto* Board=Mode->GetBoard();auto* Definition=Mode->RunDefinition->Stages[0].Enemies[0].Definition.Get();
    auto Count=[Mode](){int32 N=0;for(auto* U:Mode->GetCombatUnits())if(U&&U->GetFaction_Implementation()==EEGFaction::Enemy)++N;return N;};
    if(Board&&Definition)for(int32 Y=0;Y<Board->Dimensions.Y&&Count()<3;++Y)
     for(int32 X=0;X<Board->Dimensions.X&&Count()<3;++X)
      if(Board->IsDeploymentCell({X,Y},EEGFaction::Enemy)&&!Board->GetOccupant({X,Y}))Mode->SpawnEnemy(Definition,1,{X,Y});
   }
   if(!Test->TestTrue(TEXT("Enter formal board combat"),Mode->EnterCombat()))return true;
   TArray<AEGChessUnit*> Enemies;
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetDefinition())
   {
    if(U->GetFaction_Implementation()==EEGFaction::Enemy)Enemies.Add(U);
    else if(U->GetDefinition()->HeroKind==EEGHeroKind::Sakura)Sakura=U;
   }
   if(!Test->TestTrue(TEXT("Sakura and suitable enemies spawned"),Sakura.IsValid()&&Enemies.Num()>=(bNatural?1:2)))return true;
   if(!bNatural){Ordinary=Enemies[0];Execution=Enemies[1];}
   Test->AddInfo(bNatural?TEXT("Natural AI: grant/deploy Sakura through CardManager; stage enemies, health, mana, damage and timing are unchanged. Screenshot is not a visual-acceptance verdict."):
    TEXT("Controlled AI: grant/deploy Sakura, add enemies if needed, set units to high health, disable enemy combat ticks, reposition two targets and fill Sakura mana once; montage and notifications advance naturally. Not natural balance or real input."));
   bStarted=true;return false;
  }
  if(Capture.IsValid()&&!CompleteCapture())return false;
  if(bCaptureFailed)return true;
  if(bClosing)return true;
  if(!Sakura.IsValid()){Test->AddError(TEXT("Sakura disappeared before capture"));return true;}
  if(!Mode->BattleRules->IsCombatRunning())
  {if(BattleStart>=0.){Test->AddError(TEXT("Battle ended before required capture"));return true;}return false;}
  if(BattleStart<0.)BattleStart=W->GetTimeSeconds();
  const double Elapsed=W->GetTimeSeconds()-BattleStart;
  auto* Skill=Sakura->GetHeroSkill();auto* D=Cast<UEGSakuraSkillDefinition>(Sakura->GetDefinition()->SkillDefinition);
  if(!Test->TestNotNull(TEXT("Formal Sakura skill configuration"),D))return true;
  if(!bNatural&&!bControlled)
  {
   const FVector Origin=Sakura->GetActorLocation();Sakura->SetActorRotation(FRotator::ZeroRotator);
   for(auto* U:Mode->GetCombatUnits())if(U&&U->GetAbilitySystemComponent())
   {
    auto* ASC=U->GetAbilitySystemComponent();ASC->SetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute(),100000.f);
    ASC->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f);
    if(U->GetFaction_Implementation()==EEGFaction::Enemy){U->GetCombat()->SetComponentTickEnabled(false);U->SetActorLocation(Origin+FVector(4000,0,0));}
   }
   Ordinary->SetActorLocation(Origin+FVector(120,0,0));Execution->SetActorLocation(Origin+FVector(220,0,0));
   Execution->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),100000.f*D->ExecuteHealthFraction);
   Sakura->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Sakura->GetAttributes()->GetMaxMana());
   bControlled=true;Test->AddInfo(TEXT("Controlled targets: ordinary 100000/100000, execute exactly at configured threshold; mana filled at combat start."));
  }
  auto* Montage=Skill?Skill->GetActiveMontage():nullptr;
  if(!bNatural&&!bLogged&&Skill&&Skill->IsCasting()&&Montage)
  {
   for(const auto& N:Montage->Notifies)if(const auto* Notify=Cast<UEGSkillNotify>(N.Notify);Notify&&(Notify->Event==TEXT("FanStart")||Notify->Event==TEXT("Slash")))
    Test->AddInfo(FString::Printf(TEXT("Montage notify %s at source %.3fs"),*Notify->Event.ToString(),N.GetTime()));
   bLogged=true;
  }
  if(bNatural&&!bCapturedBasic&&SakuraBladeVisible(W,Sakura.Get()))
  {BeginCapture(W,0,TEXT("natural_basic"));return false;}
  const float FanAge=SakuraFXAge(W,Sakura.Get(),D->AreaPresentation.Get());
  if(!bNatural&&!bCapturedFan&&FanAge>=.10f&&FanAge<=.25f)
  {BeginCapture(W,1,TEXT("ultimate_fan"));return false;}
  const float ReleaseAge=SakuraFXAge(W,Sakura.Get(),D->SlashReleasePresentation.Get());
  const float NormalAge=SakuraFXAge(W,Sakura.Get(),D->NormalImpactPresentation.Get());
  const float ExecuteAge=SakuraFXAge(W,Sakura.Get(),D->ExecutionImpactPresentation.Get());
  if(!bNatural&&!bCapturedImpact&&ReleaseAge>=.03f&&ReleaseAge<=.12f&&
     NormalAge>=.03f&&NormalAge<=.12f&&ExecuteAge>=.03f&&ExecuteAge<=.12f)
  {BeginCapture(W,2,TEXT("ultimate_normal_and_execution"));return false;}
  if((bNatural&&bCapturedBasic)||(!bNatural&&bCapturedFan&&bCapturedImpact))
  {
   Test->AddInfo(FString::Printf(TEXT("Required frames observed at %.3fs combat time; normal and execution targets %s/%s"),Elapsed,*GetNameSafe(Ordinary.Get()),*GetNameSafe(Execution.Get())));
   bClosing=true;return false;
  }
  if(Elapsed>40.){Test->AddError(FString::Printf(TEXT("Required capture absent: basic=%d fan=%d impact=%d"),bCapturedBasic,bCapturedFan,bCapturedImpact));return true;}
  return false;
 }
};

bool QueueSakuraCold(FAutomationTestBase* Test,bool bNatural)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Sakura graphical test requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Sakura graphical test requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FSakuraColdStartPIE());
 ADD_LATENT_AUTOMATION_COMMAND(FSakuraColdBattle(Test,bNatural));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraColdNatural1080,"ElyGambit.Integration.SakuraColdNatural1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraColdNatural1080::RunTest(const FString&){return QueueSakuraCold(this,true);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSakuraColdControlled1080,"ElyGambit.Integration.SakuraColdControlled1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSakuraColdControlled1080::RunTest(const FString&){return QueueSakuraCold(this,false);}
#endif
