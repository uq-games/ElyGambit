#pragma once
#include "Actor/EGArtPiece.h"
#include "Components/SkeletalMeshComponent.h"
#include "Presentation/EGSettlementPresentation.h"

// Synchronous fixtures have no engine frames: repeated World::Tick calls in
// one frame can skip skeletal pose updates. Advance the actual AnimGraph and
// queued notifies explicitly, then advance presentation time by the same step.
// The separate PIE test covers normal engine scheduling and the audio clock.
inline void AdvanceSettlementFixture(AEGSettlementPresentation* Presentation, int32 Frames)
{
 for (int32 Frame=0;Frame<Frames;++Frame)
 {
  for (bool bFriendly : {true,false})
   if (auto* Master=Presentation->GetMasterActor(bFriendly))
    if (auto* Body=Master->Body.Get())
    {
     Body->TickAnimation(.01f,false);
     Body->RefreshBoneTransforms();
    }
  Presentation->Tick(.01f);
 }
}
