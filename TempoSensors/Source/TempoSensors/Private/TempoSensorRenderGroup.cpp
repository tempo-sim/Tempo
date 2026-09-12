// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoSensorRenderGroup.h"

#include "TempoMultiViewCapture.h"
#include "TempoSensors.h"
#include "TempoTiledSceneCaptureComponent.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "RenderingThread.h"
#include "RHICommandList.h"
#include "RHIGlobals.h"
#include "TextureResource.h"
#include "TimerManager.h"

bool FTempoSensorFamilySignature::operator==(const FTempoSensorFamilySignature& Other) const
{
	return SensorClass == Other.SensorClass
		&& CaptureSource == Other.CaptureSource
		&& PixelFormat == Other.PixelFormat
		&& TargetGamma == Other.TargetGamma
		&& bForceLinearGamma == Other.bForceLinearGamma
		&& bSRGB == Other.bSRGB
		&& ResolutionFraction == Other.ResolutionFraction
		&& bUseRayTracingIfEnabled == Other.bUseRayTracingIfEnabled
		&& CompositeMode == Other.CompositeMode
		&& ShowFlags == Other.ShowFlags;
}

FIntPoint FTempoSensorGroupRenderDesc::GetBlockSize() const
{
	return BlockRT ? FIntPoint(BlockRT->SizeX, BlockRT->SizeY) : FIntPoint::ZeroValue;
}

FTempoSensorFamilySignature FTempoSensorGroupRenderDesc::MakeSignature(const UTempoTiledSceneCaptureComponent* Sensor) const
{
	check(BlockRT && Sensor);
	FTempoSensorFamilySignature Signature;
	Signature.SensorClass = Sensor->GetClass();
	Signature.CaptureSource = CaptureSource;
	Signature.PixelFormat = BlockRT->GetFormat();
	Signature.TargetGamma = BlockRT->TargetGamma;
	Signature.bForceLinearGamma = BlockRT->bForceLinearGamma;
	Signature.bSRGB = BlockRT->SRGB;
	Signature.ResolutionFraction = ResolutionFraction;
	Signature.ShowFlags = ShowFlags.ToString();
	Signature.bUseRayTracingIfEnabled = bAllowRayTracing && Sensor->bUseRayTracingIfEnabled;
	Signature.CompositeMode = Sensor->CompositeMode;
	return Signature;
}

namespace TempoSensorRenderGroupLayout
{
namespace
{
	// Shelf-pack Blocks, in Order, into rows no wider than TargetWidth and an atlas no taller than
	// MaxDimension. Writes each block's offset (or (-1, -1)) and returns the atlas size.
	FIntPoint PackWithWidth(TArrayView<FBlock> Blocks, TConstArrayView<int32> Order, int32 TargetWidth, int32 MaxDimension, int32& OutNumPlaced)
	{
		for (FBlock& Block : Blocks)
		{
			Block.Offset = FIntPoint(-1, -1);
		}
		OutNumPlaced = 0;

		FIntPoint AtlasSize = FIntPoint::ZeroValue;
		int32 ShelfX = 0;
		int32 ShelfY = 0;
		int32 ShelfHeight = 0;
		for (const int32 Index : Order)
		{
			FBlock& Block = Blocks[Index];
			if (Block.Size.X > MaxDimension || Block.Size.Y > MaxDimension)
			{
				continue;
			}
			if (ShelfX + Block.Size.X > TargetWidth && ShelfX > 0)
			{
				ShelfY += ShelfHeight;
				ShelfX = 0;
				ShelfHeight = 0;
			}
			if (ShelfY + Block.Size.Y > MaxDimension)
			{
				// Blocks are sorted tallest first, so nothing after this fits a new shelf either, but a
				// shorter one may still fit on the current shelf. Keep going.
				continue;
			}
			Block.Offset = FIntPoint(ShelfX, ShelfY);
			++OutNumPlaced;
			ShelfX += Block.Size.X;
			ShelfHeight = FMath::Max(ShelfHeight, Block.Size.Y);
			AtlasSize.X = FMath::Max(AtlasSize.X, ShelfX);
			AtlasSize.Y = FMath::Max(AtlasSize.Y, ShelfY + ShelfHeight);
		}
		return AtlasSize;
	}
}

FIntPoint Pack(TArrayView<FBlock> Blocks, int32 MaxDimension)
{
	// Tallest first so each shelf wastes as little height as possible; widest first within a height
	// so the shelf fills from its widest occupant.
	TArray<int32, TInlineAllocator<16>> Order;
	int64 TotalArea = 0;
	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		const FIntPoint Size = Blocks[Index].Size;
		if (Size.X <= 0 || Size.Y <= 0)
		{
			continue;
		}
		Order.Add(Index);
		TotalArea += static_cast<int64>(Size.X) * Size.Y;
	}
	Order.Sort([&Blocks](int32 A, int32 B)
	{
		const FIntPoint& SizeA = Blocks[A].Size;
		const FIntPoint& SizeB = Blocks[B].Size;
		if (SizeA.Y != SizeB.Y)
		{
			return SizeA.Y > SizeB.Y;
		}
		if (SizeA.X != SizeB.X)
		{
			return SizeA.X > SizeB.X;
		}
		return A < B;
	});

