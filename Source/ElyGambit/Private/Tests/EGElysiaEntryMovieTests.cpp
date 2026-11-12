#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Presentation/EGElysiaHerrscherPresentationComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanEntryMovieCleanup,
 "ElyGambit.ElysiaHerrscher.EntryMovieCleanup",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)

bool FEGHumanEntryMovieCleanup::RunTest(const FString&)
{
 auto* Component=NewObject<UEGElysiaHerrscherPresentationComponent>();
 if(!TestNotNull(TEXT("presentation component"),Component))return false;
 TestFalse(TEXT("movie defaults to disabled"),Component->bEntryMovieEnabled);
 TestFalse(TEXT("preload is not a pending action"),Component->IsEntryMoviePending());
 TestFalse(TEXT("movie initially not playing"),Component->IsEntryMoviePlaying());

 Component->bEntryMoviePending=true;Component->bEntryMoviePlaying=true;
 Component->EntryMovieRound=FGuid::NewGuid();Component->EntryMovieAction=FGuid::NewGuid();
 const uint64 OldGeneration=Component->EntryMovieGeneration;
 Component->StopEntryMovie();
 TestFalse(TEXT("cancel clears pending"),Component->IsEntryMoviePending());
 TestFalse(TEXT("cancel clears playing"),Component->IsEntryMoviePlaying());
 TestFalse(TEXT("cancel clears action identity"),Component->EntryMovieAction.IsValid());
 Component->StopEntryMovie();
 TestFalse(TEXT("repeated cleanup is safe"),Component->IsEntryMoviePending());

 Component->bEntryMoviePending=true;Component->EntryMovieAction=FGuid::NewGuid();
 const FGuid NewAction=Component->EntryMovieAction;
 Component->OpenLoadedEntryMovie(OldGeneration);
 TestTrue(TEXT("old async load leaves new action pending"),Component->IsEntryMoviePending());
 TestTrue(TEXT("old async load preserves new action identity"),Component->EntryMovieAction==NewAction);
 Component->OnEntryMovieOpenFailed(FString());
 TestFalse(TEXT("open failure releases pending"),Component->IsEntryMoviePending());
 TestFalse(TEXT("open failure clears action identity"),Component->EntryMovieAction.IsValid());
 return !HasAnyErrors();
}
#endif
