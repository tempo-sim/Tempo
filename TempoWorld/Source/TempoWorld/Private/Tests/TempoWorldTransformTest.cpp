// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoWorldTransforms.h"

#include "TempoWorld/WorldControl.pb.h"

#include "Misc/AutomationTest.h"

// Unit tests for the conversions between TempoCore.Transform and FTransform, and for which scale a
// SetActorTransform or SetComponentTransform request asks for. No world and no RHI. Run via:
//   Scripts/Test.sh Tempo.World.Transform

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoWorldTransformTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	void SetVector(TempoCore::Vector& Out, double X, double Y, double Z)
	{
		Out.set_x(X);
		Out.set_y(Y);
		Out.set_z(Z);
	}
}

// A given scale is used unconverted, an unset one is unit, and a reported transform always has one.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldTransformScaleTest,
	"Tempo.World.Transform.Scale", TempoWorldTransformTestFlags)
bool FTempoWorldTransformScaleTest::RunTest(const FString& Parameters)
{
	TempoCore::Transform Given;
	SetVector(*Given.mutable_location(), 1.0, 2.0, 3.0);
	Given.mutable_rotation()->set_y(UE_DOUBLE_HALF_PI);

	const FTransform Unscaled = ToUnrealTransform(Given);
	TestTrue(TEXT("unset scale is one"), Unscaled.GetScale3D().Equals(FVector::OneVector, 0.0));
	TestTrue(TEXT("location is converted"), Unscaled.GetLocation().Equals(FVector(100.0, -200.0, 300.0), 1e-9));

	SetVector(*Given.mutable_scale(), 2.0, -3.0, 0.5);
	const FTransform Scaled = ToUnrealTransform(Given);
	TestTrue(TEXT("given scale is used unconverted"), Scaled.GetScale3D().Equals(FVector(2.0, -3.0, 0.5), 0.0));
	TestTrue(TEXT("scale leaves location alone"), Scaled.GetLocation().Equals(Unscaled.GetLocation(), 0.0));

	const TempoCore::Transform Reported = FromUnrealTransform(Scaled);
	if (TestTrue(TEXT("a reported transform has a scale"), Reported.has_scale()))
	{
		TestEqual(TEXT("reported scale x"), Reported.scale().x(), 2.0);
		TestEqual(TEXT("reported scale y"), Reported.scale().y(), -3.0);
		TestEqual(TEXT("reported scale z"), Reported.scale().z(), 0.5);
	}
	TestTrue(TEXT("unit scale is reported too"), FromUnrealTransform(FTransform::Identity).has_scale());

	const FTransform RoundTrip = ToUnrealTransform(Reported);
	TestTrue(TEXT("round trip"), RoundTrip.Equals(Scaled, 1e-9));

	return true;
}

// transform.scale, else the deprecated field, and never both.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoWorldTransformRequestedScaleTest,
	"Tempo.World.Transform.RequestedScale", TempoWorldTransformTestFlags)
bool FTempoWorldTransformRequestedScaleTest::RunTest(const FString& Parameters)
{
	auto Check = [this](const TCHAR* What, auto Request)
	{
		TOptional<FVector> Scale;
		TestTrue(FString::Printf(TEXT("%s: no scale asked for"), What), GetRequestedScale(Request, Scale).ok() && !Scale.IsSet());

		SetVector(*Request.mutable_transform()->mutable_scale(), 2.0, 3.0, 4.0);
		Scale.Reset();
		TestTrue(FString::Printf(TEXT("%s: transform.scale accepted"), What), GetRequestedScale(Request, Scale).ok());
		TestTrue(FString::Printf(TEXT("%s: transform.scale used"), What), Scale.IsSet() && Scale->Equals(FVector(2.0, 3.0, 4.0), 0.0));

PRAGMA_DISABLE_DEPRECATION_WARNINGS
		SetVector(*Request.mutable_scale(), 5.0, 6.0, 7.0);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Scale.Reset();
		const grpc::Status Both = GetRequestedScale(Request, Scale);
		TestEqual(FString::Printf(TEXT("%s: both set is INVALID_ARGUMENT"), What), Both.error_code(), grpc::INVALID_ARGUMENT);
		TestFalse(FString::Printf(TEXT("%s: both set yields no scale"), What), Scale.IsSet());

		Request.mutable_transform()->clear_scale();
		Scale.Reset();
		TestTrue(FString::Printf(TEXT("%s: deprecated scale accepted alone"), What), GetRequestedScale(Request, Scale).ok());
		TestTrue(FString::Printf(TEXT("%s: deprecated scale used"), What), Scale.IsSet() && Scale->Equals(FVector(5.0, 6.0, 7.0), 0.0));
	};

	Check(TEXT("SetActorTransform"), TempoWorld::SetActorTransformRequest());
	Check(TEXT("SetComponentTransform"), TempoWorld::SetComponentTransformRequest());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