	// Candidate row widths: the width of a perfectly packed square, and every prefix sum of the
	// sorted block widths (so a row can hold exactly k of the widest blocks). Pack with each and
	// keep the result that places the most blocks, then the squarest, then the smallest.
	TArray<int32, TInlineAllocator<16>> CandidateWidths;
	CandidateWidths.Add(FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TotalArea))), 1, MaxDimension));
	int32 PrefixWidth = 0;
	for (const int32 Index : Order)
	{
		PrefixWidth += Blocks[Index].Size.X;
		CandidateWidths.AddUnique(FMath::Clamp(PrefixWidth, 1, MaxDimension));
	}

	TArray<FBlock, TInlineAllocator<16>> Best;
	FIntPoint BestSize = FIntPoint::ZeroValue;
	int32 BestNumPlaced = -1;
	TArray<FBlock, TInlineAllocator<16>> Scratch;
	for (const int32 Width : CandidateWidths)
	{
		Scratch = TArray<FBlock, TInlineAllocator<16>>(Blocks);
		int32 NumPlaced = 0;
		const FIntPoint Size = PackWithWidth(Scratch, Order, Width, MaxDimension, NumPlaced);
		const bool bBetter =
			NumPlaced > BestNumPlaced
			|| (NumPlaced == BestNumPlaced && FMath::Max(Size.X, Size.Y) < FMath::Max(BestSize.X, BestSize.Y))
			|| (NumPlaced == BestNumPlaced && FMath::Max(Size.X, Size.Y) == FMath::Max(BestSize.X, BestSize.Y)
				&& static_cast<int64>(Size.X) * Size.Y < static_cast<int64>(BestSize.X) * BestSize.Y);
		if (bBetter)
		{
			Best = Scratch;
			BestSize = Size;
			BestNumPlaced = NumPlaced;
		}
	}

	for (int32 Index = 0; Index < Blocks.Num(); ++Index)
	{
		Blocks[Index].Offset = Best.IsValidIndex(Index) ? Best[Index].Offset : FIntPoint(-1, -1);
	}
	return BestSize;
}
} // namespace TempoSensorRenderGroupLayout

void UTempoSensorRenderGroup::Initialize(AActor* InOwner, float InRateHz, const UClass* InSensorClass)
{
	Owner = InOwner;
	RateHz = InRateHz;
	SensorClass = InSensorClass;
	StartTimer();
}

bool UTempoSensorRenderGroup::Matches(const AActor* InOwner, float InRateHz, const UClass* InSensorClass) const
{
	return Owner.Get() == InOwner && SensorClass == InSensorClass && FMath::IsNearlyEqual(RateHz, InRateHz);
}

