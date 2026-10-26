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
			Double = NewProperty<FDoubleProperty>(Struct, TEXT("Double"));
			Struct->AddCppProperty(Double);
			Float = NewProperty<FFloatProperty>(Struct, TEXT("Float"));
			Struct->AddCppProperty(Float);
			Doubles = AddArray<FDoubleProperty>(TEXT("Doubles"));
			Rotator = AddStruct(TEXT("Rotator"), TBaseStructure<FRotator>::Get());
			Quat = AddStruct(TEXT("Quat"), TBaseStructure<FQuat>::Get());
			Transform = AddStruct(TEXT("Transform"), TBaseStructure<FTransform>::Get());
			Color = AddStruct(TEXT("Color"), TBaseStructure<FColor>::Get());
			LinearColor = AddStruct(TEXT("LinearColor"), TBaseStructure<FLinearColor>::Get());

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

		FStructProperty* AddStruct(const TCHAR* Name, UScriptStruct* ValueStruct)
		{
			FStructProperty* Property = NewProperty<FStructProperty>(Struct, Name);
			Property->Struct = ValueStruct;
			Struct->AddCppProperty(Property);
			return Property;
		}

		template <typename T>
		T& ValueOf(const FStructProperty* Property) const
		{
			return *Property->ContainerPtrToValuePtr<T>(Memory);
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
		FDoubleProperty* Double = nullptr;
		FFloatProperty* Float = nullptr;
		FArrayProperty* Doubles = nullptr;
		FStructProperty* Rotator = nullptr;
		FStructProperty* Quat = nullptr;
		FStructProperty* Transform = nullptr;
		FStructProperty* Color = nullptr;
		FStructProperty* LinearColor = nullptr;
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
		const grpc::Status SetStatus = SetValueOnTarget(nullptr, Fixture.TargetFor(Property), In);
		if (!SetStatus.ok())
		{
			AddError(FString::Printf(TEXT("%s: write failed: %s"), What, UTF8_TO_TCHAR(SetStatus.error_message().c_str())));
			return false;
		}
		const grpc::Status GetStatus = WritePropertyToValue(Fixture.Memory, Property, Out);
		if (!GetStatus.ok())
		{
			AddError(FString::Printf(TEXT("%s: read failed: %s"), What, UTF8_TO_TCHAR(GetStatus.error_message().c_str())));
			return false;
		}

		const grpc::Status ResetStatus = SetValueOnTarget(nullptr, Fixture.TargetFor(Property), Out);
		TempoWorld::Value Again;
		const grpc::Status ReadStatus = WritePropertyToValue(Fixture.Memory, Property, Again);
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
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Bytes), IntArrayValue({ 256 })).error_code(), grpc::OUT_OF_RANGE);

	// TArray<int16>: signed, narrower than the wire.
	if (RoundTrip(TEXT("TArray<int16>"), Fixture.Shorts, IntArrayValue({ -32768, 0, 32767 }), Out))
	{
		TestTrue(TEXT("TArray<int16> reads as int_array_value"), Out.has_int_array_value());
		TestEqual(TEXT("TArray<int16> values"), IntValues(Out.int_array_value()), TArray<int32>({ -32768, 0, 32767 }));
	}
	TestEqual(TEXT("TArray<int16> rejects 32768 as out of range"),
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Shorts), IntArrayValue({ 32768 })).error_code(), grpc::OUT_OF_RANGE);

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
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Bytes), StringArrayValue({ "x" })).error_code(), grpc::FAILED_PRECONDITION);
	TestEqual(TEXT("int_array_value onto an int32 is a type mismatch"),
		SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Count), IntArrayValue({ 1 })).error_code(), grpc::FAILED_PRECONDITION);

	return true;
}

// A Value is in Unreal's own units and shapes in both directions: nothing is converted, a transform
// reads as native_transform_value with its scale, an FLinearColor as linear_color_value, and a
// quaternion keeps its full double precision.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldValueRoundTripNativeShapesTest,
	"Tempo.World.ValueRoundTrip.NativeShapes", TempoWorldValueRoundTripTestFlags)
