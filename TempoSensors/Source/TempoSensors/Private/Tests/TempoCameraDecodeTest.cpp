// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoCamera.h"
#include "TempoSensors.h"
#include "TempoSensorsConstants.h"
#include "TempoSensorsSettings.h"

#include "Misc/AutomationTest.h"

// Unit tests for the camera measurement decode (TTextureRead<...>::RespondToRequests).
//
// The decode walks the pixel image once, in parallel over bands of rows, filling whichever of the
// color / label / depth / bounding box outputs were requested. These tests drive that real code
// path against a naive serial reference and check the results byte for byte. They need no RHI and
// no world: an FTextureRead is a plain struct, and the tests fill its Image directly rather than
// rendering into it.
//
// What they are looking for is band-splitting bugs — a dropped, duplicated or misplaced row, or a
// bounding box merged wrongly across bands. So the image sizes deliberately straddle the band
// count (see GDecodeRowBands in TempoCamera.cpp), the pixel pattern is unique per pixel so any
// misplaced row fails loudly instead of aliasing to a plausible value, and the bounding box cases
// put instances exactly on band seams and in disjoint bands.
//
// Run via Scripts/Test.sh Tempo.Sensors.CameraDecode, or from the editor console with
//   Automation RunTests Tempo.Sensors.CameraDecode

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr EAutomationTestFlags TempoCameraDecodeTestFlags =
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;

	// Depth is compared in meters. Any row misplacement moves depth by whole meters under the
	// pattern below, so this is far tighter than needed to catch one while still tolerating the
	// last-bit differences a vectorized inner loop can produce against a scalar reference.
	constexpr float DepthTolM = 1e-4f;

	constexpr float TestMinDepth = 10.0f;      // cm
	constexpr float TestMaxDepth = 40000.0f;   // cm

	// The pixel structs keep their channels private and expose only accessors — the GPU fills them
	// by writing raw bytes from the post-process material, so the tests do the same. Both layouts
	// are 4 label/color bytes (order differs per type, which is exactly why the reference below
	// reads through the accessors rather than assuming an order) plus, for the depth format, a
	// uint32 of discretized inverse depth.
	template <typename PixelType>
	void WritePixelBytes(PixelType& Pixel, uint8 Byte0, uint8 Byte1, uint8 Byte2, uint8 Label, uint32 DiscreteDepth)
	{
		uint8* Bytes = reinterpret_cast<uint8*>(&Pixel);
		Bytes[0] = Byte0;
		Bytes[1] = Byte1;
		Bytes[2] = Byte2;
		Bytes[3] = Label;
		if constexpr (PixelType::bSupportsDepth)
		{
			FMemory::Memcpy(Bytes + 4, &DiscreteDepth, sizeof(uint32));
		}
	}

	// Fill with a pattern that is unique per pixel, so a dropped, duplicated or misplaced row can
	// never coincidentally match the reference. Depth varies strongly with the row for the same
	// reason. Labels are supplied by the caller so the bounding box tests can control them.
	template <typename PixelType>
	void FillPattern(TArray<PixelType>& Image, int32 Width, int32 Height,
		const TFunctionRef<uint8(int32 /*X*/, int32 /*Y*/)>& LabelFn)
	{
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const int32 Index = Y * Width + X;
				// Three decorrelated bytes so a swapped color channel is also caught.
				const uint8 Byte0 = static_cast<uint8>((X * 7 + Y * 13) & 0xFF);
				const uint8 Byte1 = static_cast<uint8>((X * 31 + Y * 3 + 17) & 0xFF);
				const uint8 Byte2 = static_cast<uint8>((Index * 101) & 0xFF);
				// Spread across the 24-bit discretized inverse depth range, dominated by the row.
				const uint32 DiscreteDepth =
					static_cast<uint32>((static_cast<int64>(Y) * 4099 + X * 7) % (1 << 24));
				WritePixelBytes(Image[Index], Byte0, Byte1, Byte2, LabelFn(X, Y), DiscreteDepth);
			}
		}
	}

	// Every pixel labeled 0: bounding boxes stay empty, color/label/depth still exercised.
	uint8 NoLabels(int32, int32) { return 0; }

	// --- Serial references. Deliberately the simplest possible loops, reading through the same
	// accessors the production decode uses, so they test the band/parallel logic and nothing else.

	template <typename PixelType>
	TArray<uint8> ReferenceColor(const TArray<PixelType>& Image, EColorImageEncoding Encoding)
	{
		TArray<uint8> Out;
		Out.SetNumUninitialized(Image.Num() * 3);
		for (int32 Index = 0; Index < Image.Num(); ++Index)
		{
			if (Encoding == EColorImageEncoding::BGR8)
			{
				Out[Index * 3 + 0] = Image[Index].B();
				Out[Index * 3 + 1] = Image[Index].G();
				Out[Index * 3 + 2] = Image[Index].R();
			}
			else
			{
				Out[Index * 3 + 0] = Image[Index].R();
				Out[Index * 3 + 1] = Image[Index].G();
				Out[Index * 3 + 2] = Image[Index].B();
			}
		}
		return Out;
	}

	template <typename PixelType>
	TArray<uint8> ReferenceLabel(const TArray<PixelType>& Image)
	{
		TArray<uint8> Out;
		Out.SetNumUninitialized(Image.Num());
		for (int32 Index = 0; Index < Image.Num(); ++Index)
		{
			Out[Index] = Image[Index].Label();
		}
		return Out;
	}

	TArray<float> ReferenceDepth(const TArray<FCameraPixelWithDepth>& Image, float MinDepth, float MaxDepth)
	{
		TArray<float> Out;
		Out.SetNumUninitialized(Image.Num());
		for (int32 Index = 0; Index < Image.Num(); ++Index)
		{
			Out[Index] = QuantityConverter<CM2M>::Convert(
				Image[Index].Depth(MinDepth, MaxDepth, GTempoCamera_Max_Discrete_Depth));
		}
		return Out;
	}

	// A plain serial bounding box implementation, the reference the fused decode is checked against.
	template <typename PixelType>
	TMap<int32, FBox2D> ReferenceBoundingBoxes(const TArray<PixelType>& Image, int32 Width, int32 Height)
	{
		TMap<int32, FBox2D> Boxes;
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const uint8 InstanceId = Image[Y * Width + X].Label();
				if (InstanceId > 0)
				{
					Boxes.FindOrAdd(InstanceId) += FUintPoint(X, Y);
				}
			}
		}
		return Boxes;
	}

	// Collected responses from one RespondToRequests call.
	struct FDecodeResults
	{
		TOptional<TempoSensors::ColorImage> Color;
		TOptional<TempoSensors::LabelImage> Label;
		TOptional<TempoSensors::DepthImage> Depth;
		TOptional<TempoSensors::BoundingBoxes> Boxes;
		int32 NumResponses = 0;
	};

	template <typename RequestType, typename ResponseType>
	void AddRequest(TArray<RequestType>& Requests, TOptional<ResponseType>& Slot, int32& NumResponses)
	{
		RequestType Request;
		Request.ResponseContinuation = TResponseDelegate<ResponseType>::CreateLambda(
			[&Slot, &NumResponses](const ResponseType& Response, grpc::Status)
			{
				Slot = Response;
				++NumResponses;
			});
		Requests.Add(MoveTemp(Request));
	}

	// Scoped override of the private, config-backed color encoding, so both swizzle branches get
	// covered whatever the project is configured for. Returns false if the property moved, in which
	// case the caller tests only the configured encoding rather than silently passing.
	class FScopedColorEncoding
	{
	public:
		explicit FScopedColorEncoding(EColorImageEncoding Encoding)
		{
			Property = CastField<FByteProperty>(
				UTempoSensorsSettings::StaticClass()->FindPropertyByName(TEXT("ColorImageEncoding")));
			if (!Property)
			{
				return;
			}
			Settings = GetMutableDefault<UTempoSensorsSettings>();
			uint8* Value = Property->ContainerPtrToValuePtr<uint8>(Settings);
			Previous = *Value;
			*Value = static_cast<uint8>(Encoding);
			bApplied = true;
		}

		~FScopedColorEncoding()
		{
			if (bApplied)
			{
				*Property->ContainerPtrToValuePtr<uint8>(Settings) = Previous;
			}
		}

		bool IsApplied() const { return bApplied; }

	private:
		FByteProperty* Property = nullptr;
		UTempoSensorsSettings* Settings = nullptr;
		uint8 Previous = 0;
		bool bApplied = false;
	};

	// Sizes chosen to straddle the decode's band count: below it, exactly on it, one either side,
	// plus a tall image whose height does not divide evenly into bands, and a realistic frame.
	const TArray<FIntPoint>& BandStressSizes()
	{
		static const TArray<FIntPoint> Sizes = {
			FIntPoint(1, 1),      // degenerate
			FIntPoint(3, 1),
			FIntPoint(1, 3),
			FIntPoint(5, 63),     // one band short of the cap
			FIntPoint(5, 64),     // exactly the cap
			FIntPoint(5, 65),     // one over: uneven bands
			FIntPoint(7, 127),
			FIntPoint(17, 128),
			FIntPoint(16, 1080),  // 1080 / 64 is not an integer
			FIntPoint(320, 240),  // realistic-ish frame
		};
		return Sizes;
	}
}

