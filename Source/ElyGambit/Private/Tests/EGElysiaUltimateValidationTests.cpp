#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimMontage.h"
#include "Data/EGHeroDefinition.h"
#include "Presentation/EGSkillNotify.h"
#include "UObject/StrongObjectPtr.h"
#include <initializer_list>

namespace
{
void SetActionEvents(UAnimMontage* Montage,std::initializer_list<const TCHAR*> Events)
{
 Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Entry)
 {
  const auto* Notify=Cast<UEGSkillNotify>(Entry.Notify);
  return Notify&&(Notify->Event==TEXT("Hit")||Notify->Event==TEXT("Release")||Notify->Event==TEXT("Finish"));
 });
 for(const TCHAR* Event:Events)
 {
  FAnimNotifyEvent& Entry=Montage->Notifies.AddDefaulted_GetRef();
  auto* Notify=NewObject<UEGSkillNotify>(Montage);
  Notify->Event=FName(Event);Entry.Notify=Notify;
 }
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGHumanUltimateValidation,
 "ElyGambit.ElysiaHerrscher.UltimateNotifyValidation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGHumanUltimateValidation::RunTest(const FString&)
{
 auto* Source=LoadObject<UEGHeroDefinition>(nullptr,TEXT("/Game/Blueprints/Characters/ElysiaHerrscherOfHuman/Data/ThreeUnits20261101/DA_Human.DA_Human"));
 if(!TestNotNull(TEXT("Formal human definition"),Source)||!TestNotNull(TEXT("Formal presentation"),Source->PresentationDefinition.Get()))return false;
 TStrongObjectPtr<UEGHeroDefinition> Hero(DuplicateObject<UEGHeroDefinition>(Source,GetTransientPackage()));
 Hero->PresentationDefinition=DuplicateObject<UEGHeroPresentationDefinition>(Source->PresentationDefinition,Hero.Get());
 auto* P=Hero->PresentationDefinition.Get();
 if(!TestEqual(TEXT("Human has entry and finisher"),P->UltimateSequence.Num(),2))return false;
 auto* SourceEntry=P->UltimateSequence[0].Montage.LoadSynchronous();
 auto* SourceFinisher=P->UltimateSequence[1].Montage.LoadSynchronous();
 if(!TestNotNull(TEXT("Entry montage"),SourceEntry)||!TestNotNull(TEXT("Finisher montage"),SourceFinisher))return false;
 auto* Entry=DuplicateObject<UAnimMontage>(SourceEntry,P);
 auto* Finisher=DuplicateObject<UAnimMontage>(SourceFinisher,P);
 P->UltimateSequence[0].Montage=Entry;P->UltimateSequence[1].Montage=Finisher;
 SetActionEvents(Entry,{TEXT("Release"),TEXT("Finish")});
 SetActionEvents(Finisher,{TEXT("Hit"),TEXT("Finish")});
 FString Error;
 TestTrue(TEXT("Entry Release and finisher Hit satisfy the formal contract"),Hero->ValidateDefinition(Error,true));
 if(!Error.IsEmpty())AddInfo(Error);
 SetActionEvents(Finisher,{TEXT("Release"),TEXT("Finish")});Error.Reset();
 TestTrue(TEXT("Existing Release finisher remains supported"),Hero->ValidateDefinition(Error,true));
 if(!Error.IsEmpty())AddInfo(Error);
 SetActionEvents(Finisher,{TEXT("Finish")});Error.Reset();
 TestFalse(TEXT("Finisher without a commit notify is rejected"),Hero->ValidateDefinition(Error,true));
 TestTrue(TEXT("Missing finisher commit reports its sequence"),Error.Contains(TEXT("Human finisher sequence")));
 SetActionEvents(Finisher,{TEXT("Hit")});Error.Reset();
 TestFalse(TEXT("Finisher without Finish is rejected"),Hero->ValidateDefinition(Error,true));
 TestTrue(TEXT("Missing finisher Finish reports its sequence"),Error.Contains(TEXT("Human finisher sequence")));
 SetActionEvents(Finisher,{TEXT("Hit"),TEXT("Finish")});
 SetActionEvents(Entry,{TEXT("Hit"),TEXT("Finish")});Error.Reset();
 TestFalse(TEXT("Entry still requires Release"),Hero->ValidateDefinition(Error,true));
 TestTrue(TEXT("Missing entry Release reports the ultimate sequence"),Error.Contains(TEXT("ultimate sequence")));
 return true;
}
#endif
