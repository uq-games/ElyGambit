#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/AutomationEditorCommon.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformStackWalk.h"
#include "UnrealClient.h"
#include "Engine/GameViewportClient.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "PlayInEditorDataTypes.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "TextureCompiler.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Game/EGDemoGameMode.h"
#include "Game/EGCardManager.h"
#include "Game/EGHeroRoster.h"
#include "Game/EGMatchManager.h"
#include "Data/EGCardDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGRunDefinition.h"
#include "Data/EGStigmataDefinition.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGCombatComponent.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"
#include "Data/EGMonsterAttackDefinition.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/PanelWidget.h"
#include "Components/WidgetComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Placement/EGBoardScene.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGRichText.h"
#include "UI/Widget/EGWidgets.h"
#include "UI/HUD/EGHUD.h"

namespace
{
bool ContainsDrawnImage(const UWidget* Widget)
{
 if(!Widget)return false;
 if(const auto* Image=Cast<UImage>(Widget))return Image->GetBrush().DrawAs!=ESlateBrushDrawType::NoDrawType;
 if(const auto* Panel=Cast<UPanelWidget>(Widget))
  for(int32 Index=0;Index<Panel->GetChildrenCount();++Index)
   if(ContainsDrawnImage(Panel->GetChildAt(Index)))return true;
 return false;
}

struct FEGPIECheckState
{
 TWeakObjectPtr<AEGChessUnit> PreviousUnit;
};

enum class EStigmataRoute { Five, Nine, Herrscher };

FIntPoint RouteViewportSize(EStigmataRoute Route)
{
 switch(Route)
 {
 case EStigmataRoute::Nine: return FIntPoint(1600,900);
 case EStigmataRoute::Herrscher: return FIntPoint(2560,1080);
 default: return FIntPoint(1920,1080);
 }
}

void CheckStatBlock(FAutomationTestBase* Test,const TCHAR* Label,const FEGStatBlock& Actual,const FEGStatBlock& Expected)
{
 const FString Prefix(Label);
 Test->TestTrue(*(Prefix+TEXT(" max health")),FMath::IsNearlyEqual(Actual.MaxHealth,Expected.MaxHealth,.01f));
 Test->TestTrue(*(Prefix+TEXT(" max mana")),FMath::IsNearlyEqual(Actual.MaxMana,Expected.MaxMana,.01f));
 Test->TestTrue(*(Prefix+TEXT(" attack")),FMath::IsNearlyEqual(Actual.AttackPower,Expected.AttackPower,.01f));
 Test->TestTrue(*(Prefix+TEXT(" attack speed")),FMath::IsNearlyEqual(Actual.AttackSpeed,Expected.AttackSpeed,.01f));
 Test->TestTrue(*(Prefix+TEXT(" defense")),FMath::IsNearlyEqual(Actual.Defense,Expected.Defense,.01f));
}

bool ChooseReward(FAutomationTestBase* Test,AEGDemoGameMode* Mode,int32 Stage,EStigmataRoute Route,bool bUseWidget)
{
 auto* Cards=Mode->GetCards();
 auto* Match=Mode->GetMatch();
 const TArray<FName> Offer=Stage==6?Mode->GetEndgameRouteOffer():Mode->GetStigmataOffer();
 if(!Test->TestTrue(TEXT("Reward occurs after stage 2, 4 or 6"),Stage==2||Stage==4||Stage==6))return false;
 if(!Test->TestEqual(TEXT("Three distinct stigmata choices"),Offer.Num(),3))return false;
 TSet<FName> Unique;
 for(FName Id:Offer)
 {
  Unique.Add(Id);
  Test->TestFalse(TEXT("Previously owned stigmata is not offered"),Match->HasStigmata(Id));
  if(Stage!=6)Test->TestFalse(TEXT("Ordinary offer excludes final routes"),Id==TEXT("OriginFlower")||Id==TEXT("Story")||Id==TEXT("Unity"));
  Test->TestNotNull(TEXT("Ordinary offer resolves in the catalog"),UEGStigmataCatalog::Find(Id));
 }
 if(!Test->TestEqual(TEXT("Candidates are distinct"),Unique.Num(),3))return false;
 FName Choice=Offer[0];
 if(Stage!=6)for(const FName Id:Offer)if(Id!=TEXT("Reverie")&&Id!=TEXT("Stars")&&Id!=TEXT("Vicissitude")){Choice=Id;break;}
 const int32 Revision=Mode->GetStigmataRevision();
 const int32 OwnedBefore=Match->GetStigmataOwned().Num();
 const int32 HandBefore=Cards->GetHand().Num();
 const int64 GoldBefore=Cards->GetGold();
 const TArray<FEGCardInstance> PreviousHand=Cards->GetHand();
 TSet<FGuid> PreviousCardIds;
 for(const auto& Card:PreviousHand)PreviousCardIds.Add(Card.InstanceId);
 TSet<FGuid> Participants;
 int32 ParticipantLevels=0;
 for(const auto& Hero:Mode->GetRoster()->GetHeroes())
  if(Hero.bDeployed){Participants.Add(Hero.InstanceId);ParticipantLevels+=Hero.Level;}
 UEGCardDefinition* HumanSourceCard=nullptr;
 UEGHeroDefinition* HumanSourceHero=nullptr;
 UEGHeroStatsDefinition* HumanSourceStats=nullptr;
 FEGStatBlock HumanSourceGrowth;
 if(Stage==6&&Route==EStigmataRoute::Herrscher)
 {
  HumanSourceCard=Mode->HumanFusionCard.LoadSynchronous();
  if(!Test->TestNotNull(TEXT("Source human card exists"),HumanSourceCard)||
     !Test->TestNotNull(TEXT("Source human definition exists"),HumanSourceCard->HeroDefinition.Get()))return false;
  HumanSourceHero=HumanSourceCard->HeroDefinition.Get();
  HumanSourceStats=HumanSourceHero->StatsDefinition.Get();
  if(HumanSourceHero->bUseSplitConfiguration&&!Test->TestNotNull(TEXT("Source human stats exist"),HumanSourceStats))return false;
  HumanSourceGrowth=HumanSourceHero->GetGrowthPerLevel();
  CheckStatBlock(Test,TEXT("Source human growth before fusion"),HumanSourceGrowth,UEGHeroDefinition::MakeZeroGrowth());
 }
 if(Stage!=6)
 {
  Test->TestFalse(TEXT("Stale revision rejected"),Mode->ChooseStigmata(Choice,Revision-1));
  Test->TestFalse(TEXT("Non-candidate rejected"),Mode->ChooseStigmata(FName(TEXT("InvalidStigmataForTest")),Revision));
 }
 Test->TestEqual(TEXT("Rejected choice preserves hand"),Cards->GetHand().Num(),HandBefore);
 for(const auto& Card:Cards->GetHand())
  Test->TestTrue(TEXT("Rejected choice preserves card identity"),PreviousCardIds.Contains(Card.InstanceId));
 Test->TestEqual(TEXT("Rejected choice preserves gold"),Cards->GetGold(),GoldBefore);
 Test->TestEqual(TEXT("Rejected choice preserves owned count"),Match->GetStigmataOwned().Num(),OwnedBefore);
 Test->TestEqual(TEXT("Rejected choice preserves revision"),Mode->GetStigmataRevision(),Revision);
 if(bUseWidget)
 {
  auto* Panel=Mode->GetDemoPanel();
  if(!Test->TestNotNull(TEXT("Stigmata panel exists"),Panel))return false;
  const int32 ChoiceIndex=Offer.IndexOfByKey(Choice);
  const FName ButtonName(*FString::Printf(TEXT("Button_StigmataChoice%d"),ChoiceIndex));
  auto* Candidate=Cast<UButton>(Panel->GetWidgetFromName(ButtonName));
  if(!Test->TestNotNull(TEXT("Candidate button exists"),Candidate))return false;
  if(!Test->TestTrue(TEXT("Candidate is visible"),Candidate->IsVisible()))return false;
  Candidate->OnClicked.Broadcast();
  Test->TestEqual(TEXT("Programmatic short click grants nothing"),Match->GetStigmataOwned().Num(),OwnedBefore);
 }
 // The campaign fixture is synchronous; a delegate broadcast is not a held Slate
 // input. Keep transaction coverage here and exercise the timed UI separately.
 if(Stage!=6)
 {
  if(!Test->TestTrue(TEXT("Programmatic production choice commits"),Mode->ChooseStigmata(Choice,Revision)))return false;
  Test->TestTrue(TEXT("Chosen stigmata is owned"),Match->HasStigmata(Choice));
  Test->TestEqual(TEXT("One stigmata acquired"),Match->GetStigmataOwned().Num(),OwnedBefore+1);
  Test->TestFalse(TEXT("Choice revision cannot be reused"),Mode->ChooseStigmata(Choice,Revision));
  Test->TestEqual(TEXT("Ordinary choice grants no cards"),Cards->GetHand().Num(),HandBefore);
  for(const auto& Card:Cards->GetHand())Test->TestTrue(TEXT("Ordinary choice preserves card identity"),PreviousCardIds.Contains(Card.InstanceId));
  Test->TestEqual(TEXT("Only Reverie grants immediate gold"),Cards->GetGold(),GoldBefore+(Choice==TEXT("Reverie")?6:0));
 }
 const int64 GoldAfterOrdinary=Cards->GetGold();
 const TArray<FEGHeroInstance> RosterAfterOrdinary=Mode->GetRoster()->GetHeroes();
 if(Stage==6)
 {
  if(!Test->TestTrue(TEXT("Stage six directly opens final route selection"),Mode->IsChoosingEndgameRoute()))return false;
  Test->TestTrue(TEXT("Stage six has no third ordinary offer"),Mode->GetStigmataOffer().IsEmpty());
  const auto Routes=Mode->GetEndgameRouteOffer();
  if(!Test->TestTrue(TEXT("All three final routes are offered"),Routes.Num()==3&&Routes.Contains(TEXT("OriginFlower"))&&Routes.Contains(TEXT("Story"))&&Routes.Contains(TEXT("Unity"))))return false;
  const FName FinalRoute=Route==EStigmataRoute::Nine?FName(TEXT("Story")):Route==EStigmataRoute::Herrscher?FName(TEXT("OriginFlower")):FName(TEXT("Unity"));
  const int32 RouteRevision=Mode->GetStigmataRevision();
  Test->TestFalse(TEXT("Stale revision cannot commit a route"),Mode->ChooseEndgameRoute(FinalRoute,RouteRevision-1));
  Test->TestFalse(TEXT("Invalid route is rejected"),Mode->ChooseEndgameRoute(TEXT("InvalidRouteForTest"),RouteRevision));
  Test->TestEqual(TEXT("Rejected route preserves hand"),Cards->GetHand().Num(),HandBefore);
  Test->TestEqual(TEXT("Rejected route preserves gold"),Cards->GetGold(),GoldAfterOrdinary);
  Test->TestEqual(TEXT("Rejected route preserves ordinary ownership"),Match->GetStigmataOwned().Num(),OwnedBefore);
  if(!Test->TestTrue(TEXT("Final route transaction commits"),Mode->ChooseEndgameRoute(FinalRoute,RouteRevision)))return false;
  Test->TestEqual(TEXT("Selected route persists separately"),Match->GetEndgameRoute(),FinalRoute);
  Test->TestFalse(TEXT("Route does not occupy an ordinary stigmata slot"),Match->HasStigmata(FinalRoute));
  Test->TestFalse(TEXT("Route revision cannot be reused"),Mode->ChooseEndgameRoute(FinalRoute,RouteRevision));
  Test->TestEqual(TEXT("Route keeps both ordinary signets"),Match->GetStigmataOwned().Num(),OwnedBefore);
 }
 if(Stage==6&&Route==EStigmataRoute::Nine)
 {
  Test->TestEqual(TEXT("Story grants nine cards"),Cards->GetHand().Num(),HandBefore+9);
  Test->TestEqual(TEXT("Story raises population to nine"),Cards->GetRules().DeploymentLimit,9);
  TSet<EEGHeroKind> GrantedKinds;
  for(const auto& Card:Cards->GetHand())
   if(!PreviousCardIds.Contains(Card.InstanceId)&&Card.Definition&&Card.Definition->HeroDefinition)
   {
    GrantedKinds.Add(Card.Definition->HeroDefinition->HeroKind);
    Test->TestTrue(TEXT("Story gifted card has minimum level six"),Card.InitialLevel>=6);
    Test->TestEqual(TEXT("Story gifted card has no paid value"),Card.PaidCost,int64(0));
   }
  Test->TestEqual(TEXT("Story grants nine distinct base hero identities"),GrantedKinds.Num(),9);
  const TSet<EEGHeroKind> ExpectedKinds{
   EEGHeroKind::Elysia,EEGHeroKind::Pardofelis,EEGHeroKind::Sakura,
   EEGHeroKind::Hua,EEGHeroKind::Griseo,EEGHeroKind::Aponia,
   EEGHeroKind::VillV,EEGHeroKind::Mobius,EEGHeroKind::Eden};
  for(EEGHeroKind Kind:ExpectedKinds)Test->TestTrue(TEXT("Story grants required base identity"),GrantedKinds.Contains(Kind));
  for(const auto& Card:Cards->GetHand())Test->TestTrue(TEXT("Story raises every held card to at least six"),Card.InitialLevel>=6);
  for(const auto& Hero:Mode->GetRoster()->GetHeroes())Test->TestTrue(TEXT("Story raises every owned hero to at least six"),Hero.Level>=6);
 }
 if(Stage==6&&Route==EStigmataRoute::Five)
 {
  Test->TestEqual(TEXT("Unity preserves population limit"),Cards->GetRules().DeploymentLimit,5);
  Test->TestEqual(TEXT("Unity preserves gold"),Cards->GetGold(),GoldAfterOrdinary);
  Test->TestEqual(TEXT("Unity preserves hand count"),Cards->GetHand().Num(),HandBefore);
  Test->TestEqual(TEXT("Unity preserves roster count"),Mode->GetRoster()->GetHeroes().Num(),RosterAfterOrdinary.Num());
  for(const auto& Before:RosterAfterOrdinary)
  {
   FEGHeroInstance After;
   if(Test->TestTrue(TEXT("Unity preserves roster identity"),Mode->GetRoster()->GetHero(Before.InstanceId,After)))
   {
    Test->TestEqual(TEXT("Unity preserves permanent level"),After.Level,Before.Level);
    Test->TestEqual(TEXT("Unity preserves paid cost"),After.PaidCost,Before.PaidCost);
    Test->TestEqual(TEXT("Unity preserves deployment state"),After.bDeployed,Before.bDeployed);
    if(Before.bDeployed)Test->TestEqual(TEXT("Unity preserves deployment cell"),After.DeploymentCell,Before.DeploymentCell);
   }
  }
 }
 if(Stage==6&&Route==EStigmataRoute::Herrscher)
 {
  Test->TestTrue(TEXT("Fusion had deployed participants"),Participants.Num()>0);
  Test->TestEqual(TEXT("Fusion grants no gold"),Cards->GetGold(),GoldAfterOrdinary);
  Test->TestEqual(TEXT("Fusion grants one hand card"),Cards->GetHand().Num(),HandBefore+1);
  for(const auto& Hero:Mode->GetRoster()->GetHeroes())
   Test->TestFalse(TEXT("Fusion removes participant identity"),Participants.Contains(Hero.InstanceId));
  Test->TestTrue(TEXT("Fusion removes all deployed pieces"),Cards->GetPieces().IsEmpty());
  const FEGCardInstance* HumanCard=nullptr;
  const TArray<FEGCardInstance> FusionHand=Cards->GetHand();
  for(const auto& Card:FusionHand)
  {
   if(PreviousCardIds.Contains(Card.InstanceId))continue;
   if(!Test->TestNull(TEXT("Fusion grants exactly one new card"),HumanCard))return false;
   HumanCard=&Card;
  }
  if(!Test->TestNotNull(TEXT("Fusion grants a human card"),HumanCard)||
     !Test->TestNotNull(TEXT("Fusion card has definition"),HumanCard->Definition.Get())||
     !Test->TestNotNull(TEXT("Fusion card has hero definition"),HumanCard->Definition->HeroDefinition.Get()))return false;
  const UEGHeroDefinition* HumanHero=HumanCard->Definition->HeroDefinition.Get();
  Test->TestEqual(TEXT("Fusion card is Herrscher"),HumanHero->HeroKind,EEGHeroKind::ElysiaHerrscher);
  Test->TestEqual(TEXT("Fusion card uses twice permanent levels"),HumanCard->InitialLevel,ParticipantLevels*2);
  Test->TestEqual(TEXT("Herrscher occupies one cell"),HumanHero->GetPlacementFootprint(),FIntPoint(1,1));
  Test->TestTrue(TEXT("Fusion uses a run-private human definition"),HumanHero!=HumanSourceHero&&HumanHero->IsIn(Mode));
  Test->TestTrue(TEXT("Private card belongs to run"),HumanCard->Definition.Get()!=HumanSourceCard&&HumanCard->Definition->GetOuter()==Mode);
  if(HumanSourceHero->bUseSplitConfiguration)
   Test->TestTrue(TEXT("Private stats belong to private definition"),HumanHero->StatsDefinition&&
    HumanHero->StatsDefinition.Get()!=HumanSourceStats&&HumanHero->StatsDefinition->GetOuter()==HumanHero);
  Test->TestTrue(TEXT("Source card retains original definition"),HumanSourceCard->HeroDefinition.Get()==HumanSourceHero);
  Test->TestTrue(TEXT("Source definition retains original stats"),HumanSourceHero->StatsDefinition.Get()==HumanSourceStats);
  CheckStatBlock(Test,TEXT("Source human growth after fusion"),HumanSourceHero->GetGrowthPerLevel(),HumanSourceGrowth);
  CheckStatBlock(Test,TEXT("Private human keeps source base"),HumanHero->GetBaseStats(),HumanSourceHero->GetBaseStats());
  FEGStatBlock ExpectedGrowth=HumanSourceGrowth;
  const auto& Reference=Mode->RunDefinition->StartingCard->HeroDefinition->GetGrowthPerLevel();
  ExpectedGrowth.MaxHealth=Reference.MaxHealth;ExpectedGrowth.AttackPower=Reference.AttackPower;ExpectedGrowth.Defense=Reference.Defense;
  CheckStatBlock(Test,TEXT("Private human uses ordinary Elysia growth"),HumanHero->GetGrowthPerLevel(),ExpectedGrowth);
  const FEGStatBlock Evaluated=HumanHero->EvaluateStats(HumanCard->InitialLevel);
  const FEGStatBlock LevelOne=HumanHero->EvaluateStats(1);
  Test->TestTrue(TEXT("Human level raises maximum health"),Evaluated.MaxHealth>LevelOne.MaxHealth);
  Test->TestTrue(TEXT("Human level raises attack"),Evaluated.AttackPower>LevelOne.AttackPower);
  Test->TestTrue(TEXT("Human evaluated health follows reference growth"),FMath::IsNearlyEqual(Evaluated.MaxHealth,LevelOne.MaxHealth+(HumanCard->InitialLevel-1)*Reference.MaxHealth,.01f));
  Test->TestTrue(TEXT("Human evaluated attack follows reference growth"),FMath::IsNearlyEqual(Evaluated.AttackPower,LevelOne.AttackPower+(HumanCard->InitialLevel-1)*Reference.AttackPower,.01f));
 }

 return true;
}

EEGHeroKind BaseKind(EEGHeroKind Kind)
{
 switch(Kind)
 {
 case EEGHeroKind::CollapsedHua:return EEGHeroKind::Hua;
 case EEGHeroKind::GriseoAdult:return EEGHeroKind::Griseo;
 case EEGHeroKind::ElysiaHerrscher:return EEGHeroKind::Elysia;
 default:return Kind;
 }
}

void VerifyCopies(FAutomationTestBase* Test,AEGDemoGameMode* Mode,int32 Stage)
{
 if(Stage!=3&&Stage!=6)return;
 struct FPlayerCandidate {AEGChessUnit* Unit; FEGHeroInstance Hero;};
 TArray<FPlayerCandidate> Players;
 TArray<AEGChessUnit*> Copies;
 int32 OrdinaryEnemies=0;
 for(AEGChessUnit* Unit:Mode->GetCombatUnits())
 {
  if(!Unit||!Unit->GetDefinition())continue;
  FEGHeroInstance Hero;
  if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Mode->GetRoster()->GetHero(Unit->GetInstanceId(),Hero)&&Hero.bDeployed)
   Players.Add({Unit,Hero});
  else if(Unit->GetFaction_Implementation()==EEGFaction::Enemy&&!Unit->GetHeroSkill()->bSummon&&!Unit->IsAuxiliaryEmplacement())
  {
   if(Unit->GetDefinition()->MonsterAttack)++OrdinaryEnemies;
   else Copies.Add(Unit);
  }
 }
 const int32 Expected=FMath::Min(Stage==3?1:2,Players.Num());
 Test->TestEqual(TEXT("Corrupted copy count follows deployed units"),Copies.Num(),Expected);
 if(Stage==6&&Players.Num()==1)
  Test->TestTrue(TEXT("Missing sixth-stage copy is replaced by an ordinary enemy"),OrdinaryEnemies>=(Mode->GetMatch()->GetCorruption()<40?2:3));
 TSet<FGuid> PickedPlayers;
 const int32 Corruption=Mode->GetMatch()->GetCorruption();
 const int32 Bonus=Corruption<40?0:Corruption<=70?1:2;
 for(AEGChessUnit* Copy:Copies)
 {
  const FPlayerCandidate* Candidate=Players.FindByPredicate([&](const FPlayerCandidate& Entry)
  {return Entry.Hero.Definition&&BaseKind(Entry.Hero.Definition->HeroKind)==Copy->GetDefinition()->HeroKind&&!PickedPlayers.Contains(Entry.Hero.InstanceId);});
  if(!Test->TestNotNull(TEXT("Copy maps to a distinct deployed base hero"),Candidate))continue;
  PickedPlayers.Add(Candidate->Hero.InstanceId);
  Test->TestEqual(TEXT("Copy level uses corruption bonus and cap"),Copy->GetHeroSkill()->GetLevel(),FMath::Clamp(Candidate->Hero.Level+Bonus,1,11));
  for(const auto& Player:Players)
  {
   Test->TestNotEqual(TEXT("Copy GUID is independent"),Copy->GetInstanceId(),Player.Unit->GetInstanceId());
   Test->TestTrue(TEXT("Copy ASC is independent"),Copy->GetAbilitySystemComponent()!=Player.Unit->GetAbilitySystemComponent());
  }
 }
}
void RequestGameScreenshot(UWorld* World,const TCHAR* Name,FAutomationTestBase* Test)
{
 if(!FApp::CanEverRender())
 {
  Test->AddInfo(TEXT("Screenshot skipped because rendering is unavailable."));
  return;
 }
 const FString Filename=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/FString::Printf(TEXT("%s.png"),Name));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
 const auto* Viewport=World&&World->GetGameViewport()?World->GetGameViewport()->Viewport:nullptr;
 const FIntPoint Size=Viewport?Viewport->GetSizeXY():FIntPoint::ZeroValue;
 FScreenshotRequest::RequestScreenshot(Filename,true,false,false,FIntRect(),true);
 Test->AddInfo(FString::Printf(TEXT("Requested game screenshot with Slate/UMG: %s; game viewport %dx%d. Inspect the saved image separately."),*Filename,Size.X,Size.Y));
}
struct FPreparationDrawCapture
{
 TWeakObjectPtr<UGameViewportClient> Client;
 FDelegateHandle DrawHandle;
 int32 Draws=0;
 bool bRequested=false;
 bool bProgressMissed=false;
 bool bCelebrationMissed=false;
 int32 RequestPercent=INDEX_NONE,ObservedPercent=INDEX_NONE;
 FString Name;
 FString Path;
 ~FPreparationDrawCapture()
 {
  if(Client.IsValid()&&DrawHandle.IsValid())Client->OnDrawn().Remove(DrawHandle);
 }
};

bool HasExpectedPngDimensions(const FString& Path,FIntPoint Expected,FAutomationTestBase* Test)
{
 TArray<uint8> Bytes;
 if(!FFileHelper::LoadFileToArray(Bytes,*Path)||Bytes.Num()<36)return false;
 const int32 End=Bytes.Num()-12;
 if(Bytes[End]!=0||Bytes[End+1]!=0||Bytes[End+2]!=0||Bytes[End+3]!=0||
    Bytes[End+4]!='I'||Bytes[End+5]!='E'||Bytes[End+6]!='N'||Bytes[End+7]!='D')return false;
 const uint8 Signature[]={137,80,78,71,13,10,26,10};
 for(int32 Index=0;Index<8;++Index)if(Bytes[Index]!=Signature[Index])
 {Test->AddError(TEXT("Preparation screenshot is not a PNG"));return true;}
 auto ReadBE=[&Bytes](int32 Offset)->uint32
 {return (uint32(Bytes[Offset])<<24)|(uint32(Bytes[Offset+1])<<16)|(uint32(Bytes[Offset+2])<<8)|uint32(Bytes[Offset+3]);};
 const FIntPoint Actual(static_cast<int32>(ReadBE(16)),static_cast<int32>(ReadBE(20)));
 Test->TestEqual(TEXT("Preparation PNG matches actual game viewport dimensions"),Actual,Expected);
 return true;
}

// A portrait may draw directly from the original texture or through a UI
// material instance. Resolve the actual bound original in either case; absent
// or incorrect parameters must fail the same identity and aspect checks.
UTexture2D* StigmataBrushTexture(const UImage* Image,FName TextureParameter=FName(TEXT("PortraitTexture")))
{
 if(!Image)return nullptr;
 UObject* Resource=Image->GetBrush().GetResourceObject();
 if(auto* Texture=Cast<UTexture2D>(Resource))return Texture;
 if(auto* Material=Cast<UMaterialInterface>(Resource))
 {
  UTexture* ParameterTexture=nullptr;
  if(Material->GetTextureParameterValue(FHashedMaterialParameterInfo(TextureParameter),ParameterTexture))
   return Cast<UTexture2D>(ParameterTexture);
 }
 return nullptr;
}

FString StigmataPortraitPath(const FString& Hero)
{
 if(Hero==TEXT("Human"))return TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_Portrait_HumanOfficial.T_StigmaC_Portrait_HumanOfficial");
 const FString Name=Hero==TEXT("VillV")?FString(TEXT("T_StigmaC_Portrait_VillVCompleteHat")):FString::Printf(TEXT("T_UI_Portrait_%s"),*Hero);
 return FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/%s.%s"),*Name,*Name);
}