void UTempoSensorRenderGroup::AddMember(UTempoTiledSceneCaptureComponent* Sensor)
{
	Members.AddUnique(Sensor);
}

void UTempoSensorRenderGroup::RemoveMember(UTempoTiledSceneCaptureComponent* Sensor)
{
	Members.Remove(Sensor);
	WarnedUnplaced.Remove(Sensor);
}

void UTempoSensorRenderGroup::Shutdown()
{
	StopTimer();
	Members.Empty();
	for (TArray<FFamilyLayout>& Layouts : StageLayouts)
	{
		for (FFamilyLayout& Layout : Layouts)
		{
			RetireAtlas(Layout.Atlas);
		}
	}
	StageLayouts.Empty();
}

void UTempoSensorRenderGroup::BeginDestroy()
{
	StopTimer();
	Super::BeginDestroy();
}

void UTempoSensorRenderGroup::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UTempoSensorRenderGroup* This = CastChecked<UTempoSensorRenderGroup>(InThis);
	for (TArray<FFamilyLayout>& Layouts : This->StageLayouts)
	{
		for (FFamilyLayout& Layout : Layouts)
		{
			Collector.AddReferencedObject(Layout.Atlas);
		}
	}
	Super::AddReferencedObjects(InThis, Collector);
}

void UTempoSensorRenderGroup::StartTimer()
{
	if (UWorld* World = GetWorld())
	{
		const float TimerPeriod = 1.0f / FMath::Max(UE_KINDA_SMALL_NUMBER, RateHz);
		World->GetTimerManager().SetTimer(TimerHandle, this, &UTempoSensorRenderGroup::OnTimer, TimerPeriod, true);
	}
}

void UTempoSensorRenderGroup::StopTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TimerHandle);
	}
}

void UTempoSensorRenderGroup::PruneMembers()
{
	Members.RemoveAll([](const TWeakObjectPtr<UTempoTiledSceneCaptureComponent>& Member)
	{
		return !Member.IsValid();
	});
}

void UTempoSensorRenderGroup::OnTimer()
{
	PruneMembers();
	for (const TWeakObjectPtr<UTempoTiledSceneCaptureComponent>& WeakMember : Members)
	{
		UTempoTiledSceneCaptureComponent* Member = WeakMember.Get();
		if (!Member || !Member->IsActive())
		{
			continue;
		}
		Member->EnforceGroupRate(RateHz);
		Member->MaybeMarkPendingCapture();
	}
}

