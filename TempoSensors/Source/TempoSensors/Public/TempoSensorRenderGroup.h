// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoSubsystems.h"

#include "CoreMinimal.h"
#include "Engine/Scene.h"
#include "ShowFlags.h"
#include "UObject/Object.h"

#include "TempoSensorRenderGroup.generated.h"

class FTextureRenderTargetResource;
class UTempoTiledSceneCaptureComponent;
class UTextureRenderTarget2D;

// The family-level render settings two sensors must agree on before their tiles can be views of
// one FSceneViewFamily. Everything else the engine treats per view (hide/show lists, view owner,
// post-process, view state, projection) and is free to differ.
struct TEMPOSENSORS_API FTempoSensorFamilySignature
{
	const UClass* SensorClass = nullptr;
	ESceneCaptureSource CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	EPixelFormat PixelFormat = EPixelFormat::PF_Unknown;
	float TargetGamma = 0.0f;
	bool bForceLinearGamma = false;
	bool bSRGB = false;
	float ResolutionFraction = 1.0f;
	// FEngineShowFlags has no equality operator; its string form is canonical.
	FString ShowFlags;
	bool bUseRayTracingIfEnabled = false;
	ESceneCaptureCompositeMode CompositeMode = ESceneCaptureCompositeMode::SCCM_Overwrite;

	bool operator==(const FTempoSensorFamilySignature& Other) const;
	bool operator!=(const FTempoSensorFamilySignature& Other) const { return !(*this == Other); }
};

// What a tiled sensor tells its render group about the block it renders into: the sensor-owned
// render target the block is copied back into (which also fixes the block size and pixel format),
// plus the family-level settings its tiles must be rendered with.
struct TEMPOSENSORS_API FTempoSensorGroupRenderDesc
{
	UTextureRenderTarget2D* BlockRT = nullptr;
	ESceneCaptureSource CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	float ResolutionFraction = 1.0f;
	FEngineShowFlags ShowFlags = FEngineShowFlags(ESFIM_Game);
	// False for a stage whose views discard their scene content (the camera's proxy tonemap render),
	// so the family builds no ray tracing scene. Sensors still need bUseRayTracingIfEnabled set.
	bool bAllowRayTracing = true;

	FIntPoint GetBlockSize() const;
	FTempoSensorFamilySignature MakeSignature(const UTempoTiledSceneCaptureComponent* Sensor) const;
};

namespace TempoSensorRenderGroupLayout
{
	struct FBlock
	{
		FIntPoint Size = FIntPoint::ZeroValue;
		// Filled by Pack. (-1, -1) for a block that could not be placed within MaxDimension.
		FIntPoint Offset = FIntPoint(-1, -1);
	};

	// Shelf-packs Blocks into one atlas no wider or taller than MaxDimension and returns the atlas
	// size. Deterministic for a given input order. Pure; unit-tested in TempoSensorRenderGroupLayoutTest.
	TEMPOSENSORS_API FIntPoint Pack(TArrayView<FBlock> Blocks, int32 MaxDimension);
}