bool FTempoWorldValueRoundTripNativeShapesTest::RunTest(const FString& Parameters)
{
	FValueRoundTripFixture Fixture;
	TempoWorld::Value Out;

	// FRotator: roll, pitch and yaw in degrees, exactly as given.
	{
		TempoWorld::Value In;
		In.mutable_rotator_value()->set_r(10.0);
		In.mutable_rotator_value()->set_p(20.0);
		In.mutable_rotator_value()->set_y(30.0);
		TestTrue(TEXT("rotator write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Rotator), In).ok());
		TestEqual(TEXT("rotator in memory"), Fixture.ValueOf<FRotator>(Fixture.Rotator), FRotator(20.0, 30.0, 10.0));
		TestTrue(TEXT("rotator read"), WritePropertyToValue(Fixture.Memory, Fixture.Rotator, Out).ok());
		TestTrue(TEXT("rotator reads as rotator_value"), Out.has_rotator_value());
		TestEqual(TEXT("rotator read roll"), Out.rotator_value().r(), 10.0);
		TestEqual(TEXT("rotator read pitch"), Out.rotator_value().p(), 20.0);
		TestEqual(TEXT("rotator read yaw"), Out.rotator_value().y(), 30.0);
	}

	// FQuat: Unreal's own components, at double precision a float would not hold.
	{
		const FQuat Given = FQuat(0.1234567890123, -0.2345678901234, 0.3456789012345, 0.0).GetNormalized();
		TempoWorld::Value In;
		In.mutable_quat_value()->set_x(Given.X);
		In.mutable_quat_value()->set_y(Given.Y);
		In.mutable_quat_value()->set_z(Given.Z);
		In.mutable_quat_value()->set_w(Given.W);
		TestTrue(TEXT("quat write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Quat), In).ok());
		const FQuat& InMemory = Fixture.ValueOf<FQuat>(Fixture.Quat);
		TestTrue(TEXT("quat in memory is unconverted"), InMemory.X == Given.X && InMemory.Y == Given.Y && InMemory.Z == Given.Z && InMemory.W == Given.W);
		Out.Clear();
		TestTrue(TEXT("quat read"), WritePropertyToValue(Fixture.Memory, Fixture.Quat, Out).ok());
		TestTrue(TEXT("quat reads as quat_value"), Out.has_quat_value());
		TestTrue(TEXT("quat read is exact"), Out.quat_value().x() == Given.X && Out.quat_value().y() == Given.Y
			&& Out.quat_value().z() == Given.Z && Out.quat_value().w() == Given.W);
	}

	// FTransform: quaternion, translation in cm and scale, as native_transform_value.
	{
		const FTransform Given(FRotator(10.0, 20.0, 30.0).Quaternion(), FVector(100.0, -200.0, 300.0), FVector(2.0, 3.0, 4.0));
		Fixture.ValueOf<FTransform>(Fixture.Transform) = Given;
		Out.Clear();
		TestTrue(TEXT("transform read"), WritePropertyToValue(Fixture.Memory, Fixture.Transform, Out).ok());
		if (TestTrue(TEXT("transform reads as native_transform_value"), Out.has_native_transform_value()))
		{
			const TempoWorld::NativeTransform& Native = Out.native_transform_value();
			TestEqual(TEXT("transform read translation"), FVector(Native.translation().x(), Native.translation().y(), Native.translation().z()), Given.GetTranslation());
			TestEqual(TEXT("transform read scale"), FVector(Native.scale3d().x(), Native.scale3d().y(), Native.scale3d().z()), Given.GetScale3D());
			const FQuat Rotation(Native.rotation().x(), Native.rotation().y(), Native.rotation().z(), Native.rotation().w());
			TestTrue(TEXT("transform read rotation"), Rotation.Equals(Given.GetRotation(), 1e-12));
		}

		Fixture.ValueOf<FTransform>(Fixture.Transform) = FTransform::Identity;
		TestTrue(TEXT("transform written back"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Transform), Out).ok());
		TestTrue(TEXT("transform round trip"), Fixture.ValueOf<FTransform>(Fixture.Transform).Equals(Given, 1e-9));
	}

	// FLinearColor: linear float RGBA, not squeezed into 8-bit sRGB.
	{
		Fixture.ValueOf<FLinearColor>(Fixture.LinearColor) = FLinearColor(0.25f, 1.5f, -0.5f, 0.75f);
		Out.Clear();
		TestTrue(TEXT("linear color read"), WritePropertyToValue(Fixture.Memory, Fixture.LinearColor, Out).ok());
		if (TestTrue(TEXT("linear color reads as linear_color_value"), Out.has_linear_color_value()))
		{
			TestEqual(TEXT("linear color read r"), Out.linear_color_value().r(), 0.25f);
			TestEqual(TEXT("linear color read g"), Out.linear_color_value().g(), 1.5f);
			TestEqual(TEXT("linear color read b"), Out.linear_color_value().b(), -0.5f);
			TestEqual(TEXT("linear color read a"), Out.linear_color_value().a(), 0.75f);
		}
	}

	// FColor: color_value, alpha always reported.
	{
		Fixture.ValueOf<FColor>(Fixture.Color) = FColor(1, 2, 3, 4);
		Out.Clear();
		TestTrue(TEXT("color read"), WritePropertyToValue(Fixture.Memory, Fixture.Color, Out).ok());
		if (TestTrue(TEXT("color reads as color_value"), Out.has_color_value()))
		{
			TestTrue(TEXT("color read sets alpha"), Out.color_value().has_a());
			TestEqual(TEXT("color read rgba"), FColor(Out.color_value().r(), Out.color_value().g(), Out.color_value().b(), Out.color_value().a()), FColor(1, 2, 3, 4));
		}
	}

	return true;
}

// A color_value write uses its alpha when given and is opaque when not, into an FColor and into the
// FLinearColor fallback alike.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldValueRoundTripColorAlphaTest,
	"Tempo.World.ValueRoundTrip.ColorAlpha", TempoWorldValueRoundTripTestFlags)
bool FTempoWorldValueRoundTripColorAlphaTest::RunTest(const FString& Parameters)
{
	FValueRoundTripFixture Fixture;

	TempoWorld::Value Opaque;
	Opaque.mutable_color_value()->set_r(10);
	Opaque.mutable_color_value()->set_g(20);
	Opaque.mutable_color_value()->set_b(30);

	TempoWorld::Value Translucent = Opaque;
	Translucent.mutable_color_value()->set_a(40);

	Fixture.ValueOf<FColor>(Fixture.Color) = FColor(0, 0, 0, 0);
	TestTrue(TEXT("color write without alpha"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Color), Opaque).ok());
	TestEqual(TEXT("unset alpha is 255"), Fixture.ValueOf<FColor>(Fixture.Color), FColor(10, 20, 30, 255));

	TestTrue(TEXT("color write with alpha"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Color), Translucent).ok());
	TestEqual(TEXT("given alpha is used"), Fixture.ValueOf<FColor>(Fixture.Color), FColor(10, 20, 30, 40));

	TempoWorld::Value Zero = Opaque;
	Zero.mutable_color_value()->set_a(0);
	TestTrue(TEXT("color write with zero alpha"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Color), Zero).ok());
	TestEqual(TEXT("an explicit zero alpha is not opaque"), Fixture.ValueOf<FColor>(Fixture.Color).A, static_cast<uint8>(0));

	TestTrue(TEXT("color_value into FLinearColor without alpha"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.LinearColor), Opaque).ok());
	TestEqual(TEXT("color_value into FLinearColor is sRGB-decoded and opaque"), Fixture.ValueOf<FLinearColor>(Fixture.LinearColor), FLinearColor(FColor(10, 20, 30, 255)));

	TestTrue(TEXT("color_value into FLinearColor with alpha"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.LinearColor), Translucent).ok());
	TestEqual(TEXT("color_value into FLinearColor keeps the given alpha"), Fixture.ValueOf<FLinearColor>(Fixture.LinearColor), FLinearColor(FColor(10, 20, 30, 40)));

	return true;
}