TArray<FString> ExpectedStigmataHeroes(FName Id)
{
 if(Id==TEXT("OriginFlower"))return {TEXT("Human")};
 if(Id==TEXT("LoveBeginning")||Id==TEXT("Flawless")||Id==TEXT("Story"))return {TEXT("Elysia")};
 if(Id==TEXT("Unity"))return {TEXT("Elysia"),TEXT("Hua"),TEXT("Griseo")};
 if(Id==TEXT("Deliverance"))return {TEXT("Kevin")};
 if(Id==TEXT("Discipline"))return {TEXT("Aponia")};
 if(Id==TEXT("Gold"))return {TEXT("Eden")};
 if(Id==TEXT("Helix"))return {TEXT("VillV")};
 if(Id==TEXT("Decimation"))return {TEXT("Kalpas")};
 if(Id==TEXT("Wisdom"))return {TEXT("Su")};
 if(Id==TEXT("Setsuna"))return {TEXT("Sakura")};
 if(Id==TEXT("Daybreak"))return {TEXT("Kosma")};
 if(Id==TEXT("Infinity"))return {TEXT("Mobius")};
 if(Id==TEXT("Stars"))return {TEXT("Griseo")};
 if(Id==TEXT("Vicissitude"))return {TEXT("Hua")};
 if(Id==TEXT("Reverie"))return {TEXT("Pardo")};
 if(Id==TEXT("DuskStreet"))return {TEXT("Pardo"),TEXT("Sakura"),TEXT("Aponia")};
 if(Id==TEXT("Kindred"))return {TEXT("VillV"),TEXT("Mobius")};
 if(Id==TEXT("LastBanquet"))return {TEXT("Aponia"),TEXT("Eden"),TEXT("Elysia")};
 return {};
}

// SImage::OnPaint renders its brush over the entire AllottedGeometry. Measure
// the local basis after Slate's accumulated render transform so DPI, parent
// transforms and uniform scaling are reflected in the actual drawn rectangle.
// Clipping can hide part of that rectangle but cannot correct a distorted one.
void CheckStigmataDrawnAspect(FAutomationTestBase* Test,UImage* Image,const FString& Label,FName TextureParameter=FName(TEXT("PortraitTexture")))
{
 if(!Test->TestTrue(*(Label+TEXT(" is visible")),Image&&Image->IsVisible()))return;
 const FSlateBrush& Brush=Image->GetBrush();
 auto* Texture=StigmataBrushTexture(Image,TextureParameter);
 if(!Test->TestTrue(*(Label+TEXT(" uses a drawable texture")),Texture&&Brush.DrawAs==ESlateBrushDrawType::Image))return;
 const FBox2D UV=Brush.GetUVRegion();
 if(Texture->GetFName()==TEXT("T_StigmaC_Portrait_VillVCompleteHat"))
  Test->TestTrue(*(Label+TEXT(" includes the source hat top and crown region")),UV.bIsValid&&
   UV.Min.X<=.46&&UV.Max.X>=.67&&UV.Min.Y<=.066&&UV.Max.Y>=.25);
 if(Texture->GetFName()==TEXT("T_StigmaC_Portrait_HumanOfficial"))
 {
  Test->TestTrue(*(Label+TEXT(" uses the official Human upper-body framing")),UV.bIsValid&&
   UV.Min.Equals(FVector2D(.28,.075),.0001)&&UV.Max.Equals(FVector2D(.80,.78),.0001));
  Test->TestTrue(*(Label+TEXT(" keeps Human's crown, face and raised hand inside the crop")),UV.bIsValid&&
   UV.Min.X<=.40&&UV.Max.X>=.76&&UV.Min.Y<=.08&&UV.Max.Y>=.46);
 }
 const FVector2D UVSize=UV.bIsValid?UV.Max-UV.Min:FVector2D(1.f,1.f);
 const double SourceWidth=double(Texture->GetSizeX())*UVSize.X;
 const double SourceHeight=double(Texture->GetSizeY())*UVSize.Y;
 if(!Test->TestTrue(*(Label+TEXT(" has valid source dimensions")),SourceWidth>0.&&SourceHeight>0.))return;
 const FGeometry& Geometry=Image->GetCachedGeometry();
 const FVector2D Size=Geometry.GetLocalSize();
 if(!Test->TestTrue(*(Label+TEXT(" has painted geometry")),Size.X>0.&&Size.Y>0.))return;
 const FVector2D Origin=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
 const FVector2D XEnd=Geometry.LocalToAbsolute(FVector2D(Size.X,0.f));
 const FVector2D YEnd=Geometry.LocalToAbsolute(FVector2D(0.f,Size.Y));
 const double DrawnWidth=(XEnd-Origin).Size();
 const double DrawnHeight=(YEnd-Origin).Size();
 if(!Test->TestTrue(*(Label+TEXT(" has nonzero screen dimensions")),DrawnWidth>0.&&DrawnHeight>0.))return;
 const double AxisSkew=FMath::Abs(FVector2D::DotProduct(XEnd-Origin,YEnd-Origin)/(DrawnWidth*DrawnHeight));
 Test->TestTrue(*(Label+TEXT(" has orthogonal screen axes")),AxisSkew<=.02);
 const double Expected=SourceWidth/SourceHeight,Actual=DrawnWidth/DrawnHeight;
 Test->TestTrue(*FString::Printf(TEXT("%s preserves aspect: actual %.4f, UV source %.4f"),*Label,Actual,Expected),
  FMath::Abs(Actual/Expected-1.)<=.02);
}

// New additive backlights use the portrait's original normalized UV space.
// The light can extend beyond the portrait crop, so compare the UV-to-screen
// mapping rather than requiring the two images to have identical bounds.
// Legacy abstract glows deliberately use a different silhouette.
void CheckStigmataBacklightAlignment(FAutomationTestBase* Test,UImage* Glow,UImage* Portrait,const FString& Label)
{
 auto* Light=StigmataBrushTexture(Glow,TEXT("LightTexture"));
 if(!Light||!Light->GetName().StartsWith(TEXT("T_StigmaC_Backlight_")))return;
 if(!Test->TestTrue(*(Label+TEXT(" has visible portrait and backlight")),Glow&&Portrait&&Glow->IsVisible()&&Portrait->IsVisible()))return;
 const FBox2D GlowUV=Glow->GetBrush().GetUVRegion(),PortraitUV=Portrait->GetBrush().GetUVRegion();
 if(!Test->TestTrue(*(Label+TEXT(" has valid UV regions")),GlowUV.bIsValid&&PortraitUV.bIsValid))return;
 const FVector2D GU=GlowUV.Max-GlowUV.Min,PU=PortraitUV.Max-PortraitUV.Min;
 if(!Test->TestTrue(*(Label+TEXT(" has nonzero UV extents")),GU.X>0.&&GU.Y>0.&&PU.X>0.&&PU.Y>0.))return;
 Test->TestTrue(*(Label+TEXT(" covers portrait source region")),
  GlowUV.Min.X<=PortraitUV.Min.X+.0001&&GlowUV.Min.Y<=PortraitUV.Min.Y+.0001&&
  GlowUV.Max.X>=PortraitUV.Max.X-.0001&&GlowUV.Max.Y>=PortraitUV.Max.Y-.0001);
 const FGeometry& G=Glow->GetCachedGeometry();const FGeometry& P=Portrait->GetCachedGeometry();
 const FVector2D GS=G.GetLocalSize(),PS=P.GetLocalSize();
 if(!Test->TestTrue(*(Label+TEXT(" has painted backlight and portrait geometry")),
  GS.X>0.&&GS.Y>0.&&PS.X>0.&&PS.Y>0.))return;
 const FVector2D G0=G.LocalToAbsolute(FVector2D::ZeroVector),P0=P.LocalToAbsolute(FVector2D::ZeroVector);
 const FVector2D GX=G.LocalToAbsolute(FVector2D(GS.X,0.)),PX=P.LocalToAbsolute(FVector2D(PS.X,0.));
 const FVector2D GY=G.LocalToAbsolute(FVector2D(0.,GS.Y)),PY=P.LocalToAbsolute(FVector2D(0.,PS.Y));
 auto MapUV=[](const FVector2D& UV,const FBox2D& Region,const FVector2D& Origin,const FVector2D& XEnd,const FVector2D& YEnd)
 {
  const FVector2D Extent=Region.Max-Region.Min;
  return Origin+(XEnd-Origin)*((UV.X-Region.Min.X)/Extent.X)+(YEnd-Origin)*((UV.Y-Region.Min.Y)/Extent.Y);
 };
 const FVector2D Samples[]={PortraitUV.Min,FVector2D(PortraitUV.Max.X,PortraitUV.Min.Y),FVector2D(PortraitUV.Min.X,PortraitUV.Max.Y)};
 for(int32 I=0;I<UE_ARRAY_COUNT(Samples);++I)
 {
  const FVector2D GLight=MapUV(Samples[I],GlowUV,G0,GX,GY),PPortrait=MapUV(Samples[I],PortraitUV,P0,PX,PY);
  Test->TestTrue(*FString::Printf(TEXT("%s source UV sample %d aligns: light (%.2f,%.2f), portrait (%.2f,%.2f)"),
   *Label,I,GLight.X,GLight.Y,PPortrait.X,PPortrait.Y),GLight.Equals(PPortrait,2.));
 }
}

// Glows use one generated image per character. All glow siblings must draw
// behind every portrait in the same group, even where members overlap.
void CheckStigmataHeroGlow(FAutomationTestBase* Test,UEGDemoPanel* Panel,int32 Index,int32 Hero,const TArray<FString>& Heroes)
{
 auto* Glow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),Index,Hero))));
 if(!Test->TestTrue(TEXT("Character glow widget exists"),Glow&&Heroes.IsValidIndex(Hero)))return;
 FString BacklightName=FString::Printf(TEXT("T_StigmaC_Backlight_%s"),*Heroes[Hero]);
 const FString LegacyName=FString::Printf(TEXT("T_StigmaC_Glow_%s"),*Heroes[Hero]);
 const FString Folder=TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/");
 if(Heroes[Hero]==TEXT("VillV")&&FPackageName::DoesPackageExist(Folder+TEXT("T_StigmaC_Portrait_VillVCompleteHat"))&&FPackageName::DoesPackageExist(Folder+TEXT("T_StigmaC_Backlight_VillVCompleteHat")))BacklightName=TEXT("T_StigmaC_Backlight_VillVCompleteHat");
 if(Heroes[Hero]==TEXT("Mobius")&&FPackageName::DoesPackageExist(Folder+TEXT("T_StigmaC_Backlight_MobiusV2")))BacklightName=TEXT("T_StigmaC_Backlight_MobiusV2");
 const bool bHasBacklight=FPackageName::DoesPackageExist(Folder+BacklightName);
 const bool bHuman=Heroes[Hero]==TEXT("Human");
 if(bHuman)
 {
  Test->TestTrue(TEXT("Human's dedicated backlight package is required"),bHasBacklight);
  Test->TestNotNull(TEXT("Human backlight uses a dynamic UI material"),Cast<UMaterialInstanceDynamic>(Glow->GetBrush().GetResourceObject()));
 }
 const FString AssetName=(bHasBacklight||bHuman)?BacklightName:LegacyName;
 const FString Package=Folder+AssetName;
 const FString Expected=FString::Printf(TEXT("%s.%s"),*Package,*AssetName);
 const UObject* Art=StigmataBrushTexture(Glow,TEXT("LightTexture"));
 if(FPackageName::DoesPackageExist(Package))
 {
  Test->TestTrue(*FString::Printf(TEXT("Choice %d member %d glow uses %s"),Index,Hero,*Heroes[Hero]),Art&&Art->GetPathName()==Expected);
  if(bHasBacklight)
   Test->TestTrue(*FString::Printf(TEXT("Choice %d member %d backlight uses a UI material"),Index,Hero),Cast<UMaterialInterface>(Glow->GetBrush().GetResourceObject())!=nullptr);
 }
 else
 {
  const bool bRequiredSample=bHuman||Heroes[Hero]==TEXT("Elysia")||Heroes[Hero]==TEXT("Kevin")||Heroes[Hero]==TEXT("Mobius");
  Test->TestFalse(*FString::Printf(TEXT("Missing %s glow remains hidden"),*Heroes[Hero]),Glow->IsVisible());
  if(bRequiredSample)Test->AddError(FString::Printf(TEXT("Required sample glow is missing: %s"),*Package));
  else Test->AddInfo(FString::Printf(TEXT("Character glow not yet imported; hidden fallback checked: %s"),*Package));
 }
 auto* GlowContainer=Glow->GetParent();
 auto* Area=GlowContainer?GlowContainer->GetParent():nullptr;
 auto* GlowSlot=GlowContainer?Cast<UCanvasPanelSlot>(GlowContainer->Slot):nullptr;
 if(!Test->TestTrue(TEXT("Character glow is in its art area"),Area&&GlowSlot))return;
 const int32 GlowIndex=Area->GetChildIndex(GlowContainer);
 for(int32 Other=0;Other<Heroes.Num();++Other)
 {
  auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),Index,Other))));
  auto* PortraitContainer=Portrait?Portrait->GetParent():nullptr;
  auto* PortraitSlot=PortraitContainer?Cast<UCanvasPanelSlot>(PortraitContainer->Slot):nullptr;
  const int32 PortraitIndex=PortraitContainer?Area->GetChildIndex(PortraitContainer):INDEX_NONE;
  const bool bBehind=PortraitContainer&&PortraitContainer->GetParent()==Area&&PortraitSlot&&GlowIndex!=INDEX_NONE&&PortraitIndex!=INDEX_NONE&&
   (GlowSlot->GetZOrder()<PortraitSlot->GetZOrder()||
    (GlowSlot->GetZOrder()==PortraitSlot->GetZOrder()&&GlowIndex<PortraitIndex));
  Test->TestTrue(*FString::Printf(TEXT("Choice %d member %d glow draws behind portrait %d"),Index,Hero,Other),bBehind);
 }
}

void CheckStigmataSelectedCorners(FAutomationTestBase* Test,UEGDemoPanel* Panel,int32 Selected)
{
 for(int32 Index=0;Index<3;++Index)
 {
  auto* Corners=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataSelectedCorners%d"),Index))));
  const auto* Texture=StigmataBrushTexture(Corners,TEXT("LightTexture"));
  Test->TestTrue(*FString::Printf(TEXT("Choice %d has selected-corner material and art"),Index),Corners&&
   Cast<UMaterialInstanceDynamic>(Corners->GetBrush().GetResourceObject())&&Texture&&
   Texture->GetPathName()==TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_SelectedCorners.T_StigmaC_SelectedCorners"));
  Test->TestTrue(*FString::Printf(TEXT("Choice %d corner visibility follows hover %d"),Index,Selected),Corners&&Corners->IsVisible()==(Index==Selected));
 }
}

void CheckRequiredStigmataBacklight(FAutomationTestBase* Test,UImage* Glow,const FString& Hero)
{
 const FString AssetName=Hero==TEXT("Mobius")?FString(TEXT("T_StigmaC_Backlight_MobiusV2")):
  (Hero==TEXT("VillV")?FString(TEXT("T_StigmaC_Backlight_VillVCompleteHat")):FString::Printf(TEXT("T_StigmaC_Backlight_%s"),*Hero));
 const FString Expected=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/%s.%s"),*AssetName,*AssetName);
 const UTexture2D* Light=StigmataBrushTexture(Glow,TEXT("LightTexture"));
 Test->TestTrue(*FString::Printf(TEXT("%s uses its required registered backlight"),*Hero),Glow&&
  Cast<UMaterialInstanceDynamic>(Glow->GetBrush().GetResourceObject())&&Light&&Light->GetPathName()==Expected);
}

int32 StigmataErrorCount(FAutomationTestBase* Test)
{
 FAutomationTestExecutionInfo Info;Test->GetExecutionInfo(Info);return Info.GetErrorTotal();
}

TSharedPtr<FPreparationDrawCapture> StartGameDrawCapture(UWorld* World,const FString& Prefix,FAutomationTestBase* Test)
{
 auto* Client=World?World->GetGameViewport():nullptr;
 if(!Client)return nullptr;
 auto Capture=MakeShared<FPreparationDrawCapture>();
 Capture->Client=Client;
 Capture->Name=FString::Printf(TEXT("%s_%s"),*Prefix,*FGuid::NewGuid().ToString(EGuidFormats::Digits));
 Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(Capture->Name+TEXT(".png")));
 const TWeakPtr<FPreparationDrawCapture> WeakCapture=Capture;
 const TWeakObjectPtr<UWorld> WeakWorld=World;
 Capture->DrawHandle=Client->OnDrawn().AddLambda([WeakCapture,WeakWorld,Test]()
 {
  const auto State=WeakCapture.Pin();
  if(!State.IsValid())return;
  ++State->Draws;
  if(State->Draws>=3&&!State->bRequested&&WeakWorld.IsValid()&&!FScreenshotRequest::IsScreenshotRequested())
  {State->bRequested=true;RequestGameScreenshot(WeakWorld.Get(),*State->Name,Test);}
 });
 return Capture;
}

// Observe the real, unmodified success animation and request one screenshot
// during its brief upward bounce, before the panel begins fading.
TSharedPtr<FPreparationDrawCapture> StartStigmataSuccessCapture(UWorld* World,UButton* Choice,UWidget* Overlay,FAutomationTestBase* Test)
{
 auto* Client=World?World->GetGameViewport():nullptr;
 if(!Client||!Choice||!Overlay)return nullptr;
 auto Capture=MakeShared<FPreparationDrawCapture>();Capture->Client=Client;
 Capture->Name=FString::Printf(TEXT("StigmataSuccessMid_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits));
 Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(Capture->Name+TEXT(".png")));
 const TWeakPtr<FPreparationDrawCapture> WeakCapture=Capture;
 const TWeakObjectPtr<UWorld> WeakWorld=World;
 const TWeakObjectPtr<UButton> WeakChoice=Choice;
 const TWeakObjectPtr<UWidget> WeakOverlay=Overlay;
 const double Started=FPlatformTime::Seconds();
 Capture->DrawHandle=Client->OnDrawn().AddLambda([WeakCapture,WeakWorld,WeakChoice,WeakOverlay,Started,Test]()
 {
  const auto State=WeakCapture.Pin();if(!State.IsValid())return;
  ++State->Draws;if(State->bRequested)return;
  const auto* CurrentChoice=WeakChoice.Get();const auto* CurrentOverlay=WeakOverlay.Get();
  const double Elapsed=FPlatformTime::Seconds()-Started;
  if(!CurrentChoice||!CurrentOverlay||!CurrentOverlay->IsVisible()||Elapsed>1.15)
   {State->bCelebrationMissed=true;return;}
  const FWidgetTransform& Transform=CurrentChoice->GetRenderTransform();
  const float Scale=Transform.Scale.X,OffsetY=Transform.Translation.Y,Opacity=CurrentOverlay->GetRenderOpacity();
  if(Scale>=1.06f&&OffsetY<=-10.f&&Opacity>=.95f&&WeakWorld.IsValid()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   State->bRequested=true;
   Test->AddInfo(FString::Printf(TEXT("Success bounce screenshot requested %.3fs after observed commit, choice scale %.3f, translation Y %.1f, overlay opacity %.3f."),Elapsed,Scale,OffsetY,Opacity));
   RequestGameScreenshot(WeakWorld.Get(),*State->Name,Test);
  }
 });
 return Capture;
}

// Request the screenshot while the displayed progress is in range, never after releasing.
TSharedPtr<FPreparationDrawCapture> StartProtectedHoldCapture(UWorld* World,UTextBlock* Progress,UImage* ProgressImage,bool bReference,FAutomationTestBase* Test)
{
 auto* Client=World?World->GetGameViewport():nullptr;
 if(!Client||!Progress||!ProgressImage)return nullptr;
 auto Capture=MakeShared<FPreparationDrawCapture>();
 Capture->Client=Client;
 Capture->Name=FString::Printf(TEXT("%s_%s"),bReference?TEXT("StigmataReferenceC_HoldProtected"):TEXT("StigmataHoldProgress_Protected"),*FGuid::NewGuid().ToString(EGuidFormats::Digits));
 Capture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(Capture->Name+TEXT(".png")));
 const TWeakPtr<FPreparationDrawCapture> WeakCapture=Capture;
 const TWeakObjectPtr<UWorld> WeakWorld=World;
 const TWeakObjectPtr<UTextBlock> WeakProgress=Progress;
 const TWeakObjectPtr<UImage> WeakProgressImage=ProgressImage;
 Capture->DrawHandle=Client->OnDrawn().AddLambda([WeakCapture,WeakWorld,WeakProgress,WeakProgressImage,Test]()
 {
  const auto State=WeakCapture.Pin();
  if(!State.IsValid())return;
  ++State->Draws;
  const auto* Label=WeakProgress.Get();
  if(!Label||!Label->IsVisible()){State->bProgressMissed=true;return;}
  const FString Text=Label->GetText().ToString();
  if(!Text.EndsWith(TEXT("%"))){State->bProgressMissed=true;return;}
  const FString Digits=Text.LeftChop(1);
  if(!Digits.IsNumeric()){State->bProgressMissed=true;return;}
  const int32 Percent=FCString::Atoi(*Digits);
  State->ObservedPercent=Percent;
  if(Percent<0||Percent>100){State->bProgressMissed=true;return;}
  if(State->bRequested)
  {
   // A pending screenshot must not be allowed to drift beyond the requested
   // range while the render thread is still processing it.
   if(FScreenshotRequest::IsScreenshotRequested()&&Percent>80)State->bProgressMissed=true;
   return;
  }
  if(Percent<45)return;
  if(Percent>80){State->bProgressMissed=true;return;}
  if(State->Draws>=3&&WeakWorld.IsValid()&&!FScreenshotRequest::IsScreenshotRequested())
  {
   auto* Image=WeakProgressImage.Get();
   auto* Material=Image?Cast<UMaterialInstanceDynamic>(Image->GetBrush().GetResourceObject()):nullptr;
   Test->TestNotNull(TEXT("Reference progress uses a dynamic material"),Material);
   Test->TestTrue(TEXT("Reference progress image is visible and opaque"),Image&&Image->IsVisible()&&Image->GetRenderOpacity()>.99f);
   if(Image)
   {
    const FGeometry& G=Image->GetCachedGeometry();
    const FVector2D Size=G.GetLocalSize();
    const FVector2D Origin=G.LocalToAbsolute(FVector2D::ZeroVector);
    const FVector2D End=G.LocalToAbsolute(Size);
    Test->TestTrue(TEXT("Reference progress has painted geometry"),Size.X>0.&&Size.Y>0.);
    Test->AddInfo(FString::Printf(TEXT("Progress image: visible=%d opacity=%.3f local=%.1fx%.1f screen=(%.1f,%.1f)-(%.1f,%.1f) drawAs=%d"),
     Image->IsVisible()?1:0,Image->GetRenderOpacity(),Size.X,Size.Y,Origin.X,Origin.Y,End.X,End.Y,int32(Image->GetBrush().DrawAs)));
   }
   if(Material)
   {
    const FString ParentPath=Material->Parent?Material->Parent->GetPathName():TEXT("<null>");
    Test->TestEqual(TEXT("Reference progress material parent"),ParentPath,FString(TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/M_UI_StigmaC_ProgressReveal.M_UI_StigmaC_ProgressReveal")));
    float Value=-1.f;
    const bool bHasValue=Material->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Progress")),Value);
    Test->TestTrue(TEXT("Reference material progress follows displayed percentage"),bHasValue&&Value>=.44f&&Value<=.81f&&FMath::Abs(Value-Percent/100.f)<=.02f);
    auto* Texture=StigmataBrushTexture(Image,TEXT("RingTexture"));
    Test->TestTrue(TEXT("Reference progress binds its additive light texture"),Texture&&Texture->GetPathName()==TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_ProgressRingLightV2.T_StigmaC_ProgressRingLightV2"));
    Test->TestTrue(TEXT("Reference progress ring texture has dimensions"),Texture&&Texture->GetSizeX()>0&&Texture->GetSizeY()>0);
    Test->AddInfo(FString::Printf(TEXT("Progress material: parent=%s value=%.4f displayed=%d%% texture=%s dimensions=%dx%d"),
     *ParentPath,Value,Percent,Texture?*Texture->GetPathName():TEXT("<null>"),Texture?Texture->GetSizeX():0,Texture?Texture->GetSizeY():0));
   }
   State->RequestPercent=Percent;State->bRequested=true;
   Test->AddInfo(FString::Printf(TEXT("Protected reference hold screenshot requested at %d%%."),Percent));
   RequestGameScreenshot(WeakWorld.Get(),*State->Name,Test);
  }
 });
 return Capture;
}
class FEGStartSizedPIE final : public IAutomationLatentCommand
{
 const FIntPoint Size;
public:
 explicit FEGStartSizedPIE(FIntPoint InSize=FIntPoint(1920,1080)):Size(InSize){}
 virtual bool Update() override
 {
  auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
  Settings->NewWindowWidth=Size.X;
  Settings->NewWindowHeight=Size.Y;
  FRequestPlaySessionParams Params;
  Params.EditorPlaySettings=Settings;
  // UE's regular PIE window puts a Slate title bar inside its client area.
  // A borderless PIE window gives the game viewport the full requested pixels.
  TSharedRef<SWindow> Window=SNew(SWindow)
   .ScreenPosition(FVector2D(0,0))
   .ClientSize(FVector2D(Size.X,Size.Y))
   .AutoCenter(EAutoCenter::None)
   .CreateTitleBar(false)
   .UseOSWindowBorder(false)
   .SizingRule(ESizingRule::FixedSize)
   .SaneWindowPlacement(false)
   .AdjustInitialSizeAndPositionForDPIScale(false);
  FSlateApplication::Get().AddWindow(Window);
  Params.CustomPIEWindow=Window;
  if(GUnrealEd->CheckForPlayerStart()==nullptr)
   FAutomationEditorCommonUtils::SetPlaySessionStartToActiveViewport(Params);
  GUnrealEd->RequestPlaySession(Params);
  return true;
 }
};
void RequestBossHighResScreenshot(UWorld* World,const TCHAR* Name,FAutomationTestBase* Test)
{
 if(!FApp::CanEverRender())
 {
  Test->AddInfo(TEXT("Boss screenshot skipped because rendering is unavailable."));
  return;
 }
 auto* Controller=World?World->GetFirstPlayerController():nullptr;
 if(!Controller)return;
 const FString Filename=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/FString::Printf(TEXT("%s.png"),Name));
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename),true);
 Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1920x1080 filename=\"%s\""),*Filename));
 Test->AddInfo(FString::Printf(TEXT("Requested 1920x1080 Boss screenshot: %s. Inspect the saved image separately."),*Filename));
}
const TCHAR* RouteName(EStigmataRoute Route)
{
 switch(Route)
 {
 case EStigmataRoute::Nine: return TEXT("Nine");
 case EStigmataRoute::Herrscher: return TEXT("Herrscher");
 default: return TEXT("Five");
 }
}
bool WaitForRewardScreenshot(UWorld* World,FAutomationTestBase* Test,int32 Stage,EStigmataRoute Route,
 const TCHAR* Scenario,int32& CapturedStage,int32& Frames)
{
 if(CapturedStage!=Stage){CapturedStage=Stage;Frames=0;}
 ++Frames;
 if(Frames==3)
 {
  const FString Name=FString::Printf(TEXT("SevenStage_%s_%s_Reward%d"),Scenario,RouteName(Route),Stage);
  RequestGameScreenshot(World,*Name,Test);
 }
 return Frames<=5;
}
class FEGPIETransactions final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 TSharedRef<FEGPIECheckState> State;
 const EStigmataRoute Route;
 double Deadline=0.;
 double BossStarted=0.;
 int32 LastStartedStage=0;
 int32 VerifiedCopyStage=0;
 bool bSetup=false;
 bool bBossScreenshotRequested=false;
 int32 CapturedRewardStage=0;
 int32 RewardFrames=0;
