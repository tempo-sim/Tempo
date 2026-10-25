// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldPropertyValues.h"
#include "TempoWorldReflection.h"

#include "TempoWorld/WorldControl.pb.h"

#include "Misc/AutomationTest.h"
#include "Misc/EngineVersionComparison.h"
#include "UObject/Package.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"

// Round trips through the two halves of the Value API: SetValueOnTarget writes a Value into a
// property and WritePropertyToValue reads one back out. The invariant under test is that anything
// the classifier calls value-expressible survives both directions -- including the container
// elements that are not their kind's canonical width or spelling. A TArray<uint8> reads as an
// int_array_value, so an int_array_value must write a TArray<uint8>; a TArray<FText> reads as a
// string_array_value, so a string_array_value must write it; and a set takes the array variant of its
// kind, since the two carry the same payload and the property knows which it is.
//
// The properties live on a struct built here at run time, so the test owns every type it exercises
// rather than depending on some engine class happening to declare a TArray<int16>. No world and no
// RHI. Run via:
//   Scripts/Test.sh Tempo.World.ValueRoundTrip

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoWorldValueRoundTripTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// UE 5.8 dropped the object-flags parameter from the property constructors; 5.7 has only the
	// form that takes it.
	template <typename PropertyType>
	PropertyType* NewProperty(FFieldVariant Owner, const FString& Name)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
		return new PropertyType(Owner, *Name);
#else
		return new PropertyType(Owner, *Name, RF_Public);
#endif
	}

	// A transient struct with one property of each shape under test, and one zeroed instance of it.
	struct FValueRoundTripFixture
	{
		FValueRoundTripFixture()
		{
			Struct = NewObject<UScriptStruct>(GetTransientPackage(), NAME_None, RF_Transient);
			Struct->AddToRoot();

			Bytes = AddArray<FByteProperty>(TEXT("Bytes"));
			Shorts = AddArray<FInt16Property>(TEXT("Shorts"));
			Texts = AddArray<FTextProperty>(TEXT("Texts"));
			Names = AddArray<FNameProperty>(TEXT("Names"));
			Ints = AddSet<FIntProperty>(TEXT("Ints"));
			Count = NewProperty<FIntProperty>(Struct, TEXT("Count"));
			Struct->AddCppProperty(Count);

			Struct->Bind();
			Struct->StaticLink(true);

			Memory = FMemory::Malloc(Struct->GetStructureSize(), Struct->GetMinAlignment());
			Struct->InitializeStruct(Memory);
		}

		~FValueRoundTripFixture()
		{
			Struct->DestroyStruct(Memory);
			FMemory::Free(Memory);
			Struct->RemoveFromRoot();
		}

		template <typename ElementPropertyType>
		FArrayProperty* AddArray(const TCHAR* Name)
		{
			FArrayProperty* Array = NewProperty<FArrayProperty>(Struct, Name);
			Array->Inner = NewProperty<ElementPropertyType>(Array, FString::Printf(TEXT("%s_Inner"), Name));
			Struct->AddCppProperty(Array);
			return Array;
		}

		template <typename ElementPropertyType>
		FSetProperty* AddSet(const TCHAR* Name)
		{
			FSetProperty* Set = NewProperty<FSetProperty>(Struct, Name);
			// Assigned rather than added through FSetProperty::AddCppProperty, whose hashability
			// check reads a flag that Link has not computed yet. Linking the struct computes it.
			Set->ElementProp = NewProperty<ElementPropertyType>(Set, FString::Printf(TEXT("%s_Element"), Name));
			Struct->AddCppProperty(Set);
			return Set;
		}

		FPropertyTarget TargetFor(FProperty* Property) const
		{
			FPropertyTarget Target;
			Target.Container = Memory;
			Target.Property = Property;
			return Target;
		}

		UScriptStruct* Struct = nullptr;
		void* Memory = nullptr;
		FArrayProperty* Bytes = nullptr;
		FArrayProperty* Shorts = nullptr;
		FArrayProperty* Texts = nullptr;
		FArrayProperty* Names = nullptr;
		FSetProperty* Ints = nullptr;
		FIntProperty* Count = nullptr;
	};

	TArray<int32> IntValues(const TempoWorld::IntArray& Given)
	{
		TArray<int32> Out;
		for (const int32 V : Given.values()) { Out.Add(V); }
		return Out;
	}

	TArray<FString> StringValues(const TempoWorld::StringArray& Given)
	{
		TArray<FString> Out;
		for (const std::string& V : Given.values()) { Out.Add(UTF8_TO_TCHAR(V.c_str())); }
		return Out;
	}

	TArray<int32> Sorted(TArray<int32> Values)
	{
		Values.Sort();
		return Values;
	}

	TempoWorld::Value IntArrayValue(std::initializer_list<int32> Values)
	{
		TempoWorld::Value Value;
		for (const int32 V : Values) { Value.mutable_int_array_value()->add_values(V); }
		return Value;
	}

	TempoWorld::Value IntSetValue(std::initializer_list<int32> Values)
	{
		TempoWorld::Value Value;
		for (const int32 V : Values) { Value.mutable_int_set_value()->add_values(V); }
		return Value;
	}

	TempoWorld::Value StringArrayValue(std::initializer_list<const char*> Values)
	{
		TempoWorld::Value Value;
		for (const char* V : Values) { Value.mutable_string_array_value()->add_values(V); }
		return Value;
	}
}

