#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Data/EGElysiaSkillDefinition.h"
#include "Data/EGElysiaHerrscherSkillDefinition.h"
#include "Data/EGEdenAponiaSkillDefinition.h"
#include "Data/EGGriseoSkillDefinition.h"
#include "Data/EGHeroDefinition.h"
#include "Data/EGSakuraVillVSkillDefinition.h"
#include "Animation/AnimInstance.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEGSkillAssetRulesTest,"ElyGambit.Assets.SkillRuleValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FEGSkillAssetRulesTest::RunTest(const FString&)
{
 auto* Eden=NewObject<UEGEdenSkillDefinition>();Eden->UltimateRadius=100.f;
 TestTrue(TEXT("Valid Eden rules"),Eden->HasValidSkillRules());
 Eden->NoteMana=-1.f;TestFalse(TEXT("Negative Eden mana rejected"),Eden->HasValidSkillRules());
 auto* Aponia=NewObject<UEGAponiaSkillDefinition>();
 TestTrue(TEXT("Valid Aponia rules"),Aponia->HasValidSkillRules());
 Aponia->LightningInterval=.01f;TestFalse(TEXT("Too short lightning interval rejected"),Aponia->HasValidSkillRules());
 auto* Griseo=NewObject<UEGGriseoSkillDefinition>();
 TestTrue(TEXT("Valid Griseo rules"),Griseo->HasValidSkillRules());
 Griseo->FieldReduction=1.1f;TestFalse(TEXT("Invalid protection ratio rejected"),Griseo->HasValidSkillRules());
 auto* Elysia=NewObject<UEGElysiaSkillDefinition>();
 TestEqual(TEXT("C1 normal multiplier default"),Elysia->NormalAttackMultiplier,1.f);
 TestEqual(TEXT("C1 charged interval default"),Elysia->ChargedAttackInterval,5);
 Elysia->UltimateRadius=500.f;
 TestTrue(TEXT("Elysia rules do not require cosmetic assemblies"),Elysia->HasValidSkillRules());
 Elysia->ChargedAttackInterval=0;
 TestFalse(TEXT("C1 zero charged interval rejected"),Elysia->HasValidSkillRules());
 Elysia->ChargedAttackInterval=5;
 Elysia->ArrowFlightSeconds=0.f;
 TestFalse(TEXT("Basic-only Elysia still rejects an invalid projectile timing rule"),Elysia->HasValidSkillRules());
 auto* Human=NewObject<UEGElysiaHerrscherSkillDefinition>();
 TestTrue(TEXT("Human default rules are valid"),Human->HasValidSkillRules());
 TestEqual(TEXT("Bow interval default"),Human->BowAttackInterval,3);
 TestEqual(TEXT("Origin interval default"),Human->OriginAttackInterval,3);
 TestEqual(TEXT("Bow lane width default"),Human->ArrowLaneWidthCells,1.f);
 TestEqual(TEXT("Origin lane width default"),Human->OriginWaveWidthCells,2.f);
 TestEqual(TEXT("Scepter radius default"),Human->ScepterSplashRadiusCells,1);
 Human->BowAttackInterval=0;TestFalse(TEXT("Human zero bow interval rejected"),Human->HasValidSkillRules());Human->BowAttackInterval=3;
 Human->OriginWaveWidthCells=0;TestFalse(TEXT("Human zero wave width rejected"),Human->HasValidSkillRules());
 auto* Sakura=NewObject<UEGSakuraSkillDefinition>();
 TestEqual(TEXT("Sakura execution uses a separate twenty-percent default"),Sakura->ExecuteHealthFraction,.2f);
 TestTrue(TEXT("Default Sakura execution rules are valid"),Sakura->HasValidSkillRules());
 Sakura->ExecuteHealthFraction=0;TestFalse(TEXT("Zero execution threshold is rejected"),Sakura->HasValidSkillRules());
 Sakura->ExecuteHealthFraction=1.01f;TestFalse(TEXT("Execution above full health is rejected"),Sakura->HasValidSkillRules());
 Sakura->ExecuteHealthFraction=std::numeric_limits<float>::quiet_NaN();TestFalse(TEXT("Non-finite execution threshold is rejected"),Sakura->HasValidSkillRules());
 Sakura->ExecuteHealthFraction=.2f;
 auto* Hero=NewObject<UEGHeroDefinition>();Hero->HeroId=TEXT("AnimationValidationFixture");
 Hero->AnimationClass=UAnimInstance::StaticClass();
 FString Error;
 TestTrue(TEXT("Lightweight validation does not synchronously validate animation classes"),Hero->ValidateDefinition(Error,false));
 Error.Reset();
 TestFalse(TEXT("Unsupported animation class rejected by full validation"),Hero->ValidateDefinition(Error,true));
 TestTrue(TEXT("Unsupported animation class reports a useful reason"),Error.Contains(TEXT("unsupported animation class")));
 return true;
}
#endif