public:
 FEGPIETransactions(FAutomationTestBase* InTest,TSharedRef<FEGPIECheckState> InState,EStigmataRoute InRoute)
  :Test(InTest),State(InState),Route(InRoute){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+240.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Production seven-stage PIE lifecycle timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards())return false;
  auto* Cards=Mode->GetCards();auto* Match=Mode->GetMatch();
  if(!bSetup)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   Test->AddInfo(FString::Printf(TEXT("Controlled PIE: %s -> %s. Purchases, deployment and stigmata use public transactions. Rounds 1-6 use injected results. This is not real mouse input or balance validation."),*Mode->GetClass()->GetPathName(),*Mode->RunDefinition->GetPathName()));
   Test->TestTrue(TEXT("Formal campaign rules enabled"),Mode->RunDefinition->bSevenStageRules);
   Test->TestEqual(TEXT("Opening corruption"),Match->GetCorruption(),50);
   Test->TestEqual(TEXT("Four shop slots"),Cards->GetShop().Num(),4);
   Cards->AwardGold(100);
   const int64 Gold=Cards->GetGold();const auto Shop=Cards->GetShop();
   if(!Test->TestTrue(TEXT("Production shop has a product"),Shop.IsValidIndex(0)&&Shop[0]))return true;
   const int32 Revision=Cards->GetShopSlotRevision(0);
   if(!Test->TestTrue(TEXT("PIE purchase commits"),Cards->BuyShopSlotExpected(0,Revision)))return true;
   Test->TestEqual(TEXT("PIE purchase charges once"),Cards->GetGold(),Gold-Cards->GetRules().PurchasePrice);
   Test->TestFalse(TEXT("Stale product revision cannot purchase twice"),Cards->BuyShopSlotExpected(0,Revision));
   Test->TestTrue(TEXT("PIE shop refresh"),Cards->RefreshShop());
   if(!Test->TestTrue(TEXT("Grant deployment fixture"),Cards->GrantCard(Mode->RunDefinition->StartingCard)))return true;
   const FGuid DeploymentCard=Cards->GetHand().Last().InstanceId;
   if(!Test->TestTrue(TEXT("PIE deploy commits"),Cards->TryDeployHandCard(DeploymentCard,{2,1})))return true;
   const FGuid Hero=Mode->GetRoster()->FindHeroByDefinition(Mode->RunDefinition->StartingCard->HeroDefinition);
   if(!Test->TestTrue(TEXT("Grant merge fixture"),Cards->GrantCard(Mode->RunDefinition->StartingCard)))return true;
   Test->TestTrue(TEXT("PIE same-name merge commits"),Cards->TryMergeIntoHero(Cards->GetHand().Last().InstanceId,Hero));
   FEGHeroInstance Instance;Mode->GetRoster()->GetHero(Hero,Instance);
   Test->TestEqual(TEXT("PIE merge counts exactly two cards"),Instance.SameNameCards,2);
   bSetup=true;
  }

  const int32 Stage=Match->GetStageNumber();
  const EEGMatchPhase Phase=Match->GetPhase();
  if(Phase==EEGMatchPhase::Preparation)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(Stage>1)Test->TestTrue(TEXT("Previous combat incarnation is discarded"),!State->PreviousUnit.IsValid()||State->PreviousUnit->IsActorBeingDestroyed());
   if(Stage==6)Test->TestEqual(TEXT("Stage six uses normal population"),Cards->GetRules().DeploymentLimit,5);
   if(Stage==7)Test->TestEqual(TEXT("Final stage keeps chosen population limit"),Cards->GetRules().DeploymentLimit,Route==EStigmataRoute::Nine?9:5);
   if(Stage==7&&Route==EStigmataRoute::Herrscher)
   {
    const TArray<FEGCardInstance> FinalHand=Cards->GetHand();
    const auto Human=FinalHand.FindByPredicate([](const FEGCardInstance& Card)
     {return Card.Definition&&Card.Definition->HeroDefinition&&Card.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;});
    if(!Test->TestNotNull(TEXT("Fused human remains in hand before deployment"),Human))return true;
    if(!Test->TestTrue(TEXT("Fused human can be deployed"),Cards->TryDeployHandCard(Human->InstanceId,{2,1})))return true;
   }
   if(!Test->TestTrue(TEXT("PIE battle starts"),Mode->EnterCombat()))return true;
   LastStartedStage=Stage;
   for(auto* Unit:Mode->GetCombatUnits())
    if(Unit&&Unit->GetFaction_Implementation()==EEGFaction::Player){State->PreviousUnit=Unit;break;}
   return false;
  }
  if(Phase==EEGMatchPhase::Combat)
  {
   if(!Mode->BattleRules||!Mode->BattleRules->IsCombatRunning())return false;
   if(VerifiedCopyStage!=Stage){VerifyCopies(Test,Mode,Stage);VerifiedCopyStage=Stage;}
   if(Stage<7)
   {
    const bool bVictory=(Stage%2)==1;
    Test->TestTrue(TEXT("Controlled normal stage result"),Mode->ResolveCombat(bVictory));
    return false;
   }
   if(BossStarted==0.)BossStarted=FPlatformTime::Seconds();
   if(!bBossScreenshotRequested)
   {
    bBossScreenshotRequested=true;
    RequestBossHighResScreenshot(World,Route==EStigmataRoute::Nine?TEXT("SevenStageBoss9"):TEXT("SevenStageBoss5"),Test);
   }
   if(FPlatformTime::Seconds()-BossStarted<15.)return false;
   Test->AddInfo(TEXT("Boss ran autonomous combat for 15 seconds; a victory is then injected if combat is still active."));
   Test->TestTrue(TEXT("Controlled Boss victory"),Mode->ResolveCombat(true));
   return false;
  }
  if(Phase==EEGMatchPhase::Settlement||Phase==EEGMatchPhase::Resolution)return false;
  if(Phase==EEGMatchPhase::Reward)
  {
   if(WaitForRewardScreenshot(World,Test,Stage,Route,TEXT("Controlled"),CapturedRewardStage,RewardFrames))return false;
   if(!ChooseReward(Test,Mode,Stage,Route,false))return true;
   return false;
  }
  if(Phase!=EEGMatchPhase::Finished)return false;
  Test->TestEqual(TEXT("Final stage actually reached"),LastStartedStage,7);
  Test->TestTrue(TEXT("PIE run restarts"),Mode->AdvanceDemo());
  Test->TestTrue(TEXT("Restart restores preparation"),Mode->CanPrepare());
  Test->TestEqual(TEXT("Restart returns to first stage"),Match->GetStageNumber(),1);
  Test->TestEqual(TEXT("Restart restores opening hand"),Cards->GetHand().Num(),Mode->RunDefinition->StartingCards);
  Test->TestEqual(TEXT("Restart resets corruption"),Match->GetCorruption(),50);
  Test->TestEqual(TEXT("Restart restores population"),Cards->GetRules().DeploymentLimit,3);
  Test->TestTrue(TEXT("Restart clears prior roster"),Mode->GetRoster()->GetHeroes().IsEmpty());
  Test->TestTrue(TEXT("Restart clears stigmata ownership"),Match->GetStigmataOwned().IsEmpty());
  return true;
 }
};
class FEGPIENaturalRun final : public IAutomationLatentCommand
{
 struct FPerformanceFrame
 {
  uint64 Frame=0;
  double AppDelta=0,WallDelta=0,WorldTime=0,BattleTime=0;
  float BossHealth=-1,BossMaxHealth=-1,TimeDilation=1;
  float HumanHealth=-1,HumanMana=-1,HumanMaxMana=-1,HumanAttackSpeed=-1,HumanAttackRange=-1,HumanEffectiveRange=-1;
  int32 Phase=0,PlayerCount=0,AsyncPackages=0,BossPhase=0;
  int32 BossShieldHits=-1,HumanLevel=-1,HumanActionCount=-1,HumanCommitCount=-1,HumanPresentationPhase=-1;
  bool bScreenshot=false,bScreenshotPending=false,bFirstCombatFrame=false,bPaused=false;
 };
 FAutomationTestBase* Test;
 EStigmataRoute Route;
 bool bStarted=false;
 bool bBossScreenshotRequested=false;
 int32 CapturedRewardStage=0;
 int32 RewardFrames=0;
 bool bSettlementScreenshotRequested=false;
 bool bCopyScreenshotRequested=false;
 bool bBossPhaseScreenshotRequested=false;
 bool bHerrscherBattleScreenshotRequested=false;
 bool bStageOnePreparationCaptured=false;
 bool bStageOneCombatCaptured=false;
 bool bBossUICaptured=false;
 bool bDenseCombatCaptured=false,bCameraCombatCaptured=false,bFinalStigmataChecked=false;
 bool bCaptureIsCamera=false;
 TSharedPtr<FPreparationDrawCapture> DenseCapture;
 TWeakObjectPtr<ACameraActor> ModifiedCamera;
 FTransform OriginalCameraTransform;
 float OriginalCameraFOV=0.f;
 bool bCameraModified=false;
 int32 StageOnePreparationFrames=0;
 TSharedPtr<FPreparationDrawCapture> PreparationCapture;
 bool bPreparationOnly=false;
 bool bCollectionCaptured=false;
 int32 CollectionFrames=0;
 int32 SidebarCheckStep=0;
 double SidebarCheckUntil=0.;
 TWeakObjectPtr<AEGChessUnit> BossUnit;
 TWeakObjectPtr<AEGChessUnit> HumanUnit;
 int32 LastStage=0;
 int32 VerifiedCopyStage=0;
 double Deadline=0.;
 double PreviousWallTime=0.;
 bool bWasFinalCombat=false;
 int32 ScreenshotMarkerFrames=0;
 FString PerformanceRunId=FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
 FString PerformanceStartedUtc=FDateTime::UtcNow().ToIso8601();
 TArray<FPerformanceFrame> PerformanceFrames;

