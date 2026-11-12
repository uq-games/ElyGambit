#include "UI/Widget/EGCombatTextLayer.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Character/EGChessUnit.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "TimerManager.h"

FString UEGCombatNumber::FormatAmount(double Amount)
{
 if(!FMath::IsFinite(Amount)||Amount<=0)return FString();
 return Amount<1.f?TEXT("<1"):FString::Printf(TEXT("%.0f"),Amount);
}
void UEGCombatNumber::NativeOnInitialized()
{
 Super::NativeOnInitialized();SetVisibility(ESlateVisibility::HitTestInvisible);
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
 Number=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("CombatNumberValue"));
 FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),22);
 Font.OutlineSettings.OutlineSize=2;Font.OutlineSettings.OutlineColor=FLinearColor(.012f,.016f,.022f,1);
 Number->SetFont(Font);Number->SetJustification(ETextJustify::Center);
 auto* CanvasSlot=Root->AddChildToCanvas(Number);
 CanvasSlot->SetAutoSize(true);CanvasSlot->SetAlignment(FVector2D(.5f,0.f));CanvasSlot->SetPosition(FVector2D(90.f,0.f));
 SetRenderTransformPivot(FVector2D(.5,.5));
}
void UEGCombatNumber::Configure(double Amount,bool bHealing,bool bAbsorbed,bool bEnemyDamage)
{
 bShield=bAbsorbed;
 // Center the whole shield+gap+text group, including the shield's dark stroke.
 if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(Number->Slot))CanvasSlot->SetPosition(FVector2D(bShield?101.5f:90.f,0.f));
 FSlateFontInfo Font=FCoreStyle::GetDefaultFontStyle(TEXT("Bold"),bEnemyDamage?16:(bHealing||bShield?20:22));
 Font.OutlineSettings.OutlineSize=2;Font.OutlineSettings.OutlineColor=FLinearColor(.012f,.016f,.022f,1);
 Number->SetFont(Font);
 SetAmount(Amount,bHealing);
 Number->SetColorAndOpacity(bHealing?FLinearColor(.35f,1.f,.48f):bEnemyDamage?FLinearColor(1.f,.06f,.04f):bShield?FLinearColor(.5f,.85f,1):FLinearColor(1.f,.96f,.87f));
}
void UEGCombatNumber::SetAmount(double Amount,bool bHealing)
{Number->SetText(FText::FromString((bHealing?TEXT("+"):TEXT(""))+FormatAmount(Amount)));}
bool FEGActiveCombatNumber::TryAccumulate(const FGuid& CandidateId,bool bCandidateHealing,bool bCandidateShield,double Now,float Delta)
{
 if(TargetId!=CandidateId||bHealing!=bCandidateHealing||bShield!=bCandidateShield||
    !FMath::IsFinite(Now)||!FMath::IsFinite(Delta)||Delta<=0.f||Now<StartedAt||Now-StartedAt>=.12)
  return false;
 Amount+=static_cast<double>(Delta);
 return true;
}
float FEGActiveCombatNumber::ChooseStartOffsetY(const TArray<FEGActiveCombatNumber>& Entries,const FGuid& CandidateId,double Now,float NewDuration,float PreferredOffset)
{
 if(!FMath::IsFinite(Now)||!FMath::IsFinite(NewDuration)||NewDuration<=0.f)return PreferredOffset;
 // The two lifetimes have different speeds (44 units over .85 or .95 s).
 // A lane is forbidden if their vertical positions could approach within this
 // distance at any point while both are alive, not merely at spawn time.
 constexpr double Gap=34.;
 struct FInterval { double Min,Max; };
 TArray<FInterval,TInlineAllocator<6>> Forbidden;
 for(const auto& Entry:Entries)
 {
  if(Entry.TargetId!=CandidateId||Entry.bEnemyDamage||Entry.Duration<=0.f)continue;
  const double Elapsed=FMath::Max(0.,Now-Entry.StartedAt);
  const double Remaining=Entry.Duration-Elapsed;
  if(Remaining<=0.)continue;
  const double Overlap=FMath::Min(Remaining,static_cast<double>(NewDuration));
  const double ExistingY=Entry.StartOffsetY-44.*Elapsed/Entry.Duration;
  const double EndBoundary=ExistingY+44.*Overlap*(1./NewDuration-1./Entry.Duration);
  Forbidden.Add({FMath::Min(ExistingY,EndBoundary)-Gap,FMath::Max(ExistingY,EndBoundary)+Gap});
 }
 double Best=PreferredOffset,BestDistance=MAX_dbl;
 const auto TryCandidate=[&](double Candidate)
 {
  for(const auto& Interval:Forbidden)if(Candidate>Interval.Min&&Candidate<Interval.Max)return;
  const double Distance=FMath::Abs(Candidate-PreferredOffset);
  if(Distance<BestDistance){Best=Candidate;BestDistance=Distance;}
 };
 TryCandidate(PreferredOffset);
 for(const auto& Interval:Forbidden){TryCandidate(Interval.Min-.01);TryCandidate(Interval.Max+.01);}
 return static_cast<float>(Best);
}
void FEGActiveCombatNumber::SampleEnemyDamage(float T,int8 Direction,FVector2D& Offset,float& Scale,float& Opacity)
{
 T=FMath::Clamp(T,0.f,1.f);
 const float Side=Direction<0?-1.f:1.f;
 // A short arc with its peak near the middle, then a quick side/down exit.
 Offset=FVector2D(Side*(12.f+58.f*T),-196.f*T+208.f*T*T);
 constexpr float Peak=196.f/416.f;
 Scale=T<Peak?FMath::Lerp(.9f,1.12f,T/Peak):FMath::Lerp(1.12f,.58f,(T-Peak)/(1.f-Peak));
 Opacity=FMath::Clamp((1.f-T)/.40f,0.f,1.f);
}
float FEGActiveCombatNumber::ChooseEnemyStartOffsetY(const TArray<FEGActiveCombatNumber>& Entries,const FGuid& CandidateId,double Now,int8 Direction)
{
 // Keep the cluster near the target even when all lanes are occupied. Opposite
 // enemy-number directions separate horizontally; other kinds stay centered.
 constexpr float Candidates[]={-8.f,-28.f,12.f,-48.f,32.f};
 float Best=Candidates[0],BestClearance=-1.f;
 for(const float Candidate:Candidates)
 {
  float Clearance=MAX_flt;
  for(const auto& Entry:Entries)
  {
   if(Entry.TargetId!=CandidateId||Entry.Duration<=0.f||
      (Entry.bEnemyDamage&&Entry.Direction!=Direction))continue;
   const double Elapsed=Now-Entry.StartedAt;
   if(!FMath::IsFinite(Elapsed)||Elapsed<0.||Elapsed>=Entry.Duration)continue;
   const float T=static_cast<float>(Elapsed/Entry.Duration);
   float Y=-44.f*T;
   if(Entry.bEnemyDamage)
   {
    FVector2D Offset;float Scale,Opacity;
    SampleEnemyDamage(T,Entry.Direction,Offset,Scale,Opacity);Y=Offset.Y;
   }
   Clearance=FMath::Min(Clearance,FMath::Abs(Candidate-(Entry.StartOffsetY+Y)));
  }
  if(Clearance>=20.f)return Candidate;
  if(Clearance>BestClearance){Best=Candidate;BestClearance=Clearance;}
 }
 return Best;
}
int32 UEGCombatNumber::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Clip,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bEnabled) const
{
 int32 Result=Super::NativePaint(Args,G,Clip,Out,Layer,Style,bEnabled);
 if(bShield)
 {
  // The autosized text's desired width includes its font outline and is updated
  // by Slate's layout prepass when a pooled/aggregated number changes. Its slot
  // is centered at Position.X, so this tracks short and long amounts alike.
  const auto* CanvasSlot=Cast<UCanvasPanelSlot>(Number->Slot);
  const float TextCenter=CanvasSlot?CanvasSlot->GetPosition().X:101.5f;
  const float TextLeft=TextCenter-Number->GetDesiredSize().X*.5f;
  const float Left=TextLeft-6.f-2.5f-12.f;
  const TArray<FVector2D> Points={FVector2D(Left,10),FVector2D(Left+12,10),FVector2D(Left+12,22),FVector2D(Left+6,29),FVector2D(Left,22),FVector2D(Left,10)};
  FSlateDrawElement::MakeLines(Out,++Result,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.01f,.02f,.03f)*Style.GetColorAndOpacityTint(),true,5);
  FSlateDrawElement::MakeLines(Out,++Result,G.ToPaintGeometry(),Points,ESlateDrawEffect::None,FLinearColor(.5f,.85f,1)*Style.GetColorAndOpacityTint(),true,2);
 }
 return Result;
}
void UEGCombatTextLayer::NativeOnInitialized()
{
 Super::NativeOnInitialized();Canvas=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Canvas;
 SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UEGCombatTextLayer::Present(AEGChessUnit* Target,const FEGCombatResolution& R)
{
 if(!IsValid(Target))return;
 if(R.bHealing){if(R.HealthDelta>0)AddNumber(Target,R.HealthDelta,true,false);}
 else
 {
  if(R.HealthDelta<0)AddNumber(Target,-R.HealthDelta,false,false);
  if(R.ShieldAbsorbed>0)AddNumber(Target,R.ShieldAbsorbed,false,true);
 }
}
void UEGCombatTextLayer::AddNumber(AEGChessUnit* Target,float Amount,bool bHealing,bool bShield)
{
 if(!FMath::IsFinite(Amount)||Amount<=0||!Canvas||!GetOwningPlayer())return;
 FVector2D Screen;const FVector World=Target->GetActorLocation()+FVector(0,0,25);
 if(!UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),World,Screen,true))return;
 const auto Size=UWidgetLayoutLibrary::GetViewportSize(this)/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
 if(Screen.X<0||Screen.Y<0||Screen.X>Size.X||Screen.Y>Size.Y)return;
 const auto Id=Target->GetInstanceId();
 const double Now=GetWorld()->GetTimeSeconds();
 // Search newest first: a newer window can coexist with an older, still-visible number.
 for(int32 I=Active.Num()-1;I>=0;--I)
 {
  auto& Entry=Active[I];
  if(Entry.Target.Get()==Target&&Entry.TryAccumulate(Id,bHealing,bShield,Now,Amount))
  {
   Entry.Widget->SetAmount(Entry.Amount,bHealing);
   return;
  }
 }
 int32 Count=0,Oldest=INDEX_NONE;
 for(int32 I=0;I<Active.Num();++I)if(Active[I].TargetId==Id){++Count;if(Oldest==INDEX_NONE)Oldest=I;}
 if(Count>=6)Release(Oldest);
 if(Active.Num()>=64)Release(0);
 UEGCombatNumber* W=nullptr;
 if(!Pool.IsEmpty())W=Pool.Pop();else W=CreateWidget<UEGCombatNumber>(GetOwningPlayer());
 if(!W)return;
 const bool bEnemyDamage=!bHealing&&Target->GetFaction_Implementation()==EEGFaction::Enemy;
 const int8 Direction=bNextEnemyDamageRight?1:-1;
 if(bEnemyDamage)bNextEnemyDamageRight=!bNextEnemyDamageRight;
 W->Configure(Amount,bHealing,bShield,bEnemyDamage);W->SetVisibility(ESlateVisibility::HitTestInvisible);W->SetRenderOpacity(1);W->SetRenderScale(FVector2D(bEnemyDamage?.9f:1.10f));
 const float Duration=bEnemyDamage?.72f:(bHealing?.95f:.85f);
 const float StartOffsetY=bEnemyDamage?FEGActiveCombatNumber::ChooseEnemyStartOffsetY(Active,Id,Now,Direction):
  FEGActiveCombatNumber::ChooseStartOffsetY(Active,Id,Now,Duration,bShield?20.f:-12.f);
 auto* CanvasSlot=Canvas->AddChildToCanvas(W);
 CanvasSlot->SetSize(FVector2D(180,48));
 CanvasSlot->SetAlignment(FVector2D(.5,.5));
 CanvasSlot->SetPosition(Screen+FVector2D(bEnemyDamage?12.f*Direction:0.f,StartOffsetY));
 auto& Entry=Active.AddDefaulted_GetRef();
 Entry.Widget=W;
 Entry.Target=Target;
 Entry.TargetId=Id;
 Entry.LastWorld=World;
 Entry.Duration=Duration;
 Entry.StartOffsetY=StartOffsetY;
 Entry.StartedAt=Now;Entry.Amount=Amount;Entry.bHealing=bHealing;Entry.bShield=bShield;
 Entry.bEnemyDamage=bEnemyDamage;Entry.Direction=Direction;
 ++PresentedCount;
 if(!GetWorld()->GetTimerManager().IsTimerActive(AnimationTimer))
  GetWorld()->GetTimerManager().SetTimer(AnimationTimer,this,&ThisClass::Animate,1.f/60.f,true);
}
void UEGCombatTextLayer::Release(int32 Index)
{
 if(!Active.IsValidIndex(Index))return;
 auto* W=Active[Index].Widget.Get();if(W){W->RemoveFromParent();W->SetVisibility(ESlateVisibility::Collapsed);Pool.Add(W);}Active.RemoveAt(Index);
}
void UEGCombatTextLayer::Animate()
{
 const double Now=GetWorld()->GetTimeSeconds();
 const auto Size=UWidgetLayoutLibrary::GetViewportSize(this)/FMath::Max(.1f,UWidgetLayoutLibrary::GetViewportScale(this));
 for(int32 I=Active.Num()-1;I>=0;--I)
 {
  Active[I].Age=static_cast<float>(FMath::Max(0.,Now-Active[I].StartedAt));
  if(Active[I].Age>=Active[I].Duration)Release(I);
 }
 for(auto& E:Active)
 {
  if(E.Target.IsValid())E.LastWorld=E.Target->GetActorLocation()+FVector(0,0,25);
  FVector2D P=FVector2D::ZeroVector;
  const bool bProjected=GetOwningPlayer()&&UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),E.LastWorld,P,true);
  const bool bVisible=bProjected&&P.X>=0&&P.Y>=0&&P.X<=Size.X&&P.Y<=Size.Y;
  E.Widget->SetVisibility(bVisible?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
  if(!bVisible)continue;
  const float T=E.Age/E.Duration;
  if(E.bEnemyDamage)
  {
   FVector2D Offset;float Scale,Opacity;
   FEGActiveCombatNumber::SampleEnemyDamage(T,E.Direction,Offset,Scale,Opacity);
   if(auto* S=Cast<UCanvasPanelSlot>(E.Widget->Slot))S->SetPosition(P+Offset+FVector2D(0.f,E.StartOffsetY));
   E.Widget->SetRenderScale(FVector2D(Scale));E.Widget->SetRenderOpacity(Opacity);
  }
  else
  {
   if(auto* S=Cast<UCanvasPanelSlot>(E.Widget->Slot))S->SetPosition(P+FVector2D(0.f,E.StartOffsetY-44.f*T));
   E.Widget->SetRenderScale(FVector2D(1.f+.10f*FMath::Max(0.f,1.f-E.Age/.10f)));
   E.Widget->SetRenderOpacity(FMath::Clamp((E.Duration-E.Age)/.25f,0.f,1.f));
  }
 }
 if(Active.IsEmpty())GetWorld()->GetTimerManager().ClearTimer(AnimationTimer);
}
void UEGCombatTextLayer::ClearNumbers()
{if(GetWorld())GetWorld()->GetTimerManager().ClearTimer(AnimationTimer);while(!Active.IsEmpty())Release(Active.Num()-1);bNextEnemyDamageRight=true;}
void UEGCombatTextLayer::NativeDestruct(){ClearNumbers();Super::NativeDestruct();}
