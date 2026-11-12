#include "UI/Widget/EGEncounterNoticeWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Actor/EGCorruptionBarrier.h"
#include "Character/EGChessUnit.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EGCorruptionCombatComponent.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/EGChessGameMode.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "UI/EGVisualStyle.h"
#include "UI/Widget/EGDemoPanel.h"
#include "UI/Widget/EGVitalsGraphics.h"

namespace
{
void Place(UCanvasPanel* Parent,UWidget* Child,FAnchors Anchors,FVector2D Alignment,FMargin Bounds)
{
    auto* Slot=Parent->AddChildToCanvas(Child);
    Slot->SetAnchors(Anchors); Slot->SetAlignment(Alignment); Slot->SetOffsets(Bounds);
}
UTextBlock* Label(UWidgetTree* Tree,const TCHAR* Name,int32 Size,FLinearColor Color)
{
    auto* Text=Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(Name));
    FSlateFontInfo Font=FEGVisualStyle::BodyFont(Size,true);
    Font.OutlineSettings.OutlineSize=1;
    Font.OutlineSettings.OutlineColor=FLinearColor(.015f,.008f,.025f,.9f);
    Text->SetFont(Font); Text->SetJustification(ETextJustify::Center);
    Text->SetColorAndOpacity(FSlateColor(Color)); Text->SetVisibility(ESlateVisibility::HitTestInvisible);
    return Text;
}

FEGUnitVitalsStyle ResolveUnitVitalsStyle()
{
    // The overhead widget's actual Blueprint class can override these tokens.
    // ResolveWidgetClass uses the same fallback as the unit-vitals path.
    if (const UClass* VitalsClass=UEGUnitVitals::ResolveWidgetClass())
        if (const auto* Defaults=VitalsClass->GetDefaultObject<UEGUnitVitals>())
            return Defaults->VitalsStyle;
    return FEGUnitVitalsStyle();
}

void ConfigureEnemyHealthBar(UEGVitalsBar* Bar,const FEGUnitVitalsStyle& Style)
{
    Bar->FrameColor=Style.FrameBlack;
    Bar->RimColor=Style.Rim;
    Bar->TrackColor=Style.TrackBottom;
    Bar->TrackTopColor=Style.TrackTop;
    Bar->ShieldColor=Style.Shield;
    Bar->Segments=Style.HealthSegments;
    Bar->SetFillColorAndOpacity(Style.Enemy);
    Bar->SetHighlightColor(Style.EnemyHighlight);
    Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
}
}

void UEGEncounterNoticeWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();
    Root->SetVisibility(ESlateVisibility::HitTestInvisible); WidgetTree->RootWidget=Root;
    NoticePanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("ElitePreparationNotice"));
    FSlateBrush NoPanel; NoPanel.DrawAs=ESlateBrushDrawType::NoDrawType;
    NoticePanel->SetBrush(NoPanel);
    NoticePanel->SetPadding(FMargin(8.f)); NoticePanel->SetHorizontalAlignment(HAlign_Center); NoticePanel->SetVerticalAlignment(VAlign_Center);
    NoticePanel->SetContent(Label(WidgetTree,TEXT("ElitePreparationNoticeText"),21,FLinearColor(1.f,.86f,1.f,1.f)));
    CastChecked<UTextBlock>(NoticePanel->GetContent())->SetText(FText::FromString(TEXT("侵蚀之律者侵蚀了我方英桀！")));
    Place(Root,NoticePanel,FAnchors(.5f,0.f),FVector2D(.5f,0.f),FMargin(0.f,83.f,590.f,48.f));
    NoticePanel->SetVisibility(ESlateVisibility::Collapsed);

    BossPanel=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("CorruptionBossPanel"));
    BossPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
    // Leave room below the phase clock and for the CJK font's full line height.
    Place(Root,BossPanel,FAnchors(.5f,0.f),FVector2D(.5f,0.f),FMargin(0.f,58.f,360.f,70.f));
    BossPanel->SetVisibility(ESlateVisibility::Collapsed);
    BossTitle=Label(WidgetTree,TEXT("CorruptionBossTitle"),18,FLinearColor(.96f,.87f,1.f,1.f));
    Place(BossPanel,BossTitle,FAnchors(0.f,0.f,1.f,0.f),FVector2D::ZeroVector,FMargin(0.f,0.f,0.f,42.f));
    const FEGUnitVitalsStyle VitalsStyle=ResolveUnitVitalsStyle();
    auto* BossVitals=WidgetTree->ConstructWidget<UEGVitalsBar>(UEGVitalsBar::StaticClass(),TEXT("CorruptionBossHealth"));
    ConfigureEnemyHealthBar(BossVitals,VitalsStyle); BossBar=BossVitals;
    Place(BossPanel,BossBar,FAnchors(0.f,0.f,1.f,0.f),FVector2D::ZeroVector,FMargin(20.f,52.f,20.f,14.f));

    // This panel is projected from the actual collision wall's top edge each frame.
    // Its background is transparent: only the countdown and durability bar draw.
    BarrierPanel=WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(),TEXT("CorruptionBarrierPanel"));
    Place(Root,BarrierPanel,FAnchors(0.f,0.f),FVector2D(.5f,1.f),FMargin(0.f,0.f,224.f,36.f));
    BarrierPanel->SetVisibility(ESlateVisibility::Collapsed);
    BarrierTime=Label(WidgetTree,TEXT("CorruptionBarrierTime"),15,FLinearColor(.96f,.85f,1.f,1.f));
    Place(BarrierPanel,BarrierTime,FAnchors(0.f,0.f),FVector2D::ZeroVector,FMargin(0.f,0.f,28.f,20.f));
    auto* BarrierVitals=WidgetTree->ConstructWidget<UEGVitalsBar>(UEGVitalsBar::StaticClass(),TEXT("CorruptionBarrierHealth"));
    ConfigureEnemyHealthBar(BarrierVitals,VitalsStyle); BarrierBar=BarrierVitals;
    Place(BarrierPanel,BarrierBar,FAnchors(0.f,0.f,1.f,0.f),FVector2D::ZeroVector,FMargin(0.f,22.f,0.f,12.f));
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UEGEncounterNoticeWidget::ApplyMatchView(bool bSevenStages,int32 Stage,EEGMatchPhase Phase)
{
    if(NoticeState.Update(bSevenStages,Stage,Phase))
    {
        NoticeEnd=FPlatformTime::Seconds()+4.;
        if(NoticePanel){NoticePanel->SetVisibility(ESlateVisibility::HitTestInvisible);NoticePanel->SetRenderOpacity(1.f);}
    }
    else if(Phase!=EEGMatchPhase::Preparation)
    {
        NoticeEnd=0.; if(NoticePanel)NoticePanel->SetVisibility(ESlateVisibility::Collapsed);
    }
    CurrentPhase=Phase;
    if(Phase!=EEGMatchPhase::Combat)
    {
        Boss.Reset(); Barrier.Reset();
        if(BossPanel)BossPanel->SetVisibility(ESlateVisibility::Collapsed);
        if(BarrierPanel)BarrierPanel->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UEGEncounterNoticeWidget::NativeTick(const FGeometry& Geometry,float DeltaSeconds)
{
    Super::NativeTick(Geometry,DeltaSeconds);
    const double Now=FPlatformTime::Seconds();
    if(NoticePanel && NoticeEnd>0.)
    {
        const double Remaining=NoticeEnd-Now;
        if(Remaining<=0.){NoticeEnd=0.;NoticePanel->SetVisibility(ESlateVisibility::Collapsed);}
        else NoticePanel->SetRenderOpacity(FMath::Clamp(static_cast<float>(Remaining/.5),0.f,1.f));
    }
    if(CurrentPhase==EEGMatchPhase::Combat)RefreshBoss(Geometry);
}

void UEGEncounterNoticeWidget::RefreshBoss(const FGeometry& Geometry)
{
    if(!Boss.IsValid())
    {
        auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGChessGameMode>():nullptr;
        if(Mode)for(const auto& WeakUnit:Mode->GetBattleUnits())
        {
            auto* Unit=WeakUnit.Get(); auto* Candidate=Unit?Unit->FindComponentByClass<UEGCorruptionCombatComponent>():nullptr;
            if(Candidate&&Candidate->IsBoss()){Boss=Candidate;break;}
        }
    }
    auto* C=Boss.Get();
    if(!C || C->GetBossPhase()==EEGCorruptionBossPhase::Finished)
    {
        Barrier.Reset();
        if(BossPanel)BossPanel->SetVisibility(ESlateVisibility::Collapsed);
        if(BarrierPanel)BarrierPanel->SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    const auto Phase=C->GetBossPhase();
    BossPanel->SetVisibility(Phase==EEGCorruptionBossPhase::Barrier?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    BossTitle->SetText(FText::FromString(C->IsEnraged()?TEXT("侵蚀之律者  ·  狂暴"):TEXT("侵蚀之律者")));
    const float Max=FMath::Max(1.f,C->GetPhaseMaxHealth());
    const float Fraction=FMath::Clamp(C->GetPhaseHealth()/Max,0.f,1.f);
    BossBar->SetPercent(Phase==EEGCorruptionBossPhase::Barrier?0.f:Fraction);
    if(Phase!=EEGCorruptionBossPhase::Barrier)
    {
        Barrier.Reset(); BarrierPanel->SetVisibility(ESlateVisibility::Collapsed); return;
    }
    if(!Barrier.IsValid()&&GetWorld())
        for(TActorIterator<AEGCorruptionBarrier> It(GetWorld());It;++It)
            if(IsValid(*It)&&!It->IsActorBeingDestroyed()&&It->GetOwner()==C->GetOwner()){Barrier=*It;break;}

    auto* Wall=Barrier.Get(); auto* Player=GetOwningPlayer();
    FVector Origin,Extent;
    if(!IsValid(Wall)||Wall->IsActorBeingDestroyed()||!Player)
    {BarrierPanel->SetVisibility(ESlateVisibility::Collapsed);return;}
    Wall->GetActorBounds(false,Origin,Extent);
    FVector2D Projected;
    const FVector2D ViewSize=Geometry.GetLocalSize();
    if(Extent.Z<=0.f||ViewSize.X<=0.f||ViewSize.Y<=0.f||
       !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(Player,Origin+FVector(0,0,Extent.Z),Projected,false)||
       Projected.X<0.f||Projected.X>ViewSize.X||Projected.Y<0.f||Projected.Y>ViewSize.Y)
    {BarrierPanel->SetVisibility(ESlateVisibility::Collapsed);return;}

    auto* BarrierSlot=Cast<UCanvasPanelSlot>(BarrierPanel->Slot);
    if(!BarrierSlot){BarrierPanel->SetVisibility(ESlateVisibility::Collapsed);return;}
    const float HalfWidth=FMath::Min(112.f,ViewSize.X*.48f);
    BarrierSlot->SetSize(FVector2D(HalfWidth*2.f,36.f));
    BarrierSlot->SetPosition(FVector2D(FMath::Clamp(Projected.X,HalfWidth,ViewSize.X-HalfWidth),FMath::Max(36.f,Projected.Y-6.f)));
    BarrierBar->SetPercent(FMath::Clamp(C->GetBarrierHealth()/FMath::Max(1.f,C->GetBarrierMaxHealth()),0.f,1.f));
    BarrierTime->SetText(FText::AsNumber(FMath::CeilToInt(FMath::Max(0.f,C->GetBarrierRemainingSeconds()))));
    BarrierPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
}
