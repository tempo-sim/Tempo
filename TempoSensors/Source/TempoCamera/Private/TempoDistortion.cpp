// Copyright Tempo Simulation, LLC. All Rights Reserved

#include "TempoDistortion.h"

#include "Components/SceneCaptureComponent2D.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "OpenCVLensDistortionBlueprintLibrary.h"
#include "TextureResource.h"

// Helper: Pure Radial Distortion (Forward Model)
static void DistortPointInternal(double x, double y, 
                                 double k1, double k2, double k3, 
                                 double& out_x, double& out_y)
{
    double r2 = x*x + y*y;
    double r4 = r2*r2;
    double r6 = r4*r2;

    // The Brown-Conrady Radial Factor
    double cDist = 1.0 + k1*r2 + k2*r4 + k3*r6;
    
    out_x = x * cDist;
    out_y = y * cDist;
}

ATempoDistortion::ATempoDistortion()
{
    PrimaryActorTick.bCanEverTick = true;
    
    LensParameters.K1 = 0.0f;
    LensParameters.K2 = 0.0f;
    LensParameters.K3 = 0.0f;
    LensParameters.P1 = 0.0f;
    LensParameters.P2 = 0.0f;
    
    LensParameters.F = FVector2D(0.5f, 0.5f); 
    LensParameters.C = FVector2D(0.5f, 0.5f);
    LensParameters.bUseFisheyeModel = false;
    ManualResolution = FIntPoint(1920, 1080);
}

void ATempoDistortion::BeginPlay()
{
    Super::BeginPlay();
    UpdateDistortionMap();
}

void ATempoDistortion::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    if (bUpdateEveryFrame)
    {
        UpdateDistortionMap();
    }
}

UTexture2D* ATempoDistortion::GenerateTrueDistortionMap(const FIntPoint& InSize)
{
    // 1. Texture Format
    UTexture2D* Result = UTexture2D::CreateTransient(InSize.X, InSize.Y, PF_G16R16F);
    Result->CompressionSettings = TC_HDR;
    Result->Filter = TF_Bilinear;
    Result->SRGB = 0;
    Result->AddressX = TA_Clamp;
    Result->AddressY = TA_Clamp;

    FTexture2DMipMap& Mip = Result->GetPlatformData()->Mips[0];
    uint16* MipData = reinterpret_cast<uint16*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
    
    // 2. Base Focal Length (Source Texture Space)
    double fx_base = LensParameters.F.X * InSize.X;
    double fy_base = fx_base; 
    
    double cx = LensParameters.C.X * InSize.X;
    double cy = LensParameters.C.Y * InSize.Y;

    double k1 = LensParameters.K1;
    double k2 = LensParameters.K2;
    double k3 = LensParameters.K3;

    // 3. Frustum Boundary Management
    double CornerU = (double)InSize.X; 
    double CornerV = 0.0; 
    
    double x_corner_norm = (CornerU - cx) / fx_base;
    double y_corner_norm = (CornerV - cy) / fy_base;

    double r2_corner = x_corner_norm*x_corner_norm + y_corner_norm*y_corner_norm;
    double r4_corner = r2_corner * r2_corner;
    double r6_corner = r4_corner * r2_corner;

    double ScaleFactor = 1.0 + k1*r2_corner + k2*r4_corner + k3*r6_corner;
	
    // Pincushion (ScaleFactor > 1.0): Keep it to "Zoom Out" and prevent cropping.
    // Barrel (ScaleFactor < 1.0): Force 1.0 to preserve black borders/fisheye look.
    if (ScaleFactor < 1.0) 
    {
        ScaleFactor = 1.0;
    }

    // 4. Adjusted Focal Length (Screen/Target Space)
    double fx = fx_base / ScaleFactor;
    double fy = fy_base / ScaleFactor;

    // 5. Inverse Solver (Robust)
    const int max_iterations = 10; 
    const double epsilon = 1e-6;

    for (int v = 0; v < InSize.Y; ++v)
    {
        uint16* Row = &MipData[v * InSize.X * 2];
        
        for (int u = 0; u < InSize.X; ++u)
        {
            // Use ADJUSTED fx to define the Screen View
            double x_target = (u - cx) / fx;
            double y_target = (v - cy) / fy;

            double x = x_target;
            double y = y_target;
            
            // Solve for Source Position
            for (int j = 0; j < max_iterations; ++j)
            {
                double r2 = x*x + y*y;
                double r4 = r2*r2;
                double r6 = r4*r2;
                
                double x_dist, y_dist;
                DistortPointInternal(x, y, k1, k2, k3, x_dist, y_dist);

                double err_x = x_target - x_dist;
                double err_y = y_target - y_dist;
                
                if ((err_x*err_x + err_y*err_y) < epsilon)
                    break;
                
                double derivative = 1.0 + 3.0*k1*r2 + 5.0*k2*r4 + 7.0*k3*r6;
            	
                // If the slope gets too flat (near singularity), clamp it.
                if (derivative < 0.1) derivative = 0.1;
            	
                double step_x = err_x / derivative;
                double step_y = err_y / derivative;
                double max_step = 0.1 * InSize.X; 
                step_x = FMath::Clamp(step_x, -max_step, max_step);
                step_y = FMath::Clamp(step_y, -max_step, max_step);

                x += step_x;
                y += step_y;
            }
        	
            float final_u = (float)(x * fx_base + cx) / (float)InSize.X;
            float final_v = (float)(y * fy_base + cy) / (float)InSize.Y;
            
            Row[u * 2 + 0] = FFloat16(final_u).Encoded;
            Row[u * 2 + 1] = FFloat16(final_v).Encoded;
        }
    }
       
    Mip.BulkData.Unlock();
    
#ifdef UpdateResource
#undef UpdateResource
#endif
    
    Result->UpdateResource();
    return Result;
}

