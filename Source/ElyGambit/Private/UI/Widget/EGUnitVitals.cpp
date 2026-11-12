#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGHeroInfoWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Character/EGChessUnit.h"
#include "Character/EGVillVEmplacement.h"
#include "Game/EGHeroRoster.h"
#include "Components/EGHeroSkillComponent.h"
#include "Components/EGVillVMechanismComponent.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/EGBattleContextComponent.h"
#include "AbilitySystem/EGAttributeSet.h"
#include "Data/EGHeroDefinition.h"
#include "Engine/Font.h"
#include "TimerManager.h"
#include "Misc/PackageName.h"
#include "UI/EGVisualStyle.h"
#include "Skills/EGElysiaSkillBehavior.h"
#include "Skills/EGElysiaHerrscherSkillBehavior.h"

namespace
{
bool HasDedicatedBossHealth(const AEGChessUnit* Unit)
{
 if(!IsValid(Unit))return false;
 const auto* Corruption=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();
 return Corruption&&Corruption->IsBoss();
}
FEGUnitVitalsStyle CombatMetrics(FEGUnitVitalsStyle Style,const AEGChessUnit* Unit)
{
 Style.HealthSize=FVector2D(80,10);
 Style.ManaSize=FVector2D(64,5);
 Style.CombatFontSize=14;
 if(Unit&&(Unit->GetHeroSkill()->bSummon||Unit->IsAuxiliaryEmplacement()))
 {Style.HealthSize=FVector2D(64,8);Style.ManaSize=FVector2D(50,4);Style.CombatFontSize=12;}
 else if(Unit)
  if(const auto* Corruption=Unit->FindComponentByClass<UEGCorruptionCombatComponent>();Corruption&&Corruption->IsBoss())
  {Style.HealthSize=FVector2D(112,12);Style.CombatFontSize=16;}
 return Style;
}
void Place(UCanvasPanel* Parent,UWidget* Widget,FVector2D P,FVector2D Size,int32 Z=0)
{
 auto* Slot=Cast<UCanvasPanelSlot>(Widget->Slot);
 if(Widget->GetParent()!=Parent)Slot=Parent->AddChildToCanvas(Widget);
 Slot->SetPosition(P);Slot->SetSize(Size);Slot->SetZOrder(Z);
}
UTextBlock* VitalsLabel(UWidgetTree* Tree,FName Name,const FSlateFontInfo& Font)
{
 if(auto* Existing=Cast<UTextBlock>(Tree->FindWidget(Name))){Existing->SetFont(Font);return Existing;}
 auto* Result=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Name);
 Result->SetFont(Font);
 Result->SetText(FText::AsNumber(1));Result->SetJustification(ETextJustify::Center);
 Result->SetColorAndOpacity(FSlateColor(FLinearColor(.97f,.94f,.87f,1.f)));
 return Result;
}
// WBP_UnitVitals may have been authored before this status row existed. Create
// missing children on the instance as well as on newly built native layouts.
void EnsurePassiveRow(UWidgetTree* Tree,UCanvasPanel* Combat)
{
 if(!Tree||!Combat)return;
 auto* Label=VitalsLabel(Tree,TEXT("ElysiaPassiveLabel"),FEGVisualStyle::BodyFont(10));
 auto Font=Label->GetFont();Font.OutlineSettings.OutlineSize=1;Font.OutlineSettings.OutlineColor=FLinearColor(0,0,0,.92f);Label->SetFont(Font);
 Label->SetColorAndOpacity(FSlateColor(FLinearColor(1.f,.75f,.91f,1.f)));
 Place(Combat,Label,FVector2D(0,21),FVector2D(128,15),3);
 auto* Bar=Cast<UProgressBar>(Tree->FindWidget(TEXT("ElysiaPassiveBar")));
 if(!Bar)Bar=Tree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("ElysiaPassiveBar"));
 Bar->SetFillColorAndOpacity(FLinearColor(1.f,.35f,.66f,1.f));
 Place(Combat,Bar,FVector2D(32,38),FVector2D(64,3),3);
 Label->SetVisibility(ESlateVisibility::Collapsed);Bar->SetVisibility(ESlateVisibility::Collapsed);
}
/** Moves a child into an overlay slot with the requested alignment. */
void DockOverlay(UOverlay* Host,UWidget* Child,EHorizontalAlignment H,EVerticalAlignment V)
{
 if(!Host||!Child)return;
 if(Child->GetParent()!=Host)
 {
  Child->RemoveFromParent();
  Host->AddChildToOverlay(Child);
 }
 if(auto* Slot=Cast<UOverlaySlot>(Child->Slot))
 {
  Slot->SetPadding(FMargin(0));Slot->SetHorizontalAlignment(H);Slot->SetVerticalAlignment(V);
 }
}
}