// A double property keeps its full precision both ways, scalar and in a container, and a float
// property takes the nearest float.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldValueRoundTripFloatPrecisionTest,
	"Tempo.World.ValueRoundTrip.FloatPrecision", TempoWorldValueRoundTripTestFlags)
bool FTempoWorldValueRoundTripFloatPrecisionTest::RunTest(const FString& Parameters)
{
	FValueRoundTripFixture Fixture;
	// Not representable as a float: it would come back as 0.10000000149011612.
	constexpr double Precise = 0.1;

	TempoWorld::Value Scalar;
	Scalar.set_float_value(Precise);
	TestTrue(TEXT("double write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Double), Scalar).ok());
	TestEqual(TEXT("a double is stored exactly"), *Fixture.Double->ContainerPtrToValuePtr<double>(Fixture.Memory), Precise);
	TempoWorld::Value ReadScalar;
	TestTrue(TEXT("double read"), WritePropertyToValue(Fixture.Memory, Fixture.Double, ReadScalar).ok());
	TestEqual(TEXT("a double reads exactly"), ReadScalar.float_value(), Precise);

	TestTrue(TEXT("float write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Float), Scalar).ok());
	TestEqual(TEXT("a float takes the nearest float"), *Fixture.Float->ContainerPtrToValuePtr<float>(Fixture.Memory), 0.1f);

	TempoWorld::Value Array;
	Array.mutable_float_array_value()->add_values(Precise);
	Array.mutable_float_array_value()->add_values(-Precise);
	TestTrue(TEXT("double array write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Doubles), Array).ok());
	TempoWorld::Value ReadArray;
	TestTrue(TEXT("double array read"), WritePropertyToValue(Fixture.Memory, Fixture.Doubles, ReadArray).ok());
	TestTrue(TEXT("double array reads exactly"), ReadArray.float_array_value().values_size() == 2
		&& ReadArray.float_array_value().values(0) == Precise && ReadArray.float_array_value().values(1) == -Precise);

	return true;
}

// The deprecated typed setters take radians, meters and right-handed axes. Each converts at its entry
// point into the native Value the shared write path takes, so what lands in memory is what it always
// was.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldValueRoundTripTypedSetterSITest,
	"Tempo.World.ValueRoundTrip.TypedSetterSI", TempoWorldValueRoundTripTestFlags)
bool FTempoWorldValueRoundTripTypedSetterSITest::RunTest(const FString& Parameters)
{
	FValueRoundTripFixture Fixture;

	// Rotator: radians to degrees; right-handed pitch and yaw are negated, roll is kept. The tolerance
	// is TempoConversion's: its radians-to-degrees factor is built from the single-precision UE_PI.
	TestTrue(TEXT("SI rotator write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Rotator),
		RotatorValueFromSI(UE_DOUBLE_PI / 6.0, UE_DOUBLE_PI / 4.0, UE_DOUBLE_HALF_PI)).ok());
	TestTrue(TEXT("SI rotator in memory"), Fixture.ValueOf<FRotator>(Fixture.Rotator).Equals(FRotator(-45.0, -90.0, 30.0), 1e-4));

	// Quat: right-handed to left-handed negates X and Z.
	TestTrue(TEXT("SI quat write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Quat),
		QuatValueFromSI(0.1, 0.2, 0.3, 0.9)).ok());
	const FQuat& Quat = Fixture.ValueOf<FQuat>(Fixture.Quat);
	TestTrue(TEXT("SI quat in memory"), Quat.Equals(FQuat(-0.1, 0.2, -0.3, 0.9), 1e-12));

	// Transform: meters to centimeters, right-handed to left-handed, the scale as given.
	TempoCore::Transform Given;
	Given.mutable_location()->set_x(1.0);
	Given.mutable_location()->set_y(2.0);
	Given.mutable_location()->set_z(3.0);
	Given.mutable_rotation()->set_y(UE_DOUBLE_HALF_PI);
	Given.mutable_scale()->set_x(2.0);
	Given.mutable_scale()->set_y(3.0);
	Given.mutable_scale()->set_z(4.0);
	TestTrue(TEXT("SI transform write"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Transform), TransformValueFromSI(Given)).ok());
	const FTransform& Transform = Fixture.ValueOf<FTransform>(Fixture.Transform);
	TestTrue(TEXT("SI transform location"), Transform.GetLocation().Equals(FVector(100.0, -200.0, 300.0), 1e-9));
	TestTrue(TEXT("SI transform rotation"), Transform.Rotator().Equals(FRotator(0.0, -90.0, 0.0), 1e-4));
	TestTrue(TEXT("SI transform scale"), Transform.GetScale3D().Equals(FVector(2.0, 3.0, 4.0), 0.0));

	// ... and without a scale, unit scale, as the typed setter has always written.
	Given.clear_scale();
	TestTrue(TEXT("SI transform write without scale"), SetValueOnTarget(nullptr, Fixture.TargetFor(Fixture.Transform), TransformValueFromSI(Given)).ok());
	TestTrue(TEXT("SI transform unset scale is one"), Fixture.ValueOf<FTransform>(Fixture.Transform).GetScale3D().Equals(FVector::OneVector, 0.0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