 void MarkScreenshot()
 {
  if(!FApp::CanEverRender())return;
  if(!PerformanceFrames.IsEmpty())PerformanceFrames.Last().bScreenshot=true;
  ScreenshotMarkerFrames=3;
 }
 void RecordPerformance(UWorld* World,AEGDemoGameMode* Mode,int32 Stage,EEGMatchPhase Phase)
 {
  if(!FApp::CanEverRender())return;
  const double CurrentWallTime=FPlatformTime::Seconds();
  const double WallDelta=PreviousWallTime>0.?CurrentWallTime-PreviousWallTime:0.;
  PreviousWallTime=CurrentWallTime;
  const bool bFinalCombat=Stage==7&&Phase==EEGMatchPhase::Combat;
  if(Stage!=7){bWasFinalCombat=false;return;}
  FPerformanceFrame& Sample=PerformanceFrames.AddDefaulted_GetRef();
  Sample.Frame=GFrameCounter;
  Sample.AppDelta=FApp::GetDeltaTime();
  Sample.WallDelta=WallDelta;
  Sample.WorldTime=World->GetTimeSeconds();
  Sample.BattleTime=Mode->GetBattleSeconds();
  Sample.TimeDilation=UGameplayStatics::GetGlobalTimeDilation(World);
  Sample.Phase=static_cast<int32>(Phase);
  Sample.AsyncPackages=GetNumAsyncPackages();
  Sample.bPaused=UGameplayStatics::IsGamePaused(World);
  Sample.bFirstCombatFrame=bFinalCombat&&!bWasFinalCombat;
  Sample.bScreenshotPending=FScreenshotRequest::IsScreenshotRequested();
  Sample.bScreenshot=ScreenshotMarkerFrames>0||Sample.bScreenshotPending;
  if(ScreenshotMarkerFrames>0)--ScreenshotMarkerFrames;
  if(BossUnit.IsValid()&&BossUnit->GetAttributes())
  {
   Sample.BossHealth=BossUnit->GetAttributes()->GetHealth();
   Sample.BossMaxHealth=BossUnit->GetAttributes()->GetMaxHealth();
   if(const auto* Rules=BossUnit->FindComponentByClass<UEGCorruptionCombatComponent>())
   {Sample.BossPhase=Rules->IsPhaseTwo()?2:1;Sample.BossShieldHits=Rules->GetShieldHitsRemaining();}
  }
  if(HumanUnit.IsValid()&&HumanUnit->GetAttributes()&&HumanUnit->GetHeroSkill())
  {
   const auto* Attributes=HumanUnit->GetAttributes();
   const auto* Skill=HumanUnit->GetHeroSkill();
   Sample.HumanHealth=Attributes->GetHealth();Sample.HumanMana=Attributes->GetMana();Sample.HumanMaxMana=Attributes->GetMaxMana();
   Sample.HumanAttackSpeed=Attributes->GetAttackSpeed();Sample.HumanLevel=Skill->GetLevel();
   Sample.HumanActionCount=Skill->ActionCount;Sample.HumanCommitCount=Skill->CommitCount;
   if(const auto* Definition=HumanUnit->GetDefinition())Sample.HumanAttackRange=Definition->GetAttackRange();
   if(BossUnit.IsValid()&&HumanUnit->GetCombat())Sample.HumanEffectiveRange=HumanUnit->GetCombat()->GetEffectiveAttackRange(BossUnit.Get());
   if(const auto* Behavior=Cast<UEGElysiaHerrscherSkillBehavior>(Skill->GetBehavior()))
    Sample.HumanPresentationPhase=static_cast<int32>(Behavior->GetPresentationPhase());
  }
  for(const AEGChessUnit* Unit:Mode->GetCombatUnits())
   if(IsValid(Unit)&&Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->IsAlive_Implementation()&&
      !Unit->IsAuxiliaryEmplacement()&&Unit->GetHeroSkill()&&!Unit->GetHeroSkill()->bSummon)++Sample.PlayerCount;
  bWasFinalCombat=bFinalCombat;
 }
 void WritePerformance()
 {
  if(!FApp::CanEverRender()||PerformanceFrames.IsEmpty())return;
  const FString Directory=FPaths::ProjectSavedDir()/TEXT("PCUIPhase20261103");
  if(!IFileManager::Get().MakeDirectory(*Directory,true)){Test->AddError(TEXT("Unable to create performance directory"));return;}
  const FString Path=Directory/TEXT("PerfRoute.csv");
  FString Output;
  if(IFileManager::Get().FileSize(*Path)<=0)
   Output=TEXT("run_id,started_utc,route,viewport_width,viewport_height,frame,phase,world_seconds,battle_seconds,time_dilation,app_delta_ms,wall_delta_ms,boss_health,boss_max_health,boss_phase,boss_shield_hits,alive_player_heroes,human_health,human_mana,human_max_mana,human_level,human_attack_speed,human_attack_range,human_effective_range,human_action_count,human_commit_count,human_presentation_phase,async_packages,paused,first_combat_frame,screenshot_frame,screenshot_pending,eligible_for_summary\n");
  TArray<double> AppTimes,WallTimes;
  for(const auto& Sample:PerformanceFrames)
  {
   const bool bEligible=Sample.Phase==static_cast<int32>(EEGMatchPhase::Combat)&&!Sample.bFirstCombatFrame&&
    !Sample.bScreenshot&&!Sample.bScreenshotPending&&!Sample.bPaused&&Sample.AsyncPackages==0&&
    Sample.AppDelta>0.&&Sample.WallDelta>0.&&FMath::IsFinite(Sample.AppDelta)&&FMath::IsFinite(Sample.WallDelta);
   if(bEligible){AppTimes.Add(Sample.AppDelta*1000.);WallTimes.Add(Sample.WallDelta*1000.);}
   const FIntPoint ViewportSize=RouteViewportSize(Route);
   Output+=FString::Printf(TEXT("%s,%s,%s,%d,%d,%llu,%d,%.6f,%.6f,%.3f,%.6f,%.6f,%.2f,%.2f,%d,%d,%d,%.2f,%.2f,%.2f,%d,%.3f,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d,%d\n"),
    *PerformanceRunId,*PerformanceStartedUtc,RouteName(Route),ViewportSize.X,ViewportSize.Y,Sample.Frame,Sample.Phase,Sample.WorldTime,Sample.BattleTime,
    Sample.TimeDilation,Sample.AppDelta*1000.,Sample.WallDelta*1000.,Sample.BossHealth,Sample.BossMaxHealth,Sample.BossPhase,Sample.BossShieldHits,
    Sample.PlayerCount,Sample.HumanHealth,Sample.HumanMana,Sample.HumanMaxMana,Sample.HumanLevel,Sample.HumanAttackSpeed,Sample.HumanAttackRange,
    Sample.HumanEffectiveRange,Sample.HumanActionCount,Sample.HumanCommitCount,Sample.HumanPresentationPhase,Sample.AsyncPackages,
    Sample.bPaused?1:0,Sample.bFirstCombatFrame?1:0,Sample.bScreenshot?1:0,
    Sample.bScreenshotPending?1:0,bEligible?1:0);
  }
  if(!FFileHelper::SaveStringToFile(Output,*Path,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append))
  {Test->AddError(TEXT("Unable to write performance CSV"));return;}
  if(AppTimes.IsEmpty()){Test->AddWarning(TEXT("No eligible final battle performance frames"));return;}
  auto Describe=[](TArray<double>& Times)
  {
   double Total=0.;for(double Value:Times)Total+=Value;
   Times.Sort();const int32 Count=Times.Num();
   return FString::Printf(TEXT("avg %.3f ms, P95 %.3f ms, P99 %.3f ms"),Total/Count,
    Times[FMath::Clamp(FMath::CeilToInt(Count*.95)-1,0,Count-1)],Times[FMath::Clamp(FMath::CeilToInt(Count*.99)-1,0,Count-1)]);
  };
  const FString AppSummary=Describe(AppTimes),WallSummary=Describe(WallTimes);
  Test->AddInfo(FString::Printf(TEXT("Final performance %s, %d eligible/%d recorded frames; app %s; wall %s; per-frame time dilation is recorded in CSV: %s"),
   RouteName(Route),AppTimes.Num(),PerformanceFrames.Num(),*AppSummary,*WallSummary,*Path));
 }
public:
 FEGPIENaturalRun(FAutomationTestBase* InTest,EStigmataRoute InRoute,bool bInPreparationOnly=false)
  :Test(InTest),Route(InRoute),bPreparationOnly(bInPreparationOnly){}
 ~FEGPIENaturalRun() override {RestoreCamera();}
 void RestoreCamera()
 {
  if(bCameraModified&&ModifiedCamera.IsValid())
  {
   ModifiedCamera->SetActorTransform(OriginalCameraTransform);
   if(auto* Camera=ModifiedCamera->GetCameraComponent())Camera->SetFieldOfView(OriginalCameraFOV);
  }
  bCameraModified=false;ModifiedCamera.Reset();
 }
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+600.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Natural campaign timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards())return false;
  auto* Cards=Mode->GetCards();auto* Match=Mode->GetMatch();
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(FApp::CanEverRender())
   {
    auto* Viewport=World->GetGameViewport()?World->GetGameViewport()->Viewport:nullptr;
    if(!Test->TestNotNull(TEXT("Graphical game viewport exists"),Viewport))return true;
    Test->TestEqual(TEXT("Graphical game viewport matches route resolution"),Viewport->GetSizeXY(),RouteViewportSize(Route));
   }
   Test->AddInfo(TEXT("Natural combat uses opening and earned currency, affordable shop purchases and refreshes, and normal 1x starting time dilation. Overtime is controlled by the campaign. Outcomes, health, damage, currency, cards and population are not injected. Stigmata and other interactions use public transactions programmatically."));
   UGameplayStatics::SetGlobalTimeDilation(World,1.f);
   bStarted=true;
  }
  const int32 Stage=Match->GetStageNumber();
  const EEGMatchPhase Phase=Match->GetPhase();
  if(Phase!=EEGMatchPhase::Combat)
  {
   if(bCameraModified)RestoreCamera();
   DenseCapture.Reset();
  }
  RecordPerformance(World,Mode,Stage,Phase);
  if(Phase==EEGMatchPhase::Preparation)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(Route==EStigmataRoute::Nine&&Stage==7&&!bFinalStigmataChecked)
   {
    auto* Panel=Mode->GetDemoPanel();
    if(!Panel)return false;
    auto* Final=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataOwned2")));
    auto* Close=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_CloseStigmataCollection")));
    auto* Details=Panel->GetWidgetFromName(TEXT("StigmataCollectionPanel"));
    if(!Test->TestEqual(TEXT("Story route was earned before final stigmata inspection"),Match->GetEndgameRoute(),FName(TEXT("Story")))||
       !Test->TestNotNull(TEXT("Final owned stigmata button exists"),Final)||
       !Test->TestNotNull(TEXT("Stigmata detail close button exists"),Close)||
       !Test->TestNotNull(TEXT("Stigmata detail panel exists"),Details))return true;
    Test->TestTrue(TEXT("Final owned stigmata is visible"),Final->IsVisible());
    Final->OnClicked.Broadcast();
    Test->TestTrue(TEXT("Programmatic final stigmata click opens detail"),Details->IsVisible());
    Close->OnClicked.Broadcast();
    Test->TestFalse(TEXT("Programmatic final stigmata detail closes"),Details->IsVisible());
    if(FSlateApplication::IsInitialized())FSlateApplication::Get().SetAllUserFocusToGameViewport();
    bFinalStigmataChecked=true;
   }
   if(Stage==3&&!bCollectionCaptured)
   {
    auto* Panel=Mode->GetDemoPanel();
    if(!Panel)return false;
    auto* Open=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataCollection")));
    auto* Close=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_CloseStigmataCollection")));
    auto* Collection=Panel->GetWidgetFromName(TEXT("StigmataCollectionPanel"));
    auto* Preview=Panel->GetWidgetFromName(TEXT("StigmataPreviewPanel"));
    if(!Test->TestNotNull(TEXT("Collection open button exists"),Open)||
       !Test->TestNotNull(TEXT("Collection close button exists"),Close)||
       !Test->TestNotNull(TEXT("Collection panel exists"),Collection)||
       !Test->TestNotNull(TEXT("Stigmata hover preview exists"),Preview))return true;
    if(CollectionFrames==0)
    {
     if(!Test->TestTrue(TEXT("Collection button is visible after acquisition"),Open->IsVisible()))return true;
     if(auto* Player=World->GetFirstPlayerController())Open->SetUserFocus(Player);
     Open->OnHovered.Broadcast();
     if(!Test->TestTrue(TEXT("Hover opens stigmata preview"),Preview->IsVisible()))return true;
    }
    ++CollectionFrames;
    if(CollectionFrames==3)
    {
     Test->TestTrue(TEXT("Focused stigmata preview remains open"),Preview->IsVisible());
     const FString Name=FString::Printf(TEXT("PCUI_%s_StigmataHover"),RouteName(Route));
     RequestGameScreenshot(World,*Name,Test);
    }
    if(CollectionFrames==6)
    {
     Open->OnUnhovered.Broadcast();
     Test->TestFalse(TEXT("Unhover hides stigmata preview"),Preview->IsVisible());
     Open->OnClicked.Broadcast();
     if(!Test->TestTrue(TEXT("Collection opens"),Collection->IsVisible()))return true;
    }
    if(CollectionFrames==9)
    {
     const FString Name=FString::Printf(TEXT("SevenStage_Natural_%s_Collection"),RouteName(Route));
     RequestGameScreenshot(World,*Name,Test);
    }
    if(CollectionFrames<=11)return false;
    Close->OnClicked.Broadcast();
    Test->TestFalse(TEXT("Collection closes"),Collection->IsVisible());
    auto* Player=World->GetFirstPlayerController();
    auto* GameHUD=Player?Cast<AEGHUD>(Player->GetHUD()):nullptr;
    auto* Main=GameHUD?GameHUD->GetMainWidget():nullptr;
    auto* Statistics=Main?Cast<UButton>(Main->GetWidgetFromName(TEXT("Button_Statistics"))):nullptr;
    auto* Signets=Main?Cast<UButton>(Main->GetWidgetFromName(TEXT("Button_SignetSidebar"))):nullptr;
    auto* Audio=Main?Cast<UButton>(Main->GetWidgetFromName(TEXT("AudioVolumeButton"))):nullptr;
    auto* Dock=Main?Main->GetWidgetFromName(TEXT("BoardStatistics")):nullptr;
    if(!Test->TestNotNull(TEXT("Statistics button exists"),Statistics)||
       !Test->TestNotNull(TEXT("Audio button exists"),Audio)||!Test->TestNotNull(TEXT("Statistics drawer exists"),Dock))return true;
    if(SidebarCheckStep==0)
    {
     Test->TestTrue(TEXT("Statistics button contains image logo"),ContainsDrawnImage(Statistics->GetContent()));
     Test->TestTrue(TEXT("Signet sidebar has no separate button"),!Signets||!Signets->IsVisible());
     Test->TestTrue(TEXT("Audio button contains image logo"),ContainsDrawnImage(Audio->GetContent()));
     Test->TestTrue(TEXT("Owned signet is visible by default"),Open->IsVisible());
     const auto Owned=Match->GetStigmataOwned();
     if(!Owned.IsEmpty())
     {
      const auto Heroes=ExpectedStigmataHeroes(Owned[0]);
      for(int32 Hero=0;Hero<Heroes.Num();++Hero)
      {
       const FName WidgetName(Heroes.Num()==1?TEXT("StigmataOwnedIcon0"):*FString::Printf(TEXT("StigmataOwnedGroupIcon0_%d"),Hero));
       auto* Image=Cast<UImage>(Panel->GetWidgetFromName(WidgetName));
       const auto* Texture=StigmataBrushTexture(Image);
       const FString Path=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelection20261105/T_UI_Signet_%s.T_UI_Signet_%s"),*Heroes[Hero],*Heroes[Hero]);
       Test->TestTrue(TEXT("Owned signet displays its matching character emblem"),Image&&Image->IsVisible()&&Texture&&Texture->GetPathName()==Path);
      }
     }
     Statistics->OnClicked.Broadcast();
     Test->TestFalse(TEXT("Opening statistics immediately hides signet"),Open->IsVisible());
     SidebarCheckUntil=FPlatformTime::Seconds()+.3;SidebarCheckStep=1;return false;
    }
    if(SidebarCheckStep==1)
    {
     if(FPlatformTime::Seconds()<SidebarCheckUntil)return false;
     Test->TestTrue(TEXT("Statistics drawer is visible"),Dock->IsVisible());
     Test->TestFalse(TEXT("Signet remains hidden under statistics"),Open->IsVisible());
     Statistics->OnClicked.Broadcast();
     SidebarCheckUntil=FPlatformTime::Seconds()+.3;SidebarCheckStep=2;return false;
    }
    if(FPlatformTime::Seconds()<SidebarCheckUntil)return false;
    Test->TestFalse(TEXT("Statistics button closes statistics"),Dock->IsVisible());
    Test->TestTrue(TEXT("Signet returns after statistics closes"),Open->IsVisible());
    Panel->RemoveFromParent();
    Panel->AddToViewport(20);
    Test->TestTrue(TEXT("Reconstructed panel retains phase clock"),Panel->GetWidgetFromName(TEXT("PhaseClockRoot"))!=nullptr);
    Test->TestTrue(TEXT("Reconstructed stigmata button remains visible"),Open->IsVisible());
    Open->OnHovered.Broadcast();
    Test->TestTrue(TEXT("Reconstructed stigmata preview responds"),Preview->IsVisible());
    Open->OnUnhovered.Broadcast();
    bCollectionCaptured=true;
   }
   const int32 Goal=Cards->GetRules().DeploymentLimit;
   const int32 PurchaseCost=Cards->GetRules().PurchasePrice;
   const int64 FuturePopulationReserve=static_cast<int64>(FMath::Max(0,5-Goal))*PurchaseCost;
   int32 Attempts=0;
   while(Attempts++<100&&!(Stage==1&&StageOnePreparationFrames>0))
   {
    bool bProgress=false;
    // Existing matching cards cost nothing to merge and keep their deployed identity.
    for(const auto& Card:Cards->GetHand())
    {
     if(!Card.Definition||!Card.Definition->HeroDefinition)continue;
     const FGuid HeroId=Mode->GetRoster()->FindHeroByDefinition(Card.Definition->HeroDefinition);
     FEGHeroInstance Hero;
     if(HeroId.IsValid()&&Mode->GetRoster()->GetHero(HeroId,Hero)&&Hero.bDeployed&&Cards->TryMergeIntoHero(Card.InstanceId,HeroId))
     {bProgress=true;break;}
    }
    if(bProgress)continue;
    if(Cards->GetPieces().Num()<Goal)
    {
     TArray<FEGCardInstance> OrderedHand=Cards->GetHand();
     if(Stage==7&&Route==EStigmataRoute::Herrscher)
      OrderedHand.StableSort([](const FEGCardInstance& A,const FEGCardInstance& B)
      {
       const bool AH=A.Definition&&A.Definition->HeroDefinition&&A.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;
       const bool BH=B.Definition&&B.Definition->HeroDefinition&&B.Definition->HeroDefinition->HeroKind==EEGHeroKind::ElysiaHerrscher;
       return AH&&!BH;
      });
     for(const auto& Card:OrderedHand)
     {
      if(!Card.Definition||!Card.Definition->HeroDefinition||Mode->GetRoster()->FindHeroByDefinition(Card.Definition->HeroDefinition).IsValid())continue;
      for(int32 Y=0;Y<3&&!bProgress;++Y)
       for(int32 X=0;X<7&&!bProgress;++X)
        if(Cards->EvaluateHandPlacement(Card.InstanceId,{X,Y}).IsAllowed())bProgress=Cards->TryDeployHandCard(Card.InstanceId,{X,Y});
      if(bProgress)break;
     }
     if(bProgress)continue;
    }
    const bool bNeedPopulation=Cards->GetPieces().Num()<Goal;
    const int64 Reserve=bNeedPopulation?0:FuturePopulationReserve;
    if(Cards->GetGold()>=PurchaseCost+Reserve)
    {
     const auto Offers=Cards->GetShop();
     for(int32 Slot=0;Slot<Offers.Num();++Slot)
     {
      const auto* Definition=Offers[Slot];
      if(!Definition||!Definition->HeroDefinition)continue;
      const FGuid HeroId=Mode->GetRoster()->FindHeroByDefinition(Definition->HeroDefinition);
      FEGHeroInstance Hero;
      const bool bDeployed=HeroId.IsValid()&&Mode->GetRoster()->GetHero(HeroId,Hero)&&Hero.bDeployed;
      const bool bAlreadyInHand=Cards->GetHand().ContainsByPredicate([&](const FEGCardInstance& Card){return Card.Definition==Definition;});
      if((bNeedPopulation&&(HeroId.IsValid()||bAlreadyInHand))||(!bNeedPopulation&&!bDeployed))continue;
      if(Cards->BuyShopSlot(Slot)){bProgress=true;break;}
     }
    }
    if(bProgress)continue;
    if(Cards->GetGold()<PurchaseCost+Reserve+Cards->GetRefreshCost()||!Cards->RefreshShop())break;
   }
   const int32 Deployed=Cards->GetPieces().Num();
   if(Stage==1&&!bStageOnePreparationCaptured)
   {
    if(StageOnePreparationFrames==0)
    {
     StageOnePreparationFrames=1;
     auto* Panel=Mode->GetDemoPanel();
     if(!Test->TestNotNull(TEXT("PC run panel exists"),Panel))return true;
     auto* Clock=Panel->GetWidgetFromName(TEXT("PhaseClockRoot"));
     auto* Left=Panel->GetWidgetFromName(TEXT("PhaseClockLeft"));
     auto* Right=Panel->GetWidgetFromName(TEXT("PhaseClockRight"));
     if(!Test->TestNotNull(TEXT("PC phase clock exists"),Clock)||
        !Test->TestNotNull(TEXT("PC left clock line exists"),Left)||
        !Test->TestNotNull(TEXT("PC right clock line exists"),Right))return true;
     Test->TestTrue(TEXT("PC phase clock is visible"),Clock->IsVisible());
     if(const auto* Slot=Cast<UCanvasPanelSlot>(Clock->Slot))
      Test->TestEqual(TEXT("Clock root uses 660 logical pixels"),Slot->GetSize().X,660.0);
     else Test->AddError(TEXT("PC phase clock is missing its canvas slot"));
     Test->TestEqual(TEXT("Left clock line retracts from its inner end"),Left->GetRenderTransformPivot(),FVector2D(1,.5));
     Test->TestEqual(TEXT("Right clock line retracts from its inner end"),Right->GetRenderTransformPivot(),FVector2D(0,.5));
     Test->TestTrue(TEXT("Left clock line tracks countdown"),FMath::IsNearlyEqual(Left->GetRenderTransform().Scale.X,Mode->GetPhaseClock().Normalized,.05f));
     Test->TestTrue(TEXT("Right clock line tracks countdown"),FMath::IsNearlyEqual(Right->GetRenderTransform().Scale.X,Mode->GetPhaseClock().Normalized,.05f));
     for(const TCHAR* Name:{TEXT("PhaseClockNumber"),TEXT("PhaseClockStage"),TEXT("PhaseClockCorruption")})
      Test->TestNotNull(TEXT("PC clock text exists"),Panel->GetWidgetFromName(FName(Name)));
     if(FApp::CanEverRender())
     {
      auto* Client=World->GetGameViewport();
      if(!Test->TestNotNull(TEXT("Preparation screenshot game viewport client"),Client))return true;
      PreparationCapture=MakeShared<FPreparationDrawCapture>();
      PreparationCapture->Client=Client;
      PreparationCapture->Name=FString::Printf(TEXT("PCUI_%s_Preparation_%s"),RouteName(Route),*FGuid::NewGuid().ToString(EGuidFormats::Digits));
      PreparationCapture->Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Screenshots")/(PreparationCapture->Name+TEXT(".png")));
      const TWeakPtr<FPreparationDrawCapture> WeakCapture=PreparationCapture;
      const TWeakObjectPtr<UWorld> WeakWorld=World;
      PreparationCapture->DrawHandle=Client->OnDrawn().AddLambda([WeakCapture,WeakWorld,InTest=Test]()
      {
       const auto Capture=WeakCapture.Pin();
       if(!Capture.IsValid())return;
       ++Capture->Draws;
       if(Capture->Draws==3&&WeakWorld.IsValid())
       {
        Capture->bRequested=true;
        RequestGameScreenshot(WeakWorld.Get(),*Capture->Name,InTest);
       }
      });
     }
    }
    if(PreparationCapture.IsValid())
    {
     if(!PreparationCapture->bRequested||PreparationCapture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
     if(!HasExpectedPngDimensions(PreparationCapture->Path,RouteViewportSize(Route),Test))return false;
     PreparationCapture.Reset();
    }
    bStageOnePreparationCaptured=true;
    if(bPreparationOnly)return true;
   }
   if(Stage==6)Test->TestEqual(TEXT("Stage six uses normal population"),Goal,5);
   if(Stage==7)Test->TestEqual(TEXT("Expected final population limit"),Goal,Route==EStigmataRoute::Nine?9:5);
   if(Stage==7&&Route!=EStigmataRoute::Herrscher&&!Test->TestEqual(TEXT("Final battle has the required deployed team"),Deployed,Route==EStigmataRoute::Nine?9:5))return true;
   Test->AddInfo(FString::Printf(TEXT("Natural stage %d starts with %d/%d deployed units, corruption %d, remaining currency %lld."),Stage,Deployed,Goal,Match->GetCorruption(),Cards->GetGold()));
   if(!Test->TestTrue(TEXT("Natural stage enters combat"),Mode->EnterCombat()))return true;
   if(Stage==7)
   {
    if(Route==EStigmataRoute::Herrscher)
    {
     bool bHumanDeployed=false;
     for(const auto& Hero:Mode->GetRoster()->GetHeroes())
      if(Hero.bDeployed&&Hero.Definition&&Hero.Definition->HeroKind==EEGHeroKind::ElysiaHerrscher)bHumanDeployed=true;
     Test->TestTrue(TEXT("Fused Herrscher remains deployed after normal purchases"),bHumanDeployed);
    }
    for(AEGChessUnit* Unit:Mode->GetCombatUnits())
    {
     if(!Unit||!Unit->GetDefinition())continue;
     if(Unit->GetFaction_Implementation()==EEGFaction::Enemy&&Unit->GetDefinition()->HeroId==FName(TEXT("ElysiaCorruption")))BossUnit=Unit;
     if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)HumanUnit=Unit;
     if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetDefinition()->HeroKind==EEGHeroKind::ElysiaHerrscher)
     {
      const FEGStatBlock Evaluated=Unit->GetDefinition()->EvaluateStats(Unit->GetHeroSkill()->GetLevel());
      auto* ASC=Unit->GetAbilitySystemComponent();
      if(Test->TestNotNull(TEXT("Fused human has an ability system"),ASC))
      {
       Test->TestTrue(TEXT("Human ASC base maximum health matches level"),FMath::IsNearlyEqual(ASC->GetNumericAttributeBase(UEGAttributeSet::GetMaxHealthAttribute()),Evaluated.MaxHealth,.01f));
       Test->TestTrue(TEXT("Human ASC base attack matches level"),FMath::IsNearlyEqual(ASC->GetNumericAttributeBase(UEGAttributeSet::GetAttackPowerAttribute()),Evaluated.AttackPower,.01f));
       Test->TestTrue(TEXT("Human ASC base defense matches level"),FMath::IsNearlyEqual(ASC->GetNumericAttributeBase(UEGAttributeSet::GetDefenseAttribute()),Evaluated.Defense,.01f));
      }
     }
     if(Unit->GetFaction_Implementation()==EEGFaction::Player&&Unit->GetAttributes())
      Test->AddInfo(FString::Printf(TEXT("Final player: %s level %d, health %.1f/%.1f, attack %.1f."),
       *Unit->GetDefinition()->HeroId.ToString(),Unit->GetHeroSkill()->GetLevel(),Unit->GetAttributes()->GetHealth(),
       Unit->GetAttributes()->GetMaxHealth(),Unit->GetAttributes()->GetAttackPower()));
    }
   }
   LastStage=Stage;return false;
  }
  if(Phase==EEGMatchPhase::Combat)
  {
   if(!Mode->BattleRules||!Mode->BattleRules->IsCombatRunning())return false;
   if(VerifiedCopyStage!=Stage){VerifyCopies(Test,Mode,Stage);VerifiedCopyStage=Stage;}
   if(Route==EStigmataRoute::Nine&&Stage==7&&FApp::CanEverRender())
   {
    if(DenseCapture.IsValid())
    {
     if(!DenseCapture->bRequested||DenseCapture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
     if(!HasExpectedPngDimensions(DenseCapture->Path,RouteViewportSize(Route),Test))return false;
     if(bCaptureIsCamera){bCameraCombatCaptured=true;RestoreCamera();}
     else bDenseCombatCaptured=true;
     DenseCapture.Reset();
    }
    if(!bDenseCombatCaptured&&Mode->GetBattleSeconds()>=10.f)
    {
     bCaptureIsCamera=false;MarkScreenshot();
     DenseCapture=StartGameDrawCapture(World,TEXT("PCUI_Nine_DenseCombat"),Test);
     if(!Test->TestTrue(TEXT("Dense combat capture has game viewport"),DenseCapture.IsValid()))return true;
     return false;
    }
    if(bDenseCombatCaptured&&!bCameraCombatCaptured&&Mode->GetBattleSeconds()>=10.2f)
    {
     auto* Controller=Cast<AEGBoardPlayerController>(World->GetFirstPlayerController());
     auto* Scene=Controller?Controller->GetSceneSettings():nullptr;
     auto* Camera=Scene?Scene->FixedCamera.Get():nullptr;
     if(Camera&&Controller->GetViewTarget()==Camera&&Camera->GetCameraComponent())
     {
      ModifiedCamera=Camera;OriginalCameraTransform=Camera->GetActorTransform();
      OriginalCameraFOV=Camera->GetCameraComponent()->FieldOfView;bCameraModified=true;
      Camera->SetActorLocation(Camera->GetActorLocation()+Camera->GetActorRightVector()*50.f);
      Camera->GetCameraComponent()->SetFieldOfView(OriginalCameraFOV*1.04f);
      Test->AddInfo(TEXT("Camera capture temporarily pans the active board camera 50 world units and widens its FOV by 4%; it is restored after the capture."));
     }
     else Test->AddWarning(TEXT("Active board camera was unavailable; camera combat screenshot uses the current view."));
     bCaptureIsCamera=true;MarkScreenshot();
     DenseCapture=StartGameDrawCapture(World,TEXT("PCUI_Nine_CameraCombat"),Test);
     if(!Test->TestTrue(TEXT("Camera combat capture has game viewport"),DenseCapture.IsValid()))return true;
     return false;
    }
   }
   if(Stage==1&&!bStageOneCombatCaptured&&Mode->GetBattleSeconds()>=.5f)
   {
    bStageOneCombatCaptured=true;
    int32 AllyVitals=0,EnemyVitals=0;
    for(AEGChessUnit* Unit:Mode->GetCombatUnits())
    {
     if(!IsValid(Unit)||!Unit->IsAlive_Implementation()||Unit->IsAuxiliaryEmplacement()||Unit->GetHeroSkill()->bSummon)continue;
     auto* Component=Unit->FindComponentByClass<UWidgetComponent>();
     auto* Vitals=Component?Cast<UEGUnitVitals>(Component->GetUserWidgetObject()):nullptr;
     if(!Test->TestNotNull(TEXT("Living combat unit has vitals widget"),Vitals))continue;
     if(Unit->GetFaction_Implementation()==EEGFaction::Player)++AllyVitals;
     if(Unit->GetFaction_Implementation()==EEGFaction::Enemy)++EnemyVitals;
     Test->TestEqual(TEXT("Combat vitals are bound"),Vitals->DisplayMode,EEGVitalsMode::Combat);
     if(auto* Health=Vitals->GetHealthBar())
     {
      const auto* Slot=Cast<UCanvasPanelSlot>(Health->Slot);
      if(Test->TestNotNull(TEXT("Health bar canvas slot"),Slot))
       Test->TestEqual(TEXT("Standard health bar size"),Slot->GetSize(),FVector2D(80,10));
     }
     else Test->AddError(TEXT("Living combat unit is missing health bar"));
     if(Unit->GetAttributes()&&Unit->GetAttributes()->GetMaxMana()>0.f)
     {
      auto* Mana=Vitals->GetManaBar();
      const auto* Slot=Mana?Cast<UCanvasPanelSlot>(Mana->Slot):nullptr;
      if(Test->TestNotNull(TEXT("Mana bar canvas slot"),Slot))
       Test->TestEqual(TEXT("Standard mana bar size"),Slot->GetSize(),FVector2D(64,5));
     }
     if(auto* Label=Cast<UTextBlock>(Vitals->GetWidgetFromName(TEXT("LevelLabel"))))
      Test->TestEqual(TEXT("Combat level text is 14 logical pixels"),Label->GetFont().Size,14.f);
     else Test->AddError(TEXT("Living combat unit is missing level label"));
    }
    Test->TestTrue(TEXT("Player health bars are present"),AllyVitals>0);
    Test->TestTrue(TEXT("Enemy health bars are present"),EnemyVitals>0);
    const FString Name=FString::Printf(TEXT("PCUI_%s_FirstCombat"),RouteName(Route));
    RequestGameScreenshot(World,*Name,Test);
    return false;
   }
   if(Route==EStigmataRoute::Five&&Stage==3&&!bCopyScreenshotRequested&&Mode->GetBattleSeconds()>=.5f)
   {
    bCopyScreenshotRequested=true;
    RequestGameScreenshot(World,TEXT("SevenStageNaturalCopyBattle"),Test);
   }
   if(Stage==7&&!bBossScreenshotRequested)
   {
    bBossScreenshotRequested=true;
    MarkScreenshot();
    const FString Name=FString::Printf(TEXT("SevenStageNaturalBoss_%s"),RouteName(Route));
    RequestBossHighResScreenshot(World,*Name,Test);
    return false;
   }
   if(Stage==7&&!bBossUICaptured&&Mode->GetBattleSeconds()>=.5f)
   {
    bBossUICaptured=true;
    MarkScreenshot();
    const FString Name=FString::Printf(TEXT("PCUI_%s_FinalCombat"),RouteName(Route));
    RequestGameScreenshot(World,*Name,Test);
    return false;
   }
   if(Route==EStigmataRoute::Herrscher&&Stage==7&&!bHerrscherBattleScreenshotRequested&&Mode->GetBattleSeconds()>=.5f)
   {
    bHerrscherBattleScreenshotRequested=true;
    MarkScreenshot();
    RequestGameScreenshot(World,TEXT("SevenStageNaturalHerrscherCombat"),Test);
    return false;
   }
   if(Route==EStigmataRoute::Five&&Stage==7&&!bBossPhaseScreenshotRequested&&BossUnit.IsValid())
    if(const auto* Corruption=BossUnit->FindComponentByClass<UEGCorruptionCombatComponent>();Corruption&&Corruption->IsPhaseTwo())
    {
     bBossPhaseScreenshotRequested=true;
     MarkScreenshot();
     RequestGameScreenshot(World,TEXT("SevenStageNaturalBossPhaseTwo"),Test);
    }
   return false;
  }
  if(Phase==EEGMatchPhase::Settlement)
  {
   if(Route==EStigmataRoute::Five&&Stage==1&&!bSettlementScreenshotRequested)
   {
    bSettlementScreenshotRequested=true;
    RequestGameScreenshot(World,TEXT("SevenStageNaturalSettlement"),Test);
   }
   return false;
  }
  if(Phase==EEGMatchPhase::Resolution)return false;
  if(Phase==EEGMatchPhase::Reward)
  {
   if(WaitForRewardScreenshot(World,Test,Stage,Route,TEXT("Natural"),CapturedRewardStage,RewardFrames))return false;
   if(!ChooseReward(Test,Mode,Stage,Route,true))return true;
   return false;
  }
  if(Phase!=EEGMatchPhase::Finished)return false;
  WritePerformance();
  if(BossUnit.IsValid()&&BossUnit->GetAttributes())
  {
   const auto* BossRules=BossUnit->FindComponentByClass<UEGCorruptionCombatComponent>();
   Test->AddInfo(FString::Printf(TEXT("Final battle %.1f seconds, boss health %.1f/%.1f, phase two %d."),
    Mode->GetBattleSeconds(),BossUnit->GetAttributes()->GetHealth(),BossUnit->GetAttributes()->GetMaxHealth(),BossRules&&BossRules->IsPhaseTwo()?1:0));
  }
  if(HumanUnit.IsValid()&&HumanUnit->GetAttributes())
   Test->AddInfo(FString::Printf(TEXT("Final Herrscher health %.1f/%.1f."),HumanUnit->GetAttributes()->GetHealth(),HumanUnit->GetAttributes()->GetMaxHealth()));
  Test->TestEqual(TEXT("Natural run reaches final combat"),LastStage,7);
  Test->TestTrue(TEXT("Natural run wins final combat"),Match->DidWinRun());
  if(Route==EStigmataRoute::Nine&&FApp::CanEverRender())
  {
   if(!bDenseCombatCaptured)Test->AddWarning(TEXT("Boss combat ended before the 10-second dense screenshot completed."));
   if(!bCameraCombatCaptured)Test->AddWarning(TEXT("Boss combat ended before the 10.2-second camera screenshot completed."));
  }
  Test->AddInfo(FString::Printf(TEXT("Natural result: final stage %d, victory %d, corruption %d."),LastStage,Match->DidWinRun()?1:0,Match->GetCorruption()));
  if(Route==EStigmataRoute::Herrscher)
  {
   TWeakObjectPtr<UEGHeroDefinition> RunHuman=HumanUnit.IsValid()?HumanUnit->GetDefinition():nullptr;
   if(Test->TestTrue(TEXT("Restart after human route"),Mode->AdvanceDemo()))
   {
    Test->TestTrue(TEXT("Restart clears fused roster"),Mode->GetRoster()->GetHeroes().IsEmpty());
    Test->TestTrue(TEXT("Restart clears stigmata"),Match->GetStigmataOwned().IsEmpty());
    Test->TestTrue(TEXT("Restart removes human combat incarnation"),!HumanUnit.IsValid()||HumanUnit->IsActorBeingDestroyed());
    for(const auto& Card:Cards->GetHand())
     Test->TestTrue(TEXT("Restart hand does not hold private human"),!Card.Definition||Card.Definition->HeroDefinition.Get()!=RunHuman.Get());
   }
  }
  return true;
 }
};
class FEGPIEEmptyDeployment final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.;
 bool bStarted=false;
public:
 explicit FEGPIEEmptyDeployment(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+90.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Empty deployment settlement timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards())return false;
  auto* Match=Mode->GetMatch();
  if(!bStarted)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(!Test->TestTrue(TEXT("First stage starts with no deployed pieces"),Mode->GetCards()->GetPieces().IsEmpty()))return true;
   if(!Test->TestEqual(TEXT("Starting corruption"),Match->GetCorruption(),50))return true;
   if(!Test->TestTrue(TEXT("Empty deployment resolves as a loss"),Mode->EnterCombat()))return true;
   Test->TestEqual(TEXT("Loss starts settlement"),Match->GetPhase(),EEGMatchPhase::Settlement);
   Test->TestEqual(TEXT("Corruption is unchanged before settlement impact"),Match->GetCorruption(),50);
   bStarted=true;
   return false;
  }
  if(Match->GetPhase()==EEGMatchPhase::Settlement)return false;
  if(Match->GetPhase()==EEGMatchPhase::Resolution&&Match->GetStageNumber()==1)return false;
  Test->TestEqual(TEXT("Empty deployment applies one loss delta"),Match->GetCorruption(),55);
  Test->TestFalse(TEXT("Empty deployment cannot win the run"),Match->DidWinRun());
  Test->TestTrue(TEXT("Empty deployment continues to reward or the next stage"),
   Match->GetPhase()==EEGMatchPhase::Reward || Match->GetStageNumber()==2);
  return true;
 }
};
class FEGPIEPhasePause final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.,WaitUntil=0.;
 double PreparationResumeWall=0.,PreparationResumeAudio=0.;
 double OpeningResumeWall=0.,OpeningResumeAudio=0.;
 int32 Step=0;
 float PreparationBeforePause=0.f,OpeningBeforePause=0.f;
 FGuid OpeningRound;