void ATempoDistortion::UpdateDistortionMap()
{
	if (!OutputRenderTarget) return;
	
	FIntPoint TargetSize = ManualResolution;
	if (bMatchViewportResolution && GetWorld() && GetWorld()->GetGameViewport())
	{
		FVector2D ViewportSize;
		GetWorld()->GetGameViewport()->GetViewportSize(ViewportSize);
        
		if (ViewportSize.X > 0 && ViewportSize.Y > 0)
		{
			TargetSize = FIntPoint(ViewportSize.X, ViewportSize.Y);
		}
	}
	
	if (TargetSceneCaptureActor)
	{
		// Extract the component from the selected Actor
		USceneCaptureComponent2D* CaptureComp = TargetSceneCaptureActor->GetCaptureComponent2D();
        
		if (CaptureComp && CaptureComp->ProjectionType == ECameraProjectionMode::Perspective)
		{
			float H_FOV_Degrees = CaptureComp->FOVAngle;
            
			H_FOV_Degrees = FMath::Clamp(H_FOV_Degrees, 1.0f, 179.0f);
			float HalfFOV_Radians = FMath::DegreesToRadians(H_FOV_Degrees / 2.0f);
			float NewFocalLength = 0.5f / FMath::Tan(HalfFOV_Radians);

			LensParameters.F = FVector2D(NewFocalLength, NewFocalLength);
		}
	}

	if (OutputRenderTarget->SizeX != TargetSize.X || OutputRenderTarget->SizeY != TargetSize.Y)
	{
		OutputRenderTarget->ResizeTarget(TargetSize.X, TargetSize.Y);
	}
	
	UTexture2D* DisplacementMap = GenerateTrueDistortionMap(TargetSize);
    
	if (DisplacementMap)
	{
		FOpenCVLensDistortionParameters::DrawDisplacementMapToRenderTarget(
		   GetWorld(), 
		   OutputRenderTarget, 
		   DisplacementMap
		);
	}
}