// One render group: every tiled sensor on one actor that captures at one rate. Each frame the
// group renders the tiles of all its members that have a capture pending as the views of one
// FSceneViewFamily per family signature, into an atlas the group owns, then copies each member's
// block back into that member's own render target so the member's downstream passes and readback
// run unchanged. The group also owns the capture timer, so its members capture on the same frames.
//
// A capture may take several render stages (a camera on the multi-tile path renders its tiles,
// then its proxy tonemap view). The group runs stage by stage: every member's stage N is rendered
// and finished before any member's stage N+1, each stage with its own atlases and layouts.
//
// Rates are fixed for the life of a group: a member whose RateHz changes while grouped has the
// change logged and reverted. Group at a different rate by disabling and re-enabling grouping in
// the settings, or by setting the rate before the sensor activates.
UCLASS()
class TEMPOSENSORS_API UTempoSensorRenderGroup : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(AActor* InOwner, float InRateHz, const UClass* InSensorClass);

	bool Matches(const AActor* InOwner, float InRateHz, const UClass* InSensorClass) const;

	void AddMember(UTempoTiledSceneCaptureComponent* Sensor);
	void RemoveMember(UTempoTiledSceneCaptureComponent* Sensor);
	int32 NumMembers() const { return Members.Num(); }

	float GetRateHz() const { return RateHz; }

	// Render every member with a pending capture. Any member's ExecutePendingCapture forwards here;
	// the group runs at most once per frame, whichever member reaches it first.
	void ExecutePendingCaptures();

	// Stop the timer and drop members and atlases. Called by the subsystem before the group is
	// garbage; the timer must not outlive the group's usefulness by a GC cycle.
	void Shutdown();

	virtual void BeginDestroy() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

private:
	struct FBlockPlacement
	{
		TWeakObjectPtr<UTempoTiledSceneCaptureComponent> Member;
		TWeakObjectPtr<UTextureRenderTarget2D> BlockRT;
		FIntPoint Size = FIntPoint::ZeroValue;
		FIntPoint Offset = FIntPoint::ZeroValue;
	};

	struct FFamilyLayout
	{
		FTempoSensorFamilySignature Signature;
		TArray<FBlockPlacement> Blocks;
		FIntPoint AtlasSize = FIntPoint::ZeroValue;
		// Not a UPROPERTY: reported through AddReferencedObjects.
		TObjectPtr<UTextureRenderTarget2D> Atlas = nullptr;
	};

	void OnTimer();
	void StartTimer();
	void StopTimer();
	void PruneMembers();

	// Bring a stage's layouts in line with the members' current descs for that stage. Returns true
	// if anything changed.
	bool RefreshLayouts(int32 Stage, const TArray<TPair<UTempoTiledSceneCaptureComponent*, FTempoSensorGroupRenderDesc>>& Descs);
	void RetireAtlas(UTextureRenderTarget2D* Atlas);
	void TrimRetiredAtlases();

	static void EnqueueBlockCopy(FTextureRenderTargetResource* Atlas, FTextureRenderTargetResource* Block, const FIntPoint& Offset, const FIntPoint& Size);

	TWeakObjectPtr<AActor> Owner;
	float RateHz = 0.0f;
	const UClass* SensorClass = nullptr;

	TArray<TWeakObjectPtr<UTempoTiledSceneCaptureComponent>> Members;
	// Indexed by render stage.
	TArray<TArray<FFamilyLayout>> StageLayouts;

	// Members that did not fit in an atlas and were warned about, so the warning fires once.
	TSet<TWeakObjectPtr<UTempoTiledSceneCaptureComponent>> WarnedUnplaced;

	// Atlases replaced by a re-layout, kept alive while render commands from prior captures may
	// still reference them, and the frame each was retired on. Dropped once that frame is far
	// enough behind that no queued command can name them.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextureRenderTarget2D>> RetiredAtlases;
	TArray<uint64> RetiredAtlasFrames;

	FTimerHandle TimerHandle;
	uint64 LastExecutedFrame = 0;
};

// Owns the render groups of one game world and hands sensors their group by (owner actor, rate,
// sensor class).
UCLASS()
class TEMPOSENSORS_API UTempoSensorRenderGroupSubsystem : public UTempoGameWorldSubsystem
{
	GENERATED_BODY()

public:
	// Returns the group the sensor now belongs to, or null if it cannot be grouped (no owner).
	UTempoSensorRenderGroup* JoinGroup(UTempoTiledSceneCaptureComponent* Sensor);
	void LeaveGroup(UTempoTiledSceneCaptureComponent* Sensor, UTempoSensorRenderGroup* Group);

	virtual void Deinitialize() override;

private:
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTempoSensorRenderGroup>> Groups;
};