bool UTempoSensorRenderGroup::RefreshLayouts(int32 Stage, const TArray<TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>>& Descs)
{
	if (StageLayouts.Num() <= Stage)
	{
		StageLayouts.SetNum(Stage + 1);
	}
	TArray<FFamilyLayout>& Layouts = StageLayouts[Stage];

	// Wanted: one family per distinct signature, in order of first appearance, holding every member
	// with a valid desc. Layout is computed over all members, not just the ones capturing this
	// frame, so a member's rect in the atlas stays put while other members come and go.
	TArray<FFamilyLayout> Wanted;
	for (const TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>& Pair : Descs)
	{
		const FTempoSensorFamilySignature Signature = Pair.Value.MakeSignature(Pair.Key);
		FFamilyLayout* Family = Wanted.FindByPredicate([&Signature](const FFamilyLayout& Layout)
		{
			return Layout.Signature == Signature;
		});
		if (!Family)
		{
			Family = &Wanted.AddDefaulted_GetRef();
			Family->Signature = Signature;
		}
		FBlockPlacement& Placement = Family->Blocks.AddDefaulted_GetRef();
		Placement.Member = Pair.Key;
		Placement.BlockRT = Pair.Value.BlockRT;
		Placement.Size = Pair.Value.GetBlockSize();
	}

	// Unchanged if every family still has the same members, in order, with the same block RTs and
	// sizes. A block RT swap (the member re-created its render target) counts as a change even at
	// the same size: the copy targets the RT captured at layout time.
	bool bChanged = Wanted.Num() != Layouts.Num();
	for (int32 FamilyIndex = 0; !bChanged && FamilyIndex < Wanted.Num(); ++FamilyIndex)
	{
		const FFamilyLayout& Old = Layouts[FamilyIndex];
		const FFamilyLayout& New = Wanted[FamilyIndex];
		bChanged = Old.Signature != New.Signature || Old.Blocks.Num() != New.Blocks.Num() || !Old.Atlas;
		for (int32 BlockIndex = 0; !bChanged && BlockIndex < New.Blocks.Num(); ++BlockIndex)
		{
			bChanged = Old.Blocks[BlockIndex].Member != New.Blocks[BlockIndex].Member
				|| Old.Blocks[BlockIndex].BlockRT != New.Blocks[BlockIndex].BlockRT
				|| Old.Blocks[BlockIndex].Size != New.Blocks[BlockIndex].Size;
		}
	}
	if (!bChanged)
	{
		return false;
	}

	for (FFamilyLayout& Old : Layouts)
	{
		RetireAtlas(Old.Atlas);
	}
	Layouts = MoveTemp(Wanted);

	const int32 MaxDimension = static_cast<int32>(GetMax2DTextureDimension());
	for (FFamilyLayout& Family : Layouts)
	{
		TArray<TempoSensorRenderGroupLayout::FBlock> Blocks;
		Blocks.Reserve(Family.Blocks.Num());
		for (const FBlockPlacement& Placement : Family.Blocks)
		{
			Blocks.Add({ Placement.Size, FIntPoint(-1, -1) });
		}
		Family.AtlasSize = TempoSensorRenderGroupLayout::Pack(Blocks, MaxDimension);
		for (int32 BlockIndex = 0; BlockIndex < Blocks.Num(); ++BlockIndex)
		{
			Family.Blocks[BlockIndex].Offset = Blocks[BlockIndex].Offset;
		}

		if (Family.AtlasSize.X <= 0 || Family.AtlasSize.Y <= 0)
		{
			continue;
		}

		// The atlas mirrors the members' block render targets: the family renders into it exactly
		// as a member's tiles would render into their own atlas, and the block copy back requires
		// matching formats. The members' RTs agree on all of these by construction — they are part
		// of the family signature.
		UTextureRenderTarget2D* Template = Family.Blocks[0].BlockRT.Get();
		check(Template);
		UTextureRenderTarget2D* Atlas = NewObject<UTextureRenderTarget2D>(this);
		Atlas->TargetGamma = Template->TargetGamma;
		Atlas->bGPUSharedFlag = true;
		Atlas->RenderTargetFormat = Template->RenderTargetFormat;
		Atlas->Filter = Template->Filter;
		Atlas->SRGB = Template->SRGB;
		Atlas->InitCustomFormat(Family.AtlasSize.X, Family.AtlasSize.Y, Template->GetFormat(), Template->bForceLinearGamma);
		Family.Atlas = Atlas;

		FString MemberNames;
		for (const FBlockPlacement& Placement : Family.Blocks)
		{
			if (const UTempoTiledSceneCaptureComponent* Member = Placement.Member.Get())
			{
				MemberNames += FString::Printf(TEXT("%s%s(%dx%d@%d,%d)"), MemberNames.IsEmpty() ? TEXT("") : TEXT(", "),
					*Member->GetName(), Placement.Size.X, Placement.Size.Y, Placement.Offset.X, Placement.Offset.Y);
			}
		}
		UE_LOG(LogTempoSensors, Display, TEXT("Render group %s @ %g Hz: %s stage %d atlas %dx%d for %s"),
			Owner.IsValid() ? *Owner->GetName() : TEXT("<none>"), RateHz,
			SensorClass ? *SensorClass->GetName() : TEXT("<none>"), Stage, Family.AtlasSize.X, Family.AtlasSize.Y, *MemberNames);

		// A member's rect moved (or the member is new), so its tiles' temporal history no longer
		// lines up with what they will render. Cut once rather than let it resolve over frames.
		for (const FBlockPlacement& Placement : Family.Blocks)
		{
			if (UTempoTiledSceneCaptureComponent* Member = Placement.Member.Get())
			{
				Member->OnGroupLayoutChanged(Stage);
			}
		}
	}

	return true;
}