// Color, label and depth for a WithDepth read, across sizes that straddle the band count and in
// both color encodings.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraDecodeImagesTest,
	"Tempo.Sensors.CameraDecode.Images", TempoCameraDecodeTestFlags)
bool FTempoCameraDecodeImagesTest::RunTest(const FString& Parameters)
{
	for (const EColorImageEncoding Encoding : { EColorImageEncoding::BGR8, EColorImageEncoding::RGB8 })
	{
		const FScopedColorEncoding ScopedEncoding(Encoding);
		if (!ScopedEncoding.IsApplied())
		{
			AddWarning(TEXT("Could not override UTempoSensorsSettings::ColorImageEncoding by name; "
				"testing only the configured encoding. Did the property get renamed?"));
		}
		const EColorImageEncoding ActualEncoding = GetDefault<UTempoSensorsSettings>()->GetColorImageEncoding();

		for (const FIntPoint& Size : BandStressSizes())
		{
			const FString What = FString::Printf(TEXT("%dx%d enc=%d"), Size.X, Size.Y, static_cast<int32>(ActualEncoding));

			TTextureRead<FCameraPixelWithDepth> Read(Size, /*SequenceId=*/7, /*CaptureTime=*/1.5,
				TEXT("Owner"), TEXT("Sensor"), FTransform::Identity, TestMinDepth, TestMaxDepth, {});
			FillPattern(Read.Image, Size.X, Size.Y, TFunctionRef<uint8(int32, int32)>(NoLabels));

			TArray<FColorImageRequest> ColorRequests;
			TArray<FLabelImageRequest> LabelRequests;
			TArray<FDepthImageRequest> DepthRequests;
			const TArray<FBoundingBoxesRequest> NoBoxRequests;
			FDecodeResults Results;
			AddRequest(ColorRequests, Results.Color, Results.NumResponses);
			AddRequest(LabelRequests, Results.Label, Results.NumResponses);
			AddRequest(DepthRequests, Results.Depth, Results.NumResponses);

			Read.RespondToRequests(ColorRequests, LabelRequests, DepthRequests, NoBoxRequests, /*TransmissionTime=*/2.0f);

			if (!TestEqual(*FString::Printf(TEXT("%s: three responses sent"), *What), Results.NumResponses, 3))
			{
				return false;
			}

			// Color
			const TArray<uint8> ExpectedColor = ReferenceColor(Read.Image, ActualEncoding);
			const std::string& ActualColor = Results.Color->data();
			if (!TestEqual(*FString::Printf(TEXT("%s: color byte count"), *What),
				static_cast<int32>(ActualColor.size()), ExpectedColor.Num()))
			{
				return false;
			}
			if (FMemory::Memcmp(ActualColor.data(), ExpectedColor.GetData(), ExpectedColor.Num()) != 0)
			{
				AddError(FString::Printf(TEXT("%s: color bytes differ from reference"), *What));
				return false;
			}
			TestEqual(*FString::Printf(TEXT("%s: color width"), *What), static_cast<int32>(Results.Color->width_px()), Size.X);
			TestEqual(*FString::Printf(TEXT("%s: color height"), *What), static_cast<int32>(Results.Color->height_px()), Size.Y);

			// Label
			const TArray<uint8> ExpectedLabel = ReferenceLabel(Read.Image);
			const std::string& ActualLabel = Results.Label->data();
			if (!TestEqual(*FString::Printf(TEXT("%s: label byte count"), *What),
				static_cast<int32>(ActualLabel.size()), ExpectedLabel.Num()))
			{
				return false;
			}
			if (FMemory::Memcmp(ActualLabel.data(), ExpectedLabel.GetData(), ExpectedLabel.Num()) != 0)
			{
				AddError(FString::Printf(TEXT("%s: label bytes differ from reference"), *What));
				return false;
			}

			// Depth
			const TArray<float> ExpectedDepth = ReferenceDepth(Read.Image, TestMinDepth, TestMaxDepth);
			const std::string& ActualDepthBlob = Results.Depth->depths_m();
			if (!TestEqual(*FString::Printf(TEXT("%s: depth byte count"), *What),
				static_cast<int32>(ActualDepthBlob.size()), ExpectedDepth.Num() * static_cast<int32>(sizeof(float))))
			{
				return false;
			}
			const float* ActualDepth = reinterpret_cast<const float*>(ActualDepthBlob.data());
			for (int32 Index = 0; Index < ExpectedDepth.Num(); ++Index)
			{
				if (FMath::Abs(ActualDepth[Index] - ExpectedDepth[Index]) > DepthTolM)
				{
					AddError(FString::Printf(TEXT("%s: depth differs at pixel %d (%f vs %f)"),
						*What, Index, ActualDepth[Index], ExpectedDepth[Index]));
					return false;
				}
			}
		}
	}

	return true;
}