UClass* UEGUnitVitals::ResolveWidgetClass()
{
 if(FPackageName::DoesPackageExist(TEXT("/Game/Blueprints/UI/Widgets/WBP_UnitVitals")))
  if(auto* C=LoadClass<UEGUnitVitals>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/WBP_UnitVitals.WBP_UnitVitals_C")))return C;
 return StaticClass();
}
void UEGUnitVitals::BuildLayout(UWidgetTree* Tree)
{
 if(!Tree->RootWidget)Tree->RootWidget=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("VitalsRoot"));
 auto* Root=Cast<UCanvasPanel>(Tree->RootWidget);if(!Root)return;
 Root->SetVisibility(ESlateVisibility::HitTestInvisible);
 // Retain old named decoration for Blueprint compatibility; it is no longer rendered.
 for(const TCHAR* Name:{TEXT("AwakeningCrown"),TEXT("HealthBarFrame"),TEXT("ManaBarFrame"),TEXT("ShieldBarFrame"),TEXT("ShieldBar")})
  if(auto* W=Tree->FindWidget(FName(Name)))W->SetVisibility(ESlateVisibility::Collapsed);
 auto Group=[&](const TCHAR* Name)
 {
  auto* C=Cast<UCanvasPanel>(Tree->FindWidget(FName(Name)));
  if(!C)C=Tree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),FName(Name));
  Place(Root,C,FVector2D::ZeroVector,FVector2D(128,72));return C;
 };
 auto* Prep=Group(TEXT("PreparationGroup"));auto* Combat=Group(TEXT("CombatGroup"));
 auto* Ring=Cast<UEGAwakeningRing>(Tree->FindWidget(TEXT("AwakeningRing")));
 if(!Ring)Ring=Tree->ConstructWidget<UEGAwakeningRing>(UEGAwakeningRing::StaticClass(),TEXT("AwakeningRing"));
 Place(Prep,Ring,FVector2D(42,20),FVector2D(44));
 // WXQ medal hosts: medallion + centred number live in one overlay so the level
 // stays a plain editable TextBlock while the chrome is a vector widget.
 auto EnsureBadge=[&](UCanvasPanel* Parent,const TCHAR* HostName,const TCHAR* MedalName,const TCHAR* TextName)->UOverlay*
 {
  auto* Host=Cast<UOverlay>(Tree->FindWidget(FName(HostName)));
  if(!Host)Host=Tree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),FName(HostName));
  Place(Parent,Host,FVector2D::ZeroVector,FVector2D(22,22),2);
  auto* Medal=Cast<UEGLevelMedal>(Tree->FindWidget(FName(MedalName)));
  if(!Medal)Medal=Tree->ConstructWidget<UEGLevelMedal>(UEGLevelMedal::StaticClass(),FName(MedalName));
  auto* Text=VitalsLabel(Tree,FName(TextName),FEGVisualStyle::NumeralFont(13));
  DockOverlay(Host,Medal,HAlign_Fill,VAlign_Fill);
  DockOverlay(Host,Text,HAlign_Center,VAlign_Center);
  return Host;
 };
 EnsureBadge(Prep,TEXT("PreparationBadge"),TEXT("PreparationMedal"),TEXT("PreparationLevel"));
 EnsureBadge(Combat,TEXT("LevelBadge"),TEXT("LevelMedal"),TEXT("LevelLabel"));
 auto Bar=[&](const TCHAR* Name,FVector2D P,FVector2D Size)
 {
  auto* B=Cast<UEGVitalsBar>(Tree->FindWidget(FName(Name)));
  if(!B)
  {
   if(auto* Old=Tree->FindWidget(FName(Name))){Old->RemoveFromParent();Old->Rename(nullptr,GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);}
   B=Tree->ConstructWidget<UEGVitalsBar>(UEGVitalsBar::StaticClass(),FName(Name));
  }
  Place(Combat,B,P,Size);return B;
 };
 const FEGUnitVitalsStyle DefaultStyle;
 auto* HealthPreview=Bar(TEXT("HealthBar"),FVector2D(20,41),DefaultStyle.HealthSize);
 auto* ManaPreview=Bar(TEXT("ManaBar"),FVector2D(34,57),DefaultStyle.ManaSize);
 HealthPreview->SetPercent(1.f);HealthPreview->SetFillColorAndOpacity(DefaultStyle.Ally);HealthPreview->SetHighlightColor(DefaultStyle.AllyHighlight);
 ManaPreview->SetPercent(.65f);ManaPreview->SetFillColorAndOpacity(DefaultStyle.Mana);ManaPreview->SetHighlightColor(DefaultStyle.ManaHighlight);
 auto* Status=VitalsLabel(Tree,TEXT("StatusLabel"),FEGVisualStyle::BodyFont(10));Status->SetText(FText::GetEmpty());
 Place(Combat,Status,FVector2D(-16,7),FVector2D(160,18),3);
 EnsurePassiveRow(Tree,Combat);
 ApplyLayoutMetrics(Tree);
 Prep->SetVisibility(ESlateVisibility::Collapsed);Combat->SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UEGUnitVitals::ApplyLayoutMetrics(UWidgetTree* Tree,const FEGUnitVitalsStyle& S,bool bHasMana)
{
 if(!Tree)return;
 auto Move=[Tree](const TCHAR* Name,FVector2D P,FVector2D Size)
 {if(auto* W=Tree->FindWidget(FName(Name)))if(auto* Slot=Cast<UCanvasPanelSlot>(W->Slot)){Slot->SetPosition(P);Slot->SetSize(Size);}};
 const float Bottom=72.f-S.HeadGap;
 const float StackHeight=S.HealthSize.Y+S.BarGap+(bHasMana?S.ManaSize.Y:0.f);
 const float HealthY=Bottom-StackHeight;
 const float Centre=64.f;
 Move(TEXT("HealthBar"),FVector2D(Centre-S.HealthSize.X*.5f,HealthY),S.HealthSize);
 if(S.bBadgeAboveBars)
  Move(TEXT("LevelBadge"),FVector2D(Centre-S.LevelBadgeSize*.5f,HealthY-S.BadgeOverlap-S.LevelBadgeSize),FVector2D(S.LevelBadgeSize));
 else
  Move(TEXT("LevelBadge"),FVector2D(Centre-S.HealthSize.X*.5f-S.LevelBadgeSize+S.BadgeOverlap,
   HealthY+S.HealthSize.Y*.5f-S.LevelBadgeSize*.5f),FVector2D(S.LevelBadgeSize));
 if(bHasMana)Move(TEXT("ManaBar"),FVector2D(Centre-S.ManaSize.X*.5f,HealthY+S.HealthSize.Y+S.BarGap),S.ManaSize);
 // Preparation: level diamond centred inside the awakening ring.
 Move(TEXT("AwakeningRing"),FVector2D(Centre-S.RingDiameter*.5f,Bottom-S.RingDiameter),FVector2D(S.RingDiameter));
 const float PrepBadge=S.RingDiameter*.60f;
 Move(TEXT("PreparationBadge"),FVector2D(Centre-PrepBadge*.5f,Bottom-S.RingDiameter*.5f-PrepBadge*.5f),FVector2D(PrepBadge));
 // Styles flow from the single Class Defaults token set into the vector widgets.
 if(auto* Health=Cast<UEGVitalsBar>(Tree->FindWidget(TEXT("HealthBar"))))
 {
  Health->FrameColor=S.FrameBlack;Health->RimColor=S.Rim;Health->TrackColor=S.TrackBottom;
  Health->TrackTopColor=S.TrackTop;Health->ShieldColor=S.Shield;Health->Segments=S.HealthSegments;
 }
 if(auto* Mana=Cast<UEGVitalsBar>(Tree->FindWidget(TEXT("ManaBar"))))
 {
  Mana->FrameColor=S.FrameBlack;Mana->RimColor=S.ManaRim;Mana->TrackColor=S.TrackBottom;
  Mana->TrackTopColor=S.TrackTop;Mana->ShieldColor=S.Shield;Mana->Segments=S.ManaSegments;
 }
 for(const TCHAR* MedalName:{TEXT("LevelMedal"),TEXT("PreparationMedal")})
  if(auto* Medal=Cast<UEGLevelMedal>(Tree->FindWidget(MedalName)))
  {
   Medal->Rim=S.BadgeRim;Medal->RimBright=S.BadgeRimBright;Medal->RimShadow=S.BadgeRimShadow;
   Medal->Face=S.BadgeFace;Medal->FaceGloss=S.BadgeFaceGloss;Medal->InnerLine=S.BadgeRim;
   Medal->AwakenedRim=S.BadgeAwakenedRim;Medal->AwakenedFace=S.BadgeAwakenedFace;Medal->AwakenedGlow=S.BadgeAwakenedRim;
  }
 if(auto* RingWidget=Cast<UEGAwakeningRing>(Tree->FindWidget(TEXT("AwakeningRing"))))
 {
  RingWidget->FrameColor=S.RingRim;RingWidget->DiskColor=S.RingDisk;RingWidget->TrackColor=S.RingTrack;
  RingWidget->ProgressColor=S.RingProgress;RingWidget->AwakenedColor=S.RingAwakened;
 }
 for(const auto Pair:{TPair<const TCHAR*,int32>(TEXT("PreparationLevel"),S.PreparationFontSize),TPair<const TCHAR*,int32>(TEXT("LevelLabel"),S.CombatFontSize),TPair<const TCHAR*,int32>(TEXT("StatusLabel"),10)})
  if(auto* T=Cast<UTextBlock>(Tree->FindWidget(FName(Pair.Key))))
  {auto F=T->GetFont();F.Size=Pair.Value;F.OutlineSettings.OutlineSize=1;F.OutlineSettings.OutlineColor=FLinearColor(0.f,0.f,0.f,.92f);T->SetFont(F);}
}
void UEGUnitVitals::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 VitalsStyle.RingDiameter=36.f;
 VitalsStyle.HealthSize=FVector2D(80,10);
 VitalsStyle.ManaSize=FVector2D(64,5);
 VitalsStyle.LevelBadgeSize=18.f;
 VitalsStyle.bBadgeAboveBars=false;
 VitalsStyle.HeadGap=8.f;
 VitalsStyle.BarGap=2.f;
 VitalsStyle.CombatFontSize=14;
 if(!WidgetTree->RootWidget||!WidgetTree->FindWidget(TEXT("LevelBadge")))BuildLayout(WidgetTree);
 ApplyLayoutMetrics(WidgetTree,VitalsStyle);SetRenderTransformPivot(FVector2D(.5,1));SetRenderScale(FVector2D(1));
 PreparationContainer=Cast<UCanvasPanel>(GetWidgetFromName(TEXT("PreparationGroup")));CombatContainer=Cast<UCanvasPanel>(GetWidgetFromName(TEXT("CombatGroup")));
 EnsurePassiveRow(WidgetTree,CombatContainer);
 RingGraphic=Cast<UEGAwakeningRing>(GetWidgetFromName(TEXT("AwakeningRing")));
 CombatMedal=Cast<UEGLevelMedal>(GetWidgetFromName(TEXT("LevelMedal")));
 PrepMedal=Cast<UEGLevelMedal>(GetWidgetFromName(TEXT("PreparationMedal")));
 PreparationLabel=Cast<UTextBlock>(GetWidgetFromName(TEXT("PreparationLevel")));
 Label=Cast<UTextBlock>(GetWidgetFromName(TEXT("LevelLabel")));Health=Cast<UProgressBar>(GetWidgetFromName(TEXT("HealthBar")));
 Mana=Cast<UProgressBar>(GetWidgetFromName(TEXT("ManaBar")));StatusText=Cast<UTextBlock>(GetWidgetFromName(TEXT("StatusLabel")));
 PassiveText=Cast<UTextBlock>(GetWidgetFromName(TEXT("ElysiaPassiveLabel")));PassiveBar=Cast<UProgressBar>(GetWidgetFromName(TEXT("ElysiaPassiveBar")));
 ApplyMode(EEGVitalsMode::Unbound);
}
void UEGUnitVitals::NativeConstruct()
{
 Super::NativeConstruct();
 bPresentationConstructed=true;
 if(Unit.IsValid())SetUnit(Unit.Get());else if(PreparationRoster.IsValid())SetPreparationHero(PreparationRoster.Get(),PreparationId);
}
void UEGUnitVitals::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
 Super::NativeTick(MyGeometry,InDeltaTime);
 SynchronizePresentationState();
 if(DisplayMode==EEGVitalsMode::Combat&&bPresentationConstructed)
 {
  if(PassiveCaptionRemaining>0.f)
  {
   PassiveCaptionRemaining=FMath::Max(0.f,PassiveCaptionRemaining-InDeltaTime);
   if(PassiveCaptionRemaining<=0.f){PassiveCaption.Empty();RefreshStatusCaption();}
  }
  PassiveRefreshElapsed+=InDeltaTime;
  if(PassiveRefreshElapsed>=.2f){PassiveRefreshElapsed=0.f;RefreshPassive();}
 }
}
void UEGUnitVitals::SynchronizePresentationState()
{
 if(!bPresentationConstructed||DisplayMode!=EEGVitalsMode::Combat||!Unit.IsValid())return;
 SynchronizeVillVVitals();
 // The encounter attaches its Boss component after this widget has bound.
 // Keep the outer widget ticking and only hide the redundant head-up group.
 if(CombatContainer)
 {
  const bool bHide=bOccupiedDeviceVitalsHidden||HasDedicatedBossHealth(Unit.Get());
  const ESlateVisibility Desired=bHide?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible;
  if(CombatContainer->GetVisibility()!=Desired)CombatContainer->SetVisibility(Desired);
 }
}
void UEGUnitVitals::ApplyMode(EEGVitalsMode Mode)
{
 DisplayMode=Mode;
 SetVisibility(Mode==EEGVitalsMode::Unbound?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
 if(PreparationContainer)PreparationContainer->SetVisibility(Mode==EEGVitalsMode::Preparation?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 if(CombatContainer)CombatContainer->SetVisibility(Mode==EEGVitalsMode::Combat&&!bOccupiedDeviceVitalsHidden&&!HasDedicatedBossHealth(Unit.Get())?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
}
void UEGUnitVitals::SetPreparationHero(UEGHeroRoster* Roster,FGuid HeroId)
{
 SetUnit(nullptr);PreparationRoster=Roster;PreparationId=HeroId;
 if(Roster)Roster->OnHeroChanged.AddUniqueDynamic(this,&ThisClass::RosterChanged);
 RosterChanged(HeroId);
}
void UEGUnitVitals::RosterChanged(FGuid HeroId)
{
 if(HeroId.IsValid()&&HeroId!=PreparationId)return;
 FEGHeroInstance H;
 if(!PreparationRoster.IsValid()||!PreparationRoster->GetHero(PreparationId,H)){ApplyMode(EEGVitalsMode::Unbound);return;}
 const auto* D=PreparationRoster->ResolveDefinition(PreparationId);
 if(!D){ApplyMode(EEGVitalsMode::Unbound);return;}
 ApplyMode(EEGVitalsMode::Preparation);
 if(PreparationLabel)
 {
  PreparationLabel->SetText(FText::AsNumber(H.Level));
  PreparationLabel->SetColorAndOpacity(FSlateColor(H.bAwakened?VitalsStyle.BadgeAwakenedNumber:VitalsStyle.BadgeNumber));
 }
 if(PrepMedal)PrepMedal->SetAwakened(H.bAwakened);
 if(RingGraphic)RingGraphic->SetProgress(H.SameNameCards,D->GetAwakeningCardCount(),H.bAwakened);
 RefreshSkillTooltip(D,FText::FromString(FString::Printf(TEXT("Lv.%d  %s"),H.Level,H.bAwakened?TEXT("已觉醒"):TEXT("未觉醒"))));
}
void UEGUnitVitals::Refresh()
{
 if(!Unit.IsValid()||!Health||!Unit->GetDefinition()){ApplyMode(EEGVitalsMode::Unbound);return;}
 VitalsStyle=CombatMetrics(VitalsStyle,Unit.Get());
 const auto& Layout=VitalsStyle;
 ApplyLayoutMetrics(WidgetTree,Layout,Unit->GetAttributes()->GetMaxMana()>0.f);
 if(CombatMedal)CombatMedal->SetVisibility(ESlateVisibility::HitTestInvisible);
 ApplyMode(EEGVitalsMode::Combat);SynchronizeVillVVitals();RefreshHealth();RefreshMana();RefreshIdentity();StatusChanged();
 if(!Unit->IsAlive_Implementation()&&!Unit->GetHeroSkill()->IsRecoverable())
  SetVisibility(ESlateVisibility::Collapsed);
}
void UEGUnitVitals::RefreshHealth()
{
 if(!Unit.IsValid()||!Health)return;
 const FEGUnitVitalsStyle Layout=CombatMetrics(VitalsStyle,Unit.Get());
 if(Layout.HealthSize!=VitalsStyle.HealthSize)
 {VitalsStyle=Layout;ApplyLayoutMetrics(WidgetTree,VitalsStyle,Unit->GetAttributes()->GetMaxMana()>0.f);}
 Health->SetVisibility(ESlateVisibility::HitTestInvisible);
 const auto* Source=HealthSource.IsValid()?HealthSource.Get():Unit.Get();
 const auto* A=Source->GetAttributes();if(!A)return;
 Health->SetPercent(FMath::Clamp(A->GetHealth()/FMath::Max(1.f,A->GetMaxHealth()),0.f,1.f));
 const bool Ally=IEGCombatant::Execute_GetFaction(Unit.Get())==EEGFaction::Player;
 Health->SetFillColorAndOpacity(Ally?VitalsStyle.Ally:VitalsStyle.Enemy);
 if(auto* Bar=Cast<UEGVitalsBar>(Health))
 {Bar->SetHighlightColor(Ally?VitalsStyle.AllyHighlight:VitalsStyle.EnemyHighlight);Bar->SetShieldFraction(Source->GetHeroSkill()->Shield/FMath::Max(1.f,A->GetMaxHealth()));}
}
void UEGUnitVitals::RefreshMana()
{
 if(!Unit.IsValid()||!Mana)return;const auto* A=Unit->GetAttributes();const bool HasMana=A->GetMaxMana()>0;
 Mana->SetVisibility(HasMana?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 Mana->SetFillColorAndOpacity(VitalsStyle.Mana);Mana->SetPercent(FMath::Clamp(A->GetMana()/FMath::Max(1.f,A->GetMaxMana()),0.f,1.f));
 if(auto* Bar=Cast<UEGVitalsBar>(Mana))Bar->SetHighlightColor(VitalsStyle.ManaHighlight);
 if(HasMana!=bLastHadMana)
 {
  bLastHadMana=HasMana;
  ApplyLayoutMetrics(WidgetTree,VitalsStyle,HasMana);
 }
}
void UEGUnitVitals::RefreshIdentity()
{
 if(!Unit.IsValid()||!Label||!Unit->GetDefinition())return;
 const bool bAwakened=Unit->GetHeroSkill()->bAwakened;
 Label->SetText(FText::AsNumber(Unit->GetHeroSkill()->GetLevel()));
 Label->SetColorAndOpacity(FSlateColor(bAwakened?VitalsStyle.BadgeAwakenedNumber:VitalsStyle.BadgeNumber));
 if(CombatMedal)CombatMedal->SetAwakened(bAwakened);
 if(TooltipDefinition.Get()==Unit->GetDefinition()&&TooltipLevel==Unit->GetHeroSkill()->GetLevel()&&bTooltipAwakened==Unit->GetHeroSkill()->bAwakened)return;
 TooltipDefinition=Unit->GetDefinition();TooltipLevel=Unit->GetHeroSkill()->GetLevel();bTooltipAwakened=Unit->GetHeroSkill()->bAwakened;
 // The tooltip already renders CardInformation::Effect. Only append live state
 // here, rather than repeating the card's static description in SkillExtra.
 FString Info=FString::Printf(TEXT("Lv.%d  %s"),TooltipLevel,bTooltipAwakened?TEXT("已觉醒"):TEXT("未觉醒"));
 if(Unit->GetHeroSkill()->bSummon&&Unit->GetHeroSkill()->Battle.IsValid())Info+=FString::Printf(TEXT("\n出生等级已包含本场召唤加成 +%d；主人升级不改变已有召唤物等级。"),Unit->GetHeroSkill()->Battle->GetSummonBonus());
 RefreshSkillTooltip(Unit->GetDefinition(),FText::FromString(Info));
}
void UEGUnitVitals::RefreshSkillTooltip(const UEGHeroDefinition* Definition,const FText& Extra)
{
 if(!SkillTooltipView)
 {
  auto* Class=LoadClass<UEGHeroInfoWidget>(nullptr,TEXT("/Game/Blueprints/UI/Widgets/Board/WBP_HeroSkillTooltip.WBP_HeroSkillTooltip_C"));
  SkillTooltipView=CreateWidget<UEGHeroInfoWidget>(GetWorld(),Class?Class:UEGHeroInfoWidget::StaticClass());
 }
 if(SkillTooltipView){SkillTooltipView->Present(Definition,Extra);SetToolTip(SkillTooltipView);}
}
void UEGUnitVitals::SetHealthSource(AEGChessUnit* NewSource)
{
 AEGChessUnit* Previous=HealthSource.Get();
 if(Previous==NewSource)return;
 if(Previous&&Previous!=Unit.Get())
 {
  Previous->OnAttributeChanged.RemoveDynamic(this,&ThisClass::HealthSourceAttributeChanged);
  Previous->OnCombatStateChanged.RemoveDynamic(this,&ThisClass::HealthSourceStateChanged);
  Previous->OnDestroyed.RemoveDynamic(this,&ThisClass::HealthSourceDestroyed);
  if(Previous->GetHeroSkill())Previous->GetHeroSkill()->OnStatusChanged.RemoveDynamic(this,&ThisClass::HealthSourceStatusChanged);
 }
 HealthSource=NewSource;
 if(NewSource&&NewSource!=Unit.Get())
 {
  NewSource->OnAttributeChanged.AddUniqueDynamic(this,&ThisClass::HealthSourceAttributeChanged);
  NewSource->OnCombatStateChanged.AddUniqueDynamic(this,&ThisClass::HealthSourceStateChanged);
  NewSource->OnDestroyed.AddUniqueDynamic(this,&ThisClass::HealthSourceDestroyed);
  if(NewSource->GetHeroSkill())NewSource->GetHeroSkill()->OnStatusChanged.AddUniqueDynamic(this,&ThisClass::HealthSourceStatusChanged);
 }
}
void UEGUnitVitals::SynchronizeVillVVitals()
{
 AEGChessUnit* OwnerUnit=Unit.Get();if(!OwnerUnit)return;
 AEGVillVEmplacement* Device=Cast<AEGVillVEmplacement>(OwnerUnit);
 AEGChessUnit* Pilot=Device?Device->Pilot.Get():OwnerUnit;
 UEGVillVMechanismComponent* Mechanism=Pilot?Pilot->FindComponentByClass<UEGVillVMechanismComponent>():nullptr;
 AEGVillVEmplacement* Occupied=nullptr;
 if(Mechanism&&Mechanism->IsOperating()&&IsValid(Pilot)&&Pilot->IsAlive_Implementation())
 {
  AEGVillVEmplacement* Candidate=Mechanism->GetDevice(Mechanism->Mode());
  if(IsValid(Candidate)&&!Candidate->IsActorBeingDestroyed()&&Candidate->Pilot.Get()==Pilot&&Candidate->IsAlive_Implementation())Occupied=Candidate;
 }
 const bool bHideDevice=Device&&Device==Occupied;
 if(bOccupiedDeviceVitalsHidden!=bHideDevice)
 {
  bOccupiedDeviceVitalsHidden=bHideDevice;
  // Keep the outer widget ticking so a seat change can show the group again.
  if(CombatContainer&&DisplayMode==EEGVitalsMode::Combat)
   CombatContainer->SetVisibility(bHideDevice||HasDedicatedBossHealth(OwnerUnit)?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
 }
 AEGChessUnit* DesiredSource=!Device&&Occupied?static_cast<AEGChessUnit*>(Occupied):OwnerUnit;
 if(HealthSource.Get()!=DesiredSource)
 {
  SetHealthSource(DesiredSource);
  RefreshHealth();
 }
}
void UEGUnitVitals::HealthSourceAttributeChanged(FName Name,float Old,float Value)
{
 if(Name==TEXT("Health")||Name==TEXT("MaxHealth"))RefreshHealth();
}
void UEGUnitVitals::HealthSourceStateChanged(EEGUnitState State)
{
 SynchronizeVillVVitals();
}
void UEGUnitVitals::HealthSourceStatusChanged()
{
 RefreshHealth();
}
void UEGUnitVitals::HealthSourceDestroyed(AActor* DestroyedActor)
{
 SynchronizeVillVVitals();
}
void UEGUnitVitals::SetUnit(AEGChessUnit* InUnit)
{
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(StatusTimer);
 ClearPassive();
 SetHealthSource(nullptr);bOccupiedDeviceVitalsHidden=false;
 if(PreparationRoster.IsValid())PreparationRoster->OnHeroChanged.RemoveDynamic(this,&ThisClass::RosterChanged);
 PreparationRoster.Reset();PreparationId.Invalidate();
 if(Unit.IsValid())
 {Unit->OnAttributeChanged.RemoveDynamic(this,&ThisClass::AttributeChanged);Unit->OnCombatStateChanged.RemoveDynamic(this,&ThisClass::StateChanged);Unit->GetHeroSkill()->OnStatusChanged.RemoveDynamic(this,&ThisClass::StatusChanged);}
 Unit=InUnit;TooltipDefinition.Reset();TooltipLevel=INDEX_NONE;
 if(Unit.IsValid())
 {Unit->OnAttributeChanged.AddUniqueDynamic(this,&ThisClass::AttributeChanged);Unit->OnCombatStateChanged.AddUniqueDynamic(this,&ThisClass::StateChanged);Unit->GetHeroSkill()->OnStatusChanged.AddUniqueDynamic(this,&ThisClass::StatusChanged);}
 Refresh();
}
void UEGUnitVitals::AttributeChanged(FName Name,float Old,float Value)
{
 if(Name==TEXT("Health")||Name==TEXT("MaxHealth"))RefreshHealth();
 else if(Name==TEXT("Mana")||Name==TEXT("MaxMana"))RefreshMana();
}
void UEGUnitVitals::StateChanged(EEGUnitState State)
{
 if(State==EEGUnitState::Dead){ClearPassive();SetHealthSource(nullptr);bOccupiedDeviceVitalsHidden=false;ApplyMode(EEGVitalsMode::Unbound);}else Refresh();
}
void UEGUnitVitals::StatusChanged()
{
 if(!Unit.IsValid())return;
 RefreshHealth();RefreshIdentity();RefreshPassive();RefreshStatusCaption();
}
void UEGUnitVitals::RefreshStatusCaption()
{
 if(StatusText)
 {
  FString Caption=Unit.IsValid()&&Unit->GetHeroSkill()&&Unit->GetHeroSkill()->IsTauntActive()?TEXT("嘲讽"):TEXT("");
  if(PassiveCaptionRemaining>0.f&&!PassiveCaption.IsEmpty()){if(!Caption.IsEmpty())Caption+=TEXT(" · ");Caption+=PassiveCaption;}
  StatusText->SetText(FText::FromString(Caption));
  StatusText->SetVisibility(Caption.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
 }
}
void UEGUnitVitals::ClearPassive()
{
 PassiveBehavior.Reset();PassiveRound.Invalidate();PreviousPassivePhase=0;bPreviousPassiveReady=false;bHavePassiveSnapshot=false;
 PassiveRefreshElapsed=0.f;PassiveCaptionRemaining=0.f;PassiveCaption.Empty();
 if(PassiveText)PassiveText->SetVisibility(ESlateVisibility::Collapsed);
 if(PassiveBar)PassiveBar->SetVisibility(ESlateVisibility::Collapsed);
 RefreshStatusCaption();
}
void UEGUnitVitals::RefreshPassive()
{
 if(!Unit.IsValid()||!Unit->GetHeroSkill()||!Unit->IsAlive_Implementation()){ClearPassive();return;}
 auto* Behavior=Unit->GetHeroSkill()->GetBehavior();
 FEGElysiaPassiveState State;
 if(const auto* P=Cast<UEGElysiaSkillBehavior>(Behavior))State=P->GetPassiveState();
 else if(const auto* HumanBehavior=Cast<UEGElysiaHerrscherSkillBehavior>(Behavior))State=HumanBehavior->GetPassiveState();
 else {ClearPassive();return;}
 if(State.Phase==EEGElysiaPassivePhase::Inactive||!State.RoundId.IsValid()){ClearPassive();return;}
 if(PassiveBehavior.Get()!=Behavior||PassiveRound!=State.RoundId)
 {
  ClearPassive();PassiveBehavior=Behavior;PassiveRound=State.RoundId;
 }
 FString Caption;
 if(bHavePassiveSnapshot)
 {
  const auto Old=static_cast<EEGElysiaPassivePhase>(PreviousPassivePhase);
  if(State.Phase==EEGElysiaPassivePhase::Empowered&&Old!=State.Phase)Caption=TEXT("◆");
  else if(State.Phase==EEGElysiaPassivePhase::Origin&&Old!=State.Phase)Caption=TEXT("◆");
  else if((State.Phase==EEGElysiaPassivePhase::FinisherPending||State.Phase==EEGElysiaPassivePhase::Finishing)&&Old!=EEGElysiaPassivePhase::FinisherPending&&Old!=EEGElysiaPassivePhase::Finishing)Caption=TEXT("◆");
  else if(State.bNextAttackEnhanced&&!bPreviousPassiveReady)
  {
   if(State.Phase==EEGElysiaPassivePhase::Normal||State.Phase==EEGElysiaPassivePhase::Bow)Caption=TEXT("◆");
  }
 }
 PreviousPassivePhase=static_cast<int32>(State.Phase);bPreviousPassiveReady=State.bNextAttackEnhanced;bHavePassiveSnapshot=true;
 if(!Caption.IsEmpty()){PassiveCaption=Caption;PassiveCaptionRemaining=1.2f;RefreshStatusCaption();}
 FString Text;
 switch(State.Phase)
 {
 case EEGElysiaPassivePhase::Normal:Text=FString::Printf(TEXT("%d/%d"),State.Progress,State.Threshold);break;
 case EEGElysiaPassivePhase::Empowered:Text=FString::Printf(TEXT("%.1fs"),FMath::Max(0.f,State.RemainingSeconds));break;
 case EEGElysiaPassivePhase::Bow:Text=FString::Printf(TEXT("%d/%d"),State.Progress,State.Threshold);break;
 case EEGElysiaPassivePhase::Entering:Text=TEXT("◇");break;
 case EEGElysiaPassivePhase::Origin:Text=FString::Printf(TEXT("%.1fs"),FMath::Max(0.f,State.RemainingSeconds));break;
 case EEGElysiaPassivePhase::FinisherPending:Text=TEXT("◇");break;
 case EEGElysiaPassivePhase::Finishing:Text=TEXT("◆");break;
 default:break;
 }
 if(PassiveText)
 {
  PassiveText->SetText(FText::FromString(Text));
  PassiveText->SetColorAndOpacity(FSlateColor(State.bNextAttackEnhanced?FLinearColor(1.f,.91f,.55f,1.f):FLinearColor(1.f,.75f,.91f,1.f)));
  PassiveText->SetVisibility(ESlateVisibility::HitTestInvisible);
 }
 const bool bShowProgress=State.Threshold>0&&(State.Phase==EEGElysiaPassivePhase::Normal||State.Phase==EEGElysiaPassivePhase::Bow||State.Phase==EEGElysiaPassivePhase::Origin);
 if(PassiveBar)
 {
  PassiveBar->SetPercent(bShowProgress?FMath::Clamp(static_cast<float>(State.Progress)/State.Threshold,0.f,1.f):0.f);
  PassiveBar->SetFillColorAndOpacity(State.bNextAttackEnhanced?FLinearColor(1.f,.8f,.35f,1.f):FLinearColor(1.f,.35f,.66f,1.f));
  PassiveBar->SetVisibility(bShowProgress?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
 }
}
void UEGUnitVitals::NativeDestruct()
{
 bPresentationConstructed=false;
 ClearPassive();
 // A screen layer may reconstruct this widget. Retain weak source identity and
 // let NativeConstruct rebind; an explicit SetUnit(nullptr) still clears it.
 if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(StatusTimer);
 SetHealthSource(nullptr);bOccupiedDeviceVitalsHidden=false;
 if(PreparationRoster.IsValid())PreparationRoster->OnHeroChanged.RemoveDynamic(this,&ThisClass::RosterChanged);
 if(Unit.IsValid())
 {Unit->OnAttributeChanged.RemoveDynamic(this,&ThisClass::AttributeChanged);Unit->OnCombatStateChanged.RemoveDynamic(this,&ThisClass::StateChanged);Unit->GetHeroSkill()->OnStatusChanged.RemoveDynamic(this,&ThisClass::StatusChanged);}
 Super::NativeDestruct();
}
