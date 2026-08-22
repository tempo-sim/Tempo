// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "Misc/AutomationTest.h"

// Unit test for the suffix stripping behind GetSubClassWithName's leniency: a Blueprint asset reads
// as "BP_Foo" while the class it generates is "BP_Foo_C", and clients may name either. This touches
// no world and no RHI, so it runs headlessly via:
//   Scripts/Test.sh            (runs all "Tempo." automation tests)
//   Automation RunTests Tempo.Core.ClassName        (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoClassNameTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoStripBlueprintClassSuffixTest,
	"Tempo.Core.ClassName.StripBlueprintSuffix", TempoClassNameTestFlags)
bool FTempoStripBlueprintClassSuffixTest::RunTest(const FString& Parameters)
{
	auto Expect = [this](const TCHAR* Input, const TCHAR* Expected)
	{
		const FString Actual = UTempoCoreUtils::StripBlueprintClassSuffix(Input);
		if (!Actual.Equals(Expected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("StripBlueprintClassSuffix('%s'): expected '%s', got '%s'"),
				Input, Expected, *Actual));
		}
	};

	Expect(TEXT("BP_Foo_C"), TEXT("BP_Foo"));
	Expect(TEXT("BP_Foo"), TEXT("BP_Foo"));
	// Only a trailing "_C" is a suffix, and only in that exact case.
	Expect(TEXT("BP_Foo_c"), TEXT("BP_Foo_c"));
	Expect(TEXT("BP_C_Foo"), TEXT("BP_C_Foo"));
	Expect(TEXT("_C"), TEXT(""));
	Expect(TEXT(""), TEXT(""));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