// Bounding boxes, with instances placed to break a wrong band merge.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraDecodeBoundingBoxesTest,
	"Tempo.Sensors.CameraDecode.BoundingBoxes", TempoCameraDecodeTestFlags)
bool FTempoCameraDecodeBoundingBoxesTest::RunTest(const FString& Parameters)
{
	// 1080 rows over 64 bands gives bands of uneven height, and enough of them that a merge that
	// overwrote instead of unioning would be obvious.
	const FIntPoint Size(64, 1080);
	const int32 W = Size.X;
	const int32 H = Size.Y;

	// Instance layout, each case aimed at a specific way the band merge could go wrong:
	//   1: spans every row in one column      -> a merge that keeps only one band truncates it
	//   2: present only in the first and last row -> disjoint bands, must union to the full height
	//   3: single pixel at the origin         -> first row of the first band
	//   4: single pixel at the far corner     -> last row of the last band
	//   5: one row tall, sitting on a band seam
	// 255: max instance id
	const int32 SeamRow = H / 64;  // first row of the second band
	const auto LabelFn = [W, H, SeamRow](int32 X, int32 Y) -> uint8
	{
		if (X == 5) { return 1; }
		if ((Y == 0 || Y == H - 1) && X == 9) { return 2; }
		if (X == 0 && Y == 0) { return 3; }
		if (X == W - 1 && Y == H - 1) { return 4; }
		if (Y == SeamRow && X >= 20 && X <= 25) { return 5; }
		if (Y == H / 2 && X >= 30 && X <= 40) { return 255; }
		return 0;
	};

	TMap<uint8, uint8> SemanticMap;
	for (const uint8 InstanceId : { 1, 2, 3, 4, 5, 255 })
	{
		SemanticMap.Add(InstanceId, InstanceId);
	}

	TTextureRead<FCameraPixelWithDepth> Read(Size, /*SequenceId=*/1, /*CaptureTime=*/0.0,
		TEXT("Owner"), TEXT("Sensor"), FTransform::Identity, TestMinDepth, TestMaxDepth, MoveTemp(SemanticMap));
	FillPattern(Read.Image, W, H, TFunctionRef<uint8(int32, int32)>(LabelFn));

	const TArray<FColorImageRequest> NoColor;
	const TArray<FLabelImageRequest> NoLabel;
	const TArray<FDepthImageRequest> NoDepth;
	TArray<FBoundingBoxesRequest> BoxRequests;
	FDecodeResults Results;
	AddRequest(BoxRequests, Results.Boxes, Results.NumResponses);

	Read.RespondToRequests(NoColor, NoLabel, NoDepth, BoxRequests, /*TransmissionTime=*/0.0f);

	if (!TestEqual(TEXT("one bounding boxes response sent"), Results.NumResponses, 1))
	{
		return false;
	}

	const TMap<int32, FBox2D> Expected = ReferenceBoundingBoxes(Read.Image, W, H);
	if (!TestEqual(TEXT("bounding box count matches reference"),
		Results.Boxes->bounding_boxes_size(), Expected.Num()))
	{
		return false;
	}

	for (const TempoSensors::BoundingBox2D& Actual : Results.Boxes->bounding_boxes())
	{
		const FBox2D* ExpectedBox = Expected.Find(static_cast<int32>(Actual.instance_id()));
		if (!ExpectedBox)
		{
			AddError(FString::Printf(TEXT("decode reported instance %u that the reference did not find"),
				Actual.instance_id()));
			return false;
		}
		const FString What = FString::Printf(TEXT("instance %u"), Actual.instance_id());
		TestEqual(*(What + TEXT(" min x")), static_cast<int32>(Actual.min_x_px()), FMath::RoundToInt32(ExpectedBox->Min.X));
		TestEqual(*(What + TEXT(" min y")), static_cast<int32>(Actual.min_y_px()), FMath::RoundToInt32(ExpectedBox->Min.Y));
		TestEqual(*(What + TEXT(" max x")), static_cast<int32>(Actual.max_x_px()), FMath::RoundToInt32(ExpectedBox->Max.X));
		TestEqual(*(What + TEXT(" max y")), static_cast<int32>(Actual.max_y_px()), FMath::RoundToInt32(ExpectedBox->Max.Y));
	}

	// Spell out the two cases the reference could get wrong in the same way the decode might, so a
	// shared misunderstanding cannot make both agree.
	const TMap<int32, FBox2D>& Ref = Expected;
	if (const FBox2D* FullColumn = Ref.Find(1))
	{
		TestEqual(TEXT("instance 1 spans every row (min)"), FMath::RoundToInt32(FullColumn->Min.Y), 0);
		TestEqual(TEXT("instance 1 spans every row (max)"), FMath::RoundToInt32(FullColumn->Max.Y), H - 1);
	}
	if (const FBox2D* Disjoint = Ref.Find(2))
	{
		TestEqual(TEXT("instance 2 unions across disjoint bands (min)"), FMath::RoundToInt32(Disjoint->Min.Y), 0);
		TestEqual(TEXT("instance 2 unions across disjoint bands (max)"), FMath::RoundToInt32(Disjoint->Max.Y), H - 1);
	}
	TestFalse(TEXT("label 0 does not produce a bounding box"), Ref.Contains(0));

	return true;
}

