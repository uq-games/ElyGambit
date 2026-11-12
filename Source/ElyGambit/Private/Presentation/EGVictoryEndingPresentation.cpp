#include "Presentation/EGVictoryEndingPresentation.h"
#include "Presentation/EGVictoryEndingDefinition.h"
#include "Presentation/EGBoardMasterPresentation.h"
#include "Presentation/EGBoardMasterActionComponent.h"
#include "Actor/EGArtPiece.h"
#include "Animation/AnimSequence.h"
#include "Game/EGDemoGameMode.h"
#include "UI/Widget/EGVictoryEndingWidget.h"
#include "UI/HUD/EGHUD.h"
#include "Components/EGGameAudioComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "LevelSequence.h"
#include "LevelSequenceActor.h"
#include "LevelSequencePlayer.h"
#include "MediaPlayer.h"
#include "MediaSoundComponent.h"
#include "MediaSource.h"
#include "MediaTexture.h"
#include "Sound/SoundBase.h"

AEGVictoryEndingPresentation::AEGVictoryEndingPresentation()
{
 PrimaryActorTick.bCanEverTick=true;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 Music=CreateDefaultSubobject<UAudioComponent>(TEXT("Music")); Music->bAutoActivate=false; Music->bIsUISound=true;
 Voice=CreateDefaultSubobject<UAudioComponent>(TEXT("Voice")); Voice->bAutoActivate=false; Voice->bIsUISound=true;
 Effect=CreateDefaultSubobject<UAudioComponent>(TEXT("Effect")); Effect->bAutoActivate=false; Effect->bIsUISound=true;
 MediaAudio=CreateDefaultSubobject<UMediaSoundComponent>(TEXT("MediaAudio")); MediaAudio->SetupAttachment(RootComponent);
}

void AEGVictoryEndingPresentation::BeginEnding(AEGHUD* InHUD,TSoftObjectPtr<UEGVictoryEndingDefinition> InDefinition,float LeadInSeconds)
{
 if(bStarted||!IsValid(InHUD))return;
 auto* PC=InHUD->GetOwningPlayerController(); if(!PC||!PC->IsLocalController())return;
 bStarted=true; HUD=InHUD; DefinitionPath=InDefinition; LeadInRemaining=FMath::Max(0.f,LeadInSeconds);
 // The result actor can outlive, or exist without, the final settlement.
 // Own a separate visibility request for both persistent board masters.
 if(auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr)
 {
  MasterPresentation=Mode->FindComponentByClass<UEGBoardMasterPresentation>();
  if(MasterPresentation.IsValid())MasterPresentation->SetSettlementPresenter(this,true);
 }
 OriginalViewTarget=PC->GetViewTarget();
 Widget=CreateWidget<UEGVictoryEndingWidget>(PC); if(!Widget){Fail(TEXT("无法创建结局界面"));return;}
 Widget->BindEnding(this); Widget->AddToViewport(100);
 MediaPlayer=NewObject<UMediaPlayer>(this); MediaPlayer->PlayOnOpen=false;
 MediaPlayer->OnMediaOpened.AddDynamic(this,&ThisClass::MediaOpened);
 MediaPlayer->OnMediaOpenFailed.AddDynamic(this,&ThisClass::MediaFailed);
 MediaPlayer->OnEndReached.AddDynamic(this,&ThisClass::MediaFinished);
 MediaTexture=NewObject<UMediaTexture>(this); MediaTexture->SetMediaPlayer(MediaPlayer); MediaTexture->UpdateResource();
 MediaAudio->SetMediaPlayer(MediaPlayer);
 Music->OnAudioFinished.AddUniqueDynamic(this,&ThisClass::MusicFinished);
 if(LeadInRemaining<=0.f)BeginTransition(EEGEndingTransition::Start,.15f);
}

