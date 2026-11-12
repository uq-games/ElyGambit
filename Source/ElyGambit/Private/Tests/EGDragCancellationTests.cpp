#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/EGCardDragOperation.h"
#include "UI/Widget/EGShopCard.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/DragAndDrop.h"
#include "Layout/WidgetPath.h"
#include "HAL/PlatformTime.h"
#include "UObject/StrongObjectPtr.h"
#include "InputCoreTypes.h"
#include "Widgets/SWindow.h"
#include "Layout/ArrangedWidget.h"

namespace
{
// Exercise the engine's complete pointer-move tail, without windows or rendering.
class FEGCancelDuringDrag final : public FDragDropOperation
{
public:
 int32 Drops=0,CursorQueries=0;
 bool bCancelOnDrag=true;
 virtual bool AffectedByPointerEvent(const FPointerEvent&) override {return true;}
 virtual void OnDragged(const FDragDropEvent&) override
 {if(bCancelOnDrag)UEGCardDragOperation::CancelSlateDragDeferred(AsShared());}
 virtual FCursorReply OnCursorQuery() override {++CursorQueries;return FCursorReply::Unhandled();}
 virtual void OnDrop(bool,const FPointerEvent&) override {++Drops;}
};
class FEGAfterDragTicker final : public IAutomationLatentCommand
{
 TFunction<void()> Action;
 double Start=-1;
public:
 explicit FEGAfterDragTicker(TFunction<void()> InAction):Action(MoveTemp(InAction)){}
 virtual bool Update() override
 {
  const double Now=FPlatformTime::Seconds();
  if(Start<0){Start=Now;return false;}
  if(Now-Start<.02)return false;
  Action();return true;
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGDragCancellationSafety,"ElyGambit.DragCancellation.SlateReentryAndReplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGDragCancellationSafety::RunTest(const FString&)
{
 if(!TestTrue(TEXT("Slate available"),FSlateApplication::IsInitialized()))return false;
 auto User=FSlateApplication::Get().GetCursorUser();
 if(!TestFalse(TEXT("Isolated test must not replace a real drag"),User->IsDragDropping()))return false;
 const auto Probe=MakeShared<FEGCancelDuringDrag>();
 const FPointerEvent Move(0,FVector2D(-200,-200),FVector2D(20,20),TSet<FKey>{EKeys::LeftMouseButton},EKeys::Invalid,0,FModifierKeysState());
 // A detached Slate path supplies the public drag-start reply; it is never
 // registered with the application and creates no OS window or visual output.
 const auto Window=SNew(SWindow).ClientSize(FVector2D(64,64));
 TArray<FWidgetAndPointer> Arranged;
 Arranged.Emplace(FArrangedWidget(Window,FGeometry::MakeRoot(FVector2D(64,64),FSlateLayoutTransform())));
 const auto Path=MakeShared<FWidgetPath>(MakeArrayView(Arranged));
 auto Begin=[Path,Move](TSharedRef<FDragDropOperation> Operation)
 {FSlateApplication::Get().ProcessReply(*Path,FReply::Handled().BeginDragDrop(Operation),&Path.Get(),&Move,Move.GetUserIndex());};
 Begin(Probe);
 // OnDragged queues disposal, and Slate then queries the same pointer. Immediate
 // CancelDragDrop here would assert in FSlateUser::NotifyPointerMoveComplete.
 FSlateApplication::Get().ProcessMouseMoveEvent(Move,false);
 TestTrue(TEXT("Operation remains valid until the engine finishes its mouse event"),User->GetDragDropContent()==Probe);
 TestTrue(TEXT("Engine's cursor query after OnDragged completed safely"),Probe->CursorQueries>0);
 TestEqual(TEXT("Cancellation has not re-entered the current callback"),Probe->Drops,0);

 const auto Replacement=MakeShared<FEGCancelDuringDrag>();
 Replacement->bCancelOnDrag=false;
 const auto Obsolete=MakeShared<FEGCancelDuringDrag>();
 ADD_LATENT_AUTOMATION_COMMAND(FEGAfterDragTicker([this,User,Probe,Replacement,Obsolete,Begin]
 {
  TestFalse(TEXT("Deferred cancellation clears the finished operation"),User->IsDragDropping());
  TestEqual(TEXT("Native cancellation occurs once"),Probe->Drops,1);
  Begin(Obsolete);
  UEGCardDragOperation::CancelSlateDragDeferred(Obsolete);
  User->CancelDragDrop();
  Begin(Replacement);
 }));
 ADD_LATENT_AUTOMATION_COMMAND(FEGAfterDragTicker([this,User,Probe,Replacement,Obsolete]
 {
  TestTrue(TEXT("Old cancellation cannot clear a newer drag"),User->GetDragDropContent()==Replacement);
  TestEqual(TEXT("Old operation is not cancelled twice"),Probe->Drops,1);
  TestEqual(TEXT("Obsolete operation is not cancelled twice"),Obsolete->Drops,1);
  User->CancelDragDrop();
  TestEqual(TEXT("Replacement cleaned up by its own cancellation"),Replacement->Drops,1);
 }));

 for(UClass* Class:{UEGCardDragOperation::StaticClass(),UEGShopDragOperation::StaticClass()})
 {
  TStrongObjectPtr<UEGCardDragOperation> Operation(NewObject<UEGCardDragOperation>(GetTransientPackage(),Class));
  Operation->MarkFinished();
  Operation->Dragged_Implementation(Move);Operation->Drop_Implementation(Move);Operation->DragCancelled_Implementation(Move);
  TestTrue(TEXT("Finished hand/shop callbacks remain terminal"),Operation->HasFinished()&&!Operation->Controller.IsValid());
 }
 return true;
}
#endif