// Every combination of requested measurement types. The decode branches on which outputs are
// non-null, so a subset is a genuinely different path from the full set.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraDecodeRequestSubsetsTest,
	"Tempo.Sensors.CameraDecode.RequestSubsets", TempoCameraDecodeTestFlags)
bool FTempoCameraDecodeRequestSubsetsTest::RunTest(const FString& Parameters)
{
	const FIntPoint Size(37, 101);
	const EColorImageEncoding Encoding = GetDefault<UTempoSensorsSettings>()->GetColorImageEncoding();

	const auto LabelFn = [](int32 X, int32 Y) -> uint8 { return static_cast<uint8>(((X / 4) + (Y / 8)) % 7); };

	for (int32 Mask = 0; Mask < 16; ++Mask)
	{
		const bool bColor = (Mask & 1) != 0;
		const bool bLabel = (Mask & 2) != 0;
		const bool bDepth = (Mask & 4) != 0;
		const bool bBoxes = (Mask & 8) != 0;
		const FString What = FString::Printf(TEXT("mask=%d"), Mask);

		TMap<uint8, uint8> SemanticMap;
		for (uint8 InstanceId = 1; InstanceId < 7; ++InstanceId)
		{
			SemanticMap.Add(InstanceId, InstanceId);
		}

		TTextureRead<FCameraPixelWithDepth> Read(Size, /*SequenceId=*/3, /*CaptureTime=*/0.0,
			TEXT("Owner"), TEXT("Sensor"), FTransform::Identity, TestMinDepth, TestMaxDepth, MoveTemp(SemanticMap));
		FillPattern(Read.Image, Size.X, Size.Y, TFunctionRef<uint8(int32, int32)>(LabelFn));

		TArray<FColorImageRequest> ColorRequests;
		TArray<FLabelImageRequest> LabelRequests;
		TArray<FDepthImageRequest> DepthRequests;
		TArray<FBoundingBoxesRequest> BoxRequests;
		FDecodeResults Results;
		if (bColor) { AddRequest(ColorRequests, Results.Color, Results.NumResponses); }
		if (bLabel) { AddRequest(LabelRequests, Results.Label, Results.NumResponses); }
		if (bDepth) { AddRequest(DepthRequests, Results.Depth, Results.NumResponses); }
		if (bBoxes) { AddRequest(BoxRequests, Results.Boxes, Results.NumResponses); }

		Read.RespondToRequests(ColorRequests, LabelRequests, DepthRequests, BoxRequests, /*TransmissionTime=*/0.0f);

		const int32 ExpectedResponses = (bColor ? 1 : 0) + (bLabel ? 1 : 0) + (bDepth ? 1 : 0) + (bBoxes ? 1 : 0);
		if (!TestEqual(*FString::Printf(TEXT("%s: response count"), *What), Results.NumResponses, ExpectedResponses))
		{
			return false;
		}

		// Only the requested types are answered.
		TestEqual(*FString::Printf(TEXT("%s: color answered"), *What), Results.Color.IsSet(), bColor);
		TestEqual(*FString::Printf(TEXT("%s: label answered"), *What), Results.Label.IsSet(), bLabel);
		TestEqual(*FString::Printf(TEXT("%s: depth answered"), *What), Results.Depth.IsSet(), bDepth);
		TestEqual(*FString::Printf(TEXT("%s: boxes answered"), *What), Results.Boxes.IsSet(), bBoxes);

		// ...and each answered type is still correct when decoded alongside any other subset.
		if (bColor)
		{
			const TArray<uint8> ExpectedColor = ReferenceColor(Read.Image, Encoding);
			const std::string& ActualColor = Results.Color->data();
			TestEqual(*FString::Printf(TEXT("%s: color size"), *What), static_cast<int32>(ActualColor.size()), ExpectedColor.Num());
			if (ActualColor.size() == static_cast<size_t>(ExpectedColor.Num())
				&& FMemory::Memcmp(ActualColor.data(), ExpectedColor.GetData(), ExpectedColor.Num()) != 0)
			{
				AddError(FString::Printf(TEXT("%s: color bytes differ"), *What));
				return false;
			}
		}
		if (bLabel)
		{
			const TArray<uint8> ExpectedLabel = ReferenceLabel(Read.Image);
			const std::string& ActualLabel = Results.Label->data();
			TestEqual(*FString::Printf(TEXT("%s: label size"), *What), static_cast<int32>(ActualLabel.size()), ExpectedLabel.Num());
			if (ActualLabel.size() == static_cast<size_t>(ExpectedLabel.Num())
				&& FMemory::Memcmp(ActualLabel.data(), ExpectedLabel.GetData(), ExpectedLabel.Num()) != 0)
			{
				AddError(FString::Printf(TEXT("%s: label bytes differ"), *What));
				return false;
			}
		}
		if (bDepth)
		{
			const TArray<float> ExpectedDepth = ReferenceDepth(Read.Image, TestMinDepth, TestMaxDepth);
			const std::string& Blob = Results.Depth->depths_m();
			TestEqual(*FString::Printf(TEXT("%s: depth size"), *What),
				static_cast<int32>(Blob.size()), ExpectedDepth.Num() * static_cast<int32>(sizeof(float)));
			const float* ActualDepth = reinterpret_cast<const float*>(Blob.data());
			for (int32 Index = 0; Index < ExpectedDepth.Num(); ++Index)
			{
				if (FMath::Abs(ActualDepth[Index] - ExpectedDepth[Index]) > DepthTolM)
				{
					AddError(FString::Printf(TEXT("%s: depth differs at pixel %d"), *What, Index));
					return false;
				}
			}
		}
		if (bBoxes)
		{
			const TMap<int32, FBox2D> ExpectedBoxes = ReferenceBoundingBoxes(Read.Image, Size.X, Size.Y);
			TestEqual(*FString::Printf(TEXT("%s: box count"), *What),
				Results.Boxes->bounding_boxes_size(), ExpectedBoxes.Num());
		}
	}

	return true;
}

