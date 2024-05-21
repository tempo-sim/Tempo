// Copyright Tempo Simulation, LLC. All Rights Reserved

#pragma once

#include "TempoCamera/Camera.pb.h"

#include "TempoSceneCaptureComponent2D.h"

#include "TempoScriptingServer.h"

#include "CoreMinimal.h"

#include "TempoDepthCamera.generated.h"

struct FDepthImageRequest
{
	TempoCamera::DepthImageRequest Request;
	TResponseDelegate<TempoCamera::DepthImage> ResponseContinuation;
};

UCLASS(Blueprintable, BlueprintType, ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class TEMPOCAMERA_API UTempoDepthCamera : public UTempoSceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	UTempoDepthCamera();

	virtual void UpdateSceneCaptureContents(FSceneInterface* Scene) override;

	void RequestMeasurement(const TempoCamera::DepthImageRequest& Request, const TResponseDelegate<TempoCamera::DepthImage>& ResponseContinuation);

	virtual void FlushMeasurementResponses() override;

	virtual bool HasPendingRenderingCommands() override { return TextureReadQueue.HasOutstandingTextureReads(); }

protected:
	virtual bool HasPendingRequests() const override { return !PendingDepthImageRequests.IsEmpty(); }
	
private:
	TArray<FDepthImageRequest> PendingDepthImageRequests;

	TTextureReadQueue<FLinearColor> TextureReadQueue;
};