void UTempoSensorRenderGroup::RetireAtlas(UTextureRenderTarget2D* Atlas)
{
	if (!Atlas || RetiredAtlases.Contains(Atlas))
	{
		return;
	}
	RetiredAtlases.Add(Atlas);
	RetiredAtlasFrames.Add(GFrameCounter);
}

void UTempoSensorRenderGroup::TrimRetiredAtlases()
{
	// The render thread runs at most a few frames behind the game thread, so a command that names
	// an atlas retired this many frames ago has long since executed.
	constexpr uint64 RetainFrames = 8;
	while (!RetiredAtlasFrames.IsEmpty() && RetiredAtlasFrames[0] + RetainFrames < GFrameCounter)
	{
		RetiredAtlases.RemoveAt(0);
		RetiredAtlasFrames.RemoveAt(0);
	}
}

void UTempoSensorRenderGroup::EnqueueBlockCopy(FTextureRenderTargetResource* Atlas, FTextureRenderTargetResource* Block, const FIntPoint& Offset, const FIntPoint& Size)
{
	ENQUEUE_RENDER_COMMAND(TempoSensorRenderGroupBlockCopy)(
		[Atlas, Block, Offset, Size](FRHICommandListImmediate& RHICmdList)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(TempoSensorRenderGroupBlockCopy);
			FRHITexture* Source = Atlas->GetRenderTargetTexture();
			FRHITexture* Dest = Block->GetRenderTargetTexture();
			if (!Source || !Dest)
			{
				return;
			}
			{
				const FRHITransitionInfo Transitions[] = {
					FRHITransitionInfo(Source, ERHIAccess::Unknown, ERHIAccess::CopySrc),
					FRHITransitionInfo(Dest, ERHIAccess::Unknown, ERHIAccess::CopyDest),
				};
				RHICmdList.Transition(Transitions);
			}
			FRHICopyTextureInfo CopyInfo;
			CopyInfo.SourcePosition = FIntVector(Offset.X, Offset.Y, 0);
			CopyInfo.DestPosition = FIntVector::ZeroValue;
			CopyInfo.Size = FIntVector(Size.X, Size.Y, 1);
			RHICmdList.CopyTexture(Source, Dest, CopyInfo);
			{
				// Everything downstream samples the block (stitch and merge materials, the staging
				// copy transitions it itself) and the next family render registers the atlas as an
				// external texture in its own graph.
				const FRHITransitionInfo Transitions[] = {
					FRHITransitionInfo(Source, ERHIAccess::CopySrc, ERHIAccess::SRVMask),
					FRHITransitionInfo(Dest, ERHIAccess::CopyDest, ERHIAccess::SRVMask),
				};
				RHICmdList.Transition(Transitions);
			}
		});
}