// The NoDepth specialization: a different pixel layout (its color channels are in the opposite
// order) and a RespondToRequests that takes no depth requests at all.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTempoCameraDecodeNoDepthTest,
	"Tempo.Sensors.CameraDecode.NoDepthPixel", TempoCameraDecodeTestFlags)
bool FTempoCameraDecodeNoDepthTest::RunTest(const FString& Parameters)
{
	const EColorImageEncoding Encoding = GetDefault<UTempoSensorsSettings>()->GetColorImageEncoding();
	const auto LabelFn = [](int32 X, int32 Y) -> uint8 { return static_cast<uint8>((X == Y) ? 1 : 0); };

	for (const FIntPoint& Size : BandStressSizes())
	{
		const FString What = FString::Printf(TEXT("%dx%d"), Size.X, Size.Y);

		TMap<uint8, uint8> SemanticMap;
		SemanticMap.Add(1, 1);

		TTextureRead<FCameraPixelNoDepth> Read(Size, /*SequenceId=*/2, /*CaptureTime=*/0.0,
			TEXT("Owner"), TEXT("Sensor"), FTransform::Identity, MoveTemp(SemanticMap));
		FillPattern(Read.Image, Size.X, Size.Y, TFunctionRef<uint8(int32, int32)>(LabelFn));

		TArray<FColorImageRequest> ColorRequests;
		TArray<FLabelImageRequest> LabelRequests;
		TArray<FBoundingBoxesRequest> BoxRequests;
		FDecodeResults Results;
		AddRequest(ColorRequests, Results.Color, Results.NumResponses);
		AddRequest(LabelRequests, Results.Label, Results.NumResponses);
		AddRequest(BoxRequests, Results.Boxes, Results.NumResponses);

		Read.RespondToRequests(ColorRequests, LabelRequests, BoxRequests, /*TransmissionTime=*/0.0f);

		if (!TestEqual(*FString::Printf(TEXT("%s: three responses sent"), *What), Results.NumResponses, 3))
		{
			return false;
		}

		const TArray<uint8> ExpectedColor = ReferenceColor(Read.Image, Encoding);
		const std::string& ActualColor = Results.Color->data();
		if (!TestEqual(*FString::Printf(TEXT("%s: color size"), *What),
			static_cast<int32>(ActualColor.size()), ExpectedColor.Num()))
		{
			return false;
		}
		if (FMemory::Memcmp(ActualColor.data(), ExpectedColor.GetData(), ExpectedColor.Num()) != 0)
		{
			AddError(FString::Printf(TEXT("%s: color bytes differ from reference"), *What));
			return false;
		}

		const TArray<uint8> ExpectedLabel = ReferenceLabel(Read.Image);
		const std::string& ActualLabel = Results.Label->data();
		if (!TestEqual(*FString::Printf(TEXT("%s: label size"), *What),
			static_cast<int32>(ActualLabel.size()), ExpectedLabel.Num()))
		{
			return false;
		}
		if (FMemory::Memcmp(ActualLabel.data(), ExpectedLabel.GetData(), ExpectedLabel.Num()) != 0)
		{
			AddError(FString::Printf(TEXT("%s: label bytes differ from reference"), *What));
			return false;
		}

		const TMap<int32, FBox2D> ExpectedBoxes = ReferenceBoundingBoxes(Read.Image, Size.X, Size.Y);
		TestEqual(*FString::Printf(TEXT("%s: box count"), *What),
			Results.Boxes->bounding_boxes_size(), ExpectedBoxes.Num());
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