public:
 explicit FEGPIEPhasePause(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+90.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Phase pause PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards()||!Mode->BattleRules)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   auto* Cards=Mode->GetCards();
   UEGCardDefinition* Griseo=nullptr;
   for(const auto& Card:Mode->RunDefinition->CardPool)
    if(Card&&Card->HeroDefinition&&Card->HeroDefinition->HeroKind==EEGHeroKind::Griseo){Griseo=Card.Get();break;}
   if(!Test->TestNotNull(TEXT("Opening pause fixture has Griseo"),Griseo)||
      !Test->TestTrue(TEXT("Grant controlled Griseo fixture"),Cards->GrantCard(Griseo)))return true;
   if(!Test->TestTrue(TEXT("Deploy controlled Griseo fixture"),Cards->TryDeployHandCard(Cards->GetHand().Last().InstanceId,FIntPoint(3,1))))return true;
   PreparationBeforePause=Mode->GetPhaseClock().RemainingRealSeconds;
   if(!Test->TestTrue(TEXT("Pause preparation"),UGameplayStatics::SetGamePaused(World,true)))return true;
   WaitUntil=FPlatformTime::Seconds()+1.2;Step=1;return false;
  }
  if(Step==1)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   Test->TestTrue(TEXT("Paused preparation clock is frozen"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,PreparationBeforePause,.05f));
   if(!Test->TestTrue(TEXT("Resume preparation"),UGameplayStatics::SetGamePaused(World,false)))return true;
   PreparationResumeWall=FPlatformTime::Seconds();PreparationResumeAudio=World->GetAudioTimeSeconds();
   Step=2;return false;
  }
  if(Step==2)
  {
   const double WallElapsed=FPlatformTime::Seconds()-PreparationResumeWall;
   const double AudioElapsed=World->GetAudioTimeSeconds()-PreparationResumeAudio;
   if(AudioElapsed<.2&&WallElapsed<10.)return false;
   Test->AddInfo(FString::Printf(TEXT("Preparation resume observation: wall=%.3fs audio=%.3fs paused=%d before=%.3fs after=%.3fs"),
    WallElapsed,AudioElapsed,World->IsPaused()?1:0,PreparationBeforePause,Mode->GetPhaseClock().RemainingRealSeconds));
   Test->TestTrue(TEXT("Unpaused preparation world clock advances"),AudioElapsed>=.2);
   Test->TestTrue(TEXT("Preparation clock resumes"),Mode->GetPhaseClock().RemainingRealSeconds<PreparationBeforePause-.05f);
   if(!Test->TestTrue(TEXT("Enter combat before preparation expires"),Mode->EnterCombat()))return true;
   if(!Test->TestFalse(TEXT("Griseo opening is pending"),Mode->BattleRules->IsCombatRunning()))return true;
   OpeningRound=Mode->BattleRules->GetRoundId();
   OpeningBeforePause=Mode->GetPhaseClock().RemainingRealSeconds;
   if(!Test->TestTrue(TEXT("Pause during opening"),UGameplayStatics::SetGamePaused(World,true)))return true;
   WaitUntil=FPlatformTime::Seconds()+1.2;Step=3;return false;
  }
  if(Step==3)
  {
   if(FPlatformTime::Seconds()<WaitUntil)return false;
   Test->TestFalse(TEXT("Paused opening did not finish"),Mode->BattleRules->IsCombatRunning());
   Test->TestEqual(TEXT("Paused opening has no effective battle time"),Mode->GetBattleSeconds(),0.f);
   Test->TestTrue(TEXT("Paused opening phase clock is frozen"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,OpeningBeforePause,.05f));
   if(!Test->TestTrue(TEXT("Resume opening"),UGameplayStatics::SetGamePaused(World,false)))return true;
   OpeningResumeWall=FPlatformTime::Seconds();OpeningResumeAudio=World->GetAudioTimeSeconds();
   Step=4;return false;
  }
  if(Step==4)
  {
   const double WallElapsed=FPlatformTime::Seconds()-OpeningResumeWall;
   const double AudioElapsed=World->GetAudioTimeSeconds()-OpeningResumeAudio;
   if(AudioElapsed<.2&&WallElapsed<10.)return false;
   Test->AddInfo(FString::Printf(TEXT("Opening resume observation: wall=%.3fs audio=%.3fs paused=%d running=%d battle=%.3fs"),
    WallElapsed,AudioElapsed,World->IsPaused()?1:0,
    Mode->BattleRules->IsCombatRunning()?1:0,Mode->GetBattleSeconds()));
   Test->TestTrue(TEXT("Unpaused opening world clock advances"),AudioElapsed>=.2);
   Test->TestFalse(TEXT("Opening retains remaining time after pause"),Mode->BattleRules->IsCombatRunning());
   Test->TestEqual(TEXT("Opening still excludes effective battle time"),Mode->GetBattleSeconds(),0.f);
   Step=5;return false;
  }
  if(!Mode->BattleRules->IsCombatRunning())return false;
  Test->TestTrue(TEXT("Opening preserves the normal battle clock"),FMath::IsNearlyEqual(Mode->GetPhaseClock().RemainingRealSeconds,OpeningBeforePause,.25f));
  if(!Test->TestTrue(TEXT("End controlled combat"),Mode->ResolveCombat(false)))return true;
  Mode->BattleRules->OnCombatRunning.Broadcast(OpeningRound);
  Test->TestFalse(TEXT("Stale opening callback cannot restart stopped battle"),Mode->BattleRules->IsCombatRunning());
  Test->TestEqual(TEXT("Stale opening callback preserves settlement"),Mode->GetMatch()->GetPhase(),EEGMatchPhase::Settlement);
  return true;
 }
};
class FEGPIEEmptyFusion final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.;
public:
 explicit FEGPIEEmptyFusion(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Empty fusion route timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards()||!Mode->GetRoster())return false;
  auto* Match=Mode->GetMatch();
  auto* Cards=Mode->GetCards();
  const int32 Stage=Match->GetStageNumber();
  const EEGMatchPhase Phase=Match->GetPhase();
  if(Phase==EEGMatchPhase::Preparation)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(!Test->TestTrue(TEXT("Empty fusion route has no deployed pieces"),Cards->GetPieces().IsEmpty()))return true;
   if(!Test->TestTrue(TEXT("Empty formation resolves as loss"),Mode->EnterCombat()))return true;
   return false;
  }
  if(Phase!=EEGMatchPhase::Reward)return false;
  if(Stage==2||Stage==4)
  {
   if(!ChooseReward(Test,Mode,Stage,EStigmataRoute::Five,false))return true;
   return false;
  }
  if(!Test->TestEqual(TEXT("Final choice follows stage six"),Stage,6))return true;
  if(!Mode->IsChoosingEndgameRoute())
  {
   Test->AddError(TEXT("Stage six must directly enter final route selection"));return true;
  }
  Test->TestTrue(TEXT("Stage six has no ordinary offer"),Mode->GetStigmataOffer().IsEmpty());
  const int32 Revision=Mode->GetStigmataRevision();
  const int32 HandBefore=Cards->GetHand().Num();
  const int64 GoldBefore=Cards->GetGold();
  const int32 OwnedBefore=Match->GetStigmataOwned().Num();
  const FName Origin(TEXT("OriginFlower"));
  if(!Test->TestTrue(TEXT("Origin option is offered with empty formation"),Mode->GetEndgameRouteOffer().Contains(Origin)))return true;
  Test->TestFalse(TEXT("Fusion rejects no deployed heroes"),Mode->ChooseEndgameRoute(Origin,Revision));
  Test->TestEqual(TEXT("Failed fusion preserves revision"),Mode->GetStigmataRevision(),Revision);
  Test->TestEqual(TEXT("Failed fusion preserves hand"),Cards->GetHand().Num(),HandBefore);
  Test->TestEqual(TEXT("Failed fusion preserves gold"),Cards->GetGold(),GoldBefore);
  Test->TestEqual(TEXT("Failed fusion preserves ownership"),Match->GetStigmataOwned().Num(),OwnedBefore);
  Test->TestTrue(TEXT("Failed fusion preserves unchosen route"),Match->GetEndgameRoute().IsNone());
  Test->TestEqual(TEXT("Failed fusion remains in choice"),Match->GetPhase(),EEGMatchPhase::Reward);
  Test->TestTrue(TEXT("Failed fusion explains that no heroes are deployed"),Mode->GetHint().Contains(TEXT("没有上阵英桀")));
  if(!Test->TestTrue(TEXT("Alternative story can still be chosen"),Mode->ChooseEndgameRoute(TEXT("Story"),Revision)))return true;
  Test->TestEqual(TEXT("Story is recorded separately from ordinary stigmata"),Match->GetEndgameRoute(),FName(TEXT("Story")));
  Test->TestEqual(TEXT("Alternative grants nine cards"),Cards->GetHand().Num(),HandBefore+9);
  Test->TestEqual(TEXT("Alternative grants nine population"),Cards->GetRules().DeploymentLimit,9);
  return true;
 }
};
/** Controlled campaign to the first reward; synthetic Slate input is not OS input. */
class FEGStigmataPointerPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 const FIntPoint ViewportSize;
 double Deadline=0.,Until=0.;
 int32 Step=0,Revision=INDEX_NONE,OwnedBefore=0;
 FName Chosen;
 FVector2D Cursor=FVector2D::ZeroVector,Inside=FVector2D::ZeroVector;
 TWeakObjectPtr<UButton> Candidate;
 bool bPointerDown=false;
 bool bRewardCaptured=false,bFocusLostDuringHold=false,bInactiveDuringHold=false,bRoutePageObserved=false;
 int32 ObservedRouteRevision=INDEX_NONE;
 double RouteReadyWorldTime=0.;
 double HoldStartWorldSeconds=0.,HoldStartWallSeconds=0.;
 double ShortPressStartWall=0.;
 uint64 HoldStartFrame=0;
 int32 HoldChoiceIndex=INDEX_NONE;
 TSharedPtr<FPreparationDrawCapture> RewardCapture;
 TSharedPtr<FPreparationDrawCapture> SuccessCapture;
 TSharedPtr<FPreparationDrawCapture> HumanRouteCapture;
 TSharedPtr<FPreparationDrawCapture> HumanHoldCapture;
 FDelegateHandle FocusTraceHandle;
 TWeakObjectPtr<UEGDemoPanel> FocusPanel;
 UAudioComponent* HoldAudio(UEGDemoPanel* Panel) const
 {
  auto* Audio=UEGGameAudioComponent::FindFor(Panel);
  auto* Property=FindFProperty<FObjectPropertyBase>(UEGGameAudioComponent::StaticClass(),TEXT("StigmataHoldVoice"));
  return Audio&&Property?Cast<UAudioComponent>(Property->GetObjectPropertyValue_InContainer(Audio)):nullptr;
 }
 void Move(FVector2D Position,bool bHeld)
 {
  auto& App=FSlateApplication::Get();App.SetCursorPos(Position);
  const TSet<FKey> Buttons=bHeld?TSet<FKey>{EKeys::LeftMouseButton}:TSet<FKey>{};
  App.ProcessMouseMoveEvent(FPointerEvent(0,Position,Cursor,Buttons,EKeys::Invalid,0,FModifierKeysState()),false);
  Cursor=Position;
 }
 bool Down()
 {
  if(!Candidate.IsValid())return false;
  auto& App=FSlateApplication::Get();
  const TSharedPtr<SWindow> Window=App.FindWidgetWindow(Candidate->TakeWidget());
  if(!Window.IsValid())return false;
  const bool bHandled=App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
   FPointerEvent(0,Cursor,Cursor,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
  bPointerDown=true;return bHandled;
 }
 void Up()
 {
  if(!bPointerDown)return;
  FSlateApplication::Get().ProcessMouseButtonUpEvent(
   FPointerEvent(0,Cursor,Cursor,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
  bPointerDown=false;
 }
 void StartMouseHoldTrace(UWorld* World,int32 Index)
 {
  HoldChoiceIndex=Index;HoldStartWorldSeconds=World?World->GetTimeSeconds():0.;
  HoldStartWallSeconds=FPlatformTime::Seconds();HoldStartFrame=GFrameCounter;
 }
 bool MouseHoldInputValid() const
 {
  return FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive()&&
   bPointerDown&&Candidate.IsValid()&&Candidate->IsPressed()&&Candidate->IsHovered();
 }
 void ReportRoutePreview(const TCHAR* Label,AEGDemoGameMode* Mode,UEGDemoPanel* Panel)
 {
  auto* Details=Panel?Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(TEXT("StigmataChoiceDetailsText"))):nullptr;
  auto* Close=Panel?Panel->GetWidgetFromName(TEXT("Button_CloseStigmataChoiceDetails")):nullptr;
  auto* Layout=Close?Close->GetParent():nullptr;
  auto* DetailsPanel=Layout?Layout->GetParent():nullptr;
  const TArray<FName> Routes=Mode?Mode->GetEndgameRouteOffer():TArray<FName>();
  Test->AddInfo(FString::Printf(TEXT("Route preview %s: frame=%llu route=%d revision=%d index=%d offer=%s candidate=%s pressed=%d hovered=%d pointerDown=%d detailsPresent=%d panelVisible=%d text=%s"),
   Label,(unsigned long long)GFrameCounter,Mode&&Mode->IsChoosingEndgameRoute()?1:0,Mode?Mode->GetStigmataRevision():INDEX_NONE,
   HoldChoiceIndex,Routes.IsValidIndex(HoldChoiceIndex)?*Routes[HoldChoiceIndex].ToString():TEXT("<invalid>"),
   Candidate.IsValid()?*Candidate->GetName():TEXT("<missing>"),Candidate.IsValid()&&Candidate->IsPressed()?1:0,
   Candidate.IsValid()&&Candidate->IsHovered()?1:0,bPointerDown?1:0,Details?1:0,DetailsPanel&&DetailsPanel->IsVisible()?1:0,
   Details?*Details->GetPlainText().ToString():TEXT("<missing>")));
 }
 void ReportMouseHold(const TCHAR* Label,UWorld* World,AEGDemoGameMode* Mode,UEGDemoPanel* Panel)
 {
  const FName RingName(*FString::Printf(TEXT("StigmataHoldRing%d"),HoldChoiceIndex));
  const FName PercentName(*FString::Printf(TEXT("StigmataHoldLabel%d"),HoldChoiceIndex));
  auto* Ring=Panel?Panel->GetWidgetFromName(RingName):nullptr;
  auto* Percent=Panel?Cast<UTextBlock>(Panel->GetWidgetFromName(PercentName)):nullptr;
  auto* Details=Panel?Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(TEXT("StigmataChoiceDetailsText"))):nullptr;
  const auto* Match=Mode?Mode->GetMatch():nullptr;
  Test->AddInfo(FString::Printf(TEXT("%s: wall=%.3fs world=%.3fs frames=%llu active=%d pointerDown=%d pressed=%d hovered=%d ringVisible=%d percent=%s phase=%d revision=%d owned=%d hint=%s details=%s"),
   Label,FPlatformTime::Seconds()-HoldStartWallSeconds,World?World->GetTimeSeconds()-HoldStartWorldSeconds:-1.,
   (unsigned long long)(GFrameCounter-HoldStartFrame),
   FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive()?1:0,bPointerDown?1:0,
   Candidate.IsValid()&&Candidate->IsPressed()?1:0,Candidate.IsValid()&&Candidate->IsHovered()?1:0,
   Ring&&Ring->IsVisible()?1:0,Percent?*Percent->GetText().ToString():TEXT("<missing>"),
   Match?int32(Match->GetPhase()):-1,Mode?Mode->GetStigmataRevision():INDEX_NONE,
   Match?Match->GetStigmataOwned().Num():INDEX_NONE,Mode?*Mode->GetHint():TEXT("<missing>"),
   Details?*Details->GetPlainText().ToString():TEXT("<missing>")));
 }
public:
 explicit FEGStigmataPointerPIE(FAutomationTestBase* InTest,FIntPoint InViewportSize=FIntPoint(1600,900))
  :Test(InTest),ViewportSize(InViewportSize){}
 ~FEGStigmataPointerPIE()
 {
  if(FSlateApplication::IsInitialized())
  {
   if(bPointerDown)Up();
   if(FocusTraceHandle.IsValid())FSlateApplication::Get().OnFocusChanging().Remove(FocusTraceHandle);
  }
 }
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Stigmata pointer PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards())return false;
  auto* Match=Mode->GetMatch();
  if(Step==0)
  {
   if(Match->GetPhase()==EEGMatchPhase::Preparation)
   {
    if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
    if(!Test->TestTrue(TEXT("Empty fixture enters combat"),Mode->EnterCombat()))return true;
    return false;
   }
   if(Match->GetPhase()!=EEGMatchPhase::Reward)return false;
   if(!Test->TestEqual(TEXT("First reward after stage two"),Match->GetStageNumber(),2))return true;
   auto* Panel=Mode->GetDemoPanel();
   if(!Panel||!Panel->GetWidgetFromName(TEXT("StigmaSkipPanel"))||
      !Panel->GetWidgetFromName(TEXT("StigmaSkipPanel"))->IsVisible())return false;
   Candidate=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataChoice0")));
   if(!Test->TestTrue(TEXT("First choice visible"),Candidate.IsValid()&&Candidate->IsVisible()))return true;
   if(Candidate->GetCachedGeometry().GetLocalSize().IsNearlyZero())return false;
   if(!FSlateApplication::Get().IsActive())return false;
   if(!bRewardCaptured)
   {
    if(!RewardCapture.IsValid())
    {
     const FString CaptureName=FString::Printf(TEXT("StigmataSelectionFirstReward_%dx%d"),ViewportSize.X,ViewportSize.Y);
     RewardCapture=StartGameDrawCapture(World,*CaptureName,Test);
     if(!RewardCapture.IsValid()){Test->AddError(TEXT("Stigmata screenshot lacks game viewport"));return true;}
     return false;
    }
    if(!RewardCapture->bRequested||RewardCapture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
    if(!HasExpectedPngDimensions(RewardCapture->Path,ViewportSize,Test))return false;
    bRewardCaptured=true;RewardCapture.Reset();
   }
   const TArray<FName> Offers=Mode->GetStigmataOffer();
   if(!Test->TestEqual(TEXT("Three choices presented"),Offers.Num(),3))return true;
   auto* RewardOverlay=Panel->GetWidgetFromName(TEXT("StigmaSkipPanel"));
   const FGeometry& OverlayGeometry=RewardOverlay->GetCachedGeometry();
   const FVector2D OverlayTopLeft=OverlayGeometry.LocalToAbsolute(FVector2D::ZeroVector);
   const FVector2D OverlayBottomRight=OverlayGeometry.LocalToAbsolute(OverlayGeometry.GetLocalSize());
   float LastRight=-FLT_MAX;
   for(int32 Index=0;Index<3;++Index)
   {
    auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),Index))));
    if(!Test->TestTrue(TEXT("Each choice is visible"),Choice&&Choice->IsVisible()))return true;
    const FGeometry& ChoiceGeometry=Choice->GetCachedGeometry();
    if(ChoiceGeometry.GetLocalSize().IsNearlyZero())return false;
    const float Left=ChoiceGeometry.LocalToAbsolute(FVector2D::ZeroVector).X;
    const float Right=ChoiceGeometry.LocalToAbsolute(FVector2D(ChoiceGeometry.GetLocalSize().X,0)).X;
    const FVector2D TopLeft=ChoiceGeometry.LocalToAbsolute(FVector2D::ZeroVector);
    const FVector2D BottomRight=ChoiceGeometry.LocalToAbsolute(ChoiceGeometry.GetLocalSize());
    Test->TestTrue(TEXT("Each choice remains inside the reward viewport"),
     TopLeft.X>=OverlayTopLeft.X-1.f&&TopLeft.Y>=OverlayTopLeft.Y-1.f&&
     BottomRight.X<=OverlayBottomRight.X+1.f&&BottomRight.Y<=OverlayBottomRight.Y+1.f);
    Test->TestTrue(TEXT("Choices appear left to right without overlap"),Left>=LastRight-1.f);
    LastRight=Right;
    // Check the resolved original texture and painted proportions after five
    // draws for every hero in the actual, shuffled first reward offer.
    const TArray<FString> Heroes=ExpectedStigmataHeroes(Offers[Index]);
    Test->TestTrue(TEXT("Offered stigmata has an expected character mapping"),!Heroes.IsEmpty());
    for(int32 Hero=0;Hero<Heroes.Num();++Hero)
    {
     auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),Index,Hero))));
     auto* Signet=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataSignet%d_%d"),Index,Hero))));
     const FString PortraitPath=StigmataPortraitPath(Heroes[Hero]);
     const FString SignetPath=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelection20261105/T_UI_Signet_%s.T_UI_Signet_%s"),*Heroes[Hero],*Heroes[Hero]);
     const auto* PortraitTexture=StigmataBrushTexture(Portrait);
     const auto* SignetTexture=StigmataBrushTexture(Signet);
     Test->TestTrue(*FString::Printf(TEXT("Choice %d portrait %d uses %s"),Index,Hero,*Heroes[Hero]),
      PortraitTexture&&PortraitTexture->GetPathName()==PortraitPath);
     Test->TestTrue(*FString::Printf(TEXT("Choice %d signet %d uses %s"),Index,Hero,*Heroes[Hero]),
      SignetTexture&&SignetTexture->GetPathName()==SignetPath);
     CheckStigmataDrawnAspect(Test,Portrait,FString::Printf(TEXT("Choice %d member %d portrait"),Index,Hero));
     CheckStigmataDrawnAspect(Test,Signet,FString::Printf(TEXT("Choice %d member %d signet"),Index,Hero));
     CheckStigmataHeroGlow(Test,Panel,Index,Hero,Heroes);
    }
   }
   Chosen=Offers[0];Revision=Mode->GetStigmataRevision();OwnedBefore=Match->GetStigmataOwned().Num();
   const FGeometry& G=Candidate->GetCachedGeometry();Inside=G.LocalToAbsolute(G.GetLocalSize()*.5f);
   Move(Inside,false);Step=1;return false;
  }
  auto* Panel=Mode->GetDemoPanel();
  if(!Test->TestNotNull(TEXT("Reward panel still exists"),Panel))return true;
  if(Step==1)
  {
   if(!Test->TestNotNull(TEXT("Stigmata hold voice property is reflected"),FindFProperty<FObjectPropertyBase>(UEGGameAudioComponent::StaticClass(),TEXT("StigmataHoldVoice"))))return true;
   if(!Test->TestNotNull(TEXT("Local stigmata audio component exists"),UEGGameAudioComponent::FindFor(Panel)))return true;
   if(!GEngine||!GEngine->UseSound())Test->AddInfo(TEXT("Sound disabled: hold voice playback and cancellation are not verified by this run."));
   if(!Test->TestTrue(TEXT("Synthetic pointer hovers choice"),Candidate.IsValid()&&Candidate->IsHovered()))return true;
   if(!Test->TestTrue(TEXT("Synthetic pointer press reaches choice"),Down()&&Candidate->IsPressed()))return true;
    ShortPressStartWall=FPlatformTime::Seconds();Until=ShortPressStartWall+.15;Step=2;return false;
  }
  if(Step==2)
  {
   if(FPlatformTime::Seconds()<Until)return false;
    if(GEngine&&GEngine->UseSound())
    {
     if(FPlatformTime::Seconds()-ShortPressStartWall<.25)
      Test->TestNull(TEXT("Short click does not create a hold audio voice"),HoldAudio(Panel));
     else Test->AddWarning(TEXT("Frame timing exceeded the short-press audio threshold; no short-press audio conclusion."));
    }
    Up();
   Test->TestEqual(TEXT("Short click grants nothing"),Match->GetStigmataOwned().Num(),OwnedBefore);
   auto* Details=Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(TEXT("StigmataChoiceDetailsText")));
   const auto* Definition=UEGStigmataCatalog::Find(Chosen);
   if(!Test->TestTrue(TEXT("Short click displays chosen details"),Details&&Definition&&Details->GetPlainText().ToString().Contains(Definition->Name.ToString())))return true;
   auto* Close=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_CloseStigmataChoiceDetails")));
   auto* Player=World->GetFirstPlayerController();
   if(!Test->TestTrue(TEXT("Details close button is available to keyboard input"),Close&&Player&&Close->IsVisible()))return true;
   Close->SetUserFocus(Player);Until=FPlatformTime::Seconds()+2.;Step=20;return false;
  }
  if(Step==20)
  {
   auto* Close=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_CloseStigmataChoiceDetails")));
   auto* Player=World->GetFirstPlayerController();
   if(!Close||!Player){Test->AddError(TEXT("Details close target disappeared"));return true;}
   if(!Close->HasUserFocus(Player))
   {
    if(FPlatformTime::Seconds()<Until)return false;
    Test->AddError(TEXT("Details close target did not receive keyboard focus"));return true;
   }
   if(!Test->TestTrue(TEXT("Accept key reaches the details close button"),FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0))))return true;
   Until=FPlatformTime::Seconds()+1.2;Step=21;return false;
  }
  if(Step==21)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
   Test->TestEqual(TEXT("Holding accept on details close never grants a stigmata"),Match->GetStigmataOwned().Num(),OwnedBefore);
   Test->TestEqual(TEXT("Details close keeps the offer revision"),Mode->GetStigmataRevision(),Revision);
   auto* Close=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_CloseStigmataChoiceDetails")));
   auto* CloseLayout=Close?Close->GetParent():nullptr;
   auto* DetailsPanel=CloseLayout?CloseLayout->GetParent():nullptr;
   if(!Test->TestTrue(TEXT("Accept closes the details panel"),DetailsPanel&&!DetailsPanel->IsVisible()))return true;
   Until=FPlatformTime::Seconds()+2.;Step=22;return false;
  }
  if(Step==22)
  {
   auto* Player=World->GetFirstPlayerController();
   if(!Candidate.IsValid()||!Player){Test->AddError(TEXT("Choice focus target disappeared after closing details"));return true;}
   if(!Candidate->HasUserFocus(Player))
   {
    if(FPlatformTime::Seconds()<Until)return false;
    Test->AddError(TEXT("Closing details did not return keyboard focus to the choice"));return true;
   }
   Move(Inside,false);Step=3;return false;
  }
  if(Step==3)
  {
   if(!Test->TestTrue(TEXT("Second press starts hold"),Down()&&Candidate->IsPressed()))return true;
    Until=FPlatformTime::Seconds()+.35;Step=4;return false;
  }
  if(Step==4)
  {
   if(FPlatformTime::Seconds()<Until)return false;
    if(GEngine&&GEngine->UseSound())
    {
     auto* Voice=HoldAudio(Panel);
     if(!Test->TestTrue(TEXT("Sustained mouse hold starts the actual audio component"),IsValid(Voice)&&Voice->IsPlaying()))return true;
    }
    Move(Candidate->GetCachedGeometry().LocalToAbsolute(FVector2D(-30,-30)),true);
    if(GEngine&&GEngine->UseSound())Test->TestNull(TEXT("Pointer exit immediately releases the hold audio component"),HoldAudio(Panel));
   Until=FPlatformTime::Seconds()+1.1;Step=5;return false;
  }
  if(Step==5)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   Up();
   Test->TestEqual(TEXT("Moving outside cancels the hold"),Match->GetStigmataOwned().Num(),OwnedBefore);
   Test->TestEqual(TEXT("Cancelled hold keeps reward phase"),Match->GetPhase(),EEGMatchPhase::Reward);
   Test->TestEqual(TEXT("Cancelled hold keeps revision"),Mode->GetStigmataRevision(),Revision);
   Move(Inside,false);
   if(auto* Player=World->GetFirstPlayerController())Candidate->SetUserFocus(Player);
   Step=50;return false;
  }
  if(Step==50)
  {
   auto& App=FSlateApplication::Get();
   if(!Test->TestTrue(TEXT("Accept key reaches choice"),App.ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0))))return true;
   Until=FPlatformTime::Seconds()+.15;Step=51;return false;
  }
  if(Step==51)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
   Until=FPlatformTime::Seconds()+1.1;Step=52;return false;
  }
  if(Step==52)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   Test->TestEqual(TEXT("Short keyboard press does not become a hold"),Match->GetStigmataOwned().Num(),OwnedBefore);
   if(!Test->TestEqual(TEXT("Keyboard release leaves reward active"),Match->GetPhase(),EEGMatchPhase::Reward))return true;
   if(auto* Player=World->GetFirstPlayerController())Candidate->SetUserFocus(Player);
   Step=53;return false;
  }
  if(Step==53)
  {
   if(!Test->TestTrue(TEXT("Focus-loss fixture starts keyboard hold"),FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0))))return true;
    Until=FPlatformTime::Seconds()+.35;Step=531;return false;
   }
   if(Step==531)
   {
    if(FPlatformTime::Seconds()<Until)return false;
    if(GEngine&&GEngine->UseSound())
    {
     auto* Voice=HoldAudio(Panel);
     if(!Test->TestTrue(TEXT("Sustained keyboard hold starts the actual audio component"),IsValid(Voice)&&Voice->IsPlaying()))return true;
    }
    auto* Other=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataChoice1")));
   auto* Player=World->GetFirstPlayerController();
   if(!Test->TestTrue(TEXT("Alternate choice can take focus"),Other&&Player))return true;
    Other->SetUserFocus(Player);Until=FPlatformTime::Seconds()+.2;Step=532;return false;
   }
   if(Step==532)
   {
    auto* Player=World->GetFirstPlayerController();
    if(Player&&Panel->HasUserFocus(Player)&&FPlatformTime::Seconds()<Until)return false;
    if(!Test->TestTrue(TEXT("Keyboard hold loses focus before checking audio cleanup"),Player&&!Panel->HasUserFocus(Player)))return true;
    if(GEngine&&GEngine->UseSound())Test->TestNull(TEXT("Focus loss releases the hold audio component"),HoldAudio(Panel));
    Until=FPlatformTime::Seconds()+1.1;Step=54;return false;
  }
  if(Step==54)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
   Test->TestEqual(TEXT("Focus loss cancels keyboard hold"),Match->GetStigmataOwned().Num(),OwnedBefore);
   if(!Test->TestEqual(TEXT("Focus loss keeps reward active"),Match->GetPhase(),EEGMatchPhase::Reward))return true;
   if(auto* Player=World->GetFirstPlayerController())Candidate->SetUserFocus(Player);
   Step=6;return false;
  }
  if(Step==6)
  {
   auto* Player=World->GetFirstPlayerController();
   if(!Test->TestTrue(TEXT("Keyboard final choice has player focus"),Player&&Candidate->HasUserFocus(Player)))return true;
   FocusPanel=Panel;
   FocusTraceHandle=FSlateApplication::Get().OnFocusChanging().AddLambda(
    [this](const FFocusEvent& Event,const FWeakWidgetPath&,const TSharedPtr<SWidget>& Old,
     const FWidgetPath&,const TSharedPtr<SWidget>& New)
    {
     if(Step!=6&&Step!=7)return;
     const TSharedPtr<SWidget> PanelWidget=FocusPanel.IsValid()?FocusPanel->GetCachedWidget():nullptr;
     const TSharedPtr<SWidget> ChoiceWidget=Candidate.IsValid()?Candidate->GetCachedWidget():nullptr;
     Test->AddInfo(FString::Printf(TEXT("Focus trace: step=%d frame=%llu user=%u cause=%d old=%s new=%s oldIsPanel=%d newIsPanel=%d newIsChoice=%d"),
      Step,(unsigned long long)GFrameCounter,Event.GetUser(),int32(Event.GetCause()),
      Old.IsValid()?*Old->ToString():TEXT("<none>"),New.IsValid()?*New->ToString():TEXT("<none>"),
      Old==PanelWidget?1:0,New==PanelWidget?1:0,New==ChoiceWidget?1:0));
     if(Old.IsValid()&&Old==PanelWidget&&New!=PanelWidget)
     {
      ANSICHAR Callstack[16384]={};FPlatformStackWalk::StackWalkAndDump(Callstack,UE_ARRAY_COUNT(Callstack),0);
      Test->AddInfo(FString::Printf(TEXT("Focus loss call stack:\n%s"),ANSI_TO_TCHAR(Callstack)));
     }
    });
   if(!Test->TestTrue(TEXT("Keyboard press starts final hold"),FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0))))return true;
   if(!Test->TestTrue(TEXT("Panel takes focus for keyboard hold"),Panel->HasUserFocus(Player)))return true;
   HoldStartWorldSeconds=World->GetTimeSeconds();HoldStartWallSeconds=FPlatformTime::Seconds();
   Until=FPlatformTime::Seconds()+3.;Step=7;return false;
  }
  if(Step==7)
  {
   bInactiveDuringHold|=!FSlateApplication::Get().IsActive();
   bFocusLostDuringHold|=!Panel->HasUserFocus(World->GetFirstPlayerController());
   if(!Match->HasStigmata(Chosen))
   {
    if(FPlatformTime::Seconds()<Until)return false;
    TArray<FString> OwnedNames;for(const FName Id:Match->GetStigmataOwned())OwnedNames.Add(Id.ToString());
    const TSharedPtr<SWidget> Focused=FSlateApplication::Get().GetUserFocusedWidget(0);
    Test->AddInfo(FString::Printf(TEXT("Hold diagnostics: target=%s phase=%d owned=%d worldDelta=%.3f wallDelta=%.3f currentFrameDelta=%.4f appActive=%d wasInactive=%d panelFocus=%d focusLost=%d candidateFocus=%d"),
     *Chosen.ToString(),int32(Match->GetPhase()),Match->GetStigmataOwned().Num(),World->GetTimeSeconds()-HoldStartWorldSeconds,
     FPlatformTime::Seconds()-HoldStartWallSeconds,World->GetDeltaSeconds(),FSlateApplication::Get().IsActive()?1:0,bInactiveDuringHold?1:0,
     Panel->HasUserFocus(World->GetFirstPlayerController())?1:0,bFocusLostDuringHold?1:0,
     Candidate->HasUserFocus(World->GetFirstPlayerController())?1:0));
    Test->AddInfo(FString::Printf(TEXT("Hold diagnostics: ownedIds=%s hint=%s focusedSlate=%s"),
     *FString::Join(OwnedNames,TEXT(",")),*Mode->GetHint(),Focused.IsValid()?*Focused->ToString():TEXT("<none>")));
    Test->AddError(TEXT("Holding choice did not complete within three seconds"));return true;
   }
   FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Enter,FModifierKeysState(),0,false,0,0));
   Test->TestEqual(TEXT("Completed hold commits exactly once"),Match->GetStigmataOwned().Num(),OwnedBefore+1);
   Test->TestFalse(TEXT("Committed revision cannot be reused"),Mode->ChooseStigmata(Chosen,Revision));
   auto* Overlay=Panel->GetWidgetFromName(TEXT("StigmaSkipPanel"));
   if(!Test->TestTrue(TEXT("Success presentation remains visible after commit"),Overlay&&Overlay->IsVisible()))return true;
   SuccessCapture=StartStigmataSuccessCapture(World,Candidate.Get(),Overlay,Test);
   if(!Test->TestTrue(TEXT("Success presentation screenshot has a game viewport"),SuccessCapture.IsValid()))return true;
   Until=FPlatformTime::Seconds()+.2;Step=8;return false;
  }
  auto* Overlay=Panel->GetWidgetFromName(TEXT("StigmaSkipPanel"));
  if(Step==8)
  {
   if(FPlatformTime::Seconds()<Until)return false;
   Test->TestTrue(TEXT("Success presentation survives phase synchronisation"),Overlay&&Overlay->IsVisible());
   Until=FPlatformTime::Seconds()+5.;Step=9;return false;
  }
  if(Step==9)
  {
   if(SuccessCapture.IsValid())
   {
    if(!SuccessCapture->bRequested)
    {
     if(!SuccessCapture->bCelebrationMissed&&FPlatformTime::Seconds()<Until)return false;
     Test->AddError(TEXT("Success screenshot missed the visible enlarged presentation window."));
     SuccessCapture.Reset();
    }
    else if(FScreenshotRequest::IsScreenshotRequested()||!HasExpectedPngDimensions(SuccessCapture->Path,ViewportSize,Test))
    {
     if(FPlatformTime::Seconds()<Until)return false;
     Test->AddError(TEXT("Success screenshot did not finish writing within five seconds."));
     SuccessCapture.Reset();
    }
    else
    {
     Test->AddInfo(FString::Printf(TEXT("Success screenshot saved: %s; inspect the image separately."),*SuccessCapture->Path));
     SuccessCapture.Reset();
    }
   }
   if(Overlay&&Overlay->IsVisible()&&FPlatformTime::Seconds()<Until)return false;
   if(!Test->TestFalse(TEXT("Success presentation eventually closes"),Overlay&&Overlay->IsVisible()))return true;
   Step=10;return false;
  }
  if(Step==10)
  {
   const int32 Stage=Match->GetStageNumber();
   if(Match->GetPhase()==EEGMatchPhase::Preparation)
   {
    if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
    if(!Test->TestTrue(TEXT("Empty fixture advances to later reward"),Mode->EnterCombat()))return true;
    return false;
   }
   if(Match->GetPhase()!=EEGMatchPhase::Reward)return false;
   if(Stage==4)
   {
    const TArray<FName> Offer=Mode->GetStigmataOffer();
    if(!Test->TestEqual(TEXT("Stage four has three ordinary candidates"),Offer.Num(),3))return true;
    if(!Test->TestTrue(TEXT("Stage four ordinary selection advances"),Mode->ChooseStigmata(Offer[0],Mode->GetStigmataRevision())))return true;
    return false;
   }
   if(!Test->TestEqual(TEXT("Failure fixture reaches stage six"),Stage,6))return true;
   if(!Mode->IsChoosingEndgameRoute())
   {
    Test->AddError(TEXT("Stage six must directly enter final route choice"));return true;
   }
   if(!Test->TestTrue(TEXT("Stage six has no ordinary offer"),Mode->GetStigmataOffer().IsEmpty()))return true;
   if(!Overlay||!Overlay->IsVisible())return false;
   const int32 Index=Mode->GetEndgameRouteOffer().IndexOfByKey(FName(TEXT("OriginFlower")));
   if(!Test->TestTrue(TEXT("Fusion choice is offered"),Index!=INDEX_NONE))return true;
   HoldChoiceIndex=Index;
    // Wait for the production route page, rather than calling PresentStigmaSkip
    // ourselves. The game mode also synchronizes the panel on its actor tick;
    // the grace period lets that synchronization complete before input starts.
    auto* Summary=Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataChoiceSummary%d"),Index))));
    const bool bRoutePageReady=Summary&&Summary->GetPlainText().ToString().Contains(TEXT("当前没有上阵英桀，无法汇聚"));
    const int32 CurrentRevision=Mode->GetStigmataRevision();
    if(!bRoutePageReady){bRoutePageObserved=false;return false;}
    if(!bRoutePageObserved||ObservedRouteRevision!=CurrentRevision)
    {
     bRoutePageObserved=true;ObservedRouteRevision=CurrentRevision;
     RouteReadyWorldTime=World->GetTimeSeconds()+FMath::Max(.25f,2.f*Mode->GetActorTickInterval());
     ReportRoutePreview(TEXT("route page observed"),Mode,Panel);return false;
    }
    if(World->GetTimeSeconds()<RouteReadyWorldTime)return false;
   auto* HumanPortrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_0"),Index))));
   auto* HumanSignet=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataSignet%d_0"),Index))));
   const UTexture2D* HumanArt=StigmataBrushTexture(HumanPortrait);
   const UTexture2D* HumanCrest=StigmataBrushTexture(HumanSignet);
   Test->TestTrue(TEXT("Fusion route shows the official Human form portrait"),HumanArt&&HumanArt->GetPathName()==StigmataPortraitPath(TEXT("Human")));
   const auto* HumanDefinition=UEGStigmataCatalog::Find(TEXT("OriginFlower"));
   Test->TestTrue(TEXT("OriginFlower retains its identity with the Radiant Vow display name"),HumanDefinition&&HumanDefinition->Name.ToString()==TEXT("光辉矢愿"));
   CheckStigmataDrawnAspect(Test,HumanPortrait,TEXT("Human final-route portrait"));
   CheckStigmataHeroGlow(Test,Panel,Index,0,ExpectedStigmataHeroes(TEXT("OriginFlower")));
   Test->TestTrue(TEXT("Fusion route retains Elysia's signet"),HumanCrest&&HumanCrest->GetPathName()==TEXT("/Game/Assets/UI/Textures/StigmataSelection20261105/T_UI_Signet_Elysia.T_UI_Signet_Elysia"));
   Candidate=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),Index))));
   if(!Test->TestTrue(TEXT("Fusion choice is visible"),Candidate.IsValid()&&Candidate->IsVisible()))return true;
   const FGeometry& G=Candidate->GetCachedGeometry();if(G.GetLocalSize().IsNearlyZero())return false;
   Inside=G.LocalToAbsolute(G.GetLocalSize()*.5f);Revision=Mode->GetStigmataRevision();OwnedBefore=Match->GetStigmataOwned().Num();
   Move(Inside,false);Until=FPlatformTime::Seconds()+.5;Step=17;return false;
  }
  if(Step==17)
  {
   // Capture the real final-route page with the dedicated Human hover light,
   // before a short press opens its modal. No gameplay or animation is frozen.
   if(FPlatformTime::Seconds()<Until)return false;
   if(!HumanRouteCapture.IsValid())
   {
    HumanRouteCapture=StartGameDrawCapture(World,FString::Printf(TEXT("RadiantVowFinalRoute_%dx%d"),ViewportSize.X,ViewportSize.Y),Test);
    if(!HumanRouteCapture.IsValid()){Test->AddError(TEXT("Human route screenshot lacks game viewport"));return true;}
    return false;
   }
   if(!HumanRouteCapture->bRequested||HumanRouteCapture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
   if(!HasExpectedPngDimensions(HumanRouteCapture->Path,ViewportSize,Test))return false;
   Test->AddInfo(FString::Printf(TEXT("Human final-route hover screenshot saved: %s; visual review required."),*HumanRouteCapture->Path));
   HumanRouteCapture.Reset();Step=15;return false;
  }
  if(Step==15)
  {
   if(!Test->TestTrue(TEXT("Fusion route preview receives a short press"),Down()&&Candidate->IsPressed()))return true;
    ReportRoutePreview(TEXT("after press"),Mode,Panel);
   Until=FPlatformTime::Seconds()+.15;Step=16;return false;
  }
  if(Step==16)
  {
   if(FPlatformTime::Seconds()<Until)return false;
    ReportRoutePreview(TEXT("before release"),Mode,Panel);
   auto* HumanPortrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_0"),HoldChoiceIndex))));
   auto* HumanGlow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_0"),HoldChoiceIndex))));
   Test->TestTrue(TEXT("Held Human dedicated backlight is visible and nonzero"),HumanGlow&&HumanGlow->IsVisible()&&HumanGlow->GetRenderOpacity()>.001f);
   CheckStigmataDrawnAspect(Test,HumanPortrait,TEXT("Held Human final-route portrait"));
   CheckStigmataBacklightAlignment(Test,HumanGlow,HumanPortrait,TEXT("Held Human dedicated backlight"));
   Up();
    ReportRoutePreview(TEXT("after release"),Mode,Panel);
   auto* Details=Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(TEXT("StigmataChoiceDetailsText")));
   if(!Test->TestTrue(TEXT("Fusion route details are shown"),Details&&Details->GetPlainText().ToString().Contains(TEXT("光辉矢愿"))))return true;
   Test->TestFalse(TEXT("Fusion details do not explain signet inheritance"),Details->GetPlainText().ToString().Contains(TEXT("继承")));
   Test->TestFalse(TEXT("Fusion details do not warn of signet invalidation"),Details->GetPlainText().ToString().Contains(TEXT("失效")));
   if(!Test->TestTrue(TEXT("Inspecting fusion route commits nothing"),Match->GetEndgameRoute().IsNone()))return true;
   auto& App=FSlateApplication::Get();
   if(!Test->TestTrue(TEXT("Escape closes the fusion preview"),App.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0))))return true;
   App.ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
   Move(Inside,false);Step=11;return false;
  }
  if(Step==11)
  {
   if(!Test->TestTrue(TEXT("Failed-fusion choice press reaches UI"),Down()&&Candidate->IsPressed()))return true;
   StartMouseHoldTrace(World,HoldChoiceIndex);
   ReportMouseHold(TEXT("Fusion hold started"),World,Mode,Panel);
   Until=FPlatformTime::Seconds()+10.;Step=12;return false;
  }
  if(Step==12)
  {
   auto* BeforeRelease=Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(TEXT("StigmataChoiceDetailsText")));
   const bool bObservedFailure=BeforeRelease&&BeforeRelease->GetPlainText().ToString().Contains(TEXT("未能领取"));
   if(!bObservedFailure&&MouseHoldInputValid()&&FPlatformTime::Seconds()<Until)
   {
    if(!HumanHoldCapture.IsValid()&&FPlatformTime::Seconds()-HoldStartWallSeconds>=.3)
     HumanHoldCapture=StartGameDrawCapture(World,FString::Printf(TEXT("RadiantVowFinalHold_%dx%d"),ViewportSize.X,ViewportSize.Y),Test);
    return false;
   }
   if(Test->TestTrue(TEXT("Human hold screenshot was requested"),HumanHoldCapture.IsValid()&&HumanHoldCapture->bRequested))
    Test->TestTrue(TEXT("Human hold screenshot has the expected dimensions"),HasExpectedPngDimensions(HumanHoldCapture->Path,ViewportSize,Test));
   HumanHoldCapture.Reset();
   ReportMouseHold(TEXT("Fusion hold result before release"),World,Mode,Panel);
   if(!bObservedFailure)
    Test->AddError(MouseHoldInputValid()?TEXT("Fusion hold reached its ten-second deadline without a rejection"):
     TEXT("Fusion hold lost active application, pointer press, or hover before rejection"));
   Up();
   Test->TestEqual(TEXT("Rejected fusion leaves ownership unchanged"),Match->GetStigmataOwned().Num(),OwnedBefore);
   Test->TestEqual(TEXT("Rejected fusion keeps reward phase"),Match->GetPhase(),EEGMatchPhase::Reward);
   Test->TestEqual(TEXT("Rejected fusion keeps offer revision"),Mode->GetStigmataRevision(),Revision);
   auto* Details=Cast<UEGRichTextBlock>(Panel->GetWidgetFromName(TEXT("StigmataChoiceDetailsText")));
   Test->TestTrue(TEXT("Rejected fusion explains failure"),Details&&Details->GetPlainText().ToString().Contains(TEXT("未能领取")));
   if(!Test->TestTrue(TEXT("Rejected fusion leaves the panel retryable"),Overlay&&Overlay->IsVisible()&&Candidate.IsValid()&&Candidate->GetIsEnabled()))return true;
   // The failure explanation is now a centered modal over the choices. Close
   // it through the same Escape input available to the player before retrying.
   auto* FailureClose=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_CloseStigmataChoiceDetails")));
   auto* FailureLayout=FailureClose?FailureClose->GetParent():nullptr;
   auto* FailurePanel=FailureLayout?FailureLayout->GetParent():nullptr;
   if(!Test->TestTrue(TEXT("Rejected fusion shows a closable failure explanation"),FailureClose&&FailureClose->IsVisible()&&FailurePanel&&FailurePanel->IsVisible()))return true;
   const TArray<FName> RoutesBeforeClose=Mode->GetEndgameRouteOffer();
   auto& App=FSlateApplication::Get();
   if(!Test->TestTrue(TEXT("Escape closes the failure explanation"),App.ProcessKeyDownEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0))))return true;
   App.ProcessKeyUpEvent(FKeyEvent(EKeys::Escape,FModifierKeysState(),0,false,0,0));
   if(!Test->TestFalse(TEXT("Failure explanation is closed before retry"),FailurePanel->IsVisible()))return true;
   Test->TestEqual(TEXT("Closing failure preserves ownership"),Match->GetStigmataOwned().Num(),OwnedBefore);
   Test->TestEqual(TEXT("Closing failure preserves offer revision"),Mode->GetStigmataRevision(),Revision);
   Test->TestEqual(TEXT("Closing failure preserves reward phase"),Match->GetPhase(),EEGMatchPhase::Reward);
   Test->TestTrue(TEXT("Closing failure preserves route choices"),Mode->GetEndgameRouteOffer()==RoutesBeforeClose);
   Chosen=TEXT("Story");
   const int32 RetryIndex=Mode->GetEndgameRouteOffer().IndexOfByKey(Chosen);
   if(!Test->TestTrue(TEXT("Alternative choice remains offered after rejection"),RetryIndex!=INDEX_NONE))return true;
   HoldChoiceIndex=RetryIndex;
   Candidate=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),RetryIndex))));
   if(!Test->TestTrue(TEXT("Alternative choice remains visible and enabled"),Candidate.IsValid()&&Candidate->IsVisible()&&Candidate->GetIsEnabled()))return true;
   const FGeometry& G=Candidate->GetCachedGeometry();
   if(!Test->TestFalse(TEXT("Retry target has laid out geometry"),G.GetLocalSize().IsNearlyZero()))return true;
   Inside=G.LocalToAbsolute(G.GetLocalSize()*.5f);Move(Inside,false);Step=19;return false;
  }
  if(Step==19)
  {
   if(!Test->TestTrue(TEXT("Retry candidate remains visible"),Candidate.IsValid()&&Candidate->IsVisible()))return true;
   // Re-enter after a frame; this route must accept a direct hold without a preview.
   Move(Inside,false);
   Step=13;return false;
  }
  if(Step==13)
  {
   if(!Test->TestTrue(TEXT("Retry pointer hovers alternative choice"),Candidate.IsValid()&&Candidate->IsHovered()))return true;
   if(!Test->TestTrue(TEXT("Retry pointer press starts held selection"),Down()&&Candidate->IsPressed()))return true;
   StartMouseHoldTrace(World,HoldChoiceIndex);
   ReportMouseHold(TEXT("Alternative hold started"),World,Mode,Panel);
   Until=FPlatformTime::Seconds()+10.;Step=14;return false;
  }
  if(Step==14)
  {
   if(Match->GetEndgameRoute()!=Chosen)
   {
    if(MouseHoldInputValid()&&FPlatformTime::Seconds()<Until)return false;
    ReportMouseHold(TEXT("Alternative hold failed before release"),World,Mode,Panel);
    Test->AddError(MouseHoldInputValid()?TEXT("Mouse hold on the alternative reached its ten-second deadline"):
     TEXT("Mouse hold on the alternative lost active application, pointer press, or hover before completion"));
    Test->AddError(TEXT("Mouse hold on the alternative did not complete after rejected fusion"));return true;
   }
   ReportMouseHold(TEXT("Alternative hold succeeded before release"),World,Mode,Panel);
   Up();
   Test->TestEqual(TEXT("Mouse retry commits the selected route"),Match->GetEndgameRoute(),Chosen);
   Test->TestEqual(TEXT("Route selection preserves ordinary stigmata ownership"),Match->GetStigmataOwned().Num(),OwnedBefore);
   Test->TestFalse(TEXT("Mouse retry cannot reuse its committed route revision"),Mode->ChooseEndgameRoute(Chosen,Revision));
   Test->TestTrue(TEXT("Successful mouse retry preserves celebration"),Overlay&&Overlay->IsVisible());
   Test->AddInfo(TEXT("Controlled stage results and synthetic Slate pointer events; not real OS mouse, sound, visual or balance acceptance."));
   return true;
  }
  return false;
 }
};
/** Presentation-only fixture: override the reflected offer in this PIE world,
 * capture the actual UMG tree, then restore it without committing. */
class FEGStigmataBondPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 const bool bReferenceFixture;
 const FIntPoint ViewportSize;
 double Deadline=0.,ProgressDeadline=0.,GlowDeadline=0.;
 double HoldStartWall=0.;float HoldStartWorld=0.f;
 double HoldHoverStart=0.;int32 HoldHoverTicks=0;
 float ReferenceHoverGlow=0.f;
 int32 Step=0,OriginalRevision=INDEX_NONE,OriginalOwned=0;
 int32 CoverageBatch=0,CoverageChoice=1;
 FArrayProperty* OfferProperty=nullptr;
 TWeakObjectPtr<AEGDemoGameMode> InjectedMode;
 TWeakObjectPtr<UEGDemoPanel> InjectedPanel;
 TArray<FName> OriginalOffer;
 bool bInjected=false,bPointerDown=false,bReferenceSwitchVerified=false;
 bool bScreenMessagesChanged=false,bPreviousScreenMessages=true;
 FVector2D Cursor=FVector2D::ZeroVector,Outside=FVector2D::ZeroVector;
 TSharedPtr<FPreparationDrawCapture> Capture;

 void ReleasePointer()
 {
  if(!bPointerDown||!FSlateApplication::IsInitialized())return;
  auto& App=FSlateApplication::Get();
  App.SetCursorPos(Outside);
  App.ProcessMouseMoveEvent(FPointerEvent(0,Outside,Cursor,TSet<FKey>{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState()),false);
  Cursor=Outside;
  App.ProcessMouseButtonUpEvent(FPointerEvent(0,Cursor,Cursor,TSet<FKey>{},EKeys::LeftMouseButton,0,FModifierKeysState()));
  bPointerDown=false;
 }
 void Restore()
 {
  if(bScreenMessagesChanged){GAreScreenMessagesEnabled=bPreviousScreenMessages;bScreenMessagesChanged=false;}
  if(!bInjected)return;
  ReleasePointer();
  bInjected=false;
  auto* Mode=InjectedMode.Get();
  auto* Panel=InjectedPanel.Get();
  if(!Mode||!OfferProperty)return;
  *OfferProperty->ContainerPtrToValuePtr<TArray<FName>>(Mode)=OriginalOffer;
  if(Panel&&Mode->GetMatch()&&Mode->GetMatch()->GetPhase()==EEGMatchPhase::Reward)
   Panel->PresentStigmaSkip(OriginalRevision);
 }
public:
 explicit FEGStigmataBondPIE(FAutomationTestBase* InTest,bool bInReferenceFixture=false)
  :Test(InTest),bReferenceFixture(bInReferenceFixture),ViewportSize(bInReferenceFixture?FIntPoint(1920,1080):FIntPoint(1600,900)){}
 ~FEGStigmataBondPIE(){Restore();}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Stigmata bond presentation PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards())return false;
  auto* Match=Mode->GetMatch();
  if(Step==0)
  {
   if(Match->GetPhase()==EEGMatchPhase::Preparation)
   {
    if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
    if(!Test->TestTrue(TEXT("Bond fixture enters combat"),Mode->EnterCombat()))return true;
    return false;
   }
   if(Match->GetPhase()!=EEGMatchPhase::Reward)return false;
   auto* Panel=Mode->GetDemoPanel();
   auto* Overlay=Panel?Panel->GetWidgetFromName(TEXT("StigmaSkipPanel")):nullptr;
   if(!Overlay||!Overlay->IsVisible())return false;
   if(!Test->TestEqual(TEXT("Bond fixture begins at first reward"),Match->GetStageNumber(),2))return true;
   OfferProperty=FindFProperty<FArrayProperty>(AEGDemoGameMode::StaticClass(),TEXT("StigmataOffer"));
   if(!Test->TestTrue(TEXT("Offer is a reflected name array"),OfferProperty&&CastField<FNameProperty>(OfferProperty->Inner)))return true;
   auto* Offer=OfferProperty->ContainerPtrToValuePtr<TArray<FName>>(Mode);
   if(!Test->TestNotNull(TEXT("PIE offer storage is available"),Offer))return true;
   OriginalOffer=*Offer;OriginalRevision=Mode->GetStigmataRevision();OriginalOwned=Match->GetStigmataOwned().Num();
   InjectedMode=Mode;InjectedPanel=Panel;bInjected=true;
   if(bReferenceFixture)*Offer={FName(TEXT("LoveBeginning")),FName(TEXT("Deliverance")),FName(TEXT("Infinity"))};
   else *Offer={FName(TEXT("DuskStreet")),FName(TEXT("Kindred")),FName(TEXT("LastBanquet"))};
   Panel->PresentStigmaSkip(OriginalRevision);
   if(bReferenceFixture)
   {
    bPreviousScreenMessages=GAreScreenMessagesEnabled;bScreenMessagesChanged=true;GAreScreenMessagesEnabled=false;
    TArray<UTexture*> Textures;
    for(int32 Index=0;Index<3;++Index)
    {
     auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_0"),Index))));
     auto* Glow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_0"),Index))));
     auto* Signet=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataSignet%d_0"),Index))));
     for(UTexture2D* Texture:{StigmataBrushTexture(Portrait),StigmataBrushTexture(Glow,TEXT("LightTexture")),StigmataBrushTexture(Signet)})
      if(Texture)Textures.AddUnique(Texture);
    }
    FTextureCompilingManager::Get().FinishCompilation(Textures);
    for(UTexture* Texture:Textures)if(auto* Texture2D=Cast<UTexture2D>(Texture))Texture2D->WaitForStreaming();
    Test->AddInfo(FString::Printf(TEXT("Reference screenshot prepared %d bound portrait, light and signet textures; screen messages temporarily hidden."),Textures.Num()));
   }
   Test->AddInfo(bReferenceFixture?TEXT("Reference C fixture temporarily injects Elysia, Kevin and Mobius in an isolated PIE world; no selection is committed."):
    TEXT("Bond presentation fixture temporarily injects three offers in an isolated PIE world; no selection is committed."));
   Step=1;return false;
  }
  auto* Panel=InjectedPanel.Get();
  if(!Test->TestNotNull(TEXT("Bond fixture panel remains alive"),Panel))return true;
  if(Step==12)
  {
   const int32 HoldIndex=bReferenceFixture?1:0;
   auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),HoldIndex))));
   auto* Ring=Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),HoldIndex)));
   if(!Test->TestTrue(TEXT("Protected hold target remains hovered while input settles"),FSlateApplication::Get().IsActive()&&Choice&&Choice->IsHovered()&&Ring))return true;
   if(++HoldHoverTicks<5||FPlatformTime::Seconds()-HoldHoverStart<.15)return false;
   auto& App=FSlateApplication::Get();const TSharedPtr<SWindow> Window=App.FindWidgetWindow(Choice->TakeWidget());
   if(!Test->TestTrue(TEXT("Protected hold has a window"),Window.IsValid()))return true;
   // Keep the UI's cached display intact but make the authority reject any
   // accidental completion, even if a frame stalls beyond the hold threshold.
   OfferProperty->ContainerPtrToValuePtr<TArray<FName>>(Mode)->Reset();
   bPointerDown=true;
   const bool bHandled=App.ProcessMouseButtonDownEvent(Window->GetNativeWindow(),
    FPointerEvent(0,Cursor,Cursor,TSet<FKey>{EKeys::LeftMouseButton},EKeys::LeftMouseButton,0,FModifierKeysState()));
   if(!Test->TestTrue(TEXT("Protected hold starts on the displayed choice"),bHandled&&Choice->IsPressed()&&Choice->IsHovered()&&Ring->IsVisible()))return true;
   HoldStartWall=FPlatformTime::Seconds();HoldStartWorld=World->GetTimeSeconds();
   Test->AddInfo(FString::Printf(TEXT("Protected hold target settled: %.3f seconds, %d ticks; choice=%d"),HoldStartWall-HoldHoverStart,HoldHoverTicks,HoldIndex));
   auto* Progress=Cast<UTextBlock>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldLabel%d"),HoldIndex))));
   if(!Test->TestTrue(TEXT("Protected hold percentage is visible"),Progress&&Progress->IsVisible()))return true;
   auto* ProgressImage=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldProgress%d"),HoldIndex))));
   Capture=StartProtectedHoldCapture(World,Progress,ProgressImage,bReferenceFixture,Test);
   if(!Test->TestTrue(TEXT("Protected hold screenshot has a game viewport"),Capture.IsValid()))return true;
   ProgressDeadline=FPlatformTime::Seconds()+10.;
   Test->AddInfo(TEXT("Hold screenshot clears the test world's authority offer to prevent any accidental reward commit."));
   Step=3;return false;
  }
  if(Step==11)
  {
   auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),CoverageChoice))));
   if(!Test->TestTrue(TEXT("Coverage hover remains active during capture"),FSlateApplication::Get().IsActive()&&Choice&&Choice->IsHovered()))return true;
   if(!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
   if(!HasExpectedPngDimensions(Capture->Path,ViewportSize,Test))return false;
   Test->AddInfo(FString::Printf(TEXT("Coverage image saved before switching selection: %s"),*Capture->Path));
   Capture.Reset();++CoverageChoice;Step=9;return false;
  }
  if(Step==9)
  {
   if(CoverageChoice>=3)
   {
    ++CoverageBatch;CoverageChoice=0;
    if(CoverageBatch>=3)
    {
     Test->TestEqual(TEXT("All-character preview preserves offer revision"),Mode->GetStigmataRevision(),OriginalRevision);
     Test->TestEqual(TEXT("All-character preview preserves stigmata ownership"),Match->GetStigmataOwned().Num(),OriginalOwned);
     Test->TestEqual(TEXT("All-character preview preserves reward phase"),Match->GetPhase(),EEGMatchPhase::Reward);
     Restore();
     Test->TestTrue(TEXT("Original PIE offer was restored after all-character preview"),Mode->GetStigmataOffer()==OriginalOffer);
     Test->TestEqual(TEXT("Restoring all-character preview preserves offer revision"),Mode->GetStigmataRevision(),OriginalRevision);
     Test->AddInfo(TEXT("Thirteen-character coverage uses injected offers and synthetic Slate pointer events; not OS input or visual acceptance."));
     return true;
    }
    auto* Offer=OfferProperty->ContainerPtrToValuePtr<TArray<FName>>(Mode);
    if(CoverageBatch==1)*Offer={FName(TEXT("Deliverance")),FName(TEXT("Decimation")),FName(TEXT("Wisdom"))};
    else *Offer={FName(TEXT("Daybreak")),FName(TEXT("Stars")),FName(TEXT("Vicissitude"))};
    Panel->PresentStigmaSkip(OriginalRevision);
    return false;
   }
   auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),CoverageChoice))));
   if(!Test->TestTrue(TEXT("All-character preview choice exists"),Choice&&Choice->IsVisible()))return true;
   if(!FSlateApplication::Get().IsActive()||Choice->GetCachedGeometry().GetLocalSize().IsNearlyZero())return false;
   auto& App=FSlateApplication::Get();const FGeometry& G=Choice->GetCachedGeometry();
   const FVector2D Position=G.LocalToAbsolute(G.GetLocalSize()*.5f);
   App.SetCursorPos(Position);
   App.ProcessMouseMoveEvent(FPointerEvent(0,Position,Cursor,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
   Cursor=Position;
   if(!Test->TestTrue(TEXT("All-character preview receives hover"),Choice->IsHovered()))return true;
   GlowDeadline=FPlatformTime::Seconds()+3.;Step=10;return false;
  }
  if(Step==10)
  {
   const TArray<TArray<FString>> BondHeroes={{TEXT("Pardo"),TEXT("Sakura"),TEXT("Aponia")},{TEXT("VillV"),TEXT("Mobius")},{TEXT("Aponia"),TEXT("Eden"),TEXT("Elysia")}};
   const TArray<TArray<FString>> FirstHeroes={{TEXT("Kevin")},{TEXT("Kalpas")},{TEXT("Su")}};
   const TArray<TArray<FString>> SecondHeroes={{TEXT("Kosma")},{TEXT("Griseo")},{TEXT("Hua")}};
   const auto& Heroes=CoverageBatch==0?BondHeroes:(CoverageBatch==1?FirstHeroes:SecondHeroes);
   bool bReady=true;
   for(int32 Index=0;Index<3;++Index)
    for(int32 Hero=0;Hero<Heroes[Index].Num();++Hero)
    {
     auto* Glow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),Index,Hero))));
     if(Index==CoverageChoice)
      bReady&=Glow&&Glow->IsVisible()&&Glow->GetRenderOpacity()>=.40f&&!Glow->GetCachedGeometry().GetLocalSize().IsNearlyZero();
     else bReady&=Glow&&(!Glow->IsVisible()||Glow->GetRenderOpacity()<=.04f);
    }
   if(!bReady&&FPlatformTime::Seconds()<GlowDeadline)return false;
   Test->TestTrue(*FString::Printf(TEXT("Coverage batch %d choice %d lights only hovered members"),CoverageBatch,CoverageChoice),bReady);
   for(int32 Hero=0;Hero<Heroes[CoverageChoice].Num();++Hero)
   {
    const int32 ErrorsBefore=StigmataErrorCount(Test);
    auto* Glow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),CoverageChoice,Hero))));
    auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),CoverageChoice,Hero))));
    const FString Label=FString::Printf(TEXT("Coverage %s"),*Heroes[CoverageChoice][Hero]);
    const FString PortraitPath=StigmataPortraitPath(Heroes[CoverageChoice][Hero]);
    const auto* PortraitArt=StigmataBrushTexture(Portrait);
    Test->TestTrue(*(Label+TEXT(" uses the matching portrait")),PortraitArt&&PortraitArt->GetPathName()==PortraitPath);
    CheckRequiredStigmataBacklight(Test,Glow,Heroes[CoverageChoice][Hero]);
    CheckStigmataHeroGlow(Test,Panel,CoverageChoice,Hero,Heroes[CoverageChoice]);
    CheckStigmataDrawnAspect(Test,Portrait,Label+TEXT(" portrait"));
    CheckStigmataDrawnAspect(Test,Glow,Label+TEXT(" backlight"),TEXT("LightTexture"));
    CheckStigmataBacklightAlignment(Test,Glow,Portrait,Label+TEXT(" backlight"));
    Test->AddInfo(FString::Printf(TEXT("%s %s: portrait/backlight identity, layer, aspect and UV alignment; synthetic hover, not visual acceptance."),
     bReady&&StigmataErrorCount(Test)==ErrorsBefore?TEXT("PASS"):TEXT("FAIL"),*Label));
   }
   const bool bCaptureCoverage=CoverageBatch==0||CoverageChoice==1;
   if(bCaptureCoverage)
   {
    const FString Name=CoverageBatch==0?(CoverageChoice==1?TEXT("StigmataBond_Kindred_Hover"):TEXT("StigmataBond_LastBanquet_Hover")):
     (CoverageBatch==1?TEXT("StigmataIndividuals_Kevin_Kalpas_Su"):TEXT("StigmataIndividuals_Kosma_Griseo_Hua"));
    Capture=StartGameDrawCapture(World,Name,Test);
    if(!Test->TestTrue(TEXT("Coverage screenshot has a game viewport"),Capture.IsValid()))return true;
    Step=11;return false;
   }
   ++CoverageChoice;Step=9;return false;
  }
  if(Step==5)
  {
   auto* Left=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow0_0"));
   auto* Center=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow1_0"));
   auto* Right=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow2_0"));
   if(!Test->TestTrue(TEXT("Reference switch glow widgets exist"),Left&&Center&&Right))return true;
   const bool bLeftLit=Left->IsVisible()&&Left->GetRenderOpacity()>=.45f&&Left->GetRenderOpacity()<=.55f;
   const bool bOthersDim=(!Center->IsVisible()||Center->GetRenderOpacity()<=.04f)&&
    (!Right->IsVisible()||Right->GetRenderOpacity()<=.04f);
   const bool bReady=!Left->GetCachedGeometry().GetLocalSize().IsNearlyZero();
   if((!bLeftLit||!bOthersDim||!bReady)&&FPlatformTime::Seconds()<GlowDeadline)return false;
   Test->AddInfo(FString::Printf(TEXT("Elysia hover: active=%d left=%.3f/%d center=%.3f/%d right=%.3f/%d geometryReady=%d"),FSlateApplication::Get().IsActive()?1:0,Left->GetRenderOpacity(),Left->IsVisible()?1:0,Center->GetRenderOpacity(),Center->IsVisible()?1:0,Right->GetRenderOpacity(),Right->IsVisible()?1:0,bReady?1:0));
   if(!Test->TestTrue(TEXT("Hover first illuminates only Elysia"),bLeftLit&&bOthersDim&&bReady))return true;
   CheckStigmataSelectedCorners(Test,Panel,0);
   CheckStigmataDrawnAspect(Test,Cast<UImage>(Left),TEXT("Hovered Elysia glow"),TEXT("LightTexture"));
   CheckStigmataBacklightAlignment(Test,Cast<UImage>(Left),Cast<UImage>(Panel->GetWidgetFromName(TEXT("StigmataPortrait0_0"))),TEXT("Hovered Elysia backlight"));
   Capture=StartGameDrawCapture(World,TEXT("StigmataReferenceC_ElysiaHover"),Test);
   if(!Test->TestTrue(TEXT("Elysia hover capture has a game viewport"),Capture.IsValid()))return true;
   Step=6;return false;
  }
  if(Step==6||Step==8)
  {
   auto* HoverChoice=Cast<UButton>(Panel->GetWidgetFromName(Step==6?TEXT("Button_StigmataChoice0"):TEXT("Button_StigmataChoice2")));
   auto* Glow=Panel->GetWidgetFromName(Step==6?TEXT("StigmataHeroGlow0_0"):TEXT("StigmataHeroGlow2_0"));
   if(!Test->TestTrue(TEXT("Reference hover persists until screenshot is complete"),FSlateApplication::Get().IsActive()&&HoverChoice&&HoverChoice->IsHovered()&&Glow&&Glow->IsVisible()))return true;
   if(!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
   if(!HasExpectedPngDimensions(Capture->Path,ViewportSize,Test))return false;
   Capture.Reset();
   if(Step==6)
   {
    auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataChoice2")));
    if(!Test->TestTrue(TEXT("Reference fixture has the right Mobius choice"),Choice&&Choice->IsVisible()))return true;
    auto& App=FSlateApplication::Get();const FGeometry& G=Choice->GetCachedGeometry();
    const FVector2D Position=G.LocalToAbsolute(G.GetLocalSize()*.5f);
    App.SetCursorPos(Position);
    App.ProcessMouseMoveEvent(FPointerEvent(0,Position,Cursor,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
    Cursor=Position;
    if(!Test->TestTrue(TEXT("Reference switch hovers Mobius"),Choice->IsHovered()))return true;
    GlowDeadline=FPlatformTime::Seconds()+2.;Step=7;return false;
   }
   auto* CenterChoice=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataChoice1")));
   if(!Test->TestTrue(TEXT("Reference fixture has the central Kevin choice"),CenterChoice&&CenterChoice->IsVisible()))return true;
   auto& App=FSlateApplication::Get();
   const FGeometry& G=CenterChoice->GetCachedGeometry();
   const FVector2D Position=G.LocalToAbsolute(G.GetLocalSize()*.5f);
   App.SetCursorPos(Position);
   App.ProcessMouseMoveEvent(FPointerEvent(0,Position,Cursor,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
   Cursor=Position;
   if(!Test->TestTrue(TEXT("Reference switch hovers Kevin"),CenterChoice->IsHovered()))return true;
   bReferenceSwitchVerified=true;GlowDeadline=0.;Step=1;return false;
  }
  if(Step==7)
  {
   auto* Left=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow0_0"));
   auto* Center=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow1_0"));
   auto* Right=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow2_0"));
   if(!Test->TestTrue(TEXT("Reference Mobius glow widgets exist"),Left&&Center&&Right))return true;
   const bool bRightLit=Right->IsVisible()&&Right->GetRenderOpacity()>=.45f&&Right->GetRenderOpacity()<=.55f;
   const bool bOthersDim=(!Left->IsVisible()||Left->GetRenderOpacity()<=.04f)&&(!Center->IsVisible()||Center->GetRenderOpacity()<=.04f);
   const bool bReady=!Right->GetCachedGeometry().GetLocalSize().IsNearlyZero();
   if((!bRightLit||!bOthersDim||!bReady)&&FPlatformTime::Seconds()<GlowDeadline)return false;
   Test->AddInfo(FString::Printf(TEXT("Mobius hover: active=%d left=%.3f/%d center=%.3f/%d right=%.3f/%d geometryReady=%d"),FSlateApplication::Get().IsActive()?1:0,Left->GetRenderOpacity(),Left->IsVisible()?1:0,Center->GetRenderOpacity(),Center->IsVisible()?1:0,Right->GetRenderOpacity(),Right->IsVisible()?1:0,bReady?1:0));
   if(!Test->TestTrue(TEXT("Hover illuminates only Mobius"),bRightLit&&bOthersDim&&bReady))return true;
   CheckStigmataSelectedCorners(Test,Panel,2);
   CheckStigmataDrawnAspect(Test,Cast<UImage>(Right),TEXT("Hovered Mobius glow"),TEXT("LightTexture"));
   CheckStigmataBacklightAlignment(Test,Cast<UImage>(Right),Cast<UImage>(Panel->GetWidgetFromName(TEXT("StigmataPortrait2_0"))),TEXT("Hovered Mobius backlight"));
   Capture=StartGameDrawCapture(World,TEXT("StigmataReferenceC_MobiusHover"),Test);
   if(!Test->TestTrue(TEXT("Mobius hover capture has a game viewport"),Capture.IsValid()))return true;
   Step=8;return false;
  }
  if(Step==4)
  {
   auto* Glow=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow1_0"));
   const bool bFaded=Glow&&(!Glow->IsVisible()||Glow->GetRenderOpacity()<=.02f);
   if(!bFaded&&FPlatformTime::Seconds()<GlowDeadline)return false;
   Test->TestTrue(TEXT("Reference glow fades after pointer leaves"),bFaded);
   auto* Ring=Panel->GetWidgetFromName(TEXT("StigmataHoldRing1"));
   Test->TestTrue(TEXT("Cancelled reference hold hides its progress ring"),Ring&&!Ring->IsVisible());
   Test->TestEqual(TEXT("Reference cancel preserves offer revision"),Mode->GetStigmataRevision(),OriginalRevision);
   Test->TestEqual(TEXT("Reference cancel preserves stigmata ownership"),Match->GetStigmataOwned().Num(),OriginalOwned);
   Test->TestEqual(TEXT("Reference cancel preserves reward phase"),Match->GetPhase(),EEGMatchPhase::Reward);
   Restore();
   Test->TestTrue(TEXT("Original PIE offer was restored"),Mode->GetStigmataOffer()==OriginalOffer);
   Test->TestEqual(TEXT("Restoring preview preserves offer revision"),Mode->GetStigmataRevision(),OriginalRevision);
   Test->AddInfo(TEXT("Injected reference preview is not a natural campaign offer, OS input, or visual acceptance."));
   return true;
  }
  if(Step==1)
  {
   const TArray<TArray<FString>> Heroes=bReferenceFixture?
    TArray<TArray<FString>>{{TEXT("Elysia")},{TEXT("Kevin")},{TEXT("Mobius")}}:
    TArray<TArray<FString>>{{TEXT("Pardo"),TEXT("Sakura"),TEXT("Aponia")},
     {TEXT("VillV"),TEXT("Mobius")},{TEXT("Aponia"),TEXT("Eden"),TEXT("Elysia")}};
   for(int32 Index=0;Index<Heroes.Num();++Index)
   {
    auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),Index))));
    if(!Test->TestTrue(TEXT("Injected bond choice is visible"),Choice&&Choice->IsVisible()))return true;
    if(Choice->GetCachedGeometry().GetLocalSize().IsNearlyZero())return false;
    for(int32 Hero=0;Hero<3;++Hero)
    {
     auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),Index,Hero))));
     auto* Signet=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataSignet%d_%d"),Index,Hero))));
     if(!Test->TestTrue(TEXT("Bond portrait and signet widgets exist"),Portrait&&Signet))return true;
     if(!Heroes[Index].IsValidIndex(Hero))
     {
      Test->TestFalse(TEXT("Unused bond portrait is hidden"),Portrait->IsVisible());
      Test->TestFalse(TEXT("Unused bond signet is hidden"),Signet->IsVisible());
      continue;
     }
     const FString PortraitPath=StigmataPortraitPath(Heroes[Index][Hero]);
     const FString SignetPath=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelection20261105/T_UI_Signet_%s.T_UI_Signet_%s"),*Heroes[Index][Hero],*Heroes[Index][Hero]);
     const UObject* PortraitArt=StigmataBrushTexture(Portrait);
     const UObject* SignetArt=Signet->GetBrush().GetResourceObject();
     Test->TestTrue(*FString::Printf(TEXT("Bond %d portrait %d uses %s"),Index,Hero,*Heroes[Index][Hero]),
      Portrait->IsVisible()&&PortraitArt&&PortraitArt->GetPathName()==PortraitPath);
     Test->TestTrue(*FString::Printf(TEXT("Bond %d signet %d uses %s"),Index,Hero,*Heroes[Index][Hero]),
      Signet->IsVisible()&&SignetArt&&SignetArt->GetPathName()==SignetPath);
     CheckStigmataHeroGlow(Test,Panel,Index,Hero,Heroes[Index]);
     if(bReferenceFixture)
     {
      auto* Glow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow%d_%d"),Index,Hero))));
      const auto* Light=StigmataBrushTexture(Glow,TEXT("LightTexture"));
      const FString Name=Heroes[Index][Hero]==TEXT("Mobius")?FString(TEXT("T_StigmaC_Backlight_MobiusV2")):FString::Printf(TEXT("T_StigmaC_Backlight_%s"),*Heroes[Index][Hero]);
      const FString Path=FString::Printf(TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/%s.%s"),*Name,*Name);
      Test->TestTrue(*FString::Printf(TEXT("Reference %s uses its registered backlight"),*Heroes[Index][Hero]),Light&&Light->GetPathName()==Path&&Cast<UMaterialInstanceDynamic>(Glow->GetBrush().GetResourceObject()));
      auto* Plate=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataInfoPlate%d"),Index))));
      const auto* PlateArt=StigmataBrushTexture(Plate);
      Test->TestTrue(*FString::Printf(TEXT("Reference choice %d uses idle plate"),Index),Plate&&Plate->IsVisible()&&PlateArt&&PlateArt->GetPathName()==TEXT("/Game/Assets/UI/Textures/StigmataSelectionC20261105/T_StigmaC_InfoPlateIdleV2.T_StigmaC_InfoPlateIdleV2"));
     }
    }
   }
   if(bReferenceFixture)
   {
    auto& App=FSlateApplication::Get();
    if(!App.IsActive())return false;
    if(!bReferenceSwitchVerified)
    {
     auto* Left=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataChoice0")));
     if(!Test->TestTrue(TEXT("Reference fixture has the left Elysia choice"),Left&&Left->IsVisible()))return true;
     const FGeometry& G=Left->GetCachedGeometry();
     const FVector2D Position=G.LocalToAbsolute(G.GetLocalSize()*.5f);
     Cursor=App.GetCursorPos();App.SetCursorPos(Position);
     App.ProcessMouseMoveEvent(FPointerEvent(0,Position,Cursor,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
     Cursor=Position;
     if(!Test->TestTrue(TEXT("Reference fixture hovers Elysia"),Left->IsHovered()))return true;
     GlowDeadline=FPlatformTime::Seconds()+2.;Step=5;return false;
    }
    auto* Center=Cast<UButton>(Panel->GetWidgetFromName(TEXT("Button_StigmataChoice1")));
    if(!Test->TestTrue(TEXT("Reference fixture has the central Kevin choice"),Center&&Center->IsVisible()))return true;
    const FGeometry& CenterGeometry=Center->GetCachedGeometry();
    const FVector2D CenterPoint=CenterGeometry.LocalToAbsolute(CenterGeometry.GetLocalSize()*.5f);
    Cursor=App.GetCursorPos();App.SetCursorPos(CenterPoint);
    App.ProcessMouseMoveEvent(FPointerEvent(0,CenterPoint,Cursor,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
    Cursor=CenterPoint;
    if(!Test->TestTrue(TEXT("Reference fixture hovers central Kevin"),Center->IsHovered()))return true;
   }
   Capture=StartGameDrawCapture(World,bReferenceFixture?TEXT("StigmataReferenceC_Injected"):TEXT("StigmataBondPresentation_Injected"),Test);
   if(!Test->TestTrue(TEXT("Bond screenshot has a game viewport"),Capture.IsValid()))return true;
   Step=2;return false;
  }
  if(Step==3)
  {
   const int32 HoldIndex=bReferenceFixture?1:0;
   const auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),HoldIndex))));
   const auto* Ring=Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),HoldIndex)));
   const auto* Percent=Cast<UTextBlock>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldLabel%d"),HoldIndex))));
   const bool bActive=FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive();
   const bool bPressed=Choice&&Choice->IsPressed(),bHovered=Choice&&Choice->IsHovered();
   const bool bRingVisible=Ring&&Ring->IsVisible(),bLabelVisible=Percent&&Percent->IsVisible();
   const bool bAuthorityOfferEmpty=OfferProperty->ContainerPtrToValuePtr<TArray<FName>>(Mode)->IsEmpty();
   if(Capture->bProgressMissed||!bPointerDown||!bActive||!bPressed||!bHovered||!bRingVisible||!bLabelVisible||
      !bAuthorityOfferEmpty||FPlatformTime::Seconds()>ProgressDeadline)
   {
    Test->AddError(FString::Printf(TEXT("Protected hold capture failed: progressMissed=%d observed=%d requested=%d active=%d pointerDown=%d pressed=%d hovered=%d ringVisible=%d labelVisible=%d authorityOfferEmpty=%d deadlineExceeded=%d wall=%.3f world=%.3f draws=%d screenshotPending=%d label=%s"),
     Capture->bProgressMissed?1:0,Capture->ObservedPercent,Capture->RequestPercent,bActive?1:0,bPointerDown?1:0,
     bPressed?1:0,bHovered?1:0,bRingVisible?1:0,bLabelVisible?1:0,bAuthorityOfferEmpty?1:0,
     FPlatformTime::Seconds()>ProgressDeadline?1:0,FPlatformTime::Seconds()-HoldStartWall,
     World->GetTimeSeconds()-HoldStartWorld,Capture->Draws,FScreenshotRequest::IsScreenshotRequested()?1:0,
     Percent?*Percent->GetText().ToString():TEXT("<missing>")));
    for(int32 Index=0;Index<3;++Index)
    {
     const auto* Candidate=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),Index))));
     Test->AddInfo(FString::Printf(TEXT("Hold failure choice %d: exists=%d visible=%d hovered=%d pressed=%d"),Index,Candidate?1:0,
      Candidate&&Candidate->IsVisible()?1:0,Candidate&&Candidate->IsHovered()?1:0,Candidate&&Candidate->IsPressed()?1:0));
    }
    return true;
   }
  }
  if(!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
  if(!HasExpectedPngDimensions(Capture->Path,ViewportSize,Test))return false;
  if(Step==2&&!FSlateApplication::Get().IsActive())return false;
  if(Step==2&&bReferenceFixture)
  {
   if(GlowDeadline==0.)GlowDeadline=FPlatformTime::Seconds()+2.;
   auto* Left=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow0_0"));
   auto* Center=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow1_0"));
   auto* Right=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow2_0"));
   if(!Test->TestTrue(TEXT("Reference glow widgets exist"),Left&&Center&&Right))return true;
   const bool bCenterLit=Center->IsVisible()&&Center->GetRenderOpacity()>.05f;
   const bool bOthersDim=(!Left->IsVisible()||Left->GetRenderOpacity()<=.04f)&&
    (!Right->IsVisible()||Right->GetRenderOpacity()<=.04f);
   if((!bCenterLit||!bOthersDim)&&FPlatformTime::Seconds()<GlowDeadline)return false;
   Test->TestTrue(TEXT("Hover illuminates only the central character"),bCenterLit&&bOthersDim);
   CheckStigmataSelectedCorners(Test,Panel,1);
   ReferenceHoverGlow=Center->GetRenderOpacity();
   CheckStigmataDrawnAspect(Test,Cast<UImage>(Center),TEXT("Hovered Kevin glow"),TEXT("LightTexture"));
   CheckStigmataBacklightAlignment(Test,Cast<UImage>(Center),Cast<UImage>(Panel->GetWidgetFromName(TEXT("StigmataPortrait1_0"))),TEXT("Hovered Kevin backlight"));
  }
  if(Step==3)
  {
   Test->TestTrue(TEXT("Protected hold screenshot requested within 45–80 percent"),Capture->RequestPercent>=45&&Capture->RequestPercent<=80);
   Test->AddInfo(FString::Printf(TEXT("Protected hold capture completed: request=%d%% observed=%d%% wall=%.3f world=%.3f draws=%d"),
    Capture->RequestPercent,Capture->ObservedPercent,FPlatformTime::Seconds()-HoldStartWall,World->GetTimeSeconds()-HoldStartWorld,Capture->Draws));
  }
  Capture.Reset();
  if(Step==2)
  {
   // Validate every visible bond member after the injected UMG tree has been
   // drawn repeatedly; geometry before the first paint can be stale.
   const int32 BondCounts[]={3,2,3};
   const int32 ReferenceCounts[]={1,1,1};
   const int32* Counts=bReferenceFixture?ReferenceCounts:BondCounts;
   for(int32 Index=0;Index<3;++Index)
    for(int32 Hero=0;Hero<Counts[Index];++Hero)
    {
     auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait%d_%d"),Index,Hero))));
     auto* Signet=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataSignet%d_%d"),Index,Hero))));
     CheckStigmataDrawnAspect(Test,Portrait,FString::Printf(TEXT("Bond %d member %d portrait"),Index,Hero));
     CheckStigmataDrawnAspect(Test,Signet,FString::Printf(TEXT("Bond %d member %d signet"),Index,Hero));
    }
   auto& App=FSlateApplication::Get();
   const int32 HoldIndex=bReferenceFixture?1:0;
   auto* Choice=Cast<UButton>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("Button_StigmataChoice%d"),HoldIndex))));
   auto* Ring=Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),HoldIndex)));
   if(!Test->TestTrue(TEXT("Protected hold fixture has choice and ring"),Choice&&Ring&&Choice->IsVisible()))return true;
   const FGeometry& Geometry=Choice->GetCachedGeometry();
   const FVector2D Inside=Geometry.LocalToAbsolute(Geometry.GetLocalSize()*.5f);
   Outside=Geometry.LocalToAbsolute(FVector2D(-30.f,-30.f));Cursor=App.GetCursorPos();
   App.SetCursorPos(Inside);
   App.ProcessMouseMoveEvent(FPointerEvent(0,Inside,Cursor,TSet<FKey>{},EKeys::Invalid,0,FModifierKeysState()),false);
   Cursor=Inside;
   HoldHoverStart=FPlatformTime::Seconds();HoldHoverTicks=0;Step=12;return false;
  }
  auto* Ring=Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHoldRing%d"),bReferenceFixture?1:0)));
  Test->TestTrue(TEXT("Hold ring remains visible until captured"),Ring&&Ring->IsVisible());
  if(bReferenceFixture)
  {
   auto* Glow=Panel->GetWidgetFromName(TEXT("StigmataHeroGlow1_0"));
   Test->TestTrue(TEXT("Holding strengthens the hovered character glow"),Glow&&Glow->IsVisible()&&Glow->GetRenderOpacity()>ReferenceHoverGlow+.02f);
  }
  else
   for(int32 Hero=0;Hero<3;++Hero)
   {
    const int32 ErrorsBefore=StigmataErrorCount(Test);
    auto* Glow=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataHeroGlow0_%d"),Hero))));
    const FString RequiredHeroes[]={TEXT("Pardo"),TEXT("Sakura"),TEXT("Aponia")};
    const TArray<FString> BondHeroes={TEXT("Pardo"),TEXT("Sakura"),TEXT("Aponia")};
    auto* Portrait=Cast<UImage>(Panel->GetWidgetFromName(FName(*FString::Printf(TEXT("StigmataPortrait0_%d"),Hero))));
    const auto* PortraitArt=StigmataBrushTexture(Portrait);
    const FString PortraitPath=StigmataPortraitPath(RequiredHeroes[Hero]);
    Test->TestTrue(TEXT("Held bond uses matching portrait"),PortraitArt&&PortraitArt->GetPathName()==PortraitPath);
    CheckRequiredStigmataBacklight(Test,Glow,RequiredHeroes[Hero]);
    CheckStigmataHeroGlow(Test,Panel,0,Hero,BondHeroes);
    CheckStigmataDrawnAspect(Test,Portrait,FString::Printf(TEXT("Held bond member %d portrait"),Hero));
    if(Glow&&Glow->GetBrush().GetResourceObject())
    {
     Test->TestTrue(*FString::Printf(TEXT("Held bond lights imported member %d"),Hero),Glow->IsVisible()&&Glow->GetRenderOpacity()>.01f);
     CheckStigmataDrawnAspect(Test,Glow,FString::Printf(TEXT("Held bond member %d glow"),Hero),TEXT("LightTexture"));
     CheckStigmataBacklightAlignment(Test,Glow,Portrait,FString::Printf(TEXT("Held bond member %d backlight"),Hero));
    }
    else
     Test->TestTrue(*FString::Printf(TEXT("Held bond keeps unavailable member %d glow hidden"),Hero),Glow&&!Glow->IsVisible());
    Test->AddInfo(FString::Printf(TEXT("%s Coverage %s: portrait/backlight identity, layer, aspect and UV alignment; synthetic hold, not visual acceptance."),
     StigmataErrorCount(Test)==ErrorsBefore?TEXT("PASS"):TEXT("FAIL"),*RequiredHeroes[Hero]));
   }
  ReleasePointer();
  if(bReferenceFixture){GlowDeadline=FPlatformTime::Seconds()+2.;Step=4;return false;}
  *OfferProperty->ContainerPtrToValuePtr<TArray<FName>>(Mode)={FName(TEXT("DuskStreet")),FName(TEXT("Kindred")),FName(TEXT("LastBanquet"))};
  CoverageBatch=0;CoverageChoice=1;Step=9;return false;
 }
};

