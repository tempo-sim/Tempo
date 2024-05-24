// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"

#include "TempoSceneCaptureComponent2D.h"

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"

#include "TempoColorCamera.generated.h"

struct FColorImageRequest
{
	TempoCamera::ColorImageRequest Request;
	TResponseDelegate<TempoCamera::ColorImage> ResponseContinuation;
};

struct FLabelImageRequest
{
	TempoCamera::LabelImageRequest Request;
	TResponseDelegate<TempoCamera::LabelImage> ResponseContinuation;
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoColorCamera : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoColorCamera();

	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	void RequestMeasurement(const TempoCamera::ColorImageRequest& Request, const TResponseDelegate<TempoCamera::ColorImage>& ResponseContinuation);

	void RequestMeasurement(const TempoCamera::LabelImageRequest& Request, const TResponseDelegate<TempoCamera::LabelImage>& ResponseContinuation);

	virtual void FlushMeasurementResponses() override;

	virtual bool HasPendingRenderingCommands() override { return TextureReadQueue.HasOutstandingTextureReads(); }

protected:
	virtual bool HasPendingRequests() const override { return !PendingColorImageRequests.IsEmpty() || !PendingLabelImageRequests.IsEmpty(); }
	
private:	
	TArray<FColorImageRequest> PendingColorImageRequests;
	TArray<FLabelImageRequest> PendingLabelImageRequests;

	TTextureReadQueue<FColor> TextureReadQueue;
};