// Every container the classifier promises a Value for takes that Value back, in both directions.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldValueRoundTripContainersTest,
	"Tempo.World.ValueRoundTrip.Containers", TempoWorldValueRoundTripTestFlags)
bool FTempoWorldValueRoundTripContainersTest::RunTest(const FString& Parameters)
{
	FValueRoundTripFixture Fixture;

	// The promise under test: each of these is one the generated stubs will present as assignable.
	for (FProperty* Property : { static_cast<FProperty*>(Fixture.Bytes), static_cast<FProperty*>(Fixture.Shorts),
		static_cast<FProperty*>(Fixture.Texts), static_cast<FProperty*>(Fixture.Names), static_cast<FProperty*>(Fixture.Ints) })
	{
		const TempoWorldReflection::FTypeInfo Type = TempoWorldReflection::ClassifyProperty(Property);
		if (!Type.IsValueExpressible())
		{
			AddError(FString::Printf(TEXT("%s (%s) should be value-expressible"), *Property->GetName(), *Type.TypeString));
		}
	}

	// Writes In, reads the property back into Out, then does it once more from Out so a read is
	// shown to be writable as-is: the second read must equal the first. Out is cleared first, as a
	// read appends to a repeated variant the message already holds.
	auto RoundTrip = [this, &Fixture](const TCHAR* What, FProperty* Property, const TempoWorld::Value& In, TempoWorld::Value& Out) -> bool
	{
		Out.Clear();
		const grpc::Status SetStatus = SetValueOnTarget(nullptr, Fixture.TargetFor(Property), In, false);
		if (!SetStatus.ok())
		{
			AddError(FString::Printf(TEXT("%s: write failed: %s"), What, UTF8_TO_TCHAR(SetStatus.error_message().c_str())));
			return false;
		}
		const grpc::Status GetStatus = WritePropertyToValue(Fixture.Memory, Property, false, Out);
		if (!GetStatus.ok())
		{
			AddError(FString::Printf(TEXT("%s: read failed: %s"), What, UTF8_TO_TCHAR(GetStatus.error_message().c_str())));
			return false;
		}

		const grpc::Status ResetStatus = SetValueOnTarget(nullptr, Fixture.TargetFor(Property), Out, false);
		TempoWorld::Value Again;
		const grpc::Status ReadStatus = WritePropertyToValue(Fixture.Memory, Property, false, Again);
		if (!ResetStatus.ok() || !ReadStatus.ok())
		{
			AddError(FString::Printf(TEXT("%s: a read value could not be written back"), What));
			return false;
		}
		TestEqual(FString::Printf(TEXT("%s: read, written back and read again"), What), Again.SerializeAsString(), Out.SerializeAsString());
		return true;
	};

	TempoWorld::Value Out;

	// TArray<uint8>: int-kinded at a narrower width than the wire's int32.
	if (RoundTrip(TEXT("TArray<uint8>"), Fixture.Bytes, IntArrayValue({ 0, 7, 255 }), Out))
	{
		TestTrue(TEXT("TArray<uint8> reads as int_array_value"), Out.has_int_array_value());
		TestEqual(TEXT("TArray<uint8> values"), IntValues(Out.int_array_value()), TArray<int32>({ 0, 7, 255 }));

		FScriptArrayHelper Helper(Fixture.Bytes, Fixture.Bytes->ContainerPtrToValuePtr<void>(Fixture.Memory));
		if (TestEqual(TEXT("TArray<uint8> length in memory"), Helper.Num(), 3))
		{
			TestEqual(TEXT("TArray<uint8> last element in memory"),
				static_cast<int32>(CastFieldChecked<FByteProperty>(Fixture.Bytes->Inner)->GetPropertyValue(Helper.GetRawPtr(2))), 255);
		}
	}
	// A value that does not fit is refused for its range, which is only possible once the right
	// element type has been found.
	TestEqual(TEXT("TArray<uint8> rejects 256 as out of range"),
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Bytes), IntArrayValue({ 256 }), false).error_code(), grpc::OUT_OF_RANGE);

	// TArray<int16>: signed, narrower than the wire.
	if (RoundTrip(TEXT("TArray<int16>"), Fixture.Shorts, IntArrayValue({ -32768, 0, 32767 }), Out))
	{
		TestTrue(TEXT("TArray<int16> reads as int_array_value"), Out.has_int_array_value());
		TestEqual(TEXT("TArray<int16> values"), IntValues(Out.int_array_value()), TArray<int32>({ -32768, 0, 32767 }));
	}
	TestEqual(TEXT("TArray<int16> rejects 32768 as out of range"),
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Shorts), IntArrayValue({ 32768 }), false).error_code(), grpc::OUT_OF_RANGE);

	// TArray<FText>: the third spelling of a string, after FString and FName.
	if (RoundTrip(TEXT("TArray<FText>"), Fixture.Texts, StringArrayValue({ "one", "two" }), Out))
	{
		TestTrue(TEXT("TArray<FText> reads as string_array_value"), Out.has_string_array_value());
		TestEqual(TEXT("TArray<FText> values"), StringValues(Out.string_array_value()), TArray<FString>({ TEXT("one"), TEXT("two") }));

		FScriptArrayHelper Helper(Fixture.Texts, Fixture.Texts->ContainerPtrToValuePtr<void>(Fixture.Memory));
		if (TestEqual(TEXT("TArray<FText> length in memory"), Helper.Num(), 2))
		{
			TestEqual(TEXT("TArray<FText> first element in memory"),
				CastFieldChecked<FTextProperty>(Fixture.Texts->Inner)->GetPropertyValue(Helper.GetRawPtr(0)).ToString(), FString(TEXT("one")));
		}
	}

	// TArray<FName>: the spelling AActor::Tags has.
	if (RoundTrip(TEXT("TArray<FName>"), Fixture.Names, StringArrayValue({ "Alpha", "Beta" }), Out))
	{
		TestTrue(TEXT("TArray<FName> reads as string_array_value"), Out.has_string_array_value());
		TestEqual(TEXT("TArray<FName> values"), StringValues(Out.string_array_value()), TArray<FString>({ TEXT("Alpha"), TEXT("Beta") }));
	}

	// TSet<int32>, by its own variant.
	if (RoundTrip(TEXT("TSet<int32>"), Fixture.Ints, IntSetValue({ 3, 1, 2 }), Out))
	{
		TestTrue(TEXT("TSet<int32> reads as int_set_value"), Out.has_int_set_value());
		TestEqual(TEXT("TSet<int32> values"), Sorted(IntValues(Out.int_set_value())), TArray<int32>({ 1, 2, 3 }));
	}

	// ... and by the array variant, which carries the same payload. Duplicates collapse, as a set's do.
	if (RoundTrip(TEXT("TSet<int32> from int_array_value"), Fixture.Ints, IntArrayValue({ 5, 6, 6 }), Out))
	{
		TestTrue(TEXT("TSet<int32> still reads as int_set_value"), Out.has_int_set_value());
		TestEqual(TEXT("TSet<int32> values from the array variant"), Sorted(IntValues(Out.int_set_value())), TArray<int32>({ 5, 6 }));
	}

	// The wrong element kind, and a destination that is no container at all, are both type
	// mismatches rather than anything that touches memory.
	TestEqual(TEXT("string_array_value onto TArray<uint8> is a type mismatch"),
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Bytes), StringArrayValue({ "x" }), false).error_code(), grpc::FAILED_PRECONDITION);
	TestEqual(TEXT("int_array_value onto an int32 is a type mismatch"),
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Count), IntArrayValue({ 1 }), false).error_code(), grpc::FAILED_PRECONDITION);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