// Opt-in only: advance through the controlled empty-board fixture, then leave
// the actual reward transaction and all subsequent input to the operator.
class FEGStigmataManualPreview final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.;
 bool bSawWorld=false,bReady=false;
public:
 explicit FEGStigmataManualPreview(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  if(!World)
  {
   if(bReady){Test->AddInfo(TEXT("Manual preview ended when the operator closed PIE."));return true;}
   if(bSawWorld){Test->AddError(TEXT("PIE ended before the manual reward preview was ready."));return true;}
   if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Manual preview could not start PIE."));return true;}
   return false;
  }
  bSawWorld=true;
  if(bReady)return false;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Manual preview did not reach the first reward."));return true;}
  auto* Mode=World->GetAuthGameMode<AEGDemoGameMode>();
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards())return false;
  auto* Match=Mode->GetMatch();
  if(Match->GetPhase()==EEGMatchPhase::Preparation)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   if(!Test->TestTrue(TEXT("Manual preview enters controlled empty-board combat"),Mode->EnterCombat()))return true;
   return false;
  }
  if(Match->GetPhase()!=EEGMatchPhase::Reward)return false;
  auto* Panel=Mode->GetDemoPanel();auto* Overlay=Panel?Panel->GetWidgetFromName(TEXT("StigmaSkipPanel")):nullptr;
  if(!Overlay||!Overlay->IsVisible())return false;
  if(!Test->TestTrue(TEXT("Manual preview has three authoritative choices"),Mode->GetStigmataOffer().Num()==3))return true;
  bReady=true;
  UE_LOG(LogTemp,Display,TEXT("[EGStigmataManualPreview] READY: stage=%d revision=%d; real input enabled, stop PIE explicitly when finished."),Match->GetStageNumber(),Mode->GetStigmataRevision());
  Test->AddInfo(TEXT("Manual preview ready: controlled empty-board progression; authoritative offers retained, no synthetic input after readiness. Stop PIE explicitly."));
  return false;
 }
};