void UTempoSensorRenderGroup::ExecutePendingCaptures()
{
	// Every member forwards its ExecutePendingCapture here; the first one this frame does the work.
	if (LastExecutedFrame == GFrameCounter)
	{
		return;
	}
	LastExecutedFrame = GFrameCounter;

	PruneMembers();
	TrimRetiredAtlases();

	// Members still in the capture: a member whose stage fails to render drops out of the later
	// stages, as it would if it were rendering on its own.
	TArray<UTempoTiledSceneCaptureComponent*, TInlineAllocator<8>> Active;
	for (const TWeakObjectPtr<UTempoTiledSceneCaptureComponent>& WeakMember : Members)
	{
		UTempoTiledSceneCaptureComponent* Member = WeakMember.Get();
		if (Member && Member->ConsumePendingCapture())
		{
			Active.Add(Member);
		}
	}
	if (Active.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	FSceneInterface* Scene = World ? World->Scene : nullptr;
	if (!Scene)
	{
		return;
	}

	int32 NumStages = 0;
	for (const UTempoTiledSceneCaptureComponent* Member : Active)
	{
		NumStages = FMath::Max(NumStages, Member->GetNumRenderStages());
	}

	// Idempotent per frame; every family render below needs it ahead of it.
	UTempoSceneCaptureComponent2D::EnsureRayTracingReadbackBuffersExpanded(Scene);

	const FString GroupName = FString::Printf(TEXT("TempoGroup %s %s"),
		Owner.IsValid() ? *Owner->GetActorNameOrLabel() : TEXT("<none>"),
		SensorClass ? *SensorClass->GetName() : TEXT("<none>"));

	for (int32 Stage = 0; Stage < NumStages; ++Stage)
	{
		// Descs for every member with this stage, capturing or not, so the layout stays stable
		// across frames.
		TArray<TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>> Descs;
		for (const TWeakObjectPtr<UTempoTiledSceneCaptureComponent>& WeakMember : Members)
		{
			UTempoTiledSceneCaptureComponent* Member = WeakMember.Get();
			if (!Member || Member->GetNumRenderStages() <= Stage)
			{
				continue;
			}
			FTempoSensorGroupRenderDesc Desc;
			if (Member->GetRenderStageDesc(Stage, Desc) && Desc.BlockRT && Desc.BlockRT->GameThread_GetRenderTargetResource())
			{
				Descs.Emplace(Member, MoveTemp(Desc));
			}
		}
		RefreshLayouts(Stage, Descs);

		TSet<UTempoTiledSceneCaptureComponent*> Rendered;
		TSet<UTempoTiledSceneCaptureComponent*> Dropped;
		for (FFamilyLayout& Family : StageLayouts[Stage])
		{
			if (!Family.Atlas)
			{
				continue;
			}
			FTextureRenderTargetResource* AtlasResource = Family.Atlas->GameThread_GetRenderTargetResource();
			if (!AtlasResource)
			{
				continue;
			}

			TArray<TempoMultiViewCapture::FViewSetup> Views;
			TArray<const FBlockPlacement*, TInlineAllocator<8>> RenderedBlocks;
			FString MemberNames;
			for (const FBlockPlacement& Placement : Family.Blocks)
			{
				UTempoTiledSceneCaptureComponent* Member = Placement.Member.Get();
				if (!Member || !Active.Contains(Member) || Member->GetNumRenderStages() <= Stage || Placement.Offset.X < 0)
				{
					continue;
				}
				TArray<TempoMultiViewCapture::FViewSetup> MemberViews;
				if (!Member->PrepareRenderStage(Stage, MemberViews) || MemberViews.IsEmpty())
				{
					Dropped.Add(Member);
					continue;
				}
				for (TempoMultiViewCapture::FViewSetup& View : MemberViews)
				{
					View.ViewRect += Placement.Offset;
				}
				Views.Append(MoveTemp(MemberViews));
				RenderedBlocks.Add(&Placement);
				Rendered.Add(Member);
				MemberNames += FString::Printf(TEXT("%s%s"), MemberNames.IsEmpty() ? TEXT("") : TEXT(","), *Member->GetName());
			}
			if (RenderedBlocks.IsEmpty())
			{
				continue;
			}

			// The first rendered member supplies the family-level settings. Every member of the family
			// agrees on them (that is what the signature guarantees), so the choice is immaterial.
			UTempoTiledSceneCaptureComponent* Primary = RenderedBlocks[0]->Member.Get();
			const TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>* PrimaryPair = Descs.FindByPredicate(
				[Primary](const TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>& Pair)
				{
					return Pair.Key == Primary;
				});
			// Layouts were just rebuilt from Descs, so every placed member has one.
			if (!ensure(PrimaryPair))
			{
				continue;
			}
			const FTempoSensorGroupRenderDesc& PrimaryDesc = PrimaryPair->Value;

			// Re-armed before every render: the engine clears it at the end of each renderer's frame.
			UTempoSceneCaptureComponent2D::PinRayTracingSceneUsedThisFrame(Scene);

			TempoMultiViewCapture::RenderTiles(
				Scene, Primary, Family.Atlas, Views, PrimaryDesc.CaptureSource, PrimaryDesc.ResolutionFraction, &PrimaryDesc.ShowFlags,
				FString::Printf(TEXT("%s stage %d [%s]"), *GroupName, Stage, *MemberNames));

			for (const FBlockPlacement* Placement : RenderedBlocks)
			{
				UTempoTiledSceneCaptureComponent* Member = Placement->Member.Get();
				UTextureRenderTarget2D* BlockRT = Placement->BlockRT.Get();
				FTextureRenderTargetResource* BlockResource = BlockRT ? BlockRT->GameThread_GetRenderTargetResource() : nullptr;
				if (BlockResource)
				{
					EnqueueBlockCopy(AtlasResource, BlockResource, Placement->Offset, Placement->Size);
				}
				Member->FinishRenderStage(Stage);
			}
		}

		// Anything still active with this stage that found no place in an atlas — no valid desc this
		// frame, or a block too large to pack — renders the stage on its own, as it would ungrouped.
		for (int32 Index = Active.Num() - 1; Index >= 0; --Index)
		{
			UTempoTiledSceneCaptureComponent* Member = Active[Index];
			if (Member->GetNumRenderStages() <= Stage || Rendered.Contains(Member))
			{
				continue;
			}
			if (Dropped.Contains(Member))
			{
				Active.RemoveAt(Index);
				continue;
			}
			const bool bHasDesc = Descs.ContainsByPredicate(
				[Member](const TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>& Pair)
				{
					return Pair.Key == Member;
				});
			if (bHasDesc && !WarnedUnplaced.Contains(Member))
			{
				WarnedUnplaced.Add(Member);
				UE_LOG(LogTempoSensors, Warning, TEXT("Sensor %s does not fit in its render group's atlas; rendering it on its own."), *Member->GetName());
			}
			if (!Member->RenderStageStandalone(Stage))
			{
				Active.RemoveAt(Index);
			}
		}
	}
}

UTempoSensorRenderGroup* UTempoSensorRenderGroupSubsystem::JoinGroup(UTempoTiledSceneCaptureComponent* Sensor)
{
	check(Sensor);
	AActor* Owner = Sensor->GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	const float RateHz = Sensor->GetRate();
	const UClass* SensorClass = Sensor->GetClass();

	UTempoSensorRenderGroup* Group = nullptr;
	for (const TObjectPtr<UTempoSensorRenderGroup>& Candidate : Groups)
	{
		if (Candidate && Candidate->Matches(Owner, RateHz, SensorClass))
		{
			Group = Candidate;
			break;
		}
	}
	if (!Group)
	{
		Group = NewObject<UTempoSensorRenderGroup>(this);
		Group->Initialize(Owner, RateHz, SensorClass);
		Groups.Add(Group);
	}
	Group->AddMember(Sensor);
	return Group;
}

void UTempoSensorRenderGroupSubsystem::LeaveGroup(UTempoTiledSceneCaptureComponent* Sensor, UTempoSensorRenderGroup* Group)
{
	if (!Group)
	{
		return;
	}
	Group->RemoveMember(Sensor);
	if (Group->NumMembers() == 0)
	{
		Groups.Remove(Group);
		Group->Shutdown();
		Group->MarkAsGarbage();
	}
}

void UTempoSensorRenderGroupSubsystem::Deinitialize()
{
	for (const TObjectPtr<UTempoSensorRenderGroup>& Group : Groups)
	{
		if (Group)
		{
			Group->Shutdown();
			Group->MarkAsGarbage();
		}
	}
	Groups.Empty();
	Super::Deinitialize();
}