void AEGVictoryEndingPresentation::Tick(float DeltaSeconds)
{
 Super::Tick(DeltaSeconds); if(!bStarted)return;
 const float Delta=FMath::Max(0.f,DeltaSeconds);
 if(!bFinished&&LeadInRemaining>0.f)
 {
  LeadInRemaining=FMath::Max(0.f,LeadInRemaining-Delta);
  if(LeadInRemaining<=0.f)BeginTransition(EEGEndingTransition::Start,.15f);
 }
 if(!bFinished&&bWaitingForMedia)
 {
  MediaOpeningRemaining-=Delta;
  if(MediaOpeningRemaining<=0.f){bWaitingForMedia=false;Fail(TEXT("视频打开超时"));}
 }
 if(!bFinished&&!bInError&&!bStoppingNode&&
    ((bFriendlyActionPending&&(!FriendlyAction.IsValid()||!IsValid(FriendlyAction->GetOwner())||FriendlyAction->GetOwner()->IsActorBeingDestroyed()))||
     (bEnemyActionPending&&(!EnemyAction.IsValid()||!IsValid(EnemyAction->GetOwner())||EnemyAction->GetOwner()->IsActorBeingDestroyed()))))
  Fail(TEXT("棋手动作中断，角色已不可用"));
 TickTransition(Delta);
 if(IsActorBeingDestroyed())return;
 const auto* Settings=UEGGameAudioComponent::FindFor(this);
 const float MusicLevel=Settings?Settings->GetMusicVolume():1.f;
 const float SoundLevel=Settings?Settings->GetUISoundVolume():1.f;
 const bool bVoicePresent=(Voice&&Voice->IsPlaying())||(MediaPlayer&&MediaPlayer->IsPlaying()&&!bMediaAudioMuted);
 const float DuckTarget=bVoicePresent?.35f:1.f;
 MusicDuck=FMath::FInterpConstantTo(MusicDuck,DuckTarget,Delta,(1.f-.35f)/(bVoicePresent?.15f:.5f));
 const float ExitVolume=bFinished?1.f-TransitionOpacity:1.f;
 const float NodeVolume=Transition!=EEGEndingTransition::None&&!bTransitionFadingIn?1.f-TransitionOpacity:1.f;
 if(Music)Music->SetVolumeMultiplier(MusicLevel*MusicDuck*ExitVolume);
 if(Voice)Voice->SetVolumeMultiplier(SoundLevel*NodeVolume); if(Effect)Effect->SetVolumeMultiplier(SoundLevel*NodeVolume);
 if(MediaAudio)MediaAudio->SetVolumeMultiplier(bMediaAudioMuted?0.f:SoundLevel*NodeVolume);
}

void AEGVictoryEndingPresentation::BeginTransition(EEGEndingTransition Action,float FadeOutSeconds)
{
 if(Action==EEGEndingTransition::None)return;
 if(Transition!=EEGEndingTransition::None&&Action!=EEGEndingTransition::Finish)return;
 Transition=Action;bTransitionFadingIn=false;TransitionElapsed=0.f;TransitionDuration=FMath::Max(.01f,FadeOutSeconds);
 TransitionStartOpacity=TransitionOpacity;
}
void AEGVictoryEndingPresentation::TickTransition(float DeltaSeconds)
{
 if(Transition==EEGEndingTransition::None)return;
 TransitionElapsed=FMath::Min(TransitionDuration,TransitionElapsed+DeltaSeconds);
 const float Alpha=FMath::SmoothStep(0.f,1.f,TransitionElapsed/TransitionDuration);
 TransitionOpacity=bTransitionFadingIn?1.f-Alpha:FMath::Lerp(TransitionStartOpacity,1.f,Alpha);
 if(Widget)Widget->SetTransitionOpacity(TransitionOpacity);
 if(TransitionElapsed<TransitionDuration)return;
 if(bTransitionFadingIn)
 {
  const bool bWasFinish=Transition==EEGEndingTransition::Finish;
  Transition=EEGEndingTransition::None;TransitionOpacity=0.f;
  if(bWasFinish){Destroy();return;}
  if(bPendingNodeAdvance){bPendingNodeAdvance=false;QueueNextNode();}
  return;
 }
 const EEGEndingTransition Action=Transition;
 Transition=EEGEndingTransition::None;
 if(Action==EEGEndingTransition::Start)StartContent();
 else if(Action==EEGEndingTransition::Next){StopNode();++NodeIndex;StartNode();}
 else if(Action==EEGEndingTransition::Retry)
 {StopNode();bInError=false;if(!Definition||NodeIndex==INDEX_NONE)StartContent();else StartNode();}
 else if(Action==EEGEndingTransition::Finish)
 {
  StopNode();ReleaseMasterPresentation();if(Music)Music->Stop();
  if(HUD.IsValid())
  {
   if(auto* PC=HUD->GetOwningPlayerController())if(OriginalViewTarget.IsValid())PC->SetViewTarget(OriginalViewTarget.Get());
   HUD->VictoryEndingFinished(this);
  }
 }
 // A callback may have started final completion while this transition was executing.
 if(Transition!=EEGEndingTransition::None)return;
 Transition=Action;bTransitionFadingIn=true;TransitionElapsed=0.f;TransitionDuration=Action==EEGEndingTransition::Finish?.25f:.15f;
}

