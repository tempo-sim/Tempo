// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCoreUtils.h"

#include "Engine/StaticMeshActor.h"
#include "Misc/AutomationTest.h"

// Unit tests for the class-name convention Tempo's API speaks: Blueprint-generated classes are
// reported without Unreal's generated "_C" suffix, matching the spelling clients pass in. These
// touch no world and no RHI, so they run headlessly via:
//   Scripts/Test.sh            (runs all "Tempo." automation tests)
//   Automation RunTests Tempo.Core.ClassIdentifier   (from the editor console)

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoClassIdentifierTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoStripBlueprintClassSuffixTest,
	"Tempo.Core.ClassIdentifier.StripSuffix", TempoClassIdentifierTestFlags)
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoGetClassIdentifierTest,
	"Tempo.Core.ClassIdentifier.NativeClasses", TempoClassIdentifierTestFlags)
bool FTempoGetClassIdentifierTest::RunTest(const FString& Parameters)
{
	auto Expect = [this](const UClass* Class, const TCHAR* Expected)
	{
		const FString Actual = UTempoCoreUtils::GetClassIdentifier(Class);
		if (!Actual.Equals(Expected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("GetClassIdentifier: expected '%s', got '%s'"), Expected, *Actual));
		}
	};

	// Native class names pass through untouched. (Blueprint-generated classes need loaded content,
	// so the de-suffixing side is covered by the Python integration tests.)
	Expect(AActor::StaticClass(), TEXT("Actor"));
	Expect(AStaticMeshActor::StaticClass(), TEXT("StaticMeshActor"));
	// A null class is reported as empty rather than crashing: property values can be unset.
	Expect(nullptr, TEXT(""));

	if (UTempoCoreUtils::GetClassIdentifierName(AStaticMeshActor::StaticClass()) != FName(TEXT("StaticMeshActor")))
	{
		AddError(TEXT("GetClassIdentifierName disagreed with GetClassIdentifier"));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