class FEGPIECleanup final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double FirstCheck=0;
public:
 explicit FEGPIECleanup(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(!FirstCheck)FirstCheck=FPlatformTime::Seconds();
  if(GEditor->PlayWorld&&FPlatformTime::Seconds()-FirstCheck<15.)return false;
  Test->TestNull(TEXT("PIE world is closed after lifecycle test"),GEditor->PlayWorld);return true;
 }
};

/** Real PIE world, with enemy actions disabled and one controlled full-mana switch. */
class FEGVillVVitalsPIE final : public IAutomationLatentCommand
{
 FAutomationTestBase* Test;
 double Deadline=0.;
 int32 Step=0;
 FGuid PilotId;
 TWeakObjectPtr<AEGChessUnit> Pilot;
 TSharedPtr<FPreparationDrawCapture> Capture;
 bool bBeforeObserved=false,bTransitionObserved=false;
 bool bGunCaptured=false,bHaveHeadSample=false;
 int32 HeadSamples=0,HeadObservationFrames=0;
 FVector2D PreviousHeadOffset=FVector2D::ZeroVector;
 double LargestHeadOffsetStep=0.;
 bool CaptureFrame(UWorld* World,const TCHAR* Name)
 {
  if(!Capture.IsValid())
  {
   Capture=StartGameDrawCapture(World,Name,Test);
   if(!Capture.IsValid()){Test->AddError(TEXT("Vill-V capture lacks a game viewport"));return true;}
   return false;
  }
  if(!Capture->bRequested||Capture->Draws<5||FScreenshotRequest::IsScreenshotRequested())return false;
  if(!HasExpectedPngDimensions(Capture->Path,FIntPoint(1920,1080),Test))return false;
  Capture.Reset();return true;
 }
public:
 explicit FEGVillVVitalsPIE(FAutomationTestBase* InTest):Test(InTest){}
 virtual bool Update() override
 {
  if(Deadline==0.)Deadline=FPlatformTime::Seconds()+180.;
  if(FPlatformTime::Seconds()>Deadline){Test->AddError(TEXT("Vill-V vitals PIE timed out"));return true;}
  UWorld* World=GEditor?GEditor->PlayWorld:nullptr;
  auto* Mode=World?World->GetAuthGameMode<AEGDemoGameMode>():nullptr;
  if(!Mode||!Mode->GetMatch()||!Mode->GetCards()||!Mode->GetRoster()||!Mode->BattleRules)return false;
  if(Step==0)
  {
   if(!Mode->CanPrepare()||!Mode->AreStageResourcesReady())return false;
   UEGCardDefinition* Card=nullptr;
   for(const auto& Candidate:Mode->RunDefinition->CardPool)
    if(Candidate&&Candidate->HeroDefinition&&Candidate->HeroDefinition->HeroKind==EEGHeroKind::VillV){Card=Candidate.Get();break;}
   if(!Test->TestNotNull(TEXT("Production pool contains Vill-V"),Card)||
      !Test->TestTrue(TEXT("Grant controlled Vill-V card"),Mode->GetCards()->GrantCard(Card))||
      !Test->TestTrue(TEXT("Deploy Vill-V group"),Mode->GetCards()->TryDeployHandCard(Mode->GetCards()->GetHand().Last().InstanceId,FIntPoint(2,2))))return true;
   PilotId=Mode->GetRoster()->FindHeroByDefinition(Card->HeroDefinition);
   if(!Test->TestTrue(TEXT("Enter Vill-V combat"),PilotId.IsValid()&&Mode->EnterCombat()))return true;
   for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetCombat())Unit->GetCombat()->SetComponentTickEnabled(false);
   Step=1;return false;
  }
  if(!Pilot.IsValid())for(auto* Unit:Mode->GetCombatUnits())if(Unit&&Unit->GetInstanceId()==PilotId){Pilot=Unit;break;}
  if(!Pilot.IsValid()||!Mode->BattleRules->IsCombatRunning())return false;
  auto* Mechanism=Pilot->FindComponentByClass<UEGVillVMechanismComponent>();
  auto* Component=Pilot->FindComponentByClass<UWidgetComponent>();
  auto* Vitals=Component?Cast<UEGUnitVitals>(Component->GetUserWidgetObject()):nullptr;
  if(!Test->TestNotNull(TEXT("Pilot mechanism exists"),Mechanism)||!Test->TestNotNull(TEXT("Pilot vitals exist"),Vitals))return true;
  if(!Test->TestNotNull(TEXT("Pilot health bar exists"),Vitals->GetHealthBar())||
     !Test->TestNotNull(TEXT("Pilot mana bar exists"),Vitals->GetManaBar()))return true;
  if(Step==1)
  {
   if(Capture.IsValid())
   {
    if(!CaptureFrame(World,TEXT("VillVVitals_BeforeBoarding")))return false;
    Step=2;return false;
   }
   if(!Mechanism->IsOperating())
   {
    bBeforeObserved=true;
    if(!CaptureFrame(World,TEXT("VillVVitals_BeforeBoarding")))return false;
   }
   else if(!bBeforeObserved)Test->AddWarning(TEXT("Pilot boarded before first rendered pre-boarding capture."));
   Step=2;return false;
  }
  if(Step==2)
  {
   if(!Mechanism->IsOperating()||Mechanism->Mode()!=0)return false;
   if(!bGunCaptured)
   {
    if(!CaptureFrame(World,TEXT("VillVVitals_Gatling")))return false;
    bGunCaptured=true;
   }
   ++HeadObservationFrames;
   auto* Body=Pilot->GetVisiblePresentationMesh();
   const FName Names[]={TEXT("Bip001 Head"),TEXT("Bip001_Head"),TEXT("Bip001-Head"),TEXT("Bip01 Head"),TEXT("Bip01_Head"),TEXT("Bip01-Head"),TEXT("Head"),TEXT("Bip001Head")};
   FName HeadName=NAME_None;
   if(Body)for(const FName Name:Names)if(Body->GetBoneIndex(Name)!=INDEX_NONE){HeadName=Name;break;}
   FVector2D HeadScreen,AnchorScreen;
   auto* Player=World->GetFirstPlayerController();
   if(Player&&Body&&!HeadName.IsNone()&&Player->ProjectWorldLocationToScreen(Body->GetSocketLocation(HeadName),HeadScreen,true)&&
      Player->ProjectWorldLocationToScreen(Component->GetComponentLocation(),AnchorScreen,true))
   {
    const FVector2D Offset=AnchorScreen+Vitals->GetRenderTransform().Translation*UWidgetLayoutLibrary::GetViewportScale(World)-HeadScreen;
    if(bHaveHeadSample)LargestHeadOffsetStep=FMath::Max(LargestHeadOffsetStep,(Offset-PreviousHeadOffset).Size());
    PreviousHeadOffset=Offset;bHaveHeadSample=true;++HeadSamples;
   }
   if(HeadSamples<30&&HeadObservationFrames<90)return false;
   Test->TestTrue(TEXT("Both pilot bars visible while seated"),Vitals->GetHealthBar()&&Vitals->GetHealthBar()->IsVisible()&&Vitals->GetManaBar()&&Vitals->GetManaBar()->IsVisible());
   Test->TestTrue(TEXT("Visible pilot head projects for 30 frames"),HeadSamples>=30);
   Test->TestTrue(TEXT("Pilot anchor remains above the projected head"),PreviousHeadOffset.Y<0.);
   Test->TestTrue(TEXT("Pilot anchor has no large back-and-forth frame jump"),LargestHeadOffsetStep<25.);
   Test->AddInfo(FString::Printf(TEXT("Vill-V seated head anchor: %d projected frames, max relative step %.2f pixels; inspect screenshots for sub-frame jitter and readability."),HeadSamples,LargestHeadOffsetStep));
   auto* Gun=Mechanism->GetDevice(0);
   if(!Test->TestNotNull(TEXT("Seated gun exists"),Gun))return true;
   const float GunMax=Gun->GetAttributes()->GetMaxHealth();
   Gun->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),GunMax*.55f);
   Test->TestTrue(TEXT("Actual PIE pilot bar observes occupied gun health"),FMath::IsNearlyEqual(Vitals->GetHealthBar()->GetPercent(),.55f,.01f));
   Pilot->GetHeroSkill()->CancelAction();
   Pilot->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetManaAttribute(),Pilot->GetAttributes()->GetMaxMana());
   if(!Test->TestTrue(TEXT("Controlled full-mana switch starts"),Pilot->GetHeroSkill()->BeginAction(true)))return true;
   Step=3;return false;
  }
  if(Step==3)
  {
   if(Capture.IsValid())
   {
    if(!CaptureFrame(World,TEXT("VillVVitals_Switch")))return false;
    Step=4;return false;
   }
   if(!Mechanism->IsOperating())
   {
    bTransitionObserved=true;
    if(!CaptureFrame(World,TEXT("VillVVitals_Switch")))return false;
    Step=4;return false;
   }
   if(Mechanism->Mode()==1)
   {Test->AddWarning(TEXT("Switch completed before its rendered transition capture."));Step=4;}
   return false;
  }
  if(!Mechanism->IsOperating()||Mechanism->Mode()!=1)return false;
  if(!CaptureFrame(World,TEXT("VillVVitals_Cannon")))return false;
  Test->TestTrue(TEXT("Both pilot bars visible after switch"),Vitals->GetHealthBar()&&Vitals->GetHealthBar()->IsVisible()&&Vitals->GetManaBar()&&Vitals->GetManaBar()->IsVisible());
  auto* Cannon=Mechanism->GetDevice(1);
  if(!Test->TestNotNull(TEXT("Seated cannon exists"),Cannon))return true;
  const float CannonMax=Cannon->GetAttributes()->GetMaxHealth();
  Cannon->GetAbilitySystemComponent()->SetNumericAttributeBase(UEGAttributeSet::GetHealthAttribute(),CannonMax*.35f);
  Test->TestTrue(TEXT("Actual PIE pilot bar observes switched cannon health"),FMath::IsNearlyEqual(Vitals->GetHealthBar()->GetPercent(),.35f,.01f));
  Test->TestTrue(TEXT("Controlled switch exposed transition"),bTransitionObserved);
  Test->AddInfo(TEXT("Vill-V screenshots use a real PIE game viewport; actor combat AI was disabled and the switch used controlled mana, so this is not natural balance or OS input evidence."));
  return true;
 }
};
}

static bool QueuePreparationCapture(FAutomationTestBase* Test,EStigmataRoute Route)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Preparation capture requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Preparation capture requires a rendering RHI"));return false;}
 FString Map;
 if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),Map,GEngineIni))
 {Test->AddError(TEXT("GameDefaultMap is missing"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(RouteViewportSize(Route)));
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIENaturalRun(Test,Route,true));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(Test));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPreparationCaptureFive,"ElyGambit.Integration.PreparationCapture1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPreparationCaptureFive::RunTest(const FString&){return QueuePreparationCapture(this,EStigmataRoute::Five);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataPointerPIETest,"ElyGambit.Integration.StigmataPointerPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataPointerPIETest::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Stigmata pointer PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Stigmata pointer PIE requires a rendering RHI and active window"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(FIntPoint(1600,900)));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStigmataPointerPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

static bool QueueStigmataPointerAtSize(FAutomationTestBase* Test,FIntPoint Size)
{
 if(!GEditor||GEditor->PlayWorld){Test->AddError(TEXT("Stigmata pointer PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){Test->AddError(TEXT("Stigmata pointer PIE requires a rendering RHI and active window"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(Size));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStigmataPointerPIE(Test,Size));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(Test));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataPointer720,"ElyGambit.Integration.StigmataPointer720",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataPointer720::RunTest(const FString&){return QueueStigmataPointerAtSize(this,FIntPoint(1280,720));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataPointer1080,"ElyGambit.Integration.StigmataPointer1080",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataPointer1080::RunTest(const FString&){return QueueStigmataPointerAtSize(this,FIntPoint(1920,1080));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataPointerWide,"ElyGambit.Integration.StigmataPointerWide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataPointerWide::RunTest(const FString&){return QueueStigmataPointerAtSize(this,FIntPoint(2560,1080));}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataManualPreviewTest,"ElyGambit.Integration.StigmataManualPreview",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataManualPreviewTest::RunTest(const FString&)
{
 if(!FParse::Param(FCommandLine::Get(),TEXT("EGStigmataManualPreview")))
  {AddInfo(TEXT("Manual preview skipped: launch with -EGStigmataManualPreview to opt in."));return true;}
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Manual preview requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Manual preview requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(FIntPoint(1920,1080)));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStigmataManualPreview(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataBondPresentation,"ElyGambit.Integration.StigmataBondPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataBondPresentation::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Stigmata bond PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Stigmata bond PIE requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(FIntPoint(1600,900)));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStigmataBondPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGStigmataReferenceCPresentation,"ElyGambit.Integration.StigmataReferenceCPresentation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGStigmataReferenceCPresentation::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Stigmata reference PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Stigmata reference PIE requires a rendering RHI"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(FIntPoint(1920,1080)));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStigmataBondPIE(this,true));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPreparationCaptureNine,"ElyGambit.Integration.PreparationCapture900",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPreparationCaptureNine::RunTest(const FString&){return QueuePreparationCapture(this,EStigmataRoute::Nine);}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPreparationCaptureWide,"ElyGambit.Integration.PreparationCaptureWide",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPreparationCaptureWide::RunTest(const FString&){return QueuePreparationCapture(this,EStigmataRoute::Herrscher);}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGVillVVitalsPIETest,"ElyGambit.Integration.VillVVitalsPIE",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGVillVVitalsPIETest::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Vill-V vitals PIE requires an idle editor"));return false;}
 if(!FApp::CanEverRender()){AddError(TEXT("Vill-V vitals PIE requires a rendering RHI"));return false;}
 // The vitals fixture starts in the production board world.
 const FString Map=TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7");
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(FIntPoint(1920,1080)));
 ADD_LATENT_AUTOMATION_COMMAND(FEGVillVVitalsPIE(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGProductionPIELifecycle,"ElyGambit.Integration.ProductionPIELifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGProductionPIELifecycle::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Production PIE test requires an idle editor"));return false;}
 FString Map;
 if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),Map,GEngineIni)){AddError(TEXT("GameDefaultMap is missing"));return false;}
 const auto NineState=MakeShared<FEGPIECheckState>();
 const auto FiveState=MakeShared<FEGPIECheckState>();
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIETransactions(this,NineState,EStigmataRoute::Nine));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIETransactions(this,FiveState,EStigmataRoute::Five));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEmptyDeploymentPIE,"ElyGambit.Integration.ProductionEmptyDeployment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEmptyDeploymentPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Empty deployment PIE requires an idle editor"));return false;}
 FString Map;
 if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),Map,GEngineIni))return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIEEmptyDeployment(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGEmptyFusionPIE,"ElyGambit.Integration.ProductionEmptyFusion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGEmptyFusionPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Empty fusion PIE requires an idle editor"));return false;}
 FString Map;
 if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),Map,GEngineIni))return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIEEmptyFusion(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGPhasePausePIE,"ElyGambit.Integration.ProductionPhasePause",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGPhasePausePIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Phase pause PIE requires an idle editor"));return false;}
 FString Map;
 if(!GConfig->GetString(TEXT("/Script/EngineSettings.GameMapsSettings"),TEXT("GameDefaultMap"),Map,GEngineIni))return false;
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(Map));
 ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIEPhasePause(this));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNaturalFivePIE,"ElyGambit.Integration.ProductionNaturalFive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNaturalFivePIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Natural PIE requires an idle editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 if(FApp::CanEverRender()){ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(RouteViewportSize(EStigmataRoute::Five)));}
 else {ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));}
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIENaturalRun(this,EStigmataRoute::Five));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNaturalNinePIE,"ElyGambit.Integration.ProductionNaturalNine",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNaturalNinePIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Natural PIE requires an idle editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 if(FApp::CanEverRender()){ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(RouteViewportSize(EStigmataRoute::Nine)));}
 else {ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));}
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIENaturalRun(this,EStigmataRoute::Nine));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGNaturalHerrscherPIE,"ElyGambit.Integration.ProductionNaturalHerrscher",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGNaturalHerrscherPIE::RunTest(const FString&)
{
 if(!GEditor||GEditor->PlayWorld){AddError(TEXT("Natural PIE requires an idle editor"));return false;}
 ADD_LATENT_AUTOMATION_COMMAND(FEditorLoadMap(TEXT("/Game/Assets/Maps/Gameplay/L_ElysianBoard_7x7")));
 if(FApp::CanEverRender()){ADD_LATENT_AUTOMATION_COMMAND(FEGStartSizedPIE(RouteViewportSize(EStigmataRoute::Herrscher)));}
 else {ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));}
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIENaturalRun(this,EStigmataRoute::Herrscher));
 ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
 ADD_LATENT_AUTOMATION_COMMAND(FEGPIECleanup(this));
 return true;
}
#endif