void AEGVictoryEndingPresentation::StartContent()
{
 if(bFinished)return; Definition=DefinitionPath.LoadSynchronous();
 if(!Definition||Definition->Nodes.IsEmpty()){Fail(TEXT("结局内容尚未配置"));return;}
 USoundBase* Track=Definition->BackgroundMusic.LoadSynchronous();
 if(!Track){Fail(TEXT("结局背景音乐未配置或加载失败"));return;}
 Music->SetSound(Track); Music->Play(); NodeIndex=0; StartNode();
}

void AEGVictoryEndingPresentation::StartNode()
{
 if(bFinished||!Definition)return;
 if(!Definition->Nodes.IsValidIndex(NodeIndex)){Complete();return;}
 bInError=false; if(Widget)Widget->ClearContent();
 const auto& Node=Definition->Nodes[NodeIndex];
 if(Node.bUseCamera&&!ApplyCamera(Node)){Fail(TEXT("镜头目标不可用"));return;}
 if(Node.Type==EEGVictoryEndingNodeType::Dialogue && (Node.Subtitle.IsEmpty()||Node.Speaker.IsEmpty()||Node.Voice.IsNull()))
 {Fail(TEXT("对白的说话人、字幕或原声尚未配置"));return;}
 if(!Node.ElysiaAnimation.IsNull()&&!StartMasterAction(true,Node.ElysiaAnimation))
 {Fail(TEXT("爱莉希雅的结局动作不可用，请检查资源和骨架"));return;}
 if(!Node.CorruptionAnimation.IsNull()&&!StartMasterAction(false,Node.CorruptionAnimation))
 {Fail(TEXT("侵蚀的结局动作不可用，请检查资源和骨架"));return;}
 if(!Node.Voice.IsNull())
 {
  USoundBase* Sound=Node.Voice.LoadSynchronous(); if(!Sound){Fail(TEXT("对白原声加载失败"));return;}
  Voice->SetSound(Sound); Voice->Play();
 }
 if(!Node.MediaEffect.IsNull())
 {
  USoundBase* Sound=Node.MediaEffect.LoadSynchronous(); if(!Sound){Fail(TEXT("片段音效加载失败"));return;}
  Effect->SetSound(Sound); Effect->Play();
 }
 if(Widget&&!Node.Subtitle.IsEmpty())Widget->ShowDialogue(Node.Speaker,Node.Subtitle,Node.Type==EEGVictoryEndingNodeType::Dialogue);
 if(!Node.Sequence.IsNull())
 {
  ULevelSequence* Sequence=Node.Sequence.LoadSynchronous();
  if(!Sequence){Fail(TEXT("镜头动作加载失败"));return;}
  FMovieSceneSequencePlaybackSettings Settings; Settings.FinishCompletionStateOverride=EMovieSceneCompletionModeOverride::ForceRestoreState;
  ALevelSequenceActor* CreatedSequence=nullptr;
  SequencePlayer=ULevelSequencePlayer::CreateLevelSequencePlayer(GetWorld(),Sequence,Settings,CreatedSequence);
  SequenceActor=CreatedSequence;
  if(!SequencePlayer){Fail(TEXT("镜头动作无法播放"));return;}
  // The authored sequence uses binding tags for the existing board masters;
  // it does not spawn or replace gameplay-owned characters.
  if(SequenceActor)if(auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>())
   if(auto* Masters=Mode->FindComponentByClass<UEGBoardMasterPresentation>())
   {
    if(auto* Elysia=Masters->GetMaster(true))SequenceActor->SetBindingByTag(TEXT("ElysiaMaster"),{Elysia});
    if(auto* Corruption=Masters->GetMaster(false))SequenceActor->SetBindingByTag(TEXT("CorruptionMaster"),{Corruption});
   }
  SequencePlayer->OnFinished.AddUniqueDynamic(this,&ThisClass::SequenceFinished);
  bSequencePlaying=true; SequencePlayer->Play();
 }
 if(Node.Type==EEGVictoryEndingNodeType::Sequence&&Node.Sequence.IsNull()){Fail(TEXT("镜头动作尚未配置"));return;}
 if(Node.Type==EEGVictoryEndingNodeType::Video)
 {
  UMediaSource* Source=Node.MediaSource.LoadSynchronous(); if(!Source){Fail(TEXT("视频尚未配置或加载失败"));return;}
  bMediaAudioMuted=Node.bMuteMediaAudio;
  if(Widget)Widget->ShowVideo(MediaTexture);
  bWaitingForMedia=true; MediaOpeningRemaining=15.f;
  if(!MediaPlayer->OpenSource(Source)){bWaitingForMedia=false;Fail(TEXT("视频打开失败"));}
 }
}
bool AEGVictoryEndingPresentation::StartMasterAction(bool bFriendly,TSoftObjectPtr<UAnimSequence> ClipPath)
{
 auto* Mode=GetWorld()?GetWorld()->GetAuthGameMode<AEGDemoGameMode>():nullptr;
 auto* Masters=Mode?Mode->FindComponentByClass<UEGBoardMasterPresentation>():nullptr;
 auto* Master=Masters?Masters->GetMaster(bFriendly):nullptr;
 auto* Action=IsValid(Master)?Master->FindComponentByClass<UEGBoardMasterActionComponent>():nullptr;
 UAnimSequence* Clip=ClipPath.LoadSynchronous();
 if(!Action||!Clip)return false;
 if(bFriendly)
 {
  FriendlyAction=Action;FriendlyActionHandle=Action->OnPresentationFinished.AddUObject(this,&ThisClass::FriendlyActionFinished);
  bFriendlyActionPending=true;
 }
 else
 {
  EnemyAction=Action;EnemyActionHandle=Action->OnPresentationFinished.AddUObject(this,&ThisClass::EnemyActionFinished);
  bEnemyActionPending=true;
 }
 return Action->PlayPresentation(this,Clip);
}
void AEGVictoryEndingPresentation::FriendlyActionFinished(AActor* Requester,bool bInterrupted)
{
 if(Requester!=this||bStoppingNode||bFinished||!bFriendlyActionPending)return;
 bFriendlyActionPending=false;
 if(bInterrupted){Fail(TEXT("爱莉希雅的结局动作被中断"));return;}
 if(bAdvanceWhenActionsEnd)QueueNextNode();
}
void AEGVictoryEndingPresentation::EnemyActionFinished(AActor* Requester,bool bInterrupted)
{
 if(Requester!=this||bStoppingNode||bFinished||!bEnemyActionPending)return;
 bEnemyActionPending=false;
 if(bInterrupted){Fail(TEXT("侵蚀的结局动作被中断"));return;}
 if(bAdvanceWhenActionsEnd)QueueNextNode();
}
void AEGVictoryEndingPresentation::StopMasterActions()
{
 if(FriendlyAction.IsValid())
 {FriendlyAction->OnPresentationFinished.Remove(FriendlyActionHandle);FriendlyAction->Cancel(this);}
 if(EnemyAction.IsValid())
 {EnemyAction->OnPresentationFinished.Remove(EnemyActionHandle);EnemyAction->Cancel(this);}
 FriendlyActionHandle.Reset();EnemyActionHandle.Reset();FriendlyAction.Reset();EnemyAction.Reset();
 bFriendlyActionPending=false;bEnemyActionPending=false;bAdvanceWhenActionsEnd=false;
}
void AEGVictoryEndingPresentation::ReleaseMasterPresentation()
{
 auto* Masters=MasterPresentation.Get();MasterPresentation.Reset();
 // During world teardown the owner removes its own weak requests and actors.
 if(IsValid(Masters)&&GetWorld()&&!GetWorld()->bIsTearingDown)
  Masters->SetSettlementPresenter(this,false);
}
bool AEGVictoryEndingPresentation::ApplyCamera(const FEGVictoryEndingNode& Node)
{
 auto* PC=HUD.IsValid()?HUD->GetOwningPlayerController():nullptr;
 if(!PC||!GetWorld())return false;
 FTransform Shot=Node.CameraTransform;
 AEGArtPiece* Anchor=nullptr;
 if(Node.CameraAnchor!=EEGVictoryCameraAnchor::World)
 {
  const auto* Mode=GetWorld()->GetAuthGameMode<AEGDemoGameMode>();
  const auto* Masters=Mode?Mode->FindComponentByClass<UEGBoardMasterPresentation>():nullptr;
  Anchor=Masters?Masters->GetMaster(Node.CameraAnchor==EEGVictoryCameraAnchor::ElysiaMaster):nullptr;
  if(!IsValid(Anchor))return false;
  if(!Node.bFrameMasterUpperBody)Shot=Node.CameraTransform*Anchor->GetActorTransform();
 }
 if(Node.bFrameMasterUpperBody)
 {
  // The board master scales its mesh independently from the Actor. Resolve the
  // visible body in world space, never assume the Actor origin is a head height.
  auto* Body=IsValid(Anchor)?Anchor->Body.Get():nullptr;
  if(!IsValid(Body)||!Body->GetSkeletalMeshAsset())return false;
  int32 HeadIndex=INDEX_NONE;double LowestFoot=TNumericLimits<double>::Max();
  for(int32 I=0;I<Body->GetNumBones();++I)
  {
   const FString Name=Body->GetBoneName(I).ToString();
   if(HeadIndex==INDEX_NONE&&Name.EndsWith(TEXT("Head"),ESearchCase::IgnoreCase))HeadIndex=I;
   if(Name.EndsWith(TEXT("Foot"),ESearchCase::IgnoreCase))
   {
    const FVector Foot=Body->GetBoneLocation(Body->GetBoneName(I));
    if(!Foot.ContainsNaN())LowestFoot=FMath::Min(LowestFoot,double(Foot.Z));
   }
  }
  if(HeadIndex==INDEX_NONE)return false;
  const FVector Head=Body->GetBoneLocation(Body->GetBoneName(HeadIndex));
  if(Head.ContainsNaN())return false;
  if(LowestFoot==TNumericLimits<double>::Max())LowestFoot=Body->Bounds.Origin.Z-Body->Bounds.BoxExtent.Z;
  const double Height=Head.Z-LowestFoot;
  if(!FMath::IsFinite(Height)||Height<=1.)return false;
  FVector LocalDirection=Node.CameraTransform.GetTranslation();LocalDirection.Z=0.;
  const FVector Direction=Anchor->GetActorRotation().RotateVector(LocalDirection).GetSafeNormal2D();
  if(Direction.IsNearlyZero())return false;
  int32 ViewWidth=0,ViewHeight=0;PC->GetViewportSize(ViewWidth,ViewHeight);
  const double Aspect=ViewWidth>0&&ViewHeight>0?double(ViewWidth)/double(ViewHeight):16./9.;
  const double HalfFOV=FMath::DegreesToRadians(double(FMath::Clamp(Node.CameraFOV,5.f,170.f))*.5);
  const double TanVertical=FMath::Tan(HalfFOV)/Aspect;
  if(!FMath::IsFinite(TanVertical)||TanVertical<=UE_DOUBLE_SMALL_NUMBER)return false;
  // The head bone is inside the skull; leave room for hair and head ornaments.
  const FVector Focus=Head-FVector(0,0,Height*.22);
  const FVector Position=Focus+Direction*(Height*.48/TanVertical)+FVector(0,0,Height*.03);
  if(Position.ContainsNaN()||Focus.ContainsNaN())return false;
  Shot=FTransform((Focus-Position).Rotation(),Position);
  UE_LOG(LogTemp,Display,TEXT("Victory portrait master=%s head=%s height=%.2f camera=%s"),
   *Anchor->GetName(),*Head.ToCompactString(),Height,*Position.ToCompactString());
 }
 if(!IsValid(ShotCamera))ShotCamera=GetWorld()->SpawnActor<ACameraActor>();
 if(!ShotCamera||Shot.ContainsNaN())return false;
 ShotCamera->SetActorTransform(Shot);
 if(auto* Camera=ShotCamera->GetCameraComponent())Camera->SetFieldOfView(FMath::Clamp(Node.CameraFOV,5.f,170.f));
 PC->SetViewTargetWithBlend(ShotCamera,.3f);
 return true;
}
void AEGVictoryEndingPresentation::AdvanceDialogue()
{
 if(bFinished||bInError||Transition!=EEGEndingTransition::None||LeadInRemaining>0.f||!Definition||!Definition->Nodes.IsValidIndex(NodeIndex))return;
 if(Definition->Nodes[NodeIndex].Type!=EEGVictoryEndingNodeType::Dialogue)return;
 if(Widget&&!Widget->RevealOrIsComplete())return;
 QueueNextNode();
}
void AEGVictoryEndingPresentation::SkipSegment()
{
 if(bFinished||Transition!=EEGEndingTransition::None||LeadInRemaining>0.f||!Definition)return;
 QueueNextNode(true);
}
void AEGVictoryEndingPresentation::QueueNextNode(bool bForce)
{
 if(bFinished||!Definition)return;
 if(bForce)StopMasterActions();
 else if(Definition->Nodes.IsValidIndex(NodeIndex))
 {
  if(Definition->Nodes[NodeIndex].bWaitForAnimations&&(bFriendlyActionPending||bEnemyActionPending))
  {bAdvanceWhenActionsEnd=true;return;}
  bAdvanceWhenActionsEnd=false;
  if(bSequencePlaying){bAdvanceWhenSequenceEnds=true;return;}
  bAdvanceWhenSequenceEnds=false;
 }
 if(Transition!=EEGEndingTransition::None){bPendingNodeAdvance=true;return;}
 if(!Definition->Nodes.IsValidIndex(NodeIndex+1)){Complete();return;}
 BeginTransition(EEGEndingTransition::Next,.15f);
}
void AEGVictoryEndingPresentation::SkipAll(){Complete();}
void AEGVictoryEndingPresentation::Retry()
{
 if(!bInError||bFinished||Transition!=EEGEndingTransition::None)return;
 BeginTransition(EEGEndingTransition::Retry,.15f);
}
void AEGVictoryEndingPresentation::Fail(const FString& Details)
{
 UE_LOG(LogTemp,Error,TEXT("Victory ending: %s"),*Details); StopNode(); bInError=true;
 if(Widget)Widget->ShowError(FText::FromString(Details+TEXT("，请重试或跳过全部。")));
 else Complete();
}
void AEGVictoryEndingPresentation::StopNode()
{
 bStoppingNode=true; bWaitingForMedia=false; bSequencePlaying=false; bAdvanceWhenSequenceEnds=false;
 StopMasterActions();
 if(Voice)Voice->Stop(); if(Effect)Effect->Stop(); if(MediaPlayer)MediaPlayer->Close();
 if(SequencePlayer){SequencePlayer->OnFinished.RemoveAll(this);SequencePlayer->Stop();SequencePlayer=nullptr;}
 if(IsValid(SequenceActor)){SequenceActor->Destroy();SequenceActor=nullptr;}
 bStoppingNode=false;
}
void AEGVictoryEndingPresentation::SequenceFinished()
{
 if(bStoppingNode||bFinished||!Definition||!Definition->Nodes.IsValidIndex(NodeIndex))return;
 bSequencePlaying=false;
 if(Definition->Nodes[NodeIndex].Type==EEGVictoryEndingNodeType::Sequence||bAdvanceWhenSequenceEnds)
 {bAdvanceWhenSequenceEnds=false;QueueNextNode();}
}
void AEGVictoryEndingPresentation::MediaOpened(FString)
{if(!bWaitingForMedia||bFinished)return;bWaitingForMedia=false;if(MediaPlayer&&!MediaPlayer->Play())Fail(TEXT("视频无法播放"));}
void AEGVictoryEndingPresentation::MediaFailed(FString)
{if(!bStoppingNode&&!bFinished&&bWaitingForMedia){bWaitingForMedia=false;Fail(TEXT("视频打开失败"));}}
void AEGVictoryEndingPresentation::MediaFinished()
{if(!bStoppingNode&&!bFinished&&Definition&&Definition->Nodes.IsValidIndex(NodeIndex)&&Definition->Nodes[NodeIndex].Type==EEGVictoryEndingNodeType::Video)QueueNextNode();}
void AEGVictoryEndingPresentation::MusicFinished(){if(!bFinished&&!bStoppingNode&&Music&&Music->Sound)Music->Play();}
void AEGVictoryEndingPresentation::Complete()
{
 if(bFinished)return; bFinished=true;bPendingNodeAdvance=false;StopMasterActions();
 if(!Widget){StopNode();if(Music)Music->Stop();if(HUD.IsValid())HUD->VictoryEndingFinished(this);Destroy();return;}
 BeginTransition(EEGEndingTransition::Finish,.6f);
}
void AEGVictoryEndingPresentation::EndPlay(const EEndPlayReason::Type Reason)
{
 bFinished=true; StopNode();ReleaseMasterPresentation();if(Music)Music->Stop();
 if(MediaPlayer){MediaPlayer->OnMediaOpened.RemoveAll(this);MediaPlayer->OnMediaOpenFailed.RemoveAll(this);MediaPlayer->OnEndReached.RemoveAll(this);}
 if(Widget){Widget->BindEnding(nullptr);Widget->RemoveFromParent();Widget=nullptr;}
 if(HUD.IsValid())if(auto* PC=HUD->GetOwningPlayerController())if(OriginalViewTarget.IsValid())PC->SetViewTarget(OriginalViewTarget.Get());
 if(IsValid(ShotCamera)){ShotCamera->Destroy();ShotCamera=nullptr;}
 Super::EndPlay(Reason);
}
